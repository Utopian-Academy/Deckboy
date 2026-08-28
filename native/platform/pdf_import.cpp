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
#else
#include "core/subprocess.hpp"
#endif

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
    auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
      winrt::hstring(pdfPath.wstring())).get();
    auto doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
    auto folder = winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(
      winrt::hstring(outputDir.wstring())).get();

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
