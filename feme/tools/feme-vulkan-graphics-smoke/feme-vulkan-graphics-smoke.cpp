//===- feme-vulkan-graphics-smoke.cpp - Off-screen render client ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A tiny Vulkan client, linked against the real Khronos Vulkan loader (not
// `libfeme_vulkan` directly), that renders off-screen -- V6's own
// completion scenario (see "V6: Graphics queue and basic rendering" in
// feme/docs/FeMeVulkanDesign.md) and, since V6's own status note records no
// off-screen differential against lavapipe ever ran, the client
// feme/test/Vulkan/graphics-lavapipe-diff.test drives twice (once per ICD)
// to close exactly that gap for every scenario below.
//
// Selects a queue family advertising `VK_QUEUE_GRAPHICS_BIT` (the bit V6
// adds) and renders one of several fixed scenarios, chosen by `argv[1]`,
// each covering one bullet of the completion test ("render off-screen
// through a VkRenderPass and through dynamic rendering, with depth,
// stencil, blending, MRT, and multisample resolves"):
//
//   render-pass       <vs> <fs>                  -- VkRenderPass, one draw.
//   dynamic-rendering <vs> <fs>                  -- vkCmdBeginRenderingKHR.
//   depth             <near-vs> <red-fs> <far-vs> <green-fs>
//   stencil           <vs> <red-fs> <green-fs>
//   blend             <vs> <half-alpha-red-fs>
//   mrt               <vs> <dual-output-fs>
//   msaa-resolve      <vs> <red-fs>
//
// Every scenario prints its color target(s) as one line per texel, eight
// hexadecimal digits each (RGBA8), attachments separated by a line of
// dashes when there is more than one. This exists for the same reason
// feme-vulkan-image-loader-smoke does: the unit tests link `libfeme_vulkan`
// directly, so only a real-loader client catches entry-point resolution and
// struct-ABI problems at the process boundary -- and a graphics pipeline's
// create-info chain is by far the largest such structure this driver
// accepts.
//
//===----------------------------------------------------------------------===//

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
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

