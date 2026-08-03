//===- SPIRVToLLVMTranslator.h - spirv dialect -> LLVM IR ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::SPIRVToLLVMTranslator, a convenience Translator
// composing feme::SPIRVToLLVMDialectTranslator (`spirv` dialect -> `llvm`
// dialect) and feme::LLVMDialectToLLVMIRTranslator (`llvm` dialect ->
// llvm::Module) into a single end-to-end step. See the "SPIR-V -> MLIR llvm
// dialect -> LLVM IR" section of feme/docs/Design.md for why the
// intermediate `llvm` dialect stage is also exposed as its own Translator,
// and the deviation note on SPIR-V retargeting: this is the first stage of
// the "null pipeline" (SPIR-V -> `spirv` dialect -> (this) -> llvm::Module
// -> feme::Backend using LLVM's own `SPIRV` target -> SPIR-V binary) used to
// validate this Translator round-trips before real ISA retargeting is
// attempted.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSLATE_SPIRV_SPIRVTOLLVMTRANSLATOR_H
#define FEME_TRANSLATE_SPIRV_SPIRVTOLLVMTRANSLATOR_H

#include "feme/Translate/Translator.h"

namespace feme {

/// Translates a Module holding an `mlir::spirv::ModuleOp` into a Module
/// holding an llvm::Module, by composing feme::SPIRVToLLVMDialectTranslator
/// and feme::LLVMDialectToLLVMIRTranslator. Does not itself produce a
/// SPIR-V binary or retarget to any ISA -- that is feme::Backend's job.
class SPIRVToLLVMTranslator : public Translator {
public:
  llvm::Expected<Module> translate(Module &&M, Context &Ctx) const override;

  llvm::StringRef getFromFormatName() const override;
  llvm::StringRef getToFormatName() const override;
};

} // namespace feme

#endif // FEME_TRANSLATE_SPIRV_SPIRVTOLLVMTRANSLATOR_H
