//===- ExecutorTest.cpp - Tests for feme::graphics::executeDraws --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Covers feme::graphics::executeDraws (roadmap R32, "Basic triangle
// pipeline"): vertex/index fetch, triangle assembly, clipping, viewport
// transform, culling, tile binning, top-left coverage and interpolation
// through a real compiled vertex/fragment pipeline pair.
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Executor.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Core/Signature.h"
#include "feme/Graphics/Pipeline.h"
#include "feme/Graphics/PreparedDraw.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

#include <array>
#include <cmath>
#include <vector>

using namespace feme;
using namespace feme::cpu;
using namespace feme::graphics;
using namespace llvm;

namespace {

// A vertex shader with two inputs (location 0: float3 position, location 1:
// float4 color) and two outputs (element 2: SV_Position, element 3, location
// 0: float4 color passthrough).
constexpr char VertexShaderIR[] = R"(
  define void @vs_main() #0 {
    %px = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %py = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    %pz = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
    %cr = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
    %cg = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
    %cb = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
    %ca = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %px, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %py, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %pz, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %cr, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %cg, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %cb, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float %ca, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="vertex" }
)";

// A fragment shader passing its one input (element 0, location 0: float4
// color) straight through to SV_Target0 (element 1, location 0).
constexpr char FragmentShaderIR[] = R"(
  define void @fs_main() #0 {
    %r = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %g = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    %b = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
    %a = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %r, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %g, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %b, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float %a, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

SignatureElement
makeElement(uint32_t ElementID, SignatureDirection Dir, uint32_t ComponentCount,
            std::optional<uint32_t> Location,
            SignatureSystemValue SysVal = SignatureSystemValue::None) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = Dir;
  Elt.ComponentType = SignatureComponentType::Float;
  Elt.BitWidth = 32;
  Elt.ComponentCount = ComponentCount;
  Elt.Location = Location;
  Elt.SystemValue = SysVal;
  return Elt;
}

Expected<std::shared_ptr<CompiledStage>>
compileStage(Context &Ctx, StringRef IR, StringRef EntryName,
             const EntrySignature &Sig, ShaderStage Stage) {
  SMDiagnostic Err;
  auto LLVMMod = parseAssemblyString(IR, Err, Ctx.getLLVMContext());
  if (!LLVMMod)
    return createStringError(inconvertibleErrorCode(), "parse error: %s",
                             Err.getMessage().str().c_str());
  dxil::setEntrySignature(*LLVMMod->getFunction(EntryName), Sig);
  feme::Module Mod = feme::Module::fromLLVMIR(std::move(LLVMMod));
  StageCompileOptions Opts;
  Opts.Stage = Stage;
  Opts.WaveSize = 4;
  Expected<std::unique_ptr<CompiledStage>> Compiled =
      CompiledStage::create(Ctx, std::move(Mod), Opts);
  if (!Compiled)
    return Compiled.takeError();
  return std::shared_ptr<CompiledStage>(std::move(*Compiled));
}

/// Builds the color-passthrough vertex+fragment `GraphicsPipeline` the
/// shaders above implement, with the given raster state and topology.
Expected<GraphicsPipeline>
buildPipeline(Context &Ctx, RasterState Raster,
              PrimitiveTopology Topology = PrimitiveTopology::TriangleList,
              DepthState Depth = DepthState{},
              StencilState Stencil = StencilState{}) {
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, VertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  if (!VS)
    return VS.takeError();

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  if (!FS)
    return FS.takeError();

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}};
  return GraphicsPipeline(std::move(*VS), std::move(*FS), Topology, Raster,
                          Depth, BlendMode::Replace,
                          /*SampleCount=*/1, std::move(Attachments), Stencil);
}

struct TriangleScene {
  std::array<uint8_t, 64> AttachmentStorage{};
  // 4x4 depth attachment, one float per texel, initialized to the far
  // plane so a test that binds it without an explicit clear still starts
  // from a sensible default.
  std::array<float, 16> DepthStorage;
  bool BindDepth = false;
  // 4x4 stencil attachment, one byte per texel.
  std::array<uint8_t, 16> StencilStorage{};
  bool BindStencil = false;
  // Interleaved position (xyz) + color (rgba) per vertex, 7 floats/vertex.
  std::vector<float> VertexData;
  std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
  std::vector<uint32_t> Indices;

