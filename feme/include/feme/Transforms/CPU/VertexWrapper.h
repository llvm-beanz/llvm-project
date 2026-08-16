//===- VertexWrapper.h - CPU target vertex entry wrapper ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::VertexWrapperPass, roadmap R28's vertex-stage
// counterpart to `feme::cpu::EntryWrapperPass`: it lowers stage input/output
// operations against `FemeStageLayout` and wraps a widened wave body in the
// batch ABI `feme_cpu_entry_<name>(const FemeVertexArgs *)`.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_VERTEXWRAPPER_H
#define FEME_TRANSFORMS_CPU_VERTEXWRAPPER_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

class VertexWrapperPass : public llvm::PassInfoMixin<VertexWrapperPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-wrap-vertex"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_VERTEXWRAPPER_H
