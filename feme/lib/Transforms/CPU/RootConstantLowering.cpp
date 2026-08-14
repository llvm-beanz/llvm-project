//===- RootConstantLowering.cpp - CPU target root constant lowering -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/RootConstantLowering.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <algorithm>
#include <optional>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// The one binding this milestone recognizes -- see the header comment for
/// why `--cpu-root-constants=bN,spaceM` isn't implemented yet.
constexpr uint32_t RootConstantSpace = 0;
constexpr uint32_t RootConstantRegister = 0;

/// A full DXIL cbuffer row is always 16 bytes, regardless of how many
/// components of what width it is split into (see
/// `int_dx_resource_load_cbufferrow_4`'s TableGen comment: "The total size
/// of the return should always be 128 bits").
constexpr uint32_t RowSizeBytes = 16;

/// Returns the intrinsic ID of the call \p V is, or `not_intrinsic`.
Intrinsic::ID getIntrinsicID(const Value *V) {
  const auto *CI = dyn_cast<CallInst>(V);
  const Function *Callee = CI ? CI->getCalledFunction() : nullptr;
  return Callee ? Callee->getIntrinsicID() : Intrinsic::not_intrinsic;
}

/// Whether \p F contains any bindless (`handlefromheap`) resource access --
/// see the header comment for the two different ways such a function's
/// root-constant access ends up lowered.
bool usesResourceHeap(Function &F) {
  for (Instruction &I : instructions(F))
    if (getIntrinsicID(&I) == Intrinsic::dx_resource_handlefromheap)
      return true;
  return false;
}

/// Returns \p Handle if it is a `dx.CBuffer` handle bound at the one
/// recognized binding (`RootConstantSpace`/`RootConstantRegister`, a
/// non-array range), or nullptr otherwise.
CallInst *matchRootConstantHandle(CallInst &Handle) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy || HandleTy->getName() != "dx.CBuffer")
    return nullptr;

  auto *SpaceC = dyn_cast<ConstantInt>(Handle.getArgOperand(0));
  auto *LowerBoundC = dyn_cast<ConstantInt>(Handle.getArgOperand(1));
  auto *RangeSizeC = dyn_cast<ConstantInt>(Handle.getArgOperand(2));
  if (!SpaceC || !LowerBoundC || !RangeSizeC)
    return nullptr;
  if (SpaceC->getZExtValue() != RootConstantSpace ||
      LowerBoundC->getZExtValue() != RootConstantRegister ||
      RangeSizeC->getZExtValue() != 1)
    return nullptr;
  return &Handle;
}

/// Collects every `llvm.dx.resource.load.cbufferrow.4.*` call \p Handle is
/// used through, or `std::nullopt` if any use is something else (a
/// non-constant row index, a different `cbufferrow.N` width, or any other
/// call) -- in which case \p Handle is left entirely alone, for
/// `feme::cpu::checkSupportedRaisedOps` to reject exactly as before this
/// pass existed, rather than partially rewriting it.
std::optional<SmallVector<RootConstantRowLoad, 4>>
collectRowLoads(CallInst &Handle) {
  SmallVector<RootConstantRowLoad, 4> Loads;
  for (User *U : Handle.users()) {
    auto *CI = dyn_cast<CallInst>(U);
    if (!CI || getIntrinsicID(CI) != Intrinsic::dx_resource_load_cbufferrow_4)
      return std::nullopt;
    auto *RowC = dyn_cast<ConstantInt>(CI->getArgOperand(1));
    if (!RowC)
      return std::nullopt;
    Loads.push_back(RootConstantRowLoad{CI, RowC->getZExtValue()});
  }
  return Loads;
}

