//===- ImageTest.cpp - VkImage/VkImageView/VkSampler tests --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "Image.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"

#include "llvm/Support/Error.h"
#include "llvm/Testing/Support/Error.h"

#include "gtest/gtest.h"

#include <cstring>
#include <vector>

using namespace feme::vulkan;

namespace {

class ImageTest : public ::testing::Test {
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
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  /// Creates and binds a `Width x Height` `R8G8B8A8_UNORM` 2D image with
  /// \p Usage, \p MipLevels and \p Samples, returning its handle and (via
  /// \p OutMemory) its backing `VkDeviceMemory`, which the caller must free.
  VkImage
  createBoundImage2D(uint32_t Width, uint32_t Height, VkImageUsageFlags Usage,
                     VkDeviceMemory &OutMemory, uint32_t MipLevels = 1,
                     VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT) {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ImageInfo.extent = {Width, Height, 1};
    ImageInfo.mipLevels = MipLevels;
    ImageInfo.arrayLayers = 1;
    ImageInfo.samples = Samples;
    ImageInfo.usage = Usage;
    VkImage Img = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);

    VkMemoryRequirements Reqs{};
    vkGetImageMemoryRequirements(Device, Img, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Reqs.size;
    AllocInfo.memoryTypeIndex = 0;
    EXPECT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &OutMemory),
              VK_SUCCESS);
    EXPECT_EQ(vkBindImageMemory(Device, Img, OutMemory, 0), VK_SUCCESS);
    return Img;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
};

