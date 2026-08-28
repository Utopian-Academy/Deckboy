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

// A presentation that is NOT a PDF (.pptx, .ppt, .key, .odp). These cannot be
// rendered here; the caller tells the operator to export a PDF. Kept as its own
// question so the app can say something useful instead of "unsupported file".
bool isPresentationDocumentPath(const std::filesystem::path& path);

struct PdfRasterResult {
  std::vector<std::string> pagePaths;   // in page order
  std::string error;                    // empty on success
  bool ok() const { return error.empty() && !pagePaths.empty(); }
};

// Render every page of `pdfPath` into `outputDir` as PNG, at `scale` times the
// page's natural size, and return the files in order.
//
// `scale` is the only place the resolution is decided: once a page is a PNG the
// detail is gone, so this renders generously rather than to the current output
// size, which the operator may change after importing.
//
// `onProgress(pageIndex, pageCount)` is called from THIS thread as it goes;
// callers running it in the background must marshal anything it touches.
PdfRasterResult rasterisePdf(const std::filesystem::path& pdfPath,
                             const std::filesystem::path& outputDir,
                             double scale,
                             const std::function<void(int, int)>& onProgress);

// Whether this build can rasterise at all, and what to tell the operator when
// it cannot. On Linux this is "is pdftoppm installed"; elsewhere it is always
// available because the engine ships with the OS.
bool pdfRasterAvailable(std::string& whyNot);

}  // namespace deckboy::platform

#endif  // DECKBOY_PLATFORM_PDF_IMPORT_HPP
