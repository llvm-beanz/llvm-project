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

// (roadmap H7t) Like FragmentShaderIR above, but only stores its 3 color
// components (r, g, b) to SV_Target0 -- no alpha write at all, matching a
// `vec3` fragment output's own signature (`ComponentCount == 3`), legal
// per spec with the missing alpha reading back as its identity value
// (`1.0`).
constexpr char Vec3FragmentShaderIR[] = R"(
  define void @fs_main() #0 {
    %r = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %g = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    %b = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %r, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %g, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %b, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

// (roadmap H7x) The fragment-side counterpart of
// `ClipCullDistanceVertexShaderIR`: reads the interpolated
// `gl_ClipDistance[0]`/`gl_CullDistance[0]` system-value inputs (elements
// 0/1, no `Location` -- these link by `SystemValue` instead) straight
// into SV_Target0's R/G channels, letting a test observe whatever value
// the executor's own fragment/vertex linking resolved them to.
constexpr char ClipCullDistanceReadFragmentShaderIR[] = R"(
  define void @fs_main() #0 {
    %clip = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %cull = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %clip, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %cull, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.0, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

// (roadmap H7h) Like VertexShaderIR above, but with two extra scalar
// inputs (location 2: `gl_ClipDistance[0]`, location 3:
// `gl_CullDistance[0]`) passed straight through to their own system-value
// outputs (elements 6/7), rather than derived from position -- letting a
// test set each vertex's clip/cull-distance value independently of its
// position.
constexpr char ClipCullDistanceVertexShaderIR[] = R"(
  define void @vs_main() #0 {
    %px = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %py = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    %pz = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
    %cr = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
    %cg = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
    %cb = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
    %ca = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 0)
    %clip = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
    %cull = call float @feme.stage.input.load.f32(i32 3, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %px, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 1, float %py, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 2, float %pz, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 5, i32 0, i32 0, float %cr, i32 0)
    call void @feme.stage.output.store.f32(i32 5, i32 0, i32 1, float %cg, i32 0)
    call void @feme.stage.output.store.f32(i32 5, i32 0, i32 2, float %cb, i32 0)
    call void @feme.stage.output.store.f32(i32 5, i32 0, i32 3, float %ca, i32 0)
    call void @feme.stage.output.store.f32(i32 6, i32 0, i32 0, float %clip, i32 0)
    call void @feme.stage.output.store.f32(i32 7, i32 0, i32 0, float %cull, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="vertex" }
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

/// (roadmap H7h) Builds a `TriangleList` pipeline using
/// `ClipCullDistanceVertexShaderIR`: like `buildPipeline`'s own
/// color-passthrough pair, but with two extra per-vertex scalar inputs
/// (location 2/3) feeding a real `gl_ClipDistance[0]`/`gl_CullDistance[0]`
/// output apiece, letting a test set each vertex's clip/cull-distance
/// value independently of its position.
Expected<GraphicsPipeline> buildClipCullDistancePipeline(Context &Ctx,
                                                          RasterState Raster) {
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Input, 1, /*Location=*/2),
      makeElement(3, SignatureDirection::Input, 1, /*Location=*/3),
      makeElement(4, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(5, SignatureDirection::Output, 4, /*Location=*/0),
      makeElement(6, SignatureDirection::Output, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::ClipDistance, /*RowCount=*/1),
      makeElement(7, SignatureDirection::Output, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::CullDistance, /*RowCount=*/1)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, ClipCullDistanceVertexShaderIR, "vs_main", VSSig,
                  ShaderStage::Vertex);
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
  return GraphicsPipeline(std::move(*VS), std::move(*FS),
                          PrimitiveTopology::TriangleList, Raster,
                          DepthState{}, BlendMode::Replace,
                          /*SampleCount=*/1, std::move(Attachments),
                          StencilState{}, std::vector<BlendState>{BlendState{}});
}

/// (roadmap H7x) Like `buildClipCullDistancePipeline` above, but pairs
/// `ClipCullDistanceVertexShaderIR` with `ClipCullDistanceReadFragmentShaderIR`
/// instead of the ordinary color-passthrough one, so a test can observe
/// what the fragment stage's own interpolated read of
/// `gl_ClipDistance`/`gl_CullDistance` resolves to.
Expected<GraphicsPipeline>
buildClipCullDistanceReadPipeline(Context &Ctx, RasterState Raster) {
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Input, 1, /*Location=*/2),
      makeElement(3, SignatureDirection::Input, 1, /*Location=*/3),
      makeElement(4, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(5, SignatureDirection::Output, 4, /*Location=*/0),
      makeElement(6, SignatureDirection::Output, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::ClipDistance, /*RowCount=*/1),
      makeElement(7, SignatureDirection::Output, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::CullDistance, /*RowCount=*/1)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, ClipCullDistanceVertexShaderIR, "vs_main", VSSig,
                  ShaderStage::Vertex);
  if (!VS)
    return VS.takeError();

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::ClipDistance, /*RowCount=*/1),
      makeElement(1, SignatureDirection::Input, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::CullDistance, /*RowCount=*/1),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, ClipCullDistanceReadFragmentShaderIR, "fs_main", FSSig,
                  ShaderStage::Fragment);
  if (!FS)
    return FS.takeError();

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}};
  return GraphicsPipeline(std::move(*VS), std::move(*FS),
                          PrimitiveTopology::TriangleList, Raster,
                          DepthState{}, BlendMode::Replace,
                          /*SampleCount=*/1, std::move(Attachments),
                          StencilState{}, std::vector<BlendState>{BlendState{}});
}

/// Prepares a single-triangle draw against `buildClipCullDistancePipeline`'s
/// signature: interleaved position (xyz), color (rgba), clip-distance and
/// cull-distance, 9 floats/vertex.
PreparedDraw
prepareClipCullDistanceDraw(std::array<uint8_t, 64> &AttachmentStorage,
                            std::vector<float> &VertexData,
                            std::array<VertexBufferBinding, 1> &Bindings,
                            std::array<AttachmentView, 1> &Attachments,
                            AttachmentView &Color) {
  static const std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12},
      {2, cpu::ResourceFormat::R32_FLOAT, 28},
      {3, cpu::ResourceFormat::R32_FLOAT, 32}};
  Color = AttachmentView{AttachmentStorage, cpu::ResourceFormat::R8G8B8A8_UNORM,
                         4, 4};
  Attachments = {Color};
  Bindings = {VertexBufferBinding{
      0, 36,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attributes}};
  PreparedDraw Draw;
  Draw.Attachments = Attachments;
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = static_cast<uint32_t>(VertexData.size() / 9);
  Cmd.InstanceCount = 1;
  static std::array<DrawCommand, 1> Draws;
  Draws = {Cmd};
  Draw.Draws = Draws;
  return Draw;
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

/// (Roadmap H8b) Renders the same fully-covered, solid-color triangle as
/// `FillsFullyCoveredTriangleWithSolidColor`, but the color vertex
/// attribute (location 1, still bound to `buildPipeline`'s own float4
/// color input) is supplied in \p ColorFormat's raw bytes rather than
/// `R32G32B32A32_FLOAT` -- exercises `Executor.cpp`'s
/// `decodeAttribute`/`attributeComponentByteSize` new non-32-bit-scalar
/// cases through a real compiled pipeline, not just their component-count/
/// byte-size arithmetic in isolation.
void renderSolidColorTriangleWithAttributeFormat(
    cpu::ResourceFormat ColorFormat, ArrayRef<uint8_t> ColorBytes,
    std::array<uint8_t, 4> ExpectedTexel) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  const uint32_t PositionBytes = 12; // R32G32B32_FLOAT, 3 x 4 bytes.
  const uint32_t Stride =
      PositionBytes + static_cast<uint32_t>(ColorBytes.size());
  std::vector<uint8_t> VertexData(Stride * 3);
  auto writeVertex = [&](uint32_t I, std::array<float, 3> Pos) {
    memcpy(VertexData.data() + I * Stride, Pos.data(), PositionBytes);
    memcpy(VertexData.data() + I * Stride + PositionBytes, ColorBytes.data(),
           ColorBytes.size());
  };
  // A triangle covering the whole [-1, 1] NDC square (and more), CCW-wound.
  writeVertex(0, {-1.0f, -1.0f, 0.0f});
  writeVertex(1, {3.0f, -1.0f, 0.0f});
  writeVertex(2, {-1.0f, 3.0f, 0.0f});

  std::array<VertexAttribute, 2> Attributes = {
      VertexAttribute{0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      VertexAttribute{1, ColorFormat, PositionBytes}};
  std::array<uint8_t, 64> AttachmentStorage{};
  AttachmentView Color{AttachmentStorage, cpu::ResourceFormat::R8G8B8A8_UNORM,
                       4, 4};
  std::array<AttachmentView, 1> Attachments = {Color};
  std::array<VertexBufferBinding, 1> Bindings = {
      VertexBufferBinding{0, Stride, ArrayRef(VertexData), Attributes}};

  PreparedDraw Draw;
  Draw.Attachments = Attachments;
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t I = 0; I != 16; ++I) {
    const uint8_t *Texel = AttachmentStorage.data() + I * 4;
    EXPECT_EQ(Texel[0], ExpectedTexel[0]) << "texel " << I;
    EXPECT_EQ(Texel[1], ExpectedTexel[1]) << "texel " << I;
    EXPECT_EQ(Texel[2], ExpectedTexel[2]) << "texel " << I;
    EXPECT_EQ(Texel[3], ExpectedTexel[3]) << "texel " << I;
  }
}

TEST(ExecutorTest, VertexAttributeDecodesR16G16B16A16UnormColor) {
  // (Roadmap H8b) UNORM: 0xFFFF/0x0000 (uint16) decode to 1.0f/0.0f.
  std::array<uint16_t, 4> Values = {65535, 0, 0, 65535};
  std::array<uint8_t, 8> ColorBytes;
  memcpy(ColorBytes.data(), Values.data(), ColorBytes.size());
  renderSolidColorTriangleWithAttributeFormat(
      cpu::ResourceFormat::R16G16B16A16_UNORM, ColorBytes, {255, 0, 0, 255});
}

TEST(ExecutorTest, VertexAttributeDecodesR16G16B16A16SnormColor) {
  // (Roadmap H8b) SNORM: 32767/0 (int16) decode to 1.0f/0.0f.
  std::array<int16_t, 4> Values = {32767, 0, 0, 32767};
  std::array<uint8_t, 8> ColorBytes;
  memcpy(ColorBytes.data(), Values.data(), ColorBytes.size());
  renderSolidColorTriangleWithAttributeFormat(
      cpu::ResourceFormat::R16G16B16A16_SNORM, ColorBytes, {255, 0, 0, 255});
}

TEST(ExecutorTest, VertexAttributeDecodesR16G16B16A16FloatColor) {
  // (Roadmap H8b) SFLOAT: the binary16 bit patterns for 1.0f (0x3C00) and
  // 0.0f (0x0000) -- exercises `decodeAttribute`'s new
  // `halfBitsToFloat`/`llvm::APFloat`-based half->float32 conversion.
  std::array<uint16_t, 4> Values = {0x3C00, 0x0000, 0x0000, 0x3C00};
  std::array<uint8_t, 8> ColorBytes;
  memcpy(ColorBytes.data(), Values.data(), ColorBytes.size());
  renderSolidColorTriangleWithAttributeFormat(
      cpu::ResourceFormat::R16G16B16A16_FLOAT, ColorBytes, {255, 0, 0, 255});
}

TEST(ExecutorTest, VertexAttributeDecodesB8G8R8A8UnormColor) {
  // (Roadmap H8t) `B8G8R8A8_UNORM`'s memory order is B, G, R, A (unlike
  // `R8G8B8A8_UNORM`'s R, G, B, A) -- feed the raw memory bytes for
  // logical solid red (R=255,G=0,B=0,A=255) as {B=0, G=0, R=255, A=255}
  // and confirm `decodeAttribute`'s swizzle recovers logical red, not
  // blue, in the rendered attachment (which is itself `R8G8B8A8_UNORM`,
  // so a byte-for-byte swizzle bug would show up as a blue triangle).
  std::array<uint8_t, 4> ColorBytes = {0, 0, 255, 255}; // B, G, R, A memory.
  renderSolidColorTriangleWithAttributeFormat(
      cpu::ResourceFormat::B8G8R8A8_UNORM, ColorBytes, {255, 0, 0, 255});
}

TEST(ExecutorTest, VertexAttributeDecodesR10G10B10A2UnormColor) {
  // (Roadmap H8h) `R10G10B10A2_UNORM` (`VK_FORMAT_A2B10G10R10_UNORM_
  // PACK32`) is one packed 32-bit word, MSB down: 2 bits A, 10 bits each
  // of B/G/R. Solid red (R=1.0, G=0.0, B=0.0, A=1.0) packs to R=1023,
  // G=0, B=0, A=3 -- confirms `decodeAttribute`'s dedicated packed-word
  // case, not just `attributeFetchLayout`'s all-or-nothing bounds
  // arithmetic in isolation, through a real compiled pipeline.
  uint32_t Raw = 1023u | (0u << 10) | (0u << 20) | (3u << 30);
  std::array<uint8_t, 4> ColorBytes;
  memcpy(ColorBytes.data(), &Raw, sizeof(Raw));
  renderSolidColorTriangleWithAttributeFormat(
      cpu::ResourceFormat::R10G10B10A2_UNORM, ColorBytes, {255, 0, 0, 255});
}

