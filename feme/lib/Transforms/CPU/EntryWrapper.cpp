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
//    zeroes it (roadmap milestone E13,
//    `VK_KHR_zero_initialize_workgroup_memory`) if any groupshared global in
//    the module requested it
//    (`GroupSharedLayout::NeedsZeroInit`), and erases the now-dead groupshared
//    globals once done with them.
//  - **Barrier region splitting**: a `..._with_group_sync` barrier
//    (`feme::cpu::matchBarrierCall`, see BarrierCalls.h) requires every
//    invocation in the group to arrive before any proceeds, which the wave
//    loop above cannot honor on its own -- it runs each wave to completion
//    before starting the next. `splitAtGroupSyncBarriers` below cuts the
//    wave body into one region per barrier and wraps *each* region in its
//    own wave loop, with a memory fence between consecutive loops (see
//    "Barriers" in "Phase 6"). A barrier with no group-sync requirement is
//    memory-ordering-only and needs no split: it becomes an in-place
//    `fence`.
//
// Roadmap step R5 (feme/docs/Roadmap.md) closes milestone 9's two
// remaining narrowings:
//
//  - **Values live across a barrier**: any SSA value defined in one
//    barrier-split region and used by a later one is spilled into a
//    per-wave context array (`spillValuesLiveAcrossBarriers` below) --
//    `[WavesPerGroup x SpillTy]`, allocated by the wrapper alongside
//    groupshared memory, indexed by `wave_index` -- rather than being
//    diagnosed: the defining region stores into its own slot right after
//    computing the value, and every later region reloads from the same
//    slot in place of the (now cross-function-invalid) original use. Every
//    region function gains a trailing `barrier_spill` parameter once any
//    value needs this.
//  - **Barriers inside a uniform loop**: `matchLoopShape` recognizes the
//    canonical header-tested loop shape a stride-halving reduction (or
//    similar) compiles to -- see `LoopShape`'s doc comment -- and
//    `buildWrapperForLoop` clones its header/latch (a pure, side-effect-
//    free scalar recurrence, safe to run once per iteration rather than
//    once per wave) directly into the wrapper as an ordinary scalar loop,
//    while the loop body's barrier-split regions each still run through
//    the usual per-wave `buildWaveLoop`, once per iteration (see "A barrier
//    inside a uniform loop" in "Phase 6: Group Execution and Barriers").
//    The loop's own induction variable(s) become a `loopvarN` parameter
//    threaded through every body region. A loop shape other than this one
//    remains diagnosed rather than mis-split.
//
// Roadmap step R24 closes milestone 9's remaining two narrowings:
//
//  - **Barrier inside a surviving branch**: `matchBranchShape` recognizes a
//    uniform two-way branch -- guaranteed uniform, since a divergent branch
//    a barrier could survive inside is already gone by this point
//    (`feme::cpu::LinearizePass`) -- whose arms are each a linear chain
//    (possibly empty, for a plain `if` with no `else`) reconverging at a
//    merge block with no phi of its own. `buildWrapperForBranch` clones
//    the branch's own uniform condition (computed once, not once per wave)
//    directly into the wrapper as an ordinary scalar `br`, and
//    barrier-splits each arm exactly like a straight-line wave body
//    (`splitArmAtBarriers`), with the wrapper's own real control flow
//    choosing which arm's wave loops run. A merge block with a phi (a
//    value one arm computes differently from the other, needed after the
//    branch) remains diagnosed: threading it would mean spilling across a
//    control-flow choice made once, scalar, in the wrapper, not just across
//    a barrier within a single region, which this milestone's spilling
//    does not yet support. A value live across a barrier *within* one arm
//    (not across the branch itself) is also still diagnosed, for the same
//    reason `buildWaveLoop`'s simple argument-name dispatch narrowed
//    milestone 9 in the first place: each arm would need its own spill
//    buffer, and there is only one `barrier_spill` parameter name to go
//    around.
//  - **A `phi` live across a barrier**: spilled exactly like any other
//    value (`spillValuesLiveAcrossBarriers` below), with its spill store
//    placed after its own block's last phi rather than immediately after
//    itself (a phi must stay grouped with any others at the top of a
//    block).
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
#include "feme/Core/ShaderStage.h"
#include "feme/Transforms/CPU/SIMDize.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SetVector.h"
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
#include "llvm/Transforms/Utils/ValueMapper.h"

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
  /// The image heap/count (roadmap R30); see `ResourceCallEnv::ImageHeap`'s
  /// comment in ResourceCalls.h for why these are always loaded here even
  /// for a shader with no image access of its own.
  Value *ImageHeap;
  Value *ImageHeapCount;
  Value *GroupIDX;
  Value *GroupIDY;
  Value *GroupIDZ;
  /// This group's flat groupshared buffer -- either a stack `alloca` this
  /// pass created or `Args->GroupShared`, depending on
  /// `GroupSharedStackLimit` (see the file comment above).
  Value *GroupShared;
  /// This group's barrier-context spill buffer -- `ptr` to
  /// `[WavesPerGroup x SpillTy]`, one slot per wave -- or null if no SSA
  /// value is live across a barrier in this shader (see "Values live
  /// across a barrier" in the file comment above). Every wave of every
  /// region writes and reads only its own slot (index `wave_index`), so
  /// this needs no synchronization beyond the barrier's own fence.
  Value *BarrierSpill;

  /// The `FemeMeshArgs`-only fields `feme::cpu::MeshOutputWrapperPass`
  /// (roadmap H6c-a-a) appends to a mesh entry's wave body before this
  /// pass runs, or null for every non-mesh stage (see `buildWrapperEnv`'s
  /// own `IsMesh` parameter). See MeshOutputWrapper.h for what each one
  /// means; this pass only threads them through to the wave body by name,
  /// the same way it already does for `resource_heap`/`wave_groupshared`/
  /// etc. -- see `buildWaveLoop`'s dispatch below.
  Value *MeshVertexOutputLayout = nullptr;
  Value *MeshVertexOutputs = nullptr;
  Value *MeshPrimitiveOutputLayout = nullptr;
  Value *MeshPrimitiveOutputs = nullptr;
  Value *MeshMaxOutputVertices = nullptr;
  Value *MeshMaxOutputPrimitives = nullptr;
};

/// Builds the `FemeDispatchArgs`-derived values every region's wave loop
/// shares, and, if \p GSLayout is non-empty, this group's groupshared
/// buffer -- on the wrapper's own stack if it fits, else the host-supplied
/// `Args->GroupShared` (see the file comment above). If \p SpillTy is
/// non-null, also allocates the barrier-context spill buffer sized for
/// \p WavesPerGroup waves. If \p IsMesh, \p ArgsTy is actually
/// `getMeshArgsType`'s longer struct (see `WrapperEnv::MeshVertexOutputLayout`
/// et al.'s own comment), and this also loads its mesh-only fields.
WrapperEnv buildWrapperEnv(IRBuilder<> &Entry, StructType *ArgsTy, Value *Args,
                           const GroupSharedLayout &GSLayout,
                           StructType *SpillTy, uint32_t WavesPerGroup,
                           bool IsMesh = false) {
  Type *PtrTy = PointerType::get(Entry.getContext(), 0);
  Type *I32Ty = Entry.getInt32Ty();
  Type *I32x3 = ArrayType::get(I32Ty, 3);

  WrapperEnv Env;
  Env.ResourceHeap = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldResourceHeap, PtrTy);
  Env.ResourceHeapCount = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldResourceHeapCount, I32Ty);
  Env.SamplerHeap = loadResourcesField(Entry, ArgsTy, Args,
                                       ShaderResourcesFieldSamplerHeap, PtrTy);
  Env.SamplerHeapCount = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldSamplerHeapCount, I32Ty);
  Env.RootConstants = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldRootConstants, PtrTy);
  Env.RootConstantSize = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldRootConstantSize, I32Ty);
  Env.ImageHeap = loadResourcesField(Entry, ArgsTy, Args,
                                     ShaderResourcesFieldImageHeap, PtrTy);
  Env.ImageHeapCount = loadResourcesField(
      Entry, ArgsTy, Args, ShaderResourcesFieldImageHeapCount, I32Ty);
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

  // `VK_KHR_zero_initialize_workgroup_memory` (roadmap milestone E13):
  // this group's flat buffer -- a fresh stack `alloca` above, or a
  // host-supplied one this dispatch's caller reuses across every group in
  // the same `vkCmdDispatch` (see `runDispatch` in
  // feme/lib/Vulkan/CommandBuffer.cpp) -- is otherwise left holding
  // whatever the host's allocator or a previous group's own invocation
  // happened to leave behind. Zeroing it here, once per group, is what
  // gives a `zero_initialized` groupshared global (`GSLayout.NeedsZeroInit`)
  // its own guarantee regardless of which of those two backing stores it
  // ended up in.
  if (GSLayout.NeedsZeroInit) {
    Entry.CreateMemSet(Env.GroupShared, Entry.getInt8(0), GSLayout.TotalSize,
                       Align(GSLayout.Alignment));
  }

  Env.BarrierSpill = nullptr;
  if (SpillTy) {
    AllocaInst *Buf = Entry.CreateAlloca(ArrayType::get(SpillTy, WavesPerGroup),
                                         nullptr, "barrier.spill");
    Env.BarrierSpill = Buf;
  }

  if (IsMesh) {
    Env.MeshVertexOutputLayout = loadArgsField(
        Entry, ArgsTy, Args, MeshArgsFieldVertexOutputLayout, PtrTy);
    Env.MeshVertexOutputs = loadArgsField(Entry, ArgsTy, Args,
                                          MeshArgsFieldVertexOutputs, PtrTy);
    Env.MeshPrimitiveOutputLayout = loadArgsField(
        Entry, ArgsTy, Args, MeshArgsFieldPrimitiveOutputLayout, PtrTy);
    Env.MeshPrimitiveOutputs = loadArgsField(
        Entry, ArgsTy, Args, MeshArgsFieldPrimitiveOutputs, PtrTy);
    Env.MeshMaxOutputVertices = loadArgsField(
        Entry, ArgsTy, Args, MeshArgsFieldMaxOutputVertices, I32Ty);
    Env.MeshMaxOutputPrimitives = loadArgsField(
        Entry, ArgsTy, Args, MeshArgsFieldMaxOutputPrimitives, I32Ty);
  }
  return Env;
}

