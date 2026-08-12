//===- ResourceLowering.h - CPU target resource canonicalization -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::ResourceLoweringPass, which will re-express a
// raised shader's bindless descriptor-heap resource access
// (`llvm.{dx,spv}.resource.handlefromheap` and its accompanying loads/
// stores) as canonical, type-mangled `feme.cpu.resource.*` calls -- see the
// "Resource Model" -> "Lowering" section of feme/docs/FeMeCPUDesign.md.
//
// This is currently scaffolding (roadmap milestone 1): the pass is
// registered under its final name (`feme-cpu-lower-resources`) so the CPU
// pipeline's command-line surface exists end to end, but it does not yet
// canonicalize anything -- see the Roadmap / Milestones section of
// feme/docs/FeMeCPUDesign.md for when this lands (milestone 3).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_RESOURCELOWERING_H
#define FEME_TRANSFORMS_CPU_RESOURCELOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Canonicalizes a raised shader's descriptor-heap resource access into
/// `feme.cpu.resource.*` calls. See the file comment above for current
/// scope.
class ResourceLoweringPass : public llvm::PassInfoMixin<ResourceLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-lower-resources"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_RESOURCELOWERING_H
