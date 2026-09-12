#include "pdf_import.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>
#include <utility>
#if defined(DECKBOY_HAS_ZLIB)
#include <zlib.h>
#endif

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
#include "core/io_utils.hpp"
#include "core/paths.hpp"
#include "core/subprocess.hpp"

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

// Run a helper and keep what it said, however it ended.
//
// This used readAllText, which discards the output on a non-zero exit -- so a
// converter that failed AND EXPLAINED ITSELF came back with an empty string
// and the caller could only say "it failed". PowerPoint's "can't save ^0 to
// ^1" names its fault exactly; losing it turned a five-minute diagnosis into
// an hour. Output is captured rather than inherited so a converter cannot
// print over the app's own console.
bool runQuietly(const std::vector<std::string>& args, std::string& output) {
  ProcessCapture captured = runCaptured(args);
  output = captured.output;
  return captured.ok();
}

// The last line a tool printed, which is usually the one that says what went
// wrong; the rest is banners and progress. Trimmed to something a toast can
// carry without becoming a wall of text.
// THE LINE THAT SAYS WHAT WENT WRONG, which is neither the first nor the
// last. A PowerShell error record is several lines of scaffolding around one
// sentence: the message, then "At <script>:<line>", then CategoryInfo, then
// FullyQualifiedErrorId. Taking the last line handed the operator
// "FullyQualifiedErrorId : ...WriteErrorException,_export.ps1", which is true
// and useless. osascript and soffice put their message first instead.
//
// So: the first substantive line, skipping the scaffolding by shape rather
// than by matching any one tool.
// WHAT THE TOOL SAID, from a line it was asked to tag.
//
// The scripts Deckboy drives print their own failure as "DECKBOY-ERROR: ..."
// on a single line, because PowerShell's own error rendering wraps at a
// width that depends on whether it has a console -- through a pipe it split
// the script path across two lines mid-word, which defeated every attempt to
// recognise it by shape. A tag we choose is the one thing that survives.
//
// Falls back to the first substantive line for tools that were not written
// here (osascript, soffice), which print a plain message and nothing else.
std::string toolMessage(const std::string& text, std::size_t limit = 160) {
  const std::string kTag = "DECKBOY-ERROR: ";
  const std::size_t tagged = text.find(kTag);
  if (tagged != std::string::npos) {
    const std::size_t from = tagged + kTag.size();
    const std::size_t nl = text.find('\n', from);
    std::string line = text.substr(from, nl == std::string::npos ? std::string::npos
                                                                 : nl - from);
    const std::size_t last = line.find_last_not_of(" \t\r");
    line = (last == std::string::npos) ? std::string() : line.substr(0, last + 1);
    if (line.size() > limit) {
      line.resize(limit);
    }
    return line;
  }
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t nl = text.find('\n', at);
    std::string line = text.substr(at, nl == std::string::npos ? std::string::npos
                                                               : nl - at);
    at = (nl == std::string::npos) ? text.size() : nl + 1;
    const std::size_t first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
      continue;
    }
    line = line.substr(first);
    const std::size_t last = line.find_last_not_of(" \t\r");
    line = line.substr(0, last + 1);
    if (line.size() > limit) {
      line.resize(limit);
    }
    return line;
  }
  return {};
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
  if (!fs::exists(convertedPdfPath(source, outputDir), ec)) {
    // Whether it ran matters less than what it said: LibreOffice exits 0 while
    // refusing a file often enough that the PDF's absence is the real test.
    const std::string said = toolMessage(output);
    error = said.empty()
      ? std::string(ran ? "LibreOffice produced no PDF"
                        : "LibreOffice could not be run")
      : ("LibreOffice: " + said);
    return false;
  }
  (void) ran;
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
        // A MARKED, PLAIN LINE rather than Write-Error. PowerShell renders
        // an error record as several lines wrapped at a width that depends
        // on whether it has a console -- and through a pipe it wrapped the
        // script path itself mid-word, which no line-shape heuristic can
        // survive. Emitting one tagged line is the only reliable way to
        // get the message back out.
        << "  Write-Output (\"DECKBOY-ERROR: \" + $_.Exception.Message)\n"
        << "  exit 1\n"
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
    const std::string said = toolMessage(output);
    error = said.empty()
      ? std::string("PowerPoint could not export this file")
      : ("PowerPoint: " + said);
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
    const std::string said = toolMessage(output);
    error = said.empty()
      ? std::string("Keynote could not export this file (it may need "
                    "permission to be automated: System Settings > Privacy "
                    "& Security > Automation)")
      : ("Keynote: " + said);
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

