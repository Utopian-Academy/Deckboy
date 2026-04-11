// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <vector>
#include "core/constants.hpp"

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

inline int snapUpToGrid(int value) {
  if (kLayoutSpacingUnit <= 1) {
    return value;
  }
  int down = snapDownToGrid(value);
  return down == value ? value : down + kLayoutSpacingUnit;
}

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
// ascender/descender cushion returned by TTF_FontHeight. Falls back to a
// conservative constant if the font is null so callers can use this at
// compile-time-ish layout sites without guarding.
inline int textLineHeight(TTF_Font* font) {
  return font ? TTF_FontHeight(font) : 18;
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

class VerticalLayout {
 public:
  explicit VerticalLayout(SDL_Rect bounds, int gap = kLayoutPanelGap)
    : bounds_(snapRectToGrid(bounds)),
      cursorY_(snapDownToGrid(bounds_.y)),
      gap_(std::max(0, gap)) {}

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
  int cursorY_ = 0;
  int gap_ = 0;
};

class HorizontalLayout {
 public:
  explicit HorizontalLayout(SDL_Rect bounds, int gap = kLayoutPanelGap)
    : bounds_(snapRectToGrid(bounds)),
      cursorX_(snapDownToGrid(bounds_.x)),
      gap_(std::max(0, gap)) {}

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
  int cursorX_ = 0;
  int gap_ = 0;
};

class GridLayout {
 public:
  GridLayout(SDL_Rect bounds, int columns, int rows, int gap = kLayoutPanelGap)
    : bounds_(snapRectToGrid(bounds)),
      columns_(std::max(1, columns)),
      rows_(std::max(1, rows)),
      gap_(std::max(0, gap)) {}

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
