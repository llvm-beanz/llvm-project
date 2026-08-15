//===- StageIODecorations.cpp - stage-IO variable decoration metadata ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements roadmap R19's decoration side channel: a non-builtin
// `Input`/`Output` variable's `Location`/`Component`/`Index`/interpolation/
// per-primitive/per-patch decorations survive the `spirv` -> `llvm` dialect
// conversion as an ad hoc `llvm.mlir.global` attribute (there being no
// `LLVMTranslationDialectInterface` FeMe can hook into `llvm.mlir.global`'s
// translation with, since FeMe does not define its own MLIR dialect), then
// get re-attached as the real `!spirv.Decorations` metadata LLVM's SPIRV
// backend reads once a genuine `llvm::Module` exists to hold it -- see
// `buildOpSpirvDecorations` in `llvm/lib/Target/SPIRV/SPIRVUtils.cpp` and
// `llvm/test/CodeGen/SPIRV/linkage/hidden-interface-vars.ll` for the shape
// this matches.
//
//===----------------------------------------------------------------------===//

#include "feme/Conversion/SPIRVToLLVM/SPIRVToLLVM.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

llvm::StringRef feme::spirv::getStageIODecorationsAttrName() {
  return "feme.spirv.decorations";
}

feme::spirv::StageIODecorationsMap
feme::spirv::collectStageIODecorations(mlir::Operation *Module) {
  StageIODecorationsMap Result;
  Module->walk([&](mlir::LLVM::GlobalOp Global) {
    auto Decorations = mlir::dyn_cast_or_null<mlir::ArrayAttr>(
        Global->getAttr(getStageIODecorationsAttrName()));
    if (Decorations)
      Result[Global.getSymName()] = Decorations;
  });
  return Result;
}

void feme::spirv::attachStageIODecorations(
    const StageIODecorationsMap &Decorations, llvm::Module &LLVMModule) {
  llvm::LLVMContext &Ctx = LLVMModule.getContext();
  llvm::IntegerType *I32 = llvm::Type::getInt32Ty(Ctx);
  for (const auto &Entry : Decorations) {
    llvm::GlobalVariable *GV = LLVMModule.getGlobalVariable(Entry.getKey());
    if (!GV)
      continue;

    llvm::SmallVector<llvm::Metadata *> DecorationNodes;
    for (mlir::Attribute OneDecoration : Entry.getValue()) {
      llvm::SmallVector<llvm::Metadata *> Operands;
      for (mlir::Attribute Operand :
           mlir::cast<mlir::ArrayAttr>(OneDecoration)) {
        auto Value = mlir::cast<mlir::IntegerAttr>(Operand).getInt();
        Operands.push_back(
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(I32, Value)));
      }
      DecorationNodes.push_back(llvm::MDNode::get(Ctx, Operands));
    }
    GV->setMetadata("spirv.Decorations",
                    llvm::MDNode::get(Ctx, DecorationNodes));
  }
}
