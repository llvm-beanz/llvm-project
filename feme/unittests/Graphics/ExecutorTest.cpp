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

// (Roadmap C8) A vertex shader like VertexShaderIR above, but its color
// varying (element 4, location 1) is a 2x2 matrix -- `RowCount == 2`,
// `ComponentCount == 2` -- rather than a plain float4, packing the same
// per-vertex (r, g, b, a) into (row 0: r, g), (row 1: b, a). Exercises
// this row's `Row`-aware `StageStorage`/`LinkedVarying` support end to end
// through a real triangle draw, the way a SPIR-V-imported shader's own
// `spirv.CompositeConstruct`-built matrix output now reaches it via
// CanonicalizeStage.cpp.
constexpr char MatrixVaryingVertexShaderIR[] = R"(
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
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %cr, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 1, float %cg, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 1, i32 0, float %cb, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 1, i32 1, float %ca, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="vertex" }
)";

// The fragment-side counterpart of MatrixVaryingVertexShaderIR: reads the
// same 2x2 matrix input (element 0, location 0) back out one (row,
// component) at a time and unpacks it into SV_Target0 in the same order.
constexpr char MatrixVaryingFragmentShaderIR[] = R"(
  define void @fs_main() #0 {
    %r = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %g = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    %b = call float @feme.stage.input.load.f32(i32 0, i32 1, i32 0, i32 0)
    %a = call float @feme.stage.input.load.f32(i32 0, i32 1, i32 1, i32 0)
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
            SignatureSystemValue SysVal = SignatureSystemValue::None,
            uint32_t RowCount = 1) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = Dir;
  Elt.ComponentType = SignatureComponentType::Float;
  Elt.BitWidth = 32;
  Elt.ComponentCount = ComponentCount;
  Elt.RowCount = RowCount;
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
    Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
    Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};

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

