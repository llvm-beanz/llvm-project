//===- feme-vulkan-image-loader-smoke.cpp - Image loader client -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A tiny Vulkan client, linked against the real Khronos Vulkan loader (not
// `libfeme_vulkan` directly), for V5's own "Images and sampling" milestone
// lit test (see "Testing Strategy" in feme/docs/FeMeVulkanDesign.md: "Lit
// tests invoking tiny Vulkan clients with `VK_DRIVER_FILES` set to the
// build-tree manifest"). V5's own unit tests (unittests/Vulkan/ImageTest.cpp)
// already exercise this ICD's image/view/sampler/copy object model directly
// against `libfeme_vulkan`; this client instead exercises the exact same
// object model through the real loader's dispatch tables, catching anything
// specific to that process boundary (entry-point resolution, extension
// struct sizes, `VkAllocationCallbacks`-less allocation, etc.) the way
// storage-buffer-lavapipe-diff.test does for compute dispatch.
//
// No shader consumption is exercised (see FeMeVulkanDesign.md's "V5"
// deviation note: a real dispatch cannot yet read an image or sampler --
// that is R30's remaining scope), so this client only creates a 2D image
// and a view/sampler pair, uploads a byte pattern through
// `vkCmdCopyBufferToImage`, round-trips it through `vkCmdCopyImage` to a
// second image of a *different but texel-size-compatible* format (see
// CommandBuffer.cpp's `runCopyImage`), and reads it back with
// `vkCmdCopyImageToBuffer`, printing the final bytes as hex for `FileCheck`
// to compare against the uploaded pattern.
//
//===----------------------------------------------------------------------===//

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

[[noreturn]] void fail(const char *Step, VkResult Result) {
  std::fprintf(stderr, "FAIL: %s (VkResult = %d)\n", Step,
               static_cast<int>(Result));
  std::exit(1);
}

} // namespace

