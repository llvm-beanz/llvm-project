//===- GeometryStreamCollection.h - Compiled-batch stream replay -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares `feme::graphics::collectGeometryStreams`, roadmap R34's
// closing piece for the geometry wrapper: turning a completed
// `feme::cpu::CompiledStage::invokeGeometry` batch's flat, host-owned
// `emit`/`cut` records (`feme::cpu::FemeGeometryArgs::EmittedVertices`/
// `EmittedVertexCounts`/`StripEndsAfter` -- see that struct's own comment and
// GeometryWrapper.cpp's file comment for why they are flat records rather
// than a live `feme::graphics::GeometryStreamBuilder` object) back into real
// `GeometryStreamBuilder`s, one per input primitive, merged in primitive
// (lane) order via the already-tested `mergeGeometryStreamsInLaneOrder`
// (GeometryStream.h). This is what closes that function's own "driving it
// from a real widened invocation" deferral.
//
// This lives in `feme::graphics` rather than `feme::cpu` because it depends
// on `feme::graphics::GeometryStreamBuilder`: `feme::cpu`'s own libraries
// (FeMeTargetCPU) do not depend on `feme::graphics` (FeMeGraphics depends on
// FeMeTargetCPU, not the reverse -- see feme/lib/Graphics/CMakeLists.txt),
// matching how feme/lib/Graphics/Executor.cpp already bridges the two
// layers for the vertex/fragment stages.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_GEOMETRYSTREAMCOLLECTION_H
#define FEME_GRAPHICS_GEOMETRYSTREAMCOLLECTION_H

#include "feme/Graphics/GeometryStream.h"

namespace feme::cpu {
struct FemeGeometryArgs;
} // namespace feme::cpu

namespace feme::graphics {

/// Replays \p Args's flat emitted-vertex records -- one real
/// `GeometryStreamBuilder` per input primitive, in primitive order -- and
/// merges them into \p Combined via `mergeGeometryStreamsInLaneOrder`. \p
/// Args must be the same batch a completed `CompiledStage::invokeGeometry`
/// call populated (its `EmittedVertices`/`EmittedVertexCounts`/
/// `StripEndsAfter` arrays read, not written).
GeometryStreamMergeResult
collectGeometryStreams(const cpu::FemeGeometryArgs &Args,
                       GeometryStreamBuilder &Combined);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_GEOMETRYSTREAMCOLLECTION_H
