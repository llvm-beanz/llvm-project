//===- TranslateRegistration.cpp - feme-translate hooks ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/DXSA/TranslateRegistration.h"

#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "feme/Target/DXSA/BinaryParser.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Tools/mlir-translate/Translation.h"

using namespace mlir;

void feme::registerDXSAImportBinTranslation() {
  TranslateToMLIRRegistration Registration{
      "import-dxsa-bin", "Translate DXSA binary to MLIR",
      [](llvm::SourceMgr &SourceMgr,
         MLIRContext *Context) -> OwningOpRef<Operation *> {
        return feme::dxsa::deserialize(SourceMgr, Context);
      },
      [](DialectRegistry &Registry) {
        Registry.insert<feme::dxsa::DXSADialect>();
      }};
}

void feme::registerDXSAExportBinTranslation() {
  TranslateFromMLIRRegistration Registration{
      "export-dxsa-bin", "Translate MLIR to DXSA binary",
      [](ModuleOp Source, raw_ostream &Output) {
        return feme::dxsa::serialize(Source, Output);
      },
      [](DialectRegistry &Registry) {
        Registry.insert<feme::dxsa::DXSADialect>();
      }};
}
