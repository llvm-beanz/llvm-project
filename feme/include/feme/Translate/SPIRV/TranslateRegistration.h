//===- TranslateRegistration.h - feme-translate hooks ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the registration hooks exposing
// feme::SPIRVToLLVMTranslator and feme::SPIRVToLLVMDialectTranslator to
// feme-translate as the `--spirv-to-llvmir` and `--spirv-to-llvmdialect`
// flags (see the "Testing Tools" / `feme-translate` section of
// feme/docs/Design.md).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSLATE_SPIRV_TRANSLATEREGISTRATION_H
#define FEME_TRANSLATE_SPIRV_TRANSLATEREGISTRATION_H

namespace feme {

/// Registers feme::SPIRVToLLVMTranslator with MLIR's translation registry
/// under the `spirv-to-llvmir` name, for use by feme-translate and any other
/// tool linking MLIRTranslateLib.
void registerSPIRVToLLVMIRTranslation();

/// Registers feme::SPIRVToLLVMDialectTranslator with MLIR's translation
/// registry under the `spirv-to-llvmdialect` name, for use by feme-translate
/// and any other tool linking MLIRTranslateLib.
void registerSPIRVToLLVMDialectTranslation();

} // namespace feme

#endif // FEME_TRANSLATE_SPIRV_TRANSLATEREGISTRATION_H
