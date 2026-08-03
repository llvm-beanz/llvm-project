//===- RaisedLowering.h - Lower raised IR to AMDGPU conventions -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::amdgpu::RaisedLoweringPass, the pass that
// translates a shader's raised, format-agnostic LLVM IR representation (the
// `llvm.dx.*`/`llvm.spv.*`-flavored output of feme::dxil::OpRaisingPass or a
// SPIR-V `Translator`, see feme/docs/Design.md's "Per-Format Representation
// Strategy" section) into LLVM IR expressed purely in terms the in-tree
// AMDGPU target understands, so the result can be handed directly to
// feme::TargetMachineBackend targeting an `amdgcn-*` triple.
//
// This is deliberately incremental, matching feme::dxil::OpRaisingPass's own
// precedent (see that pass's header comment). It currently covers:
//
//  - Shader entry points: given AMDGPU's kernel calling convention and the
//    `amdgpu-flat-work-group-size` bound their thread group dimensions
//    describe, so a host runtime can actually dispatch them.
//  - Thread/group index queries: the ones with a direct per-component
//    mapping to an AMDGPU intrinsic (`llvm.dx.group.id`,
//    `llvm.dx.thread.id.in.group`), plus the two that do not have one --
//    `llvm.dx.thread.id` and `llvm.dx.flattened.thread.id.in.group` -- which
//    are synthesized from the entry point's thread group dimensions.
//
// It does not yet cover resource-handle ops (`llvm.dx.resource.*`), or
// SPIR-V's raised builtin-variable equivalents, which do not yet exist
// upstream of this pass. Ops not (yet) covered are left unmodified rather
// than erroring, so this pass composes safely with modules that mix lowered
// and not-yet-lowered operations.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_AMDGPU_RAISEDLOWERING_H
#define FEME_TRANSFORMS_AMDGPU_RAISEDLOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace amdgpu {

/// Rewrites the subset of raised, format-agnostic intrinsic calls this pass
/// currently covers into the AMDGPU target intrinsic calls they correspond
/// to, so the result is valid input to an AMDGPU `llvm::TargetMachine`.
class RaisedLoweringPass : public llvm::PassInfoMixin<RaisedLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-amdgpu-lower-raised"; }
};

} // namespace amdgpu
} // namespace feme

#endif // FEME_TRANSFORMS_AMDGPU_RAISEDLOWERING_H
