//===- WaveCalls.h - `feme.cpu.wave.*` call helpers ---------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the creation and recognition helpers for the canonical,
// wave-size-mangled `feme.cpu.wave.*` calls `feme::cpu::SIMDizePass`
// introduces in place of a raised wave intrinsic (`llvm.{dx,spv}.wave.*`,
// other than `wave.getlaneindex`, which is a `feme::cpu::BuiltinCallKind`
// instead -- see BuiltinCalls.h) once its operand(s) are widened to
// `<W x T>`. This mirrors how `feme::cpu::ResourceCalls`/`BuiltinCalls`
// separate canonicalization (`SIMDizePass`, Phase 4) from lowering
// (`feme::cpu::WaveLoweringPass`, Phase 5): Phase 4 only needs to know how
// to widen a wave intrinsic's operands, not the cross-lane arithmetic
// (reductions, scans, broadcasts) its result requires, which is exactly the
// "wave op" half of Phase 5's job the file comment in WaveLowering.h
// describes.
//
// Every canonical call carries the wave's entry mask (`M` in "Phase 5: Wave
// and Builtin Lowering" in feme/docs/FeMeCPUDesign.md) as its leading
// operand, honoring the design's requirement that a wave op reduce over
// exactly the currently-active lanes -- and never itself create poison from
// an all-inactive mask.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_WAVECALLS_H
#define FEME_TRANSFORMS_CPU_WAVECALLS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Intrinsics.h"

#include <cstdint>
#include <optional>
#include <string>

namespace llvm {
class CallInst;
class Constant;
class Function;
class IRBuilderBase;
class Module;
class Type;
class Value;
} // namespace llvm

