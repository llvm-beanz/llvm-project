//===- TessellatorTest.cpp - Tests for feme::graphics::tessellate --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Tessellator.h"

#include "llvm/ADT/STLExtras.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>

using namespace feme::graphics;

namespace {

constexpr float Epsilon = 1e-5f;

TEST(TessellatorTest, IntegerPartitioningRoundsUp) {
  EXPECT_EQ(computeSegmentCount(1.0f, TessPartitioning::Integer), 1u);
  EXPECT_EQ(computeSegmentCount(3.2f, TessPartitioning::Integer), 4u);
  EXPECT_EQ(computeSegmentCount(4.0f, TessPartitioning::Integer), 4u);
}

TEST(TessellatorTest, Pow2PartitioningRoundsToAPowerOfTwo) {
  EXPECT_EQ(computeSegmentCount(1.0f, TessPartitioning::Pow2), 1u);
  EXPECT_EQ(computeSegmentCount(3.0f, TessPartitioning::Pow2), 4u);
  EXPECT_EQ(computeSegmentCount(4.0f, TessPartitioning::Pow2), 4u);
  EXPECT_EQ(computeSegmentCount(5.0f, TessPartitioning::Pow2), 8u);
}

TEST(TessellatorTest, FractionalOddPartitioningIsAlwaysOdd) {
  for (float F = 1.0f; F <= 9.0f; F += 0.5f)
    EXPECT_EQ(computeSegmentCount(F, TessPartitioning::FractionalOdd) % 2, 1u)
        << "factor " << F;
  EXPECT_EQ(computeSegmentCount(1.0f, TessPartitioning::FractionalOdd), 1u);
  EXPECT_EQ(computeSegmentCount(2.0f, TessPartitioning::FractionalOdd), 3u);
}

TEST(TessellatorTest, FractionalEvenPartitioningIsAlwaysEvenAndAtLeastTwo) {
  // Per the Vulkan/GLSL spec's own "Tessellator Spacing" section,
  // `SpacingFractionalEven` clamps to `[2, maxLevel]` (a stricter minimum
  // than every other partitioning mode's own `[1, maxLevel]`), so it can
  // never collapse an edge to an un-subdivided single point (a segment
  // count of 1) the way `Integer`/`FractionalOdd` do at `Factor <= 1`.
  for (float F = 0.0f; F <= 1.0f; F += 0.5f)
    EXPECT_EQ(computeSegmentCount(F, TessPartitioning::FractionalEven), 2u)
        << "factor " << F;
  for (float F = 1.5f; F <= 9.0f; F += 0.5f)
    EXPECT_EQ(computeSegmentCount(F, TessPartitioning::FractionalEven) % 2, 0u)
        << "factor " << F;
  EXPECT_EQ(computeSegmentCount(3.0f, TessPartitioning::FractionalEven), 4u);
}

TEST(TessellatorTest, FactorsAreClampedToMaxTessFactor) {
  EXPECT_EQ(computeSegmentCount(1000.0f, TessPartitioning::Integer,
                                /*MaxTessFactor=*/16),
            16u);
}

TEST(TessellatorTest, NonPositiveFactorCullsThePatch) {
  TessFactors Factors;
  Factors.Edges[0] = 0.0f;
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Isoline, TessPartitioning::Integer,
                 TessOutputPrimitive::Line, Factors);
  EXPECT_TRUE(Patch.Points.empty());
  EXPECT_TRUE(Patch.Indices.empty());
}

/// (Roadmap L24(b)) Renamed from `IsolineGeneratesADensityByDetailGrid`:
/// `DomainPoint::U`/`V` for an isoline domain are `U` = along-line
/// (detail) position, `V` = which-line (density) index -- the opposite of
/// what this test originally asserted, before that swap was found to
/// disagree with the real `SV_DomainLocation` convention (see
/// `DomainPoint`'s own doc comment and `tessellateIsoline`'s).
TEST(TessellatorTest, IsolineGeneratesADetailByDensityGrid) {
  TessFactors Factors;
  Factors.Edges = {3.0f, 4.0f, 1.0f, 1.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Isoline, TessPartitioning::Integer,
                 TessOutputPrimitive::Line, Factors);
  // 3 lines, each with 4 segments (5 points).
  EXPECT_EQ(Patch.Points.size(), 3u * 5u);
  // Each line contributes 4 line segments (2 indices each).
  EXPECT_EQ(Patch.Indices.size(), 3u * 4u * 2u);
  for (const DomainPoint &P : Patch.Points) {
    // `U` (along-line/detail) spans the full, inclusive `[0, 1]` range.
    EXPECT_GE(P.U, 0.0f);
    EXPECT_LE(P.U, 1.0f);
    // `V` (which-line/density) is a discrete index in `[0, 1)`.
    EXPECT_GE(P.V, 0.0f);
    EXPECT_LT(P.V, 1.0f);
  }
}

