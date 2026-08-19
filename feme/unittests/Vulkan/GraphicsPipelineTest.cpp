//===- GraphicsPipelineTest.cpp - vkCreateGraphicsPipelines tests -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (V6) Covers graphics stage compilation and pipeline state translation:
// real SPIR-V vertex/fragment modules compiled into `feme::cpu::
// CompiledStage`s, their cross-stage interface validated against the core
// reflection, and every state combination with no implemented path rejected
// at creation rather than at draw time (see "Graphics pipeline state" in
// feme/docs/FeMeVulkanDesign.md).
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "GraphicsPipeline.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"
#include "RenderPass.h"

#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/SPIRV/Serialization.h"

#include "gtest/gtest.h"

#include <string>
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

/// A vertex stage selecting one of three hard-coded oversized-triangle
/// corners from `gl_VertexIndex`, exactly like the executor's own
/// `feme-render` triangle fixture -- no vertex buffer needed.
constexpr llvm::StringLiteral VertexSource = R"mlir(
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

/// A fragment stage writing solid red to location 0 (SV_Target0).
constexpr llvm::StringLiteral FragmentSource = R"mlir(
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

class GraphicsPipelineTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);

    VkPipelineLayoutCreateInfo LayoutInfo{};
    ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout),
              VK_SUCCESS);

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

    // (roadmap C4c) A second render pass, identical but for a depth
    // attachment: used only by tests exercising a dynamic depth/stencil
    // state, which -- unlike the fixture's other tests -- needs one
    // declared for the pipeline to legally test/write into.
    VkAttachmentDescription DepthAttachment{};
    DepthAttachment.format = VK_FORMAT_D32_SFLOAT;
    DepthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    DepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkAttachmentReference DepthRef{
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentDescription DepthAttachments[2] = {Attachment, DepthAttachment};
    VkSubpassDescription DepthSubpass{};
    DepthSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    DepthSubpass.colorAttachmentCount = 1;
    DepthSubpass.pColorAttachments = &ColorRef;
    DepthSubpass.pDepthStencilAttachment = &DepthRef;
    VkRenderPassCreateInfo DepthPassInfo{};
    DepthPassInfo.attachmentCount = 2;
    DepthPassInfo.pAttachments = DepthAttachments;
    DepthPassInfo.subpassCount = 1;
    DepthPassInfo.pSubpasses = &DepthSubpass;
    ASSERT_EQ(vkCreateRenderPass(Device, &DepthPassInfo, nullptr, &PassWithDepth),
              VK_SUCCESS);
  }

  void TearDown() override {
    vkDestroyRenderPass(Device, Pass, nullptr);
    vkDestroyRenderPass(Device, PassWithDepth, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
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

  /// A fully populated `VkGraphicsPipelineCreateInfo` over the fixture's
  /// render pass, with every state block at its supported default. The
  /// caller may mutate the state structures (kept alive as members) before
  /// calling `create`.
  VkGraphicsPipelineCreateInfo makeCreateInfo(VkShaderModule Vertex,
                                              VkShaderModule Fragment) {
    Stages[0] = {};
    Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = Vertex;
    Stages[0].pName = "main";
    Stages[1] = {};
    Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = Fragment;
    Stages[1].pName = "main";

    VertexInput = {};
    InputAssembly = {};
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    Viewport = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
    Scissor = {{0, 0}, {4, 4}};
    ViewportState = {};
    ViewportState.viewportCount = 1;
    ViewportState.pViewports = &Viewport;
    ViewportState.scissorCount = 1;
    ViewportState.pScissors = &Scissor;
    Raster = {};
    Raster.cullMode = VK_CULL_MODE_NONE;
    Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    Raster.lineWidth = 1.0f;
    Multisample = {};
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    BlendAttachment = {};
    BlendAttachment.colorWriteMask = 0xF;
    Blend = {};
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
    return Info;
  }

  VkResult create(const VkGraphicsPipelineCreateInfo &Info, VkPipeline &Out,
                  VkPipelineCache Cache = VK_NULL_HANDLE) {
    return vkCreateGraphicsPipelines(Device, Cache, 1, &Info, nullptr, &Out);
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkRenderPass Pass = VK_NULL_HANDLE;
  VkRenderPass PassWithDepth = VK_NULL_HANDLE;

  VkPipelineShaderStageCreateInfo Stages[2]{};
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  VkViewport Viewport{};
  VkRect2D Scissor{};
  VkPipelineViewportStateCreateInfo ViewportState{};
  VkPipelineRasterizationStateCreateInfo Raster{};
  VkPipelineMultisampleStateCreateInfo Multisample{};
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  VkPipelineColorBlendStateCreateInfo Blend{};
};

TEST_F(GraphicsPipelineTest, CompilesVertexAndFragmentStages) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Obj = fromHandle<Pipeline>(Pipe);
  ASSERT_EQ(Obj->kind(), Pipeline::Kind::Graphics);
  auto *Graphics = static_cast<GraphicsPipeline *>(Obj);
  EXPECT_EQ(Graphics->colorAttachmentCount(), 1u);
  EXPECT_EQ(Graphics->sampleCount(), 1u);
  EXPECT_FALSE(Graphics->needsDepthAttachment());
  EXPECT_EQ(Graphics->vertexStage().getStage(), feme::ShaderStage::Vertex);
  EXPECT_EQ(Graphics->fragmentStage().getStage(), feme::ShaderStage::Fragment);

  // The executor pipeline this builds per draw carries the translated
  // state, one blend state per color attachment.
  DynamicGraphicsState Dynamic;
  feme::graphics::GraphicsPipeline Executor =
      Graphics->buildExecutorPipeline(Dynamic);
  EXPECT_EQ(Executor.getTopology(),
            feme::graphics::PrimitiveTopology::TriangleList);
  EXPECT_EQ(Executor.getColorBlends().size(), 1u);
  EXPECT_EQ(Graphics->resolveViewport(Dynamic).Width, 4.0f);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

TEST_F(GraphicsPipelineTest, RejectsUnimplementedStateCombinations) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipeline Pipe = VK_NULL_HANDLE;

  // An unimplemented topology.
  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  // Rasterizer discard.
  Info = makeCreateInfo(Vertex, Fragment);
  Raster.rasterizerDiscardEnable = VK_TRUE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  // Multiple viewports.
  Info = makeCreateInfo(Vertex, Fragment);
  ViewportState.viewportCount = 2;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  // A dynamic state with no implemented path.
  Info = makeCreateInfo(Vertex, Fragment);
  VkDynamicState Unsupported = VK_DYNAMIC_STATE_DEPTH_BIAS;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Unsupported;
  Info.pDynamicState = &DynamicInfo;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  // Depth testing with no depth attachment in the render target.
  Info = makeCreateInfo(Vertex, Fragment);
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthTestEnable = VK_TRUE;
  DepthInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  Info.pDepthStencilState = &DepthInfo;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  // A stage this milestone does not compile.
  Info = makeCreateInfo(Vertex, Fragment);
  Stages[1].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  // Primitive restart with a list topology: only strip topologies restart.
  Info = makeCreateInfo(Vertex, Fragment);
  InputAssembly.primitiveRestartEnable = VK_TRUE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Primitive restart is implemented for `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_
/// STRIP`, and the pipeline records it for the executor to honor.
TEST_F(GraphicsPipelineTest, AcceptsPrimitiveRestartOnTriangleStrip) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  InputAssembly.primitiveRestartEnable = VK_TRUE;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  feme::graphics::GraphicsPipeline Executor =
      Graphics->buildExecutorPipeline(Dynamic);
  EXPECT_TRUE(Executor.getPrimitiveRestartEnable());

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// `VK_CULL_MODE_FRONT_AND_BACK` is a legal `VkCullModeFlags` value (it
/// culls every primitive of the pipeline's topology, "no representation
/// for it" no longer describes this executor -- see
/// `feme::graphics::CullMode::FrontAndBack`), so pipeline creation must
/// accept it rather than fail.
TEST_F(GraphicsPipelineTest, AcceptsFrontAndBackCulling) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.cullMode = VK_CULL_MODE_FRONT_AND_BACK;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  EXPECT_EQ(Graphics->buildExecutorPipeline(DynamicGraphicsState{})
                .getRasterState()
                .Cull,
            feme::graphics::CullMode::FrontAndBack);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap C4c) `VK_DYNAMIC_STATE_CULL_MODE`/`VK_DYNAMIC_STATE_FRONT_FACE`:
/// a pipeline may now declare either dynamic, and `buildExecutorPipeline`
/// must then read the per-draw snapshot rather than this pipeline's own
/// (here, deliberately mismatched) creation-time value.
TEST_F(GraphicsPipelineTest, DynamicCullModeAndFrontFaceOverrideStaticState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.cullMode = VK_CULL_MODE_BACK_BIT;
  Raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
  VkDynamicState DynStates[2] = {VK_DYNAMIC_STATE_CULL_MODE,
                                 VK_DYNAMIC_STATE_FRONT_FACE};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 2;
  DynamicInfo.pDynamicStates = DynStates;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  Dynamic.Cull = feme::graphics::CullMode::FrontAndBack;
  Dynamic.Front = feme::graphics::FrontFace::CounterClockwise;
  feme::graphics::RasterState Resolved =
      Graphics->buildExecutorPipeline(Dynamic).getRasterState();
  EXPECT_EQ(Resolved.Cull, feme::graphics::CullMode::FrontAndBack);
  EXPECT_EQ(Resolved.Front, feme::graphics::FrontFace::CounterClockwise);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap C4c) A pipeline declaring `VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE`
/// (or `_WRITE_ENABLE`) dynamic may still enable the test at draw time even
/// though its own static `depthTestEnable`/`depthWriteEnable` are both
/// `VK_FALSE` -- so it still needs a depth attachment in its render target,
/// exactly like a pipeline whose *static* fields already enable the test.
TEST_F(GraphicsPipelineTest, DynamicDepthTestEnableRequiresDepthAttachment) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Dynamic;
  Info.pDynamicState = &DynamicInfo;
  // `Info.renderPass` (set by `makeCreateInfo`) is the fixture's
  // depth-less `Pass`.

  VkPipeline Pipe = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// The same three dynamic states, this time over `PassWithDepth`: creation
/// succeeds despite a static depth-stencil state with the test disabled
/// (and, deliberately, `depthCompareOp` left at its zero-initialized
/// `VK_COMPARE_OP_NEVER`, which `translateDepthStencilState` never even
/// looks at while `DynamicStateDepthCompareOp` is set), and
/// `buildExecutorPipeline` resolves depth state from the per-draw snapshot.
TEST_F(GraphicsPipelineTest, DynamicDepthStateOverridesStaticState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.renderPass = PassWithDepth;
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  Info.pDepthStencilState = &DepthInfo;
  VkDynamicState DynStates[3] = {VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
                                 VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                                 VK_DYNAMIC_STATE_DEPTH_COMPARE_OP};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 3;
  DynamicInfo.pDynamicStates = DynStates;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  Dynamic.DepthTestEnable = true;
  Dynamic.DepthWriteEnable = true;
  Dynamic.DepthCompare = feme::graphics::CompareOp::Greater;
  feme::graphics::DepthState Resolved =
      Graphics->buildExecutorPipeline(Dynamic).getDepthState();
  EXPECT_TRUE(Resolved.TestEnable);
  EXPECT_TRUE(Resolved.WriteEnable);
  EXPECT_EQ(Resolved.Compare, feme::graphics::CompareOp::Greater);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap C4c) `VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE`/`_STENCIL_OP`: a
/// depth-less-but-stencil-attached render target is enough (stencil
/// testing needs its own `S8_UINT` attachment, not a depth one), and a
/// pipeline created with stencil testing statically disabled still
/// resolves the dynamically-enabled test and its ops.
TEST_F(GraphicsPipelineTest, DynamicStencilStateOverridesStaticState) {
  // A render pass with an `S8_UINT`-only depth-stencil attachment (see
  // `isSupportedStencilAttachmentFormat`), distinct from the fixture's own
  // `PassWithDepth` (`D32_SFLOAT`, which has no stencil aspect at all).
  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentDescription StencilAttachment{};
  StencilAttachment.format = VK_FORMAT_S8_UINT;
  StencilAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  StencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  StencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentDescription Attachments[2] = {Attachment, StencilAttachment};
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
  VkRenderPass PassWithStencil = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &PassWithStencil),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.renderPass = PassWithStencil;
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  Info.pDepthStencilState = &DepthInfo;
  VkDynamicState DynStates[2] = {VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
                                 VK_DYNAMIC_STATE_STENCIL_OP};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 2;
  DynamicInfo.pDynamicStates = DynStates;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  Dynamic.StencilTestEnable = true;
  Dynamic.StencilOps[0].FailOp = feme::graphics::StencilOp::Replace;
  Dynamic.StencilOps[0].PassOp = feme::graphics::StencilOp::IncrementClamp;
  Dynamic.StencilOps[0].DepthFailOp = feme::graphics::StencilOp::Zero;
  Dynamic.StencilOps[0].Compare = feme::graphics::CompareOp::Equal;
  feme::graphics::StencilState Resolved =
      Graphics->buildExecutorPipeline(Dynamic).getStencilState();
  EXPECT_TRUE(Resolved.TestEnable);
  EXPECT_EQ(Resolved.Front.FailOp, feme::graphics::StencilOp::Replace);
  EXPECT_EQ(Resolved.Front.PassOp, feme::graphics::StencilOp::IncrementClamp);
  EXPECT_EQ(Resolved.Front.DepthFailOp, feme::graphics::StencilOp::Zero);
  EXPECT_EQ(Resolved.Front.Compare, feme::graphics::CompareOp::Equal);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyRenderPass(Device, PassWithStencil, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// A fragment stage writing no `SV_Target0` cannot fill the render pass's
/// one color attachment; the mismatch is a creation failure, not a draw-time
/// surprise.
TEST_F(GraphicsPipelineTest, RejectsMissingFragmentOutput) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir");
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline Pipe = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// A fragment input at a location no vertex output writes is a mislinked
/// varying; cross-stage interface matching catches it at creation.
TEST_F(GraphicsPipelineTest, RejectsUnmatchedVarying) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @in_var {location = 3 : i32} : !spirv.ptr<vector<4xf32>, Input>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %ip = spirv.mlir.addressof @in_var : !spirv.ptr<vector<4xf32>, Input>
    %v = spirv.Load "Input" %ip : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %v : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @in_var, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir");
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline Pipe = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// A dynamic-rendering pipeline names its attachment formats through a
/// chained `VkPipelineRenderingCreateInfo` instead of a `VkRenderPass`, and
/// normalizes into exactly the same translated state.
TEST_F(GraphicsPipelineTest, AcceptsDynamicRenderingFormats) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 1;
  Rendering.pColorAttachmentFormats = &ColorFormat;
  Info.renderPass = VK_NULL_HANDLE;
  Info.pNext = &Rendering;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  EXPECT_EQ(static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe))
                ->colorAttachmentCount(),
            1u);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap C1 ("Mandatory formats"): every format
