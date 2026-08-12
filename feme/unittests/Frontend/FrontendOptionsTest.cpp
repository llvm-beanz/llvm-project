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
  const char *Args[] = {"--target=spirv", "-o", "out.spv", "input.dxil"};
  std::optional<DriverOptions> Opts = parseArgs(Args, DiagsOS);
  ASSERT_TRUE(Opts.has_value()) << Diags;
  EXPECT_FALSE(Opts->ShowHelp);
  EXPECT_FALSE(Opts->ShowVersion);
  EXPECT_EQ(Opts->Target, "spirv");
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

TEST(FrontendOptionsTest, DefaultsToOptLevelO0) {
  std::string Diags;
  llvm::raw_string_ostream DiagsOS(Diags);
  const char *Args[] = {"input.dxil"};
  std::optional<DriverOptions> Opts = parseArgs(Args, DiagsOS);
  ASSERT_TRUE(Opts.has_value()) << Diags;
  EXPECT_EQ(Opts->OptLevel, llvm::OptimizationLevel::O0);
}

TEST(FrontendOptionsTest, ParsesEachOptimizationLevel) {
  struct Case {
    const char *Flag;
    llvm::OptimizationLevel Expected;
  };
  const Case Cases[] = {
      {"-O0", llvm::OptimizationLevel::O0},
      {"-O1", llvm::OptimizationLevel::O1},
      {"-O2", llvm::OptimizationLevel::O2},
      {"-O3", llvm::OptimizationLevel::O3},
      // `-Od` is DXC/clang-cl's spelling for "disable optimizations",
      // aliased to `-O0` (see Options.td).
      {"-Od", llvm::OptimizationLevel::O0},
  };
  for (const Case &C : Cases) {
    std::string Diags;
    llvm::raw_string_ostream DiagsOS(Diags);
    const char *Args[] = {C.Flag, "input.dxil"};
    std::optional<DriverOptions> Opts = parseArgs(Args, DiagsOS);
    ASSERT_TRUE(Opts.has_value()) << C.Flag << ": " << Diags;
    EXPECT_EQ(Opts->OptLevel, C.Expected) << C.Flag;
  }
}

TEST(FrontendOptionsTest, LastOptimizationLevelFlagWins) {
  std::string Diags;
  llvm::raw_string_ostream DiagsOS(Diags);
  const char *Args[] = {"-O2", "-O0", "input.dxil"};
  std::optional<DriverOptions> Opts = parseArgs(Args, DiagsOS);
  ASSERT_TRUE(Opts.has_value()) << Diags;
  EXPECT_EQ(Opts->OptLevel, llvm::OptimizationLevel::O0);
}

TEST(FrontendOptionsTest, WaveSizeDefaultsToUnset) {
  std::string Diags;
  llvm::raw_string_ostream DiagsOS(Diags);
  const char *Args[] = {"input.dxil"};
  std::optional<DriverOptions> Opts = parseArgs(Args, DiagsOS);
  ASSERT_TRUE(Opts.has_value()) << Diags;
  EXPECT_FALSE(Opts->WaveSize.has_value());
}

TEST(FrontendOptionsTest, ParsesWaveSize) {
  std::string Diags;
  llvm::raw_string_ostream DiagsOS(Diags);
  const char *Args[] = {"--wave-size=16", "input.dxil"};
  std::optional<DriverOptions> Opts = parseArgs(Args, DiagsOS);
  ASSERT_TRUE(Opts.has_value()) << Diags;
  ASSERT_TRUE(Opts->WaveSize.has_value());
  EXPECT_EQ(*Opts->WaveSize, 16u);
}

TEST(FrontendOptionsTest, FailsOnNonNumericWaveSize) {
  std::string Diags;
  llvm::raw_string_ostream DiagsOS(Diags);
  const char *Args[] = {"--wave-size=abc", "input.dxil"};
  std::optional<DriverOptions> Opts = parseArgs(Args, DiagsOS);
  EXPECT_FALSE(Opts.has_value());
  EXPECT_FALSE(Diags.empty());
}

} // namespace
