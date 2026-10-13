//===- TriangleRasterization.cpp - Fixed-point triangle coverage ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See TriangleRasterization.h for the roadmap L406 context.
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/TriangleRasterization.h"

#include "feme/Graphics/LineRasterization.h"

namespace feme::graphics {

int64_t fixedPointEdgeFn(std::array<float, 2> A, std::array<float, 2> B,
                         std::array<float, 2> P, unsigned Bits) {
  int64_t Ax = toSubpixelCoord(A[0], Bits), Ay = toSubpixelCoord(A[1], Bits);
  int64_t Bx = toSubpixelCoord(B[0], Bits), By = toSubpixelCoord(B[1], Bits);
  int64_t Px = toSubpixelCoord(P[0], Bits), Py = toSubpixelCoord(P[1], Bits);
  return (Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax);
}

FixedPointEdge makeFixedPointEdge(std::array<float, 2> A,
                                  std::array<float, 2> B, unsigned Bits) {
  return {toSubpixelCoord(A[0], Bits), toSubpixelCoord(A[1], Bits),
          toSubpixelCoord(B[0], Bits), toSubpixelCoord(B[1], Bits)};
}

int64_t evaluateFixedPointEdge(const FixedPointEdge &Edge, int64_t Px,
                               int64_t Py) {
  return (Px - Edge.Ax) * (Edge.By - Edge.Ay) -
        (Py - Edge.Ay) * (Edge.Bx - Edge.Ax);
}

} // namespace feme::graphics
