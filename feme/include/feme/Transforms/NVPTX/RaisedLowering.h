//===- RaisedLowering.h - Lower raised IR to NVPTX conventions -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::nvptx::RaisedLoweringPass, the NVPTX counterpart
// to feme::amdgpu::RaisedLoweringPass (see that pass's own header comment
// for the general shape this mirrors): it rewrites a shader's raised,
// format-agnostic LLVM IR (the `llvm.dx.*`/`llvm.spv.*`-flavored output of
// feme::dxil::OpRaisingPass or a SPIR-V `Translator`) into LLVM IR expressed
// purely in terms the in-tree NVPTX target understands, so the result can be
// handed directly to feme::TargetMachineBackend targeting an `nvptx64-*`
// triple (Design.md milestone 9's NVPTX remainder, see the "Retargeting"
// section of feme/docs/Roadmap.md).
//
// Covers the same ground as the AMDGPU pass, mapped onto NVPTX's own
// primitives instead:
//
//  - Shader entry points: PTX's kernel calling convention
//    (`CallingConv::PTX_Kernel`), so a host runtime (e.g. the CUDA driver
//    API) can actually launch them. NVPTX has no AMDGPU-style
//    `amdgpu-flat-work-group-size` attribute equivalent to set.
//  - Thread/group index queries: the ones with a direct per-component
//    mapping to an NVVM intrinsic (`llvm.dx.group.id`/`llvm.spv.group.id` ->
//    `llvm.nvvm.read.ptx.sreg.ctaid.*`, `llvm.dx.thread.id.in.group`/
//    `llvm.spv.thread.id.in.group` -> `llvm.nvvm.read.ptx.sreg.tid.*`), plus
//    the two that do not have one -- `llvm.dx.thread.id`/`llvm.spv.thread.id`
//    and `llvm.dx.flattened.thread.id.in.group`/
//    `llvm.spv.flattened.thread.id.in.group` -- synthesized from the entry
//    point's thread group dimensions the same way the AMDGPU pass does.
//  - Local variables: moved into NVPTX's local address space (5, matching
//    `llvm::NVPTXAS::ADDRESS_SPACE_LOCAL` -- the same numeric value as
//    AMDGPU's private address space, purely by coincidence).
//
// It does not yet cover resource-handle ops (see
// feme::nvptx::ResourceLoweringPass instead) or the wave/quad ops. Ops not
// (yet) covered are left unmodified, so this pass composes safely with
// modules that mix lowered and not-yet-lowered operations.
//
// Unlike AMDGPU, NVPTX has no native object-file (ELF) code generator --
// only PTX assembly text -- and `feme::Backend`'s `BackendOptions::FileType`
// has no knob to request that instead of `llvm::CodeGenFileType::
// ObjectFile` yet (see Roadmap.md's "Retargeting" section). So while this
// pass and feme::nvptx::ResourceLoweringPass are independently tested (see
// test/Transforms/NVPTX), retargeting a shader to NVPTX through the full
// `feme` CLI end to end -- the way test/Tools/feme/feme-dxil-to-amdgpu.ll
// exercises AMDGPU -- does not work yet; that is a separate, pre-existing
// gap in feme::TargetMachineBackend's own options, not one either of these
// two passes can close on its own.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_NVPTX_RAISEDLOWERING_H
#define FEME_TRANSFORMS_NVPTX_RAISEDLOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace nvptx {

/// Rewrites the subset of raised, format-agnostic intrinsic calls this pass
/// currently covers into the NVPTX target intrinsic calls they correspond
/// to, so the result is valid input to an NVPTX `llvm::TargetMachine`.
class RaisedLoweringPass : public llvm::PassInfoMixin<RaisedLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-nvptx-lower-raised"; }
};

} // namespace nvptx
} // namespace feme

#endif // FEME_TRANSFORMS_NVPTX_RAISEDLOWERING_H
