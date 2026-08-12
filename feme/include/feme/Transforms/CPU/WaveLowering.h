//===- WaveLowering.h - CPU target Phase 5: wave/builtin lowering -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::WaveLoweringPass, "Phase 5: Wave and Builtin
// Lowering" in feme/docs/FeMeCPUDesign.md: lowering the widened `<W x T>`
// module's raised wave intrinsics (`llvm.{dx,spv}.wave.*`) and thread/group
// builtins into the vector/scalar operations the CPU target's runtime
// support library and entry wrapper (feme::cpu::EntryWrapperPass) expect.
//
// This is currently scaffolding (roadmap milestone 1): the pass is
// registered under its final name (`feme-cpu-lower-wave`) so the CPU
// pipeline's command-line surface exists end to end, but it does not yet
// lower anything -- see the Roadmap / Milestones section of
// feme/docs/FeMeCPUDesign.md for when this lands (milestone 4, for the
// builtin half; milestone 8 for the remaining wave intrinsics).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_WAVELOWERING_H
#define FEME_TRANSFORMS_CPU_WAVELOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Phase 5: lowers wave intrinsics and thread/group builtins in a widened
/// module. See the file comment above for current scope.
class WaveLoweringPass : public llvm::PassInfoMixin<WaveLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-lower-wave"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_WAVELOWERING_H
