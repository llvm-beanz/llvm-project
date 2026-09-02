//===- ImageOpsTest.cpp - Clear/blit/resolve tests ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (V6) Covers the image operations that land alongside draws: whole-image
// and attachment clears, blits, and multisample resolves (see "Draw
// commands and vertex data" in feme/docs/FeMeVulkanDesign.md).
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "ImageOps.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Image.h"
#include "Objects.h"

#include "gtest/gtest.h"

#include <cstring>

using namespace feme::vulkan;

namespace {

class ImageOpsTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);
  }
  void TearDown() override {
    for (VkDeviceMemory Memory : Allocations)
      vkFreeMemory(Device, Memory, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  VkImage createImage(uint32_t Width, uint32_t Height, VkFormat Format,
                      VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT,
                      uint32_t ArrayLayers = 1) {
    VkImageCreateInfo Info{};
    Info.imageType = VK_IMAGE_TYPE_2D;
    Info.format = Format;
    Info.extent = {Width, Height, 1};
    Info.mipLevels = 1;
    Info.arrayLayers = ArrayLayers;
    Info.samples = Samples;
    Info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImage Img = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateImage(Device, &Info, nullptr, &Img), VK_SUCCESS);
    VkMemoryRequirements Reqs{};
    vkGetImageMemoryRequirements(Device, Img, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Reqs.size;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    EXPECT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory),
              VK_SUCCESS);
    EXPECT_EQ(vkBindImageMemory(Device, Img, Memory, 0), VK_SUCCESS);
    Allocations.push_back(Memory);
    return Img;
  }

  static std::array<uint8_t, 4> texel(VkImage Img, uint32_t X, uint32_t Y,
                                      uint32_t Sample = 0, uint32_t Layer = 0) {
    const void *Ptr =
        fromHandle<Image>(Img)->texelPointer(0, Layer, X, Y, 0, Sample);
    std::array<uint8_t, 4> Result{};
    std::memcpy(Result.data(), Ptr, 4);
    return Result;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  std::vector<VkDeviceMemory> Allocations;
};

TEST_F(ImageOpsTest, ClearsWholeColorImage) {
  VkImage Img = createImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM);
  VkClearColorValue Color{};
  Color.float32[0] = 1.0f;
  Color.float32[3] = 1.0f;
  VkImageSubresourceRange Range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  ASSERT_FALSE(runClearColorImage(fromHandle<Image>(Img), Color, Range));
  for (uint32_t Y = 0; Y != 4; ++Y)
    for (uint32_t X = 0; X != 4; ++X) {
      EXPECT_EQ(texel(Img, X, Y)[0], 0xFF);
      EXPECT_EQ(texel(Img, X, Y)[1], 0x00);
      EXPECT_EQ(texel(Img, X, Y)[3], 0xFF);
    }

  vkDestroyImage(Device, Img, nullptr);
}

TEST_F(ImageOpsTest, RejectsOutOfRangeClearSubresource) {
  VkImage Img = createImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM);
  VkClearColorValue Color{};
  VkImageSubresourceRange Range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 3, 0, 1};
  llvm::Error E = runClearColorImage(fromHandle<Image>(Img), Color, Range);
  EXPECT_TRUE(static_cast<bool>(E));
  llvm::consumeError(std::move(E));
  vkDestroyImage(Device, Img, nullptr);
}

