//===- ReferenceLowering.h - `--reference`'s scalar builtin half -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::ReferenceLoweringPass, part of
// `feme-run --reference`'s implementation (see the "CFG restructurization
// test suite" section of feme/docs/FeMeCPUDesign.md): the ground truth
// that suite diffs against, running a shader one invocation at a time
// through the unwidened module instead of Phases 3/4 (linearization and
// widening).
//
// This pass is that mode's counterpart to `feme::cpu::WaveLoweringPass`'s
// builtin half: it rewrites every raised thread/group id builtin into
// scalar arithmetic over a pair of module-level globals
// (`feme.cpu.ref.thread_index_in_group`, `feme.cpu.ref.group_id`) that
// `feme::cpu::ReferenceEntryWrapperPass` sets once per invocation/group --
// there is no wave here, only one invocation at a time, so unlike
// `WaveLoweringPass` there is no `<W x T>` vector arithmetic to build, and
// no separate canonical-call indirection is needed (see
// `feme::cpu::BuiltinCalls`) since this pass runs directly on the
// unwidened function. Wave intrinsics (lane index, `WaveActive*`, ...) have
// no meaning one invocation at a time and are diagnosed rather than
// silently mis-lowered, matching the design's "the mode rejects them".
//
// Roadmap R27 adds this mode's counterpart to `feme::cpu::LinearizePass`'s
// `feme.stage.discard`/`.demote`/`.is_helper` lowering (see "the reference
// path" in FeMeGraphicsDesign.md's "CPU Lowering Pipeline"): one invocation
// at a time has no mask to narrow, so `discard(cond)` becomes a real
// conditional early return (splitting the block right after the call), and
// `demote(cond)`/`is_helper()` read and write a per-invocation `helper`
// flag (a function-local `alloca`) instead. Deviation: unlike the widened
// path, a `demote`d invocation's later `store`/`atomicrmw`/resource-write
// calls are *not* suppressed here -- doing so would need the same
// block-splitting predication machinery `feme::cpu::LinearizePass` builds,
// which this deliberately-unwidened ground-truth mode has no other use for
// (see the "CFG restructurization test suite" section this mode serves).
// `is_helper` and the side-effect summary bits
// `feme::cpu::computeSideEffectFlags` reports are still correct; only the
// write suppression itself is left for a later milestone, once a genuine
// fragment-stage reference test needs it.
//
// Runs after `feme::cpu::ResourceLoweringPass` and instead of
// `feme::cpu::LinearizePass`/`feme::cpu::SIMDizePass`/
// `feme::cpu::WaveLoweringPass`.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_REFERENCELOWERING_H
#define FEME_TRANSFORMS_CPU_REFERENCELOWERING_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// The name of the module-level global `feme::cpu::ReferenceLoweringPass`
/// reads the current invocation's flat thread index within its group from,
/// and `feme::cpu::ReferenceEntryWrapperPass` stores it into once per
/// invocation.
extern const char ReferenceThreadIndexInGroupGlobalName[];

/// The name of the module-level global (a `[3 x i32]`)
/// `feme::cpu::ReferenceLoweringPass` reads the current dispatch group's id
/// from, and `feme::cpu::ReferenceEntryWrapperPass` stores it into once per
/// group.
extern const char ReferenceGroupIDGlobalName[];

/// The function attribute `feme::cpu::ReferenceLoweringPass` marks a
/// successfully-lowered function with, so
/// `feme::cpu::ReferenceEntryWrapperPass` knows which functions are its own
/// to wrap (mirroring how `feme::cpu::SIMDizePass`'s `WaveBodyEnv`
/// parameters mark a widened one for `feme::cpu::EntryWrapperPass`).
extern const char ReferenceLoweredAttrName[];

/// See the file comment above.
class ReferenceLoweringPass
    : public llvm::PassInfoMixin<ReferenceLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-reference-lower-builtins"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_REFERENCELOWERING_H
