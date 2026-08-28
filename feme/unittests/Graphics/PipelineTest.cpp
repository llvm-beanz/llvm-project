//===- PipelineTest.cpp - Tests for feme::graphics::GraphicsPipeline ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap R31 ("FeMeGraphics skeleton") only defines the normalized pipeline
// *description* -- these tests cover the plumbing of that description
// (state getters, attachment list), not stage compilation (already covered
// by unittests/Target/CPU/PipelineTest.cpp) or execution (roadmap R32, not
// implemented yet).
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Pipeline.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Target/CPU/CompiledStage.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::graphics;
using namespace llvm;

namespace {

/// A minimal mesh entry point, compiled just far enough to attach to a
/// `GraphicsPipeline` -- these tests only cover `setMeshStage`'s own
/// plumbing, not stage execution (`ExecutorTest.cpp`'s mesh-chaining
/// tests, roadmap H6e, cover that).
constexpr char MinimalMeshShaderIR[] = R"(
  define void @ms_main() #0 {
    ret void
  }
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

/// Same shape as `MinimalMeshShaderIR`, tagged as the task (amplification)
/// stage instead.
constexpr char MinimalTaskShaderIR[] = R"(
  define void @ts_main() #0 {
    ret void
  }
  attributes #0 = { "hlsl.shader"="amplification" "hlsl.numthreads"="1,1,1" }
)";

Expected<std::shared_ptr<cpu::CompiledStage>>
compileMinimalStage(Context &Ctx, StringRef IR, ShaderStage Stage) {
  SMDiagnostic Err;
  auto M = parseAssemblyString(IR, Err, Ctx.getLLVMContext());
  if (!M)
    return createStringError(inconvertibleErrorCode(), "parse error: %s",
                             Err.getMessage().str().c_str());
  feme::Module Mod = feme::Module::fromLLVMIR(std::move(M));
  cpu::StageCompileOptions Opts;
  Opts.Stage = Stage;
  Opts.WaveSize = 4;
  return cpu::CompiledStage::create(Ctx, std::move(Mod), Opts);
}

TEST(GraphicsPipelineTest, DescribesFixedFunctionState) {
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}};
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, /*FragmentStage=*/nullptr,
      PrimitiveTopology::TriangleList,
      RasterState{CullMode::Back, FrontFace::CounterClockwise},
      DepthState{/*TestEnable=*/true, /*WriteEnable=*/true, CompareOp::Less},
      BlendMode::Replace, /*SampleCount=*/1, Attachments);

  EXPECT_EQ(Pipeline.getTopology(), PrimitiveTopology::TriangleList);
  EXPECT_EQ(Pipeline.getRasterState().Cull, CullMode::Back);
  EXPECT_EQ(Pipeline.getRasterState().Front, FrontFace::CounterClockwise);
  EXPECT_TRUE(Pipeline.getDepthState().TestEnable);
  EXPECT_TRUE(Pipeline.getDepthState().WriteEnable);
  EXPECT_EQ(Pipeline.getDepthState().Compare, CompareOp::Less);
  EXPECT_EQ(Pipeline.getBlendMode(), BlendMode::Replace);
  EXPECT_EQ(Pipeline.getSampleCount(), 1u);
  ASSERT_EQ(Pipeline.getAttachments().size(), 1u);
  EXPECT_EQ(Pipeline.getAttachments()[0].Format,
            cpu::ResourceFormat::R8G8B8A8_UNORM);
  EXPECT_EQ(Pipeline.getAttachments()[0].Width, 4u);
  EXPECT_EQ(Pipeline.getAttachments()[0].Height, 4u);
}

