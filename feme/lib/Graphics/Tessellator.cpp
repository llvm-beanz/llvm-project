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

/// Appends one concentric inner triangle's own boundary ring (see the
/// Vulkan/GLSL spec's "Triangle Tessellation" section: a set of concentric
/// equilateral triangles, each one inset from the previous by a uniform
/// "perpendicular projection" corner construction) to \p Patch, at
/// uniform per-edge resolution \p Resolution and homothety scale
/// \p Scale, both already resolved by the caller (`tessellateTriangle`'s
/// own concentric-ring loop, which derives \p Scale as the cumulative
/// product of each ring's own `(1 - 2 / n)` shrink factor -- the closed
/// form the spec's own corner-projection construction reduces to, since
/// composing several homotheties centered on the same point (the
/// triangle's centroid) is itself a homothety with the product scale).
/// \p Scale is always in `(0, 1)`, so the ring is always strictly inset
/// from the true, unscaled reference triangle. No interior triangles are
/// generated here: every concentric ring's own interior is filled either
/// by bridging to the next ring inward (`bridgeRingsByEdge`) or, for the
/// two terminal cases the spec describes, directly by `tessellateTriangle`
/// itself (a single triangle when \p Resolution is 1, a center-point fan
/// via `fanRingToPoint` when a further ring would be degenerate). Returns
/// the CCW ring (see `RingEdges`), in the same per-edge/per-corner
/// convention `appendTriangleBoundaryRing` uses.
RingEdges appendTriangleRingBoundary(TessellatedPatch &Patch,
                                     uint32_t Resolution, float Scale) {
  constexpr float Third = 1.0f / 3.0f;
  RingEdges Edges(3);
  auto AddPoint = [&](unsigned Edge, float U, float V, float W) {
    Edges[Edge].push_back(static_cast<uint32_t>(Patch.Points.size()));
    Patch.Points.push_back({Third + Scale * (U - Third),
                            Third + Scale * (V - Third),
                            Third + Scale * (W - Third)});
  };
  for (uint32_t K = 0; K < Resolution; ++K) {
    float T = static_cast<float>(K) / Resolution;
    AddPoint(0, 1.0f - T, T, 0.0f);
  }
  for (uint32_t K = 0; K < Resolution; ++K) {
    float T = static_cast<float>(K) / Resolution;
    AddPoint(1, 0.0f, 1.0f - T, T);
  }
  for (uint32_t K = 0; K < Resolution; ++K) {
    float T = static_cast<float>(K) / Resolution;
    AddPoint(2, T, 0.0f, 1.0f - T);
  }
  return Edges;
}

