//===- DrawTest.cpp - End-to-end V6 draw tests --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (V6) The milestone's own end-to-end scenario: render off-screen through a
// `VkRenderPass`, from real SPIR-V vertex/fragment modules, and observe the
// resulting image -- the whole path from `vkCmdBeginRenderPass` through the
// normalized render-target binding, the prepared draw, and the software
// graphics executor (see "Draw commands and vertex data" in
// feme/docs/FeMeVulkanDesign.md).
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "Buffer.h"
#include "CommandBuffer.h"
#include "EntryPoints.h"
#include "GraphicsPipeline.h"
#include "Icd.h"
#include "Image.h"
#include "Objects.h"
#include "RenderPass.h"

#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/SPIRV/Serialization.h"

#include "gtest/gtest.h"

#include <cstring>
#include <vector>

using namespace feme::vulkan;

namespace {

std::vector<uint32_t> assembleSPIRV(llvm::StringRef Source) {
  mlir::MLIRContext Ctx;
  Ctx.loadDialect<mlir::spirv::SPIRVDialect>();
  mlir::OwningOpRef<mlir::spirv::ModuleOp> Module =
      mlir::parseSourceString<mlir::spirv::ModuleOp>(Source, &Ctx);
  if (!Module)
    return {};
  llvm::SmallVector<uint32_t, 0> Binary;
  if (mlir::failed(mlir::spirv::serialize(*Module, Binary)))
    return {};
  return std::vector<uint32_t>(Binary.begin(), Binary.end());
}

/// One oversized counter-clockwise triangle selected from `gl_VertexIndex`,
/// covering the whole viewport after clipping.
constexpr llvm::StringLiteral FullscreenVertexSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vid built_in("VertexIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vidp = spirv.mlir.addressof @vid : !spirv.ptr<i32, Input>
    %v = spirv.Load "Input" %vidp : i32
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %is0 = spirv.IEqual %v, %c0 : i32
    %is1 = spirv.IEqual %v, %c1 : i32
    %neg1 = spirv.Constant -1.0 : f32
    %three = spirv.Constant 3.0 : f32
    %xb = spirv.Select %is1, %three, %neg1 : i1, f32
    %x = spirv.Select %is0, %neg1, %xb : i1, f32
    %yb = spirv.Select %is1, %neg1, %three : i1, f32
    %y = spirv.Select %is0, %neg1, %yb : i1, f32
    %z = spirv.Constant 0.0 : f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @pos
}
)mlir";