TEST_F(ImageTest, CreateBindDestroy2D) {
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkImage Img = createBoundImage2D(
      4, 4, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, Memory);
  ASSERT_NE(Img, VK_NULL_HANDLE);

  auto *Obj = fromHandle<Image>(Img);
  EXPECT_TRUE(Obj->isBound());
  EXPECT_EQ(Obj->width(), 4u);
  EXPECT_EQ(Obj->height(), 4u);
  // 4x4 texels at 4 bytes/texel, tightly packed: 64 bytes.
  EXPECT_EQ(Obj->sizeInBytes(), 64u);

  vkDestroyImage(Device, Img, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(ImageTest, MipChainSizeIsSumOfLevels) {
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  // 4x4 with 3 mips: level 0 = 4x4 (64B), level 1 = 2x2 (16B),
  // level 2 = 1x1 (4B); total 84 bytes.
  VkImage Img = createBoundImage2D(4, 4, VK_IMAGE_USAGE_SAMPLED_BIT, Memory,
                                   /*MipLevels=*/3);
  auto *Obj = fromHandle<Image>(Img);
  EXPECT_EQ(Obj->sizeInBytes(), 84u);
  ASSERT_EQ(Obj->mipLayouts().size(), 3u);
  EXPECT_EQ(Obj->mipLayouts()[0].Offset, 0u);
  EXPECT_EQ(Obj->mipLayouts()[1].Offset, 64u);
  EXPECT_EQ(Obj->mipLayouts()[2].Offset, 80u);

  vkDestroyImage(Device, Img, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(ImageTest, RejectsUnsupportedFormat) {
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R16_UNORM; // Not mapped by `mapVkFormat`.
  ImageInfo.extent = {1, 1, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  VkImage Img = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img),
            VK_ERROR_FORMAT_NOT_SUPPORTED);
}

TEST_F(ImageTest, RejectsMultisample) {
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {4, 4, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_4_BIT;
  // No `VK_IMAGE_USAGE_SAMPLED_BIT`/`_STORAGE_BIT`: this ICD's device
  // limits only advertise a >1 sample count for those two usages (see
  // PhysicalDeviceInfo.cpp), so a multisample image of any other usage
  // stays rejected, same as before multisample support existed at all.
  VkImage Img = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img),
            VK_ERROR_INITIALIZATION_FAILED);
}

TEST_F(ImageTest, AcceptsMultisampleForSampledOrStorageUsage) {
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  // 4x4 texels, 4 samples/texel, 4 bytes/sample: 4*4*4*4 = 256 bytes.
  VkImage Img = createBoundImage2D(4, 4, VK_IMAGE_USAGE_STORAGE_BIT, Memory,
                                   /*MipLevels=*/1, VK_SAMPLE_COUNT_4_BIT);
  ASSERT_NE(Img, VK_NULL_HANDLE);
  auto *Obj = fromHandle<Image>(Img);
  EXPECT_EQ(Obj->sampleCount(), 4u);
  EXPECT_EQ(Obj->sizeInBytes(), 256u);
  EXPECT_EQ(Obj->mipLayouts()[0].SampleStride, 4u);

  vkDestroyImage(Device, Img, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(ImageTest, MultisampleImageRejectsBufferCopy) {
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkImage Img = createBoundImage2D(
      2, 2,
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
          VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      Memory, /*MipLevels=*/1, VK_SAMPLE_COUNT_2_BIT);

  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 2 * 2 * 2 * 4; // width * height * samples * texel size.
  BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = BufferInfo.size;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory BufMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &BufMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, Buf, BufMemory, 0), VK_SUCCESS);

  VkCommandPoolCreateInfo PoolInfo{};
  VkCommandPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool), VK_SUCCESS);
  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.commandPool = Pool;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer CmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &CmdAllocInfo, &CmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);
  VkBufferImageCopy Region{};
  Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.imageExtent = {2, 2, 1};
  vkCmdCopyBufferToImage(CmdBuf, Buf, Img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &Region);
  ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

  EXPECT_THAT_ERROR(executeCommandBuffer(*fromHandle<CommandBuffer>(CmdBuf)),
                    llvm::Failed());

  vkDestroyCommandPool(Device, Pool, nullptr);
  vkDestroyBuffer(Device, Buf, nullptr);
  vkFreeMemory(Device, BufMemory, nullptr);
  vkDestroyImage(Device, Img, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(ImageTest, CreateViewAndSampler) {
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkImage Img = createBoundImage2D(4, 4, VK_IMAGE_USAGE_SAMPLED_BIT, Memory);

  VkImageViewCreateInfo ViewInfo{};
  ViewInfo.image = Img;
  ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView View = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &View), VK_SUCCESS);
  EXPECT_EQ(fromHandle<ImageView>(View)->image(), fromHandle<Image>(Img));
  EXPECT_EQ(fromHandle<ImageView>(View)->dimension(),
            feme::cpu::ImageDimension::Texture2D);

  VkSamplerCreateInfo SamplerInfo{};
  SamplerInfo.magFilter = VK_FILTER_LINEAR;
  SamplerInfo.minFilter = VK_FILTER_NEAREST;
  SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  VkSampler Samp = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSampler(Device, &SamplerInfo, nullptr, &Samp), VK_SUCCESS);
  const feme::cpu::FemeSamplerDescriptor &Desc =
      fromHandle<Sampler>(Samp)->descriptor();
  EXPECT_EQ(Desc.MagFilter,
            static_cast<uint32_t>(feme::cpu::SamplerFilter::Linear));
  EXPECT_EQ(Desc.MinFilter,
            static_cast<uint32_t>(feme::cpu::SamplerFilter::Nearest));
  EXPECT_EQ(Desc.AddressU,
            static_cast<uint32_t>(feme::cpu::SamplerAddressMode::ClampToEdge));

  vkDestroySampler(Device, Samp, nullptr);
  vkDestroyImageView(Device, View, nullptr);
  vkDestroyImage(Device, Img, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(ImageTest, LayoutTrackingViaPipelineBarrier) {
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkImage Img = createBoundImage2D(4, 4, VK_IMAGE_USAGE_STORAGE_BIT, Memory);
  auto *Obj = fromHandle<Image>(Img);
  EXPECT_EQ(Obj->layout(0, 0), VK_IMAGE_LAYOUT_UNDEFINED);

  VkCommandPoolCreateInfo PoolInfo{};
  VkCommandPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool), VK_SUCCESS);
  VkCommandBufferAllocateInfo AllocInfo{};
  AllocInfo.commandPool = Pool;
  AllocInfo.commandBufferCount = 1;
  VkCommandBuffer CmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &CmdBuf), VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);

  VkImageMemoryBarrier Barrier{};
  Barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  Barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  Barrier.image = Img;
  Barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(CmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &Barrier);
  ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

  ASSERT_THAT_ERROR(executeCommandBuffer(*fromHandle<CommandBuffer>(CmdBuf)),
                    llvm::Succeeded());
  EXPECT_EQ(Obj->layout(0, 0), VK_IMAGE_LAYOUT_GENERAL);

  vkDestroyCommandPool(Device, Pool, nullptr);
  vkDestroyImage(Device, Img, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(ImageTest, CopyBufferToImageAndBack) {
  VkDeviceMemory ImageMemory = VK_NULL_HANDLE;
  VkImage Img = createBoundImage2D(
      2, 2, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      ImageMemory);

  std::vector<uint8_t> SrcPixels(2 * 2 * 4);
  for (size_t I = 0; I != SrcPixels.size(); ++I)
    SrcPixels[I] = static_cast<uint8_t>(I + 1);

  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = SrcPixels.size();
  BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  VkBuffer SrcBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &SrcBuf), VK_SUCCESS);
  VkMemoryAllocateInfo SrcAllocInfo{};
  SrcAllocInfo.allocationSize = SrcPixels.size();
  SrcAllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory SrcMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &SrcAllocInfo, nullptr, &SrcMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, SrcBuf, SrcMemory, 0), VK_SUCCESS);
  std::memcpy(fromHandle<Buffer>(SrcBuf)->data(), SrcPixels.data(),
              SrcPixels.size());

  VkBufferCreateInfo DstBufferInfo{};
  DstBufferInfo.size = SrcPixels.size();
  DstBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer DstBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &DstBufferInfo, nullptr, &DstBuf),
            VK_SUCCESS);
  VkDeviceMemory DstMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &SrcAllocInfo, nullptr, &DstMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, DstBuf, DstMemory, 0), VK_SUCCESS);

  VkCommandPoolCreateInfo PoolInfo{};
  VkCommandPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool), VK_SUCCESS);
  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.commandPool = Pool;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer CmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &CmdAllocInfo, &CmdBuf),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);
  VkBufferImageCopy Region{};
  Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.imageExtent = {2, 2, 1};
  vkCmdCopyBufferToImage(CmdBuf, SrcBuf, Img,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
  vkCmdCopyImageToBuffer(CmdBuf, Img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         DstBuf, 1, &Region);
  ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

  ASSERT_THAT_ERROR(executeCommandBuffer(*fromHandle<CommandBuffer>(CmdBuf)),
                    llvm::Succeeded());
  EXPECT_EQ(std::memcmp(fromHandle<Buffer>(DstBuf)->data(), SrcPixels.data(),
                        SrcPixels.size()),
            0);

  vkDestroyCommandPool(Device, Pool, nullptr);
  vkDestroyBuffer(Device, SrcBuf, nullptr);
  vkDestroyBuffer(Device, DstBuf, nullptr);
  vkFreeMemory(Device, SrcMemory, nullptr);
  vkFreeMemory(Device, DstMemory, nullptr);
  vkDestroyImage(Device, Img, nullptr);
  vkFreeMemory(Device, ImageMemory, nullptr);
}