/// `isSupportedColorAttachmentFormat` grants Vulkan 1.2's mandatory
/// `COLOR_ATTACHMENT_BIT | COLOR_ATTACHMENT_BLEND_BIT` status to must build
/// a pipeline the same way `VK_FORMAT_R8G8B8A8_UNORM` already does.
TEST_F(GraphicsPipelineTest, AcceptsMandatoryColorAttachmentFormats) {
  for (VkFormat Format :
       {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_R16G16B16A16_SFLOAT}) {
    VkShaderModule Vertex = createModule(VertexSource);
    VkShaderModule Fragment = createModule(FragmentSource);

    VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
    VkPipelineRenderingCreateInfo Rendering{};
    Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    Rendering.colorAttachmentCount = 1;
    Rendering.pColorAttachmentFormats = &Format;
    Info.renderPass = VK_NULL_HANDLE;
    Info.pNext = &Rendering;

    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(create(Info, Pipe), VK_SUCCESS) << "format " << Format;
    vkDestroyPipeline(Device, Pipe, nullptr);
    vkDestroyShaderModule(Device, Fragment, nullptr);
    vkDestroyShaderModule(Device, Vertex, nullptr);
  }
}

/// An identical `VkGraphicsPipelineCreateInfo` (same SPIR-V, same layout,
/// same fixed-function state) creates a cache hit that shares the compiled
/// stages rather than recompiling them.
TEST_F(GraphicsPipelineTest, CachedPipelineSharesCompiledStages) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline First = VK_NULL_HANDLE, Second = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, First, Cache), VK_SUCCESS);
  ASSERT_EQ(create(Info, Second, Cache), VK_SUCCESS);

  auto *FirstPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(First));
  auto *SecondPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Second));
  EXPECT_EQ(&FirstPipe->vertexStage(), &SecondPipe->vertexStage());
  EXPECT_EQ(&FirstPipe->fragmentStage(), &SecondPipe->fragmentStage());

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Without a `VkPipelineCache`, two otherwise-identical creations compile
/// independent artifacts.
TEST_F(GraphicsPipelineTest, NoCacheCompilesIndependentStagesEachTime) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline First = VK_NULL_HANDLE, Second = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, First), VK_SUCCESS);
  ASSERT_EQ(create(Info, Second), VK_SUCCESS);

  auto *FirstPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(First));
  auto *SecondPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Second));
  EXPECT_NE(&FirstPipe->vertexStage(), &SecondPipe->vertexStage());

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Two pipelines built from the same SPIR-V but disagreeing fixed-function
/// state (here, cull mode) must not share a cache entry: the key covers the
/// whole normalized pipeline description, not only the two stages' bytes.
TEST_F(GraphicsPipelineTest, DifferingFixedFunctionStateIsACacheMiss) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline First = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, First, Cache), VK_SUCCESS);

  Info = makeCreateInfo(Vertex, Fragment);
  Raster.cullMode = VK_CULL_MODE_BACK_BIT;
  VkPipeline Second = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Second, Cache), VK_SUCCESS);

  auto *FirstPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(First));
  auto *SecondPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Second));
  EXPECT_NE(&FirstPipe->vertexStage(), &SecondPipe->vertexStage());

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

} // namespace
