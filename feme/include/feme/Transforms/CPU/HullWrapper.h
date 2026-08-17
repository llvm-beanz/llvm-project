//===- HullWrapper.h - CPU target hull control-point wrapper -----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::HullWrapperPass, roadmap R34's continuation:
// the hull (control) stage's counterpart to `feme::cpu::VertexWrapperPass`.
// It lowers the control-point phase's stage input/output operations against
// `FemePatchArgs` and wraps a widened wave body in the batch ABI
// `feme_cpu_entry_<name>(const FemePatchArgs *)`. See HullWrapper.cpp's file
// comment for this milestone's scope and what remains (the patch-constant
// phase and barrier-requiring control-point shapes).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_HULLWRAPPER_H
#define FEME_TRANSFORMS_CPU_HULLWRAPPER_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

class HullWrapperPass : public llvm::PassInfoMixin<HullWrapperPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-wrap-hull"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_HULLWRAPPER_H
