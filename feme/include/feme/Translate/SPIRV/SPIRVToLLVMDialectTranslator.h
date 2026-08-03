//===- SPIRVToLLVMDialectTranslator.h - spirv dialect -> llvm dialect -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::SPIRVToLLVMDialectTranslator, a thin Translator
// wrapper around MLIR's existing `convert-spirv-to-llvm` conversion pass.
// This is the first of the two stages feme::SPIRVToLLVMTranslator composes
// (see feme/docs/Design.md's "SPIR-V -> MLIR llvm dialect -> LLVM IR"
// section): it stops at the `llvm` dialect rather than continuing on to an
// llvm::Module, so that stage can be inspected/lit-tested in isolation
// (mirroring how DXIL's `feme::dxil::OpRaisingPass` is tested on its own,
// rather than only end to end).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSLATE_SPIRV_SPIRVTOLLVMDIALECTTRANSLATOR_H
#define FEME_TRANSLATE_SPIRV_SPIRVTOLLVMDIALECTTRANSLATOR_H

#include "feme/Translate/Translator.h"

namespace feme {

/// Translates a Module holding an `mlir::spirv::ModuleOp` into a Module
/// holding a `mlir::ModuleOp` in the `llvm` dialect, via MLIR's
/// `createConvertSPIRVToLLVMPass`. Does not itself reach an llvm::Module --
/// that is feme::LLVMDialectToLLVMIRTranslator's job.
class SPIRVToLLVMDialectTranslator : public Translator {
public:
  llvm::Expected<Module> translate(Module &&M, Context &Ctx) const override;

  llvm::StringRef getFromFormatName() const override;
  llvm::StringRef getToFormatName() const override;
};

} // namespace feme

#endif // FEME_TRANSLATE_SPIRV_SPIRVTOLLVMDIALECTTRANSLATOR_H