/// (roadmap L79) A `float4` shader input bound to a narrower vertex
/// attribute format must have its missing trailing components (here, both
/// B and A -- `R32G32_FLOAT` supplies only R/G) default per the standard
/// HLSL/Vulkan convention (0 for a missing X/Y/Z, 1 for a missing W),
/// rather than reading the *next* vertex's own attribute data as if it
/// belonged to this one -- the exact bug this row's own roadmap text
/// names. Each vertex's position+color record is tightly packed with no
/// padding gap, so an un-fixed `attributeFetchLayout` (which never capped
/// decoding by the format's own channel count, only by the shader's
/// declared component count and the whole remaining buffer) would read
/// straight into the next vertex's own (deliberately distinct, nonzero,
/// non-identity-default) position floats for the missing B/A components
/// instead of defaulting them -- producing a visibly wrong, easily
/// distinguished pixel rather than an accidental pass.
TEST(ExecutorTest, VertexAttributeDefaultsComponentsBeyondFormatChannelCount) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  struct Vertex {
    float Pos[3];
    float ColorRG[2];
  };
  // A triangle covering the whole [-1, 1] NDC square (and more), CCW-
  // wound; every vertex's own color data is (R=1, G=1), but each vertex's
  // *position* is different and deliberately neither 0 nor 1, so a
  // regression reading through into the next vertex's position as this
  // vertex's missing B/A produces a clearly wrong pixel.
  std::array<Vertex, 3> Vertices = {{
      {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
      {{3.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
      {{-1.0f, 3.0f, 0.0f}, {1.0f, 1.0f}},
  }};

  std::array<VertexAttribute, 2> Attributes = {
      VertexAttribute{0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      VertexAttribute{1, cpu::ResourceFormat::R32G32_FLOAT,
                      sizeof(Vertex::Pos)}};
  std::array<uint8_t, 64> AttachmentStorage{};
  AttachmentView Color{AttachmentStorage, cpu::ResourceFormat::R8G8B8A8_UNORM,
                       4, 4};
  std::array<AttachmentView, 1> Attachments = {Color};
  std::array<VertexBufferBinding, 1> Bindings = {VertexBufferBinding{
      0, sizeof(Vertex),
      ArrayRef(reinterpret_cast<const uint8_t *>(Vertices.data()),
               sizeof(Vertices)),
      Attributes}};

  PreparedDraw Draw;
  Draw.Attachments = Attachments;
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  // Solid yellow (R=1, G=1, B=0, A=1 -> 255, 255, 0, 255): B/A default
  // rather than reading the next vertex's own nonzero position floats.
  for (uint32_t I = 0; I != 16; ++I) {
    const uint8_t *Texel = AttachmentStorage.data() + I * 4;
    EXPECT_EQ(Texel[0], 255) << "texel " << I;
    EXPECT_EQ(Texel[1], 255) << "texel " << I;
    EXPECT_EQ(Texel[2], 0) << "texel " << I;
    EXPECT_EQ(Texel[3], 255) << "texel " << I;
  }
}

/// (roadmap L79) Like
/// `VertexAttributeDefaultsComponentsBeyondFormatChannelCount` above, but
/// only the trailing W/alpha component is missing (`R32G32B32_FLOAT`
/// supplies R/G/B; the shader's own `float4` color input still declares a
/// 4th, A, component) -- the "missing W only" shape, distinct from that
/// test's "missing multiple trailing components" shape, confirming the
/// defaulting logic's `Elt.ComponentCount == 4` branch is reached the same
/// way regardless of how many trailing components the bound format
/// actually omits.
TEST(ExecutorTest, VertexAttributeDefaultsOnlyMissingAlphaComponent) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  struct Vertex {
    float Pos[3];
    float ColorRGB[3];
  };
  // Solid red (R=1, G=0, B=0) color data on every vertex; each vertex's
  // own position is distinct and neither 0 nor 1, so a regression reading
  // through into the next vertex's position.x as this vertex's missing A
  // produces a clearly wrong (non-1.0) alpha.
  std::array<Vertex, 3> Vertices = {{
      {{-1.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
      {{3.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
      {{-1.0f, 3.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
  }};

  std::array<VertexAttribute, 2> Attributes = {
      VertexAttribute{0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      VertexAttribute{1, cpu::ResourceFormat::R32G32B32_FLOAT,
                      sizeof(Vertex::Pos)}};
  std::array<uint8_t, 64> AttachmentStorage{};
  AttachmentView Color{AttachmentStorage, cpu::ResourceFormat::R8G8B8A8_UNORM,
                       4, 4};
  std::array<AttachmentView, 1> Attachments = {Color};
  std::array<VertexBufferBinding, 1> Bindings = {VertexBufferBinding{
      0, sizeof(Vertex),
      ArrayRef(reinterpret_cast<const uint8_t *>(Vertices.data()),
               sizeof(Vertices)),
      Attributes}};

  PreparedDraw Draw;
  Draw.Attachments = Attachments;
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t I = 0; I != 16; ++I) {
    const uint8_t *Texel = AttachmentStorage.data() + I * 4;
    EXPECT_EQ(Texel[0], 255) << "texel " << I;
    EXPECT_EQ(Texel[1], 0) << "texel " << I;
    EXPECT_EQ(Texel[2], 0) << "texel " << I;
    EXPECT_EQ(Texel[3], 255) << "texel " << I;
  }
}

/// (roadmap H7t) The same fully-covered, solid-color triangle as
/// `FillsFullyCoveredTriangleWithSolidColor`, but the fragment stage's own
/// `SV_Target0` output is a 3-component `vec3` (`Vec3FragmentShaderIR`,
/// `ComponentCount == 3`) rather than a plain float4 -- legal per spec, no
/// alpha write at all. The attachment (R8G8B8A8_UNORM) is initialized to
/// fully transparent black before the draw so that a wrong "missing alpha
/// reads as zero" implementation would leave every texel's own alpha
/// untouched (0), distinguishably wrong from this test's expected fully
/// opaque (255) result.
TEST(ExecutorTest, FillsTriangleWithColorOutputNarrowerThan4Components) {
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
      makeElement(1, SignatureDirection::Output, 3, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, Vec3FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
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

  // A triangle covering the whole [-1, 1] NDC square (and more), CCW-wound,
  // every vertex a solid green -- the alpha component below is never
  // consulted (the fragment stage's own output has no alpha to read), it
  // only exercises that a wider vertex-side varying feeding a narrower
  // fragment output is not itself rejected.
  TriangleScene Scene;
  Scene.AttachmentStorage.fill(0);
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // v2
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t I = 0; I != 16; ++I) {
    const uint8_t *Texel = Scene.AttachmentStorage.data() + I * 4;
    EXPECT_EQ(Texel[0], 0) << "texel " << I;
    EXPECT_EQ(Texel[1], 255) << "texel " << I;
    EXPECT_EQ(Texel[2], 0) << "texel " << I;
    EXPECT_EQ(Texel[3], 255) << "texel " << I << " (missing alpha should "
                                                 "default to fully opaque)";
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

  // (Roadmap L24) This triangle -- "the same triangle as
  // `CullsEveryTriangleWithFrontAndBack`'s own, but wound clockwise
  // (v1/v2 swapped)" -- is, under the corrected winding/viewport
  // formulas, actually *front*-facing under `FrontFace::CounterClockwise`
  // (the opposite of what this test originally assumed, back when
  // `projectVertex`'s own Y formula had a since-removed bug): a real
  // `dEQP-VK.rasterization.culling.*` CTS re-run confirms 42/43 Pass with
  // this sign convention (matching this project's pre-L24 baseline
  // exactly), so `CullMode::Back` must *not* discard it here -- the
  // triangle renders, leaving every attachment byte its shader's authored
  // color (opaque red, matching the vertex data's own RGBA below).
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f,  0.0f, 1.0f, -1.0f, 3.0f, 0.0f, 1.0f,
      0.0f,  0.0f,  1.0f, 3.0f, -1.0f, 0.0f, 1.0f, 0.0f,  0.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  // Opaque red (`1.0, 0.0, 0.0, 1.0`) at every covered texel, encoded as
  // `R8G8B8A8_UNORM`: byte 0 (R) is `0xFF`, bytes 1-2 (G, B) are `0x00`,
  // byte 3 (A) is `0xFF`.
  for (size_t I = 0; I != Scene.AttachmentStorage.size(); I += 4) {
    EXPECT_EQ(Scene.AttachmentStorage[I + 0], 0xFF);
    EXPECT_EQ(Scene.AttachmentStorage[I + 1], 0x00);
    EXPECT_EQ(Scene.AttachmentStorage[I + 2], 0x00);
    EXPECT_EQ(Scene.AttachmentStorage[I + 3], 0xFF);
  }
}

TEST(ExecutorTest, CullsEveryTriangleWithFrontAndBack) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::FrontAndBack, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // (Roadmap L24) This triangle -- the same one every other test in this
  // file renders unculled with `CullMode::None` (previously described here
  // as "front-facing (CCW)") -- is actually *back*-facing under
  // `FrontFace::CounterClockwise` and the corrected winding/viewport
  // formulas (confirmed against a real `dEQP-VK.rasterization.culling.*`
  // CTS re-run, 42/43 Pass): `CullMode::FrontAndBack` discards it
  // regardless, so this test's own assertion doesn't depend on which way
  // it's actually classified.
  TriangleScene Scene;
  Scene.VertexData = {-1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint8_t Byte : Scene.AttachmentStorage)
    EXPECT_EQ(Byte, 0);
}

// (roadmap H7l) Every adjacency topology is legal on a pipeline with no
// bound geometry stage at all -- `VUID-VkGraphicsPipelineCreateInfo-
// topology-00738`/neighbors require the `geometryShader` *device feature*
// (this ICD always advertises it), not that this particular pipeline
// binds a geometry stage. Found via a real `dEQP-VK.clipping.clip_volume.
// depth_clamp.triangle_list_with_adjacency` reproduction, whose own
// vertex/fragment-only pipeline is exactly this combination. Per the
// spec's own "Primitive Topologies" text, "if there is no geometry
// shader, ... adjacency ... is ignored" -- rasterization proceeds exactly
// as `stripAdjacency(Topology)` (here, `TriangleList`) would, reading
// only each primitive's 3 core vertices out of its 6-vertex adjacency
// window (`v0, adj01, v1, adj12, v2, adj20`, `SplitPrimitiveAdjacency`'s
// own documented order).
TEST(ExecutorTest, RendersTriangleListWithAdjacencyCoreTriangleWithoutAGeometryStage) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleListWithAdjacency);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // Core vertices (0, 2, 4) are the same full-viewport-covering, opaque
  // red triangle `FillsFullyCoveredTriangleWithSolidColor` uses; the
  // adjacency-only vertices (1, 3, 5) sit at a position a leaked core
  // triangle could never reach (far outside the [-1, 1] NDC square) and
  // in a different, easily distinguished color (blue), so accidentally
  // rasterizing them would visibly fail the all-red check below.
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0 (core)
      50.0f, 50.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, // adj01
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1 (core)
      50.0f, 50.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, // adj12
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2 (core)
      50.0f, 50.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, // adj20
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

// roadmap C4: a `PointList` draws a point `RasterState::MaxPointSize`
// pixels across by default when the vertex shader never writes
// `gl_PointSize` (Vulkan's own documented fallback for an unwritten
// `PointSize` output is 1.0 -- see `RasterVertex::PointSize`'s own
// comment in Executor.cpp). `largePoints`/`RendersAPointAtItsWrittenPoint
// Size` below cover a real, non-default derived size.
TEST(ExecutorTest, RendersAPointList) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::PointList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  // Two points, pixel centers (1, 2) and (2, 1) of the 4x4 target: NDC
  // ((1 + 0.5) / 4 * 2 - 1, ...) with Y following the viewport transform's
  // Vulkan-spec formula, just like X.
  Scene.VertexData = {
      -0.25f, 0.25f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // pixel (1, 2), red
      0.25f,  -0.25f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, // pixel (2, 1), green
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  const uint8_t *Red = texel(1, 2);
  EXPECT_EQ(Red[0], 255);
  EXPECT_EQ(Red[1], 0);
  EXPECT_EQ(Red[3], 255);
  const uint8_t *Green = texel(2, 1);
  EXPECT_EQ(Green[0], 0);
  EXPECT_EQ(Green[1], 255);
  EXPECT_EQ(Green[3], 255);
  // Every other texel is untouched by either 1-pixel point.
  const uint8_t *Untouched = texel(0, 0);
  EXPECT_EQ(Untouched[3], 0);
}

// roadmap H7k: unlike a triangle (`clipTriangle`), a `Point`/`Line`-class
// primitive was never tested against the near/far Z planes at all before
// this row -- confirmed via a real `dEQP-VK.clipping.clip_volume.
// depth_clamp.point_list` reproduction that every one of its points still
// rendered regardless of `depthClampEnable`, including ones behind the
// near plane. A point with Z < 0 (behind the near plane, `VertexShaderIR`
// writes clip.w = 1.0 unconditionally, so clip-space Z equals NDC Z
// directly) is discarded outright when depth clamp is disabled --
// `pointPassesDepthClip`'s own all-or-nothing test, since Vulkan has no
// notion of "half a point".
TEST(ExecutorTest, DiscardsAPointBehindTheNearPlaneWhenDepthClampIsDisabled) {
  Context Ctx;
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.DepthClampEnable = false;
  Expected<GraphicsPipeline> Pipeline =
      buildPipeline(Ctx, Raster, PrimitiveTopology::PointList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  // Pixel (1, 1)'s center, but at clip-space Z = -0.5: behind the near
  // plane (valid range is [0, w] = [0, 1] here).
  Scene.VertexData = {
      -0.25f, 0.25f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint8_t Byte : Scene.AttachmentStorage)
    EXPECT_EQ(Byte, 0);
}

// roadmap H7k: the same point as above, but with depth clamp enabled --
// per the Vulkan spec, depth clamp disables near/far clipping entirely
// (the out-of-range depth is clamped per-fragment instead), so the point
// renders normally at pixel (1, 2).
TEST(ExecutorTest, RendersAPointBehindTheNearPlaneWhenDepthClampIsEnabled) {
  Context Ctx;
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.DepthClampEnable = true;
  Expected<GraphicsPipeline> Pipeline =
      buildPipeline(Ctx, Raster, PrimitiveTopology::PointList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {
      -0.25f, 0.25f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  const uint8_t *Red = texel(1, 2);
  EXPECT_EQ(Red[0], 255);
  EXPECT_EQ(Red[3], 255);
}

// A vertex shader like `VertexShaderIR` above, but with a third input
// attribute (element 0, location 2: one float) it writes straight through
// to a new `SignatureSystemValue::PointSize` output (element 4) alongside
// `Position`/color -- exercises `RasterVertex::PointSize`/`emitPointQuad`'s
// clamp (roadmap H7e, `largePoints`) end to end through a real `PointList`
// draw.
constexpr char PointSizeVertexShaderIR[] = R"(
  define void @vs_pointsize() #0 {
    %px = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %py = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    %pz = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
    %cr = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
    %cg = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
    %cb = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
    %ca = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 0)
    %ps = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %px, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %py, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %pz, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %cr, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 1, float %cg, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 2, float %cb, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 3, float %ca, i32 0)
    call void @feme.stage.output.store.f32(i32 5, i32 0, i32 0, float %ps, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="vertex" }
)";

/// Builds a `PointList` pipeline whose vertex shader writes a real
/// `gl_PointSize` (`PointSizeVertexShaderIR` above), with the given
/// `RasterState` (so a test can set `MaxPointSize` to any test-local
/// clamp bound without needing a real device-sized render target).
Expected<GraphicsPipeline> buildPointSizePipeline(Context &Ctx,
                                                  RasterState Raster) {
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Input, 1, /*Location=*/2),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(4, SignatureDirection::Output, 4, /*Location=*/0),
      makeElement(5, SignatureDirection::Output, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::PointSize)};
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, PointSizeVertexShaderIR, "vs_pointsize", VSSig, ShaderStage::Vertex);
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
  return GraphicsPipeline(std::move(*VS), std::move(*FS),
                          PrimitiveTopology::PointList, Raster, DepthState{},
                          BlendMode::Replace, /*SampleCount=*/1,
                          std::move(Attachments), StencilState{},
                          std::vector<BlendState>{BlendState{}});
}

/// One point, its position/color/`PointSize` interleaved per vertex (11
/// floats/vertex: xyz, rgba, size), centered at the 4x4 target's exact
/// pixel-grid center (screen (2, 2), NDC (0, 0) -- chosen, like
/// `RendersAPointList`'s own pixel centers, so the expansion's own corners
/// land on exact half-pixel boundaries).
PreparedDraw preparePointSizeDraw(std::array<uint8_t, 64> &AttachmentStorage,
                                  std::vector<float> &VertexData,
                                  std::array<VertexBufferBinding, 1> &Bindings,
                                  std::array<AttachmentView, 1> &Attachments,
                                  AttachmentView &Color) {
  static const std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12},
      {2, cpu::ResourceFormat::R32_FLOAT, 28}};
  Color = AttachmentView{AttachmentStorage, cpu::ResourceFormat::R8G8B8A8_UNORM,
                         4, 4};
  Attachments = {Color};
  Bindings = {VertexBufferBinding{
      0, 32,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attributes}};
  PreparedDraw Draw;
  Draw.Attachments = Attachments;
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 1;
  Cmd.InstanceCount = 1;
  static std::array<DrawCommand, 1> Draws;
  Draws = {Cmd};
  Draw.Draws = Draws;
  return Draw;
}

// roadmap H7e (`largePoints`): a `PointList` vertex writing a real
// `gl_PointSize` expands to a quad that many pixels across, not the
// unwritten default's 1 pixel -- a size of 2.0 centered exactly on the
// pixel grid covers exactly the 2x2 block of pixels straddling that
// intersection.
TEST(ExecutorTest, RendersAPointAtItsWrittenPointSize) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPointSizePipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  std::vector<float> VertexData = {
      0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f,
  };
  std::array<uint8_t, 64> AttachmentStorage{};
  std::array<VertexBufferBinding, 1> Bindings;
  std::array<AttachmentView, 1> Attachments;
  AttachmentView Color;
  PreparedDraw Draw = preparePointSizeDraw(AttachmentStorage, VertexData,
                                           Bindings, Attachments, Color);

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  for (uint32_t Y : {1u, 2u})
    for (uint32_t X : {1u, 2u})
      EXPECT_EQ(texel(X, Y)[3], 255) << "x=" << X << " y=" << Y;
  // Every pixel outside the 2x2 block is untouched.
  EXPECT_EQ(texel(0, 0)[3], 0);
  EXPECT_EQ(texel(3, 3)[3], 0);
  EXPECT_EQ(texel(0, 3)[3], 0);
  EXPECT_EQ(texel(3, 0)[3], 0);
}

// roadmap H7e (`largePoints`): the derived point size is clamped to
// `[1.0, RasterState::MaxPointSize]` before quad expansion, exactly like a
// real `dEQP-VK.rasterization.primitive_size.points.*` case's own expected
// clamped result -- writing a size far beyond a (test-local) 2-pixel
// maximum still renders only the 2x2 block `RendersAPointAtItsWrittenPoint
// Size` above does, not the unclamped, much larger size.
TEST(ExecutorTest, ClampsPointSizeToTheDevicesMaximum) {
  Context Ctx;
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.MaxPointSize = 2.0f;
  Expected<GraphicsPipeline> Pipeline = buildPointSizePipeline(Ctx, Raster);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  std::vector<float> VertexData = {
      0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 999.0f,
  };
  std::array<uint8_t, 64> AttachmentStorage{};
  std::array<VertexBufferBinding, 1> Bindings;
  std::array<AttachmentView, 1> Attachments;
  AttachmentView Color;
  PreparedDraw Draw = preparePointSizeDraw(AttachmentStorage, VertexData,
                                           Bindings, Attachments, Color);

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  for (uint32_t Y : {1u, 2u})
    for (uint32_t X : {1u, 2u})
      EXPECT_EQ(texel(X, Y)[3], 255) << "x=" << X << " y=" << Y;
  // A point clamped to 2 pixels does not spill onto the target's edges.
  EXPECT_EQ(texel(0, 0)[3], 0);
  EXPECT_EQ(texel(3, 3)[3], 0);
}

// roadmap H7e (`largePoints`): the derived point size is also clamped up
// to `pointSizeRange[0]` (1.0) when a vertex shader writes a size below
// it -- a size of 0.0 still renders exactly 1 pixel, not zero.
TEST(ExecutorTest, ClampsPointSizeUpToTheMinimum) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPointSizePipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  std::vector<float> VertexData = {
      0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,
  };
  std::array<uint8_t, 64> AttachmentStorage{};
  std::array<VertexBufferBinding, 1> Bindings;
  std::array<AttachmentView, 1> Attachments;
  AttachmentView Color;
  PreparedDraw Draw = preparePointSizeDraw(AttachmentStorage, VertexData,
                                           Bindings, Attachments, Color);

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  // Exactly one pixel of the 2x2 intersection -- the top-left rule's own
  // tie-break, same as any other exactly grid-aligned quad corner.
  EXPECT_EQ(texel(1, 1)[3], 255);
  EXPECT_EQ(texel(2, 2)[3], 0);
  EXPECT_EQ(texel(1, 2)[3], 0);
  EXPECT_EQ(texel(2, 1)[3], 0);
}

// roadmap C4: a `LineList` draws a 1-pixel-wide line between each pair of
// vertices when the pipeline's own `RasterState::LineWidth` (this test's
// default) is 1.0; `RendersAWideRectangularLine` below covers a wider one
// (roadmap F5/H7e, `wideLines`).
TEST(ExecutorTest, RendersAHorizontalLineList) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::LineList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  // A horizontal line through the row of pixels at Y=2 (NDC y = 0.25,
  // mapped by the viewport transform to screen row 2's center), spanning
  // the target's full width.
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
    const uint8_t *Texel = texel(X, 2);
    EXPECT_EQ(Texel[3], 255) << "x=" << X;
  }
  // The row above/below the line is untouched.
  EXPECT_EQ(texel(0, 1)[3], 0);
  EXPECT_EQ(texel(0, 3)[3], 0);
}

// roadmap H7k: unlike `DiscardsAPointBehindTheNearPlaneWhenDepthClampIs
// Disabled` above (points are all-or-nothing, per the Vulkan spec), a line
// segment straddling the near plane is *partially* clipped:
// `clipLineDepthRange` interpolates a fresh endpoint exactly at the near
// plane, so only the segment's still-valid portion draws. This line runs
// from clip Z = -1 (invalid, behind the near plane) at screen-left to
// clip Z = 1 (valid) at screen-right, crossing Z = 0 at NDC x = 0 (screen
// x = 2): only the right half (screen columns 2-3) should render, on row 2.
TEST(ExecutorTest, ClipsALineAtTheNearPlaneWhenDepthClampIsDisabled) {
  Context Ctx;
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.DepthClampEnable = false;
  Expected<GraphicsPipeline> Pipeline =
      buildPipeline(Ctx, Raster, PrimitiveTopology::LineList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, 0.25f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
      1.0f,  0.25f, 1.0f,  1.0f, 1.0f, 1.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  EXPECT_EQ(texel(0, 2)[3], 0);
  EXPECT_EQ(texel(1, 2)[3], 0);
  EXPECT_EQ(texel(2, 2)[3], 255);
  EXPECT_EQ(texel(3, 2)[3], 255);
}

// roadmap H7k: the same straddling line as above, but with depth clamp
// enabled -- near/far clipping is disabled entirely, so the full line
// (all four columns on row 2) renders.
TEST(ExecutorTest, RendersAFullLineAcrossTheNearPlaneWhenDepthClampIsEnabled) {
  Context Ctx;
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.DepthClampEnable = true;
  Expected<GraphicsPipeline> Pipeline =
      buildPipeline(Ctx, Raster, PrimitiveTopology::LineList);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, 0.25f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
      1.0f,  0.25f, 1.0f,  1.0f, 1.0f, 1.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  for (uint32_t X = 0; X != 4; ++X)
    EXPECT_EQ(texel(X, 2)[3], 255) << "x=" << X;
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
      // Segment 1: a horizontal red line through screen row 3.
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
      // Segment 2: a horizontal green line through screen row 0.
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
  EXPECT_EQ(texel(0, 3)[0], 255);
  EXPECT_EQ(texel(0, 3)[1], 0);
  EXPECT_EQ(texel(0, 0)[0], 0);
  EXPECT_EQ(texel(0, 0)[1], 255);
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
  // screen row 2's pixel center (y = 2.5), now 3 pixels wide so its
  // [1, 4) extent covers rows 1-3 and stops just short of row 0.
  Scene.VertexData = {
      -1.0f, 0.25f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
      1.0f,  0.25f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  for (uint32_t Y : {1u, 2u, 3u})
    for (uint32_t X = 0; X != 4; ++X)
      EXPECT_EQ(texel(X, Y)[3], 255) << "x=" << X << " y=" << Y;
  for (uint32_t X = 0; X != 4; ++X)
    EXPECT_EQ(texel(X, 0)[3], 0) << "x=" << X;
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
  // positions exactly on pixel (0, 3)'s and (3, 0)'s centers.
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
    EXPECT_EQ(texel(D, 3 - D)[3], 255) << "d=" << D;
  // A pixel off the diagonal is untouched.
  EXPECT_EQ(texel(0, 0)[3], 0);
  EXPECT_EQ(texel(3, 3)[3], 0);
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
  EXPECT_EQ(texel(0, 2)[3], 0);
  EXPECT_EQ(texel(1, 2)[3], 255);
  EXPECT_EQ(texel(2, 2)[3], 0);
  EXPECT_EQ(texel(3, 2)[3], 255);
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
  // A horizontal line whose centerline sits at screen y = 2.25, 0.25
  // pixels off of row 2's center -- close enough to fully light row 2
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
  // Row 2 (center 2.5, |edge| = 0.25): coverage = 1 - 0.25 = 0.75.
  EXPECT_NEAR(texel(0, 2)[3], 0.75 * 255, 2);
  // Row 1 (center 1.5, |edge| = 0.75): coverage = 1 - 0.75 = 0.25.
  EXPECT_NEAR(texel(0, 1)[3], 0.25 * 255, 2);
  // Row 0 (center 0.5, |edge| = 1.75) and row 3 (center 3.5, |edge| =
  // 1.25) are both fully outside the 1-pixel feather and get no coverage.
  EXPECT_EQ(texel(0, 0)[3], 0);
  EXPECT_EQ(texel(0, 3)[3], 0);
}

// roadmap H7c: `PolygonMode::Line` decomposes a triangle into its three
// edges as independent (Bresenham, for a pixel-exact prediction here)
// line segments instead of a filled interior. Vertices sit exactly on
// pixel centers (0, 3), (3, 3), (0, 0) so each of the 3 edges' own
// Bresenham walk is easy to hand-derive: the bottom edge lights row 3's
// 4 pixels, the left edge lights column 0's 4 pixels, and the diagonal
// edge lights the (0,0)-(1,1)-(2,2)-(3,3) main diagonal -- 9 pixels
// total, leaving the remaining 7 (including the far corner (3, 0) and
// the triangle's own centroid-ish (2, 1)) untouched, unlike `Fill` mode.
TEST(ExecutorTest, PolygonModeLineRastersOnlyTheTrianglesThreeEdges) {
  Context Ctx;
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.Polygon = PolygonMode::Line;
  Raster.LineMode = LineRasterizationMode::Bresenham;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(Ctx, Raster);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {
      -0.75f, 0.75f,  0.0f, 1.0f, 1.0f, 1.0f, 1.0f, // pixel (0, 3)
      0.75f,  0.75f,  0.0f, 1.0f, 1.0f, 1.0f, 1.0f, // pixel (3, 3)
      -0.75f, -0.75f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, // pixel (0, 0)
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  for (auto [X, Y] : {std::pair{0u, 0u},
                      {0u, 1u},
                      {1u, 1u},
                      std::pair{0u, 2u},
                      {2u, 2u},
                      std::pair{0u, 3u},
                      {1u, 3u},
                      {2u, 3u},
                      {3u, 3u}})
    EXPECT_EQ(texel(X, Y)[3], 255) << "x=" << X << " y=" << Y;
  for (auto [X, Y] : {std::pair{1u, 0u},
                      {2u, 0u},
                      {3u, 0u},
                      std::pair{2u, 1u},
                      {3u, 1u},
                      std::pair{1u, 2u},
                      {3u, 2u}})
    EXPECT_EQ(texel(X, Y)[3], 0) << "x=" << X << " y=" << Y;
}

// roadmap H7c: `PolygonMode::Point` rasterizes only the triangle's own 3
// vertices as independent 1-pixel points, reusing the exact same
// vertices as the test above (each landing exactly on a pixel center)
// so each vertex lights exactly the one pixel it sits on and nothing
// else -- notably, none of the "edge" pixels the `Line` test above
// lights (e.g. (0, 1), (1, 1), (2, 2)) get lit here.
TEST(ExecutorTest, PolygonModePointRastersOnlyTheTrianglesThreeVertices) {
  Context Ctx;
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.Polygon = PolygonMode::Point;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(Ctx, Raster);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {
      -0.75f, 0.75f,  0.0f, 1.0f, 1.0f, 1.0f, 1.0f, // pixel (0, 3)
      0.75f,  0.75f,  0.0f, 1.0f, 1.0f, 1.0f, 1.0f, // pixel (3, 3)
      -0.75f, -0.75f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, // pixel (0, 0)
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  for (auto [X, Y] : {std::pair{0u, 0u}, {0u, 3u}, {3u, 3u}})
    EXPECT_EQ(texel(X, Y)[3], 255) << "x=" << X << " y=" << Y;
  for (uint32_t Y = 0; Y != 4; ++Y)
    for (uint32_t X = 0; X != 4; ++X) {
      if ((X == 0 && Y == 0) || (X == 0 && Y == 3) || (X == 3 && Y == 3))
        continue;
      EXPECT_EQ(texel(X, Y)[3], 0) << "x=" << X << " y=" << Y;
    }
}

// (roadmap H7h) `shaderClipDistance`: a triangle covering the whole
// [-1, 1] NDC square (`FillsFullyCoveredTriangleWithSolidColor`'s own
// full-screen-triangle trick), each vertex's own `gl_ClipDistance[0]`
// written equal to its own NDC Y coordinate -- an affine function that,
// being linearly interpolated the same way NDC Y itself is (both vertex
// attributes with the triangle's own constant W = 1), evaluates to
// exactly the fragment's own NDC Y everywhere inside the primitive. Only
// the NDC-Y->0 half-plane (`ClipDistance >= 0`) survives clipping: rows 2
// and 3 (positive NDC Y, `PolygonModePointRastersOnlyTheTrianglesThree
// Vertices`'s own "positive Y = bottom row" convention) stay lit; rows 0
// and 1 (negative NDC Y) are clipped away entirely.
TEST(ExecutorTest, ClipsATriangleAgainstAWrittenClipDistance) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildClipCullDistancePipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // Interleaved position (xyz), color (rgba), clip-distance, cull-distance
  // (9 floats/vertex). Cull-distance is a uniformly positive 1.0 so it
  // never interferes with this test's own clip-only scenario.
  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 3.0f,  1.0f, // v2
  };
  std::array<uint8_t, 64> AttachmentStorage{};
  std::array<VertexBufferBinding, 1> Bindings;
  std::array<AttachmentView, 1> Attachments;
  AttachmentView Color;
  PreparedDraw Draw = prepareClipCullDistanceDraw(
      AttachmentStorage, VertexData, Bindings, Attachments, Color);

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  for (uint32_t Y : {2u, 3u})
    for (uint32_t X = 0; X != 4; ++X)
      EXPECT_EQ(texel(X, Y)[3], 255) << "x=" << X << " y=" << Y;
  for (uint32_t Y : {0u, 1u})
    for (uint32_t X = 0; X != 4; ++X)
      EXPECT_EQ(texel(X, Y)[3], 0) << "x=" << X << " y=" << Y;
}

// (roadmap H7h) `shaderCullDistance`: the same full-screen triangle as
// `ClipsATriangleAgainstAWrittenClipDistance`, but with every vertex's own
// `gl_CullDistance[0]` written negative -- per the Vulkan spec, a whole
// primitive is discarded, before it ever reaches clipping, when one
// declared cull plane is negative for *every* one of its vertices.
// Clip-distance is a uniformly positive 1.0 so it never interferes.
TEST(ExecutorTest, CullsATriangleWhenCullDistanceIsNegativeForEveryVertex) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildClipCullDistancePipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, -1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, -1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, -1.0f, // v2
  };
  std::array<uint8_t, 64> AttachmentStorage{};
  std::array<VertexBufferBinding, 1> Bindings;
  std::array<AttachmentView, 1> Attachments;
  AttachmentView Color;
  PreparedDraw Draw = prepareClipCullDistanceDraw(
      AttachmentStorage, VertexData, Bindings, Attachments, Color);

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t I = 0; I != 16; ++I)
    EXPECT_EQ(AttachmentStorage[I * 4 + 3], 0) << "texel " << I;
}

