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
// single entry point of the requested `feme::ShaderStage` -- by name if
// `EntryPoint` is given, else the module's only one -- discarding every
// other entry point and any definition left unreachable from it.
//
// Roadmap milestone 16 replaces that selection's original `hlsl.shader ==
// "compute"` string comparison with the checked `feme::ShaderStage`
// enumeration ("Stage identity" in feme/docs/FeMeGraphicsDesign.md), so that
// a non-compute stage is a `Stage` argument rather than a different
// comparison. `hlsl.shader` is still accepted as the source of an entry
// point's stage when `feme.shader.stage` is absent; see
// `feme::getShaderStage`.
//
// Roadmap milestone 5 adds an assertions-only postcondition check
// (`feme::cpu::verifyStructured`) at the end of `run`, matching the "CFG
// restructurization test suite" section of feme/docs/FeMeCPUDesign.md.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_PREPARE_H
#define FEME_TRANSFORMS_CPU_PREPARE_H

#include "feme/Core/ShaderStage.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"

#include <string>

namespace feme::cpu {

/// Phase 1: prepares a raised module for the rest of the CPU pipeline. See
/// the file comment above for current scope.
class PreparePass : public llvm::PassInfoMixin<PreparePass> {
  /// The entry point to keep, or empty to require the module to have
  /// exactly one entry point of `Stage`.
  std::string EntryPoint;

  /// The pipeline stage the selected entry point must declare.
  ShaderStage Stage;

public:
  explicit PreparePass(llvm::StringRef EntryPoint = "",
                       ShaderStage Stage = ShaderStage::Compute)
      : EntryPoint(EntryPoint), Stage(Stage) {}

  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-prepare"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_PREPARE_H
