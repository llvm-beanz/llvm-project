//===- WaveCallsTest.cpp - Tests for `feme.cpu.wave.*` call helpers -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/WaveCalls.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

/// A tiny module with one function whose entry block a test can build a
/// call into, and a ready-made `<WaveSize x i1>` mask value.
struct Harness {
  LLVMContext Ctx;
  std::unique_ptr<Module> M;
  Function *F;
  IRBuilder<> Builder;
  Value *Mask;

  explicit Harness(unsigned WaveSize)
      : M(std::make_unique<Module>("m", Ctx)),
        F(Function::Create(FunctionType::get(Type::getVoidTy(Ctx), false),
                           GlobalValue::ExternalLinkage, "f", M.get())),
        Builder(BasicBlock::Create(Ctx, "entry", F)) {
    Builder.CreateRetVoid();
    Builder.SetInsertPoint(F->getEntryBlock().getTerminator());
    Mask = Constant::getAllOnesValue(
        FixedVectorType::get(Type::getInt1Ty(Ctx), WaveSize));
  }
};

TEST(WaveCallsTest, IsDivergentWaveCallResultMatchesDesignTable) {
  // Only `IsFirstLane` and `PrefixBitCount` produce a genuinely per-lane
  // result (see "Phase 5" in feme/docs/FeMeCPUDesign.md); every other kind
  // reduces/broadcasts to the same scalar answer on every lane.
  EXPECT_TRUE(isDivergentWaveCallResult(WaveCallKind::IsFirstLane));
  EXPECT_TRUE(isDivergentWaveCallResult(WaveCallKind::PrefixBitCount));
  EXPECT_FALSE(isDivergentWaveCallResult(WaveCallKind::GetLaneCount));
  EXPECT_FALSE(isDivergentWaveCallResult(WaveCallKind::Any));
  EXPECT_FALSE(isDivergentWaveCallResult(WaveCallKind::All));
  EXPECT_FALSE(isDivergentWaveCallResult(WaveCallKind::AllEqual));
  EXPECT_FALSE(isDivergentWaveCallResult(WaveCallKind::ReadLane));
  EXPECT_FALSE(isDivergentWaveCallResult(WaveCallKind::ActiveCountBits));
}

TEST(WaveCallsTest, GetLaneCountRoundTrips) {
  Harness H(4);
  CallInst *CI =
      createWaveCall(H.Builder, WaveCallKind::GetLaneCount, 4, nullptr);
  EXPECT_TRUE(CI->getType()->isIntegerTy(32));

  std::optional<MatchedWaveCall> Matched = matchWaveCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, WaveCallKind::GetLaneCount);
  EXPECT_EQ(Matched->WaveSize, 4u);
  EXPECT_EQ(Matched->WideMask, nullptr);
  EXPECT_EQ(Matched->WideOperand, nullptr);
  EXPECT_FALSE(verifyModule(*H.M, &errs()));
}

TEST(WaveCallsTest, IsFirstLaneRoundTrips) {
  Harness H(4);
  CallInst *CI =
      createWaveCall(H.Builder, WaveCallKind::IsFirstLane, 4, H.Mask);
  EXPECT_EQ(CI->getType(), FixedVectorType::get(Type::getInt1Ty(H.Ctx), 4));

  std::optional<MatchedWaveCall> Matched = matchWaveCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, WaveCallKind::IsFirstLane);
  EXPECT_EQ(Matched->WideMask, H.Mask);
  EXPECT_EQ(Matched->WideOperand, nullptr);
  EXPECT_FALSE(verifyModule(*H.M, &errs()));
}

TEST(WaveCallsTest, AnyRoundTrips) {
  Harness H(4);
  Value *Operand = Constant::getAllOnesValue(
      FixedVectorType::get(Type::getInt1Ty(H.Ctx), 4));
  CallInst *CI =
      createWaveCall(H.Builder, WaveCallKind::Any, 4, H.Mask, Operand);
  EXPECT_TRUE(CI->getType()->isIntegerTy(1));

  std::optional<MatchedWaveCall> Matched = matchWaveCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, WaveCallKind::Any);
  EXPECT_EQ(Matched->WideMask, H.Mask);
  EXPECT_EQ(Matched->WideOperand, Operand);
  EXPECT_FALSE(verifyModule(*H.M, &errs()));
}

TEST(WaveCallsTest, AllEqualIsTypeOverloaded) {
  // `AllEqual` (and `ReadLane`) are overloaded on the operand's scalar
  // type, so an `i32` and an `f32` instance must not collide on the same
  // callee (see WaveCalls.cpp's `mangleWaveCallName`).
  Harness H(4);
  Value *IntOperand = ConstantVector::getSplat(
      ElementCount::getFixed(4), ConstantInt::get(Type::getInt32Ty(H.Ctx), 0));
  Value *FloatOperand = ConstantVector::getSplat(
      ElementCount::getFixed(4), ConstantFP::get(Type::getFloatTy(H.Ctx), 0.0));

  CallInst *IntCall =
      createWaveCall(H.Builder, WaveCallKind::AllEqual, 4, H.Mask, IntOperand);
  CallInst *FloatCall = createWaveCall(H.Builder, WaveCallKind::AllEqual, 4,
                                       H.Mask, FloatOperand);
  EXPECT_NE(IntCall->getCalledFunction(), FloatCall->getCalledFunction());

  std::optional<MatchedWaveCall> MatchedInt = matchWaveCall(*IntCall);
  std::optional<MatchedWaveCall> MatchedFloat = matchWaveCall(*FloatCall);
  ASSERT_TRUE(MatchedInt);
  ASSERT_TRUE(MatchedFloat);
  EXPECT_EQ(MatchedInt->Kind, WaveCallKind::AllEqual);
  EXPECT_EQ(MatchedFloat->Kind, WaveCallKind::AllEqual);
  EXPECT_FALSE(verifyModule(*H.M, &errs()));
}