// (roadmap H7x) A fragment stage's own read of the interpolated
// `gl_ClipDistance[0]`/`gl_CullDistance[0]` values a vertex stage wrote
// (`ClipCullDistanceReadFragmentShaderIR`), rather than the executor's own
// vertex-side clip/cull consumer (`ClipsATriangleAgainstAWrittenClip
// Distance`/`CullsATriangleWhenCullDistanceIsNegativeForEveryVertex`
// above) being the only thing that ever sees them. Every vertex of the
// same oversized, fully-covering triangle those tests use writes the same
// constant clip/cull-distance value (0.5/1.0 respectively, both positive
// so nothing clips/culls), so the interpolated value the fragment stage
// reads back is that same constant everywhere -- exercising the new
// `Executor.cpp` linking-by-`SystemValue` path (rather than by
// `Location`, which these builtins have none of) end to end through a
// real triangle draw.
TEST(ExecutorTest, FragmentShaderReadsBackInterpolatedClipAndCullDistance) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildClipCullDistanceReadPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // Interleaved position (xyz), color (rgba, unused by the fragment
  // shader here), clip-distance, cull-distance (9 floats/vertex).
  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f, // v2
  };
  std::array<uint8_t, 64> AttachmentStorage{};
  std::array<VertexBufferBinding, 1> Bindings;
  std::array<AttachmentView, 1> Attachments;
  AttachmentView Color;
  PreparedDraw Draw = prepareClipCullDistanceDraw(
      AttachmentStorage, VertexData, Bindings, Attachments, Color);

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t Y = 0; Y != 4; ++Y) {
    for (uint32_t X = 0; X != 4; ++X) {
      const uint8_t *Texel = AttachmentStorage.data() + (Y * 4 + X) * 4;
      EXPECT_NEAR(Texel[0], std::lround(0.5f * 255.0f), 2)
          << "x=" << X << " y=" << Y;
      EXPECT_EQ(Texel[1], 255) << "x=" << X << " y=" << Y;
    }
  }
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
      float NdcY = (PY + 0.5f) / 2.0f - 1.0f;
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
/// triangle's hypotenuse is the main diagonal of the 4x4 viewport
/// (screen `x == y`), which four of the sixteen pixel centers
/// (`(0.5,0.5)`, `(1.5,1.5)`, `(2.5,2.5)`, `(3.5,3.5)`) land exactly on;
/// the corrected `isTopLeftEdge` polarity (this edge walks
/// bottom-right-to-top-left, i.e. `Dy < 0`, neither the horizontal+
/// leftward "top" case nor the downward "left" case) excludes all four,
/// leaving exactly the 6 pixels strictly inside (`x < y`) filled.
/// Before H4j's fix, the old (backwards) polarity included all four
/// boundary pixels too, matching this bug's `glsl_triangles_*` CTS
/// symptom of an exact off-by-one row/column fill count.
TEST(ExecutorTest, TopLeftTieBreakExcludesALoneTrianglesOwnBoundaryEdge) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise});
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // Screen-space corners (0,4), (4,4), (0,0) -- NDC (-1,1), (1,1), (-1,-1):
  // a right triangle covering the lower-left half of the 4x4 viewport,
  // whose hypotenuse is the main diagonal `x == y`.
  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, 1.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0 = screen (0,4)
      1.0f,  1.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1 = screen (4,4)
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2 = screen (0,0)
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  for (uint32_t Y = 0; Y != 4; ++Y) {
    for (uint32_t X = 0; X != 4; ++X) {
      uint32_t I = Y * 4 + X;
      const uint8_t *Texel = Scene.AttachmentStorage.data() + I * 4;
      bool IsRed = Texel[0] == 255 && Texel[1] == 0 && Texel[3] == 255;
      bool IsClear = Texel[3] == 0;
      // Strictly inside the hypotenuse (x < y): must be filled red.
      // Exactly on it (x == y, the four boundary pixel centers): must
      // be excluded (left as the untouched, transparent clear color).
      if (X < Y)
        EXPECT_TRUE(IsRed) << "texel (" << X << "," << Y << ")";
      else if (X == Y)
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
/// (16,47)'s sample point (16.5,47.5) lies, in exact real-number math,
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
  // (16,47) their shared diagonal passes almost exactly through. NDC
  // (-1,1) projects to screen (0,64) and NDC (1,-1) to screen (64,0).
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
      uint32_t X = 16 + DX, Y = 47 + DY;
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

// (Roadmap H29k) A draw with *no* attachments at all -- neither color nor
// depth/stencil, e.g. a fragment stage kept alive purely for descriptor
// side effects, the exact shape
// `dEQP-VK.pipeline.pipeline_library.graphics_library.
// independent_sets_random.*.mesh_frag.*` exercises -- has nothing
// attachment-shaped to derive a rasterization extent from. Before this
// roadmap row, `executeDraws`'s `ExtentWidth`/`ExtentHeight` silently
// stayed `0` in that case, collapsing every triangle's scissor to a
// degenerate 0x0 rectangle and discarding the whole draw before a single
// fragment ran -- regardless of `Draw.Scissors` naming a real, non-empty,
// already-render-area-clipped rectangle. `executeDraws` now falls back to
// the union of `Draw.Scissors` for its extent whenever no attachment
// supplies one, so a fully covered triangle still produces exactly as
// many covered samples as its scissor rectangle's own area.
TEST(ExecutorTest, RasterizesWithNoAttachmentsAtAllUsingScissorAsExtent) {
  Context Ctx;

  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position)};
  constexpr char PositionOnlyVertexShaderIR[] = R"(
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
      Ctx, PositionOnlyVertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  // No fragment stage, and depth testing left disabled: this pipeline has
  // nothing attachment-shaped at all, exactly the shape this roadmap row
  // fixes.
  GraphicsPipeline Pipeline(
      std::move(*VS), /*FragmentStage=*/nullptr,
      PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, /*Attachments=*/{}, StencilState{},
      /*ColorBlends=*/{}, /*LogicOpEnable=*/false, LogicOp::Copy,
      std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false);
  EXPECT_FALSE(Pipeline.hasFragmentStage());

  TriangleScene Scene;
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2
  };
  PreparedDraw Draw = Scene.prepare();
  Draw.Attachments = {}; // No color attachments at all.
  // `Scene.prepare()` leaves `BindDepth`/`BindStencil` false by default, so
  // `Draw.DepthStencil` is already empty too -- this draw truly has no
  // attachment of any kind, only its own scissor rect (still `{0, 0, 4, 4}`,
  // set unconditionally by `Scene.prepare()`).
  uint64_t PassedSamples = 0;
  Draw.PassedSampleCounter = &PassedSamples;
  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());
  // Occlusion-query bookkeeping still runs off the rasterizer's own
  // coverage test, one sample per one of the 16 texels the scissor rect
  // covers -- proof the draw was not silently discarded by a degenerate
  // 0x0 extent.
  EXPECT_EQ(PassedSamples, 16u);
}

// (Roadmap H21c) `VK_EXT_transform_feedback`'s capture: a vertex-shader-
// only pipeline (no tessellation/geometry stage) with one `Output`-
// direction element tagged `XfbBuffer = 0` must, for every vertex
// invocation actually drawn, write that element's raw bits into the
// bound transform-feedback buffer at `vertexIndex * XfbStride +
// XfbOffset`, and advance `PreparedDraw::XfbCaptureBuffer::CapturedBytes`
// by exactly `vertexCount * XfbStride` -- the same real behavior
// `vkCmdEndTransformFeedbackEXT` (`CommandBuffer.cpp`) reads back out to
// report to its own counter buffer. Mirrors `RendersWithNoFragmentStage`
// above: no fragment stage at all, matching
// `dEQP-VK.transform_feedback.simple.basic`'s own vertex-only pipeline
// shape.
TEST(ExecutorTest, CapturesVertexShaderOutputToBoundTransformFeedbackBuffer) {
  Context Ctx;

  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(2, SignatureDirection::Output, 1, /*Location=*/0)};
  VSSig.Elements[2].XfbBuffer = 0;
  VSSig.Elements[2].XfbOffset = 0;
  VSSig.Elements[2].XfbStride = 4;
  constexpr char XfbVertexShaderIR[] = R"(
    define void @vs_main() #0 {
      %px = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %py = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %pz = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %px, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %py, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %pz, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %px, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="vertex" }
  )";
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, XfbVertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  GraphicsPipeline Pipeline(
      std::move(*VS), /*FragmentStage=*/nullptr, PrimitiveTopology::PointList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, /*Attachments=*/{}, StencilState{},
      /*ColorBlends=*/{}, /*LogicOpEnable=*/false, LogicOp::Copy,
      std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false);
  ASSERT_FALSE(Pipeline.hasTessellationStages());
  ASSERT_FALSE(Pipeline.hasGeometryStages());

  TriangleScene Scene;
  Scene.VertexData = {
      1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // v0: px = 1.0
      2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // v1: px = 2.0
      3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // v2: px = 3.0
  };
  PreparedDraw Draw = Scene.prepare();
  Draw.Attachments = {}; // No color attachment at all.

  std::array<uint8_t, 12> XfbStorage{}; // 3 vertices * 4 bytes each.
  uint64_t CapturedBytes = 0;
  std::array<PreparedDraw::XfbCaptureBuffer, 1> XfbBuffers = {
      PreparedDraw::XfbCaptureBuffer{MutableArrayRef(XfbStorage),
                                    &CapturedBytes}};
  Draw.XfbBuffers = XfbBuffers;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());

  float Captured[3];
  std::memcpy(Captured, XfbStorage.data(), sizeof(Captured));
  EXPECT_FLOAT_EQ(Captured[0], 1.0f);
  EXPECT_FLOAT_EQ(Captured[1], 2.0f);
  EXPECT_FLOAT_EQ(Captured[2], 3.0f);
  EXPECT_EQ(CapturedBytes, 12u);
}

// (Roadmap H21c) A second draw within the same transform-feedback scope
// (modeled here as a second `executeDraws` call sharing the same
// `CapturedBytes` accumulator, the same way `CommandBuffer.cpp`'s
// `runDraw` reuses `Gfx.XfbCapturedBytes` across every draw between one
// `vkCmdBeginTransformFeedbackEXT`/`vkCmdEndTransformFeedbackEXT` pair)
// appends after the first draw's own captured vertices rather than
// overwriting them from offset 0.
TEST(ExecutorTest, TransformFeedbackCaptureAppendsAcrossMultipleDraws) {
  Context Ctx;

  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(2, SignatureDirection::Output, 1, /*Location=*/0)};
  VSSig.Elements[2].XfbBuffer = 0;
  VSSig.Elements[2].XfbOffset = 0;
  VSSig.Elements[2].XfbStride = 4;
  constexpr char XfbVertexShaderIR[] = R"(
    define void @vs_main() #0 {
      %px = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %py = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      %pz = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %px, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %py, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %pz, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %px, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="vertex" }
  )";
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, XfbVertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  GraphicsPipeline Pipeline(
      std::move(*VS), /*FragmentStage=*/nullptr, PrimitiveTopology::PointList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, /*Attachments=*/{}, StencilState{},
      /*ColorBlends=*/{}, /*LogicOpEnable=*/false, LogicOp::Copy,
      std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false);

  std::array<uint8_t, 8> XfbStorage{}; // 2 vertices * 4 bytes each.
  uint64_t CapturedBytes = 0;
  std::array<PreparedDraw::XfbCaptureBuffer, 1> XfbBuffers = {
      PreparedDraw::XfbCaptureBuffer{MutableArrayRef(XfbStorage),
                                    &CapturedBytes}};

  TriangleScene FirstScene;
  FirstScene.VertexData = {10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  PreparedDraw FirstDraw = FirstScene.prepare();
  FirstDraw.Attachments = {};
  FirstDraw.XfbBuffers = XfbBuffers;
  ASSERT_THAT_ERROR(executeDraws(Pipeline, FirstDraw), Succeeded());
  EXPECT_EQ(CapturedBytes, 4u);

  TriangleScene SecondScene;
  SecondScene.VertexData = {20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  PreparedDraw SecondDraw = SecondScene.prepare();
  SecondDraw.Attachments = {};
  SecondDraw.XfbBuffers = XfbBuffers;
  ASSERT_THAT_ERROR(executeDraws(Pipeline, SecondDraw), Succeeded());
  EXPECT_EQ(CapturedBytes, 8u);

  float Captured[2];
  std::memcpy(Captured, XfbStorage.data(), sizeof(Captured));
  EXPECT_FLOAT_EQ(Captured[0], 10.0f);
  EXPECT_FLOAT_EQ(Captured[1], 20.0f);
}

// Roadmap R33: stencil testing/writes with a real `S8_UINT` attachment.
TEST(ExecutorTest, StencilTestRejectsMismatchedReference) {
  Context Ctx;
  StencilState Stencil;
  Stencil.TestEnable = true;
  // (Roadmap L24) This test's triangle (below) is *back*-facing under
  // `FrontFace::CounterClockwise` and the corrected winding formula (the
  // same one `dEQP-VK.rasterization.culling.*` confirms, 42/43 Pass) --
  // `Executor.cpp`'s stencil-face selection (`FrontFacing ? Stencil.Front
  // : Stencil.Back`) picks `Back` here, so this test configures `Back`
  // rather than `Front` to actually exercise the rejection path; before
  // this roadmap row's fix, the opposite classification made `Front`
  // (with the same fields) the one that applied.
  Stencil.Back.Compare = CompareOp::Equal;
  Stencil.Back.Reference = 5;
  Stencil.Back.PassOp = StencilOp::Replace;
  Stencil.Back.FailOp = StencilOp::Zero;
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
  // (Roadmap L24) See `StencilTestRejectsMismatchedReference`'s own
  // comment: this triangle is back-facing, so `Back` (not `Front`) is the
  // face state that actually applies. (This test happened to still pass
  // with `Front` configured instead, since its two possible outcomes --
  // `Back`'s all-`Keep`/`Always` defaults, or `Front`'s configured
  // `Equal`/`Replace` against an already-matching stencil value -- produce
  // the same observable result; `Back` is used here anyway, to actually
  // exercise the configured `CompareOp`/`PassOp` rather than rely on that
  // coincidence.)
  Stencil.Back.Compare = CompareOp::Equal;
  Stencil.Back.Reference = 5;
  Stencil.Back.PassOp = StencilOp::Replace;
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

// (Roadmap H103) A minimal, single-draw, single-quad reproduction of one
// blend-state combination taken directly from a real, currently-failing
// `dEQP-VK.pipeline.fast_linked_library.blend.format.r8g8b8a8_unorm`
// case (`color_1mcc_sc_add_alpha_cc_1mca_sub-...`, its first quad's own
// state) -- deliberately avoiding that CTS test's own 4-overlapping-quad
// geometry entirely, to isolate whether the bug is in the underlying
// per-pixel blend-equation arithmetic (`blendColor`/`blendFactorValue`/
// `applyBlendOp`) in isolation, decoupled from any multi-draw
// accumulation or quad-overlap rasterization question. A first "replace"
// draw (`SrcColorFactor=One`/`DstColorFactor=Zero`) establishes a known
// starting (destination) color; a second draw with the real
// `OneMinusConstantColor`/`SrcColor`/`Add` (color) and
// `ConstantColor`/`OneMinusConstantAlpha`/`Subtract` (alpha) state blends
// a known source color on top. The expected result is hand-computed from
// the Vulkan blend-equation spec directly (see the comment beside each
// expected value below), independent of this codebase's own
// implementation.
TEST(ExecutorTest, MatchesHandComputedBlendEquationForConstantColorFactors) {
  Context Ctx;

  // Draw 1: establish a known destination color via an unblended
  // "replace" draw (Dst = Src exactly, since DstColorFactor/
  // DstAlphaFactor are both Zero).
  BlendState Replace;
  Replace.BlendEnable = true;
  Replace.SrcColorFactor = BlendFactor::One;
  Replace.DstColorFactor = BlendFactor::Zero;
  Replace.SrcAlphaFactor = BlendFactor::One;
  Replace.DstAlphaFactor = BlendFactor::Zero;
  Expected<GraphicsPipeline> ReplacePipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, StencilState{}, Replace);
  ASSERT_THAT_EXPECTED(ReplacePipeline, Succeeded());

  TriangleScene Scene;
  // Dst = (0.25, 0.55, 0.35, 0.85), a full-viewport quad.
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 0.25f, 0.55f, 0.35f, 0.85f,
      3.0f,  -1.0f, 0.0f, 0.25f, 0.55f, 0.35f, 0.85f,
      -1.0f, 3.0f,  0.0f, 0.25f, 0.55f, 0.35f, 0.85f,
  };
  PreparedDraw ReplaceDraw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*ReplacePipeline, ReplaceDraw),
                    Succeeded());

  // Draw 2: the real blend-state-under-test, its own second full-viewport
  // quad drawn over the now-known Dst above.
  BlendState Blend;
  Blend.BlendEnable = true;
  Blend.SrcColorFactor = BlendFactor::OneMinusConstantColor;
  Blend.DstColorFactor = BlendFactor::SrcColor;
  Blend.ColorOp = BlendOp::Add;
  Blend.SrcAlphaFactor = BlendFactor::ConstantColor;
  Blend.DstAlphaFactor = BlendFactor::OneMinusConstantAlpha;
  Blend.AlphaOp = BlendOp::Subtract;
  std::array<float, 4> BlendConstants{0.1f, 0.2f, 0.3f, 0.4f};
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, StencilState{}, Blend,
      /*LogicOpEnable=*/false, LogicOp::Copy, BlendConstants);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // Src = (0.6, 0.4, 0.7, 0.9).
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 0.6f, 0.4f, 0.7f, 0.9f,
      3.0f,  -1.0f, 0.0f, 0.6f, 0.4f, 0.7f, 0.9f,
      -1.0f, 3.0f,  0.0f, 0.6f, 0.4f, 0.7f, 0.9f,
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  // Color: Result[C] = Src[C]*(1-Constant[C]) + Dst[C]*Src[C] (Add).
  //   R: 0.6*(1-0.1) + 0.25*0.6 = 0.54 + 0.15 = 0.69 -> round(0.69*255) = 176
  //   G: 0.4*(1-0.2) + 0.55*0.4 = 0.32 + 0.22 = 0.54 -> round(0.54*255) = 138
  //   B: 0.7*(1-0.3) + 0.35*0.7 = 0.49 + 0.245 = 0.735 -> round(0.735*255) =
  //      187
  // Alpha: Result = Src[3]*Constant[3] - Dst[3]*(1-Constant[3])
  //   (Subtract) = 0.9*0.4 - 0.85*0.6 = 0.36 - 0.51 = -0.15, clamped to 0
  //   when packed into an 8-bit UNORM channel.
  for (uint32_t I = 0; I != 16; ++I) {
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4], 176, 1) << "texel " << I;
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4 + 1], 138, 1) << "texel " << I;
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4 + 2], 187, 1) << "texel " << I;
    EXPECT_EQ(Scene.AttachmentStorage[I * 4 + 3], 0) << "texel " << I;
  }
}

