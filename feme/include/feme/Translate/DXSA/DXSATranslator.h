//===- DXSATranslator.h - dxsa dialect -> LLVM IR Translator ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::dxsa::DXSAToLLVMIRTranslator, a thin
// feme::Translator wrapper around feme::dxsa::translateToLLVMIR (see
// DXSAToLLVMIRTranslator.h), so feme::Driver can dispatch to the DXBC -> DXIL
// translation generically alongside feme::SPIRVToLLVMTranslator (see the
// "Pipeline Abstraction" section of feme/docs/Design.md). Kept in a separate
// translation unit from DXSAToLLVMIRTranslator.cpp: that file's own
// file-local `Translator` class (the translation's actual implementation)
// would otherwise be ambiguous with feme::Translator wherever both are
// visible unqualified.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSLATE_DXSA_DXSATRANSLATOR_H
#define FEME_TRANSLATE_DXSA_DXSATRANSLATOR_H

#include "feme/Translate/Translator.h"

namespace feme {
namespace dxsa {

/// Translates a Module holding a `feme::dxsa::ModuleOp` (as produced by
/// feme::DXBCImporter) into a Module holding an llvm::Module, via
/// feme::dxsa::translateToLLVMIR. Unlike that free function, this always
/// synthesizes the input/output signature from the dxsa.module's own
/// declarations: recovering the real container signature is
/// feme-translate's `--dxbc-container` testing-only flag's job (see
/// "Building complete legacy DXBC containers for testing" in
/// feme/docs/Design.md), not Driver's.
class DXSAToLLVMIRTranslator : public Translator {
public:
  llvm::Expected<Module> translate(Module &&M, Context &Ctx) const override;

  llvm::StringRef getFromFormatName() const override;
  llvm::StringRef getToFormatName() const override;
};

} // namespace dxsa
} // namespace feme

#endif // FEME_TRANSLATE_DXSA_DXSATRANSLATOR_H
