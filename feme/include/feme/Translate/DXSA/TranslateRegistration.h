//===- TranslateRegistration.h - feme-translate hooks ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the registration hook exposing the `dxsa` dialect -> DXIL
// translation (the DXBC -> DXIL edge of feme/docs/Design.md's translation
// matrix) to feme-translate as the `--dxsa-to-llvmir` flag.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSLATE_DXSA_TRANSLATEREGISTRATION_H
#define FEME_TRANSLATE_DXSA_TRANSLATEREGISTRATION_H

namespace feme {

/// Registers feme::dxsa::translateToLLVMIR under the `dxsa-to-llvmir` name,
/// translating a decoded DXBC program to DXIL-shaped LLVM IR.
void registerDXSAToLLVMIRTranslation();

} // namespace feme

#endif // FEME_TRANSLATE_DXSA_TRANSLATEREGISTRATION_H