// (Roadmap H103) The `VK_BLEND_OP_MIN`/`VK_BLEND_OP_MAX`/
// `VK_BLEND_OP_REVERSE_SUBTRACT` counterpart of
// `MatchesHandComputedBlendEquationForConstantColorFactors` above --
// taken from the same real, currently-failing CTS case's *second* quad
// (`color_1mcc_sc_min_alpha_z_1mca_rsub`), reusing that test's own
// Src/Dst/BlendConstants values so any divergence found here is
// specifically attributable to `Min`/`ReverseSubtract`, not to a
// different set of input values.
TEST(ExecutorTest, MatchesHandComputedBlendEquationForMinAndReverseSubtract) {
  Context Ctx;

  BlendState Replace;
  Replace.BlendEnable = true;
  Replace.SrcColorFactor = BlendFactor::One;
  Replace.DstColorFactor = BlendFactor::Zero;
  Replace.SrcAlphaFactor = BlendFactor::One;
  Replace.DstAlphaFactor = BlendFactor::Zero;
  Expected<GraphicsPipeline> ReplacePipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, StencilState{}, Replace);
  ASSERT_THAT_EXPECTED(ReplacePipeline, Succeeded());

  TriangleScene Scene;
  // Dst = (0.25, 0.55, 0.35, 0.85), a full-viewport quad.
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 0.25f, 0.55f, 0.35f, 0.85f,
      3.0f,  -1.0f, 0.0f, 0.25f, 0.55f, 0.35f, 0.85f,
      -1.0f, 3.0f,  0.0f, 0.25f, 0.55f, 0.35f, 0.85f,
  };
  PreparedDraw ReplaceDraw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*ReplacePipeline, ReplaceDraw),
                    Succeeded());

  BlendState Blend;
  Blend.BlendEnable = true;
  Blend.SrcColorFactor = BlendFactor::OneMinusConstantColor;
  Blend.DstColorFactor = BlendFactor::SrcColor;
  Blend.ColorOp = BlendOp::Min;
  Blend.SrcAlphaFactor = BlendFactor::Zero;
  Blend.DstAlphaFactor = BlendFactor::OneMinusConstantAlpha;
  Blend.AlphaOp = BlendOp::ReverseSubtract;
  std::array<float, 4> BlendConstants{0.1f, 0.2f, 0.3f, 0.4f};
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, StencilState{}, Blend,
      /*LogicOpEnable=*/false, LogicOp::Copy, BlendConstants);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // Src = (0.6, 0.4, 0.7, 0.9).
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 0.6f, 0.4f, 0.7f, 0.9f,
      3.0f,  -1.0f, 0.0f, 0.6f, 0.4f, 0.7f, 0.9f,
      -1.0f, 3.0f,  0.0f, 0.6f, 0.4f, 0.7f, 0.9f,
  };
  PreparedDraw Draw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  // Color: Result[C] = min(Src[C]*(1-Constant[C]), Dst[C]*Src[C]).
  //   R: min(0.6*0.9, 0.25*0.6) = min(0.54, 0.15) = 0.15 -> round(0.15*255)
  //      = 38
  //   G: min(0.4*0.8, 0.55*0.4) = min(0.32, 0.22) = 0.22 -> round(0.22*255)
  //      = 56
  //   B: min(0.7*0.7, 0.35*0.7) = min(0.49, 0.245) = 0.245 ->
  //      round(0.245*255) = 62
  // Alpha (ReverseSubtract, DstTerm - SrcTerm):
  //   Result = Dst[3]*(1-Constant[3]) - Src[3]*0 = 0.85*0.6 - 0 = 0.51 ->
  //     round(0.51*255) = 130
  for (uint32_t I = 0; I != 16; ++I) {
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4], 38, 1) << "texel " << I;
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4 + 1], 56, 1) << "texel " << I;
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4 + 2], 62, 1) << "texel " << I;
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4 + 3], 130, 1) << "texel " << I;
  }
}

// (Roadmap H103) Chains `MatchesHandComputedBlendEquationForConstantColor
// Factors`'s own Add-op draw and `...ForMinAndReverseSubtract`'s own
// Min-op draw back to back over the *same* attachment -- reusing both
// tests' own Src/BlendConstants values, but this time reading the second
// draw's Dst from the *first* draw's real blended-and-packed-to-8-bit
// result (not a hand-set starting color) -- to isolate whether a
// *second* draw correctly re-reads what a prior draw, using a different
// blend state, just wrote, decoupled from both single-draw arithmetic
// (already confirmed correct by the two tests above) and any
// quad-overlap rasterization question.
TEST(ExecutorTest,
    SequentialDrawsWithDifferentBlendStatesCorrectlyAccumulate) {
  Context Ctx;

  BlendState Replace;
  Replace.BlendEnable = true;
  Replace.SrcColorFactor = BlendFactor::One;
  Replace.DstColorFactor = BlendFactor::Zero;
  Replace.SrcAlphaFactor = BlendFactor::One;
  Replace.DstAlphaFactor = BlendFactor::Zero;
  Expected<GraphicsPipeline> ReplacePipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, StencilState{}, Replace);
  ASSERT_THAT_EXPECTED(ReplacePipeline, Succeeded());

  TriangleScene Scene;
  // Dst = (0.25, 0.55, 0.35, 0.85), a full-viewport quad.
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 0.25f, 0.55f, 0.35f, 0.85f,
      3.0f,  -1.0f, 0.0f, 0.25f, 0.55f, 0.35f, 0.85f,
      -1.0f, 3.0f,  0.0f, 0.25f, 0.55f, 0.35f, 0.85f,
  };
  PreparedDraw ReplaceDraw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*ReplacePipeline, ReplaceDraw),
                    Succeeded());

  std::array<float, 4> BlendConstants{0.1f, 0.2f, 0.3f, 0.4f};
  // Src = (0.6, 0.4, 0.7, 0.9) for both subsequent draws.
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 0.6f, 0.4f, 0.7f, 0.9f,
      3.0f,  -1.0f, 0.0f, 0.6f, 0.4f, 0.7f, 0.9f,
      -1.0f, 3.0f,  0.0f, 0.6f, 0.4f, 0.7f, 0.9f,
  };

  // Draw 2: Add + ConstantColor factors (same as
  // MatchesHandComputedBlendEquationForConstantColorFactors), producing
  // (176, 138, 187, 0) as its own real, packed 8-bit result -- confirmed
  // by that test above.
  BlendState AddBlend;
  AddBlend.BlendEnable = true;
  AddBlend.SrcColorFactor = BlendFactor::OneMinusConstantColor;
  AddBlend.DstColorFactor = BlendFactor::SrcColor;
  AddBlend.ColorOp = BlendOp::Add;
  AddBlend.SrcAlphaFactor = BlendFactor::ConstantColor;
  AddBlend.DstAlphaFactor = BlendFactor::OneMinusConstantAlpha;
  AddBlend.AlphaOp = BlendOp::Subtract;
  Expected<GraphicsPipeline> AddPipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, StencilState{},
      AddBlend, /*LogicOpEnable=*/false, LogicOp::Copy, BlendConstants);
  ASSERT_THAT_EXPECTED(AddPipeline, Succeeded());
  PreparedDraw AddDraw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*AddPipeline, AddDraw), Succeeded());
  EXPECT_NEAR(Scene.AttachmentStorage[0], 176, 1);
  EXPECT_NEAR(Scene.AttachmentStorage[1], 138, 1);
  EXPECT_NEAR(Scene.AttachmentStorage[2], 187, 1);
  EXPECT_EQ(Scene.AttachmentStorage[3], 0);

  // Draw 3: Min + ReverseSubtract (same as
  // MatchesHandComputedBlendEquationForMinAndReverseSubtract), this time
  // reading Draw 2's real (176, 138, 187, 0) as its own Dst.
  BlendState MinBlend;
  MinBlend.BlendEnable = true;
  MinBlend.SrcColorFactor = BlendFactor::OneMinusConstantColor;
  MinBlend.DstColorFactor = BlendFactor::SrcColor;
  MinBlend.ColorOp = BlendOp::Min;
  MinBlend.SrcAlphaFactor = BlendFactor::Zero;
  MinBlend.DstAlphaFactor = BlendFactor::OneMinusConstantAlpha;
  MinBlend.AlphaOp = BlendOp::ReverseSubtract;
  Expected<GraphicsPipeline> MinPipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, StencilState{},
      MinBlend, /*LogicOpEnable=*/false, LogicOp::Copy, BlendConstants);
  ASSERT_THAT_EXPECTED(MinPipeline, Succeeded());
  PreparedDraw MinDraw = Scene.prepare();
  ASSERT_THAT_ERROR(executeDraws(*MinPipeline, MinDraw), Succeeded());

  // Dst2 = (176, 138, 187, 0) / 255 = (0.6902, 0.5412, 0.7333, 0.0).
  // Color: min(Src[C]*(1-Constant[C]), Dst2[C]*Src[C]):
  //   R: min(0.54, 0.6902*0.6=0.41412) = 0.41412 -> round(*255) = 106
  //   G: min(0.32, 0.5412*0.4=0.21648) = 0.21648 -> round(*255) = 55
  //   B: min(0.49, 0.7333*0.7=0.51333) = 0.49 -> round(*255) = 125
  // Alpha (ReverseSubtract): Dst2[3]*(1-Constant[3]) - Src[3]*0 = 0 - 0 =
  //   0.
  for (uint32_t I = 0; I != 16; ++I) {
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4], 106, 1) << "texel " << I;
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4 + 1], 55, 1) << "texel " << I;
    EXPECT_NEAR(Scene.AttachmentStorage[I * 4 + 2], 125, 1) << "texel " << I;
    EXPECT_EQ(Scene.AttachmentStorage[I * 4 + 3], 0) << "texel " << I;
  }
}

// (Roadmap H103) Reproduces the real `dEQP-VK.pipeline.fast_linked_
// library.blend.format.*` CTS family's own geometry exactly --
// `createOverlappingQuads`' four translated, partially-overlapping
// quads over a 32x32 attachment (`vktPipelineVertexUtil.cpp`) -- but
// with each quad's own blend state forced to a plain "replace"
// (`SrcColorFactor=One`/`DstColorFactor=Zero`) and a distinct solid
// color per quad, decoupling the question entirely from blend-equation
// arithmetic (already confirmed correct in isolation by the three tests
// above): whichever quad is drawn *last* over a given pixel should be
// the color that pixel ends up as, with no blending at all. Any texel
// that ends up a color other than one of the four quads' own solid
// colors (or the initial clear color, for the 0 pixels no quad
// reaches) would indicate a rasterization/coverage bug specific to
// this exact overlapping-quad geometry -- the one remaining untested
// ingredient of the real CTS test's own repro shape.
TEST(ExecutorTest,
    OverlappingQuadGeometryLeavesEveryTexelOneOfTheFourSolidColors) {
  Context Ctx;
  BlendState Replace;
  Replace.BlendEnable = true;
  Replace.SrcColorFactor = BlendFactor::One;
  Replace.DstColorFactor = BlendFactor::Zero;
  Replace.SrcAlphaFactor = BlendFactor::One;
  Replace.DstAlphaFactor = BlendFactor::Zero;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::TriangleList, DepthState{}, StencilState{}, Replace);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  constexpr uint32_t Size = 32;
  std::array<uint8_t, Size * Size * 4> AttachmentStorage{};
  AttachmentView Color{AttachmentStorage, cpu::ResourceFormat::R8G8B8A8_UNORM,
                      Size, Size};
  std::array<AttachmentView, 1> Attachments{Color};

  // Same translations/quadSize/colors as `createOverlappingQuads`
  // (`vktPipelineVertexUtil.cpp`).
  struct { float X, Y; } Translations[4] = {
      {-0.25f, -0.25f}, {-1.0f, -0.25f}, {-1.0f, -1.0f}, {-0.25f, -1.0f}};
  struct { float R, G, B, A; } QuadColors[4] = {
      {1.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 1.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f, 1.0f},
      {1.0f, 0.0f, 1.0f, 1.0f}};
  constexpr float QuadSize = 1.25f;

  for (uint32_t Q = 0; Q != 4; ++Q) {
    float X = Translations[Q].X, Y = Translations[Q].Y;
    float R = QuadColors[Q].R, G = QuadColors[Q].G, B = QuadColors[Q].B,
          A = QuadColors[Q].A;
    std::vector<float> VertexData = {
        X,          Y,          0.0f, R, G, B, A, // lower-left
        X + QuadSize, Y,          0.0f, R, G, B, A, // lower-right
        X,          Y + QuadSize, 0.0f, R, G, B, A, // upper-left
        X + QuadSize, Y,          0.0f, R, G, B, A, // lower-right
        X,          Y + QuadSize, 0.0f, R, G, B, A, // upper-left
        X + QuadSize, Y + QuadSize, 0.0f, R, G, B, A, // upper-right
    };
    std::vector<VertexAttribute> Attributes = {
        {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
        {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
    std::vector<VertexBufferBinding> Bindings = {VertexBufferBinding{
        0, 28,
        ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
                 VertexData.size() * sizeof(float)),
        Attributes}};

    PreparedDraw Draw;
    Draw.Attachments = Attachments;
    Draw.Viewports[0] =
        ViewportState{0.0f, 0.0f, float(Size), float(Size), 0.0f, 1.0f};
    Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
    Draw.VertexBuffers = Bindings;
    DrawCommand Cmd;
    Cmd.VertexCount = static_cast<uint32_t>(VertexData.size() / 7);
    Cmd.InstanceCount = 1;
    std::array<DrawCommand, 1> Draws{Cmd};
    Draw.Draws = Draws;
    ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded())
        << "quad " << Q;
  }

  auto IsOneOf = [](uint8_t R, uint8_t G, uint8_t B, uint8_t A) {
    // (0,0,0,0): untouched clear. The four quad colors: red, green,
    // blue, magenta, each fully opaque.
    return (R == 0 && G == 0 && B == 0 && A == 0) ||
           (R == 255 && G == 0 && B == 0 && A == 255) ||
           (R == 0 && G == 255 && B == 0 && A == 255) ||
           (R == 0 && G == 0 && B == 255 && A == 255) ||
           (R == 255 && G == 0 && B == 255 && A == 255);
  };
  uint32_t Bad = 0;
  for (uint32_t Y = 0; Y != Size; ++Y) {
    for (uint32_t X = 0; X != Size; ++X) {
      uint32_t I = (Y * Size + X) * 4;
      uint8_t R = AttachmentStorage[I], G = AttachmentStorage[I + 1],
              B = AttachmentStorage[I + 2], A = AttachmentStorage[I + 3];
      if (!IsOneOf(R, G, B, A)) {
        ++Bad;
        ADD_FAILURE() << "texel (" << X << "," << Y << ") = (" << (int)R
                      << "," << (int)G << "," << (int)B << "," << (int)A
                      << ") is not one of the clear color or the four "
                         "quads' own solid colors";
      }
    }
  }
  EXPECT_EQ(Bad, 0u);
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

/// (Roadmap H11) A genuinely-bound (not `VK_ATTACHMENT_UNUSED`/unmapped)
/// color attachment with no matching fragment-shader output at all --
/// distinct from `ColorAttachmentLocationsUnusedLeavesAttachmentUnchanged`
/// above, which tests an explicit unused *remap*, not a plain single-output
/// fragment stage bound against more attachments than it writes -- must
/// leave that attachment unchanged rather than fail the draw. CTS's own
/// `dEQP-VK.renderpasses.*.unused_clear_attachments.*` family shares one
/// pipeline (with every slot's format populated) across draws that only
/// ever write a subset of its declared attachments.
TEST(ExecutorTest, ColorAttachmentWithNoFragmentOutputLeavesItUnchanged) {
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

  // Only declares an output at location 0; there is no location-1 output at
  // all (not merely an unused remap of one).
  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  // Two real, bound color attachments -- the pipeline itself only knows
  // about attachment 0's format via the fragment stage's own output, but
  // the draw binds a second, genuinely-real attachment the shader never
  // writes.
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4},
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments),
      StencilState{}, std::vector<BlendState>{BlendState{}, BlendState{}});

  std::array<uint8_t, 64> Color0Storage{};
  // A distinctive pre-existing value so a surviving "unchanged" attachment
  // is unambiguous.
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

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());
  for (uint32_t I = 0; I != 16; ++I) {
    // Target 0 gets the solid red input color...
    EXPECT_EQ(Color0Storage[I * 4], 255) << "texel " << I;
    EXPECT_EQ(Color0Storage[I * 4 + 1], 0) << "texel " << I;
    // ...and target 1, with no fragment output at all, is unchanged from
    // its distinctive pre-existing value.
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

// Roadmap H7f: `sampleShadingEnable`. A fragment shader that reads
// `SV_SampleIndex` (element 0, no location -- a pure system value) and
// writes `SampleIndex / 3.0` into its red channel: with sample shading
// enabled and no resolve attachment bound, each of a fully-covered pixel's
// 4 raw MSAA samples must show a distinct red value matching its own
// sample index, proving the fragment stage really ran once per sample
// rather than once per pixel with its single result broadcast to every
// sample (the pre-H7f/`SampleShadingEnable == false` behavior, still
// covered by `MultisampleResolveAveragesPerPixelCoverage` above, which
// only ever observes one shaded value per pixel after resolve).
constexpr char SampleIndexFragmentShaderIR[] = R"(
  define void @fs_sampleindex() #0 {
    %sidx = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
    %sf = uitofp i32 %sidx to float
    %r = fdiv float %sf, 3.0
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %r, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
    ret void
  }
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

