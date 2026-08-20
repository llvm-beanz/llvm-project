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

/// Solid green into SV_Target0.
constexpr llvm::StringLiteral GreenFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c = spirv.Constant dense<[0.0, 1.0, 0.0, 1.0]> : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// Half-alpha red into SV_Target0, for `BlendState::BlendEnable` coverage.
constexpr llvm::StringLiteral HalfAlphaRedFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c = spirv.Constant dense<[1.0, 0.0, 0.0, 0.5]> : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// Solid red into SV_Target0 and solid green into SV_Target1, for
/// multiple-render-target coverage.
constexpr llvm::StringLiteral DualOutputFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color0 {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @color1 {location = 1 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c0 = spirv.Constant dense<[1.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %c1 = spirv.Constant dense<[0.0, 1.0, 0.0, 1.0]> : vector<4xf32>
    %p0 = spirv.mlir.addressof @color0 : !spirv.ptr<vector<4xf32>, Output>
    %p1 = spirv.mlir.addressof @color1 : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p0, %c0 : vector<4xf32>
    spirv.Store "Output" %p1, %c1 : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color0, @color1
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// Opaque white into `SV_Target0`'s ordinary (`Index=0`) output and
/// (0.25, 0.5, 0.75, 1.0) into its `Index=1` companion at the same
/// `Location=0` -- roadmap C4's dual-source blend coverage
/// (`VK_BLEND_FACTOR_SRC1_*`).
constexpr llvm::StringLiteral DualSourceFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color0 {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @color1 {location = 0 : i32, index = 1 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c0 = spirv.Constant dense<[1.0, 1.0, 1.0, 1.0]> : vector<4xf32>
    %c1 = spirv.Constant dense<[0.25, 0.5, 0.75, 1.0]> : vector<4xf32>
    %p0 = spirv.mlir.addressof @color0 : !spirv.ptr<vector<4xf32>, Output>
    %p1 = spirv.mlir.addressof @color1 : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p0, %c0 : vector<4xf32>
    spirv.Store "Output" %p1, %c1 : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color0, @color1
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// One oversized counter-clockwise triangle at a fixed depth of 0.2 (nearer
/// to the viewer under `CompareOp::Less`), for `DepthState` coverage.
constexpr llvm::StringLiteral NearDepthVertexSource = R"mlir(
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
    %z = spirv.Constant 0.2 : f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @pos
}
)mlir";

/// The same oversized triangle, at a fixed depth of 0.8 (farther from the
/// viewer under `CompareOp::Less`).
constexpr llvm::StringLiteral FarDepthVertexSource = R"mlir(
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
    %z = spirv.Constant 0.8 : f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @pos
}
)mlir";

/// A vertex stage whose position comes from `gl_VertexIndex` (the same
/// oversized fullscreen triangle as `FullscreenVertexSource`) but whose
/// fragment color is a per-instance vertex input attribute at location 1,
/// passed through unchanged -- covers per-instance vertex input rate
/// (`VK_VERTEX_INPUT_RATE_INSTANCE`).
constexpr llvm::StringLiteral PerInstanceColorVertexSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vid built_in("VertexIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @aColor {location = 1 : i32} : !spirv.ptr<vector<4xf32>, Input>
  spirv.GlobalVariable @vColor {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
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

    %colp = spirv.mlir.addressof @aColor : !spirv.ptr<vector<4xf32>, Input>
    %col = spirv.Load "Input" %colp : vector<4xf32>
    %vcp = spirv.mlir.addressof @vColor : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %vcp, %col : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @aColor, @vColor, @pos
}
)mlir";

/// A fragment stage passing its location-0 input straight through to
/// SV_Target0, for `PerInstanceColorVertexSource`'s varying.
constexpr llvm::StringLiteral PassthroughColorFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vColor {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Input>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vp = spirv.mlir.addressof @vColor : !spirv.ptr<vector<4xf32>, Input>
    %v = spirv.Load "Input" %vp : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %v : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @vColor, @color
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

  /// Texel (X, Y) of \p Img, as four bytes -- any 4-byte-per-texel color
  /// target, not only the fixture's default one.
  std::array<uint8_t, 4> texelOf(VkImage Img, uint32_t X, uint32_t Y) {
    const auto *Data =
        static_cast<const uint8_t *>(fromHandle<Image>(Img)->data());
    std::array<uint8_t, 4> Result{};
    std::memcpy(Result.data(), Data + ((size_t)Y * Extent + X) * 4, 4);
    return Result;
  }

  /// Texel (X, Y) of the fixture's default color target, as four bytes.
  std::array<uint8_t, 4> texel(uint32_t X, uint32_t Y) {
    return texelOf(ColorImage, X, Y);
  }

  /// Creates and binds a `Extent`x`Extent` image (and its view) with
  /// \p Format, \p Usage, \p Aspect, and \p Samples -- used by tests needing
  /// an attachment beyond the fixture's single default color target (depth,
  /// stencil, a second color attachment, or a multisample source).
  void
  createImageAndView(VkFormat Format, VkImageUsageFlags Usage,
                     VkImageAspectFlags Aspect, VkImage &OutImage,
                     VkImageView &OutView, VkDeviceMemory &OutMemory,
                     VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT) {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = Format;
    ImageInfo.extent = {Extent, Extent, 1};
    ImageInfo.mipLevels = 1;
    ImageInfo.arrayLayers = 1;
    ImageInfo.samples = Samples;
    ImageInfo.usage = Usage;
    ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &OutImage),
              VK_SUCCESS);
    VkMemoryRequirements Reqs{};
    vkGetImageMemoryRequirements(Device, OutImage, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Reqs.size;
    ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &OutMemory),
              VK_SUCCESS);
    ASSERT_EQ(vkBindImageMemory(Device, OutImage, OutMemory, 0), VK_SUCCESS);

    VkImageViewCreateInfo ViewInfo{};
    ViewInfo.image = OutImage;
    ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format = Format;
    ViewInfo.subresourceRange.aspectMask = Aspect;
    ViewInfo.subresourceRange.levelCount = 1;
    ViewInfo.subresourceRange.layerCount = 1;
    ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &OutView),
              VK_SUCCESS);
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

