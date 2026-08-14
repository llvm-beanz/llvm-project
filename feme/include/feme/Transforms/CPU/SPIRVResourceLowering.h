//===- SPIRVResourceLowering.h - SPIR-V bound resource emulation -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::SPIRVResourceLoweringPass, the SPIR-V
// counterpart to `feme::cpu::BoundResourceNormalizationPass` +
// `feme::cpu::ResourceLoweringPass`: it rewrites a SPIR-V-sourced module's
// `spirv.VulkanBuffer` resource access directly into the canonical,
// type-mangled `feme.cpu.resource.*` calls those two DXIL-facing passes
// jointly produce (see `feme::cpu::ResourceCalls`), and attaches the same
// `feme.cpu.resources`/`feme.cpu.bound_resources` metadata they do -- so
// every later stage of the CPU pipeline, and the host-facing
// `feme::cpu::ResourceInfo`/`feme::cpu::ResourceHeap` machinery
// `feme::cpu::JITEngine`/`feme-run` use to supply a dispatch's bound
// resources, need no SPIR-V-specific case of their own at all.
//
// SPIR-V has no bindless descriptor-heap counterpart to DXIL's
// `ResourceDescriptorHeap` (see "Resource Model" in
// feme/docs/FeMeCPUDesign.md's SPIR-V bullet), so every SPIR-V resource is a
// traditional, register-bound one -- there is no dynamic-heap case to
// distinguish, unlike the DXIL side's two-pass split around
// `feme::cpu::checkSupportedRaisedOps`. This pass therefore normalizes and
// lowers a bound handle in one step rather than two: a (descriptor set,
// binding) identity plays the same role DXIL's (register space, register)
// does (see `feme::spirv::RaisedLoweringPass`'s header comment for the
// SPIR-V -> raised direction's own use of that same correspondence), always
// with an implicit range size of 1 -- SPIR-V has no notion of an array of
// resources bound to one descriptor slot the way a DXIL `register(t0,
// space0, numDescriptors=N)` range does.
//
// Scope (roadmap step R10, see feme/docs/Roadmap.md's §1.2/§2.4.2):
//
//  - Only a `StorageBuffer`-derived `spirv.VulkanBuffer` handle (an
//    `RWStructuredBuffer<T>`/`StructuredBuffer<T>` in HLSL, see
//    `feme::spirv::convertBufferBlockType` in SPIRVToLLVMPatterns.cpp) is
//    normalized; an image/sampler handle (`Buffer`/`RWBuffer<T>`/
//    `Texture*`/`Sampler*`) is left untouched, matching the DXIL side's own
//    "typed and raw buffers only" narrowing (constant buffers, textures,
//    and samplers are not yet covered there either -- see
//    `feme::cpu::ResourceLoweringPass`'s header comment).
//  - Only the access shape `feme::spirv::StorageBufferAccessChainPattern`
//    itself produces for a flat (non-aggregate) buffer element -- a direct
//    `llvm.spv.resource.getpointer` followed immediately by an ordinary
//    `load`/`store`, with no intervening `getelementptr` into the element's
//    own fields -- is rewritten. A structured-buffer element with fields
//    accessed individually is left untouched, exactly as
//    `feme::cpu::ResourceLoweringPass` leaves any access shape it does not
//    itself model.
//  - As with the DXIL passes this mirrors, an unsupported access shape or a
//    conflicting re-declaration of the same (descriptor set, binding)
//    identity (two handles disagreeing about the buffer element's stride)
//    leaves every handle at that identity un-normalized, so
//    `feme::cpu::checkSupportedRaisedOps` still rejects it.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_SPIRVRESOURCELOWERING_H
#define FEME_TRANSFORMS_CPU_SPIRVRESOURCELOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Normalizes and lowers SPIR-V `spirv.VulkanBuffer` storage-buffer access
/// directly into the same canonical `feme.cpu.resource.*` calls the DXIL
/// `BoundResourceNormalizationPass` + `ResourceLoweringPass` pair produces.
/// See the file comment above for current scope.
class SPIRVResourceLoweringPass
    : public llvm::PassInfoMixin<SPIRVResourceLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-lower-spirv-resources"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_SPIRVRESOURCELOWERING_H
