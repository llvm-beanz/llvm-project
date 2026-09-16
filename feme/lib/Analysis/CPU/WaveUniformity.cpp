//===- WaveUniformity.cpp - CPU target Phase 2: uniformity analysis -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Analysis/CPU/WaveUniformity.h"

#include "feme/Core/StageOps.h"
#include "llvm/ADT/GenericUniformityImpl.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"

using namespace llvm;

namespace feme::cpu {

ValueUniformity WaveTTIImpl::getValueUniformity(const Value *V) const {
  // `feme.cpu.mask.any` (see "Mask representation between phases" in
  // feme/docs/FeMeCPUDesign.md, and `feme::cpu::getOrInsertMaskAny` in
  // Transforms/CPU/MaskIntrinsics.h, which this analysis library cannot
  // depend on without an include cycle -- Transforms/CPU already depends on
  // Analysis/CPU) is an ordinary `CallInst`, not an `IntrinsicInst`, so it
  // needs its own name-based check ahead of the intrinsic switch below. It
  // is always uniform regardless of its (necessarily divergent -- it is
  // only ever called on a per-lane "active" mask) operand: it stands in for
  // a cross-lane reduction that is meaningless before Phase 4 widens it, and
  // is defined to produce the same answer on every lane once it does
  // (`feme::cpu::SIMDizePass` lowers it to `llvm.vector.reduce.or`).
  // Classifying it here is what lets a Phase 3 loop's mask-gated backedge
  // (`feme::cpu::LinearizePass`'s `LoopLinearizer`) be recognized as a
  // uniform branch, which `feme::cpu::SIMDizePass`'s widening of that loop
  // (roadmap milestone 7) depends on.
  if (const auto *CI = dyn_cast<CallInst>(V)) {
    const Function *Callee = CI->getCalledFunction();
    if (Callee && Callee->getName() == "feme.cpu.mask.any")
      return ValueUniformity::AlwaysUniform;

    // `feme.cpu.masked.atomicrmw.*` (see `feme::cpu::matchMaskedAtomicRMW`,
    // Transforms/CPU/MaskIntrinsics.h -- again matched by name, for the
    // same include-cycle reason as `feme.cpu.mask.any` above) stands in
    // for one genuine, per-lane atomic read-modify-write: unlike an
    // ordinary call, whose result is uniform whenever every operand is
    // (this call's own operands -- a uniform pointer, value and mask are
    // the common case, e.g. every lane racing to increment one shared
    // counter) its result is a *different* value on every lane by
    // construction, since each lane's real atomic op observes whatever the
    // memory location holds at that lane's own turn (dispatch is
    // sequential -- see "Dispatch is sequential, not thread-pooled" in
    // feme/docs/Roadmap.md's §1.6), not one shared answer every lane
    // agrees on. Leaving this at `Default` let the generic operand-driven
    // rule conclude "uniform" whenever every operand happened to be
    // (exactly the case a real CTS mesh-shader "allocate a unique output
    // slot via an atomic increment" pattern hits), which left this call's
    // *consumers* -- e.g. an `icmp`/`br` deciding whether *this* lane is
    // one of the first `N` to get a slot -- unwidened too, so
    // `feme::cpu::FunctionWidener` never gave them a real per-lane value
    // to read and its final "sever remaining uses of an erased
    // instruction" fallback silently substituted `poison` for the read
    // instead (`FunctionWidener::widen`'s last loop) -- a real, load-
    // bearing branch condition that is later provably always `poison`,
    // which one of this pipeline's own middle-end passes (`IPSCCPPass`)
    // then legitimately folds down to `unreachable`, producing a compiled
    // stage with no code at all (roadmap milestone L41's JIT-link crash).
    if (Callee &&
        Callee->getName().starts_with("feme.cpu.masked.atomicrmw."))
      return ValueUniformity::NeverUniform;

    // (Roadmap H152) The resource-heap atomic runtime-call family
    // (`feme.cpu.resource.atomic.*.{raw,typed}.*`/
    // `feme.cpu.image.atomic.*.*`, emitted by `feme::cpu::
    // SPIRVResourceLoweringPass` for every `RWStructuredBuffer`/
    // `RWByteAddressBuffer`/`RWBuffer`/`RWTexture*`-destination
    // `InterlockedAdd`/`InterlockedCompareExchange`/etc, as opposed to a
    // groupshared destination's plain `AtomicRMWInst`/
    // `AtomicCmpXchgInst`) needs the exact same `NeverUniform` treatment
    // as `feme.cpu.masked.atomicrmw.*`/`AtomicRMWInst` above, for the
    // identical reason: dispatch is sequential, so each lane's own real
    // atomic op observes whatever the resource holds at that lane's own
    // turn, not one shared answer every lane agrees on, regardless of how
    // uniform this call's own operands (heap handle, binding, offset,
    // compare/exchange values) happen to be. Found via
    // `Feature/HLSLLib/InterlockedCompareExchange.resources.32.test`:
    // left at the generic operand-driven `Default` rule, a call whose
    // every operand is uniform (a compile-time-constant compare/exchange
    // value, exactly this test's own `InterlockedCompareExchange(SBufU[1],
    // 10u, 0u, OrigSBufU)` shape) was wrongly classified uniform, so
    // `feme::cpu::DiamondFlattener` left a real, later branch on this
    // call's result (part of a short-circuit `&&`-chain building
    // `OutOrig`) un-flattened, believing (wrongly) it was already
    // uniform -- surfacing only later as `feme-cpu-simdize`'s own,
    // separately-recomputed uniformity analysis correctly flagged that
    // same branch as genuinely divergent, with no real value to widen it
    // into (the branch itself, not just one consumer, having escaped
    // linearization).
    if (Callee &&
        (Callee->getName().starts_with("feme.cpu.resource.atomic.") ||
         Callee->getName().starts_with("feme.cpu.image.atomic.")))
      return ValueUniformity::NeverUniform;

    StageOpKind Kind;
    if (Callee && feme::isStageOpCall(*CI, &Kind)) {
      switch (Kind) {
      case StageOpKind::InputLoad:
      case StageOpKind::IsHelper:
      case StageOpKind::DerivativeXFine:
      case StageOpKind::DerivativeYFine:
      case StageOpKind::DerivativeXCoarse:
      case StageOpKind::DerivativeYCoarse:
      case StageOpKind::QuadRead:
      case StageOpKind::InterpolateAtCentroid:
      case StageOpKind::InterpolateAtSample:
      case StageOpKind::InterpolateAtOffset:
      case StageOpKind::SubpassLoad:
      case StageOpKind::TaskPayloadLoad:
        return ValueUniformity::NeverUniform;
      case StageOpKind::OutputStore:
      case StageOpKind::Discard:
      case StageOpKind::Demote:
      case StageOpKind::StreamEmit:
      case StageOpKind::StreamCut:
      case StageOpKind::TaskPayloadStore:
      case StageOpKind::SetMeshOutputs:
      case StageOpKind::EmitMeshTasks:
      case StageOpKind::NumStageOpKinds:
        break;
      }
    }
  }

  // Roadmap L43: a plain, not-yet-lowered `llvm::AtomicRMWInst` needs the
  // exact same `NeverUniform` treatment as the `feme.cpu.masked.
  // atomicrmw.*` call form above, and for the identical reason -- but
  // `feme::cpu::UniformityInfo` is computed once, up front, in
  // `feme::cpu::LinearizePass::run`, strictly *before* `DiamondFlattener`'s
  // `applyStageMasks` ever converts a real `atomicrmw` into that masked
  // call form (see `Transforms/CPU/Linearize.cpp`). So the very first
  // divergent branch a real CTS shader's own "allocate a unique output
  // slot" pattern produces is still, at the point this analysis actually
  // runs, guarding a plain `atomicrmw`'s result, not yet the masked call
  // -- leaving this case unhandled reintroduces the exact same
  // wrongly-uniform misclassification the masked-call case above was
  // added to fix, just one step earlier in the pipeline (a divergent
  // branch `feme::cpu::DiamondFlattener` then never actually flattens,
  // instead silently reusing whatever mask was already in scope and
  // leaving the real branch in place for `feme::cpu::SIMDizePass` to
  // reject later as an unremoved divergent branch).
  if (isa<AtomicRMWInst>(V))
    return ValueUniformity::NeverUniform;

  // Roadmap H164: a plain, not-yet-lowered `llvm::AtomicCmpXchgInst`
  // (HLSL's groupshared-destination `InterlockedCompareExchange`, still in
  // its raw two-result-struct form at the point this analysis runs) needs
  // the exact same `NeverUniform` treatment as the plain `AtomicRMWInst`
  // case immediately above, and for the identical reason: dispatch is
  // sequential, so each lane's own real compare-exchange observes whatever
  // the destination holds at that lane's own turn, not one shared answer
  // every lane agrees on -- regardless of how uniform this instruction's
  // own operands (pointer, compare value, new value) happen to be. Left
  // unhandled, a real `Feature/HLSLLib/InterlockedCompareExchange.32.test`
  // shape -- a short-circuit `||`/`&&` chain branching on an
  // `extractvalue` of this instruction's `{ iN, i1 }` result (e.g.
  // `OrigMatchInt == -5 || OrigMatchInt == 42`) -- was wrongly classified
  // uniform by the generic operand-driven rule, so `feme::cpu::
  // DiamondFlattener` never flattened the real, per-lane-divergent branch;
  // `feme::cpu::SIMDizePass`'s own, separately-recomputed uniformity
  // analysis then correctly rejected it as divergent with no widened value
  // to give it, and `FunctionWidener::widen`'s "sever remaining uses of an
  // erased instruction" fallback silently substituted `poison` for every
  // `extractvalue` of the (by-then-erased) scalar `cmpxchg`, so the branch
  // condition became provably `poison` -- undefined behaviour that
  // surfaced as a JIT-compiled stage segfaulting with no usable stack.
  if (isa<AtomicCmpXchgInst>(V))
    return ValueUniformity::NeverUniform;

  const auto *II = dyn_cast<IntrinsicInst>(V);
  if (!II)
    return ValueUniformity::Default;

  switch (II->getIntrinsicID()) {
  // Every invocation in a group observes a different id/index by
  // construction, and `WavePrefix*` reduces over "lanes before mine", which
  // is different for every lane -- see "Phase 2: Uniformity Analysis" in
  // feme/docs/FeMeCPUDesign.md. No fixed-point iteration can ever prove
  // these uniform.
  case Intrinsic::dx_thread_id:
  case Intrinsic::spv_thread_id:
  case Intrinsic::dx_thread_id_in_group:
  case Intrinsic::spv_thread_id_in_group:
  case Intrinsic::dx_flattened_thread_id_in_group:
  case Intrinsic::spv_flattened_thread_id_in_group:
  case Intrinsic::dx_wave_getlaneindex:
  // (roadmap L7s) `llvm.spv.subgroup.local.invocation.id` is
  // `dx_wave_getlaneindex`'s exact SPIR-V-sourced twin (both classify as
  // `feme::cpu::BuiltinCallKind::LaneIndex` in `SIMDize.cpp`'s own
  // `classifyBuiltin`, and both are unconditionally widened by
  // `FunctionWidener::widenInstruction`'s call-shape dispatch regardless
  // of this analysis's own verdict) -- but this case was missing here,
  // even though its DXIL sibling has always been present. Left at the
  // conservative `Default` classification, a purely-arithmetic consumer
  // of this call's own scalar result (e.g. `gl_SubgroupInvocationID % 32`,
  // computing which bit of a manual ballot to set) could still be judged
  // uniform by the generic operand-based analysis below, since this
  // intrinsic itself has no operands to propagate divergence from -- left
  // unwidened as a result, even though `widenInstruction` had already
  // unconditionally replaced the call it reads with a genuinely per-lane
  // vector value, leaving it referencing a poisoned, since-erased operand
  // once widening finished. A real `dEQP-VK.subgroups.basic.compute.
  // subgroupelect` reduction found this producing entirely wrong runtime
  // ballot/popcount results (not a crash: `urem`/`udiv`/`shl` are all
  // well-defined over `poison`, so nothing failed loudly).
  case Intrinsic::spv_subgroup_local_invocation_id:
  case Intrinsic::dx_wave_is_first_lane:
  case Intrinsic::spv_wave_is_first_lane:
  case Intrinsic::dx_wave_prefix_bit_count:
  case Intrinsic::dx_wave_prefix_sum:
  case Intrinsic::dx_wave_prefix_usum:
  case Intrinsic::dx_wave_prefix_product:
  case Intrinsic::dx_wave_prefix_uproduct:
  case Intrinsic::spv_wave_prefix_sum:
  case Intrinsic::spv_wave_prefix_product:
    return ValueUniformity::NeverUniform;

  // `WaveActive*` reductions and DXIL's `WaveReadLaneAt` are defined to
  // reduce/broadcast over exactly the `W` lanes of the wave, honouring the
  // active mask -- see "Wave size semantics" in feme/docs/FeMeCPUDesign.md
  // -- so their result is by definition the same on every lane. HLSL's
  // language rule requires `WaveReadLaneAt`'s lane-index operand to be
  // dynamically uniform, so `dx_wave_readlane` keeps this classification
  // regardless of its *value* operand's own divergence (the common case:
  // reading one, uniformly-selected lane's otherwise-divergent data and
  // broadcasting it is itself a uniform result, e.g. `combined.hlsl`'s
  // `WaveReadLaneAt(sum, 0)` where `sum` is a divergent per-lane
  // accumulation). `spv_wave_readlane` is deliberately excluded: SPIR-V's
  // broader `OpGroupNonUniformShuffle` semantics permit a genuinely varying
  // index, in which case the result differs per lane (see
  // `WaveCallKind::ReadLane`'s comment in WaveCalls.h) -- it is left at
  // `Default`, so the generic operand-divergence rule applies instead
  // (conservative: divergent whenever either operand is, including a
  // divergent value read through a uniform index, which is stricter than
  // necessary but never unsound).
  case Intrinsic::dx_wave_readlane:
  case Intrinsic::dx_wave_get_lane_count:
  case Intrinsic::spv_wave_get_lane_count:
  case Intrinsic::dx_wave_any:
  case Intrinsic::spv_wave_any:
  case Intrinsic::dx_wave_all:
  case Intrinsic::spv_wave_all:
  case Intrinsic::dx_wave_all_equal:
  case Intrinsic::spv_wave_all_equal:
  case Intrinsic::dx_wave_active_countbits:
  case Intrinsic::spv_wave_active_countbits:
  case Intrinsic::dx_wave_reduce_or:
  case Intrinsic::spv_wave_reduce_or:
  case Intrinsic::dx_wave_reduce_xor:
  case Intrinsic::spv_wave_reduce_xor:
  case Intrinsic::dx_wave_reduce_and:
  case Intrinsic::spv_wave_reduce_and:
  case Intrinsic::dx_wave_reduce_max:
  case Intrinsic::spv_wave_reduce_max:
  case Intrinsic::dx_wave_reduce_umax:
  case Intrinsic::spv_wave_reduce_umax:
  case Intrinsic::dx_wave_reduce_min:
  case Intrinsic::spv_wave_reduce_min:
  case Intrinsic::dx_wave_reduce_umin:
  case Intrinsic::spv_wave_reduce_umin:
  case Intrinsic::dx_wave_reduce_sum:
  case Intrinsic::spv_wave_reduce_sum:
  case Intrinsic::dx_wave_reduce_usum:
  case Intrinsic::dx_wave_product:
  case Intrinsic::spv_wave_product:
  case Intrinsic::dx_wave_uproduct:
  case Intrinsic::dx_wave_ballot:
  // (Roadmap L85) `llvm.spv.subgroup.ballot`'s own DXIL-origin
  // counterpart, `Intrinsic::dx_wave_ballot` above, was already listed
  // here, but its SPIR-V-origin twin was missed when
  // `classifyWaveCall`/`SPIRVToLLVMPatterns.cpp`'s `BallotConversionPattern`
  // first wired it up (roadmap L85): a `spirv.GroupNonUniformBallot`
  // result, exactly like `WaveActiveBallot`'s, is by definition the
  // identical whole-subgroup mask on every lane, so it must be classified
  // `AlwaysUniform` here too, not left at the generic operand-driven
  // `Default` rule (which would otherwise mark it divergent whenever its
  // predicate operand is, the common case, causing `feme::cpu::SIMDizePass`
  // to wrongly try to decompose/widen it lane-by-lane downstream).
  case Intrinsic::spv_subgroup_ballot:
    return ValueUniformity::AlwaysUniform;

  // A group-sync/memory barrier (`feme::cpu::matchBarrierCall`,
  // Transforms/CPU/BarrierCalls.h) is, by the source languages' own rule,
  // only ever reached by every invocation in the group or by none --
  // reaching one from divergent control flow is undefined behaviour in
  // both DXIL and SPIR-V. Its own call site is therefore always uniform
  // regardless of which (reconverged) block it sits in, which is what
  // roadmap step R5's "barrier inside a uniform loop" case needs: a
  // barrier immediately following a divergent `if`'s join point (a common
  // reduction-loop shape) must not be scalarized as though it were itself
  // divergent (`feme::cpu::FunctionWidener::widenScalarizedFallback` would
  // otherwise try to name-and-widen a `void`-typed call and assert).
  case Intrinsic::dx_group_memory_barrier:
  case Intrinsic::spv_group_memory_barrier:
  case Intrinsic::dx_group_memory_barrier_with_group_sync:
  case Intrinsic::spv_group_memory_barrier_with_group_sync:
  case Intrinsic::dx_device_memory_barrier:
  case Intrinsic::spv_device_memory_barrier:
  case Intrinsic::dx_device_memory_barrier_with_group_sync:
  case Intrinsic::spv_device_memory_barrier_with_group_sync:
  case Intrinsic::dx_all_memory_barrier:
  case Intrinsic::spv_all_memory_barrier:
  case Intrinsic::dx_all_memory_barrier_with_group_sync:
  case Intrinsic::spv_all_memory_barrier_with_group_sync:
    return ValueUniformity::AlwaysUniform;

  default:
    return ValueUniformity::Default;
  }
}

UniformityInfo computeWaveUniformity(Function &F, DominatorTree &DT,
                                     CycleInfo &CI) {
  TargetTransformInfo TTI(std::make_unique<WaveTTIImpl>(F.getDataLayout()));
  UniformityInfo UI(DT, CI, &TTI);
  UI.compute();
  return UI;
}

AnalysisKey WaveUniformityAnalysis::Key;

WaveUniformityAnalysis::Result
WaveUniformityAnalysis::run(Function &F, FunctionAnalysisManager &AM) {
  DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
  CycleInfo &CI = AM.getResult<CycleAnalysis>(F);
  return computeWaveUniformity(F, DT, CI);
}

PreservedAnalyses WaveUniformityPrinterPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  OS << "WaveUniformityInfo for function '" << F.getName() << "':\n";
  AM.getResult<WaveUniformityAnalysis>(F).print(OS);
  return PreservedAnalyses::all();
}

} // namespace feme::cpu
