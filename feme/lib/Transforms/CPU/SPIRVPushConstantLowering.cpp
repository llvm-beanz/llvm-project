//===- SPIRVPushConstantLowering.cpp - SPIR-V push constant lowering ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SPIRVPushConstantLowering.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <limits>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// The LLVM address space LLVM's own SPIR-V backend uses for the
/// PushConstant storage class (see `storageClassToAddressSpace` in
/// `llvm/lib/Target/SPIRV/SPIRVUtils.h`), and the one
/// `feme::spirv::PushConstantGlobalVariablePattern` converts a push
/// constant `spirv.GlobalVariable` into.
constexpr unsigned PushConstantAddressSpace = 13;

/// Returns the module's push-constant global, or nullptr if it has none.
/// SPIR-V permits at most one, so the first one found is the only one.
GlobalVariable *findPushConstantGlobal(Module &M) {
  for (GlobalVariable &GV : M.globals())
    if (GV.getAddressSpace() == PushConstantAddressSpace)
      return &GV;
  return nullptr;
}

/// Whether \p F already has the `root_constants`/`root_constant_size`
/// parameter pair `feme::cpu::SPIRVResourceLoweringPass`'s own
/// `addResourceEnvParams` unconditionally appends to every function it
/// touches -- if so, that pass has already (or will already, since it runs
/// before this one in the pipeline) handled this function's push-constant
/// access itself, reusing those parameters, and this pass has nothing to
/// do (see the header comment's "combined case").
bool hasRootConstantParams(const Function &F) {
  return llvm::any_of(F.args(), [](const Argument &A) {
    return A.getName() == "root_constants";
  });
}

/// Walks every user of \p Base -- `Access.Global` itself, at the top-level
/// call from `matchSPIRVPushConstantAccess`, or a GEP already resolved
/// relative to it on a deeper recursive call -- recognizing a load (pushed
/// onto \p Loads) or a further GEP (recursed into) and rejecting (returning
/// false) anything else, exactly per the file comment's scope note. \p
/// BaseOffset/\p DynamicIndex/\p DynamicStride describe \p Base's own
/// address relative to `Access.Global`: a compile-time-constant offset,
/// plus -- if a dynamic index was already found on the way from the global
/// down to \p Base -- that index's `Value` and byte stride (`nullptr`/`0`
/// if every step so far has been compile-time constant).
///
/// A plain `Instruction` user is only visited once, from whichever call
/// reaches it walking that same function's own users (`UI->getFunction()
/// != &F` skips any other function's own use of a *shared* node -- see
/// below); a `ConstantExpr` user, uniqued module-wide, is walked from every
/// call that reaches it, since two different push-constant-consuming
/// functions can share the exact same `ConstantExpr` instance (this is
/// exactly why a per-load function filter is still needed one level below
/// a `ConstantExpr`, even though a top-level `Instruction` walk needs it
/// only once, at its own root).
bool visitPushConstantUsers(Value *Base, uint64_t BaseOffset,
                            Value *DynamicIndex, uint64_t DynamicStride,
                            Function &F, const DataLayout &DL,
                            SmallVectorImpl<SPIRVPushConstantLoad> &Loads) {
  for (User *U : Base->users()) {
    if (auto *UI = dyn_cast<Instruction>(U); UI && UI->getFunction() != &F)
      continue; // Not this function's own use; visited when that one is.

    if (auto *LI = dyn_cast<LoadInst>(U)) {
      Loads.push_back({LI, BaseOffset, DynamicIndex, DynamicStride});
      continue;
    }

    // Anything else must be a further GEP (a store, or a GEP result used
    // as anything but a load or a further GEP, is unsupported) -- see the
    // file comment. This also covers a constant-index GEP folded straight
    // to a `ConstantExpr` rather than left as a real `GetElementPtrInst`
    // (the shape the global's own address and every index being already
    // compile-time constant collapses to by default, under every ordinary
    // constant-folding IR builder, including the one `feme-translate`'s
    // `--llvmdialect-to-llvmir` step uses): both are a `GEPOperator`,
    // handled identically here.
    auto *GEP = dyn_cast<GEPOperator>(U);
    if (!GEP)
      return false;

    // Decompose this GEP's own indices into a compile-time-constant byte
    // offset plus, if at most one of its own indices is not a compile-time
    // constant, that index's own `Value` and byte stride --
    // `GEPOperator::collectOffset` itself rejects (returns false) a
    // dynamic *struct-member* index (GLSL/SPIR-V never generates one) or a
    // dynamic index into a scalable-vector type (never applicable to a
    // push-constant block).
    unsigned BitWidth = DL.getIndexSizeInBits(GEP->getPointerAddressSpace());
    SmallMapVector<Value *, APInt, 4> VariableOffsets;
    APInt ConstantOffset(BitWidth, 0);
    if (!GEP->collectOffset(DL, BitWidth, VariableOffsets, ConstantOffset))
      return false;
    if (VariableOffsets.size() + (DynamicIndex ? 1 : 0) > 1)
      return false; // A second, independent dynamic term: out of scope.

    uint64_t NewBaseOffset = BaseOffset + ConstantOffset.getZExtValue();
    Value *NewDynamicIndex = DynamicIndex;
    uint64_t NewDynamicStride = DynamicStride;
    if (!VariableOffsets.empty()) {
      auto &[Index, Stride] = *VariableOffsets.begin();
      NewDynamicIndex = Index;
      NewDynamicStride = Stride.getZExtValue();
    }
    if (!visitPushConstantUsers(GEP, NewBaseOffset, NewDynamicIndex,
                                NewDynamicStride, F, DL, Loads))
      return false;
  }
  return true;
}