/// Builds one region's wave loop -- `for (w = 0; w < WavesPerGroup; ++w)
/// RegionFn(..., w, entry_mask(w), ...);` -- branching into it from \p Pred
/// and returning its exit block, left with no terminator so the caller can
/// either chain a fence into the next region's loop or return. Block names
/// get \p Suffix appended (empty for the common single-region case, to
/// match the names milestone 4's tests already check for). \p LoopScalars,
/// if non-empty, supplies the current value of each "Barriers inside a
/// uniform loop" induction variable (see the file comment above), matched
/// by \p RegionFn's `loopvarN` parameters the same way every other
/// `WaveBodyEnv`/`barrier_spill` parameter is matched by name.
BasicBlock *buildWaveLoop(Function &Wrapper, BasicBlock *Pred,
                          Function &RegionFn, const WrapperEnv &Env,
                          unsigned WaveSize, uint32_t GroupSizeTotal,
                          uint32_t WavesPerGroup, const Twine &Suffix,
                          ArrayRef<Value *> LoopScalars = {}) {
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
    else if (Arg.getName() == "image_heap")
      CallArgs.push_back(Env.ImageHeap);
    else if (Arg.getName() == "image_heap_count")
      CallArgs.push_back(Env.ImageHeapCount);
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
    else if (Arg.getName() == "wave_sideeffect_mask")
      CallArgs.push_back(Mask);
    else if (Arg.getName() == "wave_groupshared")
      CallArgs.push_back(Env.GroupShared);
    else if (Arg.getName() == "barrier_spill")
      CallArgs.push_back(Env.BarrierSpill);
    else if (Arg.getName() == "mesh_vertex_output_layout")
      CallArgs.push_back(Env.MeshVertexOutputLayout);
    else if (Arg.getName() == "mesh_vertex_outputs")
      CallArgs.push_back(Env.MeshVertexOutputs);
    else if (Arg.getName() == "mesh_primitive_output_layout")
      CallArgs.push_back(Env.MeshPrimitiveOutputLayout);
    else if (Arg.getName() == "mesh_primitive_outputs")
      CallArgs.push_back(Env.MeshPrimitiveOutputs);
    else if (Arg.getName() == "mesh_max_output_vertices")
      CallArgs.push_back(Env.MeshMaxOutputVertices);
    else if (Arg.getName() == "mesh_max_output_primitives")
      CallArgs.push_back(Env.MeshMaxOutputPrimitives);
    else if (Arg.getName().starts_with("loopvar")) {
      unsigned N;
      bool Failed =
          Arg.getName().drop_front(strlen("loopvar")).getAsInteger(10, N);
      assert(!Failed && N < LoopScalars.size());
      (void)Failed;
      CallArgs.push_back(LoopScalars[N]);
    } else
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

/// Recreates \p F with one extra trailing parameter of type \p ExtraType
/// named \p ExtraName appended to its signature, moving every existing
/// block/instruction over unchanged -- the same technique
/// `feme::cpu::FunctionWidener::buildWidenedFunction` uses to append
/// `SIMDizePass`'s own trailing parameters (see SIMDize.cpp) -- and
/// replacing \p F in its module. \p F itself is erased; instructions and
/// basic blocks keep their identity (only \p F's own `Argument`s do not),
/// so any pointer into \p F's body other than to one of its old arguments
/// remains valid after this call.
std::pair<Function *, Argument *>
appendTrailingParam(Function &F, Type *ExtraType, const Twine &ExtraName) {
  SmallVector<Type *, 8> ParamTypes(F.getFunctionType()->params());
  ParamTypes.push_back(ExtraType);
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
  Argument *Extra = NewF->getArg(NewF->arg_size() - 1);
  Extra->setName(ExtraName);

  NewF->takeName(&F);
  F.replaceAllUsesWith(NewF);
  F.eraseFromParent();
  return {NewF, Extra};
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

/// Spills every SSA value in \p Order that is live across one of
/// \p Barriers (defined before it, used strictly after -- see "Values
/// live across a barrier" in the file comment above) into a per-wave
/// context array, appending a new trailing `barrier_spill` parameter to
/// \p WaveBody if any such value is found. \p WaveBody is reassigned to
/// the recreated function in that case (see `appendTrailingParam`);
/// \p Order/\p IndexOf/\p Barriers's `BasicBlock*`/`Instruction*` pointers
/// stay valid regardless. A `phi` is spilled exactly like any other
/// instruction (see "Barrier inside a surviving branch, and a `phi` live
/// across a barrier" in the file comment above), except its spill store
/// goes after its block's last phi rather than right after itself, since a
/// phi must stay grouped with any others at the top of its block. Returns
/// false (having emitted a diagnostic, leaving \p WaveBody unmodified) if a
/// live value's shape is not one this milestone's spilling supports.
bool spillValuesLiveAcrossBarriers(
    Function *&WaveBody, ArrayRef<BasicBlock *> Order,
    ArrayRef<CallInst *> Barriers,
    const DenseMap<Instruction *, unsigned> &IndexOf, StructType *&SpillTyOut) {
  SpillTyOut = nullptr;
  SmallSetVector<Instruction *, 4> SpilledDefs;
  SmallVector<std::tuple<Instruction *, Instruction *, unsigned>, 8>
      SpilledUses;
  for (CallInst *Barrier : Barriers) {
    unsigned BarrierIdx = IndexOf.lookup(Barrier);
    for (BasicBlock *BB : Order) {
      for (Instruction &I : *BB) {
        if (IndexOf.lookup(&I) <= BarrierIdx)
          continue;
        for (Use &Op : I.operands()) {
          auto *OpI = dyn_cast<Instruction>(Op.get());
          if (!OpI)
            continue;
          auto It = IndexOf.find(OpI);
          if (It != IndexOf.end() && It->second < BarrierIdx) {
            SpilledDefs.insert(OpI);
            SpilledUses.emplace_back(OpI, &I, Op.getOperandNo());
          }
        }
      }
    }
  }

  if (SpilledDefs.empty())
    return true;

  LLVMContext &Ctx = WaveBody->getContext();
  SmallVector<Type *, 4> FieldTypes;
  DenseMap<Instruction *, unsigned> FieldOf;
  for (Instruction *Def : SpilledDefs) {
    FieldOf[Def] = FieldTypes.size();
    FieldTypes.push_back(Def->getType());
  }
  auto *SpillTy = StructType::create(
      Ctx, FieldTypes, (WaveBody->getName() + ".barrier_spill").str());
  SpillTyOut = SpillTy;

  std::optional<WaveBodyEnv> Env = getWaveBodyEnv(*WaveBody);
  unsigned WaveIndexArgNo = cast<Argument>(Env->WaveIndex)->getArgNo();

  Type *PtrTy = PointerType::get(Ctx, 0);
  auto AppendResult = appendTrailingParam(*WaveBody, PtrTy, "barrier_spill");
  Function *NewWaveBody = AppendResult.first;
  Argument *SpillArg = AppendResult.second;
  WaveBody = NewWaveBody;
  Argument *WaveIndex = WaveBody->getArg(WaveIndexArgNo);

  auto buildFieldPtr = [&](IRBuilder<> &Builder, Instruction *Def,
                           const Twine &Name) {
    Value *Slot =
        Builder.CreateGEP(SpillTy, SpillArg, {WaveIndex}, Name + ".slot");
    return Builder.CreateStructGEP(SpillTy, Slot, FieldOf[Def], Name);
  };

  for (Instruction *Def : SpilledDefs) {
    // A phi's own block may hold further phis right after it (LLVM
    // requires every phi in a block to precede every non-phi instruction),
    // so a spilled phi's store goes after the block's last phi rather than
    // right after the phi itself.
    Instruction *InsertPt = isa<PHINode>(Def)
                                ? &*Def->getParent()->getFirstNonPHIOrDbg()
                                : Def->getNextNode();
    IRBuilder<> Builder(InsertPt);
    Value *Field = buildFieldPtr(Builder, Def, Def->getName() + ".spill");
    Builder.CreateStore(Def, Field);
  }
  for (auto &[Def, User, OperandNo] : SpilledUses) {
    IRBuilder<> Builder(User);
    Value *Field = buildFieldPtr(Builder, Def, Def->getName() + ".reload");
    Value *Reloaded = Builder.CreateLoad(Def->getType(), Field,
                                         Def->getName() + ".reload.val");
    User->setOperand(OperandNo, Reloaded);
  }
  return true;
}

/// Splits \p WaveBody into one function per `..._with_group_sync` barrier
/// region (see the file comment above), returning every region function in
/// order (the last one is \p WaveBody itself, left with only its final
/// region's blocks) and each boundary's memory scope, or `std::nullopt`
/// (having emitted a diagnostic) if \p WaveBody's shape is not one this
/// milestone supports. Any SSA value live across a barrier is spilled
/// (see `spillValuesLiveAcrossBarriers`), which may recreate \p WaveBody
/// with an extra trailing parameter -- \p WaveBody is reassigned in that
/// case, so callers must use its new value afterward -- and sets
/// \p SpillTyOut to the spill context's struct type (null if no value
/// needed spilling).
std::optional<SmallVector<Function *, 4>>
splitAtGroupSyncBarriers(Function *&WaveBody,
                         SmallVectorImpl<RegionBoundary> &Boundaries,
                         StructType *&SpillTyOut) {
  SmallVector<BasicBlock *, 8> Order;
  if (!isLinearChain(*WaveBody, Order)) {
    WaveBody->getContext().emitError(
        "feme-cpu-wrap-entry: function '" + WaveBody->getName() +
        "' has a barrier inside non-linear control flow (a surviving "
        "branch not part of a supported loop); region splitting only "
        "supports a straight-line wave body or a single uniform loop "
        "(roadmap milestone 9 deviation)");
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

  if (!spillValuesLiveAcrossBarriers(WaveBody, Order, Barriers, IndexOf,
                                     SpillTyOut))
    return std::nullopt;

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
  for (BasicBlock &BB : *WaveBody) {
    if (BoundaryBlocks.contains(&BB))
      RegionBlocks.emplace_back();
    RegionBlocks.back().push_back(&BB);
  }

  SmallVector<Function *, 4> Regions;
  for (unsigned R = 0; R + 1 < RegionBlocks.size(); ++R) {
    ArrayRef<BasicBlock *> Blocks = RegionBlocks[R];
    Function *RegionFn = Function::Create(
        WaveBody->getFunctionType(), GlobalValue::InternalLinkage,
        WaveBody->getAddressSpace(), WaveBody->getName() + ".region" + Twine(R),
        WaveBody->getParent());
    RegionFn->copyAttributesFrom(WaveBody);
    RegionFn->setComdat(WaveBody->getComdat());
    for (auto [OldArg, NewArg] : llvm::zip(WaveBody->args(), RegionFn->args()))
      NewArg.setName(OldArg.getName());

    RegionFn->splice(RegionFn->begin(), WaveBody, Blocks.front()->getIterator(),
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
            Arg && Arg->getParent() == WaveBody)
          U.set(RegionFn->getArg(Arg->getArgNo()));

    Regions.push_back(RegionFn);
  }
  Regions.push_back(WaveBody);
  return Regions;
}

/// One induction variable driving a "Barriers inside a uniform loop" shape
/// (see the file comment above): \p Header's own phi node, and the
/// constant value it starts at (the incoming value from outside the loop
/// -- this milestone only supports a compile-time-constant initial value,
/// e.g. a stride-halving reduction loop's `GroupSize / 2`).
struct LoopInduction {
  PHINode *HeaderPhi;
  Constant *InitialValue;
};

/// The natural, header-tested loop shape this milestone's region splitting
/// supports for "Barriers inside a uniform loop" (see the file comment
/// above): a header block containing only phis, a single comparison and a
/// conditional branch to the loop body or the loop's exit; a body chain
/// reached in a straight line from the header's body successor, containing
/// this function's group-sync barrier(s), that reaches a latch block
/// whose non-terminator instructions are a pure, side-effect-free
/// recurrence over the header's own phis (and constants) feeding back into
/// them; and linear prefix/suffix chains (from the function's entry to the
/// header, and from the loop's exit block to a `ret`) that contain no
/// barrier of their own. Every block of the function belongs to exactly
/// one of these four regions.
struct LoopShape {
  BasicBlock *Header;
  BasicBlock *Latch;
  BasicBlock *ExitBlock;
  SmallVector<BasicBlock *, 4> PrefixOrder;
  /// The header's body successor .. the block just before `Latch`
  /// (exclusive of `Latch` itself -- see `matchLoopShape`'s doc comment).
  SmallVector<BasicBlock *, 4> BodyOrder;
  SmallVector<BasicBlock *, 4> SuffixOrder;
  SmallVector<LoopInduction, 2> Inductions;
};

/// Walks from \p Start following only single-successor unconditional
/// branches, appending every block visited to \p Order, stopping (without
/// failing) at a block whose own terminator is not such a branch --
/// returned as the walk's final block, or nullptr if a cycle was found
/// before reaching one (an inner loop/diamond this milestone doesn't
/// support in a prefix/suffix chain).
BasicBlock *walkLinearChain(BasicBlock *Start,
                            SmallVectorImpl<BasicBlock *> &Order) {
  SmallPtrSet<BasicBlock *, 8> Visited;
  BasicBlock *BB = Start;
  while (true) {
    if (!Visited.insert(BB).second)
      return nullptr;
    Instruction *Term = BB->getTerminator();
    auto *Br = dyn_cast<UncondBrInst>(Term);
    if (!Br)
      return BB;
    Order.push_back(BB);
    BB = Br->getSuccessor(0);
  }
}

/// Whether every instruction in \p Blocks other than a trailing terminator
/// has no side effects, with every operand either a `Constant`, another
/// instruction within \p Blocks, a phi in \p AllowedPhis, or (if
/// \p AllowArgument is set) one of \p Blocks' enclosing function's own
/// parameters \p AllowArgument accepts -- the "pure, closed scalar
/// recurrence" `LoopShape`'s header condition and latch recurrence must be
/// (see its doc comment), or `BranchShape`'s header condition must be (see
/// its doc comment): safe to clone directly into the wrapper as ordinary,
/// once-per-iteration (or once-per-branch) scalar code, run outside any
/// per-wave loop.
bool isPureClosedChain(ArrayRef<BasicBlock *> Blocks,
                       ArrayRef<PHINode *> AllowedPhis,
                       function_ref<bool(Argument &)> AllowArgument = nullptr) {
  SmallPtrSet<Instruction *, 8> Local;
  for (BasicBlock *BB : Blocks)
    for (Instruction &I : *BB)
      if (!I.isTerminator())
        Local.insert(&I);
  for (BasicBlock *BB : Blocks) {
    for (Instruction &I : *BB) {
      // A phi's "operands" are really its predecessors' incoming edges,
      // not data flow within this chain -- `AllowedPhis` (the loop's own
      // induction variables) is what licenses referencing one at all, so
      // there is nothing further to check about the phi itself here.
      if (I.isTerminator() || isa<PHINode>(I))
        continue;
      if (I.mayHaveSideEffects())
        return false;
      for (Value *Op : I.operands()) {
        if (isa<Constant>(Op))
          continue;
        if (auto *OpI = dyn_cast<Instruction>(Op); OpI && Local.contains(OpI))
          continue;
        if (auto *PN = dyn_cast<PHINode>(Op);
            PN && is_contained(AllowedPhis, PN))
          continue;
        if (auto *Arg = dyn_cast<Argument>(Op);
            Arg && AllowArgument && AllowArgument(*Arg))
          continue;
        return false;
      }
    }
  }
  return true;
}

/// Recognizes \p F's shape as the header-tested loop `LoopShape` describes
/// (see its doc comment), or `std::nullopt` if it is not -- without
/// emitting any diagnostic: an unrecognized shape here just means the
/// existing straight-line `splitAtGroupSyncBarriers` path should be tried
/// (and will emit its own diagnostic if that fails too).
std::optional<LoopShape> matchLoopShape(Function &F) {
  LoopShape Shape;
  BasicBlock *H = walkLinearChain(&F.getEntryBlock(), Shape.PrefixOrder);
  if (!H)
    return std::nullopt;
  auto *HeaderBr = dyn_cast<CondBrInst>(H->getTerminator());
  if (!HeaderBr)
    return std::nullopt;
  Shape.Header = H;

  for (PHINode &PN : H->phis())
    Shape.Inductions.push_back({&PN, nullptr});
  if (Shape.Inductions.empty())
    return std::nullopt; // No loop-carried state: not this shape.

  SmallVector<PHINode *, 2> HeaderPhis;
  for (LoopInduction &Ind : Shape.Inductions)
    HeaderPhis.push_back(Ind.HeaderPhi);
  if (!isPureClosedChain({H}, HeaderPhis))
    return std::nullopt;

  // Figure out which successor continues the loop (its own chain reaches
  // a block that branches back to `H`) and which exits it. Walked
  // manually rather than via `walkLinearChain`, which doesn't know to stop
  // at the block that branches back to `H` -- it would otherwise keep
  // going straight through `H` itself.
  BasicBlock *Succ0 = HeaderBr->getSuccessor(0);
  BasicBlock *Succ1 = HeaderBr->getSuccessor(1);
  for (BasicBlock *BodyEntry : {Succ0, Succ1}) {
    BasicBlock *ExitCandidate = BodyEntry == Succ0 ? Succ1 : Succ0;
    SmallVector<BasicBlock *, 4> Body;
    SmallPtrSet<BasicBlock *, 8> Visited;
    BasicBlock *Cur = BodyEntry;
    BasicBlock *Latch = nullptr;
    while (Visited.insert(Cur).second) {
      Body.push_back(Cur);
      auto *Br = dyn_cast<UncondBrInst>(Cur->getTerminator());
      if (!Br)
        break; // Something other than a straight chain: not this shape.
      if (Br->getSuccessor(0) == H) {
        Latch = Cur;
        break;
      }
      Cur = Br->getSuccessor(0);
    }
    if (!Latch)
      continue;
    Shape.BodyOrder.assign(Body.begin(), std::prev(Body.end()));
    Shape.Latch = Latch;
    Shape.ExitBlock = ExitCandidate;
    break;
  }
  if (!Shape.Latch || Shape.BodyOrder.empty())
    return std::nullopt; // No separate region block, or shape mismatch.

  if (!isPureClosedChain({Shape.Latch}, HeaderPhis))
    return std::nullopt;

  for (LoopInduction &Ind : Shape.Inductions) {
    Value *Initial = Ind.HeaderPhi->getIncomingValueForBlock(
        Shape.PrefixOrder.empty() ? &F.getEntryBlock()
                                  : Shape.PrefixOrder.back());
    Ind.InitialValue = dyn_cast<Constant>(Initial);
    if (!Ind.InitialValue)
      return std::nullopt; // Only a compile-time-constant start is supported.
  }

  BasicBlock *SuffixEnd = walkLinearChain(Shape.ExitBlock, Shape.SuffixOrder);
  if (!SuffixEnd || !isa<ReturnInst>(SuffixEnd->getTerminator()))
    return std::nullopt;
  Shape.SuffixOrder.push_back(SuffixEnd);

  // Every block of `F` must belong to exactly one region: otherwise some
  // other block reaches this shape from elsewhere (a second predecessor
  // this match didn't account for), which is not a shape this milestone
  // supports.
  size_t Covered = Shape.PrefixOrder.size() + 1 /* Header */ +
                   Shape.BodyOrder.size() + 1 /* Latch */ +
                   Shape.SuffixOrder.size();
  if (Covered != F.size())
    return std::nullopt;

  return Shape;
}

/// One of `feme::cpu::WaveBodyEnv`'s (or `SIMDizePass`'s trailing
/// resource/root-constant parameters') own parameters that is the *same*
/// value for every wave of a group, not just every lane of one wave --
/// group id, the resource/sampler heap, and root constants -- as opposed
/// to a genuinely per-wave one (`wave_index`, `wave_entry_mask`,
/// `wave_groupshared`, `barrier_spill`, any `loopvarN`). A `BranchShape`
/// header condition referencing only these (see `matchBranchShape`) is
/// what makes cloning it into the wrapper as ordinary, once-per-group
/// scalar code (rather than once per wave) sound: a divergent branch a
/// barrier could survive inside has already been turned into masked data
/// flow by this point (`feme::cpu::LinearizePass`), so a *genuinely*
/// per-wave-varying condition here would mean this function's own
/// uniformity classification was wrong, not a shape this milestone should
/// try to support.
bool isUniformWaveBodyArgument(Argument &Arg) {
  StringRef Name = Arg.getName();
  return Name == "resource_heap" || Name == "resource_heap_count" ||
         Name == "sampler_heap" || Name == "sampler_heap_count" ||
         Name == "root_constants" || Name == "root_constant_size" ||
         Name == "image_heap" || Name == "image_heap_count" ||
         Name == "wave_group_id_x" || Name == "wave_group_id_y" ||
         Name == "wave_group_id_z";
}

/// The shape "Barrier inside a surviving branch" (see the file comment
/// above) supports: a uniform two-way branch -- \p Header's own condition
/// references only constants and `isUniformWaveBodyArgument`-accepted
/// parameters, verified pure/side-effect-free by `isPureClosedChain`, so
/// it is safe to compute once, scalar, in the wrapper -- whose two arms
/// are each a linear chain (\p TrueOrder/\p FalseOrder, empty for a plain
/// `if` with no `else`) that reconverge at \p MergeBlock without a phi of
/// its own (a value one arm computes differently from the other, needed
/// after the branch, is not yet supported -- see `buildWrapperForBranch`),
/// followed by a linear \p SuffixOrder to a `ret`. Every block of the
/// function belongs to exactly one of \p PrefixOrder/\p Header/
/// \p TrueOrder/\p FalseOrder/\p MergeBlock/\p SuffixOrder.
struct BranchShape {
  BasicBlock *Header;
  SmallVector<BasicBlock *, 4> PrefixOrder;
  SmallVector<BasicBlock *, 4> TrueOrder;
  SmallVector<BasicBlock *, 4> FalseOrder;
  BasicBlock *MergeBlock;
  SmallVector<BasicBlock *, 4> SuffixOrder;
};

/// Walks from \p Start following only single-successor unconditional
/// branches, appending every block visited to \p Order, stopping as soon
/// as a block with more than one predecessor is reached (the arms'
/// reconvergence point, per `BranchShape`'s doc comment) -- returned
/// without being added to \p Order -- or returning nullptr (a shape this
/// milestone doesn't support) if a cycle is found first or the chain ends
/// in anything other than an unconditional branch before reconverging.
BasicBlock *walkBranchArm(BasicBlock *Start,
                          SmallVectorImpl<BasicBlock *> &Order) {
  SmallPtrSet<BasicBlock *, 8> Visited;
  BasicBlock *BB = Start;
  while (true) {
    if (BB->hasNPredecessorsOrMore(2))
      return BB;
    if (!Visited.insert(BB).second)
      return nullptr;
    auto *Br = dyn_cast<UncondBrInst>(BB->getTerminator());
    if (!Br)
      return nullptr;
    Order.push_back(BB);
    BB = Br->getSuccessor(0);
  }
}

/// Recognizes \p F's shape as the uniform two-way branch `BranchShape`
/// describes (see its doc comment), or `std::nullopt` if it is not --
/// without emitting any diagnostic, exactly like `matchLoopShape`: an
/// unrecognized shape here just means the existing straight-line
/// `splitAtGroupSyncBarriers` path should be tried (and will emit its own
/// diagnostic if that fails too).
std::optional<BranchShape> matchBranchShape(Function &F) {
  BranchShape Shape;
  BasicBlock *H = walkLinearChain(&F.getEntryBlock(), Shape.PrefixOrder);
  if (!H)
    return std::nullopt;
  auto *HeaderBr = dyn_cast<CondBrInst>(H->getTerminator());
  if (!HeaderBr)
    return std::nullopt;
  Shape.Header = H;
  if (!isPureClosedChain({H}, {}, isUniformWaveBodyArgument))
    return std::nullopt;

  BasicBlock *TrueMerge =
      walkBranchArm(HeaderBr->getSuccessor(0), Shape.TrueOrder);
  BasicBlock *FalseMerge =
      walkBranchArm(HeaderBr->getSuccessor(1), Shape.FalseOrder);
  if (!TrueMerge || !FalseMerge || TrueMerge != FalseMerge)
    return std::nullopt;
  Shape.MergeBlock = TrueMerge;

  // A value one arm computes differently from the other, needed after the
  // branch, would show up here as a phi -- not yet supported (see
  // `BranchShape`'s doc comment).
  if (!Shape.MergeBlock->phis().empty())
    return std::nullopt;

  BasicBlock *SuffixEnd = walkLinearChain(Shape.MergeBlock, Shape.SuffixOrder);
  if (!SuffixEnd || !isa<ReturnInst>(SuffixEnd->getTerminator()))
    return std::nullopt;
  Shape.SuffixOrder.push_back(SuffixEnd);

  // Every block of `F` must belong to exactly one region: otherwise some
  // other block reaches this shape from elsewhere, which is not a shape
  // this milestone supports (see `matchLoopShape`'s identical check).
  size_t Covered = Shape.PrefixOrder.size() + 1 /* Header */ +
                   Shape.TrueOrder.size() + Shape.FalseOrder.size() +
                   Shape.SuffixOrder.size();
  if (Covered != F.size())
    return std::nullopt;

  return Shape;
}

/// into its own new function named \p WaveBody's name + \p NameSuffix,
/// splicing those blocks out of \p WaveBody and, unless \p EndsInRet
/// (the chain's last block already ends with a `ret`), replacing the
/// chain's trailing unconditional branch with one. Every reference to one
/// of \p WaveBody's own parameters is rewritten to the new function's
/// corresponding one (see `splitAtGroupSyncBarriers`'s identical rewrite).
Function *outlineChain(Function &WaveBody, ArrayRef<BasicBlock *> Chain,
                       const Twine &NameSuffix, bool EndsInRet) {
  Function *Fn =
      Function::Create(WaveBody.getFunctionType(), GlobalValue::InternalLinkage,
                       WaveBody.getAddressSpace(),
                       WaveBody.getName() + NameSuffix, WaveBody.getParent());
  Fn->copyAttributesFrom(&WaveBody);
  Fn->setComdat(WaveBody.getComdat());
  for (auto [OldArg, NewArg] : llvm::zip(WaveBody.args(), Fn->args()))
    NewArg.setName(OldArg.getName());

  Fn->splice(Fn->begin(), &WaveBody, Chain.front()->getIterator(),
             std::next(Chain.back()->getIterator()));

  if (!EndsInRet) {
    Instruction *Term = Fn->back().getTerminator();
    assert(isa<UncondBrInst>(Term));
    Term->eraseFromParent();
    IRBuilder<>(&Fn->back()).CreateRetVoid();
  }

  for (Instruction &I : instructions(*Fn))
    for (Use &U : I.operands())
      if (auto *Arg = dyn_cast<Argument>(U.get());
          Arg && Arg->getParent() == &WaveBody)
        U.set(Fn->getArg(Arg->getArgNo()));
  return Fn;
}

/// Splits \p BodyOrder (the loop-body portion of a `LoopShape`, not
/// including its `Latch`) at each `..._with_group_sync` barrier found
/// within it, exactly like `splitAtGroupSyncBarriers` does for a whole
/// straight-line function, but outlining every resulting chunk -- including
/// the last -- into its own new `.bodyN`-suffixed function (see
/// `outlineChain`) rather than reusing \p WaveBody's own identity for one
/// of them, since \p WaveBody has other blocks (its header/latch/prefix/
/// suffix) left to deal with once this returns.
SmallVector<Function *, 4>
splitLoopBodyAtBarriers(Function &WaveBody, ArrayRef<BasicBlock *> BodyOrder,
                        BasicBlock *Latch,
                        SmallVectorImpl<RegionBoundary> &Boundaries) {
  SmallVector<CallInst *, 4> Barriers;
  SmallVector<BarrierMemoryScope, 4> Scopes;
  for (BasicBlock *BB : BodyOrder)
    for (Instruction &I : *BB)
      if (auto *CI = dyn_cast<CallInst>(&I))
        if (std::optional<MatchedBarrier> Matched = matchBarrierCall(*CI);
            Matched && Matched->GroupSync) {
          Barriers.push_back(CI);
          Scopes.push_back(Matched->MemoryScope);
        }

  SmallPtrSet<BasicBlock *, 4> BoundaryBlocks;
  for (CallInst *Barrier : Barriers) {
    BasicBlock *After = SplitBlock(Barrier->getParent(), Barrier,
                                   static_cast<DominatorTree *>(nullptr));
    BoundaryBlocks.insert(After);
    Barrier->eraseFromParent();
  }
  for (BarrierMemoryScope Scope : Scopes)
    Boundaries.push_back({Scope});

  // Rebuild the (possibly now-longer, thanks to `SplitBlock`) chain from
  // its first block, stopping once we reach the block that now branches
  // to `Latch` -- the new final block of this chain.
  SmallVector<BasicBlock *, 8> PostSplitOrder;
  BasicBlock *Cur = BodyOrder.front();
  while (true) {
    PostSplitOrder.push_back(Cur);
    auto *Br = cast<UncondBrInst>(Cur->getTerminator());
    if (Br->getSuccessor(0) == Latch)
      break;
    Cur = Br->getSuccessor(0);
  }

  SmallVector<SmallVector<BasicBlock *, 8>, 4> RegionBlocks(1);
  for (BasicBlock *BB : PostSplitOrder) {
    if (BoundaryBlocks.contains(BB))
      RegionBlocks.emplace_back();
    RegionBlocks.back().push_back(BB);
  }

  SmallVector<Function *, 4> Regions;
  for (unsigned R = 0, E = RegionBlocks.size(); R != E; ++R)
    Regions.push_back(outlineChain(WaveBody, RegionBlocks[R],
                                   ".body" + Twine(R), /*EndsInRet=*/false));
  return Regions;
}

/// Builds the exported `feme_cpu_entry_<name>` wrapper for a wave body
/// matching `LoopShape` -- "Barriers inside a uniform loop" in the file
/// comment above. \p WaveBody's header/latch (pure, side-effect-free
/// scalar recurrence, verified by `matchLoopShape`) are cloned directly
/// into the wrapper as an ordinary scalar loop, run once per iteration
/// rather than once per wave; its prefix chain, each barrier-split body
/// region, and its suffix chain each become their own function, invoked
/// through the usual per-wave `buildWaveLoop`. Returns nullptr (having
/// emitted a diagnostic) if a body region's cross-barrier liveness is not
/// one this milestone's spilling supports.
Function *buildWrapperForLoop(Function &WaveBodyIn, LoopShape Shape,
                              unsigned WaveSize, uint32_t GroupSizeTotal,
                              uint32_t WavesPerGroup) {
  bool IsMesh = feme::getShaderStage(WaveBodyIn) == feme::ShaderStage::Mesh;
  Function *WaveBody = &WaveBodyIn;
  Module &M = *WaveBody->getParent();
  LLVMContext &Ctx = M.getContext();

  // Give the body chain's uses of each header induction phi their own
  // trailing `loopvarN` parameter (see the file comment's "Barriers inside
  // a uniform loop" case): the phi itself does not survive into any region
  // function, since `Shape.Header` is cloned into the wrapper, not
  // outlined.
  SmallVector<unsigned, 2> LoopVarArgNo;
  for (auto [N, Ind] : llvm::enumerate(Shape.Inductions)) {
    SmallVector<Use *, 4> UsesInBody;
    for (Use &U : Ind.HeaderPhi->uses())
      if (auto *UI = dyn_cast<Instruction>(U.getUser());
          UI && is_contained(Shape.BodyOrder, UI->getParent()))
        UsesInBody.push_back(&U);

    auto AppendResult = appendTrailingParam(*WaveBody, Ind.HeaderPhi->getType(),
                                            "loopvar" + Twine(N));
    WaveBody = AppendResult.first;
    Argument *NewArg = AppendResult.second;
    LoopVarArgNo.push_back(NewArg->getArgNo());
    for (Use *U : UsesInBody)
      U->set(NewArg);
  }
  // Refresh `Shape`'s pointers: `appendTrailingParam` moved every
  // instruction/block into a new function unchanged, so only `WaveBody`
  // itself (and the header/latch phi/instruction identities, unaffected by
  // that move) matter going forward -- `Shape.Header`/`Shape.Latch`/
  // `Shape.*Order`'s `BasicBlock*`s remain valid.

  SmallVector<CallInst *, 4> Barriers;
  DenseMap<Instruction *, unsigned> IndexOf;
  unsigned Idx = 0;
  for (BasicBlock *BB : Shape.BodyOrder) {
    for (Instruction &I : *BB) {
      IndexOf[&I] = Idx++;
      if (auto *CI = dyn_cast<CallInst>(&I))
        if (std::optional<MatchedBarrier> Matched = matchBarrierCall(*CI);
            Matched && Matched->GroupSync)
          Barriers.push_back(CI);
    }
  }
  StructType *SpillTy = nullptr;
  if (!spillValuesLiveAcrossBarriers(WaveBody, Shape.BodyOrder, Barriers,
                                     IndexOf, SpillTy))
    return nullptr;

  Function *PrefixFn = Shape.PrefixOrder.empty()
                           ? nullptr
                           : outlineChain(*WaveBody, Shape.PrefixOrder,
                                          ".prefix", /*EndsInRet=*/false);
  SmallVector<RegionBoundary, 4> Boundaries;
  SmallVector<Function *, 4> BodyRegions = splitLoopBodyAtBarriers(
      *WaveBody, Shape.BodyOrder, Shape.Latch, Boundaries);
  Function *SuffixFn = outlineChain(*WaveBody, Shape.SuffixOrder, ".suffix",
                                    /*EndsInRet=*/true);

  // `WaveBody` now contains only the (dead) header/latch; clone their
  // instructions directly into the wrapper below, then discard it.
  GroupSharedLayout GSLayout = computeGroupSharedLayout(M);
  StructType *ArgsTy = IsMesh ? getMeshArgsType(Ctx) : getDispatchArgsType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  std::string WrapperName = getEntrySymbolName(WaveBody->getName());
  FunctionType *WrapperTy =
      FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false);
  Function *Wrapper =
      Function::Create(WrapperTy, GlobalValue::ExternalLinkage, WrapperName, M);
  Argument *Args = Wrapper->getArg(0);
  Args->setName("args");

  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Wrapper);
  IRBuilder<> Entry(EntryBB);
  WrapperEnv WEnv = buildWrapperEnv(Entry, ArgsTy, Args, GSLayout, SpillTy,
                                   WavesPerGroup, IsMesh);

  // The prefix region's own wave loop runs before the wrapper's scalar
  // loop phi(s) exist; it never actually reads its `loopvarN` parameter
  // (only the loop body does), so a poison placeholder is safe there.
  SmallVector<Value *, 2> LoopScalars;
  for (LoopInduction &Ind : Shape.Inductions)
    LoopScalars.push_back(PoisonValue::get(Ind.HeaderPhi->getType()));

  BasicBlock *Pred = EntryBB;
  if (PrefixFn)
    Pred = buildWaveLoop(*Wrapper, Pred, *PrefixFn, WEnv, WaveSize,
                         GroupSizeTotal, WavesPerGroup, "", LoopScalars);

  BasicBlock *LoopHeaderBB = BasicBlock::Create(Ctx, "loop.header", Wrapper);
  BasicBlock *LoopBodyBB = BasicBlock::Create(Ctx, "loop.body.iter", Wrapper);
  BasicBlock *LoopLatchBB = BasicBlock::Create(Ctx, "loop.latch", Wrapper);
  BasicBlock *LoopExitBB = BasicBlock::Create(Ctx, "loop.exit", Wrapper);
  IRBuilder<>(Pred).CreateBr(LoopHeaderBB);

  // Clone the header's phis and its pure comparison directly as the
  // wrapper's own scalar loop condition (see the file comment's "Barriers
  // inside a uniform loop" case): this runs once per iteration, not once
  // per wave.
  IRBuilder<> LoopHeader(LoopHeaderBB);
  SmallVector<PHINode *, 2> WrapperPhis;
  for (auto [N, Ind] : llvm::enumerate(Shape.Inductions)) {
    PHINode *NewPhi =
        LoopHeader.CreatePHI(Ind.HeaderPhi->getType(), 2, "loopvar" + Twine(N));
    NewPhi->addIncoming(Ind.InitialValue, Pred);
    WrapperPhis.push_back(NewPhi);
    LoopScalars[N] = NewPhi;
  }
  ValueToValueMapTy HeaderMap;
  for (auto [Ind, NewPhi] : llvm::zip(Shape.Inductions, WrapperPhis))
    HeaderMap[Ind.HeaderPhi] = NewPhi;
  Instruction *ClonedCond = nullptr;
  for (Instruction &I : *Shape.Header) {
    if (isa<PHINode>(&I) || I.isTerminator())
      continue;
    Instruction *Clone = I.clone();
    RemapInstruction(Clone, HeaderMap, RF_IgnoreMissingLocals);
    LoopHeader.Insert(Clone);
    HeaderMap[&I] = Clone;
    ClonedCond = Clone;
  }
  auto *HeaderBr = cast<CondBrInst>(Shape.Header->getTerminator());
  Value *Cond = ClonedCond
                    ? static_cast<Value *>(ClonedCond)
                    : static_cast<Value *>(HeaderMap[HeaderBr->getCondition()]);
  bool BodyIsSucc0 = HeaderBr->getSuccessor(0) != Shape.ExitBlock;
  LoopHeader.CreateCondBr(Cond, BodyIsSucc0 ? LoopBodyBB : LoopExitBB,
                          BodyIsSucc0 ? LoopExitBB : LoopBodyBB);

  BasicBlock *BodyPred = LoopBodyBB;
  for (unsigned R = 0, E = BodyRegions.size(); R != E; ++R) {
    Twine Suffix = Twine(".body") + Twine(R);
    BasicBlock *ExitBB =
        buildWaveLoop(*Wrapper, BodyPred, *BodyRegions[R], WEnv, WaveSize,
                      GroupSizeTotal, WavesPerGroup, Suffix, LoopScalars);
    if (R + 1 != E) {
      IRBuilder<> ExitBuilder(ExitBB);
      ExitBuilder.CreateFence(AtomicOrdering::AcquireRelease,
                              syncScopeFor(Boundaries[R].MemoryScope));
    }
    BodyPred = ExitBB;
  }
  IRBuilder<>(BodyPred).CreateBr(LoopLatchBB);

  // Clone the latch's pure recurrence the same way as the header's
  // condition above, then close the wrapper's own backedge.
  IRBuilder<> LoopLatch(LoopLatchBB);
  for (Instruction &I : *Shape.Latch) {
    if (I.isTerminator())
      continue;
    Instruction *Clone = I.clone();
    RemapInstruction(Clone, HeaderMap, RF_IgnoreMissingLocals);
    LoopLatch.Insert(Clone);
    HeaderMap[&I] = Clone;
  }
  LoopLatch.CreateBr(LoopHeaderBB);
  for (auto [Ind, NewPhi] : llvm::zip(Shape.Inductions, WrapperPhis)) {
    Value *NextVal = Ind.HeaderPhi->getIncomingValueForBlock(Shape.Latch);
    // `NextVal` is either a `Constant` (used as-is) or an instruction this
    // loop's clone above already has a mapping for (a `PHINode` is also an
    // `Instruction`, so this covers the rare "another header phi feeds
    // this one directly" case too).
    if (auto *NextInst = dyn_cast<Instruction>(NextVal))
      NextVal = HeaderMap[NextInst];
    NewPhi->addIncoming(NextVal, LoopLatchBB);
  }

  BasicBlock *FinalBB =
      buildWaveLoop(*Wrapper, LoopExitBB, *SuffixFn, WEnv, WaveSize,
                    GroupSizeTotal, WavesPerGroup, "", LoopScalars);
  IRBuilder<>(FinalBB).CreateRetVoid();

  for (Function *Region : BodyRegions)
    Region->setLinkage(GlobalValue::InternalLinkage);
  if (PrefixFn)
    PrefixFn->setLinkage(GlobalValue::InternalLinkage);
  SuffixFn->setLinkage(GlobalValue::InternalLinkage);

  if (GSLayout.TotalSize != 0)
    for (auto &[GVConst, Offset] : GSLayout.Offsets) {
      auto *GV = const_cast<GlobalVariable *>(GVConst);
      if (GV->use_empty())
        GV->eraseFromParent();
    }

  WaveBody->eraseFromParent();
  return Wrapper;
}

/// Splits one arm (\p ArmOrder, possibly empty) of a `BranchShape` at each
/// `..._with_group_sync` barrier within it -- exactly like
/// `splitLoopBodyAtBarriers` does for a loop body, and reusing
/// `spillValuesLiveAcrossBarriers` for any value live across one of those
/// barriers *within this arm* -- outlining every resulting chunk into its
/// own new function suffixed \p NameSuffix + `N`. \p WaveBody is
/// reassigned if spilling recreated it (see `spillValuesLiveAcrossBarriers`);
/// returns an empty vector, having changed neither \p WaveBody nor
/// \p Boundaries, if \p ArmOrder is empty (no arm to run at all -- a plain
/// `if` with no `else`). Returns `std::nullopt` (having emitted a
/// diagnostic) if spilling failed, or if a value needed spilling at all:
/// `buildWrapperForBranch` allocates no spill buffer for a branch shape
/// today (each arm would need its own, which `buildWaveLoop`'s
/// argument-name dispatch cannot yet tell apart), so this is a deliberate
/// narrowing rather than a silent null-buffer miscompile.
std::optional<SmallVector<Function *, 4>>
splitArmAtBarriers(Function *&WaveBody, ArrayRef<BasicBlock *> ArmOrder,
                   BasicBlock *MergeBlock, const Twine &NameSuffix,
                   SmallVectorImpl<RegionBoundary> &Boundaries) {
  if (ArmOrder.empty())
    return SmallVector<Function *, 4>();

  SmallVector<CallInst *, 4> Barriers;
  SmallVector<BarrierMemoryScope, 4> Scopes;
  DenseMap<Instruction *, unsigned> IndexOf;
  unsigned Idx = 0;
  for (BasicBlock *BB : ArmOrder)
    for (Instruction &I : *BB) {
      IndexOf[&I] = Idx++;
      if (auto *CI = dyn_cast<CallInst>(&I))
        if (std::optional<MatchedBarrier> Matched = matchBarrierCall(*CI);
            Matched && Matched->GroupSync) {
          Barriers.push_back(CI);
          Scopes.push_back(Matched->MemoryScope);
        }
    }

  StructType *SpillTy = nullptr;
  if (!spillValuesLiveAcrossBarriers(WaveBody, ArmOrder, Barriers, IndexOf,
                                     SpillTy))
    return std::nullopt;
  if (SpillTy) {
    // `buildWrapperForBranch` never allocates a spill buffer for a branch
    // shape (each arm would need its own -- `buildWaveLoop`'s simple
    // argument-name dispatch has no way to tell one arm's `barrier_spill`
    // parameter from the other's, since both would need the same name to
    // match `spillValuesLiveAcrossBarriers`'s own convention), so a value
    // live across a barrier *within* a branch arm is diagnosed rather than
    // silently passed a null spill buffer.
    WaveBody->getContext().emitError(
        "feme-cpu-wrap-entry: function '" + WaveBody->getName() +
        "' has a value live across a group-sync barrier inside a branch "
        "arm; only a barrier inside a straight-line wave body or a "
        "uniform loop supports spilling for now (roadmap step R24 "
        "deviation)");
    return std::nullopt;
  }

  SmallPtrSet<BasicBlock *, 4> BoundaryBlocks;
  for (CallInst *Barrier : Barriers) {
    BasicBlock *After = SplitBlock(Barrier->getParent(), Barrier,
                                   static_cast<DominatorTree *>(nullptr));
    BoundaryBlocks.insert(After);
    Barrier->eraseFromParent();
  }
  for (BarrierMemoryScope Scope : Scopes)
    Boundaries.push_back({Scope});

  // The arm's chain may now be longer thanks to `SplitBlock`; rebuild it
  // from its own first block, stopping once a block's successor is the
  // (unchanged, since only a barrier's own source block was split) arm's
  // final target: `MergeBlock` (see `splitLoopBodyAtBarriers`'s identical
  // technique, stopping at `Latch` there instead).
  SmallVector<BasicBlock *, 8> PostSplitOrder;
  BasicBlock *Cur = ArmOrder.front();
  while (true) {
    PostSplitOrder.push_back(Cur);
    auto *Br = cast<UncondBrInst>(Cur->getTerminator());
    if (Br->getSuccessor(0) == MergeBlock)
      break;
    Cur = Br->getSuccessor(0);
  }

  SmallVector<SmallVector<BasicBlock *, 8>, 4> RegionBlocks(1);
  for (BasicBlock *BB : PostSplitOrder) {
    if (BoundaryBlocks.contains(BB))
      RegionBlocks.emplace_back();
    RegionBlocks.back().push_back(BB);
  }

  SmallVector<Function *, 4> Regions;
  for (unsigned R = 0, E = RegionBlocks.size(); R != E; ++R)
    Regions.push_back(outlineChain(*WaveBody, RegionBlocks[R],
                                   NameSuffix + Twine(R), /*EndsInRet=*/false));
  return Regions;
}

/// Builds the exported `feme_cpu_entry_<name>` wrapper for a wave body
/// matching `BranchShape` -- "Barrier inside a surviving branch" in the
/// file comment above. \p WaveBody's header condition (verified pure and
/// referencing only uniform parameters by `matchBranchShape`) is cloned
/// directly into the wrapper as an ordinary scalar `br`, run once for the
/// whole group rather than once per wave; each arm's barrier-split
/// regions (`splitArmAtBarriers`) run through the usual per-wave
/// `buildWaveLoop`, with the wrapper's own real control flow choosing
/// which arm's wave loops run at all. Returns nullptr (having emitted a
/// diagnostic) if either arm's cross-barrier liveness is not one this
/// milestone's spilling supports.
Function *buildWrapperForBranch(Function &WaveBodyIn, BranchShape Shape,
                                unsigned WaveSize, uint32_t GroupSizeTotal,
                                uint32_t WavesPerGroup) {
  bool IsMesh = feme::getShaderStage(WaveBodyIn) == feme::ShaderStage::Mesh;
  Function *WaveBody = &WaveBodyIn;
  Module &M = *WaveBody->getParent();
  LLVMContext &Ctx = M.getContext();

  Function *PrefixFn = Shape.PrefixOrder.empty()
                           ? nullptr
                           : outlineChain(*WaveBody, Shape.PrefixOrder,
                                          ".prefix", /*EndsInRet=*/false);

  SmallVector<RegionBoundary, 4> TrueBoundaries, FalseBoundaries;
  std::optional<SmallVector<Function *, 4>> TrueRegions =
      splitArmAtBarriers(WaveBody, Shape.TrueOrder, Shape.MergeBlock,
                         ".true.body", TrueBoundaries);
  if (!TrueRegions)
    return nullptr;
  std::optional<SmallVector<Function *, 4>> FalseRegions =
      splitArmAtBarriers(WaveBody, Shape.FalseOrder, Shape.MergeBlock,
                         ".false.body", FalseBoundaries);
  if (!FalseRegions)
    return nullptr;

  Function *SuffixFn = outlineChain(*WaveBody, Shape.SuffixOrder, ".suffix",
                                    /*EndsInRet=*/true);

  // `WaveBody` now contains only the (dead) header; clone its instructions
  // directly into the wrapper below, then discard it.
  GroupSharedLayout GSLayout = computeGroupSharedLayout(M);
  StructType *ArgsTy = IsMesh ? getMeshArgsType(Ctx) : getDispatchArgsType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  std::string WrapperName = getEntrySymbolName(WaveBody->getName());
  FunctionType *WrapperTy =
      FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false);
  Function *Wrapper =
      Function::Create(WrapperTy, GlobalValue::ExternalLinkage, WrapperName, M);
  Argument *Args = Wrapper->getArg(0);
  Args->setName("args");

  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Wrapper);
  IRBuilder<> Entry(EntryBB);
  WrapperEnv WEnv = buildWrapperEnv(Entry, ArgsTy, Args, GSLayout,
                                    /*SpillTy=*/nullptr, WavesPerGroup, IsMesh);

  BasicBlock *Pred = EntryBB;
  if (PrefixFn)
    Pred = buildWaveLoop(*Wrapper, Pred, *PrefixFn, WEnv, WaveSize,
                         GroupSizeTotal, WavesPerGroup, "");

  // Clone the header's own condition computation directly into the
  // wrapper: `matchBranchShape` verified it references only constants and
  // this wave body's own uniform parameters, mapped below to `WEnv` the
  // same way every region function's call site is (see `buildWaveLoop`),
  // so it is safe to run once, scalar, for the whole group.
  ValueToValueMapTy HeaderMap;
  for (Argument &Arg : WaveBody->args()) {
    StringRef Name = Arg.getName();
    Value *EnvVal = nullptr;
    if (Name == "resource_heap")
      EnvVal = WEnv.ResourceHeap;
    else if (Name == "resource_heap_count")
      EnvVal = WEnv.ResourceHeapCount;
    else if (Name == "sampler_heap")
      EnvVal = WEnv.SamplerHeap;
    else if (Name == "sampler_heap_count")
      EnvVal = WEnv.SamplerHeapCount;
    else if (Name == "root_constants")
      EnvVal = WEnv.RootConstants;
    else if (Name == "root_constant_size")
      EnvVal = WEnv.RootConstantSize;
    else if (Name == "image_heap")
      EnvVal = WEnv.ImageHeap;
    else if (Name == "image_heap_count")
      EnvVal = WEnv.ImageHeapCount;
    else if (Name == "wave_group_id_x")
      EnvVal = WEnv.GroupIDX;
    else if (Name == "wave_group_id_y")
      EnvVal = WEnv.GroupIDY;
    else if (Name == "wave_group_id_z")
      EnvVal = WEnv.GroupIDZ;
    if (EnvVal)
      HeaderMap[&Arg] = EnvVal;
  }

  BasicBlock *CondBB = BasicBlock::Create(Ctx, "branch.cond", Wrapper);
  IRBuilder<>(Pred).CreateBr(CondBB);
  IRBuilder<> CondBuilder(CondBB);
  Instruction *ClonedCond = nullptr;
  for (Instruction &I : *Shape.Header) {
    if (I.isTerminator())
      continue;
    Instruction *Clone = I.clone();
    RemapInstruction(Clone, HeaderMap, RF_IgnoreMissingLocals);
    CondBuilder.Insert(Clone);
    HeaderMap[&I] = Clone;
    ClonedCond = Clone;
  }
  auto *HeaderBr = cast<CondBrInst>(Shape.Header->getTerminator());
  Value *RawCond = HeaderBr->getCondition();
  Value *Cond = ClonedCond ? ClonedCond
                           : (isa<Constant>(RawCond)
                                  ? RawCond
                                  : static_cast<Value *>(HeaderMap[RawCond]));

  BasicBlock *TrueEntryBB = BasicBlock::Create(Ctx, "branch.true", Wrapper);
  BasicBlock *FalseEntryBB = BasicBlock::Create(Ctx, "branch.false", Wrapper);
  BasicBlock *MergeBB = BasicBlock::Create(Ctx, "branch.merge", Wrapper);
  CondBuilder.CreateCondBr(Cond, TrueEntryBB, FalseEntryBB);

  BasicBlock *TrueExit = TrueEntryBB;
  for (unsigned R = 0, E = TrueRegions->size(); R != E; ++R) {
    Twine Suffix = Twine(".true.body") + Twine(R);
    BasicBlock *ExitBB =
        buildWaveLoop(*Wrapper, TrueExit, *(*TrueRegions)[R], WEnv, WaveSize,
                      GroupSizeTotal, WavesPerGroup, Suffix);
    if (R + 1 != E) {
      IRBuilder<> ExitBuilder(ExitBB);
      ExitBuilder.CreateFence(AtomicOrdering::AcquireRelease,
                              syncScopeFor(TrueBoundaries[R].MemoryScope));
    }
    TrueExit = ExitBB;
  }
  IRBuilder<>(TrueExit).CreateBr(MergeBB);

  BasicBlock *FalseExit = FalseEntryBB;
  for (unsigned R = 0, E = FalseRegions->size(); R != E; ++R) {
    Twine Suffix = Twine(".false.body") + Twine(R);
    BasicBlock *ExitBB =
        buildWaveLoop(*Wrapper, FalseExit, *(*FalseRegions)[R], WEnv, WaveSize,
                      GroupSizeTotal, WavesPerGroup, Suffix);
    if (R + 1 != E) {
      IRBuilder<> ExitBuilder(ExitBB);
      ExitBuilder.CreateFence(AtomicOrdering::AcquireRelease,
                              syncScopeFor(FalseBoundaries[R].MemoryScope));
    }
    FalseExit = ExitBB;
  }
  IRBuilder<>(FalseExit).CreateBr(MergeBB);

  BasicBlock *FinalBB =
      buildWaveLoop(*Wrapper, MergeBB, *SuffixFn, WEnv, WaveSize,
                    GroupSizeTotal, WavesPerGroup, "");
  IRBuilder<>(FinalBB).CreateRetVoid();

  for (Function *Region : *TrueRegions)
    Region->setLinkage(GlobalValue::InternalLinkage);
  for (Function *Region : *FalseRegions)
    Region->setLinkage(GlobalValue::InternalLinkage);
  if (PrefixFn)
    PrefixFn->setLinkage(GlobalValue::InternalLinkage);
  SuffixFn->setLinkage(GlobalValue::InternalLinkage);

  if (GSLayout.TotalSize != 0)
    for (auto &[GVConst, Offset] : GSLayout.Offsets) {
      auto *GV = const_cast<GlobalVariable *>(GVConst);
      if (GV->use_empty())
        GV->eraseFromParent();
    }

  WaveBody->eraseFromParent();
  return Wrapper;
}

