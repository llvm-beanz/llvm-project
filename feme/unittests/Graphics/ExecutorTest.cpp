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
              StencilState Stencil = StencilState{},
              BlendState ColorBlend = BlendState{}, bool LogicOpEnable = false,
              LogicOp Logic = LogicOp::Copy,
              std::array<float, 4> BlendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
              bool PrimitiveRestartEnable = false) {
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
                          /*SampleCount=*/1, std::move(Attachments), Stencil,
                          std::vector<BlendState>{ColorBlend}, LogicOpEnable,
                          Logic, BlendConstants, PrimitiveRestartEnable);
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

/// A `VK_VERTEX_INPUT_RATE`-unrelated milestone deviation: primitive restart
/// on an indexed `TriangleStrip`. The restart marker (the index type's
/// all-1-bits value) between two disjoint triangles must not be treated as
/// a real vertex index (which would either fetch out of bounds or bridge
/// the two triangles into one connected, wrongly-shaped strip); each
/// triangle instead renders only its own solid color.
TEST(ExecutorTest, HonorsPrimitiveRestartOnIndexedTriangleStrip) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleStrip, DepthState{}, StencilState{},
      BlendState{}, /*LogicOpEnable=*/false, LogicOp::Copy,
      /*BlendConstants=*/{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/true);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {
      // Segment 1: a red triangle in the lower-left region.
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  // v1
      -1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  // v2
      // Segment 2: a green triangle in the upper-right region.
      0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,  // v3
      1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,  // v4
      1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // v5
  };
  // A restart index between the two segments: without primitive restart
  // this would otherwise be read as a (nonsensical, out-of-bounds) vertex
  // index bridging the two triangles into one continuous strip.
  Scene.Indices = {0, 1, 2, 0xFFFFFFFFu, 3, 4, 5};
  PreparedDraw Draw = Scene.prepare(/*Indexed=*/true);

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  // Centroid of segment 1's triangle: definitely red.
  const uint8_t *Red = texel(0, 2);
  EXPECT_EQ(Red[0], 255);
  EXPECT_EQ(Red[1], 0);
  EXPECT_EQ(Red[2], 0);
  // Centroid of segment 2's triangle: definitely green.
  const uint8_t *Green = texel(3, 1);
  EXPECT_EQ(Green[0], 0);
  EXPECT_EQ(Green[1], 255);
  EXPECT_EQ(Green[2], 0);
  // The screen center is covered by neither triangle (and no phantom
  // triangle bridging the restart): still the cleared background.
  const uint8_t *Center = texel(2, 2);
  EXPECT_EQ(Center[0], 0);
  EXPECT_EQ(Center[1], 0);
  EXPECT_EQ(Center[2], 0);
  EXPECT_EQ(Center[3], 0);
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

TEST(ExecutorTest, CullsEveryTriangleWithFrontAndBack) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::FrontAndBack, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // The same oversized, front-facing (CCW) triangle every other test in
  // this file leaves unculled: `CullMode::FrontAndBack` must discard it
  // too, regardless of winding (`VK_CULL_MODE_FRONT_AND_BACK` rasterizes
  // no primitive of the pipeline's topology at all).
  TriangleScene Scene;
  Scene.VertexData = {-1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint8_t Byte : Scene.AttachmentStorage)
    EXPECT_EQ(Byte, 0);
}

TEST(ExecutorTest, RejectsUnsupportedTopology) {
  Context Ctx;
  // `*WithAdjacency` topologies need a geometry stage (roadmap R34), still
  // unimplemented; every other topology is now accepted (roadmap C4).
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleListWithAdjacency);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {-1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  PreparedDraw Draw = Scene.prepare();

  EXPECT_THAT_ERROR(executeDraws(*Pipeline, Draw), Failed());
}

// roadmap C4: `mapTopology` beyond `TriangleList`/`TriangleStrip`. A
// `TriangleFan` needs no new rasterizer primitive at all: it is the same
// clip/rasterize path as `TriangleList`/`TriangleStrip`, just a different
// per-primitive vertex-index assembly (every triangle shares the fan's
// first fetched vertex as its pivot).
TEST(ExecutorTest, RendersATriangleFan) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleFan);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // A fan pivoting on v0, covering the whole [-1, 1] NDC square with two
  // triangles: (v0, v1, v2) and (v0, v2, v3).
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1
      3.0f,  3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v3
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t I = 0; I != 16; ++I) {
    const uint8_t *Texel = Scene.AttachmentStorage.data() + I * 4;
    EXPECT_EQ(Texel[0], 255) << "texel " << I;
    EXPECT_EQ(Texel[3], 255) << "texel " << I;
  }
}