// (Roadmap H6e) `setMeshStage`'s own plumbing: a mesh pipeline has no real
// vertex stage of its own (`VertexStage=nullptr` below, mirroring how
// `FragmentStage` may already be null for a depth/stencil-only pipeline,
// roadmap H2j) -- `Executor::executeDraws` gates its own single
// `getVertexStage()` call site behind `!hasMeshStages()` to make that safe.
TEST(GraphicsPipelineTest, SetMeshStageRecordsTheMeshAndTaskStagesAndState) {
  Context Ctx;
  Expected<std::shared_ptr<cpu::CompiledStage>> MS =
      compileMinimalStage(Ctx, MinimalMeshShaderIR, ShaderStage::Mesh);
  ASSERT_THAT_EXPECTED(MS, Succeeded());
  Expected<std::shared_ptr<cpu::CompiledStage>> TS =
      compileMinimalStage(Ctx, MinimalTaskShaderIR, ShaderStage::Amplification);
  ASSERT_THAT_EXPECTED(TS, Succeeded());

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}};
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, /*FragmentStage=*/nullptr,
      PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, Attachments);
  EXPECT_FALSE(Pipeline.hasMeshStages());

  MeshState Mesh;
  Mesh.OutputTopology = MeshOutputTopology::Triangles;
  Mesh.MaxOutputVertices = 3;
  Mesh.MaxOutputPrimitives = 1;
  // (roadmap H6f) See `ExecutorTest.cpp`'s `makeMeshPipeline` comment: the
  // dispatch limits are a required, explicit argument now.
  AmplificationDispatchLimits Limits;
  Limits.MaxGroupCount = {65535, 65535, 65535};
  Limits.MaxTotalGroupCount = 4194304;
  Pipeline.setMeshStage(*TS, *MS, Mesh, Limits, Limits);

  EXPECT_TRUE(Pipeline.hasMeshStages());
  EXPECT_TRUE(Pipeline.hasTaskStage());
  EXPECT_EQ(&Pipeline.getMeshStage(), MS->get());
  EXPECT_EQ(&Pipeline.getTaskStage(), TS->get());
  EXPECT_EQ(Pipeline.getMeshState().OutputTopology,
            MeshOutputTopology::Triangles);
  EXPECT_EQ(Pipeline.getMeshState().MaxOutputVertices, 3u);
  EXPECT_EQ(Pipeline.getMeshState().MaxOutputPrimitives, 1u);
}

// A mesh pipeline may legally omit its task (amplification) stage
// (`vkCmdDrawMeshTasksEXT` with no task shader): `setMeshStage`'s own
// `TaskStage` argument is the one allowed to be null, unlike `MeshStage`
// itself.
TEST(GraphicsPipelineTest, SetMeshStageAllowsAnOmittedTaskStage) {
  Context Ctx;
  Expected<std::shared_ptr<cpu::CompiledStage>> MS =
      compileMinimalStage(Ctx, MinimalMeshShaderIR, ShaderStage::Mesh);
  ASSERT_THAT_EXPECTED(MS, Succeeded());

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}};
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, /*FragmentStage=*/nullptr,
      PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, Attachments);
  Pipeline.setMeshStage(
      /*TaskStage=*/nullptr, *MS, MeshState{},
      AmplificationDispatchLimits{{65535, 65535, 65535}, 4194304});

  EXPECT_TRUE(Pipeline.hasMeshStages());
  EXPECT_FALSE(Pipeline.hasTaskStage());
}

TEST(PrimitiveTopologyTest, HasAdjacencyIdentifiesTheFourAdjacencyKinds) {
  EXPECT_FALSE(topologyHasAdjacency(PrimitiveTopology::PointList));
  EXPECT_FALSE(topologyHasAdjacency(PrimitiveTopology::TriangleStrip));
  EXPECT_TRUE(topologyHasAdjacency(PrimitiveTopology::LineListWithAdjacency));
  EXPECT_TRUE(topologyHasAdjacency(PrimitiveTopology::LineStripWithAdjacency));
  EXPECT_TRUE(
      topologyHasAdjacency(PrimitiveTopology::TriangleListWithAdjacency));
  EXPECT_TRUE(
      topologyHasAdjacency(PrimitiveTopology::TriangleStripWithAdjacency));
}

