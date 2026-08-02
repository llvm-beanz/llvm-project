//===- TranslateRegistration.cpp - feme-translate hooks ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/SPIRV/TranslateRegistration.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Translate/SPIRV/SPIRVToLLVMTranslator.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace feme;

// Runs feme::SPIRVToLLVMTranslator on `SpirvModule`, printing the resulting
// llvm::Module as textual LLVM IR to `Output`. `SpirvModule` is owned by
// mlir-translate's caller, so it is cloned before being handed to
// SPIRVToLLVMTranslator, which takes ownership of (and mutates in place) the
// Module it is given.
static mlir::LogicalResult
translateSPIRVToLLVMIR(mlir::spirv::ModuleOp SpirvModule,
                       llvm::raw_ostream &Output) {
  mlir::MLIRContext *MLIRCtx = SpirvModule.getContext();
  Context Ctx(*MLIRCtx);

  mlir::OwningOpRef<mlir::spirv::ModuleOp> Cloned(SpirvModule.clone());
  Module Input = Module::fromMLIR(std::move(Cloned));

  SPIRVToLLVMTranslator Translator;
  llvm::Expected<Module> Result = Translator.translate(std::move(Input), Ctx);
  if (!Result) {
    mlir::emitError(SpirvModule.getLoc()) << llvm::toString(Result.takeError());
    return mlir::failure();
  }

  Result->getLLVMModule().print(Output, /*AAW=*/nullptr);
  return mlir::success();
}

void feme::registerSPIRVToLLVMIRTranslation() {
  mlir::TranslateFromMLIRRegistration Registration(
      "spirv-to-llvmir",
      "translate a `spirv` dialect module to LLVM IR via "
      "feme::SPIRVToLLVMTranslator",
      translateSPIRVToLLVMIR, [](mlir::DialectRegistry &Registry) {
        Registry.insert<mlir::spirv::SPIRVDialect>();
      });
}