// roadmap C4: an indexed `TriangleFan` honors primitive restart the same
// way an indexed `TriangleStrip` does (each restarted segment is a fresh
// fan with its own pivot, not a phantom triangle bridging the two fans).
TEST(ExecutorTest, HonorsPrimitiveRestartOnIndexedTriangleFan) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleFan, DepthState{}, StencilState{},
      BlendState{}, /*LogicOpEnable=*/false, LogicOp::Copy,
      /*BlendConstants=*/{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/true);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {
      // Segment 1: a red triangle in the lower-left region.
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  // v1
      -1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  // v2
      // Segment 2: a green triangle in the upper-right region.
      0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,  // v3
      1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,  // v4
      1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // v5
  };
  Scene.Indices = {0, 1, 2, 0xFFFFFFFFu, 3, 4, 5};
  PreparedDraw Draw = Scene.prepare(/*Indexed=*/true);

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  const uint8_t *Red = texel(0, 2);
  EXPECT_EQ(Red[0], 255);
  EXPECT_EQ(Red[1], 0);
  const uint8_t *Green = texel(3, 1);
  EXPECT_EQ(Green[0], 0);
  EXPECT_EQ(Green[1], 255);
}

// roadmap C4: a `PointList` draws a fixed 1-pixel-square point at each
// vertex (`largePoints` is not an advertised device feature, so a
// conformant point size is always 1.0 -- see the executor's own comment).
TEST(ExecutorTest, RendersAPointList) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::PointList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  // Two points, pixel centers (1, 1) and (2, 2) of the 4x4 target: NDC
  // ((1 + 0.5) / 4 * 2 - 1, ...) with Y flipped by the viewport transform.
  Scene.VertexData = {
      -0.25f, 0.25f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // pixel (1, 1), red
      0.25f,  -0.25f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // pixel (2, 2), green
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  const uint8_t *Red = texel(1, 1);
  EXPECT_EQ(Red[0], 255);
  EXPECT_EQ(Red[1], 0);
  EXPECT_EQ(Red[3], 255);
  const uint8_t *Green = texel(2, 2);
  EXPECT_EQ(Green[0], 0);
  EXPECT_EQ(Green[1], 255);
  EXPECT_EQ(Green[3], 255);
  // Every other texel is untouched by either 1-pixel point.
  const uint8_t *Untouched = texel(0, 0);
  EXPECT_EQ(Untouched[3], 0);
}

// roadmap C4: a `LineList` draws a fixed 1-pixel-wide line between each
// pair of vertices (`wideLines` is not an advertised device feature, so a
// conformant line width is always 1.0, matching `lineWidthRange`'s fixed
// `[1.0, 1.0]` in `PhysicalDeviceInfo.cpp`).
TEST(ExecutorTest, RendersAHorizontalLineList) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::LineList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  // A horizontal line through the row of pixels at Y=1 (NDC y = 0.25,
  // viewport-flipped to screen row 1's center), spanning the target's
  // full width.
  Scene.VertexData = {
      -1.0f, 0.25f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
      1.0f,  0.25f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  for (uint32_t X = 0; X != 4; ++X) {
    const uint8_t *Texel = texel(X, 1);
    EXPECT_EQ(Texel[3], 255) << "x=" << X;
  }
  // The row above/below the line is untouched.
  EXPECT_EQ(texel(0, 0)[3], 0);
  EXPECT_EQ(texel(0, 2)[3], 0);
}