/// Roadmap H5e-b: every strip/fan topology restarts, not just
/// `TriangleStrip` (`GraphicsPipeline.cpp`'s own creation-time gate used
/// to only allow that one, a stale check `Executor.cpp`'s own
/// `RestartEnabled` condition had already outgrown as of roadmap H5d).
TEST(PrimitiveTopologyTest,
     SupportsPrimitiveRestartIdentifiesEveryStripAndFanKind) {
  EXPECT_TRUE(topologySupportsPrimitiveRestart(PrimitiveTopology::LineStrip));
  EXPECT_TRUE(
      topologySupportsPrimitiveRestart(PrimitiveTopology::TriangleStrip));
  EXPECT_TRUE(topologySupportsPrimitiveRestart(PrimitiveTopology::TriangleFan));
  EXPECT_TRUE(topologySupportsPrimitiveRestart(
      PrimitiveTopology::LineStripWithAdjacency));
  EXPECT_TRUE(topologySupportsPrimitiveRestart(
      PrimitiveTopology::TriangleStripWithAdjacency));

  EXPECT_FALSE(topologySupportsPrimitiveRestart(PrimitiveTopology::PointList));
  EXPECT_FALSE(topologySupportsPrimitiveRestart(PrimitiveTopology::LineList));
  EXPECT_FALSE(
      topologySupportsPrimitiveRestart(PrimitiveTopology::TriangleList));
  EXPECT_FALSE(topologySupportsPrimitiveRestart(
      PrimitiveTopology::LineListWithAdjacency));
  EXPECT_FALSE(topologySupportsPrimitiveRestart(
      PrimitiveTopology::TriangleListWithAdjacency));
  EXPECT_FALSE(topologySupportsPrimitiveRestart(PrimitiveTopology::PatchList));
}

TEST(PrimitiveTopologyTest, StripAdjacencyReturnsTheAssembledTopology) {
  EXPECT_EQ(stripAdjacency(PrimitiveTopology::LineListWithAdjacency),
            PrimitiveTopology::LineList);
  EXPECT_EQ(stripAdjacency(PrimitiveTopology::TriangleListWithAdjacency),
            PrimitiveTopology::TriangleList);
  // A non-adjacency topology maps to itself.
  EXPECT_EQ(stripAdjacency(PrimitiveTopology::TriangleList),
            PrimitiveTopology::TriangleList);
}

TEST(PrimitiveTopologyTest, ListPrimitiveVertexCounts) {
  EXPECT_EQ(getListPrimitiveVertexCount(PrimitiveTopology::PointList), 1u);
  EXPECT_EQ(getListPrimitiveVertexCount(PrimitiveTopology::LineList), 2u);
  EXPECT_EQ(
      getListPrimitiveVertexCount(PrimitiveTopology::LineListWithAdjacency),
      4u);
  EXPECT_EQ(getListPrimitiveVertexCount(PrimitiveTopology::TriangleList), 3u);
  EXPECT_EQ(
      getListPrimitiveVertexCount(PrimitiveTopology::TriangleListWithAdjacency),
      6u);
}

TEST(PrimitiveTopologyTest, SplitListPrimitiveAdjacencyForLines) {
  SplitPrimitiveAdjacency Split = splitListPrimitiveAdjacency(
      PrimitiveTopology::LineListWithAdjacency, {10, 11, 12, 13});
  EXPECT_EQ(Split.Primitive, (llvm::SmallVector<uint32_t, 3>{11, 12}));
  EXPECT_EQ(Split.Adjacent, (llvm::SmallVector<uint32_t, 3>{10, 13}));
}

TEST(PrimitiveTopologyTest, SplitListPrimitiveAdjacencyForTriangles) {
  SplitPrimitiveAdjacency Split = splitListPrimitiveAdjacency(
      PrimitiveTopology::TriangleListWithAdjacency, {0, 1, 2, 3, 4, 5});
  EXPECT_EQ(Split.Primitive, (llvm::SmallVector<uint32_t, 3>{0, 2, 4}));
  EXPECT_EQ(Split.Adjacent, (llvm::SmallVector<uint32_t, 3>{1, 3, 5}));
}

TEST(PrimitiveTopologyTest,
     SplitListPrimitiveAdjacencyWithoutAdjacencyIsIdentity) {
  SplitPrimitiveAdjacency Split =
      splitListPrimitiveAdjacency(PrimitiveTopology::TriangleList, {5, 6, 7});
  EXPECT_EQ(Split.Primitive, (llvm::SmallVector<uint32_t, 3>{5, 6, 7}));
  EXPECT_TRUE(Split.Adjacent.empty());
}

