//===- RaisedLowering.cpp - Lower raised IR to NVPTX conventions ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/NVPTX/RaisedLowering.h"

#include "feme/Core/ShaderStage.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"
#include <array>
#include <optional>

using namespace llvm;
using namespace feme::nvptx;

namespace {

/// A shader entry point's thread group dimensions, recovered from the
/// `hlsl.numthreads` function attribute (see
/// feme::amdgpu::RaisedLoweringPass's own `getThreadGroupSize`, which this
/// mirrors).
using ThreadGroupSize = std::array<uint32_t, 3>;

std::optional<ThreadGroupSize> getThreadGroupSize(const Function &F) {
  StringRef NumThreads = F.getFnAttribute("hlsl.numthreads").getValueAsString();
  if (NumThreads.empty())
    return std::nullopt;

  SmallVector<StringRef, 3> Components;
  NumThreads.split(Components, ',');
  if (Components.size() != 3)
    return std::nullopt;

  ThreadGroupSize Size{};
  for (unsigned I = 0; I != 3; ++I)
    if (!llvm::to_integer(Components[I], Size[I], 10))
      return std::nullopt;
  return Size;
}

/// The NVVM intrinsics naming a thread's index within its block (CUDA's
/// analog of a workitem within a workgroup), and its block's index within
/// the grid (CUDA's analog of a workgroup within a dispatch), per component
/// (x/y/z).
constexpr Intrinsic::ID ThreadIDs[] = {Intrinsic::nvvm_read_ptx_sreg_tid_x,
                                       Intrinsic::nvvm_read_ptx_sreg_tid_y,
                                       Intrinsic::nvvm_read_ptx_sreg_tid_z};
constexpr Intrinsic::ID BlockIDs[] = {Intrinsic::nvvm_read_ptx_sreg_ctaid_x,
                                      Intrinsic::nvvm_read_ptx_sreg_ctaid_y,
                                      Intrinsic::nvvm_read_ptx_sreg_ctaid_z};

/// A raised, format-agnostic intrinsic that queries a thread/group index by
/// a constant component operand (0/1/2 for x/y/z), and the three NVVM
/// intrinsics -- one per component -- it maps to (see
/// feme::amdgpu::RaisedLoweringPass's `ComponentQuery`, which this mirrors).
struct ComponentQuery {
  Intrinsic::ID RaisedID;
  const Intrinsic::ID *NVPTXComponentIDs;
};

static const ComponentQuery ComponentQueries[] = {
    {Intrinsic::dx_group_id, BlockIDs},
    {Intrinsic::dx_thread_id_in_group, ThreadIDs},
    {Intrinsic::spv_group_id, BlockIDs},
    {Intrinsic::spv_thread_id_in_group, ThreadIDs},
};

Value *createComponentCall(IRBuilder<> &Builder, const Intrinsic::ID *IDs,
                           unsigned Component) {
  Function *Fn = Intrinsic::getOrInsertDeclaration(
      Builder.GetInsertBlock()->getModule(), IDs[Component]);
  return Builder.CreateCall(Fn, {});
}

/// Rewrites a single call to one of the intrinsics in \p ComponentQueries
/// into the corresponding per-component NVVM intrinsic call, mirroring
/// feme::amdgpu::RaisedLoweringPass's `lowerComponentQuery` (see its own
/// comment for the width/range checks this shares).
bool lowerComponentQuery(CallInst &CI, const ComponentQuery &Query) {
  if (!CI.getType()->isIntegerTy(32) || CI.arg_size() != 1)
    return false;

  auto *Component = dyn_cast<ConstantInt>(CI.getArgOperand(0));
  if (!Component)
    return false;

  uint64_t ComponentIndex = Component->getZExtValue();
  if (ComponentIndex >= 3)
    return false;

  IRBuilder<> Builder(&CI);
  Value *NewCall = createComponentCall(Builder, Query.NVPTXComponentIDs,
                                       static_cast<unsigned>(ComponentIndex));
  NewCall->takeName(&CI);
  CI.replaceAllUsesWith(NewCall);
  CI.eraseFromParent();
  return true;
}

/// Lowers a `llvm.dx.thread.id`/`llvm.spv.thread.id` call -- the
/// *dispatch-wide* invocation index, which NVPTX has no single intrinsic
/// for either -- into `block_id * <thread group size> + thread_id` for the
/// requested component, mirroring
/// feme::amdgpu::RaisedLoweringPass::lowerThreadID.
bool lowerThreadID(CallInst &CI) {
  if (!CI.getType()->isIntegerTy(32) || CI.arg_size() != 1)
    return false;
  auto *Component = dyn_cast<ConstantInt>(CI.getArgOperand(0));
  if (!Component || Component->getZExtValue() >= 3)
    return false;
  std::optional<ThreadGroupSize> Size = getThreadGroupSize(*CI.getFunction());
  if (!Size)
    return false;

  unsigned Index = static_cast<unsigned>(Component->getZExtValue());
  IRBuilder<> Builder(&CI);
  Value *BlockID = createComponentCall(Builder, BlockIDs, Index);
  Value *ThreadInBlock = createComponentCall(Builder, ThreadIDs, Index);
  Value *Scaled = Builder.CreateMul(BlockID, Builder.getInt32((*Size)[Index]));
  Value *ThreadID = Builder.CreateAdd(Scaled, ThreadInBlock);
  ThreadID->takeName(&CI);
  CI.replaceAllUsesWith(ThreadID);
  CI.eraseFromParent();
  return true;
}

/// Lowers a `llvm.dx.flattened.thread.id.in.group`/
/// `llvm.spv.flattened.thread.id.in.group` call into the linearized
/// `x + y * X + z * X * Y` combination of NVPTX's per-component thread ids,
/// using the entry point's `hlsl.numthreads` dimensions -- mirroring
/// feme::amdgpu::RaisedLoweringPass::lowerFlattenedThreadIDInGroup.
bool lowerFlattenedThreadIDInGroup(CallInst &CI) {
  std::optional<ThreadGroupSize> Size = getThreadGroupSize(*CI.getFunction());
  if (!Size)
    return false;

  IRBuilder<> Builder(&CI);
  Value *Flat = createComponentCall(Builder, ThreadIDs, 0);
  uint32_t Stride = 1;
  for (unsigned I = 1; I != 3; ++I) {
    Stride *= (*Size)[I - 1];
    Value *Component = createComponentCall(Builder, ThreadIDs, I);
    Flat = Builder.CreateAdd(
        Flat, Builder.CreateMul(Component, Builder.getInt32(Stride)));
  }
  Flat->takeName(&CI);
  CI.replaceAllUsesWith(Flat);
  CI.eraseFromParent();
  return true;
}

/// Gives a shader entry point PTX's kernel calling convention. Without this
/// the entry point is emitted as an ordinary device function, which no host
/// runtime can launch.
bool lowerEntryPoint(Function &F) {
  if (!feme::isShaderEntryPoint(F) ||
      F.getCallingConv() == CallingConv::PTX_Kernel)
    return false;

  F.setCallingConv(CallingConv::PTX_Kernel);
  return true;
}

/// Runs \p Lower over every call to the intrinsic \p ID in \p M.
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

/// NVPTX's local address space (`llvm::NVPTXAS::ADDRESS_SPACE_LOCAL`), the
/// only one its `alloca`/frame-index selection covers -- see
/// feme::amdgpu::RaisedLoweringPass's own `PrivateAddressSpace` comment,
/// which this mirrors (the two constants share the same numeric value only
/// by coincidence).
constexpr unsigned LocalAddressSpace = 5;

/// See feme::amdgpu::RaisedLoweringPass::hasOnlySupportedPointerUses, which
/// this is a direct copy of: this pass only knows how to retarget the same
/// `getelementptr`-chain-then-`load`/`store` shapes that one does.
bool hasOnlySupportedPointerUses(const Value &Ptr) {
  for (const User *U : Ptr.users()) {
    if (isa<GetElementPtrInst>(U)) {
      if (!hasOnlySupportedPointerUses(*U))
        return false;
      continue;
    }
    if (auto *SI = dyn_cast<StoreInst>(U)) {
      if (SI->getPointerOperand() != &Ptr)
        return false;
      continue;
    }
    if (!isa<LoadInst>(U))
      return false;
  }
  return true;
}

/// See feme::amdgpu::RaisedLoweringPass::retypePointerUsers, which this is
/// a direct copy of.
void retypePointerUsers(Value &OldPtr, Value &NewPtr) {
  for (Use &U : llvm::make_early_inc_range(OldPtr.uses())) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U.getUser())) {
      IRBuilder<> Builder(GEP);
      SmallVector<Value *, 4> Indices(GEP->indices());
      Value *NewGEP = Builder.CreateGEP(GEP->getSourceElementType(), &NewPtr,
                                        Indices, "", GEP->isInBounds());
      NewGEP->takeName(GEP);
      retypePointerUsers(*GEP, *NewGEP);
      GEP->eraseFromParent();
      continue;
    }
    U.set(&NewPtr);
  }
}

