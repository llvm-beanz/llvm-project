//===- SPIRVRaising.h - Raise SPIR-V-derived IR to DXIL conventions -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::dxil::SPIRVRaisingPass, the SPIR-V -> DXIL
// direction of Design.md milestone 6 ("DXIL <-> SPIR-V translation"): the
// mirror image of feme::spirv::RaisedLoweringPass (which rewrites raised,
// format-agnostic `llvm.dx.*` IR into `llvm.spv.*` conventions). This pass
// instead rewrites the `llvm.spv.*` intrinsics and `target("spirv.*")`
// handle types a SPIR-V module's `Translator` produces (see
// feme::SPIRVToLLVMTranslator) into the `llvm.dx.*`/`target("dx.*")`
// conventions feme::dxil::OpRaisingPass's own output already uses, so the
// result is valid input to feme::DXILExporter/LLVM's DirectX target the
// same way DXIL- or DXBC-derived raised IR already is.
//
// Covers:
//
//  - Thread/group index queries with a direct 1:1 mapping
//    (`llvm.spv.thread.id`, `llvm.spv.group.id`,
//    `llvm.spv.thread.id.in.group`, `llvm.spv.flattened.thread.id.in.group`).
//  - A bound `StorageBuffer` resource (HLSL's `(RW)StructuredBuffer<T>`,
//    i.e. a `target("spirv.VulkanBuffer", ...)` handle -- see
//    feme::spirv::convertBufferBlockType) accessed only through a flat
//    `llvm.spv.resource.getpointer` plus an ordinary load/store (no
//    further `getelementptr` into the element, i.e. no structured-buffer
//    field access -- matching feme::cpu::SPIRVResourceLoweringPass's own
//    narrowing), raised into DXIL's `target("dx.RawBuffer", ...)` handle
//    and `llvm.dx.resource.load.rawbuffer`/`store.rawbuffer`.
//
// Still missing (see Roadmap.md's "SPIR-V -> DXIL direction" item): a
// typed-buffer image resource (blocked upstream: MLIR's `SPIRVToLLVM`
// conversion has no patterns for image *access* ops yet, only image
// *types* -- see the "Known gap" note in feme/docs/Design.md's SPIR-V
// section, so no SPIR-V shader that reads or writes one reaches LLVM IR
// today), a structured-buffer field access, `SPV_EXT_descriptor_heap`
// (bindless) resources, and the wave/quad ops. Ops not (yet) covered are
// left unmodified, so this pass composes safely with modules that mix
// raised and not-yet-raised operations.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_DXIL_SPIRVRAISING_H
#define FEME_TRANSFORMS_DXIL_SPIRVRAISING_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace dxil {

/// Rewrites the SPIR-V-derived, format-specific intrinsic calls this pass
/// covers into the raised, format-agnostic `llvm.dx.*` conventions
/// feme::dxil::OpRaisingPass's own output already uses, so the result is
/// valid input to feme::DXILExporter/an in-tree DirectX `TargetMachine`.
class SPIRVRaisingPass : public llvm::PassInfoMixin<SPIRVRaisingPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-dxil-raise-spirv"; }
};

} // namespace dxil
} // namespace feme

#endif // FEME_TRANSFORMS_DXIL_SPIRVRAISING_H
