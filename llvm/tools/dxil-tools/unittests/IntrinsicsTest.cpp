//===- IntrinsicsTest.cpp - Legacy intrinsic name recognition
//--------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/Intrinsics.h"
#include "llvm/ADT/StringRef.h"
#include "gtest/gtest.h"
#include <cstring>

namespace dxil {
namespace {

Intrinsic::ID recognizeIntrinsic(llvm::StringRef FullName) {
  using llvm::StringRef;
  const char *Name = FullName.data();
  unsigned Len = FullName.size();

#define GET_FUNCTION_RECOGNIZER
#include "dxil/IR/Intrinsics.gen"
#undef GET_FUNCTION_RECOGNIZER

  return Intrinsic::not_intrinsic;
}

TEST(Intrinsics, RecognizesLegacyNames) {
  EXPECT_EQ(Intrinsic::assume, recognizeIntrinsic("llvm.assume"));
  EXPECT_EQ(Intrinsic::dbg_value, recognizeIntrinsic("llvm.dbg.value"));
  EXPECT_EQ(Intrinsic::log, recognizeIntrinsic("llvm.log.f32"));
  EXPECT_EQ(Intrinsic::log10, recognizeIntrinsic("llvm.log10.f64"));
  EXPECT_EQ(Intrinsic::log2, recognizeIntrinsic("llvm.log2.v4f32"));
  EXPECT_EQ(Intrinsic::memcpy, recognizeIntrinsic("llvm.memcpy.p0i8.p0i8.i32"));
}

TEST(Intrinsics, RejectsUnknownNames) {
  EXPECT_EQ(Intrinsic::not_intrinsic,
            recognizeIntrinsic("llvm.not_an_intrinsic"));
  EXPECT_EQ(Intrinsic::not_intrinsic, recognizeIntrinsic("llvm.assume.extra"));
  EXPECT_EQ(Intrinsic::not_intrinsic, recognizeIntrinsic("llvm.logarithm.f32"));
  EXPECT_EQ(Intrinsic::not_intrinsic, recognizeIntrinsic("llvm.log2"));
}

} // namespace
} // namespace dxil