TEST(WaveCallsTest, ReadLaneCarriesLaneIndexOperand) {
  Harness H(4);
  Value *Operand = ConstantVector::getSplat(
      ElementCount::getFixed(4), ConstantInt::get(Type::getInt32Ty(H.Ctx), 7));
  Value *LaneIndex = ConstantVector::getSplat(
      ElementCount::getFixed(4), ConstantInt::get(Type::getInt32Ty(H.Ctx), 0));
  CallInst *CI = createWaveCall(H.Builder, WaveCallKind::ReadLane, 4, H.Mask,
                                Operand, LaneIndex);
  EXPECT_TRUE(CI->getType()->isIntegerTy(32));

  std::optional<MatchedWaveCall> Matched = matchWaveCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, WaveCallKind::ReadLane);
  EXPECT_EQ(Matched->WideOperand, Operand);
  EXPECT_EQ(Matched->WideLaneIndex, LaneIndex);
  EXPECT_FALSE(verifyModule(*H.M, &errs()));
}

TEST(WaveCallsTest, BallotRoundTrips) {
  Harness H(4);
  Value *Operand = Constant::getAllOnesValue(
      FixedVectorType::get(Type::getInt1Ty(H.Ctx), 4));
  CallInst *CI =
      createWaveCall(H.Builder, WaveCallKind::Ballot, 4, H.Mask, Operand);
  Type *I32Ty = Type::getInt32Ty(H.Ctx);
  EXPECT_EQ(CI->getType(),
            StructType::get(H.Ctx, {I32Ty, I32Ty, I32Ty, I32Ty}));
  EXPECT_FALSE(isDivergentWaveCallResult(WaveCallKind::Ballot));

  std::optional<MatchedWaveCall> Matched = matchWaveCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, WaveCallKind::Ballot);
  EXPECT_EQ(Matched->WideMask, H.Mask);
  EXPECT_EQ(Matched->WideOperand, Operand);
  EXPECT_FALSE(verifyModule(*H.M, &errs()));
}

TEST(WaveCallsTest, MatchWaveCallRejectsUnrelatedCall) {
  Harness H(4);
  Function *Unrelated = Function::Create(
      FunctionType::get(Type::getVoidTy(H.Ctx), false),
      GlobalValue::ExternalLinkage, "not.a.wave.call", H.M.get());
  CallInst *CI = H.Builder.CreateCall(Unrelated, {});
  EXPECT_FALSE(matchWaveCall(*CI));
}

TEST(WaveCallsTest, ActiveSumRoundTrips) {
  Harness H(4);
  Value *Operand = ConstantVector::getSplat(
      ElementCount::getFixed(4), ConstantInt::get(Type::getInt32Ty(H.Ctx), 1));
  CallInst *CI =
      createWaveCall(H.Builder, WaveCallKind::ActiveSum, 4, H.Mask, Operand);
  EXPECT_TRUE(CI->getType()->isIntegerTy(32));
  EXPECT_FALSE(isDivergentWaveCallResult(WaveCallKind::ActiveSum));

  std::optional<MatchedWaveCall> Matched = matchWaveCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, WaveCallKind::ActiveSum);
  EXPECT_EQ(Matched->WideMask, H.Mask);
  EXPECT_EQ(Matched->WideOperand, Operand);
  EXPECT_FALSE(verifyModule(*H.M, &errs()));
}

TEST(WaveCallsTest, PrefixSumRoundTripsAndIsDivergent) {
  Harness H(4);
  Value *Operand = ConstantVector::getSplat(
      ElementCount::getFixed(4), ConstantInt::get(Type::getInt32Ty(H.Ctx), 1));
  CallInst *CI =
      createWaveCall(H.Builder, WaveCallKind::PrefixSum, 4, H.Mask, Operand);
  EXPECT_EQ(CI->getType(), FixedVectorType::get(Type::getInt32Ty(H.Ctx), 4));
  EXPECT_TRUE(isDivergentWaveCallResult(WaveCallKind::PrefixSum));

  std::optional<MatchedWaveCall> Matched = matchWaveCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, WaveCallKind::PrefixSum);
  EXPECT_FALSE(verifyModule(*H.M, &errs()));
}

TEST(WaveCallsTest, ActiveMaxIsTypeOverloaded) {
  // Like `AllEqual`/`ReadLane`, the roadmap step R4 reduce/scan kinds are
  // overloaded on the operand's scalar type, so an `i32` and an `f32`
  // instance must not collide on the same callee.
  Harness H(4);
  Value *IntOperand = ConstantVector::getSplat(
      ElementCount::getFixed(4), ConstantInt::get(Type::getInt32Ty(H.Ctx), 0));
  Value *FloatOperand = ConstantVector::getSplat(
      ElementCount::getFixed(4), ConstantFP::get(Type::getFloatTy(H.Ctx), 0.0));

  CallInst *IntCall =
      createWaveCall(H.Builder, WaveCallKind::ActiveMax, 4, H.Mask, IntOperand);
  CallInst *FloatCall = createWaveCall(H.Builder, WaveCallKind::ActiveMax, 4,
                                       H.Mask, FloatOperand);
  EXPECT_NE(IntCall->getCalledFunction(), FloatCall->getCalledFunction());
  EXPECT_TRUE(IntCall->getType()->isIntegerTy(32));
  EXPECT_TRUE(FloatCall->getType()->isFloatTy());
  EXPECT_FALSE(verifyModule(*H.M, &errs()));
}

} // namespace
