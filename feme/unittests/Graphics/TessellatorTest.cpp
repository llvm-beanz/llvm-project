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

TEST(TessellatorTest, FractionalEvenPartitioningIsAlwaysEvenOrOne) {
  EXPECT_EQ(computeSegmentCount(1.0f, TessPartitioning::FractionalEven), 1u);
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

TEST(TessellatorTest, TriangleDomainGeneratesTheAnalyticLatticeSize) {
  TessFactors Factors;
  Factors.Inside = {4.0f, 0.0f};
  Factors.Edges = {4.0f, 4.0f, 4.0f, 0.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  // Uniform factors mean the per-edge outer boundary (`M` vertices, one
  // ring vertex per edge segment) and the inset uniform core (resolution
  // `N`, per `computeSegmentCount`) agree on `N` -- see Tessellator.cpp's
  // `bridgeRings`/`appendTriangleLattice`.
  const uint32_t N = 4;
  const uint32_t M = 3 * N;
  EXPECT_EQ(Patch.Points.size(), M + (N + 1) * (N + 2) / 2);
  // `N * N` core triangles, plus `M + 3 * N` bridging triangles (one per
  // outer/core ring vertex).
  EXPECT_EQ(Patch.Indices.size(), 3 * (N * N + M + 3 * N));
  for (const DomainPoint &P : Patch.Points) {
    EXPECT_NEAR(P.U + P.V + P.W, 1.0f, Epsilon);
    EXPECT_GE(P.U, -Epsilon);
    EXPECT_GE(P.V, -Epsilon);
    EXPECT_GE(P.W, -Epsilon);
  }
}

// (Roadmap H7x) At the fully unsubdivided factor (every edge and the
// interior both at 1 segment), `tessellateTriangle` must emit the real,
// single triangle directly -- not the general inset/bridge path's usual
// 7-triangle core+annulus split, which spuriously shrinks two of the
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
  TessFactors Factors;
  Factors.Inside = {2.0f, 3.0f};
  Factors.Edges = {1.0f, 1.0f, 1.0f, 1.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  // Uniform unit edge factors give a 4-vertex outer boundary ring (one
  // vertex per edge); the inset core lattice's own division count is one
  // less than each Inside factor's whole-axis segment count (roadmap
  // L82: an inside factor of `N` implies `N - 1` strictly-interior
  // lattice lines, since the core is always inset strictly within the
  // boundary and never touches it) -- a 1x2 grid here, whose own ring has
  // `2 * (1 + 2)` vertices -- see Tessellator.cpp's
  // `appendQuadBoundaryRing`/`bridgeRingsByEdge`.
  const uint32_t Nu = 1, Nv = 2;
  const uint32_t OuterRingSize = 4;
  const uint32_t CoreRingSize = 2 * (Nu + Nv);
  EXPECT_EQ(Patch.Points.size(), OuterRingSize + (Nu + 1) * (Nv + 1));
  // `Nu * Nv` interior cells (2 triangles each), plus one bridging
  // triangle per outer/core ring vertex.
  EXPECT_EQ(Patch.Indices.size(),
            3 * (Nu * Nv * 2 + OuterRingSize + CoreRingSize));
  for (const DomainPoint &P : Patch.Points) {
    EXPECT_GE(P.U, 0.0f);
    EXPECT_LE(P.U, 1.0f);
    EXPECT_GE(P.V, 0.0f);
    EXPECT_LE(P.V, 1.0f);
  }
}

TEST(TessellatorTest, QuadMatchingEdgeAndInsideFactorsGiveDyadicCoreCoords) {
  // Roadmap L82 regression: when the inside factors exactly match the
  // (uniform) edge factors -- the common `DomainSystemValues.test`/
  // `QuadDomainTessellation.test` shape, all factors == 2 -- the interior
  // core lattice must land on exactly-representable dyadic fractions
  // (0.25/0.75), not a spurious extra interior ring landing on
  // non-dyadic fractions like 1/6 that cannot be represented exactly in
  // `float32` (see Tessellator.cpp's `tessellateQuad` for the full
  // rationale). Every core (non-boundary) point's U and V must be one of
  // exactly {0.25, 0.75} bit-for-bit.
  TessFactors Factors;
  Factors.Inside = {2.0f, 2.0f};
  Factors.Edges = {2.0f, 2.0f, 2.0f, 2.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  bool FoundInterior = false;
  for (const DomainPoint &P : Patch.Points) {
    // Boundary-ring points sit at U/V == 0, 0.5, or 1; only inspect the
    // strictly-interior core points this regression cares about.
    if (P.U == 0.0f || P.U == 1.0f || P.V == 0.0f || P.V == 1.0f)
      continue;
    FoundInterior = true;
    EXPECT_TRUE(P.U == 0.25f || P.U == 0.75f) << "U = " << P.U;
    EXPECT_TRUE(P.V == 0.25f || P.V == 0.75f) << "V = " << P.V;
  }
  EXPECT_TRUE(FoundInterior);
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

} // namespace
