//===- WaveLowering.cpp - CPU target Phase 5: wave/builtin lowering ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap milestone 4's "builtin half" (see WaveLowering.h): every
// `feme.cpu.builtin.*` call reduces to the same flattened per-lane index,
//
//   flat = WaveIndex * W + lane        (lane = 0, 1, ..., W-1, a constant
//                                        iota -- feme::cpu::LaneIndex)
//
// decomposed into the thread-in-group id's x/y/z components the same way
// `feme::amdgpu::RaisedLoweringPass::lowerFlattenedThreadIDInGroup` combines
// them, just in reverse (division/remainder by the thread group dimensions
// instead of multiply/add):
//
//   x = flat % Gx             (feme::cpu::BuiltinCallKind::ThreadIdInGroup)
//   y = (flat / Gx) % Gy
//   z = flat / (Gx * Gy)
//
// and the dispatch-wide id (`ThreadId`) adds the group id, scaled by the
// group's own size, on top of that:
//
//   thread_id[c] = group_id[c] * NumThreads[c] + thread_id_in_group[c]
//
// Roadmap milestone 8's "wave op half": every `feme.cpu.wave.*` call (see
// feme::cpu::WaveCalls) `feme::cpu::SIMDizePass` canonicalized lowers per
// "Phase 5"'s table in feme/docs/FeMeCPUDesign.md, `M` being the call's wide
// entry-mask operand:
//
//   GetLaneCount     -> the constant `W`
//   IsFirstLane      -> `M != 0 && lane == cttz(bitcast M to iW, false)`
//   Any / All        -> `reduce.or(M & X)` / `reduce.and(select(M, X, true))`
//   AllEqual         -> broadcast of the first active lane, `icmp eq` against
//                       `X` under `M` (vacuously true where `M` is all-zero)
//   ReadLane(X, I)   -> per-lane guarded extract: lane `L`'s result is
//                       `X[I[L]]` if lane `I[L]` is active, else zero --
//                       a genuine gather, since `I` is not required uniform
//                       (see WaveCalls.h)
//   ActiveCountBits  -> `ctpop(bitcast (M & X) to iW)`
//   PrefixBitCount   -> exclusive running `ctpop`-style count of `M & X`,
//                       lane by lane (the "lane loop for large W" option --
//                       see the header's row for why no shuffle-scan is
//                       needed at these wave sizes)
//   Ballot           -> `bitcast (M & X) to iW`, split and zero-pad into
//                       the source ABI's 32-bit result words (roadmap step
//                       R3; see `lowerBallot`)
//   ActiveSum/Product/Max/UMax/Min/UMin/BitAnd/BitOr/BitXor
//                    -> `llvm.vector.reduce.*` over `select(M, X, identity)`
//                       (roadmap step R4; see `lowerActiveReduce`)
//   PrefixSum/PrefixProduct
//                    -> exclusive running sum/product of `select(M, X,
//                       identity)`, lane by lane, the same "lane loop for
//                       large W" `PrefixBitCount` uses (roadmap step R4;
//                       see `lowerPrefixReduce`)
//
// `getFirstActiveLane` is the one piece of arithmetic `IsFirstLane` and
// `AllEqual` share (see its own comment for why it never reads out of
// bounds, even when `M` is all-zero).
//
// `QuadOp`'s `llvm.dx.quad.read.*` family is raised (roadmap step R4) but
// not lowered here: quad ops need a fixed lane-to-quad mapping this target
// does not yet implement (an explicit v1 non-goal, see
// feme/docs/FeMeCPUDesign.md's "Non-Goals").
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/WaveLowering.h"

#include "feme/Transforms/CPU/BuiltinCalls.h"
#include "feme/Transforms/CPU/WaveCalls.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// A constant `<W x i32>` of `0, 1, ..., W-1` -- the lane index within the
/// wave (`feme::cpu::BuiltinCallKind::LaneIndex`), and the basis every other
/// builtin's flattened index is built from.
Constant *getLaneIota(LLVMContext &Ctx, unsigned WaveSize) {
  SmallVector<Constant *, 32> Lanes;
  Type *I32Ty = Type::getInt32Ty(Ctx);
  for (unsigned I = 0; I != WaveSize; ++I)
    Lanes.push_back(ConstantInt::get(I32Ty, I));
  return ConstantVector::get(Lanes);
}