/// Builds \p F's replacement: the same function with `root_constants`
/// (`ptr`) and `root_constant_size` (`i32`) appended, mirroring
/// `feme::cpu::ResourceCallEnv`'s fields of the same name so
/// `feme::cpu::EntryWrapperPass`'s existing by-name argument wiring (see
/// EntryWrapper.cpp) needs no changes to recognize them. The body is moved
/// across (not cloned), exactly as `feme::cpu::ResourceLoweringPass`'s own
/// `addResourceEnvParams` does for its differently-shaped parameter list.
/// Only called for a function `matchRootConstantAccess` accepted that has
/// no bindless resource access of its own (see the header comment) --
/// `feme::cpu::ResourceLoweringPass` reuses its own, already-added
/// parameters of the same name for the other case instead of calling this.
Function *addRootConstantParams(Function &F, Value *&RootConstants,
                                Value *&RootConstantSize) {
  LLVMContext &Ctx = F.getContext();
  SmallVector<Type *, 2> ParamTypes(F.getFunctionType()->params());
  ParamTypes.push_back(PointerType::get(Ctx, 0));
  ParamTypes.push_back(Type::getInt32Ty(Ctx));

  FunctionType *NewTy = FunctionType::get(F.getReturnType(), ParamTypes,
                                          F.getFunctionType()->isVarArg());
  Function *NewF = Function::Create(NewTy, F.getLinkage(), F.getAddressSpace(),
                                    "", F.getParent());
  NewF->copyAttributesFrom(&F);
  NewF->setComdat(F.getComdat());
  NewF->splice(NewF->begin(), &F);

  for (auto [OldArg, NewArg] : llvm::zip(F.args(), NewF->args())) {
    NewArg.takeName(&OldArg);
    OldArg.replaceAllUsesWith(&NewArg);
  }

  auto ArgIt = NewF->arg_begin() + F.arg_size();
  RootConstants = &*ArgIt++;
  RootConstants->setName("root_constants");
  RootConstantSize = &*ArgIt++;
  RootConstantSize->setName("root_constant_size");

  NewF->takeName(&F);
  F.replaceAllUsesWith(NewF);
  F.eraseFromParent();
  return NewF;
}

/// Attaches the same `!feme.cpu.resources` heap-usage metadata node
/// "Resource usage discovery" describes for \p F (see `attachResourceMetadata`
/// in ResourceLowering.cpp for the shape this mirrors): its name, the root
/// constant block size this function reads, and `false`/no trailing indices
/// (a root-constant-only function uses neither the sampler heap nor any
/// statically-known dynamic heap index).
void attachRootConstantMetadata(Function &F, uint32_t RootConstantSize) {
  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  MDNode *Node = MDNode::get(
      Ctx, {MDString::get(Ctx, F.getName()),
            ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstantSize)),
            ConstantAsMetadata::get(ConstantInt::getFalse(Ctx))});
  F.getParent()
      ->getOrInsertNamedMetadata("feme.cpu.resources")
      ->addOperand(Node);
}

/// Lowers \p F's root-constant access, if any and if \p F has no bindless
/// resource access of its own (see the header comment for the other case,
/// handled by `feme::cpu::ResourceLoweringPass` instead), returning the
/// rewritten function (a new one, since its signature grows), or nullptr
/// otherwise.
Function *lowerFunctionRootConstants(Function &F) {
  if (F.isDeclaration() || usesResourceHeap(F))
    return nullptr;

  std::optional<RootConstantAccess> Access = matchRootConstantAccess(F);
  if (!Access)
    return nullptr;

  Value *RootConstants;
  Value *RootConstantSize;
  Function *NewF = addRootConstantParams(F, RootConstants, RootConstantSize);

  uint32_t RootConstantSizeNeeded =
      lowerRootConstantAccess(*Access, RootConstants, RootConstantSize);
  attachRootConstantMetadata(*NewF, RootConstantSizeNeeded);
  return NewF;
}

} // namespace

namespace feme::cpu {

std::optional<RootConstantAccess> matchRootConstantAccess(Function &F) {
  if (F.isDeclaration())
    return std::nullopt;

  CallInst *Handle = nullptr;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || getIntrinsicID(CI) != Intrinsic::dx_resource_handlefrombinding)
      continue;
    if (CallInst *Matched = matchRootConstantHandle(*CI)) {
      if (Handle)
        return std::nullopt; // More than one candidate: reject both.
      Handle = Matched;
    }
  }
  if (!Handle)
    return std::nullopt;

  std::optional<SmallVector<RootConstantRowLoad, 4>> Loads =
      collectRowLoads(*Handle);
  if (!Loads)
    return std::nullopt;

  return RootConstantAccess{Handle, std::move(*Loads)};
}