// roadmap C4: a `LineStrip` connects consecutive vertices, and an indexed
// strip honors primitive restart exactly as a `TriangleStrip`/`TriangleFan`
// does (a restart marker starts a fresh strip rather than bridging the two
// with a phantom segment).
TEST(ExecutorTest, HonorsPrimitiveRestartOnIndexedLineStrip) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::LineStrip, DepthState{}, StencilState{}, BlendState{},
      /*LogicOpEnable=*/false, LogicOp::Copy,
      /*BlendConstants=*/{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/true);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {
      // Segment 1: a horizontal red line through screen row 0.
      -1.0f,
      0.75f,
      0.0f,
      1.0f,
      0.0f,
      0.0f,
      1.0f,
      1.0f,
      0.75f,
      0.0f,
      1.0f,
      0.0f,
      0.0f,
      1.0f,
      // Segment 2: a horizontal green line through screen row 3.
      -1.0f,
      -0.75f,
      0.0f,
      0.0f,
      1.0f,
      0.0f,
      1.0f,
      1.0f,
      -0.75f,
      0.0f,
      0.0f,
      1.0f,
      0.0f,
      1.0f,
  };
  Scene.Indices = {0, 1, 0xFFFFFFFFu, 2, 3};
  PreparedDraw Draw = Scene.prepare(/*Indexed=*/true);

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  EXPECT_EQ(texel(0, 0)[0], 255);
  EXPECT_EQ(texel(0, 0)[1], 0);
  EXPECT_EQ(texel(0, 3)[0], 0);
  EXPECT_EQ(texel(0, 3)[1], 255);
  // No phantom segment bridges the restart across the middle rows.
  EXPECT_EQ(texel(0, 1)[3], 0);
  EXPECT_EQ(texel(0, 2)[3], 0);
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

// Roadmap R33: blending, write masks, and logic ops.
TEST(ExecutorTest, AlphaBlendsOverExistingColor) {
  Context Ctx;
  BlendState Blend;
  Blend.BlendEnable = true;
  Blend.SrcColorFactor = BlendFactor::SrcAlpha;
  Blend.DstColorFactor = BlendFactor::OneMinusSrcAlpha;
  Blend.SrcAlphaFactor = BlendFactor::One;
  Blend.DstAlphaFactor = BlendFactor::Zero;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, StencilState{}, Blend);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  // Every texel starts opaque green.
  for (uint32_t I = 0; I != 16; ++I) {
    Scene.AttachmentStorage[I * 4] = 0;
    Scene.AttachmentStorage[I * 4 + 1] = 255;
    Scene.AttachmentStorage[I * 4 + 2] = 0;
    Scene.AttachmentStorage[I * 4 + 3] = 255;
  }
  // A half-alpha red triangle covering the whole viewport.
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 0.5f, 3.0f, -1.0f, 0.0f, 1.0f,
      0.0f,  0.0f,  0.5f, -1.0f, 3.0f, 0.0f, 1.0f, 0.0f, 0.0f,  0.5f,
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());
  // result = src*srcAlpha + dst*(1-srcAlpha) = (1,0,0)*0.5 + (0,1,0)*0.5
  for (uint32_t I = 0; I != 16; ++I) {
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4], 128, 2) << "texel " << I;
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4 + 1], 128, 2) << "texel " << I;
    EXPECT_EQ(Scene.AttachmentStorage[I * 4 + 2], 0) << "texel " << I;
  }
}

// roadmap C4: dual-source blend factors (`VK_BLEND_FACTOR_SRC1_*`). A
// fragment stage's second color output -- `SV_Target0`'s `Index=1`
// companion, `SignatureElement::Index` -- is read by a `Src1Color`/
// `Src1Alpha` blend factor instead of the fragment's ordinary
// (`Index=0`) output. This test's fragment stage writes a fixed (1, 1, 1,
// 1) to its ordinary output and a fixed (0.25, 0.5, 0.75, 1.0) to its
// `Index=1` output; `SrcColorFactor`/`SrcAlphaFactor` of `Src1Color`/
// `Src1Alpha` with `DstColorFactor`/`DstAlphaFactor` of `Zero` isolates
// exactly the second output's value in the result (`1 * Src1 + Dst * 0`).
TEST(ExecutorTest, DualSourceBlendReadsTheSecondFragmentOutput) {
  Context Ctx;

  constexpr char VertexIR[] = R"(
    define void @vs_dualsrc() #0 {
      %px = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %py = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %pz = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %px, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %py, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %pz, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="vertex" }
  )";
  constexpr char FragmentIR[] = R"(
    define void @fs_dualsrc() #0 {
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 1.0, i32 0)
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 1.0, i32 0)
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 1.0, i32 0)
      call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float 0.25, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float 0.5, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float 0.75, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
      ret void
    }
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";

  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, VertexIR, "vs_dualsrc", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  EntrySignature FSSig;
  SignatureElement Src0 =
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/0);
  SignatureElement Src1 =
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0);
  Src1.Index = 1;
  FSSig.Elements = {Src0, Src1};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, FragmentIR, "fs_dualsrc", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  BlendState Blend;
  Blend.BlendEnable = true;
  Blend.SrcColorFactor = BlendFactor::Src1Color;
  Blend.DstColorFactor = BlendFactor::Zero;
  Blend.SrcAlphaFactor = BlendFactor::Src1Alpha;
  Blend.DstAlphaFactor = BlendFactor::Zero;
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace,
      /*SampleCount=*/1, std::move(Attachments), StencilState{},
      std::vector<BlendState>{Blend});

  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f,
      0.0f,  0.0f,  1.0f, -1.0f, 3.0f, 0.0f, 1.0f, 0.0f, 0.0f,  1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());

  for (uint32_t I = 0; I != 16; ++I) {
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4], 64, 2) << "texel " << I;
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4 + 1], 128, 2) << "texel " << I;
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4 + 2], 191, 2) << "texel " << I;
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4 + 3], 255, 2) << "texel " << I;
  }
}

