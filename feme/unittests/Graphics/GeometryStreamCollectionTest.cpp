//===- GeometryStreamCollectionTest.cpp - Tests for collectGeometryStreams ==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/GeometryStreamCollection.h"

#include "feme/Target/CPU/RuntimeABI.h"
#include "gtest/gtest.h"

#include <vector>

using namespace feme;
using namespace feme::cpu;
using namespace feme::graphics;

namespace {

// Two primitives, each emitting two vertices onto stream 0. The first
// primitive cuts after its second vertex (closing one strip); the second
// leaves its strip open (a trailing open strip with vertices, per
// `GeometryStreamBuilder::getStrips`'s own contract).
TEST(GeometryStreamCollectionTest, ReplaysFlatBatchRecordsInPrimitiveOrder) {
  constexpr uint32_t PrimitiveCount = 2;
  constexpr uint32_t MaxVerticesPerStream = 4;
  constexpr uint32_t OutputScalarsPerVertex = 1;

  std::vector<float> EmittedVertices(
      PrimitiveCount * MaxVerticesPerStream * OutputScalarsPerVertex, 0.0f);
  std::vector<uint32_t> EmittedVertexCounts(PrimitiveCount, 0);
  std::vector<uint8_t> StripEndsAfter(PrimitiveCount * MaxVerticesPerStream, 0);

  auto Slot = [&](uint32_t Primitive, uint32_t Vertex) {
    return Primitive * MaxVerticesPerStream + Vertex;
  };
  EmittedVertices[Slot(0, 0)] = 10.0f;
  EmittedVertices[Slot(0, 1)] = 11.0f;
  EmittedVertexCounts[0] = 2;
  StripEndsAfter[Slot(0, 1)] = 1;

  EmittedVertices[Slot(1, 0)] = 20.0f;
  EmittedVertices[Slot(1, 1)] = 21.0f;
  EmittedVertexCounts[1] = 2;

  FemeGeometryArgs Args{};
  Args.PrimitiveCount = PrimitiveCount;
  Args.MaxVerticesPerStream = MaxVerticesPerStream;
  Args.OutputScalarsPerVertex = OutputScalarsPerVertex;
  Args.EmittedVertices = EmittedVertices.data();
  Args.EmittedVertexCounts = EmittedVertexCounts.data();
  Args.StripEndsAfter = StripEndsAfter.data();

  GeometryStreamBuilder Combined(/*StreamCount=*/1,
                                 /*MaxVerticesPerStream=*/8);
  GeometryStreamMergeResult Result = collectGeometryStreams(Args, Combined);

  EXPECT_FALSE(Result.Truncated);
  ASSERT_EQ(Result.MergedVertexCount.size(), 1u);
  EXPECT_EQ(Result.MergedVertexCount[0], 4u);

  ASSERT_EQ(Combined.getVertices(0).size(), 4u);
  EXPECT_EQ(Combined.getVertices(0)[0], (StreamVertex{10.0f}));
  EXPECT_EQ(Combined.getVertices(0)[1], (StreamVertex{11.0f}));
  EXPECT_EQ(Combined.getVertices(0)[2], (StreamVertex{20.0f}));
  EXPECT_EQ(Combined.getVertices(0)[3], (StreamVertex{21.0f}));

  std::vector<StreamStrip> Strips = Combined.getStrips(0);
  ASSERT_EQ(Strips.size(), 2u);
  EXPECT_EQ(Strips[0].Begin, 0u);
  EXPECT_EQ(Strips[0].End, 2u);
  EXPECT_EQ(Strips[1].Begin, 2u);
  EXPECT_EQ(Strips[1].End, 4u);
}

TEST(GeometryStreamCollectionTest, TruncatesWhenTheCombinedBuilderIsTooSmall) {
  constexpr uint32_t PrimitiveCount = 2;
  constexpr uint32_t MaxVerticesPerStream = 2;
  constexpr uint32_t OutputScalarsPerVertex = 1;

  std::vector<float> EmittedVertices(
      PrimitiveCount * MaxVerticesPerStream * OutputScalarsPerVertex, 0.0f);
  std::vector<uint32_t> EmittedVertexCounts = {2, 2};
  std::vector<uint8_t> StripEndsAfter(PrimitiveCount * MaxVerticesPerStream, 0);

  FemeGeometryArgs Args{};
  Args.PrimitiveCount = PrimitiveCount;
  Args.MaxVerticesPerStream = MaxVerticesPerStream;
  Args.OutputScalarsPerVertex = OutputScalarsPerVertex;
  Args.EmittedVertices = EmittedVertices.data();
  Args.EmittedVertexCounts = EmittedVertexCounts.data();
  Args.StripEndsAfter = StripEndsAfter.data();

  // Only room for one primitive's worth of vertices in the combined builder.
  GeometryStreamBuilder Combined(/*StreamCount=*/1,
                                 /*MaxVerticesPerStream=*/2);
  GeometryStreamMergeResult Result = collectGeometryStreams(Args, Combined);

  EXPECT_TRUE(Result.Truncated);
  EXPECT_EQ(Result.MergedVertexCount[0], 2u);
}

} // namespace
