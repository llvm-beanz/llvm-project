//===- OptionsTest.cpp - Tests for feme::frontend::getOptTable -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Frontend/Options.h"

#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "gtest/gtest.h"

using namespace feme::frontend;
using namespace llvm::opt;

namespace {

TEST(OptionsTest, RecognizesHelpFlag) {
  const OptTable &Opts = getOptTable();
  unsigned MissingIndex, MissingCount;
  const char *Args[] = {"--help"};
  InputArgList Parsed = Opts.ParseArgs(Args, MissingIndex, MissingCount);
  EXPECT_EQ(MissingCount, 0u);
  EXPECT_TRUE(Parsed.hasArg(OPT_help));
}

TEST(OptionsTest, RecognizesJoinedFromEquals) {
  const OptTable &Opts = getOptTable();
  unsigned MissingIndex, MissingCount;
  const char *Args[] = {"--from=dxil"};
  InputArgList Parsed = Opts.ParseArgs(Args, MissingIndex, MissingCount);
  ASSERT_EQ(MissingCount, 0u);
  const Arg *A = Parsed.getLastArg(OPT_from_EQ);
  ASSERT_NE(A, nullptr);
  EXPECT_STREQ(A->getValue(), "dxil");
}

TEST(OptionsTest, FlagsUnknownOptions) {
  const OptTable &Opts = getOptTable();
  unsigned MissingIndex, MissingCount;
  const char *Args[] = {"--not-a-real-option"};
  InputArgList Parsed = Opts.ParseArgs(Args, MissingIndex, MissingCount);
  bool SawUnknown = false;
  for (const Arg *A : Parsed.filtered(OPT_UNKNOWN)) {
    (void)A;
    SawUnknown = true;
  }
  EXPECT_TRUE(SawUnknown);
}

} // namespace
