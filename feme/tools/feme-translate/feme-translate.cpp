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
// `--import-spirv` (roadmap step 2) wraps feme::SPIRVImporter; `--import-dxil`
// (roadmap step 4) wraps feme::DXILImporter. Later roadmap steps will
// register e.g. `--import-dxbc` here, migrating the registration pattern
// from the `wip/dxsa-mlir` prototype's TranslateRegistration.cpp.
// `--spirv-to-llvmir` and `--llvm-backend` (roadmap step 3) wrap
// feme::SPIRVToLLVMTranslator and feme::TargetMachineBackend respectively,
// so that each stage of the SPIR-V "null pipeline" (see
// feme/docs/Design.md's Retargeting to Native ISA section) can be
// lit-tested in isolation instead of only via gtest.
// `--spirv-to-llvmdialect` and `--llvmdialect-to-llvmir` further split
// `--spirv-to-llvmir` into its two component stages (`spirv` dialect ->
// `llvm` dialect, then `llvm` dialect -> LLVM IR), so that the intermediate
// `llvm` dialect representation SPIR-V import reaches on its way to LLVM IR
// can also be lit-tested on its own -- see the "SPIR-V -> MLIR llvm dialect
// -> LLVM IR" section of feme/docs/Design.md.
//
//===----------------------------------------------------------------------===//

#include "feme/Import/DXIL/TranslateRegistration.h"
#include "feme/Import/SPIRV/TranslateRegistration.h"
#include "feme/Target/DXSA/TranslateRegistration.h"
#include "feme/Target/TranslateRegistration.h"
#include "feme/Translate/DXSA/TranslateRegistration.h"
#include "feme/Translate/LLVMIR/TranslateRegistration.h"
#include "feme/Translate/SPIRV/TranslateRegistration.h"
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllTranslations.h"
#include "mlir/Tools/mlir-translate/MlirTranslateMain.h"

int main(int argc, char **argv) {
  mlir::registerAllTranslations();
  feme::registerSPIRVImportTranslation();
  feme::registerDXILImportTranslation();
  feme::registerDXSAImportBinTranslation();
  feme::registerDXSAExportBinTranslation();
  feme::registerDXSAToLLVMIRTranslation();
  feme::registerSPIRVToLLVMDialectTranslation();
  feme::registerSPIRVToLLVMIRTranslation();
  feme::registerLLVMDialectToLLVMIRTranslation();
  feme::registerTargetMachineBackendTranslation();
  // TODO: Register FeMe's other import/export translations (DXBC) here as
  // they are implemented.

  return mlir::failed(
      mlir::mlirTranslateMain(argc, argv, "FeMe import/export testing driver"));
}
