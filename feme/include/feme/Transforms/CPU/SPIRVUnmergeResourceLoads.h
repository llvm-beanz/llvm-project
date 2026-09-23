//===- SPIRVUnmergeResourceLoads.h - Split phi-merged resource loads -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::SPIRVUnmergeResourceLoadsPass (roadmap
// L174), which reverses a specific, otherwise-ordinary InstCombine
// canonicalization (`InstCombinerImpl::foldPHIArgLoadIntoPHI`, see
// `InstCombinePHI.cpp`) whenever it has been applied to a `load` whose
// per-predecessor pointer operands are each a distinct
// `llvm.spv.resource.getpointer` call.
//
// Why this matters: every one of `feme::cpu::SPIRVResourceLoweringPass`'s
// resource-access matchers (`hasOnlySupportedStorageImageUses` and its
// buffer-side analogs) requires a `llvm.spv.resource.getpointer` call's
// result to be used *directly* by a `load`/`store` in the getpointer
// call's own block -- never indirected through a `PHINode`. A
// `vertex`/`fragment`/`vertex_fragment`-stage graphics pipeline runs its
// own `feme::graphics::UnrollConstantTripCountStageLoopsPass` (see that
// pass's header comment, roadmap L128) *before* ever reaching this
// pipeline, whose own `InstCombinePass` cleans up the loop it just force-
// unrolled -- but the CTS's own `quadrant_id`-based if/else-if resource-
// access idiom (`vktBindingShaderAccessTests.cpp`'s
// `genResourceAccessSource`, used identically by every
// `binding_model.shader_access.*` binding-type generator) has, by that
// point, exactly the shape InstCombine's `foldPHIArgLoadIntoPHI` looks
// for: a `PHINode` whose every incoming value is a `load` of the *same*
// type from a *different* pointer defined in its own predecessor block.
// InstCombine folds this into the inverse shape -- a `PHINode` of the
// *pointers* instead, with one shared `load` reading through it at the
// merge point -- which is, in general, a perfectly ordinary and
// beneficial canonicalization, but defeats `SPIRVResourceLoweringPass`'s
// deliberately narrow "flat access only" matchers here (roadmap L155's
// root cause: `storage_image`/`storage_buffer(_dynamic)`/
// `uniform_buffer(_dynamic)`/texel-buffer/`with_push*` bindings all fail
// identically in `vertex`/`fragment`/`vertex_fragment` stage, never
// `compute`, which has no `UnrollConstantTripCountStageLoopsPass`-driven
// `InstCombinePass` run over it upstream of `feme::cpu::runPipeline` at
// all).
//
// Running this pass early in `feme::cpu::runPipeline`'s `Normalize`
// sequence, before `feme::cpu::BoundResourceNormalizationPass`/
// `feme::cpu::SPIRVResourceLoweringPass`, restores the "flat access"
// shape those passes require: it finds every `PHINode` of pointer type
// all of whose incoming values are direct `llvm.spv.resource.getpointer`
// calls (each the last non-terminator instruction of its own incoming
// block, so hoisting a load from the merge block back into that block is
// never observably different -- no store or other memory effect can
// intervene on that path), then, for each `load` using that `PHINode` as
// its pointer operand, clones the load into every incoming block (right
// after its own `getpointer` call) and replaces the original `load` with
// a new `PHINode` of the newly cloned loads' *values* -- i.e. exactly
// undoes `foldPHIArgLoadIntoPHI`'s own transform, but only for this
// specific, narrowly-matched shape. Scoped to SPIR-V's
// `llvm.spv.resource.getpointer` only (DXIL has no equivalent
// intermediate "get a pointer to this binding" call this bug could ever
// apply to); a no-op for a DXIL-sourced module.
//
// A qualifying `load` need not live in the merge block itself: an earlier
// pass may have sunk it into a later block (e.g. a `vertex_fragment`
// pipeline's own extra, outer `if` choosing between this stage's own
// resource access and a value passed through from the other stage --
// roadmap L175/L176). This pass rewrites such a sunk `load` too, as long
// as its block is reachable from the merge block via a single,
// unbranched chain of blocks (each having exactly one predecessor) --
// which proves the merge block dominates it and that no memory write
// intervenes anywhere along that one path -- since the newly built
// value-`PHINode` (placed at the merge block, where the old pointer-phi
// was) then already dominates the sunk `load`'s site and can simply
// replace it directly, without needing to rebuild any further merge
// along the way. A `load` reachable only via more than one control-flow
// path (a second, independent join downstream of this one) is left
// untouched -- this pass has no way to re-merge a value at that second
// join point.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_SPIRVUNMERGERESOURCELOADS_H
#define FEME_TRANSFORMS_CPU_SPIRVUNMERGERESOURCELOADS_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Splits a `load` whose pointer operand is a `PHINode` all of whose
/// incoming values are `llvm.spv.resource.getpointer` calls (one per
/// incoming block) back into one `load` per incoming block plus a
/// `PHINode` of the loaded *values* -- see the file comment above for
/// the full rationale (roadmap L174/L155).
class SPIRVUnmergeResourceLoadsPass
    : public llvm::OptionalPassInfoMixin<SPIRVUnmergeResourceLoadsPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() {
    return "feme-cpu-spirv-unmerge-resource-loads";
  }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_SPIRVUNMERGERESOURCELOADS_H
