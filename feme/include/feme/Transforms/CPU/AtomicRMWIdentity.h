//===- AtomicRMWIdentity.h - Maskable atomicrmw identity element -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares `feme::cpu::getAtomicRMWIdentity`, shared between
// `feme::cpu::SIMDizePass` (masking a resource-heap/groupshared atomic
// widened across a wave, `FunctionWidener::widenMaskedAtomicRMW`/
// `widenGroupSharedAtomicRMW` in SIMDize.cpp) and
// `feme::cpu::TaskPayloadWrapperPass` (masking a per-lane task-payload
// atomic, `lowerTaskPayloadAtomicRMW` in TaskPayloadWrapper.cpp): both need
// the identical "what value makes this lane's real `atomicrmw` a no-op"
// answer for a masked-off lane, rather than duplicating the same
// `AtomicRMWInst::BinOp` switch twice.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_ATOMICRMWIDENTITY_H
#define FEME_TRANSFORMS_CPU_ATOMICRMWIDENTITY_H

#include "llvm/IR/Instructions.h"
#include <optional>

namespace llvm {
class Constant;
class Type;
} // namespace llvm

namespace feme::cpu {

/// Returns the identity element `Id` for \p Op such that `Op(old, Id) ==
/// old` for every `old` -- i.e. the value a masked-off lane's `atomicrmw`
/// should contribute so it becomes a no-op instead of a real, unmasked
/// modification. Every `llvm::AtomicRMWInst::BinOp` HLSL's `Interlocked*`
/// builtins actually lower to (`Add`/`Sub`/`And`/`Or`/`Xor`/`Max`/`Min`/
/// `UMax`/`UMin`) has one (`FAdd`/`FSub`/`FMax`/`FMin`/`FMaximum`/
/// `FMinimum`/`USubCond`/`USubSat` do too, for whatever future front end
/// produces them); `std::nullopt` for `Xchg` (its caller must fall back to
/// a masked load-then-write-back instead, since no single value is ever a
/// no-op for it) and the three operations (`Nand`, `UIncWrap`, `UDecWrap`)
/// whose result depends on `old` in a way no single operand value can leave
/// unchanged for every `old`.
std::optional<llvm::Constant *>
getAtomicRMWIdentity(llvm::AtomicRMWInst::BinOp Op, llvm::Type *Ty);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_ATOMICRMWIDENTITY_H
