//===- SIMDize.h - CPU target Phase 4: widening -------------------*- C++ -*-===//
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
// This is currently scaffolding (roadmap milestone 1): the pass is
// registered under its final name (`feme-cpu-simdize`) so the CPU
// pipeline's command-line surface exists end to end, but it does not yet
// widen anything -- see the Roadmap / Milestones section of
// feme/docs/FeMeCPUDesign.md for when this lands (milestone 4, for
// uniform-control-flow shaders; milestone 7 for the remaining wave sizes and
// masked memory ops).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_SIMDIZE_H
#define FEME_TRANSFORMS_CPU_SIMDIZE_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Phase 4: widens a linearized wave body to `<W x T>`. See the file comment
/// above for current scope.
class SIMDizePass : public llvm::PassInfoMixin<SIMDizePass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-simdize"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_SIMDIZE_H