TEST(ExecutorTest, WriteMaskLeavesUnselectedChannelsUnchanged) {
  Context Ctx;
  BlendState Blend;
  Blend.WriteMask = 0b0001; // Only the red channel may be written.
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, StencilState{}, Blend);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  for (uint32_t I = 0; I != 16; ++I) {
    Scene.AttachmentStorage[I * 4] = 10;
    Scene.AttachmentStorage[I * 4 + 1] = 20;
    Scene.AttachmentStorage[I * 4 + 2] = 30;
    Scene.AttachmentStorage[I * 4 + 3] = 40;
  }
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f,
      1.0f,  1.0f,  1.0f, -1.0f, 3.0f, 0.0f, 1.0f, 1.0f, 1.0f,  1.0f,
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());
  for (uint32_t I = 0; I != 16; ++I) {
    EXPECT_EQ(Scene.AttachmentStorage[I * 4], 255) << "texel " << I;
    EXPECT_EQ(Scene.AttachmentStorage[I * 4 + 1], 20) << "texel " << I;
    EXPECT_EQ(Scene.AttachmentStorage[I * 4 + 2], 30) << "texel " << I;
    EXPECT_EQ(Scene.AttachmentStorage[I * 4 + 3], 40) << "texel " << I;
  }
}

TEST(ExecutorTest, LogicOpAndsWithExistingColor) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, StencilState{},
      BlendState{}, /*LogicOpEnable=*/true, LogicOp::And);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  for (uint32_t I = 0; I != 16; ++I)
    for (unsigned C = 0; C != 4; ++C)
      Scene.AttachmentStorage[I * 4 + C] = 0b11001100;
  // Solid white (0xFF per channel): AND with 0b11001100 keeps 0b11001100.
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f,
      1.0f,  1.0f,  1.0f, -1.0f, 3.0f, 0.0f, 1.0f, 1.0f, 1.0f,  1.0f,
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());
  for (uint32_t I = 0; I != 16; ++I)
    for (unsigned C = 0; C != 4; ++C)
      EXPECT_EQ(Scene.AttachmentStorage[I * 4 + C], 0b11001100)
          << "texel " << I << " channel " << C;
}

// Roadmap R33: multiple render targets. A fragment shader with two
// `SV_Target` outputs (element 1, location 0 and element 2, location 1)
// writes its input color to target 0 and its complement to target 1.
constexpr char MRTFragmentShaderIR[] = R"(
  define void @fs_mrt() #0 {
    %r = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %g = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    %b = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
    %ir = fsub float 1.0, %r
    %ig = fsub float 1.0, %g
    %ib = fsub float 1.0, %b
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %r, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %g, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %b, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %ir, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %ig, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %ib, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.0, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

