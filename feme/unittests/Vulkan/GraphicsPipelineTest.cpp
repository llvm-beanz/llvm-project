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

/// (Roadmap H2b) A fragment stage with no *color* output at all -- only a
/// `gl_FragDepth` write -- the shape `dEQP-VK.multiview.depth_without_
/// fragment_shader`'s own depth-only pipeline uses.
constexpr llvm::StringLiteral NoColorOutputFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @depth built_in("FragDepth") : !spirv.ptr<f32, Output>
  spirv.func @main() -> () "None" {
    %d = spirv.Constant 0.5 : f32
    %p = spirv.mlir.addressof @depth : !spirv.ptr<f32, Output>
    spirv.Store "Output" %p, %d : f32
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @depth
  spirv.ExecutionMode @main "OriginUpperLeft"
  spirv.ExecutionMode @main "DepthReplacing"
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

/// Roadmap F9 (`VK_EXT_pipeline_protected_access`): the extension's two
/// restriction bits apply to a graphics pipeline exactly like a compute one
/// (`Pipeline` is their common base -- see `PipelineTest.
/// Accepts{No,ProtectedAccessOnly}CreateFlag`'s compute-side coverage) --
/// creation records the flag verbatim on `Pipeline::createFlags`.
TEST_F(GraphicsPipelineTest, RecordsProtectedAccessCreateFlags) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.flags = VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  EXPECT_EQ(fromHandle<Pipeline>(Pipe)->createFlags() &
                VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT,
            static_cast<VkPipelineCreateFlags>(
                VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT));

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap E19 (`VK_EXT_pipeline_creation_feedback`): two stages (vertex +
/// fragment) get two feedback slots, both `VALID_BIT`-only on a cache
/// miss, matching `PipelineTest.ReportsPipelineCreationFeedback`'s compute
/// counterpart.
TEST_F(GraphicsPipelineTest, ReportsPipelineCreationFeedback) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipelineCreationFeedback Feedback{};
  VkPipelineCreationFeedback StageFeedbacks[2]{};
  VkPipelineCreationFeedbackCreateInfo FeedbackInfo{};
  FeedbackInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO;
  FeedbackInfo.pPipelineCreationFeedback = &Feedback;
  FeedbackInfo.pipelineStageCreationFeedbackCount = 2;
  FeedbackInfo.pPipelineStageCreationFeedbacks = StageFeedbacks;
  Info.pNext = &FeedbackInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);

  EXPECT_EQ(Feedback.flags,
            static_cast<VkPipelineCreationFeedbackFlags>(
                VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT));
  for (const VkPipelineCreationFeedback &StageFeedback : StageFeedbacks)
    EXPECT_EQ(StageFeedback.flags,
              static_cast<VkPipelineCreationFeedbackFlags>(
                  VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT));

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

// roadmap C4: `mapTopology` beyond `TriangleList`/`TriangleStrip`. Every
// `VkPrimitiveTopology` this milestone's executor implements
// (point/line/line-strip/triangle-fan) creates successfully and translates
// to the matching `feme::graphics::PrimitiveTopology`.
TEST_F(GraphicsPipelineTest, AcceptsEveryImplementedTopology) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  static constexpr std::pair<VkPrimitiveTopology,
                             feme::graphics::PrimitiveTopology>
      Cases[] = {
          {VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
           feme::graphics::PrimitiveTopology::PointList},
          {VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
           feme::graphics::PrimitiveTopology::LineList},
          {VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
           feme::graphics::PrimitiveTopology::LineStrip},
          {VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
           feme::graphics::PrimitiveTopology::TriangleList},
          {VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
           feme::graphics::PrimitiveTopology::TriangleStrip},
          {VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
           feme::graphics::PrimitiveTopology::TriangleFan},
      };
  for (auto [VkTopology, ExpectedTopology] : Cases) {
    VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
    InputAssembly.topology = VkTopology;
    VkPipeline Pipe = VK_NULL_HANDLE;
    ASSERT_EQ(create(Info, Pipe), VK_SUCCESS) << "topology " << VkTopology;
    ASSERT_NE(Pipe, VK_NULL_HANDLE);

    auto *Graphics =
        static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
    DynamicGraphicsState Dynamic;
    feme::graphics::GraphicsPipeline Executor =
        Graphics->buildExecutorPipeline(Dynamic);
    EXPECT_EQ(Executor.getTopology(), ExpectedTopology)
        << "topology " << VkTopology;

    vkDestroyPipeline(Device, Pipe, nullptr);
  }

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

TEST_F(GraphicsPipelineTest, RejectsUnimplementedStateCombinations) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipeline Pipe = VK_NULL_HANDLE;

  // An unimplemented topology (an adjacency topology, needing a geometry
  // stage -- roadmap R34/C4c; every other topology is implemented now).
  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
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

/// (roadmap F5) `VkPipelineRasterizationLineStateCreateInfoKHR`, chained
/// from `pRasterizationState->pNext`, sets the line style/width/stipple
/// state `RasterState` now carries.
TEST_F(GraphicsPipelineTest, TranslatesLineRasterizationState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.lineWidth = 3.0f;
  VkPipelineRasterizationLineStateCreateInfoKHR LineState{};
  LineState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_KHR;
  LineState.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_BRESENHAM_KHR;
  LineState.stippledLineEnable = VK_TRUE;
  LineState.lineStippleFactor = 3;
  LineState.lineStipplePattern = 0x00FF;
  Raster.pNext = &LineState;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  feme::graphics::RasterState Resolved =
      Graphics->buildExecutorPipeline(DynamicGraphicsState{}).getRasterState();
  EXPECT_EQ(Resolved.LineMode, feme::graphics::LineRasterizationMode::Bresenham);
  EXPECT_EQ(Resolved.LineWidth, 3.0f);
  EXPECT_TRUE(Resolved.StippledLineEnable);
  EXPECT_EQ(Resolved.StippleFactor, 3u);
  EXPECT_EQ(Resolved.StipplePattern, 0x00FFu);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F6) `VkPipelineVertexInputDivisorStateCreateInfo` overrides a
/// per-instance binding's default divisor (1) with an explicit value,
/// recorded on the pipeline's own `VertexInputBinding` for the executor's
/// fetch-index formula to use.
TEST_F(GraphicsPipelineTest, TranslatesVertexAttributeDivisorState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkVertexInputBindingDescription BindingDesc{0, 16,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VkVertexInputBindingDivisorDescription DivisorDesc{/*binding=*/0,
                                                     /*divisor=*/3};
  VkPipelineVertexInputDivisorStateCreateInfo DivisorState{};
  DivisorState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO;
  DivisorState.vertexBindingDivisorCount = 1;
  DivisorState.pVertexBindingDivisors = &DivisorDesc;
  VertexInput.pNext = &DivisorState;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  ASSERT_EQ(Graphics->vertexBindings().size(), 1u);
  EXPECT_EQ(Graphics->vertexBindings()[0].Divisor, 3u);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F6) A divisor of `0` (`vertexAttributeInstanceRateZeroDivisor`)
/// is accepted too: it is not a new mechanism, just this same per-binding
/// field's own degenerate value.
TEST_F(GraphicsPipelineTest, AcceptsZeroVertexAttributeDivisor) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkVertexInputBindingDescription BindingDesc{0, 16,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VkVertexInputBindingDivisorDescription DivisorDesc{/*binding=*/0,
                                                     /*divisor=*/0};
  VkPipelineVertexInputDivisorStateCreateInfo DivisorState{};
  DivisorState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO;
  DivisorState.vertexBindingDivisorCount = 1;
  DivisorState.pVertexBindingDivisors = &DivisorDesc;
  VertexInput.pNext = &DivisorState;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  ASSERT_EQ(Graphics->vertexBindings().size(), 1u);
  EXPECT_EQ(Graphics->vertexBindings()[0].Divisor, 0u);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F6) A `VkVertexInputBindingDivisorDescription` naming a binding
/// the pipeline never declared, or one declared
/// `VK_VERTEX_INPUT_RATE_VERTEX` (the divisor only ever applies to a
/// per-instance binding), or a divisor exceeding `maxVertexAttribDivisor`,
/// is rejected at creation rather than silently ignored or clamped.
TEST_F(GraphicsPipelineTest, RejectsInvalidVertexAttributeDivisorState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipeline Pipe = VK_NULL_HANDLE;

  // Names a binding the pipeline does not declare.
  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkVertexInputBindingDescription BindingDesc{0, 16,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VkVertexInputBindingDivisorDescription BadBinding{/*binding=*/1,
                                                    /*divisor=*/2};
  VkPipelineVertexInputDivisorStateCreateInfo DivisorState{};
  DivisorState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO;
  DivisorState.vertexBindingDivisorCount = 1;
  DivisorState.pVertexBindingDivisors = &BadBinding;
  VertexInput.pNext = &DivisorState;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  // Names a `VK_VERTEX_INPUT_RATE_VERTEX` binding.
  Info = makeCreateInfo(Vertex, Fragment);
  VkVertexInputBindingDescription VertexRateBinding{
      0, 16, VK_VERTEX_INPUT_RATE_VERTEX};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &VertexRateBinding;
  VkVertexInputBindingDivisorDescription WrongRate{/*binding=*/0,
                                                   /*divisor=*/2};
  DivisorState.pVertexBindingDivisors = &WrongRate;
  VertexInput.pNext = &DivisorState;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// STIPPLE_KHR`: a pipeline may declare either dynamic, and
/// `buildExecutorPipeline` must then read the per-draw snapshot rather
/// than this pipeline's own (deliberately mismatched) creation-time
/// value.
TEST_F(GraphicsPipelineTest, DynamicLineWidthAndStippleOverrideStaticState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.lineWidth = 1.0f;
  VkPipelineRasterizationLineStateCreateInfoKHR LineState{};
  LineState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_KHR;
  LineState.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_RECTANGULAR_KHR;
  LineState.stippledLineEnable = VK_TRUE;
  LineState.lineStippleFactor = 1;
  LineState.lineStipplePattern = 0x0001;
  Raster.pNext = &LineState;
  VkDynamicState DynStates[2] = {VK_DYNAMIC_STATE_LINE_WIDTH,
                                 VK_DYNAMIC_STATE_LINE_STIPPLE_KHR};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 2;
  DynamicInfo.pDynamicStates = DynStates;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  Dynamic.LineWidth = 5.0f;
  Dynamic.StippleFactor = 7;
  Dynamic.StipplePattern = 0xABCD;
  feme::graphics::RasterState Resolved =
      Graphics->buildExecutorPipeline(Dynamic).getRasterState();
  EXPECT_EQ(Resolved.LineWidth, 5.0f);
  EXPECT_EQ(Resolved.StippleFactor, 7u);
  EXPECT_EQ(Resolved.StipplePattern, 0xABCDu);

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

/// (roadmap C4c) `VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT`/`_SCISSOR_WITH_
/// COUNT`: the same effective dynamic state as `VIEWPORT`/`SCISSOR`, so a
/// pipeline may declare `viewportCount`/`scissorCount` as anything (even
/// `0`, as here) once either "with count" state makes that field ignored,
/// and `resolveViewport`/`resolveScissor` still read the per-draw snapshot.
TEST_F(GraphicsPipelineTest, ViewportWithCountIsTheSameDynamicStateAsViewport) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  ViewportState.viewportCount = 0;
  ViewportState.pViewports = nullptr;
  ViewportState.scissorCount = 0;
  ViewportState.pScissors = nullptr;
  VkDynamicState DynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
                                 VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 2;
  DynamicInfo.pDynamicStates = DynStates;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  Dynamic.Viewport = feme::graphics::ViewportState{1.0f, 2.0f, 8.0f, 8.0f,
                                                   0.0f, 1.0f};
  Dynamic.Scissor = feme::graphics::ScissorRect{0, 0, 8, 8};
  EXPECT_EQ(Graphics->resolveViewport(Dynamic).Width, 8.0f);
  EXPECT_EQ(Graphics->resolveScissor(Dynamic).Width, 8u);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap C4c) `VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY`, restricted to the
/// triangle class this executor implements: a pipeline created with
/// `TriangleList` may resolve to `TriangleStrip` at draw time (still the
/// same class), and an out-of-class dynamic value (a defensive case no
/// conformant caller reaches, per `DynamicGraphicsState::Topology`'s own
/// comment) falls back to the pipeline's own static topology rather than
/// resolving to something unspecified.
TEST_F(GraphicsPipelineTest, DynamicPrimitiveTopologySwitchesWithinTriangleClass) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Dynamic;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dyn;
  Dyn.Topology = feme::graphics::PrimitiveTopology::TriangleStrip;
  EXPECT_EQ(Graphics->buildExecutorPipeline(Dyn).getTopology(),
            feme::graphics::PrimitiveTopology::TriangleStrip);

  // The defensive fallback: an unmapped dynamic value (`nullopt`) resolves
  // to the pipeline's own static topology (`TriangleList`, per
  // `makeCreateInfo`) instead.
  DynamicGraphicsState Fallback;
  EXPECT_EQ(Graphics->buildExecutorPipeline(Fallback).getTopology(),
            feme::graphics::PrimitiveTopology::TriangleList);

  vkDestroyPipeline(Device, Pipe, nullptr);
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

/// Roadmap H2b: a depth-only pipeline -- zero color attachments and a
/// fragment stage with no color output -- is legal Vulkan
/// (`dEQP-VK.multiview.depth_without_fragment_shader`'s own shape) and must
/// build rather than being rejected for lacking a color attachment.
TEST_F(GraphicsPipelineTest, AcceptsZeroColorAttachments) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(NoColorOutputFragmentSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Blend.attachmentCount = 0;
  Blend.pAttachments = nullptr;
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthTestEnable = VK_TRUE;
  DepthInfo.depthWriteEnable = VK_TRUE;
  DepthInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  Info.pDepthStencilState = &DepthInfo;

  VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 0;
  Rendering.depthAttachmentFormat = DepthFormat;
  Info.renderPass = VK_NULL_HANDLE;
  Info.pNext = &Rendering;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  EXPECT_EQ(static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe))
                ->colorAttachmentCount(),
            0u);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H2j: a depth-only pipeline may omit the fragment stage from
/// `pStages` entirely -- distinct from `AcceptsZeroColorAttachments` above
/// (whose fragment stage is present but merely writes no color output) --
/// exactly `dEQP-VK.multiview.depth_without_fragment_shader`'s own shape
/// (`VUID-VkGraphicsPipelineCreateInfo-pStages-06894`/neighbors).
TEST_F(GraphicsPipelineTest, AcceptsMissingFragmentStage) {
  VkShaderModule Vertex = createModule(VertexSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, VK_NULL_HANDLE);
  Info.stageCount = 1;
  Blend.attachmentCount = 0;
  Blend.pAttachments = nullptr;
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthTestEnable = VK_TRUE;
  DepthInfo.depthWriteEnable = VK_TRUE;
  DepthInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  Info.pDepthStencilState = &DepthInfo;

  VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 0;
  Rendering.depthAttachmentFormat = DepthFormat;
  Info.renderPass = VK_NULL_HANDLE;
  Info.pNext = &Rendering;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  auto *Pipe2 = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  EXPECT_EQ(Pipe2->colorAttachmentCount(), 0u);
  EXPECT_FALSE(Pipe2->hasFragmentStage());

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H2j: unlike a depth-only pipeline, a pipeline whose render
/// target has at least one color attachment still requires a fragment
/// stage to produce it -- omitting `pStages`'s fragment entry is only
/// legal when `Targets.Colors` is empty, matching
/// `VUID-VkGraphicsPipelineCreateInfo-pStages-06894`'s own condition.
TEST_F(GraphicsPipelineTest, RejectsMissingFragmentStageWithColorAttachments) {
  VkShaderModule Vertex = createModule(VertexSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, VK_NULL_HANDLE);
  Info.stageCount = 1;

  VkPipeline Pipe = VK_NULL_HANDLE;
  EXPECT_NE(create(Info, Pipe), VK_SUCCESS);

  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H2j: when a pipeline has no fragment stage, it has no fragment
/// output interface, so per the Vulkan spec `pColorBlendState` -- including
/// its own `attachmentCount` -- must be entirely ignored rather than
/// validated against the render target's (necessarily empty) color
/// attachments. Exercises exactly the shape real CTS tests build (e.g.
/// `vktMultiViewRenderTests.cpp`'s `depth_without_fragment_shader` case),
/// which hardcodes `attachmentCount = 1` unconditionally even with zero
/// color attachments and no fragment shader.
TEST_F(GraphicsPipelineTest,
       AcceptsMissingFragmentStageWithMismatchedColorBlendState) {
  VkShaderModule Vertex = createModule(VertexSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, VK_NULL_HANDLE);
  Info.stageCount = 1;
  // Deliberately left at the fixture's default of 1, mismatching the zero
  // color attachments below -- this must be ignored, not rejected.
  ASSERT_EQ(Blend.attachmentCount, 1u);
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthTestEnable = VK_TRUE;
  DepthInfo.depthWriteEnable = VK_TRUE;
  DepthInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  Info.pDepthStencilState = &DepthInfo;

  VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 0;
  Rendering.depthAttachmentFormat = DepthFormat;
  Info.renderPass = VK_NULL_HANDLE;
  Info.pNext = &Rendering;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  auto *Pipe2 = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  EXPECT_EQ(Pipe2->colorAttachmentCount(), 0u);
  EXPECT_FALSE(Pipe2->hasFragmentStage());

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap C1 ("Mandatory formats"): every format
/// `isSupportedColorAttachmentFormat` grants Vulkan 1.2's mandatory
/// `COLOR_ATTACHMENT_BIT | COLOR_ATTACHMENT_BLEND_BIT` status to must build
/// a pipeline the same way `VK_FORMAT_R8G8B8A8_UNORM` already does. Roadmap
/// E5 extends this same acceptance to `VK_KHR_maintenance5`'s two new
/// formats, `A8_UNORM`/`A1B5G5R5_UNORM_PACK16`, which are not mandatory but
/// are `COLOR_ATTACHMENT_BIT | COLOR_ATTACHMENT_BLEND_BIT` capable.
TEST_F(GraphicsPipelineTest, AcceptsMandatoryColorAttachmentFormats) {
  for (VkFormat Format :
       {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_R16G16B16A16_SFLOAT,
        // Roadmap E5: `VK_KHR_maintenance5`'s two new formats.
        VK_FORMAT_A8_UNORM_KHR, VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR}) {
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

/// Roadmap E9: `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT`
/// with no cache at all must always report `VK_PIPELINE_COMPILE_REQUIRED`
/// and leave the pipeline null, the same as the compute path (see
/// `PipelineCacheTest.FailOnCompileRequiredWithNoCacheAlwaysFails`).
TEST_F(GraphicsPipelineTest, FailOnCompileRequiredWithNoCacheAlwaysFails) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.flags = VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipeline), VK_PIPELINE_COMPILE_REQUIRED);
  EXPECT_EQ(Pipeline, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap E9: with a cache, a first creation carrying the bit misses (the
/// cache starts empty) without populating it; an ordinary creation then
/// compiles and populates it; a third creation with the bit set again now
/// hits and succeeds, reusing the second creation's compiled stages.
TEST_F(GraphicsPipelineTest, FailOnCompileRequiredSucceedsOnceCachePopulated) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkGraphicsPipelineCreateInfo NoCompileInfo = makeCreateInfo(Vertex, Fragment);
  NoCompileInfo.flags =
      VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
  VkPipeline Missed = VK_NULL_HANDLE;
  EXPECT_EQ(create(NoCompileInfo, Missed, Cache), VK_PIPELINE_COMPILE_REQUIRED);
  EXPECT_EQ(Missed, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo NormalInfo = makeCreateInfo(Vertex, Fragment);
  VkPipeline Compiled = VK_NULL_HANDLE;
  ASSERT_EQ(create(NormalInfo, Compiled, Cache), VK_SUCCESS);

  VkGraphicsPipelineCreateInfo HitInfo = makeCreateInfo(Vertex, Fragment);
  HitInfo.flags = VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
  VkPipeline Hit = VK_NULL_HANDLE;
  ASSERT_EQ(create(HitInfo, Hit, Cache), VK_SUCCESS);

  auto *CompiledPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Compiled));
  auto *HitPipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Hit));
  EXPECT_EQ(&CompiledPipe->vertexStage(), &HitPipe->vertexStage());

  vkDestroyPipeline(Device, Compiled, nullptr);
  vkDestroyPipeline(Device, Hit, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F10) `VkPipelineRobustnessCreateInfo` is resolved independently
/// per stage: the vertex stage's own chained struct is honored for the
/// vertex stage, the pipeline-level one is the fragment stage's fallback
/// (it names no struct of its own here), matching the extension's own
/// "scoped to all accesses emanating from the shader code of this shader
/// stage" spec text.
TEST_F(GraphicsPipelineTest, ResolvesPipelineRobustnessPerStage) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkPipelineRobustnessCreateInfo PipelineRobustnessInfo{};
  PipelineRobustnessInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO;
  PipelineRobustnessInfo.images =
      VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED;

  VkPipelineRobustnessCreateInfo VertexRobustnessInfo{};
  VertexRobustnessInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO;
  VertexRobustnessInfo.vertexInputs =
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2;

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.pNext = &PipelineRobustnessInfo;
  Stages[0].pNext = &VertexRobustnessInfo;

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  auto *Pipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Handle));
  EXPECT_EQ(Pipe->vertexRobustness().VertexInputs,
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2);
  EXPECT_EQ(Pipe->vertexRobustness().Images,
            VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DEVICE_DEFAULT);
  EXPECT_EQ(Pipe->fragmentRobustness().Images,
            VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// An out-of-range behavior value in either the pipeline-level or a
/// stage-level `VkPipelineRobustnessCreateInfo` must fail pipeline
/// creation.
TEST_F(GraphicsPipelineTest, RejectsInvalidPipelineRobustnessBehavior) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkPipelineRobustnessCreateInfo Robustness{};
  Robustness.sType = VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO;
  Robustness.images = static_cast<VkPipelineRobustnessImageBehavior>(0xFFFF);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Stages[1].pNext = &Robustness;

  VkPipeline Handle = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Handle), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Handle, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

} // namespace
