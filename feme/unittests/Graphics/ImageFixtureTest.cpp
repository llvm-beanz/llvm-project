//===- ImageFixtureTest.cpp - Tests for the textual image fixture format ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Covers feme::graphics::parseImageFixtures/printImageFixture (roadmap R31)
// against the exact example "Textual scene and image fixtures" in
// feme/docs/Design.md gives, plus the round trip through a floating-point
// format and the diagnostics for a malformed fixture.
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/ImageFixture.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

#include <array>
#include <cstring>

using namespace feme;
using namespace feme::graphics;
using namespace llvm;

namespace {

TEST(ImageFixtureTest, ParsesDesignDocExample) {
  StringRef Text = "image color0 4x4 r8g8b8a8-unorm\n"
                   "  y=0: ff0000ff ff0000ff 000000ff 000000ff\n"
                   "  y=1: ff0000ff 000000ff 000000ff 000000ff\n"
                   "  y=2: 000000ff 000000ff 000000ff 000000ff\n"
                   "  y=3: 000000ff 000000ff 000000ff 000000ff\n";
  Expected<std::vector<ImageFixture>> Images = parseImageFixtures(Text);
  ASSERT_THAT_EXPECTED(Images, Succeeded());
  ASSERT_EQ(Images->size(), 1u);
  const ImageFixture &Img = (*Images)[0];
  EXPECT_EQ(Img.Name, "color0");
  EXPECT_EQ(Img.Width, 4u);
  EXPECT_EQ(Img.Height, 4u);
  EXPECT_EQ(Img.Format, cpu::ResourceFormat::R8G8B8A8_UNORM);
  ASSERT_EQ(Img.Data.size(), 4u * 4u * 4u);
  // Texel (0, 0) is opaque red: R=ff, G=00, B=00, A=ff, most significant
  // component (R) first.
  EXPECT_EQ(Img.Data[0], 0xff);
  EXPECT_EQ(Img.Data[1], 0x00);
  EXPECT_EQ(Img.Data[2], 0x00);
  EXPECT_EQ(Img.Data[3], 0xff);
  // Texel (2, 0) is transparent black.
  EXPECT_EQ(Img.Data[2 * 4], 0x00);
  EXPECT_EQ(Img.Data[2 * 4 + 3], 0xff);

  std::string Printed;
  raw_string_ostream OS(Printed);
  ASSERT_THAT_ERROR(printImageFixture(OS, Img), Succeeded());
  EXPECT_EQ(Printed, Text);
}

TEST(ImageFixtureTest, RoundTripsFloatFormat) {
  ImageFixture Img;
  Img.Name = "tex";
  Img.Format = cpu::ResourceFormat::R32G32B32A32_FLOAT;
  Img.Width = 2;
  Img.Height = 1;
  float Storage[2][4] = {{1.0f, 2.0f, 3.0f, 4.0f}, {-1.0f, 0.5f, 0.0f, 1.0f}};
  Img.Data.resize(sizeof(Storage));
  memcpy(Img.Data.data(), Storage, sizeof(Storage));

  std::string Printed;
  raw_string_ostream OS(Printed);
  ASSERT_THAT_ERROR(printImageFixture(OS, Img), Succeeded());

  Expected<std::vector<ImageFixture>> Reparsed = parseImageFixtures(Printed);
  ASSERT_THAT_EXPECTED(Reparsed, Succeeded());
  ASSERT_EQ(Reparsed->size(), 1u);
  EXPECT_EQ((*Reparsed)[0].Data, Img.Data);
}

TEST(ImageFixtureTest, RejectsWrongRowWidth) {
  StringRef Text = "image bad 2x1 r8g8b8a8-unorm\n"
                   "  y=0: ff0000ff\n";
  Expected<std::vector<ImageFixture>> Images = parseImageFixtures(Text);
  ASSERT_THAT_EXPECTED(Images, Failed());
}

TEST(ImageFixtureTest, RejectsUnknownFormat) {
  StringRef Text = "image bad 1x1 not-a-format\n  y=0: 00\n";
  Expected<std::vector<ImageFixture>> Images = parseImageFixtures(Text);
  ASSERT_THAT_EXPECTED(Images, Failed());
}

// Roadmap R33 ("Depth, stencil, blending, and multisampling") adds the
// depth/stencil formats a depth/stencil attachment needs; they round-trip
// through the same fixture format every color attachment uses.
TEST(ImageFixtureTest, RoundTripsDepthFloatFormat) {
  StringRef Text = "image depth0 2x1 d32-float\n"
                   "  y=0: +1.0000e+00 +5.0000e-01\n";
  Expected<std::vector<ImageFixture>> Images = parseImageFixtures(Text);
  ASSERT_THAT_EXPECTED(Images, Succeeded());
  ASSERT_EQ(Images->size(), 1u);
  const ImageFixture &Img = (*Images)[0];
  EXPECT_EQ(Img.Format, cpu::ResourceFormat::D32_FLOAT);
  ASSERT_EQ(Img.Data.size(), 2u * 4u);
  float V0, V1;
  memcpy(&V0, Img.Data.data(), 4);
  memcpy(&V1, Img.Data.data() + 4, 4);
  EXPECT_FLOAT_EQ(V0, 1.0f);
  EXPECT_FLOAT_EQ(V1, 0.5f);

  std::string Printed;
  raw_string_ostream OS(Printed);
  ASSERT_THAT_ERROR(printImageFixture(OS, Img), Succeeded());
  EXPECT_EQ(Printed, Text);
}

TEST(ImageFixtureTest, RoundTripsStencilFormat) {
  StringRef Text = "image stencil0 2x1 s8-uint\n"
                   "  y=0: 00 ff\n";
  Expected<std::vector<ImageFixture>> Images = parseImageFixtures(Text);
  ASSERT_THAT_EXPECTED(Images, Succeeded());
  ASSERT_EQ(Images->size(), 1u);
  const ImageFixture &Img = (*Images)[0];
  EXPECT_EQ(Img.Format, cpu::ResourceFormat::S8_UINT);
  ASSERT_EQ(Img.Data.size(), 2u);
  EXPECT_EQ(Img.Data[0], 0x00);
  EXPECT_EQ(Img.Data[1], 0xff);
}

TEST(ImageFixtureTest, PacksDepthAndStencilClearColors) {
  std::array<uint8_t, 4> DepthTexel{};
  ASSERT_THAT_ERROR(
      packClearColor(cpu::ResourceFormat::D32_FLOAT, {0.75}, DepthTexel),
      Succeeded());
  float Depth;
  memcpy(&Depth, DepthTexel.data(), 4);
  EXPECT_FLOAT_EQ(Depth, 0.75f);

  std::array<uint8_t, 1> StencilTexel{};
  ASSERT_THAT_ERROR(
      packClearColor(cpu::ResourceFormat::S8_UINT, {42.0}, StencilTexel),
      Succeeded());
  EXPECT_EQ(StencilTexel[0], 42);
}

TEST(ImageFixtureTest, UnpackColorIsThePackInverse) {
  std::array<uint8_t, 4> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R8G8B8A8_UNORM,
                                   {1.0, 0.5, 0.0, 0.75}, Texel),
                    Succeeded());
  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::R8G8B8A8_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.01);
  EXPECT_NEAR(Unpacked[1], 0.5, 0.01);
  EXPECT_NEAR(Unpacked[2], 0.0, 0.01);
  EXPECT_NEAR(Unpacked[3], 0.75, 0.01);
}

} // namespace
