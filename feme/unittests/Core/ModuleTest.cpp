//===- ModuleTest.cpp - Tests for feme::Module ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Module.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

TEST(ModuleTest, WrapsMLIROperation) {
  mlir::MLIRContext MLIRCtx;
  mlir::OwningOpRef<mlir::ModuleOp> Op =
      mlir::ModuleOp::create(mlir::UnknownLoc::get(&MLIRCtx));
  mlir::Operation *RawOp = Op.get();

  Module M = Module::fromMLIR(std::move(Op));
  EXPECT_EQ(M.getKind(), Module::Kind::MLIR);
  EXPECT_EQ(M.getMLIROperation(), RawOp);
}

TEST(ModuleTest, TakeMLIROperationTransfersOwnership) {
  mlir::MLIRContext MLIRCtx;
  mlir::OwningOpRef<mlir::ModuleOp> Op =
      mlir::ModuleOp::create(mlir::UnknownLoc::get(&MLIRCtx));
  mlir::Operation *RawOp = Op.get();

  Module M = Module::fromMLIR(std::move(Op));
  mlir::OwningOpRef<mlir::Operation *> Taken = M.takeMLIROperation();
  EXPECT_EQ(Taken.get(), RawOp);
}

TEST(ModuleTest, WrapsLLVMModule) {
  llvm::LLVMContext LLVMCtx;
  auto LLVMMod = std::make_unique<llvm::Module>("test", LLVMCtx);
  llvm::Module *RawMod = LLVMMod.get();

  Module M = Module::fromLLVMIR(std::move(LLVMMod));
  EXPECT_EQ(M.getKind(), Module::Kind::LLVMIR);
  EXPECT_EQ(&M.getLLVMModule(), RawMod);
}

TEST(ModuleTest, IsMoveOnly) {
  static_assert(!std::is_copy_constructible<Module>::value,
                "Module must not be copyable");
  static_assert(std::is_move_constructible<Module>::value,
                "Module must be movable");
}

} // namespace