TEST(ExecutorTest, RendersToMultipleColorAttachments) {
  Context Ctx;
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, VertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/1)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, MRTFragmentShaderIR, "fs_mrt", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4},
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments),
      StencilState{}, std::vector<BlendState>{BlendState{}, BlendState{}});

  std::array<uint8_t, 64> Color0Storage{};
  std::array<uint8_t, 64> Color1Storage{};
  AttachmentView Color0{Color0Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4,
                        4};
  AttachmentView Color1{Color1Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4,
                        4};
  std::array<AttachmentView, 2> Attachs{Color0, Color1};

  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f,
      0.0f,  0.0f,  1.0f, -1.0f, 3.0f, 0.0f, 1.0f, 0.0f, 0.0f,  1.0f,
  };
  std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
  std::array<VertexBufferBinding, 1> Bindings = {VertexBufferBinding{
      0, 28,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attributes}};

  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewport = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissor = ScissorRect{0, 0, 4, 4};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());
  for (uint32_t I = 0; I != 16; ++I) {
    // Target 0 gets the solid red input color...
    EXPECT_EQ(Color0Storage[I * 4], 255) << "texel " << I;
    EXPECT_EQ(Color0Storage[I * 4 + 1], 0) << "texel " << I;
    // ...and target 1 gets its complement (cyan).
    EXPECT_EQ(Color1Storage[I * 4], 0) << "texel " << I;
    EXPECT_EQ(Color1Storage[I * 4 + 1], 255) << "texel " << I;
  }
}

TEST(ExecutorTest, RejectsMismatchedColorBlendCount) {
  Context Ctx;
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, VertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/1)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, MRTFragmentShaderIR, "fs_mrt", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  // Two attachment formats but the default single-element ColorBlends
  // list: the pipeline/draw mismatch must be rejected before any rendering
  // work happens.
  std::vector<AttachmentFormat> AttachmentFormats = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4},
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace,
      /*SampleCount=*/1, std::move(AttachmentFormats));

  std::array<uint8_t, 64> Color0Storage{}, Color1Storage{};
  AttachmentView Color0{Color0Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4,
                        4};
  AttachmentView Color1{Color1Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4,
                        4};
  std::array<AttachmentView, 2> Attachs{Color0, Color1};

  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewport = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissor = ScissorRect{0, 0, 4, 4};
  EXPECT_THAT_ERROR(executeDraws(Pipeline, Draw), Failed());
}

// Roadmap R33: multisample coverage and resolve. A vertical-edged triangle
// covers the left half of a 4-wide viewport, with its edge running exactly
// through pixel 2's center -- splitting its 4 fixed sample offsets (see
// Executor.cpp's own "Fixed per-pixel sample offsets") 2 covered / 2
// uncovered by construction (each has an x offset on a different side of
// 0.5), independent of which two exact positions the table uses.
TEST(ExecutorTest, MultisampleResolveAveragesPerPixelCoverage) {
  Context Ctx;
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, VertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());
  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/4,
      {AttachmentFormat{cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}});

  constexpr uint32_t Samples = 4;
  std::vector<uint8_t> MSStorage(4u * 4u * Samples * 4u, 0);
  // Clear every sample to opaque black.
  for (size_t I = 0; I + 3 < MSStorage.size(); I += 4)
    MSStorage[I + 3] = 255;
  std::array<uint8_t, 64> ResolveStorage{};

  AttachmentView MSColor{MSStorage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4};
  AttachmentView Resolve{ResolveStorage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4,
                         4};
  std::array<AttachmentView, 1> Attachs{MSColor};
  std::array<AttachmentView, 1> Resolves{Resolve};

  // Two CCW triangles forming a quad covering ndc_x in [-3, 0.25] (see the
  // comment above for why 0.25 lands the edge on pixel 2's center).
  std::vector<float> VertexData = {
      -3.0f, -3.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
      0.25f, -3.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
      0.25f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
      -3.0f, -3.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
      0.25f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
      -3.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
  };
  std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
  std::array<VertexBufferBinding, 1> Bindings = {VertexBufferBinding{
      0, 28,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attributes}};

  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.ResolveAttachments = Resolves;
  Draw.Viewport = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissor = ScissorRect{0, 0, 4, 4};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 6;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());
  // Row 0, pixels 0/1 fully covered (solid red), pixel 2 half-covered (a
  // red/black blend), pixel 3 fully uncovered (solid black).
  EXPECT_EQ(ResolveStorage[0 * 4], 255); // pixel 0 red
  EXPECT_EQ(ResolveStorage[0 * 4 + 3], 255);
  EXPECT_EQ(ResolveStorage[1 * 4], 255);      // pixel 1 red
  EXPECT_NEAR(ResolveStorage[2 * 4], 128, 2); // pixel 2 ~50% red
  EXPECT_EQ(ResolveStorage[2 * 4 + 3], 255);  // alpha was opaque both sides
  EXPECT_EQ(ResolveStorage[3 * 4], 0);        // pixel 3 black
}

