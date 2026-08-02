//===- Module.cpp - FeMe's cross-representation module wrapper ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Module.h"

#include "mlir/IR/Operation.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"

using namespace feme;

Module::Module(mlir::OwningOpRef<mlir::Operation *> M)
    : ModKind(Kind::MLIR), MLIRModule(std::move(M)) {}

Module::Module(std::unique_ptr<llvm::Module> M)
    : ModKind(Kind::LLVMIR), LLVMModule(std::move(M)) {}

// Out-of-line so that the destructor is emitted where mlir::Operation and
// llvm::Module are complete types.
Module::~Module() = default;

Module Module::fromLLVMIR(std::unique_ptr<llvm::Module> M) {
  return Module(std::move(M));
}

mlir::Operation *Module::getMLIROperation() const {
  assert(ModKind == Kind::MLIR && "not an MLIR module");
  return MLIRModule.get();
}

llvm::Module &Module::getLLVMModule() const {
  assert(ModKind == Kind::LLVMIR && "not an LLVM IR module");
  return *LLVMModule;
}
