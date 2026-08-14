//===- EntryWrapper.h - CPU target Phase 6: group execution -------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::EntryWrapperPass, "Phase 6: Group Execution
// and Barriers" in feme/docs/FeMeCPUDesign.md: wrapping a widened,
// wave-lowered shader body in the wave loop, groupshared allocation and
// barrier region splitting needed to produce the
// `feme_cpu_entry_<name>(const FemeDispatchArgs *)` symbol described in the
// "Kernel ABI" section (see feme/include/feme/Target/CPU/RuntimeABI.h).
//
// Roadmap milestone 4 implements the barrier-free case: the wave loop over
// a `feme::cpu::WaveBodyEnv`-shaped wave body (see EntryWrapper.cpp's file
// comment), producing the exported `feme_cpu_entry_<name>` ABI function.
// Milestone 9 adds barrier region splitting and groupshared allocation;
// roadmap step R5 (feme/docs/Roadmap.md) closes that milestone's two
// narrowings -- a value live across a barrier is spilled rather than
// diagnosed, and a barrier inside a uniform loop (not just a straight-line
// wave body) is split -- see EntryWrapper.cpp's file comment for the
// current scope and what remains diagnosed (a barrier inside a branch).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_ENTRYWRAPPER_H
#define FEME_TRANSFORMS_CPU_ENTRYWRAPPER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"

#include <string>

namespace feme::cpu {

/// The exported ABI symbol name for the entry point named \p EntryName (see
/// "Kernel ABI" in feme/docs/FeMeCPUDesign.md): `feme_cpu_entry_<name>`.
std::string getEntrySymbolName(llvm::StringRef EntryName);

/// Phase 6: wraps a shader body in the group/wave loop and produces the
/// `feme_cpu_entry_<name>` ABI entry point. See the file comment above for
/// current scope.
class EntryWrapperPass : public llvm::PassInfoMixin<EntryWrapperPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-wrap-entry"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_ENTRYWRAPPER_H
