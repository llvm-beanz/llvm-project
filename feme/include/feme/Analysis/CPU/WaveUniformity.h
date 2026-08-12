//===- WaveUniformity.h - CPU target Phase 2: uniformity analysis -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares "Phase 2: Uniformity Analysis" from
// feme/docs/FeMeCPUDesign.md: which values in a raised shader are
// lane-varying (divergent) across a wave, and which are uniform. LLVM's
// `llvm::UniformityInfo` already implements this analysis in full (including
// sync dependence); it is driven entirely through `TargetTransformInfo`'s
// `hasBranchDivergence()`/`getValueUniformity()` hooks, which neither the
// `DirectX` nor the `SPIRV` target implements and which the host CPU target
// answers "no divergence" for. `WaveTTIImpl` supplies FeMe's own answers,
// describing the SPMD execution model a raised shader runs under
// (independent of the host it will eventually run on): every raised shader
// has branch divergence, and the lane-varying builtins FeMe raises
// (`llvm.{dx,spv}.thread.id`, ..., `WavePrefix*`) are its sources.
//
// This is an analysis only -- it produces no IR and is not retained across
// "Phase 3: Linearization", which recomputes uniformity over the linearized
// function instead (see the Roadmap / Milestones section: this is milestone
// 2, "no transform yet").
//
//===----------------------------------------------------------------------===//

#ifndef FEME_ANALYSIS_CPU_WAVEUNIFORMITY_H
#define FEME_ANALYSIS_CPU_WAVEUNIFORMITY_H

#include "llvm/ADT/GenericUniformityInfo.h"
#include "llvm/Analysis/TargetTransformInfoImpl.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/SSAContext.h"

namespace llvm {
class CycleInfo;
class DominatorTree;
} // namespace llvm

namespace feme::cpu {

/// A `TargetTransformInfo` implementation describing the SPMD execution
/// model of a raised shader, independent of the host it will run on: every
/// function has branch divergence, and the lane-varying builtins FeMe raises
/// (thread/lane indices, `WavePrefix*`) are the analysis's sources of
/// divergence. See the file comment above.
class WaveTTIImpl : public llvm::TargetTransformInfoImplBase {
public:
  explicit WaveTTIImpl(const llvm::DataLayout &DL)
      : llvm::TargetTransformInfoImplBase(DL) {}

  /// A raised shader is always an SPMD program under this model: even a
  /// shader with no branches at all still has per-lane-varying values (e.g.
  /// `WaveGetLaneIndex()`), so `UniformityInfo::compute()` always has
  /// something to do.
  bool hasBranchDivergence(const llvm::Function * = nullptr) const override {
    return true;
  }

  /// See the file comment above for the classification this implements:
  /// lane-index/id builtins and `WavePrefix*` are never uniform; wave-wide
  /// reductions and broadcasts (`WaveActive*`, `WaveReadLaneAt`) are always
  /// uniform; everything else is uniform iff its operands are.
  llvm::ValueUniformity getValueUniformity(const llvm::Value *V) const override;
};

/// Computes `UniformityInfo` for \p F under the SPMD model `WaveTTIImpl`
/// describes.
llvm::UniformityInfo computeWaveUniformity(llvm::Function &F,
                                           llvm::DominatorTree &DT,
                                           llvm::CycleInfo &CI);

/// A `FunctionAnalysisManager` pass computing `computeWaveUniformity`'s
/// result for a function, so it can be queried by later CPU-pipeline passes
/// (and printed by `WaveUniformityPrinterPass` below) the same way any other
/// function analysis is.
class WaveUniformityAnalysis
    : public llvm::AnalysisInfoMixin<WaveUniformityAnalysis> {
  friend llvm::AnalysisInfoMixin<WaveUniformityAnalysis>;
  static llvm::AnalysisKey Key;

public:
  using Result = llvm::UniformityInfo;

  Result run(llvm::Function &F, llvm::FunctionAnalysisManager &AM);
};

/// Prints `WaveUniformityAnalysis`'s result for each function, in the same
/// format `llvm::UniformityInfoPrinterPass` uses upstream, so `lit` tests can
/// check it the same way `print<uniformity>` tests do (see the "Phase 2"
/// section of feme/docs/FeMeCPUDesign.md).
class WaveUniformityPrinterPass
    : public llvm::PassInfoMixin<WaveUniformityPrinterPass> {
  llvm::raw_ostream &OS;

public:
  explicit WaveUniformityPrinterPass(llvm::raw_ostream &OS) : OS(OS) {}

  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &AM);

  static llvm::StringRef name() { return "print<feme-cpu-uniformity>"; }
};

} // namespace feme::cpu

#endif // FEME_ANALYSIS_CPU_WAVEUNIFORMITY_H
