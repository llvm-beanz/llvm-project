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

#include <optional>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// A full DXIL cbuffer row is always 16 bytes, regardless of how many
/// components of what width it is split into (see
/// `int_dx_resource_load_cbufferrow_4`'s TableGen comment: "The total size
/// of the return should always be 128 bits").
constexpr uint32_t RowSizeBytes = 16;

/// DXIL's sentinel `RangeSize` for an unbounded array binding
/// (`register(bN[])`, no upper bound). Such a binding has no fixed
/// advertised size to report or bounds-check reads against, so it is never
/// a candidate root-constant binding (see the header comment).
constexpr uint32_t UnboundedRangeSize = ~0u;

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

/// Returns \p Handle's declared binding shape (source register space/base
/// register, declared array length, and one array element's declared byte
/// size) if it is a `dx.CBuffer` handle whose binding this milestone can
/// represent (see the header comment: any finite, statically-known
/// binding), or `std::nullopt` otherwise (a non-`dx.CBuffer` handle, an
/// unbounded range, or a non-constant space/register/range-size -- none of
/// which DXIL ever actually produces for these operands, but this pass
/// still declines rather than assumes).
std::optional<RootConstantAccess> matchRootConstantHandle(CallInst &Handle) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy || HandleTy->getName() != "dx.CBuffer")
    return std::nullopt;

  auto *SpaceC = dyn_cast<ConstantInt>(Handle.getArgOperand(0));
  auto *RegisterC = dyn_cast<ConstantInt>(Handle.getArgOperand(1));
  auto *RangeSizeC = dyn_cast<ConstantInt>(Handle.getArgOperand(2));
  if (!SpaceC || !RegisterC || !RangeSizeC)
    return std::nullopt;

  uint64_t RangeSize = RangeSizeC->getZExtValue();
  if (RangeSize == 0 || RangeSize == UnboundedRangeSize)
    return std::nullopt;

  // The handle type's own array-of-bytes type parameter (e.g. `[32 x i8]`
  // for `target("dx.CBuffer", [32 x i8])`) is one array element's declared
  // byte size.
  auto *ElemArrayTy = dyn_cast<ArrayType>(HandleTy->getTypeParameter(0));
  if (!ElemArrayTy || !ElemArrayTy->getElementType()->isIntegerTy(8))
    return std::nullopt;

  RootConstantAccess Access;
  Access.Handle = &Handle;
  Access.Space = static_cast<uint32_t>(SpaceC->getZExtValue());
  Access.Register = static_cast<uint32_t>(RegisterC->getZExtValue());
  Access.RangeSize = static_cast<uint32_t>(RangeSize);
  Access.ElementSize = static_cast<uint32_t>(ElemArrayTy->getNumElements());
  return Access;
}

/// Collects every `llvm.dx.resource.load.cbufferrow.4.*` call \p Handle is
/// used through, or `std::nullopt` if any use is something else (a
/// different `cbufferrow.N` width, or any other call) -- in which case
/// \p Handle is left entirely alone, for
/// `feme::cpu::checkSupportedRaisedOps` to reject exactly as before this
/// pass existed, rather than partially rewriting it. A row index need not
/// be constant (roadmap R25); it is kept as whatever `Value` the load
/// already uses.
std::optional<SmallVector<RootConstantRowLoad, 4>>
collectRowLoads(CallInst &Handle) {
  SmallVector<RootConstantRowLoad, 4> Loads;
  for (User *U : Handle.users()) {
    auto *CI = dyn_cast<CallInst>(U);
    if (!CI || getIntrinsicID(CI) != Intrinsic::dx_resource_load_cbufferrow_4)
      return std::nullopt;
    Loads.push_back(RootConstantRowLoad{CI, CI->getArgOperand(1)});
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
  // `GlobalObject::copyAttributesFrom()` does not copy function-attached
  // metadata (e.g. `!feme.signature` attached by
  // `feme::graphics::CanonicalizeStagePass`) -- copy it explicitly so
  // stage reflection metadata survives this parameter-injection, exactly
  // like `feme::cpu::ResourceLoweringPass::addResourceEnvParams` and
  // `feme::cpu::addSubpassInputHeapParams` already do for their own
  // trailing-parameter replacements (roadmap H7o: a real
  // `dEQP-VK.pipeline.monolithic.multisample.min_sample_shading_*`
  // fragment shader reading only a push constant -- no bound resource --
  // was the first real case to reach this specific helper with a
  // `feme.stage.*` op needing that metadata back later, surfacing the gap
  // as `feme-cpu-wrap-fragment`'s "requires attached feme.signature
  // metadata" diagnostic).
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  for (auto [Kind, Node] : MDs)
    NewF->setMetadata(Kind, Node);
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
/// constant block size this function reads, `false` (a root-constant-only
/// function uses neither the sampler heap nor any statically-known dynamic
/// heap index), the binding's source register space and base register
/// (roadmap R25: any single binding is recognized now, so a host needs to
/// be told which one this is), a root-constant min offset (always 0: a
/// DXIL root constant's own register-bound view always starts its own span
/// at byte 0 -- see `feme::cpu::ResourceInfo::RootConstantMinOffset`'s own
/// comment, roadmap H6u), and no trailing indices.
void attachRootConstantMetadata(Function &F, uint32_t RootConstantSize,
                                uint32_t Space, uint32_t Register) {
  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  MDNode *Node = MDNode::get(
      Ctx, {MDString::get(Ctx, F.getName()),
            ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstantSize)),
            ConstantAsMetadata::get(ConstantInt::getFalse(Ctx)),
            ConstantAsMetadata::get(ConstantInt::get(I32Ty, Space)),
            ConstantAsMetadata::get(ConstantInt::get(I32Ty, Register)),
            ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0))});
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
  if (!matchRootConstantAccess(F))
    return nullptr;

  Value *RootConstants;
  Value *RootConstantSize;
  Function *NewF = addRootConstantParams(F, RootConstants, RootConstantSize);

  // Re-matched against `NewF` rather than reusing the match against `F`
  // above: a dynamic row or array index (roadmap R25) may be one of `F`'s
  // own arguments, which `addRootConstantParams` has just replaced with
  // `NewF`'s corresponding one -- the match above only existed to decide
  // whether this function has anything to lower at all, cheaply, before
  // committing to rebuilding it.
  std::optional<RootConstantAccess> Access = matchRootConstantAccess(*NewF);
  uint32_t RootConstantSizeNeeded =
      lowerRootConstantAccess(*Access, RootConstants, RootConstantSize);
  attachRootConstantMetadata(*NewF, RootConstantSizeNeeded, Access->Space,
                             Access->Register);
  return NewF;
}

} // namespace