/// Builds `WaveIndex * W + lane` in `<W x i32>` (see the file comment
/// above): the flattened thread id within the group, for every lane of the
/// wave at once.
Value *buildFlattenedThreadIdInGroup(IRBuilder<> &Builder, Value *WaveIndex,
                                     unsigned WaveSize) {
  Value *Base = Builder.CreateMul(WaveIndex, Builder.getInt32(WaveSize));
  Value *WideBase = Builder.CreateVectorSplat(WaveSize, Base);
  return Builder.CreateAdd(WideBase,
                           getLaneIota(Builder.getContext(), WaveSize));
}

/// Decomposes \p Flat (see `buildFlattenedThreadIdInGroup`) into thread group
/// dimension \p Component's (0/1/2 for x/y/z) thread-in-group id, per the
/// file comment above.
Value *decomposeComponent(IRBuilder<> &Builder, Value *Flat, unsigned Component,
                          uint32_t NumThreadsX, uint32_t NumThreadsY) {
  switch (Component) {
  case 0:
    return Builder.CreateURem(
        Flat, Builder.CreateVectorSplat(
                  cast<FixedVectorType>(Flat->getType())->getNumElements(),
                  Builder.getInt32(NumThreadsX)));
  case 1: {
    unsigned W = cast<FixedVectorType>(Flat->getType())->getNumElements();
    Value *DivX = Builder.CreateUDiv(
        Flat, Builder.CreateVectorSplat(W, Builder.getInt32(NumThreadsX)));
    return Builder.CreateURem(
        DivX, Builder.CreateVectorSplat(W, Builder.getInt32(NumThreadsY)));
  }
  case 2: {
    unsigned W = cast<FixedVectorType>(Flat->getType())->getNumElements();
    uint64_t XY = static_cast<uint64_t>(NumThreadsX) * NumThreadsY;
    return Builder.CreateUDiv(
        Flat, Builder.CreateVectorSplat(
                  W, ConstantInt::get(Builder.getInt32Ty(),
                                      static_cast<uint32_t>(XY))));
  }
  default:
    llvm_unreachable("component out of range");
  }
}

/// Lowers one matched `feme.cpu.builtin.*` call into the arithmetic the file
/// comment above describes, and replaces/erases the call.
void lowerBuiltinCall(const MatchedBuiltinCall &Matched) {
  CallInst &CI = *Matched.Call;
  IRBuilder<> Builder(&CI);
  unsigned W = Matched.WaveSize;

  Value *Result;
  switch (Matched.Kind) {
  case BuiltinCallKind::LaneIndex:
    Result = getLaneIota(Builder.getContext(), W);
    break;
  case BuiltinCallKind::FlattenedThreadIdInGroup:
    Result = buildFlattenedThreadIdInGroup(Builder, Matched.Env.WaveIndex, W);
    break;
  case BuiltinCallKind::ThreadIdInGroup: {
    Value *Flat =
        buildFlattenedThreadIdInGroup(Builder, Matched.Env.WaveIndex, W);
    Result = decomposeComponent(Builder, Flat, Matched.Component,
                                Matched.NumThreadsX, Matched.NumThreadsY);
    break;
  }
  case BuiltinCallKind::ThreadId: {
    Value *Flat =
        buildFlattenedThreadIdInGroup(Builder, Matched.Env.WaveIndex, W);
    Value *InGroup =
        decomposeComponent(Builder, Flat, Matched.Component,
                           Matched.NumThreadsX, Matched.NumThreadsY);
    Value *GroupIDComponent = Matched.Component == 0   ? Matched.Env.GroupIDX
                              : Matched.Component == 1 ? Matched.Env.GroupIDY
                                                       : Matched.Env.GroupIDZ;
    uint32_t NumThreadsComponent = Matched.Component == 0 ? Matched.NumThreadsX
                                   : Matched.Component == 1
                                       ? Matched.NumThreadsY
                                       : Matched.NumThreadsZ;
    Value *Scaled = Builder.CreateMul(GroupIDComponent,
                                      Builder.getInt32(NumThreadsComponent));
    Value *WideScaled = Builder.CreateVectorSplat(W, Scaled);
    Result = Builder.CreateAdd(WideScaled, InGroup);
    break;
  }
  }

  Result->takeName(&CI);
  CI.replaceAllUsesWith(Result);
  CI.eraseFromParent();
}