/// Returns the end byte (exclusive) of whichever member of \p PC's own
/// declared type contains \p BaseOffset -- the tightest sound `MaxOffset`
/// contribution a dynamically-indexed access can make (see
/// `lowerSPIRVPushConstantAccess`'s header comment): the dynamic index's
/// own runtime range is not known statically, but it can never carry a
/// load past the end of the single declared array/vector/matrix member
/// \p BaseOffset itself already pins down (every access this pass
/// recognizes reaches its dynamic index through exactly one member, per
/// the file comment's "one independent dynamic term" scope). Falls back to
/// \p PC's own full declared-type store size if \p PC is not (as it always
/// is for every real SPIR-V push-constant block, but a unit test may use a
/// bare scalar or array global directly) a `StructType`.
uint64_t dynamicAccessMemberEndByte(GlobalVariable *PC, uint64_t BaseOffset,
                                   const DataLayout &DL) {
  Type *ValueTy = PC->getValueType();
  if (auto *STy = dyn_cast<StructType>(ValueTy)) {
    const StructLayout *SL = DL.getStructLayout(STy);
    if (BaseOffset < SL->getSizeInBytes()) {
      unsigned ElementIdx = SL->getElementContainingOffset(BaseOffset);
      return SL->getElementOffset(ElementIdx) +
             DL.getTypeStoreSize(STy->getElementType(ElementIdx))
                 .getFixedValue();
    }
  }
  return DL.getTypeStoreSize(ValueTy).getFixedValue();
}

/// Erases every now-unused `getelementptr` reachable, through any chain of
/// further GEPs, from a user of \p V -- post-order, so an outer GEP whose
/// own base is another GEP is only erased once it is itself already empty.
/// A `ConstantExpr` GEP is walked (its own users may still need cleaning
/// up) but never erased -- unlike a `GetElementPtrInst`, it is not owned by
/// any one function to begin with, and LLVM's own constant uniquing
/// reclaims it once truly unreferenced.
void eraseDeadPushConstantGEPs(Value *V) {
  for (User *U : llvm::make_early_inc_range(V->users())) {
    auto *GEP = dyn_cast<GEPOperator>(U);
    if (!GEP)
      continue;
    eraseDeadPushConstantGEPs(GEP);
    if (auto *GEPInst = dyn_cast<GetElementPtrInst>(GEP);
        GEPInst && GEPInst->use_empty())
      GEPInst->eraseFromParent();
  }
}

} // namespace