TEST_F(ImageTest, CopyImageToImage) {
  VkDeviceMemory SrcMemory = VK_NULL_HANDLE;
  VkImage SrcImg =
      createBoundImage2D(2, 2, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, SrcMemory);
  VkDeviceMemory DstMemory = VK_NULL_HANDLE;
  VkImage DstImg =
      createBoundImage2D(2, 2, VK_IMAGE_USAGE_TRANSFER_DST_BIT, DstMemory);

  auto *SrcObj = fromHandle<Image>(SrcImg);
  for (uint32_t I = 0; I != SrcObj->sizeInBytes(); ++I)
    static_cast<uint8_t *>(SrcObj->data())[I] = static_cast<uint8_t>(I + 5);

  VkCommandPoolCreateInfo PoolInfo{};
  VkCommandPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool), VK_SUCCESS);
  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.commandPool = Pool;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer CmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &CmdAllocInfo, &CmdBuf),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);
  VkImageCopy Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.extent = {2, 2, 1};
  vkCmdCopyImage(CmdBuf, SrcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, DstImg,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
  ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

  ASSERT_THAT_ERROR(executeCommandBuffer(*fromHandle<CommandBuffer>(CmdBuf)),
                    llvm::Succeeded());

  auto *DstObj = fromHandle<Image>(DstImg);
  EXPECT_EQ(std::memcmp(SrcObj->data(), DstObj->data(), SrcObj->sizeInBytes()),
            0);

  vkDestroyCommandPool(Device, Pool, nullptr);
  vkDestroyImage(Device, SrcImg, nullptr);
  vkDestroyImage(Device, DstImg, nullptr);
  vkFreeMemory(Device, SrcMemory, nullptr);
  vkFreeMemory(Device, DstMemory, nullptr);
}