/// Fans every vertex of \p Ring's own boundary (walked corner to corner,
/// same convention `bridgeRingsByEdge` uses) to a single, already-appended
/// \p Center point, emitting one triangle per boundary segment -- the
/// spec's own degenerate-inner-ring rule ("If the innermost triangle is
/// degenerate (i.e., a point), the triangle containing it is subdivided
/// into six triangles by connecting each of the six vertices on that
/// triangle with the center point", generalized here to \p Ring's own
/// point count rather than assuming exactly six, since the same rule also
/// covers the "first (and only) inner triangle is degenerate" case, which
/// fans the real, independently-subdivided outer boundary directly instead
/// of a uniform six-vertex ring). Each triangle's winding matches
/// `bridgeRingsByEdge`'s own "advance the outer ring" case exactly (the
/// same `(OuterAt, OuterNext, InnerAt)` operand order, with \p Center
/// standing in for a fully degenerate inner ring), so a fan appended after
/// bridging \p Ring to its own outer neighbor stays crack-free.
void fanRingToPoint(TessellatedPatch &Patch, const RingEdges &Ring,
                    uint32_t Center, bool Cw) {
  size_t NumEdges = Ring.size();
  for (size_t E = 0; E != NumEdges; ++E) {
    llvm::ArrayRef<uint32_t> Edge = Ring[E];
    uint32_t NextCorner = Ring[(E + 1) % NumEdges].front();
    for (size_t I = 0; I < Edge.size(); ++I) {
      uint32_t A = Edge[I];
      uint32_t B = (I + 1 < Edge.size()) ? Edge[I + 1] : NextCorner;
      appendTriangle(Patch, A, B, Center, Cw);
    }
  }
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

  // (Roadmap L24(b)) The line count (`Edges[0]`, density) always rounds
  // up, matching both APIs' shared rule that only the per-line detail
  // factor (`Edges[1]`, segments) honors `Partitioning`. Line density
  // becomes each generated point's own `V` coordinate below, and per-line
  // detail becomes `U` -- the opposite of what their names ("line count"
  // first, "segments" second) might suggest, but matching the real
  // `SV_DomainLocation` convention `DomainPoint`'s own doc comment
  // describes (`U` = along-line position, `V` = which line).
  uint32_t Lines = static_cast<uint32_t>(
      std::ceil(clampFactor(Factors.Edges[0], MaxTessFactor)));
  uint32_t Segments =
      computeSegmentCount(Factors.Edges[1], Partitioning, MaxTessFactor);

  TessellatedPatch Patch;
  for (uint32_t I = 0; I < Lines; ++I) {
    // (Roadmap L24(b)) Which line this row belongs to -- `DomainPoint::V`,
    // not `U`; an earlier version of this function stored it as `U`
    // instead, silently swapping the two coordinates' real meaning (see
    // `DomainPoint`'s own doc comment) and producing an all-zero
    // along-line coordinate for every point of a single-line patch, since
    // `Lines == 1` always makes this a constant `0.0f`.
    float LineIndex = Lines > 1 ? static_cast<float>(I) / Lines : 0.0f;
    uint32_t RowStart = static_cast<uint32_t>(Patch.Points.size());
    for (uint32_t J = 0; J <= Segments; ++J) {
      // (Roadmap L24(b)) The position along this one line -- `DomainPoint::U`.
      float AlongLine = static_cast<float>(J) / Segments;
      Patch.Points.push_back({AlongLine, LineIndex, 0.0f});
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

  if (E01 == 1 && E12 == 1 && E20 == 1 && N == 1) {
    // (roadmap H7x) At the minimum, fully unsubdivided factor (every edge
    // and the interior both at 1 segment), the general inset+bridge path
    // below still synthesizes a 7-triangle core+annulus split out of this
    // single triangle, purely for its own crack-avoidance bookkeeping (see
    // the `Inset` comment below). That split is invisible to ordinary
    // linear position/varying interpolation (barycentric interpolation is
    // affine, so any consistent subdivision reproduces it exactly), but it
    // is *not* invisible to `gl_CullDistance`'s whole-*primitive* culling
    // rule (Vulkan spec: a primitive is discarded outright if some one
    // cull-distance index is negative at *every one* of its own vertices).
    // A synthetic sub-triangle whose 3 vertices all happen to land near
    // one real edge of the un-subdivided triangle can satisfy that
    // all-negative test even when the *real*, un-subdivided triangle's own
    // 3 control points do not (e.g. two adjacent corners share a negative
    // cull distance while the third does not) -- spuriously culling a
    // sliver along that edge that a non-subdividing tessellator (or any
    // conformant implementation that honors "tess factor 1 needs no
    // subdivision") would render correctly. This was
    // `dEQP-VK.clipping.user_defined.clip_cull_distance.{vert_tess,
    // vert_tess_geom}.*_fragmentshader_read`'s own remaining failure: a
    // full-width band of spuriously culled sub-triangles along the
    // `gl_Position.y == -1` patch edge shared by every one of the test's 8
    // bars. Emitting the real, single triangle directly here avoids ever
    // creating that spurious internal primitive boundary in the first
    // place.
    TessellatedPatch Patch;
    Patch.Points = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    appendTriangle(Patch, 0, 1, 2, Cw);
    if (OutputPrimitive == TessOutputPrimitive::Point)
      Patch.Indices.clear();
    return Patch;
  }

  // (Roadmap L220) This implements the Vulkan/GLSL spec's own "Triangle
  // Tessellation" algorithm literally -- a set of concentric equilateral
  // triangles shrinking toward the centroid, each one's own per-edge
  // resolution derived from the *previous* ring's by subtracting 2 -- not
  // the single inset-toward-centroid uniform core an earlier version of
  // this function used (see git history/roadmap L220 for that approach's
  // own now-fixed non-conformance). `computeSegmentCount`'s ordinary
  // rounding rule directly gives the first ring's own resolution, *except*
  // when the inside factor rounds to exactly 1 while some outer edge does
  // not (the `E01 == 1 && ... && N == 1` case just above already covers
  // the case where every one of those *also* rounds to 1): the spec's own
  // "1 + epsilon" rule then applies, since a bare `N == 1` here would
  // otherwise mean "no interior subdivision at all" even though the outer
  // boundary itself is subdivided -- an invalid, edge-touching interior
  // ring the spec explicitly disallows. `SpacingFractionalOdd` resolves
  // that epsilon to a 3-segment ring (matching its own odd-rounding rule);
  // every other partitioning (including `Pow2`, a Direct3D-only mode with
  // no equivalent spec text of its own) resolves it to 2 segments, `Pow2`'s
  // own smallest-representable-power-of-two-above-1 value.
  uint32_t N0;
  if (N == 1)
    N0 = Partitioning == TessPartitioning::FractionalOdd ? 3 : 2;
  else
    N0 = N;

  TessellatedPatch Patch;
  RingEdges OuterRing = appendTriangleBoundaryRing(Patch, E01, E12, E20);

  // Walks the concentric-ring sequence the spec describes: `CurrentN` is
  // the resolution used to build the *next* ring from `PrevRing` (starting
  // at the real outer boundary, using `N0` from the inside factor); each
  // ring's own resolution then becomes the next iteration's `CurrentN`,
  // exactly matching the spec's own recursive "using the generated
  // triangle as an outer triangle" step. `CumulativeScale` is the closed
  // form of the spec's per-corner "perpendicular projection" construction
  // (see `appendTriangleRingBoundary`'s own comment): composing each
  // ring's own homothety-toward-centroid transform is itself a homothety
  // with the product of their individual `(1 - 2 / CurrentN)` scale
  // factors.
  RingEdges PrevRing = OuterRing;
  uint32_t CurrentN = N0;
  float CumulativeScale = 1.0f;
  for (;;) {
    CumulativeScale *= 1.0f - 2.0f / static_cast<float>(CurrentN);
    if (CurrentN == 2) {
      // The next ring is degenerate -- a single point at the centroid
      // (`CumulativeScale` is exactly 0 here, so every ring built from it
      // would collapse to the centroid regardless of its own resolution).
      // Per spec, `PrevRing`'s own boundary (the real outer boundary
      // itself, when this is reached directly from `N0 == 2`, or an
      // already-built concentric ring's own six-vertex boundary when
      // reached after at least one full ring) is fanned directly to this
      // point instead of ever materializing that degenerate ring.
      uint32_t Center = static_cast<uint32_t>(Patch.Points.size());
      constexpr float Third = 1.0f / 3.0f;
      Patch.Points.push_back({Third, Third, Third});
      fanRingToPoint(Patch, PrevRing, Center, Cw);
      break;
    }
    // Resolution 1 (`CurrentN == 3`) is the spec's own "edges of the inner
    // triangle are not subdivided" terminal case: a single, un-subdivided
    // triangle, added to the output directly rather than bridged to a
    // still-smaller ring.
    uint32_t Resolution = CurrentN == 3 ? 1 : CurrentN - 2;
    RingEdges NewRing =
        appendTriangleRingBoundary(Patch, Resolution, CumulativeScale);
    bridgeRingsByEdge(Patch, PrevRing, NewRing, Cw);
    if (Resolution == 1) {
      appendTriangle(Patch, NewRing[0][0], NewRing[1][0], NewRing[2][0], Cw);
      break;
    }
    PrevRing = NewRing;
    CurrentN = Resolution;
  }

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
  // (roadmap L219) When both inside factors round down to their own
  // minimum of 1 segment (no interior subdivision along either axis), the
  // quad domain needs no separate interior core at all -- mirroring
  // `tessellateTriangle`'s own fully-unsubdivided special case above.
  // Per the Vulkan/GLSL tessellation spec (matched by dEQP's own
  // `generateReferenceTessCoords` precondition assert, which requires
  // `inner[0] == 1 && inner[1] == 1` to imply *every* outer edge is also
  // 1), this can only happen alongside every outer edge also degenerating
  // to a single segment, so `OuterRing` already holds exactly the 4 quad
  // corners with no other boundary points to bridge to. The general
  // inset+bridge path below unconditionally forced at least one interior
  // ring even here (`Nu`/`Nv`'s own `max(1u, ...)` clamp, needed to avoid
  // a divide-by-zero in the general case), spuriously synthesizing 4
  // extra interior points at `(0.25, 0.25)`/`(0.25, 0.75)`/`(0.75, 0.25)`/
  // `(0.75, 0.75)` that a real tessellator's own equal-spacing algorithm
  // never produces at this tessellation level -- found via a real
  // `dEQP-VK.tessellation.tesscoord.quads_equal_spacing` reproduction
  // (`inner: { 1, 1 }, outer: { 1, 1, 1, 1 }`).
  if (computeSegmentCount(Factors.Inside[0], Partitioning, MaxTessFactor) ==
          1 &&
      computeSegmentCount(Factors.Inside[1], Partitioning, MaxTessFactor) ==
          1) {
    TessellatedPatch Patch;
    RingEdges OuterRing = appendQuadBoundaryRing(Patch, Ev0, Eu1, Ev1, Eu0);
    uint32_t P0 = OuterRing[0][0], P1 = OuterRing[1][0], P2 = OuterRing[2][0],
             P3 = OuterRing[3][0];
    appendTriangle(Patch, P0, P1, P2, Cw);
    appendTriangle(Patch, P0, P2, P3, Cw);
    if (OutputPrimitive == TessOutputPrimitive::Point)
      Patch.Indices.clear();
    return Patch;
  }
  // (Roadmap L82.) `computeSegmentCount` returns the number of segments
  // spanning the *whole* `[0, 1]` axis (matching its use for edges, above,
  // where the boundary ring legitimately needs a point at every one of
  // those segment endpoints, including `0`/`1` themselves). The interior
  // core lattice built below is different: it is always inset strictly
  // *within* the boundary (see the `Margin` comment below) and never
  // touches it, so its own division count is the number of strictly
  // *interior* grid lines the inside tessellation factor implies -- one
  // fewer than the whole-axis segment count (an inside factor of `N`
  // divides the axis into `N` segments, leaving `N - 1` interior lattice
  // lines, exactly as for the boundary edges' own segment endpoints).
  // Using the whole-axis segment count directly here (the pre-fix
  // behavior) generated a spurious *extra* interior ring: for the common
  // `Inside == Edges == 2` case this produced core lattice points at
  // `U/V == 1/6, 1/2, 5/6` instead of the correct `U/V == 0.25, 0.75`,
  // introducing a non-dyadic (`1/6`) fraction that cannot be represented
  // exactly in `float32` -- an unrecoverable 1-ULP error, no matter how
  // precisely the rest of the pipeline computes with it. Clamping to a
  // minimum of `1` preserves the existing degenerate single-ring behavior
  // when the inside factor is already at its own minimum of `1`.
  uint32_t Nu = std::max(
      1u,
      computeSegmentCount(Factors.Inside[0], Partitioning, MaxTessFactor) -
          1);
  uint32_t Nv = std::max(
      1u,
      computeSegmentCount(Factors.Inside[1], Partitioning, MaxTessFactor) -
          1);

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
