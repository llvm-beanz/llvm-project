//===- Tessellator.cpp - Fixed-function tessellator state/generation -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Tessellator.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"

#include <algorithm>
#include <cassert>
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

/// Appends one triangle's indices to \p Patch, honoring \p Cw the same way
/// every other triangle emitter in this file does: `Cw` keeps the operand
/// order, while the "Ccw" case swaps the last two operands.
void appendTriangle(TessellatedPatch &Patch, uint32_t A, uint32_t B, uint32_t C,
                    bool Cw) {
  if (Cw)
    Patch.Indices.insert(Patch.Indices.end(), {A, B, C});
  else
    Patch.Indices.insert(Patch.Indices.end(), {A, C, B});
}

/// A closed ring's point indices, grouped by which boundary edge each
/// point sits on (3 edges for a triangle domain, 4 for a quad), in walking
/// order and *excluding* each edge's trailing corner (shared with the next
/// edge's first point). Bridging by matching edge rather than by raw ring
/// position (`bridgeRingsByEdge`) keeps a shared corner's own position
/// aligned between two rings with different total vertex counts, instead
/// of letting one edge's extra vertices drift the whole ring out of phase
/// with the other.
using RingEdges = llvm::SmallVector<llvm::SmallVector<uint32_t, 8>, 4>;

/// Bridges two concentric, same-winding rings -- \p Outer (the patch's
/// per-edge boundary) and \p Inner (an interior core's own outer ring,
/// strictly inset from \p Outer) -- with a triangulated annulus, per
/// Tessellator.h's crack-free tessellation note. \p Outer and \p Inner
/// must have the same edge count. Each corresponding edge pair is walked
/// independently by proportional arc length (index / edge length),
/// starting and ending at the same pair of (approximately) shared corners,
/// always advancing whichever edge's next vertex comes first. Emits
/// exactly `Outer[e].size() + Inner[e].size()` triangles for each edge
/// `e`.
void bridgeRingsByEdge(TessellatedPatch &Patch, const RingEdges &Outer,
                       const RingEdges &Inner, bool Cw) {
  assert(Outer.size() == Inner.size() &&
         "bridged rings must have matching edge counts");
  size_t NumEdges = Outer.size();
  for (size_t E = 0; E != NumEdges; ++E) {
    llvm::ArrayRef<uint32_t> OuterEdge = Outer[E];
    llvm::ArrayRef<uint32_t> InnerEdge = Inner[E];
    uint32_t OuterNextCorner = Outer[(E + 1) % NumEdges].front();
    uint32_t InnerNextCorner = Inner[(E + 1) % NumEdges].front();
    size_t Mo = OuterEdge.size();
    size_t Mi = InnerEdge.size();
    size_t I = 0, J = 0;
    // Each step advances exactly one ring by one vertex and emits the
    // triangle spanning that step and the *other* ring's current vertex.
    // Once a ring is exhausted its "current vertex" is the shared corner
    // the next edge starts at, not a wrap back to this edge's own first
    // vertex: the annulus being triangulated runs from one shared corner
    // pair to the next, so wrapping would fold the last triangles of every
    // edge back across the strip and leave a crack behind them.
    while (I < Mo || J < Mi) {
      uint32_t OuterAt = I < Mo ? OuterEdge[I] : OuterNextCorner;
      uint32_t InnerAt = J < Mi ? InnerEdge[J] : InnerNextCorner;
      if (J >= Mi || (I < Mo && static_cast<double>(I + 1) / Mo <=
                                    static_cast<double>(J + 1) / Mi)) {
        uint32_t OuterNext = (I + 1 < Mo) ? OuterEdge[I + 1] : OuterNextCorner;
        appendTriangle(Patch, OuterAt, OuterNext, InnerAt, Cw);
        ++I;
        continue;
      }
      uint32_t InnerNext = (J + 1 < Mi) ? InnerEdge[J + 1] : InnerNextCorner;
      appendTriangle(Patch, InnerAt, OuterAt, InnerNext, Cw);
      ++J;
    }
  }
}

