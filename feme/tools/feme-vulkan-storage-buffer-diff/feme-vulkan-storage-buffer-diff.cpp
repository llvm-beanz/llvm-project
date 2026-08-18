//===- feme-vulkan-storage-buffer-diff.cpp - Storage buffer diff client -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A tiny Vulkan client, linked against the real Khronos Vulkan loader (not
// `libfeme_vulkan` directly), for V2's "Differentially compare results with
// lavapipe for the supported subset" milestone bullet (see
// feme/docs/FeMeVulkanDesign.md's "V2: Storage buffers and descriptors").
//
// Loads a compute shader from a `.spv` file (produced ahead of time by
// `feme-translate --serialize-spirv`, so the SPIR-V bytes are identical for
// every ICD this runs against), binds two storage buffers through a real
// `VkDescriptorSet`, dispatches it, and prints the output buffer's `int32`
// elements to stdout, one per line. The lit test that drives this
// (feme/test/Vulkan/storage-buffer-lavapipe-diff.test) runs the same binary
// twice with `VK_DRIVER_FILES` restricted to a single ICD manifest each
// time -- once FeMe's, once Mesa lavapipe's -- and diffs the two outputs:
// whichever device the loader reports (there is only ever one, since each
// invocation's manifest names exactly one ICD) is used, with no FeMe-vendor-
// ID filtering the way feme-vulkan-loader-smoke's two-ICD coexistence test
// needs (that test wants *both* devices visible at once; this one wants
// exactly one at a time).
//
// The shader itself reads `in[gl_GlobalInvocationID.x]`, adds one, and
// writes the result to `out[gl_GlobalInvocationID.x]` -- two flat
// (non-aggregate) `i32` `StorageBuffer` bindings in one descriptor set, the
// same shape feme/unittests/Vulkan/CommandBufferTest.cpp's
// StorageBufferDispatchTest exercises directly against feme::vulkan's own
// entry points.
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
  if (argc != 3) {
    std::fprintf(stderr,
                 "usage: %s <shader.spv> <element-count>\n", argv[0]);
    return 1;
  }
  std::vector<uint32_t> SPIRVWords = readSPIRVFile(argv[1]);
  uint32_t ElementCount = static_cast<uint32_t>(std::atoi(argv[2]));

  VkApplicationInfo AppInfo{};
  AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  AppInfo.pApplicationName = "feme-vulkan-storage-buffer-diff";
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
  if (VkResult R = vkCreateDevice(PhysicalDevice, &DeviceInfo, nullptr, &Device))
    fail("vkCreateDevice", R);

  VkQueue Queue;
  vkGetDeviceQueue(Device, ComputeFamily, 0, &Queue);

  VkDescriptorSetLayoutBinding Bindings[2]{};
  Bindings[0].binding = 0;
  Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Bindings[0].descriptorCount = 1;
  Bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Bindings[1].binding = 1;
  Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Bindings[1].descriptorCount = 1;
  Bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  SetLayoutInfo.bindingCount = 2;
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
  if (VkResult R = vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout))
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
  PipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  PipelineInfo.stage.module = Module;
  PipelineInfo.stage.pName = "main";
  PipelineInfo.layout = Layout;
  VkPipeline Pipeline;
  if (VkResult R = vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1,
                                            &PipelineInfo, nullptr, &Pipeline))
    fail("vkCreateComputePipelines", R);

  VkDeviceSize BufferSize = ElementCount * sizeof(uint32_t);
  auto createStorageBuffer = [&](VkBuffer &Buf, VkDeviceMemory &Memory,
                                 void *&Mapped) {
    VkBufferCreateInfo BufferInfo{};
    BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufferInfo.size = BufferSize;
    BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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

  VkBuffer InBuffer, OutBuffer;
  VkDeviceMemory InMemory, OutMemory;
  void *InData, *OutData;
  createStorageBuffer(InBuffer, InMemory, InData);
  createStorageBuffer(OutBuffer, OutMemory, OutData);

  std::vector<uint32_t> Initial(ElementCount);
  for (uint32_t I = 0; I != ElementCount; ++I)
    Initial[I] = I;
  std::memcpy(InData, Initial.data(), BufferSize);
  std::memset(OutData, 0, BufferSize);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 1;
  PoolInfo.pPoolSizes = &PoolSize;
  VkDescriptorPool DescPool;
  if (VkResult R = vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &DescPool))
    fail("vkCreateDescriptorPool", R);

  VkDescriptorSetAllocateInfo DSAllocInfo{};
  DSAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  DSAllocInfo.descriptorPool = DescPool;
  DSAllocInfo.descriptorSetCount = 1;
  DSAllocInfo.pSetLayouts = &SetLayout;
  VkDescriptorSet Set;
  if (VkResult R = vkAllocateDescriptorSets(Device, &DSAllocInfo, &Set))
    fail("vkAllocateDescriptorSets", R);

  VkDescriptorBufferInfo InInfo{InBuffer, 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo OutInfo{OutBuffer, 0, VK_WHOLE_SIZE};
  VkWriteDescriptorSet Writes[2]{};
  Writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[0].pBufferInfo = &InInfo;
  Writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[1].pBufferInfo = &OutInfo;
  vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

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
  vkCmdDispatch(CmdBuf, ElementCount, 1, 1);
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

  std::vector<uint32_t> Result(ElementCount);
  std::memcpy(Result.data(), OutData, BufferSize);
  for (uint32_t Value : Result)
    std::printf("%u\n", Value);

  vkDestroyCommandPool(Device, CmdPool, nullptr);
  vkDestroyDescriptorPool(Device, DescPool, nullptr);
  vkDestroyBuffer(Device, InBuffer, nullptr);
  vkDestroyBuffer(Device, OutBuffer, nullptr);
  vkFreeMemory(Device, InMemory, nullptr);
  vkFreeMemory(Device, OutMemory, nullptr);
  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
  vkDestroyPipelineLayout(Device, Layout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
  vkDestroyDevice(Device, nullptr);
  vkDestroyInstance(Instance, nullptr);
  return 0;
}
