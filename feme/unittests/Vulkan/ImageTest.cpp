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
    return createBoundImage2DWithFormat(VK_FORMAT_R8G8B8A8_UNORM, Width,
                                        Height, Usage, OutMemory, MipLevels,
                                        Samples);
  }

  /// The same as `createBoundImage2D`, but for an arbitrary \p Format --
  /// used by the roadmap E22 ASTC copy tests below, which need a
  /// block-compressed format `createBoundImage2D` itself does not take.
  VkImage createBoundImage2DWithFormat(
      VkFormat Format, uint32_t Width, uint32_t Height,
      VkImageUsageFlags Usage, VkDeviceMemory &OutMemory,
      uint32_t MipLevels = 1,
      VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT) {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = Format;
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

/// Roadmap E22: `vkCreateImage` no longer rejects a block-compressed
/// `VkFormat` outright (E20 landed the block-aware layout math;
/// `blockPointer`/`CommandBuffer.cpp`'s copy paths now address one -- see
/// Image.h's file comment). A live 4x4 ASTC_4x4 image is exactly one
/// 16-byte block.
TEST_F(ImageTest, AcceptsASTCFormat) {
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
  ImageInfo.extent = {4, 4, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkImage Img = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);

  auto *Obj = fromHandle<Image>(Img);
  EXPECT_EQ(Obj->sizeInBytes(), 16u);

  VkMemoryRequirements Reqs{};
  vkGetImageMemoryRequirements(Device, Img, &Reqs);
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = Reqs.size;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, Img, Memory, 0), VK_SUCCESS);
  EXPECT_TRUE(Obj->isBound());

  vkDestroyImage(Device, Img, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

/// `blockPointer` addresses a block-compressed image's storage a whole
/// block at a time, in block-grid coordinates -- a 6x6 ASTC_4x4 image is a
/// 2x2 block grid (each block covering a 4x4 texel tile, the last column/
/// row's block only half-covered by real texels, per `computeSubresourceLayouts`'s
/// own ceiling-division rounding), so block (1, 1) starts 3 blocks (48
/// bytes) into the image's 4-block, 64-byte storage.
TEST_F(ImageTest, BlockPointerAddressesBlockGrid) {
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
  ImageInfo.extent = {6, 6, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkImage Img = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);
  auto *Obj = fromHandle<Image>(Img);
  ASSERT_EQ(Obj->sizeInBytes(), 64u);

  VkMemoryRequirements Reqs{};
  vkGetImageMemoryRequirements(Device, Img, &Reqs);
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = Reqs.size;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, Img, Memory, 0), VK_SUCCESS);

  auto *Base = static_cast<uint8_t *>(Obj->data());
  EXPECT_EQ(Obj->blockPointer(0, 0, 0, 0, 0), Base);
  EXPECT_EQ(Obj->blockPointer(0, 0, 1, 0, 0), Base + 16);
  EXPECT_EQ(Obj->blockPointer(0, 0, 0, 1, 0), Base + 32);
  EXPECT_EQ(Obj->blockPointer(0, 0, 1, 1, 0), Base + 48);

  vkDestroyImage(Device, Img, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

/// Roadmap E20: `computeSubresourceLayouts`' block-based rework, exercised
/// both through the info-only `vkGetDeviceImageMemoryRequirements` path
/// (matching `GetDeviceImageMemoryRequirementsMatchesLiveImage`'s own
/// pattern for a non-block-compressed format) and a live `Image` (roadmap
/// E22 made one constructible). A 6x6 ASTC_4x4 image (3 mips) rounds each
/// level's *block* extent up, not its texel extent: level 0 is 6x6 texels
/// -> ceil(6/4) = 2x2 blocks (64B); level 1 is 3x3 texels -> still
/// ceil(3/4) = 1x1 block (16B), not empty; level 2 is 1x1 texels -> 1x1
/// block (16B). Total 96 bytes, every ASTC block always 16 bytes
/// regardless of footprint (`bytesPerBlock`).
TEST_F(ImageTest, GetDeviceImageMemoryRequirementsForASTCBlockLayout) {
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
  ImageInfo.extent = {6, 6, 1};
  ImageInfo.mipLevels = 3;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

  VkDeviceImageMemoryRequirements Info{};
  Info.pCreateInfo = &ImageInfo;
  VkMemoryRequirements2 Reqs2{};
  vkGetDeviceImageMemoryRequirements(Device, &Info, &Reqs2);
  EXPECT_EQ(Reqs2.memoryRequirements.size, 96u);

  VkImage Img = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);
  EXPECT_EQ(fromHandle<Image>(Img)->sizeInBytes(), 96u);
  vkDestroyImage(Device, Img, nullptr);
}