int main() {
  VkApplicationInfo AppInfo{};
  AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  AppInfo.pApplicationName = "feme-vulkan-image-loader-smoke";
  AppInfo.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo InstanceInfo{};
  InstanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  InstanceInfo.pApplicationInfo = &AppInfo;

  VkInstance Instance;
  if (VkResult R = vkCreateInstance(&InstanceInfo, nullptr, &Instance))
    fail("vkCreateInstance", R);

  uint32_t DeviceCount = 1;
  VkPhysicalDevice PhysicalDevice;
  if (VkResult R =
          vkEnumeratePhysicalDevices(Instance, &DeviceCount, &PhysicalDevice))
    if (R != VK_INCOMPLETE)
      fail("vkEnumeratePhysicalDevices", R);
  if (DeviceCount == 0) {
    std::fprintf(stderr, "FAIL: no Vulkan devices reported\n");
    return 1;
  }

  uint32_t FamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &FamilyCount,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> Families(FamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &FamilyCount,
                                           Families.data());
  uint32_t ComputeFamily = FamilyCount;
  for (uint32_t I = 0; I < FamilyCount; ++I)
    if (Families[I].queueFlags & VK_QUEUE_COMPUTE_BIT) {
      ComputeFamily = I;
      break;
    }
  if (ComputeFamily == FamilyCount) {
    std::fprintf(stderr, "FAIL: no compute queue family reported\n");
    return 1;
  }

  float Priority = 1.0f;
  VkDeviceQueueCreateInfo QueueInfo{};
  QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  QueueInfo.queueFamilyIndex = ComputeFamily;
  QueueInfo.queueCount = 1;
  QueueInfo.pQueuePriorities = &Priority;

  VkDeviceCreateInfo DeviceInfo{};
  DeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  DeviceInfo.queueCreateInfoCount = 1;
  DeviceInfo.pQueueCreateInfos = &QueueInfo;

  VkDevice Device;
  if (VkResult R =
          vkCreateDevice(PhysicalDevice, &DeviceInfo, nullptr, &Device))
    fail("vkCreateDevice", R);

  VkQueue Queue;
  vkGetDeviceQueue(Device, ComputeFamily, 0, &Queue);

  constexpr uint32_t Width = 2, Height = 2, TexelSize = 4;
  constexpr VkDeviceSize ImageSize = Width * Height * TexelSize;

  auto createImage2D = [&](VkFormat Format, VkImageUsageFlags Usage,
                           VkImage &Img, VkDeviceMemory &Memory) {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = Format;
    ImageInfo.extent = {Width, Height, 1};
    ImageInfo.mipLevels = 1;
    ImageInfo.arrayLayers = 1;
    ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ImageInfo.usage = Usage;
    if (VkResult R = vkCreateImage(Device, &ImageInfo, nullptr, &Img))
      fail("vkCreateImage", R);
    VkMemoryRequirements Reqs;
    vkGetImageMemoryRequirements(Device, Img, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocInfo.allocationSize = Reqs.size;
    AllocInfo.memoryTypeIndex = 0;
    if (VkResult R = vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory))
      fail("vkAllocateMemory", R);
    if (VkResult R = vkBindImageMemory(Device, Img, Memory, 0))
      fail("vkBindImageMemory", R);
  };

  VkImage SrcImage, DstImage;
  VkDeviceMemory SrcImageMemory, DstImageMemory;
  createImage2D(VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                SrcImage, SrcImageMemory);
  // A different, texel-size-compatible format from `SrcImage` (see this
  // file's own comment): exercises the "compatible formats" `vkCmdCopyImage`
  // rule end to end through the real loader, not just this ICD's unit
  // tests.
  createImage2D(VK_FORMAT_R8G8B8A8_UINT,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                DstImage, DstImageMemory);

  VkImageViewCreateInfo ViewInfo{};
  ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ViewInfo.image = SrcImage;
  ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView View;
  if (VkResult R = vkCreateImageView(Device, &ViewInfo, nullptr, &View))
    fail("vkCreateImageView", R);

  VkSamplerCreateInfo SamplerInfo{};
  SamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  SamplerInfo.magFilter = VK_FILTER_LINEAR;
  SamplerInfo.minFilter = VK_FILTER_LINEAR;
  VkSampler Sampler;
  if (VkResult R = vkCreateSampler(Device, &SamplerInfo, nullptr, &Sampler))
    fail("vkCreateSampler", R);

  auto createHostBuffer = [&](VkBufferUsageFlags Usage, VkBuffer &Buf,
                              VkDeviceMemory &Memory, void *&Mapped) {
    VkBufferCreateInfo BufferInfo{};
    BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufferInfo.size = ImageSize;
    BufferInfo.usage = Usage;
    if (VkResult R = vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf))
      fail("vkCreateBuffer", R);
    VkMemoryRequirements Reqs;
    vkGetBufferMemoryRequirements(Device, Buf, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocInfo.allocationSize = Reqs.size;
    AllocInfo.memoryTypeIndex = 0;
    if (VkResult R = vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory))
      fail("vkAllocateMemory", R);
    if (VkResult R = vkBindBufferMemory(Device, Buf, Memory, 0))
      fail("vkBindBufferMemory", R);
    if (VkResult R = vkMapMemory(Device, Memory, 0, VK_WHOLE_SIZE, 0, &Mapped))
      fail("vkMapMemory", R);
  };

  VkBuffer SrcBuffer, DstBuffer;
  VkDeviceMemory SrcBufferMemory, DstBufferMemory;
  void *SrcData, *DstData;
  createHostBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, SrcBuffer, SrcBufferMemory,
                   SrcData);
  createHostBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, DstBuffer, DstBufferMemory,
                   DstData);

  std::vector<uint8_t> Pattern(ImageSize);
  for (uint32_t I = 0; I != ImageSize; ++I)
    Pattern[I] = static_cast<uint8_t>(I + 1);
  std::memcpy(SrcData, Pattern.data(), ImageSize);
  std::memset(DstData, 0, ImageSize);

  VkCommandPoolCreateInfo CmdPoolInfo{};
  CmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  CmdPoolInfo.queueFamilyIndex = ComputeFamily;
  VkCommandPool CmdPool;
  if (VkResult R = vkCreateCommandPool(Device, &CmdPoolInfo, nullptr, &CmdPool))
    fail("vkCreateCommandPool", R);

  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  CmdAllocInfo.commandPool = CmdPool;
  CmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer CmdBuf;
  if (VkResult R = vkAllocateCommandBuffers(Device, &CmdAllocInfo, &CmdBuf))
    fail("vkAllocateCommandBuffers", R);

  VkCommandBufferBeginInfo BeginInfo{};
  BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (VkResult R = vkBeginCommandBuffer(CmdBuf, &BeginInfo))
    fail("vkBeginCommandBuffer", R);

  VkBufferImageCopy Region{};
  Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.imageExtent = {Width, Height, 1};
  vkCmdCopyBufferToImage(CmdBuf, SrcBuffer, SrcImage,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

  VkImageCopy ImageCopyRegion{};
  ImageCopyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  ImageCopyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  ImageCopyRegion.extent = {Width, Height, 1};
  vkCmdCopyImage(CmdBuf, SrcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 DstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                 &ImageCopyRegion);

  vkCmdCopyImageToBuffer(CmdBuf, DstImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         DstBuffer, 1, &Region);
  if (VkResult R = vkEndCommandBuffer(CmdBuf))
    fail("vkEndCommandBuffer", R);

  VkSubmitInfo Submit{};
  Submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &CmdBuf;
  if (VkResult R = vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE))
    fail("vkQueueSubmit", R);
  if (VkResult R = vkQueueWaitIdle(Queue))
    fail("vkQueueWaitIdle", R);

  const auto *Result = static_cast<const uint8_t *>(DstData);
  for (uint32_t I = 0; I != ImageSize; ++I)
    std::printf("%02x\n", Result[I]);

  vkDestroyCommandPool(Device, CmdPool, nullptr);
  vkDestroySampler(Device, Sampler, nullptr);
  vkDestroyImageView(Device, View, nullptr);
  vkDestroyImage(Device, SrcImage, nullptr);
  vkDestroyImage(Device, DstImage, nullptr);
  vkDestroyBuffer(Device, SrcBuffer, nullptr);
  vkDestroyBuffer(Device, DstBuffer, nullptr);
  vkFreeMemory(Device, SrcImageMemory, nullptr);
  vkFreeMemory(Device, DstImageMemory, nullptr);
  vkFreeMemory(Device, SrcBufferMemory, nullptr);
  vkFreeMemory(Device, DstBufferMemory, nullptr);
  vkDestroyDevice(Device, nullptr);
  vkDestroyInstance(Instance, nullptr);
  return 0;
}
