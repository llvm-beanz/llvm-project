//===- TranslateRegistration.h - feme-translate hooks ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the registration hook exposing feme::SPIRVImporter to
// feme-translate as the `--import-spirv` flag (see the "Testing Tools" /
// `feme-translate` section of feme/docs/Design.md).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_IMPORT_SPIRV_TRANSLATEREGISTRATION_H
#define FEME_IMPORT_SPIRV_TRANSLATEREGISTRATION_H

namespace feme {

/// Registers feme::SPIRVImporter with MLIR's translation registry under the
/// `import-spirv` name, for use by feme-translate and any other tool linking
/// MLIRTranslateLib.
void registerSPIRVImportTranslation();

} // namespace feme

#endif // FEME_IMPORT_SPIRV_TRANSLATEREGISTRATION_H
