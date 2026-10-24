//===- AttributeImplTest.cpp - Legacy attribute profiling tests
//-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../lib/IR/AttributeImpl.h"
#include "gtest/gtest.h"

namespace {

TEST(AttributeImpl, EnumProfile) {
  llvm::FoldingSetNodeID Actual, Expected;
  dxil::AttributeImpl::Profile(Actual, dxil::Attribute::NoReturn, 0);
  Expected.AddInteger(dxil::Attribute::NoReturn);
  EXPECT_EQ(Expected, Actual);
}

TEST(AttributeImpl, IntegerProfile) {
  llvm::FoldingSetNodeID Actual, Expected;
  dxil::AttributeImpl::Profile(Actual, dxil::Attribute::Alignment, 16);
  Expected.AddInteger(dxil::Attribute::Alignment);
  Expected.AddInteger(uint64_t(16));
  EXPECT_EQ(Expected, Actual);
}

TEST(AttributeImpl, StringProfile) {
  llvm::FoldingSetNodeID Actual, Expected;
  dxil::AttributeImpl::Profile(Actual, "target-cpu", "generic");
  Expected.AddString("target-cpu");
  Expected.AddString("generic");
  EXPECT_EQ(Expected, Actual);

  Actual = llvm::FoldingSetNodeID();
  Expected = llvm::FoldingSetNodeID();
  dxil::AttributeImpl::Profile(Actual, "no-frame-pointer-elim", "");
  Expected.AddString("no-frame-pointer-elim");
  EXPECT_EQ(Expected, Actual);
}

} // namespace
