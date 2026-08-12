//===- WaveSizeTest.cpp - Tests for feme::cpu::resolveWaveSize -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Covers every row of the "Wave Size Selection" resolution table in
// feme/docs/FeMeCPUDesign.md, plus the out-of-range/non-power-of-two and
// user/shader conflict diagnostics.
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/WaveSize.h"

#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme::cpu;

namespace {

/// Resolves and asserts success, returning the resolved wave size.
unsigned resolveOrFail(std::optional<unsigned> User,
                       std::optional<ShaderWaveSizeRequirement> Shader,
                       unsigned HostVectorBits = 128) {
  llvm::Expected<unsigned> Result =
      resolveWaveSize(User, Shader, HostVectorBits);
  EXPECT_TRUE(!!Result) << llvm::toString(Result.takeError());
  return Result ? *Result : 0;
}

/// Resolves and asserts failure, returning the diagnostic text.
std::string resolveOrFailExpectingError(
    std::optional<unsigned> User,
    std::optional<ShaderWaveSizeRequirement> Shader,
    unsigned HostVectorBits = 128) {
  llvm::Expected<unsigned> Result =
      resolveWaveSize(User, Shader, HostVectorBits);
  EXPECT_FALSE(!!Result);
  return llvm::toString(Result.takeError());
}

TEST(WaveSizeTest, NeitherSetUsesHostDerivedDefault) {
  // max(4, HostVectorBits / 32), rounded down to a power of two.
  EXPECT_EQ(resolveOrFail(std::nullopt, std::nullopt, /*HostVectorBits=*/128),
           4u);
  EXPECT_EQ(resolveOrFail(std::nullopt, std::nullopt, /*HostVectorBits=*/256),
           8u);
  EXPECT_EQ(resolveOrFail(std::nullopt, std::nullopt, /*HostVectorBits=*/512),
           16u);
  // A vector-less host still gets a legal minimum, not zero.
  EXPECT_EQ(resolveOrFail(std::nullopt, std::nullopt, /*HostVectorBits=*/0),
           4u);
  // Clamped to MaxWaveSize even for an implausibly wide host vector.
  EXPECT_EQ(
      resolveOrFail(std::nullopt, std::nullopt, /*HostVectorBits=*/100000),
      128u);
  // Not a power of two: rounds down.
  EXPECT_EQ(resolveOrFail(std::nullopt, std::nullopt, /*HostVectorBits=*/200),
           4u);
}

TEST(WaveSizeTest, ShaderSetUsesItsPreferredSize) {
  // Single required value (SM 6.6 form, widened to "n,0,0").
  EXPECT_EQ(resolveOrFail(std::nullopt, ShaderWaveSizeRequirement{16, 0, 0}),
           16u);
  // Range with an explicit preferred size.
  EXPECT_EQ(
      resolveOrFail(std::nullopt, ShaderWaveSizeRequirement{8, 32, 16}), 16u);
  // Range with no preferred size: the low end of the range.
  EXPECT_EQ(resolveOrFail(std::nullopt, ShaderWaveSizeRequirement{8, 32, 0}),
           8u);
}

TEST(WaveSizeTest, UserSetUsesUserValue) {
  EXPECT_EQ(resolveOrFail(32u, std::nullopt), 32u);
}

TEST(WaveSizeTest, UserAndShaderAgreeingExactValue) {
  EXPECT_EQ(resolveOrFail(16u, ShaderWaveSizeRequirement{16, 0, 0}), 16u);
}

TEST(WaveSizeTest, UserInsideShaderRange) {
  EXPECT_EQ(resolveOrFail(16u, ShaderWaveSizeRequirement{8, 32, 0}), 16u);
}

TEST(WaveSizeTest, UserAndShaderConflictIsAnError) {
  std::string Msg =
      resolveOrFailExpectingError(32u, ShaderWaveSizeRequirement{16, 0, 0});
  EXPECT_NE(Msg.find("conflict"), std::string::npos) << Msg;

  Msg = resolveOrFailExpectingError(64u, ShaderWaveSizeRequirement{8, 32, 0});
  EXPECT_NE(Msg.find("conflict"), std::string::npos) << Msg;
}

TEST(WaveSizeTest, UserValueOutOfRangeIsAnError) {
  EXPECT_NE(resolveOrFailExpectingError(2u, std::nullopt).find("power of two"),
           std::string::npos);
  EXPECT_NE(
      resolveOrFailExpectingError(256u, std::nullopt).find("power of two"),
      std::string::npos);
  EXPECT_NE(resolveOrFailExpectingError(6u, std::nullopt).find("power of two"),
           std::string::npos);
}

TEST(WaveSizeTest, ShaderValueOutOfRangeIsAnError) {
  // A shader declaring [WaveSize(3)] is malformed, not a request FeMe rounds
  // up.
  EXPECT_NE(resolveOrFailExpectingError(std::nullopt,
                                        ShaderWaveSizeRequirement{3, 0, 0})
                .find("power of two"),
           std::string::npos);
}

TEST(WaveSizeTest, ParsesNormalizedAttribute) {
  std::optional<ShaderWaveSizeRequirement> Req =
      parseShaderWaveSizeAttr("16,0,0");
  ASSERT_TRUE(Req.has_value());
  EXPECT_EQ(Req->Min, 16u);
  EXPECT_EQ(Req->Max, 0u);
  EXPECT_EQ(Req->Preferred, 0u);

  Req = parseShaderWaveSizeAttr("8,32,16");
  ASSERT_TRUE(Req.has_value());
  EXPECT_EQ(Req->Min, 8u);
  EXPECT_EQ(Req->Max, 32u);
  EXPECT_EQ(Req->Preferred, 16u);
}

TEST(WaveSizeTest, RejectsMalformedAttribute) {
  EXPECT_FALSE(parseShaderWaveSizeAttr("16").has_value());
  EXPECT_FALSE(parseShaderWaveSizeAttr("16,0").has_value());
  EXPECT_FALSE(parseShaderWaveSizeAttr("a,b,c").has_value());
  EXPECT_FALSE(parseShaderWaveSizeAttr("").has_value());
}

} // namespace