/// A nearest-filter blit doubling a 2x2 image into a 4x4 one replicates
/// each source texel into a 2x2 destination block.
TEST_F(ImageOpsTest, BlitsNearest) {
  VkImage Src = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM);
  VkImage Dst = createImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM);
  for (uint32_t Y = 0; Y != 2; ++Y)
    for (uint32_t X = 0; X != 2; ++X) {
      uint8_t Value = uint8_t(0x40 * (Y * 2 + X));
      std::array<uint8_t, 4> Texel{Value, Value, Value, 0xFF};
      std::memcpy(fromHandle<Image>(Src)->texelPointer(0, 0, X, Y, 0),
                  Texel.data(), 4);
    }

  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.srcOffsets[1] = {2, 2, 1};
  Region.dstOffsets[1] = {4, 4, 1};

  ASSERT_FALSE(runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                            Region, VK_FILTER_NEAREST));
  EXPECT_EQ(texel(Dst, 0, 0)[0], 0x00);
  EXPECT_EQ(texel(Dst, 3, 0)[0], 0x40);
  EXPECT_EQ(texel(Dst, 0, 3)[0], 0x80);
  EXPECT_EQ(texel(Dst, 3, 3)[0], 0xC0);

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// A blit between differing formats converts through the same unpack/pack
/// path the bilinear filter always used, rather than reinterpreting bytes.
TEST_F(ImageOpsTest, ConvertsFormatDuringBlit) {
  VkImage Src = createImage(1, 1, VK_FORMAT_R8G8B8A8_UNORM);
  VkImage Dst = createImage(1, 1, VK_FORMAT_R32G32B32A32_SFLOAT);
  std::array<uint8_t, 4> SrcTexel{0x80, 0x40, 0x00, 0xFF};
  std::memcpy(fromHandle<Image>(Src)->texelPointer(0, 0, 0, 0, 0),
              SrcTexel.data(), 4);

  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.srcOffsets[1] = {1, 1, 1};
  Region.dstOffsets[1] = {1, 1, 1};
  ASSERT_FALSE(runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                            Region, VK_FILTER_NEAREST));

  std::array<float, 4> DstTexel{};
  std::memcpy(DstTexel.data(),
              fromHandle<Image>(Dst)->texelPointer(0, 0, 0, 0, 0),
              sizeof(DstTexel));
  EXPECT_NEAR(DstTexel[0], 0x80 / 255.0f, 1e-6f);
  EXPECT_NEAR(DstTexel[1], 0x40 / 255.0f, 1e-6f);
  EXPECT_NEAR(DstTexel[2], 0.0f, 1e-6f);
  EXPECT_NEAR(DstTexel[3], 1.0f, 1e-6f);

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// Opposite-corner offsets on an axis mirror a blit along that axis.
TEST_F(ImageOpsTest, MirrorsBlitRegion) {
  VkImage Src = createImage(2, 1, VK_FORMAT_R8G8B8A8_UNORM);
  VkImage Dst = createImage(2, 1, VK_FORMAT_R8G8B8A8_UNORM);
  std::array<uint8_t, 4> Left{0x00, 0x00, 0x00, 0xFF};
  std::array<uint8_t, 4> Right{0xFF, 0xFF, 0xFF, 0xFF};
  std::memcpy(fromHandle<Image>(Src)->texelPointer(0, 0, 0, 0, 0), Left.data(),
              4);
  std::memcpy(fromHandle<Image>(Src)->texelPointer(0, 0, 1, 0, 0), Right.data(),
              4);

  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  // A mirrored source: srcOffsets run from (2, 1) down to (0, 0).
  Region.srcOffsets[0] = {2, 1, 0};
  Region.srcOffsets[1] = {0, 0, 1};
  Region.dstOffsets[1] = {2, 1, 1};
  ASSERT_FALSE(runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                            Region, VK_FILTER_NEAREST));

  // The mirrored blit flips the row: the source's white right texel now
  // lands on the destination's left, and vice versa.
  EXPECT_EQ(texel(Dst, 0, 0)[0], 0xFF);
  EXPECT_EQ(texel(Dst, 1, 0)[0], 0x00);

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// `runBlitImage`'s own `LayerCount` (`min` of both subresources'
/// `layerCount`) had the same unresolved-`VK_REMAINING_ARRAY_LAYERS` bug
/// `ImageTest.cpp`'s `CopyBufferToImageWithRemainingArrayLayers` documents
/// for `runCopyBufferToImage` -- confirmed hanging `deqp-vk`'s own
/// `blit_image.simple_tests.array.*_remaining_layers` cases. This blits the
/// last two of a three-layer source into a two-layer destination.
TEST_F(ImageOpsTest, BlitsWithRemainingArrayLayers) {
  VkImage Src = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_SAMPLE_COUNT_1_BIT, /*ArrayLayers=*/3);
  VkImage Dst = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_SAMPLE_COUNT_1_BIT, /*ArrayLayers=*/2);
  for (uint32_t Layer = 1; Layer != 3; ++Layer)
    for (uint32_t Y = 0; Y != 2; ++Y)
      for (uint32_t X = 0; X != 2; ++X) {
        uint8_t Value = uint8_t(0x40 * (Layer * 4 + Y * 2 + X));
        std::array<uint8_t, 4> Texel{Value, Value, Value, 0xFF};
        std::memcpy(fromHandle<Image>(Src)->texelPointer(0, Layer, X, Y, 0),
                    Texel.data(), 4);
      }

  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, /*baseArrayLayer=*/1,
                           VK_REMAINING_ARRAY_LAYERS};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
                           VK_REMAINING_ARRAY_LAYERS};
  Region.srcOffsets[1] = {2, 2, 1};
  Region.dstOffsets[1] = {2, 2, 1};
  ASSERT_FALSE(runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                            Region, VK_FILTER_NEAREST));

  for (uint32_t Layer = 0; Layer != 2; ++Layer)
    for (uint32_t Y = 0; Y != 2; ++Y)
      for (uint32_t X = 0; X != 2; ++X)
        EXPECT_EQ(texel(Dst, X, Y, /*Sample=*/0, Layer)[0],
                  texel(Src, X, Y, /*Sample=*/0, Layer + 1)[0]);

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// Resolving a 4-sample image averages its samples, the same box filter the
/// executor's own resolve attachments use.
TEST_F(ImageOpsTest, ResolvesMultisampleImage) {
  VkImage Src =
      createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_4_BIT);
  VkImage Dst = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM);
  for (uint32_t Y = 0; Y != 2; ++Y)
    for (uint32_t X = 0; X != 2; ++X)
      for (uint32_t S = 0; S != 4; ++S) {
        // Two samples black, two white: the resolve must land halfway.
        uint8_t Value = S < 2 ? 0x00 : 0xFF;
        std::array<uint8_t, 4> Texel{Value, Value, Value, 0xFF};
        std::memcpy(fromHandle<Image>(Src)->texelPointer(0, 0, X, Y, 0, S),
                    Texel.data(), 4);
      }

  VkImageResolve Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.extent = {2, 2, 1};

  ASSERT_FALSE(
      runResolveImage(fromHandle<Image>(Src), fromHandle<Image>(Dst), Region));
  for (uint32_t Y = 0; Y != 2; ++Y)
    for (uint32_t X = 0; X != 2; ++X) {
      EXPECT_NEAR(texel(Dst, X, Y)[0], 0x80, 1);
      EXPECT_EQ(texel(Dst, X, Y)[3], 0xFF);
    }

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// `runResolveImage`'s own `LayerCount` had the same bug the two
/// regression tests above document for `runCopyBufferToImage`/
/// `runBlitImage`; this is its peer, resolving the last two of a
/// three-layer multisample source into a two-layer destination.
TEST_F(ImageOpsTest, ResolvesWithRemainingArrayLayers) {
  VkImage Src = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_SAMPLE_COUNT_4_BIT, /*ArrayLayers=*/3);
  VkImage Dst = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_SAMPLE_COUNT_1_BIT, /*ArrayLayers=*/2);
  for (uint32_t Layer = 1; Layer != 3; ++Layer)
    for (uint32_t Y = 0; Y != 2; ++Y)
      for (uint32_t X = 0; X != 2; ++X)
        for (uint32_t S = 0; S != 4; ++S) {
          uint8_t Value = S < 2 ? 0x00 : 0xFF;
          std::array<uint8_t, 4> Texel{Value, Value, Value, 0xFF};
          std::memcpy(
              fromHandle<Image>(Src)->texelPointer(0, Layer, X, Y, 0, S),
              Texel.data(), 4);
        }

  VkImageResolve Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, /*baseArrayLayer=*/1,
                           VK_REMAINING_ARRAY_LAYERS};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
                           VK_REMAINING_ARRAY_LAYERS};
  Region.extent = {2, 2, 1};

  ASSERT_FALSE(
      runResolveImage(fromHandle<Image>(Src), fromHandle<Image>(Dst), Region));
  for (uint32_t Layer = 0; Layer != 2; ++Layer)
    for (uint32_t Y = 0; Y != 2; ++Y)
      for (uint32_t X = 0; X != 2; ++X)
        EXPECT_NEAR(texel(Dst, X, Y, /*Sample=*/0, Layer)[0], 0x80, 1);

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// Roadmap E16 (`VK_EXT_image_robustness`/`robustImageAccess`): a blit
/// region naming a source rectangle far larger than the source image's own
/// declared extent must clamp every fetch to the image's edge rather than
/// reading past it.
TEST_F(ImageOpsTest, BlitClampsOutOfBoundsSourceRegion) {
  VkImage Src = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM);
  VkImage Dst = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM);
  for (uint32_t Y = 0; Y != 2; ++Y)
    for (uint32_t X = 0; X != 2; ++X) {
      uint8_t Value = uint8_t(0x40 * (Y * 2 + X));
      std::array<uint8_t, 4> Texel{Value, Value, Value, 0xFF};
      std::memcpy(fromHandle<Image>(Src)->texelPointer(0, 0, X, Y, 0),
                  Texel.data(), 4);
    }

  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  // A source rectangle 50x larger than Src's real 2x2 extent: every
  // destination texel's fractional position lands well past (1, 1), so
  // without the E16 clamp this would read out of bounds.
  Region.srcOffsets[1] = {100, 100, 1};
  Region.dstOffsets[1] = {2, 2, 1};

  ASSERT_FALSE(runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                            Region, VK_FILTER_NEAREST));
  // Every destination texel's source position clamps to the image's last
  // valid texel, (1, 1).
  for (uint32_t Y = 0; Y != 2; ++Y)
    for (uint32_t X = 0; X != 2; ++X)
      EXPECT_EQ(texel(Dst, X, Y)[0], 0xC0);

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// The write-side peer of `BlitClampsOutOfBoundsSourceRegion`: a blit
/// region naming a destination rectangle larger than the destination
/// image's own declared extent must discard the out-of-bounds texels
/// rather than fault, while still writing every in-bounds one correctly
/// (roadmap E16).
TEST_F(ImageOpsTest, BlitDiscardsOutOfBoundsDestinationTexels) {
  VkImage Src = createImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM);
  VkImage Dst = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM);
  for (uint32_t Y = 0; Y != 4; ++Y)
    for (uint32_t X = 0; X != 4; ++X) {
      uint8_t Value = uint8_t(0x10 * (Y * 4 + X));
      std::array<uint8_t, 4> Texel{Value, Value, Value, 0xFF};
      std::memcpy(fromHandle<Image>(Src)->texelPointer(0, 0, X, Y, 0),
                  Texel.data(), 4);
    }

  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.srcOffsets[1] = {4, 4, 1};
  // A 4x4 destination rectangle against a real 2x2 destination image:
  // without the E16 discard, the (2, *)/(*, 2)/(3, *)/(*, 3) destination
  // texels below would write out of bounds.
  Region.dstOffsets[1] = {4, 4, 1};

  ASSERT_FALSE(runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                            Region, VK_FILTER_NEAREST));
  // The in-bounds corner still copies the correct (same-format, nearest)
  // source texel.
  EXPECT_EQ(texel(Dst, 0, 0)[0], texel(Src, 0, 0)[0]);

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// Roadmap E16: `vkCmdResolveImage`'s peer of the two blit tests above -- a
/// region whose extent runs past either image's real extent discards the
/// out-of-bounds texels (both the multisample read and the resolved
/// write) rather than faulting, while still resolving every in-bounds
/// texel correctly.
TEST_F(ImageOpsTest, ResolveDiscardsOutOfBoundsRegion) {
  VkImage Src =
      createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_4_BIT);
  VkImage Dst = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM);
  for (uint32_t Y = 0; Y != 2; ++Y)
    for (uint32_t X = 0; X != 2; ++X)
      for (uint32_t S = 0; S != 4; ++S) {
        uint8_t Value = S < 2 ? 0x00 : 0xFF;
        std::array<uint8_t, 4> Texel{Value, Value, Value, 0xFF};
        std::memcpy(fromHandle<Image>(Src)->texelPointer(0, 0, X, Y, 0, S),
                    Texel.data(), 4);
      }

  VkImageResolve Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  // Requests resolving a 4x4 region though both images are only 2x2.
  Region.extent = {4, 4, 1};

  ASSERT_FALSE(
      runResolveImage(fromHandle<Image>(Src), fromHandle<Image>(Dst), Region));
  for (uint32_t Y = 0; Y != 2; ++Y)
    for (uint32_t X = 0; X != 2; ++X) {
      EXPECT_NEAR(texel(Dst, X, Y)[0], 0x80, 1);
      EXPECT_EQ(texel(Dst, X, Y)[3], 0xFF);
    }

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// Roadmap E22: a block-compressed format is never multisampled in real
/// Vulkan, so resolving one is rejected outright rather than falling
/// through to `texelPointer`, which asserts against a block-compressed
/// `Format` (see Image.h's file comment).
TEST_F(ImageOpsTest, RejectsResolveOfBlockCompressedImage) {
  VkImage Src = createImage(4, 4, VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
  VkImage Dst = createImage(4, 4, VK_FORMAT_ASTC_4x4_UNORM_BLOCK);

  VkImageResolve Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.extent = {4, 4, 1};

  llvm::Error E =
      runResolveImage(fromHandle<Image>(Src), fromHandle<Image>(Dst), Region);
  EXPECT_TRUE(static_cast<bool>(E));
  llvm::consumeError(std::move(E));

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// Roadmap E22: no ASTC encoder exists (`ASTCDecode.h` is decode-only), so
/// blitting *to* a block-compressed destination is rejected outright,
/// regardless of the source's own format.
TEST_F(ImageOpsTest, RejectsBlitToBlockCompressedDestination) {
  VkImage Src = createImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM);
  VkImage Dst = createImage(4, 4, VK_FORMAT_ASTC_4x4_UNORM_BLOCK);

  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.srcOffsets[1] = {4, 4, 1};
  Region.dstOffsets[1] = {4, 4, 1};

  llvm::Error E = runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                               Region, VK_FILTER_NEAREST);
  EXPECT_TRUE(static_cast<bool>(E));
  llvm::consumeError(std::move(E));

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// Sets bits `[Start, Start + Len)` of \p Block (bit 0 the LSB of byte 0)
/// to \p Value's low \p Len bits -- the same hand-construction convention
/// `ASTCDecodeTest.cpp`'s own `setBits` uses, duplicated here rather than
/// shared since it is test-only code with no production caller.
void setAstcBits(uint8_t Block[16], unsigned Start, unsigned Len,
                 uint32_t Value) {
  for (unsigned I = 0; I != Len; ++I) {
    unsigned BitIndex = Start + I;
    unsigned Byte = BitIndex / 8, Bit = BitIndex % 8;
    if ((Value >> I) & 1)
      Block[Byte] |= uint8_t(1u << Bit);
    else
      Block[Byte] &= uint8_t(~(1u << Bit));
  }
}

/// A void-extent (solid-fill) ASTC block encoding opaque red -- the same
/// bit layout `ASTCDecodeTest.cpp`'s `VoidExtentDecodesToSolidColor` test
/// constructs, reused here to exercise `runBlitImage`'s own ASTC-decoding
/// path end to end rather than `decodeASTCBlock` in isolation.
std::array<uint8_t, 16> makeOpaqueRedBlock() {
  std::array<uint8_t, 16> Block{};
  setAstcBits(Block.data(), 0, 9, 0x1FC); // Void extent signature.
  setAstcBits(Block.data(), 9, 1, 0);     // LDR.
  setAstcBits(Block.data(), 10, 2, 0x3);  // Reserved bits, both 1.
  setAstcBits(Block.data(), 64, 16, 65535); // R.
  setAstcBits(Block.data(), 80, 16, 0);     // G.
  setAstcBits(Block.data(), 96, 16, 0);     // B.
  setAstcBits(Block.data(), 112, 16, 65535); // A.
  return Block;
}

/// Roadmap E22: a nearest blit from an ASTC LDR source decodes each
/// fetched texel through `feme::vulkan::decodeASTCBlock` (`runBlitImage`'s
/// own `srcColor`) rather than reinterpreting compressed bytes as if they
/// were the destination's own format.
TEST_F(ImageOpsTest, BlitDecodesASTCSource) {
  VkImage Src = createImage(4, 4, VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
  VkImage Dst = createImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM);
  std::array<uint8_t, 16> Block = makeOpaqueRedBlock();
  std::memcpy(fromHandle<Image>(Src)->blockPointer(0, 0, 0, 0, 0),
              Block.data(), Block.size());

  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.srcOffsets[1] = {4, 4, 1};
  Region.dstOffsets[1] = {4, 4, 1};

  ASSERT_FALSE(runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                            Region, VK_FILTER_NEAREST));
  for (uint32_t Y = 0; Y != 4; ++Y)
    for (uint32_t X = 0; X != 4; ++X) {
      EXPECT_EQ(texel(Dst, X, Y)[0], 255);
      EXPECT_EQ(texel(Dst, X, Y)[1], 0);
      EXPECT_EQ(texel(Dst, X, Y)[2], 0);
      EXPECT_EQ(texel(Dst, X, Y)[3], 255);
    }

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// The HDR-only ASTC formats (roadmap E21) are rejected as a blit source:
/// `decodeASTCBlock` is LDR-only, and `decodeASTCBlockHDR`'s
/// float-producing interface does not fit the UNORM8 unpack/pack path
/// every blit here shares (`runBlitImage`'s own comment).
TEST_F(ImageOpsTest, RejectsBlitOfHDRASTCSource) {
  VkImage Src = createImage(4, 4, VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT);
  VkImage Dst = createImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM);

  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.srcOffsets[1] = {4, 4, 1};
  Region.dstOffsets[1] = {4, 4, 1};

  llvm::Error E = runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                               Region, VK_FILTER_NEAREST);
  EXPECT_TRUE(static_cast<bool>(E));
  llvm::consumeError(std::move(E));

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// Roadmap H8n: a nearest blit from a BC1 source decodes each fetched
/// texel through `feme::vulkan::decodeBCBlock` (`runBlitImage`'s own
/// `srcColor`, via `BCSamplingBridge.h`) -- BC1 is the RGBA8-shaped half
/// of the BC family (`isBCRGBA8Format`), mirroring `BlitDecodesASTCSource`
/// above. `color0 == color1` (both opaque red in RGB565) forces BC1's own
/// four-color (opaque) mode regardless of the 2-bit index field, so every
/// texel decodes to opaque red no matter which index each texel encodes.
TEST_F(ImageOpsTest, BlitDecodesBC1Source) {
  VkImage Src = createImage(4, 4, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
  VkImage Dst = createImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM);
  uint8_t Block[8] = {};
  // color0 = color1 = RGB565 opaque red (R=31, G=0, B=0) => 0xF800,
  // little-endian bytes 0x00, 0xF8; leave the two 4-byte index fields
  // implicit-zero.
  Block[0] = 0x00;
  Block[1] = 0xF8;
  Block[2] = 0x00;
  Block[3] = 0xF8;
  std::memcpy(fromHandle<Image>(Src)->blockPointer(0, 0, 0, 0, 0), Block,
              sizeof(Block));

  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.srcOffsets[1] = {4, 4, 1};
  Region.dstOffsets[1] = {4, 4, 1};

  ASSERT_FALSE(runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                            Region, VK_FILTER_NEAREST));
  for (uint32_t Y = 0; Y != 4; ++Y)
    for (uint32_t X = 0; X != 4; ++X) {
      EXPECT_EQ(texel(Dst, X, Y)[0], 255);
      EXPECT_EQ(texel(Dst, X, Y)[1], 0);
      EXPECT_EQ(texel(Dst, X, Y)[2], 0);
      EXPECT_EQ(texel(Dst, X, Y)[3], 255);
    }

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// Roadmap H8n: BC4/BC5/BC6H are rejected as a blit source -- their own
/// sampling-bridge targets (`R8_UNORM`/`R8G8_UNORM`/`R16G16B16A16_FLOAT`)
/// do not fit `runBlitImage`'s own RGBA8-only
/// `unpackColor`/`packClearColor` pipeline, mirroring
/// `RejectsBlitOfHDRASTCSource` above.
TEST_F(ImageOpsTest, RejectsBlitOfBC4Source) {
  VkImage Src = createImage(4, 4, VK_FORMAT_BC4_UNORM_BLOCK);
  VkImage Dst = createImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM);

  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.srcOffsets[1] = {4, 4, 1};
  Region.dstOffsets[1] = {4, 4, 1};

  llvm::Error E = runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                               Region, VK_FILTER_NEAREST);
  EXPECT_TRUE(static_cast<bool>(E));
  llvm::consumeError(std::move(E));

  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

} // namespace

