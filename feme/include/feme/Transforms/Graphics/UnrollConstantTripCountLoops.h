//===- UnrollConstantTripCountLoops.h - Force-unroll small loops -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::UnrollConstantTripCountStageLoopsPass
// (roadmap L128/L128(a)/L128(b)): forces a full unroll of any loop, in a
// Vertex or Fragment entry point, whose trip count `ScalarEvolution` can
// prove is a small compile-time constant.
//
// Why this exists, and why it runs where it does: `CanonicalizeStagePass`
// (CanonicalizeStage.h) recognizes a plain (non-matrix) arrayed Vertex/
// Fragment stage-IO global (e.g. GLSL's `layout(location = N) in vec4
// attr[K]`) as one `SignatureElement` with `RowCount == K`, addressed
// exactly like a real matrix's own rows -- but only when each `attr[i]`
// access reaches it as a separate, already-constant-indexed GEP. SPIR-V/
// glslang always emits a real matrix's row accesses that way (unrolled at
// GLSL-to-SPIR-V translation time, one access per row), so a `RowCount<=4`
// real matrix never needs this pass. A specialization-constant-sized
// array (e.g. `attr[numAttributes-1]`, `dEQP-VK.pipeline.monolithic.
// vertex_input.max_attributes.query_max_attributes.*`'s own shape) is
// different: even once `feme::vulkan::patchSpecializationConstants`
// resolves `numAttributes` to a concrete compile-time value (fixing the
// array's own declared length), glslang itself never unrolls a real GLSL
// `for` loop reading it -- so `CanonicalizeStagePass` sees a genuinely
// non-constant, loop-carried SSA index and cannot resolve which array
// slot -- and therefore which signature element row -- is being read,
// leaving the element out of the built `EntrySignature` entirely (roadmap
// L128(b)'s full root-cause writeup). Running this pass immediately
// before `feme::vulkan::compileGraphicsStage`'s own `CanonicalizeStagePass`
// call (GraphicsPipeline.cpp, not `feme::cpu::runPipeline`/Pipeline.cpp,
// which only ever sees the module *after* that authoritative signature-
// building step has already run and is therefore too late to help) turns
// exactly this shape's loop into `K` separate, constant-indexed accesses
// ahead of time, matching the shape `CanonicalizeStagePass` already knows
// how to canonicalize.
//
// Scoped to Vertex/Fragment entry points only (`getShaderStage`), matching
// exactly where `CanonicalizeStagePass` builds a plain-arrayed-global's
// `EntrySignature` from scratch (`addElements`/`addElement` in
// CanonicalizeStage.cpp) -- Hull/Domain/Geometry/Mesh stage-IO arrays are a
// different shape entirely (a genuine, always-dynamic per-vertex/per-
// primitive index, addressed via `feme.stage.*`'s own `Vertex` operand,
// see `getDynamicVertexIndexedAccess`), where forcing a loop to unroll
// would be pointless (the real per-invocation index is a runtime value
// this pass's `ScalarEvolution`-provable-constant-trip-count test can
// never resolve to a single case at compile time) rather than merely
// unhelpful.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_GRAPHICS_UNROLLCONSTANTTRIPCOUNTLOOPS_H
#define FEME_TRANSFORMS_GRAPHICS_UNROLLCONSTANTTRIPCOUNTLOOPS_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace graphics {

/// Forces a full unroll of every loop, in a Vertex or Fragment shader-stage
/// entry point, whose exact trip count `ScalarEvolution::
/// getSmallConstantTripCount` can prove is a compile-time constant no
/// larger than an internal cap -- see this file's own header comment.
class UnrollConstantTripCountStageLoopsPass
    : public llvm::OptionalPassInfoMixin<
          UnrollConstantTripCountStageLoopsPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() {
    return "feme-graphics-unroll-constant-trip-count-stage-loops";
  }
};

} // namespace graphics
} // namespace feme

#endif // FEME_TRANSFORMS_GRAPHICS_UNROLLCONSTANTTRIPCOUNTLOOPS_H