uint32_t lowerRootConstantAccess(const RootConstantAccess &Access,
                                 Value *RootConstants,
                                 Value *RootConstantSize) {
  uint32_t RootConstantSizeNeeded = 0;
  for (const RootConstantRowLoad &RL : Access.Loads) {
    // Rewrites `RL` into a bounds-checked load from `RootConstants`: reads
    // zero for any component wholly or partly outside `RootConstantSize`'s
    // declared span, matching "root-constant accesses outside
    // `RootConstantSize` read zero" in feme/docs/FeMeCPUDesign.md's "Root
    // constants" section (the same behaviour a descriptor-backed constant
    // buffer's bounds check gives an out-of-range read).
    auto *RetTy = cast<StructType>(RL.Load->getType());
    unsigned NumComponents = RetTy->getNumElements();
    Type *ElemTy = RetTy->getElementType(0);
    uint32_t ComponentSize = RowSizeBytes / NumComponents;
    uint64_t RowByteOffset = RL.Row * RowSizeBytes;
    RootConstantSizeNeeded =
        std::max(RootConstantSizeNeeded,
                 static_cast<uint32_t>(RowByteOffset) + RowSizeBytes);

    // A `select` between the loaded value and zero is not enough on its
    // own: unlike a descriptor-backed load (whose bounds check guards a
    // call into `libFeMeRuntimeCPU`, not a raw pointer dereference), the
    // load here would still execute unconditionally, reading through
    // `RootConstants` even when the whole block is empty (a null
    // pointer -- see `feme::cpu::DispatchResources::RootConstants`) or too
    // small for this row. A real (uniform -- this bounds check depends
    // only on the dispatch-wide `RootConstantSize`, never on per-lane
    // data) branch guards the load itself instead.
    IRBuilder<> Builder(RL.Load);
    Value *InBounds = Builder.CreateICmpUGE(
        RootConstantSize,
        Builder.getInt32(static_cast<uint32_t>(RowByteOffset) + RowSizeBytes),
        "root_const.inbounds");

    Instruction *ThenTerm = nullptr;
    Instruction *ElseTerm = nullptr;
    SplitBlockAndInsertIfThenElse(InBounds, RL.Load, &ThenTerm, &ElseTerm);

    IRBuilder<> ThenBuilder(ThenTerm);
    IRBuilder<> MergeBuilder(RL.Load); // The original block is now the merge.
    // Every `phi` in a block must be grouped at its top, so all `NumComponents`
    // of them are created first, in one pass, before the `insertvalue` chain
    // assembling `Result` (itself ordinary, non-`phi` instructions) below.
    SmallVector<PHINode *, 4> Merged(NumComponents);
    for (unsigned Component = 0; Component != NumComponents; ++Component) {
      uint64_t ByteOffset = RowByteOffset + Component * ComponentSize;
      Value *Ptr = ThenBuilder.CreateConstInBoundsGEP1_64(
          ThenBuilder.getInt8Ty(), RootConstants, ByteOffset, "root_const.ptr");
      Value *Loaded = ThenBuilder.CreateAlignedLoad(ElemTy, Ptr, Align(4),
                                                    "root_const.load");
      Merged[Component] = MergeBuilder.CreatePHI(ElemTy, 2, "root_const.value");
      Merged[Component]->addIncoming(Loaded, ThenTerm->getParent());
      Merged[Component]->addIncoming(Constant::getNullValue(ElemTy),
                                     ElseTerm->getParent());
    }

    Value *Result = PoisonValue::get(RetTy);
    for (unsigned Component = 0; Component != NumComponents; ++Component)
      Result = MergeBuilder.CreateInsertValue(Result, Merged[Component],
                                              {Component});

    RL.Load->replaceAllUsesWith(Result);
    RL.Load->eraseFromParent();
  }

  Access.Handle->eraseFromParent();
  return RootConstantSizeNeeded;
}

PreservedAnalyses RootConstantLoweringPass::run(Module &M,
                                                ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (lowerFunctionRootConstants(F))
      Changed = true;

  // An unused `handlefrombinding` declaration is left behind once its last
  // caller is rewritten away; a candidate this pass declined to rewrite
  // (or left for `feme::cpu::ResourceLoweringPass`, see the header
  // comment) may still have users, and is left alone.
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (F.isDeclaration() && F.use_empty() &&
        F.getIntrinsicID() == Intrinsic::dx_resource_handlefrombinding)
      F.eraseFromParent();

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace feme::cpu
