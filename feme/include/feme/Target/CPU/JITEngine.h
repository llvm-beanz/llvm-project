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
// feme/docs/FeMeCPUDesign.md: it owns running dispatches -- scheduling every
// group of a dispatch across `JITOptions::NumThreads` worker threads, filling
// in the dispatch arguments, and joining -- against a shader compiled
// through the whole CPU pipeline (Phases 1/resource-lowering/3-6) and linked
// against `libFeMeRuntimeCPU`.
//
// Roadmap milestone R21 factors the compiled-code ownership this type used
// to hold entirely on its own out into `feme::cpu::CompiledStage`
// (CompiledStage.h): `JITEngine` is now a convenience wrapper around it,
// adding only dispatch scheduling -- see that header's own file comment for
// the rationale (this is the same factoring FeMeVulkanDesign.md's "CPU
// Runtime API Changes" and FeMeGraphicsDesign.md's "Compiled stage API"
// describe). `JITOptions::NumThreads` is real as of this milestone: see
// `dispatch`'s own comment for its threading policy.
//
// Roadmap milestone 4 implements the core of the compile step now owned by
// `CompiledStage::create` (barrier-free, uniform-control-flow shaders only,
// matching SIMDizePass/EntryWrapperPass's own current scope). Groupshared
// memory is also not yet allocated (milestone 9), so a shader declaring any
// is rejected.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_JITENGINE_H
#define FEME_TARGET_CPU_JITENGINE_H

#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/ResourceHeap.h"
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
class ThreadPoolInterface;
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
/// `CompiledStage::create`'s exact-name matching when it links the runtime
/// module in, so this strips it from every global in \p M first. Defined in
/// CompiledStage.cpp (that is where compilation now happens); exposed
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
/// target triples" warning. Defined in CompiledStage.cpp; exposed (only)
/// for `JITEngineTest`'s regression coverage of this behavior.
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
  /// How many worker threads `dispatch` runs a dispatch's groups across: 0
  /// requests hardware concurrency (`llvm::hardware_concurrency`'s own
  /// convention), 1 runs every group sequentially on the calling thread
  /// with no pool overhead, and any other value requests that many worker
  /// threads. Groups are independent by definition (see "Dispatch
  /// parallelism" in feme/docs/FeMeCPUDesign.md's "JIT Flow" section), so
  /// this needs no synchronization beyond `dispatch`'s own join.
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

/// A convenience wrapper around `CompiledStage` (CompiledStage.h) that adds
/// dispatch-wide group scheduling: compiling a shader and running whole
/// dispatches against it in one type, for `feme-run` and every existing
/// caller that has no reason to iterate groups itself. See the file
/// comment above for the roadmap context.
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
  const ResourceInfo &getResourceInfo() const { return Stage->getResourceInfo(); }

  /// The resolved wave size this shader was compiled at.
  unsigned getWaveSize() const { return Stage->getWaveSize(); }

  /// The shader's declared thread group dimensions (`hlsl.numthreads`).
  std::array<uint32_t, 3> getGroupSize() const { return Stage->getGroupSize(); }

  /// The reflection artifact this engine's compiled stage exposes; see
  /// `CompiledStage::getArtifactInfo`.
  StageArtifactInfo getArtifactInfo() const { return Stage->getArtifactInfo(); }

  /// Runs the whole dispatch to completion: prepares \p Resources once
  /// (see `PreparedDispatch`), then runs every group in \p GroupCount
  /// against `CompiledStage::invokeGroup`, across the engine's own worker
  /// pool (see `JITOptions::NumThreads`'s comment), and joins.
  llvm::Error dispatch(const DispatchResources &Resources,
                       std::array<uint32_t, 3> GroupCount) const;

private:
  JITEngine(std::unique_ptr<CompiledStage> Stage,
           std::unique_ptr<llvm::ThreadPoolInterface> Pool);

  std::unique_ptr<CompiledStage> Stage;
  /// The worker pool `dispatch` schedules groups across, owned by the
  /// engine for its whole lifetime (see "Dispatch parallelism" in
  /// feme/docs/FeMeCPUDesign.md's "JIT Flow" section) -- null when
  /// `JITOptions::NumThreads == 1`, so `dispatch` runs every group on the
  /// calling thread with no pool at all.
  std::unique_ptr<llvm::ThreadPoolInterface> Pool;
};

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_JITENGINE_H
