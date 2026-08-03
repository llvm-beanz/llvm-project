//===- LLVMDialectToLLVMIRTranslator.cpp - `llvm` dialect -> LLVM IR -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/LLVMIR/LLVMDialectToLLVMIRTranslator.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"

using namespace feme;

llvm::Expected<Module>
LLVMDialectToLLVMIRTranslator::translate(Module &&M, Context &Ctx) const {
  if (M.getKind() != Module::Kind::MLIR)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "LLVMDialectToLLVMIRTranslator requires an MLIR input Module");

  mlir::OwningOpRef<mlir::Operation *> InputOp = M.takeMLIROperation();
  auto ModuleOp = mlir::dyn_cast<mlir::ModuleOp>(InputOp.get());
  if (!ModuleOp)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "LLVMDialectToLLVMIRTranslator requires a mlir::ModuleOp input");

  mlir::MLIRContext &MLIRCtx = Ctx.getMLIRContext();

  // translateModuleToLLVMIR looks up each dialect's LLVMTranslationInterface
  // on the MLIRContext, so the builtin/llvm dialects' translations need to
  // be registered here rather than assumed to already be present (unlike
  // feme-translate/mlir-translate-hosted uses of this Translator, this one
  // may run against a bare feme::Context).
  mlir::registerBuiltinDialectTranslation(MLIRCtx);
  mlir::registerLLVMDialectTranslation(MLIRCtx);

  std::unique_ptr<llvm::Module> LLVMModule =
      mlir::translateModuleToLLVMIR(ModuleOp, Ctx.getLLVMContext());
  if (!LLVMModule)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "failed to translate llvm dialect module "
                                   "to an llvm::Module");

  return Module::fromLLVMIR(std::move(LLVMModule));
}

llvm::StringRef LLVMDialectToLLVMIRTranslator::getFromFormatName() const {
  return "llvmdialect";
}

llvm::StringRef LLVMDialectToLLVMIRTranslator::getToFormatName() const {
  return "llvmir";
}