/// Roadmap E4 (`VK_KHR_maintenance4`): the same requirements a live
/// `VkImage` of this shape would report, computed from its
/// `VkImageCreateInfo` alone -- no `vkCreateImage` call at all.
TEST_F(ImageTest, GetDeviceImageMemoryRequirementsMatchesLiveImage) {
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {4, 4, 1};
  ImageInfo.mipLevels = 3;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

  VkImage Img = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);
  VkMemoryRequirements LiveReqs{};
  vkGetImageMemoryRequirements(Device, Img, &LiveReqs);
  vkDestroyImage(Device, Img, nullptr);

  VkDeviceImageMemoryRequirements Info{};
  Info.pCreateInfo = &ImageInfo;
  VkMemoryRequirements2 Reqs2{};
  vkGetDeviceImageMemoryRequirements(Device, &Info, &Reqs2);
  EXPECT_EQ(Reqs2.memoryRequirements.size, LiveReqs.size);
  EXPECT_EQ(Reqs2.memoryRequirements.alignment, LiveReqs.alignment);
  EXPECT_EQ(Reqs2.memoryRequirements.memoryTypeBits, LiveReqs.memoryTypeBits);
  // 4x4 (64B) + 2x2 (16B) + 1x1 (4B), same as `MipChainSizeIsSumOfLevels`.
  EXPECT_EQ(Reqs2.memoryRequirements.size, 84u);
}