/// Appends a uniform triangle-domain lattice of resolution \p N
/// (barycentric coordinates `(i, j, k) / N`) to \p Patch, passing each raw
/// lattice point through \p Transform before storing it (the identity for
/// a standalone full-size triangle domain, or an inset-toward-centroid
/// transform for the crack-free core an outer per-edge boundary bridges
/// to -- see Tessellator.h). `Cw` selects the emitted triangles' winding.
/// Returns the lattice's own CCW outer-boundary ring (see `RingEdges`),
/// split at the same three corners `appendTriangleBoundaryRing` uses: a
/// caller that does not bridge a separate outer boundary can also use this
/// directly as the whole patch's boundary.
RingEdges
appendTriangleLattice(TessellatedPatch &Patch, uint32_t N, bool Cw,
                      llvm::function_ref<DomainPoint(DomainPoint)> Transform) {
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
      Patch.Points.push_back(Transform({I / N, J / N, K / N}));
    }
  }
  for (uint32_t R = 0; R < N; ++R) {
    for (uint32_t C = 0; C + R < N; ++C) {
      uint32_t A = RowStart[R][C];
      uint32_t B = RowStart[R][C + 1];
      uint32_t D = RowStart[R + 1][C];
      appendTriangle(Patch, A, B, D, Cw);
      // The "upward" triangle at this cell exists whenever a fourth lattice
      // point closes it on the next row.
      if (C + R + 1 < N) {
        uint32_t E = RowStart[R + 1][C + 1];
        appendTriangle(Patch, B, E, D, Cw);
      }
    }
  }

  RingEdges Edges(3);
  for (uint32_t C = 0; C < N; ++C)
    Edges[0].push_back(RowStart[0][C]);
  for (uint32_t R = 0; R < N; ++R)
    Edges[1].push_back(RowStart[R][N - R]);
  for (uint32_t R = N; R > 0; --R)
    Edges[2].push_back(RowStart[R][0]);
  return Edges;
}

/// Appends a triangle domain's per-edge boundary ring (no interior) to
/// \p Patch: \p E01/\p E12/\p E20 are the segment counts (each edge's own
/// `computeSegmentCount` result) for the `P0->P1`, `P1->P2`, `P2->P0`
/// edges, where `P0 = (1, 0, 0)`, `P1 = (0, 1, 0)`, `P2 = (0, 0, 1)`.
/// Returns the CCW ring (see `RingEdges`), one edge per entry, in walking
/// order starting at `P0`.
RingEdges appendTriangleBoundaryRing(TessellatedPatch &Patch, uint32_t E01,
                                     uint32_t E12, uint32_t E20) {
  RingEdges Edges(3);
  auto AddPoint = [&](unsigned Edge, float U, float V, float W) {
    Edges[Edge].push_back(static_cast<uint32_t>(Patch.Points.size()));
    Patch.Points.push_back({U, V, W});
  };
  for (uint32_t K = 0; K < E01; ++K) {
    float T = static_cast<float>(K) / E01;
    AddPoint(0, 1.0f - T, T, 0.0f);
  }
  for (uint32_t K = 0; K < E12; ++K) {
    float T = static_cast<float>(K) / E12;
    AddPoint(1, 0.0f, 1.0f - T, T);
  }
  for (uint32_t K = 0; K < E20; ++K) {
    float T = static_cast<float>(K) / E20;
    AddPoint(2, T, 0.0f, 1.0f - T);
  }
  return Edges;
}

