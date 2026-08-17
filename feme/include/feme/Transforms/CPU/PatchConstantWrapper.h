//===- PatchConstantWrapper.h - CPU target patch-constant wrapper -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::PatchConstantWrapperPass, closing roadmap
// R34's "patch-constant function" open item: the hull stage's second phase,
// a single invocation that reads a whole patch's completed output control
// points and writes its tessellation factors and patch constants. See
// PatchConstantWrapper.cpp's file comment for this milestone's scope and
// what remains.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_PATCHCONSTANTWRAPPER_H
#define FEME_TRANSFORMS_CPU_PATCHCONSTANTWRAPPER_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

class PatchConstantWrapperPass
    : public llvm::PassInfoMixin<PatchConstantWrapperPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-wrap-patch-constant"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_PATCHCONSTANTWRAPPER_H