  AttachmentView Color;
  std::array<AttachmentView, 1> Attachments;
  std::vector<VertexBufferBinding> Bindings;
  std::array<DrawCommand, 1> Draws;

  TriangleScene() { DepthStorage.fill(1.0f); }

  PreparedDraw prepare(bool Indexed = false) {
    PreparedDraw Draw;
    Color = AttachmentView{AttachmentStorage,
                           cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4};
    Attachments = {Color};
    Draw.Attachments = Attachments;
    if (BindDepth)
      Draw.DepthStencil.Depth = AttachmentView{
          MutableArrayRef(reinterpret_cast<uint8_t *>(DepthStorage.data()),
                          DepthStorage.size() * sizeof(float)),
          cpu::ResourceFormat::D32_FLOAT, 4, 4};
    if (BindStencil)
      Draw.DepthStencil.Stencil =
          AttachmentView{StencilStorage, cpu::ResourceFormat::S8_UINT, 4, 4};
    Draw.Viewport = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
    Draw.Scissor = ScissorRect{0, 0, 4, 4};

    Bindings = {VertexBufferBinding{
        0, 28,
        ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
                 VertexData.size() * sizeof(float)),
        Attributes}};
    Draw.VertexBuffers = Bindings;

    DrawCommand Cmd;
    Cmd.VertexCount = Indexed ? static_cast<uint32_t>(Indices.size())
                              : static_cast<uint32_t>(VertexData.size() / 7);
    Cmd.InstanceCount = 1;
    Cmd.Indexed = Indexed;
    Draws = {Cmd};
    Draw.Draws = Draws;

    if (Indexed)
      Draw.IndexBuffer = IndexBufferBinding{
          IndexType::UInt32,
          ArrayRef(reinterpret_cast<const uint8_t *>(Indices.data()),
                   Indices.size() * sizeof(uint32_t))};
    return Draw;
  }
};

TEST(ExecutorTest, FillsFullyCoveredTriangleWithSolidColor) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // A triangle covering the whole [-1, 1] NDC square (and more), CCW-wound,
  // every vertex red.
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t I = 0; I != 16; ++I) {
    const uint8_t *Texel = Scene.AttachmentStorage.data() + I * 4;
    EXPECT_EQ(Texel[0], 255) << "texel " << I;
    EXPECT_EQ(Texel[1], 0) << "texel " << I;
    EXPECT_EQ(Texel[2], 0) << "texel " << I;
    EXPECT_EQ(Texel[3], 255) << "texel " << I;
  }
}

TEST(ExecutorTest, RendersTheSameTriangleThroughAnIndexBuffer) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  // One extra unused leading vertex so `VertexOffset` is exercised.
  Scene.VertexData = {
      0.0f,  0.0f,  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // unused
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // v0 (green)
      3.0f,  -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // v2
  };
  Scene.Indices = {0, 1, 2};
  PreparedDraw Draw = Scene.prepare(/*Indexed=*/true);
  Scene.Draws[0].VertexOffset = 1;
  Draw.Draws = Scene.Draws;

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t I = 0; I != 16; ++I) {
    const uint8_t *Texel = Scene.AttachmentStorage.data() + I * 4;
    EXPECT_EQ(Texel[0], 0) << "texel " << I;
    EXPECT_EQ(Texel[1], 255) << "texel " << I;
    EXPECT_EQ(Texel[2], 0) << "texel " << I;
    EXPECT_EQ(Texel[3], 255) << "texel " << I;
  }
}

TEST(ExecutorTest, CullsBackFacingTrianglesWhenConfigured) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::Back, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // Same triangle as above but wound clockwise (v1/v2 swapped) -- back
  // facing under `FrontFace::CounterClockwise`, so `CullMode::Back` should
  // discard it and leave the attachment untouched (all zero).
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f,  0.0f, 1.0f, -1.0f, 3.0f, 0.0f, 1.0f,
      0.0f,  0.0f,  1.0f, 3.0f, -1.0f, 0.0f, 1.0f, 0.0f,  0.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint8_t Byte : Scene.AttachmentStorage)
    EXPECT_EQ(Byte, 0);
}

