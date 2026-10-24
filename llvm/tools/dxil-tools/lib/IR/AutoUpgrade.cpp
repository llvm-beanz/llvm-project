//===-- AutoUpgrade.cpp - Implement auto-upgrade helper functions ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the auto-upgrade helper functions.
// This is where deprecated IR intrinsics and other IR features are updated to
// current specifications.
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/AutoUpgrade.h"
#include "dxil/IR/CFG.h"
#include "dxil/IR/CallSite.h"
#include "dxil/IR/Constants.h"
#include "dxil/IR/DIBuilder.h"
#include "dxil/IR/DebugInfo.h"
#include "dxil/IR/DiagnosticInfo.h"
#include "dxil/IR/Function.h"
#include "dxil/IR/IRBuilder.h"
#include "dxil/IR/Instruction.h"
#include "dxil/IR/IntrinsicInst.h"
#include "dxil/IR/LLVMContext.h"
#include "dxil/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstring>
using namespace llvm;


static bool UpgradeIntrinsicFunction1(Function *F, Function *&NewFn) {
  assert(F && "Illegal to upgrade a non-existent Function.");

  // Quickly eliminate it, if it's not a candidate.
  StringRef Name = F->getName();
  if (Name.size() <= 8 || !Name.startswith("llvm."))
    return false;
  Name = Name.substr(5); // Strip off "llvm."

  switch (Name[0]) {
  default: break;
  case 'a': {
    if (Name.startswith("arm.neon.vclz")) {
      Type* args[2] = {
        F->arg_begin()->getType(),
        Type::getInt1Ty(F->getContext())
      };
      // Can't use Intrinsic::getDeclaration here as it adds a ".i1" to
      // the end of the name. Change name from llvm.arm.neon.vclz.* to
      //  llvm.ctlz.*
      FunctionType* fType = FunctionType::get(F->getReturnType(), args, false);
      NewFn = Function::Create(fType, F->getLinkage(),
                               "llvm.ctlz." + Name.substr(14), F->getParent());
      return true;
    }
    if (Name.startswith("arm.neon.vcnt")) {
      NewFn = Intrinsic::getDeclaration(F->getParent(), Intrinsic::ctpop,
                                        F->arg_begin()->getType());
      return true;
    }
    break;
  }
  case 'c': {
    if (Name.startswith("ctlz.") && F->arg_size() == 1) {
      F->setName(Name + ".old");
      NewFn = Intrinsic::getDeclaration(F->getParent(), Intrinsic::ctlz,
                                        F->arg_begin()->getType());
      return true;
    }
    if (Name.startswith("cttz.") && F->arg_size() == 1) {
      F->setName(Name + ".old");
      NewFn = Intrinsic::getDeclaration(F->getParent(), Intrinsic::cttz,
                                        F->arg_begin()->getType());
      return true;
    }
    break;
  }

  case 'o':
    // We only need to change the name to match the mangling including the
    // address space.
    if (F->arg_size() == 2 && Name.startswith("objectsize.")) {
      Type *Tys[2] = { F->getReturnType(), F->arg_begin()->getType() };
      if (F->getName() != Intrinsic::getName(Intrinsic::objectsize, Tys)) {
        F->setName(Name + ".old");
        NewFn = Intrinsic::getDeclaration(F->getParent(),
                                          Intrinsic::objectsize, Tys);
        return true;
      }
    }
    break;

  }

  //  This may not belong here. This function is effectively being overloaded
  //  to both detect an intrinsic which needs upgrading, and to provide the
  //  upgraded form of the intrinsic. We should perhaps have two separate
  //  functions for this.
  return false;
}

bool llvm::UpgradeIntrinsicFunction(Function *F, Function *&NewFn) {
  NewFn = nullptr;
  bool Upgraded = UpgradeIntrinsicFunction1(F, NewFn);
  assert(F != NewFn && "Intrinsic function upgraded to the same function");

  // Upgrade intrinsic attributes.  This does not change the function.
  if (NewFn)
    F = NewFn;
  if (Intrinsic::ID id = F->getIntrinsicID())
    F->setAttributes(Intrinsic::getAttributes(F->getContext(), id));
  return Upgraded;
}

bool llvm::UpgradeGlobalVariable(GlobalVariable *GV) {
  // Nothing to do yet.
  return false;
}


// UpgradeIntrinsicCall - Upgrade a call to an old intrinsic to be a call the
// upgraded intrinsic. All argument and return casting must be provided in
// order to seamlessly integrate with existing context.
void llvm::UpgradeIntrinsicCall(CallInst *CI, Function *NewFn) {
  Function *F = CI->getCalledFunction();
  LLVMContext &C = CI->getContext();
  IRBuilder<> Builder(C);
  Builder.SetInsertPoint(CI->getParent(), CI);

  assert(F && "Intrinsic call is not direct?");

  if (!NewFn) {
    (void)F;  // HLSL Change - unused local variable
    llvm_unreachable("HLSL - should not be upgrading platform intrinsics."); // HLSL Change - remove platform intrinsics

    return;
  }

  std::string Name = CI->getName();
  if (!Name.empty())
    CI->setName(Name + ".old");

  switch (NewFn->getIntrinsicID()) {
  default:
    llvm_unreachable("Unknown function for CallInst upgrade.");

  case Intrinsic::ctlz:
  case Intrinsic::cttz:
    assert(CI->getNumArgOperands() == 1 &&
           "Mismatch between function args and call args");
    CI->replaceAllUsesWith(Builder.CreateCall(
        NewFn, {CI->getArgOperand(0), Builder.getFalse()}, Name));
    CI->eraseFromParent();
    return;

  case Intrinsic::objectsize:
    CI->replaceAllUsesWith(Builder.CreateCall(
        NewFn, {CI->getArgOperand(0), CI->getArgOperand(1)}, Name));
    CI->eraseFromParent();
    return;

  case Intrinsic::ctpop: {
    CI->replaceAllUsesWith(Builder.CreateCall(NewFn, {CI->getArgOperand(0)}));
    CI->eraseFromParent();
    return;
  }

  }
}

