//===- GroupShared.cpp - CPU target groupshared memory layout ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GroupShared.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ReplaceConstant.h"
#include "llvm/Support/Alignment.h"

using namespace llvm;

namespace feme::cpu {

GroupSharedLayout computeGroupSharedLayout(const Module &M) {
  GroupSharedLayout Layout;
  const DataLayout &DL = M.getDataLayout();
  uint64_t Offset = 0;
  Align Strictest(1);

  for (const GlobalVariable &GV : M.globals()) {
    if (GV.getAddressSpace() != GroupSharedAddressSpace)
      continue;
    Align GVAlign = DL.getPreferredAlign(&GV);
    Strictest = std::max(Strictest, GVAlign);
    Offset = alignTo(Offset, GVAlign);
    Layout.Offsets[&GV] = Offset;
    Offset += DL.getTypeAllocSize(GV.getValueType());
  }

  Layout.TotalSize = Offset;
  Layout.Alignment = Strictest.value();
  return Layout;
}

bool rewriteGroupSharedGlobals(Function &F, Value *GroupSharedBase,
                               const GroupSharedLayout &Layout) {
  Module &M = *F.getParent();
  LLVMContext &Ctx = M.getContext();
  Type *I8Ty = Type::getInt8Ty(Ctx);

  // Every use of a groupshared global, gathered up front so the pass can
  // bail without touching `F` at all if it finds a shape it does not
  // support (see `rewriteGroupSharedGlobals`'s doc comment): this
  // milestone only canonicalizes a *uniform* access, left by
  // `feme::cpu::SIMDizePass`'s widening walk as a plain scalar
  // `getelementptr`/`load`/`store` -- a divergent access instead becomes a
  // vector-of-pointers `getelementptr` feeding `llvm.masked.gather`/
  // `.scatter` (see "Widening" in feme/docs/FeMeCPUDesign.md), which this
  // milestone does not yet rewrite.
  for (auto &[GVConst, Offset] : Layout.Offsets) {
    auto *GV = const_cast<GlobalVariable *>(GVConst);
    if (GV->use_empty())
      continue;

    // Normalize any constant-expression use (e.g. a folded
    // `getelementptr` constant, or a broadcast splat of `GV` itself) within
    // `F` into ordinary instructions first, so every use handled below is a
    // plain instruction operand.
    convertUsersOfConstantsToInstructions({GV}, &F);

    for (Use &U : GV->uses()) {
      auto *UserInst = dyn_cast<Instruction>(U.getUser());
      if (UserInst && UserInst->getFunction() != &F)
        continue; // A different entry point's use of the same global.
      if (!UserInst || (!isa<LoadInst>(UserInst) && !isa<StoreInst>(UserInst) &&
                        !isa<GetElementPtrInst>(UserInst))) {
        Ctx.emitError(
            "feme-cpu-simdize: groupshared global '" + GV->getName() +
            "' is used in a way this milestone does not yet canonicalize; "
            "only a uniform getelementptr, load, or store is supported "
            "(roadmap milestone 9 deviation)");
        return false;
      }
      if (auto *GEP = dyn_cast<GetElementPtrInst>(UserInst)) {
        if (GEP->getType()->isVectorTy()) {
          Ctx.emitError(
              "feme-cpu-simdize: groupshared global '" + GV->getName() +
              "' has a divergent (vector-of-pointers) access; only a "
              "uniform access is supported for now (roadmap milestone 9 "
              "deviation)");
          return false;
        }
        for (const User *GEPUser : GEP->users())
          if (!isa<LoadInst>(GEPUser) && !isa<StoreInst>(GEPUser)) {
            Ctx.emitError(
                "feme-cpu-simdize: groupshared global '" + GV->getName() +
                "' feeds a nested getelementptr or another unsupported "
                "user; only a first-level getelementptr feeding a direct "
                "load/store is supported (roadmap milestone 9 deviation)");
            return false;
          }
      }
    }
  }

  for (auto &[GVConst, Offset] : Layout.Offsets) {
    auto *GV = const_cast<GlobalVariable *>(GVConst);
    for (Use &U : make_early_inc_range(GV->uses())) {
      auto *UserInst = dyn_cast<Instruction>(U.getUser());
      if (!UserInst || UserInst->getFunction() != &F)
        continue;

      IRBuilder<> Builder(UserInst);
      Value *Flat =
          Builder.CreateGEP(I8Ty, GroupSharedBase, Builder.getInt64(Offset),
                            GV->getName() + ".flat");
      if (auto *GEP = dyn_cast<GetElementPtrInst>(UserInst)) {
        SmallVector<Value *, 4> Idxs(GEP->indices());
        Value *NewGEP =
            Builder.CreateGEP(GEP->getSourceElementType(), Flat, Idxs,
                              GEP->getName(), GEP->isInBounds());
        // `NewGEP` is address space 0 while `GEP` was `addrspace(3)`
        // (the address space "cast away", per the file comment above) --
        // `replaceAllUsesWith` requires identical types, so each use is
        // retargeted individually instead; every user this milestone
        // supports (a `load`/`store`, checked above) reads its pointer
        // operand's type dynamically rather than caching it.
        for (Use &GEPUse : make_early_inc_range(GEP->uses()))
          GEPUse.set(NewGEP);
        GEP->eraseFromParent();
      } else {
        U.set(Flat);
      }
    }
  }
  return true;
}

} // namespace feme::cpu
