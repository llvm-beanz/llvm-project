//===- RootConstantLoweringTest.cpp - Constant row lowering tests --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/RootConstantLowering.h"
#include "feme/Transforms/CPU/UnsupportedOps.h"
#include "feme/Transforms/DXIL/OpRaising.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

std::unique_ptr<Module> makeModule(LLVMContext &Context, Type *ElementTy,
                                   uint64_t ElementSize, uint32_t RangeSize) {
  auto M = std::make_unique<Module>("constants", Context);
  auto *F = Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                             GlobalValue::ExternalLinkage, "main", *M);
  IRBuilder<> B(BasicBlock::Create(Context, "entry", F));
  auto *HandleTy = TargetExtType::get(
      Context, "dx.CBuffer", {ArrayType::get(B.getInt8Ty(), ElementSize)});
  auto *HandleFn = Intrinsic::getOrInsertDeclaration(
      M.get(), Intrinsic::dx_resource_handlefrombinding, {HandleTy});
  auto *Handle = B.CreateCall(
      HandleFn, {B.getInt32(7), B.getInt32(3), B.getInt32(RangeSize),
                 B.getInt32(0), ConstantPointerNull::get(B.getPtrTy())});
  unsigned Components = 128 / ElementTy->getScalarSizeInBits();
  SmallVector<Type *, 9> Overloads(Components, ElementTy);
  auto *RowTy = StructType::get(Context, Overloads);
  Overloads.push_back(HandleTy);
  auto *LoadFn = Intrinsic::getOrInsertDeclaration(
      M.get(), feme::dxil::getCBufferRowIntrinsic(*RowTy), Overloads);
  B.CreateCall(LoadFn, {Handle, B.getInt32(0)});
  B.CreateRetVoid();
  return M;
}

TEST(RootConstantLoweringTest, CanonicalShapesRejectMalformedAggregates) {
  LLVMContext C;
  auto *I32 = Type::getInt32Ty(C);
  EXPECT_EQ(feme::dxil::getCBufferRowIntrinsic(*StructType::get(C)),
            Intrinsic::not_intrinsic);
  EXPECT_EQ(feme::dxil::getCBufferRowIntrinsic(*StructType::create(C)),
            Intrinsic::not_intrinsic);
  EXPECT_EQ(feme::dxil::getCBufferRowIntrinsic(*StructType::get(C, {I32, I32})),
            Intrinsic::not_intrinsic);
  EXPECT_EQ(feme::dxil::getCBufferRowIntrinsic(
                *StructType::get(C, {I32, I32, I32, Type::getFloatTy(C)})),
            Intrinsic::not_intrinsic);
}

TEST(RootConstantLoweringTest, LowersAllSixShapesAndVerifiesIR) {
  LLVMContext C;
  SmallVector<Type *, 6> Types{Type::getInt16Ty(C), Type::getHalfTy(C),
                               Type::getInt32Ty(C), Type::getFloatTy(C),
                               Type::getInt64Ty(C), Type::getDoubleTy(C)};
  for (Type *Ty : Types) {
    auto M = makeModule(C, Ty, 16, 1);
    auto Access = matchRootConstantAccess(*M->getFunction("main"));
    ASSERT_TRUE(Access);
    EXPECT_EQ(Access->ElementSize, 16u);
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
    ModuleAnalysisManager AM;
    RootConstantLoweringPass().run(*M, AM);
    EXPECT_FALSE(verifyModule(*M, &errs()));
    EXPECT_FALSE(matchRootConstantAccess(*M->getFunction("main")));
    EXPECT_EQ(M->getFunction("main")->arg_size(), 2u);
  }
}

TEST(RootConstantLoweringTest, ChecksExactABIByteSpanLimit) {
  LLVMContext C;
  auto Max = makeModule(C, Type::getInt32Ty(C), UINT32_MAX, 1);
  EXPECT_TRUE(matchRootConstantAccess(*Max->getFunction("main")));
  for (auto [Size, Range] :
       {std::pair<uint64_t, uint32_t>{uint64_t(UINT32_MAX) + 1, 1},
        {uint64_t(1) << 31, 2},
        {UINT32_MAX, 2},
        {16, UINT32_MAX}}) {
    auto M = makeModule(C, Type::getInt32Ty(C), Size, Range);
    EXPECT_FALSE(matchRootConstantAccess(*M->getFunction("main")));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  }
}

} // namespace
