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

} // namespace