/// Appends a quad domain's per-edge boundary ring (no interior) to
/// \p Patch: \p Ev0/\p Eu1/\p Ev1/\p Eu0 are the segment counts for the
/// `v == 0`, `u == 1`, `v == 1`, `u == 0` edges. Returns the CCW ring (see
/// `RingEdges`), one edge per entry, in walking order starting at
/// `(0, 0)`.
RingEdges appendQuadBoundaryRing(TessellatedPatch &Patch, uint32_t Ev0,
                                 uint32_t Eu1, uint32_t Ev1, uint32_t Eu0) {
  RingEdges Edges(4);
  auto AddPoint = [&](unsigned Edge, float U, float V) {
    Edges[Edge].push_back(static_cast<uint32_t>(Patch.Points.size()));
    Patch.Points.push_back({U, V, 0.0f});
  };
  for (uint32_t K = 0; K < Ev0; ++K)
    AddPoint(0, static_cast<float>(K) / Ev0, 0.0f);
  for (uint32_t K = 0; K < Eu1; ++K)
    AddPoint(1, 1.0f, static_cast<float>(K) / Eu1);
  for (uint32_t K = 0; K < Ev1; ++K)
    AddPoint(2, 1.0f - static_cast<float>(K) / Ev1, 1.0f);
  for (uint32_t K = 0; K < Eu0; ++K)
    AddPoint(3, 0.0f, 1.0f - static_cast<float>(K) / Eu0);
  return Edges;
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

  bool Cw = OutputPrimitive == TessOutputPrimitive::TriangleCw;
  // Edge `i` is opposite input control point `i` (`P0 = (1,0,0)`,
  // `P1 = (0,1,0)`, `P2 = (0,0,1)`): edge 2 is the `P0->P1` edge, edge 0 is
  // `P1->P2`, edge 1 is `P2->P0`.
  uint32_t E01 =
      computeSegmentCount(Factors.Edges[2], Partitioning, MaxTessFactor);
  uint32_t E12 =
      computeSegmentCount(Factors.Edges[0], Partitioning, MaxTessFactor);
  uint32_t E20 =
      computeSegmentCount(Factors.Edges[1], Partitioning, MaxTessFactor);
  uint32_t N =
      computeSegmentCount(Factors.Inside[0], Partitioning, MaxTessFactor);

  TessellatedPatch Patch;
  RingEdges OuterRing = appendTriangleBoundaryRing(Patch, E01, E12, E20);
  // Inset the uniform interior core strictly within the outer boundary --
  // never touching it -- by blending each lattice point toward the
  // centroid. The blend factor approaches 1 (no inset) as N grows, but is
  // always strictly less than 1, so a corner point (whose smallest
  // barycentric component is 0) still maps to a strictly positive one.
  float Alpha = 1.0f - 1.0f / static_cast<float>(N + 2);
  auto Inset = [Alpha](DomainPoint P) -> DomainPoint {
    constexpr float Third = 1.0f / 3.0f;
    return {Third + Alpha * (P.U - Third), Third + Alpha * (P.V - Third),
            Third + Alpha * (P.W - Third)};
  };
  RingEdges CoreRing = appendTriangleLattice(Patch, N, Cw, Inset);
  bridgeRingsByEdge(Patch, OuterRing, CoreRing, Cw);

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

  bool Cw = OutputPrimitive == TessOutputPrimitive::TriangleCw;
  // `Edges[0]`/`Edges[2]` are the `u == 0`/`u == 1` edges (varying over
  // `v`); `Edges[1]`/`Edges[3]` are the `v == 0`/`v == 1` edges (varying
  // over `u`), per TessFactors's own comment.
  uint32_t Eu0 =
      computeSegmentCount(Factors.Edges[0], Partitioning, MaxTessFactor);
  uint32_t Eu1 =
      computeSegmentCount(Factors.Edges[2], Partitioning, MaxTessFactor);
  uint32_t Ev0 =
      computeSegmentCount(Factors.Edges[1], Partitioning, MaxTessFactor);
  uint32_t Ev1 =
      computeSegmentCount(Factors.Edges[3], Partitioning, MaxTessFactor);
  uint32_t Nu =
      computeSegmentCount(Factors.Inside[0], Partitioning, MaxTessFactor);
  uint32_t Nv =
      computeSegmentCount(Factors.Inside[1], Partitioning, MaxTessFactor);

  TessellatedPatch Patch;
  RingEdges OuterRing = appendQuadBoundaryRing(Patch, Ev0, Eu1, Ev1, Eu0);

  // Inset the uniform interior core strictly within `[0, 1]^2`, the same
  // way the triangle domain insets its own core toward the centroid: the
  // margin shrinks toward 0 as `Nu`/`Nv` grow, but is always strictly
  // positive, so the core never touches the outer boundary.
  float MarginU = 0.5f / static_cast<float>(Nu + 1);
  float MarginV = 0.5f / static_cast<float>(Nv + 1);
  std::vector<std::vector<uint32_t>> Index(Nu + 1,
                                           std::vector<uint32_t>(Nv + 1));
  for (uint32_t I = 0; I <= Nu; ++I) {
    for (uint32_t J = 0; J <= Nv; ++J) {
      Index[I][J] = static_cast<uint32_t>(Patch.Points.size());
      float U = MarginU + (1.0f - 2.0f * MarginU) * static_cast<float>(I) / Nu;
      float V = MarginV + (1.0f - 2.0f * MarginV) * static_cast<float>(J) / Nv;
      Patch.Points.push_back({U, V, 0.0f});
    }
  }
  for (uint32_t I = 0; I < Nu; ++I) {
    for (uint32_t J = 0; J < Nv; ++J) {
      uint32_t A = Index[I][J];
      uint32_t B = Index[I + 1][J];
      uint32_t C = Index[I + 1][J + 1];
      uint32_t D = Index[I][J + 1];
      appendTriangle(Patch, A, B, C, Cw);
      appendTriangle(Patch, A, C, D, Cw);
    }
  }

  RingEdges CoreRing(4);
  for (uint32_t I = 0; I < Nu; ++I)
    CoreRing[0].push_back(Index[I][0]);
  for (uint32_t J = 0; J < Nv; ++J)
    CoreRing[1].push_back(Index[Nu][J]);
  for (uint32_t I = Nu; I > 0; --I)
    CoreRing[2].push_back(Index[I][Nv]);
  for (uint32_t J = Nv; J > 0; --J)
    CoreRing[3].push_back(Index[0][J]);
  bridgeRingsByEdge(Patch, OuterRing, CoreRing, Cw);

  if (OutputPrimitive == TessOutputPrimitive::Point)
    Patch.Indices.clear();
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
