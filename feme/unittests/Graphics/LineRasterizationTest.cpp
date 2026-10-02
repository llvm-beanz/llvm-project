//===- LineRasterizationTest.cpp - Tests for the diamond-exit rule ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/LineRasterization.h"

#include "gtest/gtest.h"

using namespace feme::graphics;

namespace {

constexpr unsigned Bits = 8;

/// The subpixel coordinate of pixel \p Pixel's own center, on the \c Bits
/// -fractional-bit subpixel grid: pixel `k`'s domain is `[k, k+1)`, so its
/// center sits at `k + 0.5`.
int64_t pixelCenter(int32_t Pixel) {
  return toSubpixelCoord(Pixel, Bits) + (1ll << (Bits - 1));
}

SubpixelPoint pixelCenterPoint(int32_t X, int32_t Y) {
  return {pixelCenter(X), pixelCenter(Y)};
}

/// A horizontal Bresenham line visits every pixel center from its start up
/// to, but excluding, its own endpoint (the spec's half-open diamond-exit
/// rule -- see roadmap L324) and nothing off that row.
TEST(LineRasterizationTest, HorizontalLineExcludesItsOwnEndpoint) {
  const SubpixelLineSegment Line{pixelCenterPoint(0, 2),
                                 pixelCenterPoint(3, 2)};

  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(0, 2), Bits));
  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(1, 2), Bits));
  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(2, 2), Bits));
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(3, 2), Bits));

  // A pixel one row off the line is never covered.
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(1, 1), Bits));
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(1, 3), Bits));
}

/// A vertical Bresenham line behaves the same way, along a column.
TEST(LineRasterizationTest, VerticalLineExcludesItsOwnEndpoint) {
  const SubpixelLineSegment Line{pixelCenterPoint(2, 0),
                                 pixelCenterPoint(2, 3)};

  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(2, 0), Bits));
  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(2, 1), Bits));
  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(2, 2), Bits));
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(2, 3), Bits));

  // A pixel one column off the line is never covered.
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(1, 1), Bits));
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(3, 1), Bits));
}

/// A 45-degree diagonal line passes exactly through each walked diamond's
/// own corner (not its edge interior), exercising the corner-case table
/// directly. This reproduces `ExecutorTest.RendersABresenhamDiagonalLine`'s
/// own geometry (screen endpoints (0, 3) and (3, 0)) as an independent
/// cross-check: that test's CTS-confirmed, already-fixed (roadmap L324)
/// DDA-based output lights exactly (0,3), (1,2), (2,1) and excludes (3,0);
/// the exact diamond-exit rule ported here must agree.
TEST(LineRasterizationTest, DiagonalLineMatchesKnownGoodDDAOutput) {
  const SubpixelLineSegment Line{pixelCenterPoint(0, 3),
                                 pixelCenterPoint(3, 0)};

  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(0, 3), Bits));
  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(1, 2), Bits));
  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(2, 1), Bits));
  // `P1`'s own pixel is the segment's final fragment, excluded by the
  // half-open rule.
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(3, 0), Bits));

  // Pixels off the diagonal are untouched.
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(0, 0), Bits));
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(3, 3), Bits));
}

/// A diamond far from the line's bounding box is rejected by the function's
/// own early "broad reject" distance check.
TEST(LineRasterizationTest, FarDiamondIsRejected) {
  const SubpixelLineSegment Line{pixelCenterPoint(0, 0),
                                 pixelCenterPoint(3, 0)};
  EXPECT_FALSE(
      doesLineSegmentExitDiamond(Line, pixelCenterPoint(1000, 1000), Bits));
}

/// A shallow, non-45-degree slope (roadmap L326's own reduction: segment 1
/// of the failing `bresenham_line_strip_wide` case was y-major with a
/// shallow slope, not a 45-degree line) still obeys the half-open rule and
/// only lights the one pixel per major-axis step the exact geometric test
/// selects.
TEST(LineRasterizationTest, ShallowSlopeLineExcludesItsOwnEndpoint) {
  // y-major: dy=3, dx=1 (the minor axis moves once across the whole walk).
  const SubpixelLineSegment Line{pixelCenterPoint(5, 0),
                                 pixelCenterPoint(6, 3)};

  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(5, 0), Bits));
  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(5, 1), Bits));
  EXPECT_TRUE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(6, 2), Bits));
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(6, 3), Bits));

  // Exactly one pixel per row is covered -- the other column in each row is
  // not.
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(6, 0), Bits));
  EXPECT_FALSE(doesLineSegmentExitDiamond(Line, pixelCenterPoint(5, 2), Bits));
}

} // namespace
