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
//                 Args->GroupID[0..2], w, entry_mask(w), groupshared);
//   }
//
// per "Phase 6: Group Execution and Barriers" and "Kernel ABI" in
// feme/docs/FeMeCPUDesign.md, where `WavesPerGroup` and `entry_mask` follow
// directly from the entry point's `hlsl.numthreads` dimensions and Phase 4's
// resolved wave size -- both compile-time constants, so `WavesPerGroup`
// needs no runtime computation and `entry_mask(w)` is a small vector
// comparison.
//
// Roadmap milestone 9 adds this pass's other two jobs:
//
//  - **Groupshared allocation**: `feme::cpu::SIMDizePass` (Phase 4) already
//    canonicalized every groupshared access into a `getelementptr` off the
//    wave body's `wave_groupshared` parameter (see GroupShared.h); this
//    pass computes the identical layout, allocates the backing buffer --
//    on the wrapper's own stack if it fits under `GroupSharedStackLimit`,
//    else from `FemeDispatchArgs::GroupShared` (a host-supplied buffer) --
//    and erases the now-dead groupshared globals once done with them.
//  - **Barrier region splitting**: a `..._with_group_sync` barrier
//    (`feme::cpu::matchBarrierCall`, see BarrierCalls.h) requires every
//    invocation in the group to arrive before any proceeds, which the wave
//    loop above cannot honor on its own -- it runs each wave to completion
//    before starting the next. `splitAtGroupSyncBarriers` below cuts the
//    wave body into one region per barrier and wraps *each* region in its
//    own wave loop, with a memory fence between consecutive loops (see
//    "Barriers" in "Phase 6"). A barrier with no group-sync requirement is
//    memory-ordering-only and needs no split: it becomes an in-place
//    `fence`. See the Status section's milestone 9 deviation note in
//    feme/docs/FeMeCPUDesign.md for this milestone's narrowing: only a
//    strictly linear (no branch, no loop) wave body is supported, and no
//    SSA value may be live across a `..._with_group_sync` barrier (only
//    groupshared/resource-heap memory may carry state across one) --
//    per-value context spilling is deferred.
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

#include "BarrierCalls.h"
#include "DispatchArgsLayout.h"
#include "GroupShared.h"
#include "feme/Transforms/CPU/SIMDize.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

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

/// The largest groupshared allocation this pass will place on the
/// wrapper's own stack; a shader declaring more falls back to a
/// host-supplied buffer (`FemeDispatchArgs::GroupShared`), per "Groupshared
/// memory" in "Phase 6: Group Execution and Barriers". 16 KiB matches the
/// smallest groupshared limit HLSL's source APIs guarantee (D3D11-class
/// hardware), a conservative choice for what's safe to put on a host
/// thread's stack.
constexpr uint64_t GroupSharedStackLimit = 16 * 1024;

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

/// The `FemeDispatchArgs`-derived values every wave-body call site needs,
/// gathered once in the wrapper's entry block (see `buildWrapper`).
struct WrapperEnv {
  Value *ResourceHeap;
  Value *ResourceHeapCount;
  Value *SamplerHeap;
  Value *SamplerHeapCount;
  Value *RootConstants;
  Value *RootConstantSize;
  Value *GroupIDX;
  Value *GroupIDY;
  Value *GroupIDZ;
  /// This group's flat groupshared buffer -- either a stack `alloca` this
  /// pass created or `Args->GroupShared`, depending on
  /// `GroupSharedStackLimit` (see the file comment above).
  Value *GroupShared;
};

