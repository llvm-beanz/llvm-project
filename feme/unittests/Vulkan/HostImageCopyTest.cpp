//===- HostImageCopyTest.cpp - vkCopy*/vkTransitionImageLayout tests -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap F11: `VK_EXT_host_image_copy`'s four commands, each exercised
// with no `VkCommandBuffer` at all (see HostImageCopy.h's own file
// comment).
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "HostImageCopy.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Image.h"
#include "Objects.h"

#include "gtest/gtest.h"

#include <cstring>
#include <vector>

using namespace feme::vulkan;

namespace {

class HostImageCopyTest : public ::testing::Test {
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
    for (VkImage Img : Images)
      vkDestroyImage(Device, Img, nullptr);
    for (VkDeviceMemory Memory : Allocations)
      vkFreeMemory(Device, Memory, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  /// Creates and binds a `Width x Height` `R8G8B8A8_UNORM` 2D image.
  VkImage createBoundImage2D(uint32_t Width, uint32_t Height,
                             VkImageUsageFlags Usage) {
    return createBoundImage2DWithFormat(VK_FORMAT_R8G8B8A8_UNORM, Width,
                                        Height, Usage);
  }

  /// The same as `createBoundImage2D`, but for an arbitrary \p Format.
  VkImage createBoundImage2DWithFormat(VkFormat Format, uint32_t Width,
                                       uint32_t Height,
                                       VkImageUsageFlags Usage) {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = Format;
    ImageInfo.extent = {Width, Height, 1};
    ImageInfo.mipLevels = 1;
    ImageInfo.arrayLayers = 1;
    ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ImageInfo.usage = Usage;
    VkImage Img = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);

    VkMemoryRequirements Reqs{};
    vkGetImageMemoryRequirements(Device, Img, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Reqs.size;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    EXPECT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory),
              VK_SUCCESS);
    EXPECT_EQ(vkBindImageMemory(Device, Img, Memory, 0), VK_SUCCESS);
    Allocations.push_back(Memory);
    Images.push_back(Img);
    return Img;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  std::vector<VkDeviceMemory> Allocations;
  std::vector<VkImage> Images;
};

TEST_F(HostImageCopyTest, CopiesMemoryToImageAndBack) {
  VkImage Img = createBoundImage2D(
      2, 2, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

  std::vector<uint8_t> SrcPixels(2 * 2 * 4);
  for (size_t I = 0; I != SrcPixels.size(); ++I)
    SrcPixels[I] = static_cast<uint8_t>(I + 1);

  VkMemoryToImageCopy ToImageRegion{};
  ToImageRegion.pHostPointer = SrcPixels.data();
  ToImageRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  ToImageRegion.imageExtent = {2, 2, 1};

  VkCopyMemoryToImageInfo ToImageInfo{};
  ToImageInfo.dstImage = Img;
  ToImageInfo.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
  ToImageInfo.regionCount = 1;
  ToImageInfo.pRegions = &ToImageRegion;
  ASSERT_EQ(vkCopyMemoryToImage(Device, &ToImageInfo), VK_SUCCESS);

  EXPECT_EQ(std::memcmp(fromHandle<Image>(Img)->texelPointer(0, 0, 0, 0, 0),
                        SrcPixels.data(), SrcPixels.size()),
            0);

  std::vector<uint8_t> DstPixels(SrcPixels.size());
  VkImageToMemoryCopy ToMemoryRegion{};
  ToMemoryRegion.pHostPointer = DstPixels.data();
  ToMemoryRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  ToMemoryRegion.imageExtent = {2, 2, 1};

  VkCopyImageToMemoryInfo ToMemoryInfo{};
  ToMemoryInfo.srcImage = Img;
  ToMemoryInfo.srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
  ToMemoryInfo.regionCount = 1;
  ToMemoryInfo.pRegions = &ToMemoryRegion;
  ASSERT_EQ(vkCopyImageToMemory(Device, &ToMemoryInfo), VK_SUCCESS);

  EXPECT_EQ(std::memcmp(DstPixels.data(), SrcPixels.data(), SrcPixels.size()),
            0);
}

TEST_F(HostImageCopyTest, CopyMemoryToImageRejectsUnboundDestination) {
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {2, 2, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkImage Img = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);

  uint8_t Pixel[4] = {1, 2, 3, 4};
  VkMemoryToImageCopy Region{};
  Region.pHostPointer = Pixel;
  Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.imageExtent = {1, 1, 1};

  VkCopyMemoryToImageInfo Info{};
  Info.dstImage = Img;
  Info.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
  Info.regionCount = 1;
  Info.pRegions = &Region;
  EXPECT_EQ(vkCopyMemoryToImage(Device, &Info), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyImage(Device, Img, nullptr);
}

/// Roadmap F11a: `copyBufferImageRegion`'s buffer-side sizing always used
/// `Img.format()`'s own combined `bytesPerBlock`, but a copy region for a
/// combined depth/stencil format always names exactly one aspect, whose
/// own buffer-side size differs from the combined texel's -- unlike
/// `vkCmdCopyBufferToImage` (a bound `VkBuffer`'s own size at least
/// bounds-checks a too-large computed row against, turning the mismatch
/// into a clean rejection), `vkCopyMemoryToImage`'s raw host pointer has
/// no size of its own to catch it against at all, previously making this
/// a real out-of-bounds host-memory read/write (F11's own finding). This
/// now exercises the real, per-texel read-modify-write support F11a adds:
/// a depth-aspect `vkCopyMemoryToImage`/`vkCopyImageToMemory` round trip
/// through a raw host pointer, with no bound `VkBuffer` at all.
TEST_F(HostImageCopyTest,
       CopiesDepthAspectOfCombinedDepthStencilFormatRoundTrip) {
  VkImage Img = createBoundImage2DWithFormat(
      VK_FORMAT_D32_SFLOAT_S8_UINT, 2, 2,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

  // Seed every texel's stencil word with a distinct value the depth-aspect
  // copy below must not disturb.
  auto *ImgObj = fromHandle<Image>(Img);
  for (uint32_t I = 0; I != 4; ++I) {
    auto *Texel =
        static_cast<uint8_t *>(ImgObj->texelPointer(0, 0, I % 2, I / 2, 0));
    uint32_t Word1 = 0x5A;
    std::memcpy(Texel + 4, &Word1, sizeof(Word1));
  }

  std::vector<float> DepthValues = {0.0f, 0.25f, 0.5f, 1.0f};
  std::vector<uint8_t> DepthPixels(DepthValues.size() * 4);
  std::memcpy(DepthPixels.data(), DepthValues.data(), DepthPixels.size());

  VkMemoryToImageCopy ToImageRegion{};
  ToImageRegion.pHostPointer = DepthPixels.data();
  ToImageRegion.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
  ToImageRegion.imageExtent = {2, 2, 1};

  VkCopyMemoryToImageInfo ToImageInfo{};
  ToImageInfo.dstImage = Img;
  ToImageInfo.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
  ToImageInfo.regionCount = 1;
  ToImageInfo.pRegions = &ToImageRegion;
  ASSERT_EQ(vkCopyMemoryToImage(Device, &ToImageInfo), VK_SUCCESS);

  for (uint32_t I = 0; I != 4; ++I) {
    auto *Texel =
        static_cast<uint8_t *>(ImgObj->texelPointer(0, 0, I % 2, I / 2, 0));
    uint32_t Word1;
    std::memcpy(&Word1, Texel + 4, sizeof(Word1));
    EXPECT_EQ(Word1, 0x5Au); // Stencil word untouched.
  }

  std::vector<uint8_t> DstPixels(DepthPixels.size());
  VkImageToMemoryCopy ToMemoryRegion{};
  ToMemoryRegion.pHostPointer = DstPixels.data();
  ToMemoryRegion.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
  ToMemoryRegion.imageExtent = {2, 2, 1};

  VkCopyImageToMemoryInfo ToMemoryInfo{};
  ToMemoryInfo.srcImage = Img;
  ToMemoryInfo.srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
  ToMemoryInfo.regionCount = 1;
  ToMemoryInfo.pRegions = &ToMemoryRegion;
  ASSERT_EQ(vkCopyImageToMemory(Device, &ToMemoryInfo), VK_SUCCESS);

  EXPECT_EQ(
      std::memcmp(DstPixels.data(), DepthPixels.data(), DepthPixels.size()), 0);
}

TEST_F(HostImageCopyTest, CopiesImageToImage) {
  VkImage SrcImg = createBoundImage2D(2, 2, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  VkImage DstImg = createBoundImage2D(2, 2, VK_IMAGE_USAGE_TRANSFER_DST_BIT);

  auto *SrcObj = fromHandle<Image>(SrcImg);
  for (uint32_t I = 0; I != SrcObj->sizeInBytes(); ++I)
    static_cast<uint8_t *>(SrcObj->data())[I] = static_cast<uint8_t>(I + 9);

  VkImageCopy2 Region{};
  Region.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.extent = {2, 2, 1};

  VkCopyImageToImageInfo Info{};
  Info.srcImage = SrcImg;
  Info.srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
  Info.dstImage = DstImg;
  Info.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
  Info.regionCount = 1;
  Info.pRegions = &Region;
  ASSERT_EQ(vkCopyImageToImage(Device, &Info), VK_SUCCESS);

  auto *DstObj = fromHandle<Image>(DstImg);
  EXPECT_EQ(std::memcmp(SrcObj->data(), DstObj->data(), SrcObj->sizeInBytes()),
            0);
}

TEST_F(HostImageCopyTest, TransitionsImageLayoutWithoutACommandBuffer) {
  VkImage Img = createBoundImage2D(2, 2, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  auto *ImgObj = fromHandle<Image>(Img);
  EXPECT_EQ(ImgObj->layout(0, 0), VK_IMAGE_LAYOUT_UNDEFINED);

  VkHostImageLayoutTransitionInfo Transition{};
  Transition.image = Img;
  Transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  Transition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  Transition.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                 VK_REMAINING_MIP_LEVELS, 0,
                                 VK_REMAINING_ARRAY_LAYERS};
  ASSERT_EQ(vkTransitionImageLayout(Device, 1, &Transition), VK_SUCCESS);
  EXPECT_EQ(ImgObj->layout(0, 0), VK_IMAGE_LAYOUT_GENERAL);
}

TEST_F(HostImageCopyTest, SupportedLayoutListsAgreeWithProperties) {
  // Roadmap F11: the same lists `EntryPoints.cpp` reports through
  // `VkPhysicalDeviceHostImageCopyProperties`/the aggregate
  // `VkPhysicalDeviceVulkan14Properties` (PhysicalDeviceInfoTest.cpp) come
  // from these two functions -- verified directly here so a future edit to
  // one without the other is caught close to the source.
  llvm::ArrayRef<VkImageLayout> SrcLayouts =
      getSupportedHostImageCopySrcLayouts();
  ASSERT_EQ(SrcLayouts.size(), 2u);
  EXPECT_EQ(SrcLayouts[0], VK_IMAGE_LAYOUT_GENERAL);
  EXPECT_EQ(SrcLayouts[1], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  llvm::ArrayRef<VkImageLayout> DstLayouts =
      getSupportedHostImageCopyDstLayouts();
  ASSERT_EQ(DstLayouts.size(), 2u);
  EXPECT_EQ(DstLayouts[0], VK_IMAGE_LAYOUT_GENERAL);
  EXPECT_EQ(DstLayouts[1], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}

} // namespace
