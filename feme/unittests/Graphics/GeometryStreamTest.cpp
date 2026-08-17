//===- GeometryStreamTest.cpp - Tests for GeometryStreamBuilder ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/GeometryStream.h"

#include "gtest/gtest.h"

using namespace feme::graphics;

namespace {

TEST(GeometryStreamTest, EmitAppendsVerticesInOrder) {
  GeometryStreamBuilder Builder(/*StreamCount=*/1,
                                /*MaxVerticesPerStream=*/8);
  EXPECT_TRUE(Builder.emit(0, {1.0f, 2.0f}));
  EXPECT_TRUE(Builder.emit(0, {3.0f, 4.0f}));
  ASSERT_EQ(Builder.getVertices(0).size(), 2u);
  EXPECT_EQ(Builder.getVertices(0)[0], (StreamVertex{1.0f, 2.0f}));
  EXPECT_EQ(Builder.getVertices(0)[1], (StreamVertex{3.0f, 4.0f}));
}

TEST(GeometryStreamTest, EmitRejectsAnOutOfRangeStream) {
  GeometryStreamBuilder Builder(1, 8);
  EXPECT_FALSE(Builder.emit(1, {1.0f}));
}

TEST(GeometryStreamTest, EmitRejectsBeyondTheDeclaredMaximum) {
  GeometryStreamBuilder Builder(1, /*MaxVerticesPerStream=*/2);
  EXPECT_TRUE(Builder.emit(0, {0.0f}));
  EXPECT_TRUE(Builder.emit(0, {0.0f}));
  EXPECT_FALSE(Builder.emit(0, {0.0f}));
  EXPECT_EQ(Builder.getVertices(0).size(), 2u);
}

TEST(GeometryStreamTest, CutClosesAStripWithoutStartingANewVertex) {
  GeometryStreamBuilder Builder(1, 8);
  Builder.emit(0, {0.0f});
  Builder.emit(0, {1.0f});
  Builder.cut(0);
  Builder.emit(0, {2.0f});
  Builder.emit(0, {3.0f});
  Builder.emit(0, {4.0f});

  std::vector<StreamStrip> Strips = Builder.getStrips(0);
  ASSERT_EQ(Strips.size(), 2u);
  EXPECT_EQ(Strips[0].Begin, 0u);
  EXPECT_EQ(Strips[0].End, 2u);
  // The trailing strip is still open (no closing `cut`) but is still
  // reported, per `getStrips`'s own comment.
  EXPECT_EQ(Strips[1].Begin, 2u);
  EXPECT_EQ(Strips[1].End, 5u);
}

TEST(GeometryStreamTest, RepeatedCutWithNoInterveningEmitIsANoOp) {
  GeometryStreamBuilder Builder(1, 8);
  Builder.emit(0, {0.0f});
  Builder.cut(0);
  Builder.cut(0);
  Builder.cut(0);
  EXPECT_EQ(Builder.getStrips(0).size(), 1u);
}

TEST(GeometryStreamTest, StreamsAreIndependent) {
  GeometryStreamBuilder Builder(/*StreamCount=*/2, 8);
  Builder.emit(0, {1.0f});
  Builder.emit(1, {2.0f});
  Builder.emit(1, {3.0f});
  EXPECT_EQ(Builder.getVertices(0).size(), 1u);
  EXPECT_EQ(Builder.getVertices(1).size(), 2u);
}

TEST(GeometryStreamTest, OutOfRangeStreamQueriesReturnEmpty) {
  GeometryStreamBuilder Builder(1, 8);
  EXPECT_TRUE(Builder.getVertices(5).empty());
  EXPECT_TRUE(Builder.getStrips(5).empty());
}

TEST(GeometryStreamMergeTest, MergesLanesInOrderAcrossOneStream) {
  GeometryStreamBuilder Lane0(1, 8);
  Lane0.emit(0, {0.0f});
  Lane0.emit(0, {1.0f});
  GeometryStreamBuilder Lane1(1, 8);
  Lane1.emit(0, {2.0f});

  GeometryStreamBuilder Combined(1, 8);
  GeometryStreamMergeResult Result =
      mergeGeometryStreamsInLaneOrder({Lane0, Lane1}, Combined);

  EXPECT_FALSE(Result.Truncated);
  ASSERT_EQ(Result.MergedVertexCount.size(), 1u);
  EXPECT_EQ(Result.MergedVertexCount[0], 3u);
  ASSERT_EQ(Combined.getVertices(0).size(), 3u);
  EXPECT_EQ(Combined.getVertices(0)[0], (StreamVertex{0.0f}));
  EXPECT_EQ(Combined.getVertices(0)[1], (StreamVertex{1.0f}));
  EXPECT_EQ(Combined.getVertices(0)[2], (StreamVertex{2.0f}));
}

TEST(GeometryStreamMergeTest, ForcesAStripBoundaryAtEveryLaneEdge) {
  // Lane 0 leaves its strip open (no explicit cut); Lane 1's own strip must
  // still not merge with it.
  GeometryStreamBuilder Lane0(1, 8);
  Lane0.emit(0, {0.0f});
  Lane0.emit(0, {1.0f});
  GeometryStreamBuilder Lane1(1, 8);
  Lane1.emit(0, {2.0f});
  Lane1.emit(0, {3.0f});

  GeometryStreamBuilder Combined(1, 8);
  mergeGeometryStreamsInLaneOrder({Lane0, Lane1}, Combined);

  std::vector<StreamStrip> Strips = Combined.getStrips(0);
  ASSERT_EQ(Strips.size(), 2u);
  EXPECT_EQ(Strips[0].Begin, 0u);
  EXPECT_EQ(Strips[0].End, 2u);
  EXPECT_EQ(Strips[1].Begin, 2u);
  EXPECT_EQ(Strips[1].End, 4u);
}

TEST(GeometryStreamMergeTest, PreservesAWithinLaneCutAsItsOwnStrip) {
  GeometryStreamBuilder Lane0(1, 8);
  Lane0.emit(0, {0.0f});
  Lane0.cut(0);
  Lane0.emit(0, {1.0f});
  Lane0.emit(0, {2.0f});

  GeometryStreamBuilder Combined(1, 8);
  mergeGeometryStreamsInLaneOrder({Lane0}, Combined);

  std::vector<StreamStrip> Strips = Combined.getStrips(0);
  ASSERT_EQ(Strips.size(), 2u);
  EXPECT_EQ(Strips[0].Begin, 0u);
  EXPECT_EQ(Strips[0].End, 1u);
  EXPECT_EQ(Strips[1].Begin, 1u);
  EXPECT_EQ(Strips[1].End, 3u);
}

TEST(GeometryStreamMergeTest, TruncatesRatherThanOverflowingCombinedCapacity) {
  GeometryStreamBuilder Lane0(1, /*MaxVerticesPerStream=*/3);
  Lane0.emit(0, {0.0f});
  Lane0.emit(0, {1.0f});
  GeometryStreamBuilder Lane1(1, 3);
  Lane1.emit(0, {2.0f});
  Lane1.emit(0, {3.0f});
  GeometryStreamBuilder Lane2(1, 3);
  Lane2.emit(0, {4.0f});

  GeometryStreamBuilder Combined(1, /*MaxVerticesPerStream=*/3);
  GeometryStreamMergeResult Result =
      mergeGeometryStreamsInLaneOrder({Lane0, Lane1, Lane2}, Combined);

  // Lane0's 2 vertices fit; Lane1's 2 more would overflow the bound of 3
  // and are rejected wholesale (no partial strip); Lane2, though it alone
  // would fit in the 1 remaining slot, is also dropped, since the
  // reservation is monotonic and lane order must not be violated.
  EXPECT_TRUE(Result.Truncated);
  EXPECT_EQ(Result.MergedVertexCount[0], 2u);
  ASSERT_EQ(Combined.getVertices(0).size(), 2u);
  EXPECT_EQ(Combined.getVertices(0)[0], (StreamVertex{0.0f}));
  EXPECT_EQ(Combined.getVertices(0)[1], (StreamVertex{1.0f}));
}

TEST(GeometryStreamMergeTest, StreamsAreMergedIndependently) {
  GeometryStreamBuilder Lane0(/*StreamCount=*/2, 8);
  Lane0.emit(0, {0.0f});
  Lane0.emit(1, {10.0f});
  GeometryStreamBuilder Lane1(2, 8);
  Lane1.emit(0, {1.0f});
  Lane1.emit(1, {11.0f});

  GeometryStreamBuilder Combined(2, 8);
  GeometryStreamMergeResult Result =
      mergeGeometryStreamsInLaneOrder({Lane0, Lane1}, Combined);

  EXPECT_FALSE(Result.Truncated);
  EXPECT_EQ(Combined.getVertices(0).size(), 2u);
  EXPECT_EQ(Combined.getVertices(1).size(), 2u);
  EXPECT_EQ(Combined.getVertices(1)[0], (StreamVertex{10.0f}));
  EXPECT_EQ(Combined.getVertices(1)[1], (StreamVertex{11.0f}));
}

TEST(GeometryStreamMergeTest, EmptyLanesContributeNothing) {
  GeometryStreamBuilder Lane0(1, 8);
  GeometryStreamBuilder Combined(1, 8);
  GeometryStreamMergeResult Result =
      mergeGeometryStreamsInLaneOrder({Lane0}, Combined);
  EXPECT_FALSE(Result.Truncated);
  EXPECT_EQ(Result.MergedVertexCount[0], 0u);
  EXPECT_TRUE(Combined.getVertices(0).empty());
  EXPECT_TRUE(Combined.getStrips(0).empty());
}

} // namespace
