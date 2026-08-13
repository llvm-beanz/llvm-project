//===- BarrierCalls.h - raised barrier intrinsic classification --*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is private to feme/lib/Transforms/CPU: it declares
// `feme::cpu::matchBarrierCall`, which recognizes a call to one of the six
// `llvm.{dx,spv}.*_memory_barrier[_with_group_sync]` intrinsics
// `feme::dxil::OpRaisingPass::raiseBarrierCall` (and its SPIR-V
// counterpart) produce, decoding the execution/memory scope and
// convergence semantics `feme::cpu::EntryWrapperPass`'s "Phase 6: Group
// Execution and Barriers" needs (feme/docs/FeMeCPUDesign.md): whether the
// intrinsic requires every invocation in the group to arrive before any
// proceeds (`GroupSync`, requiring the barrier-splitting transform), and
// how wide a memory fence its semantics need.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_BARRIERCALLS_H
#define FEME_TRANSFORMS_CPU_BARRIERCALLS_H

#include <optional>

namespace llvm {
class CallInst;
} // namespace llvm

namespace feme::cpu {

/// The memory this milestone's barrier support recognizes a raised barrier
/// intrinsic as ordering (see "Barriers" in "Phase 6: Group Execution and
/// Barriers" in feme/docs/FeMeCPUDesign.md): `Group` orders only
/// groupshared-memory accesses, while `Device`/`All` also order accesses to
/// the descriptor-heap-backed resource memory a *different* group's wave
/// loop (running on a different host thread) may concurrently touch.
/// `Device` and `All` are not distinguished further -- both need a fence
/// visible across host threads, unlike `Group` (see `matchBarrierCall`'s
/// doc comment).
enum class BarrierMemoryScope { Group, Device, All };

/// A raised barrier intrinsic call's decoded semantics.
struct MatchedBarrier {
  BarrierMemoryScope MemoryScope;
  /// Whether this is one of the three `..._with_group_sync` intrinsics:
  /// requires every invocation in the group to reach this point before any
  /// proceeds (the property `feme::cpu::EntryWrapperPass`'s region
  /// splitting implements), rather than being a memory-ordering-only
  /// barrier with no such convergence requirement.
  bool GroupSync = false;
};

/// Recognizes \p CI as a call to one of the six raised barrier intrinsics,
/// or `std::nullopt` if it is not one.
std::optional<MatchedBarrier> matchBarrierCall(const llvm::CallInst &CI);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_BARRIERCALLS_H