/// Builds the `FemeDispatchArgs`-derived values every region's wave loop
/// shares, and, if \p GSLayout is non-empty, this group's groupshared
/// buffer -- on the wrapper's own stack if it fits, else the host-supplied
/// `Args->GroupShared` (see the file comment above).
WrapperEnv buildWrapperEnv(IRBuilder<> &Entry, StructType *ArgsTy, Value *Args,
                           const GroupSharedLayout &GSLayout) {
  Type *PtrTy = PointerType::get(Entry.getContext(), 0);
  Type *I32Ty = Entry.getInt32Ty();
  Type *I32x3 = ArrayType::get(I32Ty, 3);

  WrapperEnv Env;
  Env.ResourceHeap = loadArgsField(Entry, ArgsTy, Args,
                                   DispatchArgsField::ResourceHeap, PtrTy);
  Env.ResourceHeapCount = loadArgsField(
      Entry, ArgsTy, Args, DispatchArgsField::ResourceHeapCount, I32Ty);
  Env.SamplerHeap =
      loadArgsField(Entry, ArgsTy, Args, DispatchArgsField::SamplerHeap, PtrTy);
  Env.SamplerHeapCount = loadArgsField(
      Entry, ArgsTy, Args, DispatchArgsField::SamplerHeapCount, I32Ty);
  Env.RootConstants = loadArgsField(Entry, ArgsTy, Args,
                                    DispatchArgsField::RootConstants, PtrTy);
  Env.RootConstantSize = loadArgsField(
      Entry, ArgsTy, Args, DispatchArgsField::RootConstantSize, I32Ty);
  Value *GroupIDVec =
      loadArgsField(Entry, ArgsTy, Args, DispatchArgsField::GroupID, I32x3);
  Env.GroupIDX = Entry.CreateExtractValue(GroupIDVec, 0);
  Env.GroupIDY = Entry.CreateExtractValue(GroupIDVec, 1);
  Env.GroupIDZ = Entry.CreateExtractValue(GroupIDVec, 2);

  if (GSLayout.TotalSize == 0 || GSLayout.TotalSize > GroupSharedStackLimit) {
    Env.GroupShared = loadArgsField(Entry, ArgsTy, Args,
                                    DispatchArgsField::GroupShared, PtrTy);
  } else {
    AllocaInst *Buf = Entry.CreateAlloca(
        ArrayType::get(Entry.getInt8Ty(), GSLayout.TotalSize), nullptr,
        "groupshared");
    Buf->setAlignment(Align(GSLayout.Alignment));
    Env.GroupShared = Buf;
  }
  return Env;
}

/// Builds one region's wave loop -- `for (w = 0; w < WavesPerGroup; ++w)
/// RegionFn(..., w, entry_mask(w), ...);` -- branching into it from \p Pred
/// and returning its exit block, left with no terminator so the caller can
/// either chain a fence into the next region's loop or return. Block names
/// get \p Suffix appended (empty for the common single-region case, to
/// match the names milestone 4's tests already check for).
BasicBlock *buildWaveLoop(Function &Wrapper, BasicBlock *Pred,
                          Function &RegionFn, const WrapperEnv &Env,
                          unsigned WaveSize, uint32_t GroupSizeTotal,
                          uint32_t WavesPerGroup, const Twine &Suffix) {
  LLVMContext &Ctx = Wrapper.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);

  BasicBlock *HeaderBB =
      BasicBlock::Create(Ctx, "wave.loop.header" + Suffix, &Wrapper);
  BasicBlock *BodyBB =
      BasicBlock::Create(Ctx, "wave.loop.body" + Suffix, &Wrapper);
  BasicBlock *ExitBB =
      BasicBlock::Create(Ctx, "wave.loop.exit" + Suffix, &Wrapper);

  IRBuilder<>(Pred).CreateBr(HeaderBB);

  IRBuilder<> Header(HeaderBB);
  PHINode *W = Header.CreatePHI(I32Ty, 2, "w" + Suffix);
  W->addIncoming(Header.getInt32(0), Pred);
  Value *Cond = Header.CreateICmpULT(W, Header.getInt32(WavesPerGroup),
                                     "wave.cond" + Suffix);
  Header.CreateCondBr(Cond, BodyBB, ExitBB);

  IRBuilder<> Body(BodyBB);
  Value *Mask = buildEntryMask(Body, W, WaveSize, GroupSizeTotal);

  SmallVector<Value *, 12> CallArgs;
  for (const Argument &Arg : RegionFn.args()) {
    if (Arg.getName() == "resource_heap")
      CallArgs.push_back(Env.ResourceHeap);
    else if (Arg.getName() == "resource_heap_count")
      CallArgs.push_back(Env.ResourceHeapCount);
    else if (Arg.getName() == "sampler_heap")
      CallArgs.push_back(Env.SamplerHeap);
    else if (Arg.getName() == "sampler_heap_count")
      CallArgs.push_back(Env.SamplerHeapCount);
    else if (Arg.getName() == "root_constants")
      CallArgs.push_back(Env.RootConstants);
    else if (Arg.getName() == "root_constant_size")
      CallArgs.push_back(Env.RootConstantSize);
    else if (Arg.getName() == "wave_group_id_x")
      CallArgs.push_back(Env.GroupIDX);
    else if (Arg.getName() == "wave_group_id_y")
      CallArgs.push_back(Env.GroupIDY);
    else if (Arg.getName() == "wave_group_id_z")
      CallArgs.push_back(Env.GroupIDZ);
    else if (Arg.getName() == "wave_index")
      CallArgs.push_back(W);
    else if (Arg.getName() == "wave_entry_mask")
      CallArgs.push_back(Mask);
    else if (Arg.getName() == "wave_groupshared")
      CallArgs.push_back(Env.GroupShared);
    else
      llvm_unreachable("unexpected wave-body parameter for EntryWrapperPass");
  }
  Body.CreateCall(&RegionFn, CallArgs);
  Value *WNext = Body.CreateAdd(W, Body.getInt32(1), "w.next" + Suffix);
  Body.CreateBr(HeaderBB);
  W->addIncoming(WNext, BodyBB);

  return ExitBB;
}

