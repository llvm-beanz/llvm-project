//===- LineRasterization.h - Exact Bresenham diamond-exit rule -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap L327: a standalone, exact implementation of the Vulkan spec's
// Bresenham line-rasterization "diamond-exit rule" ("Basic Line Segment
// Rasterization", `primsrast.adoc`), expressed in fixed-point subpixel
// arithmetic to match the spec's own exact-rasterization requirement.
//
// `Executor.cpp`'s `emitLineSegment` currently approximates this rule with
// a classic integer DDA (error-accumulator) walk, which is exact for
// axis-aligned and 45-degree lines but can select a different step count
// than the literal diamond-exit rule for other slopes -- see roadmap L326,
// which root-caused the resulting `*stipple*.bresenham_line_strip_wide`
// CTS failures to exactly this discrepancy. This file provides the exact
// primitive `L328` will use to replace that approximation; it is not yet
// wired into `emitLineSegment`.
//
// This is a direct, bit-for-bit port of VK-GL-CTS's own reference
// implementation (`LineRasterUtil::doesLineSegmentExitDiamond` and its
// helpers, `framework/referencerenderer/rrRasterizer.cpp`), the
// authoritative oracle the Vulkan CTS verifies Bresenham-mode line
// rasterization against. VK-GL-CTS is licensed under the Apache License
// 2.0, compatible with this project's own Apache-2.0-WITH-LLVM-exception
// license.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_LINERASTERIZATION_H
#define FEME_GRAPHICS_LINERASTERIZATION_H

#include <cstdint>

namespace feme::graphics {

/// Converts a floating-point pixel-space coordinate into a fixed-point
/// subpixel coordinate with \p Bits fractional bits, rounding to nearest
/// (ties away from zero) -- the Vulkan spec's exact-rasterization
/// requirement is defined entirely in terms of this fixed-point
/// representation, not floating point.
int64_t toSubpixelCoord(float Value, unsigned Bits);

/// Converts an integer pixel-space coordinate into a fixed-point subpixel
/// coordinate with \p Bits fractional bits (an exact left-shift, since an
/// integer coordinate has no fractional part to round).
int64_t toSubpixelCoord(int32_t Value, unsigned Bits);

/// A 2D point in fixed-point subpixel coordinates.
struct SubpixelPoint {
  int64_t X = 0;
  int64_t Y = 0;

  friend SubpixelPoint operator+(const SubpixelPoint &A,
                                 const SubpixelPoint &B) {
    return {A.X + B.X, A.Y + B.Y};
  }
  friend SubpixelPoint operator-(const SubpixelPoint &A,
                                 const SubpixelPoint &B) {
    return {A.X - B.X, A.Y - B.Y};
  }
  friend bool operator==(const SubpixelPoint &A, const SubpixelPoint &B) {
    return A.X == B.X && A.Y == B.Y;
  }
  friend bool operator!=(const SubpixelPoint &A, const SubpixelPoint &B) {
    return !(A == B);
  }
};

/// A line segment expressed in fixed-point subpixel coordinates -- the
/// representation the Vulkan spec's diamond-exit rule is defined over.
struct SubpixelLineSegment {
  SubpixelPoint P0;
  SubpixelPoint P1;

  SubpixelPoint direction() const { return P1 - P0; }
};

/// Implements the Vulkan spec's Bresenham line-rasterization "diamond-exit
/// rule": returns true if \p Line exits the unit diamond centered at
/// \p DiamondCenter (also in subpixel coordinates, required to sit exactly
/// at a pixel center on the \p Bits-fractional-bit subpixel grid), i.e.
/// whether the pixel whose center is \p DiamondCenter should be considered
/// covered by \p Line.
bool doesLineSegmentExitDiamond(const SubpixelLineSegment &Line,
                                const SubpixelPoint &DiamondCenter,
                                unsigned Bits);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_LINERASTERIZATION_H