TEST(ExecutorTest, SampleShadingEnableInvokesFragmentOncePerSample) {
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

  SignatureElement SampleIndexIn;
  SampleIndexIn.ElementID = 0;
  SampleIndexIn.Direction = SignatureDirection::Input;
  SampleIndexIn.SystemValue = SignatureSystemValue::SampleIndex;
  SampleIndexIn.ComponentType = SignatureComponentType::UInt;
  EntrySignature FSSig;
  FSSig.Elements = {SampleIndexIn, makeElement(1, SignatureDirection::Output, 4,
                                               /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, SampleIndexFragmentShaderIR, "fs_sampleindex", FSSig,
                   ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/4,
      {AttachmentFormat{cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}},
      StencilState{}, std::vector<BlendState>{BlendState{}},
      /*LogicOpEnable=*/false, LogicOp::Copy,
      std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false, /*SampleShadingEnable=*/true);

  constexpr uint32_t Samples = 4;
  std::vector<uint8_t> MSStorage(4u * 4u * Samples * 4u, 0);
  AttachmentView MSColor{MSStorage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4};
  std::array<AttachmentView, 1> Attachs{MSColor};

  // A triangle covering the whole [-1, 1] NDC square, so every sample of
  // every pixel is covered.
  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
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

  // Pixel (0, 0)'s 4 raw samples: each one's red channel must match its
  // own sample index (`S / 3.0`, quantized to a UNORM8 byte), not all be
  // identical (the broadcast-from-one-shaded-value behavior this feature
  // replaces).
  for (uint32_t S = 0; S != Samples; ++S) {
    size_t Off = S * 4;
    uint8_t Expected =
        static_cast<uint8_t>(std::lround(255.0 * static_cast<double>(S) / 3.0));
    EXPECT_NEAR(MSStorage[Off], Expected, 2) << "sample " << S;
  }
  EXPECT_NE(MSStorage[0], MSStorage[1 * 4]);
  EXPECT_NE(MSStorage[0], MSStorage[3 * 4]);
}

// Roadmap H7p: a fragment shader that reads `SV_Position`/`gl_FragCoord`
// (element 0, `SystemValue::Position`, components 0/1 -- `.x`/`.y`) and
// writes them straight into its red/green channels, with no
// `SV_SampleIndex`/`gl_SampleID` dependency at all -- the exact shape
// that exposed a real `dEQP-VK.pipeline.monolithic.multisample.
// min_sample_shading_enabled.min_1_0.samples_2.quad` failure
// ("Got less unique colors than requested through minSampleShading"),
// since `gl_FragCoord` previously stayed pixel-center on every one of
// `processTile`'s per-sample passes (see H7o/H7p's own roadmap entries).
constexpr char FragCoordFragmentShaderIR[] = R"(
  define void @fs_fragcoord() #0 {
    %fx = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %fy = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %fx, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %fy, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

TEST(ExecutorTest, SampleShadingEnableVariesFragCoordPerSamplePosition) {
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

  SignatureElement FragCoordIn;
  FragCoordIn.ElementID = 0;
  FragCoordIn.Direction = SignatureDirection::Input;
  FragCoordIn.ComponentType = SignatureComponentType::Float;
  FragCoordIn.SystemValue = SignatureSystemValue::Position;
  FragCoordIn.FirstComponent = 0;
  FragCoordIn.ComponentCount = 4;
  EntrySignature FSSig;
  FSSig.Elements = {FragCoordIn, makeElement(1, SignatureDirection::Output, 4,
                                             /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, FragCoordFragmentShaderIR, "fs_fragcoord", FSSig,
                   ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/4,
      {AttachmentFormat{cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}},
      StencilState{}, std::vector<BlendState>{BlendState{}},
      /*LogicOpEnable=*/false, LogicOp::Copy,
      std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false, /*SampleShadingEnable=*/true);

  constexpr uint32_t Samples = 4;
  std::vector<uint8_t> MSStorage(4u * 4u * Samples * 4u, 0);
  AttachmentView MSColor{MSStorage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4};
  std::array<AttachmentView, 1> Attachs{MSColor};

  // A triangle covering the whole [-1, 1] NDC square, so every sample of
  // every pixel is covered.
  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, //
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

  // Pixel (0, 0)'s own 4-sample "N-rooks" pattern (see `samplePositions`
  // in Executor.cpp): each sample's fractional offset within the pixel is
  // distinct in both X and Y, so each raw sample's stored red/green byte
  // must match that sample's own real position -- not the pixel-center
  // (0.5, 0.5) every sample would show under the pre-H7p bug.
  constexpr std::array<std::array<float, 2>, Samples> ExpectedOffsets = {
      {{0.375f, 0.125f}, {0.875f, 0.375f}, {0.125f, 0.625f}, {0.625f, 0.875f}}};
  for (uint32_t S = 0; S != Samples; ++S) {
    size_t Off = S * 4;
    uint8_t ExpectedR = static_cast<uint8_t>(
        std::lround(255.0 * static_cast<double>(ExpectedOffsets[S][0])));
    uint8_t ExpectedG = static_cast<uint8_t>(
        std::lround(255.0 * static_cast<double>(ExpectedOffsets[S][1])));
    EXPECT_NEAR(MSStorage[Off], ExpectedR, 2) << "sample " << S << " red";
    EXPECT_NEAR(MSStorage[Off + 1], ExpectedG, 2) << "sample " << S << " green";
  }
  // No two samples' red channels (nor green channels) coincide -- proving
  // every pass genuinely re-evaluated `gl_FragCoord` at its own sample
  // position rather than broadcasting one pixel-center-derived value.
  for (uint32_t S = 0; S != Samples; ++S)
    for (uint32_t T = S + 1; T != Samples; ++T) {
      EXPECT_NE(MSStorage[S * 4], MSStorage[T * 4]) << S << " vs " << T;
      EXPECT_NE(MSStorage[S * 4 + 1], MSStorage[T * 4 + 1]) << S << " vs " << T;
    }
}

// Roadmap H7f: `alphaToOneEnable` forces every color attachment's output
// alpha to `1.0` regardless of what the fragment shader itself wrote,
// applied after `RectangularSmooth`'s own line-coverage alpha multiply
// (F5) -- exercised here with a shader that writes a partially-transparent
// alpha (0.25) to prove the pipeline state, not the shader, wins.
TEST(ExecutorTest, AlphaToOneEnableForcesOutputAlphaToOne) {
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
      BlendMode::Replace, /*SampleCount=*/1,
      {AttachmentFormat{cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}},
      StencilState{}, std::vector<BlendState>{BlendState{}},
      /*LogicOpEnable=*/false, LogicOp::Copy,
      std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false, /*SampleShadingEnable=*/false,
      /*AlphaToOneEnable=*/true);

  TriangleScene Scene;
  // Same fully-covering triangle as `FillsFullyCoveredTriangleWithSolidColor`,
  // but every vertex's alpha is 0.25.
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.25f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.25f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 0.25f, // v2
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());

  for (uint32_t I = 0; I != 16; ++I) {
    const uint8_t *Texel = Scene.AttachmentStorage.data() + I * 4;
    EXPECT_EQ(Texel[0], 255) << "texel " << I;
    EXPECT_EQ(Texel[3], 255) << "texel " << I << " (alphaToOne)";
  }
}

/// (roadmap H7n) `alphaToCoverageEnable` clears a sample's coverage bit
/// when the fragment stage's own output at location 0's alpha falls
/// below that sample's own per-sample threshold `(S + 0.5) /
/// SampleCount`, before either the depth/stencil test or the color merge
/// -- so an `alpha == 0.25` quad at `SampleCount == 4` covers exactly
/// sample 0 (`0.25 >= (0 + 0.5) / 4 == 0.125`) and clears samples 1-3
/// (`0.25 < (1 + 0.5) / 4 == 0.375`, and likewise for 2 and 3), leaving
/// those three samples at whatever the attachment already held (`0` here,
/// its zero-initialized clear value) rather than the shaded color.
TEST(ExecutorTest, AlphaToCoverageEnableGeneratesPerSampleCoverageFromAlpha) {
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
      {AttachmentFormat{cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}},
      StencilState{}, std::vector<BlendState>{BlendState{}},
      /*LogicOpEnable=*/false, LogicOp::Copy,
      std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false, /*SampleShadingEnable=*/false,
      /*AlphaToOneEnable=*/false, /*AlphaToCoverageEnable=*/true);

  constexpr uint32_t Samples = 4;
  std::vector<uint8_t> MSStorage(4u * 4u * Samples * 4u, 0);
  AttachmentView MSColor{MSStorage, cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4};
  std::array<AttachmentView, 1> Attachs{MSColor};

  TriangleScene Scene;
  // Same fully-covering triangle as `AlphaToOneEnableForcesOutputAlphaToOne`,
  // but every vertex's alpha is 0.25.
  Scene.VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.25f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.25f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 0.25f, // v2
  };
  PreparedDraw Draw = Scene.prepare();
  Draw.Attachments = Attachs;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw), Succeeded());

  for (uint32_t Pixel = 0; Pixel != 16; ++Pixel) {
    for (uint32_t S = 0; S != Samples; ++S) {
      const uint8_t *Texel = MSStorage.data() + (Pixel * Samples + S) * 4;
      if (S == 0) {
        EXPECT_EQ(Texel[0], 255) << "pixel " << Pixel << " sample " << S;
        EXPECT_EQ(Texel[3], 64)
            << "pixel " << Pixel << " sample " << S << " (unorm8 0.25)";
      } else {
        EXPECT_EQ(Texel[0], 0) << "pixel " << Pixel << " sample " << S
                               << " (alphaToCoverage should have culled "
                                  "this sample)";
      }
    }
  }
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

// (Roadmap H112) A tessellation-path counterpart to
// `ClipCullDistanceVertexShaderIR`/`ClipsATriangleAgainstAWrittenClip
// Distance` above: each control point's own `gl_ClipDistance[0]` (location
// 1, a plain per-vertex scalar attribute here, not derived from position)
// is passed through the hull stage's own per-invocation self-indexed
// `gl_out[id].gl_ClipDistance[0] = gl_in[id].gl_ClipDistance[0]` (mirroring
// the real, non-barrier `dEQP-VK.clipping.user_defined.clip_distance.
// vert_tess.*` shader's exact shape -- see this milestone's own roadmap
// row and `VulkanCTSReport.md`), then the domain stage barycentrically
// interpolates it the same way it already interpolates position, and
// writes the result to the final `SignatureSystemValue::ClipDistance`
// output `Executor.cpp`'s own clip-plane test reads.
constexpr char TessClipDistanceVertexShaderIR[] = R"(
  define void @vs_main() #0 {
    %px = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %py = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
    %pz = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
    %clip = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %px, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %py, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %pz, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %clip, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="vertex" }
)";

// The hull control-point phase: a self-indexed passthrough of this
// invocation's own input control point's position and `gl_ClipDistance`,
// exactly matching the real CTS shader's own shape (see the file comment
// above).
constexpr char TessClipDistanceHullShaderIR[] = R"(
  define void @hs_main() #0 {
    %id = call i32 @feme.stage.input.load.i32(i32 2, i32 0, i32 0, i32 0)
    %px = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 %id)
    %py = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 %id)
    %pz = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 %id)
    %clip = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 %id)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %px, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %py, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %pz, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %clip, i32 0)
    ret void
  }
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="hull" }
)";

// The domain stage: barycentric evaluation of the three control points'
// position (as `TessDomainShaderIR` above) and `gl_ClipDistance` alike,
// writing the interpolated clip distance to the final
// `SignatureSystemValue::ClipDistance` output (element 4) `Executor.cpp`'s
// own clip-plane test reads back.
constexpr char TessClipDistanceDomainShaderIR[] = R"(
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
    %c0 = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
    %c1 = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 1)
    %c2 = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 2)
    %cu = fmul float %c0, %u
    %cv = fmul float %c1, %v
    %cw = fmul float %c2, %w
    %ca = fadd float %cu, %cv
    %c = fadd float %ca, %cw
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %c, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="domain" }
)";

/// A constant, solid-red fragment shader taking no inputs -- this test
/// only cares whether the executor's own clip-distance test admits a
/// fragment at all, not what color it writes.
constexpr char TessClipDistanceSolidRedFragmentShaderIR[] = R"(
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

/// Builds the vertex/hull/patch-constant/domain/fragment pipeline the
/// shaders above implement: like `buildTessellatedPipeline`, but each
/// control point also carries a `gl_ClipDistance[0]` value (location 1)
/// threaded through the hull and domain stages instead of a color varying.
Expected<GraphicsPipeline>
buildTessClipDistancePipeline(Context &Ctx, uint32_t AttachmentSize) {
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 1, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 3, /*Location=*/1),
      makeElement(3, SignatureDirection::Output, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::ClipDistance, /*RowCount=*/1)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, TessClipDistanceVertexShaderIR, "vs_main", VSSig,
                  ShaderStage::Vertex);
  if (!VS)
    return VS.takeError();

  EntrySignature HSSig;
  SignatureElement ControlPointID =
      makeElement(2, SignatureDirection::Input, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::OutputControlPointID);
  ControlPointID.ComponentType = SignatureComponentType::UInt;
  HSSig.Elements = {makeElement(0, SignatureDirection::Input, 3,
                                /*Location=*/1),
                    makeElement(1, SignatureDirection::Input, 1,
                                /*Location=*/std::nullopt,
                                SignatureSystemValue::ClipDistance,
                                /*RowCount=*/1),
                    ControlPointID,
                    makeElement(3, SignatureDirection::Output, 3,
                                /*Location=*/1),
                    makeElement(4, SignatureDirection::Output, 1,
                                /*Location=*/std::nullopt,
                                SignatureSystemValue::ClipDistance,
                                /*RowCount=*/1)};
  Expected<std::shared_ptr<CompiledStage>> HS =
      compileStage(Ctx, TessClipDistanceHullShaderIR, "hs_main", HSSig,
                  ShaderStage::Hull);
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
  std::string PCIR = formatPatchConstantIR("4.0");
  Expected<std::shared_ptr<CompiledStage>> PCS =
      compileStage(Ctx, PCIR, "pc_main", PCSig, ShaderStage::Hull);
  if (!PCS)
    return PCS.takeError();

  EntrySignature DSSig;
  DSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/std::nullopt,
                  SignatureSystemValue::DomainLocation),
      makeElement(1, SignatureDirection::Input, 3, /*Location=*/1),
      makeElement(2, SignatureDirection::Input, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::ClipDistance, /*RowCount=*/1),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(4, SignatureDirection::Output, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::ClipDistance, /*RowCount=*/1)};
  Expected<std::shared_ptr<CompiledStage>> DS = compileStage(
      Ctx, TessClipDistanceDomainShaderIR, "ds_main", DSSig, ShaderStage::Domain);
  if (!DS)
    return DS.takeError();

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, TessClipDistanceSolidRedFragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
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

// (Roadmap H112) A tessellation-path counterpart to
// `ClipsATriangleAgainstAWrittenClipDistance`: the same full-viewport
// triangle, tessellated (factor 4.0, so this only rasterizes correctly if
// every one of the domain stage's own generated points really carries the
// right interpolated `gl_ClipDistance[0]`), each control point's own clip
// distance written equal to its own NDC Y coordinate -- exactly the same
// affine-interpolation trick `ClipsATriangleAgainstAWrittenClipDistance`
// uses, except carried through the hull/domain chain instead of read
// straight off the vertex stage. If this test fails while
// `ClipsATriangleAgainstAWrittenClipDistance` (the non-tessellated
// sibling) passes, the bug is specific to the tessellation path -- see
// this milestone's own roadmap row.
TEST(ExecutorTest, ClipsATessellatedPatchAgainstAWrittenClipDistance) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline =
      buildTessClipDistancePipeline(Ctx, /*AttachmentSize=*/4);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // Interleaved position (xyz), clip-distance (1 float), 4 floats/control
  // point -- clip-distance equal to each control point's own NDC Y, same
  // as `ClipsATriangleAgainstAWrittenClipDistance`'s own vertex data.
  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, -1.0f, // control point 0
      3.0f,  -1.0f, 0.0f, -1.0f, // control point 1
      -1.0f, 3.0f,  0.0f, 3.0f,  // control point 2
  };
  std::vector<VertexAttribute> Attributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32_FLOAT, 12}};
  std::vector<VertexBufferBinding> Bindings = {VertexBufferBinding{
      0, 16,
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
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Storage.data() + (Y * 4 + X) * 4;
  };
  for (uint32_t Y : {2u, 3u})
    for (uint32_t X = 0; X != 4; ++X)
      EXPECT_EQ(texel(X, Y)[3], 255) << "x=" << X << " y=" << Y;
  for (uint32_t Y : {0u, 1u})
    for (uint32_t X = 0; X != 4; ++X)
      EXPECT_EQ(texel(X, Y)[3], 0) << "x=" << X << " y=" << Y;
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

// (Roadmap H21e) A two-stream geometry entry point: stream 0 emits a
// degenerate decoy triangle (an unreferenced varying only, no
// `SV_Position`), while stream 1 emits a real full-viewport triangle and
// is the only stream that writes `SV_Position` -- so a real render only
// covers the viewport if rasterization actually consumes stream 1's own
// records, not stream 0's (the merged-stream default before this row).
constexpr char TwoStreamGeometryShaderIR[] = R"(
  define void @gs_main() #0 {
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 9.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 9.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 9.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 9.0, i32 0)
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.cut(i32 0)

    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.stream.emit(i32 1)

    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float 3.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.stream.emit(i32 1)

    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float 3.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.stream.emit(i32 1)
    call void @feme.stage.stream.cut(i32 1)
    ret void
  }
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.stream.emit(i32)
  declare void @feme.stage.stream.cut(i32)
  attributes #0 = { "feme.shader.stage"="geometry" }
)";

/// Builds a vertex/geometry/fragment `GraphicsPipeline` whose geometry
/// stage (`TwoStreamGeometryShaderIR`) writes two independent output
/// streams, with \p RasterizationStream selecting which one rasterization
/// consumes (`VkPipelineRasterizationStateStreamCreateInfoEXT`, roadmap
/// H21e).
Expected<GraphicsPipeline>
buildTwoStreamGeometryPipeline(Context &Ctx, uint32_t AttachmentSize,
                               uint32_t RasterizationStream) {
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, VertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  if (!VS)
    return VS.takeError();

  // Stream 0's own decoy element carries no system value and no
  // `Location` the fragment stage below ever reads, so it stays
  // completely inert whichever stream rasterization selects; only
  // stream 1 declares `SV_Position`, so `findElement` (unfiltered by
  // stream, an existing simplification this milestone does not lift --
  // see FeMeVulkanDesign.md's V7 section) always resolves it to stream
  // 1's own position, matching every real shape any importable shader
  // can produce today (the MLIR SPIR-V dialect cannot deserialize
  // `OpEmitStreamVertex`/`OpEndStreamPrimitive` at all, so a real,
  // CTS-driven multi-stream signature with `SV_Position` on more than
  // one stream can never actually reach this code).
  SignatureElement StreamZeroDecoy =
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/1);
  StreamZeroDecoy.Stream = 0;
  SignatureElement StreamOnePosition = makeElement(
      1, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
      SignatureSystemValue::Position);
  StreamOnePosition.Stream = 1;
  SignatureElement StreamOneColor =
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/0);
  StreamOneColor.Stream = 1;
  EntrySignature GSSig;
  GSSig.Elements = {StreamZeroDecoy, StreamOnePosition, StreamOneColor};
  Expected<std::shared_ptr<CompiledStage>> GS = compileStage(
      Ctx, TwoStreamGeometryShaderIR, "gs_main", GSSig, ShaderStage::Geometry);
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
  RasterState Raster{CullMode::None, FrontFace::CounterClockwise};
  Raster.RasterizationStream = RasterizationStream;
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      Raster, DepthState{}, BlendMode::Replace, /*SampleCount=*/1,
      std::move(Attachments));
  GeometryState Geom;
  Geom.InputPrimitive = GeometryInputPrimitive::Triangles;
  Geom.OutputPrimitive = GeometryOutputPrimitive::TriangleStrip;
  Geom.MaxOutputVertices = 3;
  Pipeline.setGeometryStage(std::move(*GS), Geom);
  return Pipeline;
}

TEST(ExecutorTest, RasterizationStreamSelectsGeometryOutputFromANonzeroStream) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildTwoStreamGeometryPipeline(
      Ctx, /*AttachmentSize=*/8, /*RasterizationStream=*/1);
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

  // Solid green: stream 1's own color, at stream 1's own (viewport-
  // covering) position -- stream 0's decoy triangle (degenerate, off
  // screen, and colorless since it carries no `SV_Position`) never
  // reaches the rasterizer at all.
  for (uint32_t I = 0; I != Size * Size; ++I) {
    const uint8_t *Texel = Storage.data() + I * 4;
    EXPECT_EQ(Texel[0], 0) << "texel " << I;
    EXPECT_EQ(Texel[1], 255) << "texel " << I;
    EXPECT_EQ(Texel[2], 0) << "texel " << I;
    EXPECT_EQ(Texel[3], 255) << "texel " << I;
  }
}

// (Roadmap H21e) `VK_EXT_transform_feedback` capture from a geometry
// stage's own output (roadmap H21c originally scoped this to a vertex-
// shader-only pipeline): every one of the geometry stage's own emitted
// vertices, in emission order, has its `XfbBuffer`-tagged element(s)
// written to the bound transform-feedback buffer, exactly like the
// vertex-shader-only capture path does for `VSOutput`.
// (Roadmap H21e) A geometry entry point with a real `SV_Position` (a
// full-viewport triangle, like the passthrough test's own shape) that
// also writes a constant color to an `XfbBuffer`-tagged element, used to
// test transform-feedback capture sourced from a geometry stage's own
// output rather than `VSOutput`.
constexpr char GeometryXfbCaptureShaderIR[] = R"(
  define void @gs_main() #0 {
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.stream.emit(i32 0)

    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 3.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.stream.emit(i32 0)

    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 3.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.cut(i32 0)
    ret void
  }
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.stream.emit(i32)
  declare void @feme.stage.stream.cut(i32)
  attributes #0 = { "feme.shader.stage"="geometry" }
)";

TEST(ExecutorTest, CapturesGeometryStageOutputToBoundTransformFeedbackBuffer) {
  Context Ctx;
  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Input, 4, /*Location=*/1),
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position),
      makeElement(3, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, VertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  EntrySignature GSSig;
  SignatureElement PositionOut = makeElement(
      0, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
      SignatureSystemValue::Position);
  SignatureElement ColorOut =
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0);
  ColorOut.XfbBuffer = 0;
  ColorOut.XfbOffset = 0;
  ColorOut.XfbStride = 16;
  GSSig.Elements = {PositionOut, ColorOut};
  Expected<std::shared_ptr<CompiledStage>> GS = compileStage(
      Ctx, GeometryXfbCaptureShaderIR, "gs_main", GSSig,
      ShaderStage::Geometry);
  ASSERT_THAT_EXPECTED(GS, Succeeded());

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, 4, 4}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  GeometryState Geom;
  Geom.InputPrimitive = GeometryInputPrimitive::Triangles;
  Geom.OutputPrimitive = GeometryOutputPrimitive::TriangleStrip;
  Geom.MaxOutputVertices = 3;
  Pipeline.setGeometryStage(std::move(*GS), Geom);

  std::vector<float> VertexData = {
      -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v0
      3.0f,  -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v1
      -1.0f, 3.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // v2
  };
  std::vector<VertexAttribute> VtxAttributes = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0},
      {1, cpu::ResourceFormat::R32G32B32A32_FLOAT, 12}};
  std::vector<VertexBufferBinding> Bindings = {VertexBufferBinding{
      0, 28,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      VtxAttributes}};

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

  std::array<uint8_t, 48> XfbStorage{}; // 3 vertices * 16 bytes each.
  uint64_t CapturedBytes = 0;
  std::array<PreparedDraw::XfbCaptureBuffer, 1> XfbBuffers = {
      PreparedDraw::XfbCaptureBuffer{MutableArrayRef(XfbStorage),
                                     &CapturedBytes}};
  Draw.XfbBuffers = XfbBuffers;

  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;
  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  EXPECT_EQ(CapturedBytes, 48u);
  for (uint32_t V = 0; V != 3; ++V) {
    float Captured[4];
    std::memcpy(Captured, XfbStorage.data() + V * 16, sizeof(Captured));
    EXPECT_FLOAT_EQ(Captured[0], 1.0f) << "vertex " << V;
    EXPECT_FLOAT_EQ(Captured[1], 0.0f) << "vertex " << V;
    EXPECT_FLOAT_EQ(Captured[2], 0.0f) << "vertex " << V;
    EXPECT_FLOAT_EQ(Captured[3], 1.0f) << "vertex " << V;
  }
}

