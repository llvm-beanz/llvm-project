//===- ReferenceEntryWrapper.cpp - `--reference`'s entry wrapper ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ReferenceEntryWrapper.h"

#include "DispatchArgsLayout.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/ReferenceLowering.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Builds the exported `feme_cpu_entry_<name>` wrapper around \p Body (see
/// the file comment in ReferenceEntryWrapper.h), or nullptr if \p Body was
/// not lowered by `feme::cpu::ReferenceLoweringPass` -- nothing for this
/// pass to wrap in that case (e.g. it used an unsupported wave intrinsic
/// and was left alone, with a diagnostic already emitted).
Function *buildWrapper(Function &Body) {
  if (!Body.hasFnAttribute(ReferenceLoweredAttrName))
    return nullptr;

  Module &M = *Body.getParent();
  LLVMContext &Ctx = M.getContext();
  std::array<uint32_t, 3> NumThreads = getThreadGroupSize(Body);
  uint32_t GroupSizeTotal = NumThreads[0] * NumThreads[1] * NumThreads[2];

  StructType *ArgsTy = getDispatchArgsType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I32x3 = ArrayType::get(I32Ty, 3);

  std::string WrapperName = getEntrySymbolName(Body.getName());
  FunctionType *WrapperTy =
      FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false);
  Function *Wrapper =
      Function::Create(WrapperTy, GlobalValue::ExternalLinkage, WrapperName, M);
  Argument *Args = Wrapper->getArg(0);
  Args->setName("args");

  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Wrapper);
  BasicBlock *HeaderBB =
      BasicBlock::Create(Ctx, "invocation.loop.header", Wrapper);
  BasicBlock *BodyBB = BasicBlock::Create(Ctx, "invocation.loop.body", Wrapper);
  BasicBlock *ExitBB = BasicBlock::Create(Ctx, "invocation.loop.exit", Wrapper);

  IRBuilder<> Entry(EntryBB);
  Value *ResourceHeapVal = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldResourceHeap, PtrTy);
  Value *ResourceHeapCountVal = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldResourceHeapCount, I32Ty);
  Value *SamplerHeapVal = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldSamplerHeap, PtrTy);
  Value *SamplerHeapCountVal = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldSamplerHeapCount, I32Ty);
  Value *RootConstantsVal = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldRootConstants, PtrTy);
  Value *RootConstantSizeVal = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldRootConstantSize, I32Ty);
  Value *ImageHeapVal = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldImageHeap, PtrTy);
  Value *ImageHeapCountVal = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldImageHeapCount, I32Ty);
  Value *GroupIDVec =
      loadArgsField(Entry, ArgsTy, Args, DispatchArgsField::GroupID, I32x3);

  // The group id is invariant across every invocation in this group, so it
  // is stored into `@feme.cpu.ref.group_id` once, outside the loop.
  GlobalVariable *GroupIDGlobal =
      M.getGlobalVariable(ReferenceGroupIDGlobalName, /*AllowInternal=*/true);
  if (GroupIDGlobal)
    Entry.CreateStore(GroupIDVec, GroupIDGlobal);
  GlobalVariable *FlatGlobal = M.getGlobalVariable(
      ReferenceThreadIndexInGroupGlobalName, /*AllowInternal=*/true);
  Entry.CreateBr(HeaderBB);

  IRBuilder<> Header(HeaderBB);
  PHINode *Flat = Header.CreatePHI(I32Ty, 2, "flat");
  Flat->addIncoming(Header.getInt32(0), EntryBB);
  Value *Cond = Header.CreateICmpULT(Flat, Header.getInt32(GroupSizeTotal),
                                     "invocation.cond");
  Header.CreateCondBr(Cond, BodyBB, ExitBB);

  IRBuilder<> BodyIR(BodyBB);
  if (FlatGlobal)
    BodyIR.CreateStore(Flat, FlatGlobal);

  SmallVector<Value *, 6> CallArgs;
  for (const Argument &Arg : Body.args()) {
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
    else if (Arg.getName() == "image_heap")
      CallArgs.push_back(ImageHeapVal);
    else if (Arg.getName() == "image_heap_count")
      CallArgs.push_back(ImageHeapCountVal);
    else
      llvm_unreachable(
          "unexpected parameter for ReferenceEntryWrapperPass: --reference "
          "runs before widening, so a shader body only ever takes the "
          "resource/root-constant/image parameters "
          "feme::cpu::ResourceLoweringPass appends");
  }
  BodyIR.CreateCall(&Body, CallArgs);
  Value *FlatNext = BodyIR.CreateAdd(Flat, BodyIR.getInt32(1), "flat.next");
  BodyIR.CreateBr(HeaderBB);
  Flat->addIncoming(FlatNext, BodyBB);

  IRBuilder<> Exit(ExitBB);
  Exit.CreateRetVoid();

  // The shader body is an implementation detail behind the exported
  // wrapper (see "Kernel ABI"), matching `feme::cpu::EntryWrapperPass`.
  Body.setLinkage(GlobalValue::InternalLinkage);

  return Wrapper;
}

} // namespace

PreservedAnalyses ReferenceEntryWrapperPass::run(Module &M,
                                                 ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() && feme::isShaderEntryPoint(F))
      Candidates.push_back(&F);

  for (Function *F : Candidates)
    if (buildWrapper(*F))
      Changed = true;

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
