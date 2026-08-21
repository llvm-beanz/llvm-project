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

// Roadmap C1 ("Mandatory formats"): `B8G8R8A8_UNORM` is one of the Vulkan
// 1.2 mandatory color-attachment/blend formats. It shares `R8G8B8A8_UNORM`'s
// per-byte encoding but swaps red and blue in storage; `Clear`/`Out` stay
// in logical [R, G, B, A] order (matching `VkClearColorValue::float32`).
TEST(ImageFixtureTest, PacksAndUnpacksB8G8R8A8Unorm) {
  std::array<uint8_t, 4> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::B8G8R8A8_UNORM,
                                   {1.0, 0.5, 0.0, 0.25}, Texel),
                    Succeeded());
  // Memory order is B, G, R, A: B=0x00, G=~0x80, R=0xff, A=~0x40.
  EXPECT_EQ(Texel[0], 0);
  EXPECT_NEAR(Texel[1], 128, 2);
  EXPECT_EQ(Texel[2], 255);
  EXPECT_NEAR(Texel[3], 64, 2);

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::B8G8R8A8_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.01);
  EXPECT_NEAR(Unpacked[1], 0.5, 0.01);
  EXPECT_NEAR(Unpacked[2], 0.0, 0.01);
  EXPECT_NEAR(Unpacked[3], 0.25, 0.01);
}

// `R10G10B10A2_UNORM` (`VK_FORMAT_A2B10G10R10_UNORM_PACK32`), the other
// mandatory format roadmap C1 adds: all four components packed into one
// 32-bit word, 2 bits of alpha at the top down to 10 bits of red at the
// bottom.
TEST(ImageFixtureTest, PacksAndUnpacksR10G10B10A2Unorm) {
  std::array<uint8_t, 4> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R10G10B10A2_UNORM,
                                   {1.0, 0.0, 0.5, 1.0}, Texel),
                    Succeeded());
  uint32_t Word;
  memcpy(&Word, Texel.data(), 4);
  EXPECT_EQ(Word & 0x3FF, 1023u);          // R = 1.0
  EXPECT_EQ((Word >> 10) & 0x3FF, 0u);     // G = 0.0
  EXPECT_NEAR((Word >> 20) & 0x3FF, 512, 2); // B = 0.5
  EXPECT_EQ((Word >> 30) & 0x3, 3u);       // A = 1.0

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(unpackColor(cpu::ResourceFormat::R10G10B10A2_UNORM, Texel,
                               Unpacked),
                    Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.01);
  EXPECT_NEAR(Unpacked[1], 0.0, 0.01);
  EXPECT_NEAR(Unpacked[2], 0.5, 0.01);
  EXPECT_NEAR(Unpacked[3], 1.0, 0.01);
}

// Roadmap E5's `VK_FORMAT_A8_UNORM`: a single alpha byte -- the clear
// color's R/G/B components are ignored on pack and read back as `0` on
// unpack.
TEST(ImageFixtureTest, PacksAndUnpacksA8Unorm) {
  std::array<uint8_t, 1> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::A8_UNORM,
                                   {1.0, 1.0, 1.0, 0.5}, Texel),
                    Succeeded());
  EXPECT_NEAR(Texel[0], 128, 2);

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(unpackColor(cpu::ResourceFormat::A8_UNORM, Texel, Unpacked),
                    Succeeded());
  EXPECT_EQ(Unpacked[0], 0.0);
  EXPECT_EQ(Unpacked[1], 0.0);
  EXPECT_EQ(Unpacked[2], 0.0);
  EXPECT_NEAR(Unpacked[3], 0.5, 0.01);
}

// Roadmap E5's `VK_FORMAT_A1B5G5R5_UNORM_PACK16`: all four components
// packed into one 16-bit word, 1 bit of alpha at the top down to 5 bits of
// red at the bottom -- the same packing `R10G10B10A2_UNORM` above uses,
// just half the width and with alpha moved to the MSB.
TEST(ImageFixtureTest, PacksAndUnpacksA1B5G5R5Unorm) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::A1B5G5R5_UNORM,
                                   {1.0, 0.0, 0.5, 1.0}, Texel),
                    Succeeded());
  uint16_t Word;
  memcpy(&Word, Texel.data(), 2);
  EXPECT_EQ(static_cast<uint32_t>(Word) & 0x1Fu, 31u);       // R = 1.0
  EXPECT_EQ((static_cast<uint32_t>(Word) >> 5) & 0x1Fu, 0u); // G = 0.0
  EXPECT_NEAR((static_cast<uint32_t>(Word) >> 10) & 0x1Fu, 16,
              1);                                            // B = 0.5
  EXPECT_EQ((static_cast<uint32_t>(Word) >> 15) & 0x1u, 1u); // A = 1.0

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::A1B5G5R5_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.05);
  EXPECT_NEAR(Unpacked[1], 0.0, 0.05);
  EXPECT_NEAR(Unpacked[2], 0.5, 0.05);
  EXPECT_NEAR(Unpacked[3], 1.0, 0.01);
}

