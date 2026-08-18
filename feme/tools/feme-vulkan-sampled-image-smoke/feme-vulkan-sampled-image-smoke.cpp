//===- feme-vulkan-sampled-image-smoke.cpp - Image sampling client -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A tiny Vulkan client, linked against the real Khronos Vulkan loader (not
// `libfeme_vulkan` directly), that *samples* a bound image from a compute
// dispatch -- the scenario V5 could only set up and copy into, and which
// roadmap R30's SPIR-V image lowering completes (see "V5: Images and
// sampling" in feme/docs/FeMeVulkanDesign.md).
//
// Creates a 2x2 `R32G32B32A32_SFLOAT` image whose texel `i`'s channels hold
// `4i .. 4i+3`, a nearest/clamp-to-edge sampler, and a four-float storage
// buffer; runs the compute shader loaded from the `.spv` file named on the
// command line (produced ahead of time by `feme-translate
// --serialize-spirv`, so the same SPIR-V bytes would run against any ICD);
// and prints the four sampled components, one per line.
//
// This exists for the same reason feme-vulkan-image-loader-smoke does: the
// unit tests link `libfeme_vulkan` directly, so only a real-loader client
// catches entry-point resolution and struct-ABI problems at the process
// boundary.
//
//===----------------------------------------------------------------------===//

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

constexpr uint32_t Extent = 2;
constexpr VkDeviceSize OutputSize = 4 * sizeof(float);

[[noreturn]] void fail(const char *Step, VkResult Result) {
  std::fprintf(stderr, "FAIL: %s (VkResult = %d)\n", Step,
               static_cast<int>(Result));
  std::exit(1);
}

