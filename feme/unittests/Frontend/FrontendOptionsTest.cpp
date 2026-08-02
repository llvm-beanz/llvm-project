//===- FrontendOptionsTest.cpp - Tests for feme::frontend::parseArgs -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Frontend/FrontendOptions.h"

#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace feme::frontend;

namespace {

TEST(FrontendOptionsTest, ParsesFullTranslationInvocation) {
  std::string Diags;
  llvm::raw_string_ostream DiagsOS(Diags);
  const char *Args[] = {"--from=dxil", "--to=spirv", "-o", "out.spv",
                        "input.dxil"};
  std::optional<DriverOptions> Opts = parseArgs(Args, DiagsOS);
  ASSERT_TRUE(Opts.has_value()) << Diags;
  EXPECT_FALSE(Opts->ShowHelp);
  EXPECT_FALSE(Opts->ShowVersion);
  EXPECT_EQ(Opts->From, "dxil");
  EXPECT_EQ(Opts->To, "spirv");
  EXPECT_EQ(Opts->OutputFilename, "out.spv");
  EXPECT_EQ(Opts->InputFilename, "input.dxil");
}

TEST(FrontendOptionsTest, HelpDoesNotRequireInputFile) {
  std::string Diags;
  llvm::raw_string_ostream DiagsOS(Diags);
  const char *Args[] = {"--help"};
  std::optional<DriverOptions> Opts = parseArgs(Args, DiagsOS);
  ASSERT_TRUE(Opts.has_value()) << Diags;
  EXPECT_TRUE(Opts->ShowHelp);
}

TEST(FrontendOptionsTest, FailsWithoutInputFile) {
  std::string Diags;
  llvm::raw_string_ostream DiagsOS(Diags);
  std::optional<DriverOptions> Opts = parseArgs({}, DiagsOS);
  EXPECT_FALSE(Opts.has_value());
  EXPECT_FALSE(Diags.empty());
}

TEST(FrontendOptionsTest, FailsOnUnknownOption) {
  std::string Diags;
  llvm::raw_string_ostream DiagsOS(Diags);
  const char *Args[] = {"--not-a-real-option", "input.dxil"};
  std::optional<DriverOptions> Opts = parseArgs(Args, DiagsOS);
  EXPECT_FALSE(Opts.has_value());
  EXPECT_FALSE(Diags.empty());
}

TEST(FrontendOptionsTest, FailsOnMultipleInputFiles) {
  std::string Diags;
  llvm::raw_string_ostream DiagsOS(Diags);
  const char *Args[] = {"input1.dxil", "input2.dxil"};
  std::optional<DriverOptions> Opts = parseArgs(Args, DiagsOS);
  EXPECT_FALSE(Opts.has_value());
  EXPECT_FALSE(Diags.empty());
}

} // namespace
