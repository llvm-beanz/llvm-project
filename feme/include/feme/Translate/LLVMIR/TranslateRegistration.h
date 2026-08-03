//===- TranslateRegistration.h - feme-translate hooks ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the registration hook exposing
// feme::LLVMDialectToLLVMIRTranslator to feme-translate as the
// `--llvmdialect-to-llvmir` flag (see the "Testing Tools" /
// `feme-translate` section of feme/docs/Design.md).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSLATE_LLVMIR_TRANSLATEREGISTRATION_H
#define FEME_TRANSLATE_LLVMIR_TRANSLATEREGISTRATION_H

namespace feme {

/// Registers feme::LLVMDialectToLLVMIRTranslator with MLIR's translation
/// registry under the `llvmdialect-to-llvmir` name, for use by
/// feme-translate and any other tool linking MLIRTranslateLib.
void registerLLVMDialectToLLVMIRTranslation();

} // namespace feme

#endif // FEME_TRANSLATE_LLVMIR_TRANSLATEREGISTRATION_H