/// Roadmap C6: an imageless framebuffer (`VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT`)
/// defers its attachment view to `vkCmdBeginRenderPass`'s own
/// `VkRenderPassAttachmentBeginInfo` instead of binding one at creation
/// time -- otherwise identical to `RendersTriangleThroughRenderPass` above,
/// confirming the render-target binding built from that deferred view
/// renders exactly the same image a concrete framebuffer would.
TEST_F(DrawTest, RendersThroughImagelessFramebuffer) {
  VkFramebufferAttachmentImageInfo AttachmentImageInfo{};
  AttachmentImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  AttachmentImageInfo.width = Extent;
  AttachmentImageInfo.height = Extent;
  AttachmentImageInfo.layerCount = 1;
  VkFormat ViewFormat = VK_FORMAT_R8G8B8A8_UNORM;
  AttachmentImageInfo.viewFormatCount = 1;
  AttachmentImageInfo.pViewFormats = &ViewFormat;
  VkFramebufferAttachmentsCreateInfo AttachmentsInfo{};
  AttachmentsInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO;
  AttachmentsInfo.attachmentImageInfoCount = 1;
  AttachmentsInfo.pAttachmentImageInfos = &AttachmentImageInfo;

  VkFramebufferCreateInfo FbInfo{};
  FbInfo.pNext = &AttachmentsInfo;
  FbInfo.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
  FbInfo.renderPass = Pass;
  FbInfo.attachmentCount = 1;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer ImagelessFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &ImagelessFb),
            VK_SUCCESS);
  EXPECT_TRUE(
      fromHandle<feme::vulkan::Framebuffer>(ImagelessFb)->isImageless());
  EXPECT_TRUE(fromHandle<feme::vulkan::Framebuffer>(ImagelessFb)
                  ->attachments()
                  .empty());

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValue{};
  ClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassAttachmentBeginInfo AttachmentBeginInfo{};
  AttachmentBeginInfo.sType =
      VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO;
  AttachmentBeginInfo.attachmentCount = 1;
  AttachmentBeginInfo.pAttachments = &ColorView;
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.pNext = &AttachmentBeginInfo;
  PassBegin.renderPass = Pass;
  PassBegin.framebuffer = ImagelessFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 1;
  PassBegin.pClearValues = &ClearValue;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(0, 0)[0], 0xFF);
  EXPECT_EQ(texel(0, 0)[1], 0x00);

  vkDestroyFramebuffer(Device, ImagelessFb, nullptr);
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

