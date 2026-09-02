// pdf_import.hpp — turn a page-based document into stills, one per page.
//
// A slide deck belongs in a show as ORDINARY IMAGE CUES, rendered once at
// import and never touched again. That is not a shortcut, it is the reason to
// do it this way: nothing in a live show should depend on a document renderer
// being fast, or being present, or not deciding to reflow a page halfway
// through the keynote. Once the pages are stills they behave exactly like every
// other cue -- they take, they fade, they carry effects, they crossfade to the
// next one -- and the operator's clicker walks them with Page Down.
//
// EACH PLATFORM'S OWN ENGINE, no bundled library:
//
//   Windows  Windows.Data.Pdf, which is the renderer Edge uses.
//   macOS    CoreGraphics CGPDFDocument, which is the renderer Preview uses.
//   Linux    pdftoppm from poppler-utils, which is what the desktop already
//            renders PDFs with. The one platform where it is a subprocess, and
//            the one platform where the tool is reliably installed.
//
// Bundling a rasteriser instead would mean either an AGPL engine (MuPDF,
// Ghostscript), which Deckboy cannot link, or vendoring something the size of
// pdfium for a job the operating system already does well.
//
// WHAT THIS CANNOT DO, and no PDF-based route can: PowerPoint flattens every
// build to its final state on export and drops transitions entirely, so a deck
// that animates arrives here as static slides. Keynote can export one page per
// build stage, and those come through as one cue per stage, which is the
// behaviour an operator wants. For a PowerPoint deck that genuinely animates,
// capture it live with a window-source cue instead -- see the notes in
// CHANGES.md.

#ifndef DECKBOY_PLATFORM_PDF_IMPORT_HPP
#define DECKBOY_PLATFORM_PDF_IMPORT_HPP

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace deckboy::platform {

// Is this a document this module can turn into pages?
bool isPdfDocumentPath(const std::filesystem::path& path);

// A presentation that is NOT a PDF (.pptx, .ppt, .key, .odp). Nothing in this
// module can rasterise one directly -- they go through convertPresentationToPdf
// first. Kept as its own question so the app can treat them as a class.
bool isPresentationDocumentPath(const std::filesystem::path& path);

// ── Presentations, by way of the application that owns the format ───────────
//
// A .pptx is not a document format anyone should reimplement: it is a bag of
// XML whose meaning is whatever PowerPoint does with it, and a half-right
// renderer that puts a slide's type in the wrong place is worse on a show day
// than an honest refusal. So Deckboy does not render one. It asks whatever is
// already on the machine to export a PDF, and then rasterises that with the
// engine it already trusts.
//
// In preference order, per platform:
//
//   Windows  PowerPoint itself (COM), then LibreOffice.
//   macOS    Keynote for .key, then LibreOffice, then PowerPoint if present.
//   Linux    LibreOffice.
//
// PowerPoint before LibreOffice on Windows because a format's owner is the
// authority on it; LibreOffice before PowerPoint on macOS only for the formats
// PowerPoint does not own. Where none is installed the operator is told which
// to install, which is a better answer than "unsupported file".
//
// THE CAVEAT FROM ABOVE STILL HOLDS: exporting to PDF flattens builds and drops
// transitions. This makes the import work; it does not make an animated deck
// animate.
struct PresentationConversion {
  std::filesystem::path pdfPath;   // the PDF that was produced
  std::string converter;           // human-readable name of what did it
  std::string error;               // empty on success
  bool ok() const { return error.empty() && !pdfPath.empty(); }
};

// Is anything on this machine able to convert a presentation? `whyNot` is set
// to something an operator can act on when the answer is no.
bool presentationConvertAvailable(std::string& whyNot);

// Export `source` to a PDF inside `outputDir` and return where it landed.
// Blocking, and slow enough to matter -- Office can take tens of seconds on a
// large deck -- so callers run it off the UI thread.
PresentationConversion convertPresentationToPdf(
  const std::filesystem::path& source, const std::filesystem::path& outputDir);

struct PdfRasterResult {
  std::vector<std::string> pagePaths;   // in page order
  std::string error;                    // empty on success
  bool ok() const { return error.empty() && !pagePaths.empty(); }
};

// Render every page of `pdfPath` into `outputDir` as PNG, each page scaled to
// `targetWidthPixels` wide with its aspect kept, and return the files in order.
//
// A TARGET WIDTH, not a scale factor. The three engines measure a page in three
// different units -- WinRT reports device-independent pixels at 96dpi,
// CoreGraphics reports PDF points at 72dpi, pdftoppm wants a dpi -- so the same
// "2x" produced 1632px wide on Windows and 1224px on Linux for the same
// document. A deck must not import at a different resolution depending on which
// machine it was loaded on.
//
// This is the only place the resolution is decided. Once a page is a PNG the
// detail is gone, so it renders for the largest output the app supports rather
// than the one currently configured, which the operator may change afterwards.
//
// `onProgress(pageIndex, pageCount)` is called from THIS thread as it goes;
// callers running it in the background must marshal anything it touches.
PdfRasterResult rasterisePdf(const std::filesystem::path& pdfPath,
                             const std::filesystem::path& outputDir,
                             int targetWidthPixels,
                             const std::function<void(int, int)>& onProgress);

// Whether this build can rasterise at all, and what to tell the operator when
// it cannot. On Linux this is "is pdftoppm installed"; elsewhere it is always
// available because the engine ships with the OS.
bool pdfRasterAvailable(std::string& whyNot);

}  // namespace deckboy::platform

#endif  // DECKBOY_PLATFORM_PDF_IMPORT_HPP