/// The wave's mask bitcast to an `iW` integer, alongside whether it is
/// entirely zero -- the common starting point for `IsFirstLane` and
/// `AllEqual`, both of which need "the first active lane" (`llvm.cttz`,
/// never poison since `is_zero_poison=false`, see "No lowering may create
/// poison merely because `M` is all-zero" in "Phase 5").
struct FirstActiveLane {
  Value *MaskAsInt; // `iW`
  Value *IsAllZero; // scalar `i1`
  Value *Cttz;      // `iW`: the first set bit's index, or `W` if none
};

FirstActiveLane getFirstActiveLane(IRBuilder<> &Builder, Value *WideMask,
                                   unsigned WaveSize) {
  Type *IWTy = IntegerType::get(Builder.getContext(), WaveSize);
  FirstActiveLane R;
  R.MaskAsInt = Builder.CreateBitCast(WideMask, IWTy);
  R.IsAllZero = Builder.CreateICmpEQ(R.MaskAsInt, ConstantInt::get(IWTy, 0));
  R.Cttz = Builder.CreateIntrinsic(IWTy, Intrinsic::cttz,
                                   {R.MaskAsInt, Builder.getFalse()});
  return R;
}

/// `wave.is.first.lane`: `M != 0 && lane == cttz(bitcast M to iW, false)`,
/// computed per lane (see the file comment's table). No clamping is needed
/// here (unlike `lowerAllEqual`'s use of the same first-active-lane index
/// to actually extract an element): an all-zero mask makes `Cttz` equal
/// `WaveSize`, which never equals any real lane index, and the leading
/// `M != 0` conjunct is false in that case regardless.
Value *lowerIsFirstLane(IRBuilder<> &Builder, Value *WideMask,
                        unsigned WaveSize) {
  FirstActiveLane First = getFirstActiveLane(Builder, WideMask, WaveSize);
  Value *FirstLaneI32 =
      Builder.CreateZExtOrTrunc(First.Cttz, Builder.getInt32Ty());
  Value *FirstLaneWide = Builder.CreateVectorSplat(WaveSize, FirstLaneI32);
  Value *LaneEq = Builder.CreateICmpEQ(
      getLaneIota(Builder.getContext(), WaveSize), FirstLaneWide);
  Value *NotZeroWide =
      Builder.CreateVectorSplat(WaveSize, Builder.CreateNot(First.IsAllZero));
  return Builder.CreateAnd(LaneEq, NotZeroWide);
}

/// The first active lane's index, clamped to lane 0 when the mask is
/// entirely zero -- unlike `lowerIsFirstLane` above, this feeds an actual
/// `extractelement`, so an out-of-range index (`WaveSize`, from `cttz` of a
/// zero mask) would be real undefined behaviour rather than merely an
/// always-false comparison.
Value *getClampedFirstActiveLaneIndex(IRBuilder<> &Builder, Value *WideMask,
                                      unsigned WaveSize) {
  FirstActiveLane First = getFirstActiveLane(Builder, WideMask, WaveSize);
  Value *Clamped = Builder.CreateSelect(
      First.IsAllZero, ConstantInt::get(First.Cttz->getType(), 0), First.Cttz);
  return Builder.CreateZExtOrTrunc(Clamped, Builder.getInt32Ty());
}

/// `wave.any`: `reduce.or(M & X)`.
Value *lowerAny(IRBuilder<> &Builder, Value *WideMask, Value *WideOperand) {
  return Builder.CreateOrReduce(Builder.CreateAnd(WideMask, WideOperand));
}

/// `wave.all`: `reduce.and(select(M, X, true))` -- an inactive lane
/// contributes `true`, the identity for `and`, so it can never make an
/// otherwise-all-true wave read as `false`.
Value *lowerAll(IRBuilder<> &Builder, Value *WideMask, Value *WideOperand) {
  Value *AllOnes = Constant::getAllOnesValue(WideOperand->getType());
  Value *Selected = Builder.CreateSelect(WideMask, WideOperand, AllOnes);
  return Builder.CreateAndReduce(Selected);
}

