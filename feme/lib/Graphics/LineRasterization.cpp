//===- LineRasterization.cpp - Exact Bresenham diamond-exit rule --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See LineRasterization.h for the roadmap L327 context. This is a direct
// port of VK-GL-CTS's `LineRasterUtil::doesLineSegmentExitDiamond` and its
// helpers (`framework/referencerenderer/rrRasterizer.cpp`), preserving its
// corner-case structure and comments as closely as feme's own naming
// conventions allow, since the diamond-exit rule's correctness hinges on
// those corner cases matching the reference bit-for-bit.
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/LineRasterization.h"

#include "llvm/ADT/bit.h"

#include <cassert>

namespace feme::graphics {

int64_t toSubpixelCoord(float Value, unsigned Bits) {
  return (int64_t)(Value * (float)(1ll << Bits) + (Value < 0.f ? -0.5f : 0.5f));
}

int64_t toSubpixelCoord(int32_t Value, unsigned Bits) {
  return (int64_t)Value << Bits;
}

namespace {

int64_t dot(const SubpixelPoint &A, const SubpixelPoint &B) {
  return A.X * B.X + A.Y * B.Y;
}

int64_t lengthSquared(const SubpixelPoint &A) { return dot(A, A); }

int64_t cross(const SubpixelPoint &A, const SubpixelPoint &B) {
  return A.X * B.Y - A.Y * B.X;
}

enum class LineSide { Intersect, Left, Right };

/// Returns true if \p P is on the left side of \p Line.
bool vertexOnLeftSideOfLine(const SubpixelPoint &P,
                            const SubpixelLineSegment &Line) {
  return cross(Line.direction(), P - Line.P0) < 0;
}

/// Returns true if \p P is on the right side of \p Line.
bool vertexOnRightSideOfLine(const SubpixelPoint &P,
                             const SubpixelLineSegment &Line) {
  return cross(Line.direction(), P - Line.P0) > 0;
}

/// Returns true if \p P is on the infinite line through \p Line.
bool vertexOnLine(const SubpixelPoint &P, const SubpixelLineSegment &Line) {
  return cross(Line.direction(), P - Line.P0) == 0;
}

/// Returns true if \p P is on the line segment \p Line (not just the
/// infinite line through it).
bool vertexOnLineSegment(const SubpixelPoint &P,
                         const SubpixelLineSegment &Line) {
  if (!vertexOnLine(P, Line))
    return false;

  const SubpixelPoint V = Line.direction();
  const SubpixelPoint U1 = P - Line.P0;
  const SubpixelPoint U2 = P - Line.P1;

  if (V.X == 0 && V.Y == 0)
    return false;

  // dot(A->B, A->V) >= 0 and dot(B->A, B->V) >= 0
  return dot(V, U1) >= 0 && dot({-V.X, -V.Y}, U2) >= 0;
}

LineSide getVertexSide(const SubpixelPoint &P,
                       const SubpixelLineSegment &Line) {
  if (vertexOnLeftSideOfLine(P, Line))
    return LineSide::Left;
  if (vertexOnRightSideOfLine(P, Line))
    return LineSide::Right;
  assert(vertexOnLine(P, Line));
  return LineSide::Intersect;
}

/// Returns true if the angle between \p Line and \p CornerExitNormal is in
/// range (-45, 45) degrees.
bool lineInCornerAngleRange(const SubpixelLineSegment &Line,
                            const SubpixelPoint &CornerExitNormal) {
  const SubpixelPoint V = Line.direction();
  const int64_t DotProduct = dot(V, CornerExitNormal);

  // dotProduct > |v1-v0|*|cornerExitNormal|/sqrt(2)
  if (DotProduct < 0)
    return false;
  return 2 * DotProduct * DotProduct >
         lengthSquared(V) * lengthSquared(CornerExitNormal);
}

/// Returns true if the angle between \p Line and \p CornerExitNormal is in
/// range (-135, 135) degrees.
bool lineInCornerOutsideAngleRange(const SubpixelLineSegment &Line,
                                   const SubpixelPoint &CornerExitNormal) {
  const SubpixelPoint V = Line.direction();
  const int64_t DotProduct = dot(V, CornerExitNormal);

  // dotProduct > -|v1-v0|*|cornerExitNormal|/sqrt(2)
  if (DotProduct >= 0)
    return true;
  return 2 * (-DotProduct) * (-DotProduct) <
         lengthSquared(V) * lengthSquared(CornerExitNormal);
}

} // namespace

bool doesLineSegmentExitDiamond(const SubpixelLineSegment &Line,
                                const SubpixelPoint &DiamondCenter,
                                unsigned Bits) {
  const int64_t HalfPixel = 1ll << (Bits - 1);
  const uint64_t PixelSize = (uint64_t)1 << Bits;
  assert(((uint64_t)DiamondCenter.X & (PixelSize - 1)) == (uint64_t)HalfPixel &&
         ((uint64_t)DiamondCenter.Y & (PixelSize - 1)) == (uint64_t)HalfPixel &&
         "DiamondCenter must sit exactly at a pixel center");

  // Reject distant diamonds early.
  {
    const SubpixelPoint U = Line.direction();
    const SubpixelPoint V = DiamondCenter - Line.P0;
    const int64_t CrossProduct = cross(U, V);

    // crossProduct = |p| |l| sin(theta)
    // distanceFromLine = |p| sin(theta)
    // => distanceFromLine = crossProduct / |l|
    //
    // |distanceFromLine| > C
    // => distanceFromLine^2 > C^2
    // => crossProduct^2 / |l|^2 > C^2
    // => crossProduct^2 > |l|^2 * C^2
    const int64_t FloorSqrtMaxInt64 = 3037000499LL; // floor(sqrt(MAX_INT64))

    const int64_t BroadRejectDistance = 2 * HalfPixel;
    const int64_t BroadRejectDistanceSquared =
        BroadRejectDistance * BroadRejectDistance;
    const bool CrossProductOverflows =
        (CrossProduct > FloorSqrtMaxInt64 || CrossProduct < -FloorSqrtMaxInt64);
    const int64_t CrossProductSquared =
        CrossProductOverflows ? 0 : (CrossProduct * CrossProduct);
    const int64_t LineLengthSquared = lengthSquared(U);
    const bool LimitValueCouldOverflow =
        ((64 - llvm::countl_zero((uint64_t)LineLengthSquared)) +
         (64 - llvm::countl_zero((uint64_t)BroadRejectDistanceSquared))) > 63;
    const int64_t LimitValue =
        LimitValueCouldOverflow
            ? 0
            : (LineLengthSquared * BroadRejectDistanceSquared);

    // only cross overflows
    if (CrossProductOverflows && !LimitValueCouldOverflow)
      return false;

    // both representable
    if (!CrossProductOverflows && !LimitValueCouldOverflow) {
      if (CrossProductSquared > LimitValue)
        return false;
    }
  }

  struct DiamondBound {
    SubpixelPoint P0;
    SubpixelPoint P1;
    bool EdgeInclusive; // would a point on the bound be inside the region
  };
  const DiamondBound Bounds[] = {
      {DiamondCenter + SubpixelPoint{0, -HalfPixel},
       DiamondCenter + SubpixelPoint{-HalfPixel, 0}, false},
      {DiamondCenter + SubpixelPoint{-HalfPixel, 0},
       DiamondCenter + SubpixelPoint{0, HalfPixel}, false},
      {DiamondCenter + SubpixelPoint{0, HalfPixel},
       DiamondCenter + SubpixelPoint{HalfPixel, 0}, true},
      {DiamondCenter + SubpixelPoint{HalfPixel, 0},
       DiamondCenter + SubpixelPoint{0, -HalfPixel}, true},
  };

  enum class CornerEdgeCase {
    None, // if the line intersects just a corner, no entering/exiting
    Hit,  // if the line intersects just a corner, entering and exit
    HitFirstQuarter,  // corner hit, either endpoint in (+X,-Y) direction
    HitSecondQuarter, // corner hit, either endpoint in (+X,+Y) direction
  };
  enum class CornerStartCase {
    None,        // the line starting point is outside, no exiting
    Outside,     // exit, if line does not intersect the region
    PositiveY45, // exit, if angle of line and X-axis is in (0, 45] +Y side
    NegativeY45, // exit, if angle of line and X-axis is in [0, 45] -Y side
  };
  enum class CornerEndCase {
    None,                      // end is inside, no exiting
    Direction,                 // exit, if line intersected the region
    DirectionAndFirstQuarter,  // ... or line originates from (+X,-Y)
    DirectionAndSecondQuarter, // ... or line originates from (+X,+Y)
  };
  struct DiamondCorner {
    SubpixelPoint Delta;
    bool PointInclusive; // would a point in this corner intersect the region
    CornerEdgeCase LineBehavior;
    CornerStartCase StartBehavior;
    CornerEndCase EndBehavior;
  };
  const DiamondCorner Corners[] = {
      {{0, -HalfPixel},
       false,
       CornerEdgeCase::HitSecondQuarter,
       CornerStartCase::PositiveY45,
       CornerEndCase::DirectionAndSecondQuarter},
      {{-HalfPixel, 0},
       false,
       CornerEdgeCase::None,
       CornerStartCase::None,
       CornerEndCase::Direction},
      {{0, HalfPixel},
       false,
       CornerEdgeCase::HitFirstQuarter,
       CornerStartCase::NegativeY45,
       CornerEndCase::DirectionAndFirstQuarter},
      {{HalfPixel, 0},
       true,
       CornerEdgeCase::Hit,
       CornerStartCase::Outside,
       CornerEndCase::None},
  };

  // Corner cases at the corners.
  for (const DiamondCorner &Corner : Corners) {
    const SubpixelPoint P = DiamondCenter + Corner.Delta;
    if (!vertexOnLineSegment(P, Line))
      continue;

    // line segment body intersects with the corner
    if (P != Line.P0 && P != Line.P1) {
      if (Corner.LineBehavior == CornerEdgeCase::Hit)
        return true;

      // endpoint in (+X, -Y) (X or Y may be 0) direction <==> x*y <= 0
      if (Corner.LineBehavior == CornerEdgeCase::HitFirstQuarter &&
          (Line.direction().X * Line.direction().Y) <= 0)
        return true;

      // endpoint in (+X, +Y) (Y > 0) direction <==> x*y > 0
      if (Corner.LineBehavior == CornerEdgeCase::HitSecondQuarter &&
          (Line.direction().X * Line.direction().Y) > 0)
        return true;
    }

    // line exits the area at the corner
    if (lineInCornerAngleRange(Line, Corner.Delta)) {
      const bool StartIsInside = Corner.PointInclusive || P != Line.P0;
      const bool EndIsOutside = !Corner.PointInclusive || P != Line.P1;

      // starting point is inside the region and end endpoint is outside
      if (StartIsInside && EndIsOutside)
        return true;
    }

    // line end is at the corner
    if (P == Line.P1) {
      if (Corner.EndBehavior == CornerEndCase::Direction ||
          Corner.EndBehavior == CornerEndCase::DirectionAndFirstQuarter ||
          Corner.EndBehavior == CornerEndCase::DirectionAndSecondQuarter) {
        // did the line intersect the region
        if (lineInCornerAngleRange(Line, Corner.Delta))
          return true;
      }

      // due to the perturbed endpoint, lines at this angle will cause an
      // enter-exit pair
      if (Corner.EndBehavior == CornerEndCase::DirectionAndFirstQuarter &&
          Line.direction().X < 0 && Line.direction().Y > 0)
        return true;
      if (Corner.EndBehavior == CornerEndCase::DirectionAndSecondQuarter &&
          Line.direction().X > 0 && Line.direction().Y > 0)
        return true;
    }

    // line start is at the corner
    if (P == Line.P0) {
      if (Corner.StartBehavior == CornerStartCase::Outside) {
        // if the line is not going inside, it will exit
        if (lineInCornerOutsideAngleRange(Line, Corner.Delta))
          return true;
      }

      // exit, if angle between line vector and X-axis is in (0, 45] +Y side
      if (Corner.StartBehavior == CornerStartCase::PositiveY45 &&
          Line.direction().X > 0 && Line.direction().Y > 0 &&
          Line.direction().Y <= Line.direction().X)
        return true;

      // exit, if angle between line vector and X-axis is in [0, 45] -Y side
      if (Corner.StartBehavior == CornerStartCase::NegativeY45 &&
          Line.direction().X > 0 && Line.direction().Y <= 0 &&
          -Line.direction().Y <= Line.direction().X)
        return true;
    }
  }

  // Does the line intersect the boundary at the left == exits the diamond.
  for (const DiamondBound &Bound : Bounds) {
    const SubpixelLineSegment BoundSegment{Bound.P0, Bound.P1};
    const bool StartVertexInside =
        vertexOnLeftSideOfLine(Line.P0, BoundSegment) ||
        (Bound.EdgeInclusive && vertexOnLine(Line.P0, BoundSegment));
    const bool EndVertexInside =
        vertexOnLeftSideOfLine(Line.P1, BoundSegment) ||
        (Bound.EdgeInclusive && vertexOnLine(Line.P1, BoundSegment));

    // start must be inside this half-space (left or at the inclusive bound)
    if (!StartVertexInside)
      continue;

    // end must be outside of this half-space (right or at the non-inclusive
    // bound)
    if (EndVertexInside)
      continue;

    // Does the line via P0 and P1 intersect the segment Bound.P0-Bound.P1?
    // <==> Bound.P0 and Bound.P1 are on different sides (Left, Right) of the
    // P0-P1 line. Corners are not allowed here, they are checked already.
    const LineSide SideP0 = getVertexSide(Bound.P0, Line);
    const LineSide SideP1 = getVertexSide(Bound.P1, Line);

    if (SideP0 != LineSide::Intersect && SideP1 != LineSide::Intersect &&
        SideP0 != SideP1)
      return true;
  }

  return false;
}

} // namespace feme::graphics
