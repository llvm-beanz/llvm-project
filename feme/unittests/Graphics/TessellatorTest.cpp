//===- TessellatorTest.cpp - Tests for feme::graphics::tessellate --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Tessellator.h"

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
  const uint32_t N = 4;
  EXPECT_EQ(Patch.Points.size(), (N + 1) * (N + 2) / 2);
  EXPECT_EQ(Patch.Indices.size(), 3 * N * N);
  for (const DomainPoint &P : Patch.Points) {
    EXPECT_NEAR(P.U + P.V + P.W, 1.0f, Epsilon);
    EXPECT_GE(P.U, -Epsilon);
    EXPECT_GE(P.V, -Epsilon);
    EXPECT_GE(P.W, -Epsilon);
  }
}

TEST(TessellatorTest, TriangleWindingMatchesRequestedOutputPrimitive) {
  TessFactors Factors;
  Factors.Inside = {1.0f, 0.0f};
  Factors.Edges = {1.0f, 1.0f, 1.0f, 0.0f};
  TessellatedPatch Ccw =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  TessellatedPatch Cw =
      tessellate(TessellatorDomain::Triangle, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCw, Factors);
  ASSERT_EQ(Ccw.Indices.size(), 3u);
  ASSERT_EQ(Cw.Indices.size(), 3u);
  // A single-triangle patch's Cw output reverses Ccw's winding.
  EXPECT_EQ(Cw.Indices[0], Ccw.Indices[0]);
  EXPECT_EQ(Cw.Indices[1], Ccw.Indices[2]);
  EXPECT_EQ(Cw.Indices[2], Ccw.Indices[1]);
}

TEST(TessellatorTest, QuadDomainGeneratesTheAnalyticGridSize) {
  TessFactors Factors;
  Factors.Inside = {2.0f, 3.0f};
  Factors.Edges = {1.0f, 1.0f, 1.0f, 1.0f};
  TessellatedPatch Patch =
      tessellate(TessellatorDomain::Quad, TessPartitioning::Integer,
                 TessOutputPrimitive::TriangleCcw, Factors);
  EXPECT_EQ(Patch.Points.size(), 3u * 4u);
  // 2*3 cells, 2 triangles (6 indices) each.
  EXPECT_EQ(Patch.Indices.size(), 2u * 3u * 6u);
  for (const DomainPoint &P : Patch.Points) {
    EXPECT_GE(P.U, 0.0f);
    EXPECT_LE(P.U, 1.0f);
    EXPECT_GE(P.V, 0.0f);
    EXPECT_LE(P.V, 1.0f);
  }
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
