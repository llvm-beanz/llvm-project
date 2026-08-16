//===- SIMDize.h - CPU target Phase 4: widening -------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::SIMDizePass, "Phase 4: Widening" in
// feme/docs/FeMeCPUDesign.md: widening a linearized, masked wave body into
// `<W x T>` vector IR for the resolved wave size `W` (see
// feme::cpu::resolveWaveSize).
//
// Roadmap milestone 4 implements this pass for straight-line,
// uniform-control-flow shaders (acyclic CFGs with no divergent branch): see
// SIMDize.cpp's file comment for the widening algorithm. Masked memory ops,
// loops, and the remaining wave sizes' scalarization fallback are
// milestone 7.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_SIMDIZE_H
#define FEME_TRANSFORMS_CPU_SIMDIZE_H

#include "llvm/IR/PassManager.h"

#include <optional>

namespace llvm {
class Function;
class Value;
} // namespace llvm

namespace feme::cpu {

/// The wave-body parameters `feme::cpu::SIMDizePass` appends to a widened
/// function's signature (see "Wave-body interface" in "Phase 4: Widening"):
/// the group id, wave index, entry mask and groupshared pointer that
/// `feme::cpu::WaveLoweringPass` (Phase 5) and `feme::cpu::EntryWrapperPass`
/// (Phase 6) both need to find again after widening has already run.
/// Recovered by name (see `getWaveBodyEnv`) rather than by fixed argument
/// position, since the resource/root-constant parameters
/// `feme::cpu::ResourceLoweringPass` may already have appended come first.
struct WaveBodyEnv {
  llvm::Value *GroupIDX = nullptr;
  llvm::Value *GroupIDY = nullptr;
  llvm::Value *GroupIDZ = nullptr;
  llvm::Value *WaveIndex = nullptr;
  /// `<W x i1>`: which lanes of this wave are active/live (see "Mask
  /// representation between phases" in feme/docs/FeMeCPUDesign.md).
  llvm::Value *EntryMask = nullptr;
  /// `<W x i1>`: which lanes of this wave may perform side effects. Equal to
  /// `EntryMask` for compute and vertex stages; fragment wrappers seed it from
  /// helper-lane state.
  llvm::Value *SideEffectMask = nullptr;
  /// `ptr`: this group's groupshared storage, or null if the shader
  /// declares none (see "Groupshared memory" in "Phase 6").
  llvm::Value *GroupShared = nullptr;
};

/// Recovers \p F's `WaveBodyEnv`, i.e. the parameters `SIMDizePass` appended
/// to it, by the fixed names it gives them -- or `std::nullopt` if \p F does
/// not have them (has not been through `SIMDizePass`).
std::optional<WaveBodyEnv> getWaveBodyEnv(llvm::Function &F);

/// Phase 4: widens a linearized wave body to `<W x T>`. See the file comment
/// above for current scope.
class SIMDizePass : public llvm::PassInfoMixin<SIMDizePass> {
  /// The wave size to widen to; 0 resolves it per-function from the
  /// `feme.cpu.wavesize` attribute `feme::Driver` records (see "Wave Size
  /// Selection"), falling back to `feme::cpu::MinWaveSize`.
  unsigned WaveSize;

public:
  explicit SIMDizePass(unsigned WaveSize = 0) : WaveSize(WaveSize) {}

  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-simdize"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_SIMDIZE_H
