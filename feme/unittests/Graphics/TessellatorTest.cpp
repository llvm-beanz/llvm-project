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

TEST(TessellatorTest, IsolineGeneratesADensityByDetailGrid) {
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
    EXPECT_GE(P.U, 0.0f);
    EXPECT_LT(P.U, 1.0f);
    EXPECT_GE(P.V, 0.0f);
    EXPECT_LE(P.V, 1.0f);
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
  // vertex per edge); the inset core is a 2x3 grid (Inside factors), whose
  // own ring has `2 * (2 + 3)` vertices -- see Tessellator.cpp's
  // `appendQuadBoundaryRing`/`bridgeRingsByEdge`.
  const uint32_t Nu = 2, Nv = 3;
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

} // namespace