/// (Roadmap C8) The same fully-covered, solid-color triangle as
/// `FillsFullyCoveredTriangleWithSolidColor`, but the color varying between
/// the vertex and fragment stage is a `RowCount == 2` "matrix" element
/// (`MatrixVaryingVertexShaderIR`/`MatrixVaryingFragmentShaderIR` above)
/// instead of a plain float4 -- the shape a SPIR-V-imported shader's own
/// matrix output now produces via CanonicalizeStage.cpp. A wrong `Row`
/// stride/offset anywhere in `StageStorage`/`LinkedVarying` would show up
/// as a scrambled (not just wrong) color, since each of the 4 scalars
/// packed into the 2x2 matrix is a different, distinguishable value.
TEST(ExecutorTest, InterpolatesConstantColorPackedInAMatrixVarying) {
  Context Ctx;

  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(4, SignatureDirection::Output, /*ComponentCount=*/2,
                  /*Location=*/1, SignatureSystemValue::None,
                  /*RowCount=*/2)};
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, MatrixVaryingVertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, /*ComponentCount=*/2,
                  /*Location=*/1, SignatureSystemValue::None,
                  /*RowCount=*/2),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, MatrixVaryingFragmentShaderIR, "fs_main", FSSig,
                   ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}};
  Expected<GraphicsPipeline> Pipeline = GraphicsPipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace,
      /*SampleCount=*/1, std::move(Attachments), StencilState{},
      std::vector<BlendState>{BlendState{}}, /*LogicOpEnable=*/false,
      LogicOp::Copy, std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // A triangle covering the whole [-1, 1] NDC square, CCW-wound, every
  // vertex a distinguishable (r, g, b, a) = (0.2, 0.4, 0.6, 0.8) so a
  // scrambled row/component mapping would not accidentally read back
  // correct either.
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, // v0
      3.0f,  -1.0f, 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, // v1
      -1.0f, 3.0f,  0.0f, 0.2f, 0.4f, 0.6f, 0.8f, // v2
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t I = 0; I != 16; ++I) {
    const uint8_t *Texel = Scene.AttachmentStorage.data() + I * 4;
    EXPECT_EQ(Texel[0], 51) << "texel " << I;  // round(0.2 * 255)
    EXPECT_EQ(Texel[1], 102) << "texel " << I; // round(0.4 * 255)
    EXPECT_EQ(Texel[2], 153) << "texel " << I; // round(0.6 * 255)
    EXPECT_EQ(Texel[3], 204) << "texel " << I; // round(0.8 * 255)
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

/// Roadmap F7 (`VK_KHR_index_type_uint8`): the same indexed triangle as
/// above, but through an 8-bit index buffer -- the executor's index-fetch
/// path (`Executor.cpp`) must read a 1-byte-per-element index exactly like
/// its pre-existing 16-/32-bit cases.
TEST(ExecutorTest, RendersTheSameTriangleThroughAnEightBitIndexBuffer) {
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
  std::array<uint8_t, 3> Indices8 = {0, 1, 2};
  // `Scene.prepare()` sizes `Cmd.VertexCount` off `Scene.Indices`'s element
  // count; the real 8-bit index data below replaces `Draw.IndexBuffer`
  // itself, so only the count (3, matching `Indices8`) matters here.
  Scene.Indices = {0, 1, 2};
  PreparedDraw Draw = Scene.prepare(/*Indexed=*/true);
  Draw.IndexBuffer = IndexBufferBinding{IndexType::UInt8, Indices8};
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
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, // v0
      0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, // v1
      -1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, // v2
      // Segment 2: a green triangle in the upper-right region.
      0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
      1.0f, // v3
      1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
      1.0f, // v4
      1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
      1.0f, // v5
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

/// Roadmap F7: the same restart scenario as above, but through an 8-bit
/// index buffer -- the restart marker is that type's own all-1-bits value
/// (`0xFF`), not the 32-bit one, so this exercises `Executor.cpp`'s
/// per-index-type `RestartValue` selection, not only its element-size one.
TEST(ExecutorTest,
     HonorsPrimitiveRestartOnIndexedTriangleStripWithEightBitIndices) {
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
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, // v0
      0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, // v1
      -1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, // v2
      // Segment 2: a green triangle in the upper-right region.
      0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
      1.0f, // v3
      1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
      1.0f, // v4
      1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
      1.0f, // v5
  };
  std::array<uint8_t, 7> Indices8 = {0, 1, 2, 0xFFu, 3, 4, 5};
  // `Scene.prepare()` sizes `Cmd.VertexCount` off `Scene.Indices`, one
  // 32-bit element per index; only its element count (not its 32-bit
  // values) matters, since `Draw.IndexBuffer` is overridden with the real
  // 8-bit data right below.
  Scene.Indices = {0, 1, 2, 0, 3, 4, 5};
  PreparedDraw Draw = Scene.prepare(/*Indexed=*/true);
  Draw.IndexBuffer = IndexBufferBinding{IndexType::UInt8, Indices8};

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
  const uint8_t *Center2 = texel(2, 2);
  EXPECT_EQ(Center2[0], 0);
  EXPECT_EQ(Center2[1], 0);
  EXPECT_EQ(Center2[2], 0);
  EXPECT_EQ(Center2[3], 0);
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
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, // v0
      0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, // v1
      -1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, // v2
      // Segment 2: a green triangle in the upper-right region.
      0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
      1.0f, // v3
      1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
      1.0f, // v4
      1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
      1.0f, // v5
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

// roadmap F5: `RasterState::LineWidth` generalizes the fixed 1-pixel
// rectangular line quad to an arbitrary width, covering every screen row
// whose pixel center falls within the centerline's `LineWidth / 2` on
// either side.
TEST(ExecutorTest, RendersAWideRectangularLine) {
  Context Ctx;
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.LineWidth = 3.0f;
  Expected<GraphicsPipeline> Pipeline =
      buildPipeline(Ctx, Raster, PrimitiveTopology::LineList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  // Same horizontal line as `RendersAHorizontalLineList`: centerline at
  // screen row 1's pixel center (y = 1.5), now 3 pixels wide so its
  // [0, 3) extent covers rows 0-2 and stops just short of row 3.
  Scene.VertexData = {
      -1.0f, 0.25f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
      1.0f,  0.25f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  for (uint32_t Y : {0u, 1u, 2u})
    for (uint32_t X = 0; X != 4; ++X)
      EXPECT_EQ(texel(X, Y)[3], 255) << "x=" << X << " y=" << Y;
  for (uint32_t X = 0; X != 4; ++X)
    EXPECT_EQ(texel(X, 3)[3], 0) << "x=" << X;
}

// roadmap F5: `LineRasterizationMode::Bresenham` walks the integer pixel
// grid directly rather than expanding a width-dependent quad, so a
// perfectly diagonal line lights exactly the diagonal pixels.
TEST(ExecutorTest, RendersABresenhamDiagonalLine) {
  Context Ctx;
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.LineMode = LineRasterizationMode::Bresenham;
  Expected<GraphicsPipeline> Pipeline =
      buildPipeline(Ctx, Raster, PrimitiveTopology::LineList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  // NDC endpoints chosen so the viewport transform lands their screen
  // positions exactly on pixel (0, 0)'s and (3, 3)'s centers.
  Scene.VertexData = {
      -0.75f, 0.75f,  0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
      0.75f,  -0.75f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  for (uint32_t D = 0; D != 4; ++D)
    EXPECT_EQ(texel(D, D)[3], 255) << "d=" << D;
  // A pixel off the diagonal is untouched.
  EXPECT_EQ(texel(0, 3)[3], 0);
  EXPECT_EQ(texel(3, 0)[3], 0);
}

// roadmap F5: a stippled line rejects a covered fragment whose position
// along the line's length falls in one of `StipplePattern`'s "off" bits.
TEST(ExecutorTest, RendersAStippledLine) {
  Context Ctx;
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.StippledLineEnable = true;
  Raster.StippleFactor = 1;
  Raster.StipplePattern = 0b1010; // columns 0/2 off, 1/3 on
  Expected<GraphicsPipeline> Pipeline =
      buildPipeline(Ctx, Raster, PrimitiveTopology::LineList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  // A horizontal line starting exactly at the target's left edge, so each
  // pixel column's arc-length distance from the line's start equals its
  // own column index plus one half (its pixel center).
  Scene.VertexData = {
      -1.0f, 0.25f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
      1.0f,  0.25f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  EXPECT_EQ(texel(0, 1)[3], 0);
  EXPECT_EQ(texel(1, 1)[3], 255);
  EXPECT_EQ(texel(2, 1)[3], 0);
  EXPECT_EQ(texel(3, 1)[3], 255);
}

// roadmap F5: `LineRasterizationMode::RectangularSmooth` feathers the
// line's edge over 1 pixel, writing a fractional coverage into the
// fragment's alpha instead of `Rectangular`'s binary in/out test.
TEST(ExecutorTest, RectangularSmoothLineAntialiasesItsEdge) {
  Context Ctx;
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.LineMode = LineRasterizationMode::RectangularSmooth;
  Raster.LineWidth = 1.0f;
  Expected<GraphicsPipeline> Pipeline =
      buildPipeline(Ctx, Raster, PrimitiveTopology::LineList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  // A horizontal line whose centerline sits at screen y = 1.75, 0.25
  // pixels off of row 1's center -- close enough to fully light row 1
  // were this `Rectangular`, but chosen here specifically so neither
  // covered row's coverage falls exactly on a 0.0/1.0 clamp boundary.
  Scene.VertexData = {
      -1.0f, 0.125f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
      1.0f,  0.125f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  // Row 1 (center 1.5, |edge| = 0.25): coverage = 1 - 0.25 = 0.75.
  EXPECT_NEAR(texel(0, 1)[3], 0.75 * 255, 2);
  // Row 2 (center 2.5, |edge| = 0.75): coverage = 1 - 0.75 = 0.25.
  EXPECT_NEAR(texel(0, 2)[3], 0.25 * 255, 2);
  // Row 0 (center 0.5, |edge| = 1.25) and row 3 (center 3.5, |edge| =
  // 2.25) are both fully outside the 1-pixel feather and get no coverage.
  EXPECT_EQ(texel(0, 0)[3], 0);
  EXPECT_EQ(texel(0, 3)[3], 0);
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

/// (Roadmap H4j) A lone triangle's own outer boundary edge (no
/// edge-sharing partner triangle at all) must give the same well-defined
/// inside/outside answer the top-left tie-break gives a *shared* edge:
/// a sample landing exactly on it belongs to at most one side. This
/// triangle's hypotenuse is the anti-diagonal of the 4x4 viewport
/// (screen `x + y == 4`), which four of the sixteen pixel centers
/// (`(0.5,3.5)`, `(1.5,2.5)`, `(2.5,1.5)`, `(3.5,0.5)`) land exactly on;
/// the corrected `isTopLeftEdge` polarity (this edge walks
/// top-right-to-bottom-left, i.e. `Dy < 0`, neither the horizontal+
/// leftward "top" case nor the downward "left" case) excludes all four,
/// leaving exactly the 6 pixels strictly inside (`x + y < 3`) filled.
/// Before H4j's fix, the old (backwards) polarity included all four
/// boundary pixels too, matching this bug's `glsl_triangles_*` CTS
/// symptom of an exact off-by-one row/column fill count.
TEST(ExecutorTest, TopLeftTieBreakExcludesALoneTrianglesOwnBoundaryEdge) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // Screen-space corners (0,0), (4,0), (0,4) -- NDC (-1,1), (1,1), (-1,-1)
  // (the executor's `projectVertex` flips Y, NDC y=1 landing at screen
  // y=0): a right triangle covering the origin corner of the 4x4
  // viewport, whose hypotenuse is the anti-diagonal `x + y == 4`.
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, 1.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0 = screen (0,0)
      1.0f,  1.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1 = screen (4,0)
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2 = screen (0,4)
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t Y = 0; Y != 4; ++Y) {
    for (uint32_t X = 0; X != 4; ++X) {
      uint32_t I = Y * 4 + X;
      const uint8_t *Texel = Scene.AttachmentStorage.data() + I * 4;
      bool IsRed = Texel[0] == 255 && Texel[1] == 0 && Texel[3] == 255;
      bool IsClear = Texel[3] == 0;
      // Strictly inside the hypotenuse (x + y < 3): must be filled red.
      // Exactly on it (x + y == 3, the four boundary pixel centers): must
      // be excluded (left as the untouched, transparent clear color).
      if (X + Y < 3)
        EXPECT_TRUE(IsRed) << "texel (" << X << "," << Y << ")";
      else if (X + Y == 3)
        EXPECT_TRUE(IsClear) << "texel (" << X << "," << Y
                             << ") should be excluded by the top-left rule";
      else
        EXPECT_TRUE(IsClear) << "texel (" << X << "," << Y << ")";
    }
  }
}

/// (Roadmap H4j) Two triangles sharing an exact edge must give a sample
/// landing on that edge to exactly one of them, even when the edge is
/// neither axis-aligned nor at a "nice" fraction -- the scenario the
/// tessellator's own crack-free bridging produces for a non-trivial
/// tessellation factor. `A`/`B` below are two float32 screen positions
/// (reached through the executor's own NDC-to-screen `projectVertex`
/// transform, not hand-picked screen coordinates) chosen so that pixel
/// (16,16)'s sample point (16.5,16.5) lies, in exact real-number math,
/// almost exactly on segment `A`-`B`: evaluating the coverage test's edge
/// function in `float` independently from each triangle's own vertex
/// order (`edgeFn(A,B,P)` for one, `edgeFn(B,A,P)` for the other) rounds
/// *both* to a spuriously negative value, leaving the pixel covered by
/// neither triangle -- a rasterization crack. Evaluating in `double`
/// (`edgeFnD`) resolves the tie in exactly one triangle's favor.
TEST(ExecutorTest,
     DoublePrecisionEdgeTestClosesAFloatRoundingCrackBetweenAdjacentTriangles) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // A quadrilateral covering the whole 64x64 viewport, split along the
  // diagonal A-B into two CCW triangles (A,B,(0,0)) and (B,A,(64,64)):
  // together they must leave no gap, including at the crack-prone pixel
  // (16,16) their shared diagonal passes almost exactly through. NDC
  // (-1,1) projects to screen (0,0) and NDC (1,-1) to screen (64,64)
  // (`projectVertex` flips Y).
  std::vector<float> VertexData = {
      // clang-format off
      -0.8589868f, 0.2970691f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // A, red
      0.32081833f, 0.88697165f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // B
      -1.0f,       1.0f,        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // (0,0)
      0.32081833f, 0.88697165f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // B, green
      -0.8589868f, 0.2970691f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // A
      1.0f,        -1.0f,       0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // (64,64)
      // clang-format on
  };
  std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
  std::vector<uint8_t> Storage(64u * 64u * 4u, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, 64, 64};
  std::array<AttachmentView, 1> Attachs{Color};
  std::vector<VertexBufferBinding> Bindings = {VertexBufferBinding{
      0, 28,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attributes}};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 64, 64};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 6;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  // A neighborhood around the exact crack pixel this test was built
  // around: every one of these must be covered by exactly one of the two
  // triangles (non-zero alpha), never left as the untouched clear color.
  // (The two triangles' *other* two edges legitimately exclude some
  // far-off pixels near the viewport's own corners per the top-left rule;
  // this test only asserts about the shared diagonal's own neighborhood.)
  for (int32_t DY = -2; DY <= 2; ++DY) {
    for (int32_t DX = -2; DX <= 2; ++DX) {
      uint32_t X = 16 + DX, Y = 16 + DY;
      uint32_t I = Y * 64 + X;
      const uint8_t *Texel = Storage.data() + I * 4;
      EXPECT_NE(Texel[3], 0) << "texel (" << X << "," << Y
                             << ") uncovered by either triangle (crack)";
    }
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

// Roadmap H2b: a depth-only pipeline (zero color attachments, no fragment
// shader color output) is legal Vulkan (`dEQP-VK.multiview.depth_without_
// fragment_shader`'s own shape) and must render successfully rather than
// being rejected for lacking a color attachment.
TEST(ExecutorTest, RendersWithZeroColorAttachments) {
  Context Ctx;

  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position)};
  constexpr char DepthOnlyVertexShaderIR[] = R"(
    define void @vs_main() #0 {
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
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, DepthOnlyVertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  // A fragment stage with no color output at all -- depth writes alone,
  // exactly like `dEQP-VK.multiview.depth_without_fragment_shader`'s own
  // pipeline shape.
  EntrySignature FSSig;
  constexpr char NoOutputFragmentShaderIR[] = R"(
    define void @fs_main() #0 {
      ret void
    }
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, NoOutputFragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  DepthState Depth;
  Depth.TestEnable = true;
  Depth.WriteEnable = true;
  Depth.Compare = CompareOp::Less;
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, Depth,
      BlendMode::Replace, /*SampleCount=*/1, /*Attachments=*/{}, StencilState{},
      /*ColorBlends=*/{}, /*LogicOpEnable=*/false, LogicOp::Copy,
      std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false);

  TriangleScene Scene;
  Scene.BindDepth = true;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2
  };
  PreparedDraw Draw = Scene.prepare();
  Draw.Attachments = {}; // No color attachments at all.
  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());
  // Depth is still written for every covered texel even with no color
  // attachment to shade.
  for (uint32_t I = 0; I != 16; ++I)
    EXPECT_FLOAT_EQ(Scene.DepthStorage[I], 0.0f) << "texel " << I;
}

// Roadmap H2j: unlike `RendersWithZeroColorAttachments` above (whose
// fragment stage is present but writes no color output), a pipeline may
// omit the fragment stage entirely -- `GraphicsPipeline`'s own
// `FragmentStage` is `nullptr` -- and still clip/rasterize/early-depth-test
// correctly, with no per-fragment shading (`FS.invokeFragments`) ever
// running at all.
TEST(ExecutorTest, RendersWithNoFragmentStage) {
  Context Ctx;

  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position)};
  constexpr char DepthOnlyVertexShaderIR[] = R"(
    define void @vs_main() #0 {
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
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, DepthOnlyVertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  DepthState Depth;
  Depth.TestEnable = true;
  Depth.WriteEnable = true;
  Depth.Compare = CompareOp::Less;
  GraphicsPipeline Pipeline(
      std::move(*VS), /*FragmentStage=*/nullptr,
      PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, Depth,
      BlendMode::Replace, /*SampleCount=*/1, /*Attachments=*/{}, StencilState{},
      /*ColorBlends=*/{}, /*LogicOpEnable=*/false, LogicOp::Copy,
      std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false);
  EXPECT_FALSE(Pipeline.hasFragmentStage());

  TriangleScene Scene;
  Scene.BindDepth = true;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2
  };
  PreparedDraw Draw = Scene.prepare();
  Draw.Attachments = {}; // No color attachments at all.
  uint64_t PassedSamples = 0;
  Draw.PassedSampleCounter = &PassedSamples;
  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());
  // Depth is still written for every covered texel by the early
  // depth test/write alone, with no fragment stage ever invoked.
  for (uint32_t I = 0; I != 16; ++I)
    EXPECT_FLOAT_EQ(Scene.DepthStorage[I], 0.0f) << "texel " << I;
  // Occlusion-query bookkeeping still runs off the early test's own result,
  // one sample per one of the 16 fully-covered texels.
  EXPECT_EQ(PassedSamples, 16u);
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
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
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

