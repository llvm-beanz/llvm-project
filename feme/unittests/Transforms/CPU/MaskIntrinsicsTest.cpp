//===- MaskIntrinsicsTest.cpp - Tests for `feme.cpu.mask*` calls ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/MaskIntrinsics.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

class MaskIntrinsicsTest : public testing::Test {
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
};

TEST_F(MaskIntrinsicsTest, MaskAnyRoundTrips) {
  IRBuilder<> Builder(BB);
  Value *Mask = Builder.getInt1(true);
  CallInst *CI = createMaskAny(Builder, Mask, "any");
  EXPECT_EQ(CI->getCalledFunction()->getName(), "feme.cpu.mask.any");
  EXPECT_TRUE(isMaskAnyCall(*CI));

  CallInst *Unrelated = Builder.CreateCall(
      M->getOrInsertFunction("feme.cpu.mask.any.not.actually", CI->getFunctionType()),
      {Mask});
  EXPECT_FALSE(isMaskAnyCall(*Unrelated));
}

TEST_F(MaskIntrinsicsTest, MaskedLoadMangleAndRoundTrip) {
  IRBuilder<> Builder(BB);
  Value *Ptr = ConstantPointerNull::get(PointerType::get(Ctx, 0));
  Value *Mask = Builder.getInt1(true);
  Value *Passthru = Builder.getInt32(0);
  CallInst *CI = createMaskedLoad(Builder, Ptr, /*Align=*/4, Mask, Passthru,
                                  "load");
  EXPECT_EQ(CI->getCalledFunction()->getName(), "feme.cpu.masked.load.i32");

  std::optional<MatchedMaskedMemOp> Matched = matchMaskedLoad(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Ptr, Ptr);
  EXPECT_EQ(Matched->Align, 4u);
  EXPECT_EQ(Matched->Mask, Mask);
  EXPECT_EQ(Matched->ValueOperand, Passthru);

  EXPECT_FALSE(matchMaskedStore(*CI));
}

TEST_F(MaskIntrinsicsTest, MaskedStoreMangleAndRoundTrip) {
  IRBuilder<> Builder(BB);
  Value *Ptr = ConstantPointerNull::get(PointerType::get(Ctx, 0));
  Value *Mask = Builder.getInt1(true);
  Value *Val = ConstantFP::get(Type::getFloatTy(Ctx), 1.0);
  CallInst *CI = createMaskedStore(Builder, Val, Ptr, /*Align=*/4, Mask);
  EXPECT_EQ(CI->getCalledFunction()->getName(), "feme.cpu.masked.store.f32");

  std::optional<MatchedMaskedMemOp> Matched = matchMaskedStore(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Ptr, Ptr);
  EXPECT_EQ(Matched->Align, 4u);
  EXPECT_EQ(Matched->Mask, Mask);
  EXPECT_EQ(Matched->ValueOperand, Val);

  EXPECT_FALSE(matchMaskedLoad(*CI));
}

} // namespace