// THE RENDER ITSELF, in whatever process is willing to host it.
//
// Public so the `--pdf-render` child can reach it; rasterisePdf below is what
// the app calls, and it does not run this in the show's own process. See the
// long note there.
PdfRasterResult rasterisePdfInProcess(const fs::path& pdfPath, const fs::path& outputDir,
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
  try {
    // This runs on a worker thread, so it needs its own apartment.
    // Multi-threaded rather than single: there is no message pump out here to
    // service an STA.
    //
    // INSIDE the try, which it was not. init_apartment throws on failure --
    // RPC_E_CHANGED_MODE when the thread already has an apartment of the other
    // kind, and it is not the only way it can fail -- and from outside the try
    // that throw escaped rasterisePdf, escaped the caller's lambda, and left a
    // std::thread by exception. An exception leaving a thread function calls
    // std::terminate, so the whole app died. That is what "I could not drop a
    // PDF in" was: not a refusal, a crash, with nothing said and nothing
    // written.
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    // AND LEAVE IT AGAIN ON THE WAY OUT.
    //
    // init_apartment was never paired with uninit_apartment, so every slide
    // render left an initialised COM apartment behind on a worker thread that
    // then exited. The import itself is unaffected -- pages render, cues
    // appear, the app runs on quite happily -- and then the process faults on
    // shutdown, tearing down an apartment whose thread is long gone. It landed
    // late enough that the crash handler could open its log and not write to
    // it: a zero-byte deckboy-crash.log and an app that vanished.
    //
    // A guard rather than a plain call at the end, because every failure path
    // below returns early and each one has to leave the apartment too.
    struct ApartmentGuard {
      ~ApartmentGuard() { winrt::uninit_apartment(); }
    } apartmentGuard;
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
      // ROUNDED, like the width above it, and for the same reason.
      //
      // A page's Size comes back as FLOATS, so 960x540 asked for at 3840 wide
      // computes a scale a hair under 4 and a height of 2159.9998 -- which a
      // cast truncates to 2159. One pixel short of 2160 is not cosmetic on a
      // slide deck: the output then rescales every page to fill the raster,
      // and rescaling by a 2159/2160 ratio softens every glyph on it. The
      // width was fixed for this once; the height was missed.
      options.DestinationHeight(static_cast<uint32_t>(
        std::max(1.0, static_cast<double>(std::llround(size.Height * pageScale)))));

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
  } catch (const std::exception& e) {
    // Anything else at all. This is called from a worker thread, so an escape
    // is not an error the operator sees, it is the process gone.
    result.error = std::string("could not read the PDF: ") + e.what();
    result.pagePaths.clear();
  } catch (...) {
    result.error = "could not read the PDF";
    result.pagePaths.clear();
  }
  return result;
}

