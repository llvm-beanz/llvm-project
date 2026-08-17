//===- Tessellator.cpp - Fixed-function tessellator state/generation -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Tessellator.h"

#include "llvm/Support/ErrorHandling.h"

#include <algorithm>
#include <cmath>

using namespace feme::graphics;

namespace {

float clampFactor(float Factor, uint32_t MaxTessFactor) {
  return std::clamp(Factor, 1.0f, static_cast<float>(MaxTessFactor));
}

/// Whether any of \p Factors is `<= 0`, per `TessFactors`'s degenerate-patch
/// rule; \p Count is how many of the leading entries in \p Factors matter
/// for the domain being checked (an isoline reads none of `Inside`, a
/// triangle reads one, a quad reads two).
bool anyFactorCullsPatch(const float *Factors, size_t Count) {
  return std::any_of(Factors, Factors + Count,
                     [](float F) { return F <= 0.0f; });
}

/// Emits a triangle-list fan-free lattice covering a triangle domain,
/// subdivided uniformly into \p N rings of barycentric coordinates
/// `(i, j, k) / N` with `i + j + k == N` -- see Tessellator.h's scope note.
/// `Cw` selects the emitted triangles' index winding.
TessellatedPatch tessellateTriangleLattice(uint32_t N, bool Cw) {
  TessellatedPatch Patch;
  // Row `r` (0 at one corner, N at the opposite edge) holds `N - r + 1`
  // points; point (r, c) has barycentric coordinates
  // (N - r - c, c, r) / N.
  std::vector<std::vector<uint32_t>> RowStart(N + 1);
  for (uint32_t R = 0; R <= N; ++R) {
    for (uint32_t C = 0; C + R <= N; ++C) {
      RowStart[R].push_back(static_cast<uint32_t>(Patch.Points.size()));
      float I = static_cast<float>(N - R - C);
      float J = static_cast<float>(C);
      float K = static_cast<float>(R);
      Patch.Points.push_back({I / N, J / N, K / N});
    }
  }
  for (uint32_t R = 0; R < N; ++R) {
    for (uint32_t C = 0; C + R < N; ++C) {
      uint32_t A = RowStart[R][C];
      uint32_t B = RowStart[R][C + 1];
      uint32_t D = RowStart[R + 1][C];
      if (Cw) {
        Patch.Indices.insert(Patch.Indices.end(), {A, B, D});
      } else {
        Patch.Indices.insert(Patch.Indices.end(), {A, D, B});
      }
      // The "upward" triangle at this cell exists whenever a fourth lattice
      // point closes it on the next row.
      if (C + R + 1 < N) {
        uint32_t E = RowStart[R + 1][C + 1];
        if (Cw) {
          Patch.Indices.insert(Patch.Indices.end(), {B, E, D});
        } else {
          Patch.Indices.insert(Patch.Indices.end(), {B, D, E});
        }
      }
    }
  }
  return Patch;
}

TessellatedPatch tessellateIsoline(const TessFactors &Factors,
                                   TessPartitioning Partitioning,
                                   TessOutputPrimitive OutputPrimitive,
                                   uint32_t MaxTessFactor) {
  if (anyFactorCullsPatch(Factors.Edges.data(), 2))
    return {};

  // The isoline's line count (`u` axis) always rounds up, matching both
  // APIs' shared rule that only the per-line detail factor (`v` axis, below)
  // honors `Partitioning`.
  uint32_t Lines = static_cast<uint32_t>(
      std::ceil(clampFactor(Factors.Edges[0], MaxTessFactor)));
  uint32_t Segments =
      computeSegmentCount(Factors.Edges[1], Partitioning, MaxTessFactor);

  TessellatedPatch Patch;
  for (uint32_t I = 0; I < Lines; ++I) {
    float U = Lines > 1 ? static_cast<float>(I) / Lines : 0.0f;
    uint32_t RowStart = static_cast<uint32_t>(Patch.Points.size());
    for (uint32_t J = 0; J <= Segments; ++J) {
      float V = static_cast<float>(J) / Segments;
      Patch.Points.push_back({U, V, 0.0f});
    }
    if (OutputPrimitive != TessOutputPrimitive::Line)
      continue;
    for (uint32_t J = 0; J < Segments; ++J)
      Patch.Indices.insert(Patch.Indices.end(),
                           {RowStart + J, RowStart + J + 1});
  }
  return Patch;
}

TessellatedPatch tessellateTriangle(const TessFactors &Factors,
                                    TessPartitioning Partitioning,
                                    TessOutputPrimitive OutputPrimitive,
                                    uint32_t MaxTessFactor) {
  std::array<float, 4> All = {Factors.Inside[0], Factors.Edges[0],
                              Factors.Edges[1], Factors.Edges[2]};
  if (anyFactorCullsPatch(All.data(), All.size()))
    return {};

  // Per the file comment's scope note, the whole interior subdivides
  // uniformly from the largest of the interior and three edge factors
  // (rather than placing extra edge-matching vertices per edge and
  // stitching a crack-free fan to a coarser interior).
  float Factor = *std::max_element(All.begin(), All.end());
  uint32_t N = computeSegmentCount(Factor, Partitioning, MaxTessFactor);

  TessellatedPatch Patch = tessellateTriangleLattice(
      N, OutputPrimitive == TessOutputPrimitive::TriangleCw);
  if (OutputPrimitive == TessOutputPrimitive::Point)
    Patch.Indices.clear();
  return Patch;
}

TessellatedPatch tessellateQuad(const TessFactors &Factors,
                                TessPartitioning Partitioning,
                                TessOutputPrimitive OutputPrimitive,
                                uint32_t MaxTessFactor) {
  std::array<float, 6> All = {Factors.Inside[0], Factors.Inside[1],
                              Factors.Edges[0],  Factors.Edges[1],
                              Factors.Edges[2],  Factors.Edges[3]};
  if (anyFactorCullsPatch(All.data(), All.size()))
    return {};

  uint32_t Nu =
      computeSegmentCount(Factors.Inside[0], Partitioning, MaxTessFactor);
  uint32_t Nv =
      computeSegmentCount(Factors.Inside[1], Partitioning, MaxTessFactor);

  TessellatedPatch Patch;
  std::vector<std::vector<uint32_t>> Index(Nu + 1,
                                           std::vector<uint32_t>(Nv + 1));
  for (uint32_t I = 0; I <= Nu; ++I) {
    for (uint32_t J = 0; J <= Nv; ++J) {
      Index[I][J] = static_cast<uint32_t>(Patch.Points.size());
      Patch.Points.push_back(
          {static_cast<float>(I) / Nu, static_cast<float>(J) / Nv, 0.0f});
    }
  }
  if (OutputPrimitive == TessOutputPrimitive::Point)
    return Patch;

  bool Cw = OutputPrimitive == TessOutputPrimitive::TriangleCw;
  for (uint32_t I = 0; I < Nu; ++I) {
    for (uint32_t J = 0; J < Nv; ++J) {
      uint32_t A = Index[I][J];
      uint32_t B = Index[I + 1][J];
      uint32_t C = Index[I + 1][J + 1];
      uint32_t D = Index[I][J + 1];
      if (Cw) {
        Patch.Indices.insert(Patch.Indices.end(), {A, B, C, A, C, D});
      } else {
        Patch.Indices.insert(Patch.Indices.end(), {A, C, B, A, D, C});
      }
    }
  }
  return Patch;
}

} // namespace

