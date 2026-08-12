//===- BuiltinCalls.cpp - `feme.cpu.builtin.*` call helpers --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/BuiltinCalls.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// The name each `BuiltinCallKind` mangles to, before the wave-size suffix.
StringRef kindName(BuiltinCallKind Kind) {
  switch (Kind) {
  case BuiltinCallKind::ThreadId:
    return "feme.cpu.builtin.thread_id";
  case BuiltinCallKind::ThreadIdInGroup:
    return "feme.cpu.builtin.thread_id_in_group";
  case BuiltinCallKind::FlattenedThreadIdInGroup:
    return "feme.cpu.builtin.flattened_thread_id_in_group";
  case BuiltinCallKind::LaneIndex:
    return "feme.cpu.builtin.lane_index";
  }
  llvm_unreachable("unknown BuiltinCallKind");
}

std::optional<BuiltinCallKind> parseKindName(StringRef Name) {
  if (Name == "feme.cpu.builtin.thread_id")
    return BuiltinCallKind::ThreadId;
  if (Name == "feme.cpu.builtin.thread_id_in_group")
    return BuiltinCallKind::ThreadIdInGroup;
  if (Name == "feme.cpu.builtin.flattened_thread_id_in_group")
    return BuiltinCallKind::FlattenedThreadIdInGroup;
  if (Name == "feme.cpu.builtin.lane_index")
    return BuiltinCallKind::LaneIndex;
  return std::nullopt;
}

/// Each kind's operand list, before the `.vW` mangled suffix: see the
/// header comment's table -- `ThreadId` needs the group id and thread group
/// dimensions to compute a dispatch-wide index; `ThreadIdInGroup` needs only
/// the dimensions; the other two need neither.
bool needsGroupID(BuiltinCallKind Kind) {
  return Kind == BuiltinCallKind::ThreadId;
}
bool needsNumThreads(BuiltinCallKind Kind) {
  return Kind == BuiltinCallKind::ThreadId ||
        Kind == BuiltinCallKind::ThreadIdInGroup;
}
bool needsComponent(BuiltinCallKind Kind) {
  return Kind == BuiltinCallKind::ThreadId ||
        Kind == BuiltinCallKind::ThreadIdInGroup;
}
bool needsWaveIndex(BuiltinCallKind Kind) {
  return Kind != BuiltinCallKind::LaneIndex;
}

} // namespace

namespace feme::cpu {

CallInst *createBuiltinCall(IRBuilderBase &Builder, BuiltinCallKind Kind,
                            const BuiltinCallEnv &Env, unsigned WaveSize,
                            uint32_t NumThreadsX, uint32_t NumThreadsY,
                            uint32_t NumThreadsZ, unsigned Component,
                            const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  LLVMContext &Ctx = M->getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *ResultTy = FixedVectorType::get(I32Ty, WaveSize);

  SmallVector<Type *, 8> ParamTypes;
  SmallVector<Value *, 8> Args;
  if (needsGroupID(Kind)) {
    ParamTypes.append({I32Ty, I32Ty, I32Ty});
    Args.append({Env.GroupIDX, Env.GroupIDY, Env.GroupIDZ});
  }
  if (needsWaveIndex(Kind)) {
    ParamTypes.push_back(I32Ty);
    Args.push_back(Env.WaveIndex);
  }
  if (needsNumThreads(Kind)) {
    ParamTypes.append({I32Ty, I32Ty, I32Ty});
    Args.append({Builder.getInt32(NumThreadsX), Builder.getInt32(NumThreadsY),
                Builder.getInt32(NumThreadsZ)});
  }
  if (needsComponent(Kind)) {
    ParamTypes.push_back(I32Ty);
    Args.push_back(Builder.getInt32(Component));
  }

  SmallString<48> MangledName;
  raw_svector_ostream OS(MangledName);
  OS << kindName(Kind) << ".v" << WaveSize;

  Function *Callee = M->getFunction(MangledName);
  if (!Callee) {
    FunctionType *FTy = FunctionType::get(ResultTy, ParamTypes, false);
    Callee = Function::Create(FTy, GlobalValue::ExternalLinkage, MangledName,
                              M);
    Callee->setDoesNotAccessMemory();
    Callee->setWillReturn();
  }
  return Builder.CreateCall(Callee, Args, Name);
}

std::optional<MatchedBuiltinCall> matchBuiltinCall(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee)
    return std::nullopt;

  StringRef Name = Callee->getName();
  auto [Base, WaveSizeStr] = Name.rsplit(".v");
  unsigned WaveSize = 0;
  if (WaveSizeStr.empty() || WaveSizeStr.getAsInteger(10, WaveSize))
    return std::nullopt;

  std::optional<BuiltinCallKind> Kind = parseKindName(Base);
  if (!Kind)
    return std::nullopt;

  MatchedBuiltinCall Result;
  Result.Kind = *Kind;
  Result.Call = const_cast<CallInst *>(&CI);
  Result.WaveSize = WaveSize;

  unsigned OperandIdx = 0;
  if (needsGroupID(*Kind)) {
    Result.Env.GroupIDX = CI.getArgOperand(OperandIdx++);
    Result.Env.GroupIDY = CI.getArgOperand(OperandIdx++);
    Result.Env.GroupIDZ = CI.getArgOperand(OperandIdx++);
  }
  if (needsWaveIndex(*Kind))
    Result.Env.WaveIndex = CI.getArgOperand(OperandIdx++);
  if (needsNumThreads(*Kind)) {
    auto GetConst = [&](unsigned Idx) -> uint32_t {
      auto *C = dyn_cast<ConstantInt>(CI.getArgOperand(Idx));
      return C ? static_cast<uint32_t>(C->getZExtValue()) : 0;
    };
    Result.NumThreadsX = GetConst(OperandIdx++);
    Result.NumThreadsY = GetConst(OperandIdx++);
    Result.NumThreadsZ = GetConst(OperandIdx++);
  }
  if (needsComponent(*Kind)) {
    auto *C = dyn_cast<ConstantInt>(CI.getArgOperand(OperandIdx++));
    Result.Component = C ? static_cast<unsigned>(C->getZExtValue()) : 0;
  }
  return Result;
}

} // namespace feme::cpu
