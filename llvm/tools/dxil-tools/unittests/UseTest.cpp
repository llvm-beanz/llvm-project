//===- UseTest.cpp - Legacy operand waymarking tests
//------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/User.h"
#include "gtest/gtest.h"
#include <new>

namespace {

using UserRef = dxil::Use::UserRef;

TEST(Use, IntrusiveOperandWaymarking) {
  for (unsigned Count : {1, 2, 3, 19, 20, 21, 31, 32, 63, 64, 1000}) {
    SCOPED_TRACE(Count);
    auto *Operands = static_cast<dxil::Use *>(
        ::operator new(Count * sizeof(dxil::Use) + sizeof(UserRef)));
    auto *End = Operands + Count;
    auto *Ref = new (End) UserRef(nullptr, 0);
    EXPECT_EQ(Operands, dxil::Use::initTags(Operands, End));
    for (auto *U = Operands; U != End; ++U) {
      EXPECT_EQ(nullptr, U->get());
      EXPECT_EQ(static_cast<void *>(End), static_cast<void *>(U->getUser()));
    }
    Ref->~UserRef();
    dxil::Use::zap(Operands, End, true);
  }
}

TEST(Use, HungOffOperandWaymarking) {
  for (unsigned Count : {1, 2, 3, 19, 20, 21, 31, 32, 63, 64, 1000}) {
    SCOPED_TRACE(Count);
    auto *Operands = static_cast<dxil::Use *>(
        ::operator new(Count * sizeof(dxil::Use) + sizeof(UserRef)));
    auto *End = Operands + Count;
    auto *Ref = new (End) UserRef(nullptr, 1);
    EXPECT_EQ(Operands, dxil::Use::initTags(Operands, End));
    for (auto *U = Operands; U != End; ++U) {
      EXPECT_EQ(nullptr, U->get());
      EXPECT_EQ(nullptr, U->getUser());
    }
    Ref->~UserRef();
    dxil::Use::zap(Operands, End, true);
  }
}

} // namespace
