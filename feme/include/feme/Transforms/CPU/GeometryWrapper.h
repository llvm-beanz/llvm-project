//===- GeometryWrapper.h - CPU target geometry stage wrapper --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::GeometryWrapperPass, roadmap R34's
// continuation: the geometry stage's counterpart to
// `feme::cpu::DomainWrapperPass`/`VertexWrapperPass`. It lowers the stage
// input/output and `feme.stage.stream.emit`/`.cut` operations of a geometry
// entry point against `FemeGeometryArgs` and wraps a widened wave body in the
// batch ABI `feme_cpu_entry_<name>(const FemeGeometryArgs *)`, batching one
// invocation per assembled input primitive. See GeometryWrapper.cpp's file
// comment for this milestone's scope and what remains.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_GEOMETRYWRAPPER_H
#define FEME_TRANSFORMS_CPU_GEOMETRYWRAPPER_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

class GeometryWrapperPass : public llvm::PassInfoMixin<GeometryWrapperPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-wrap-geometry"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_GEOMETRYWRAPPER_H
