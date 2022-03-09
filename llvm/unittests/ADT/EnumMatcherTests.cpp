//===- llvm/unittest/ADT/EnumMatcherTests.cpp -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/EnumMatcher.h"
#include "gtest/gtest.h"

using namespace llvm;

enum Doggos {
  Floofer,
  Woofer,
  SubWoofer,
  Pupper,
  Pupperino,
  Longboi,
};

TEST(EnumMatcher, MatchCase) {
  {
    bool Val = isInEnumSet<Doggos, Woofer, SubWoofer>(Woofer);
    ASSERT_TRUE(Val);
  }
  {
    bool Val = isInEnumSet<Doggos, Woofer, SubWoofer>(SubWoofer);
    ASSERT_TRUE(Val);
  }
  {
    bool Val = isInEnumSet<Doggos, Woofer, SubWoofer>(Pupper);
    ASSERT_FALSE(Val);
  }
}
