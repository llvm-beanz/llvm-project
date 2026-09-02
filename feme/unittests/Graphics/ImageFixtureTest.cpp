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
#include <vector>

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

// (Roadmap H8o) `VK_FORMAT_R16G16B16A16_SFLOAT`'s generic float pack/unpack
// path stores a binary16 ("half float") value per component, not a
// truncated binary32 one -- this round-trips a handful of representative
// values (including a fraction with no exact binary16 representation,
// exercising the round-to-nearest conversion) through `packClearColor`/
// `unpackColor` and checks the result survives within binary16's own
// precision. Before this row, the generic float path's `memcpy` corrupted
// every 2-byte-per-component value it ever touched (no prior test had
// exercised it) -- this is the regression test for that fix.
TEST(ImageFixtureTest, RoundTripsR16G16B16A16FloatThroughPackUnpack) {
  std::array<uint8_t, 8> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R16G16B16A16_FLOAT,
                                   {1.0, -2.5, 0.1, 65504.0}, Texel),
                    Succeeded());

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(unpackColor(cpu::ResourceFormat::R16G16B16A16_FLOAT, Texel,
                                Unpacked),
                    Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.001);
  EXPECT_NEAR(Unpacked[1], -2.5, 0.001);
  // binary16 has ~3 decimal digits of precision; 0.1 is not exactly
  // representable in either binary16 or binary32.
  EXPECT_NEAR(Unpacked[2], 0.1, 0.001);
  // The largest finite binary16 magnitude -- confirms the conversion
  // doesn't clamp/overflow at the format's own limit.
  EXPECT_NEAR(Unpacked[3], 65504.0, 1.0);
}

// (Roadmap H8o) `R8_UNORM`/`R8_SNORM`: `BC4Decode`'s own single-channel
// sampling-bridge target (BCSamplingBridge.h) -- a 1-component format
// still represented as a 4-logical-component clear color, mirroring
// `A8_UNORM`'s own precedent immediately above but with the stored channel
// at logical red (index 0) rather than alpha, and green/blue/alpha reading
// back as their own identity values (`0`/`0`/`1.0`) rather than being
// genuinely stored.
TEST(ImageFixtureTest, PacksAndUnpacksR8Unorm) {
  std::array<uint8_t, 1> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R8_UNORM,
                                   {0.5, 1.0, 1.0, 1.0}, Texel),
                    Succeeded());
  EXPECT_NEAR(Texel[0], 128, 2);

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(unpackColor(cpu::ResourceFormat::R8_UNORM, Texel, Unpacked),
                    Succeeded());
  EXPECT_NEAR(Unpacked[0], 0.5, 0.01);
  EXPECT_EQ(Unpacked[1], 0.0);
  EXPECT_EQ(Unpacked[2], 0.0);
  EXPECT_EQ(Unpacked[3], 1.0);
}

TEST(ImageFixtureTest, PacksAndUnpacksR8SnormNegative) {
  std::array<uint8_t, 1> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R8_SNORM,
                                   {-1.0, 0.0, 0.0, 0.0}, Texel),
                    Succeeded());

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(unpackColor(cpu::ResourceFormat::R8_SNORM, Texel, Unpacked),
                    Succeeded());
  EXPECT_NEAR(Unpacked[0], -1.0, 0.02);
  EXPECT_EQ(Unpacked[3], 1.0);
}

// (Roadmap H8o) `R8G8_UNORM`/`R8G8_SNORM`: `BC5Decode`'s own two-channel
// sampling-bridge target, the same convention as `R8_UNORM` above with a
// second stored channel at logical green.
TEST(ImageFixtureTest, PacksAndUnpacksR8G8Unorm) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R8G8_UNORM,
                                   {0.25, 0.75, 1.0, 1.0}, Texel),
                    Succeeded());

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::R8G8_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 0.25, 0.01);
  EXPECT_NEAR(Unpacked[1], 0.75, 0.01);
  EXPECT_EQ(Unpacked[2], 0.0);
  EXPECT_EQ(Unpacked[3], 1.0);
}

