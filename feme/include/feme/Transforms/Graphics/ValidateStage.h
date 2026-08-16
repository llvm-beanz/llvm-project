//===- ValidateStage.h - Validate canonical stage operations --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::ValidateStagePass, the validation half
// of roadmap R20's `FeMeTransformsGraphics`: checking that a vertex/fragment
// entry point's `feme.stage.*` calls (feme/include/feme/Core/StageOps.h) --
// however they got there, whether from `CanonicalizeStagePass` or written by
// hand -- have constant, in-range element/row/component operands against the
// entry's `feme::EntrySignature`, and that every operation used is legal for
// the entry's declared stage. See "Canonical stage operations" in
// feme/docs/FeMeGraphicsDesign.md: "Only operations required by implemented
// stages are legal."
//
// This pass never rewrites IR; it only diagnoses (through
// `LLVMContext::emitError`, the same mechanism
// `feme::cpu::LinearizePass`/`PreparePass` use for a precondition violation)
// and otherwise leaves the module untouched.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_GRAPHICS_VALIDATESTAGE_H
#define FEME_TRANSFORMS_GRAPHICS_VALIDATESTAGE_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace graphics {

/// Validates every vertex/fragment entry point's `feme.stage.*` calls
/// against its `feme::EntrySignature` and declared stage. Diagnoses (but
/// does not fix) any violation; always preserves all analyses, since it
/// never modifies IR.
class ValidateStagePass : public llvm::PassInfoMixin<ValidateStagePass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-graphics-validate-stage"; }
};

} // namespace graphics
} // namespace feme

#endif // FEME_TRANSFORMS_GRAPHICS_VALIDATESTAGE_H