/// `wave.all.equal`: broadcast the first active lane's value, compare every
/// lane's value against it, and reduce under the mask the same way
/// `lowerAll` does (an inactive lane's comparison is forced `true`, so it
/// can't spoil an otherwise-equal wave -- and an all-inactive wave reads as
/// vacuously `true`, matching "no lowering may create poison merely because
/// `M` is all-zero").
Value *lowerAllEqual(IRBuilder<> &Builder, Value *WideMask, Value *WideOperand,
                     unsigned WaveSize) {
  Value *FirstLaneIdx =
      getClampedFirstActiveLaneIndex(Builder, WideMask, WaveSize);
  Value *FirstVal = Builder.CreateExtractElement(WideOperand, FirstLaneIdx);
  Value *FirstValWide = Builder.CreateVectorSplat(WaveSize, FirstVal);
  Value *Cmp = FirstVal->getType()->isFloatingPointTy()
                   ? Builder.CreateFCmpOEQ(WideOperand, FirstValWide)
                   : Builder.CreateICmpEQ(WideOperand, FirstValWide);
  Value *AllOnes = Constant::getAllOnesValue(Cmp->getType());
  Value *Selected = Builder.CreateSelect(WideMask, Cmp, AllOnes);
  return Builder.CreateAndReduce(Selected);
}

/// `wave.readlane(X, I)`: a genuine per-lane gather, not a uniform extract
/// -- `I` is not required uniform across the wave (see WaveCalls.h's
/// `ReadLane` documentation), so output lane `L` independently reads
/// source lane `I[L]`'s value, guarded the same way a uniform read would
/// be: zero if that source lane is inactive (see "No lowering may create
/// poison merely because `M` is all-zero" and the "read from an inactive
/// ... lane" rule in "Phase 5"). Built as an explicit lane loop -- the same
/// "lane loop for large W" option `lowerPrefixBitCount`/`lowerPrefixReduce`
/// use -- since each output lane's dynamic (not necessarily equal) source
/// index rules out a single vectorized `shufflevector`. A uniform `I` (the
/// common HLSL case) still produces the correct answer: every iteration
/// simply reads the same source lane.
Value *lowerReadLane(IRBuilder<> &Builder, Value *WideMask, Value *WideOperand,
                     Value *WideLaneIndex, unsigned WaveSize) {
  Type *ElemTy = cast<VectorType>(WideOperand->getType())->getElementType();
  Value *Zero = Constant::getNullValue(ElemTy);
  Value *Result = PoisonValue::get(FixedVectorType::get(ElemTy, WaveSize));
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *SrcIdx =
        Builder.CreateExtractElement(WideLaneIndex, Builder.getInt32(Lane));
    Value *LaneActive = Builder.CreateExtractElement(WideMask, SrcIdx);
    Value *RawVal = Builder.CreateExtractElement(WideOperand, SrcIdx);
    Value *Selected = Builder.CreateSelect(LaneActive, RawVal, Zero);
    Result =
        Builder.CreateInsertElement(Result, Selected, Builder.getInt32(Lane));
  }
  return Result;
}

/// `wave.active.countbits`: `ctpop(bitcast (M & X) to iW)`.
Value *lowerActiveCountBits(IRBuilder<> &Builder, Value *WideMask,
                            Value *WideOperand, unsigned WaveSize) {
  Value *AndX = Builder.CreateAnd(WideMask, WideOperand);
  Type *IWTy = IntegerType::get(Builder.getContext(), WaveSize);
  Value *AsInt = Builder.CreateBitCast(AndX, IWTy);
  Value *Popcount = Builder.CreateIntrinsic(IWTy, Intrinsic::ctpop, {AsInt});
  return Builder.CreateZExtOrTrunc(Popcount, Builder.getInt32Ty());
}

/// `wave.prefix.bitcount`: the exclusive running count of `M & X` set bits
/// before each lane. Built as an explicit lane loop (the "lane loop for
/// large `W`" option in "Phase 5"'s table) rather than a log2(W)-step
/// shuffle scan: `WaveSize` is a compile-time constant no larger than
/// `feme::cpu::MaxWaveSize`, so the unrolled loop is a bounded, fixed number
/// of instructions, matching the scalarization-style unrolled loops
/// elsewhere in this target (e.g. `feme::cpu::FunctionWidener`'s
/// `widenResourceCall`/`widenScalarizedFallback`).
Value *lowerPrefixBitCount(IRBuilder<> &Builder, Value *WideMask,
                           Value *WideOperand, unsigned WaveSize) {
  Value *AndX = Builder.CreateAnd(WideMask, WideOperand);
  Type *I32Ty = Builder.getInt32Ty();
  Value *Result = PoisonValue::get(FixedVectorType::get(I32Ty, WaveSize));
  Value *Accum = Builder.getInt32(0);
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Result = Builder.CreateInsertElement(Result, Accum, Builder.getInt32(Lane));
    Value *LaneBit = Builder.CreateExtractElement(AndX, Builder.getInt32(Lane));
    Accum = Builder.CreateAdd(Accum, Builder.CreateZExt(LaneBit, I32Ty));
  }
  return Result;
}

