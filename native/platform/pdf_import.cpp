#include "pdf_import.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Data.Pdf.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

// Every platform now shells out for something: the presentation converters are
// separate applications on all three.
#include "core/subprocess.hpp"
#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

namespace deckboy::platform {
namespace {

std::string lowerExtension(const fs::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return ext;
}

#ifdef _WIN32
// The width of a PNG, straight out of its header. Big-endian at a fixed offset;
// no decoder needed and none of the pixels are read.
int pngWidth(const fs::path& path) {
  std::FILE* file = nullptr;
  if (_wfopen_s(&file, path.wstring().c_str(), L"rb") != 0 || !file) {
    return 0;
  }
  unsigned char header[24] = {};
  const std::size_t got = std::fread(header, 1, sizeof(header), file);
  std::fclose(file);
  if (got < sizeof(header)) {
    return 0;
  }
  return (header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19];
}
#endif

// Zero-padded, so a hundred-page deck sorts correctly in a folder listing and
// in any tool the operator opens it with. "page9" before "page10" is the kind
// of thing that only shows up on the day it matters.
std::string pageFileName(int index) {
  char name[32];
  std::snprintf(name, sizeof(name), "page%04d.png", index + 1);
  return name;
}

}  // namespace

bool isPdfDocumentPath(const fs::path& path) {
  return lowerExtension(path) == ".pdf";
}

bool isPresentationDocumentPath(const fs::path& path) {
  const std::string ext = lowerExtension(path);
  return ext == ".pptx" || ext == ".ppt" || ext == ".key" ||
         ext == ".odp" || ext == ".pps" || ext == ".ppsx";
}
// ---------------------------------------------------------------------------
// Presentations -> PDF, by asking whatever owns the format
// ---------------------------------------------------------------------------
//
// FIDELITY IS THE WHOLE POINT, so the choice of converter is not arbitrary.
//
// PowerPoint exports its own format exactly: it embeds the fonts it used, keeps
// every box where the author put it, and with PRINT intent it does not
// downsample the images on the way out. LibreOffice reads .pptx well but
// substitutes fonts it does not have, and reflows text to fit when it does --
// which on a slide means a line breaking in a new place, or a heading landing
// over an image. That is the failure this ordering exists to avoid, and when
// LibreOffice is all there is, the caller says so rather than quietly shipping
// a deck that is subtly not the one the operator built.
//
// What survives: layout, fonts, images at full resolution, slide order.
// What does not, and cannot through any PDF: builds, transitions, and media
// embedded in a slide. Those are properties of a running presentation, not of
// a page.

namespace {

// Where a converted PDF goes: under the state dir with the pages, never next
// to the operator's document.
fs::path convertedPdfPath(const fs::path& source, const fs::path& outputDir) {
  return outputDir / (source.stem().string() + ".pdf");
}

// Run a helper and say only whether it worked. Output is captured rather than
// inherited so a converter cannot print over the app's own console.
bool runQuietly(const std::vector<std::string>& args, std::string& output) {
  auto text = readAllText(args);
  if (!text.has_value()) {
    output.clear();
    return false;
  }
  output = *text;
  return true;
}

// LibreOffice, wherever this platform keeps it. Empty when it is not installed.
fs::path findLibreOffice() {
  const char* candidates[] = {
#ifdef _WIN32
    "C:\\Program Files\\LibreOffice\\program\\soffice.exe",
    "C:\\Program Files (x86)\\LibreOffice\\program\\soffice.exe",
#elif defined(__APPLE__)
    "/Applications/LibreOffice.app/Contents/MacOS/soffice",
    "/opt/homebrew/bin/soffice",
    "/usr/local/bin/soffice",
#else
    "/usr/bin/soffice", "/usr/bin/libreoffice",
    "/usr/local/bin/soffice", "/snap/bin/libreoffice",
#endif
  };
  std::error_code ec;
  for (const char* candidate : candidates) {
    if (fs::exists(candidate, ec)) {
      return fs::path(candidate);
    }
  }
  return {};
}

// LibreOffice refuses to run headless while another copy of it holds the
// default profile, which on an operator's own laptop it very often does. A
// private profile makes the conversion independent of whatever they have open.
bool convertWithLibreOffice(const fs::path& soffice, const fs::path& source,
                            const fs::path& outputDir, std::string& error) {
  const fs::path profile = outputDir / "_loprofile";
  std::error_code ec;
  fs::create_directories(profile, ec);
  const std::string profileUrl =
    "-env:UserInstallation=file:///" + profile.generic_string();
  std::string output;
  const bool ran = runQuietly({
    soffice.string(), profileUrl, "--headless", "--norestore",
    "--convert-to", "pdf:impress_pdf_Export",
    "--outdir", outputDir.string(), source.string()}, output);
  if (!ran) {
    error = "LibreOffice could not be run";
    return false;
  }
  if (!fs::exists(convertedPdfPath(source, outputDir), ec)) {
    error = "LibreOffice produced no PDF";
    return false;
  }
  return true;
}

}  // namespace

#ifdef _WIN32
namespace {

// PowerPoint, driven through COM by a script rather than from here.
//
// Written to a file instead of passed with -Command because the paths carry
// spaces and quotes, and a quoting mistake in a command line is a silent
// misfire; a file is also something a person can read when it goes wrong.
//
// ExportAsFixedFormat with PRINT intent, not SaveAs: SaveAs takes the screen
// preset, which downsamples the images on the way into the PDF. Print keeps
// them. BitmapMissingFonts stays at its default of true, so a font that cannot
// be embedded is drawn as pixels rather than swapped for another face -- the
// appearance survives either way, which is what matters when the next step
// turns the page into an image regardless.
bool convertWithPowerPoint(const fs::path& source, const fs::path& outputDir,
                           std::string& error) {
  const fs::path target = convertedPdfPath(source, outputDir);
  const fs::path script = outputDir / "_export.ps1";
  {
    std::ofstream out(script);
    if (!out) {
      error = "could not write the export script";
      return false;
    }
    out << "param([string]$Source,[string]$Target)\n"
        << "$ErrorActionPreference='Stop'\n"
        << "$app=$null; $pres=$null\n"
        << "try {\n"
        << "  $app = New-Object -ComObject PowerPoint.Application\n"
        // ReadOnly, not Untitled, and no window: the operator's own copy of
        // PowerPoint may be open on this very file during a show.
        << "  $pres = $app.Presentations.Open($Source, -1, 0, 0)\n"
        << "  try {\n"
        // Path, PDF, Print intent, no frame, handout order, slides only,
        // skip hidden slides.
        << "    $pres.ExportAsFixedFormat($Target, 2, 2, 0, 1, 1, 0)\n"
        << "  } catch {\n"
        // Older builds do not take that overload. The screen preset is worse
        // but it is still a PDF, and a lesser export beats refusing the file.
        << "    $pres.SaveAs($Target, 32)\n"
        << "  }\n"
        << "} catch {\n"
        << "  Write-Error $_.Exception.Message; exit 1\n"
        << "} finally {\n"
        << "  if ($pres) { try { $pres.Close() } catch {} }\n"
        << "  if ($app) { try { $app.Quit() } catch {} }\n"
        << "}\n";
  }
  std::error_code ec;
  fs::remove(target, ec);   // SaveAs prompts if the file is already there

  // BACKSLASHES, because PowerPoint will not take anything else. Handed
  // "C:/Users/.../deck.pdf" it answers "PowerPoint can't save ^0 to ^1" -- a
  // message with its placeholders still in it, which is what that fault looks
  // like from the outside. Deckboy's own paths carry either separator
  // depending on how they were built, so this cannot be left to chance.
  fs::path sourceNative = source;
  fs::path targetNative = target;
  sourceNative.make_preferred();
  targetNative.make_preferred();
  std::string output;
  runQuietly({"powershell", "-NoProfile", "-NonInteractive",
              "-ExecutionPolicy", "Bypass", "-File", script.string(),
              "-Source", sourceNative.string(),
              "-Target", targetNative.string()}, output);
  fs::remove(script, ec);
  if (!fs::exists(target, ec)) {
    // PowerPoint's own words are more use than ours: it is the thing that
    // knows the file could not be opened, or that the deck is protected.
    error = output.empty()
      ? std::string("PowerPoint could not export this file")
      : ("PowerPoint: " + output.substr(0, 200));
    return false;
  }
  return true;
}

// The registered path for the executable, which is how Windows itself finds
// it; a hard-coded Office16 path goes stale with every version.
bool havePowerPoint() {
  std::string output;
  runQuietly({"powershell", "-NoProfile", "-NonInteractive", "-Command",
              "(Get-ItemProperty 'HKLM:\\SOFTWARE\\Microsoft\\Windows\\"
              "CurrentVersion\\App Paths\\POWERPNT.EXE' "
              "-ErrorAction SilentlyContinue).'(default)'"}, output);
  return output.find("POWERPNT") != std::string::npos ||
         output.find("powerpnt") != std::string::npos;
}

}  // namespace
#endif

#ifdef __APPLE__
namespace {

// Keynote owns .key the way PowerPoint owns .pptx, and nothing else opens one
// faithfully. Driven by osascript; the first run raises the automation consent
// prompt, which is the operating system's to ask and not ours to route around.
bool convertWithKeynote(const fs::path& source, const fs::path& outputDir,
                        std::string& error) {
  const fs::path target = convertedPdfPath(source, outputDir);
  std::error_code ec;
  fs::remove(target, ec);
  const std::string quote(1, '"');
  const std::string script =
    "tell application " + quote + "Keynote" + quote + "\n"
    "  set d to open POSIX file " + quote + source.string() + quote + "\n"
    "  export d to POSIX file " + quote + target.string() + quote + " as PDF\n"
    "  close d saving no\n"
    "end tell\n";
  std::string output;
  runQuietly({"osascript", "-e", script}, output);
  if (!fs::exists(target, ec)) {
    error = output.empty()
      ? std::string("Keynote could not export this file (it may need "
                    "permission to be automated: System Settings > Privacy "
                    "& Security > Automation)")
      : ("Keynote: " + output.substr(0, 200));
    return false;
  }
  return true;
}

bool haveKeynote() {
  std::error_code ec;
  return fs::exists("/Applications/Keynote.app", ec);
}

}  // namespace
#endif

bool presentationConvertAvailable(std::string& whyNot) {
#ifdef _WIN32
  if (havePowerPoint() || !findLibreOffice().empty()) {
    whyNot.clear();
    return true;
  }
  whyNot = "no converter found: install PowerPoint or LibreOffice, "
           "or export the deck as a PDF yourself";
#elif defined(__APPLE__)
  if (haveKeynote() || !findLibreOffice().empty()) {
    whyNot.clear();
    return true;
  }
  whyNot = "no converter found: install Keynote or LibreOffice, "
           "or export the deck as a PDF yourself";
#else
  if (!findLibreOffice().empty()) {
    whyNot.clear();
    return true;
  }
  whyNot = "LibreOffice is not installed (apt install libreoffice-impress), "
           "or export the deck as a PDF yourself";
#endif
  return false;
}

PresentationConversion convertPresentationToPdf(const fs::path& source,
                                                const fs::path& outputDir) {
  PresentationConversion result;
  std::error_code ec;
  if (!fs::exists(source, ec)) {
    // A file that is listed but cannot be opened is usually a cloud
    // placeholder -- OneDrive, iCloud, Dropbox -- that has not been pulled
    // down. Saying so is more use than "not found".
    result.error = "cannot read the file: if it lives in OneDrive or iCloud, "
                   "open it once so it downloads, then import it";
    return result;
  }
  fs::create_directories(outputDir, ec);
  if (ec) {
    result.error = "could not create " + outputDir.string();
    return result;
  }
  const std::string ext = lowerExtension(source);
  std::string error;

#ifdef __APPLE__
  if (ext == ".key" && haveKeynote()) {
    if (convertWithKeynote(source, outputDir, error)) {
      result.pdfPath = convertedPdfPath(source, outputDir);
      result.converter = "Keynote";
      return result;
    }
  }
#endif
#ifdef _WIN32
  // The format's owner first: PowerPoint neither substitutes a font nor
  // reflows a line, which is the entire reason to prefer it.
  if (ext != ".key" && havePowerPoint()) {
    if (convertWithPowerPoint(source, outputDir, error)) {
      result.pdfPath = convertedPdfPath(source, outputDir);
      result.converter = "PowerPoint";
      return result;
    }
  }
#endif
  const fs::path soffice = findLibreOffice();
  if (!soffice.empty()) {
    if (convertWithLibreOffice(soffice, source, outputDir, error)) {
      result.pdfPath = convertedPdfPath(source, outputDir);
      // Named so the caller can warn: LibreOffice is a good reader and still
      // not the authority on someone else's format.
      result.converter = "LibreOffice";
      return result;
    }
  }
  if (error.empty()) {
    presentationConvertAvailable(error);
  }
  result.error = error;
  return result;
}


// ---------------------------------------------------------------------------
// Windows — Windows.Data.Pdf
// ---------------------------------------------------------------------------
#ifdef _WIN32

bool pdfRasterAvailable(std::string& whyNot) {
  whyNot.clear();
  return true;   // ships with the OS
}

PdfRasterResult rasterisePdf(const fs::path& pdfPath, const fs::path& outputDir,
                             int targetWidthPixels,
                             const std::function<void(int, int)>& onProgress) {
  std::error_code pathEc;
  PdfRasterResult result;
  std::error_code ec;
  fs::create_directories(outputDir, ec);
  if (ec) {
    result.error = "could not create " + outputDir.string();
    return result;
  }
  // This runs on a worker thread, so it needs its own apartment. Multi-threaded
  // rather than single: there is no message pump out here to service an STA.
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  try {
    // ABSOLUTE AND BACKSLASHED. GetFileFromPathAsync takes only a fully
    // qualified native path: a relative one, or one carrying the forward
    // slashes a path picks up when it has been through generic_string(), comes
    // back as 0x800700A1 (bad pathname) -- which reads like a corrupt PDF
    // rather than like a path this call would not accept.
    fs::path nativePdf = fs::absolute(pdfPath, pathEc);
    if (pathEc) {
      nativePdf = pdfPath;
    }
    nativePdf.make_preferred();
    auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
      winrt::hstring(nativePdf.wstring())).get();
    auto doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
    // Same rule for the folder, and it is the one that actually bit: the PDF
    // was named on the command line and so arrived native, while the output
    // directory had been built up with operator/ from a forward-slashed root.
    fs::path nativeOut = fs::absolute(outputDir, pathEc);
    if (pathEc) {
      nativeOut = outputDir;
    }
    nativeOut.make_preferred();
    auto folder = winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(
      winrt::hstring(nativeOut.wstring())).get();

    const uint32_t pageCount = doc.PageCount();
    if (pageCount == 0) {
      result.error = "the document has no pages";
      return result;
    }
    // MEASURE WHAT COMES OUT, then correct, rather than predicting it.
    //
    // Windows.Data.Pdf renders in DEVICE pixels: on a display at 140% every
    // page asked for at 3840 wide arrived at 5376, and the system DPI cannot
    // be read back reliably from a process that is not DPI-aware -- it answers
    // 96 and means it. So the first page is rendered, its width read from the
    // PNG header, and the request corrected by whatever factor the machine
    // actually applied. That fixes any systematic scaling, not only this one,
    // and it costs one extra render of one page.
    //
    // It matters because otherwise a deck imports at a different resolution
    // depending on the scaling of the monitor the operator happened to be
    // sitting at, which is invisible until it is a show.
    double widthCorrection = 1.0;
    for (int attempt = 0; attempt < 2; ++attempt) {
      for (uint32_t i = 0; i < pageCount; ++i) {
      if (onProgress) {
        onProgress(static_cast<int>(i), static_cast<int>(pageCount));
      }
      auto page = doc.GetPage(i);
      const auto size = page.Size();
      winrt::Windows::Data::Pdf::PdfPageRenderOptions options;
      // Rounded, not truncated: the correction is fractional, and truncating
      // the request landed the output a pixel short of the other platforms.
      const double askWidth = std::max(
        1.0, static_cast<double>(std::lround(targetWidthPixels * widthCorrection)));
      const double pageScale = size.Width > 0.0f ? askWidth / size.Width : 1.0;
      options.DestinationWidth(static_cast<uint32_t>(askWidth));
      options.DestinationHeight(
        static_cast<uint32_t>(std::max(1.0, size.Height * pageScale)));

      const std::string name = pageFileName(static_cast<int>(i));
      auto target = folder.CreateFileAsync(
        winrt::hstring(fs::path(name).wstring()),
        winrt::Windows::Storage::CreationCollisionOption::ReplaceExisting).get();
      auto stream = target.OpenAsync(
        winrt::Windows::Storage::FileAccessMode::ReadWrite).get();
      page.RenderToStreamAsync(stream, options).get();
      stream.Close();
      result.pagePaths.push_back((outputDir / name).string());

      // First page of the first pass: find out what the machine actually did.
      if (attempt == 0 && i == 0) {
        const int actual = pngWidth(outputDir / name);
        if (actual > 0 && std::abs(actual - targetWidthPixels) > 1) {
          widthCorrection =
            static_cast<double>(targetWidthPixels) / static_cast<double>(actual);
          result.pagePaths.clear();
          break;            // start again, now that the factor is known
        }
      }
      }
      if (widthCorrection == 1.0 || attempt == 1) {
        break;              // nothing to correct, or already corrected
      }
    }
  } catch (winrt::hresult_error const& e) {
    char message[256];
    std::snprintf(message, sizeof(message), "0x%08X",
                  static_cast<unsigned>(e.code()));
    result.error = std::string("Windows could not read the PDF (") + message + ")";
    result.pagePaths.clear();
  }
  return result;
}

