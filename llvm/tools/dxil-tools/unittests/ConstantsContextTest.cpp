//===- ConstantsContextTest.cpp - Legacy constant uniquing
//------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../lib/IR/LLVMContextImpl.h"
#include "gtest/gtest.h"

namespace {

TEST(ConstantsContext, IntegerKeysPreserveBitWidths) {
  llvm::DenseMap<llvm::APInt, unsigned, dxil::DenseMapAPIntKeyInfo> Map;
  EXPECT_TRUE(Map.try_emplace(llvm::APInt(1, 1), 1).second);
  EXPECT_TRUE(Map.try_emplace(llvm::APInt(64, 1), 64).second);
  EXPECT_TRUE(Map.try_emplace(llvm::APInt(128, 1), 128).second);
  EXPECT_FALSE(Map.try_emplace(llvm::APInt(128, 1), 0).second);
  EXPECT_EQ(3u, Map.size());
  EXPECT_EQ(1u, Map.lookup(llvm::APInt(1, 1)));
  EXPECT_EQ(64u, Map.lookup(llvm::APInt(64, 1)));
  EXPECT_EQ(128u, Map.lookup(llvm::APInt(128, 1)));
  EXPECT_TRUE(Map.erase(llvm::APInt(64, 1)));
  EXPECT_EQ(128u, Map.lookup(llvm::APInt(128, 1)));
  EXPECT_TRUE(Map.try_emplace(llvm::APInt(64, 1), 42).second);
  EXPECT_EQ(42u, Map.lookup(llvm::APInt(64, 1)));
}

TEST(ConstantsContext, FloatingKeysPreserveSemanticsAndBits) {
  llvm::DenseMap<llvm::APFloat, unsigned, dxil::DenseMapAPFloatKeyInfo> Map;
  const auto &Single = llvm::APFloat::IEEEsingle();
  const auto &Double = llvm::APFloat::IEEEdouble();
  llvm::APFloat PosZero = llvm::APFloat::getZero(Single);
  llvm::APFloat NegZero = llvm::APFloat::getZero(Single, true);
  llvm::APFloat DoubleZero = llvm::APFloat::getZero(Double);
  llvm::APFloat NaN1(Single, llvm::APInt(32, 0x7fc00001));
  llvm::APFloat NaN2(Single, llvm::APInt(32, 0x7fc00002));

  EXPECT_TRUE(Map.try_emplace(PosZero, 1).second);
  EXPECT_TRUE(Map.try_emplace(NegZero, 2).second);
  EXPECT_TRUE(Map.try_emplace(DoubleZero, 3).second);
  EXPECT_TRUE(Map.try_emplace(NaN1, 4).second);
  EXPECT_TRUE(Map.try_emplace(NaN2, 5).second);
  EXPECT_FALSE(Map.try_emplace(NaN1, 0).second);
  EXPECT_EQ(5u, Map.size());
  EXPECT_EQ(1u, Map.lookup(PosZero));
  EXPECT_EQ(2u, Map.lookup(NegZero));
  EXPECT_EQ(3u, Map.lookup(DoubleZero));
  EXPECT_EQ(4u, Map.lookup(NaN1));
  EXPECT_EQ(5u, Map.lookup(NaN2));
  EXPECT_TRUE(Map.erase(NaN1));
  EXPECT_EQ(5u, Map.lookup(NaN2));
}

} // namespace
