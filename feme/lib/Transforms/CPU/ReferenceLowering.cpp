//===- ReferenceLowering.cpp - `--reference`'s scalar builtin half --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ReferenceLowering.h"

#include "DispatchArgsLayout.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::cpu;

namespace feme::cpu {
const char ReferenceThreadIndexInGroupGlobalName[] =
    "feme.cpu.ref.thread_index_in_group";
const char ReferenceGroupIDGlobalName[] = "feme.cpu.ref.group_id";
const char ReferenceLoweredAttrName[] = "feme.cpu.reference";
} // namespace feme::cpu

namespace {

/// Which raised builtin \p ID is, for this pass's purposes; `std::nullopt`
/// for anything this pass leaves untouched (ordinary instructions).
enum class RaisedBuiltin {
  ThreadId,
  ThreadIdInGroup,
  FlattenedThreadIdInGroup,
  GroupId,
  Unsupported
};

std::optional<RaisedBuiltin> classify(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::dx_thread_id:
  case Intrinsic::spv_thread_id:
    return RaisedBuiltin::ThreadId;
  case Intrinsic::dx_thread_id_in_group:
  case Intrinsic::spv_thread_id_in_group:
    return RaisedBuiltin::ThreadIdInGroup;
  case Intrinsic::dx_flattened_thread_id_in_group:
  case Intrinsic::spv_flattened_thread_id_in_group:
    return RaisedBuiltin::FlattenedThreadIdInGroup;
  case Intrinsic::dx_group_id:
  case Intrinsic::spv_group_id:
    return RaisedBuiltin::GroupId;
  case Intrinsic::dx_wave_getlaneindex:
    // Wave intrinsics have no meaning one invocation at a time (see the
    // header comment): reported as `Unsupported` rather than left alone,
    // so the caller can diagnose it instead of silently leaving a raised
    // builtin nothing downstream will ever lower.
    return RaisedBuiltin::Unsupported;
  default:
    return std::nullopt;
  }
}

/// Gets (creating if absent) the module-level global \p Name of type \p Ty,
/// zero-initialized, internal linkage: `--reference`'s per-invocation/group
/// state (see the header comment).
GlobalVariable *getOrCreateRefGlobal(Module &M, StringRef Name, Type *Ty) {
  if (GlobalVariable *GV = M.getGlobalVariable(Name))
    return GV;
  return new GlobalVariable(M, Ty, /*isConstant=*/false,
                            GlobalValue::InternalLinkage,
                            Constant::getNullValue(Ty), Name);
}

/// Decomposes \p Flat (the flat thread index within its group) into thread
/// group dimension \p Component's (0/1/2 for x/y/z) thread-in-group id,
/// scalar counterpart of `feme::cpu::WaveLoweringPass`'s
/// `decomposeComponent`.
Value *decomposeComponent(IRBuilder<> &Builder, Value *Flat, unsigned Component,
                          uint32_t NumThreadsX, uint32_t NumThreadsY) {
  switch (Component) {
  case 0:
    return Builder.CreateURem(Flat, Builder.getInt32(NumThreadsX));
  case 1: {
    Value *DivX = Builder.CreateUDiv(Flat, Builder.getInt32(NumThreadsX));
    return Builder.CreateURem(DivX, Builder.getInt32(NumThreadsY));
  }
  case 2: {
    uint64_t XY = static_cast<uint64_t>(NumThreadsX) * NumThreadsY;
    return Builder.CreateUDiv(
        Flat,
        ConstantInt::get(Builder.getInt32Ty(), static_cast<uint32_t>(XY)));
  }
  default:
    llvm_unreachable("component out of range");
  }
}

