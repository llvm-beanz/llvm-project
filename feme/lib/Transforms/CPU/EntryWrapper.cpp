//===- EntryWrapper.cpp - CPU target Phase 6: group execution ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap milestone 4's barrier-free wrapper (see EntryWrapper.h): given a
// widened, wave-lowered function with a `feme::cpu::WaveBodyEnv` (Phase 4's
// interface -- group id, wave index, entry mask, groupshared pointer, plus
// any resource/root-constant parameters `feme::cpu::ResourceLoweringPass`
// appended before it), this pass builds
//
//   void feme_cpu_entry_<name>(const FemeDispatchArgs *Args) {
//     for (w = 0; w < WavesPerGroup; ++w)
//       wave_body(<resource/root-constant fields from *Args>,
//                 Args->GroupID[0..2], w, entry_mask(w), Args->GroupShared);
//   }
//
// per "Phase 6: Group Execution and Barriers" and "Kernel ABI" in
// feme/docs/FeMeCPUDesign.md, where `WavesPerGroup` and `entry_mask` follow
// directly from the entry point's `hlsl.numthreads` dimensions and Phase 4's
// resolved wave size -- both compile-time constants, so `WavesPerGroup`
// needs no runtime computation and `entry_mask(w)` is a small vector
// comparison. Barrier splitting and groupshared allocation (this milestone
// simply forwards `Args->GroupShared` through) are milestone 9.
//
// The emitted `FemeDispatchArgs` field accesses assume an LLVM struct built
// from exactly feme/include/feme/Target/CPU/RuntimeABI.h's field types, in
// order, with ordinary (non-packed) LLVM struct layout -- which matches that
// header's C layout on every target FeMe supports today, the same
// implicit assumption feme::cpu::ResourceInfo's serialized layout and the
// runtime helper library's own mirrored struct make.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/EntryWrapper.h"

#include "DispatchArgsLayout.h"
#include "feme/Transforms/CPU/SIMDize.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

#include <array>
#include <optional>

using namespace llvm;
using namespace feme::cpu;

namespace feme::cpu {

std::string getEntrySymbolName(StringRef EntryName) {
  return ("feme_cpu_entry_" + EntryName).str();
}

} // namespace feme::cpu

