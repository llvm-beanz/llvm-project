//===- Prepare.h - CPU target Phase 1: preparation ---------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::PreparePass, "Phase 1: Preparation" in
// feme/docs/FeMeCPUDesign.md: getting a raised module into the shape every
// later CPU pipeline phase assumes (structurized control flow, no `switch`,
// `mem2reg`/SROA-promoted allocas, and a single selected/canonicalized
// compute entry point).
//
// Roadmap milestone 4 implements this pass: `FixIrreducible` +
// `UnifyLoopExits` + `StructurizeCFG` (all in-tree, target-independent)
// structurize control flow, `LowerSwitch` removes multi-way branches (the
// linearizer handles two-way branches only), `PromotePass` (`mem2reg`)
// promotes allocas that can be, and entry-point canonicalization selects a
// single `hlsl.shader="compute"` function -- by name if `EntryPoint` is
// given, else the module's only one -- discarding every other entry point
// and any definition left unreachable from it.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_PREPARE_H
#define FEME_TRANSFORMS_CPU_PREPARE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Phase 1: prepares a raised module for the rest of the CPU pipeline. See
/// the file comment above for current scope.
class PreparePass : public llvm::PassInfoMixin<PreparePass> {
  /// The compute entry point to keep, or empty to require the module to
  /// have exactly one `hlsl.shader="compute"` function.
  std::string EntryPoint;

public:
  explicit PreparePass(llvm::StringRef EntryPoint = "") : EntryPoint(EntryPoint) {}

  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-prepare"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_PREPARE_H
