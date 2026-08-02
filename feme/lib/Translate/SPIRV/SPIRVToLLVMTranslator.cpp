//===- SPIRVToLLVMTranslator.cpp - spirv dialect -> LLVM IR --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/SPIRV/SPIRVToLLVMTranslator.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "mlir/Conversion/SPIRVToLLVM/SPIRVToLLVMPass.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"

using namespace feme;

llvm::Expected<Module> SPIRVToLLVMTranslator::translate(Module &&M,
                                                        Context &Ctx) const {
  if (M.getKind() != Module::Kind::MLIR)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "SPIRVToLLVMTranslator requires an MLIR input Module");

  mlir::OwningOpRef<mlir::Operation *> InputOp = M.takeMLIROperation();
  auto SpirvModule = mlir::dyn_cast<mlir::spirv::ModuleOp>(InputOp.get());
  if (!SpirvModule)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "SPIRVToLLVMTranslator requires a mlir::spirv::ModuleOp input");

  mlir::MLIRContext &MLIRCtx = Ctx.getMLIRContext();
  MLIRCtx.loadDialect<mlir::LLVM::LLVMDialect>();

  // ConvertSPIRVToLLVMPass anchors on a builtin ModuleOp (it converts a
  // nested spirv.module in-place into a nested builtin.module -- see
  // mlir/test/Conversion/SPIRVToLLVM/module-ops-to-llvm.mlir), so host the
  // (now-detached) spirv.module inside a throwaway outer module for the
  // pass to run on, then unwrap the single resulting nested module for
  // translation to LLVM IR below.
  mlir::OwningOpRef<mlir::ModuleOp> Outer =
      mlir::ModuleOp::create(mlir::UnknownLoc::get(&MLIRCtx));
  Outer->push_back(InputOp.release());

  mlir::PassManager PM(&MLIRCtx);
  PM.addPass(mlir::createConvertSPIRVToLLVMPass());
  if (mlir::failed(PM.run(*Outer)))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "failed to convert spirv dialect module to the llvm dialect");

  mlir::Block *OuterBody = Outer->getBody();
  if (!llvm::hasSingleElement(*OuterBody))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "expected convert-spirv-to-llvm to produce exactly one nested "
        "module operation");
  auto InnerModule = mlir::dyn_cast<mlir::ModuleOp>(OuterBody->front());
  if (!InnerModule)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "expected convert-spirv-to-llvm to produce a nested builtin.module "
        "operation");

  // translateModuleToLLVMIR looks up each dialect's LLVMTranslationInterface
  // on the MLIRContext, so the builtin/llvm dialects' translations need to
  // be registered here rather than assumed to already be present (unlike
  // feme-translate/mlir-translate-hosted uses of this Translator, this one
  // may run against a bare feme::Context).
  mlir::registerBuiltinDialectTranslation(MLIRCtx);
  mlir::registerLLVMDialectTranslation(MLIRCtx);

  std::unique_ptr<llvm::Module> LLVMModule =
      mlir::translateModuleToLLVMIR(InnerModule, Ctx.getLLVMContext());
  if (!LLVMModule)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "failed to translate llvm dialect module "
                                   "to an llvm::Module");

  return Module::fromLLVMIR(std::move(LLVMModule));
}

llvm::StringRef SPIRVToLLVMTranslator::getFromFormatName() const {
  return "spirv";
}

llvm::StringRef SPIRVToLLVMTranslator::getToFormatName() const {
  return "llvmir";
}
