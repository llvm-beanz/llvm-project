//===- ResourceLowering.h - CPU target resource canonicalization -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::ResourceLoweringPass, which re-expresses a
// raised shader's bindless descriptor-heap resource access
// (`llvm.dx.resource.handlefromheap` and its accompanying typed/raw-buffer
// loads and stores) as canonical, type-mangled `feme.cpu.resource.*` calls
// -- see the "Resource Model" -> "Lowering" section of
// feme/docs/FeMeCPUDesign.md and `feme::cpu::ResourceCalls` (the call
// creation/matching helpers this pass uses).
//
// Scope (roadmap milestone 3):
//
//  - Only the two resource kinds `feme::dxil::OpRaisingPass` currently
//    reconstructs handles for -- `TypedBuffer` and `RawBuffer` (which covers
//    both `ByteAddressBuffer` and `StructuredBuffer`; see the "Descriptor
//    heaps" section for how they differ) -- are rewritten. A function using
//    any other resource kind through the heap (a constant buffer, texture or
//    sampler) is left entirely unmodified rather than partially rewritten:
//    constant-buffer canonicalization needs the same multi-return-value
//    `extractvalue` reconstruction mechanism the Status section's Deviation
//    note defers for `WaveActiveBallot` et al., and sampling is a non-goal
//    (see "Goals"/"Non-Goals" in feme/docs/FeMeCPUDesign.md).
//  - Only DXIL produces `llvm.dx.resource.handlefromheap` today --
//    SPIR-V's bindless counterpart (`SPV_EXT_descriptor_heap`, see "Resource
//    Model") has no raised-IR representation yet, so this pass has nothing
//    to rewrite in a SPIR-V-sourced module until that lands upstream.
//  - The new heap/root-constant parameters this pass appends to a rewritten
//    function are threaded through the calls *within* that function only.
//    Full inter-procedural threading -- a resource access reached through a
//    helper function the entry point calls -- is deferred; a function is
//    rewritten only if every resource access it performs is local to it.
//
// Image follow-up: ordinary 2D/array/cube sampling/loading and heap-backed
// scalar FP32 1D/2D/cube (including array) SampleCmpLevelZero lower to shared
// `feme.cpu.image.*` calls. Comparison admission and lowering share the
// predicate below; constant texel offsets are preserved and comparison
// sampler usage is reflected without changing the shader argument ABI.
// Fragment-only 1D/2D/cube (including array) SampleCmp threads
// spatial/direction derivatives and an optional shader MinLod clamp through the
// same comparison helper.
// Fragment-only FP32 1D/2D/3D/cube (including array) SampleBias threads real
// spatial/direction derivatives, bias, supported offsets and optional MinLod
// clamp to the shared sampling helpers.
// FP32 1D/2D/3D/cube (including array) SampleGrad uses supplied
// spatial/direction gradients in any stage; array elements are never
// differentiated, no stage derivatives are synthesized, and supported
// offsets/clamps survive.
// FP32 1D/array and 3D SampleLevel preserve explicit LOD and constant signed
// spatial offsets in any stage with zero derivatives/bias and separate layers.
// Fragment-only FP32 1D/array and 3D Sample use coarse spatial derivatives and
// constant signed spatial offsets with zero bias; layers are not
// differentiated.
// FP32/i32 heap-backed Texture3D Load preserves integer XYZ coordinates,
// explicit mip levels and constant signed XYZ offsets in every stage, without a
// sampler.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_RESOURCELOWERING_H
#define FEME_TRANSFORMS_CPU_RESOURCELOWERING_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class CallInst;
}

namespace feme::cpu {

/// Tests heap-backed FP32/i32 3D texel loading in any stage with
/// three-component i32 coordinates, an i32 mip, constant signed XYZ offsets and
/// scalar/vector results up to the declared texel width (at most four).
bool isSupportedDXILLoad3D(const llvm::CallInst &CI);

/// Tests heap-backed FP32 1D/2D/3D/cube (including array) biased sampling in a
/// fragment stage, with scalar/vector results, supported offsets and optional
/// MinLod clamp.
bool isSupportedDXILSampleBias(const llvm::CallInst &CI);

/// Tests heap-backed FP32 1D/2D/3D/cube (including array) explicit-gradient
/// sampling with a default sampler, spatial/direction gradients excluding array
/// elements, scalar/vector results, supported offsets and optional MinLod
/// clamp. 1D gradients/offsets are scalar; 3D uses three-component spatial
/// gradients/offsets; cubes require an unused
/// (zero/undef/poison) scalar offset. No fragment-stage restriction is needed.
bool isSupportedDXILSampleGrad(const llvm::CallInst &CI);

/// Tests heap-backed FP32 1D/array or 3D explicit-LOD sampling with a default
/// sampler, shape-matched coordinates, scalar/vector results and constant
/// signed spatial offsets (scalar for 1D, three components for 3D). Other
/// ordinary sampling/loading eligibility is unchanged.
bool isSupportedDXILSampleLevel(const llvm::CallInst &CI);

/// Tests heap-backed FP32 1D/array or 3D implicit sampling in a fragment stage
/// with a default sampler, shape-matched coordinates, scalar/vector results and
/// constant signed spatial offsets (scalar for 1D, three components for 3D).
/// Other ordinary shapes/loading are unchanged.
bool isSupportedDXILSample(const llvm::CallInst &CI);

/// Tests the scalar FP32 heap-backed 1D/2D/cube comparison forms supported by
/// both early CPU admission and resource lowering.
bool isSupportedDXILSampleCmpLevelZero(const llvm::CallInst &CI);

/// Tests heap-backed scalar FP32 1D/2D/cube (including array) implicit
/// comparison in a fragment stage, with optional MinLod clamp and supported
/// offsets.
bool isSupportedDXILSampleCmp(const llvm::CallInst &CI);

/// Canonicalizes a raised shader's descriptor-heap resource access into
/// `feme.cpu.resource.*` calls. See the file comment above for current
/// scope.
class ResourceLoweringPass : public llvm::OptionalPassInfoMixin<ResourceLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-lower-resources"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_RESOURCELOWERING_H