namespace feme::cpu {

std::optional<SPIRVPushConstantAccess>
matchSPIRVPushConstantAccess(Function &F) {
  if (F.isDeclaration())
    return std::nullopt;

  GlobalVariable *PC = findPushConstantGlobal(*F.getParent());
  if (!PC)
    return std::nullopt;

  SPIRVPushConstantAccess Access;
  Access.Global = PC;
  const DataLayout &DL = PC->getDataLayout();
  if (!visitPushConstantUsers(PC, /*BaseOffset=*/0, /*DynamicIndex=*/nullptr,
                             /*DynamicStride=*/0, F, DL, Access.Loads))
    return std::nullopt;

  if (Access.Loads.empty())
    return std::nullopt; // The global exists, but not referenced by `F`.
  return Access;
}

PushConstantAccessSpan
lowerSPIRVPushConstantAccess(const SPIRVPushConstantAccess &Access,
                             Value *RootConstants, Value *RootConstantSize) {
  LLVMContext &Ctx = RootConstants->getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I64Ty = Type::getInt64Ty(Ctx);
  const DataLayout &DL = Access.Global->getDataLayout();

  // (Roadmap L10) The tightest root-constant span this access genuinely
  // needs: the highest byte any recognized load actually reads, not
  // `Access.Global`'s own declared-type `DataLayout` store size. Those two
  // differ whenever the CPU target's own struct layout pads the block's
  // tail wider than any real access reaches -- e.g. a trailing `int3`/
  // `float3` member's vector alignment rounds up to the next power of two
  // of its store size (12 -> 16) on a target whose data layout does not
  // mark vectors as element-aligned, inflating the *whole struct's* size
  // well past the last byte any `int3`/`float3` load actually touches.
  // Every access this pass recognizes has a compile-time-constant *base*
  // byte offset (see the file comment's scope note), so a constant-offset
  // load's own tighter span is always safe to report; a dynamically-
  // indexed load instead contributes its own containing member's end byte
  // (see `dynamicAccessMemberEndByte`) -- unlike
  // `feme::cpu::RootConstantLowering.h`'s own DXIL root constant, which
  // must report its full declared size unconditionally because *every*
  // access it lowers is dynamically indexed, there is no dynamic access
  // here whose containing member's own declared bound this tighter span
  // could ever fail to cover.
  //
  // (Roadmap H6u) `MinAccessedByte` is the same idea applied to the low
  // end: the lowest byte any recognized load actually reads, nonzero
  // whenever this block's own reflected struct declares a nonzero leading
  // `layout(offset=N)` gap (e.g. one shader stage's own share of a larger
  // block several stages share, each reading only its own portion).
  // `Access.Loads` is guaranteed nonempty (see `matchSPIRVPushConstantAccess`),
  // so the loop below always runs at least once and this initial sentinel
  // is always overwritten.
  uint32_t MaxAccessedByte = 0;
  uint32_t MinAccessedByte = std::numeric_limits<uint32_t>::max();

  for (const SPIRVPushConstantLoad &LoadInfo : Access.Loads) {
    LoadInst *Load = LoadInfo.Load;
    Type *LoadedTy = Load->getType();
    uint64_t LoadSize = DL.getTypeStoreSize(LoadedTy).getFixedValue();

    MinAccessedByte =
        std::min<uint64_t>(MinAccessedByte, LoadInfo.BaseOffset);
    MaxAccessedByte = std::max<uint64_t>(
        MaxAccessedByte,
        LoadInfo.DynamicIndex
            ? dynamicAccessMemberEndByte(Access.Global, LoadInfo.BaseOffset, DL)
            : LoadInfo.BaseOffset + LoadSize);

    IRBuilder<> Builder(Load);

    // The byte offset this particular load reads, as a 64-bit value ready
    // for the `getelementptr` below -- the constant `BaseOffset` alone,
    // widened, if this load's index chain never carried a dynamic term, or
    // that same `BaseOffset` plus `DynamicIndex * DynamicStride`
    // (replicating the very GEP arithmetic `matchSPIRVPushConstantAccess`
    // decomposed) otherwise. `CreateSExtOrTrunc`, not a zero-extend,
    // matches `getelementptr`'s own indexing semantics -- every index
    // `GEPOperator::collectOffset` itself scales is treated as signed (see
    // its own `sextOrTrunc` in `llvm/lib/IR/Operator.cpp`).
    Value *ByteOffset64 = ConstantInt::get(I64Ty, LoadInfo.BaseOffset);
    Value *InBoundsEndByte32 =
        ConstantInt::get(I32Ty, LoadInfo.BaseOffset + LoadSize);
    if (LoadInfo.DynamicIndex) {
      Value *Index64 =
          Builder.CreateSExtOrTrunc(LoadInfo.DynamicIndex, I64Ty);
      Value *DynamicByteOffset64 = Builder.CreateMul(
          Index64, ConstantInt::get(I64Ty, LoadInfo.DynamicStride),
          "push_const.dyn_offset");
      ByteOffset64 = Builder.CreateAdd(DynamicByteOffset64, ByteOffset64,
                                       "push_const.byte_offset");
      Value *ByteOffset32 = Builder.CreateTrunc(ByteOffset64, I32Ty);
      InBoundsEndByte32 = Builder.CreateAdd(
          ByteOffset32, ConstantInt::get(I32Ty, LoadSize),
          "push_const.end_offset");
    }
    Value *InBounds = Builder.CreateICmpULE(InBoundsEndByte32, RootConstantSize,
                                            "push_const.inbounds");

    Instruction *ThenTerm = nullptr;
    Instruction *ElseTerm = nullptr;
    SplitBlockAndInsertIfThenElse(InBounds, Load, &ThenTerm, &ElseTerm);

    IRBuilder<> ThenBuilder(ThenTerm);
    Value *ElemPtr = ThenBuilder.CreateInBoundsGEP(
        ThenBuilder.getInt8Ty(), RootConstants, ByteOffset64,
        "push_const.ptr");
    Value *Loaded = ThenBuilder.CreateAlignedLoad(
        LoadedTy, ElemPtr, Load->getAlign(), "push_const.load");

    IRBuilder<> MergeBuilder(Load); // The original block is now the merge.
    PHINode *Merged = MergeBuilder.CreatePHI(LoadedTy, 2, "push_const.value");
    Merged->addIncoming(Loaded, ThenTerm->getParent());
    Merged->addIncoming(Constant::getNullValue(LoadedTy), ElseTerm->getParent());

    Load->replaceAllUsesWith(Merged);
    Load->eraseFromParent();
  }

  // Drop every now-unused `getelementptr` this access rewrote through; the
  // global itself is left for the caller to erase once every function
  // referencing it has been processed (see the pass's own `run`).
  eraseDeadPushConstantGEPs(Access.Global);

  return PushConstantAccessSpan{MinAccessedByte, MaxAccessedByte};
}

PreservedAnalyses SPIRVPushConstantLoweringPass::run(Module &M,
                                                     ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    if (F.isDeclaration() || hasRootConstantParams(F))
      continue;
    std::optional<SPIRVPushConstantAccess> Access =
        matchSPIRVPushConstantAccess(F);
    if (!Access)
      continue;

    LLVMContext &Ctx = F.getContext();
    SmallVector<Type *, 2> ParamTypes(F.getFunctionType()->params());
    ParamTypes.push_back(PointerType::get(Ctx, 0));
    ParamTypes.push_back(Type::getInt32Ty(Ctx));
    FunctionType *NewTy = FunctionType::get(F.getReturnType(), ParamTypes,
                                            F.getFunctionType()->isVarArg());
    Function *NewF = Function::Create(NewTy, F.getLinkage(),
                                      F.getAddressSpace(), "", F.getParent());
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
    // is the first real case to reach this exact helper with a
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
      // A recognized access's own `DynamicIndex` (see
      // `matchSPIRVPushConstantAccess`, run against the original `F`
      // before this rebuild) may itself be one of `F`'s own arguments --
      // e.g. a shader-stage entry point that takes its own dynamic index
      // as a real parameter rather than computing it from an earlier
      // instruction. Unlike an instruction (relocated, not cloned, by
      // `splice` above, so its own identity as a `Value` survives), an
      // `Argument` belongs to its `Function`, not to any basic block, so
      // it is not moved by `splice` at all -- it is only replaced,
      // instruction-use by instruction-use, by the loop below. Without
      // this remap, `Access.Loads`' own `DynamicIndex` would still point
      // at `OldArg` once `F.eraseFromParent()` below frees it, a
      // dangling-pointer read in `lowerSPIRVPushConstantAccess`.
      for (SPIRVPushConstantLoad &LoadInfo : Access->Loads)
        if (LoadInfo.DynamicIndex == &OldArg)
          LoadInfo.DynamicIndex = &NewArg;
      OldArg.replaceAllUsesWith(&NewArg);
    }
    auto ArgIt = NewF->arg_begin() + F.arg_size();
    Value *RootConstants = &*ArgIt++;
    RootConstants->setName("root_constants");
    Value *RootConstantSize = &*ArgIt++;
    RootConstantSize->setName("root_constant_size");
    NewF->takeName(&F);
    F.replaceAllUsesWith(NewF);
    F.eraseFromParent();

