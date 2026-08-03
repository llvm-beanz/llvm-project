//===- TranslateRegistration.cpp - feme-translate hooks ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/LLVMIR/TranslateRegistration.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Translate/LLVMIR/LLVMDialectToLLVMIRTranslator.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace feme;

// Runs feme::LLVMDialectToLLVMIRTranslator on `ModuleOp`, printing the
// resulting llvm::Module as textual LLVM IR to `Output`. `ModuleOp` is owned
// by mlir-translate's caller, so it is cloned before being handed to
// LLVMDialectToLLVMIRTranslator, which takes ownership of (and mutates in
// place) the Module it is given.
static mlir::LogicalResult
translateLLVMDialectToLLVMIR(mlir::ModuleOp ModuleOp,
                             llvm::raw_ostream &Output) {
  mlir::MLIRContext *MLIRCtx = ModuleOp.getContext();
  Context Ctx(*MLIRCtx);

  mlir::OwningOpRef<mlir::ModuleOp> Cloned(ModuleOp.clone());
  Module Input = Module::fromMLIR(std::move(Cloned));

  LLVMDialectToLLVMIRTranslator Translator;
  llvm::Expected<Module> Result = Translator.translate(std::move(Input), Ctx);
  if (!Result) {
    mlir::emitError(ModuleOp.getLoc()) << llvm::toString(Result.takeError());
    return mlir::failure();
  }

  Result->getLLVMModule().print(Output, /*AAW=*/nullptr);
  return mlir::success();
}

void feme::registerLLVMDialectToLLVMIRTranslation() {
  mlir::TranslateFromMLIRRegistration Registration(
      "llvmdialect-to-llvmir",
      "translate an `llvm` dialect module to LLVM IR via "
      "feme::LLVMDialectToLLVMIRTranslator",
      translateLLVMDialectToLLVMIR, [](mlir::DialectRegistry &Registry) {
        Registry.insert<mlir::LLVM::LLVMDialect>();
      });
}