/// Lowers one raised builtin call \p CI of kind \p Kind in \p F, using \p
/// FlatPtr/\p GroupIDPtr (the two per-invocation/group globals) and \p
/// NumThreads (from `hlsl.numthreads`).
void lowerCall(CallInst &CI, RaisedBuiltin Kind, Value *FlatPtr,
               Value *GroupIDPtr, const std::array<uint32_t, 3> &NumThreads) {
  IRBuilder<> Builder(&CI);
  Type *I32Ty = Builder.getInt32Ty();
  Value *Flat = Builder.CreateLoad(I32Ty, FlatPtr, "ref.flat");

  Value *Result;
  switch (Kind) {
  case RaisedBuiltin::FlattenedThreadIdInGroup:
    Result = Flat;
    break;
  case RaisedBuiltin::ThreadIdInGroup: {
    unsigned Component = cast<ConstantInt>(CI.getArgOperand(0))->getZExtValue();
    Result = decomposeComponent(Builder, Flat, Component, NumThreads[0],
                                NumThreads[1]);
    break;
  }
  case RaisedBuiltin::ThreadId: {
    unsigned Component = cast<ConstantInt>(CI.getArgOperand(0))->getZExtValue();
    Value *InGroup = decomposeComponent(Builder, Flat, Component, NumThreads[0],
                                        NumThreads[1]);
    Value *GroupIDPtrComponent = Builder.CreateConstGEP2_32(
        ArrayType::get(I32Ty, 3), GroupIDPtr, 0, Component);
    Value *GroupIDComponent =
        Builder.CreateLoad(I32Ty, GroupIDPtrComponent, "ref.group_id");
    Value *Scaled = Builder.CreateMul(GroupIDComponent,
                                      Builder.getInt32(NumThreads[Component]));
    Result = Builder.CreateAdd(Scaled, InGroup);
    break;
  }
  case RaisedBuiltin::GroupId: {
    unsigned Component = cast<ConstantInt>(CI.getArgOperand(0))->getZExtValue();
    Value *Ptr = Builder.CreateConstGEP2_32(ArrayType::get(I32Ty, 3),
                                            GroupIDPtr, 0, Component);
    Result = Builder.CreateLoad(I32Ty, Ptr, "ref.group_id");
    break;
  }
  case RaisedBuiltin::Unsupported:
    llvm_unreachable("Unsupported builtins are diagnosed, never lowered");
  }

  Result->takeName(&CI);
  CI.replaceAllUsesWith(Result);
  CI.eraseFromParent();
}

/// Lowers every raised builtin in \p F, or diagnoses and leaves \p F
/// untouched if it uses one this mode does not support (see the header
/// comment). Returns whether \p F was rewritten.
bool lowerFunction(Function &F) {
  SmallVector<std::pair<CallInst *, RaisedBuiltin>, 8> Calls;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    Function *Callee = CI ? CI->getCalledFunction() : nullptr;
    if (!Callee)
      continue;
    std::optional<RaisedBuiltin> Kind = classify(Callee->getIntrinsicID());
    if (!Kind)
      continue;
    if (*Kind == RaisedBuiltin::Unsupported) {
      F.getContext().emitError(
          "feme-cpu-reference-lower-builtins: function '" + F.getName() +
          "' uses a wave intrinsic ('" + Callee->getName() +
          "'), which has no meaning one invocation at a time "
          "(--reference)");
      return false;
    }
    Calls.emplace_back(CI, *Kind);
  }
  if (Calls.empty()) {
    F.addFnAttr(ReferenceLoweredAttrName);
    return false;
  }

  Module &M = *F.getParent();
  Value *FlatPtr =
      getOrCreateRefGlobal(M, ReferenceThreadIndexInGroupGlobalName,
                           Type::getInt32Ty(M.getContext()));
  Value *GroupIDPtr =
      getOrCreateRefGlobal(M, ReferenceGroupIDGlobalName,
                           ArrayType::get(Type::getInt32Ty(M.getContext()), 3));
  std::array<uint32_t, 3> NumThreads = getThreadGroupSize(F);

  for (auto [CI, Kind] : Calls)
    lowerCall(*CI, Kind, FlatPtr, GroupIDPtr, NumThreads);

  F.addFnAttr(ReferenceLoweredAttrName);
  return true;
}

} // namespace

PreservedAnalyses ReferenceLoweringPass::run(Module &M,
                                             ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() && F.hasFnAttribute("hlsl.shader"))
      Candidates.push_back(&F);

  for (Function *F : Candidates)
    Changed = lowerFunction(*F) || Changed;

  // A raised builtin declaration left behind once its last caller is
  // rewritten away has nothing left to select it.
  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    if (!F.isDeclaration() || !F.use_empty())
      continue;
    std::optional<RaisedBuiltin> Kind = classify(F.getIntrinsicID());
    if (Kind && *Kind != RaisedBuiltin::Unsupported)
      F.eraseFromParent();
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