TEST(ExecutorTest, RejectsUnsupportedTopology) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::PointList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {-1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  PreparedDraw Draw = Scene.prepare();

  EXPECT_THAT_ERROR(executeDraws(*Pipeline, Draw), Failed());
}

TEST(ExecutorTest, InterpolatesColorAcrossTheTriangle) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // Same oversized CCW triangle as above, but a different color per vertex,
  // so every attachment texel's expected color is the affine interpolation
  // of the (unclipped) triangle's vertex colors at that texel's pixel
  // center -- clipping must not perturb this, since it only ever
  // re-triangulates within the same affine color field.
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0 red
      3.0f,  -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // v1 green
      -1.0f, 3.0f,  0.0f, 0.0f, 0.0f, 1.0f, 1.0f, // v2 blue
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t PY = 0; PY != 4; ++PY) {
    for (uint32_t PX = 0; PX != 4; ++PX) {
      float NdcX = (PX + 0.5f) / 2.0f - 1.0f;
      float NdcY = 1.0f - (PY + 0.5f) / 2.0f;
      float U = (NdcX + 1.0f) / 4.0f;
      float V = (NdcY + 1.0f) / 4.0f;
      float R = 1.0f - U - V, G = U, B = V;
      const uint8_t *Texel = Scene.AttachmentStorage.data() + (PY * 4 + PX) * 4;
      EXPECT_NEAR(Texel[0], std::lround(R * 255.0f), 2)
          << "pixel (" << PX << "," << PY << ")";
      EXPECT_NEAR(Texel[1], std::lround(G * 255.0f), 2)
          << "pixel (" << PX << "," << PY << ")";
      EXPECT_NEAR(Texel[2], std::lround(B * 255.0f), 2)
          << "pixel (" << PX << "," << PY << ")";
    }
  }
}

TEST(ExecutorTest, AdjacentTrianglesShareAnEdgeWithoutGapsOrOverlaps) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // A quad covering the whole viewport, split into two CCW triangles along
  // the (-1,-1)-(1,1) diagonal, each a solid color. The shared diagonal
  // edge's top-left tie-break must give every texel to exactly one
  // triangle: no texel may be uncovered (black) or double-blended.
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // A0 red
      1.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // A1
      1.0f,  1.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // A2
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // B0 green
      1.0f,  1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // B1
      -1.0f, 1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // B2
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t I = 0; I != 16; ++I) {
    const uint8_t *Texel = Scene.AttachmentStorage.data() + I * 4;
    bool IsRed = Texel[0] == 255 && Texel[1] == 0;
    bool IsGreen = Texel[0] == 0 && Texel[1] == 255;
    EXPECT_TRUE(IsRed || IsGreen)
        << "texel " << I << " = (" << (int)Texel[0] << "," << (int)Texel[1]
        << "," << (int)Texel[2] << "," << (int)Texel[3] << ")";
    EXPECT_EQ(Texel[3], 255) << "texel " << I;
  }
}

// Roadmap R33 ("Depth, stencil, blending, and multisampling"): depth
// testing/writes with a real `D32_FLOAT` attachment.
TEST(ExecutorTest, DepthTestRejectsFartherFragment) {
  Context Ctx;
  DepthState Depth;
  Depth.TestEnable = true;
  Depth.WriteEnable = true;
  Depth.Compare = CompareOp::Less;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, Depth);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // A near red triangle (z=0.0) drawn after clearing depth to the far
  // plane (1.0, `TriangleScene`'s default): every texel should pass and be
  // red, with the depth attachment updated to 0.0.
  TriangleScene Scene;
  Scene.BindDepth = true;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());
  for (uint32_t I = 0; I != 16; ++I) {
    EXPECT_EQ(Scene.AttachmentStorage[I * 4], 255) << "texel " << I;
    EXPECT_FLOAT_EQ(Scene.DepthStorage[I], 0.0f) << "texel " << I;
  }

  // A farther green triangle (z=0.5) drawn next must fail the depth test
  // everywhere: the color and depth attachments stay exactly as the first
  // draw left them.
  TriangleScene Scene2 = Scene;
  Scene2.VertexData = {
      -1.0f, -1.0f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f, // v2
  };
  PreparedDraw Draw2 = Scene2.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw2), Succeeded());
  for (uint32_t I = 0; I != 16; ++I) {
    EXPECT_EQ(Scene2.AttachmentStorage[I * 4], 255) << "texel " << I;
    EXPECT_EQ(Scene2.AttachmentStorage[I * 4 + 1], 0) << "texel " << I;
    EXPECT_FLOAT_EQ(Scene2.DepthStorage[I], 0.0f) << "texel " << I;
  }
}