TEST(TessellatorTest, IsolinePointModeGeneratesNoIndices) {
  TessFactors Factors;
  Factors.Edges = {2.0f, 2.0f, 1.0f, 1.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Isoline, TessPartitioning::Integer,
                 TessOutputPrimitive::Point, Factors);
  EXPECT_FALSE(Patch.Points.empty());
  EXPECT_TRUE(Patch.Indices.empty());
}

// (Roadmap L220) Verifies the spec's own concentric-ring point/triangle
// counts for one worked example (edge and inside factor 4, so the first
// ring's own resolution `N0` is 4): manually derived by walking
// `tessellateTriangle`'s own ring recursion (Tessellator.cpp's own
// comments on `appendTriangleRingBoundary`/`fanRingToPoint` describe the
// same steps), then cross-checked against the function's actual output.
//
//  - Ring 0 (the real outer boundary): 3 edges of 4 points each, 12
//    points total.
//  - `N0 == 4` is neither the point-degenerate (`== 2`) nor
//    not-further-subdivided (`== 3`) terminal case, so ring 1's own
//    resolution is `N0 - 2 == 2`: 3 edges of 2 points each, 6 points
//    total. Bridging ring 0 to ring 1 emits `12 + 6 == 18` triangles (one
//    per point on either ring, per `bridgeRingsByEdge`'s own comment).
//  - Ring 1's own resolution (2) *is* the point-degenerate case: instead
//    of a ring 2, a single centroid point is added (1 point) and ring 1's
//    own 6 boundary points are fanned to it, emitting 6 more triangles.
//
// Total: `12 + 6 + 1 == 19` points, `18 + 6 == 24` triangles (72 indices).
TEST(TessellatorTest, TriangleDomainGeneratesTheAnalyticLatticeSize) {
  TessFactors Factors;
  Factors.Inside = {4.0f, 0.0f};
  Factors.Edges = {4.0f, 4.0f, 4.0f, 0.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  EXPECT_EQ(Patch.Points.size(), 19u);
  EXPECT_EQ(Patch.Indices.size(), 3 * 24u);
  for (const DomainPoint &P : Patch.Points) {
    EXPECT_NEAR(P.U + P.V + P.W, 1.0f, Epsilon);
    EXPECT_GE(P.U, -Epsilon);
    EXPECT_GE(P.V, -Epsilon);
    EXPECT_GE(P.W, -Epsilon);
  }
}

// (Roadmap L220) `N0 == 2` (inside factor 2): the spec's own "outermost
// inner triangle is degenerate" case reached directly from the real outer
// boundary, with no intermediate ring at all -- every one of the outer
// boundary's own points fans straight to a single centroid point, and
// there is exactly one such point in the whole patch (not one per would-be
// ring, since there is only ever one ring here).
TEST(TessellatorTest, TriangleInsideFactorTwoFansOuterBoundaryToOneCenterPoint) {
  TessFactors Factors;
  Factors.Inside = {2.0f, 0.0f};
  Factors.Edges = {3.0f, 4.0f, 5.0f, 0.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  // Outer boundary: 3 + 4 + 5 == 12 points. Plus exactly one center point.
  EXPECT_EQ(Patch.Points.size(), 13u);
  // One fan triangle per outer boundary point/segment.
  EXPECT_EQ(Patch.Indices.size(), 3 * 12u);
  int CenterCount = llvm::count_if(Patch.Points, [](const DomainPoint &P) {
    constexpr float Third = 1.0f / 3.0f;
    return std::abs(P.U - Third) < Epsilon && std::abs(P.V - Third) < Epsilon;
  });
  EXPECT_EQ(CenterCount, 1);
}

// (Roadmap L220) `N0 == 3` (inside factor 3): the spec's own "edges of the
// inner triangle are not subdivided" case -- a single small triangle is
// bridged directly to the real outer boundary, with no further rings and
// no center point at all.
TEST(TessellatorTest, TriangleInsideFactorThreeBridgesToOneUnsubdividedInnerTriangle) {
  TessFactors Factors;
  Factors.Inside = {3.0f, 0.0f};
  Factors.Edges = {3.0f, 4.0f, 5.0f, 0.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  // Outer boundary (12 points) + the inner triangle's own 3 corners.
  EXPECT_EQ(Patch.Points.size(), 15u);
  // Bridge triangles (12 + 3) plus the one real inner triangle itself.
  EXPECT_EQ(Patch.Indices.size(), 3 * 16u);
  int CenterCount = llvm::count_if(Patch.Points, [](const DomainPoint &P) {
    constexpr float Third = 1.0f / 3.0f;
    return std::abs(P.U - Third) < Epsilon && std::abs(P.V - Third) < Epsilon;
  });
  EXPECT_EQ(CenterCount, 0);
}

// (Roadmap L220) The spec's own "treated as though it were originally
// specified as 1 + epsilon" rule: an inside factor of exactly 1 combined
// with at least one outer edge factor greater than 1 must still produce a
// (degenerate, `SpacingEqual`/`SpacingFractionalEven`, or 3-segment,
// `SpacingFractionalOdd`) interior ring, not the fully-unsubdivided
// single-triangle fast path above (which only applies when *every* edge
// and the inside factor are all exactly 1).
TEST(TessellatorTest, TriangleInsideFactorOneWithSubdividedEdgeGetsEpsilonBump) {
  TessFactors Factors;
  Factors.Inside = {1.0f, 0.0f};
  Factors.Edges = {2.0f, 1.0f, 1.0f, 0.0f};
  TessellatedPatch Equal =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  // Outer boundary (2 + 1 + 1 == 4 points) fanned to one center point
  // (the `SpacingEqual`/`Integer` epsilon bump resolves to `N0 == 2`).
  EXPECT_EQ(Equal.Points.size(), 5u);
  EXPECT_EQ(Equal.Indices.size(), 3 * 4u);

  TessellatedPatch FractionalOdd = tessellate(
      TessellatorDomain::Triangle, TessPartitioning::FractionalOdd,
      TessOutputPrimitive::TriangleCcw, Factors);
  // The `SpacingFractionalOdd` epsilon bump instead resolves to `N0 == 3`:
  // a real, un-subdivided inner triangle (3 corners), no center point.
  // `FractionalOdd`'s own rounding rule also rounds the edge factor of 2
  // up to 3 (the smallest odd integer at least 2), so the outer boundary
  // itself has `3 + 1 + 1 == 5` points here, not the `Integer`-rounded
  // case's 4.
  EXPECT_EQ(FractionalOdd.Points.size(), 5u + 3u);
  EXPECT_EQ(FractionalOdd.Indices.size(), 3 * (5u + 3u + 1u));
}


// patch's own three corners toward the centroid. That inset is invisible
// to affine position/varying interpolation, but not to
// `gl_CullDistance`'s whole-*primitive* culling rule: a synthetic
// sub-triangle whose 3 vertices all land near one real edge of the
// unsubdivided triangle can be all-negative (and so get spuriously
// culled) even when the real, unsubdivided triangle's own 3 control
// points are not. See PhysicalDeviceInfo.cpp's own comment for the real
// `dEQP-VK.clipping.user_defined.clip_cull_distance.vert_tess.
// 1_7_fragmentshader_read` failure this closed.
TEST(TessellatorTest, TriangleFullyUnsubdividedFactorEmitsOneRealTriangle) {
  TessFactors Factors;
  Factors.Inside = {1.0f, 0.0f};
  Factors.Edges = {1.0f, 1.0f, 1.0f, 0.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  ASSERT_EQ(Patch.Points.size(), 3u);
  ASSERT_EQ(Patch.Indices.size(), 3u);
  // The 3 points are exactly the real corners (barycentric (1,0,0),
  // (0,1,0), (0,0,1)), not inset toward the centroid.
  auto HasCorner = [&](float U, float V, float W) {
    return llvm::any_of(Patch.Points, [&](const DomainPoint &P) {
      return std::abs(P.U - U) < Epsilon && std::abs(P.V - V) < Epsilon &&
             std::abs(P.W - W) < Epsilon;
    });
  };
  EXPECT_TRUE(HasCorner(1.0f, 0.0f, 0.0f));
  EXPECT_TRUE(HasCorner(0.0f, 1.0f, 0.0f));
  EXPECT_TRUE(HasCorner(0.0f, 0.0f, 1.0f));
}

/// The signed area of triangle (A, B, C)'s (U, V) projection: positive for
/// a counter-clockwise winding, negative for clockwise. Every domain point
/// this file generates has a well-defined (U, V) (a triangle domain's `W`
/// is redundant, `1 - U - V`), so this applies to both triangle and quad
/// domains alike.
float signedArea2D(const DomainPoint &A, const DomainPoint &B,
                   const DomainPoint &C) {
  return (B.U - A.U) * (C.V - A.V) - (B.V - A.V) * (C.U - A.U);
}

TEST(TessellatorTest, TriangleWindingIsConsistentAcrossEveryTriangle) {
  TessFactors Factors;
  Factors.Inside = {5.0f, 0.0f};
  Factors.Edges = {2.0f, 3.0f, 4.0f, 0.0f};
  TessellatedPatch Ccw =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  TessellatedPatch Cw =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCw, Factors);
  ASSERT_FALSE(Ccw.Indices.empty());
  ASSERT_EQ(Ccw.Indices.size(), Cw.Indices.size());
  for (size_t I = 0; I + 2 < Ccw.Indices.size(); I += 3) {
    float Area =
        signedArea2D(Ccw.Points[Ccw.Indices[I]], Ccw.Points[Ccw.Indices[I + 1]],
                     Ccw.Points[Ccw.Indices[I + 2]]);
    // Every triangle -- boundary-ring bridge or interior core alike --
    // shares one consistent, non-degenerate winding: the crack-free
    // bridging in Tessellator.cpp's `bridgeRingsByEdge` must not flip
    // orientation partway around the ring. `TriangleCcw`'s own winding
    // (matching the pre-R34 lattice-only code this generalizes) is a
    // negative (U, V)-projected signed area, not the positive one a
    // standard screen-space CCW convention would suggest.
    EXPECT_LT(Area, 0.0f) << "triangle " << I / 3;
  }
  for (size_t I = 0; I + 2 < Cw.Indices.size(); I += 3) {
    float Area =
        signedArea2D(Cw.Points[Cw.Indices[I]], Cw.Points[Cw.Indices[I + 1]],
                     Cw.Points[Cw.Indices[I + 2]]);
    EXPECT_GT(Area, 0.0f) << "triangle " << I / 3;
  }
}

TEST(TessellatorTest, TriangleSharedEdgeVerticesMatchAcrossPatches) {
  // Two patches that agree on one shared edge's factor -- but disagree on
  // every other edge and interior factor -- must generate identical
  // vertices along that shared edge: the crack-free property Tessellator.h
  // documents. `Edges[0]` (the `P1->P2` edge) is the shared one here.
  TessFactors A;
  A.Inside = {2.0f, 0.0f};
  A.Edges = {5.0f, 3.0f, 2.0f, 0.0f};
  TessFactors B;
  B.Inside = {6.0f, 0.0f};
  B.Edges = {5.0f, 1.0f, 4.0f, 0.0f};

  TessellatedPatch PatchA =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, A);
  TessellatedPatch PatchB =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, B);

  // The `P1->P2` edge is `U == 0`; each generated point there is uniquely
  // identified by `V` (equivalently `1 - W`).
  auto CollectEdgeVs = [](const TessellatedPatch &Patch) {
    std::vector<float> Vs;
    for (const DomainPoint &P : Patch.Points)
      if (std::fabs(P.U) < Epsilon)
        Vs.push_back(P.V);
    llvm::sort(Vs);
    return Vs;
  };
  std::vector<float> VsA = CollectEdgeVs(PatchA);
  std::vector<float> VsB = CollectEdgeVs(PatchB);
  ASSERT_EQ(VsA.size(), VsB.size());
  for (size_t I = 0; I != VsA.size(); ++I)
    EXPECT_NEAR(VsA[I], VsB[I], Epsilon) << "index " << I;
}

TEST(TessellatorTest, QuadDomainGeneratesTheAnalyticGridSize) {
  // Roadmap L221: the real spec algorithm, exercised here with inside
  // factors `{4, 5}` (`M = 4`, `N = 5`, per `tessellateQuad`'s own `m`/`n`
  // naming) chosen so neither axis degenerates (`M != 2 && N != 2`),
  // landing squarely in the general grid-and-bridge case. Uniform unit
  // edge factors give a 4-vertex outer boundary ring (one vertex per
  // edge); the surviving interior grid spans u-index `1..N - 1` and
  // v-index `1..M - 1` (an `(N - 1) x (M - 1)` grid of points), of which
  // only the strictly-interior cells (u-index `1..N - 2`, v-index
  // `1..M - 2`) are triangulated directly -- the grid's own outermost
  // ring of cells is discarded and re-bridged to the real outer boundary
  // instead.
  const uint32_t M = 4, N = 5;
  TessFactors Factors;
  Factors.Inside = {static_cast<float>(M), static_cast<float>(N)};
  Factors.Edges = {1.0f, 1.0f, 1.0f, 1.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  const uint32_t OuterRingSize = 4;
  const uint32_t GridPoints = (N - 1) * (M - 1);
  const uint32_t InteriorCells = (N - 2) * (M - 2);
  // The interior grid's own boundary ring has `2 * ((N - 2) + (M - 2))`
  // vertices (its own u-edges have `N - 2` points each, its own v-edges
  // have `M - 2` points each); bridging it to the outer ring emits one
  // triangle per combined vertex on both rings (see `bridgeEdge`'s own
  // comment).
  const uint32_t InnerRingSize = 2 * ((N - 2) + (M - 2));
  EXPECT_EQ(Patch.Points.size(), OuterRingSize + GridPoints);
  EXPECT_EQ(Patch.Indices.size(),
            3 * (InteriorCells * 2 + OuterRingSize + InnerRingSize));
  for (const DomainPoint &P : Patch.Points) {
    EXPECT_GE(P.U, 0.0f);
    EXPECT_LE(P.U, 1.0f);
    EXPECT_GE(P.V, 0.0f);
    EXPECT_LE(P.V, 1.0f);
  }
}

TEST(TessellatorTest, QuadMatchingEdgeAndInsideFactorsGiveDyadicCoreCoords) {
  // Roadmap L82/L221: when the inside factors exactly match the
  // (uniform) edge factors -- the common `DomainSystemValues.test`/
  // `QuadDomainTessellation.test` shape, all factors == 2 -- both axes
  // are degenerate (`M == 2 && N == 2` per the real spec algorithm), so
  // the entire interior collapses to the single, exactly-representable
  // center point `(0.5, 0.5)`, not a spurious extra interior ring landing
  // on non-dyadic fractions like `1/6` that cannot be represented exactly
  // in `float32` (the original roadmap L82 bug this regression guards
  // against; the real spec algorithm implemented for L221 is immune to
  // it by construction, since every point it ever generates is an exact
  // fraction `i / N` or `j / M`).
  TessFactors Factors;
  Factors.Inside = {2.0f, 2.0f};
  Factors.Edges = {2.0f, 2.0f, 2.0f, 2.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  bool FoundInterior = false;
  for (const DomainPoint &P : Patch.Points) {
    // Boundary-ring points sit at U/V == 0, 0.5, or 1; only inspect the
    // strictly-interior center point this regression cares about.
    if (P.U == 0.0f || P.U == 1.0f || P.V == 0.0f || P.V == 1.0f)
      continue;
    FoundInterior = true;
    EXPECT_EQ(P.U, 0.5f);
    EXPECT_EQ(P.V, 0.5f);
  }
  EXPECT_TRUE(FoundInterior);
}

TEST(TessellatorTest, QuadFullyUnsubdividedFactorEmitsTwoRealTriangles) {
  // Roadmap L219 regression: mirrors
  // `TriangleFullyUnsubdividedFactorEmitsOneRealTriangle` for the quad
  // domain. When both inside factors round down to their own minimum of
  // 1 segment (which per the Vulkan/GLSL tessellation spec can only
  // happen alongside every outer edge also degenerating to 1 segment),
  // the quad needs exactly its own 4 real corners and no synthesized
  // interior core -- not the 4 spurious `(0.25, 0.25)`-style interior
  // points the general inset+bridge path's own `Nu`/`Nv` minimum-1 clamp
  // used to force even here.
  TessFactors Factors;
  Factors.Inside = {1.0f, 1.0f};
  Factors.Edges = {1.0f, 1.0f, 1.0f, 1.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  ASSERT_EQ(Patch.Points.size(), 4u);
  ASSERT_EQ(Patch.Indices.size(), 6u);
  auto HasCorner = [&](float U, float V) {
    return llvm::any_of(Patch.Points, [&](const DomainPoint &P) {
      return std::abs(P.U - U) < Epsilon && std::abs(P.V - V) < Epsilon;
    });
  };
  EXPECT_TRUE(HasCorner(0.0f, 0.0f));
  EXPECT_TRUE(HasCorner(1.0f, 0.0f));
  EXPECT_TRUE(HasCorner(1.0f, 1.0f));
  EXPECT_TRUE(HasCorner(0.0f, 1.0f));
}

TEST(TessellatorTest, QuadWindingIsConsistentAcrossEveryTriangle) {
  TessFactors Factors;
  Factors.Inside = {3.0f, 4.0f};
  Factors.Edges = {2.0f, 5.0f, 3.0f, 1.0f};
  TessellatedPatch Ccw =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  TessellatedPatch Cw =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCw, Factors);
  ASSERT_FALSE(Ccw.Indices.empty());
  ASSERT_EQ(Ccw.Indices.size(), Cw.Indices.size());
  for (size_t I = 0; I + 2 < Ccw.Indices.size(); I += 3) {
    float Area =
        signedArea2D(Ccw.Points[Ccw.Indices[I]], Ccw.Points[Ccw.Indices[I + 1]],
                     Ccw.Points[Ccw.Indices[I + 2]]);
    EXPECT_LT(Area, 0.0f) << "triangle " << I / 3;
  }
  for (size_t I = 0; I + 2 < Cw.Indices.size(); I += 3) {
    float Area =
        signedArea2D(Cw.Points[Cw.Indices[I]], Cw.Points[Cw.Indices[I + 1]],
                     Cw.Points[Cw.Indices[I + 2]]);
    EXPECT_GT(Area, 0.0f) << "triangle " << I / 3;
  }
}

TEST(TessellatorTest, QuadSharedEdgeVerticesMatchAcrossPatches) {
  // As with the triangle domain's analogous test: two quad patches that
  // agree on one shared edge's factor (the `u == 1` edge, `Edges[2]`) but
  // disagree on every other factor must place identical vertices along it.
  TessFactors A;
  A.Inside = {2.0f, 5.0f};
  A.Edges = {3.0f, 4.0f, 6.0f, 1.0f};
  TessFactors B;
  B.Inside = {4.0f, 2.0f};
  B.Edges = {1.0f, 2.0f, 6.0f, 5.0f};

  TessellatedPatch PatchA =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, A);
  TessellatedPatch PatchB =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, B);

  auto CollectEdgeVs = [](const TessellatedPatch &Patch) {
    std::vector<float> Vs;
    for (const DomainPoint &P : Patch.Points)
      if (std::fabs(P.U - 1.0f) < Epsilon)
        Vs.push_back(P.V);
    llvm::sort(Vs);
    return Vs;
  };
  std::vector<float> VsA = CollectEdgeVs(PatchA);
  std::vector<float> VsB = CollectEdgeVs(PatchB);
  ASSERT_EQ(VsA.size(), VsB.size());
  for (size_t I = 0; I != VsA.size(); ++I)
    EXPECT_NEAR(VsA[I], VsB[I], Epsilon) << "index " << I;
}

TEST(TessellatorTest, QuadPointModeGeneratesNoIndices) {
  TessFactors Factors;
  Factors.Inside = {2.0f, 2.0f};
  Factors.Edges = {1.0f, 1.0f, 1.0f, 1.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::Point, Factors);
  EXPECT_FALSE(Patch.Points.empty());
  EXPECT_TRUE(Patch.Indices.empty());
}

/// Checks that \p Patch's triangles form one consistently wound, non-
/// overlapping, crack-free cover of its domain, and returns a description
/// of the first violation found (or an empty string when there is none).
///
/// The check is combinatorial rather than metric, because a crack and the
/// fold that produced it have equal area and so cancel out of any
/// area-based test. Two properties together are exactly "watertight":
///
///  - every *directed* edge appears at most once, which fails as soon as
///    two triangles overlap with the same winding (the shape a ring-walk
///    regression in `bridgeRingsByEdge` produces: a "bowtie" quad whose two
///    halves fold back across their shared edge); and
///  - every directed edge whose reverse is absent lies on the domain
///    boundary, which fails as soon as an interior crack or T-junction
///    leaves an unpaired interior edge behind.
std::string findNonManifoldEdge(const TessellatedPatch &Patch,
                                TessellatorDomain Domain) {
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> Directed;
  for (size_t I = 0; I + 2 < Patch.Indices.size(); I += 3) {
    uint32_t V[3] = {Patch.Indices[I], Patch.Indices[I + 1],
                     Patch.Indices[I + 2]};
    for (unsigned K = 0; K != 3; ++K) {
      auto Edge = std::make_pair(V[K], V[(K + 1) % 3]);
      if (++Directed[Edge] > 1)
        return "directed edge " + std::to_string(Edge.first) + " -> " +
               std::to_string(Edge.second) +
               " is used by more than one "
               "triangle (overlapping fold)";
    }
  }
  auto onBoundary = [&](const DomainPoint &P) {
    constexpr float Tol = 1e-4f;
    if (Domain == TessellatorDomain::Triangle)
      return P.U <= Tol || P.V <= Tol || P.W <= Tol;
    return P.U <= Tol || P.V <= Tol || P.U >= 1.0f - Tol || P.V >= 1.0f - Tol;
  };
  for (const auto &Entry : Directed) {
    if (Directed.count({Entry.first.second, Entry.first.first}))
      continue;
    const DomainPoint &A = Patch.Points[Entry.first.first];
    const DomainPoint &B = Patch.Points[Entry.first.second];
    if (onBoundary(A) && onBoundary(B))
      continue;
    return "interior edge " + std::to_string(Entry.first.first) + " -> " +
           std::to_string(Entry.first.second) +
           " has no opposite-facing neighbor (crack)";
  }
  return "";
}

TEST(TessellatorTest, TriangleTessellationIsCrackFreeAtEveryFactor) {
  // A regression in `bridgeRingsByEdge`'s outer/core ring walk -- notably
  // wrapping an exhausted edge back to its own first vertex instead of
  // advancing to the corner it shares with the next edge -- folds the last
  // bridging triangles of every edge back across the strip, leaving a
  // wedge-shaped crack of exactly the folded triangle's own area behind
  // them.
  for (float Edge : {1.0f, 2.0f, 3.0f, 4.0f, 7.0f, 16.0f}) {
    TessFactors Factors;
    Factors.Inside = {Edge, 0.0f};
    Factors.Edges = {Edge, Edge, Edge, 0.0f};
    TessellatedPatch Patch =
        tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                   TessOutputPrimitive::TriangleCcw, Factors);
    ASSERT_FALSE(Patch.Indices.empty()) << "edge factor " << Edge;
    EXPECT_EQ(findNonManifoldEdge(Patch, TessellatorDomain::Triangle), "")
        << "edge factor " << Edge;
  }
}