/// Solid red into SV_Target0.
constexpr llvm::StringLiteral RedFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c = spirv.Constant dense<[1.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

class DrawTest : public ::testing::Test {
protected:
  static constexpr uint32_t Extent = 4;

  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);
    vkGetDeviceQueue(Device, 0, 0, &Queue);

    createColorTarget();
    createRenderPassAndFramebuffer();

    VkPipelineLayoutCreateInfo LayoutInfo{};
    ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout),
              VK_SUCCESS);

    VkCommandPoolCreateInfo PoolInfo{};
    ASSERT_EQ(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool),
              VK_SUCCESS);
    VkCommandBufferAllocateInfo AllocInfo{};
    AllocInfo.commandPool = Pool;
    AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocInfo.commandBufferCount = 1;
    ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &Cmd), VK_SUCCESS);
  }

  void TearDown() override {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyFramebuffer(Device, Framebuffer, nullptr);
    vkDestroyRenderPass(Device, Pass, nullptr);
    vkDestroyImageView(Device, ColorView, nullptr);
    vkDestroyImage(Device, ColorImage, nullptr);
    vkFreeMemory(Device, ColorMemory, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  void createColorTarget() {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ImageInfo.extent = {Extent, Extent, 1};
    ImageInfo.mipLevels = 1;
    ImageInfo.arrayLayers = 1;
    ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &ColorImage),
              VK_SUCCESS);
    VkMemoryRequirements Reqs{};
    vkGetImageMemoryRequirements(Device, ColorImage, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Reqs.size;
    ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &ColorMemory),
              VK_SUCCESS);
    ASSERT_EQ(vkBindImageMemory(Device, ColorImage, ColorMemory, 0),
              VK_SUCCESS);

    VkImageViewCreateInfo ViewInfo{};
    ViewInfo.image = ColorImage;
    ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInfo.subresourceRange.levelCount = 1;
    ViewInfo.subresourceRange.layerCount = 1;
    ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &ColorView),
              VK_SUCCESS);
  }

  void createRenderPassAndFramebuffer() {
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
    PassInfo.attachmentCount = 1;
    PassInfo.pAttachments = &Attachment;
    PassInfo.subpassCount = 1;
    PassInfo.pSubpasses = &Subpass;
    ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &Pass),
              VK_SUCCESS);

    VkFramebufferCreateInfo FbInfo{};
    FbInfo.renderPass = Pass;
    FbInfo.attachmentCount = 1;
    FbInfo.pAttachments = &ColorView;
    FbInfo.width = Extent;
    FbInfo.height = Extent;
    FbInfo.layers = 1;
    ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &Framebuffer),
              VK_SUCCESS);
  }

  VkShaderModule createModule(llvm::StringRef Source) {
    std::vector<uint32_t> Words = assembleSPIRV(Source);
    EXPECT_FALSE(Words.empty());
    VkShaderModuleCreateInfo Info{};
    Info.codeSize = Words.size() * sizeof(uint32_t);
    Info.pCode = Words.data();
    VkShaderModule Module = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateShaderModule(Device, &Info, nullptr, &Module),
              VK_SUCCESS);
    return Module;
  }

  /// Creates and binds a \p Size-byte buffer with \p Usage, returning its
  /// handle and (via \p OutMemory) its backing allocation.
  VkBuffer createBuffer(VkDeviceSize Size, VkDeviceMemory &OutMemory,
                        VkBufferUsageFlags Usage) {
    VkBufferCreateInfo Info{};
    Info.size = Size;
    Info.usage = Usage;
    VkBuffer Buf = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateBuffer(Device, &Info, nullptr, &Buf), VK_SUCCESS);
    VkMemoryRequirements Reqs{};
    vkGetBufferMemoryRequirements(Device, Buf, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Reqs.size;
    EXPECT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &OutMemory),
              VK_SUCCESS);
    EXPECT_EQ(vkBindBufferMemory(Device, Buf, OutMemory, 0), VK_SUCCESS);
    return Buf;
  }

  /// \p Rendering, when non-null, replaces the fixture's `VkRenderPass`
  /// with a chained `VkPipelineRenderingCreateInfo` (dynamic rendering).
  VkPipeline
  createPipeline(VkShaderModule Vertex, VkShaderModule Fragment,
                 const VkPipelineRenderingCreateInfo *Rendering = nullptr) {
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
    VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
    VkRect2D Scissor{{0, 0}, {Extent, Extent}};
    VkPipelineViewportStateCreateInfo ViewportState{};
    ViewportState.viewportCount = 1;
    ViewportState.pViewports = &Viewport;
    ViewportState.scissorCount = 1;
    ViewportState.pScissors = &Scissor;
    VkPipelineRasterizationStateCreateInfo Raster{};
    Raster.cullMode = VK_CULL_MODE_NONE;
    Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    VkPipelineMultisampleStateCreateInfo Multisample{};
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState BlendAttachment{};
    BlendAttachment.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo Blend{};
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
    Info.layout = Layout;
    if (Rendering)
      Info.pNext = Rendering;
    else
      Info.renderPass = Pass;

    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                        nullptr, &Pipe),
              VK_SUCCESS);
    return Pipe;
  }

  void beginRenderPass(VkClearColorValue Clear) {
    VkCommandBufferBeginInfo BeginInfo{};
    ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
    VkClearValue ClearValue{};
    ClearValue.color = Clear;
    VkRenderPassBeginInfo PassBegin{};
    PassBegin.renderPass = Pass;
    PassBegin.framebuffer = Framebuffer;
    PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
    PassBegin.clearValueCount = 1;
    PassBegin.pClearValues = &ClearValue;
    vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  }

  VkResult submit() {
    VkSubmitInfo Submit{};
    Submit.commandBufferCount = 1;
    Submit.pCommandBuffers = &Cmd;
    return vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE);
  }

  /// Texel (X, Y) of the color target, as four bytes.
  std::array<uint8_t, 4> texel(uint32_t X, uint32_t Y) {
    const auto *Data =
        static_cast<const uint8_t *>(fromHandle<Image>(ColorImage)->data());
    std::array<uint8_t, 4> Result{};
    std::memcpy(Result.data(), Data + ((size_t)Y * Extent + X) * 4, 4);
    return Result;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkQueue Queue = VK_NULL_HANDLE;
  VkDeviceMemory ColorMemory = VK_NULL_HANDLE;
  VkImage ColorImage = VK_NULL_HANDLE;
  VkImageView ColorView = VK_NULL_HANDLE;
  VkRenderPass Pass = VK_NULL_HANDLE;
  VkFramebuffer Framebuffer = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
  VkCommandBuffer Cmd = VK_NULL_HANDLE;
};