/// `WaveActiveBallot`: `bitcast (M & X) to iW`, split and zero-pad into the
/// source ABI's 32-bit result words (see "Phase 5"'s table). \p ResultTy is
/// the matched call's actual result type (DXIL's fixed `{i32, i32, i32,
/// i32}` -- see `feme::cpu::WaveCallKind::Ballot`'s comment); word \p I
/// (`0..NumWords-1`) is bits `[32*I, 32*I+32)` of the `W`-bit mask, or zero
/// once `32*I` reaches or exceeds `W` -- "Ballots always use the source
/// ABI's full result shape ..., zeroing words and high bits beyond `W`" in
/// "Phase 5". `WaveSize` is never wider than `feme::cpu::MaxWaveSize`
/// (128 bits, exactly the width of DXIL's four-word ballot ABI), so every
/// in-range word is a plain truncating (or, for `W` < 32, zero-extending)
/// shift/extract, never a shift by an out-of-range amount.
Value *lowerBallot(IRBuilder<> &Builder, Value *WideMask, Value *WideOperand,
                   unsigned WaveSize, Type *ResultTy) {
  Value *AndX = Builder.CreateAnd(WideMask, WideOperand);
  Type *IWTy = IntegerType::get(Builder.getContext(), WaveSize);
  Value *AsInt = Builder.CreateBitCast(AndX, IWTy);

  auto *StructTy = cast<StructType>(ResultTy);
  Value *Result = PoisonValue::get(StructTy);
  for (unsigned I = 0, NumWords = StructTy->getNumElements(); I != NumWords;
       ++I) {
    Type *WordTy = StructTy->getElementType(I);
    unsigned WordBits = WordTy->getIntegerBitWidth();
    Value *Word;
    if (I * WordBits >= WaveSize) {
      Word = ConstantInt::get(WordTy, 0);
    } else {
      Value *Shifted =
          I == 0
              ? AsInt
              : Builder.CreateLShr(AsInt, ConstantInt::get(IWTy, I * WordBits));
      Word = Builder.CreateZExtOrTrunc(Shifted, WordTy);
    }
    Result = Builder.CreateInsertValue(Result, Word, I);
  }
  return Result;
}

/// A masked reduction's identity element -- the value substituted for an
/// inactive lane (per \p WideMask) so it cannot affect the reduction's
/// result, matching "Phase 5"'s `llvm.vector.reduce.* over select(M, X,
/// identity)` row. \p Kind picks the identity appropriate to the
/// reduction/element-type pair; not every `WaveCallKind` this is called for
/// supports every element type (e.g. `ActiveUMax`/`ActiveBitAnd` are
/// integer-only per DXIL.td's `Overloads`), so only the combinations that
/// occur are handled.
Constant *getReduceIdentity(WaveCallKind Kind, Type *EltTy) {
  bool IsFP = EltTy->isFloatingPointTy();
  switch (Kind) {
  case WaveCallKind::ActiveSum:
  case WaveCallKind::PrefixSum:
    return Constant::getNullValue(EltTy); // additive identity: 0
  case WaveCallKind::ActiveProduct:
  case WaveCallKind::PrefixProduct:
    return IsFP ? ConstantFP::get(EltTy, 1.0)
                : ConstantInt::get(EltTy, 1); // multiplicative identity: 1
  case WaveCallKind::ActiveMax:
    return IsFP ? ConstantFP::getInfinity(EltTy, /*Negative=*/true)
                : ConstantInt::get(EltTy, APInt::getSignedMinValue(
                                              EltTy->getIntegerBitWidth()));
  case WaveCallKind::ActiveUMax:
    return Constant::getNullValue(EltTy); // unsigned min: 0
  case WaveCallKind::ActiveMin:
    return IsFP ? ConstantFP::getInfinity(EltTy, /*Negative=*/false)
                : ConstantInt::get(EltTy, APInt::getSignedMaxValue(
                                              EltTy->getIntegerBitWidth()));
  case WaveCallKind::ActiveUMin:
    return ConstantInt::getAllOnesValue(EltTy); // unsigned max: ~0
  case WaveCallKind::ActiveBitAnd:
    return ConstantInt::getAllOnesValue(EltTy);
  case WaveCallKind::ActiveBitOr:
  case WaveCallKind::ActiveBitXor:
    return Constant::getNullValue(EltTy);
  default:
    llvm_unreachable("not a reduction WaveCallKind");
  }
}