TEST(TessellatorTest, TriangleTessellationIsCrackFreeWithUnequalEdgeFactors) {
  // The interesting case for `bridgeRingsByEdge`: each boundary edge has a
  // different vertex count from the others *and* from the inset core ring
  // it bridges to, so the two rings are walked at genuinely different
  // rates.
  TessFactors Factors;
  Factors.Inside = {5.0f, 0.0f};
  Factors.Edges = {2.0f, 3.0f, 4.0f, 0.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  ASSERT_FALSE(Patch.Indices.empty());
  EXPECT_EQ(findNonManifoldEdge(Patch, TessellatorDomain::Triangle), "");
}

// (Roadmap L220) Sweeps every inside factor from 1 to 24 against a fixed
// set of unequal outer edge factors, exercising both concentric-ring
// termination cases (point-degenerate at an even `N0`, un-subdivided
// single triangle at an odd one) and every ring-recursion depth in
// between (an inside factor of 24 recurses through 11 full rings before
// terminating), plus both `SpacingEqual` (`Integer` here) and
// `SpacingFractionalOdd` partitioning, whose own epsilon-bump rule (see
// `TriangleInsideFactorOneWithSubdividedEdgeGetsEpsilonBump`) only ever
// applies here at inside factor 1.
TEST(TessellatorTest, TriangleTessellationIsCrackFreeAcrossManyInsideFactors) {
  for (TessPartitioning Partitioning :
       {TessPartitioning::Integer, TessPartitioning::FractionalOdd}) {
    for (uint32_t Inside = 1; Inside <= 24; ++Inside) {
      TessFactors Factors;
      Factors.Inside = {static_cast<float>(Inside), 0.0f};
      Factors.Edges = {3.0f, 5.0f, 7.0f, 0.0f};
      TessellatedPatch Patch =
          tessellate(TessellatorDomain::Triangle, Partitioning,
                     TessOutputPrimitive::TriangleCcw, Factors);
      ASSERT_FALSE(Patch.Indices.empty())
          << "inside factor " << Inside << " partitioning "
          << static_cast<int>(Partitioning);
      EXPECT_EQ(findNonManifoldEdge(Patch, TessellatorDomain::Triangle), "")
          << "inside factor " << Inside << " partitioning "
          << static_cast<int>(Partitioning);
    }
  }
}

TEST(TessellatorTest, QuadTessellationIsCrackFreeAtEveryFactor) {
  for (float Edge : {1.0f, 2.0f, 3.0f, 5.0f, 12.0f}) {
    TessFactors Factors;
    Factors.Inside = {Edge, Edge};
    Factors.Edges = {Edge, Edge, Edge, Edge};
    TessellatedPatch Patch =
        tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                   TessOutputPrimitive::TriangleCcw, Factors);
    ASSERT_FALSE(Patch.Indices.empty()) << "edge factor " << Edge;
    EXPECT_EQ(findNonManifoldEdge(Patch, TessellatorDomain::Quad), "")
        << "edge factor " << Edge;
  }
}