TEST_F(ImageTest, CopyImageBetweenCompatibleFormats) {
  // `vkCmdCopyImage` requires matching texel size, not matching `VkFormat`
  // (see CommandBuffer.cpp's `runCopyImage`): `R8G8B8A8_UNORM` and
  // `R8G8B8A8_UINT` are both 4 bytes/texel but distinct formats, exactly
  // the "compatible formats" case real Vulkan's own copy rule allows and
  // this ICD used to reject outright.
  VkDeviceMemory SrcMemory = VK_NULL_HANDLE;
  VkImage SrcImg =
      createBoundImage2D(2, 2, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, SrcMemory);

  VkImageCreateInfo DstInfo{};
  DstInfo.imageType = VK_IMAGE_TYPE_2D;
  DstInfo.format = VK_FORMAT_R8G8B8A8_UINT;
  DstInfo.extent = {2, 2, 1};
  DstInfo.mipLevels = 1;
  DstInfo.arrayLayers = 1;
  DstInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  DstInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkImage DstImg = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &DstInfo, nullptr, &DstImg), VK_SUCCESS);
  VkMemoryRequirements DstReqs{};
  vkGetImageMemoryRequirements(Device, DstImg, &DstReqs);
  VkMemoryAllocateInfo DstAllocInfo{};
  DstAllocInfo.allocationSize = DstReqs.size;
  DstAllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory DstMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &DstAllocInfo, nullptr, &DstMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, DstImg, DstMemory, 0), VK_SUCCESS);

  auto *SrcObj = fromHandle<Image>(SrcImg);
  for (uint32_t I = 0; I != SrcObj->sizeInBytes(); ++I)
    static_cast<uint8_t *>(SrcObj->data())[I] = static_cast<uint8_t>(I + 5);

  VkCommandPoolCreateInfo PoolInfo{};
  VkCommandPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool), VK_SUCCESS);
  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.commandPool = Pool;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer CmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &CmdAllocInfo, &CmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);
  VkImageCopy Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.extent = {2, 2, 1};
  vkCmdCopyImage(CmdBuf, SrcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, DstImg,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
  ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

  ASSERT_THAT_ERROR(executeCommandBuffer(*fromHandle<CommandBuffer>(CmdBuf)),
                    llvm::Succeeded());

  auto *DstObj = fromHandle<Image>(DstImg);
  EXPECT_EQ(std::memcmp(SrcObj->data(), DstObj->data(), SrcObj->sizeInBytes()),
            0);

  vkDestroyCommandPool(Device, Pool, nullptr);
  vkDestroyImage(Device, SrcImg, nullptr);
  vkDestroyImage(Device, DstImg, nullptr);
  vkFreeMemory(Device, SrcMemory, nullptr);
  vkFreeMemory(Device, DstMemory, nullptr);
}

