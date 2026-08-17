//===- DomainWrapper.h - CPU target domain stage wrapper --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::DomainWrapperPass, roadmap R34's
// continuation: the domain (evaluation) stage's counterpart to
// `feme::cpu::VertexWrapperPass`. It lowers the stage input/output
// operations of a domain entry point against `FemeDomainArgs` and wraps a
// widened wave body in the batch ABI
// `feme_cpu_entry_<name>(const FemeDomainArgs *)`, batching one invocation
// per tessellator-generated domain point. See DomainWrapper.cpp's file
// comment for this milestone's scope and what remains.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_DOMAINWRAPPER_H
#define FEME_TRANSFORMS_CPU_DOMAINWRAPPER_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

class DomainWrapperPass : public llvm::PassInfoMixin<DomainWrapperPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-wrap-domain"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_DOMAINWRAPPER_H