// This tests each Function to determine if it needs upgrading. When we find
// one we are interested in, we then upgrade all calls to reflect the new
// function.
void llvm::UpgradeCallsToIntrinsic(Function* F) {
  assert(F && "Illegal attempt to upgrade a non-existent intrinsic.");

  // Upgrade the function and check if it is a totaly new function.
  Function *NewFn;
  if (UpgradeIntrinsicFunction(F, NewFn)) {
    // Replace all uses to the old function with the new one if necessary.
    for (Value::user_iterator UI = F->user_begin(), UE = F->user_end();
         UI != UE;) {
      if (CallInst *CI = dyn_cast<CallInst>(*UI++))
        UpgradeIntrinsicCall(CI, NewFn);
    }
    // Remove old function, no longer used, from the module.
    F->eraseFromParent();
  }
}

void llvm::UpgradeInstWithTBAATag(Instruction *I) {
  MDNode *MD = I->getMetadata(LLVMContext::MD_tbaa);
  assert(MD && "UpgradeInstWithTBAATag should have a TBAA tag");
  // Check if the tag uses struct-path aware TBAA format.
  if (isa<MDNode>(MD->getOperand(0)) && MD->getNumOperands() >= 3)
    return;

  if (MD->getNumOperands() == 3) {
    Metadata *Elts[] = {MD->getOperand(0), MD->getOperand(1)};
    MDNode *ScalarType = MDNode::get(I->getContext(), Elts);
    // Create a MDNode <ScalarType, ScalarType, offset 0, const>
    Metadata *Elts2[] = {ScalarType, ScalarType,
                         ConstantAsMetadata::get(Constant::getNullValue(
                             Type::getInt64Ty(I->getContext()))),
                         MD->getOperand(2)};
    I->setMetadata(LLVMContext::MD_tbaa, MDNode::get(I->getContext(), Elts2));
  } else {
    // Create a MDNode <MD, MD, offset 0>
    Metadata *Elts[] = {MD, MD, ConstantAsMetadata::get(Constant::getNullValue(
                                    Type::getInt64Ty(I->getContext())))};
    I->setMetadata(LLVMContext::MD_tbaa, MDNode::get(I->getContext(), Elts));
  }
}

Instruction *llvm::UpgradeBitCastInst(unsigned Opc, Value *V, Type *DestTy,
                                      Instruction *&Temp) {
  if (Opc != Instruction::BitCast)
    return nullptr;

  Temp = nullptr;
  Type *SrcTy = V->getType();
  if (SrcTy->isPtrOrPtrVectorTy() && DestTy->isPtrOrPtrVectorTy() &&
      SrcTy->getPointerAddressSpace() != DestTy->getPointerAddressSpace()) {
    LLVMContext &Context = V->getContext();

    // We have no information about target data layout, so we assume that
    // the maximum pointer size is 64bit.
    Type *MidTy = Type::getInt64Ty(Context);
    Temp = CastInst::Create(Instruction::PtrToInt, V, MidTy);

    return CastInst::Create(Instruction::IntToPtr, Temp, DestTy);
  }

  return nullptr;
}

Value *llvm::UpgradeBitCastExpr(unsigned Opc, Constant *C, Type *DestTy) {
  if (Opc != Instruction::BitCast)
    return nullptr;

  Type *SrcTy = C->getType();
  if (SrcTy->isPtrOrPtrVectorTy() && DestTy->isPtrOrPtrVectorTy() &&
      SrcTy->getPointerAddressSpace() != DestTy->getPointerAddressSpace()) {
    LLVMContext &Context = C->getContext();

    // We have no information about target data layout, so we assume that
    // the maximum pointer size is 64bit.
    Type *MidTy = Type::getInt64Ty(Context);

    return ConstantExpr::getIntToPtr(ConstantExpr::getPtrToInt(C, MidTy),
                                     DestTy);
  }

  return nullptr;
}

/// Check the debug info version number, if it is out-dated, drop the debug
/// info. Return true if module is modified.
bool llvm::UpgradeDebugInfo(Module &M) {
  unsigned Version = getDebugMetadataVersionFromModule(M);
  if (Version == DEBUG_METADATA_VERSION)
    return false;

  bool RetCode = StripDebugInfo(M);
  if (RetCode) {
    DiagnosticInfoDebugMetadataVersion DiagVersion(M, Version);
    M.getContext().diagnose(DiagVersion);
  }
  return RetCode;
}

void llvm::UpgradeMDStringConstant(std::string &String) {
  const std::string OldPrefix = "llvm.vectorizer.";
  if (String == "llvm.vectorizer.unroll") {
    String = "llvm.loop.interleave.count";
  } else if (String.find(OldPrefix) == 0) {
    String.replace(0, OldPrefix.size(), "llvm.loop.vectorize.");
  }
}
