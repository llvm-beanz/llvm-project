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
#include "llvm/IR/Instructions.h"

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
/// `memory(argmem: read)`. \p AddressSpace is folded into the mangled name
/// (as `.as<N>`, omitted for the default address space 0) rather than the
/// declaration's `ptr` parameter type itself -- opaque pointers carry no
/// static type to overload on, so two different address spaces would
/// otherwise collide on the very same declaration, then fail with a "bad
/// signature" assertion the first time \p Ptr's actual address space (e.g.
/// a raised shader's `addrspace(3)` groupshared global, still untouched
/// until `feme::cpu::rewriteGroupSharedGlobals` runs at the very end of
/// `feme::cpu::SIMDizePass`) disagreed with whichever call happened to
/// create the shared declaration first.
///
/// Returns `nullptr`, after reporting \p ElementType through its own
/// `LLVMContext`'s diagnostic handler, if \p ElementType (or a
/// `FixedVectorType` \p ElementType's own element type) is a shape this
/// milestone cannot yet mangle a declaration name for (a matrix/aggregate
/// element type, most notably -- roadmap C8/H4e).
llvm::Function *getOrInsertMaskedLoad(llvm::Module &M, llvm::Type *ElementType,
                                      unsigned AddressSpace = 0);

/// Gets (inserting if absent) the type-mangled `feme.cpu.masked.store.*`
/// declaration for \p ElementType in \p M: `declare void
/// @feme.cpu.masked.store.<mangling>(<ElementType> %val, ptr %p, i32 immarg
/// %align, i1 %mask)`. `nounwind willreturn` and `memory(argmem: write)`.
/// See `getOrInsertMaskedLoad`'s comment for \p AddressSpace and for the
/// `nullptr`-on-unsupported-\p ElementType case.
llvm::Function *getOrInsertMaskedStore(llvm::Module &M, llvm::Type *ElementType,
                                       unsigned AddressSpace = 0);

/// Builds a `feme.cpu.masked.load.*` call reading \p ElementType at \p Ptr
/// (aligned to \p Align bytes), returning \p Passthru wherever \p Mask is
/// false (see "No lowering may create poison merely because `M` is
/// all-zero" in "Phase 5", which this mirrors for Phase 4's masked loads).
/// Returns `nullptr` (see `getOrInsertMaskedLoad`'s comment) if \p
/// Passthru's type is unsupported; the caller must leave the original
/// memory access untouched in that case rather than treat a null result as
/// success.
llvm::CallInst *createMaskedLoad(llvm::IRBuilderBase &Builder, llvm::Value *Ptr,
                                 unsigned Align, llvm::Value *Mask,
                                 llvm::Value *Passthru,
                                 const llvm::Twine &Name = "");

/// Builds a `feme.cpu.masked.store.*` call writing \p Val to \p Ptr
/// (aligned to \p Align bytes) only where \p Mask is true. Returns
/// `nullptr` (see `createMaskedLoad`'s comment) if \p Val's type is
/// unsupported.
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
std::optional<MatchedMaskedMemOp> matchMaskedLoad(const llvm::CallInst &CI);

/// Recognizes \p CI as a canonical `feme.cpu.masked.store.*` call, returning
/// its decoded operands, or `std::nullopt` if \p CI's callee isn't one.
std::optional<MatchedMaskedMemOp> matchMaskedStore(const llvm::CallInst &CI);

/// Gets (inserting if absent) the type-mangled `feme.cpu.masked.atomicrmw.*`
/// declaration for \p ValueType in \p M: `declare <ValueType>
/// @feme.cpu.masked.atomicrmw.<mangling>(i32 %op, ptr %p, <ValueType> %val,
/// i32 %align, i1 %mask)`, one declaration shared by every
/// `llvm::AtomicRMWInst::BinOp` (encoded as the leading `%op` operand rather
/// than mangled into the name, since unlike a masked load/store's element
/// type an atomic's operation is not part of its call's LLVM type) -- see
/// roadmap milestone 7's "Scalarization fallback does not mask per-lane
/// execution" deviation in feme/docs/FeMeCPUDesign.md, which this closes for
/// `AtomicRMWInst` (an `AtomicCmpXchgInst`'s `{T, i1}` result is already
/// rejected as an unsupported divergent aggregate type before this would
/// ever matter -- see `feme::cpu::SIMDizePass`'s
/// `checkVectorDecompositionSupported`). `nounwind willreturn` and
/// `memory(argmem: readwrite)`, matching the real instruction's effects.
/// See `getOrInsertMaskedLoad`'s comment for \p AddressSpace and for the
/// `nullptr`-on-unsupported-\p ValueType case.
llvm::Function *getOrInsertMaskedAtomicRMW(llvm::Module &M,
                                           llvm::Type *ValueType,
                                           unsigned AddressSpace = 0);

/// Builds a `feme.cpu.masked.atomicrmw.*` call performing \p Op at \p Ptr
/// (aligned to \p Align bytes) with operand \p Val, active only where
/// \p Mask is true (see "Mask representation between phases" in
/// feme/docs/FeMeCPUDesign.md, mirroring `createMaskedLoad`/
/// `createMaskedStore`). Returns `nullptr` (see `createMaskedLoad`'s
/// comment) if \p Val's type is unsupported.
llvm::CallInst *createMaskedAtomicRMW(llvm::IRBuilderBase &Builder,
                                      llvm::AtomicRMWInst::BinOp Op,
                                      llvm::Value *Ptr, llvm::Value *Val,
                                      unsigned Align, llvm::Value *Mask,
                                      const llvm::Twine &Name = "");

/// The result of successfully matching a call against the canonical
/// `feme.cpu.masked.atomicrmw.*` shape (see `matchMaskedAtomicRMW`).
struct MatchedMaskedAtomicRMW {
  llvm::CallInst *Call = nullptr;
  llvm::AtomicRMWInst::BinOp Op = llvm::AtomicRMWInst::BAD_BINOP;
  llvm::Value *Ptr = nullptr;
  llvm::Value *Val = nullptr;
  unsigned Align = 0;
  llvm::Value *Mask = nullptr;
};

/// Recognizes \p CI as a canonical `feme.cpu.masked.atomicrmw.*` call,
/// returning its decoded operands, or `std::nullopt` if \p CI's callee
/// isn't one.
std::optional<MatchedMaskedAtomicRMW>
matchMaskedAtomicRMW(const llvm::CallInst &CI);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_MASKINTRINSICS_H
