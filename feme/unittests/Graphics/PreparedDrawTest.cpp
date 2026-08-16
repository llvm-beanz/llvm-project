//===- PreparedDrawTest.cpp - Tests for feme::graphics::PreparedDraw ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See PipelineTest.cpp's file comment: roadmap R31 only defines the
// description, so these tests cover `PreparedDraw`'s plumbing (attachments,
// viewport/scissor, vertex buffers, draw commands), not execution (roadmap
// R32).
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/PreparedDraw.h"

#include "gtest/gtest.h"

#include <array>

using namespace feme;
using namespace feme::graphics;

namespace {

TEST(PreparedDrawTest, DescribesOneDraw) {
  std::array<uint8_t, 64> AttachmentStorage{};
  AttachmentView Color{AttachmentStorage, cpu::ResourceFormat::R8G8B8A8_UNORM,
                       4, 4};
  std::array<AttachmentView, 1> Attachments{Color};

  std::array<float, 9> VertexData = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f,
                                     0.0f,  -1.0f, 3.0f, 0.0f};
  VertexBufferBinding VertexBuffer{
      /*Binding=*/0, /*Stride=*/12,
      llvm::ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
                     VertexData.size() * sizeof(float))};
  std::array<VertexBufferBinding, 1> VertexBuffers{VertexBuffer};

  DrawCommand Draw{/*VertexCount=*/3, /*InstanceCount=*/1, /*FirstVertex=*/0,
                   /*FirstInstance=*/0};
  std::array<DrawCommand, 1> Draws{Draw};

  PreparedDraw Prepared;
  Prepared.Attachments = Attachments;
  Prepared.Viewport = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Prepared.Scissor = ScissorRect{0, 0, 4, 4};
  Prepared.VertexBuffers = VertexBuffers;
  Prepared.Draws = Draws;

  ASSERT_EQ(Prepared.Attachments.size(), 1u);
  EXPECT_EQ(Prepared.Attachments[0].Width, 4u);
  EXPECT_EQ(Prepared.Attachments[0].Height, 4u);
  EXPECT_EQ(Prepared.Attachments[0].Format,
            cpu::ResourceFormat::R8G8B8A8_UNORM);
  EXPECT_EQ(Prepared.Viewport.Width, 4.0f);
  EXPECT_EQ(Prepared.Scissor.Width, 4u);
  ASSERT_EQ(Prepared.VertexBuffers.size(), 1u);
  EXPECT_EQ(Prepared.VertexBuffers[0].Stride, 12u);
  ASSERT_EQ(Prepared.Draws.size(), 1u);
  EXPECT_EQ(Prepared.Draws[0].VertexCount, 3u);
}

} // namespace
