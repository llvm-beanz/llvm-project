//===- CompiledStage.h - FeMe CPU target compiled-code object -----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::CompiledStage, roadmap milestone R21's
// factoring of `feme::cpu::JITEngine`'s compiled-code ownership out of its
// own dispatch-scheduling policy (see feme/docs/Roadmap.md). Compiling a
// raised module through the whole CPU pipeline, linking `libFeMeRuntimeCPU`,
// and resolving the compiled entry point no longer imply any particular
// group-iteration or threading behavior: `invokeGroup` is the fine-grained,
// per-workgroup entry point that a caller -- a worker-pooled `JITEngine`
// today, an API runtime's own queue executor later -- iterates itself.
//
// This is the same type FeMeGraphicsDesign.md's "Compiled stage API"
// proposes under the name `CompiledStage` and FeMeVulkanDesign.md's "CPU
// Runtime API Changes" sketches under the name `CompiledKernel` (see that
// section's own note that they are one type). Landing it under the
// graphics design's final name here means Vulkan V1 and Direct3D W1 can
// build against `CompiledStage` directly rather than a compute-only
// `CompiledKernel` that would later need renaming.
//
// Scope deviation from both sketches: this milestone's `create` still only
// ever compiles a compute entry point, taking the existing `JITOptions`
// (JITEngine.h) rather than a stage-aware `StageCompileOptions` -- that is
// roadmap milestone R27's job, once `ShaderStage`-aware compilation exists
// at all. `getStage()` is therefore not yet exposed; every `CompiledStage`
// is implicitly `ShaderStage::Compute` for now.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_COMPILEDSTAGE_H
#define FEME_TARGET_CPU_COMPILEDSTAGE_H

#include "feme/Target/CPU/ResourceHeap.h"
#include "feme/Target/CPU/ResourceInfo.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>
#include <memory>

namespace llvm {
namespace orc {
class LLJIT;
} // namespace orc
} // namespace llvm

namespace feme {
class Context;
class Module;
} // namespace feme

namespace feme::cpu {

struct JITOptions;

/// Owns an ORC `LLJIT` instance, the compiled shader in it, and the
/// resolved entry point's address -- everything about a compiled shader
/// that does not depend on how a caller chooses to iterate its groups. See
/// the file comment above for the roadmap context and current scope
/// (compute-only).
class CompiledStage {
public:
  /// Compiles \p M (a translated `llvm::Module`, see `feme::Driver`) as a
  /// compute entry point per \p Opts, exactly as `JITEngine::create` did
  /// before this type existed -- see JITEngine.h's own file comment for the
  /// full pipeline this runs (Phases 1/resource-lowering/3-6, linking, and
  /// optimization) and its milestone-4 scope note.
  static llvm::Expected<std::unique_ptr<CompiledStage>>
  create(Context &Ctx, feme::Module M, const JITOptions &Opts);

  ~CompiledStage();
  CompiledStage(CompiledStage &&) noexcept;
  CompiledStage &operator=(CompiledStage &&) noexcept;
  CompiledStage(const CompiledStage &) = delete;
  CompiledStage &operator=(const CompiledStage &) = delete;

  /// What the shader needs from the host: root constant size, sampler heap
  /// use, and the statically-known heap indices it accesses.
  const ResourceInfo &getResourceInfo() const { return Info; }

  /// The resolved wave size this shader was compiled at.
  unsigned getWaveSize() const { return WaveSize; }

  /// The shader's declared thread group dimensions (`hlsl.numthreads`).
  std::array<uint32_t, 3> getGroupSize() const { return GroupSize; }

  /// Invokes the compiled entry point once for \p GroupID against
  /// \p Prepared's already-materialized dispatch state (see
  /// `PreparedDispatch`), using \p GroupShared as its groupshared storage
  /// -- empty until milestone 9's groupshared allocation is wired up to
  /// supply one. `const` and touches no mutable state of its own beyond the
  /// (already-linked, read-only after `create`) JIT-compiled code, so
  /// independent `GroupID`s may be invoked concurrently from multiple
  /// threads; this is what lets a caller -- `JITEngine::dispatch` below, or
  /// a future API runtime's own worker pool -- schedule groups itself
  /// rather than being limited to one whole dispatch as a unit of work
  /// (see feme/docs/FeMeVulkanDesign.md's "CPU Runtime API Changes").
  llvm::Error invokeGroup(const PreparedDispatch &Prepared,
                          std::array<uint32_t, 3> GroupID,
                          llvm::MutableArrayRef<uint8_t> GroupShared) const;

private:
  CompiledStage(std::unique_ptr<llvm::orc::LLJIT> JIT, void *EntryFn,
               ResourceInfo Info, unsigned WaveSize,
               std::array<uint32_t, 3> GroupSize);

  std::unique_ptr<llvm::orc::LLJIT> JIT;
  /// `void (*)(const FemeDispatchArgs *)`: the compiled
  /// `feme_cpu_entry_<name>` symbol's address, resolved once at `create`
  /// time (the `LLJIT` instance above keeps it valid).
  void *EntryFn;
  ResourceInfo Info;
  unsigned WaveSize;
  std::array<uint32_t, 3> GroupSize;
};

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_COMPILEDSTAGE_H