/// V6's own end-to-end scenario: an off-screen render pass whose one draw
/// covers the whole render area with the fragment stage's solid red.
TEST_F(DrawTest, RendersTriangleThroughRenderPass) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[2], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[3], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// `VK_ATTACHMENT_LOAD_OP_CLEAR` clears exactly the render area, and a
/// dynamic scissor further restricts what a draw may write -- so a draw
/// covering the whole viewport leaves everything outside the scissor at its
/// cleared value.
TEST_F(DrawTest, DynamicScissorRestrictsTheDraw) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);

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
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_SCISSOR;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Dynamic;

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
  Info.pDynamicState = &DynamicInfo;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 1.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  VkRect2D Scissor{{0, 0}, {2, 2}};
  vkCmdSetScissor(Cmd, 0, 1, &Scissor);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(0, 0)[0], 0xFF);
  EXPECT_EQ(texel(1, 1)[0], 0xFF);
  // Outside the scissor: still the clear color (blue).
  EXPECT_EQ(texel(3, 3)[0], 0x00);
  EXPECT_EQ(texel(3, 3)[2], 0xFF);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// An indexed draw fetches its vertices through the bound index buffer.
TEST_F(DrawTest, RendersIndexedDraw) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkDeviceMemory IndexMemory = VK_NULL_HANDLE;
  VkBuffer IndexBuffer = createBuffer(3 * sizeof(uint32_t), IndexMemory,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  uint32_t Indices[3] = {0, 1, 2};
  std::memcpy(fromHandle<Buffer>(IndexBuffer)->data(), Indices,
              sizeof(Indices));

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindIndexBuffer(Cmd, IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(Cmd, 3, 1, 0, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(2, 2)[0], 0xFF);
  EXPECT_EQ(texel(2, 2)[3], 0xFF);

  vkDestroyBuffer(Device, IndexBuffer, nullptr);
  vkFreeMemory(Device, IndexMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// A draw recorded outside a render pass instance, and a draw with no bound
/// graphics pipeline, both fail the submission rather than rendering
/// somewhere undefined.
TEST_F(DrawTest, RejectsDrawOutsideRenderPass) {
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);
}

/// An indirect draw reads its `VkDrawIndirectCommand` from a bound buffer,
/// validated exactly like an indirect dispatch's group counts.
TEST_F(DrawTest, RendersIndirectDraw) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkBuffer Indirect = createBuffer(sizeof(VkDrawIndirectCommand), Memory,
                                   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
  VkDrawIndirectCommand Args{};
  Args.vertexCount = 3;
  Args.instanceCount = 1;
  std::memcpy(fromHandle<Buffer>(Indirect)->data(), &Args, sizeof(Args));

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDrawIndirect(Cmd, Indirect, 0, 1, sizeof(VkDrawIndirectCommand));
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(1, 2)[0], 0xFF);
  EXPECT_EQ(texel(1, 2)[3], 0xFF);

  vkDestroyBuffer(Device, Indirect, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// An indirect draw whose command array overruns its buffer is rejected,
/// not clamped -- and so is one whose stride is smaller than the command it
/// describes.
TEST_F(DrawTest, RejectsOutOfBoundsIndirectDraw) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkBuffer Indirect = createBuffer(sizeof(VkDrawIndirectCommand), Memory,
                                   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  // Two commands in a one-command buffer.
  vkCmdDrawIndirect(Cmd, Indirect, 0, 2, sizeof(VkDrawIndirectCommand));
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkResetCommandBuffer(Cmd, 0);
  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDrawIndirect(Cmd, Indirect, 0, 1, 4);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyBuffer(Device, Indirect, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// An indexed draw whose index range overruns its bound index buffer is
/// rejected before anything is fetched.
TEST_F(DrawTest, RejectsOutOfBoundsIndexRange) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkBuffer IndexBuffer = createBuffer(3 * sizeof(uint32_t), Memory,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindIndexBuffer(Cmd, IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(Cmd, 6, 1, 0, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyBuffer(Device, IndexBuffer, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Dynamic rendering reaches the same normalized render-target binding a
/// `VkRenderPass` compiles into: the same shaders, clear and draw produce
/// the same image through `vkCmdBeginRenderingKHR`.
TEST_F(DrawTest, RendersThroughDynamicRendering) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);

  VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 1;
  Rendering.pColorAttachmentFormats = &ColorFormat;
  VkPipeline Pipe = createPipeline(Vertex, Fragment, &Rendering);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);

  VkRenderingAttachmentInfo ColorAttachment{};
  ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachment.imageView = ColorView;
  ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ColorAttachment.clearValue.color = {{0.0f, 1.0f, 0.0f, 1.0f}};

  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 1;
  RenderingInfo.pColorAttachments = &ColorAttachment;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      EXPECT_EQ(texel(X, Y)[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(texel(X, Y)[1], 0x00) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// The driver advertises `VK_KHR_dynamic_rendering` and accepts it at
/// device creation; anything it does not implement is still refused.
TEST_F(DrawTest, AdvertisesDynamicRenderingExtension) {
  uint32_t Count = 0;
  ASSERT_EQ(
      vkEnumerateDeviceExtensionProperties(Physical, nullptr, &Count, nullptr),
      VK_SUCCESS);
  ASSERT_EQ(Count, 1u);
  VkExtensionProperties Properties{};
  ASSERT_EQ(vkEnumerateDeviceExtensionProperties(Physical, nullptr, &Count,
                                                 &Properties),
            VK_SUCCESS);
  EXPECT_STREQ(Properties.extensionName,
               VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

  VkPhysicalDeviceDynamicRenderingFeatures Features{};
  Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &Features;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(Features.dynamicRendering, VK_TRUE);

  const char *Enabled = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
  VkDeviceCreateInfo DevInfo{};
  DevInfo.enabledExtensionCount = 1;
  DevInfo.ppEnabledExtensionNames = &Enabled;
  VkDevice Second = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Second), VK_SUCCESS);
  vkDestroyDevice(Second, nullptr);

  const char *Unsupported = "VK_KHR_swapchain";
  DevInfo.ppEnabledExtensionNames = &Unsupported;
  EXPECT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Second),
            VK_ERROR_EXTENSION_NOT_PRESENT);
}

/// `vkCmdClearAttachments` clears the bound attachment over its rectangles,
/// inside the render pass instance, after a draw has already written it.
TEST_F(DrawTest, ClearsAttachmentInsideRenderPass) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  VkClearAttachment Clear{};
  Clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  Clear.colorAttachment = 0;
  Clear.clearValue.color = {{0.0f, 0.0f, 1.0f, 1.0f}};
  VkClearRect Rect{};
  Rect.rect = {{0, 0}, {2, 2}};
  Rect.layerCount = 1;
  vkCmdClearAttachments(Cmd, 1, &Clear, 1, &Rect);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Inside the cleared rectangle: blue. Outside it: the draw's red.
  EXPECT_EQ(texel(0, 0)[2], 0xFF);
  EXPECT_EQ(texel(0, 0)[0], 0x00);
  EXPECT_EQ(texel(3, 3)[0], 0xFF);
  EXPECT_EQ(texel(3, 3)[2], 0x00);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

} // namespace