/// (roadmap F8) `PreparedDraw::ColorAttachmentLocations`, the same shape
/// `VkRenderingAttachmentLocationInfo::pColorAttachmentLocations` uses:
/// swapping which fragment output location writes which attachment swaps
/// the two colors `RendersToMultipleColorAttachments` above renders,
/// relative to the identity mapping that test exercises by omission.
TEST(ExecutorTest,
     ColorAttachmentLocationsRemapsWhichAttachmentEachOutputWrites) {
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
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;
  // Swap: location 0 (red) now writes attachment 1, location 1 (cyan) now
  // writes attachment 0.
  std::array<uint32_t, 2> Locations = {1, 0};
  Draw.ColorAttachmentLocations = Locations;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());
  for (uint32_t I = 0; I != 16; ++I) {
    // Target 0 now gets location 1's cyan...
    EXPECT_EQ(Color0Storage[I * 4], 0) << "texel " << I;
    EXPECT_EQ(Color0Storage[I * 4 + 1], 255) << "texel " << I;
    // ...and target 1 gets location 0's red.
    EXPECT_EQ(Color1Storage[I * 4], 255) << "texel " << I;
    EXPECT_EQ(Color1Storage[I * 4 + 1], 0) << "texel " << I;
  }
}

/// (roadmap F8) `VK_ATTACHMENT_UNUSED` (`0xFFFFFFFF`) in `ColorAttachment
/// Locations` leaves the corresponding attachment untouched: no location
/// writes it, so it keeps whatever it already held rather than reading an
/// arbitrary fragment output.
TEST(ExecutorTest, ColorAttachmentLocationsUnusedLeavesAttachmentUnchanged) {
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
  // A distinctive pre-existing value (not black, not either shader output)
  // so a surviving "unchanged" attachment is unambiguous.
  std::array<uint8_t, 64> Color1Storage;
  for (size_t I = 0; I != Color1Storage.size(); I += 4) {
    Color1Storage[I] = 0x10;
    Color1Storage[I + 1] = 0x20;
    Color1Storage[I + 2] = 0x30;
    Color1Storage[I + 3] = 0xFF;
  }
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
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;
  // Location 0 still writes attachment 0; location 1 maps nowhere.
  std::array<uint32_t, 2> Locations = {0, 0xFFFFFFFFu};
  Draw.ColorAttachmentLocations = Locations;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());
  for (uint32_t I = 0; I != 16; ++I) {
    EXPECT_EQ(Color0Storage[I * 4], 255) << "texel " << I;
    EXPECT_EQ(Color0Storage[I * 4 + 1], 0) << "texel " << I;
    // Unchanged from its distinctive pre-existing value.
    EXPECT_EQ(Color1Storage[I * 4], 0x10) << "texel " << I;
    EXPECT_EQ(Color1Storage[I * 4 + 1], 0x20) << "texel " << I;
    EXPECT_EQ(Color1Storage[I * 4 + 2], 0x30) << "texel " << I;
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
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
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
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
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
    Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
    Draw.Scissors[0] = ScissorRect{0, 0, 64, 64};
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

//===----------------------------------------------------------------------===//
// Tessellation (roadmap H4)
//===----------------------------------------------------------------------===//
//
// A vertex/hull/patch-constant/domain/fragment chain driven through
// `executeDraws` with `PrimitiveTopology::PatchList`. Each stage below
// numbers its own signature elements independently -- the position varying
// is element 2 on the vertex stage, 0 on the hull stage, 3 on its output,
// and 1 on the domain stage -- so the executor's own
// `feme::graphics::linkPatchPipeline` call is what makes the chain work,
// not coincidentally-matching `ElementID`s.

// The tessellating pipeline's vertex stage. Unlike the vertex/fragment
// pipeline's own vertex shader above, it writes no `SV_Position` at all:
// its outputs are the patch's control points, and the *domain* stage is
// what feeds clipping/rasterization. Position becomes an ordinary
// location-1 varying, color stays at location 0.
constexpr char TessVertexShaderIR[] = R"(
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

// The hull control-point phase: a plain passthrough of this invocation's
// own input control point (`SV_OutputControlPointID`, element 2).
constexpr char TessHullShaderIR[] = R"(
  define void @hs_main() #0 {
    %id = call i32 @feme.stage.input.load.i32(i32 2, i32 0, i32 0, i32 0)
    %px = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 %id)
    %py = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 %id)
    %pz = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 %id)
    %cr = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 %id)
    %cg = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 %id)
    %cb = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 %id)
    %ca = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 %id)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %px, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %py, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %pz, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %cr, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 1, float %cg, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 2, float %cb, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 3, float %ca, i32 0)
    ret void
  }
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="hull" }
)";

