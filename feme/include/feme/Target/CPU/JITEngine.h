//===- JITEngine.h - FeMe CPU target JIT dispatch engine ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::JITEngine, the "JIT Flow" section of
// feme/docs/FeMeCPUDesign.md: an ORC-based JIT that owns compiling a raised
// module through the whole CPU pipeline (Phases 1/resource-lowering/3-6),
// linking `libFeMeRuntimeCPU`, and running dispatches against the result --
// not merely a "compile and hand back a function pointer" API. See that
// section for the full rationale (dispatch ownership, thread pool,
// ObjectCache, ...).
//
// Roadmap milestone 4 implements the core of this: `create` runs the CPU
// pipeline (barrier-free, uniform-control-flow shaders only, matching
// SIMDizePass/EntryWrapperPass's own current scope) and links/optimizes the
// result; `dispatch` runs every group of a dispatch. It deviates from the
// full design in one respect the Status section's Deviation note calls
// out: `dispatch` runs groups on the calling thread, sequentially, rather
// than across a thread pool -- `JITOptions::NumThreads` is accepted but
// unused. Groupshared memory is also not yet allocated (milestone 9), so a
// shader declaring any is rejected.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_JITENGINE_H
#define FEME_TARGET_CPU_JITENGINE_H

#include "feme/Target/CPU/ResourceInfo.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {
class Module;
namespace orc {
class LLJIT;
} // namespace orc
} // namespace llvm

namespace feme {
class Context;
class Module;
} // namespace feme

namespace feme::cpu {

namespace detail {
/// `FeMeRuntimeCPU.c`'s externally-visible helpers are given their canonical
/// dotted `feme.cpu.resource.*`/`feme.cpu.rt.*` names via a GNU `asm` label
/// (see that file's top comment). On Mach-O targets, Clang spells such a
/// symbol's LLVM IR name with a leading `'\1'` (SOH) byte that tells the
/// AsmPrinter to skip the platform's usual global-symbol mangling; since
/// that byte is part of the `GlobalValue`'s actual name, it also defeats
/// `JITEngine::create`'s exact-name matching when it links the runtime
/// module in, so this strips it from every global in \p M first. Exposed
/// (only) for `JITEngineTest`'s regression coverage of this Mach-O-specific
/// behavior on hosts that are not themselves Mach-O.
void stripAsmLabelManglingEscape(llvm::Module &M);

/// Retargets \p RuntimeMod (the freshly-parsed `libFeMeRuntimeCPU` bitcode)
/// to \p M's own target triple before it is linked into \p M. `RuntimeMod`
/// is plain freestanding C compiled with no explicit `-target`, so its
/// triple is only whatever Clang defaults to for the build host and need
/// not be textually identical to \p M's (already resolved) triple even when
/// both name the same target; leaving them mismatched makes
/// `Linker::linkInModule` emit a spurious "Linking two modules of different
/// target triples" warning. Exposed (only) for `JITEngineTest`'s regression
/// coverage of this behavior.
void alignRuntimeModuleTriple(llvm::Module &RuntimeMod, const llvm::Module &M);
} // namespace detail

/// Options controlling how `JITEngine::create` compiles and runs a shader.
/// See "JIT Flow" in feme/docs/FeMeCPUDesign.md.
struct JITOptions {
  /// 0 resolves the wave size from the shader/host per "Wave Size
  /// Selection", else forces this value (validated the same way
  /// `feme::cpu::resolveWaveSize` validates `--wave-size`).
  unsigned WaveSize = 0;
  /// The compute entry point to select, or empty if the module has only
  /// one (see `feme::cpu::PreparePass`).
  std::string EntryPoint;
  llvm::CodeGenOptLevel OptLevel = llvm::CodeGenOptLevel::Default;
  /// Accepted for forward compatibility with the full design (see the file
  /// comment's Deviation note); not yet consulted by anything this
  /// milestone implements.
  bool EnableRobustness = true;
  /// Accepted for forward compatibility (see the file comment's Deviation
  /// note): `dispatch` always runs groups sequentially on the calling
  /// thread for now, regardless of this value.
  unsigned NumThreads = 0;
  /// Runs the shader one invocation at a time through the unwidened module
  /// instead of Phases 3/4 (`feme::cpu::LinearizePass`/
  /// `feme::cpu::SIMDizePass`) and Phase 5's wave-op half -- the ground
  /// truth the CFG restructurization test suite (roadmap milestone 5, see
  /// feme/docs/FeMeCPUDesign.md) diffs against. `WaveSize` is ignored when
  /// this is set. A shader using a wave intrinsic (which has no meaning
  /// one invocation at a time) is rejected.
  bool Reference = false;
};

/// The resources a dispatch runs against. Descriptor heaps are owned by the
/// caller and must remain alive until the `dispatch` call using them
/// returns.
struct DispatchResources {
  llvm::ArrayRef<FemeDescriptor> ResourceHeap;
  llvm::ArrayRef<FemeDescriptor> SamplerHeap;
  llvm::ArrayRef<uint8_t> RootConstants;
};

/// Owns an ORC `LLJIT` instance, the compiled shader in it, and the
/// execution of dispatches against it. See the file comment above for
/// current scope.
class JITEngine {
public:
  static llvm::Expected<std::unique_ptr<JITEngine>>
  create(Context &Ctx, feme::Module M, const JITOptions &Opts);

  ~JITEngine();
  JITEngine(JITEngine &&) noexcept;
  JITEngine &operator=(JITEngine &&) noexcept;
  JITEngine(const JITEngine &) = delete;
  JITEngine &operator=(const JITEngine &) = delete;

  /// What the shader needs from the host: root constant size, sampler heap
  /// use, and the statically-known heap indices it accesses.
  const ResourceInfo &getResourceInfo() const { return Info; }

  /// The resolved wave size this shader was compiled at.
  unsigned getWaveSize() const { return WaveSize; }

  /// The shader's declared thread group dimensions (`hlsl.numthreads`).
  std::array<uint32_t, 3> getGroupSize() const { return GroupSize; }

  /// Runs the whole dispatch to completion: for every group in \p
  /// GroupCount, fills in a `FemeDispatchArgs` and calls the compiled entry
  /// point. See the file comment above for this milestone's sequencing
  /// deviation from the full design.
  llvm::Error dispatch(const DispatchResources &Resources,
                       std::array<uint32_t, 3> GroupCount) const;

private:
  JITEngine(std::unique_ptr<llvm::orc::LLJIT> JIT, void *EntryFn,
            ResourceInfo Info, unsigned WaveSize,
            std::array<uint32_t, 3> GroupSize);

  std::unique_ptr<llvm::orc::LLJIT> JIT;
  /// `void (*)(const FemeDispatchArgs *)`: the compiled
  /// `feme_cpu_entry_<name>` symbol's address, resolved once at `create`
  /// time (the `LLJIT` instance below keeps it valid).
  void *EntryFn;
  ResourceInfo Info;
  unsigned WaveSize;
  std::array<uint32_t, 3> GroupSize;
};

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_JITENGINE_H
