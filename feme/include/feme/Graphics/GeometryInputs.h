//===- GeometryInputs.h - Assembled-primitive-to-geometry-batch glue -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares two pieces of host-side glue roadmap R34's open issue
// list calls out as missing: "nothing marshals ... a primitive's assembled
// vertices into a `FemeGeometryInvocation`/`Inputs` block".
//
//  - `buildGeometryInputs` gathers a batch of assembled primitives' vertex
//    attributes out of a vertex-stage batch's structure-of-arrays output
//    storage (`feme::cpu::FemeVertexArgs::Outputs`) into
//    `feme::cpu::FemeGeometryArgs::Inputs`'s own structure-of-arrays layout
//    (primitive-major, `primitive * VerticesPerPrimitive + vertexInPrimitive`
//    -- see FemeGeometryArgs's comment in RuntimeABI.h). The caller supplies
//    which vertex-output slot fills each geometry-input slot -- the same
//    index lists `feme::graphics::splitListPrimitiveAdjacency`/
//    `splitStripPrimitiveAdjacency` (Pipeline.h) already produce per
//    primitive -- so this function is pure gather, independent of topology
//    or adjacency.
//  - `buildGeometryInvocations` fills one `feme::cpu::FemeGeometryInvocation`
//    per primitive with its `SV_PrimitiveID`, mirroring
//    `buildDomainInvocations` (DomainInvocations.h)'s role for the domain
//    stage.
//
// This lives in `feme::graphics` rather than `feme::cpu` for the same
// layering reason DomainInvocations.h and GeometryStreamCollection.h do:
// `FeMeTargetCPU` does not depend on `FeMeGraphics` (see
// feme/lib/Graphics/CMakeLists.txt), and callers assembling primitives
// already live in `feme::graphics` (Pipeline.h).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_GEOMETRYINPUTS_H
#define FEME_GRAPHICS_GEOMETRYINPUTS_H

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <vector>

namespace feme::cpu {
struct FemeGeometryInvocation;
} // namespace feme::cpu

namespace feme::graphics {

/// Gathers \p VertexSlots.size() vertices' worth of attributes (\p
/// ScalarsPerVertex scalars each) out of \p VertexOutputs -- a vertex-stage
/// batch's structure-of-arrays output storage, \p ScalarsPerVertex scalars
/// per vertex-output slot -- into the layout `FemeGeometryArgs::Inputs`
/// expects. \p VertexSlots names, for every geometry-input slot in
/// primitive-major order, which `VertexOutputs` slot supplies it: entry `P *
/// VerticesPerPrimitive + V` is the vertex-output slot for primitive `P`'s
/// vertex `V` (matching a primitive's assembled vertex order, adjacency
/// vertices included where the topology has them).
///
/// Returns a flat vector of `VertexSlots.size() * ScalarsPerVertex` scalars,
/// directly usable as `FemeGeometryArgs::Inputs`. An out-of-range entry in
/// \p VertexSlots (>= `VertexOutputs.size() / ScalarsPerVertex`) gathers as
/// zero rather than reading out of bounds.
std::vector<float> buildGeometryInputs(llvm::ArrayRef<uint32_t> VertexSlots,
                                       llvm::ArrayRef<float> VertexOutputs,
                                       uint32_t ScalarsPerVertex);

/// Builds one `feme::cpu::FemeGeometryInvocation` per entry of \p
/// PrimitiveIDs, recording that entry as the invocation's `SV_PrimitiveID`,
/// in the same order, for use as a `FemeGeometryArgs::Invocations` array
/// (see FemeGeometryArgs's comment).
std::vector<cpu::FemeGeometryInvocation>
buildGeometryInvocations(llvm::ArrayRef<uint32_t> PrimitiveIDs);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_GEOMETRYINPUTS_H