namespace feme::cpu {

/// Which wave operation a `feme.cpu.wave.*` call performs, one per non-
/// trivial row of "Phase 5"'s lowering table that this milestone (plus
/// roadmap step R3's `Ballot` and step R4's `Active*`/`Prefix{Sum,Product}`
/// additions) implements (see WaveLowering.cpp's file comment for which
/// rows those are).
enum class WaveCallKind : uint8_t {
  /// `WaveGetLaneCount`: no operand, result is the constant wave size.
  GetLaneCount,
  /// `WaveIsFirstLane`: no value operand; result is a per-lane `<W x i1>`
  /// (true only for each active wave's single first active lane).
  IsFirstLane,
  /// `WaveActiveAnyTrue`: `i1` operand, uniform `i1` result.
  Any,
  /// `WaveActiveAllTrue`: `i1` operand, uniform `i1` result.
  All,
  /// `WaveActiveAllEqual`: `T` operand, uniform `i1` result.
  AllEqual,
  /// `WaveReadLaneAt`: `T` operand plus an `i32` lane index. HLSL requires
  /// the index to be dynamically uniform, but nothing enforces that at
  /// this level (SPIR-V's `OpGroupNonUniformShuffle`, which this also
  /// lowers, explicitly permits a varying one) -- so the result is a
  /// genuine per-lane `<W x T>` gather (see WaveLowering.cpp's
  /// `lowerReadLane`), not a uniform broadcast. A uniform index is simply
  /// the case where every lane's gather happens to read the same source
  /// lane.
  ReadLane,
  /// `subgroupBroadcast`/`OpGroupNonUniformBroadcast` (roadmap `L148`): `T`
  /// operand plus an `i32` lane index the SPIR-V spec *guarantees* is
  /// dynamically uniform (unlike `ReadLane`'s own index, which may not
  /// be), so unlike `ReadLane` this is always a genuine uniform broadcast:
  /// every lane reads the exact same source lane, computed once (see
  /// WaveLowering.cpp's `lowerBroadcast`). Split out from `ReadLane` as its
  /// own kind specifically so this guarantee is not lost -- reusing
  /// `ReadLane`'s always-`<W x T>`, always-per-lane-gather lowering for a
  /// call that is provably uniform produced `O(WaveSize)` IR per call site
  /// where `O(1)` suffices, which is what made
  /// `dEQP-VK.subgroups.ballot_broadcast.compute.*_requiredsubgroupsize{64,128}`
  /// hang (its shader source has `N == WaveSize` static broadcast call
  /// sites, so the old per-site `O(WaveSize)` cost compounded into
  /// `O(WaveSize^2)` total IR).
  Broadcast,
  /// `WaveActiveCountBits`/`WaveAllBitCount`: `i1` operand, uniform `i32`
  /// result.
  ActiveCountBits,
  /// `WavePrefixCountBits`/`WavePrefixBitCount`: `i1` operand, a per-lane
  /// `<W x i32>` exclusive prefix count.
  PrefixBitCount,
  /// `WaveActiveBallot`: `i1` operand, uniform `{i32, i32, i32, i32}`
  /// result (DXIL's fixed 128-bit ballot mask ABI -- see WaveLowering.cpp's
  /// `lowerBallot`).
  Ballot,
  /// `WaveActiveSum`/`WaveActiveUSum` (roadmap step R4): `T` operand,
  /// uniform `T` result. Signed/unsigned integer addition is bit-identical
  /// in two's complement, so both DXIL ops share this one kind (see
  /// WaveLowering.cpp's `lowerActiveSum`).
  ActiveSum,
  /// `WaveActiveProduct`/`WaveActiveUProduct`: `T` operand, uniform `T`
  /// result. Like `ActiveSum`, signed/unsigned multiplication is
  /// bit-identical, so both DXIL ops share this one kind.
  ActiveProduct,
  /// `WaveActiveMax` (signed integer or float): `T` operand, uniform `T`
  /// result.
  ActiveMax,
  /// `WaveActiveUMax` (unsigned integer only): `T` operand, uniform `T`
  /// result.
  ActiveUMax,
  /// `WaveActiveMin` (signed integer or float): `T` operand, uniform `T`
  /// result.
  ActiveMin,
  /// `WaveActiveUMin` (unsigned integer only): `T` operand, uniform `T`
  /// result.
  ActiveUMin,
  /// `WaveActiveBitAnd`: `T` (integer) operand, uniform `T` result.
  ActiveBitAnd,
  /// `WaveActiveBitOr`: `T` (integer) operand, uniform `T` result.
  ActiveBitOr,
  /// `WaveActiveBitXor`: `T` (integer) operand, uniform `T` result.
  ActiveBitXor,
  /// `WavePrefixSum`/`WavePrefixUSum`: `T` operand, a per-lane `<W x T>`
  /// exclusive prefix sum (see `ActiveSum`'s signedness note, and
  /// WaveLowering.cpp's `lowerPrefixReduce`).
  PrefixSum,
  /// `WavePrefixProduct`/`WavePrefixUProduct`: `T` operand, a per-lane
  /// `<W x T>` exclusive prefix product.
  PrefixProduct,
};

/// Returns whether \p Kind's result is unconditionally divergent (a genuine
/// `<W x T>` value with a different answer per lane on *every* call of that
/// kind) rather than uniform (the same scalar answer broadcast to every
/// lane) -- see each `WaveCallKind` enumerator's comment above. Only
/// `IsFirstLane`, `PrefixBitCount`, `PrefixSum` and `PrefixProduct` are
/// unconditionally divergent, the same rows `feme::cpu::WaveTTIImpl`
/// classifies `NeverUniform`.
///
/// `ReadLane` is deliberately not included even though its result type is
/// always the wide `<W x T>` shape (see its own enumerator comment): unlike
/// the rows above, whether a *specific* `ReadLane` call's result is
/// genuinely divergent depends on that call's own operands (its lane index
/// need not be uniform), matching `feme::cpu::WaveTTIImpl::
/// getValueUniformity`'s `Default` classification for it -- so
/// `feme::cpu::FunctionWidener::widenWaveCall` decides that case with the
/// actual `UniformityInfo` result instead of this static per-`Kind` table.
bool isDivergentWaveCallResult(WaveCallKind Kind);

/// The result of successfully matching a call against the canonical
/// `feme.cpu.wave.*` shape (see `matchWaveCall`).
struct MatchedWaveCall {
  WaveCallKind Kind;
  llvm::CallInst *Call = nullptr;
  /// The wave size this call was widened to (from its mangled name).
  unsigned WaveSize = 0;
  /// The wave's entry mask (`<WaveSize x i1>`), always present.
  llvm::Value *WideMask = nullptr;
  /// The widened value operand (`<WaveSize x i1>` or `<WaveSize x T>`),
  /// null for `GetLaneCount`/`IsFirstLane`, which have none.
  llvm::Value *WideOperand = nullptr;
  /// The widened lane-index operand (`<WaveSize x i32>`), only for
  /// `ReadLane`/`Broadcast`; null otherwise.
  llvm::Value *WideLaneIndex = nullptr;
};

/// Builds a `feme.cpu.wave.*` call of \p Kind, widened to \p WaveSize, over
/// \p WideMask (the wave's entry mask) and, where \p Kind needs them,
/// \p WideOperand and \p WideLaneIndex (see each `WaveCallKind`
/// enumerator's comment for which operands apply). Returns a call whose
/// result type is the scalar/vector shape `isDivergentWaveCallResult`
/// describes.
llvm::CallInst *createWaveCall(llvm::IRBuilderBase &Builder, WaveCallKind Kind,
                               unsigned WaveSize, llvm::Value *WideMask,
                               llvm::Value *WideOperand = nullptr,
                               llvm::Value *WideLaneIndex = nullptr,
                               const llvm::Twine &Name = "");

/// Recognizes \p CI as one of the canonical `feme.cpu.wave.*` calls,
/// returning its decoded operands, or `std::nullopt` if \p CI's callee
/// isn't one.
std::optional<MatchedWaveCall> matchWaveCall(const llvm::CallInst &CI);

/// Which `WaveCallKind` a *raised, not-yet-widened* `llvm.{dx,spv}.wave.*`/
/// `llvm.spv.subgroup.*` intrinsic \p ID corresponds to (`std::nullopt` for
/// anything else, including `wave.getlaneindex` -- a `BuiltinCallKind`
/// instead -- and `QuadOp`'s `llvm.dx.quad.read.*` family, not yet
/// lowered). Shared between `feme::cpu::SIMDizePass` (which widens a call
/// already recognized this way) and `feme::cpu::LinearizePass` (which needs
/// the same recognition *before* widening, to mask a reduce/scan call's own
/// value operand for a divergent region -- see `getReduceIdentity` and
/// `isArithmeticReduceOrPrefixKind` below).
std::optional<WaveCallKind> classifyWaveCall(llvm::Intrinsic::ID ID);

/// Whether \p Kind is one of the eleven arithmetic reduce (`ActiveSum`,
/// `ActiveProduct`, `ActiveMax`, `ActiveUMax`, `ActiveMin`, `ActiveUMin`,
/// `ActiveBitAnd`, `ActiveBitOr`, `ActiveBitXor`) or scan (`PrefixSum`,
/// `PrefixProduct`) kinds `getReduceIdentity` has an identity element for --
/// unlike `AllEqual`/`ReadLane`, this file's other two type-overloaded
/// kinds, every one of these combines its active lanes with an associative
/// operator that has a genuine identity element a masked-off lane can stand
/// in for (see "Phase 5: Wave and Builtin Lowering"'s `llvm.vector.reduce.*
/// over select(M, X, identity)` row), which `feme::cpu::LinearizePass` uses
/// to mask a divergent region's own such call before `feme::cpu::
/// SIMDizePass` ever widens it (roadmap H124h).
bool isArithmeticReduceOrPrefixKind(WaveCallKind Kind);

/// A masked reduction's identity element -- the value substituted for an
/// inactive lane so it cannot affect \p Kind's result (see
/// `isArithmeticReduceOrPrefixKind`'s own comment). \p EltTy is the
/// reduction's own (scalar) element type; the caller splats this to a
/// vector itself if it needs one. Not every `WaveCallKind` this is called
/// for supports every element type (e.g. `ActiveUMax`/`ActiveBitAnd` are
/// integer-only per DXIL.td's `Overloads`), so only the combinations that
/// occur are handled.
llvm::Constant *getReduceIdentity(WaveCallKind Kind, llvm::Type *EltTy);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_WAVECALLS_H