// (Roadmap H21d) A geometry entry point that emits real primitives --
// `EmitVertex`/`EndPrimitive` calls with a nonzero `max_vertices` budget
// -- but writes no per-vertex attributes at all, mirroring
// `VK_EXT_primitives_generated_query`'s own CTS geometry shaders for
// every input topology but `point_list` (only the `point_list` shape
// writes `gl_PointSize`; every other shape's geometry entry point calls
// nothing but `EmitVertex()`/`EndPrimitive()`, per topologyData's own
// `outputPoints` check in `vktPrimitivesGeneratedQueryTests.cpp`).
// `EntrySignature::Elements` for this shape reflects identically empty to
// `EmptyGeometryShaderIR` below (SPIR-V only lists an entry point's *used*
// interface variables, and this stage uses none), but unlike that shape
// this one really does emit -- three real vertices, closed with one real
// `EndPrimitive`, on every invocation. Before this fix, `executeDraws`
// conflated "empty signature" with "never emits," early-returning as a
// no-op and reporting `ClippingInvocations`/`GeometryShaderPrimitives`
// as zero regardless of how many primitives a shape like this one really
// emitted -- exactly `VK_EXT_primitives_generated_query`'s own
// `dEQP-VK.transform_feedback.primitives_generated_query.*.geom.*`
// (every non-`point_list` topology) always reporting `pgqGenerated == 0`.
constexpr char EmitsRealPrimitivesWithoutAttributesGeometryShaderIR[] = R"(
  define void @gs_main() #0 {
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.emit(i32 0)
    call void @feme.stage.stream.cut(i32 0)
    ret void
  }
  declare void @feme.stage.stream.emit(i32)
  declare void @feme.stage.stream.cut(i32)
  attributes #0 = { "feme.shader.stage"="geometry" }
)";

/// A fragment stage with no inputs at all, writing a constant color --
/// used instead of `SolidRedFragmentShaderIR` (defined later in this
/// file) purely to avoid a forward reference.
constexpr char NoInputSolidRedFragmentShaderIR[] = R"(
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

Expected<GraphicsPipeline>
buildAttributelessEmittingGeometryPipeline(Context &Ctx,
                                           uint32_t AttachmentSize) {
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

  EntrySignature GSSig; // Deliberately empty: this stage writes no
                        // per-vertex attributes, even though it emits.
  Expected<std::shared_ptr<CompiledStage>> GS = compileStage(
      Ctx, EmitsRealPrimitivesWithoutAttributesGeometryShaderIR, "gs_main",
      GSSig, ShaderStage::Geometry);
  if (!GS)
    return GS.takeError();

  EntrySignature FSSig; // No inputs: the geometry stage forwards none.
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, NoInputSolidRedFragmentShaderIR, "fs_main", FSSig,
                  ShaderStage::Fragment);
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

TEST(ExecutorTest,
    GeometryStageThatEmitsRealPrimitivesWithoutAttributesCountsThem) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline =
      buildAttributelessEmittingGeometryPipeline(Ctx, /*AttachmentSize=*/4);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  // 4 disjoint triangles in one `TriangleList` draw.
  constexpr uint32_t NumTriangles = 4;
  std::vector<float> VertexData;
  for (uint32_t I = 0; I != NumTriangles; ++I) {
    float Base = -1.0f + I * 0.1f;
    float V[3][3] = {{Base, -1.0f, 0.0f},
                      {Base + 0.05f, -1.0f, 0.0f},
                      {Base, -0.95f, 0.0f}};
    for (auto &Vert : V) {
      VertexData.push_back(Vert[0]);
      VertexData.push_back(Vert[1]);
      VertexData.push_back(Vert[2]);
      VertexData.push_back(1.0f);
      VertexData.push_back(0.0f);
      VertexData.push_back(0.0f);
      VertexData.push_back(1.0f);
    }
  }
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
  Cmd.VertexCount = NumTriangles * 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;
  PreparedDraw::PipelineStatsCounters Stats;
  Draw.Stats = &Stats;
  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // Every one of the 4 input triangles reaches the geometry stage, which
  // emits exactly one (attribute-less) output triangle per invocation --
  // this must count as 4 real primitives, not the pre-fix 0.
  EXPECT_EQ(Stats.GeometryShaderInvocations, 4u);
  EXPECT_EQ(Stats.GeometryShaderPrimitives, 4u);
  EXPECT_EQ(Stats.ClippingInvocations, 4u);

  // No real position was ever written, so nothing renders -- the color
  // attachment stays exactly as it started (all zero).
  for (uint8_t Byte : Storage)
    EXPECT_EQ(Byte, 0u);
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

// (roadmap H7l) Same fix as `RendersTriangleListWithAdjacencyCoreTriangle
// WithoutAGeometryStage` above, exercised through `LineListWithAdjacency`
// instead: each 4-vertex adjacency window is `(adj0, v0, v1, adj1)`
// (`SplitPrimitiveAdjacency`'s own documented line order), so the core
// line is indices 1 and 2. Mirrors `RendersAHorizontalLineList`'s own
// verification (a horizontal line through screen row 1, rows 0/2
// untouched), with the two adjacency-only vertices placed off-canvas so a
// leak would visibly paint outside that row.
TEST(ExecutorTest, RendersLineListWithAdjacencyCoreLineWithoutAGeometryStage) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildPipeline(
      Ctx, RasterState{CullMode::None, FrontFace::CounterClockwise},
      PrimitiveTopology::LineListWithAdjacency);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  TriangleScene Scene;
  Scene.VertexData = {
      50.0f, 50.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, // adj0
      -1.0f, 0.25f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, // v0 (core)
      1.0f,  0.25f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, // v1 (core)
      50.0f, 50.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, // adj1
  };
  PreparedDraw Draw = Scene.prepare();

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw), Succeeded());

  auto texel = [&](uint32_t X, uint32_t Y) {
    return Scene.AttachmentStorage.data() + (Y * 4 + X) * 4;
  };
  for (uint32_t X = 0; X != 4; ++X) {
    const uint8_t *Texel = texel(X, 2);
    EXPECT_EQ(Texel[3], 255) << "x=" << X;
  }
  EXPECT_EQ(texel(0, 1)[3], 0);
  EXPECT_EQ(texel(0, 3)[3], 0);
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
        @llvm.dx.resource.handlefromheap(i32 0)
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
      @llvm.dx.resource.handlefromheap(i32)
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
        @llvm.dx.resource.handlefromheap(i32 0)
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
      @llvm.dx.resource.handlefromheap(i32)
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
Expected<GraphicsPipeline> buildMeshPipeline(
    Context &Ctx, bool WithTaskStage, uint32_t AttachmentSize = 4,
    AmplificationDispatchLimits MeshLimits = {{65535, 65535, 65535}, 4194304},
    AmplificationDispatchLimits TaskLimits = {{65535, 65535, 65535}, 4194304}) {
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
  // (roadmap H6f) `setMeshStage` now requires its own dispatch limits
  // explicitly, mirroring the real `VkPhysicalDeviceMeshShaderProperties
  // EXT`-sourced values `feme::vulkan::GraphicsPipeline::
  // buildExecutorPipeline` supplies; \p MeshLimits/\p TaskLimits default to
  // the same permissive bound `Executor.cpp` itself hardcoded before that
  // plumbing existed, but a caller may tighten either to exercise
  // rejection (see `RejectsAMeshDispatchExceedingItsPipelineDispatchLimits`
  // below).
  Pipeline.setMeshStage(std::move(TS), std::move(*MS), Mesh, MeshLimits,
                        TaskLimits);
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

// (roadmap H95) Builds a mesh pipeline whose mesh entry point's own
// signature is entirely empty -- no `SV_Position`/any other Output
// element at all, exactly the shape every one of the CTS's
// `properties.*_payload_size`/`*_shared_memory_size` cases' generated
// mesh shaders takes (`SetMeshOutputsEXT(0, 0)`-only bodies whose sole
// observable effect is a bound UAV/storage-buffer write): a real device
// legally omits the fragment stage too whenever nothing it could write
// would ever reach a rendered pixel, so this also has zero color
// attachments and no fragment stage, matching the CTS's own
// zero-attachment render pass shape.
Expected<GraphicsPipeline> buildSideEffectOnlyMeshPipeline(Context &Ctx) {
  EntrySignature MeshSig; // Deliberately left with no Elements at all.
  Expected<std::shared_ptr<CompiledStage>> MS = compileStage(
      Ctx, MeshGroupIDDoublingShaderIR, "ms_main", MeshSig, ShaderStage::Mesh);
  if (!MS)
    return MS.takeError();

  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, /*FragmentStage=*/nullptr,
      PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, /*Attachments=*/{},
      StencilState{}, /*ColorBlends=*/{});
  MeshState Mesh;
  Mesh.OutputTopology = MeshOutputTopology::Triangles;
  Mesh.MaxOutputVertices = 3;
  Mesh.MaxOutputPrimitives = 1;
  AmplificationDispatchLimits Permissive{{65535, 65535, 65535}, 4194304};
  Pipeline.setMeshStage(/*TaskStage=*/nullptr, std::move(*MS), Mesh,
                        Permissive, Permissive);
  return Pipeline;
}

// (roadmap H95) Before this milestone, `executeDraws` treated a mesh
// entry point with an entirely empty signature as unconditionally a
// no-op and returned success without ever dispatching a single mesh
// workgroup -- correct for every mesh entry this implementation could
// compile before this milestone (none had any observable effect besides
// contributing to the rasterizer), but wrong the moment a mesh entry's
// only observable effect is a side effect with no rasterizer-visible
// output at all, silently dropping it. This is the root cause this
// milestone fixes (`dEQP-VK.mesh_shader.ext.properties.*_payload_size`'s
// "Unexpected shared memory result: 0"): every dispatched mesh workgroup
// must still run for its side effects even though nothing it does ever
// reaches the rasterizer.
TEST(ExecutorTest,
    RunsAMeshEntryWithNoOutputSignatureForItsSideEffectsAndRastersNothing) {
  Context Ctx;
  Expected<GraphicsPipeline> Pipeline = buildSideEffectOnlyMeshPipeline(Ctx);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

  std::vector<int32_t> MeshBuffer(4, -1);
  cpu::FemeDescriptor Desc{};
  Desc.Data = MeshBuffer.data();
  Desc.SizeInBytes = MeshBuffer.size() * sizeof(int32_t);
  Desc.Kind = static_cast<uint32_t>(cpu::ResourceKind::Raw);
  Desc.Flags = FEME_DESCRIPTOR_UAV;

  PreparedDraw Draw;
  Draw.Resources.ResourceHeap = ArrayRef<cpu::FemeDescriptor>(&Desc, 1);
  MeshDrawCommand MDC;
  MDC.GroupCount = {4, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // Every one of the 4 directly-dispatched mesh workgroups ran exactly
  // once, each writing its own GroupID.x doubled at its own slot -- had
  // the old unconditional no-op early return still been in place, every
  // slot would still read its initial -1 sentinel instead.
  EXPECT_EQ(MeshBuffer, (std::vector<int32_t>{0, 2, 4, 6}));
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

// (roadmap H6f) A direct mesh dispatch (no task stage) exceeding the
// pipeline's own `getMeshDispatchLimits()` is rejected rather than run --
// the real counterpart of `Pipeline.h`'s hardcoded placeholder this
// milestone replaced, mirroring `maxComputeWorkGroupCount`'s own draw-time
// enforcement for compute (`CommandBuffer.cpp`'s `validateGroupCount`).
TEST(ExecutorTest, RejectsADirectMeshDispatchExceedingItsPipelineLimits) {
  Context Ctx;
  AmplificationDispatchLimits TightMeshLimits{{1, 1, 1}, 1};
  Expected<GraphicsPipeline> Pipeline = buildMeshPipeline(
      Ctx, /*WithTaskStage=*/false, /*AttachmentSize=*/4, TightMeshLimits);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

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
  MeshDrawCommand MDC;
  MDC.GroupCount = {4, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1), Failed());
}

// The task-dispatch counterpart of the test above: a task stage's own
// dispatch (`MeshDrawCommand::GroupCount`, when a task stage is bound) is
// checked against `getTaskDispatchLimits()`, independently of the mesh
// dispatch limit the task's own `EmitMeshTasksEXT` request is separately
// checked against.
TEST(ExecutorTest, RejectsATaskDispatchExceedingItsPipelineLimits) {
  Context Ctx;
  AmplificationDispatchLimits Permissive{{65535, 65535, 65535}, 4194304};
  AmplificationDispatchLimits TightTaskLimits{{1, 1, 1}, 1};
  Expected<GraphicsPipeline> Pipeline =
      buildMeshPipeline(Ctx, /*WithTaskStage=*/true, /*AttachmentSize=*/4,
                        Permissive, TightTaskLimits);
  ASSERT_THAT_EXPECTED(Pipeline, Succeeded());

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
  MeshDrawCommand MDC;
  MDC.GroupCount = {4, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(*Pipeline, Draw, /*WorkerCount=*/1), Failed());
}

// Roadmap H6c-a-a-ii: a mesh entry's canonicalized `feme.stage.set_mesh_
// outputs`/`feme.stage.output.store` calls declare and write one vertex
// (`PerVertex`-frequency `SV_Position`, element 0) and one primitive
// (`PerPrimitive`-frequency element 1, an otherwise-unconsumed scalar)
// from the same workgroup. Before this row, `Executor::runMeshWorkgroup`
// never populated `MeshResources::PrimitiveOutputLayout`/`PrimitiveOutputs`
// at all (left null/empty), so `MeshOutputWrapperPass`'s already-correct
// per-primitive store lowering (H6c-a-a) would read/write through a null
// layout pointer the moment a real compiled entry exercised it, and its
// own `flattenMeshRow`/`unflattenMeshRow` walked *every* Output element
// unconditionally, silently misaligning any signature -- like this one --
// that mixes `PerVertex` and `PerPrimitive` elements. This test exercises
// exactly that mixed shape end to end and asserts the vertex data still
// renders correctly (proving the frequency split does not corrupt or
// misalign the per-vertex path) and that the primitive store's own read/
// write no longer crashes -- `PointList`-shaped output (roadmap H6e's own
// "a point-class draw rasterizes every one of its own vertices directly"
// path) needs no `PrimitiveIndices` write, which remains separately
// unwired (see `MeshOutputWrapper.h`'s own comment), so this is the
// cleanest mixed-frequency shape reachable today.
constexpr char MeshMixedFrequencyOutputShaderIR[] = R"(
  define void @ms_main() #0 {
    call void @feme.stage.set_mesh_outputs(i32 1, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -0.25, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 0.25, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float 7.0, i32 0)
    ret void
  }
  declare void @feme.stage.set_mesh_outputs(i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

TEST(
    ExecutorTest,
    RoutesAPerPrimitiveOutputElementIntoPrimitiveOutputsAlongsideAPerVertexOne) {
  Context Ctx;
  EntrySignature MeshSig;
  SignatureElement PosElt =
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position);
  SignatureElement PrimElt =
      makeElement(1, SignatureDirection::Output, 1, /*Location=*/0);
  PrimElt.Frequency = SignatureFrequency::PerPrimitive;
  MeshSig.Elements = {PosElt, PrimElt};
  Expected<std::shared_ptr<CompiledStage>> MS =
      compileStage(Ctx, MeshMixedFrequencyOutputShaderIR, "ms_main", MeshSig,
                   ShaderStage::Mesh);
  ASSERT_THAT_EXPECTED(MS, Succeeded());

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, SolidRedFragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  uint32_t Size = 4;
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, Size, Size}};
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  MeshState Mesh;
  Mesh.OutputTopology = MeshOutputTopology::Points;
  Mesh.MaxOutputVertices = 1;
  Mesh.MaxOutputPrimitives = 1;
  AmplificationDispatchLimits Permissive{{65535, 65535, 65535}, 4194304};
  Pipeline.setMeshStage(/*TaskStage=*/nullptr, std::move(*MS), Mesh, Permissive,
                        Permissive);

  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, Size,
                       Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  MeshDrawCommand MDC;
  MDC.GroupCount = {1, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // NDC (-0.25, 0.25) maps to pixel (1, 2) of the 4x4 target (same mapping
  // `RendersAPointList` already establishes) -- proving the per-vertex
  // `SV_Position` this workgroup wrote still reaches rasterization
  // correctly even though it shares this signature with a `PerPrimitive`
  // element, which the fix's own frequency-filtered `flattenMeshRow`/
  // `unflattenMeshRow` must keep from misaligning.
  auto texel = [&](uint32_t X, uint32_t Y) {
    return Storage.data() + (Y * Size + X) * 4;
  };
  const uint8_t *Red = texel(1, 2);
  EXPECT_EQ(Red[0], 255);
  EXPECT_EQ(Red[1], 0);
  EXPECT_EQ(Red[3], 255);
  const uint8_t *Untouched = texel(0, 0);
  EXPECT_EQ(Untouched[3], 0);
}

// (Roadmap H31) A real triangle-topology mesh workgroup emitting one
// primitive with its own `PerPrimitive`-frequency color output (mirroring
// `dEQP-VK.mesh_shader.ext.api.draw.*`'s own `perprimitiveEXT out vec4
// primitiveColor[]` shape, the CTS's sole mesh-shader color-output path),
// consumed by an ordinary fragment shader reading that varying by
// location like any per-vertex one (`FragmentShaderIR` above). Before this
// fix, `Executor::runMeshWorkgroup`'s merge step (`unflattenMeshRow`) only
// ever copied `PerVertex`-frequency Output elements into the flat `Merged`
// storage the shared varying-linking/interpolation code reads back from;
// a `PerPrimitive` element's own computed value -- present in each
// `Meshlet::getPrimitives()` row -- was never written into `Merged` at
// all, so the fragment shader always read zero-initialized storage for
// it, rendering fully transparent black instead of this primitive's own
// color. This exercises the real rasterization path end to end (not just
// `RoutesAPerPrimitiveOutputElementIntoPrimitiveOutputsAlongsideAPerVertexOne`
// above, which never has a fragment stage actually read the per-primitive
// value at all) and asserts every covered pixel reads back this
// primitive's own solid green, not black/transparent.
constexpr char MeshPerPrimitiveColorShaderIR[] = R"(
  define void @ms_main() #0 {
    call void @feme.stage.set_mesh_outputs(i32 3, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 3.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 3.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 2)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 1, i32 1, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 2, i32 2, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.0, i32 0)
    ret void
  }
  declare void @feme.stage.set_mesh_outputs(i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

TEST(ExecutorTest,
     PerPrimitiveMeshColorReachesFragmentInputInsteadOfReadingAsZero) {
  Context Ctx;
  SignatureElement PosElt =
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position);
  SignatureElement IdxElt = makeElement(
      1, SignatureDirection::Output, 3, /*Location=*/std::nullopt);
  IdxElt.ComponentType = SignatureComponentType::UInt;
  IdxElt.Frequency = SignatureFrequency::PerPrimitive;
  IdxElt.SystemValue = SignatureSystemValue::PrimitiveIndices;
  SignatureElement ColorElt =
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/0);
  ColorElt.Frequency = SignatureFrequency::PerPrimitive;
  EntrySignature MeshSig;
  MeshSig.Elements = {PosElt, IdxElt, ColorElt};
  Expected<std::shared_ptr<CompiledStage>> MS = compileStage(
      Ctx, MeshPerPrimitiveColorShaderIR, "ms_main", MeshSig, ShaderStage::Mesh);
  ASSERT_THAT_EXPECTED(MS, Succeeded());

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  uint32_t Size = 4;
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, Size, Size}};
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  MeshState Mesh;
  Mesh.OutputTopology = MeshOutputTopology::Triangles;
  Mesh.MaxOutputVertices = 3;
  Mesh.MaxOutputPrimitives = 1;
  AmplificationDispatchLimits Permissive{{65535, 65535, 65535}, 4194304};
  Pipeline.setMeshStage(/*TaskStage=*/nullptr, std::move(*MS), Mesh, Permissive,
                        Permissive);

  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, Size,
                       Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  MeshDrawCommand MDC;
  MDC.GroupCount = {1, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // The triangle (-1,-1)/(3,-1)/(-1,3) in NDC covers the whole [-1, 1]
  // viewport, so every pixel of this 4x4 target must read back the
  // primitive's own solid green -- not black/transparent, which is what
  // this fix's regression would silently render instead.
  auto texel = [&](uint32_t X, uint32_t Y) {
    return Storage.data() + (Y * Size + X) * 4;
  };
  for (uint32_t Y = 0; Y != Size; ++Y) {
    for (uint32_t X = 0; X != Size; ++X) {
      const uint8_t *Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0) << "x=" << X << " y=" << Y;
      EXPECT_EQ(Texel[1], 255) << "x=" << X << " y=" << Y;
      EXPECT_EQ(Texel[2], 0) << "x=" << X << " y=" << Y;
      EXPECT_EQ(Texel[3], 255) << "x=" << X << " y=" << Y;
    }
  }
}