/// (roadmap C4c) `VK_DYNAMIC_STATE_CULL_MODE`: a pipeline that declares it
/// dynamic must actually cull per whatever `vkCmdSetCullModeEXT` last
/// recorded, not per its (irrelevant) creation-time `cullMode`.
TEST_F(DrawTest, DynamicCullModeControlsCulling) {
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
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  // Creation-time cull mode is deliberately `FRONT_AND_BACK` (would cull
  // everything) to prove the dynamic value, not this one, governs the draw.
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_CULL_MODE;
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
  vkCmdSetCullModeEXT(Cmd, VK_CULL_MODE_NONE);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Cull mode set dynamically to `NONE`: the fullscreen triangle is drawn.
  EXPECT_EQ(texel(2, 2)[0], 0xFF);

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

/// A `VK_VERTEX_INPUT_RATE_INSTANCE` binding advances once per instance
/// rather than once per vertex: `firstInstance` selects the buffer's second
/// element (green), not its first (red) -- a per-vertex-rate fetch would
/// instead read vertex index 0 and always see the first element.
TEST_F(DrawTest, RendersPerInstanceVertexAttribute) {
  VkShaderModule Vertex = createModule(PerInstanceColorVertexSource);
  VkShaderModule Fragment = createModule(PassthroughColorFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkVertexInputBindingDescription BindingDesc{0, sizeof(float) * 4,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VkVertexInputAttributeDescription AttrDesc{1, 0,
                                             VK_FORMAT_R32G32B32A32_SFLOAT, 0};
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VertexInput.vertexAttributeDescriptionCount = 1;
  VertexInput.pVertexAttributeDescriptions = &AttrDesc;
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
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkDeviceMemory InstanceMemory = VK_NULL_HANDLE;
  VkBuffer InstanceBuffer = createBuffer(2 * sizeof(float) * 4, InstanceMemory,
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  float Colors[8] = {
      1.0f, 0.0f, 0.0f, 1.0f, // instance 0: red
      0.0f, 1.0f, 0.0f, 1.0f, // instance 1: green
  };
  std::memcpy(fromHandle<Buffer>(InstanceBuffer)->data(), Colors,
              sizeof(Colors));

  VkDeviceSize Offset = 0;
  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindVertexBuffers(Cmd, 0, 1, &InstanceBuffer, &Offset);
  vkCmdDraw(Cmd, 3, 1, 0, 1);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(2, 2)[0], 0x00);
  EXPECT_EQ(texel(2, 2)[1], 0xFF);
  EXPECT_EQ(texel(2, 2)[2], 0x00);

  vkDestroyBuffer(Device, InstanceBuffer, nullptr);
  vkFreeMemory(Device, InstanceMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap C4c) `VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE`: a pipeline
/// created with a deliberately wrong static stride (double the real
/// per-instance record size, which would read instance 1's color at half
/// its correct offset) still renders correctly once
/// `vkCmdBindVertexBuffers2EXT`'s `pStrides` overrides it dynamically.
TEST_F(DrawTest, DynamicVertexInputBindingStrideOverridesStaticStride) {
  VkShaderModule Vertex = createModule(PerInstanceColorVertexSource);
  VkShaderModule Fragment = createModule(PassthroughColorFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  // Wrong on purpose: the real per-instance record is `sizeof(float) * 4`.
  VkVertexInputBindingDescription BindingDesc{0, sizeof(float) * 8,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VkVertexInputAttributeDescription AttrDesc{1, 0,
                                             VK_FORMAT_R32G32B32A32_SFLOAT, 0};
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VertexInput.vertexAttributeDescriptionCount = 1;
  VertexInput.pVertexAttributeDescriptions = &AttrDesc;
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
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;
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

  VkDeviceMemory InstanceMemory = VK_NULL_HANDLE;
  VkBuffer InstanceBuffer = createBuffer(2 * sizeof(float) * 4, InstanceMemory,
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  float Colors[8] = {
      1.0f, 0.0f, 0.0f, 1.0f, // instance 0: red
      0.0f, 1.0f, 0.0f, 1.0f, // instance 1: green
  };
  std::memcpy(fromHandle<Buffer>(InstanceBuffer)->data(), Colors,
              sizeof(Colors));

  VkDeviceSize Offset = 0;
  VkDeviceSize Stride = sizeof(float) * 4;
  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindVertexBuffers2EXT(Cmd, 0, 1, &InstanceBuffer, &Offset, nullptr,
                             &Stride);
  vkCmdDraw(Cmd, 3, 1, 0, 1);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // The dynamic stride (not the pipeline's wrong static one) picked out
  // instance 1's color: green.
  EXPECT_EQ(texel(2, 2)[0], 0x00);
  EXPECT_EQ(texel(2, 2)[1], 0xFF);
  EXPECT_EQ(texel(2, 2)[2], 0x00);

  vkDestroyBuffer(Device, InstanceBuffer, nullptr);
  vkFreeMemory(Device, InstanceMemory, nullptr);
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

/// The driver advertises `VK_KHR_dynamic_rendering` and
/// `VK_EXT_extended_dynamic_state` (roadmap C4c) and accepts either at
/// device creation; anything it does not implement is still refused.
TEST_F(DrawTest, AdvertisesDynamicRenderingExtension) {
  uint32_t Count = 0;
  ASSERT_EQ(
      vkEnumerateDeviceExtensionProperties(Physical, nullptr, &Count, nullptr),
      VK_SUCCESS);
  ASSERT_EQ(Count, 2u);
  std::vector<VkExtensionProperties> Properties(Count);
  ASSERT_EQ(vkEnumerateDeviceExtensionProperties(Physical, nullptr, &Count,
                                                 Properties.data()),
            VK_SUCCESS);
  auto HasExtension = [&](const char *Name) {
    for (const VkExtensionProperties &P : Properties)
      if (std::strcmp(P.extensionName, Name) == 0)
        return true;
    return false;
  };
  EXPECT_TRUE(HasExtension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME));
  EXPECT_TRUE(HasExtension(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME));

  VkPhysicalDeviceDynamicRenderingFeatures Features{};
  Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT ExtDynState{};
  ExtDynState.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
  Features.pNext = &ExtDynState;
  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &Features;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(Features.dynamicRendering, VK_TRUE);
  EXPECT_EQ(ExtDynState.extendedDynamicState, VK_TRUE);

  const char *Enabled = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
  VkDeviceCreateInfo DevInfo{};
  DevInfo.enabledExtensionCount = 1;
  DevInfo.ppEnabledExtensionNames = &Enabled;
  VkDevice Second = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Second), VK_SUCCESS);
  vkDestroyDevice(Second, nullptr);

  Enabled = VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME;
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

/// `DepthState::TestEnable`/`WriteEnable`: a nearer draw's depth write
/// (`CompareOp::Less`) rejects a farther draw covering the same pixels, so
/// the nearer draw's color survives -- the completion scenario's own "depth"
/// bullet (see "V6: Graphics queue and basic rendering" in
/// feme/docs/FeMeVulkanDesign.md).
TEST_F(DrawTest, RendersWithDepthTest) {
  VkImage DepthImage = VK_NULL_HANDLE;
  VkImageView DepthView = VK_NULL_HANDLE;
  VkDeviceMemory DepthMemory = VK_NULL_HANDLE;
  createImageAndView(
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
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, DepthView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  VkPipelineDepthStencilStateCreateInfo DepthStencil{};
  DepthStencil.depthTestEnable = VK_TRUE;
  DepthStencil.depthWriteEnable = VK_TRUE;
  DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

  auto makePipeline = [&](llvm::StringRef VertexSource,
                          llvm::StringRef FragmentSource) {
    VkShaderModule Vertex = createModule(VertexSource);
    VkShaderModule Fragment = createModule(FragmentSource);
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
    Info.pDepthStencilState = &DepthStencil;
    Info.pColorBlendState = &Blend;
    Info.layout = Layout;
    Info.renderPass = LocalPass;
    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                        nullptr, &Pipe),
              VK_SUCCESS);
    vkDestroyShaderModule(Device, Fragment, nullptr);
    vkDestroyShaderModule(Device, Vertex, nullptr);
    return Pipe;
  };
  VkPipeline NearRed = makePipeline(NearDepthVertexSource, RedFragmentSource);
  VkPipeline FarGreen = makePipeline(FarDepthVertexSource, GreenFragmentSource);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  // The nearer (red) draw first, writing depth 0.2; the farther (green)
  // draw second, rejected by the depth test since 0.8 is not less than 0.2.
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, NearRed);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, FarGreen);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0x00) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, FarGreen, nullptr);
  vkDestroyPipeline(Device, NearRed, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, DepthView, nullptr);
  vkDestroyImage(Device, DepthImage, nullptr);
  vkFreeMemory(Device, DepthMemory, nullptr);
}

/// `StencilState::TestEnable`: a first draw (`CompareOp::Always`,
/// `StencilOp::Replace`) writes a stencil reference over half the render
/// area; a second draw (`CompareOp::Equal`) then only reaches the half whose
/// stencil value matches -- the completion scenario's own "stencil" bullet.
TEST_F(DrawTest, RendersWithStencilTest) {
  VkImage StencilImage = VK_NULL_HANDLE;
  VkImageView StencilView = VK_NULL_HANDLE;
  VkDeviceMemory StencilMemory = VK_NULL_HANDLE;
  createImageAndView(
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
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, StencilView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  auto makePipeline = [&](llvm::StringRef FragmentSource,
                          const VkRect2D &Scissor, VkCompareOp Compare,
                          VkStencilOp PassOp) {
    VkShaderModule Vertex = createModule(FullscreenVertexSource);
    VkShaderModule Fragment = createModule(FragmentSource);
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
    VkRect2D LocalScissor = Scissor;
    VkPipelineViewportStateCreateInfo ViewportState{};
    ViewportState.viewportCount = 1;
    ViewportState.pViewports = &Viewport;
    ViewportState.scissorCount = 1;
    ViewportState.pScissors = &LocalScissor;
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
    VkStencilOpState Face{};
    Face.failOp = VK_STENCIL_OP_KEEP;
    Face.passOp = PassOp;
    Face.depthFailOp = VK_STENCIL_OP_KEEP;
    Face.compareOp = Compare;
    Face.compareMask = 0xFF;
    Face.writeMask = 0xFF;
    Face.reference = 1;
    VkPipelineDepthStencilStateCreateInfo DepthStencil{};
    DepthStencil.stencilTestEnable = VK_TRUE;
    DepthStencil.front = Face;
    DepthStencil.back = Face;
    VkGraphicsPipelineCreateInfo Info{};
    Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    Info.stageCount = 2;
    Info.pStages = Stages;
    Info.pVertexInputState = &VertexInput;
    Info.pInputAssemblyState = &InputAssembly;
    Info.pViewportState = &ViewportState;
    Info.pRasterizationState = &Raster;
    Info.pMultisampleState = &Multisample;
    Info.pDepthStencilState = &DepthStencil;
    Info.pColorBlendState = &Blend;
    Info.layout = Layout;
    Info.renderPass = LocalPass;
    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                        nullptr, &Pipe),
              VK_SUCCESS);
    vkDestroyShaderModule(Device, Fragment, nullptr);
    vkDestroyShaderModule(Device, Vertex, nullptr);
    return Pipe;
  };
  // Writer: always passes, replaces stencil with 1, restricted to the left
  // half of the render area.
  VkPipeline Writer =
      makePipeline(RedFragmentSource, VkRect2D{{0, 0}, {Extent / 2, Extent}},
                   VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_REPLACE);
  // Tester: only passes where stencil already equals 1, covering the whole
  // render area.
  VkPipeline Tester =
      makePipeline(GreenFragmentSource, VkRect2D{{0, 0}, {Extent, Extent}},
                   VK_COMPARE_OP_EQUAL, VK_STENCIL_OP_KEEP);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 1.0f, 1.0f}};
  ClearValues[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Writer);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Tester);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Left half: the writer's stencil (1) matched the tester's reference, so
  // the tester's green landed. Right half: stencil stayed 0, so the tester
  // was rejected and the clear color (blue) survives.
  EXPECT_EQ(texel(0, 0)[1], 0xFF);
  EXPECT_EQ(texel(1, 3)[1], 0xFF);
  EXPECT_EQ(texel(2, 0)[2], 0xFF);
  EXPECT_EQ(texel(3, 3)[2], 0xFF);

  vkDestroyPipeline(Device, Tester, nullptr);
  vkDestroyPipeline(Device, Writer, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, StencilView, nullptr);
  vkDestroyImage(Device, StencilImage, nullptr);
  vkFreeMemory(Device, StencilMemory, nullptr);
}

