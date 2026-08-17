//===- Executor.h - FeMe software graphics executor -------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::executeDraws, the software graphics
// executor roadmap R32 ("Basic triangle pipeline") adds: it walks a
// `GraphicsPipeline`/`PreparedDraw` pair (Pipeline.h/PreparedDraw.h) through
// the "Draw flow" feme/docs/FeMeGraphicsDesign.md describes --
//
//   validate draw and materialize descriptors
//     -> assemble vertex invocation keys
//     -> fetch/convert attributes
//     -> run vertex waves
//     -> assemble primitives
//     -> clip in homogeneous coordinates
//     -> divide, viewport transform, cull, and set up edges/planes
//     -> bin primitives into tiles
//     -> generate covered 2x2 quads with helpers
//     -> interpolate linked inputs
//     -> run fragment waves
//     -> perform required late tests and output merge
//
// -- scoped, per the roadmap, to one triangle-list/triangle-strip draw, one
// color attachment, one viewport/scissor, and no multisampling. See
// Executor.cpp's file comment for the scope decisions this milestone makes
// and defers to later roadmap steps (R33+).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_EXECUTOR_H
#define FEME_GRAPHICS_EXECUTOR_H

#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::graphics {

class GraphicsPipeline;
struct PreparedDraw;

/// Executes every `PreparedDraw::Draws` command against \p Pipeline,
/// following the "Draw flow" above, and writes surviving fragments' colors
/// into \p Draw's color attachments. Returns an `Error` for a topology,
/// pipeline state, or vertex-attribute binding this milestone does not
/// implement (see the file comment above), rather than silently
/// misrendering.
///
/// \p WorkerCount selects the tile scheduling roadmap R33 adds ("Tiling
/// and scheduling" in feme/docs/FeMeGraphicsDesign.md): `1` (the default)
/// processes tiles sequentially in row-major order; a higher value
/// dispatches tiles across that many worker threads. Every tile owns a
/// disjoint attachment region, so the result is bit-identical regardless
/// of \p WorkerCount or tile processing order -- the "identical
/// deterministic output across worker counts and tile traversal orders"
/// metamorphic property "Determinism and Reference Execution" and
/// feme/docs/Roadmap.md's §2.6.3 both require.
llvm::Error executeDraws(const GraphicsPipeline &Pipeline,
                         const PreparedDraw &Draw, uint32_t WorkerCount = 1);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_EXECUTOR_H