TEST(ExecutorTest, AcceptsEightSampleCount) {
  Context Ctx;
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, VertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());
  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/8,
      {AttachmentFormat{cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}});

  constexpr uint32_t Samples = 8;
  std::vector<uint8_t> MSStorage(4u * 4u * Samples * 4u, 0);
  for (size_t I = 0; I + 3 < MSStorage.size(); I += 4)
    MSStorage[I + 3] = 255;
  std::array<uint8_t, 64> ResolveStorage{};

  AttachmentView MSColor{MSStorage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4};
  AttachmentView Resolve{ResolveStorage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4,
                         4};
  std::array<AttachmentView, 1> Attachs{MSColor};
  std::array<AttachmentView, 1> Resolves{Resolve};

  TriangleScene Scene;
  Scene.VertexData = {-1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  PreparedDraw Draw = Scene.prepare();
  Draw.Attachments = Attachs;
  Draw.ResolveAttachments = Resolves;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());
  // The triangle fully covers the attachment, so every one of the 8
  // samples at every pixel is red, and the resolve must be exactly
  // red at every pixel too -- exercising every one of `samplePositions`'
  // eight offsets rather than only the fraction a partial-coverage edge
  // would exercise.
  for (size_t Pixel = 0; Pixel != 16; ++Pixel) {
    EXPECT_EQ(ResolveStorage[Pixel * 4], 255) << "pixel " << Pixel;
    EXPECT_EQ(ResolveStorage[Pixel * 4 + 3], 255) << "pixel " << Pixel;
  }
}

TEST(ExecutorTest, RejectsUnsupportedSampleCount) {
  Context Ctx;
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, VertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());
  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/16,
      {AttachmentFormat{cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}});

  TriangleScene Scene;
  Scene.VertexData = {-1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  PreparedDraw Draw = Scene.prepare();
  EXPECT_THAT_ERROR(executeDraws(Pipeline, Draw), Failed());
}

// Roadmap R33: deterministic parallel tiled schedules. A 64x64 attachment
// (16 tiles at the executor's fixed 16x16 tile size) with several
// triangles spanning many of them must produce byte-identical output
// whether the tile schedule runs on 1 worker or several.
TEST(ExecutorTest, ParallelTileScheduleMatchesSequentialOutput) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // Four overlapping, differently-colored triangles covering different
  // parts of the viewport, so different tiles see different bins.
  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // red, bottom-left half
      1.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
      -1.0f, 1.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
      1.0f,  -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // green, top-right half
      1.0f,  1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f, //
      -1.0f, 1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f, //
      -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, // translucent blue diamond
      0.5f,  -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, //
      0.0f,  0.5f,  0.0f, 0.0f, 0.0f, 1.0f, 0.5f, //
  };
  std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
  std::array<VertexBufferBinding, 1> Bindings = {VertexBufferBinding{
      0, 28,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attributes}};
  DrawCommand Cmd;
  Cmd.VertexCount = 9;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};

  auto render = [&](uint32_t Workers) {
    std::vector<uint8_t> Storage(64u * 64u * 4u, 0);
    AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, 64, 64};
    std::array<AttachmentView, 1> Attachs{Color};
    PreparedDraw Draw;
    Draw.Attachments = Attachs;
    Draw.Viewport = ViewportState{0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
    Draw.Scissor = ScissorRect{0, 0, 64, 64};
    Draw.VertexBuffers = Bindings;
    Draw.Draws = Draws;
    EXPECT_THAT_ERROR(executeDraws(*Pipeline, Draw, Workers), Succeeded());
    return Storage;
  };

  std::vector<uint8_t> Sequential = render(1);
  std::vector<uint8_t> EightWorkers = render(8);
  std::vector<uint8_t> SixtyFourWorkers = render(64);
  EXPECT_EQ(Sequential, EightWorkers);
  EXPECT_EQ(Sequential, SixtyFourWorkers);
}

} // namespace
