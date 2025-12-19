//===- DXILStringLowering.cpp - Prepare strings for DXIL encoding ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
//===----------------------------------------------------------------------===//

#include "DirectX.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Pass.h"

#define DEBUG_TYPE "dxil-string-lowering"

using namespace llvm;

namespace {

class DXILStringLoweringModule : public ModulePass {

  StringTableBuilder StrTabBuilder;

  void addString(GlobalVariable *GV) {
    ConstantDataArray *CDA = cast<ConstantDataArray>(GV->getInitializer());
    StringRef Str = CDA->getAsString();
    StrTabBuilder.add(Str);
  }

  Value *convertStringToOffset(GlobalVariable *GV, IRBuilder<> &Builder) {
    ConstantDataArray *CDA = cast<ConstantDataArray>(GV->getInitializer());
    StringRef Str = CDA->getAsString();
    size_t Offset = StrTabBuilder.getOffset(Str);
    Constant *CastPtr = ConstantInt::get(Builder.getInt32Ty(), Offset);
    return CastPtr;
  }

public:
  bool runOnModule(Module &M) override {
    SmallVector<IntrinsicInst *> WorkList;
    for (Function &F : M)
      for (BasicBlock &BB : make_early_inc_range(F))
        for (Instruction &I : make_early_inc_range(BB))
          if (auto *II = dyn_cast<IntrinsicInst>(&I))
            if (II->getIntrinsicID() == Intrinsic::dx_string_to_offset) {
              if (!isa<GlobalVariable>(II->getArgOperand(0)))
                report_fatal_error(
                    "dx.string.to.offset argument must be a global string");
              addString(cast<GlobalVariable>(II->getArgOperand(0)));
              WorkList.push_back(II);
            }
    if (WorkList.empty())
      return false;

    StrTabBuilder.finalize();
    SmallString<256> StrTabString;
    raw_svector_ostream OS(StrTabString);
    StrTabBuilder.write(OS);
    Constant *StrTabContent =
        ConstantDataArray::getString(M.getContext(), StrTabString);
    auto *GV = new llvm::GlobalVariable(M, StrTabContent->getType(), true,
                                        GlobalValue::PrivateLinkage,
                                        StrTabContent, "dx.strtab");
    GV->setSection("STAB");
    GV->setAlignment(Align(4));
    for (IntrinsicInst *II : WorkList) {
      IRBuilder<> Builder(II);
      Value *OffsetPtr = convertStringToOffset(
          cast<GlobalVariable>(II->getArgOperand(0)), Builder);
      II->replaceAllUsesWith(OffsetPtr);
      II->eraseFromParent();
    }
    return true;
  }

  DXILStringLoweringModule()
      : ModulePass(ID), StrTabBuilder(StringTableBuilder::DXContainer) {}
  static char ID; // Pass identification.
};
char DXILStringLoweringModule::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(DXILStringLoweringModule, DEBUG_TYPE,
                "DXIL String Lowering Module", false, false)

ModulePass *llvm::createDXILStringLoweringModulePass() {
  return new DXILStringLoweringModule();
}
