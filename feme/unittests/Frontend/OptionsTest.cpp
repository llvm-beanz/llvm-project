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

TEST(OptionsTest, RecognizesJoinedTargetEquals) {
  const OptTable &Opts = getOptTable();
  unsigned MissingIndex, MissingCount;
  const char *Args[] = {"--target=spirv"};
  InputArgList Parsed = Opts.ParseArgs(Args, MissingIndex, MissingCount);
  ASSERT_EQ(MissingCount, 0u);
  const Arg *A = Parsed.getLastArg(OPT_target_EQ);
  ASSERT_NE(A, nullptr);
  EXPECT_STREQ(A->getValue(), "spirv");
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

TEST(OptionsTest, RecognizesOptimizationLevelFlags) {
  const OptTable &Opts = getOptTable();
  unsigned MissingIndex, MissingCount;
  const char *Args[] = {"-O0", "-O1", "-O2", "-O3", "-Od"};
  InputArgList Parsed = Opts.ParseArgs(Args, MissingIndex, MissingCount);
  ASSERT_EQ(MissingCount, 0u);
  EXPECT_TRUE(Parsed.hasArg(OPT_O0));
  EXPECT_TRUE(Parsed.hasArg(OPT_O1));
  EXPECT_TRUE(Parsed.hasArg(OPT_O2));
  EXPECT_TRUE(Parsed.hasArg(OPT_O3));
  // `-Od` is a plain Alias<O0> (see Options.td), so it parses as an OPT_O0
  // Arg rather than a distinct OPT_Od one.
  const Arg *Last = Parsed.getLastArg(OPT_O0, OPT_O1, OPT_O2, OPT_O3);
  ASSERT_NE(Last, nullptr);
  EXPECT_EQ(Last->getOption().getID(), OPT_O0);
}

} // namespace
