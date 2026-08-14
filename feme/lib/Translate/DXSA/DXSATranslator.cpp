//===- DXSATranslator.cpp - dxsa dialect -> LLVM IR Translator -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/DXSA/DXSATranslator.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "feme/Translate/DXSA/DXSAToLLVMIRTranslator.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"

using namespace feme;

llvm::Expected<Module>
dxsa::DXSAToLLVMIRTranslator::translate(Module &&M, Context &Ctx) const {
  if (M.getKind() != Module::Kind::MLIR)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "DXSAToLLVMIRTranslator requires an MLIR input Module");

  mlir::OwningOpRef<mlir::Operation *> InputOp = M.takeMLIROperation();
  auto Shader = mlir::dyn_cast<dxsa::ModuleOp>(InputOp.get());
  if (!Shader)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "DXSAToLLVMIRTranslator requires a feme::dxsa::ModuleOp input");

  // feme::dxsa::translateToLLVMIR expects its dxsa.module nested inside a
  // builtin module (matching how mlir-translate's textual parser always
  // wraps a parsed file's top-level ops), but feme::DXBCImporter hands back
  // the dxsa.module directly, with no such wrapper -- so build a throwaway
  // one here, mirroring feme::SPIRVToLLVMDialectTranslator's "Outer" module.
  mlir::MLIRContext *MLIRCtx = InputOp->getContext();
  mlir::OwningOpRef<mlir::ModuleOp> Outer =
      mlir::ModuleOp::create(mlir::UnknownLoc::get(MLIRCtx));
  Outer->push_back(InputOp.release());

  std::unique_ptr<llvm::Module> Result =
      dxsa::translateToLLVMIR(*Outer, Ctx.getLLVMContext());
  if (!Result)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "failed to translate the dxsa dialect to DXIL-shaped LLVM IR");

  return Module::fromLLVMIR(std::move(Result));
}

llvm::StringRef dxsa::DXSAToLLVMIRTranslator::getFromFormatName() const {
  return "dxsa";
}

llvm::StringRef dxsa::DXSAToLLVMIRTranslator::getToFormatName() const {
  return "llvmir";
}
