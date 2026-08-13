//===- MaskIntrinsics.h - `feme.cpu.mask.*` call helpers ----------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the creation helpers for the `feme.cpu.mask.*`/
// `feme.cpu.masked.*` intrinsics described in the "Mask representation
// between phases" subsection of "Phase 3: Linearization and Predication" in
// feme/docs/FeMeCPUDesign.md.
//
// `feme::cpu::LinearizePass` is the producer of all of them: `mask.any`
// (roadmap milestone 6) is how a loop with a divergent exit asks "is any
// lane still active", meaningless on the scalar `i1` mask this pass works
// with (a single invocation's mask is just that one bit) until Phase 4
// widens it to `<W x i1>` and lowers this call to `llvm.vector.reduce.or`
// (see the "Mask representation between phases" table); `masked.load`/
// `.store` (roadmap milestone 7) are how an ordinary `load`/`store` under a
// divergent condition gets a governing mask operand at all, the same way a
// canonical `feme.cpu.resource.*` call already carries one, so that Phase 4
// can widen either into the real masked vector memory op the table
// describes instead of leaving an unmasked scalar access to run on behalf
// of a lane that never should have.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_MASKINTRINSICS_H
#define FEME_TRANSFORMS_CPU_MASKINTRINSICS_H

#include "llvm/ADT/Twine.h"

#include <optional>

namespace llvm {
class CallInst;
class Function;
class IRBuilderBase;
class Module;
class Type;
class Value;
} // namespace llvm

namespace feme::cpu {

/// Gets (inserting if absent) the `feme.cpu.mask.any` declaration in \p M:
/// `declare i1 @feme.cpu.mask.any(i1)`, an ordinary declaration (not an
/// intrinsic -- see "Mask representation between phases": `feme.cpu.*` names
/// are deliberately not `llvm.`-prefixed) with `nounwind willreturn` and no
/// memory effects, since it is a pure reduction over its argument.
llvm::Function *getOrInsertMaskAny(llvm::Module &M);

/// Builds a `feme.cpu.mask.any` call over \p Mask.
llvm::CallInst *createMaskAny(llvm::IRBuilderBase &Builder, llvm::Value *Mask,
                              const llvm::Twine &Name = "");

/// Returns whether \p CI calls the exact `feme.cpu.mask.any` declaration
/// `getOrInsertMaskAny` produces -- matched by name and shape rather than by
/// `Function *` identity, so it also recognizes a call parsed from separate
/// `lit` test IR. Used by `feme::cpu::WaveTTIImpl` (the call always reduces
/// to the same value on every lane, so it is `AlwaysUniform` regardless of
/// its operand's divergence -- see "Mask representation between phases" in
/// feme/docs/FeMeCPUDesign.md) and by `feme::cpu::SIMDizePass` (which must
/// lower every surviving call to `llvm.vector.reduce.or` before Phase 5).
bool isMaskAnyCall(const llvm::CallInst &CI);

/// Gets (inserting if absent) the type-mangled `feme.cpu.masked.load.*`
/// declaration for \p ElementType in \p M: `declare <ElementType>
/// @feme.cpu.masked.load.<mangling>(ptr %p, i32 immarg %align, i1 %mask,
/// <ElementType> %passthru)`, the literal example in "Mask representation
/// between phases". An ordinary declaration (not an intrinsic, for the same
/// reason `feme.cpu.mask.any` is), with `nounwind willreturn` and
/// `memory(argmem: read)`.
llvm::Function *getOrInsertMaskedLoad(llvm::Module &M,
                                      llvm::Type *ElementType);

/// Gets (inserting if absent) the type-mangled `feme.cpu.masked.store.*`
/// declaration for \p ElementType in \p M: `declare void
/// @feme.cpu.masked.store.<mangling>(<ElementType> %val, ptr %p, i32 immarg
/// %align, i1 %mask)`. `nounwind willreturn` and `memory(argmem: write)`.
llvm::Function *getOrInsertMaskedStore(llvm::Module &M,
                                       llvm::Type *ElementType);

/// Builds a `feme.cpu.masked.load.*` call reading \p ElementType at \p Ptr
/// (aligned to \p Align bytes), returning \p Passthru wherever \p Mask is
/// false (see "No lowering may create poison merely because `M` is
/// all-zero" in "Phase 5", which this mirrors for Phase 4's masked loads).
llvm::CallInst *createMaskedLoad(llvm::IRBuilderBase &Builder, llvm::Value *Ptr,
                                 unsigned Align, llvm::Value *Mask,
                                 llvm::Value *Passthru,
                                 const llvm::Twine &Name = "");

/// Builds a `feme.cpu.masked.store.*` call writing \p Val to \p Ptr
/// (aligned to \p Align bytes) only where \p Mask is true.
llvm::CallInst *createMaskedStore(llvm::IRBuilderBase &Builder,
                                  llvm::Value *Val, llvm::Value *Ptr,
                                  unsigned Align, llvm::Value *Mask);

/// The result of successfully matching a call against the canonical
/// `feme.cpu.masked.load.*`/`.store.*` shape (see `matchMaskedLoad`/
/// `matchMaskedStore`).
struct MatchedMaskedMemOp {
  llvm::CallInst *Call = nullptr;
  llvm::Value *Ptr = nullptr;
  unsigned Align = 0;
  llvm::Value *Mask = nullptr;
  /// The passthru operand for a load; the stored value for a store.
  llvm::Value *ValueOperand = nullptr;
};

/// Recognizes \p CI as a canonical `feme.cpu.masked.load.*` call, returning
/// its decoded operands, or `std::nullopt` if \p CI's callee isn't one.
std::optional<MatchedMaskedMemOp>
matchMaskedLoad(const llvm::CallInst &CI);

/// Recognizes \p CI as a canonical `feme.cpu.masked.store.*` call, returning
/// its decoded operands, or `std::nullopt` if \p CI's callee isn't one.
std::optional<MatchedMaskedMemOp>
matchMaskedStore(const llvm::CallInst &CI);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_MASKINTRINSICS_H