// The patch-constant phase: three edge factors and one inner factor, all
// derived from the completed output patch's own first control point
// (`%x0 * 0 + K`, so the arithmetic is real but the value is fixed) --
// enough to prove the phase really does see the hull stage's output.
// `%K` is filled in per test by `formatPatchConstantIR` below.
constexpr char TessPatchConstantShaderIRTemplate[] = R"(
  define void @pc_main() #0 {
    %x0 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %zero = fmul float %x0, 0.0
    %f = fadd float %zero, FACTOR
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %f, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 1, i32 0, float %f, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 2, i32 0, float %f, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %f, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="hull" }
)";

// The domain stage: barycentric evaluation of the three control points at
// this invocation's own `SV_DomainLocation` (element 0), writing the
// result as `SV_Position` (element 3) and the interpolated color varying
// (element 4, location 0) the fragment stage consumes.
constexpr char TessDomainShaderIR[] = R"(
  define void @ds_main() #0 {
    %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    %w = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
    %x0 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
    %y0 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
    %z0 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
    %x1 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 1)
    %y1 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 1)
    %z1 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 1)
    %x2 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 2)
    %y2 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 2)
    %z2 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 2)
    %xu = fmul float %x0, %u
    %xv = fmul float %x1, %v
    %xw = fmul float %x2, %w
    %xa = fadd float %xu, %xv
    %x = fadd float %xa, %xw
    %yu = fmul float %y0, %u
    %yv = fmul float %y1, %v
    %yw = fmul float %y2, %w
    %ya = fadd float %yu, %yv
    %y = fadd float %ya, %yw
    %zu = fmul float %z0, %u
    %zv = fmul float %z1, %v
    %zw = fmul float %z2, %w
    %za = fadd float %zu, %zv
    %z = fadd float %za, %zw
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %x, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %y, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %z, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float 1.0, i32 0)
    %c0r = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
    %c0g = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 1, i32 0)
    %c0b = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 2, i32 0)
    %c0a = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 3, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %c0r, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 1, float %c0g, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 2, float %c0b, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 3, float %c0a, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="domain" }
)";