/// Moves \p Alloca into NVPTX's local address space if it is not there
/// already, rebuilding its users (see `retypePointerUsers`) to match.
bool lowerAllocaAddressSpace(AllocaInst &Alloca) {
  if (Alloca.getAddressSpace() == LocalAddressSpace)
    return false;
  if (!hasOnlySupportedPointerUses(Alloca))
    return false;

  IRBuilder<> Builder(&Alloca);
  AllocaInst *NewAlloca = Builder.CreateAlloca(
      Alloca.getAllocatedType(), LocalAddressSpace, Alloca.getArraySize());
  NewAlloca->setAlignment(Alloca.getAlign());
  NewAlloca->takeName(&Alloca);
  retypePointerUsers(Alloca, *NewAlloca);
  Alloca.eraseFromParent();
  return true;
}

} // namespace

PreservedAnalyses RaisedLoweringPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;

  for (Function &F : M)
    Changed |= lowerEntryPoint(F);

  for (const ComponentQuery &Query : ComponentQueries)
    Changed |= forEachIntrinsicCall(M, Query.RaisedID, [&Query](CallInst &CI) {
      return lowerComponentQuery(CI, Query);
    });

  Changed |= forEachIntrinsicCall(M, Intrinsic::dx_thread_id, lowerThreadID);
  Changed |= forEachIntrinsicCall(M, Intrinsic::spv_thread_id, lowerThreadID);
  Changed |= forEachIntrinsicCall(M, Intrinsic::dx_flattened_thread_id_in_group,
                                  lowerFlattenedThreadIDInGroup);
  Changed |=
      forEachIntrinsicCall(M, Intrinsic::spv_flattened_thread_id_in_group,
                           lowerFlattenedThreadIDInGroup);

  for (Function &F : M)
    for (Instruction &I : llvm::make_early_inc_range(instructions(F)))
      if (auto *Alloca = dyn_cast<AllocaInst>(&I))
        Changed |= lowerAllocaAddressSpace(*Alloca);

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