/// Maps `feme::cpu::BarrierMemoryScope` to the `fence`'s sync scope: a
/// group-only barrier orders groupshared memory only, which every wave of
/// this group already accesses on the same host thread in program order --
/// no cross-thread visibility is needed for that (see "Groupshared memory"
/// in "Phase 6"), so `SyncScope::SingleThread` suffices. `Device`/`All`
/// order the descriptor-heap-backed resource memory a *different* group,
/// running on a different host thread, may concurrently touch, and need
/// `SyncScope::System`. `Device` and `All` are not distinguished further
/// (see BarrierCalls.h's `BarrierMemoryScope` doc comment).
SyncScope::ID syncScopeFor(BarrierMemoryScope Scope) {
  return Scope == BarrierMemoryScope::Group ? SyncScope::SingleThread
                                            : SyncScope::System;
}

/// Replaces every barrier call in \p F that does not require group-sync
/// convergence (`feme::cpu::MatchedBarrier::GroupSync == false`) with an
/// in-place `fence`: it needs no region split, only a memory-ordering
/// point (see the file comment above).
void replaceMemoryOnlyBarriers(Function &F) {
  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    std::optional<MatchedBarrier> Matched = matchBarrierCall(*CI);
    if (!Matched || Matched->GroupSync)
      continue;
    IRBuilder<> Builder(CI);
    Builder.CreateFence(AtomicOrdering::AcquireRelease,
                        syncScopeFor(Matched->MemoryScope));
    CI->eraseFromParent();
  }
}

/// Whether \p F's control flow is a single straight chain from its entry
/// block to a `ret` -- no branch, no loop -- filling \p Order with its
/// blocks in that order if so. This milestone's region splitting (see the
/// file comment above) only supports this shape.
bool isLinearChain(Function &F, SmallVectorImpl<BasicBlock *> &Order) {
  SmallPtrSet<BasicBlock *, 8> Visited;
  BasicBlock *BB = &F.getEntryBlock();
  while (true) {
    if (!Visited.insert(BB).second)
      return false; // A cycle: not a loop-free chain.
    Order.push_back(BB);
    Instruction *Term = BB->getTerminator();
    if (isa<ReturnInst>(Term))
      break;
    auto *Br = dyn_cast<UncondBrInst>(Term);
    if (!Br)
      return false; // A surviving conditional branch (or something else).
    BB = Br->getSuccessor(0);
  }
  // Every block reached by exactly one step from the last: if some block
  // was never visited, it either merges into this chain from elsewhere (a
  // second predecessor this walk didn't need to take) or is unreachable
  // dead code -- either way, not the simple chain this milestone supports.
  return Order.size() == F.size();
}

/// One region boundary: the memory scope its `..._with_group_sync` barrier
/// requested, used to pick the fence `feme::cpu::EntryWrapperPass` places
/// between the two regions' wave loops.
struct RegionBoundary {
  BarrierMemoryScope MemoryScope;
};

