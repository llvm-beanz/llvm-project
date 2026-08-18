//===- feme-vulkan-graphics-smoke.cpp - Off-screen render client ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A tiny Vulkan client, linked against the real Khronos Vulkan loader (not
// `libfeme_vulkan` directly), that renders off-screen through a
// `VkRenderPass` -- V6's own completion scenario (see "V6: Graphics queue
// and basic rendering" in feme/docs/FeMeVulkanDesign.md).
//
// Selects a queue family advertising `VK_QUEUE_GRAPHICS_BIT` (the bit V6
// adds), creates a 4x4 `R8G8B8A8_UNORM` color attachment cleared to opaque
// black, draws one oversized triangle with the vertex/fragment SPIR-V
// modules named on the command line, and prints every resulting texel as
// eight hexadecimal digits, one per line.
//
// This exists for the same reason feme-vulkan-image-loader-smoke does: the
// unit tests link `libfeme_vulkan` directly, so only a real-loader client
// catches entry-point resolution and struct-ABI problems at the process
// boundary -- and a graphics pipeline's create-info chain is by far the
// largest such structure this driver accepts.
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

constexpr uint32_t Extent = 4;

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

VkShaderModule createModule(VkDevice Device, const char *Path) {
  std::vector<uint32_t> Words = readSPIRVFile(Path);
  VkShaderModuleCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  Info.codeSize = Words.size() * sizeof(uint32_t);
  Info.pCode = Words.data();
  VkShaderModule Module;
  if (VkResult R = vkCreateShaderModule(Device, &Info, nullptr, &Module))
    fail("vkCreateShaderModule", R);
  return Module;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <vertex.spv> <fragment.spv>\n", argv[0]);
    return 1;
  }

  VkApplicationInfo AppInfo{};
  AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  AppInfo.pApplicationName = "feme-vulkan-graphics-smoke";
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
  uint32_t GraphicsFamily = FamilyCount;
  for (uint32_t I = 0; I < FamilyCount; ++I)
    if (Families[I].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      GraphicsFamily = I;
      break;
    }
  if (GraphicsFamily == FamilyCount) {
    std::fprintf(stderr, "FAIL: no graphics queue family reported\n");
    return 1;
  }

  float Priority = 1.0f;
  VkDeviceQueueCreateInfo QueueInfo{};
  QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  QueueInfo.queueFamilyIndex = GraphicsFamily;
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
  vkGetDeviceQueue(Device, GraphicsFamily, 0, &Queue);

  // The attachment is read back directly through mapped memory: this ICD
  // does not distinguish linear from optimal tiling, so the host-visible
  // bytes are exactly the packed subresource layout it rendered into.
  VkImageCreateInfo ImageInfo{};
  ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {Extent, Extent, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.tiling = VK_IMAGE_TILING_LINEAR;
  ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  VkImage ColorImage;
  if (VkResult R = vkCreateImage(Device, &ImageInfo, nullptr, &ColorImage))
    fail("vkCreateImage", R);

  VkMemoryRequirements ImageReqs;
  vkGetImageMemoryRequirements(Device, ColorImage, &ImageReqs);
  VkMemoryAllocateInfo ImageAlloc{};
  ImageAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ImageAlloc.allocationSize = ImageReqs.size;
  ImageAlloc.memoryTypeIndex = 0;
  VkDeviceMemory ImageMemory;
  if (VkResult R = vkAllocateMemory(Device, &ImageAlloc, nullptr, &ImageMemory))
    fail("vkAllocateMemory (image)", R);
  if (VkResult R = vkBindImageMemory(Device, ColorImage, ImageMemory, 0))
    fail("vkBindImageMemory", R);

  VkImageViewCreateInfo ViewInfo{};
  ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ViewInfo.image = ColorImage;
  ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ViewInfo.subresourceRange.levelCount = 1;
  ViewInfo.subresourceRange.layerCount = 1;
  VkImageView ColorView;
  if (VkResult R = vkCreateImageView(Device, &ViewInfo, nullptr, &ColorView))
    fail("vkCreateImageView", R);

  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  Attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  Attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  Attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  PassInfo.attachmentCount = 1;
  PassInfo.pAttachments = &Attachment;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass RenderPass;
  if (VkResult R = vkCreateRenderPass(Device, &PassInfo, nullptr, &RenderPass))
    fail("vkCreateRenderPass", R);

  VkFramebufferCreateInfo FbInfo{};
  FbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  FbInfo.renderPass = RenderPass;
  FbInfo.attachmentCount = 1;
  FbInfo.pAttachments = &ColorView;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer Framebuffer;
  if (VkResult R = vkCreateFramebuffer(Device, &FbInfo, nullptr, &Framebuffer))
    fail("vkCreateFramebuffer", R);

  VkPipelineLayoutCreateInfo LayoutInfo{};
  LayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  VkPipelineLayout Layout;
  if (VkResult R =
          vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout))
    fail("vkCreatePipelineLayout", R);

  VkShaderModule Vertex = createModule(Device, argv[1]);
  VkShaderModule Fragment = createModule(Device, argv[2]);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo PipelineInfo{};
  PipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  PipelineInfo.stageCount = 2;
  PipelineInfo.pStages = Stages;
  PipelineInfo.pVertexInputState = &VertexInput;
  PipelineInfo.pInputAssemblyState = &InputAssembly;
  PipelineInfo.pViewportState = &ViewportState;
  PipelineInfo.pRasterizationState = &Raster;
  PipelineInfo.pMultisampleState = &Multisample;
  PipelineInfo.pColorBlendState = &Blend;
  PipelineInfo.layout = Layout;
  PipelineInfo.renderPass = RenderPass;
  VkPipeline Pipeline;
  if (VkResult R = vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1,
                                             &PipelineInfo, nullptr, &Pipeline))
    fail("vkCreateGraphicsPipelines", R);

  VkCommandPoolCreateInfo PoolInfo{};
  PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  PoolInfo.queueFamilyIndex = GraphicsFamily;
  VkCommandPool Pool;
  if (VkResult R = vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool))
    fail("vkCreateCommandPool", R);
  VkCommandBufferAllocateInfo CmdAlloc{};
  CmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  CmdAlloc.commandPool = Pool;
  CmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  CmdAlloc.commandBufferCount = 1;
  VkCommandBuffer Cmd;
  if (VkResult R = vkAllocateCommandBuffers(Device, &CmdAlloc, &Cmd))
    fail("vkAllocateCommandBuffers", R);

  VkCommandBufferBeginInfo BeginInfo{};
  BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (VkResult R = vkBeginCommandBuffer(Cmd, &BeginInfo))
    fail("vkBeginCommandBuffer", R);

  VkClearValue Clear{};
  Clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  PassBegin.renderPass = RenderPass;
  PassBegin.framebuffer = Framebuffer;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 1;
  PassBegin.pClearValues = &Clear;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  if (VkResult R = vkEndCommandBuffer(Cmd))
    fail("vkEndCommandBuffer", R);

  VkSubmitInfo Submit{};
  Submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &Cmd;
  if (VkResult R = vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE))
    fail("vkQueueSubmit", R);
  if (VkResult R = vkQueueWaitIdle(Queue))
    fail("vkQueueWaitIdle", R);

  void *Texels = nullptr;
  if (VkResult R =
          vkMapMemory(Device, ImageMemory, 0, VK_WHOLE_SIZE, 0, &Texels))
    fail("vkMapMemory (image)", R);
  const auto *Bytes = static_cast<const uint8_t *>(Texels);
  for (uint32_t I = 0; I != Extent * Extent; ++I)
    std::printf("%02x%02x%02x%02x\n", Bytes[I * 4 + 0], Bytes[I * 4 + 1],
                Bytes[I * 4 + 2], Bytes[I * 4 + 3]);
  vkUnmapMemory(Device, ImageMemory);

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyCommandPool(Device, Pool, nullptr);
  vkDestroyPipelineLayout(Device, Layout, nullptr);
  vkDestroyFramebuffer(Device, Framebuffer, nullptr);
  vkDestroyRenderPass(Device, RenderPass, nullptr);
  vkDestroyImageView(Device, ColorView, nullptr);
  vkDestroyImage(Device, ColorImage, nullptr);
  vkFreeMemory(Device, ImageMemory, nullptr);
  vkDestroyDevice(Device, nullptr);
  vkDestroyInstance(Instance, nullptr);
  return 0;
}