TEST(ImageFixtureTest, PacksAndUnpacksR8G8SnormNegative) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R8G8_SNORM,
                                   {-0.5, 1.0, 0.0, 0.0}, Texel),
                    Succeeded());

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::R8G8_SNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], -0.5, 0.02);
  EXPECT_NEAR(Unpacked[1], 1.0, 0.02);
  EXPECT_EQ(Unpacked[3], 1.0);
}

// (Roadmap H8j) `R16_UNORM`/`R16_SNORM`: `EAC_R11_{UNORM,SNORM}`'s own
// single-channel sampling-bridge target, the 16-bit analogue of
// `R8_UNORM`/`_SNORM` above.
TEST(ImageFixtureTest, PacksAndUnpacksR16Unorm) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R16_UNORM,
                                   {0.5, 1.0, 1.0, 1.0}, Texel),
                    Succeeded());

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::R16_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 0.5, 0.001);
  EXPECT_EQ(Unpacked[1], 0.0);
  EXPECT_EQ(Unpacked[2], 0.0);
  EXPECT_EQ(Unpacked[3], 1.0);
}

TEST(ImageFixtureTest, PacksAndUnpacksR16SnormNegative) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R16_SNORM,
                                   {-1.0, 0.0, 0.0, 0.0}, Texel),
                    Succeeded());

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::R16_SNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], -1.0, 0.001);
  EXPECT_EQ(Unpacked[3], 1.0);
}

// (Roadmap H8j) `R16G16_UNORM`/`R16G16_SNORM`: `EAC_R11G11_
// {UNORM,SNORM}`'s own two-channel sampling-bridge target, the same
// convention as `R16_UNORM` above with a second stored channel at
// logical green.
TEST(ImageFixtureTest, PacksAndUnpacksR16G16Unorm) {
  std::array<uint8_t, 4> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R16G16_UNORM,
                                   {0.25, 0.75, 1.0, 1.0}, Texel),
                    Succeeded());

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::R16G16_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 0.25, 0.001);
  EXPECT_NEAR(Unpacked[1], 0.75, 0.001);
  EXPECT_EQ(Unpacked[2], 0.0);
  EXPECT_EQ(Unpacked[3], 1.0);
}

TEST(ImageFixtureTest, PacksAndUnpacksR16G16SnormNegative) {
  std::array<uint8_t, 4> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R16G16_SNORM,
                                   {-0.5, 1.0, 0.0, 0.0}, Texel),
                    Succeeded());

  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::R16G16_SNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], -0.5, 0.001);
  EXPECT_NEAR(Unpacked[1], 1.0, 0.001);
  EXPECT_EQ(Unpacked[3], 1.0);
}


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

// Roadmap H7r's remaining core-1.0 packed 16-bit formats: each is a
// round-trip test of `packClearColor`/`unpackColor` for one format, using
// a color with 4 distinguishable components (or 3 plus an ignored alpha,
// for the two formats below with no alpha channel) so a component swap or
// bit-width mistake would be caught.
TEST(ImageFixtureTest, PacksAndUnpacksR4G4B4A4Unorm) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R4G4B4A4_UNORM,
                                   {1.0, 0.0, 2.0 / 3.0, 1.0 / 3.0}, Texel),
                    Succeeded());
  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::R4G4B4A4_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.07);
  EXPECT_NEAR(Unpacked[1], 0.0, 0.07);
  EXPECT_NEAR(Unpacked[2], 2.0 / 3.0, 0.07);
  EXPECT_NEAR(Unpacked[3], 1.0 / 3.0, 0.07);
}

