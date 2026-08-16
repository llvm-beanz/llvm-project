//===- FragmentWrapper.h - CPU target fragment entry wrapper -----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::FragmentWrapperPass, roadmap R28's
// fragment-stage counterpart to `feme::cpu::EntryWrapperPass`: it lowers stage
// input/output operations against `FemeStageLayout`, seeds the live and
// side-effect masks from fragment-quad records, and produces the batch ABI
// `feme_cpu_entry_<name>(const FemeFragmentArgs *)`.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_FRAGMENTWRAPPER_H
#define FEME_TRANSFORMS_CPU_FRAGMENTWRAPPER_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

class FragmentWrapperPass : public llvm::PassInfoMixin<FragmentWrapperPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-wrap-fragment"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_FRAGMENTWRAPPER_H
