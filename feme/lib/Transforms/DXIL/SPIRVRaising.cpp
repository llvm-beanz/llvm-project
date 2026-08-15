//===- SPIRVRaising.cpp - Raise SPIR-V-derived IR to DXIL conventions ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/DXIL/SPIRVRaising.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::dxil;

namespace {

/// A SPIR-V-derived, format-specific intrinsic and the raised,
/// format-agnostic `llvm.dx.*` intrinsic with the same meaning; both
/// families are the two front ends' parallel spellings of the same HLSL
/// builtin, so raising is a straight substitution of the callee (see
/// feme::spirv::RaisedLoweringPass's `DirectMapping`, whose `RaisedID`/
/// `SPIRVID` fields this is the mirror image of).
struct DirectMapping {
  Intrinsic::ID SPIRVID;
  Intrinsic::ID RaisedID;
};

static const DirectMapping DirectMappings[] = {
    {Intrinsic::spv_thread_id, Intrinsic::dx_thread_id},
    {Intrinsic::spv_group_id, Intrinsic::dx_group_id},
    {Intrinsic::spv_thread_id_in_group, Intrinsic::dx_thread_id_in_group},
    {Intrinsic::spv_flattened_thread_id_in_group,
     Intrinsic::dx_flattened_thread_id_in_group},
};

/// Rewrites a call to a `llvm.spv.*` intrinsic into the corresponding
/// `llvm.dx.*` one. `llvm.dx.*`'s family is fixed-`i32` (unlike `llvm.spv.*`,
/// which is overloaded on return width), so this only fires for a call
/// already of that width -- a narrower/wider `llvm.spv.*` result has no
/// `llvm.dx.*` counterpart to raise into.
bool substituteCallee(CallInst &CI, Intrinsic::ID To) {
  if (!CI.getType()->isIntegerTy(32))
    return false;
  Function *NewFn = Intrinsic::getOrInsertDeclaration(CI.getModule(), To);
  if (NewFn->getFunctionType() != CI.getFunctionType())
    return false;
  CI.setCalledFunction(NewFn);
  return true;
}

/// Translates a SPIR-V `StorageBuffer` handle type
/// (`target("spirv.VulkanBuffer", [0 x ElemTy], StorageClass, IsWriteable)`,
/// see feme::spirv::convertBufferBlockType in SPIRVToLLVMPatterns.cpp) into
/// the DXIL raw/structured-buffer handle type describing the same resource
/// (`target("dx.RawBuffer", ElemTy, IsUAV, IsROV)`, see
/// feme::dxil::OpRaisingPass::raiseResourceHandleFromBinding), or nullptr
/// if \p SPIRVTy is not that shape.
TargetExtType *translateHandleType(TargetExtType *SPIRVTy) {
  if (SPIRVTy->getName() != "spirv.VulkanBuffer" ||
      SPIRVTy->getNumTypeParameters() != 1 ||
      SPIRVTy->getNumIntParameters() != 2)
    return nullptr;

  auto *ArrayTy = dyn_cast<ArrayType>(SPIRVTy->getTypeParameter(0));
  if (!ArrayTy)
    return nullptr;

  bool IsUAV = SPIRVTy->getIntParameter(1) != 0;
  // SPIR-V has no read-only-vs-rasterizer-ordered distinction to recover an
  // `IsROV` bit from; a `RasterizerOrderedStructuredBuffer<T>` is future
  // work (see the header comment's scope).
  return TargetExtType::get(SPIRVTy->getContext(), "dx.RawBuffer",
                            {ArrayTy->getElementType()},
                            {static_cast<unsigned>(IsUAV), /*IsROV=*/0u});
}

/// Returns false if any user of \p GetPtr is something other than an
/// ordinary `load`, or a `store` it is the pointer (not stored-value)
/// operand of -- i.e. any further `getelementptr` into the element's own
/// fields, which this pass does not (yet) model, matching
/// feme::cpu::SPIRVResourceLoweringPass::hasOnlySupportedUses's own
/// narrowing (see the header comment).
bool hasOnlySupportedAccesses(const CallInst &GetPtr) {
  for (const User *U : GetPtr.users()) {
    if (const auto *SI = dyn_cast<StoreInst>(U)) {
      if (SI->getPointerOperand() != &GetPtr)
        return false;
      continue;
    }
    if (!isa<LoadInst>(U))
      return false;
  }
  return true;
}

/// Rewrites the `llvm.spv.resource.getpointer` plus load/store accesses
/// through \p Handle -- already replaced by the DXIL handle \p NewHandle --
/// into `llvm.dx.resource.load.rawbuffer`/`store.rawbuffer`, the same
/// raw/structured buffer access `feme::dxil::OpRaisingPass::
/// raiseRawBufferLoad`/`raiseRawBufferStore` produce. The element index
/// `getpointer`'s own operand carries becomes `Coord0`; `Coord1` (the
/// intra-element byte offset a real `StructuredBuffer` field access would
/// use) is always zero, since only a whole-element access is modeled here.
bool lowerHandleAccesses(CallInst &Handle, Value *NewHandle) {
  for (User *U : llvm::make_early_inc_range(Handle.users())) {
    auto *GetPtr = dyn_cast<CallInst>(U);
    Function *Callee = GetPtr ? GetPtr->getCalledFunction() : nullptr;
    if (!Callee ||
        Callee->getIntrinsicID() != Intrinsic::spv_resource_getpointer)
      return false;
    if (!hasOnlySupportedAccesses(*GetPtr))
      return false;

    Value *Index = GetPtr->getArgOperand(1);
    Value *ByteOffset = ConstantInt::get(Index->getType(), 0);

    for (User *PtrUser : llvm::make_early_inc_range(GetPtr->users())) {
      if (auto *SI = dyn_cast<StoreInst>(PtrUser)) {
        Value *Stored = SI->getValueOperand();
        IRBuilder<> Builder(SI);
        Function *StoreFn = Intrinsic::getOrInsertDeclaration(
            Handle.getModule(), Intrinsic::dx_resource_store_rawbuffer,
            {NewHandle->getType(), Stored->getType()});
        Builder.CreateCall(StoreFn, {NewHandle, Index, ByteOffset, Stored});
        SI->eraseFromParent();
        continue;
      }
      auto *LI = cast<LoadInst>(PtrUser);
      IRBuilder<> Builder(LI);
      Function *LoadFn = Intrinsic::getOrInsertDeclaration(
          Handle.getModule(), Intrinsic::dx_resource_load_rawbuffer,
          {LI->getType(), NewHandle->getType()});
      Value *Loaded =
          Builder.CreateCall(LoadFn, {NewHandle, Index, ByteOffset});
      Value *Value0 = Builder.CreateExtractValue(Loaded, 0);
      LI->replaceAllUsesWith(Value0);
      LI->eraseFromParent();
    }
    GetPtr->eraseFromParent();
  }
  Handle.eraseFromParent();
  return true;
}

/// Lowers one `llvm.spv.resource.handlefrombinding` call, together with
/// every flat-element access through it. SPIR-V's (descriptor set, binding)
/// pair is DXIL's (register space, register), in the same operand order
/// (see feme::spirv::RaisedLoweringPass::lowerResourceHandle's own
/// comment, which relies on the same correspondence in the other
/// direction), so only the handle type and its accesses need translating.
bool lowerResourceHandle(CallInst &CI) {
  auto *SPIRVTy = dyn_cast<TargetExtType>(CI.getType());
  TargetExtType *DXILTy = SPIRVTy ? translateHandleType(SPIRVTy) : nullptr;
  if (!DXILTy)
    return false;

  IRBuilder<> Builder(&CI);
  Function *NewFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::dx_resource_handlefrombinding, {DXILTy});
  SmallVector<Value *, 5> Args(CI.args());
  Value *NewHandle = Builder.CreateCall(NewFn, Args);
  return lowerHandleAccesses(CI, NewHandle);
}

/// Runs \p Lower over every call to the intrinsic \p ID in \p M, erasing the
/// intrinsic's declaration once it has no callers left.
bool forEachIntrinsicCall(Module &M, Intrinsic::ID ID,
                          function_ref<bool(CallInst &)> Lower) {
  bool Changed = false;
  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    if (F.getIntrinsicID() != ID)
      continue;
    for (User *U : llvm::make_early_inc_range(F.users())) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getCalledFunction() != &F)
        continue;
      Changed |= Lower(*CI);
    }
    if (F.use_empty())
      F.eraseFromParent();
  }
  return Changed;
}

} // namespace

PreservedAnalyses SPIRVRaisingPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;

  for (const DirectMapping &Mapping : DirectMappings)
    Changed |=
        forEachIntrinsicCall(M, Mapping.SPIRVID, [&Mapping](CallInst &CI) {
          return substituteCallee(CI, Mapping.RaisedID);
        });

  Changed |= forEachIntrinsicCall(M, Intrinsic::spv_resource_handlefrombinding,
                                  lowerResourceHandle);

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