TEST(ImageFixtureTest, PacksAndUnpacksB4G4R4A4Unorm) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::B4G4R4A4_UNORM,
                                   {1.0, 0.0, 2.0 / 3.0, 1.0 / 3.0}, Texel),
                    Succeeded());
  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::B4G4R4A4_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.07);
  EXPECT_NEAR(Unpacked[1], 0.0, 0.07);
  EXPECT_NEAR(Unpacked[2], 2.0 / 3.0, 0.07);
  EXPECT_NEAR(Unpacked[3], 1.0 / 3.0, 0.07);

  // B and R occupy swapped bit positions relative to `R4G4B4A4_UNORM`
  // above: confirm the packed word itself actually differs for the same
  // input color (i.e. the swap is real, not a no-op given this symmetric
  // test color).
  std::array<uint8_t, 2> RTexel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R4G4B4A4_UNORM,
                                   {1.0, 0.0, 2.0 / 3.0, 1.0 / 3.0}, RTexel),
                    Succeeded());
  EXPECT_NE(Texel, RTexel);
}

TEST(ImageFixtureTest, PacksAndUnpacksR5G6B5Unorm) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R5G6B5_UNORM,
                                   {1.0, 2.0 / 3.0, 0.0, 0.0}, Texel),
                    Succeeded());
  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::R5G6B5_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.05);
  EXPECT_NEAR(Unpacked[1], 2.0 / 3.0, 0.02);
  EXPECT_NEAR(Unpacked[2], 0.0, 0.05);
  // No alpha channel: reads back as fully opaque regardless of the clear
  // color's own (ignored) alpha component.
  EXPECT_EQ(Unpacked[3], 1.0);
}

TEST(ImageFixtureTest, PacksAndUnpacksB5G6R5Unorm) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::B5G6R5_UNORM,
                                   {1.0, 2.0 / 3.0, 0.0, 0.0}, Texel),
                    Succeeded());
  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::B5G6R5_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.05);
  EXPECT_NEAR(Unpacked[1], 2.0 / 3.0, 0.02);
  EXPECT_NEAR(Unpacked[2], 0.0, 0.05);
  EXPECT_EQ(Unpacked[3], 1.0);
}

TEST(ImageFixtureTest, PacksAndUnpacksR5G5B5A1Unorm) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::R5G5B5A1_UNORM,
                                   {1.0, 0.0, 2.0 / 3.0, 1.0}, Texel),
                    Succeeded());
  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::R5G5B5A1_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.05);
  EXPECT_NEAR(Unpacked[1], 0.0, 0.05);
  EXPECT_NEAR(Unpacked[2], 2.0 / 3.0, 0.05);
  EXPECT_EQ(Unpacked[3], 1.0);
}

TEST(ImageFixtureTest, PacksAndUnpacksB5G5R5A1Unorm) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::B5G5R5A1_UNORM,
                                   {1.0, 0.0, 2.0 / 3.0, 1.0}, Texel),
                    Succeeded());
  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::B5G5R5A1_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.05);
  EXPECT_NEAR(Unpacked[1], 0.0, 0.05);
  EXPECT_NEAR(Unpacked[2], 2.0 / 3.0, 0.05);
  EXPECT_EQ(Unpacked[3], 1.0);
}

