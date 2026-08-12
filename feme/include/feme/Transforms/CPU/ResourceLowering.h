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
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_RESOURCELOWERING_H
#define FEME_TRANSFORMS_CPU_RESOURCELOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Canonicalizes a raised shader's descriptor-heap resource access into
/// `feme.cpu.resource.*` calls. See the file comment above for current
/// scope.
class ResourceLoweringPass : public llvm::PassInfoMixin<ResourceLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-lower-resources"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_RESOURCELOWERING_H