/// Splits \p WaveBody into one function per `..._with_group_sync` barrier
/// region (see the file comment above), returning every region function in
/// order (the last one is \p WaveBody itself, left with only its final
/// region's blocks) and each boundary's memory scope, or `std::nullopt`
/// (having emitted a diagnostic, leaving \p WaveBody unmodified) if
/// \p WaveBody's shape is not one this milestone supports.
std::optional<SmallVector<Function *, 4>>
splitAtGroupSyncBarriers(Function &WaveBody,
                         SmallVectorImpl<RegionBoundary> &Boundaries) {
  SmallVector<BasicBlock *, 8> Order;
  if (!isLinearChain(WaveBody, Order)) {
    WaveBody.getContext().emitError(
        "feme-cpu-wrap-entry: function '" + WaveBody.getName() +
        "' has a barrier inside non-linear control flow (a surviving "
        "branch or a loop); region splitting only supports a "
        "straight-line wave body for now (roadmap milestone 9 deviation)");
    return std::nullopt;
  }

  SmallVector<CallInst *, 4> Barriers;
  SmallVector<BarrierMemoryScope, 4> Scopes;
  DenseMap<Instruction *, unsigned> IndexOf;
  unsigned Idx = 0;
  for (BasicBlock *BB : Order) {
    for (Instruction &I : *BB) {
      IndexOf[&I] = Idx++;
      if (auto *CI = dyn_cast<CallInst>(&I))
        if (std::optional<MatchedBarrier> Matched = matchBarrierCall(*CI);
            Matched && Matched->GroupSync) {
          Barriers.push_back(CI);
          Scopes.push_back(Matched->MemoryScope);
        }
    }
  }

  // No SSA value may be live across a barrier (see the file comment's
  // narrowing note): any operand, anywhere after a barrier, that resolves
  // to an instruction defined strictly before it (and is not one of
  // `WaveBody`'s own parameters, legitimately shared by every region) means
  // this milestone's context-spilling gap would be reached.
  for (CallInst *Barrier : Barriers) {
    unsigned BarrierIdx = IndexOf[Barrier];
    for (BasicBlock *BB : Order) {
      for (Instruction &I : *BB) {
        if (IndexOf[&I] <= BarrierIdx)
          continue;
        for (Value *Op : I.operands()) {
          auto *OpI = dyn_cast<Instruction>(Op);
          if (!OpI)
            continue;
          auto It = IndexOf.find(OpI);
          if (It != IndexOf.end() && It->second < BarrierIdx) {
            WaveBody.getContext().emitError(
                "feme-cpu-wrap-entry: function '" + WaveBody.getName() +
                "' has a value live across a group-sync barrier; "
                "per-value context spilling is not yet supported (roadmap "
                "milestone 9 deviation) -- carry state across a barrier "
                "through groupshared memory instead");
            return std::nullopt;
          }
        }
      }
    }
  }

  // Cut the chain at each barrier call: `SplitBlock` gives the boundary
  // block, then the (now-redundant, convergence is what region splitting
  // itself provides) barrier call is dropped.
  SmallPtrSet<BasicBlock *, 4> BoundaryBlocks;
  for (CallInst *Barrier : Barriers) {
    BasicBlock *After = SplitBlock(Barrier->getParent(), Barrier,
                                   static_cast<DominatorTree *>(nullptr));
    BoundaryBlocks.insert(After);
    Barrier->eraseFromParent();
  }
  for (BarrierMemoryScope Scope : Scopes)
    Boundaries.push_back({Scope});

  SmallVector<SmallVector<BasicBlock *, 8>, 4> RegionBlocks(1);
  for (BasicBlock &BB : WaveBody) {
    if (BoundaryBlocks.contains(&BB))
      RegionBlocks.emplace_back();
    RegionBlocks.back().push_back(&BB);
  }

  SmallVector<Function *, 4> Regions;
  for (unsigned R = 0; R + 1 < RegionBlocks.size(); ++R) {
    ArrayRef<BasicBlock *> Blocks = RegionBlocks[R];
    Function *RegionFn = Function::Create(
        WaveBody.getFunctionType(), GlobalValue::InternalLinkage,
        WaveBody.getAddressSpace(), WaveBody.getName() + ".region" + Twine(R),
        WaveBody.getParent());
    RegionFn->copyAttributesFrom(&WaveBody);
    RegionFn->setComdat(WaveBody.getComdat());
    for (auto [OldArg, NewArg] : llvm::zip(WaveBody.args(), RegionFn->args()))
      NewArg.setName(OldArg.getName());

    RegionFn->splice(RegionFn->begin(), &WaveBody,
                     Blocks.front()->getIterator(),
                     std::next(Blocks.back()->getIterator()));

    // The region's last block still ends with the unconditional branch
    // `SplitBlock` created to the (now-elsewhere) next region's first
    // block; replace it with the `ret void` this region's own function
    // needs instead.
    Instruction *Term = RegionFn->back().getTerminator();
    assert(isa<UncondBrInst>(Term));
    Term->eraseFromParent();
    IRBuilder<>(&RegionFn->back()).CreateRetVoid();

    for (Instruction &I : instructions(*RegionFn))
      for (Use &U : I.operands())
        if (auto *Arg = dyn_cast<Argument>(U.get());
            Arg && Arg->getParent() == &WaveBody)
          U.set(RegionFn->getArg(Arg->getArgNo()));

    Regions.push_back(RegionFn);
  }
  Regions.push_back(&WaveBody);
  return Regions;
}