// ── The PDF renderer does not get to live in the show process ───────────────
//
// Windows.Data.Pdf is Edge's rasteriser, and it brings Edge's shutdown with it.
// Once that DLL has been loaded it registers static destructors that tear down
// a D3D11 device at DLL_PROCESS_DETACH -- after the loader has already begun
// unwinding the graphics stack Deckboy itself was using. The result was an
// access violation on EXIT, every single time, for any show that had imported a
// PDF. The import worked, the pages were correct, the slides played; the app
// then died on the way out and took the "did everything save" question with it.
//
// The stack said so plainly once the crash handler could write one:
//
//   ntdll!LdrShutdownProcess -> Windows.Data.Pdf!DllMain(DETACH)
//     -> ucrtbase static dtors -> Windows.Data.Pdf -> d3d11 -> dxgi -> fault
//
// Not a line of Deckboy in it. There is nothing to fix inside our own code,
// because the fault is a third-party DLL's teardown order against the loader's.
//
// So the rasteriser runs in a child process instead -- which is exactly what
// Linux has always done with pdftoppm, and no worse than what macOS does with
// CoreGraphics, a library that has no such opinion about exiting. The child
// loads Windows.Data.Pdf, writes the PNGs, prints its progress, and goes away.
// The show process never loads the DLL at all, so it has nothing to detach.
//
// Two things fall out of this for free, and both matter more than the crash:
// a malformed PDF that takes the renderer down with it now costs an import
// instead of a show, and a slow render cannot wedge the UI thread through a
// COM call that does not return.
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

  const fs::path self = core::Paths::executablePath();
  if (self.empty()) {
    // Nothing sane left to spawn. Render here rather than refuse the import;
    // the exit fault is a bad trade but it is a better one than "no slides".
    return rasterisePdfInProcess(pdfPath, outputDir, targetWidthPixels, onProgress);
  }

  ChildProcess child;
  const std::vector<std::string> args {
      self.string(), "--pdf-render", pdfPath.string(), outputDir.string(),
      std::to_string(targetWidthPixels)};
  if (!spawnProcess(child, args, SpawnOptions::pipedStdout())) {
    result.error = "could not start the PDF renderer";
    return result;
  }

  // The child speaks one line per event, and nothing else goes to stdout:
  //   PROGRESS <index> <count>     as each page finishes
  //   PAGE <index> <count> <path>  the results, in order, at the end
  //   ERROR <message>              nothing was produced
  std::string pending;
  int pageCount = 0;
  char buffer[4096];
  for (;;) {
    const int got = readSome(child.readFd, buffer, sizeof(buffer));
    if (got <= 0) break;
    pending.append(buffer, static_cast<std::size_t>(got));
    std::size_t nl;
    while ((nl = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, nl);
      pending.erase(0, nl + 1);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      if (line.rfind("PROGRESS ", 0) == 0) {
        const char* c = line.c_str() + 9;
        const int index = std::atoi(c);
        if (const char* sp = std::strchr(c, ' ')) {
          const int count = std::atoi(sp + 1);
          if (count > 0) pageCount = count;
        }
        if (onProgress) onProgress(index, pageCount);
      } else if (line.rfind("ERROR ", 0) == 0) {
        result.error = line.substr(6);
      } else if (line.rfind("PAGE ", 0) == 0) {
        // PAGE <index> <count> <path> -- the path may contain spaces, so it is
        // everything after the third field, not a token.
        const char* c = line.c_str() + 5;
        const int index = std::atoi(c);
        const char* sp = std::strchr(c, ' ');
        if (!sp) continue;
        const int count = std::atoi(sp + 1);
        const char* sp2 = std::strchr(sp + 1, ' ');
        if (!sp2) continue;
        result.pagePaths.emplace_back(sp2 + 1);
        if (count > 0) pageCount = count;
        (void)index;
      }
    }
  }
  child.stop();

  if (result.error.empty() && result.pagePaths.empty()) {
    result.error = "the PDF renderer produced no pages";
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
    // Rounded, not truncated -- see the note on the Windows path. A page whose
    // height lands a fraction under an integer would otherwise come out a
    // pixel short and be rescaled to fill the output, softening the text.
    const size_t h = static_cast<size_t>(
      std::max(1LL, std::llround(pageHeight * scale)));
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

// ── SPEAKER NOTES OUT OF A .pptx ───────────────────────────────────────────
//
// A PowerPoint file is a ZIP, and each slide's notes are a small XML part
// inside it. That makes them the only per-slide notes that are properly
// machine-readable without a PDF parser -- and Google Slides exports .pptx as
// well as PDF, so a team that works in Slides and exports a PDF to keep its
// fonts can drop the .pptx beside it and keep its notes too.
//
// Implemented against the archive directly rather than through a library: the
// whole job is "find four kinds of entry by name, inflate them, pull the text
// out", and a ZIP central directory is a well-documented forty-six byte record.
// zlib does the one part worth not writing.
//
// Compiled out entirely without zlib. The sidecar route still works, so a
// build missing it loses a convenience rather than a capability.
#if defined(DECKBOY_HAS_ZLIB)

namespace {

// One entry of the archive's central directory: where its data is, how big it
// is, and whether it is deflated or stored.
struct ZipEntry {
  std::uint32_t localHeaderOffset = 0;
  std::uint32_t compressedSize = 0;
  std::uint32_t uncompressedSize = 0;
  std::uint16_t method = 0;
};

std::uint16_t readU16(const std::vector<unsigned char>& b, std::size_t at) {
  return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}
std::uint32_t readU32(const std::vector<unsigned char>& b, std::size_t at) {
  return static_cast<std::uint32_t>(b[at]) |
         (static_cast<std::uint32_t>(b[at + 1]) << 8) |
         (static_cast<std::uint32_t>(b[at + 2]) << 16) |
         (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

// The archive's index, by entry name. Read once; the callers below want half a
// dozen entries out of a file with hundreds.
std::map<std::string, ZipEntry> zipIndex(const std::vector<unsigned char>& buf) {
  std::map<std::string, ZipEntry> entries;
  if (buf.size() < 22) return entries;
  // End of central directory: scan back for its signature. The comment field
  // is at most 64k, so that is as far back as it can be.
  const std::size_t limit = std::min<std::size_t>(buf.size(), 66000);
  std::size_t eocd = 0;
  bool found = false;
  for (std::size_t back = 22; back <= limit; ++back) {
    const std::size_t at = buf.size() - back;
    if (readU32(buf, at) == 0x06054b50) { eocd = at; found = true; break; }
  }
  if (!found) return entries;

  const std::uint16_t count = readU16(buf, eocd + 10);
  std::size_t at = readU32(buf, eocd + 16);
  for (std::uint16_t i = 0; i < count; ++i) {
    if (at + 46 > buf.size() || readU32(buf, at) != 0x02014b50) break;
    ZipEntry entry;
    entry.method = readU16(buf, at + 10);
    entry.compressedSize = readU32(buf, at + 20);
    entry.uncompressedSize = readU32(buf, at + 24);
    const std::uint16_t nameLen = readU16(buf, at + 28);
    const std::uint16_t extraLen = readU16(buf, at + 30);
    const std::uint16_t commentLen = readU16(buf, at + 32);
    entry.localHeaderOffset = readU32(buf, at + 42);
    if (at + 46 + nameLen > buf.size()) break;
    entries.emplace(std::string(reinterpret_cast<const char*>(buf.data() + at + 46), nameLen),
                    entry);
    at += 46u + nameLen + extraLen + commentLen;
  }
  return entries;
}

// One entry's bytes. Returns empty on anything unexpected rather than throwing:
// a malformed deck should cost its notes, not the import.
std::string zipRead(const std::vector<unsigned char>& buf, const ZipEntry& entry) {
  const std::size_t at = entry.localHeaderOffset;
  if (at + 30 > buf.size() || readU32(buf, at) != 0x04034b50) return {};
  // The local header repeats the name and extra lengths, and they can DIFFER
  // from the central directory's, so the data offset must be computed here.
  const std::uint16_t nameLen = readU16(buf, at + 26);
  const std::uint16_t extraLen = readU16(buf, at + 28);
  const std::size_t data = at + 30u + nameLen + extraLen;
  if (data + entry.compressedSize > buf.size()) return {};

  if (entry.method == 0) {                       // stored
    return std::string(reinterpret_cast<const char*>(buf.data() + data),
                       entry.compressedSize);
  }
  if (entry.method != 8) return {};              // only deflate, which is what Office writes

  std::string out;
  out.resize(entry.uncompressedSize);
  z_stream zs {};
  // -MAX_WBITS: raw deflate, no zlib wrapper. A ZIP entry has none.
  if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return {};
  zs.next_in = const_cast<Bytef*>(buf.data() + data);
  zs.avail_in = entry.compressedSize;
  zs.next_out = reinterpret_cast<Bytef*>(out.data());
  zs.avail_out = static_cast<uInt>(out.size());
  const int rc = inflate(&zs, Z_FINISH);
  inflateEnd(&zs);
  if (rc != Z_STREAM_END) return {};
  return out;
}

// The text of an OOXML part, taken from its <a:t> runs.
//
// Paragraphs matter: <a:p> is a line in the notes pane, and a speaker's notes
// are usually a list. So each paragraph becomes a line, and the runs inside it
// are joined without a separator because Word splits a sentence into runs
// wherever the formatting changes.
//
// Elements are matched by their WHOLE NAME. Searching for the string "<a:t"
// instead also matches <a:tileRect>, <a:tblPr> and <a:tabLst>, and because the
// text was then read up to the next "</a:t>", one tile fill swallowed several
// hundred bytes of markup and handed it back as the speaker's notes.
std::string ooxmlText(const std::string& xml) {
  std::vector<std::string> paragraphs;
  std::string current;
  std::size_t at = 0;
  while (at < xml.size()) {
    const std::size_t open = xml.find('<', at);
    if (open == std::string::npos) break;
    const std::size_t close = xml.find('>', open + 1);
    if (close == std::string::npos) break;
    std::size_t nameEnd = open + 1;
    while (nameEnd < close && xml[nameEnd] != ' ' && xml[nameEnd] != '/' &&
           xml[nameEnd] != '\t' && xml[nameEnd] != '\n' && xml[nameEnd] != '\r') {
      ++nameEnd;
    }
    const std::string name = xml.substr(open + 1, nameEnd - open - 1);
    const bool selfClosing = xml[close - 1] == '/';
    at = close + 1;
    // <a:br/> is a soft line break, which reads as a new line too.
    if (name == "a:p" || name == "a:br") {
      paragraphs.push_back(current);
      current.clear();
      continue;
    }
    if (name == "a:t" && !selfClosing) {
      const std::size_t end = xml.find("</a:t>", at);
      if (end == std::string::npos) break;
      current += xml.substr(at, end - at);
      at = end + 6;
    }
  }
  paragraphs.push_back(current);

  std::string text;
  for (const std::string& line : paragraphs) {
    if (line.empty()) continue;
    if (!text.empty()) text += "\n";
    text += line;
  }
  // The five XML entities, which is all OOXML text can contain.
  const std::pair<const char*, const char*> entities[] = {
    {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
    {"&quot;", "\""}, {"&apos;", "'"},
  };
  for (const auto& [from, to] : entities) {
    std::size_t found = 0;
    while ((found = text.find(from, found)) != std::string::npos) {
      text.replace(found, std::strlen(from), to);
      found += std::strlen(to);
    }
  }
  return text;
}

// Which notesSlide belongs to slide N.
//
// Taken from the slide's own relationships rather than assumed: notesSlide7 is
// USUALLY slide 7's, but only because most decks have notes on every slide.
// Add notes to slides 2 and 5 of a ten-slide deck and you get notesSlide1 and
// notesSlide2, and guessing by number puts both on the wrong slides.
std::string notesPartForSlide(const std::vector<unsigned char>& zip,
                              const std::map<std::string, ZipEntry>& index,
                              int slideNumber) {
  const std::string rels = "ppt/slides/_rels/slide" + std::to_string(slideNumber) + ".xml.rels";
  const auto at = index.find(rels);
  if (at == index.end()) return {};
  const std::string xml = zipRead(zip, at->second);

  // ONE RELATIONSHIP AT A TIME, matching on its Type and then reading its own
  // Target. Searching the whole file for "notesSlide" and then looking BACK
  // for a Target does not work: the word appears in the Type URL first
  // (".../relationships/notesSlide"), and every real PowerPoint writes Type
  // before Target, so the backward search lands on the PREVIOUS relationship's
  // target -- the slide layout. A hand-written test file with the attributes
  // the other way round passed happily; a real deck found none at all.
  auto attribute = [](const std::string& element, const char* name) -> std::string {
    const std::string key = std::string(name) + "=\"";
    const std::size_t found = element.find(key);
    if (found == std::string::npos) return {};
    const std::size_t start = found + key.size();
    const std::size_t close = element.find('"', start);
    if (close == std::string::npos) return {};
    return element.substr(start, close - start);
  };

  std::size_t scan = 0;
  while ((scan = xml.find("<Relationship", scan)) != std::string::npos) {
    const std::size_t end = xml.find('>', scan);
    if (end == std::string::npos) break;
    const std::string element = xml.substr(scan, end - scan);
    scan = end + 1;
    const std::string type = attribute(element, "Type");
    // The type is a URL whose last segment names the relationship.
    const std::size_t seg = type.rfind('/');
    if (seg == std::string::npos || type.substr(seg + 1) != "notesSlide") {
      continue;
    }
    std::string target = attribute(element, "Target");
    if (target.empty()) continue;
    const std::size_t slash = target.rfind('/');
    if (slash != std::string::npos) target = target.substr(slash + 1);
    return "ppt/notesSlides/" + target;
  }
  return {};
}

// The NOTES out of a notes part.
//
// A notes slide is three shapes: a picture of the slide, the notes themselves,
// and the slide number. Only the middle one is the speaker's, so the text is
// taken from the shape carrying the BODY placeholder and the other two are
// never read.
//
// The alternative -- read the whole part and drop a leading line that looks
// like a number -- works only while PowerPoint keeps writing the number first,
// and silently eats a note that genuinely begins with one. A test deck here
// has a note reading "Test notes 12", which is exactly the shape that
// heuristic cannot tell from a page number.
std::string notesBodyText(const std::string& xml) {
  std::string fallback;
  std::size_t at = 0;
  while ((at = xml.find("<p:sp>", at)) != std::string::npos) {
    const std::size_t end = xml.find("</p:sp>", at);
    if (end == std::string::npos) break;
    const std::string shape = xml.substr(at, end - at);
    at = end + 7;

    std::string type;
    const std::size_t ph = shape.find("<p:ph");
    if (ph != std::string::npos) {
      const std::size_t key = shape.find("type=\"", ph);
      const std::size_t close = shape.find('>', ph);
      if (key != std::string::npos && close != std::string::npos && key < close) {
        const std::size_t from = key + 6;
        const std::size_t to = shape.find('"', from);
        if (to != std::string::npos) type = shape.substr(from, to - from);
      }
    }
    if (type == "sldNum" || type == "sldImg" || type == "dt" || type == "ftr") {
      continue;
    }
    const std::string text = ooxmlText(shape);
    if (type == "body") return text;
    // A deck whose notes live in a plain text box rather than in the
    // placeholder still has notes; they are just not labelled as such.
    if (fallback.empty()) fallback = text;
  }
  return fallback;
}

}  // namespace

std::vector<std::string> slideNotesFromPptx(const fs::path& pptxPath,
                                            std::size_t slideCount) {
  std::vector<std::string> notes(slideCount);
  if (slideCount == 0) return notes;
  std::ifstream in(pptxPath, std::ios::binary);
  if (!in) return notes;
  std::vector<unsigned char> buf((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
  if (buf.empty()) return notes;

  const auto index = zipIndex(buf);
  if (index.empty()) return notes;

  for (std::size_t i = 0; i < slideCount; ++i) {
    const std::string part = notesPartForSlide(buf, index, static_cast<int>(i) + 1);
    if (part.empty()) continue;
    const auto entry = index.find(part);
    if (entry == index.end()) continue;
    notes[i] = notesBodyText(zipRead(buf, entry->second));
  }
  return notes;
}

#else   // no zlib

std::vector<std::string> slideNotesFromPptx(const fs::path&, std::size_t slideCount) {
  return std::vector<std::string>(slideCount);
}

#endif

}  // namespace deckboy::platform