// ---------------------------------------------------------------------------
// macOS — CoreGraphics
// ---------------------------------------------------------------------------
#elif defined(__APPLE__)

bool pdfRasterAvailable(std::string& whyNot) {
  whyNot.clear();
  return true;   // CoreGraphics ships with the OS
}

PdfRasterResult rasterisePdf(const fs::path& pdfPath, const fs::path& outputDir,
                             int targetWidthPixels,
                             const std::function<void(int, int)>& onProgress) {
  PdfRasterResult result;
  std::error_code ec;
  fs::create_directories(outputDir, ec);
  if (ec) {
    result.error = "could not create " + outputDir.string();
    return result;
  }
  CFStringRef pathRef = CFStringCreateWithCString(
    nullptr, pdfPath.c_str(), kCFStringEncodingUTF8);
  CFURLRef url = CFURLCreateWithFileSystemPath(
    nullptr, pathRef, kCFURLPOSIXPathStyle, false);
  CGPDFDocumentRef doc = url ? CGPDFDocumentCreateWithURL(url) : nullptr;
  if (url) CFRelease(url);
  if (pathRef) CFRelease(pathRef);
  if (!doc) {
    result.error = "macOS could not read the PDF";
    return result;
  }
  const size_t pageCount = CGPDFDocumentGetNumberOfPages(doc);
  if (pageCount == 0) {
    CGPDFDocumentRelease(doc);
    result.error = "the document has no pages";
    return result;
  }
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  for (size_t i = 0; i < pageCount; ++i) {
    if (onProgress) {
      onProgress(static_cast<int>(i), static_cast<int>(pageCount));
    }
    // Pages are 1-based in CGPDFDocument.
    CGPDFPageRef page = CGPDFDocumentGetPage(doc, i + 1);
    if (!page) continue;
    const CGRect box = CGPDFPageGetBoxRect(page, kCGPDFCropBox);
    // ROTATION IS PART OF THE PAGE. A landscape deck is often stored as
    // portrait with /Rotate 90, and the crop box does not account for it -- so
    // measuring straight off the box gives the wrong aspect and renders the
    // slide sideways or clipped. Windows and pdftoppm both apply it for free;
    // CoreGraphics makes it the caller's job.
    const int rotation = ((CGPDFPageGetRotationAngle(page) % 360) + 360) % 360;
    const bool quarterTurned = (rotation == 90 || rotation == 270);
    const double pageWidth = quarterTurned ? box.size.height : box.size.width;
    const double pageHeight = quarterTurned ? box.size.width : box.size.height;
    const double scale = pageWidth > 0.0
      ? static_cast<double>(targetWidthPixels) / pageWidth : 1.0;
    const size_t w = static_cast<size_t>(std::max(1, targetWidthPixels));
    const size_t h = static_cast<size_t>(std::max(1.0, pageHeight * scale));
    CGContextRef context = CGBitmapContextCreate(
      nullptr, w, h, 8, 0, space,
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    if (!context) continue;
    // A PDF page has no background of its own. Without this a slide with white
    // text on a transparent ground arrives as white text on black.
    CGContextSetRGBFillColor(context, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(context, CGRectMake(0, 0, static_cast<CGFloat>(w),
                                          static_cast<CGFloat>(h)));
    // The transform CoreGraphics itself computes for fitting a page into a
    // rect: it applies the rotation, the crop box origin and the flip in one
    // step, which hand-rolled scale-and-translate does not.
    CGContextConcatCTM(context, CGPDFPageGetDrawingTransform(
      page, kCGPDFCropBox,
      CGRectMake(0, 0, static_cast<CGFloat>(w), static_cast<CGFloat>(h)),
      0, true));
    CGContextDrawPDFPage(context, page);

    CGImageRef image = CGBitmapContextCreateImage(context);
    const std::string name = pageFileName(static_cast<int>(i));
    const fs::path outPath = outputDir / name;
    CFStringRef outRef = CFStringCreateWithCString(
      nullptr, outPath.c_str(), kCFStringEncodingUTF8);
    CFURLRef outUrl = CFURLCreateWithFileSystemPath(
      nullptr, outRef, kCFURLPOSIXPathStyle, false);
    CGImageDestinationRef dest = outUrl
      ? CGImageDestinationCreateWithURL(outUrl, CFSTR("public.png"), 1, nullptr)
      : nullptr;
    if (dest && image) {
      CGImageDestinationAddImage(dest, image, nullptr);
      if (CGImageDestinationFinalize(dest)) {
        result.pagePaths.push_back(outPath.string());
      }
    }
    if (dest) CFRelease(dest);
    if (outUrl) CFRelease(outUrl);
    if (outRef) CFRelease(outRef);
    if (image) CGImageRelease(image);
    CGContextRelease(context);
  }
  CGColorSpaceRelease(space);
  CGPDFDocumentRelease(doc);
  if (result.pagePaths.empty()) {
    result.error = "no pages could be rendered";
  }
  return result;
}

// ---------------------------------------------------------------------------
// Linux — pdftoppm (poppler-utils)
// ---------------------------------------------------------------------------
#else

namespace {

bool haveTool(const std::string& tool) {
  auto found = readAllText({"/bin/sh", "-c", "command -v " + tool});
  return found.has_value() && !found->empty();
}

}  // namespace

bool pdfRasterAvailable(std::string& whyNot) {
  if (haveTool("pdftoppm")) {
    whyNot.clear();
    return true;
  }
  whyNot = "pdftoppm is not installed (apt install poppler-utils)";
  return false;
}

PdfRasterResult rasterisePdf(const fs::path& pdfPath, const fs::path& outputDir,
                             int targetWidthPixels,
                             const std::function<void(int, int)>& onProgress) {
  PdfRasterResult result;
  std::string whyNot;
  if (!pdfRasterAvailable(whyNot)) {
    result.error = whyNot;
    return result;
  }
  std::error_code ec;
  fs::create_directories(outputDir, ec);
  if (ec) {
    result.error = "could not create " + outputDir.string();
    return result;
  }
  if (onProgress) {
    // pdftoppm renders the whole document in one run and reports nothing as it
    // goes, so the operator gets a start and an end rather than a count.
    onProgress(0, 0);
  }
  // -scale-to-x with -scale-to-y -1 fixes the width and keeps the aspect,
  // which is the same contract as the other two backends without any dpi
  // arithmetic to get wrong.
  const fs::path prefix = outputDir / "page";
  auto run = readAllText({
    "pdftoppm", "-png",
    "-scale-to-x", std::to_string(std::max(1, targetWidthPixels)),
    "-scale-to-y", "-1",
    pdfPath.string(), prefix.string()});
  if (!run.has_value()) {
    result.error = "pdftoppm failed on " + pdfPath.filename().string();
    return result;
  }
  // pdftoppm names its output page-1.png, page-01.png or page-001.png
  // depending on the page count, so collect and sort rather than predicting.
  std::vector<fs::path> produced;
  for (fs::directory_iterator it(outputDir, ec), end; !ec && it != end; ++it) {
    if (it->is_regular_file(ec) && lowerExtension(it->path()) == ".png") {
      produced.push_back(it->path());
    }
  }
  std::sort(produced.begin(), produced.end());
  for (const fs::path& page : produced) {
    result.pagePaths.push_back(page.string());
  }
  if (result.pagePaths.empty()) {
    result.error = "pdftoppm produced no pages";
  }
  return result;
}

#endif

}  // namespace deckboy::platform