std::string formatPatchConstantIR(StringRef Factor) {
  std::string IR = TessPatchConstantShaderIRTemplate;
  size_t Pos = IR.find("FACTOR");
  IR.replace(Pos, strlen("FACTOR"), Factor.str());
  return IR;
}

/// Builds the vertex/hull/patch-constant/domain/fragment pipeline the five
/// shaders above implement, tessellating a three-control-point patch over
/// the triangle domain with the given (uniform) tessellation factor.
Expected<GraphicsPipeline> buildTessellatedPipeline(Context &Ctx,
                                                    StringRef Factor,
                                                    uint32_t AttachmentSize) {
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 3, /*Location=*/1),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, TessVertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  if (!VS)
    return VS.takeError();

  EntrySignature HSSig;
  SignatureElement ControlPointID =
      makeElement(2, SignatureDirection::Input, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::OutputControlPointID);
  ControlPointID.ComponentType = SignatureComponentType::UInt;
  HSSig.Elements = {makeElement(0, SignatureDirection::Input, 3,
                                /*Location=*/1),
                    makeElement(1, SignatureDirection::Input, 4,
                                /*Location=*/0),
                    ControlPointID,
                    makeElement(3, SignatureDirection::Output, 3,
                                /*Location=*/1),
                    makeElement(4, SignatureDirection::Output, 4,
                                /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> HS =
      compileStage(Ctx, TessHullShaderIR, "hs_main", HSSig, ShaderStage::Hull);
  if (!HS)
    return HS.takeError();

  EntrySignature PCSig;
  SignatureElement Edges =
      makeElement(1, SignatureDirection::PatchOutput, 1,
                  /*Location=*/std::nullopt,
                  SignatureSystemValue::TessFactorEdge, /*RowCount=*/3);
  Edges.Frequency = SignatureFrequency::PerPatch;
  SignatureElement Inside =
      makeElement(2, SignatureDirection::PatchOutput, 1,
                  /*Location=*/std::nullopt,
                  SignatureSystemValue::TessFactorInside, /*RowCount=*/1);
  Inside.Frequency = SignatureFrequency::PerPatch;
  PCSig.Elements = {makeElement(0, SignatureDirection::Input, 3,
                                /*Location=*/1),
                    Edges, Inside};
  std::string PCIR = formatPatchConstantIR(Factor);
  Expected<std::shared_ptr<CompiledStage>> PCS =
      compileStage(Ctx, PCIR, "pc_main", PCSig, ShaderStage::Hull);
  if (!PCS)
    return PCS.takeError();

  EntrySignature DSSig;
  DSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/std::nullopt,
                  SignatureSystemValue::DomainLocation),
      makeElement(1, SignatureDirection::Input, 3, /*Location=*/1),
      makeElement(2, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(4, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> DS = compileStage(
      Ctx, TessDomainShaderIR, "ds_main", DSSig, ShaderStage::Domain);
  if (!DS)
    return DS.takeError();

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  if (!FS)
    return FS.takeError();

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, AttachmentSize, AttachmentSize}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::PatchList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  TessellationState Tess;
  Tess.Domain = TessellatorDomain::Triangle;
  Tess.Partitioning = TessPartitioning::Integer;
  Tess.OutputPrimitive = TessOutputPrimitive::TriangleCcw;
  Tess.InputControlPointCount = 3;
  Tess.OutputControlPointCount = 3;
  Pipeline.setTessellationStages(std::move(*HS), std::move(*PCS),
                                 std::move(*DS), Tess);
  return Pipeline;
}

/// Renders one full-viewport red patch through \p Pipeline into an
/// \p Size x \p Size R8G8B8A8 attachment.
std::vector<uint8_t> renderTessellatedPatch(const GraphicsPipeline &Pipeline,
                                            uint32_t Size) {
  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // control point 0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // control point 1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // control point 2
  };
  std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
  std::vector<VertexBufferBinding> Bindings = {VertexBufferBinding{
      0, 28,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attributes}};

  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, Size,
                       Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;
  EXPECT_THAT_ERROR(executeDraws(Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());
  return Storage;
}

TEST(ExecutorTest, TessellatedPatchListCoversTheWholeViewport) {
  Context Ctx;
  // Factor 1 emits the undivided patch (a single triangle); factor 4
  // subdivides it into a 4-resolution lattice. Both must rasterize to the
  // exact same watertight full-viewport fill -- the subdivided one only if
  // every generated domain point really did run through the domain stage
  // and land in the flat, per-patch-based rasterization block.
  for (StringRef Factor : {"1.0", "4.0"}) {
    Expected<GraphicsPipeline> Pipeline =
        buildTessellatedPipeline(Ctx, Factor, /*AttachmentSize=*/8);
    ASSERT_THAT_EXPECTED(Pipeline, Succeeded());
    std::vector<uint8_t> Storage =
        renderTessellatedPatch(*Pipeline, /*Size=*/8);
    for (uint32_t I = 0; I != 8u * 8u; ++I) {
      EXPECT_EQ(Storage[I * 4 + 0], 255)
          << "factor " << Factor << " texel " << I;
      EXPECT_EQ(Storage[I * 4 + 1], 0);
      EXPECT_EQ(Storage[I * 4 + 2], 0);
      EXPECT_EQ(Storage[I * 4 + 3], 255);
    }
  }
}

TEST(ExecutorTest, TessellationFactorZeroCullsTheWholePatch) {
  Context Ctx;
  // A non-positive tessellation factor culls the patch entirely (see
  // `feme::graphics::TessFactors`), so the tessellator emits no domain
  // points, the domain stage runs zero invocations, and nothing is
  // rasterized at all.
  Expected<GraphicsPipeline> Pipeline =
      buildTessellatedPipeline(Ctx, "0.0", /*AttachmentSize=*/4);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());
  std::vector<uint8_t> Storage = renderTessellatedPatch(*Pipeline, /*Size=*/4);
  for (uint8_t Texel : Storage)
    EXPECT_EQ(Texel, 0);
}

TEST(ExecutorTest, RejectsAPatchListWithoutTessellationStages) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline =
      buildPipeline(Ctx, RasterState{CullMode::None, FrontFace::Clockwise},
                    PrimitiveTopology::PatchList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f,
      0.0f,  0.0f,  1.0f, -1.0f, 3.0f, 0.0f, 1.0f, 0.0f, 0.0f,  1.0f,
  };
  PreparedDraw Draw = Scene.prepare();
  EXPECT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1), Failed());
}

