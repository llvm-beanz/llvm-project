//===- MeshOutputTest.cpp - Tests for MeshOutputBuilder -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/MeshOutput.h"

#include "llvm/ADT/STLExtras.h"
#include "gtest/gtest.h"

using namespace feme::graphics;

namespace {

TEST(MeshOutputTest, GetVerticesPerPrimitiveMatchesTopology) {
  EXPECT_EQ(getVerticesPerPrimitive(MeshOutputTopology::Points), 1u);
  EXPECT_EQ(getVerticesPerPrimitive(MeshOutputTopology::Lines), 2u);
  EXPECT_EQ(getVerticesPerPrimitive(MeshOutputTopology::Triangles), 3u);
}

TEST(MeshOutputTest, SetOutputCountsRejectsBeyondDeclaredMaxima) {
  MeshOutputBuilder Builder(MeshOutputTopology::Triangles,
                           /*MaxVertices=*/4, /*MaxPrimitives=*/2);
  EXPECT_FALSE(Builder.setOutputCounts(5, 1));
  EXPECT_FALSE(Builder.setOutputCounts(4, 3));
  EXPECT_EQ(Builder.getVertexCount(), 0u);
  EXPECT_EQ(Builder.getPrimitiveCount(), 0u);
  EXPECT_TRUE(Builder.setOutputCounts(4, 2));
  EXPECT_EQ(Builder.getVertexCount(), 4u);
  EXPECT_EQ(Builder.getPrimitiveCount(), 2u);
}

TEST(MeshOutputTest, SetVertexWritesWithinTheDeclaredCount) {
  MeshOutputBuilder Builder(MeshOutputTopology::Triangles, 4, 2);
  // No `setOutputCounts` call yet: every index is out of range.
  EXPECT_FALSE(Builder.setVertex(0, {1.0f}));

  ASSERT_TRUE(Builder.setOutputCounts(3, 1));
  EXPECT_TRUE(Builder.setVertex(0, {1.0f, 2.0f}));
  EXPECT_TRUE(Builder.setVertex(2, {3.0f, 4.0f}));
  EXPECT_FALSE(Builder.setVertex(3, {0.0f})); // beyond the declared count.

  ASSERT_EQ(Builder.getVertices().size(), 4u); // MaxVertices, not the count.
  EXPECT_EQ(Builder.getVertices()[0], (MeshOutputRow{1.0f, 2.0f}));
  EXPECT_EQ(Builder.getVertices()[2], (MeshOutputRow{3.0f, 4.0f}));
  EXPECT_TRUE(Builder.getVertices()[1].empty()); // never written.
}

TEST(MeshOutputTest, SetPrimitiveWritesWithinTheDeclaredCount) {
  MeshOutputBuilder Builder(MeshOutputTopology::Points, 4, 2);
  ASSERT_TRUE(Builder.setOutputCounts(4, 1));
  EXPECT_TRUE(Builder.setPrimitive(0, {42.0f}));
  EXPECT_FALSE(Builder.setPrimitive(1, {0.0f})); // beyond the declared count.
  EXPECT_EQ(Builder.getPrimitives()[0], (MeshOutputRow{42.0f}));
}

TEST(MeshOutputTest, SetPrimitiveIndicesRejectsTheWrongWidth) {
  MeshOutputBuilder Builder(MeshOutputTopology::Triangles, 4, 2);
  ASSERT_TRUE(Builder.setOutputCounts(4, 2));
  EXPECT_FALSE(Builder.setPrimitiveIndices(0, {0, 1})); // only 2, needs 3.
  EXPECT_TRUE(Builder.setPrimitiveIndices(0, {0, 1, 2}));
  EXPECT_EQ(Builder.getPrimitiveIndices(0),
           (llvm::ArrayRef<uint32_t>{0u, 1u, 2u}));
}

TEST(MeshOutputTest, SetPrimitiveIndicesRejectsAnOutOfRangeVertexIndex) {
  MeshOutputBuilder Builder(MeshOutputTopology::Triangles, 3, 1);
  ASSERT_TRUE(Builder.setOutputCounts(3, 1));
  // Vertex index 3 is out of the declared 3-vertex range.
  EXPECT_FALSE(Builder.setPrimitiveIndices(0, {0, 1, 3}));
  EXPECT_TRUE(Builder.getPrimitiveIndices(0).empty() ||
             llvm::all_of(Builder.getPrimitiveIndices(0),
                          [](uint32_t V) { return V == 0; }));
}

TEST(MeshOutputTest, SetPrimitiveIndicesRejectsAnOutOfRangePrimitiveIndex) {
  MeshOutputBuilder Builder(MeshOutputTopology::Lines, 4, 1);
  ASSERT_TRUE(Builder.setOutputCounts(4, 1));
  EXPECT_FALSE(Builder.setPrimitiveIndices(1, {0, 1}));
}

} // namespace
