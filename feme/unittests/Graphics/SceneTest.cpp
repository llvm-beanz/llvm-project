//===- SceneTest.cpp - Tests for the textual scene description format ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Covers feme::graphics::parseScene (roadmap R31) against the exact example
// "Textual scene and image fixtures" in feme/docs/Design.md gives, plus its
// "unrecognized key is an error" requirement.
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Scene.h"

#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::graphics;
using namespace llvm;

namespace {

TEST(SceneTest, ParsesDesignDocExample) {
  StringRef Text = R"(
attachments:
  - name: color0
    format: r8g8b8a8-unorm
    extent: [4, 4]
    clear: [0.0, 0.0, 0.0, 1.0]
pipeline:
  vertex: { module: vs.ll, entry: main }
  fragment: { module: fs.ll, entry: main }
  cull: none
  front-face: ccw
  depth: { test: less, write: true }
  blend: replace
viewport: { rect: [0, 0, 4, 4], depth: [0.0, 1.0] }
scissor: [0, 0, 4, 4]
vertex-buffers:
  - binding: 0
    stride: 12
    attributes:
      - { location: 0, format: r32g32b32-float, offset: 0 }
    data: [-1.0, -1.0, 0.0,  3.0, -1.0, 0.0,  -1.0, 3.0, 0.0]
textures:
  - { index: 0, file: checker.image }
draws:
  - { vertices: 3, instances: 1 }
)";
  Expected<Scene> ParsedOrErr = parseScene(Text);
  ASSERT_THAT_EXPECTED(ParsedOrErr, Succeeded());
  Scene &S = *ParsedOrErr;

  ASSERT_EQ(S.Attachments.size(), 1u);
  EXPECT_EQ(S.Attachments[0].Name, "color0");
  EXPECT_EQ(S.Attachments[0].Format, "r8g8b8a8-unorm");
  EXPECT_EQ(S.Attachments[0].Width, 4u);
  EXPECT_EQ(S.Attachments[0].Height, 4u);
  ASSERT_EQ(S.Attachments[0].Clear.size(), 4u);
  EXPECT_EQ(S.Attachments[0].Clear[3], 1.0);

  ASSERT_TRUE(S.Pipeline.has_value());
  EXPECT_EQ(S.Pipeline->Vertex.Module, "vs.ll");
  EXPECT_EQ(S.Pipeline->Fragment.Entry, "main");
  EXPECT_EQ(S.Pipeline->Cull, "none");
  EXPECT_EQ(S.Pipeline->FrontFace, "ccw");
  EXPECT_EQ(S.Pipeline->Depth.Test, "less");
  EXPECT_TRUE(S.Pipeline->Depth.Write);
  EXPECT_EQ(S.Pipeline->Blend, "replace");

  ASSERT_TRUE(S.Viewport.has_value());
  ASSERT_EQ(S.Viewport->Rect.size(), 4u);
  EXPECT_EQ(S.Viewport->Rect[2], 4.0);

  ASSERT_EQ(S.Scissor.size(), 4u);

  ASSERT_EQ(S.VertexBuffers.size(), 1u);
  EXPECT_EQ(S.VertexBuffers[0].Stride, 12u);
  ASSERT_EQ(S.VertexBuffers[0].Attributes.size(), 1u);
  EXPECT_EQ(S.VertexBuffers[0].Attributes[0].Format, "r32g32b32-float");
  EXPECT_EQ(S.VertexBuffers[0].Data.size(), 9u);

  ASSERT_EQ(S.Textures.size(), 1u);
  EXPECT_EQ(S.Textures[0].File, "checker.image");

  ASSERT_EQ(S.Draws.size(), 1u);
  EXPECT_EQ(S.Draws[0].Vertices, 3u);
}

TEST(SceneTest, DefaultsOmittedPipelineState) {
  StringRef Text = R"(
pipeline:
  vertex: { module: vs.ll }
  fragment: { module: fs.ll }
)";
  Expected<Scene> ParsedOrErr = parseScene(Text);
  ASSERT_THAT_EXPECTED(ParsedOrErr, Succeeded());
  ASSERT_TRUE(ParsedOrErr->Pipeline.has_value());
  EXPECT_EQ(ParsedOrErr->Pipeline->Vertex.Entry, "main");
  EXPECT_EQ(ParsedOrErr->Pipeline->Cull, "none");
  EXPECT_EQ(ParsedOrErr->Pipeline->Depth.Test, "none");
}

TEST(SceneTest, RejectsUnknownKey) {
  StringRef Text = "not-a-real-key: 1\n";
  Expected<Scene> ParsedOrErr = parseScene(Text);
  ASSERT_THAT_EXPECTED(ParsedOrErr, Failed());
}

} // namespace