namespace {

/// Builds the entry-mask lane comparison for wave \p W (see the file
/// comment above): lane `L` of wave `w` is active iff
/// `w * WaveSize + L < GroupSizeTotal`.
Value *buildEntryMask(IRBuilder<> &Builder, Value *W, unsigned WaveSize,
                      uint32_t GroupSizeTotal) {
  Type *I32Ty = Builder.getInt32Ty();
  Value *Base = Builder.CreateMul(W, ConstantInt::get(I32Ty, WaveSize));
  Value *WideBase = Builder.CreateVectorSplat(WaveSize, Base);
  SmallVector<Constant *, 32> Lanes;
  for (unsigned I = 0; I != WaveSize; ++I)
    Lanes.push_back(ConstantInt::get(I32Ty, I));
  Value *LaneIdx = Builder.CreateAdd(WideBase, ConstantVector::get(Lanes));
  Value *WideLimit = Builder.CreateVectorSplat(
      WaveSize, ConstantInt::get(I32Ty, GroupSizeTotal));
  return Builder.CreateICmpULT(LaneIdx, WideLimit);
}

/// Builds the exported `feme_cpu_entry_<name>` wrapper around \p WaveBody
/// (see the file comment above), or nullptr if \p WaveBody was not widened
/// by `feme::cpu::SIMDizePass` (has no `WaveBodyEnv`) -- nothing for this
/// pass to wrap in that case.
Function *buildWrapper(Function &WaveBody) {
  std::optional<WaveBodyEnv> Env = getWaveBodyEnv(WaveBody);
  if (!Env)
    return nullptr;

  Module &M = *WaveBody.getParent();
  LLVMContext &Ctx = M.getContext();
  unsigned WaveSize =
      cast<FixedVectorType>(Env->EntryMask->getType())->getNumElements();
  std::array<uint32_t, 3> NumThreads = getThreadGroupSize(WaveBody);
  uint32_t GroupSizeTotal = NumThreads[0] * NumThreads[1] * NumThreads[2];
  uint32_t WavesPerGroup =
      GroupSizeTotal == 0 ? 0 : (GroupSizeTotal + WaveSize - 1) / WaveSize;

  StructType *ArgsTy = getDispatchArgsType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I32x3 = ArrayType::get(I32Ty, 3);

  std::string WrapperName = getEntrySymbolName(WaveBody.getName());
  FunctionType *WrapperTy =
      FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false);
  Function *Wrapper =
      Function::Create(WrapperTy, GlobalValue::ExternalLinkage, WrapperName, M);
  Argument *Args = Wrapper->getArg(0);
  Args->setName("args");

  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Wrapper);
  BasicBlock *HeaderBB = BasicBlock::Create(Ctx, "wave.loop.header", Wrapper);
  BasicBlock *BodyBB = BasicBlock::Create(Ctx, "wave.loop.body", Wrapper);
  BasicBlock *ExitBB = BasicBlock::Create(Ctx, "wave.loop.exit", Wrapper);

  IRBuilder<> Entry(EntryBB);
  Value *ResourceHeapVal = loadArgsField(
      Entry, ArgsTy, Args, DispatchArgsField::ResourceHeap, PtrTy);
  Value *ResourceHeapCountVal = loadArgsField(
      Entry, ArgsTy, Args, DispatchArgsField::ResourceHeapCount, I32Ty);
  Value *SamplerHeapVal =
      loadArgsField(Entry, ArgsTy, Args, DispatchArgsField::SamplerHeap, PtrTy);
  Value *SamplerHeapCountVal = loadArgsField(
      Entry, ArgsTy, Args, DispatchArgsField::SamplerHeapCount, I32Ty);
  Value *RootConstantsVal = loadArgsField(
      Entry, ArgsTy, Args, DispatchArgsField::RootConstants, PtrTy);
  Value *RootConstantSizeVal = loadArgsField(
      Entry, ArgsTy, Args, DispatchArgsField::RootConstantSize, I32Ty);
  Value *GroupIDVec =
      loadArgsField(Entry, ArgsTy, Args, DispatchArgsField::GroupID, I32x3);
  Value *GroupIDX = Entry.CreateExtractValue(GroupIDVec, 0);
  Value *GroupIDY = Entry.CreateExtractValue(GroupIDVec, 1);
  Value *GroupIDZ = Entry.CreateExtractValue(GroupIDVec, 2);
  Value *GroupSharedVal =
      loadArgsField(Entry, ArgsTy, Args, DispatchArgsField::GroupShared, PtrTy);
  Entry.CreateBr(HeaderBB);

  IRBuilder<> Header(HeaderBB);
  PHINode *W = Header.CreatePHI(I32Ty, 2, "w");
  W->addIncoming(Header.getInt32(0), EntryBB);
  Value *Cond =
      Header.CreateICmpULT(W, Header.getInt32(WavesPerGroup), "wave.cond");
  Header.CreateCondBr(Cond, BodyBB, ExitBB);

  IRBuilder<> Body(BodyBB);
  Value *Mask = buildEntryMask(Body, W, WaveSize, GroupSizeTotal);

  SmallVector<Value *, 12> CallArgs;
  for (const Argument &Arg : WaveBody.args()) {
    if (Arg.getName() == "resource_heap")
      CallArgs.push_back(ResourceHeapVal);
    else if (Arg.getName() == "resource_heap_count")
      CallArgs.push_back(ResourceHeapCountVal);
    else if (Arg.getName() == "sampler_heap")
      CallArgs.push_back(SamplerHeapVal);
    else if (Arg.getName() == "sampler_heap_count")
      CallArgs.push_back(SamplerHeapCountVal);
    else if (Arg.getName() == "root_constants")
      CallArgs.push_back(RootConstantsVal);
    else if (Arg.getName() == "root_constant_size")
      CallArgs.push_back(RootConstantSizeVal);
    else if (Arg.getName() == "wave_group_id_x")
      CallArgs.push_back(GroupIDX);
    else if (Arg.getName() == "wave_group_id_y")
      CallArgs.push_back(GroupIDY);
    else if (Arg.getName() == "wave_group_id_z")
      CallArgs.push_back(GroupIDZ);
    else if (Arg.getName() == "wave_index")
      CallArgs.push_back(W);
    else if (Arg.getName() == "wave_entry_mask")
      CallArgs.push_back(Mask);
    else if (Arg.getName() == "wave_groupshared")
      CallArgs.push_back(GroupSharedVal);
    else
      llvm_unreachable("unexpected wave-body parameter for EntryWrapperPass");
  }
  Body.CreateCall(&WaveBody, CallArgs);
  Value *WNext = Body.CreateAdd(W, Body.getInt32(1), "w.next");
  Body.CreateBr(HeaderBB);
  W->addIncoming(WNext, BodyBB);

  IRBuilder<> Exit(ExitBB);
  Exit.CreateRetVoid();

  // The wave body is an implementation detail behind the exported wrapper
  // (see "Kernel ABI"): give it internal linkage so later optimization can
  // inline/specialize it freely.
  WaveBody.setLinkage(GlobalValue::InternalLinkage);

  return Wrapper;
}

} // namespace

PreservedAnalyses EntryWrapperPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() && F.hasFnAttribute("hlsl.shader"))
      Candidates.push_back(&F);

  for (Function *F : Candidates)
    if (buildWrapper(*F))
      Changed = true;

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