TEST(ExecutorTest, RejectsAPatchListDrawWithAPartialPatch) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline =
      buildTessellatedPipeline(Ctx, "1.0", /*AttachmentSize=*/4);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
  };
  std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
  std::vector<VertexBufferBinding> Bindings = {VertexBufferBinding{
      0, 28,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attributes}};
  std::vector<uint8_t> Storage(4u * 4u * 4u, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
  Draw.VertexBuffers = Bindings;
  // Two vertices is not a whole three-control-point patch.
  DrawCommand Cmd;
  Cmd.VertexCount = 2;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;
  EXPECT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1), Failed());
}

// (Roadmap H5d) A geometry-stage passthrough: reads all three of a
// triangle's assembled vertices' position/color, emits them unchanged as
// one triangle strip, and closes it -- the trivial "input primitive class
// matches output primitive class" case chaining `Executor::executeDraws`
// into a geometry stage needs to get exactly right before H5e's real
// SPIR-V-sourced geometry pipelines can be trusted. Unlike
// `TessVertexShaderIR`'s pairing with the domain stage, this geometry
// stage -- not the vertex stage -- is what finally writes `SV_Position`
// (element 2): the vertex stage's own position output (element 2,
// location 1) stays a plain varying until the geometry stage reads it.
constexpr char PassthroughGeometryShaderIR[] = R"(
  define void @gs_main() #0 {
    %p0x = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %p0y = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    %p0z = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
    %c0r = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
    %c0g = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
    %c0b = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
    %c0a = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %p0x, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %p0y, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %p0z, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %c0r, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %c0g, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %c0b, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float %c0a, i32 0)
    call void @feme.stage.stream.emit(i32 0)

    %p1x = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 1)
    %p1y = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 1)
    %p1z = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 1)
    %c1r = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 1)
    %c1g = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 1)
    %c1b = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 1)
    %c1a = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 1)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %p1x, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %p1y, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %p1z, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %c1r, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %c1g, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %c1b, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float %c1a, i32 0)
    call void @feme.stage.stream.emit(i32 0)

    %p2x = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 2)
    %p2y = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 2)
    %p2z = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 2)
    %c2r = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 2)
    %c2g = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 2)
    %c2b = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 2)
    %c2a = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 2)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %p2x, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %p2y, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %p2z, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %c2r, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %c2g, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %c2b, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float %c2a, i32 0)
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.cut(i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.stream.emit(i32)
  declare void @feme.stage.stream.cut(i32)
  attributes #0 = { "feme.shader.stage"="geometry" }
)";

/// Builds a vertex/geometry/fragment `GraphicsPipeline` -- an ordinary
/// `TriangleList` draw whose vertex stage leaves position an unconsumed
/// varying (`TessVertexShaderIR`, reused unmodified from the tessellation
/// tests above) and whose geometry stage (`PassthroughGeometryShaderIR`)
/// both produces `SV_Position` and passes every vertex through unchanged.
Expected<GraphicsPipeline>
buildPassthroughGeometryPipeline(Context &Ctx, uint32_t AttachmentSize) {
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 3, /*Location=*/1),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, TessVertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  if (!VS)
    return VS.takeError();

  EntrySignature GSSig;
  GSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/1),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> GS =
      compileStage(Ctx, PassthroughGeometryShaderIR, "gs_main", GSSig,
                   ShaderStage::Geometry);
  if (!GS)
    return GS.takeError();

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  if (!FS)
    return FS.takeError();

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, AttachmentSize, AttachmentSize}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  GeometryState Geom;
  Geom.InputPrimitive = GeometryInputPrimitive::Triangles;
  Geom.OutputPrimitive = GeometryOutputPrimitive::TriangleStrip;
  Geom.MaxOutputVertices = 3;
  Pipeline.setGeometryStage(std::move(*GS), Geom);
  return Pipeline;
}

TEST(ExecutorTest, GeometryStagePassesThroughATriangleCoveringTheViewport) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline =
      buildPassthroughGeometryPipeline(Ctx, /*AttachmentSize=*/8);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // A triangle covering the whole [-1, 1] NDC square (and more), CCW-wound,
  // every vertex red -- the geometry stage's own `SV_Position` output is
  // what clipping/rasterization now reads, so this only rasterizes to a
  // solid-red viewport if that value made it through the geometry stage
  // unchanged.
  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2
  };
  std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
  std::vector<VertexBufferBinding> Bindings = {VertexBufferBinding{
      0, 28,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attributes}};

  uint32_t Size = 8;
  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, Size,
                       Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  for (uint32_t I = 0; I != Size * Size; ++I) {
    const uint8_t *Texel = Storage.data() + I * 4;
    EXPECT_EQ(Texel[0], 255) << "texel " << I;
    EXPECT_EQ(Texel[1], 0) << "texel " << I;
    EXPECT_EQ(Texel[2], 0) << "texel " << I;
    EXPECT_EQ(Texel[3], 255) << "texel " << I;
  }
}

// (Roadmap H5e-b) A geometry entry point that emits no vertices at all --
// `dEQP-VK.geometry.emit.*_emit_0_end_0`'s degenerate `void main(void) {}`
// bodies, which call neither `feme.stage.stream.emit` nor
// `feme.stage.stream.cut` -- reads nothing and writes nothing.
constexpr char EmptyGeometryShaderIR[] = R"(
define void @gs_main() #0 {
  ret void
}
attributes #0 = { "feme.shader.stage"="geometry" }
)";

/// Builds a vertex/geometry/fragment `GraphicsPipeline` whose geometry
/// stage (`EmptyGeometryShaderIR`) has an entirely empty `EntrySignature`
/// -- mirroring `dEQP-VK.geometry.emit.*_emit_0_end_0`'s reflected shape,
/// where SPIR-V's own "only the entry point's *used* interface variables
/// are listed" rule means a shader that writes nothing produces no
/// signature elements at all, not just a missing `SV_Position`.
Expected<GraphicsPipeline>
buildNoEmitGeometryPipeline(Context &Ctx, uint32_t AttachmentSize) {
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

  EntrySignature GSSig; // Deliberately empty: this stage emits nothing.
  Expected<std::shared_ptr<CompiledStage>> GS = compileStage(
      Ctx, EmptyGeometryShaderIR, "gs_main", GSSig, ShaderStage::Geometry);
  if (!GS)
    return GS.takeError();

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  if (!FS)
    return FS.takeError();

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, AttachmentSize, AttachmentSize}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  GeometryState Geom;
  Geom.InputPrimitive = GeometryInputPrimitive::Triangles;
  Geom.OutputPrimitive = GeometryOutputPrimitive::TriangleStrip;
  Geom.MaxOutputVertices = 0;
  Pipeline.setGeometryStage(std::move(*GS), Geom);
  return Pipeline;
}