std::vector<uint32_t> readSPIRVFile(const char *Path) {
  std::ifstream File(Path, std::ios::binary | std::ios::ate);
  if (!File) {
    std::fprintf(stderr, "FAIL: could not open '%s'\n", Path);
    std::exit(1);
  }
  std::streamsize Size = File.tellg();
  File.seekg(0, std::ios::beg);
  std::vector<uint32_t> Words(static_cast<size_t>(Size) / sizeof(uint32_t));
  if (!File.read(reinterpret_cast<char *>(Words.data()), Size)) {
    std::fprintf(stderr, "FAIL: could not read '%s'\n", Path);
    std::exit(1);
  }
  return Words;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <shader.spv>\n", argv[0]);
    return 1;
  }
  std::vector<uint32_t> SPIRVWords = readSPIRVFile(argv[1]);

  VkApplicationInfo AppInfo{};
  AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  AppInfo.pApplicationName = "feme-vulkan-sampled-image-smoke";
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

  // The image is filled directly through mapped memory: this ICD does not
  // distinguish linear from optimal tiling, so the host-visible bytes are
  // exactly the packed subresource layout the shader samples.
  VkImageCreateInfo ImageInfo{};
  ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  ImageInfo.extent = {Extent, Extent, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.tiling = VK_IMAGE_TILING_LINEAR;
  ImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  VkImage Image;
  if (VkResult R = vkCreateImage(Device, &ImageInfo, nullptr, &Image))
    fail("vkCreateImage", R);

  VkMemoryRequirements ImageReqs;
  vkGetImageMemoryRequirements(Device, Image, &ImageReqs);
  VkMemoryAllocateInfo ImageAlloc{};
  ImageAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ImageAlloc.allocationSize = ImageReqs.size;
  ImageAlloc.memoryTypeIndex = 0;
  VkDeviceMemory ImageMemory;
  if (VkResult R = vkAllocateMemory(Device, &ImageAlloc, nullptr, &ImageMemory))
    fail("vkAllocateMemory (image)", R);
  if (VkResult R = vkBindImageMemory(Device, Image, ImageMemory, 0))
    fail("vkBindImageMemory", R);
  void *Texels = nullptr;
  if (VkResult R =
          vkMapMemory(Device, ImageMemory, 0, VK_WHOLE_SIZE, 0, &Texels))
    fail("vkMapMemory (image)", R);
  auto *TexelFloats = static_cast<float *>(Texels);
  for (uint32_t I = 0; I != Extent * Extent; ++I)
    for (uint32_t C = 0; C != 4; ++C)
      TexelFloats[I * 4 + C] = static_cast<float>(I * 4 + C);

  VkImageViewCreateInfo ViewInfo{};
  ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ViewInfo.image = Image;
  ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ViewInfo.subresourceRange.levelCount = 1;
  ViewInfo.subresourceRange.layerCount = 1;
  VkImageView View;
  if (VkResult R = vkCreateImageView(Device, &ViewInfo, nullptr, &View))
    fail("vkCreateImageView", R);

  VkSamplerCreateInfo SamplerInfo{};
  SamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  SamplerInfo.magFilter = VK_FILTER_NEAREST;
  SamplerInfo.minFilter = VK_FILTER_NEAREST;
  SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  VkSampler Sampler;
  if (VkResult R = vkCreateSampler(Device, &SamplerInfo, nullptr, &Sampler))
    fail("vkCreateSampler", R);

  VkBufferCreateInfo BufferInfo{};
  BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  BufferInfo.size = OutputSize;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  VkBuffer OutBuffer;
  if (VkResult R = vkCreateBuffer(Device, &BufferInfo, nullptr, &OutBuffer))
    fail("vkCreateBuffer", R);
  VkMemoryRequirements BufferReqs;
  vkGetBufferMemoryRequirements(Device, OutBuffer, &BufferReqs);
  VkMemoryAllocateInfo BufferAlloc{};
  BufferAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  BufferAlloc.allocationSize = BufferReqs.size;
  BufferAlloc.memoryTypeIndex = 0;
  VkDeviceMemory OutMemory;
  if (VkResult R = vkAllocateMemory(Device, &BufferAlloc, nullptr, &OutMemory))
    fail("vkAllocateMemory (buffer)", R);
  if (VkResult R = vkBindBufferMemory(Device, OutBuffer, OutMemory, 0))
    fail("vkBindBufferMemory", R);
  void *OutData = nullptr;
  if (VkResult R =
          vkMapMemory(Device, OutMemory, 0, VK_WHOLE_SIZE, 0, &OutData))
    fail("vkMapMemory (buffer)", R);
  std::memset(OutData, 0, OutputSize);

  VkDescriptorSetLayoutBinding Bindings[3]{};
  Bindings[0].binding = 0;
  Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  Bindings[0].descriptorCount = 1;
  Bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Bindings[1].binding = 1;
  Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  Bindings[1].descriptorCount = 1;
  Bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Bindings[2].binding = 2;
  Bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Bindings[2].descriptorCount = 1;
  Bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  SetLayoutInfo.bindingCount = 3;
  SetLayoutInfo.pBindings = Bindings;
  VkDescriptorSetLayout SetLayout;
  if (VkResult R = vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr,
                                               &SetLayout))
    fail("vkCreateDescriptorSetLayout", R);

  VkPipelineLayoutCreateInfo LayoutInfo{};
  LayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  LayoutInfo.setLayoutCount = 1;
  LayoutInfo.pSetLayouts = &SetLayout;
  VkPipelineLayout Layout;
  if (VkResult R =
          vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout))
    fail("vkCreatePipelineLayout", R);

  VkShaderModuleCreateInfo ShaderInfo{};
  ShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  ShaderInfo.codeSize = SPIRVWords.size() * sizeof(uint32_t);
  ShaderInfo.pCode = SPIRVWords.data();
  VkShaderModule Module;
  if (VkResult R = vkCreateShaderModule(Device, &ShaderInfo, nullptr, &Module))
    fail("vkCreateShaderModule", R);

  VkComputePipelineCreateInfo PipelineInfo{};
  PipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  PipelineInfo.stage.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  PipelineInfo.stage.module = Module;
  PipelineInfo.stage.pName = "main";
  PipelineInfo.layout = Layout;
  VkPipeline Pipeline;
  if (VkResult R = vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1,
                                            &PipelineInfo, nullptr, &Pipeline))
    fail("vkCreateComputePipelines", R);

  VkDescriptorPoolSize PoolSizes[3] = {
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
  };
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 3;
  PoolInfo.pPoolSizes = PoolSizes;
  VkDescriptorPool DescPool;
  if (VkResult R =
          vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &DescPool))
    fail("vkCreateDescriptorPool", R);

  VkDescriptorSetAllocateInfo DSAllocInfo{};
  DSAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  DSAllocInfo.descriptorPool = DescPool;
  DSAllocInfo.descriptorSetCount = 1;
  DSAllocInfo.pSetLayouts = &SetLayout;
  VkDescriptorSet Set;
  if (VkResult R = vkAllocateDescriptorSets(Device, &DSAllocInfo, &Set))
    fail("vkAllocateDescriptorSets", R);

  VkDescriptorImageInfo ImgInfo{};
  ImgInfo.imageView = View;
  ImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkDescriptorImageInfo SampInfo{};
  SampInfo.sampler = Sampler;
  VkDescriptorBufferInfo OutInfo{OutBuffer, 0, VK_WHOLE_SIZE};
  VkWriteDescriptorSet Writes[3]{};
  Writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  Writes[0].pImageInfo = &ImgInfo;
  Writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  Writes[1].pImageInfo = &SampInfo;
  Writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Writes[2].dstSet = Set;
  Writes[2].dstBinding = 2;
  Writes[2].descriptorCount = 1;
  Writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[2].pBufferInfo = &OutInfo;
  vkUpdateDescriptorSets(Device, 3, Writes, 0, nullptr);

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
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
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

  float Result[4] = {};
  std::memcpy(Result, OutData, sizeof(Result));
  for (float Component : Result)
    std::printf("%.1f\n", Component);

  vkDestroyCommandPool(Device, CmdPool, nullptr);
  vkDestroyDescriptorPool(Device, DescPool, nullptr);
  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
  vkDestroyPipelineLayout(Device, Layout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
  vkDestroyBuffer(Device, OutBuffer, nullptr);
  vkFreeMemory(Device, OutMemory, nullptr);
  vkDestroySampler(Device, Sampler, nullptr);
  vkDestroyImageView(Device, View, nullptr);
  vkDestroyImage(Device, Image, nullptr);
  vkFreeMemory(Device, ImageMemory, nullptr);
  vkDestroyDevice(Device, nullptr);
  vkDestroyInstance(Instance, nullptr);
  return 0;
}