/// The state every scenario shares: an instance, a graphics-capable device
/// and queue, and the helpers built on top of them.
class GraphicsSmoke {
public:
  GraphicsSmoke() {
    VkApplicationInfo AppInfo{};
    AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppInfo.pApplicationName = "feme-vulkan-graphics-smoke";
    AppInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo InstanceInfo{};
    InstanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstanceInfo.pApplicationInfo = &AppInfo;
    if (VkResult R = vkCreateInstance(&InstanceInfo, nullptr, &Instance))
      fail("vkCreateInstance", R);

    uint32_t DeviceCount = 1;
    if (VkResult R =
            vkEnumeratePhysicalDevices(Instance, &DeviceCount, &PhysicalDevice))
      if (R != VK_INCOMPLETE)
        fail("vkEnumeratePhysicalDevices", R);
    if (DeviceCount == 0) {
      std::fprintf(stderr, "FAIL: no Vulkan devices reported\n");
      std::exit(1);
    }

    uint32_t FamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &FamilyCount,
                                             nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &FamilyCount,
                                             Families.data());
    GraphicsFamily = FamilyCount;
    for (uint32_t I = 0; I < FamilyCount; ++I)
      if (Families[I].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        GraphicsFamily = I;
        break;
      }
    if (GraphicsFamily == FamilyCount) {
      std::fprintf(stderr, "FAIL: no graphics queue family reported\n");
      std::exit(1);
    }

    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);

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

    // The `dynamic-rendering` scenario needs `vkCmdBeginRenderingKHR`
    // resolvable and usable, which (on a real driver, unlike this ICD's own
    // lenient acceptance) requires both enabling the extension by name and
    // enabling its `VkPhysicalDeviceDynamicRenderingFeatures::
    // dynamicRendering` feature at device creation -- so this is done
    // unconditionally, harmlessly, whenever the extension is advertised.
    const char *DynamicRenderingExtension =
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
    VkPhysicalDeviceDynamicRenderingFeatures DynamicRenderingFeatures{};
    DynamicRenderingFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    DynamicRenderingFeatures.dynamicRendering = VK_TRUE;
    if (hasDeviceExtension(DynamicRenderingExtension)) {
      DeviceInfo.enabledExtensionCount = 1;
      DeviceInfo.ppEnabledExtensionNames = &DynamicRenderingExtension;
      DeviceInfo.pNext = &DynamicRenderingFeatures;
    }
    if (VkResult R =
            vkCreateDevice(PhysicalDevice, &DeviceInfo, nullptr, &Device))
      fail("vkCreateDevice", R);
    vkGetDeviceQueue(Device, GraphicsFamily, 0, &Queue);

    VkPipelineLayoutCreateInfo LayoutInfo{};
    LayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (VkResult R =
            vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout))
      fail("vkCreatePipelineLayout", R);

    VkCommandPoolCreateInfo PoolInfo{};
    PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    PoolInfo.queueFamilyIndex = GraphicsFamily;
    if (VkResult R = vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool))
      fail("vkCreateCommandPool", R);
    VkCommandBufferAllocateInfo CmdAlloc{};
    CmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    CmdAlloc.commandPool = Pool;
    CmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CmdAlloc.commandBufferCount = 1;
    if (VkResult R = vkAllocateCommandBuffers(Device, &CmdAlloc, &Cmd))
      fail("vkAllocateCommandBuffers", R);
  }

  ~GraphicsSmoke() {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  /// Whether the physical device advertises \p Extension, used to enable
  /// `VK_KHR_dynamic_rendering` only when a real driver requires that (this
  /// ICD accepts the dynamic-rendering entry points unconditionally, but a
  /// real driver like lavapipe does not resolve them unless the extension
  /// and its feature are both enabled at device creation).
  bool hasDeviceExtension(const char *Extension) {
    uint32_t Count = 0;
    vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &Count,
                                         nullptr);
    std::vector<VkExtensionProperties> Properties(Count);
    vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &Count,
                                         Properties.data());
    for (const VkExtensionProperties &P : Properties)
      if (std::strcmp(P.extensionName, Extension) == 0)
        return true;
    return false;
  }

  VkShaderModule createModule(const char *Path) {
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

  /// The first memory type both `Reqs.memoryTypeBits` allows and that
  /// carries every bit of \p Properties -- the same search a well-behaved
  /// Vulkan client always does, rather than assuming index 0 is host
  /// visible (true of this ICD, not guaranteed of every ICD this client
  /// might run against).
  uint32_t findMemoryType(const VkMemoryRequirements &Reqs,
                          VkMemoryPropertyFlags Properties) {
    for (uint32_t I = 0; I != MemoryProperties.memoryTypeCount; ++I) {
      bool Allowed = Reqs.memoryTypeBits & (1u << I);
      bool HasProperties = (MemoryProperties.memoryTypes[I].propertyFlags &
                            Properties) == Properties;
      if (Allowed && HasProperties)
        return I;
    }
    std::fprintf(stderr, "FAIL: no matching memory type\n");
    std::exit(1);
    return 0;
  }

  /// Creates and binds an `Extent`x`Extent` image (and its view) with
  /// \p Format, \p Usage, \p Aspect, and \p Samples, host-visible so its
  /// texels can be read back through mapped memory directly (this ICD does
  /// not distinguish linear from optimal tiling: the host-visible bytes are
  /// exactly the packed subresource layout it rendered into).
  void
  createImageAndView(VkFormat Format, VkImageUsageFlags Usage,
                     VkImageAspectFlags Aspect, VkImage &OutImage,
                     VkImageView &OutView, VkDeviceMemory &OutMemory,
                     VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT) {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = Format;
    ImageInfo.extent = {Extent, Extent, 1};
    ImageInfo.mipLevels = 1;
    ImageInfo.arrayLayers = 1;
    ImageInfo.samples = Samples;
    ImageInfo.tiling = VK_IMAGE_TILING_LINEAR;
    ImageInfo.usage = Usage;
    if (VkResult R = vkCreateImage(Device, &ImageInfo, nullptr, &OutImage))
      fail("vkCreateImage", R);

    VkMemoryRequirements Reqs;
    vkGetImageMemoryRequirements(Device, OutImage, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocInfo.allocationSize = Reqs.size;
    AllocInfo.memoryTypeIndex =
        findMemoryType(Reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (VkResult R = vkAllocateMemory(Device, &AllocInfo, nullptr, &OutMemory))
      fail("vkAllocateMemory (image)", R);
    if (VkResult R = vkBindImageMemory(Device, OutImage, OutMemory, 0))
      fail("vkBindImageMemory", R);

    VkImageViewCreateInfo ViewInfo{};
    ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewInfo.image = OutImage;
    ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format = Format;
    ViewInfo.subresourceRange.aspectMask = Aspect;
    ViewInfo.subresourceRange.levelCount = 1;
    ViewInfo.subresourceRange.layerCount = 1;
    if (VkResult R = vkCreateImageView(Device, &ViewInfo, nullptr, &OutView))
      fail("vkCreateImageView", R);
  }

  VkResult submit() {
    VkSubmitInfo Submit{};
    Submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1;
    Submit.pCommandBuffers = &Cmd;
    if (VkResult R = vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE))
      return R;
    return vkQueueWaitIdle(Queue);
  }

  /// Prints \p Image's texels, one RGBA8 hex value per line, by mapping its
  /// backing memory directly.
  void printTexels(VkDeviceMemory Memory) {
    void *Texels = nullptr;
    if (VkResult R = vkMapMemory(Device, Memory, 0, VK_WHOLE_SIZE, 0, &Texels))
      fail("vkMapMemory (image)", R);
    const auto *Bytes = static_cast<const uint8_t *>(Texels);
    for (uint32_t I = 0; I != Extent * Extent; ++I)
      std::printf("%02x%02x%02x%02x\n", Bytes[I * 4 + 0], Bytes[I * 4 + 1],
                  Bytes[I * 4 + 2], Bytes[I * 4 + 3]);
    vkUnmapMemory(Device, Memory);
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties MemoryProperties{};
  VkDevice Device = VK_NULL_HANDLE;
  VkQueue Queue = VK_NULL_HANDLE;
  uint32_t GraphicsFamily = 0;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
  VkCommandBuffer Cmd = VK_NULL_HANDLE;
};

/// One color attachment's fixed-function state, shared by every scenario's
/// pipeline(s): no blending, no depth/stencil, one sample, full write mask.
/// Callers override what the scenario needs.
VkPipelineColorBlendAttachmentState defaultBlendAttachment() {
  VkPipelineColorBlendAttachmentState State{};
  State.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  return State;
}

/// Builds one graphics pipeline from the state every scenario shares (an
/// oversized triangle, one viewport, no vertex input) plus what the caller
/// overrides through \p Configure.
VkPipeline createPipeline(
    GraphicsSmoke &Smoke, VkShaderModule Vertex, VkShaderModule Fragment,
    VkRenderPass RenderPass,
    const std::function<void(VkGraphicsPipelineCreateInfo &)> &Configure) {
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
  VkPipelineColorBlendAttachmentState BlendAttachment =
      defaultBlendAttachment();
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Smoke.Layout;
  Info.renderPass = RenderPass;
  Configure(Info);

  VkPipeline Pipeline;
  if (VkResult R = vkCreateGraphicsPipelines(Smoke.Device, VK_NULL_HANDLE, 1,
                                             &Info, nullptr, &Pipeline))
    fail("vkCreateGraphicsPipelines", R);
  return Pipeline;
}

/// One color attachment, cleared to opaque black, plus one `vkCmdDraw`
/// covering the whole render area -- V6's own completion scenario, rendered
/// through a `VkRenderPass`.
void runRenderPassScenario(GraphicsSmoke &Smoke, int Argc, char **Argv) {
  if (Argc != 4) {
    std::fprintf(stderr, "usage: %s render-pass <vertex.spv> <fragment.spv>\n",
                 Argv[0]);
    std::exit(1);
  }
  VkImage ColorImage;
  VkImageView ColorView;
  VkDeviceMemory ColorMemory;
  Smoke.createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, ColorImage, ColorView, ColorMemory);

  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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
  if (VkResult R =
          vkCreateRenderPass(Smoke.Device, &PassInfo, nullptr, &RenderPass))
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
  if (VkResult R =
          vkCreateFramebuffer(Smoke.Device, &FbInfo, nullptr, &Framebuffer))
    fail("vkCreateFramebuffer", R);

  VkShaderModule Vertex = Smoke.createModule(Argv[2]);
  VkShaderModule Fragment = Smoke.createModule(Argv[3]);
  VkPipeline Pipeline = createPipeline(Smoke, Vertex, Fragment, RenderPass,
                                       [](VkGraphicsPipelineCreateInfo &) {});

  VkCommandBufferBeginInfo BeginInfo{};
  BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (VkResult R = vkBeginCommandBuffer(Smoke.Cmd, &BeginInfo))
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
  vkCmdBeginRenderPass(Smoke.Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Smoke.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
  vkCmdDraw(Smoke.Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Smoke.Cmd);
  if (VkResult R = vkEndCommandBuffer(Smoke.Cmd))
    fail("vkEndCommandBuffer", R);
  if (VkResult R = Smoke.submit())
    fail("vkQueueSubmit/vkQueueWaitIdle", R);

  Smoke.printTexels(ColorMemory);

  vkDestroyPipeline(Smoke.Device, Pipeline, nullptr);
  vkDestroyShaderModule(Smoke.Device, Fragment, nullptr);
  vkDestroyShaderModule(Smoke.Device, Vertex, nullptr);
  vkDestroyFramebuffer(Smoke.Device, Framebuffer, nullptr);
  vkDestroyRenderPass(Smoke.Device, RenderPass, nullptr);
  vkDestroyImageView(Smoke.Device, ColorView, nullptr);
  vkDestroyImage(Smoke.Device, ColorImage, nullptr);
  vkFreeMemory(Smoke.Device, ColorMemory, nullptr);
}

/// The same scene as `render-pass`, through `vkCmdBeginRenderingKHR` instead
/// of a `VkRenderPass`/`VkFramebuffer` pair.
void runDynamicRenderingScenario(GraphicsSmoke &Smoke, int Argc, char **Argv) {
  if (Argc != 4) {
    std::fprintf(stderr,
                 "usage: %s dynamic-rendering <vertex.spv> <fragment.spv>\n",
                 Argv[0]);
    std::exit(1);
  }
  VkImage ColorImage;
  VkImageView ColorView;
  VkDeviceMemory ColorMemory;
  Smoke.createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, ColorImage, ColorView, ColorMemory);

  VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 1;
  Rendering.pColorAttachmentFormats = &ColorFormat;

  VkShaderModule Vertex = Smoke.createModule(Argv[2]);
  VkShaderModule Fragment = Smoke.createModule(Argv[3]);
  VkPipeline Pipeline = createPipeline(
      Smoke, Vertex, Fragment, VK_NULL_HANDLE,
      [&](VkGraphicsPipelineCreateInfo &Info) { Info.pNext = &Rendering; });

  VkCommandBufferBeginInfo BeginInfo{};
  BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (VkResult R = vkBeginCommandBuffer(Smoke.Cmd, &BeginInfo))
    fail("vkBeginCommandBuffer", R);

  VkRenderingAttachmentInfo ColorAttachment{};
  ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachment.imageView = ColorView;
  ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ColorAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 1;
  RenderingInfo.pColorAttachments = &ColorAttachment;
  // `VK_KHR_dynamic_rendering` is a real extension: the loader's static
  // trampolines do not cover it, so its entry points are resolved through
  // `vkGetDeviceProcAddr` like any other extension function.
  auto BeginRendering = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(
      vkGetDeviceProcAddr(Smoke.Device, "vkCmdBeginRenderingKHR"));
  auto EndRendering = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(
      vkGetDeviceProcAddr(Smoke.Device, "vkCmdEndRenderingKHR"));
  if (!BeginRendering || !EndRendering) {
    std::fprintf(stderr,
                 "FAIL: vkCmdBeginRenderingKHR/vkCmdEndRenderingKHR not "
                 "resolved\n");
    std::exit(1);
  }
  BeginRendering(Smoke.Cmd, &RenderingInfo);
  vkCmdBindPipeline(Smoke.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
  vkCmdDraw(Smoke.Cmd, 3, 1, 0, 0);
  EndRendering(Smoke.Cmd);
  if (VkResult R = vkEndCommandBuffer(Smoke.Cmd))
    fail("vkEndCommandBuffer", R);
  if (VkResult R = Smoke.submit())
    fail("vkQueueSubmit/vkQueueWaitIdle", R);

  Smoke.printTexels(ColorMemory);

  vkDestroyPipeline(Smoke.Device, Pipeline, nullptr);
  vkDestroyShaderModule(Smoke.Device, Fragment, nullptr);
  vkDestroyShaderModule(Smoke.Device, Vertex, nullptr);
  vkDestroyImageView(Smoke.Device, ColorView, nullptr);
  vkDestroyImage(Smoke.Device, ColorImage, nullptr);
  vkFreeMemory(Smoke.Device, ColorMemory, nullptr);
}

/// `DepthState::TestEnable`/`WriteEnable`: a nearer draw's depth write
/// (`CompareOp::Less`) rejects a farther draw covering the same pixels.
void runDepthScenario(GraphicsSmoke &Smoke, int Argc, char **Argv) {
  if (Argc != 6) {
    std::fprintf(stderr,
                 "usage: %s depth <near-vertex.spv> <red-fragment.spv> "
                 "<far-vertex.spv> <green-fragment.spv>\n",
                 Argv[0]);
    std::exit(1);
  }
  VkImage ColorImage;
  VkImageView ColorView;
  VkDeviceMemory ColorMemory;
  Smoke.createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, ColorImage, ColorView, ColorMemory);
  VkImage DepthImage;
  VkImageView DepthView;
  VkDeviceMemory DepthMemory;
  Smoke.createImageAndView(
      VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_DEPTH_BIT, DepthImage, DepthView, DepthMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_D32_SFLOAT;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference DepthRef{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pDepthStencilAttachment = &DepthRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass RenderPass;
  if (VkResult R =
          vkCreateRenderPass(Smoke.Device, &PassInfo, nullptr, &RenderPass))
    fail("vkCreateRenderPass", R);

  VkImageView FbViews[2] = {ColorView, DepthView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  FbInfo.renderPass = RenderPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer Framebuffer;
  if (VkResult R =
          vkCreateFramebuffer(Smoke.Device, &FbInfo, nullptr, &Framebuffer))
    fail("vkCreateFramebuffer", R);

  VkPipelineDepthStencilStateCreateInfo DepthStencil{};
  DepthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  DepthStencil.depthTestEnable = VK_TRUE;
  DepthStencil.depthWriteEnable = VK_TRUE;
  DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  auto configure = [&](VkGraphicsPipelineCreateInfo &Info) {
    Info.pDepthStencilState = &DepthStencil;
  };

  VkShaderModule NearVertex = Smoke.createModule(Argv[2]);
  VkShaderModule RedFragment = Smoke.createModule(Argv[3]);
  VkShaderModule FarVertex = Smoke.createModule(Argv[4]);
  VkShaderModule GreenFragment = Smoke.createModule(Argv[5]);
  VkPipeline NearRed =
      createPipeline(Smoke, NearVertex, RedFragment, RenderPass, configure);
  VkPipeline FarGreen =
      createPipeline(Smoke, FarVertex, GreenFragment, RenderPass, configure);

  VkCommandBufferBeginInfo BeginInfo{};
  BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (VkResult R = vkBeginCommandBuffer(Smoke.Cmd, &BeginInfo))
    fail("vkBeginCommandBuffer", R);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  PassBegin.renderPass = RenderPass;
  PassBegin.framebuffer = Framebuffer;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Smoke.Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  // The nearer (red) draw first, writing depth 0.2; the farther (green)
  // draw second, rejected since 0.8 is not less than 0.2.
  vkCmdBindPipeline(Smoke.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, NearRed);
  vkCmdDraw(Smoke.Cmd, 3, 1, 0, 0);
  vkCmdBindPipeline(Smoke.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, FarGreen);
  vkCmdDraw(Smoke.Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Smoke.Cmd);
  if (VkResult R = vkEndCommandBuffer(Smoke.Cmd))
    fail("vkEndCommandBuffer", R);
  if (VkResult R = Smoke.submit())
    fail("vkQueueSubmit/vkQueueWaitIdle", R);

  Smoke.printTexels(ColorMemory);

  vkDestroyPipeline(Smoke.Device, FarGreen, nullptr);
  vkDestroyPipeline(Smoke.Device, NearRed, nullptr);
  vkDestroyShaderModule(Smoke.Device, GreenFragment, nullptr);
  vkDestroyShaderModule(Smoke.Device, FarVertex, nullptr);
  vkDestroyShaderModule(Smoke.Device, RedFragment, nullptr);
  vkDestroyShaderModule(Smoke.Device, NearVertex, nullptr);
  vkDestroyFramebuffer(Smoke.Device, Framebuffer, nullptr);
  vkDestroyRenderPass(Smoke.Device, RenderPass, nullptr);
  vkDestroyImageView(Smoke.Device, DepthView, nullptr);
  vkDestroyImage(Smoke.Device, DepthImage, nullptr);
  vkFreeMemory(Smoke.Device, DepthMemory, nullptr);
  vkDestroyImageView(Smoke.Device, ColorView, nullptr);
  vkDestroyImage(Smoke.Device, ColorImage, nullptr);
  vkFreeMemory(Smoke.Device, ColorMemory, nullptr);
}

/// `StencilState::TestEnable`: a writer draw (`CompareOp::Always`,
/// `StencilOp::Replace`) restricted to the left half of the render area
/// writes a stencil reference there; a tester draw (`CompareOp::Equal`)
/// covering the whole area then only lands where that reference matches.
void runStencilScenario(GraphicsSmoke &Smoke, int Argc, char **Argv) {
  if (Argc != 5) {
    std::fprintf(stderr,
                 "usage: %s stencil <vertex.spv> <red-fragment.spv> "
                 "<green-fragment.spv>\n",
                 Argv[0]);
    std::exit(1);
  }
  VkImage ColorImage;
  VkImageView ColorView;
  VkDeviceMemory ColorMemory;
  Smoke.createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, ColorImage, ColorView, ColorMemory);
  VkImage StencilImage;
  VkImageView StencilView;
  VkDeviceMemory StencilMemory;
  Smoke.createImageAndView(
      VK_FORMAT_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_STENCIL_BIT, StencilImage, StencilView, StencilMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_S8_UINT;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference StencilRef{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pDepthStencilAttachment = &StencilRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass RenderPass;
  if (VkResult R =
          vkCreateRenderPass(Smoke.Device, &PassInfo, nullptr, &RenderPass))
    fail("vkCreateRenderPass", R);

  VkImageView FbViews[2] = {ColorView, StencilView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  FbInfo.renderPass = RenderPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer Framebuffer;
  if (VkResult R =
          vkCreateFramebuffer(Smoke.Device, &FbInfo, nullptr, &Framebuffer))
    fail("vkCreateFramebuffer", R);

  auto configureStencil = [](VkPipelineDepthStencilStateCreateInfo &State,
                             VkCompareOp Compare, VkStencilOp PassOp) {
    State.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    State.stencilTestEnable = VK_TRUE;
    VkStencilOpState Face{};
    Face.failOp = VK_STENCIL_OP_KEEP;
    Face.passOp = PassOp;
    Face.depthFailOp = VK_STENCIL_OP_KEEP;
    Face.compareOp = Compare;
    Face.compareMask = 0xFF;
    Face.writeMask = 0xFF;
    Face.reference = 1;
    State.front = Face;
    State.back = Face;
  };

  VkShaderModule Vertex = Smoke.createModule(Argv[2]);
  VkShaderModule RedFragment = Smoke.createModule(Argv[3]);
  VkShaderModule GreenFragment = Smoke.createModule(Argv[4]);

  VkPipelineDepthStencilStateCreateInfo WriterDepthStencil{};
  configureStencil(WriterDepthStencil, VK_COMPARE_OP_ALWAYS,
                   VK_STENCIL_OP_REPLACE);
  VkRect2D WriterScissor{{0, 0}, {Extent / 2, Extent}};
  VkPipeline Writer = createPipeline(
      Smoke, Vertex, RedFragment, RenderPass,
      [&](VkGraphicsPipelineCreateInfo &Info) {
        Info.pDepthStencilState = &WriterDepthStencil;
        const_cast<VkPipelineViewportStateCreateInfo *>(Info.pViewportState)
            ->pScissors = &WriterScissor;
      });

  VkPipelineDepthStencilStateCreateInfo TesterDepthStencil{};
  configureStencil(TesterDepthStencil, VK_COMPARE_OP_EQUAL, VK_STENCIL_OP_KEEP);
  VkPipeline Tester = createPipeline(Smoke, Vertex, GreenFragment, RenderPass,
                                     [&](VkGraphicsPipelineCreateInfo &Info) {
                                       Info.pDepthStencilState =
                                           &TesterDepthStencil;
                                     });

  VkCommandBufferBeginInfo BeginInfo{};
  BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (VkResult R = vkBeginCommandBuffer(Smoke.Cmd, &BeginInfo))
    fail("vkBeginCommandBuffer", R);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 1.0f, 1.0f}};
  ClearValues[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  PassBegin.renderPass = RenderPass;
  PassBegin.framebuffer = Framebuffer;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Smoke.Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Smoke.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Writer);
  vkCmdDraw(Smoke.Cmd, 3, 1, 0, 0);
  vkCmdBindPipeline(Smoke.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Tester);
  vkCmdDraw(Smoke.Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Smoke.Cmd);
  if (VkResult R = vkEndCommandBuffer(Smoke.Cmd))
    fail("vkEndCommandBuffer", R);
  if (VkResult R = Smoke.submit())
    fail("vkQueueSubmit/vkQueueWaitIdle", R);

  Smoke.printTexels(ColorMemory);

  vkDestroyPipeline(Smoke.Device, Tester, nullptr);
  vkDestroyPipeline(Smoke.Device, Writer, nullptr);
  vkDestroyShaderModule(Smoke.Device, GreenFragment, nullptr);
  vkDestroyShaderModule(Smoke.Device, RedFragment, nullptr);
  vkDestroyShaderModule(Smoke.Device, Vertex, nullptr);
  vkDestroyFramebuffer(Smoke.Device, Framebuffer, nullptr);
  vkDestroyRenderPass(Smoke.Device, RenderPass, nullptr);
  vkDestroyImageView(Smoke.Device, StencilView, nullptr);
  vkDestroyImage(Smoke.Device, StencilImage, nullptr);
  vkFreeMemory(Smoke.Device, StencilMemory, nullptr);
  vkDestroyImageView(Smoke.Device, ColorView, nullptr);
  vkDestroyImage(Smoke.Device, ColorImage, nullptr);
  vkFreeMemory(Smoke.Device, ColorMemory, nullptr);
}

/// `BlendState::BlendEnable`: a half-alpha fragment source-over-blends with
/// the attachment's existing (clear) color rather than replacing it.
void runBlendScenario(GraphicsSmoke &Smoke, int Argc, char **Argv) {
  if (Argc != 4) {
    std::fprintf(stderr, "usage: %s blend <vertex.spv> <fragment.spv>\n",
                 Argv[0]);
    std::exit(1);
  }
  VkImage ColorImage;
  VkImageView ColorView;
  VkDeviceMemory ColorMemory;
  Smoke.createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, ColorImage, ColorView, ColorMemory);

  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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
  if (VkResult R =
          vkCreateRenderPass(Smoke.Device, &PassInfo, nullptr, &RenderPass))
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
  if (VkResult R =
          vkCreateFramebuffer(Smoke.Device, &FbInfo, nullptr, &Framebuffer))
    fail("vkCreateFramebuffer", R);

  VkPipelineColorBlendAttachmentState BlendAttachment =
      defaultBlendAttachment();
  BlendAttachment.blendEnable = VK_TRUE;
  BlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  BlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  BlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  BlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  BlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  BlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkShaderModule Vertex = Smoke.createModule(Argv[2]);
  VkShaderModule Fragment = Smoke.createModule(Argv[3]);
  VkPipeline Pipeline = createPipeline(Smoke, Vertex, Fragment, RenderPass,
                                       [&](VkGraphicsPipelineCreateInfo &Info) {
                                         Info.pColorBlendState = &Blend;
                                       });

  VkCommandBufferBeginInfo BeginInfo{};
  BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (VkResult R = vkBeginCommandBuffer(Smoke.Cmd, &BeginInfo))
    fail("vkBeginCommandBuffer", R);
  VkClearValue Clear{};
  Clear.color = {{0.0f, 0.0f, 1.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  PassBegin.renderPass = RenderPass;
  PassBegin.framebuffer = Framebuffer;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 1;
  PassBegin.pClearValues = &Clear;
  vkCmdBeginRenderPass(Smoke.Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Smoke.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
  vkCmdDraw(Smoke.Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Smoke.Cmd);
  if (VkResult R = vkEndCommandBuffer(Smoke.Cmd))
    fail("vkEndCommandBuffer", R);
  if (VkResult R = Smoke.submit())
    fail("vkQueueSubmit/vkQueueWaitIdle", R);

  Smoke.printTexels(ColorMemory);

  vkDestroyPipeline(Smoke.Device, Pipeline, nullptr);
  vkDestroyShaderModule(Smoke.Device, Fragment, nullptr);
  vkDestroyShaderModule(Smoke.Device, Vertex, nullptr);
  vkDestroyFramebuffer(Smoke.Device, Framebuffer, nullptr);
  vkDestroyRenderPass(Smoke.Device, RenderPass, nullptr);
  vkDestroyImageView(Smoke.Device, ColorView, nullptr);
  vkDestroyImage(Smoke.Device, ColorImage, nullptr);
  vkFreeMemory(Smoke.Device, ColorMemory, nullptr);
}

/// Two color attachments: a fragment stage with two `Output`s writes a
/// distinct color to each, printed one after the other, separated by a line
/// of dashes.
void runMRTScenario(GraphicsSmoke &Smoke, int Argc, char **Argv) {
  if (Argc != 4) {
    std::fprintf(stderr, "usage: %s mrt <vertex.spv> <fragment.spv>\n",
                 Argv[0]);
    std::exit(1);
  }
  VkImage Image0, Image1;
  VkImageView View0, View1;
  VkDeviceMemory Memory0, Memory1;
  Smoke.createImageAndView(VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, Image0, View0, Memory0);
  Smoke.createImageAndView(VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, Image1, View1, Memory1);

  VkAttachmentDescription Attachments[2]{};
  for (VkAttachmentDescription &A : Attachments) {
    A.format = VK_FORMAT_R8G8B8A8_UNORM;
    A.samples = VK_SAMPLE_COUNT_1_BIT;
    A.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    A.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  }
  VkAttachmentReference ColorRefs[2] = {
      {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
      {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 2;
  Subpass.pColorAttachments = ColorRefs;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass RenderPass;
  if (VkResult R =
          vkCreateRenderPass(Smoke.Device, &PassInfo, nullptr, &RenderPass))
    fail("vkCreateRenderPass", R);

  VkImageView FbViews[2] = {View0, View1};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  FbInfo.renderPass = RenderPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer Framebuffer;
  if (VkResult R =
          vkCreateFramebuffer(Smoke.Device, &FbInfo, nullptr, &Framebuffer))
    fail("vkCreateFramebuffer", R);

  VkPipelineColorBlendAttachmentState BlendAttachments[2] = {
      defaultBlendAttachment(), defaultBlendAttachment()};
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  Blend.attachmentCount = 2;
  Blend.pAttachments = BlendAttachments;

  VkShaderModule Vertex = Smoke.createModule(Argv[2]);
  VkShaderModule Fragment = Smoke.createModule(Argv[3]);
  VkPipeline Pipeline = createPipeline(Smoke, Vertex, Fragment, RenderPass,
                                       [&](VkGraphicsPipelineCreateInfo &Info) {
                                         Info.pColorBlendState = &Blend;
                                       });

  VkCommandBufferBeginInfo BeginInfo{};
  BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (VkResult R = vkBeginCommandBuffer(Smoke.Cmd, &BeginInfo))
    fail("vkBeginCommandBuffer", R);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  PassBegin.renderPass = RenderPass;
  PassBegin.framebuffer = Framebuffer;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Smoke.Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Smoke.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
  vkCmdDraw(Smoke.Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Smoke.Cmd);
  if (VkResult R = vkEndCommandBuffer(Smoke.Cmd))
    fail("vkEndCommandBuffer", R);
  if (VkResult R = Smoke.submit())
    fail("vkQueueSubmit/vkQueueWaitIdle", R);

  Smoke.printTexels(Memory0);
  std::printf("--\n");
  Smoke.printTexels(Memory1);

  vkDestroyPipeline(Smoke.Device, Pipeline, nullptr);
  vkDestroyShaderModule(Smoke.Device, Fragment, nullptr);
  vkDestroyShaderModule(Smoke.Device, Vertex, nullptr);
  vkDestroyFramebuffer(Smoke.Device, Framebuffer, nullptr);
  vkDestroyRenderPass(Smoke.Device, RenderPass, nullptr);
  vkDestroyImageView(Smoke.Device, View1, nullptr);
  vkDestroyImage(Smoke.Device, Image1, nullptr);
  vkFreeMemory(Smoke.Device, Memory1, nullptr);
  vkDestroyImageView(Smoke.Device, View0, nullptr);
  vkDestroyImage(Smoke.Device, Image0, nullptr);
  vkFreeMemory(Smoke.Device, Memory0, nullptr);
}

/// A 4x-multisample color attachment with a resolve attachment: a draw
/// fully covering the render area resolves to a uniform color in the
/// single-sample target.
void runMSAAResolveScenario(GraphicsSmoke &Smoke, int Argc, char **Argv) {
  if (Argc != 4) {
    std::fprintf(stderr, "usage: %s msaa-resolve <vertex.spv> <fragment.spv>\n",
                 Argv[0]);
    std::exit(1);
  }
  VkImage MSImage, ResolveImage;
  VkImageView MSView, ResolveView;
  VkDeviceMemory MSMemory, ResolveMemory;
  Smoke.createImageAndView(VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, MSImage, MSView, MSMemory,
                           VK_SAMPLE_COUNT_4_BIT);
  Smoke.createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, ResolveImage, ResolveView, ResolveMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_4_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference ResolveRef{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pResolveAttachments = &ResolveRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass RenderPass;
  if (VkResult R =
          vkCreateRenderPass(Smoke.Device, &PassInfo, nullptr, &RenderPass))
    fail("vkCreateRenderPass", R);

  VkImageView FbViews[2] = {MSView, ResolveView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  FbInfo.renderPass = RenderPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer Framebuffer;
  if (VkResult R =
          vkCreateFramebuffer(Smoke.Device, &FbInfo, nullptr, &Framebuffer))
    fail("vkCreateFramebuffer", R);

  VkShaderModule Vertex = Smoke.createModule(Argv[2]);
  VkShaderModule Fragment = Smoke.createModule(Argv[3]);
  VkPipeline Pipeline =
      createPipeline(Smoke, Vertex, Fragment, RenderPass,
                     [](VkGraphicsPipelineCreateInfo &Info) {
                       const_cast<VkPipelineMultisampleStateCreateInfo *>(
                           Info.pMultisampleState)
                           ->rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
                     });

  VkCommandBufferBeginInfo BeginInfo{};
  BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (VkResult R = vkBeginCommandBuffer(Smoke.Cmd, &BeginInfo))
    fail("vkBeginCommandBuffer", R);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  PassBegin.renderPass = RenderPass;
  PassBegin.framebuffer = Framebuffer;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Smoke.Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Smoke.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
  vkCmdDraw(Smoke.Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Smoke.Cmd);
  if (VkResult R = vkEndCommandBuffer(Smoke.Cmd))
    fail("vkEndCommandBuffer", R);
  if (VkResult R = Smoke.submit())
    fail("vkQueueSubmit/vkQueueWaitIdle", R);

  Smoke.printTexels(ResolveMemory);

  vkDestroyPipeline(Smoke.Device, Pipeline, nullptr);
  vkDestroyShaderModule(Smoke.Device, Fragment, nullptr);
  vkDestroyShaderModule(Smoke.Device, Vertex, nullptr);
  vkDestroyFramebuffer(Smoke.Device, Framebuffer, nullptr);
  vkDestroyRenderPass(Smoke.Device, RenderPass, nullptr);
  vkDestroyImageView(Smoke.Device, ResolveView, nullptr);
  vkDestroyImage(Smoke.Device, ResolveImage, nullptr);
  vkFreeMemory(Smoke.Device, ResolveMemory, nullptr);
  vkDestroyImageView(Smoke.Device, MSView, nullptr);
  vkDestroyImage(Smoke.Device, MSImage, nullptr);
  vkFreeMemory(Smoke.Device, MSMemory, nullptr);
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: %s <render-pass|dynamic-rendering|depth|stencil|"
                 "blend|mrt|msaa-resolve> ...\n",
                 argv[0]);
    return 1;
  }
  std::string Scenario = argv[1];
  GraphicsSmoke Smoke;
  if (Scenario == "render-pass")
    runRenderPassScenario(Smoke, argc, argv);
  else if (Scenario == "dynamic-rendering")
    runDynamicRenderingScenario(Smoke, argc, argv);
  else if (Scenario == "depth")
    runDepthScenario(Smoke, argc, argv);
  else if (Scenario == "stencil")
    runStencilScenario(Smoke, argc, argv);
  else if (Scenario == "blend")
    runBlendScenario(Smoke, argc, argv);
  else if (Scenario == "mrt")
    runMRTScenario(Smoke, argc, argv);
  else if (Scenario == "msaa-resolve")
    runMSAAResolveScenario(Smoke, argc, argv);
  else {
    std::fprintf(stderr, "FAIL: unknown scenario '%s'\n", Scenario.c_str());
    return 1;
  }
  return 0;
}
