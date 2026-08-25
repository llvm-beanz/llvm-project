//===- StageOpsTest.cpp - Tests for feme::StageOps -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/StageOps.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace llvm;

namespace {

class StageOpsTest : public ::testing::Test {
protected:
  LLVMContext Ctx;
  std::unique_ptr<Module> M = std::make_unique<Module>("m", Ctx);
  Function *F =
      Function::Create(FunctionType::get(Type::getVoidTy(Ctx), false),
                       GlobalValue::ExternalLinkage, "entry", M.get());
  BasicBlock *BB = BasicBlock::Create(Ctx, "", F);
  IRBuilder<> B{BB};
};

TEST_F(StageOpsTest, InputLoadRoundTrips) {
  Type *I32 = B.getInt32Ty();
  Value *Row = ConstantInt::get(I32, 0);
  Value *Component = ConstantInt::get(I32, 1);
  Value *Vertex = ConstantInt::get(I32, 0);
  CallInst *CI =
      createStageInputLoad(B, B.getFloatTy(), 3, Row, Component, Vertex);
  ASSERT_TRUE(CI);
  EXPECT_EQ(CI->getType(), B.getFloatTy());
  EXPECT_EQ(CI->getCalledFunction()->getName(), "feme.stage.input.load.f32");

  StageOpKind Kind;
  ASSERT_TRUE(isStageOpCall(*CI, &Kind));
  EXPECT_EQ(Kind, StageOpKind::InputLoad);

  ASSERT_EQ(getStageOpConstantOperand(*CI, 0), 3u);
  ASSERT_EQ(getStageOpConstantOperand(*CI, 1), 0u);
  ASSERT_EQ(getStageOpConstantOperand(*CI, 2), 1u);
  B.CreateRetVoid();
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST_F(StageOpsTest, DifferentOverloadsGetDistinctDeclarations) {
  Type *I32 = B.getInt32Ty();
  Value *Zero = ConstantInt::get(I32, 0);
  CallInst *FloatLoad =
      createStageInputLoad(B, B.getFloatTy(), 0, Zero, Zero, Zero);
  CallInst *IntLoad = createStageInputLoad(B, I32, 1, Zero, Zero, Zero);
  EXPECT_NE(FloatLoad->getCalledFunction(), IntLoad->getCalledFunction());
  EXPECT_EQ(IntLoad->getCalledFunction()->getName(),
            "feme.stage.input.load.i32");
}

TEST_F(StageOpsTest, OutputStoreIsVoidAndOverloadedOnValue) {
  Type *I32 = B.getInt32Ty();
  Value *Zero = ConstantInt::get(I32, 0);
  Value *Val = ConstantFP::get(B.getFloatTy(), 1.0);
  CallInst *CI = createStageOutputStore(B, 2, Zero, Zero, Val, Zero);
  EXPECT_TRUE(CI->getType()->isVoidTy());
  EXPECT_EQ(CI->getCalledFunction()->getName(), "feme.stage.output.store.f32");
  ASSERT_EQ(getStageOpConstantOperand(*CI, 0), 2u);
}

TEST_F(StageOpsTest, DiscardAndDemoteAreNotOverloaded) {
  Value *Cond = B.getInt1(true);
  CallInst *Discard = createStageDiscard(B, Cond);
  CallInst *Demote = createStageDemote(B, Cond);
  EXPECT_EQ(Discard->getCalledFunction()->getName(), "feme.stage.discard");
  EXPECT_EQ(Demote->getCalledFunction()->getName(), "feme.stage.demote");

  StageOpKind Kind;
  ASSERT_TRUE(isStageOpCall(*Discard, &Kind));
  EXPECT_EQ(Kind, StageOpKind::Discard);
  ASSERT_TRUE(isStageOpCall(*Demote, &Kind));
  EXPECT_EQ(Kind, StageOpKind::Demote);
}

TEST_F(StageOpsTest, IsHelperTakesNoOperands) {
  CallInst *CI = createStageIsHelper(B);
  EXPECT_EQ(CI->arg_size(), 0u);
  EXPECT_TRUE(CI->getType()->isIntegerTy(1));
  EXPECT_EQ(CI->getCalledFunction()->getName(), "feme.stage.is_helper");
}

TEST_F(StageOpsTest, DerivativesPreserveKindAndType) {
  Value *Val = ConstantFP::get(B.getFloatTy(), 1.0);
  CallInst *DdxFine =
      createStageDerivative(B, StageOpKind::DerivativeXFine, Val);
  EXPECT_EQ(DdxFine->getCalledFunction()->getName(),
            "feme.stage.derivative.x.fine.f32");
  StageOpKind Kind;
  ASSERT_TRUE(isStageOpCall(*DdxFine, &Kind));
  EXPECT_EQ(Kind, StageOpKind::DerivativeXFine);
}

TEST_F(StageOpsTest, QuadReadCarriesDirection) {
  Value *Val = ConstantInt::get(B.getInt32Ty(), 7);
  CallInst *CI = createStageQuadRead(B, Val, /*Direction=*/2);
  EXPECT_EQ(CI->getCalledFunction()->getName(), "feme.stage.quad.read.i32");
  ASSERT_EQ(getStageOpConstantOperand(*CI, 1), 2u);
}

TEST_F(StageOpsTest, InterpolateAtVariants) {
  Type *I32 = B.getInt32Ty();
  Value *Zero = ConstantInt::get(I32, 0);
  CallInst *Centroid =
      createStageInterpolateAtCentroid(B, B.getFloatTy(), 4, Zero);
  CallInst *Sample =
      createStageInterpolateAtSample(B, B.getFloatTy(), 4, Zero, Zero);
  CallInst *Offset =
      createStageInterpolateAtOffset(B, B.getFloatTy(), 4, Zero, Zero, Zero);
  EXPECT_EQ(Centroid->getCalledFunction()->getName(),
            "feme.stage.interpolate.at.centroid.f32");
  EXPECT_EQ(Sample->getCalledFunction()->getName(),
            "feme.stage.interpolate.at.sample.f32");
  EXPECT_EQ(Offset->getCalledFunction()->getName(),
            "feme.stage.interpolate.at.offset.f32");
  EXPECT_EQ(Sample->arg_size(), 3u);
  EXPECT_EQ(Offset->arg_size(), 4u);
}

TEST_F(StageOpsTest, StreamEmitAndCutCarryStreamIndex) {
  CallInst *Emit = createStageStreamEmit(B, /*Stream=*/1);
  CallInst *Cut = createStageStreamCut(B, /*Stream=*/1);
  EXPECT_EQ(Emit->getCalledFunction()->getName(), "feme.stage.stream.emit");
  EXPECT_EQ(Cut->getCalledFunction()->getName(), "feme.stage.stream.cut");
  EXPECT_FALSE(isStageOpKindOverloaded(StageOpKind::StreamEmit));
  ASSERT_EQ(getStageOpConstantOperand(*Emit, 0), 1u);
  ASSERT_EQ(getStageOpConstantOperand(*Cut, 0), 1u);
  StageOpKind Kind;
  ASSERT_TRUE(isStageOpCall(*Emit, &Kind));
  EXPECT_EQ(Kind, StageOpKind::StreamEmit);
}

TEST_F(StageOpsTest, SubpassLoadCarriesAttachmentIndexAndComponent) {
  CallInst *CI = createStageSubpassLoad(B, /*AttachmentIndex=*/2,
                                       /*Component=*/1);
  EXPECT_EQ(CI->getCalledFunction()->getName(), "feme.stage.subpass.load.f32");
  // Marked overloaded purely to give SIMDizePass's widened `<W x f32>` form
  // its own distinct symbol from this scalar declaration -- see
  // StageOpKind::SubpassLoad's comment -- not because a real call ever
  // varies its result type.
  EXPECT_TRUE(isStageOpKindOverloaded(StageOpKind::SubpassLoad));
  EXPECT_TRUE(CI->getType()->isFloatTy());
  ASSERT_EQ(getStageOpConstantOperand(*CI, 0), 2u);
  ASSERT_EQ(getStageOpConstantOperand(*CI, 1), 1u);
  StageOpKind Kind;
  ASSERT_TRUE(isStageOpCall(*CI, &Kind));
  EXPECT_EQ(Kind, StageOpKind::SubpassLoad);
}

TEST_F(StageOpsTest, NonStageOpCallIsRejected) {
  FunctionCallee Callee =
      M->getOrInsertFunction("not.a.stage.op", B.getVoidTy());
  CallInst *CI = B.CreateCall(Callee, {});
  EXPECT_FALSE(isStageOpCall(*CI));
}

TEST_F(StageOpsTest, NonConstantOperandIsRejected) {
  // Re-declare an entry point with one argument, to get a non-constant
  // value to probe (the fixture's `entry` is a no-argument `void()`).
  FunctionType *FTy =
      FunctionType::get(B.getInt32Ty(), {B.getInt32Ty()}, false);
  Function *F2 =
      Function::Create(FTy, GlobalValue::ExternalLinkage, "f2", M.get());
  Value *Arg = F2->getArg(0);
  Value *Zero = ConstantInt::get(B.getInt32Ty(), 0);
  CallInst *CI = createStageInputLoad(B, B.getFloatTy(), 0, Arg, Zero, Zero);
  EXPECT_FALSE(getStageOpConstantOperand(*CI, 1).has_value());
}

} // namespace