/// Builds the exported `feme_cpu_entry_<name>` wrapper around \p WaveBodyIn
/// (see the file comment above), or nullptr if \p WaveBodyIn was not widened
/// by `feme::cpu::SIMDizePass` (has no `WaveBodyEnv`) -- nothing for this
/// pass to wrap in that case -- or if barrier splitting failed (a
/// diagnostic has already been emitted).
Function *buildWrapper(Function &WaveBodyIn) {
  bool IsMesh = feme::getShaderStage(WaveBodyIn) == feme::ShaderStage::Mesh;
  std::optional<WaveBodyEnv> Env = getWaveBodyEnv(WaveBodyIn);
  if (!Env)
    return nullptr;

  Function *WaveBody = &WaveBodyIn;
  Module &M = *WaveBody->getParent();
  LLVMContext &Ctx = M.getContext();
  unsigned WaveSize =
      cast<FixedVectorType>(Env->EntryMask->getType())->getNumElements();
  std::array<uint32_t, 3> NumThreads = getThreadGroupSize(*WaveBody);
  uint32_t GroupSizeTotal = NumThreads[0] * NumThreads[1] * NumThreads[2];
  uint32_t WavesPerGroup =
      GroupSizeTotal == 0 ? 0 : (GroupSizeTotal + WaveSize - 1) / WaveSize;

  replaceMemoryOnlyBarriers(*WaveBody);

  SmallVector<RegionBoundary, 4> Boundaries;
  SmallVector<Function *, 4> Regions;
  StructType *SpillTy = nullptr;
  bool HasGroupSyncBarrier =
      any_of(instructions(*WaveBody), [](Instruction &I) {
        auto *CI = dyn_cast<CallInst>(&I);
        if (!CI)
          return false;
        std::optional<MatchedBarrier> Matched = matchBarrierCall(*CI);
        return Matched && Matched->GroupSync;
      });
  if (HasGroupSyncBarrier) {
    if (std::optional<LoopShape> Shape = matchLoopShape(*WaveBody))
      return buildWrapperForLoop(*WaveBody, *Shape, WaveSize, GroupSizeTotal,
                                 WavesPerGroup);
    if (std::optional<BranchShape> Shape = matchBranchShape(*WaveBody))
      return buildWrapperForBranch(*WaveBody, *Shape, WaveSize, GroupSizeTotal,
                                   WavesPerGroup);
    std::optional<SmallVector<Function *, 4>> Split =
        splitAtGroupSyncBarriers(WaveBody, Boundaries, SpillTy);
    if (!Split)
      return nullptr;
    Regions = std::move(*Split);
  } else {
    Regions.push_back(WaveBody);
  }

  GroupSharedLayout GSLayout = computeGroupSharedLayout(M);

  StructType *ArgsTy = IsMesh ? getMeshArgsType(Ctx) : getDispatchArgsType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);

  std::string WrapperName = getEntrySymbolName(WaveBody->getName());
  FunctionType *WrapperTy =
      FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false);
  Function *Wrapper =
      Function::Create(WrapperTy, GlobalValue::ExternalLinkage, WrapperName, M);
  Argument *Args = Wrapper->getArg(0);
  Args->setName("args");

  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Wrapper);
  IRBuilder<> Entry(EntryBB);
  WrapperEnv WEnv = buildWrapperEnv(Entry, ArgsTy, Args, GSLayout, SpillTy,
                                   WavesPerGroup, IsMesh);

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
    if (!F.isDeclaration() && feme::isShaderEntryPoint(F))
      Candidates.push_back(&F);

  for (Function *F : Candidates)
    if (buildWrapper(*F))
      Changed = true;

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