/// An unsupported format/shape is not fatal -- unlike `vkCreateImage`, this
/// entrypoint has no `VkResult` to report it through, so it reports an
/// all-zero result instead of asserting or reading uninitialized state.
TEST_F(ImageTest, GetDeviceImageMemoryRequirementsZeroForUnsupportedFormat) {
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R16_UNORM;
  ImageInfo.extent = {1, 1, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

  VkDeviceImageMemoryRequirements Info{};
  Info.pCreateInfo = &ImageInfo;
  VkMemoryRequirements2 Reqs2{};
  Reqs2.memoryRequirements.size = 0xdeadbeef;
  vkGetDeviceImageMemoryRequirements(Device, &Info, &Reqs2);
  EXPECT_EQ(Reqs2.memoryRequirements.size, 0u);
}

/// Roadmap E4: no sparse residency is supported, so this always reports
/// zero sparse memory requirements, mirroring
/// `vkGetPhysicalDeviceSparseImageFormatProperties`'s own empty result.
TEST_F(ImageTest, GetDeviceImageSparseMemoryRequirementsReportsNone) {
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {4, 4, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

  VkDeviceImageMemoryRequirements Info{};
  Info.pCreateInfo = &ImageInfo;
  uint32_t Count = 42;
  vkGetDeviceImageSparseMemoryRequirements(Device, &Info, &Count, nullptr);
  EXPECT_EQ(Count, 0u);
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

// Roadmap E3: mirrors `LayoutTrackingViaPipelineBarrier` above through
// `vkCmdPipelineBarrier2`'s `VkDependencyInfo`/`VkImageMemoryBarrier2`
// (2-stage/2-access-mask) shape, translated down to the identical
// `ImageLayoutTransition` payload.
TEST_F(ImageTest, LayoutTrackingViaPipelineBarrier2) {
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

  VkImageMemoryBarrier2 Barrier{};
  Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  Barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  Barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  Barrier.image = Img;
  Barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkDependencyInfo DepInfo{};
  DepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  DepInfo.imageMemoryBarrierCount = 1;
  DepInfo.pImageMemoryBarriers = &Barrier;
  vkCmdPipelineBarrier2(CmdBuf, &DepInfo);
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

/// Roadmap E22: `vkCmdCopyBufferToImage`/`vkCmdCopyImageToBuffer` now
/// address a block-compressed image a whole block at a time
/// (`CommandBuffer.cpp`'s `copyBufferImageRegion`) instead of asserting on
/// `texelPointer`. An 8x8 ASTC_4x4 image is a 2x2 block grid, 4 blocks
/// (64 bytes) total; the buffer's own bytes are never decoded, only moved
/// verbatim (real Vulkan's own "compressed copies reinterpret bits"
/// rule -- see `runCopyImage`'s comment for the same rule applied to an
/// image-to-image copy).
TEST_F(ImageTest, CopyBufferToASTCImageAndBack) {
  VkDeviceMemory ImageMemory = VK_NULL_HANDLE;
  VkImage Img = createBoundImage2DWithFormat(
      VK_FORMAT_ASTC_4x4_UNORM_BLOCK, 8, 8,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      ImageMemory);
  ASSERT_EQ(fromHandle<Image>(Img)->sizeInBytes(), 64u);

  std::vector<uint8_t> SrcBlocks(64);
  for (size_t I = 0; I != SrcBlocks.size(); ++I)
    SrcBlocks[I] = static_cast<uint8_t>(I + 1);

  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = SrcBlocks.size();
  BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  VkBuffer SrcBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &SrcBuf), VK_SUCCESS);
  VkMemoryAllocateInfo SrcAllocInfo{};
  SrcAllocInfo.allocationSize = SrcBlocks.size();
  SrcAllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory SrcMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &SrcAllocInfo, nullptr, &SrcMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, SrcBuf, SrcMemory, 0), VK_SUCCESS);
  std::memcpy(fromHandle<Buffer>(SrcBuf)->data(), SrcBlocks.data(),
              SrcBlocks.size());

  VkBufferCreateInfo DstBufferInfo{};
  DstBufferInfo.size = SrcBlocks.size();
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
  Region.imageExtent = {8, 8, 1};
  vkCmdCopyBufferToImage(CmdBuf, SrcBuf, Img,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
  vkCmdCopyImageToBuffer(CmdBuf, Img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         DstBuf, 1, &Region);
  ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

  ASSERT_THAT_ERROR(executeCommandBuffer(*fromHandle<CommandBuffer>(CmdBuf)),
                    llvm::Succeeded());
  EXPECT_EQ(std::memcmp(fromHandle<Buffer>(DstBuf)->data(), SrcBlocks.data(),
                        SrcBlocks.size()),
            0);

  vkDestroyCommandPool(Device, Pool, nullptr);
  vkDestroyBuffer(Device, SrcBuf, nullptr);
  vkDestroyBuffer(Device, DstBuf, nullptr);
  vkFreeMemory(Device, SrcMemory, nullptr);
  vkFreeMemory(Device, DstMemory, nullptr);
  vkDestroyImage(Device, Img, nullptr);
  vkFreeMemory(Device, ImageMemory, nullptr);
}

/// Roadmap E22: `vkCmdCopyImage` between two block-compressed images now
/// uses `bytesPerBlock`/`blockPointer` instead of `formatElementSize`/
/// `texelPointer` (which are meaningless/asserting, respectively, for a
/// block-compressed `Format`).
TEST_F(ImageTest, CopyASTCImageToImage) {
  VkDeviceMemory SrcMemory = VK_NULL_HANDLE;
  VkImage SrcImg = createBoundImage2DWithFormat(
      VK_FORMAT_ASTC_4x4_UNORM_BLOCK, 4, 4, VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      SrcMemory);
  VkDeviceMemory DstMemory = VK_NULL_HANDLE;
  VkImage DstImg = createBoundImage2DWithFormat(
      VK_FORMAT_ASTC_4x4_UNORM_BLOCK, 4, 4, VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      DstMemory);

  auto *SrcObj = fromHandle<Image>(SrcImg);
  ASSERT_EQ(SrcObj->sizeInBytes(), 16u);
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
  Region.extent = {4, 4, 1};
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

TEST_F(ImageTest, RejectsCustomBorderColorSwizzlePNext) {
  // Neither `VK_EXT_custom_border_color` nor `VK_EXT_border_color_swizzle`
  // is advertised (see `vkCreateSampler`'s own comment); chaining either
  // extension's create-info struct is rejected explicitly rather than
  // silently ignored.
  VkSamplerBorderColorComponentMappingCreateInfoEXT Swizzle{};
  Swizzle.sType =
      VK_STRUCTURE_TYPE_SAMPLER_BORDER_COLOR_COMPONENT_MAPPING_CREATE_INFO_EXT;
  VkSamplerCreateInfo SamplerInfo{};
  SamplerInfo.pNext = &Swizzle;
  VkSampler Samp = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateSampler(Device, &SamplerInfo, nullptr, &Samp),
            VK_ERROR_FEATURE_NOT_PRESENT);

  VkSamplerCustomBorderColorCreateInfoEXT CustomColor{};
  CustomColor.sType =
      VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT;
  SamplerInfo.pNext = &CustomColor;
  EXPECT_EQ(vkCreateSampler(Device, &SamplerInfo, nullptr, &Samp),
            VK_ERROR_FEATURE_NOT_PRESENT);
}

} // namespace
