//===- LayeredRenderingTest.cpp - Tests for layered rendering -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/LayeredRendering.h"

#include "gtest/gtest.h"

using namespace feme::graphics;

namespace {

TEST(LayeredRenderingTest, ResolvesAnInRangeLayer) {
  EXPECT_EQ(resolveRenderTargetArrayLayer(0, 4), 0u);
  EXPECT_EQ(resolveRenderTargetArrayLayer(3, 4), 3u);
}

TEST(LayeredRenderingTest, DiscardsANegativeIndex) {
  EXPECT_EQ(resolveRenderTargetArrayLayer(-1, 4), std::nullopt);
}

TEST(LayeredRenderingTest, DiscardsAnIndexAtOrPastLayerCount) {
  EXPECT_EQ(resolveRenderTargetArrayLayer(4, 4), std::nullopt);
  EXPECT_EQ(resolveRenderTargetArrayLayer(100, 4), std::nullopt);
}

TEST(LayeredRenderingTest, ANonLayeredAttachmentOnlyAcceptsLayerZero) {
  EXPECT_EQ(resolveRenderTargetArrayLayer(0, /*LayerCount=*/1), 0u);
  EXPECT_EQ(resolveRenderTargetArrayLayer(1, /*LayerCount=*/1), std::nullopt);
}

TEST(LayeredRenderingTest, ByteOffsetIsLayerMajor) {
  EXPECT_EQ(getAttachmentLayerByteOffset(0, 256), 0u);
  EXPECT_EQ(getAttachmentLayerByteOffset(3, 256), 768u);
}

} // namespace