/// `WaveActiveSum/Product/Max/UMax/Min/UMin/BitAnd/BitOr/BitXor`:
/// `llvm.vector.reduce.*` over `select(M, X, identity)` (see "Phase 5"'s
/// table and `getReduceIdentity` above). Floating-point sum/product
/// reductions are marked `fast`: HLSL's wave reductions do not specify a
/// lane order to accumulate in, so the reassociation `fast` permits is
/// exactly what a cross-lane hardware reduction already does.
Value *lowerActiveReduce(IRBuilder<> &Builder, WaveCallKind Kind,
                         Value *WideMask, Value *WideOperand) {
  Type *EltTy = cast<VectorType>(WideOperand->getType())->getElementType();
  Value *Identity = ConstantVector::getSplat(
      cast<VectorType>(WideOperand->getType())->getElementCount(),
      getReduceIdentity(Kind, EltTy));
  Value *Selected = Builder.CreateSelect(WideMask, WideOperand, Identity);
  bool IsFP = EltTy->isFloatingPointTy();

  switch (Kind) {
  case WaveCallKind::ActiveSum: {
    if (!IsFP)
      return Builder.CreateAddReduce(Selected);
    auto *Reduced = cast<Instruction>(
        Builder.CreateFAddReduce(ConstantFP::get(EltTy, 0.0), Selected));
    Reduced->setFast(true);
    return Reduced;
  }
  case WaveCallKind::ActiveProduct: {
    if (!IsFP)
      return Builder.CreateMulReduce(Selected);
    auto *Reduced = cast<Instruction>(
        Builder.CreateFMulReduce(ConstantFP::get(EltTy, 1.0), Selected));
    Reduced->setFast(true);
    return Reduced;
  }
  case WaveCallKind::ActiveMax:
    return IsFP ? Builder.CreateFPMaxReduce(Selected)
                : Builder.CreateIntMaxReduce(Selected, /*IsSigned=*/true);
  case WaveCallKind::ActiveUMax:
    return Builder.CreateIntMaxReduce(Selected, /*IsSigned=*/false);
  case WaveCallKind::ActiveMin:
    return IsFP ? Builder.CreateFPMinReduce(Selected)
                : Builder.CreateIntMinReduce(Selected, /*IsSigned=*/true);
  case WaveCallKind::ActiveUMin:
    return Builder.CreateIntMinReduce(Selected, /*IsSigned=*/false);
  case WaveCallKind::ActiveBitAnd:
    return Builder.CreateAndReduce(Selected);
  case WaveCallKind::ActiveBitOr:
    return Builder.CreateOrReduce(Selected);
  case WaveCallKind::ActiveBitXor:
    return Builder.CreateXorReduce(Selected);
  default:
    llvm_unreachable("not an active-reduce WaveCallKind");
  }
}

/// `WavePrefixSum/PrefixProduct`: the exclusive running sum/product of
/// `select(M, X, identity)` before each lane. Built as an explicit lane
/// loop, the same "lane loop for large `W`" choice `lowerPrefixBitCount`
/// makes and for the same reason (a bounded, fixed number of instructions
/// at every supported wave size, rather than a log2(W)-step shuffle scan).
Value *lowerPrefixReduce(IRBuilder<> &Builder, WaveCallKind Kind,
                         Value *WideMask, Value *WideOperand,
                         unsigned WaveSize) {
  Type *EltTy = cast<VectorType>(WideOperand->getType())->getElementType();
  bool IsFP = EltTy->isFloatingPointTy();
  Constant *Identity = getReduceIdentity(Kind, EltTy);
  Value *WideIdentity =
      ConstantVector::getSplat(ElementCount::getFixed(WaveSize), Identity);
  Value *Masked = Builder.CreateSelect(WideMask, WideOperand, WideIdentity);

  Value *Result = PoisonValue::get(FixedVectorType::get(EltTy, WaveSize));
  Value *Accum = Identity;
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Result = Builder.CreateInsertElement(Result, Accum, Builder.getInt32(Lane));
    Value *LaneVal =
        Builder.CreateExtractElement(Masked, Builder.getInt32(Lane));
    if (Kind == WaveCallKind::PrefixSum)
      Accum = IsFP ? Builder.CreateFAdd(Accum, LaneVal)
                   : Builder.CreateAdd(Accum, LaneVal);
    else
      Accum = IsFP ? Builder.CreateFMul(Accum, LaneVal)
                   : Builder.CreateMul(Accum, LaneVal);
  }
  return Result;
}

