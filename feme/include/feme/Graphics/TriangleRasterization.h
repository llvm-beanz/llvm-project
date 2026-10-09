//===- TriangleRasterization.h - Fixed-point triangle coverage -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap L406: a fixed-point (subpixel-precision) directed-edge function
// for the triangle coverage test, mirroring VK-GL-CTS's own reference
// rasterizer (`TriangleRasterizer::init`/`evaluateEdge`,
// `framework/referencerenderer/rrRasterizer.cpp`) rather than
// `Executor.cpp`'s pre-existing `edgeFnD`, which evaluates the same
// directed-edge formula in unlimited `double` precision on raw `float`
// vertex coordinates.
//
// Roadmap L402 root-caused why that matters: this device advertises
// `subPixelPrecisionBits == 4` (`feme::graphics::LineSubPixelPrecisionBits`,
// shared with the line rasterizer's own still-partially-landed L326-L328
// work), and VK-GL-CTS's reference rasterizer snaps every vertex to that
// exact 4-bit fixed-point grid before its own edge test -- so a sample
// landing within a few thousandths of a pixel of a triangle edge can
// receive a different, but individually "correct", inside/outside answer
// at full `double` precision than it does at the device's own advertised
// (coarser) precision. `L402`'s own near-tie case
// (`subpass_dependencies.late_fragment_tests`, triangle 24 covering pixel
// `(8, 0)` in `render_size_32_32`/`subpass_count_2`/`d24_unorm_s8_uint`)
// is the regression test this file's own unit tests include.
//
// This reuses `toSubpixelCoord`/`LineSubPixelPrecisionBits` from
// `LineRasterization.h` rather than redefining them, so the triangle and
// line rasterizers can never independently drift on what "the device's
// advertised subpixel precision" actually means.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_TRIANGLERASTERIZATION_H
#define FEME_GRAPHICS_TRIANGLERASTERIZATION_H

#include <array>
#include <cstdint>

namespace feme::graphics {

/// Directed-edge function for the edge `A -> B` evaluated at `P`, in
/// fixed-point subpixel coordinates with \p Bits fractional bits (see
/// `toSubpixelCoord` in `LineRasterization.h`) rather than floating point.
/// Same sign convention as `Executor.cpp`'s own `edgeFn`/`edgeFnD` (a
/// positively-wound triangle, guaranteed by its caller, is inside wherever
/// all three of its own directed edges' values are non-negative): for a
/// positively-wound triangle `edgeFnD(A, B, P)` and
/// `fixedPointEdgeFn(A, B, P, Bits)` agree in sign everywhere except the
/// narrow band within one subpixel-grid cell of the true geometric edge,
/// where this function's coarser, device-precision-matching quantization
/// is authoritative (see this header's own file comment).
int64_t fixedPointEdgeFn(std::array<float, 2> A, std::array<float, 2> B,
                         std::array<float, 2> P, unsigned Bits);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_TRIANGLERASTERIZATION_H
