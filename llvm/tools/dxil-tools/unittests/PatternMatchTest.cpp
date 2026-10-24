//===- PatternMatchTest.cpp - Legacy constant pattern predicates -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/PatternMatch.h"
#include "llvm/IR/PatternMatch.h"
#include "gtest/gtest.h"

namespace {

TEST(PatternMatch, AllOnesPredicate) {
  dxil::PatternMatch::is_all_ones Predicate;
  for (unsigned Width : {1u, 8u, 32u, 65u, 128u}) {
    SCOPED_TRACE(Width);
    EXPECT_TRUE(Predicate.isValue(llvm::APInt::getAllOnes(Width)));
    EXPECT_FALSE(Predicate.isValue(llvm::APInt::getZero(Width)));
    if (Width > 1) {
      EXPECT_FALSE(Predicate.isValue(llvm::APInt(Width, 1)));
      EXPECT_FALSE(Predicate.isValue(llvm::APInt::getSignedMinValue(Width)));
    }
  }
}

TEST(PatternMatch, SignBitPredicate) {
  dxil::PatternMatch::is_sign_bit Predicate;
  for (unsigned Width : {1u, 8u, 32u, 65u, 128u}) {
    SCOPED_TRACE(Width);
    llvm::APInt SignBit = llvm::APInt::getSignedMinValue(Width);
    EXPECT_TRUE(Predicate.isValue(SignBit));
    EXPECT_FALSE(Predicate.isValue(llvm::APInt::getZero(Width)));
    if (Width > 1) {
      EXPECT_FALSE(Predicate.isValue(llvm::APInt(Width, 1)));
      EXPECT_FALSE(Predicate.isValue(llvm::APInt::getAllOnes(Width)));
      EXPECT_FALSE(Predicate.isValue(SignBit | 1));
    }
  }
}

} // namespace
