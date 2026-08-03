//===- RaisedLowering.cpp - Lower raised IR to AMDGPU conventions --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/AMDGPU/RaisedLowering.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"
#include <array>
#include <optional>

using namespace llvm;
using namespace feme::amdgpu;

namespace {

/// A shader entry point's thread group dimensions, recovered from the
/// `hlsl.numthreads` function attribute feme::dxil::MetadataRaisingPass
/// reconstructs from DXIL's entry point metadata.
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

/// The AMDGPU intrinsics naming a workitem's index within its workgroup, and
/// its workgroup's index within the dispatch, per component (x/y/z).
constexpr Intrinsic::ID WorkitemIDs[] = {Intrinsic::amdgcn_workitem_id_x,
                                         Intrinsic::amdgcn_workitem_id_y,
                                         Intrinsic::amdgcn_workitem_id_z};
constexpr Intrinsic::ID WorkgroupIDs[] = {Intrinsic::amdgcn_workgroup_id_x,
                                          Intrinsic::amdgcn_workgroup_id_y,
                                          Intrinsic::amdgcn_workgroup_id_z};

/// A raised, format-agnostic intrinsic (see feme::dxil::OpRaisingPass and
/// feme::SPIRVToLLVMTranslator, which produce the `llvm.dx.*`/`llvm.spv.*`
/// spellings respectively -- see feme/docs/Design.md's "Per-Format
/// Representation Strategy" section) that queries a thread/group index by a
/// constant component operand (0/1/2 for x/y/z), and the three AMDGPU
/// intrinsics -- one per component -- it maps to.
struct ComponentQuery {
  Intrinsic::ID RaisedID;
  const Intrinsic::ID *AMDGPUComponentIDs;
};

/// The two parallel intrinsic families' spellings of each component query
/// this pass covers. `llvm.spv.*`'s are overloaded on return width (see
/// IntrinsicsSPIRV.td), but this pass only ever produces AMDGPU's fixed-width
/// `i32` intrinsics, so `lowerComponentQuery` below only rewrites calls whose
/// own type already matches that.
static const ComponentQuery ComponentQueries[] = {
    {Intrinsic::dx_group_id, WorkgroupIDs},
    {Intrinsic::dx_thread_id_in_group, WorkitemIDs},
    {Intrinsic::spv_group_id, WorkgroupIDs},
    {Intrinsic::spv_thread_id_in_group, WorkitemIDs},
};

Value *createComponentCall(IRBuilder<> &Builder, const Intrinsic::ID *IDs,
                           unsigned Component) {
  Function *Fn = Intrinsic::getOrInsertDeclaration(
      Builder.GetInsertBlock()->getModule(), IDs[Component]);
  return Builder.CreateCall(Fn, {});
}

/// Rewrites a single call to one of the intrinsics in \p ComponentQueries
/// into the corresponding per-component AMDGPU intrinsic call, if \p CI's
/// sole operand is a constant in range [0, 3) and \p CI itself is `i32`
/// (always true for the fixed-width `llvm.dx.*` family; only sometimes true
/// for the overloaded `llvm.spv.*` one). Returns false (leaving \p CI
/// untouched) otherwise, so callers can safely skip calls whose component
/// isn't a compile-time constant (which this direct 1:1 mapping cannot
/// express) rather than crashing on them.
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
  Value *NewCall = createComponentCall(Builder, Query.AMDGPUComponentIDs,
                                       static_cast<unsigned>(ComponentIndex));
  NewCall->takeName(&CI);
  CI.replaceAllUsesWith(NewCall);
  CI.eraseFromParent();
  return true;
}

/// Lowers a `llvm.dx.thread.id`/`llvm.spv.thread.id` call -- the
/// *dispatch-wide* invocation index, which AMDGPU has no single intrinsic
/// for -- into `workgroup_id * <thread group size> + workitem_id` for the
/// requested component. The thread group size comes from the entry point's
/// `hlsl.numthreads` attribute, so calls in a function without one are left
/// unmodified, as is a `llvm.spv.thread.id` call whose overloaded return
/// width isn't the `i32` this pass produces.
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
  Value *GroupID = createComponentCall(Builder, WorkgroupIDs, Index);
  Value *ItemID = createComponentCall(Builder, WorkitemIDs, Index);
  Value *Scaled = Builder.CreateMul(GroupID, Builder.getInt32((*Size)[Index]));
  Value *ThreadID = Builder.CreateAdd(Scaled, ItemID);
  ThreadID->takeName(&CI);
  CI.replaceAllUsesWith(ThreadID);
  CI.eraseFromParent();
  return true;
}

/// Lowers a `llvm.dx.flattened.thread.id.in.group`/
/// `llvm.spv.flattened.thread.id.in.group` call into the linearized
/// `x + y * X + z * X * Y` combination of AMDGPU's per-component workitem
/// ids, using the entry point's `hlsl.numthreads` dimensions.
bool lowerFlattenedThreadIDInGroup(CallInst &CI) {
  std::optional<ThreadGroupSize> Size = getThreadGroupSize(*CI.getFunction());
  if (!Size)
    return false;

  IRBuilder<> Builder(&CI);
  Value *Flat = createComponentCall(Builder, WorkitemIDs, 0);
  uint32_t Stride = 1;
  for (unsigned I = 1; I != 3; ++I) {
    Stride *= (*Size)[I - 1];
    Value *Component = createComponentCall(Builder, WorkitemIDs, I);
    Flat = Builder.CreateAdd(
        Flat, Builder.CreateMul(Component, Builder.getInt32(Stride)));
  }
  Flat->takeName(&CI);
  CI.replaceAllUsesWith(Flat);
  CI.eraseFromParent();
  return true;
}

/// Gives a shader entry point AMDGPU's kernel calling convention, plus the
/// `amdgpu-flat-work-group-size` bound its `hlsl.numthreads` dimensions
/// describe. Without this the entry point is emitted as an ordinary device
/// function, which no host runtime can dispatch.
bool lowerEntryPoint(Function &F) {
  if (!F.hasFnAttribute("hlsl.shader") ||
      F.getCallingConv() == CallingConv::AMDGPU_KERNEL)
    return false;

  F.setCallingConv(CallingConv::AMDGPU_KERNEL);
  if (std::optional<ThreadGroupSize> Size = getThreadGroupSize(F)) {
    uint64_t FlatSize =
        static_cast<uint64_t>((*Size)[0]) * (*Size)[1] * (*Size)[2];
    if (FlatSize > 0)
      F.addFnAttr("amdgpu-flat-work-group-size", "1," + llvm::utostr(FlatSize));
  }
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

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