/// Lowers one matched `feme.cpu.wave.*` call per the file comment's table
/// above, and replaces/erases the call.
void lowerWaveCall(const MatchedWaveCall &Matched) {
  CallInst &CI = *Matched.Call;
  IRBuilder<> Builder(&CI);
  unsigned W = Matched.WaveSize;

  Value *Result;
  switch (Matched.Kind) {
  case WaveCallKind::GetLaneCount:
    Result = Builder.getInt32(W);
    break;
  case WaveCallKind::IsFirstLane:
    Result = lowerIsFirstLane(Builder, Matched.WideMask, W);
    break;
  case WaveCallKind::Any:
    Result = lowerAny(Builder, Matched.WideMask, Matched.WideOperand);
    break;
  case WaveCallKind::All:
    Result = lowerAll(Builder, Matched.WideMask, Matched.WideOperand);
    break;
  case WaveCallKind::AllEqual:
    Result = lowerAllEqual(Builder, Matched.WideMask, Matched.WideOperand, W);
    break;
  case WaveCallKind::ReadLane:
    Result = lowerReadLane(Builder, Matched.WideMask, Matched.WideOperand,
                           Matched.WideLaneIndex, W);
    break;
  case WaveCallKind::ActiveCountBits:
    Result =
        lowerActiveCountBits(Builder, Matched.WideMask, Matched.WideOperand, W);
    break;
  case WaveCallKind::PrefixBitCount:
    Result =
        lowerPrefixBitCount(Builder, Matched.WideMask, Matched.WideOperand, W);
    break;
  case WaveCallKind::Ballot:
    Result = lowerBallot(Builder, Matched.WideMask, Matched.WideOperand, W,
                         CI.getType());
    break;
  case WaveCallKind::ActiveSum:
  case WaveCallKind::ActiveProduct:
  case WaveCallKind::ActiveMax:
  case WaveCallKind::ActiveUMax:
  case WaveCallKind::ActiveMin:
  case WaveCallKind::ActiveUMin:
  case WaveCallKind::ActiveBitAnd:
  case WaveCallKind::ActiveBitOr:
  case WaveCallKind::ActiveBitXor:
    Result = lowerActiveReduce(Builder, Matched.Kind, Matched.WideMask,
                               Matched.WideOperand);
    break;
  case WaveCallKind::PrefixSum:
  case WaveCallKind::PrefixProduct:
    Result = lowerPrefixReduce(Builder, Matched.Kind, Matched.WideMask,
                               Matched.WideOperand, W);
    break;
  }

  Result->takeName(&CI);
  CI.replaceAllUsesWith(Result);
  CI.eraseFromParent();
}

} // namespace

PreservedAnalyses WaveLoweringPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    for (Instruction &I : llvm::make_early_inc_range(instructions(F))) {
      auto *CI = dyn_cast<CallInst>(&I);
      if (!CI)
        continue;
      if (std::optional<MatchedBuiltinCall> Matched = matchBuiltinCall(*CI)) {
        lowerBuiltinCall(*Matched);
        Changed = true;
        continue;
      }
      if (std::optional<MatchedWaveCall> Matched = matchWaveCall(*CI)) {
        lowerWaveCall(*Matched);
        Changed = true;
        continue;
      }
    }
  }

  // A `feme.cpu.builtin.*`/`feme.cpu.wave.*` declaration left behind once
  // its last caller is rewritten away has nothing left to select it.
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (F.isDeclaration() && F.use_empty() &&
        (F.getName().starts_with("feme.cpu.builtin.") ||
         F.getName().starts_with("feme.cpu.wave.")))
      F.eraseFromParent();

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
