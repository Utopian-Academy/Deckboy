// ============================================================================
// app_geometry.ipp — Cue and output geometry calculations.
//
// Provides geometric transformation functions for the output compositor:
//
//   edgeBlendAlphaForUv() — per-pixel alpha for soft edge blending between
//                            adjacent projectors (linear ramp on all four edges)
//   bilerpPoint()         — bilinear interpolation of four corner points
//                            (used for perspective warp / corner-pin mapping)
//
// Edge blending creates smooth fade zones at the edges of projected output,
// allowing overlapping projectors to produce a seamless combined image.
// The blend width is configurable per edge (left/right/top/bottom) as a
// fraction of the output dimensions.
//
// Corner-pin warping uses bilinear interpolation of user-defined corner
// positions to map the rectangular output onto an arbitrary quadrilateral,
// compensating for non-perpendicular projection surfaces.
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Rendered pixel size of the cue on the focused output at outputScale 1.0
  // (scale mode + crop applied). The operator-facing geometry unit is pixels:
  // finalPx = base × outputScaleX/Y. This is the same math the compositor's
  // drawTextureFitted / renderTextureWithCueGeometry path uses — keep them in
  // step. Returns {0, 0} when the size can't be derived.
  std::pair<double, double> cueBaseRenderSize(const Cue& cue) {
    int focOutIdx = std::clamp(project_.focusedOutputIndex, 0,
                               std::max(0, static_cast<int>(project_.outputs.size()) - 1));
    auto [targetW, targetH] = outputRenderSizeForOutput(focOutIdx);
    if (targetW <= 0 || targetH <= 0) {
      return {0.0, 0.0};
    }
    int srcW = std::max(1, cue.width > 0 ? cue.width : targetW);
    int srcH = std::max(1, cue.height > 0 ? cue.height : targetH);
    int cropLPx = static_cast<int>(std::lround(srcW * cue.cropLeft));
    int cropRPx = static_cast<int>(std::lround(srcW * cue.cropRight));
    int cropTPx = static_cast<int>(std::lround(srcH * cue.cropTop));
    int cropBPx = static_cast<int>(std::lround(srcH * cue.cropBottom));
    double croppedW = std::max(1, srcW - cropLPx - cropRPx);
    double croppedH = std::max(1, srcH - cropTPx - cropBPx);
    double baseScaleX = 1.0, baseScaleY = 1.0;
    if (cue.scaleMode == ScaleMode::Fit) {
      double fit = std::min(targetW / croppedW, targetH / croppedH);
      baseScaleX = baseScaleY = fit;
    } else if (cue.scaleMode == ScaleMode::Fill) {
      double fill = std::max(targetW / croppedW, targetH / croppedH);
      baseScaleX = baseScaleY = fill;
    } else if (cue.scaleMode == ScaleMode::Stretch) {
      baseScaleX = targetW / croppedW;
      baseScaleY = targetH / croppedH;
    }
    // Unscaled: baseScale stays 1.0
    return {croppedW * baseScaleX, croppedH * baseScaleY};
  }

  // Compute the edge blend alpha (0–255) for a given UV coordinate.
  // Each edge fades linearly from 0 at the edge to full alpha at the
  // blend boundary. When edges overlap at corners, the alphas multiply.
  static Uint8 edgeBlendAlphaForUv(const Deck& deck, float u, float v) {
    float ax = 1.0f;
    if (deck.edgeBlendLeft > 0.0001f && u < deck.edgeBlendLeft) {
      ax = std::min(ax, u / deck.edgeBlendLeft);
    }
    if (deck.edgeBlendRight > 0.0001f && u > 1.0f - deck.edgeBlendRight) {
      ax = std::min(ax, (1.0f - u) / deck.edgeBlendRight);
    }
    float ay = 1.0f;
    if (deck.edgeBlendTop > 0.0001f && v < deck.edgeBlendTop) {
      ay = std::min(ay, v / deck.edgeBlendTop);
    }
    if (deck.edgeBlendBottom > 0.0001f && v > 1.0f - deck.edgeBlendBottom) {
      ay = std::min(ay, (1.0f - v) / deck.edgeBlendBottom);
    }
    float alphaValue = std::clamp(ax * ay, 0.0f, 1.0f);
    return static_cast<Uint8>(std::lround(alphaValue * 255.0f));
  }

  static SDL_FPoint bilerpPoint(const SDL_FPoint& p0,
                                const SDL_FPoint& p1,
                                const SDL_FPoint& p2,
                                const SDL_FPoint& p3,
                                float s,
                                float t) {
    float oneMinusS = 1.0f - s;
    float oneMinusT = 1.0f - t;
    return SDL_FPoint {
      p0.x * oneMinusS * oneMinusT + p1.x * s * oneMinusT + p2.x * s * t + p3.x * oneMinusS * t,
      p0.y * oneMinusS * oneMinusT + p1.y * s * oneMinusT + p2.y * s * t + p3.y * oneMinusS * t
    };
  }

  // Draw a quad as a SUBDIVIDED mesh so the edge-feather ramp is evaluated
  // across the surface instead of only at its four corners.
  //
  // This exists because of a real defect: SDL interpolates vertex colour
  // linearly, so feeding a 4-vertex quad the corner alphas turned a 10%-wide
  // edge ramp into a fade across the ENTIRE image. Every path except
  // perspective warp (which already subdivides) had that bug, which meant edge
  // blending never actually produced an edge blend on an ordinary output.
  // Same mesh idea as renderPerspectiveWarp, minus the projective correction —
  // an unwarped quad is affine, so plain bilerp is exact here.
  static bool renderFeatheredQuad(SDL_Renderer* renderer,
                                  SDL_Texture* texture,
                                  const Deck& deck,
                                  const SDL_FPoint& uvTL,
                                  const SDL_FPoint& uvTR,
                                  const SDL_FPoint& uvBR,
                                  const SDL_FPoint& uvBL,
                                  const SDL_FPoint& p0,
                                  const SDL_FPoint& p1,
                                  const SDL_FPoint& p2,
                                  const SDL_FPoint& p3) {
    constexpr int kCols = 24;
    constexpr int kRows = 24;
    std::vector<SDL_Vertex> vertices;
    vertices.resize(static_cast<size_t>(kCols + 1) * static_cast<size_t>(kRows + 1));
    std::vector<int> indices;
    indices.reserve(static_cast<size_t>(kCols * kRows * 6));

    size_t vertexIndex = 0;
    for (int row = 0; row <= kRows; ++row) {
      float t = static_cast<float>(row) / static_cast<float>(kRows);
      for (int col = 0; col <= kCols; ++col) {
        float s = static_cast<float>(col) / static_cast<float>(kCols);
        SDL_FPoint position = bilerpPoint(p0, p1, p2, p3, s, t);
        SDL_FPoint texCoord = bilerpPoint(uvTL, uvTR, uvBR, uvBL, s, t);
        // s/t are SCREEN-space parameters, so the feather always follows the
        // output's own edges regardless of how the uv corners were permuted
        // for orientation.
        float alpha = static_cast<float>(edgeBlendAlphaForUv(deck, s, t)) / 255.0f;
        vertices[vertexIndex++] =
          SDL_Vertex {position, SDL_FColor {1.0f, 1.0f, 1.0f, alpha}, texCoord};
      }
    }

    for (int row = 0; row < kRows; ++row) {
      for (int col = 0; col < kCols; ++col) {
        int rowBase = row * (kCols + 1);
        int nextRowBase = (row + 1) * (kCols + 1);
        int tl = rowBase + col;
        int tr = tl + 1;
        int bl = nextRowBase + col;
        int br = bl + 1;
        indices.push_back(tl);
        indices.push_back(tr);
        indices.push_back(br);
        indices.push_back(tl);
        indices.push_back(br);
        indices.push_back(bl);
      }
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return SDL_RenderGeometry(renderer, texture,
                              vertices.data(), static_cast<int>(vertices.size()),
                              indices.data(), static_cast<int>(indices.size()));
  }

  // ---------------------------------------------------------------------------
  // renderDisplacementMesh - the picture as a landscape of itself.
  //
  // Deckboy composites in 2D: every cue is a textured quad. This is the one
  // place that is not true. The quad becomes a grid, each vertex is pushed out
  // of the plane by the brightness of the picture at that point, and the whole
  // surface is turned under a viewpoint and projected back.
  //
  // It is REAL GEOMETRY. SDL_RenderGeometry already draws arbitrary textured
  // triangles -- the perspective warp uses it -- so this needs no new
  // dependency, no shader and no depth buffer. What it does need is the
  // brightness field, which is why it is handed a small luma grid sampled from
  // the frame rather than reading the texture back every frame.
  //
  // Two things are deliberately absent:
  //
  //   * No depth SORTING. Triangles are emitted back-to-front by row, which is
  //     correct for a heightfield seen from above the horizon -- the case this
  //     is for. Tilt past vertical and it will self-overlap; that is a knob an
  //     operator can see and hear about rather than a per-triangle sort that
  //     would cost more than the mesh.
  //   * No lighting. relight already does that as an effect, on the pixels,
  //     where it composes with everything else in the stack.
  static bool renderDisplacementMesh(SDL_Renderer* renderer,
                                     SDL_Texture* texture,
                                     const SDL_Rect& target,
                                     const std::vector<float>& luma,
                                     int lumaW, int lumaH,
                                     float height, float tiltX, float tiltY,
                                     int grid, float alpha) {
    if (!renderer || !texture || luma.empty() || lumaW < 2 || lumaH < 2) {
      return false;
    }
    const int cells = std::clamp(grid, 8, 160);
    const int verts = cells + 1;

    // The viewpoint: pitch, then yaw, then a weak perspective divide. Weak on
    // purpose -- a full projection at this field of view makes the near edge
    // of the surface race off the screen the moment it tilts.
    const double px = std::clamp(static_cast<double>(tiltX), -1.0, 1.0) * 0.9;
    const double py = std::clamp(static_cast<double>(tiltY), -1.0, 1.0) * 0.9;
    const double cx = std::cos(px), sx = std::sin(px);
    const double cy = std::cos(py), sy = std::sin(py);
    const double lift = std::clamp(static_cast<double>(height), 0.0, 1.0);

    const double halfW = target.w * 0.5;
    const double halfH = target.h * 0.5;
    const double midX = target.x + halfW;
    const double midY = target.y + halfH;

    std::vector<SDL_Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(verts) * verts);
    const SDL_FColor tint {1.0f, 1.0f, 1.0f, std::clamp(alpha, 0.0f, 1.0f)};

    for (int row = 0; row < verts; ++row) {
      const double v = static_cast<double>(row) / cells;
      for (int col = 0; col < verts; ++col) {
        const double u = static_cast<double>(col) / cells;
        // Brightness at this vertex, bilinear from the small luma grid so the
        // surface is smooth rather than stepped at the sample spacing.
        const double fx = u * (lumaW - 1);
        const double fy = v * (lumaH - 1);
        const int x0 = std::clamp(static_cast<int>(fx), 0, lumaW - 1);
        const int y0 = std::clamp(static_cast<int>(fy), 0, lumaH - 1);
        const int x1 = std::min(x0 + 1, lumaW - 1);
        const int y1 = std::min(y0 + 1, lumaH - 1);
        const double tx = fx - x0, ty = fy - y0;
        const double l00 = luma[static_cast<std::size_t>(y0) * lumaW + x0];
        const double l10 = luma[static_cast<std::size_t>(y0) * lumaW + x1];
        const double l01 = luma[static_cast<std::size_t>(y1) * lumaW + x0];
        const double l11 = luma[static_cast<std::size_t>(y1) * lumaW + x1];
        const double l = (l00 * (1 - tx) + l10 * tx) * (1 - ty)
                       + (l01 * (1 - tx) + l11 * tx) * ty;

        // Model space, centred, with brightness as height.
        double mx = (u - 0.5) * 2.0;
        double my = (v - 0.5) * 2.0;
        double mz = (l - 0.5) * 2.0 * lift;

        // Pitch about X, then yaw about Y.
        double ry = my * cx - mz * sx;
        double rz = my * sx + mz * cx;
        double rx = mx * cy + rz * sy;
        rz = -mx * sy + rz * cy;

        // Weak perspective. The 2.6 is a camera distance in model units: any
        // closer and the divide swings the near row across the whole frame.
        const double persp = 2.6 / (2.6 + rz);
        SDL_Vertex vert;
        vert.position.x = static_cast<float>(midX + rx * halfW * persp);
        vert.position.y = static_cast<float>(midY + ry * halfH * persp);
        vert.tex_coord.x = static_cast<float>(u);
        vert.tex_coord.y = static_cast<float>(v);
        vert.color = tint;
        vertices.push_back(vert);
      }
    }

    std::vector<int> indices;
    indices.reserve(static_cast<std::size_t>(cells) * cells * 6);
    for (int row = 0; row < cells; ++row) {
      for (int col = 0; col < cells; ++col) {
        const int tl = row * verts + col;
        const int tr = tl + 1;
        const int bl = (row + 1) * verts + col;
        const int br = bl + 1;
        indices.push_back(tl); indices.push_back(tr); indices.push_back(br);
        indices.push_back(tl); indices.push_back(br); indices.push_back(bl);
      }
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return SDL_RenderGeometry(renderer, texture,
                              vertices.data(), static_cast<int>(vertices.size()),
                              indices.data(), static_cast<int>(indices.size()));
  }

  static bool solve8x8(double matrix[8][9]) {
    for (int pivot = 0; pivot < 8; ++pivot) {
      int pivotRow = pivot;
      double pivotAbs = std::abs(matrix[pivot][pivot]);
      for (int row = pivot + 1; row < 8; ++row) {
        double candidateAbs = std::abs(matrix[row][pivot]);
        if (candidateAbs > pivotAbs) {
          pivotAbs = candidateAbs;
          pivotRow = row;
        }
      }
      if (pivotAbs < 1.0e-9) {
        return false;
      }
      if (pivotRow != pivot) {
        for (int col = pivot; col <= 8; ++col) {
          std::swap(matrix[pivot][col], matrix[pivotRow][col]);
        }
      }
      double invPivot = 1.0 / matrix[pivot][pivot];
      for (int col = pivot; col <= 8; ++col) {
        matrix[pivot][col] *= invPivot;
      }
      for (int row = 0; row < 8; ++row) {
        if (row == pivot) {
          continue;
        }
        double factor = matrix[row][pivot];
        if (std::abs(factor) < 1.0e-12) {
          continue;
        }
        for (int col = pivot; col <= 8; ++col) {
          matrix[row][col] -= factor * matrix[pivot][col];
        }
      }
    }
    return true;
  }

  static bool computeProjectiveUvCoefficients(const SDL_FPoint& p0,
                                              const SDL_FPoint& p1,
                                              const SDL_FPoint& p2,
                                              const SDL_FPoint& p3,
                                              const SDL_FPoint& uvTL,
                                              const SDL_FPoint& uvTR,
                                              const SDL_FPoint& uvBR,
                                              const SDL_FPoint& uvBL,
                                              std::array<double, 8>& coeffs) {
    const SDL_FPoint positions[4] {p0, p1, p2, p3};
    const SDL_FPoint texCoords[4] {uvTL, uvTR, uvBR, uvBL};
    double matrix[8][9] {};
    for (int i = 0; i < 4; ++i) {
      double x = static_cast<double>(positions[i].x);
      double y = static_cast<double>(positions[i].y);
      double u = static_cast<double>(texCoords[i].x);
      double v = static_cast<double>(texCoords[i].y);
      int row = i * 2;
      matrix[row + 0][0] = x;
      matrix[row + 0][1] = y;
      matrix[row + 0][2] = 1.0;
      matrix[row + 0][3] = 0.0;
      matrix[row + 0][4] = 0.0;
      matrix[row + 0][5] = 0.0;
      matrix[row + 0][6] = -u * x;
      matrix[row + 0][7] = -u * y;
      matrix[row + 0][8] = u;

      matrix[row + 1][0] = 0.0;
      matrix[row + 1][1] = 0.0;
      matrix[row + 1][2] = 0.0;
      matrix[row + 1][3] = x;
      matrix[row + 1][4] = y;
      matrix[row + 1][5] = 1.0;
      matrix[row + 1][6] = -v * x;
      matrix[row + 1][7] = -v * y;
      matrix[row + 1][8] = v;
    }
    if (!solve8x8(matrix)) {
      return false;
    }
    for (int i = 0; i < 8; ++i) {
      coeffs[static_cast<size_t>(i)] = matrix[i][8];
    }
    return true;
  }

  static bool renderPerspectiveWarp(SDL_Renderer* renderer,
                                    SDL_Texture* texture,
                                    const Deck& deck,
                                    const SDL_FPoint& uvTL,
                                    const SDL_FPoint& uvTR,
                                    const SDL_FPoint& uvBR,
                                    const SDL_FPoint& uvBL,
                                    const SDL_FPoint& p0,
                                    const SDL_FPoint& p1,
                                    const SDL_FPoint& p2,
                                    const SDL_FPoint& p3,
                                    bool hasBlend) {
    if (!renderer || !texture) {
      return false;
    }
    std::array<double, 8> coeffs {};
    if (!computeProjectiveUvCoefficients(p0, p1, p2, p3, uvTL, uvTR, uvBR, uvBL, coeffs)) {
      return false;
    }

    constexpr int kCols = 18;
    constexpr int kRows = 18;
    std::vector<SDL_Vertex> vertices;
    vertices.resize(static_cast<size_t>(kCols + 1) * static_cast<size_t>(kRows + 1));
    std::vector<int> indices;
    indices.reserve(static_cast<size_t>(kCols * kRows * 6));

    float minU = std::min(std::min(uvTL.x, uvTR.x), std::min(uvBR.x, uvBL.x));
    float maxU = std::max(std::max(uvTL.x, uvTR.x), std::max(uvBR.x, uvBL.x));
    float minV = std::min(std::min(uvTL.y, uvTR.y), std::min(uvBR.y, uvBL.y));
    float maxV = std::max(std::max(uvTL.y, uvTR.y), std::max(uvBR.y, uvBL.y));

    size_t vertexIndex = 0;
    for (int row = 0; row <= kRows; ++row) {
      float t = static_cast<float>(row) / static_cast<float>(kRows);
      for (int col = 0; col <= kCols; ++col) {
        float s = static_cast<float>(col) / static_cast<float>(kCols);
        SDL_FPoint position = bilerpPoint(p0, p1, p2, p3, s, t);
        double x = static_cast<double>(position.x);
        double y = static_cast<double>(position.y);
        double denom = coeffs[6] * x + coeffs[7] * y + 1.0;
        SDL_FPoint texCoord = bilerpPoint(uvTL, uvTR, uvBR, uvBL, s, t);
        if (std::abs(denom) > 1.0e-6) {
          texCoord.x = static_cast<float>((coeffs[0] * x + coeffs[1] * y + coeffs[2]) / denom);
          texCoord.y = static_cast<float>((coeffs[3] * x + coeffs[4] * y + coeffs[5]) / denom);
        }
        texCoord.x = std::clamp(texCoord.x, minU, maxU);
        texCoord.y = std::clamp(texCoord.y, minV, maxV);
        Uint8 alpha = hasBlend ? edgeBlendAlphaForUv(deck, s, t) : 255;
        // SDL3: SDL_Vertex carries a float SDL_FColor.
        vertices[vertexIndex++] = SDL_Vertex {position, SDL_FColor {1.0f, 1.0f, 1.0f, static_cast<float>(alpha) / 255.0f}, texCoord};
      }
    }

    for (int row = 0; row < kRows; ++row) {
      for (int col = 0; col < kCols; ++col) {
        int rowBase = row * (kCols + 1);
        int nextRowBase = (row + 1) * (kCols + 1);
        int tl = rowBase + col;
        int tr = tl + 1;
        int bl = nextRowBase + col;
        int br = bl + 1;
        indices.push_back(tl);
        indices.push_back(tr);
        indices.push_back(br);
        indices.push_back(tl);
        indices.push_back(br);
        indices.push_back(bl);
      }
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return SDL_RenderGeometry(
      renderer,
      texture,
      vertices.data(),
      static_cast<int>(vertices.size()),
      indices.data(),
      static_cast<int>(indices.size()));
  }
