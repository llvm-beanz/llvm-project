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

#include "gtest/gtest.h"

using namespace feme;
using namespace feme::graphics;

namespace {

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
  EXPECT_TRUE(
      topologySupportsPrimitiveRestart(PrimitiveTopology::LineStrip));
  EXPECT_TRUE(
      topologySupportsPrimitiveRestart(PrimitiveTopology::TriangleStrip));
  EXPECT_TRUE(
      topologySupportsPrimitiveRestart(PrimitiveTopology::TriangleFan));
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
