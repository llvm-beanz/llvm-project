//===- SPIRVBuiltinFolding.h - Fold SPIR-V builtin extractelements -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::SPIRVBuiltinFoldingPass, a small, targeted
// cleanup roadmap step R10 adds (see feme/docs/Roadmap.md's §2.4.2) to let
// `feme::cpu::SIMDizePass` recognize a SPIR-V-sourced module's builtin
// (thread/group ID) access the same way it already does a DXIL-sourced
// one's.
//
// `feme::spirv::createConvertSPIRVToLLVMPass` (see the "SPIR-V -> MLIR llvm
// dialect -> LLVM IR" section of feme/docs/Design.md) always materializes a
// builtin input variable (`GlobalInvocationId`, `LocalInvocationId`, ...) as
// the *whole* 3-component vector -- one `llvm.spv.thread.id`/`...`call per
// component, folded together with `insertelement` -- because SPIR-V's own
// `spirv.CompositeExtract` (which then picks the one lane a shader actually
// reads) is converted generically, with no notion that its operand is a
// builtin materialization it could fold into instead. DXIL's raised
// `llvm.dx.thread.id` intrinsic, by contrast, is already scalar, so
// `feme::cpu::SIMDizePass`'s pattern matching over a store's value operand
// (see its own `dx_thread_id`/`spv_thread_id` cases) never has to see
// through this extra construct for a DXIL-sourced module. This pass folds
// `extractelement(insertelement-chain, ConstantIdx)` back into the
// single scalar value that lane's `insertelement` carries -- using
// `llvm::findScalarElement`, the same fold `InstCombine` itself performs --
// so the redundant three-way vector construction becomes dead code (removed
// by ordinary DCE elsewhere in the pipeline) and every later CPU-pipeline
// pass sees exactly the same shape it already handles for DXIL.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_SPIRVBUILTINFOLDING_H
#define FEME_TRANSFORMS_CPU_SPIRVBUILTINFOLDING_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Folds a constant-index `extractelement` of an `insertelement` chain back
/// into the single scalar value that index's `insertelement` carries. See
/// the file comment above for why this specifically unblocks SPIR-V-sourced
/// modules.
class SPIRVBuiltinFoldingPass
    : public llvm::PassInfoMixin<SPIRVBuiltinFoldingPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-fold-spirv-builtins"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_SPIRVBUILTINFOLDING_H
