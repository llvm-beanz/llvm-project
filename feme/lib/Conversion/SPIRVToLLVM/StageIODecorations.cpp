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
// Roadmap H2c extends the same side-channel idea to a builtin interface
// block's (e.g. `gl_PerVertex`) own *per-member* `OpMemberDecorate`d
// decorations, which SPIR-V attaches to the struct type rather than to the
// block variable itself. Unlike the whole-variable channel above, there is
// no real `!spirv.Decorations`-shaped backend metadata for a per-member
// decoration to become -- this is a FeMe-internal-only channel
// (`feme.spirv.MemberDecorations`) that `feme::graphics::CanonicalizeStagePass`
// (roadmap H2d) reads to decompose the block into one signature element per
// member.
//
// Roadmap L206 adds a third, narrower channel of the same shape
// (`feme.spirv.Int16Signed`): unlike a decoration, SPIR-V's `OpTypeInt`
// `Signedness` operand is a *type* property with no `!spirv.Decorations`-
// shaped backend encoding at all, and LLVM's own integer types carry no
// signedness bit to preserve it in either -- so a stage-IO variable's true
// signed-vs-unsigned 16-bit-integer-ness would otherwise be silently
// erased by the `spirv` -> `llvm` dialect conversion, the same way it
// would be lost translating to LLVM IR proper. `CanonicalizeStage.cpp`'s
// own `storeStageIOValue` needs exactly this one bit back (see its own
// comment) to widen a genuinely signed 16-bit stage-IO output correctly
// (`sext`) instead of always defaulting to the bit-preserving-but-
// numerically-wrong-for-a-negative-value `zext` every other narrow-scalar
// case here still correctly uses.
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

namespace {

/// Converts \p Decorations -- an `ArrayAttr` of `(i32 decoration, i32
/// arg...)` tuples, the shape both getStageIODecorationsAttrName() and
/// getStageIOMemberDecorationsAttrName() use for a single decoration list --
/// into the matching `!{!{i32 ..., i32 ...}, ...}` metadata shape.
llvm::MDNode *buildDecorationListMD(mlir::ArrayAttr Decorations,
                                    llvm::LLVMContext &Ctx,
                                    llvm::IntegerType *I32) {
  llvm::SmallVector<llvm::Metadata *> DecorationNodes;
  for (mlir::Attribute OneDecoration : Decorations) {
    llvm::SmallVector<llvm::Metadata *> Operands;
    for (mlir::Attribute Operand : mlir::cast<mlir::ArrayAttr>(OneDecoration)) {
      auto Value = mlir::cast<mlir::IntegerAttr>(Operand).getInt();
      Operands.push_back(
          llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(I32, Value)));
    }
    DecorationNodes.push_back(llvm::MDNode::get(Ctx, Operands));
  }
  return llvm::MDNode::get(Ctx, DecorationNodes);
}

} // namespace

void feme::spirv::attachStageIODecorations(
    const StageIODecorationsMap &Decorations, llvm::Module &LLVMModule) {
  llvm::LLVMContext &Ctx = LLVMModule.getContext();
  llvm::IntegerType *I32 = llvm::Type::getInt32Ty(Ctx);
  for (const auto &Entry : Decorations) {
    llvm::GlobalVariable *GV = LLVMModule.getGlobalVariable(Entry.getKey());
    if (!GV)
      continue;
    GV->setMetadata("spirv.Decorations",
                    buildDecorationListMD(Entry.getValue(), Ctx, I32));
  }
}

llvm::StringRef feme::spirv::getStageIOMemberDecorationsAttrName() {
  return "feme.spirv.member.decorations";
}

feme::spirv::StageIOMemberDecorationsMap
feme::spirv::collectStageIOMemberDecorations(mlir::Operation *Module) {
  StageIOMemberDecorationsMap Result;
  Module->walk([&](mlir::LLVM::GlobalOp Global) {
    auto MemberDecorations = mlir::dyn_cast_or_null<mlir::ArrayAttr>(
        Global->getAttr(getStageIOMemberDecorationsAttrName()));
    if (MemberDecorations)
      Result[Global.getSymName()] = MemberDecorations;
  });
  return Result;
}

void feme::spirv::attachStageIOMemberDecorations(
    const StageIOMemberDecorationsMap &MemberDecorations,
    llvm::Module &LLVMModule) {
  llvm::LLVMContext &Ctx = LLVMModule.getContext();
  llvm::IntegerType *I32 = llvm::Type::getInt32Ty(Ctx);
  for (const auto &Entry : MemberDecorations) {
    llvm::GlobalVariable *GV = LLVMModule.getGlobalVariable(Entry.getKey());
    if (!GV)
      continue;

    llvm::SmallVector<llvm::Metadata *> MemberNodes;
    for (mlir::Attribute OneMember : Entry.getValue()) {
      auto MemberEntry = mlir::cast<mlir::ArrayAttr>(OneMember);
      auto MemberIndex =
          mlir::cast<mlir::IntegerAttr>(MemberEntry[0]).getInt();
      auto MemberDecorationList = mlir::cast<mlir::ArrayAttr>(MemberEntry[1]);
      llvm::Metadata *IndexMD = llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(I32, MemberIndex));
      llvm::Metadata *DecorationsMD =
          buildDecorationListMD(MemberDecorationList, Ctx, I32);
      MemberNodes.push_back(llvm::MDNode::get(Ctx, {IndexMD, DecorationsMD}));
    }
    GV->setMetadata("feme.spirv.MemberDecorations",
                    llvm::MDNode::get(Ctx, MemberNodes));
  }
}

llvm::StringRef feme::spirv::getStageIOInt16SignednessAttrName() {
  return "feme.spirv.int16.signed";
}

feme::spirv::StageIOInt16SignednessSet
feme::spirv::collectStageIOInt16Signedness(mlir::Operation *Module) {
  StageIOInt16SignednessSet Result;
  Module->walk([&](mlir::LLVM::GlobalOp Global) {
    if (Global->hasAttr(getStageIOInt16SignednessAttrName()))
      Result.insert(Global.getSymName());
  });
  return Result;
}

void feme::spirv::attachStageIOInt16Signedness(
    const StageIOInt16SignednessSet &SignedNames, llvm::Module &LLVMModule) {
  llvm::LLVMContext &Ctx = LLVMModule.getContext();
  for (const auto &Entry : SignedNames) {
    llvm::GlobalVariable *GV = LLVMModule.getGlobalVariable(Entry.getKey());
    if (!GV)
      continue;
    GV->setMetadata("feme.spirv.Int16Signed", llvm::MDNode::get(Ctx, {}));
  }
}