TEST_F(ImageTest, CopyImageRejectsIncompatibleTexelSize) {
  VkDeviceMemory SrcMemory = VK_NULL_HANDLE;
  VkImage SrcImg =
      createBoundImage2D(2, 2, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, SrcMemory);

  VkImageCreateInfo DstInfo{};
  DstInfo.imageType = VK_IMAGE_TYPE_2D;
  DstInfo.format = VK_FORMAT_R32G32B32A32_UINT; // 16 bytes/texel.
  DstInfo.extent = {2, 2, 1};
  DstInfo.mipLevels = 1;
  DstInfo.arrayLayers = 1;
  DstInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  DstInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkImage DstImg = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &DstInfo, nullptr, &DstImg), VK_SUCCESS);
  VkMemoryRequirements DstReqs{};
  vkGetImageMemoryRequirements(Device, DstImg, &DstReqs);
  VkMemoryAllocateInfo DstAllocInfo{};
  DstAllocInfo.allocationSize = DstReqs.size;
  DstAllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory DstMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &DstAllocInfo, nullptr, &DstMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, DstImg, DstMemory, 0), VK_SUCCESS);

  VkCommandPoolCreateInfo PoolInfo{};
  VkCommandPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool), VK_SUCCESS);
  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.commandPool = Pool;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer CmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &CmdAllocInfo, &CmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);
  VkImageCopy Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.extent = {2, 2, 1};
  vkCmdCopyImage(CmdBuf, SrcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, DstImg,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
  ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

  EXPECT_THAT_ERROR(executeCommandBuffer(*fromHandle<CommandBuffer>(CmdBuf)),
                    llvm::Failed());

  vkDestroyCommandPool(Device, Pool, nullptr);
  vkDestroyImage(Device, SrcImg, nullptr);
  vkDestroyImage(Device, DstImg, nullptr);
  vkFreeMemory(Device, SrcMemory, nullptr);
  vkFreeMemory(Device, DstMemory, nullptr);
}

TEST_F(ImageTest, CopyMultisampleImagePreservesEverySample) {
  VkDeviceMemory SrcMemory = VK_NULL_HANDLE;
  VkImage SrcImg = createBoundImage2D(
      2, 2, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
      SrcMemory, /*MipLevels=*/1, VK_SAMPLE_COUNT_4_BIT);
  VkDeviceMemory DstMemory = VK_NULL_HANDLE;
  VkImage DstImg = createBoundImage2D(
      2, 2, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
      DstMemory, /*MipLevels=*/1, VK_SAMPLE_COUNT_4_BIT);

  auto *SrcObj = fromHandle<Image>(SrcImg);
  for (uint32_t I = 0; I != SrcObj->sizeInBytes(); ++I)
    static_cast<uint8_t *>(SrcObj->data())[I] = static_cast<uint8_t>(I + 9);

  VkCommandPoolCreateInfo PoolInfo{};
  VkCommandPool Pool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool), VK_SUCCESS);
  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.commandPool = Pool;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer CmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &CmdAllocInfo, &CmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);
  VkImageCopy Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.extent = {2, 2, 1};
  vkCmdCopyImage(CmdBuf, SrcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, DstImg,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
  ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

  ASSERT_THAT_ERROR(executeCommandBuffer(*fromHandle<CommandBuffer>(CmdBuf)),
                    llvm::Succeeded());

  auto *DstObj = fromHandle<Image>(DstImg);
  EXPECT_EQ(SrcObj->sizeInBytes(), DstObj->sizeInBytes());
  EXPECT_EQ(std::memcmp(SrcObj->data(), DstObj->data(), SrcObj->sizeInBytes()),
            0);

  vkDestroyCommandPool(Device, Pool, nullptr);
  vkDestroyImage(Device, SrcImg, nullptr);
  vkDestroyImage(Device, DstImg, nullptr);
  vkFreeMemory(Device, SrcMemory, nullptr);
  vkFreeMemory(Device, DstMemory, nullptr);
}

} // namespace
