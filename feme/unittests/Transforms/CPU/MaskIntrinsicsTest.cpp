//===- MaskIntrinsicsTest.cpp - Tests for `feme.cpu.mask*` calls ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/MaskIntrinsics.h"

#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
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
      M->getOrInsertFunction("feme.cpu.mask.any.not.actually",
                             CI->getFunctionType()),
      {Mask});
  EXPECT_FALSE(isMaskAnyCall(*Unrelated));
}

TEST_F(MaskIntrinsicsTest, MaskedLoadMangleAndRoundTrip) {
  IRBuilder<> Builder(BB);
  Value *Ptr = ConstantPointerNull::get(PointerType::get(Ctx, 0));
  Value *Mask = Builder.getInt1(true);
  Value *Passthru = Builder.getInt32(0);
  CallInst *CI =
      createMaskedLoad(Builder, Ptr, /*Align=*/4, Mask, Passthru, "load");
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

TEST_F(MaskIntrinsicsTest, MaskedAtomicRMWMangleAndRoundTrip) {
  IRBuilder<> Builder(BB);
  Value *Ptr = ConstantPointerNull::get(PointerType::get(Ctx, 0));
  Value *Mask = Builder.getInt1(true);
  Value *Val = Builder.getInt32(1);
  CallInst *CI = createMaskedAtomicRMW(Builder, AtomicRMWInst::Add, Ptr, Val,
                                       /*Align=*/4, Mask, "rmw");
  EXPECT_EQ(CI->getCalledFunction()->getName(),
            "feme.cpu.masked.atomicrmw.i32");

  std::optional<MatchedMaskedAtomicRMW> Matched = matchMaskedAtomicRMW(*CI);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Op, AtomicRMWInst::Add);
  EXPECT_EQ(Matched->Ptr, Ptr);
  EXPECT_EQ(Matched->Val, Val);
  EXPECT_EQ(Matched->Align, 4u);
  EXPECT_EQ(Matched->Mask, Mask);

  EXPECT_FALSE(matchMaskedLoad(*CI));
  EXPECT_FALSE(matchMaskedStore(*CI));
}

// H4e: an element type `appendScalarMangling` does not recognize (a
// matrix/aggregate shape, represented here by a struct type, most notably)
// must diagnose gracefully through the element type's own `LLVMContext`
// rather than `llvm_unreachable`-crash the whole process, and every
// `createMasked*` entry point above it must report the failure back to its
// caller as `nullptr` instead of handing back a call built from a null
// callee.
TEST_F(MaskIntrinsicsTest,
       MaskedLoadUnsupportedElementTypeDiagnosesGracefully) {
  IRBuilder<> Builder(BB);
  Value *Ptr = ConstantPointerNull::get(PointerType::get(Ctx, 0));
  Value *Mask = Builder.getInt1(true);
  StructType *MatrixLikeTy =
      StructType::get(Ctx, {Type::getFloatTy(Ctx), Type::getFloatTy(Ctx)});
  Value *Passthru = ConstantAggregateZero::get(MatrixLikeTy);

  std::string ErrorMessage;
  Ctx.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() != DS_Error)
          return;
        std::string &Out = *reinterpret_cast<std::string *>(Handle);
        raw_string_ostream OS(Out);
        DiagnosticPrinterRawOStream Printer(OS);
        DI->print(Printer);
      },
      &ErrorMessage);

  CallInst *CI =
      createMaskedLoad(Builder, Ptr, /*Align=*/4, Mask, Passthru, "load");
  EXPECT_EQ(CI, nullptr);
  EXPECT_NE(ErrorMessage.find("unsupported feme.cpu.masked.* element type"),
            std::string::npos)
      << "actual diagnostic: " << ErrorMessage;
}

TEST_F(MaskIntrinsicsTest,
       MaskedStoreUnsupportedElementTypeDiagnosesGracefully) {
  IRBuilder<> Builder(BB);
  Value *Ptr = ConstantPointerNull::get(PointerType::get(Ctx, 0));
  Value *Mask = Builder.getInt1(true);
  StructType *MatrixLikeTy =
      StructType::get(Ctx, {Type::getFloatTy(Ctx), Type::getFloatTy(Ctx)});
  Value *Val = ConstantAggregateZero::get(MatrixLikeTy);

  std::string ErrorMessage;
  Ctx.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() != DS_Error)
          return;
        *reinterpret_cast<std::string *>(Handle) = "error";
      },
      &ErrorMessage);

  CallInst *CI = createMaskedStore(Builder, Val, Ptr, /*Align=*/4, Mask);
  EXPECT_EQ(CI, nullptr);
  EXPECT_EQ(ErrorMessage, "error");
}

TEST_F(MaskIntrinsicsTest,
       MaskedAtomicRMWUnsupportedElementTypeDiagnosesGracefully) {
  IRBuilder<> Builder(BB);
  Value *Ptr = ConstantPointerNull::get(PointerType::get(Ctx, 0));
  Value *Mask = Builder.getInt1(true);
  StructType *MatrixLikeTy =
      StructType::get(Ctx, {Type::getFloatTy(Ctx), Type::getFloatTy(Ctx)});
  Value *Val = ConstantAggregateZero::get(MatrixLikeTy);

  std::string ErrorMessage;
  Ctx.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Handle) {
        if (DI->getSeverity() != DS_Error)
          return;
        *reinterpret_cast<std::string *>(Handle) = "error";
      },
      &ErrorMessage);

  CallInst *CI = createMaskedAtomicRMW(Builder, AtomicRMWInst::Add, Ptr, Val,
                                       /*Align=*/4, Mask, "rmw");
  EXPECT_EQ(CI, nullptr);
  EXPECT_EQ(ErrorMessage, "error");
}

} // namespace
