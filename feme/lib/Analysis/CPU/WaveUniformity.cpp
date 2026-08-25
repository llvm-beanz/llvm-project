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
        return ValueUniformity::NeverUniform;
      case StageOpKind::OutputStore:
      case StageOpKind::Discard:
      case StageOpKind::Demote:
      case StageOpKind::StreamEmit:
      case StageOpKind::StreamCut:
      case StageOpKind::NumStageOpKinds:
        break;
      }
    }
  }

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
