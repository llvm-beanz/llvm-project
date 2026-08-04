//===- TranslateRegistration.cpp - feme-translate hooks ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/DXSA/TranslateRegistration.h"

#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "feme/Translate/DXSA/DXSAToLLVMIRTranslator.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Tools/mlir-translate/Translation.h"

using namespace mlir;

void feme::registerDXSAToLLVMIRTranslation() {
  TranslateFromMLIRRegistration Registration{
      "dxsa-to-llvmir", "Translate a decoded DXBC program to DXIL-shaped LLVM IR",
      [](ModuleOp Source, raw_ostream &Output) -> LogicalResult {
        llvm::LLVMContext Context;
        std::unique_ptr<llvm::Module> Translated =
            feme::dxsa::translateToLLVMIR(Source, Context);
        if (!Translated)
          return failure();
        Translated->print(Output, /*AAW=*/nullptr);
        return success();
      },
      [](DialectRegistry &Registry) {
        Registry.insert<feme::dxsa::DXSADialect>();
      }};
}