// Roadmap C1's combined depth+stencil format: `packDepthClear`/
// `packStencilClear` must be independent read-modify-writes of the same
// 4-byte word, and `unpackDepth`/`unpackStencil` their inverse.
TEST(ImageFixtureTest, PacksDepthAndStencilIndependentlyForCombinedFormat) {
  std::array<uint8_t, 4> Texel{0xAA, 0xAA, 0xAA, 0xAA};
  ASSERT_THAT_ERROR(
      packDepthClear(cpu::ResourceFormat::D24_UNORM_S8_UINT, 1.0, Texel),
      Succeeded());
  // The high byte (stencil) must be unaffected by a depth-only pack.
  EXPECT_EQ(Texel[3], 0xAA);

  ASSERT_THAT_ERROR(
      packStencilClear(cpu::ResourceFormat::D24_UNORM_S8_UINT, 0x7B, Texel),
      Succeeded());
  EXPECT_EQ(Texel[3], 0x7B);

  double Depth = -1.0;
  ASSERT_THAT_ERROR(
      unpackDepth(cpu::ResourceFormat::D24_UNORM_S8_UINT, Texel, Depth),
      Succeeded());
  EXPECT_NEAR(Depth, 1.0, 1e-6);

  uint32_t Stencil = 0;
  ASSERT_THAT_ERROR(
      unpackStencil(cpu::ResourceFormat::D24_UNORM_S8_UINT, Texel, Stencil),
      Succeeded());
  EXPECT_EQ(Stencil, 0x7Bu);

  // The depth pack from earlier must still hold: writing stencil did not
  // clobber the low 24 bits.
  ASSERT_THAT_ERROR(
      unpackDepth(cpu::ResourceFormat::D24_UNORM_S8_UINT, Texel, Depth),
      Succeeded());
  EXPECT_NEAR(Depth, 1.0, 1e-6);
}

TEST(ImageFixtureTest, PackDepthClearStillSupportsPureDepthFormats) {
  std::array<uint8_t, 4> Texel{};
  ASSERT_THAT_ERROR(
      packDepthClear(cpu::ResourceFormat::D32_FLOAT, 0.25, Texel),
      Succeeded());
  float V;
  memcpy(&V, Texel.data(), 4);
  EXPECT_FLOAT_EQ(V, 0.25f);

  double Depth = -1.0;
  ASSERT_THAT_ERROR(
      unpackDepth(cpu::ResourceFormat::D32_FLOAT, Texel, Depth),
      Succeeded());
  EXPECT_NEAR(Depth, 0.25, 1e-6);
}

TEST(ImageFixtureTest, PackStencilClearStillSupportsPureStencilFormat) {
  std::array<uint8_t, 1> Texel{};
  ASSERT_THAT_ERROR(
      packStencilClear(cpu::ResourceFormat::S8_UINT, 200, Texel),
      Succeeded());
  EXPECT_EQ(Texel[0], 200);

  uint32_t Stencil = 0;
  ASSERT_THAT_ERROR(
      unpackStencil(cpu::ResourceFormat::S8_UINT, Texel, Stencil),
      Succeeded());
  EXPECT_EQ(Stencil, 200u);
}

// The fixture text format also round-trips the two new formats, using the
// same opaque hex-word encoding `R11G11B10_FLOAT` already established for
// a packed format.
TEST(ImageFixtureTest, RoundTripsCombinedDepthStencilFormat) {
  StringRef Text = "image ds0 1x1 d24-unorm-s8-uint\n"
                   "  y=0: 00ffffff\n";
  Expected<std::vector<ImageFixture>> Images = parseImageFixtures(Text);
  ASSERT_THAT_EXPECTED(Images, Succeeded());
  ASSERT_EQ(Images->size(), 1u);
  const ImageFixture &Img = (*Images)[0];
  EXPECT_EQ(Img.Format, cpu::ResourceFormat::D24_UNORM_S8_UINT);
  ASSERT_EQ(Img.Data.size(), 4u);

  std::string Printed;
  raw_string_ostream OS(Printed);
  ASSERT_THAT_ERROR(printImageFixture(OS, Img), Succeeded());
  EXPECT_EQ(Printed, Text);
}

} // namespace

