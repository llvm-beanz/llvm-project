//===- ImageCallsTest.cpp - Tests for `feme.cpu.image.*` calls -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ImageCalls.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

class ImageCallsTest : public testing::Test {
protected:
  LLVMContext Ctx;
  std::unique_ptr<Module> M;
  Function *F = nullptr;
  BasicBlock *BB = nullptr;

  void SetUp() override {
    M = std::make_unique<Module>("M", Ctx);
    FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
    F = Function::Create(FTy, Function::ExternalLinkage, "main", M.get());
    BB = BasicBlock::Create(Ctx, "entry", F);
  }

  ImageCallEnv makeEnv(IRBuilderBase &Builder) {
    ImageCallEnv Env;
    Env.ImageHeap = ConstantPointerNull::get(PointerType::get(Ctx, 0));
    Env.ImageHeapCount = Builder.getInt32(1);
    return Env;
  }
};

// Roadmap H19l: `matchImageCall`'s own `AllKinds` lookup table (used to
// recognize a call by its mangled callee name before dispatching to the
// per-kind operand-extraction switch) never listed `Store2DMS`/
// `Store2DMSI32` at all -- an oversight from roadmap H19g's own original
// implementation, silently unexercised the whole time because H19g's own
// feature bit stayed `VK_FALSE` (blocked first by roadmap H19k's `feme-cpu-
// linearize` gap) until a real CTS re-run could ever reach
// `feme::cpu::FunctionWidener::widenImageCall`'s `matchImageCall` call for
// this specific call kind. The result: every multisampled storage-image
// store was silently never widened at all, and its own vector-typed
// `Texel` operand's divergent-vector-decomposition consumer check (in
// `checkAndPrepareForWidening`) fell through to `feme-cpu-simdize`'s
// generic "not yet supported" diagnostic -- a shape that looked identical
// to a real missing decomposition pattern, but was actually a simple
// name-table omission, confirmed by reducing a real failing
// `dEQP-VK.image.load_store_multisample.2d.*` case down to its exact IR
// shape (see "Roadmap H19l: measured impact" in VulkanCTSReport.md).
TEST_F(ImageCallsTest, MatchesStore2DMSCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *Texel = ConstantVector::get(
      {ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 1.0)});
  CallInst *CI = createStore2DMS(Builder, Env, Builder.getInt32(3),
                                 Builder.getInt32(1), Builder.getInt32(2),
                                 Builder.getInt32(0), Texel,
                                 Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Store2DMS);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->U, Builder.getInt32(1));
  EXPECT_EQ(Matched->V, Builder.getInt32(2));
  EXPECT_EQ(Matched->Sample, Builder.getInt32(0));
  EXPECT_EQ(Matched->Texel, Texel);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

TEST_F(ImageCallsTest, MatchesStore2DMSI32Call) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *Texel = ConstantVector::get(
      {Builder.getInt32(0), Builder.getInt32(0), Builder.getInt32(0),
       Builder.getInt32(0)});
  CallInst *CI = createStore2DMSI32(Builder, Env, Builder.getInt32(3),
                                    Builder.getInt32(1), Builder.getInt32(2),
                                    Builder.getInt32(0), Texel,
                                    Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Store2DMSI32);
  EXPECT_EQ(Matched->Texel, Texel);
  EXPECT_EQ(Matched->Sample, Builder.getInt32(0));
}

// Roadmap H19m: `Store2DArrayMS`/`Store2DArrayMSI32` are new call kinds
// combining `Store2DArray`'s own `Layer` operand and `Store2DMS`'s own
// `Sample` operand -- neither existing kind had a spare operand slot for
// the other axis, so a dedicated pair of kinds was added rather than
// widening either in place (see `ImageCalls.h`'s own comment). This test
// guards against a repeat of H19l's own `AllKinds` omission bug for these
// two new kinds specifically.
TEST_F(ImageCallsTest, MatchesStore2DArrayMSCall) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *Texel = ConstantVector::get(
      {ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 0.0),
       ConstantFP::get(Builder.getFloatTy(), 1.0)});
  CallInst *CI = createStore2DArrayMS(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(1),
      Builder.getInt32(2), Builder.getInt32(4), Builder.getInt32(0), Texel,
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Store2DArrayMS);
  EXPECT_EQ(Matched->Call, CI);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->U, Builder.getInt32(1));
  EXPECT_EQ(Matched->V, Builder.getInt32(2));
  EXPECT_EQ(Matched->Layer, Builder.getInt32(4));
  EXPECT_EQ(Matched->Sample, Builder.getInt32(0));
  EXPECT_EQ(Matched->Texel, Texel);
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

TEST_F(ImageCallsTest, MatchesStore2DArrayMSI32Call) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  Value *Texel = ConstantVector::get(
      {Builder.getInt32(0), Builder.getInt32(0), Builder.getInt32(0),
       Builder.getInt32(0)});
  CallInst *CI = createStore2DArrayMSI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(1),
      Builder.getInt32(2), Builder.getInt32(4), Builder.getInt32(0), Texel,
      Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Store2DArrayMSI32);
  EXPECT_EQ(Matched->Layer, Builder.getInt32(4));
  EXPECT_EQ(Matched->Sample, Builder.getInt32(0));
  EXPECT_EQ(Matched->Texel, Texel);
}

// Roadmap H19m: `Load2DArrayI32` was widened in place to add a `Sample`
// operand before `Mask` -- confirm `matchImageCall` extracts a real
// (non-zero) sample value through the new operand position correctly.
TEST_F(ImageCallsTest, MatchesLoad2DArrayI32CallWithRealSample) {
  IRBuilder<> Builder(BB);
  ImageCallEnv Env = makeEnv(Builder);
  CallInst *CI = createLoad2DArrayI32(
      Builder, Env, Builder.getInt32(3), Builder.getInt32(1),
      Builder.getInt32(2), Builder.getInt32(4), Builder.getInt32(0),
      Builder.getInt32(5), Builder.getInt1(true));
  Builder.CreateRetVoid();

  std::optional<MatchedImageCall> Matched = matchImageCall(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Load2DArrayI32);
  EXPECT_EQ(Matched->ImageIndex, Builder.getInt32(3));
  EXPECT_EQ(Matched->U, Builder.getInt32(1));
  EXPECT_EQ(Matched->V, Builder.getInt32(2));
  EXPECT_EQ(Matched->Layer, Builder.getInt32(4));
  EXPECT_EQ(Matched->Lod, Builder.getInt32(0));
  EXPECT_EQ(Matched->Sample, Builder.getInt32(5));
  EXPECT_EQ(Matched->Mask, Builder.getInt1(true));
}

} // namespace