/// Roadmap C1 ("Mandatory formats"): a combined `D24_UNORM_S8_UINT`
/// attachment shares one word of storage between its depth and stencil
/// halves, so the completion scenario is that writing one half through
/// `vkCmdDraw` never corrupts the other. One draw enables both depth and
/// stencil testing/writes together (depth 0.2, stencil 1); a second,
/// depth-only draw at depth 0.8 must still fail (`LESS`: 0.8 is not less
/// than 0.2, so the depth half survived); a third, stencil-only draw
/// (`EQUAL` against 1) must still pass (so the stencil half survived the
/// first draw's depth write) and its green lands last.
TEST_F(DrawTest, RendersWithCombinedDepthStencilAttachment) {
  VkImage DepthStencilImage = VK_NULL_HANDLE;
  VkImageView DepthStencilView = VK_NULL_HANDLE;
  VkDeviceMemory DepthStencilMemory = VK_NULL_HANDLE;
  createImageAndView(VK_FORMAT_D24_UNORM_S8_UINT,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                     DepthStencilImage, DepthStencilView, DepthStencilMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_D24_UNORM_S8_UINT;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference DepthStencilRef{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pDepthStencilAttachment = &DepthStencilRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, DepthStencilView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  auto makePipeline =
      [&](llvm::StringRef VertexSource, llvm::StringRef FragmentSource,
          const VkPipelineDepthStencilStateCreateInfo &DepthStencil) {
        VkShaderModule Vertex = createModule(VertexSource);
        VkShaderModule Fragment = createModule(FragmentSource);
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
        VkViewport Viewport{0.0f,          0.0f, float(Extent),
                            float(Extent), 0.0f, 1.0f};
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
        VkPipelineDepthStencilStateCreateInfo LocalDepthStencil = DepthStencil;
        VkGraphicsPipelineCreateInfo Info{};
        Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        Info.stageCount = 2;
        Info.pStages = Stages;
        Info.pVertexInputState = &VertexInput;
        Info.pInputAssemblyState = &InputAssembly;
        Info.pViewportState = &ViewportState;
        Info.pRasterizationState = &Raster;
        Info.pMultisampleState = &Multisample;
        Info.pDepthStencilState = &LocalDepthStencil;
        Info.pColorBlendState = &Blend;
        Info.layout = Layout;
        Info.renderPass = LocalPass;
        VkPipeline Pipe = VK_NULL_HANDLE;
        EXPECT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                            nullptr, &Pipe),
                  VK_SUCCESS);
        vkDestroyShaderModule(Device, Fragment, nullptr);
        vkDestroyShaderModule(Device, Vertex, nullptr);
        return Pipe;
      };

  // Draw 1: writes depth (0.2, `ALWAYS`) and stencil (1, `REPLACE` on
  // `ALWAYS`) together, in one draw against the combined attachment.
  VkStencilOpState WriteFace{};
  WriteFace.failOp = VK_STENCIL_OP_KEEP;
  WriteFace.passOp = VK_STENCIL_OP_REPLACE;
  WriteFace.depthFailOp = VK_STENCIL_OP_KEEP;
  WriteFace.compareOp = VK_COMPARE_OP_ALWAYS;
  WriteFace.compareMask = 0xFF;
  WriteFace.writeMask = 0xFF;
  WriteFace.reference = 1;
  VkPipelineDepthStencilStateCreateInfo WriteState{};
  WriteState.depthTestEnable = VK_TRUE;
  WriteState.depthWriteEnable = VK_TRUE;
  WriteState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  WriteState.stencilTestEnable = VK_TRUE;
  WriteState.front = WriteFace;
  WriteState.back = WriteFace;
  VkPipeline Writer =
      makePipeline(NearDepthVertexSource, RedFragmentSource, WriteState);

  // Draw 2: depth-only (`LESS`), no stencil test -- must still fail since
  // 0.8 is not less than the depth draw 1 stored (0.2), proving the
  // stencil write did not corrupt the depth half of the shared word.
  VkPipelineDepthStencilStateCreateInfo DepthOnlyState{};
  DepthOnlyState.depthTestEnable = VK_TRUE;
  DepthOnlyState.depthWriteEnable = VK_TRUE;
  DepthOnlyState.depthCompareOp = VK_COMPARE_OP_LESS;
  VkPipeline DepthBlocked =
      makePipeline(FarDepthVertexSource, GreenFragmentSource, DepthOnlyState);

  // Draw 3: stencil-only (`EQUAL` against 1), no depth test -- must still
  // pass since draw 1 left stencil at 1, proving the depth write did not
  // corrupt the stencil half.
  VkStencilOpState TestFace{};
  TestFace.failOp = VK_STENCIL_OP_KEEP;
  TestFace.passOp = VK_STENCIL_OP_KEEP;
  TestFace.depthFailOp = VK_STENCIL_OP_KEEP;
  TestFace.compareOp = VK_COMPARE_OP_EQUAL;
  TestFace.compareMask = 0xFF;
  TestFace.writeMask = 0xFF;
  TestFace.reference = 1;
  VkPipelineDepthStencilStateCreateInfo StencilOnlyState{};
  StencilOnlyState.stencilTestEnable = VK_TRUE;
  StencilOnlyState.front = TestFace;
  StencilOnlyState.back = TestFace;
  VkPipeline StencilPassed = makePipeline(
      FullscreenVertexSource, GreenFragmentSource, StencilOnlyState);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Writer);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, DepthBlocked);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, StencilPassed);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, StencilPassed, nullptr);
  vkDestroyPipeline(Device, DepthBlocked, nullptr);
  vkDestroyPipeline(Device, Writer, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, DepthStencilView, nullptr);
  vkDestroyImage(Device, DepthStencilImage, nullptr);
  vkFreeMemory(Device, DepthStencilMemory, nullptr);
}