// (Roadmap H5e-b) A draw against a pipeline whose geometry stage's own
// signature is entirely empty must be a legal no-op: before this fix,
// `executeDraws` unconditionally rejected any pre-rasterization stage
// missing an `SV_Position` output, including this one, with "the last
// pre-rasterization stage does not write an SV_Position output" -- even
// though a stage that emits nothing can never contribute anything to
// rasterization regardless.
TEST(ExecutorTest, ExecutesDrawsAsNoOpWhenGeometryStageNeverEmits) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline =
      buildNoEmitGeometryPipeline(Ctx, /*AttachmentSize=*/4);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2
  };
  std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
  std::vector<VertexBufferBinding> Bindings = {VertexBufferBinding{
      0, 28,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attributes}};

  uint32_t Size = 4;
  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, Size,
                       Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // No error, and the color attachment stays untouched: a geometry stage
  // that emits nothing must never rasterize a single pixel.
  for (uint8_t Byte : Storage)
    EXPECT_EQ(Byte, 0);
}

// (Roadmap H5d-a) A geometry stage declaring `GeometryState::Invocations ==
// 2`: reads its own `gl_InvocationID` (element 2) and emits a full-viewport
// triangle strip colored red for invocation 0, green for invocation 1 --
// both from the *same* single input triangle, so this only produces two
// invocations' worth of output (not one) if `Executor::executeDraws`
// widens its invocation-building loop by `Invocations` as H5d-a requires.
// Since both invocations' triangles cover the same pixels and painting is
// `BlendMode::Replace`, the final image is whichever invocation's strip
// `mergeGeometryStreamsInLaneOrder` places *last* -- solid green only if
// invocation 1 (lane 1) is correctly ordered after invocation 0 (lane 0),
// confirming "N invocations per primitive means N lanes per primitive"
// with a real test rather than assuming it.
constexpr char InvocationIDGeometryShaderIR[] = R"(
  define void @gs_main() #0 {
    %iid = call i32 @feme.stage.input.load.i32(i32 2, i32 0, i32 0, i32 0)
    %isinv0 = icmp eq i32 %iid, 0
    %selr = select i1 %isinv0, float 1.0, float 0.0
    %selg = select i1 %isinv0, float 0.0, float 1.0

    %p0x = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %p0y = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    %p0z = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %p0x, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %p0y, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %p0z, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %selr, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 1, float %selg, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.stream.emit(i32 0)

    %p1x = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 1)
    %p1y = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 1)
    %p1z = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 1)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %p1x, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %p1y, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %p1z, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %selr, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 1, float %selg, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.stream.emit(i32 0)

    %p2x = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 2)
    %p2y = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 2)
    %p2z = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 2)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %p2x, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %p2y, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %p2z, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %selr, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 1, float %selg, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.cut(i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.stream.emit(i32)
  declare void @feme.stage.stream.cut(i32)
  attributes #0 = { "feme.shader.stage"="geometry" }
)";

TEST(ExecutorTest, GeometryStageInvocationsRunOncePerDeclaredInvocationCount) {
  Context Ctx;
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 3, /*Location=*/1),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, TessVertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  EntrySignature GSSig;
  SignatureElement InvocationID =
      makeElement(2, SignatureDirection::Input, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::InvocationID);
  InvocationID.ComponentType = SignatureComponentType::UInt;
  GSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/1),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/0),
      InvocationID,
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(4, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> GS =
      compileStage(Ctx, InvocationIDGeometryShaderIR, "gs_main", GSSig,
                   ShaderStage::Geometry);
  ASSERT_THAT_EXPECTED(GS, Succeeded());

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  uint32_t Size = 8;
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, Size, Size}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  GeometryState Geom;
  Geom.InputPrimitive = GeometryInputPrimitive::Triangles;
  Geom.OutputPrimitive = GeometryOutputPrimitive::TriangleStrip;
  Geom.Invocations = 2;
  Geom.MaxOutputVertices = 3;
  Pipeline.setGeometryStage(std::move(*GS), Geom);

  // One triangle covering the whole [-1, 1] NDC square.
  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2
  };
  std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
  std::vector<VertexBufferBinding> Bindings = {VertexBufferBinding{
      0, 28,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attributes}};

  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, Size,
                       Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;
  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // Solid green: invocation 1's strip must be the last one painted, i.e.
  // must come after invocation 0's in the merged stream's own lane order.
  for (uint32_t I = 0; I != Size * Size; ++I) {
    const uint8_t *Texel = Storage.data() + I * 4;
    EXPECT_EQ(Texel[0], 0) << "texel " << I;
    EXPECT_EQ(Texel[1], 255) << "texel " << I;
    EXPECT_EQ(Texel[2], 0) << "texel " << I;
    EXPECT_EQ(Texel[3], 255) << "texel " << I;
  }
}

TEST(ExecutorTest, RejectsAdjacencyTopologyWithoutAGeometryStage) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleListWithAdjacency);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f,
      0.0f,  0.0f,  1.0f, -1.0f, 3.0f, 0.0f, 1.0f, 0.0f, 0.0f,  1.0f, 0.0f,
      0.0f,  0.0f,  1.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f, 0.0f,  1.0f, 0.0f,
      0.0f,  1.0f,  0.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();
  EXPECT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1), Failed());
}

// (Roadmap H6e) Chains the mesh path into `executeDraws`: this reuses
// `CompiledStageTest.cpp`'s own "write GroupID.x, doubled, into a bound UAV
// buffer, via groupshared-and-barrier cooperation" shape (roadmap H6c) to
// prove which mesh (and, in the next test, task) workgroups actually run
// and in what group-count shape, since no compiled mesh/task entry point
// can yet write `ActualVertexCount`/`VertexOutputs`/`MeshGroupCount` from
// real IR (roadmap H6h/H6i, see this milestone's own commit message and
// agent_thoughts.md) -- meaning every meshlet these tests' own mesh stage
// assembles is legitimately empty, and the bound color attachment must
// stay entirely untouched, exactly mirroring `ExecutesDrawsAsNoOpWhen
// GeometryStageNeverEmits`'s own precedent for a geometry stage.
constexpr char MeshGroupIDDoublingShaderIR[] = R"(
  @shared = internal addrspace(3) global [4 x i32] undef
  define void @ms_main() #0 {
    %h = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32 0, i1 false)
    %gid = call i32 @llvm.dx.group.id(i32 0)
    %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
    store i32 %gid, ptr addrspace(3) %ptr
    call void @llvm.dx.group.memory.barrier.with.group.sync()
    %val = load i32, ptr addrspace(3) %ptr
    %doubled = mul i32 %val, 2
    %offset = mul i32 %gid, 4
    call void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %doubled)
    ret void
  }
  declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32, i1)
  declare void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
  declare i32 @llvm.dx.group.id(i32)
  declare void @llvm.dx.group.memory.barrier.with.group.sync()
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

