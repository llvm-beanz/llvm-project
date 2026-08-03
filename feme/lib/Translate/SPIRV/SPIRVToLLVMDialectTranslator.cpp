//===- SPIRVToLLVMDialectTranslator.cpp - spirv dialect -> llvm dialect --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/SPIRV/SPIRVToLLVMDialectTranslator.h"

#include "feme/Conversion/SPIRVToLLVM/SPIRVToLLVM.h"
#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/Support/Error.h"

using namespace feme;

llvm::Expected<Module>
SPIRVToLLVMDialectTranslator::translate(Module &&M, Context &Ctx) const {
  if (M.getKind() != Module::Kind::MLIR)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "SPIRVToLLVMDialectTranslator requires an MLIR input Module");

  mlir::OwningOpRef<mlir::Operation *> InputOp = M.takeMLIROperation();
  auto SpirvModule = mlir::dyn_cast<mlir::spirv::ModuleOp>(InputOp.get());
  if (!SpirvModule)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "SPIRVToLLVMDialectTranslator requires a mlir::spirv::ModuleOp "
        "input");

  mlir::MLIRContext &MLIRCtx = Ctx.getMLIRContext();
  MLIRCtx.loadDialect<mlir::LLVM::LLVMDialect>();

  // The conversion pass anchors on a builtin ModuleOp (it converts a
  // nested spirv.module in-place into a nested builtin.module -- see
  // mlir/test/Conversion/SPIRVToLLVM/module-ops-to-llvm.mlir), so host the
  // (now-detached) spirv.module inside a throwaway outer module for the
  // pass to run on, then unwrap the single resulting nested module below.
  mlir::OwningOpRef<mlir::ModuleOp> Outer =
      mlir::ModuleOp::create(mlir::UnknownLoc::get(&MLIRCtx));
  Outer->push_back(InputOp.release());

  mlir::PassManager PM(&MLIRCtx);
  PM.addPass(feme::spirv::createConvertSPIRVToLLVMPass());
  if (mlir::failed(PM.run(*Outer)))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "failed to convert spirv dialect module to the llvm dialect");

  mlir::Block *OuterBody = Outer->getBody();
  if (!llvm::hasSingleElement(*OuterBody))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "expected the spirv-to-llvm conversion to produce exactly one "
        "nested module operation");
  auto InnerModule = mlir::dyn_cast<mlir::ModuleOp>(OuterBody->front());
  if (!InnerModule)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "expected the spirv-to-llvm conversion to produce a nested "
        "builtin.module operation");

  // Detach InnerModule from the throwaway Outer module so it can be handed
  // back as an owned Module without cloning it.
  InnerModule->remove();
  return Module::fromMLIR(mlir::OwningOpRef<mlir::ModuleOp>(InnerModule));
}

llvm::StringRef SPIRVToLLVMDialectTranslator::getFromFormatName() const {
  return "spirv";
}

llvm::StringRef SPIRVToLLVMDialectTranslator::getToFormatName() const {
  return "llvmdialect";
}