// (Roadmap H69) Two triangle-topology mesh primitives sharing two of a
// quad's four vertices (the same "simple quad emitted as two triangles"
// shape H69's own case-reduction traced the underlying bug to), each with
// its own distinct `PerPrimitive`-frequency color. Before this fix,
// `Executor::runMeshWorkgroup`'s merge step stashed each primitive's own
// `PerPrimitive` value into the *same* shared per-vertex `Merged` rows
// `RasterizePrimitives` reads a corner's varyings from -- legitimate reuse
// for `PerVertex` data (every primitive touching a shared vertex agrees on
// its value by construction), but not for `PerPrimitive` data, so
// whichever primitive got merged second silently clobbered the first
// one's own value at their two shared vertices, corrupting that first
// primitive's rendered color at exactly the corners it shares with its
// neighbor. This asserts each triangle's own solid color reaches every one
// of its own covered pixels -- including the ones nearest the shared
// edge -- rather than one of the two colors bleeding across it.
constexpr char MeshTwoPrimitivesSharedVerticesShaderIR[] = R"(
  define void @ms_main() #0 {
    call void @feme.stage.set_mesh_outputs(i32 4, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 1.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 1.0, i32 3)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 1.0, i32 3)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 3)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 3)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 1, i32 1, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 2, i32 2, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 1, i32 1)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 1, i32 3, i32 1)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 2, i32 2, i32 1)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float 0.0, i32 1)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float 0.0, i32 1)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float 1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.0, i32 1)
    ret void
  }
  declare void @feme.stage.set_mesh_outputs(i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

TEST(ExecutorTest,
     PerPrimitiveColorsDoNotBleedAcrossPrimitivesSharingAVertex) {
  Context Ctx;
  SignatureElement PosElt =
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position);
  SignatureElement IdxElt = makeElement(
      1, SignatureDirection::Output, 3, /*Location=*/std::nullopt);
  IdxElt.ComponentType = SignatureComponentType::UInt;
  IdxElt.Frequency = SignatureFrequency::PerPrimitive;
  IdxElt.SystemValue = SignatureSystemValue::PrimitiveIndices;
  SignatureElement ColorElt =
      makeElement(2, SignatureDirection::Output, 4, /*Location=*/0);
  ColorElt.Frequency = SignatureFrequency::PerPrimitive;
  EntrySignature MeshSig;
  MeshSig.Elements = {PosElt, IdxElt, ColorElt};
  Expected<std::shared_ptr<CompiledStage>> MS =
      compileStage(Ctx, MeshTwoPrimitivesSharedVerticesShaderIR, "ms_main",
                   MeshSig, ShaderStage::Mesh);
  ASSERT_THAT_EXPECTED(MS, Succeeded());

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, FragmentShaderIR, "fs_main", FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  uint32_t Size = 4;
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, Size, Size}};
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  MeshState Mesh;
  Mesh.OutputTopology = MeshOutputTopology::Triangles;
  Mesh.MaxOutputVertices = 4;
  Mesh.MaxOutputPrimitives = 2;
  AmplificationDispatchLimits Permissive{{65535, 65535, 65535}, 4194304};
  Pipeline.setMeshStage(/*TaskStage=*/nullptr, std::move(*MS), Mesh, Permissive,
                        Permissive);

  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, Size,
                       Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  MeshDrawCommand MDC;
  MDC.GroupCount = {1, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // Triangle 0 (vertices 0,1,2) and triangle 1 (vertices 1,3,2) meet along
  // the quad's diagonal, so a pixel `(X, Y)`'s rasterized winner is
  // determined by `X + Y` relative to that diagonal: `X + Y <= 2` lands in
  // triangle 0's own solid green, `X + Y >= 3` in triangle 1's own solid
  // blue (the tie-break along the diagonal itself, `X + Y == 2`, always
  // resolving to triangle 0). This samples the row of pixels immediately
  // on each side of that line -- exactly where the pre-fix bug's shared-
  // vertex clobbering would bleed one primitive's color into the other's
  // -- rather than only each triangle's own far corner.
  auto texel = [&](uint32_t X, uint32_t Y) {
    return Storage.data() + (Y * Size + X) * 4;
  };
  // Immediately on triangle 0's side of the diagonal (X + Y == 2): solid
  // green.
  for (auto [X, Y] : {std::pair{0u, 2u}, std::pair{1u, 1u}, std::pair{2u, 0u}}) {
    const uint8_t *Texel = texel(X, Y);
    EXPECT_EQ(Texel[0], 0) << "x=" << X << " y=" << Y;
    EXPECT_EQ(Texel[1], 255) << "x=" << X << " y=" << Y;
    EXPECT_EQ(Texel[2], 0) << "x=" << X << " y=" << Y;
    EXPECT_EQ(Texel[3], 255) << "x=" << X << " y=" << Y;
  }
  // Immediately on triangle 1's side of the diagonal (X + Y == 3): solid
  // blue.
  for (auto [X, Y] : {std::pair{0u, 3u}, std::pair{1u, 2u}, std::pair{2u, 1u}}) {
    const uint8_t *Texel = texel(X, Y);
    EXPECT_EQ(Texel[0], 0) << "x=" << X << " y=" << Y;
    EXPECT_EQ(Texel[1], 0) << "x=" << X << " y=" << Y;
    EXPECT_EQ(Texel[2], 255) << "x=" << X << " y=" << Y;
    EXPECT_EQ(Texel[3], 255) << "x=" << X << " y=" << Y;
  }
}

// (Roadmap H93) Four `Points`-topology mesh primitives whose own
// `SignatureSystemValue::PrimitiveIndices` output (width 1, per
// `getVerticesPerPrimitive(Points)`) all name the *same* single vertex --
// mirroring `dEQP-VK.mesh_shader.ext.properties.max_mesh_output_
// primitives_256`'s own shape (every primitive using `max_vertices=1` and
// writing `gl_PrimitivePointIndicesEXT[primitiveID] = 0u`). Before this
// fix, `RasterizePrimitives`'s own point path always rasterized exactly
// `RasterOutRef.InvocationCount` points -- one per *vertex* row, correct
// for every non-mesh point-topology chain (there, each point invocation
// always owns its own exclusive vertex row) but silently collapsing this
// legal mesh shape's 4 primitives down to the meshlet's own single merged
// vertex row, so only 1 of the 4 primitives ever reached the fragment
// stage. A constant-low-alpha fragment output, additively blended, makes
// the miscount directly observable: 1 surviving primitive accumulates to
// ~32/255, 4 to ~128/255.
constexpr char MeshFourPointPrimitivesSharedVertexShaderIR[] = R"(
  define void @ms_main() #0 {
    call void @feme.stage.set_mesh_outputs(i32 1, i32 4)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -0.25, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 0.25, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 0, i32 1)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 0, i32 2)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 0, i32 3)
    ret void
  }
  declare void @feme.stage.set_mesh_outputs(i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

// A fragment stage with no inputs, writing a fixed low (0.125) red/alpha --
// small enough that even 4 additively-blended fragments (0.5 total) stay
// well clear of the 8-bit UNORM target's saturation ceiling, so the sum
// itself is what distinguishes "1 primitive rasterized" from "4".
constexpr char LowAlphaConstantRedFragmentShaderIR[] = R"(
  define void @fs_main() #0 {
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 0.125, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 0.125, i32 0)
    ret void
  }
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

TEST(ExecutorTest, RastersEveryPointPrimitiveEvenWhenTheyShareASingleVertex) {
  Context Ctx;
  SignatureElement PosElt =
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position);
  SignatureElement IdxElt =
      makeElement(1, SignatureDirection::Output, 1, /*Location=*/std::nullopt);
  IdxElt.ComponentType = SignatureComponentType::UInt;
  IdxElt.Frequency = SignatureFrequency::PerPrimitive;
  IdxElt.SystemValue = SignatureSystemValue::PrimitiveIndices;
  EntrySignature MeshSig;
  MeshSig.Elements = {PosElt, IdxElt};
  Expected<std::shared_ptr<CompiledStage>> MS =
      compileStage(Ctx, MeshFourPointPrimitivesSharedVertexShaderIR, "ms_main",
                   MeshSig, ShaderStage::Mesh);
  ASSERT_THAT_EXPECTED(MS, Succeeded());

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, LowAlphaConstantRedFragmentShaderIR, "fs_main", FSSig,
                   ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  uint32_t Size = 4;
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R8G8B8A8_UNORM, Size, Size}};
  BlendState AdditiveBlend;
  AdditiveBlend.BlendEnable = true;
  AdditiveBlend.SrcColorFactor = BlendFactor::One;
  AdditiveBlend.DstColorFactor = BlendFactor::One;
  AdditiveBlend.SrcAlphaFactor = BlendFactor::One;
  AdditiveBlend.DstAlphaFactor = BlendFactor::One;
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments),
      StencilState{}, std::vector<BlendState>{AdditiveBlend});
  MeshState Mesh;
  Mesh.OutputTopology = MeshOutputTopology::Points;
  Mesh.MaxOutputVertices = 1;
  Mesh.MaxOutputPrimitives = 4;
  AmplificationDispatchLimits Permissive{{65535, 65535, 65535}, 4194304};
  Pipeline.setMeshStage(/*TaskStage=*/nullptr, std::move(*MS), Mesh, Permissive,
                        Permissive);

  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R8G8B8A8_UNORM, Size,
                       Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  MeshDrawCommand MDC;
  MDC.GroupCount = {1, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // NDC (-0.25, 0.25) maps to pixel (1, 2) of the 4x4 target (same mapping
  // `RendersAPointList` already establishes). All 4 primitives share that
  // one pixel: pre-fix, only 1 of them ever reached the fragment stage
  // (~32/255); post-fix, all 4 do (~128/255).
  auto texel = [&](uint32_t X, uint32_t Y) {
    return Storage.data() + (Y * Size + X) * 4;
  };
  const uint8_t *Pixel = texel(1, 2);
  EXPECT_NEAR(Pixel[0], 128, 2);
  EXPECT_EQ(Pixel[1], 0);
  EXPECT_EQ(Pixel[2], 0);
  EXPECT_NEAR(Pixel[3], 128, 2);
  const uint8_t *Untouched = texel(0, 0);
  EXPECT_EQ(Untouched[3], 0);
}

// (Roadmap H93b) The same two-triangles-sharing-a-diagonal quad shape as
// `PerPrimitiveColorsDoNotBleedAcrossPrimitivesSharingAVertex` above, but
// each triangle also authors its own explicit `PerPrimitive`
// `SignatureSystemValue::PrimitiveID` output -- deliberately the *reverse*
// of raster/emission order (triangle 0, emitted first, authors 100;
// triangle 1, emitted second, authors 7) so that the pre-fix behavior
// (`RasterizePrimitives` always synthesizing `gl_PrimitiveID` from its own
// raster-order `PrimitiveCounter`, ignoring any authored value) and the
// post-fix behavior (preferring the mesh entry's own authored value) are
// unambiguously distinguishable: pre-fix, the fragment shader would see
// 0/1; post-fix, it sees the authored 100/7.
constexpr char MeshTwoPrimitivesAuthoredPrimitiveIDShaderIR[] = R"(
  define void @ms_main() #0 {
    call void @feme.stage.set_mesh_outputs(i32 4, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 1.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 1.0, i32 3)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 1.0, i32 3)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 3)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 3)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 1, i32 1, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 2, i32 2, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 1, i32 1)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 1, i32 3, i32 1)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 2, i32 2, i32 1)
    call void @feme.stage.output.store.i32(i32 2, i32 0, i32 0, i32 100, i32 0)
    call void @feme.stage.output.store.i32(i32 2, i32 0, i32 0, i32 7, i32 1)
    ret void
  }
  declare void @feme.stage.set_mesh_outputs(i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

// Reads `gl_PrimitiveID` (element 0, a `SystemValue::PrimitiveID` fragment
// input) and writes it straight through to a single-channel `R32_UINT`
// color attachment, so each covered pixel directly reports the
// `PrimitiveID` value the fragment invocation actually observed.
constexpr char PrimitiveIDPassthroughFragmentShaderIR[] = R"(
  define void @fs_main() #0 {
    %pid = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 %pid, i32 0)
    ret void
  }
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

TEST(ExecutorTest, FragmentPrimitiveIDPrefersAMeshEntrysAuthoredValue) {
  Context Ctx;
  SignatureElement PosElt =
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position);
  SignatureElement IdxElt = makeElement(
      1, SignatureDirection::Output, 3, /*Location=*/std::nullopt);
  IdxElt.ComponentType = SignatureComponentType::UInt;
  IdxElt.Frequency = SignatureFrequency::PerPrimitive;
  IdxElt.SystemValue = SignatureSystemValue::PrimitiveIndices;
  SignatureElement PrimitiveIDElt =
      makeElement(2, SignatureDirection::Output, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::PrimitiveID);
  PrimitiveIDElt.ComponentType = SignatureComponentType::UInt;
  PrimitiveIDElt.Frequency = SignatureFrequency::PerPrimitive;
  EntrySignature MeshSig;
  MeshSig.Elements = {PosElt, IdxElt, PrimitiveIDElt};
  Expected<std::shared_ptr<CompiledStage>> MS = compileStage(
      Ctx, MeshTwoPrimitivesAuthoredPrimitiveIDShaderIR, "ms_main", MeshSig,
      ShaderStage::Mesh);
  ASSERT_THAT_EXPECTED(MS, Succeeded());

  SignatureElement FSPrimitiveIDElt =
      makeElement(0, SignatureDirection::Input, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::PrimitiveID);
  FSPrimitiveIDElt.ComponentType = SignatureComponentType::UInt;
  SignatureElement FSColorOut =
      makeElement(1, SignatureDirection::Output, 1, /*Location=*/0);
  FSColorOut.ComponentType = SignatureComponentType::UInt;
  EntrySignature FSSig;
  FSSig.Elements = {FSPrimitiveIDElt, FSColorOut};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, PrimitiveIDPassthroughFragmentShaderIR, "fs_main",
                   FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  uint32_t Size = 4;
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R32_UINT, Size, Size}};
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  MeshState Mesh;
  Mesh.OutputTopology = MeshOutputTopology::Triangles;
  Mesh.MaxOutputVertices = 4;
  Mesh.MaxOutputPrimitives = 2;
  AmplificationDispatchLimits Permissive{{65535, 65535, 65535}, 4194304};
  Pipeline.setMeshStage(/*TaskStage=*/nullptr, std::move(*MS), Mesh, Permissive,
                        Permissive);

  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R32_UINT, Size, Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  MeshDrawCommand MDC;
  MDC.GroupCount = {1, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // Same diagonal split as the shared-vertex-color test above: `X + Y <= 2`
  // is triangle 0 (authored `PrimitiveID` 100), `X + Y >= 3` is triangle 1
  // (authored `PrimitiveID` 7). Before this fix, every covered pixel would
  // instead read back raster order (0 or 1, in emission order), never 100
  // or 7.
  auto texel = [&](uint32_t X, uint32_t Y) -> uint32_t {
    uint32_t V;
    std::memcpy(&V, Storage.data() + (Y * Size + X) * 4, sizeof(V));
    return V;
  };
  for (auto [X, Y] : {std::pair{0u, 2u}, std::pair{1u, 1u}, std::pair{2u, 0u}})
    EXPECT_EQ(texel(X, Y), 100u) << "x=" << X << " y=" << Y;
  for (auto [X, Y] : {std::pair{0u, 3u}, std::pair{1u, 2u}, std::pair{2u, 1u}})
    EXPECT_EQ(texel(X, Y), 7u) << "x=" << X << " y=" << Y;
}

// (Roadmap H106) The same two-triangles-sharing-a-diagonal quad shape as
// `MeshTwoPrimitivesAuthoredPrimitiveIDShaderIR` above, but each triangle
// authors an explicit `PerPrimitive` `SignatureSystemValue::CullPrimitive`
// output instead of `PrimitiveID`: triangle 0 (the `X + Y <= 2` half)
// authors `true` (cull), triangle 1 (`X + Y >= 3`) authors `false` (keep).
// Pre-fix, `gl_CullPrimitiveEXT` mapped to `SignatureSystemValue::None`,
// so this write was an ordinary, ignored output and *both* triangles
// rasterized; post-fix, only triangle 1 does.
constexpr char MeshTwoPrimitivesAuthoredCullPrimitiveShaderIR[] = R"(
  define void @ms_main() #0 {
    call void @feme.stage.set_mesh_outputs(i32 4, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 1.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 1.0, i32 3)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 1.0, i32 3)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 3)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 3)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 1, i32 1, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 2, i32 2, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 1, i32 1)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 1, i32 3, i32 1)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 2, i32 2, i32 1)
    call void @feme.stage.output.store.i32(i32 2, i32 0, i32 0, i32 1, i32 0)
    call void @feme.stage.output.store.i32(i32 2, i32 0, i32 0, i32 0, i32 1)
    ret void
  }
  declare void @feme.stage.set_mesh_outputs(i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

// (Roadmap H106) A minimal fragment shader for
// `MeshCullPrimitiveDiscardsAnAuthoredCulledPrimitive`: writes a fixed,
// recognizable `UInt` constant to its one color output, so a covered
// pixel is trivially distinguishable from an untouched (pre-cleared)
// one.
constexpr char UIntConstant12345FragmentShaderIR[] = R"(
  define void @fs_main() #0 {
    call void @feme.stage.output.store.i32(i32 0, i32 0, i32 0, i32 12345, i32 0)
    ret void
  }
  declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

TEST(ExecutorTest, MeshCullPrimitiveDiscardsAnAuthoredCulledPrimitive) {
  Context Ctx;
  SignatureElement PosElt =
      makeElement(0, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position);
  SignatureElement IdxElt =
      makeElement(1, SignatureDirection::Output, 3, /*Location=*/std::nullopt);
  IdxElt.ComponentType = SignatureComponentType::UInt;
  IdxElt.Frequency = SignatureFrequency::PerPrimitive;
  IdxElt.SystemValue = SignatureSystemValue::PrimitiveIndices;
  SignatureElement CullElt =
      makeElement(2, SignatureDirection::Output, 1, /*Location=*/std::nullopt,
                  SignatureSystemValue::CullPrimitive);
  CullElt.ComponentType = SignatureComponentType::Bool;
  CullElt.Frequency = SignatureFrequency::PerPrimitive;
  EntrySignature MeshSig;
  MeshSig.Elements = {PosElt, IdxElt, CullElt};
  Expected<std::shared_ptr<CompiledStage>> MS =
      compileStage(Ctx, MeshTwoPrimitivesAuthoredCullPrimitiveShaderIR,
                   "ms_main", MeshSig, ShaderStage::Mesh);
  ASSERT_THAT_EXPECTED(MS, Succeeded());

  SignatureElement FSColorOut =
      makeElement(0, SignatureDirection::Output, 1, /*Location=*/0);
  FSColorOut.ComponentType = SignatureComponentType::UInt;
  EntrySignature FSSig;
  FSSig.Elements = {FSColorOut};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, UIntConstant12345FragmentShaderIR, "fs_main", FSSig,
                   ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  uint32_t Size = 4;
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R32_UINT, Size, Size}};
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  MeshState Mesh;
  Mesh.OutputTopology = MeshOutputTopology::Triangles;
  Mesh.MaxOutputVertices = 4;
  Mesh.MaxOutputPrimitives = 2;
  AmplificationDispatchLimits Permissive{{65535, 65535, 65535}, 4194304};
  Pipeline.setMeshStage(/*TaskStage=*/nullptr, std::move(*MS), Mesh, Permissive,
                        Permissive);

  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0xAB);
  AttachmentView Color{Storage, cpu::ResourceFormat::R32_UINT, Size, Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  MeshDrawCommand MDC;
  MDC.GroupCount = {1, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // Same diagonal split as `FragmentPrimitiveIDPrefersAMeshEntrysAuthoredValue`
  // above: `X + Y <= 2` is triangle 0 (authored `CullPrimitive` true --
  // discarded, so these pixels must keep their pre-clear 0xABABABAB
  // pattern), `X + Y >= 3` is triangle 1 (authored `CullPrimitive` false --
  // kept, so these pixels see the fragment shader's constant 12345).
  auto texel = [&](uint32_t X, uint32_t Y) -> uint32_t {
    uint32_t V;
    std::memcpy(&V, Storage.data() + (Y * Size + X) * 4, sizeof(V));
    return V;
  };
  for (auto [X, Y] : {std::pair{0u, 2u}, std::pair{1u, 1u}, std::pair{2u, 0u}})
    EXPECT_EQ(texel(X, Y), 0xABABABABu) << "x=" << X << " y=" << Y;
  for (auto [X, Y] : {std::pair{0u, 3u}, std::pair{1u, 2u}, std::pair{2u, 1u}})
    EXPECT_EQ(texel(X, Y), 12345u) << "x=" << X << " y=" << Y;
}

// (Roadmap H8p) A fragment shader with a real `uvec2` output (`UInt`,
// `ComponentCount == 2`) drawn to a real `R16G16_UINT` color attachment --
// exercises `executeDraws`'s widened `FSColors` validation (accepting a
// non-`Float` output for an integer-format attachment, rather than the
// hard rejection every fragment output faced before this row) and
// `readFragmentColorInt`/`packClearColor`'s new raw-integer write path
// end to end, not just `ImageFixtureTest.cpp`'s own pack/unpack-in-
// isolation coverage.
constexpr char PositionOnlyVertexShaderIR[] = R"(
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

constexpr char UInt2ConstantFragmentShaderIR[] = R"(
  define void @fs_main() #0 {
    call void @feme.stage.output.store.i32(i32 0, i32 0, i32 0, i32 12345, i32 0)
    call void @feme.stage.output.store.i32(i32 0, i32 0, i32 1, i32 6789, i32 0)
    ret void
  }
  declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

