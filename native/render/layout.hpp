// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// layout.hpp — Grid-aligned layout utilities for the Deckboy UI.
//
// Provides layout primitives that snap to the grid unit (kLayoutSpacingUnit)
// defined in core/constants.hpp for pixel-precise alignment:
//
//   Grid snapping:
//     snapDownToGrid()  — round down to nearest grid unit
//     snapUpToGrid()    — round up to nearest grid unit
//     snapRectToGrid()  — snap all four edges of a rectangle
//
//   Text measurement:
//     textLineHeight()  — height of one text line for a given TTF font
//     rowYBelowLabel()  — Y position below a single-line label
//     rowYBelowLines()  — Y position below N lines of text
//     safeTextRect()    — inset a rectangle to create text padding
//
//   Layout containers (stack-based, consume space from a bounding rect):
//     VerticalLayout    — stack children top-to-bottom with gaps
//     HorizontalLayout  — stack children left-to-right with gaps
//     GridLayout        — N×M grid with uniform cell sizes and spans
//     UITable           — variable-width columns with fixed row height
//
// All layout classes are header-only (inline). No .cpp counterpart.
// Used throughout the app .ipp rendering files for settings panels,
// inspector UI, transport controls, and all other Deckboy UI panels.
// ============================================================================

#pragma once

#include "core/sdl_compat.hpp"
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <vector>
#include "core/constants.hpp"

// ── Grid snapping helpers ───────────────────────────────────────────────────
// All UI layout coordinates are snapped to the grid unit to avoid subpixel
// artifacts and maintain consistent spacing across the entire interface.

// Round a pixel value down to the nearest multiple of kLayoutSpacingUnit.
// Handles negative values correctly (rounds toward negative infinity).
inline int snapDownToGrid(int value) {
  if (kLayoutSpacingUnit <= 1) {
    return value;
  }
  if (value >= 0) {
    return value - (value % kLayoutSpacingUnit);
  }
  int remainder = (-value) % kLayoutSpacingUnit;
  return remainder == 0 ? value : value - (kLayoutSpacingUnit - remainder);
}

// Round a pixel value up to the nearest multiple of kLayoutSpacingUnit.
inline int snapUpToGrid(int value) {
  if (kLayoutSpacingUnit <= 1) {
    return value;
  }
  int down = snapDownToGrid(value);
  return down == value ? value : down + kLayoutSpacingUnit;
}

// Snap all four edges of a rectangle to grid boundaries. Ensures both
// the origin (x,y) and the far edges (x+w, y+h) land on grid lines.
inline SDL_Rect snapRectToGrid(const SDL_Rect& rect) {
  int right = snapDownToGrid(rect.x + rect.w);
  int bottom = snapDownToGrid(rect.y + rect.h);
  SDL_Rect snapped {
    snapDownToGrid(rect.x),
    snapDownToGrid(rect.y),
    std::max(0, right - snapDownToGrid(rect.x)),
    std::max(0, bottom - snapDownToGrid(rect.y))
  };
  return snapped;
}

// Height of a single line of text for the given font, including the blank
// ascender/descender cushion returned by TTF_GetFontHeight. Falls back to a
// conservative constant if the font is null so callers can use this at
// compile-time-ish layout sites without guarding.
inline int textLineHeight(TTF_Font* font) {
  return font ? TTF_GetFontHeight(font) : 18;
}

// Y-coordinate of a UI row that sits directly below a single-line label
// drawn with TTF at (_, labelY). Keeps spacing consistent across stock and
// scaled font sizes instead of hard-coding a fixed pixel delta.
inline int rowYBelowLabel(int labelY, TTF_Font* font, int gap = 2) {
  return labelY + textLineHeight(font) + gap;
}

// Y-coordinate below N lines of text starting at startY.
inline int rowYBelowLines(int startY, TTF_Font* font, int lines, int gap = 2) {
  if (lines < 1) lines = 1;
  return startY + textLineHeight(font) * lines + gap;
}

// ── Text padding helper ─────────────────────────────────────────────────────
// Inset a rectangle to create padding for text content. Uses proportional
// insets: wider rects get 8px horizontal padding, narrow ones get 4px.
// Vertical padding scales with rect height (1px for <=24px, up to 3px).
inline SDL_Rect safeTextRect(const SDL_Rect& rect) {
  int targetInsetX = rect.w >= 96 ? 8 : 4;
  int insetX = std::min(targetInsetX, std::max(0, rect.w / 2 - 1));
  int insetY = rect.h <= 24 ? 1 : std::min(3, std::max(0, rect.h / 6));
  return SDL_Rect {
    rect.x + insetX,
    rect.y + insetY,
    std::max(0, rect.w - insetX * 2),
    std::max(0, rect.h - insetY * 2)
  };
}

// ── VerticalLayout ──────────────────────────────────────────────────────────
// Distributes child rectangles top-to-bottom within a bounding rect.
// Maintains a Y cursor that advances after each takeFixed() call.
// takeRemaining() consumes all remaining vertical space.
class VerticalLayout {
 public:
  explicit VerticalLayout(SDL_Rect bounds, int gap = kLayoutPanelGap)
    : bounds_(snapRectToGrid(bounds)),
      cursorY_(snapDownToGrid(bounds_.y)),
      gap_(std::max(0, gap)) {}

  // Consume a fixed-height slice from the top of remaining space.
  SDL_Rect takeFixed(int height) {
    if (bounds_.w <= 0 || bounds_.h <= 0) {
      return SDL_Rect {};
    }
    int snappedHeight = snapUpToGrid(std::max(0, height));
    int bottom = bounds_.y + bounds_.h;
    int y = cursorY_;
    int h = std::max(0, std::min(snappedHeight, bottom - y));
    SDL_Rect rect {bounds_.x, y, bounds_.w, h};
    cursorY_ = std::min(bottom, y + h + gap_);
    return rect;
  }