    // Re-matched against `NewF`: `addRootConstantParams`'s equivalent above
    // rebuilds the function, and the loads `Access` collected belong to the
    // original `F`'s instructions -- which `splice` moved into `NewF`
    // unchanged (a `BasicBlock::splice` relocates instructions in place, it
    // does not clone them), so `Access.Loads`' pointers stay valid; a
    // `DynamicIndex` that was one of `F`'s own arguments was already
    // remapped to its `NewF` counterpart just above, for the same reason;
    // `Access.Global` needs nothing further, since it is a module-level
    // `GlobalVariable`, not per-function.
    PushConstantAccessSpan Span =
        lowerSPIRVPushConstantAccess(*Access, RootConstants, RootConstantSize);

    Type *I32Ty = Type::getInt32Ty(Ctx);
    MDNode *Node = MDNode::get(
        Ctx,
        {MDString::get(Ctx, NewF->getName()),
         ConstantAsMetadata::get(ConstantInt::get(I32Ty, Span.MaxOffset)),
         ConstantAsMetadata::get(ConstantInt::getFalse(Ctx)),
         ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0)),
         ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0)),
         ConstantAsMetadata::get(ConstantInt::get(I32Ty, Span.MinOffset))});
    NewF->getParent()
        ->getOrInsertNamedMetadata("feme.cpu.resources")
        ->addOperand(Node);
    Changed = true;
  }

  if (Changed) {
    if (GlobalVariable *PC = findPushConstantGlobal(M); PC && PC->use_empty())
      PC->eraseFromParent();
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace feme::cpu