/// Builds the exported `feme_cpu_entry_<name>` wrapper around \p WaveBody
/// (see the file comment above), or nullptr if \p WaveBody was not widened
/// by `feme::cpu::SIMDizePass` (has no `WaveBodyEnv`) -- nothing for this
/// pass to wrap in that case -- or if barrier splitting failed (a
/// diagnostic has already been emitted).
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

  replaceMemoryOnlyBarriers(WaveBody);

  SmallVector<RegionBoundary, 4> Boundaries;
  SmallVector<Function *, 4> Regions;
  bool HasGroupSyncBarrier = any_of(instructions(WaveBody), [](Instruction &I) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      return false;
    std::optional<MatchedBarrier> Matched = matchBarrierCall(*CI);
    return Matched && Matched->GroupSync;
  });
  if (HasGroupSyncBarrier) {
    std::optional<SmallVector<Function *, 4>> Split =
        splitAtGroupSyncBarriers(WaveBody, Boundaries);
    if (!Split)
      return nullptr;
    Regions = std::move(*Split);
  } else {
    Regions.push_back(&WaveBody);
  }

  GroupSharedLayout GSLayout = computeGroupSharedLayout(M);

  StructType *ArgsTy = getDispatchArgsType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);

  std::string WrapperName = getEntrySymbolName(WaveBody.getName());
  FunctionType *WrapperTy =
      FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false);
  Function *Wrapper =
      Function::Create(WrapperTy, GlobalValue::ExternalLinkage, WrapperName, M);
  Argument *Args = Wrapper->getArg(0);
  Args->setName("args");

  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Wrapper);
  IRBuilder<> Entry(EntryBB);
  WrapperEnv WEnv = buildWrapperEnv(Entry, ArgsTy, Args, GSLayout);

  BasicBlock *Pred = EntryBB;
  for (unsigned R = 0, E = Regions.size(); R != E; ++R) {
    Twine Suffix = E == 1 ? Twine() : Twine(".") + Twine(R);
    BasicBlock *ExitBB =
        buildWaveLoop(*Wrapper, Pred, *Regions[R], WEnv, WaveSize,
                      GroupSizeTotal, WavesPerGroup, Suffix);
    if (R + 1 != E) {
      IRBuilder<> ExitBuilder(ExitBB);
      ExitBuilder.CreateFence(AtomicOrdering::AcquireRelease,
                              syncScopeFor(Boundaries[R].MemoryScope));
    }
    Pred = ExitBB;
  }
  IRBuilder<>(Pred).CreateRetVoid();

  // The wave body/region functions are implementation details behind the
  // exported wrapper (see "Kernel ABI"): give them internal linkage so
  // later optimization can inline/specialize them freely.
  for (Function *Region : Regions)
    Region->setLinkage(GlobalValue::InternalLinkage);

  // The groupshared globals `feme::cpu::SIMDizePass` already rewired every
  // access away from (see GroupShared.h) are now dead; drop them.
  if (GSLayout.TotalSize != 0) {
    for (auto &[GVConst, Offset] : GSLayout.Offsets) {
      auto *GV = const_cast<GlobalVariable *>(GVConst);
      if (GV->use_empty())
        GV->eraseFromParent();
    }
  }

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
