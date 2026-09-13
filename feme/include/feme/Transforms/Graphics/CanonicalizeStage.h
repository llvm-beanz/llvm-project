//===- CanonicalizeStage.h - Canonicalize vertex/fragment stage IR -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::CanonicalizeStagePass, the
// `FeMeTransformsGraphics` pass roadmap R20 asks for: rewriting DXIL's
// `loadInput`/`storeOutput` (and the format-agnostic ops raised from other
// DXIL opcodes: `IsHelperLane`, the pull-model interpolation family, and the
// already-raised `llvm.dx.discard`/derivative/quad-read intrinsics) and
// SPIR-V's stage-IO interface-variable accesses into the source-independent
// `feme.stage.*` operation family (feme/include/feme/Core/StageOps.h), per
// the "Canonical stage operations" section of
// feme/docs/FeMeGraphicsDesign.md.
//
// Scoped to the vertex and fragment stages (see that section: "Only
// operations required by implemented stages are legal" -- patch,
// stream-emission, mesh-output and ray operations are later milestones).
// A function that is not a recognized shader-stage entry point (per
// `feme::getShaderStage`) still has its SPIR-V-raised discard/demote-to-
// helper-invocation/derivative/quad-read intrinsic calls rewritten to their
// `feme.stage.*` peers (this needs no stage-IO/signature context), since a
// GLSL/glslang-sourced helper function called (not inlined) from a real
// entry point can itself contain one of these; only its stage-IO
// interface-variable rewriting and signature-building are skipped (roadmap
// H101a: a helper function's `llvm.spv.discard` was previously left
// wholly untouched and reached instruction selection unconverted).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_GRAPHICS_CANONICALIZESTAGE_H
#define FEME_TRANSFORMS_GRAPHICS_CANONICALIZESTAGE_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace graphics {

/// Rewrites a vertex/fragment entry point's DXIL- and SPIR-V-derived stage
/// IR into the canonical `feme.stage.*` operation family, and (for SPIR-V,
/// which does not carry one yet -- see roadmap R19's "Signature reflection"
/// status note) builds and attaches the entry's `feme::EntrySignature` from
/// its stage-IO interface variables.
class CanonicalizeStagePass
    : public llvm::OptionalPassInfoMixin<CanonicalizeStagePass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-graphics-canonicalize-stage"; }
};

} // namespace graphics
} // namespace feme

#endif // FEME_TRANSFORMS_GRAPHICS_CANONICALIZESTAGE_H
