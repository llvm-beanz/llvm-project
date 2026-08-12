//===- ReferenceEntryWrapper.h - `--reference`'s entry wrapper ----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::ReferenceEntryWrapperPass, `--reference`'s
// counterpart to `feme::cpu::EntryWrapperPass` (see the "CFG
// restructurization test suite" section of feme/docs/FeMeCPUDesign.md):
// given a `feme::cpu::ReferenceLoweringPass`-lowered (but never widened)
// shader body, it builds
//
//   void feme_cpu_entry_<name>(const FemeDispatchArgs *Args) {
//     <store Args->GroupID into @feme.cpu.ref.group_id>
//     for (flat = 0; flat < GroupSizeTotal; ++flat) {
//       <store flat into @feme.cpu.ref.thread_index_in_group>
//       body(<resource/root-constant fields from *Args>);
//     }
//   }
//
// i.e. the same exported ABI symbol `feme::cpu::EntryWrapperPass` produces,
// so `feme::cpu::JITEngine::dispatch` needs no `--reference`-specific code
// of its own -- but with a real per-invocation loop instead of a wave loop
// (there is no wave here: each invocation runs the unwidened body exactly
// once), matching the design's "a loop over single invocations".
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_REFERENCEENTRYWRAPPER_H
#define FEME_TRANSFORMS_CPU_REFERENCEENTRYWRAPPER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// See the file comment above.
class ReferenceEntryWrapperPass
    : public llvm::PassInfoMixin<ReferenceEntryWrapperPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-wrap-reference-entry"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_REFERENCEENTRYWRAPPER_H