  // Consume all remaining vertical space as one rectangle.
  SDL_Rect takeRemaining() {
    if (bounds_.w <= 0 || bounds_.h <= 0) {
      return SDL_Rect {};
    }
    int bottom = bounds_.y + bounds_.h;
    int h = std::max(0, bottom - cursorY_);
    SDL_Rect rect {bounds_.x, cursorY_, bounds_.w, h};
    cursorY_ = bottom;
    return rect;
  }

  int remainingHeight() const {
    return std::max(0, bounds_.y + bounds_.h - cursorY_);
  }

 private:
  SDL_Rect bounds_ {};
  int cursorY_ = 0;  // Advances downward as children are placed
  int gap_ = 0;       // Spacing between consecutive children
};

// ── HorizontalLayout ───────────────────────────────────────────────────────
// Distributes child rectangles left-to-right within a bounding rect.
// Same pattern as VerticalLayout but along the X axis.
class HorizontalLayout {
 public:
  explicit HorizontalLayout(SDL_Rect bounds, int gap = kLayoutPanelGap)
    : bounds_(snapRectToGrid(bounds)),
      cursorX_(snapDownToGrid(bounds_.x)),
      gap_(std::max(0, gap)) {}

  // Consume a fixed-width slice from the left of remaining space.
  SDL_Rect takeFixed(int width) {
    if (bounds_.w <= 0 || bounds_.h <= 0) {
      return SDL_Rect {};
    }
    int snappedWidth = snapUpToGrid(std::max(0, width));
    int right = bounds_.x + bounds_.w;
    int x = cursorX_;
    int w = std::max(0, std::min(snappedWidth, right - x));
    SDL_Rect rect {x, bounds_.y, w, bounds_.h};
    cursorX_ = std::min(right, x + w + gap_);
    return rect;
  }

  // Consume all remaining horizontal space as one rectangle.
  SDL_Rect takeRemaining() {
    if (bounds_.w <= 0 || bounds_.h <= 0) {
      return SDL_Rect {};
    }
    int right = bounds_.x + bounds_.w;
    int w = std::max(0, right - cursorX_);
    SDL_Rect rect {cursorX_, bounds_.y, w, bounds_.h};
    cursorX_ = right;
    return rect;
  }

  int remainingWidth() const {
    return std::max(0, bounds_.x + bounds_.w - cursorX_);
  }

 private:
  SDL_Rect bounds_ {};
  int cursorX_ = 0;  // Advances rightward as children are placed
  int gap_ = 0;
};

// ── GridLayout ──────────────────────────────────────────────────────────────
// Divides a bounding rect into a uniform N-column × M-row grid.
// Each cell is the same size; cell() supports multi-column/row spans.
class GridLayout {
 public:
  GridLayout(SDL_Rect bounds, int columns, int rows, int gap = kLayoutPanelGap)
    : bounds_(snapRectToGrid(bounds)),
      columns_(std::max(1, columns)),
      rows_(std::max(1, rows)),
      gap_(std::max(0, gap)) {}

  // Returns the rectangle for the cell at (column, row), optionally spanning
  // multiple columns/rows. Clamps indices and spans to valid ranges.
  SDL_Rect cell(int column, int row, int colSpan = 1, int rowSpan = 1) const {
    column = std::clamp(column, 0, columns_ - 1);
    row = std::clamp(row, 0, rows_ - 1);
    colSpan = std::clamp(colSpan, 1, columns_ - column);
    rowSpan = std::clamp(rowSpan, 1, rows_ - row);

    int totalGapW = gap_ * (columns_ - 1);
    int totalGapH = gap_ * (rows_ - 1);
    int cellW = std::max(0, (bounds_.w - totalGapW) / columns_);
    int cellH = std::max(0, (bounds_.h - totalGapH) / rows_);
    int x = bounds_.x + column * (cellW + gap_);
    int y = bounds_.y + row * (cellH + gap_);
    int w = cellW * colSpan + gap_ * (colSpan - 1);
    int h = cellH * rowSpan + gap_ * (rowSpan - 1);
    return SDL_Rect {x, y, w, h};
  }

 private:
  SDL_Rect bounds_ {};
  int columns_ = 1;
  int rows_ = 1;
  int gap_ = 0;
};

// ── UITable ─────────────────────────────────────────────────────────────────
// A table layout with variable-width columns and fixed row height.
// Unlike GridLayout, each column can have a different width (specified
// explicitly), making it suitable for data tables with label + value columns.
class UITable {
 public:
  UITable(SDL_Rect bounds, std::vector<int> columnWidths, int rowHeight, int gap = kLayoutButtonGap)
    : bounds_(snapRectToGrid(bounds)),
      columnWidths_(std::move(columnWidths)),
      rowHeight_(std::max(0, rowHeight)),
      gap_(std::max(0, gap)) {}

  SDL_Rect cell(int rowIndex, int columnIndex) const {
    if (columnIndex < 0 || columnIndex >= static_cast<int>(columnWidths_.size())) {
      return SDL_Rect {};
    }
    int x = bounds_.x;
    for (int i = 0; i < columnIndex; ++i) {
      x += columnWidths_[i] + gap_;
    }
    int y = bounds_.y + rowIndex * (rowHeight_ + gap_);
    return SDL_Rect {x, y, columnWidths_[columnIndex], rowHeight_};
  }

 private:
  SDL_Rect bounds_ {};
  std::vector<int> columnWidths_;
  int rowHeight_ = 0;
  int gap_ = 0;
};