TEST(ImageFixtureTest, PacksAndUnpacksA1R5G5B5Unorm) {
  std::array<uint8_t, 2> Texel{};
  ASSERT_THAT_ERROR(packClearColor(cpu::ResourceFormat::A1R5G5B5_UNORM,
                                   {1.0, 2.0 / 3.0, 0.0, 1.0}, Texel),
                    Succeeded());
  uint16_t Word;
  memcpy(&Word, Texel.data(), 2);
  EXPECT_EQ((static_cast<uint32_t>(Word) >> 15) & 0x1u, 1u); // A = 1.0
  std::array<double, 4> Unpacked{};
  ASSERT_THAT_ERROR(
      unpackColor(cpu::ResourceFormat::A1R5G5B5_UNORM, Texel, Unpacked),
      Succeeded());
  EXPECT_NEAR(Unpacked[0], 1.0, 0.05);
  EXPECT_NEAR(Unpacked[1], 2.0 / 3.0, 0.05);
  EXPECT_NEAR(Unpacked[2], 0.0, 0.05);
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

// Roadmap F11a: `D32_FLOAT_S8X24_UINT` never had its own case in
// `packDepthClear`/`unpackDepth`/`packStencilClear`/`unpackStencil` -- only
// `D24_UNORM_S8_UINT` did -- even for the existing
// `vkCmdClearDepthStencilImage`. Unlike `D24_UNORM_S8_UINT`'s single
// shared 32-bit word, this format's depth and stencil are two entirely
// separate 4-byte words (`getFormatInfo`'s own comment), so a depth pack
// is a plain write of the first word, not a read-modify-write.
TEST(ImageFixtureTest, PacksDepthAndStencilForD32FloatS8X24Uint) {
  std::array<uint8_t, 8> Texel{0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
  ASSERT_THAT_ERROR(
      packDepthClear(cpu::ResourceFormat::D32_FLOAT_S8X24_UINT, 0.5, Texel),
      Succeeded());
  float DepthWord;
  memcpy(&DepthWord, Texel.data(), sizeof(DepthWord));
  EXPECT_FLOAT_EQ(DepthWord, 0.5f);
  // The second word (stencil) must be unaffected by a depth-only pack.
  EXPECT_EQ(Texel[4], 0xAA);
  EXPECT_EQ(Texel[7], 0xAA);

  ASSERT_THAT_ERROR(
      packStencilClear(cpu::ResourceFormat::D32_FLOAT_S8X24_UINT, 0x5C, Texel),
      Succeeded());
  EXPECT_EQ(Texel[4], 0x5C);
  // The upper 3 bytes of the second word are untouched by the stencil pack.
  EXPECT_EQ(Texel[5], 0xAA);
  EXPECT_EQ(Texel[6], 0xAA);
  EXPECT_EQ(Texel[7], 0xAA);

  double Depth = -1.0;
  ASSERT_THAT_ERROR(
      unpackDepth(cpu::ResourceFormat::D32_FLOAT_S8X24_UINT, Texel, Depth),
      Succeeded());
  EXPECT_NEAR(Depth, 0.5, 1e-6);

  uint32_t Stencil = 0;
  ASSERT_THAT_ERROR(
      unpackStencil(cpu::ResourceFormat::D32_FLOAT_S8X24_UINT, Texel, Stencil),
      Succeeded());
  EXPECT_EQ(Stencil, 0x5Cu);
}

// Roadmap F11a: `copyDepthAspectRegion`/`copyStencilAspectRegion` generalize
// `packDepthClear`/`packStencilClear`'s single repeated clear value to an
// arbitrary per-texel buffer region, as `copyBufferImageRegion`
// (ImageOps.cpp) needs for a single-aspect `VkBufferImageCopy` of a
// combined depth/stencil image.
TEST(ImageFixtureTest, CopiesDepthAspectRegionPreservingStencilForD24) {
  // Two texels, each the combined format's own 4-byte word; stencil bits
  // (high byte) pre-seeded to a recognizable, distinct value per texel.
  std::array<uint8_t, 8> Image{0, 0, 0, 0x11, 0, 0, 0, 0x22};
  std::array<uint8_t, 8> BufferToImage{0x01, 0x02, 0x03, 0xFF,
                                       0x04, 0x05, 0x06, 0xFF};
  ASSERT_THAT_ERROR(
      copyDepthAspectRegion(cpu::ResourceFormat::D24_UNORM_S8_UINT,
                            /*ToImage=*/true, BufferToImage, Image,
                            /*TexelCount=*/2),
      Succeeded());
  EXPECT_EQ(Image[0], 0x01);
  EXPECT_EQ(Image[1], 0x02);
  EXPECT_EQ(Image[2], 0x03);
  EXPECT_EQ(Image[3], 0x11); // Stencil untouched.
  EXPECT_EQ(Image[4], 0x04);
  EXPECT_EQ(Image[5], 0x05);
  EXPECT_EQ(Image[6], 0x06);
  EXPECT_EQ(Image[7], 0x22); // Stencil untouched.

  std::array<uint8_t, 8> ImageToBuffer{};
  ASSERT_THAT_ERROR(
      copyDepthAspectRegion(cpu::ResourceFormat::D24_UNORM_S8_UINT,
                            /*ToImage=*/false, ImageToBuffer, Image,
                            /*TexelCount=*/2),
      Succeeded());
  EXPECT_EQ(ImageToBuffer[0], 0x01);
  EXPECT_EQ(ImageToBuffer[1], 0x02);
  EXPECT_EQ(ImageToBuffer[2], 0x03);
  EXPECT_EQ(ImageToBuffer[4], 0x04);
  EXPECT_EQ(ImageToBuffer[5], 0x05);
  EXPECT_EQ(ImageToBuffer[6], 0x06);
}

TEST(ImageFixtureTest, CopiesStencilAspectRegionPreservingDepthForD24) {
  std::array<uint8_t, 8> Image{0x01, 0x02, 0x03, 0, 0x04, 0x05, 0x06, 0};
  std::array<uint8_t, 2> BufferToImage{0x7B, 0x2A};
  ASSERT_THAT_ERROR(
      copyStencilAspectRegion(cpu::ResourceFormat::D24_UNORM_S8_UINT,
                              /*ToImage=*/true, BufferToImage, Image,
                              /*TexelCount=*/2),
      Succeeded());
  EXPECT_EQ(Image[0], 0x01); // Depth untouched.
  EXPECT_EQ(Image[1], 0x02);
  EXPECT_EQ(Image[2], 0x03);
  EXPECT_EQ(Image[3], 0x7B);
  EXPECT_EQ(Image[4], 0x04);
  EXPECT_EQ(Image[5], 0x05);
  EXPECT_EQ(Image[6], 0x06);
  EXPECT_EQ(Image[7], 0x2A);

  std::array<uint8_t, 2> ImageToBuffer{};
  ASSERT_THAT_ERROR(
      copyStencilAspectRegion(cpu::ResourceFormat::D24_UNORM_S8_UINT,
                              /*ToImage=*/false, ImageToBuffer, Image,
                              /*TexelCount=*/2),
      Succeeded());
  EXPECT_EQ(ImageToBuffer[0], 0x7B);
  EXPECT_EQ(ImageToBuffer[1], 0x2A);
}

TEST(ImageFixtureTest, CopiesDepthAspectRegionForD32FloatS8X24Uint) {
  // Two texels, each the combined format's own 8-byte pair of words;
  // stencil's low byte (second word) pre-seeded distinctly per texel.
  std::array<uint8_t, 16> Image{};
  Image[4] = 0x11;
  Image[12] = 0x22;
  std::vector<float> DepthValues = {1.5f, -2.25f};
  std::array<uint8_t, 8> Buffer{};
  memcpy(Buffer.data(), DepthValues.data(), Buffer.size());

  ASSERT_THAT_ERROR(
      copyDepthAspectRegion(cpu::ResourceFormat::D32_FLOAT_S8X24_UINT,
                            /*ToImage=*/true, Buffer, Image,
                            /*TexelCount=*/2),
      Succeeded());
  float Depth0, Depth1;
  memcpy(&Depth0, Image.data(), sizeof(Depth0));
  memcpy(&Depth1, Image.data() + 8, sizeof(Depth1));
  EXPECT_FLOAT_EQ(Depth0, 1.5f);
  EXPECT_FLOAT_EQ(Depth1, -2.25f);
  EXPECT_EQ(Image[4], 0x11); // Stencil untouched.
  EXPECT_EQ(Image[12], 0x22);
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

