//===- GroupSharedInfo.h - CPU target groupshared reflection --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares `feme::cpu::getGroupSharedRequirements`, the public,
// reflection-facing counterpart of `feme::cpu::computeGroupSharedLayout`
// (feme/lib/Transforms/CPU/GroupShared.h, private to that library): the
// total groupshared buffer size and alignment a module's entry point needs,
// without exposing the per-global offset assignment that is only ever
// needed internally by `feme::cpu::SIMDizePass`/`feme::cpu::
// EntryWrapperPass` themselves (see that header's own file comment).
//
// This is what lets a host-facing reflection structure --
// `feme::cpu::StageArtifactInfo`'s `GroupSharedSize`/`GroupSharedAlign`
// (feme/include/feme/Target/CPU/ResourceInfo.h) -- learn how large a
// groupshared allocation to prepare before dispatching a compiled entry
// point, per roadmap milestone R22.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_GROUPSHAREDINFO_H
#define FEME_TRANSFORMS_CPU_GROUPSHAREDINFO_H

#include <cstdint>

namespace llvm {
class Module;
} // namespace llvm

namespace feme::cpu {

/// The aggregate groupshared allocation requirements for a module's
/// `addrspace(3)` globals (see "Groupshared memory" in
/// feme/docs/FeMeCPUDesign.md's Phase 6 section).
struct GroupSharedRequirements {
  /// Total buffer size in bytes; 0 if the module declares no groupshared
  /// usage at all.
  uint64_t Size = 0;
  /// The buffer's required alignment: the strictest of every groupshared
  /// global's own.
  uint64_t Alignment = 1;
};

/// Computes \p M's aggregate groupshared requirements. Deterministic across
/// repeated calls on the same module (see `computeGroupSharedLayout`'s own
/// comment), so a caller may compute this either before or after the
/// globals it describes are rewritten/erased later in the pipeline, as long
/// as it does so before `feme::cpu::EntryWrapperPass` runs.
GroupSharedRequirements getGroupSharedRequirements(const llvm::Module &M);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_GROUPSHAREDINFO_H
