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

} // namespace