uint32_t feme::graphics::computeSegmentCount(float Factor,
                                             TessPartitioning Partitioning,
                                             uint32_t MaxTessFactor) {
  float Clamped = clampFactor(Factor, MaxTessFactor);
  uint32_t Ceil = static_cast<uint32_t>(std::ceil(Clamped));
  uint32_t N;
  switch (Partitioning) {
  case TessPartitioning::Integer:
    N = Ceil;
    break;
  case TessPartitioning::Pow2: {
    N = 1;
    while (N < Ceil)
      N <<= 1;
    break;
  }
  case TessPartitioning::FractionalOdd:
    N = Ceil;
    if (N % 2 == 0)
      ++N;
    break;
  case TessPartitioning::FractionalEven:
    if (Clamped <= 1.0f) {
      N = 1;
      break;
    }
    N = Ceil;
    if (N % 2 != 0)
      ++N;
    break;
  }
  return std::min(N, MaxTessFactor);
}

TessellatedPatch feme::graphics::tessellate(TessellatorDomain Domain,
                                            TessPartitioning Partitioning,
                                            TessOutputPrimitive OutputPrimitive,
                                            const TessFactors &Factors,
                                            uint32_t MaxTessFactor) {
  switch (Domain) {
  case TessellatorDomain::Isoline:
    return tessellateIsoline(Factors, Partitioning, OutputPrimitive,
                             MaxTessFactor);
  case TessellatorDomain::Triangle:
    return tessellateTriangle(Factors, Partitioning, OutputPrimitive,
                              MaxTessFactor);
  case TessellatorDomain::Quad:
    return tessellateQuad(Factors, Partitioning, OutputPrimitive,
                          MaxTessFactor);
  }
  llvm_unreachable("unhandled TessellatorDomain");
}