/// `BlendState::BlendEnable`: a half-alpha fragment source-over-blends with
/// the attachment's existing (clear) color, rather than replacing it -- the
/// completion scenario's own "blending" bullet.
TEST_F(DrawTest, RendersWithAlphaBlending) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(HalfAlphaRedFragmentSource);

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
  BlendAttachment.blendEnable = VK_TRUE;
  BlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  BlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  BlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  BlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  BlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  BlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
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
  Info.renderPass = Pass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 1.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // 0.5*red + 0.5*blue (clear) = (0.5, 0, 0.5); 0.5 rounds to 0x80 in
  // `R8G8B8A8_UNORM`.
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0x80) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[2], 0x80) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Dual-source blend factors (roadmap C4, `VK_BLEND_FACTOR_SRC1_*`): a
/// real SPIR-V fragment stage with an `Index=1` output at the same
/// `Location=0` as its ordinary one (`DualSourceFragmentSource`), whose
/// `Index` decoration survives `spirv` -> `llvm` conversion
/// (`feme::spirv::attachStageIODecorations`) and gets reflected into
/// `SignatureElement::Index` (`CanonicalizeStage.cpp`'s
/// `parseSPIRVDecorations`). `SrcColorFactor`/`SrcAlphaFactor` of
/// `Src1Color`/`Src1Alpha` with `DstColorFactor`/`DstAlphaFactor` of
/// `Zero` isolates the `Index=1` output's (0.25, 0.5, 0.75, 1.0) in the
/// result, exactly like
/// `ExecutorTest.DualSourceBlendReadsTheSecondFragmentOutput` but end to end
/// through real SPIR-V rather than a hand-built `EntrySignature`.
TEST_F(DrawTest, RendersWithDualSourceBlending) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(DualSourceFragmentSource);

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
  BlendAttachment.blendEnable = VK_TRUE;
  BlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC1_COLOR;
  BlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  BlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  BlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC1_ALPHA;
  BlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  BlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
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
  Info.renderPass = Pass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // result = 1*Src1Color + 0*DstColor = (0.25, 0.5, 0.75), rounding to
  // (0x40, 0x80, 0xBF) in `R8G8B8A8_UNORM`.
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_NEAR(Texel[0], 0x40, 2) << "at (" << X << ", " << Y << ")";
      EXPECT_NEAR(Texel[1], 0x80, 2) << "at (" << X << ", " << Y << ")";
      EXPECT_NEAR(Texel[2], 0xBF, 2) << "at (" << X << ", " << Y << ")";
      EXPECT_NEAR(Texel[3], 0xFF, 2) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Multiple color attachments: a fragment stage with two `Output`s writes