// Roadmap L221: sweeps both inside factors independently (each 1..12,
// covering the fully-unsubdivided fast path, the epsilon-bump at exactly
// 1, both single-axis-degenerate cases, the both-degenerate case, and the
// general grid-and-bridge case) crossed with both `Integer` and
// `FractionalOdd` partitioning (whose own epsilon-bump only ever applies
// at inside factor 1, mirroring the triangle domain's analogous sweep).
TEST(TessellatorTest, QuadTessellationIsCrackFreeAcrossManyInsideFactors) {
  for (TessPartitioning Partitioning :
       {TessPartitioning::Integer, TessPartitioning::FractionalOdd}) {
    for (uint32_t InsideM = 1; InsideM <= 12; ++InsideM) {
      for (uint32_t InsideN = 1; InsideN <= 12; ++InsideN) {
        TessFactors Factors;
        Factors.Inside = {static_cast<float>(InsideM),
                          static_cast<float>(InsideN)};
        Factors.Edges = {3.0f, 5.0f, 7.0f, 2.0f};
        TessellatedPatch Patch =
            tessellate(TessellatorDomain::Quad, Partitioning,
                       TessOutputPrimitive::TriangleCcw, Factors);
        ASSERT_FALSE(Patch.Indices.empty())
            << "inside " << InsideM << "," << InsideN << " partitioning "
            << static_cast<int>(Partitioning);
        EXPECT_EQ(findNonManifoldEdge(Patch, TessellatorDomain::Quad), "")
            << "inside " << InsideM << "," << InsideN << " partitioning "
            << static_cast<int>(Partitioning);
      }
    }
  }
}