// Same shape as `MeshGroupIDDoublingShaderIR`, tagged as the task
// (amplification) stage instead; the requested mesh-workgroup count it
// would write via `EmitMeshTasksEXT` is left unwritten, per H6c/H6d's own
// documented scope (roadmap H6h/H6i), so it always requests zero mesh
// workgroups.
constexpr char TaskGroupIDDoublingShaderIR[] = R"(
  @shared = internal addrspace(3) global [4 x i32] undef
  define void @ts_main() #0 {
    %h = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32 0, i1 false)
    %gid = call i32 @llvm.dx.group.id(i32 0)
    %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
    store i32 %gid, ptr addrspace(3) %ptr
    call void @llvm.dx.group.memory.barrier.with.group.sync()
    %val = load i32, ptr addrspace(3) %ptr
    %doubled = mul i32 %val, 2
    %offset = mul i32 %gid, 4
    call void @llvm.dx.resource.store.rawbuffer.i32(
        target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %doubled)
    ret void
  }
  declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32, i1)
  declare void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
  declare i32 @llvm.dx.group.id(i32)
  declare void @llvm.dx.group.memory.barrier.with.group.sync()
  attributes #0 = { "hlsl.shader"="amplification" "hlsl.numthreads"="1,1,1" }
)";

// A fragment stage with no inputs at all -- every rasterized fragment (were
// any ever produced) would be solid red -- so the mesh-path tests below
// need no vertex-output varying linkage at all, only `SV_Position`.
constexpr char SolidRedFragmentShaderIR[] = R"(
  define void @fs_main() #0 {
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
    ret void
  }
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

/// Builds a mesh (and, if \p WithTaskStage, task) `GraphicsPipeline`: the
/// mesh stage only declares an `SV_Position` output (roadmap H6e's own
/// `RasterizePrimitives` requires one, per the vertex/geometry path's own
/// long-standing rule) and cooperates via groupshared/barriers exactly
/// like `CompiledStageTest`'s own mesh/task cases -- real per-vertex
/// output-writing IR is blocked pending roadmap H6h/H6i (see this file's
/// own comment above).
Expected<GraphicsPipeline> buildMeshPipeline(Context &Ctx, bool WithTaskStage,
                                             uint32_t AttachmentSize = 4) {
  EntrySignature MeshSig;
  MeshSig.Elements = {makeElement(0, SignatureDirection::Output, 4,
                                  /*Location=*/std::nullopt,
                                  SignatureSystemValue::Position)};
  Expected<std::shared_ptr<CompiledStage>> MS = compileStage(
      Ctx, MeshGroupIDDoublingShaderIR, "ms_main", MeshSig, ShaderStage::Mesh);
  if (!MS)
    return MS.takeError();

  std::shared_ptr<CompiledStage> TS;
  if (WithTaskStage) {
    EntrySignature TaskSig;
    Expected<std::shared_ptr<CompiledStage>> TSExp =
        compileStage(Ctx, TaskGroupIDDoublingShaderIR, "ts_main", TaskSig,
                     ShaderStage::Amplification);
    if (!TSExp)
      return TSExp.takeError();
    TS = std::move(*TSExp);
  }

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, SolidRedFragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  if (!FS)
    return FS.takeError();

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, AttachmentSize, AttachmentSize}};
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  MeshState Mesh;
  Mesh.OutputTopology = MeshOutputTopology::Triangles;
  Mesh.MaxOutputVertices = 3;
  Mesh.MaxOutputPrimitives = 1;
  Pipeline.setMeshStage(std::move(TS), std::move(*MS), Mesh);
  return Pipeline;
}

TEST(ExecutorTest,
     RunsEveryMeshWorkgroupDirectlyWhenNoTaskStageIsBoundAndRastersNothing) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline =
      buildMeshPipeline(Ctx, /*WithTaskStage=*/false);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  std::vector<int32_t> MeshBuffer(4, -1);
  cpu::FemeDescriptor Desc{};
  Desc.Data = MeshBuffer.data();
  Desc.SizeInBytes = MeshBuffer.size() * sizeof(int32_t);
  Desc.Kind = static_cast<uint32_t>(cpu::ResourceKind::Raw);
  Desc.Flags = FEME_DESCRIPTOR_UAV;

  uint32_t Size = 4;
  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, Size,
                       Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  Draw.Resources.ResourceHeap = ArrayRef<cpu::FemeDescriptor>(&Desc, 1);
  MeshDrawCommand MDC;
  MDC.GroupCount = {4, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // Every one of the 4 directly-dispatched mesh workgroups ran exactly
  // once, each writing its own GroupID.x doubled at its own slot.
  EXPECT_EQ(MeshBuffer, (std::vector<int32_t>{0, 2, 4, 6}));

  // No compiled mesh entry point can yet declare a non-zero vertex/
  // primitive count (roadmap H6h/H6i), so every assembled meshlet is
  // empty -- the color attachment must stay entirely untouched, exactly
  // as `ExecutesDrawsAsNoOpWhenGeometryStageNeverEmits` already
  // establishes for the geometry-stage counterpart of this same
  // "correctly wired but currently produces nothing" state.
  for (uint8_t B : Storage)
    EXPECT_EQ(B, 0);
}

TEST(
    ExecutorTest,
    TaskStageDispatchDrivesWhichMeshWorkgroupsRunAndNoneRunUntilItRequestsAny) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline =
      buildMeshPipeline(Ctx, /*WithTaskStage=*/true);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  std::vector<int32_t> TaskBuffer(4, -1);
  cpu::FemeDescriptor TaskDesc{};
  TaskDesc.Data = TaskBuffer.data();
  TaskDesc.SizeInBytes = TaskBuffer.size() * sizeof(int32_t);
  TaskDesc.Kind = static_cast<uint32_t>(cpu::ResourceKind::Raw);
  TaskDesc.Flags = FEME_DESCRIPTOR_UAV;

  uint32_t Size = 4;
  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, Size,
                       Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  Draw.Resources.ResourceHeap = ArrayRef<cpu::FemeDescriptor>(&TaskDesc, 1);
  MeshDrawCommand MDC;
  // With a task stage bound, `MeshDrawCommand::GroupCount` is the *task*
  // stage's own dispatch (mirroring `vkCmdDispatch`'s shape).
  MDC.GroupCount = {4, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // Every one of the 4 task workgroups ran exactly once (this milestone's
  // own "a task entry point's dispatch driving which mesh workgroups run
  // when one is bound" charter's task-dispatch half).
  EXPECT_EQ(TaskBuffer, (std::vector<int32_t>{0, 2, 4, 6}));

  // No compiled task entry point can yet write `MeshGroupCount` from real
  // IR (roadmap H6h/H6i), so every task workgroup above requested zero
  // mesh workgroups: the mesh stage itself never runs at all, and the
  // color attachment stays untouched, exactly like the no-task-stage test
  // above.
  for (uint8_t B : Storage)
    EXPECT_EQ(B, 0);
}

} // namespace