TEST(ExecutorTest, RendersAUvec2FragmentOutputToAnIntegerColorAttachment) {
  Context Ctx;

  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, PositionOnlyVertexShaderIR, "vs_main", VSSig,
                  ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  SignatureElement UOut = makeElement(
      0, SignatureDirection::Output, 2, /*Location=*/0);
  UOut.ComponentType = SignatureComponentType::UInt;
  EntrySignature FSSig;
  FSSig.Elements = {UOut};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, UInt2ConstantFragmentShaderIR, "fs_main", FSSig,
                  ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R16G16_UINT, 4, 4}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace,
      /*SampleCount=*/1, std::move(Attachments), StencilState{},
      std::vector<BlendState>{BlendState{}}, /*LogicOpEnable=*/false,
      LogicOp::Copy, std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false);

  std::array<float, 9> VertexData = {
      -1.0f, -1.0f, 0.0f, // v0
      3.0f,  -1.0f, 0.0f, // v1
      -1.0f, 3.0f,  0.0f, // v2
  };
  std::vector<VertexAttribute> Attrs = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0}};
  std::array<VertexBufferBinding, 1> Bindings = {VertexBufferBinding{
      0, 12,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attrs}};

  std::array<uint8_t, 16 * 4> AttachmentStorage{};
  AttachmentView Color{AttachmentStorage, cpu::ResourceFormat::R16G16_UINT, 4,
                       4};
  std::array<AttachmentView, 1> Attach = {Color};

  PreparedDraw Draw;
  Draw.Attachments = Attach;
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
    uint16_t R, G;
    memcpy(&R, AttachmentStorage.data() + I * 4, 2);
    memcpy(&G, AttachmentStorage.data() + I * 4 + 2, 2);
    EXPECT_EQ(R, 12345u) << "texel " << I;
    EXPECT_EQ(G, 6789u) << "texel " << I;
  }
}

// (Roadmap H29l) The same `R16G16_UINT` attachment, but with the fragment
// output's signature reporting `SInt` rather than `UInt` -- the shape a
// *real* SPIR-V-sourced stage always has, since LLVM's integer types are
// signless and `CanonicalizeStage.cpp`'s `getComponentType` therefore maps
// every SPIR-V integer to `SInt` regardless of `OpTypeInt`'s own signedness
// bit. Exact-equality matching against `expectedColorComponentType`'s
// `UInt` rejected this, so the sibling test above (which hand-builds a
// `UInt` signature no SPIR-V front end can actually produce) passed while
// every real GLSL `uvec4`-into-a-UINT-attachment pipeline failed.
TEST(ExecutorTest, RendersAnSIntFragmentOutputToAUnsignedIntegerAttachment) {
  Context Ctx;

  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position)};
  Expected<std::shared_ptr<CompiledStage>> VS = compileStage(
      Ctx, PositionOnlyVertexShaderIR, "vs_main", VSSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  SignatureElement SOut =
      makeElement(0, SignatureDirection::Output, 2, /*Location=*/0);
  SOut.ComponentType = SignatureComponentType::SInt;
  EntrySignature FSSig;
  FSSig.Elements = {SOut};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, UInt2ConstantFragmentShaderIR, "fs_main", FSSig,
                   ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R16G16_UINT, 4, 4}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace,
      /*SampleCount=*/1, std::move(Attachments), StencilState{},
      std::vector<BlendState>{BlendState{}}, /*LogicOpEnable=*/false,
      LogicOp::Copy, std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false);

  std::array<float, 9> VertexData = {
      -1.0f, -1.0f, 0.0f, //
      3.0f,  -1.0f, 0.0f, //
      -1.0f, 3.0f,  0.0f,
  };
  std::vector<VertexAttribute> Attrs = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0}};
  std::array<VertexBufferBinding, 1> Bindings = {VertexBufferBinding{
      0, 12,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attrs}};

  std::array<uint8_t, 16 * 4> AttachmentStorage{};
  AttachmentView Color{AttachmentStorage, cpu::ResourceFormat::R16G16_UINT, 4,
                       4};
  std::array<AttachmentView, 1> Attach = {Color};

  PreparedDraw Draw;
  Draw.Attachments = Attach;
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
    uint16_t R, G;
    memcpy(&R, AttachmentStorage.data() + I * 4, 2);
    memcpy(&G, AttachmentStorage.data() + I * 4 + 2, 2);
    EXPECT_EQ(R, 12345u) << "texel " << I;
    EXPECT_EQ(G, 6789u) << "texel " << I;
  }
}

// (Roadmap H8p) A `Float`-typed fragment output is still rejected for an
// integer-format attachment (mirroring the original hard-rejection this
// row widened, not removed): the expected type is now format-dependent
// (`expectedColorComponentType`), not unconditionally `Float`.
TEST(ExecutorTest, RejectsAFloatFragmentOutputForAnIntegerColorAttachment) {
  Context Ctx;

  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, PositionOnlyVertexShaderIR, "vs_main", VSSig,
                  ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  // A plain constant float4 output, avoiding the need to also wire up a
  // matching fragment input (this test only cares about the output's own
  // `ComponentType`, not any interpolated value).
  constexpr char ConstantFloat4FragmentShaderIR[] = R"(
    define void @fs_main() #0 {
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float 1.0, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float 1.0, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float 1.0, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.0, i32 0)
      ret void
    }
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )";
  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, ConstantFloat4FragmentShaderIR, "fs_main", FSSig,
      ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R16G16_UINT, 4, 4}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace,
      /*SampleCount=*/1, std::move(Attachments), StencilState{},
      std::vector<BlendState>{BlendState{}}, /*LogicOpEnable=*/false,
      LogicOp::Copy, std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false);

  std::array<uint8_t, 16 * 4> AttachmentStorage{};
  AttachmentView Color{AttachmentStorage, cpu::ResourceFormat::R16G16_UINT, 4,
                       4};
  std::array<AttachmentView, 1> Attach = {Color};
  PreparedDraw Draw;
  Draw.Attachments = Attach;
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
  DrawCommand Cmd;
  Cmd.VertexCount = 0;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;

  EXPECT_THAT_ERROR(executeDraws(Pipeline, Draw), Failed());
}

// (Roadmap H8p) `Blend.BlendEnable` combined with an integer color-
// attachment format is rejected (`mergeColor`'s own new defensive guard):
// this combination was unreachable before this row (no integer fragment
// output could be drawn at all), but is newly reachable now that a real
// `uvec2` output can target `R16G16_UINT`, so it needs its own explicit
// rejection since blending is undefined for an integer format per spec.
TEST(ExecutorTest, RejectsBlendEnableForAnIntegerColorAttachment) {
  Context Ctx;

  EntrySignature VSSig;
  VSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 3, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/std::nullopt,
                  SignatureSystemValue::Position)};
  Expected<std::shared_ptr<CompiledStage>> VS =
      compileStage(Ctx, PositionOnlyVertexShaderIR, "vs_main", VSSig,
                  ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(VS, Succeeded());

  SignatureElement UOut = makeElement(
      0, SignatureDirection::Output, 2, /*Location=*/0);
  UOut.ComponentType = SignatureComponentType::UInt;
  EntrySignature FSSig;
  FSSig.Elements = {UOut};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, UInt2ConstantFragmentShaderIR, "fs_main", FSSig,
                  ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  BlendState Blend;
  Blend.BlendEnable = true;
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R16G16_UINT, 4, 4}};
  GraphicsPipeline Pipeline(
      std::move(*VS), std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace,
      /*SampleCount=*/1, std::move(Attachments), StencilState{},
      std::vector<BlendState>{Blend}, /*LogicOpEnable=*/false, LogicOp::Copy,
      std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
      /*PrimitiveRestartEnable=*/false);

  std::array<float, 9> VertexData = {
      -1.0f, -1.0f, 0.0f, // v0
      3.0f,  -1.0f, 0.0f, // v1
      -1.0f, 3.0f,  0.0f, // v2
  };
  std::vector<VertexAttribute> Attrs = {
      {0, cpu::ResourceFormat::R32G32B32_FLOAT, 0}};
  std::array<VertexBufferBinding, 1> Bindings = {VertexBufferBinding{
      0, 12,
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexData.data()),
               VertexData.size() * sizeof(float)),
      Attrs}};

  std::array<uint8_t, 16 * 4> AttachmentStorage{};
  AttachmentView Color{AttachmentStorage, cpu::ResourceFormat::R16G16_UINT, 4,
                       4};
  std::array<AttachmentView, 1> Attach = {Color};

  PreparedDraw Draw;
  Draw.Attachments = Attach;
  Draw.Viewports[0] = ViewportState{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, 4, 4};
  Draw.VertexBuffers = Bindings;
  DrawCommand Cmd;
  Cmd.VertexCount = 3;
  Cmd.InstanceCount = 1;
  std::array<DrawCommand, 1> Draws = {Cmd};
  Draw.Draws = Draws;

  EXPECT_THAT_ERROR(executeDraws(Pipeline, Draw), Failed());
}

// (roadmap H108) A single full-screen mesh triangle that authors only
// `SV_Position` -- deliberately never writing any ordinary (non-system-
// value) output at all, exactly like `dEQP-VK.mesh_shader.ext.
// synchronization.mesh_to_frag.*.subpass_dependency`'s own mesh module
// (which only ever writes `primitiveValue` when a preceding task shader
// is present, absent here).
constexpr char MeshFullScreenTriangleNoOrdinaryOutputShaderIR[] = R"(
  define void @ms_main() #0 {
    call void @feme.stage.set_mesh_outputs(i32 3, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 3.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 1)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float -1.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float 3.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float 0.0, i32 2)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.0, i32 2)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 1, i32 1, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 2, i32 2, i32 0)
    ret void
  }
  declare void @feme.stage.set_mesh_outputs(i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="1,1,1" }
)";

// Reads a `Location=0` fragment input (which no vertex/mesh output
// authors) and writes it straight through to a single-channel
// `R32_UINT` color attachment, so a covered pixel directly reports
// whatever value the executor supplied for the unmatched input.
constexpr char UnmatchedLocationZeroPassthroughFragmentShaderIR[] = R"(
  define void @fs_main() #0 {
    %v = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 %v, i32 0)
    ret void
  }
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
  attributes #0 = { "feme.shader.stage"="fragment" }
)";

// (roadmap H108) Before this fix, `executeDraws` rejected any fragment
// input `Location` with no matching pre-rasterization-stage output as a
// hard error ("fragment input location %u has no matching vertex stage
// output"), exactly the failure `dEQP-VK.mesh_shader.ext.synchronization.
// mesh_to_frag.*.subpass_dependency` hit at `vkQueueSubmit` even though
// its shader source is legal Vulkan (the mesh module simply never writes
// a `Location` the paired fragment module happens to read). Per the
// spec's "Shader Interfaces" text this is legal -- the unmatched input
// merely has an undefined value -- so the fix leaves it out of the
// linked `Varyings` list instead of erroring; `buildStageStorage` already
// zero-fills fragment input storage, so the shader reads back 0.
TEST(ExecutorTest,
     UnmatchedFragmentInputLocationReadsZeroInsteadOfErroringOut) {
  Context Ctx;
  EntrySignature MeshSig;
  SignatureElement IdxElt = makeElement(
      1, SignatureDirection::Output, 3, /*Location=*/std::nullopt);
  IdxElt.ComponentType = SignatureComponentType::UInt;
  IdxElt.Frequency = SignatureFrequency::PerPrimitive;
  IdxElt.SystemValue = SignatureSystemValue::PrimitiveIndices;
  MeshSig.Elements = {makeElement(0, SignatureDirection::Output, 4,
                                  /*Location=*/std::nullopt,
                                  SignatureSystemValue::Position),
                      IdxElt};
  Expected<std::shared_ptr<CompiledStage>> MS =
      compileStage(Ctx, MeshFullScreenTriangleNoOrdinaryOutputShaderIR,
                  "ms_main", MeshSig, ShaderStage::Mesh);
  ASSERT_THAT_EXPECTED(MS, Succeeded());

  SignatureElement FSIn = makeElement(0, SignatureDirection::Input, 1,
                                      /*Location=*/0);
  FSIn.ComponentType = SignatureComponentType::UInt;
  SignatureElement FSColorOut =
      makeElement(1, SignatureDirection::Output, 1, /*Location=*/0);
  FSColorOut.ComponentType = SignatureComponentType::UInt;
  EntrySignature FSSig;
  FSSig.Elements = {FSIn, FSColorOut};
  Expected<std::shared_ptr<CompiledStage>> FS = compileStage(
      Ctx, UnmatchedLocationZeroPassthroughFragmentShaderIR, "fs_main",
      FSSig, ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  uint32_t Size = 4;
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R32_UINT, Size, Size}};
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  MeshState Mesh;
  Mesh.OutputTopology = MeshOutputTopology::Triangles;
  Mesh.MaxOutputVertices = 3;
  Mesh.MaxOutputPrimitives = 1;
  AmplificationDispatchLimits Permissive{{65535, 65535, 65535}, 4194304};
  Pipeline.setMeshStage(/*TaskStage=*/nullptr, std::move(*MS), Mesh, Permissive,
                        Permissive);

  std::vector<uint8_t> Storage((size_t)Size * Size * 4, 0xAB);
  AttachmentView Color{Storage, cpu::ResourceFormat::R32_UINT, Size, Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  MeshDrawCommand MDC;
  MDC.GroupCount = {1, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  uint32_t V;
  std::memcpy(&V, Storage.data(), sizeof(V));
  EXPECT_EQ(V, 0u);
}

// (roadmap H111(b)) A mesh shader that builds two function-local constant
// arrays of `<4 x float>` (one for `SV_Position`, one for a `Location=0`
// color varying), then reads each one back through a *dynamic* (per-lane)
// index -- exactly the shape `dEQP-VK.mesh_shader.ext.smoke.*.
// fullscreen_gradient`'s own mesh shader uses for its `positions[vertex]`/
// `colors[vertex]` lookups (`vertex` being `gl_LocalInvocationIndex`),
// once H111(a)'s fix let such an array-initialized local `spirv.Variable`
// reach the CPU lowering pipeline at all. Four lanes (`hlsl.numthreads`
// `4,1,1`, matching `WaveSize=4`) each read a different array element and
// write a different output vertex/color, forming a full-screen quad out
// of two triangles.
constexpr char MeshLocalArrayDynamicIndexShaderIR[] = R"(
  define void @ms_main() #0 {
    call void @feme.stage.set_mesh_outputs(i32 4, i32 2)
    %positions = alloca [4 x <4 x float>], align 16
    store [4 x <4 x float>] [
      <4 x float> <float -1.0, float -1.0, float 0.0, float 1.0>,
      <4 x float> <float -1.0, float  1.0, float 0.0, float 1.0>,
      <4 x float> <float  1.0, float -1.0, float 0.0, float 1.0>,
      <4 x float> <float  1.0, float  1.0, float 0.0, float 1.0>
    ], ptr %positions, align 16
    %colors = alloca [4 x <4 x float>], align 16
    store [4 x <4 x float>] [
      <4 x float> <float 0.0, float 0.0, float 0.0, float 1.0>,
      <4 x float> <float 0.0, float 0.0, float 1.0, float 1.0>,
      <4 x float> <float 0.0, float 1.0, float 0.0, float 1.0>,
      <4 x float> <float 0.0, float 1.0, float 1.0, float 1.0>
    ], ptr %colors, align 16
    %indices = alloca [2 x <3 x i32>], align 16
    store [2 x <3 x i32>] [
      <3 x i32> <i32 0, i32 1, i32 2>,
      <3 x i32> <i32 1, i32 3, i32 2>
    ], ptr %indices, align 16

    %vertex = call i32 @llvm.spv.flattened.thread.id.in.group()
    %vcmp = icmp ult i32 %vertex, 4
    br i1 %vcmp, label %writevertex, label %afterVertex

  writevertex:
    %posptr = getelementptr [4 x <4 x float>], ptr %positions, i32 0, i32 %vertex
    %pos = load <4 x float>, ptr %posptr, align 16
    %p0 = extractelement <4 x float> %pos, i32 0
    %p1 = extractelement <4 x float> %pos, i32 1
    %p2 = extractelement <4 x float> %pos, i32 2
    %p3 = extractelement <4 x float> %pos, i32 3
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %p0, i32 %vertex)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float %p1, i32 %vertex)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float %p2, i32 %vertex)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float %p3, i32 %vertex)
    %colptr = getelementptr [4 x <4 x float>], ptr %colors, i32 0, i32 %vertex
    %col = load <4 x float>, ptr %colptr, align 16
    %c0 = extractelement <4 x float> %col, i32 0
    %c1 = extractelement <4 x float> %col, i32 1
    %c2 = extractelement <4 x float> %col, i32 2
    %c3 = extractelement <4 x float> %col, i32 3
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %c0, i32 %vertex)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %c1, i32 %vertex)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %c2, i32 %vertex)
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float %c3, i32 %vertex)
    br label %afterVertex

  afterVertex:
    %primitive = call i32 @llvm.spv.flattened.thread.id.in.group()
    %pcmp = icmp ult i32 %primitive, 2
    br i1 %pcmp, label %writeprim, label %end

  writeprim:
    %idxptr = getelementptr [2 x <3 x i32>], ptr %indices, i32 0, i32 %primitive
    %idx = load <3 x i32>, ptr %idxptr, align 16
    %i0 = extractelement <3 x i32> %idx, i32 0
    %i1 = extractelement <3 x i32> %idx, i32 1
    %i2 = extractelement <3 x i32> %idx, i32 2
    call void @feme.stage.output.store.i32(i32 2, i32 0, i32 0, i32 %i0, i32 %primitive)
    call void @feme.stage.output.store.i32(i32 2, i32 0, i32 1, i32 %i1, i32 %primitive)
    call void @feme.stage.output.store.i32(i32 2, i32 0, i32 2, i32 %i2, i32 %primitive)
    br label %end

  end:
    ret void
  }
  declare i32 @llvm.spv.flattened.thread.id.in.group()
  declare void @feme.stage.set_mesh_outputs(i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
  attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="4,1,1" }
)";

// Passes a `Location=0` `vec4` varying straight through to `SV_Target0`,
// reused from this file's own `FragmentShaderIR` shape.
TEST(ExecutorTest, MeshLocalArrayDynamicIndexProducesFullScreenGradient) {
  Context Ctx;
  EntrySignature MeshSig;
  SignatureElement ColorElt =
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0);
  SignatureElement IdxElt = makeElement(
      2, SignatureDirection::Output, 3, /*Location=*/std::nullopt);
  IdxElt.ComponentType = SignatureComponentType::UInt;
  IdxElt.Frequency = SignatureFrequency::PerPrimitive;
  IdxElt.SystemValue = SignatureSystemValue::PrimitiveIndices;
  MeshSig.Elements = {makeElement(0, SignatureDirection::Output, 4,
                                  /*Location=*/std::nullopt,
                                  SignatureSystemValue::Position),
                      ColorElt, IdxElt};
  Expected<std::shared_ptr<CompiledStage>> MS = compileStage(
      Ctx, MeshLocalArrayDynamicIndexShaderIR, "ms_main", MeshSig,
      ShaderStage::Mesh);
  ASSERT_THAT_EXPECTED(MS, Succeeded());

  EntrySignature FSSig;
  FSSig.Elements = {
      makeElement(0, SignatureDirection::Input, 4, /*Location=*/0),
      makeElement(1, SignatureDirection::Output, 4, /*Location=*/0)};
  Expected<std::shared_ptr<CompiledStage>> FS =
      compileStage(Ctx, FragmentShaderIR, "fs_main", FSSig,
                  ShaderStage::Fragment);
  ASSERT_THAT_EXPECTED(FS, Succeeded());

  uint32_t Size = 4;
  std::vector<AttachmentFormat> Attachments = {
      {cpu::ResourceFormat::R32G32B32A32_FLOAT, Size, Size}};
  GraphicsPipeline Pipeline(
      /*VertexStage=*/nullptr, std::move(*FS), PrimitiveTopology::TriangleList,
      RasterState{CullMode::None, FrontFace::CounterClockwise}, DepthState{},
      BlendMode::Replace, /*SampleCount=*/1, std::move(Attachments));
  MeshState Mesh;
  Mesh.OutputTopology = MeshOutputTopology::Triangles;
  Mesh.MaxOutputVertices = 4;
  Mesh.MaxOutputPrimitives = 2;
  AmplificationDispatchLimits Permissive{{65535, 65535, 65535}, 4194304};
  Pipeline.setMeshStage(/*TaskStage=*/nullptr, std::move(*MS), Mesh, Permissive,
                        Permissive);

  std::vector<uint8_t> Storage((size_t)Size * Size * 4 * sizeof(float), 0);
  AttachmentView Color{Storage, cpu::ResourceFormat::R32G32B32A32_FLOAT, Size,
                       Size};
  std::array<AttachmentView, 1> Attachs{Color};
  PreparedDraw Draw;
  Draw.Attachments = Attachs;
  Draw.Viewports[0] =
      ViewportState{0.0f, 0.0f, (float)Size, (float)Size, 0.0f, 1.0f};
  Draw.Scissors[0] = ScissorRect{0, 0, Size, Size};
  MeshDrawCommand MDC;
  MDC.GroupCount = {1, 1, 1};
  std::array<MeshDrawCommand, 1> MeshDraws = {MDC};
  Draw.MeshDraws = MeshDraws;

  ASSERT_THAT_ERROR(executeDraws(Pipeline, Draw, /*WorkerCount=*/1),
                    Succeeded());

  // Every covered pixel's green channel should increase left-to-right and
  // its blue channel top-to-bottom -- a gradient, not a uniformly black
  // image (roadmap H111(b)'s own symptom: every pixel reading back
  // `(0, 0, 0, 1)` instead).
  std::array<float, 4> TopLeft, TopRight, BottomLeft;
  std::memcpy(TopLeft.data(), Storage.data(), sizeof(TopLeft));
  std::memcpy(TopRight.data(),
              Storage.data() + (size_t)(Size - 1) * 4 * sizeof(float),
              sizeof(TopRight));
  std::memcpy(BottomLeft.data(),
              Storage.data() + (size_t)(Size - 1) * Size * 4 * sizeof(float),
              sizeof(BottomLeft));

  EXPECT_LT(TopLeft[1], TopRight[1]);
  EXPECT_LT(TopLeft[2], BottomLeft[2]);
  EXPECT_NE(TopRight[1], 0.0f);
  EXPECT_NE(BottomLeft[2], 0.0f);
}

} // namespace
