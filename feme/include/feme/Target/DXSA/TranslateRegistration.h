//===- TranslateRegistration.h - feme-translate hooks ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the registration hooks exposing the `dxsa` dialect's
// BinaryParser/BinaryWriter (see feme/docs/Design.md's "DXBC -> new MLIR
// `dxsa` dialect" section) to feme-translate as the `--import-dxsa-bin`,
// and `--export-dxsa-bin` flags.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_DXSA_TRANSLATEREGISTRATION_H
#define FEME_TARGET_DXSA_TRANSLATEREGISTRATION_H

namespace feme {

/// Registers the DXSA `BinaryParser` under the `import-dxsa-bin` name,
/// translating tokenized DXBC binary bytecode to the `dxsa` MLIR dialect.
void registerDXSAImportBinTranslation();

/// Registers the DXSA `BinaryWriter` under the `export-dxsa-bin` name,
/// translating the `dxsa` MLIR dialect back to tokenized DXBC binary
/// bytecode.
void registerDXSAExportBinTranslation();

} // namespace feme

#endif // FEME_TARGET_DXSA_TRANSLATEREGISTRATION_H