/// distinct colors to each, and both land in their own attachment -- the
/// completion scenario's own "MRT" bullet.
TEST_F(DrawTest, RendersToMultipleColorAttachments) {
  VkImage SecondImage = VK_NULL_HANDLE;
  VkImageView SecondView = VK_NULL_HANDLE;
  VkDeviceMemory SecondMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, SecondImage, SecondView, SecondMemory);

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
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, SecondView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(DualOutputFragmentSource);
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
  VkPipelineColorBlendAttachmentState BlendAttachments[2]{};
  BlendAttachments[0].colorWriteMask = 0xF;
  BlendAttachments[1].colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 2;
  Blend.pAttachments = BlendAttachments;
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
  Info.renderPass = LocalPass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel0 = texelOf(ColorImage, X, Y);
      std::array<uint8_t, 4> Texel1 = texelOf(SecondImage, X, Y);
      EXPECT_EQ(Texel0[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel0[1], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel1[0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel1[1], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, SecondView, nullptr);
  vkDestroyImage(Device, SecondImage, nullptr);
  vkFreeMemory(Device, SecondMemory, nullptr);
}

TEST_F(DrawTest, OcclusionQueryCountsPassedSamples) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkQueryPoolCreateInfo QueryInfo{};
  QueryInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
  QueryInfo.queryCount = 1;
  VkQueryPool QueryPool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateQueryPool(Device, &QueryInfo, nullptr, &QueryPool),
            VK_SUCCESS);

  beginRenderPass({{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdResetQueryPool(Cmd, QueryPool, 0, 1);
  vkCmdBeginQuery(Cmd, QueryPool, 0, 0);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndQuery(Cmd, QueryPool, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  uint64_t Results[2] = {0, 0};
  EXPECT_EQ(vkGetQueryPoolResults(Device, QueryPool, 0, 1, sizeof(Results),
                                  Results, 2 * sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT |
                                      VK_QUERY_RESULT_WITH_AVAILABILITY_BIT),
            VK_SUCCESS);
  EXPECT_EQ(Results[0], uint64_t(Extent * Extent));
  EXPECT_EQ(Results[1], 1u);

  vkDestroyQueryPool(Device, QueryPool, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// A multisample color attachment with a resolve attachment: a draw fully
/// covering the render area resolves to a uniform color in the
/// single-sample target -- the completion scenario's own "multisample
/// resolves" bullet.
TEST_F(DrawTest, ResolvesMultisampleColorDuringRenderPass) {
  VkImage MSImage = VK_NULL_HANDLE;
  VkImageView MSView = VK_NULL_HANDLE;
  VkDeviceMemory MSMemory = VK_NULL_HANDLE;
  createImageAndView(VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, MSImage, MSView, MSMemory,
                     VK_SAMPLE_COUNT_4_BIT);
  VkImage ResolveImage = VK_NULL_HANDLE;
  VkImageView ResolveView = VK_NULL_HANDLE;
  VkDeviceMemory ResolveMemory = VK_NULL_HANDLE;
  createImageAndView(
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
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {MSView, ResolveView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

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
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
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
  Info.renderPass = LocalPass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Every sample of every covered pixel is the same solid red, so the
  // resolve target's box-filtered average is exactly red too.
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texelOf(ResolveImage, X, Y);
      EXPECT_EQ(Texel[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[3], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, ResolveView, nullptr);
  vkDestroyImage(Device, ResolveImage, nullptr);
  vkFreeMemory(Device, ResolveMemory, nullptr);
  vkDestroyImageView(Device, MSView, nullptr);
  vkDestroyImage(Device, MSImage, nullptr);
  vkFreeMemory(Device, MSMemory, nullptr);
}

} // namespace
