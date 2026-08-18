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
                      VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT) {
    VkImageCreateInfo Info{};
    Info.imageType = VK_IMAGE_TYPE_2D;
    Info.format = Format;
    Info.extent = {Width, Height, 1};
    Info.mipLevels = 1;
    Info.arrayLayers = 1;
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
                                      uint32_t Sample = 0) {
    const void *Ptr = fromHandle<Image>(Img)->texelPointer(0, 0, X, Y, 0,
                                                           Sample);
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

/// A blit between differing formats has no conversion path, so it fails
/// rather than reinterpreting bytes.
TEST_F(ImageOpsTest, RejectsFormatConvertingBlit) {
  VkImage Src = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM);
  VkImage Dst = createImage(2, 2, VK_FORMAT_R32G32B32A32_SFLOAT);
  VkImageBlit Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.srcOffsets[1] = {2, 2, 1};
  Region.dstOffsets[1] = {2, 2, 1};
  llvm::Error E = runBlitImage(fromHandle<Image>(Src), fromHandle<Image>(Dst),
                               Region, VK_FILTER_NEAREST);
  EXPECT_TRUE(static_cast<bool>(E));
  llvm::consumeError(std::move(E));
  vkDestroyImage(Device, Src, nullptr);
  vkDestroyImage(Device, Dst, nullptr);
}

/// Resolving a 4-sample image averages its samples, the same box filter the
/// executor's own resolve attachments use.
TEST_F(ImageOpsTest, ResolvesMultisampleImage) {
  VkImage Src = createImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_SAMPLE_COUNT_4_BIT);
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

} // namespace