TEST(PrimitiveTopologyTest, StripPrimitiveCountForLines) {
  // A line strip with adjacency needs at least 4 indices (one primitive);
  // each additional index yields one more primitive.
  EXPECT_EQ(
      getStripPrimitiveCount(PrimitiveTopology::LineStripWithAdjacency, 0), 0u);
  EXPECT_EQ(
      getStripPrimitiveCount(PrimitiveTopology::LineStripWithAdjacency, 3), 0u);
  EXPECT_EQ(
      getStripPrimitiveCount(PrimitiveTopology::LineStripWithAdjacency, 4), 1u);
  EXPECT_EQ(
      getStripPrimitiveCount(PrimitiveTopology::LineStripWithAdjacency, 7), 4u);
}

TEST(PrimitiveTopologyTest, StripPrimitiveCountForTriangles) {
  // A triangle strip with adjacency needs at least 6 indices (one
  // primitive) and advances its window by 2 indices per primitive, so an
  // odd `IndexCount - 4` describes no whole number of primitives.
  EXPECT_EQ(
      getStripPrimitiveCount(PrimitiveTopology::TriangleStripWithAdjacency, 0),
      0u);
  EXPECT_EQ(
      getStripPrimitiveCount(PrimitiveTopology::TriangleStripWithAdjacency, 5),
      0u);
  EXPECT_EQ(
      getStripPrimitiveCount(PrimitiveTopology::TriangleStripWithAdjacency, 6),
      1u);
  EXPECT_EQ(
      getStripPrimitiveCount(PrimitiveTopology::TriangleStripWithAdjacency, 9),
      0u);
  EXPECT_EQ(
      getStripPrimitiveCount(PrimitiveTopology::TriangleStripWithAdjacency, 10),
      3u);
}

TEST(PrimitiveTopologyTest, SplitStripPrimitiveAdjacencyForLines) {
  // Vertex buffer: adj0=10, v0=11, v1=12, adj1=13, v2=14, adj2=15.
  llvm::SmallVector<uint32_t, 6> Indices = {10, 11, 12, 13, 14, 15};
  SplitPrimitiveAdjacency Split0 = splitStripPrimitiveAdjacency(
      PrimitiveTopology::LineStripWithAdjacency, Indices, 0);
  EXPECT_EQ(Split0.Primitive, (llvm::SmallVector<uint32_t, 3>{11, 12}));
  EXPECT_EQ(Split0.Adjacent, (llvm::SmallVector<uint32_t, 3>{10, 13}));

  // Primitive 1's window slides by one: (v1, v2) with adjacency (v0, adj2).
  SplitPrimitiveAdjacency Split1 = splitStripPrimitiveAdjacency(
      PrimitiveTopology::LineStripWithAdjacency, Indices, 1);
  EXPECT_EQ(Split1.Primitive, (llvm::SmallVector<uint32_t, 3>{12, 13}));
  EXPECT_EQ(Split1.Adjacent, (llvm::SmallVector<uint32_t, 3>{11, 14}));
}

TEST(PrimitiveTopologyTest, SplitStripPrimitiveAdjacencyForTriangles) {
  // Two triangles sharing an edge: 8 indices, 2 primitives.
  llvm::SmallVector<uint32_t, 8> Indices = {0, 1, 2, 3, 4, 5, 6, 7};
  SplitPrimitiveAdjacency Split0 = splitStripPrimitiveAdjacency(
      PrimitiveTopology::TriangleStripWithAdjacency, Indices, 0);
  EXPECT_EQ(Split0.Primitive, (llvm::SmallVector<uint32_t, 3>{0, 2, 4}));
  EXPECT_EQ(Split0.Adjacent, (llvm::SmallVector<uint32_t, 3>{1, 3, 5}));

  // Primitive 1's window slides by two.
  SplitPrimitiveAdjacency Split1 = splitStripPrimitiveAdjacency(
      PrimitiveTopology::TriangleStripWithAdjacency, Indices, 1);
  EXPECT_EQ(Split1.Primitive, (llvm::SmallVector<uint32_t, 3>{2, 4, 6}));
  EXPECT_EQ(Split1.Adjacent, (llvm::SmallVector<uint32_t, 3>{3, 5, 7}));
}

} // namespace
