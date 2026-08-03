//===- LLVMDialectToLLVMIRTranslator.h - `llvm` dialect -> LLVM IR -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::LLVMDialectToLLVMIRTranslator, a thin Translator
// wrapper around MLIR's existing `mlir::translateModuleToLLVMIR`. Unlike
// feme::SPIRVToLLVMTranslator, this is not specific to any source format: it
// is the common final stage any FeMe pipeline that reaches the `llvm`
// dialect (SPIR-V's `spirv` -> `llvm` conversion today, others in future)
// shares, matching how DXIL import re-enters MLIR only at the `llvm`
// dialect for passes that need it (see the "DXIL" section of
// feme/docs/Design.md).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSLATE_LLVMIR_LLVMDIALECTTOLLVMIRTRANSLATOR_H
#define FEME_TRANSLATE_LLVMIR_LLVMDIALECTTOLLVMIRTRANSLATOR_H

#include "feme/Translate/Translator.h"

namespace feme {

/// Translates a Module holding a `builtin.module` op containing (only) the
/// `llvm` dialect into a Module holding an llvm::Module, via
/// `mlir::translateModuleToLLVMIR`. Format-agnostic: any Translator that
/// produces the `llvm` dialect (e.g. feme::SPIRVToLLVMDialectTranslator) can
/// feed this one.
class LLVMDialectToLLVMIRTranslator : public Translator {
public:
  llvm::Expected<Module> translate(Module &&M, Context &Ctx) const override;

  llvm::StringRef getFromFormatName() const override;
  llvm::StringRef getToFormatName() const override;
};

} // namespace feme

#endif // FEME_TRANSLATE_LLVMIR_LLVMDIALECTTOLLVMIRTRANSLATOR_H
