//===- feme-translate.cpp - FeMe import/export testing driver ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// feme-translate is an mlir-translate-style driver exposing each format's
// Importer/Exporter as individual translation flags, for testing a single
// import/export stage in isolation with textual (not final-binary-ISA)
// output (see the "Testing Tools" section of feme/docs/Design.md).
//
// `--import-spirv` (roadmap step 2) wraps feme::SPIRVImporter; later
// roadmap steps will register e.g. `--import-dxbc` here, migrating the
// registration pattern from the `wip/dxsa-mlir` prototype's
// TranslateRegistration.cpp.
//
//===----------------------------------------------------------------------===//

#include "feme/Import/SPIRV/TranslateRegistration.h"
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllTranslations.h"
#include "mlir/Tools/mlir-translate/MlirTranslateMain.h"

int main(int argc, char **argv) {
  mlir::registerAllTranslations();
  feme::registerSPIRVImportTranslation();
  // TODO: Register FeMe's other import/export translations (DXIL, DXBC)
  // here as they are implemented.

  return mlir::failed(
      mlir::mlirTranslateMain(argc, argv, "FeMe import/export testing driver"));
}