TEST(ExecutorTest, DepthWriteDisabledLeavesAttachmentUnchanged) {
  Context Ctx;
  DepthState Depth;
  Depth.TestEnable = true;
  Depth.WriteEnable = false;
  Depth.Compare = CompareOp::Less;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, Depth);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.BindDepth = true;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f,
      0.0f,  0.0f,  1.0f, -1.0f, 3.0f, 0.0f, 1.0f, 0.0f, 0.0f,  1.0f,
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());
  for (uint32_t I = 0; I != 16; ++I) {
    // The fragment still passes the test (0.0 < 1.0) and is shaded...
    EXPECT_EQ(Scene.AttachmentStorage[I * 4], 255) << "texel " << I;
    // ...but the depth attachment is untouched since writes are disabled.
    EXPECT_FLOAT_EQ(Scene.DepthStorage[I], 1.0f) << "texel " << I;
  }
}

TEST(ExecutorTest, RejectsDepthStateWithoutBoundAttachment) {
  Context Ctx;
  DepthState Depth;
  Depth.TestEnable = true;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, Depth);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {-1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  PreparedDraw Draw = Scene.prepare(); // BindDepth left false.
  EXPECT_THAT_ERROR(executeDraws(*Pipeline, Draw), Failed());
}

// Roadmap R33: stencil testing/writes with a real `S8_UINT` attachment.
TEST(ExecutorTest, StencilTestRejectsMismatchedReference) {
  Context Ctx;
  StencilState Stencil;
  Stencil.TestEnable = true;
  Stencil.Front.Compare = CompareOp::Equal;
  Stencil.Front.Reference = 5;
  Stencil.Front.PassOp = StencilOp::Replace;
  Stencil.Front.FailOp = StencilOp::Zero;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, Stencil);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.BindStencil = true;
  Scene.StencilStorage.fill(3); // Every texel starts unequal to Reference=5.
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f,
      0.0f,  0.0f,  1.0f, -1.0f, 3.0f, 0.0f, 1.0f, 0.0f, 0.0f,  1.0f,
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());
  for (uint32_t I = 0; I != 16; ++I) {
    // The stencil test failed everywhere (3 != 5): no color write, and
    // `FailOp` (Zero) ran on every texel.
    EXPECT_EQ(Scene.AttachmentStorage[I * 4], 0) << "texel " << I;
    EXPECT_EQ(Scene.StencilStorage[I], 0) << "texel " << I;
  }
}

TEST(ExecutorTest, StencilTestPassesAndReplacesReference) {
  Context Ctx;
  StencilState Stencil;
  Stencil.TestEnable = true;
  Stencil.Front.Compare = CompareOp::Equal;
  Stencil.Front.Reference = 5;
  Stencil.Front.PassOp = StencilOp::Replace;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, Stencil);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.BindStencil = true;
  Scene.StencilStorage.fill(5); // Matches Reference=5 everywhere.
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f,
      0.0f,  0.0f,  1.0f, -1.0f, 3.0f, 0.0f, 1.0f, 0.0f, 0.0f,  1.0f,
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());
  for (uint32_t I = 0; I != 16; ++I) {
    EXPECT_EQ(Scene.AttachmentStorage[I * 4], 255) << "texel " << I;
    EXPECT_EQ(Scene.StencilStorage[I], 5) << "texel " << I;
  }
}

TEST(ExecutorTest, RejectsStencilStateWithoutBoundAttachment) {
  Context Ctx;
  StencilState Stencil;
  Stencil.TestEnable = true;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, Stencil);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {-1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  PreparedDraw Draw = Scene.prepare(); // BindStencil left false.
  EXPECT_THAT_ERROR(executeDraws(*Pipeline, Draw), Failed());
}

} // namespace
