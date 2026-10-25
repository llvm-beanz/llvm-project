//===- PatternMatchTest.cpp - Legacy constant matchers
//----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/PatternMatch.h"
#include "dxil/IR/LLVMContext.h"
#include "gtest/gtest.h"

using namespace dxil;
using namespace dxil::PatternMatch;

namespace {

TEST(PatternMatch, IntegerScalarsAndSplats) {
  LLVMContext C;
  for (unsigned Width : {1u, 8u, 32u, 65u, 128u}) {
    SCOPED_TRACE(Width);
    auto *Ty = IntegerType::get(C, Width);
    auto *One = ConstantInt::get(Ty, 1);
    const llvm::APInt *Value = nullptr;
    ASSERT_TRUE(match(One, m_APInt(Value)));
    EXPECT_EQ(&One->getValue(), Value);
    EXPECT_TRUE(match(One, m_One()));
    EXPECT_FALSE(match(One, m_Zero()));
    auto *Splat = ConstantVector::getSplat(4, One);
    ASSERT_TRUE(match(Splat, m_APInt(Value)));
    EXPECT_EQ(&One->getValue(), Value);
    EXPECT_TRUE(match(Splat, m_One()));
    auto *WithUndef = ConstantVector::get({One, UndefValue::get(Ty), One, One});
    EXPECT_FALSE(match(WithUndef, m_APInt(Value)));
    EXPECT_FALSE(match(WithUndef, m_One()));

    auto *Zero = ConstantInt::get(Ty, 0);
    auto *Mixed = ConstantVector::get({One, Zero, One, Zero});
    EXPECT_FALSE(match(Mixed, m_APInt(Value)));
    EXPECT_FALSE(match(Mixed, m_One()));
    EXPECT_FALSE(match(Mixed, m_Zero()));
    EXPECT_FALSE(match(Zero, m_One()));
  }
}

TEST(PatternMatch, ZeroAndUndef) {
  LLVMContext C;
  auto *IntTy = Type::getInt32Ty(C);
  auto *FloatTy = Type::getFloatTy(C);
  Type *Types[] = {IntTy, FloatTy, Type::getInt8PtrTy(C),
                   VectorType::get(IntTy, 4)};
  for (Type *Ty : Types) {
    auto *Zero = Constant::getNullValue(Ty);
    auto *Undef = UndefValue::get(Ty);
    EXPECT_TRUE(match(Zero, m_Zero()));
    EXPECT_FALSE(match(Zero, m_Undef()));
    EXPECT_TRUE(match(Undef, m_Undef()));
    EXPECT_FALSE(match(Undef, m_Zero()));
    EXPECT_FALSE(match(Undef, m_One()));
    const llvm::APInt *Value = nullptr;
    EXPECT_FALSE(match(Undef, m_APInt(Value)));
  }
  auto *PositiveZero = ConstantFP::get(FloatTy, 0.0);
  auto *NegativeZero = ConstantFP::getNegativeZero(FloatTy);
  EXPECT_TRUE(match(PositiveZero, m_Zero()));
  EXPECT_FALSE(match(NegativeZero, m_Zero()));
  EXPECT_FALSE(match(PositiveZero, m_One()));
  const llvm::APInt *Value = nullptr;
  EXPECT_FALSE(match(PositiveZero, m_APInt(Value)));
}

} // namespace