namespace feme::cpu {

std::optional<RootConstantAccess> matchRootConstantAccess(Function &F) {
  if (F.isDeclaration())
    return std::nullopt;

  std::optional<RootConstantAccess> Access;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || getIntrinsicID(CI) != Intrinsic::dx_resource_handlefrombinding)
      continue;
    if (std::optional<RootConstantAccess> Matched =
            matchRootConstantHandle(*CI)) {
      if (Access)
        return std::nullopt; // More than one candidate: reject both.
      Access = std::move(Matched);
    }
  }
  if (!Access)
    return std::nullopt;

  std::optional<SmallVector<RootConstantRowLoad, 4>> Loads =
      collectRowLoads(*Access->Handle);
  if (!Loads)
    return std::nullopt;

  Access->Loads = std::move(*Loads);
  return Access;
}

uint32_t lowerRootConstantAccess(const RootConstantAccess &Access,
                                 Value *RootConstants,
                                 Value *RootConstantSize) {
  LLVMContext &Ctx = Access.Handle->getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I64Ty = Type::getInt64Ty(Ctx);
  // Roadmap R25: the required span is the binding's full advertised size
  // (every byte a host is expected to supply for it), not merely the
  // subset of rows this function's own loads happen to touch statically --
  // which is no longer even knowable once a row or the array index is
  // dynamic (see the file comment above).
  uint32_t RootConstantSizeNeeded = Access.ElementSize * Access.RangeSize;

  // The array index is the same for every load through this one handle
  // (DXIL's `handlefrombinding` binds one specific element); constant-folds
  // away entirely for the common non-array (`RangeSize == 1`, `Index ==
  // 0`) case, leaving the exact code a narrower binding produced before
  // R25.
  Value *Index = Access.Handle->getArgOperand(3);

  for (const RootConstantRowLoad &RL : Access.Loads) {
    // Rewrites `RL` into a bounds-checked load from `RootConstants`: reads
    // zero for any component wholly or partly outside `RootConstantSize`'s
    // declared span, matching "root-constant accesses outside
    // `RootConstantSize` read zero" in feme/docs/FeMeCPUDesign.md's "Root
    // constants" section (the same behaviour a descriptor-backed constant
    // buffer's bounds check gives an out-of-range read). Both an
    // out-of-range row and an out-of-range array index end up caught by
    // the same check: `BaseOffset` grows past `RootConstantSize` either
    // way, since that is the binding's full advertised size.
    auto *RetTy = cast<StructType>(RL.Load->getType());
    unsigned NumComponents = RetTy->getNumElements();
    Type *ElemTy = RetTy->getElementType(0);
    uint32_t ComponentSize = RowSizeBytes / NumComponents;

    IRBuilder<> Builder(RL.Load);
    Value *ElementOffset =
        Builder.CreateMul(Index, ConstantInt::get(I32Ty, Access.ElementSize),
                          "root_const.elem_off");
    Value *RowOffset = Builder.CreateMul(
        RL.Row, ConstantInt::get(I32Ty, RowSizeBytes), "root_const.row_off");
    Value *BaseOffset =
        Builder.CreateAdd(ElementOffset, RowOffset, "root_const.base_off");

    // A `select` between the loaded value and zero is not enough on its
    // own: unlike a descriptor-backed load (whose bounds check guards a
    // call into `libFeMeRuntimeCPU`, not a raw pointer dereference), the
    // load here would still execute unconditionally, reading through
    // `RootConstants` even when the whole block is empty (a null
    // pointer -- see `feme::cpu::DispatchResources::RootConstants`) or too
    // small for this row. A real (uniform -- this bounds check depends
    // only on the dispatch-wide `RootConstantSize` and this access's own
    // operands, never on per-lane data) branch guards the load itself
    // instead.
    Value *RowEnd =
        Builder.CreateAdd(BaseOffset, ConstantInt::get(I32Ty, RowSizeBytes));
    Value *InBounds =
        Builder.CreateICmpUGE(RootConstantSize, RowEnd, "root_const.inbounds");

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
      Value *ComponentOffset = ThenBuilder.CreateAdd(
          BaseOffset, ConstantInt::get(I32Ty, Component * ComponentSize));
      Value *ComponentOffset64 = ThenBuilder.CreateZExt(ComponentOffset, I64Ty);
      Value *Ptr =
          ThenBuilder.CreateInBoundsGEP(ThenBuilder.getInt8Ty(), RootConstants,
                                        ComponentOffset64, "root_const.ptr");
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
