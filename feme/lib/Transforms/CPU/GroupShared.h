//===- GroupShared.h - CPU target groupshared memory layout ------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is private to feme/lib/Transforms/CPU: it declares the
// groupshared-memory layout helpers "Groupshared memory" in "Phase 6: Group
// Execution and Barriers" (feme/docs/FeMeCPUDesign.md) describes -- a
// `addrspace(3)` global in raised IR "becomes a buffer allocated per group
// by the wrapper ... with the address space cast away".
//
// Two passes share one deterministic layout computed from the *same*,
// still-present set of module-level `addrspace(3)` globals:
//
//  - `feme::cpu::SIMDizePass` (Phase 4) canonicalizes every access to one of
//    these globals into a `getelementptr`-off-`wave_groupshared` sequence
//    (`rewriteGroupSharedGlobals`), the same split "canonicalize at Phase 4,
//    lower at Phase 6" `feme::cpu::ResourceCalls`/`WaveCalls` already
//    establish for resource/wave calls -- but leaves the globals themselves
//    in the module, now unreferenced, since Phase 6 needs the identical
//    layout to size its allocation.
//  - `feme::cpu::EntryWrapperPass` (Phase 6) computes the same layout again,
//    allocates the backing buffer (on the wrapper's stack if it fits, else
//    from `FemeDispatchArgs::GroupShared`) and only then erases the
//    now-dead globals.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_TRANSFORMS_CPU_GROUPSHARED_H
#define FEME_LIB_TRANSFORMS_CPU_GROUPSHARED_H

#include "llvm/ADT/DenseMap.h"

#include <cstdint>

namespace llvm {
class Function;
class GlobalVariable;
class Module;
class Value;
} // namespace llvm

namespace feme::cpu {

/// The address space a `groupshared`/TGSM variable is raised into (see
/// "Groupshared memory" in feme/docs/FeMeCPUDesign.md's Phase 6 section).
constexpr unsigned GroupSharedAddressSpace = 3;

/// The layout `computeGroupSharedLayout` assigns every `addrspace(3)`
/// global in a module to: a byte offset into one flat buffer, sized and
/// aligned to fit every one of them.
struct GroupSharedLayout {
  /// Total buffer size in bytes; 0 if the module declares no groupshared
  /// usage at all.
  uint64_t TotalSize = 0;
  /// The buffer's required alignment: the strictest of every global's own.
  uint64_t Alignment = 1;
  /// Each groupshared global's byte offset into the flat buffer.
  llvm::DenseMap<const llvm::GlobalVariable *, uint64_t> Offsets;
  /// Whether any groupshared global in the module carries an explicit
  /// zero-initializer (`feme::spirv::WorkgroupGlobalVariablePattern`'s own
  /// `#llvm.zero` initializer, imported from SPIR-V's `zero_initialized`
  /// `spirv.GlobalVariable` attribute -- see
  /// `VK_KHR_zero_initialize_workgroup_memory`, roadmap milestone E13).
  /// `feme::cpu::EntryWrapperPass` zeros the *entire* flat buffer once per
  /// group when this is set, rather than tracking each flagged global's own
  /// byte range individually: every group's buffer is otherwise reused
  /// as-is between dispatches (see `runDispatch` in
  /// feme/lib/Vulkan/CommandBuffer.cpp) or left as uninitialized stack
  /// memory (see `buildWrapperEnv` in EntryWrapper.cpp), so zeroing a
  /// groupshared global with no zero-initializer of its own alongside a
  /// flagged one is harmless -- it was already free to read as anything.
  bool NeedsZeroInit = false;
};

/// Computes \p M's groupshared layout: every `addrspace(3)` global
/// variable, assigned a padded, alphabetically-stable (module order) offset
/// via \p M's data layout. Deterministic across repeated calls on the same
/// module, which is what lets `feme::cpu::SIMDizePass` and
/// `feme::cpu::EntryWrapperPass` agree on it independently -- see the file
/// comment above.
GroupSharedLayout computeGroupSharedLayout(const llvm::Module &M);

/// Rewrites every use of every groupshared global \p Layout describes,
/// within \p F only, into a flat byte offset off \p GroupSharedBase (an
/// `ptr` value, e.g. `feme::cpu::WaveBodyEnv::GroupShared`): "the address
/// space cast away" this milestone's design text describes. Must run after
/// `feme::cpu::SIMDizePass`'s own widening walk has finished with \p F (see
/// SIMDize.cpp's call site), not before: a use it rewrites is a brand new
/// instruction that walk's `UniformityInfo` never analyzed, and that
/// analysis conservatively treats any value it never saw as divergent
/// (see `feme::cpu::computeWaveUniformity`), which would otherwise make the
/// walk try to widen \p rewriteGroupSharedGlobals's own replacement
/// instructions. Supports a genuinely divergent (vector-of-pointers)
/// access, an access reached through a first-level `getelementptr`
/// (`load`/`store`/`atomicrmw`, or a masked `llvm.masked.gather`/
/// `.scatter`), and one reached through the uniform-address broadcast a
/// masked gather/scatter's pointer argument still needs even when the
/// address never varies by lane (roadmap step R23, closing roadmap
/// milestone 9's narrowing; see the Status section's Deviation note in
/// feme/docs/FeMeCPUDesign.md). Returns false (emitting a diagnostic,
/// leaving \p F unmodified) if a use this pass does not yet support is
/// found: a *nested* `getelementptr` (one level deeper than a single
/// index -- a groupshared array of arrays/structs), or a groupshared
/// pointer feeding anything else entirely.
bool rewriteGroupSharedGlobals(llvm::Function &F, llvm::Value *GroupSharedBase,
                               const GroupSharedLayout &Layout);

} // namespace feme::cpu

#endif // FEME_LIB_TRANSFORMS_CPU_GROUPSHARED_H