TEST(TessellatorTest, QuadTessellationIsCrackFreeWithUnequalEdgeFactors) {
  TessFactors Factors;
  Factors.Inside = {6.0f, 3.0f};
  Factors.Edges = {2.0f, 5.0f, 3.0f, 4.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  ASSERT_FALSE(Patch.Indices.empty());
  EXPECT_EQ(findNonManifoldEdge(Patch, TessellatorDomain::Quad), "");
}

TEST(TessellatorTest, QuadAlignedInnerAndOuterFactorsGiveUnitGridTriangles) {
  // Roadmap L221 regression (found via `dEQP-VK.tessellation.
  // geometry_interaction.scatter.geometry_scatter_primitives`, an actual
  // Vulkan CTS conformance test): when every outer edge factor exactly
  // matches the corresponding inner axis's own segment count -- the
  // common "uniform level" case, e.g. every `SV_Tess*Factor` set to the
  // same value from a single shader constant -- the interior grid's own
  // boundary ring (`InnerRing`) is *not* a same-span concentric shrink
  // of the true outer boundary the way the triangle domain's inset core
  // is: it is a strictly narrower `[1 / N, (N - 1) / N]`-ish sub-interval
  // of the outer ring's full `[0, 1]` corner-to-corner span (per the
  // spec: the outer rectangle's own edge subdivision is discarded and
  // replaced, while the inner rectangle's is retained as-is). Bridging
  // these two rings by raw index/edge-length ratio (as if they spanned
  // the same interval) bunches the extra outer vertices unevenly, most
  // visibly producing one oversized, non-unit-cell triangle pair at each
  // of the 4 corners instead of a plain, uniform grid. Every triangle
  // here must therefore be an exact `1 x 1`-grid-cell (regular diagonal
  // split), even along the outermost ring bridging the true edge to the
  // interior grid.
  const uint32_t Level = 5;
  TessFactors Factors;
  Factors.Inside = {static_cast<float>(Level), static_cast<float>(Level)};
  Factors.Edges = {static_cast<float>(Level), static_cast<float>(Level),
                   static_cast<float>(Level), static_cast<float>(Level)};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  ASSERT_EQ(Patch.Indices.size(), 3u * 2 * Level * Level);
  for (size_t I = 0; I + 2 < Patch.Indices.size(); I += 3) {
    const DomainPoint &A = Patch.Points[Patch.Indices[I]];
    const DomainPoint &B = Patch.Points[Patch.Indices[I + 1]];
    const DomainPoint &C = Patch.Points[Patch.Indices[I + 2]];
    float MinU = std::min({A.U, B.U, C.U}), MaxU = std::max({A.U, B.U, C.U});
    float MinV = std::min({A.V, B.V, C.V}), MaxV = std::max({A.V, B.V, C.V});
    EXPECT_NEAR((MaxU - MinU) * Level, 1.0f, 1e-4f) << "triangle " << I / 3;
    EXPECT_NEAR((MaxV - MinV) * Level, 1.0f, 1e-4f) << "triangle " << I / 3;
  }
  EXPECT_EQ(findNonManifoldEdge(Patch, TessellatorDomain::Quad), "");
}

TEST(TessellatorTest, QuadSingleAxisDegenerateInsideFactorGivesInteriorLine) {
  // Roadmap L221: when exactly one axis's clamped inner tessellation
  // level is 2 (`N == 2` here, the "u-degenerate" case, `M == 5 != 2`),
  // the interior collapses to a line of `M - 1` points at the lone
  // interior u-grid-index (`u == 0.5`), rather than a 2D grid or a single
  // point.
  TessFactors Factors;
  Factors.Inside = {5.0f, 2.0f};
  Factors.Edges = {1.0f, 1.0f, 1.0f, 1.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  const uint32_t M = 5;
  uint32_t InteriorCount = 0;
  for (const DomainPoint &P : Patch.Points) {
    if (P.U == 0.0f || P.U == 1.0f || P.V == 0.0f || P.V == 1.0f)
      continue;
    ++InteriorCount;
    EXPECT_EQ(P.U, 0.5f);
    EXPECT_GT(P.V, 0.0f);
    EXPECT_LT(P.V, 1.0f);
  }
  EXPECT_EQ(InteriorCount, M - 1);
  EXPECT_EQ(findNonManifoldEdge(Patch, TessellatorDomain::Quad), "");
}

TEST(TessellatorTest, QuadOtherAxisDegenerateInsideFactorGivesInteriorLine) {
  // Mirror of the above with `u`/`v` (and `Inside[0]`/`Inside[1]`)
  // swapped (`M == 2`, the "v-degenerate" case, `N == 5 != 2`).
  TessFactors Factors;
  Factors.Inside = {2.0f, 5.0f};
  Factors.Edges = {1.0f, 1.0f, 1.0f, 1.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  const uint32_t N = 5;
  uint32_t InteriorCount = 0;
  for (const DomainPoint &P : Patch.Points) {
    if (P.U == 0.0f || P.U == 1.0f || P.V == 0.0f || P.V == 1.0f)
      continue;
    ++InteriorCount;
    EXPECT_EQ(P.V, 0.5f);
    EXPECT_GT(P.U, 0.0f);
    EXPECT_LT(P.U, 1.0f);
  }
  EXPECT_EQ(InteriorCount, N - 1);
  EXPECT_EQ(findNonManifoldEdge(Patch, TessellatorDomain::Quad), "");
}

} // namespace
