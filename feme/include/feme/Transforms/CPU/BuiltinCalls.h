//===- BuiltinCalls.h - `feme.cpu.builtin.*` call helpers ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the creation and recognition helpers for the canonical,
// wave-size-mangled `feme.cpu.builtin.*` calls `feme::cpu::SIMDizePass`
// introduces in place of the raised per-lane-varying builtins
// (`llvm.{dx,spv}.thread.id`, ...) it cannot widen with an ordinary
// elementwise rule (see "Phase 4: Widening" in feme/docs/FeMeCPUDesign.md).
// A canonical call keeps Phase 4's postcondition ("everything is
// `<W x T>`") true without requiring Phase 4 itself to know how a thread id
// decomposes into the group id/wave index/lane index wave-body parameters
// it just introduced -- that arithmetic is `feme::cpu::WaveLoweringPass`'s
// "builtin half" (Phase 5), matching how `feme::cpu::ResourceCalls`
// separates canonicalization (Phase/`ResourceLoweringPass`) from lowering
// (the scalar helper calls widening emits).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_BUILTINCALLS_H
#define FEME_TRANSFORMS_CPU_BUILTINCALLS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"

#include <cstdint>
#include <optional>

namespace llvm {
class CallInst;
class Function;
class IRBuilderBase;
class Module;
class Value;
} // namespace llvm

namespace feme::cpu {

/// Which per-lane-varying builtin a `feme.cpu.builtin.*` call widens, one
/// per row of "Phase 5"'s table that Phase 4 cannot lower without the
/// group/wave-index parameters it itself introduces. `GroupID` is not one
/// of these: it is uniform, and `feme::cpu::SIMDizePass` replaces
/// `llvm.{dx,spv}.group.id` directly with the corresponding wave-body
/// parameter rather than deferring it (see `SIMDizePass`'s implementation).
enum class BuiltinCallKind : uint8_t {
  /// The dispatch-wide thread id (`llvm.{dx,spv}.thread.id`), one component.
  ThreadId,
  /// The thread id within its group (`llvm.{dx,spv}.thread.id.in.group`),
  /// one component.
  ThreadIdInGroup,
  /// The flattened thread id within its group
  /// (`llvm.{dx,spv}.flattened.thread.id.in.group`).
  FlattenedThreadIdInGroup,
  /// The lane index within the wave (`llvm.dx.wave.getlaneindex`).
  LaneIndex,
};

/// The operands every `feme.cpu.builtin.*` call carries: the wave-body
/// parameters `feme::cpu::SIMDizePass` appends to a widened function's
/// signature (see "Wave-body interface" in "Phase 4: Widening"), which
/// `feme::cpu::WaveLoweringPass` reads back out to compute the real
/// arithmetic.
struct BuiltinCallEnv {
  llvm::Value *GroupIDX = nullptr;
  llvm::Value *GroupIDY = nullptr;
  llvm::Value *GroupIDZ = nullptr;
  llvm::Value *WaveIndex = nullptr;
};

/// The result of successfully matching a call against the canonical
/// `feme.cpu.builtin.*` shape (see `matchBuiltinCall`).
struct MatchedBuiltinCall {
  BuiltinCallKind Kind;
  llvm::CallInst *Call = nullptr;
  /// The wave size this call was widened to (from its mangled name/vector
  /// result width).
  unsigned WaveSize = 0;
  BuiltinCallEnv Env;
  /// The thread group dimensions (`hlsl.numthreads`) the entry point
  /// declared, needed to decompose a flattened index into x/y/z.
  uint32_t NumThreadsX = 0;
  uint32_t NumThreadsY = 0;
  uint32_t NumThreadsZ = 0;
  /// The requested component (0/1/2 for x/y/z), for `ThreadId`/
  /// `ThreadIdInGroup`; unused for the other two kinds.
  unsigned Component = 0;
};

/// Builds a `feme.cpu.builtin.*` call of \p Kind, widened to \p WaveSize,
/// for thread group dimensions \p NumThreadsX/Y/Z (from `hlsl.numthreads`).
/// \p Component is the requested component (0/1/2), meaningful only for
/// `ThreadId`/`ThreadIdInGroup`. Returns a `<WaveSize x i32>`-typed call.
llvm::CallInst *createBuiltinCall(llvm::IRBuilderBase &Builder,
                                  BuiltinCallKind Kind,
                                  const BuiltinCallEnv &Env,
                                  unsigned WaveSize, uint32_t NumThreadsX,
                                  uint32_t NumThreadsY, uint32_t NumThreadsZ,
                                  unsigned Component = 0,
                                  const llvm::Twine &Name = "");

/// Recognizes \p CI as one of the canonical `feme.cpu.builtin.*` calls,
/// returning its decoded operands, or `std::nullopt` if \p CI's callee
/// isn't one.
std::optional<MatchedBuiltinCall> matchBuiltinCall(const llvm::CallInst &CI);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_BUILTINCALLS_H
