//===- PatchPipelineTest.cpp - Tests for feme::graphics::runPatchPipeline ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Covers `feme::graphics::linkPatchPipeline`/`runPatchPipeline` chaining a
// compiled hull control-point phase, a compiled patch-constant phase, the
// fixed-function tessellator, and a compiled domain stage together for one
// patch, linking the four stages' independently numbered signatures by
// `Location`/system value (roadmap R34's "actually chaining the ...
// compiled stage invocations together per patch" item, and roadmap H4's
// replacement of its hand-built-shared-layout stand-in with a real
// cross-stage attribute linker).
//
// Every stage below deliberately numbers its own elements differently from
// its neighbors -- the hull's own output is element 1, the same attribute
// is element 0 on the patch-constant phase and element 3 on the domain
// stage -- so a regression that linked by `ElementID` instead of by
// `Location` fails these tests rather than passing by coincidence.
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/PatchPipeline.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Core/Signature.h"
#include "feme/Graphics/StageStorage.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Transforms/DXIL/SignatureImport.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::cpu;
using namespace feme::graphics;
using namespace llvm;

namespace {

// A vertex stage writing one float varying at location 0 (element 0),
// straight from its own vertex-buffer input (element 1). Only its output
// signature matters to `linkPatchPipeline`, but running it for real is what
// makes the vertex -> hull link a real link rather than a synthetic one.
constexpr char VertexShaderIR[] = R"(
  define void @vs_main() #0 {
    %v = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
    call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %v, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="vertex" }
)";

// Doubles the output control point's own input control point -- the common
// per-control-point-independent shape (see HullWrapper.cpp).
constexpr char HullShaderIR[] = R"(
  define void @hs_main() #0 {
    %id = call i32 @feme.stage.input.load.i32(i32 2, i32 0, i32 0, i32 0)
    %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 %id)
    %doubled = fmul float %in, 2.0
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %doubled, i32 0)
    ret void
  }
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="hull" }
)";

// Reads the completed `OutputPatch`'s two control points (element 0 here,
// the hull stage's own element 1) and writes an isoline `TessFactorEdge`
// pair (element 1: row 0 is a constant line density, row 1 is the two
// control points' sum, proving the factor really is computed from the hull
// stage's own output) plus a plain per-patch constant (element 2) the
// domain stage below uses as a scale.
constexpr char PatchConstantShaderIR[] = R"(
  define void @pc_main() #0 {
    %a = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %b = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 1)
    %sum = fadd float %a, %b
    call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 1, i32 1, i32 0, float %sum, i32 0)
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float 3.0, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="hull" }
)";

// Evaluates the completed patch at this invocation's own domain location:
// linearly blends control points 0 and 1 (element 3) by `u` (element 0) and
// scales the result by the patch constant (element 1).
constexpr char DomainShaderIR[] = R"(
  define void @ds_main() #0 {
    %u = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
    %p0 = call float @feme.stage.input.load.f32(i32 3, i32 0, i32 0, i32 0)
    %p1 = call float @feme.stage.input.load.f32(i32 3, i32 0, i32 0, i32 1)
    %k = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
    %d = fsub float %p1, %p0
    %s = fmul float %d, %u
    %b = fadd float %p0, %s
    %r = fmul float %b, %k
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %r, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="domain" }
)";

SignatureElement makeFloatInput(uint32_t ElementID,
                                std::optional<uint32_t> Location = 0) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.Location = Location;
  Elt.ComponentType = SignatureComponentType::Float;
  Elt.BitWidth = 32;
  return Elt;
}

SignatureElement makeOutputControlPointIDInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::OutputControlPointID;
  Elt.ComponentType = SignatureComponentType::UInt;
  Elt.BitWidth = 32;
  return Elt;
}

SignatureElement makeFloatOutput(uint32_t ElementID, uint32_t Location = 0) {
  SignatureElement Elt = makeFloatInput(ElementID, Location);
  Elt.Direction = SignatureDirection::Output;
  return Elt;
}

SignatureElement makeTessFactorEdgePatchOutput(uint32_t ElementID,
                                               uint32_t RowCount) {
  SignatureElement Elt = makeFloatInput(ElementID, std::nullopt);
  Elt.Direction = SignatureDirection::PatchOutput;
  Elt.SystemValue = SignatureSystemValue::TessFactorEdge;
  Elt.RowCount = RowCount;
  Elt.Frequency = SignatureFrequency::PerPatch;
  return Elt;
}

SignatureElement makeFloatPatchOutput(uint32_t ElementID, uint32_t Location) {
  SignatureElement Elt = makeFloatInput(ElementID, Location);
  Elt.Direction = SignatureDirection::PatchOutput;
  Elt.Frequency = SignatureFrequency::PerPatch;
  return Elt;
}

SignatureElement makeFloatPatchInput(uint32_t ElementID, uint32_t Location) {
  SignatureElement Elt = makeFloatInput(ElementID, Location);
  Elt.Direction = SignatureDirection::PatchInput;
  Elt.Frequency = SignatureFrequency::PerPatch;
  return Elt;
}

SignatureElement makeDomainLocationInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::DomainLocation;
  Elt.ComponentType = SignatureComponentType::Float;
  Elt.ComponentCount = 3;
  Elt.BitWidth = 32;
  return Elt;
}

Expected<std::unique_ptr<CompiledStage>>
compileGraphicsStage(Context &Ctx, StringRef IR, StringRef EntryName,
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
  return CompiledStage::create(Ctx, std::move(Mod), Opts);
}

/// The vertex stage's output signature: one float varying at location 0.
EntrySignature makeVertexOutputSignature() {
  EntrySignature Sig;
  Sig.Elements = {makeFloatOutput(0, /*Location=*/0), makeFloatInput(1)};
  return Sig;
}

EntrySignature makeHullSignature() {
  EntrySignature Sig;
  Sig.Elements = {makeFloatInput(0, /*Location=*/0),
                  makeFloatOutput(1, /*Location=*/0),
                  makeOutputControlPointIDInput(2)};
  return Sig;
}

EntrySignature makePatchConstantSignature() {
  EntrySignature Sig;
  Sig.Elements = {makeFloatInput(0, /*Location=*/0),
                  makeTessFactorEdgePatchOutput(1, /*RowCount=*/2),
                  makeFloatPatchOutput(2, /*Location=*/7)};
  return Sig;
}

EntrySignature makeDomainSignature() {
  EntrySignature Sig;
  Sig.Elements = {
      makeDomainLocationInput(0), makeFloatPatchInput(1, /*Location=*/7),
      makeFloatOutput(2, /*Location=*/0), makeFloatInput(3, /*Location=*/0)};
  return Sig;
}

/// Runs \p VertexStage over \p Values, one invocation per value, returning
/// its output block -- the real producer `linkPatchPipeline` links the hull
/// stage's inputs against.
Expected<StageStorage> runVertexStage(const CompiledStage &VertexStage,
                                      const EntrySignature &Sig,
                                      ArrayRef<float> Values) {
  uint32_t Count = static_cast<uint32_t>(Values.size());
  Expected<StageStorage> In =
      buildStageStorage(Sig, SignatureDirection::Input, Count);
  if (!In)
    return In.takeError();
  Expected<StageStorage> Out =
      buildStageStorage(Sig, SignatureDirection::Output, Count);
  if (!Out)
    return Out.takeError();
  for (uint32_t I = 0; I != Count; ++I)
    In->writeFloat(/*ElementID=*/1, /*Component=*/0, I, Values[I]);

  std::vector<FemeVertexInvocation> Invocations(Count);
  for (uint32_t I = 0; I != Count; ++I)
    Invocations[I].VertexID = I;

  FemeStageLayout InLayout = In->layout();
  FemeStageLayout OutLayout = Out->layout();
  VertexResources Res;
  Res.InputLayout = &InLayout;
  Res.Inputs = In->Data.data();
  Res.OutputLayout = &OutLayout;
  Res.Outputs = Out->Data.data();
  Res.Invocations = Invocations;
  PreparedVertexBatch Prepared =
      PreparedVertexBatch::create(VertexStage.getResourceInfo(), Res);
  if (Error E = VertexStage.invokeVertices(Prepared))
    return std::move(E);
  return Out;
}

TEST(PatchPipelineTest, ChainsHullPatchConstantTessellatorAndDomain) {
  Context Ctx;

  EntrySignature VertexSig = makeVertexOutputSignature();
  Expected<std::unique_ptr<CompiledStage>> Vertex = compileGraphicsStage(
      Ctx, VertexShaderIR, "vs_main", VertexSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(Vertex, Succeeded());

  Expected<std::unique_ptr<CompiledStage>> Hull = compileGraphicsStage(
      Ctx, HullShaderIR, "hs_main", makeHullSignature(), ShaderStage::Hull);
  ASSERT_THAT_EXPECTED(Hull, Succeeded());
  Expected<std::unique_ptr<CompiledStage>> PatchConstant =
      compileGraphicsStage(Ctx, PatchConstantShaderIR, "pc_main",
                           makePatchConstantSignature(), ShaderStage::Hull);
  ASSERT_THAT_EXPECTED(PatchConstant, Succeeded());
  Expected<std::unique_ptr<CompiledStage>> Domain =
      compileGraphicsStage(Ctx, DomainShaderIR, "ds_main",
                           makeDomainSignature(), ShaderStage::Domain);
  ASSERT_THAT_EXPECTED(Domain, Succeeded());

  PatchPipelineStages Stages{**Hull, **PatchConstant, **Domain};
  Expected<PatchPipelineLinkage> Link = linkPatchPipeline(VertexSig, Stages);
  ASSERT_THAT_EXPECTED(Link, Succeeded());

  // Input control points 1.0 and 3.0: the hull phase doubles them to 2.0
  // and 6.0, so the patch-constant phase's `sum` factor (isoline detail,
  // row 1) is 8.0, and the domain stage should blend 2.0..6.0 scaled by 3.0
  // (the plain patch constant).
  Expected<StageStorage> VertexOutputs =
      runVertexStage(**Vertex, VertexSig, {1.0f, 3.0f});
  ASSERT_THAT_EXPECTED(VertexOutputs, Succeeded());

  TessellationState Tess;
  Tess.Domain = TessellatorDomain::Isoline;
  Tess.Partitioning = TessPartitioning::Integer;
  Tess.OutputPrimitive = TessOutputPrimitive::Line;
  Tess.InputControlPointCount = 2;
  Tess.OutputControlPointCount = 2;

  std::vector<uint32_t> ControlPointInvocations = {0, 1};
  Expected<PatchPipelineResult> Result = runPatchPipeline(
      Stages, *Link, Tess, *VertexOutputs, ControlPointInvocations);
  ASSERT_THAT_EXPECTED(Result, Succeeded());

  EXPECT_FLOAT_EQ(Result->OutputPatch.readFloat(1, 0, 0), 2.0f);
  EXPECT_FLOAT_EQ(Result->OutputPatch.readFloat(1, 0, 1), 6.0f);
  // Row 0 (density) is the constant 1.0; row 1 (detail) is 2.0 + 6.0.
  EXPECT_FLOAT_EQ(Result->PatchConstants.readFloat(1, 0, 0, /*Row=*/0), 1.0f);
  EXPECT_FLOAT_EQ(Result->PatchConstants.readFloat(1, 0, 0, /*Row=*/1), 8.0f);
  EXPECT_FLOAT_EQ(Result->PatchConstants.readFloat(2, 0, 0), 3.0f);

  ASSERT_FALSE(Result->Tessellated.Points.empty());
  for (size_t I = 0; I < Result->Tessellated.Points.size(); ++I) {
    float U = Result->Tessellated.Points[I].U;
    float Want = (2.0f + (6.0f - 2.0f) * U) * 3.0f;
    EXPECT_NEAR(Result->DomainOutputs.readFloat(2, 0, I), Want, 1e-4f);
  }
}

TEST(PatchPipelineTest, RejectsAnUnlinkableStageInterface) {
  Context Ctx;

  // The vertex stage's only output moves to location 5, so the hull
  // stage's location-0 input has no producer at all.
  EntrySignature VertexSig;
  VertexSig.Elements = {makeFloatOutput(0, /*Location=*/5), makeFloatInput(1)};
  Expected<std::unique_ptr<CompiledStage>> Vertex = compileGraphicsStage(
      Ctx, VertexShaderIR, "vs_main", VertexSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(Vertex, Succeeded());

  Expected<std::unique_ptr<CompiledStage>> Hull = compileGraphicsStage(
      Ctx, HullShaderIR, "hs_main", makeHullSignature(), ShaderStage::Hull);
  ASSERT_THAT_EXPECTED(Hull, Succeeded());
  Expected<std::unique_ptr<CompiledStage>> PatchConstant =
      compileGraphicsStage(Ctx, PatchConstantShaderIR, "pc_main",
                           makePatchConstantSignature(), ShaderStage::Hull);
  ASSERT_THAT_EXPECTED(PatchConstant, Succeeded());
  Expected<std::unique_ptr<CompiledStage>> Domain =
      compileGraphicsStage(Ctx, DomainShaderIR, "ds_main",
                           makeDomainSignature(), ShaderStage::Domain);
  ASSERT_THAT_EXPECTED(Domain, Succeeded());

  PatchPipelineStages Stages{**Hull, **PatchConstant, **Domain};
  Expected<PatchPipelineLinkage> Link = linkPatchPipeline(VertexSig, Stages);
  ASSERT_THAT_ERROR(Link.takeError(), Failed());
}

TEST(PatchPipelineTest, RejectsOutOfRangeControlPointCounts) {
  Context Ctx;

  EntrySignature VertexSig = makeVertexOutputSignature();
  Expected<std::unique_ptr<CompiledStage>> Vertex = compileGraphicsStage(
      Ctx, VertexShaderIR, "vs_main", VertexSig, ShaderStage::Vertex);
  ASSERT_THAT_EXPECTED(Vertex, Succeeded());
  Expected<std::unique_ptr<CompiledStage>> Hull = compileGraphicsStage(
      Ctx, HullShaderIR, "hs_main", makeHullSignature(), ShaderStage::Hull);
  ASSERT_THAT_EXPECTED(Hull, Succeeded());
  Expected<std::unique_ptr<CompiledStage>> PatchConstant =
      compileGraphicsStage(Ctx, PatchConstantShaderIR, "pc_main",
                           makePatchConstantSignature(), ShaderStage::Hull);
  ASSERT_THAT_EXPECTED(PatchConstant, Succeeded());
  Expected<std::unique_ptr<CompiledStage>> Domain =
      compileGraphicsStage(Ctx, DomainShaderIR, "ds_main",
                           makeDomainSignature(), ShaderStage::Domain);
  ASSERT_THAT_EXPECTED(Domain, Succeeded());

  PatchPipelineStages Stages{**Hull, **PatchConstant, **Domain};
  Expected<PatchPipelineLinkage> Link = linkPatchPipeline(VertexSig, Stages);
  ASSERT_THAT_EXPECTED(Link, Succeeded());
  Expected<StageStorage> VertexOutputs =
      runVertexStage(**Vertex, VertexSig, {1.0f, 3.0f});
  ASSERT_THAT_EXPECTED(VertexOutputs, Succeeded());

  TessellationState Tess;
  Tess.Domain = TessellatorDomain::Isoline;
  Tess.OutputPrimitive = TessOutputPrimitive::Line;
  Tess.InputControlPointCount = 0;
  Tess.OutputControlPointCount = 2;

  Expected<PatchPipelineResult> Result =
      runPatchPipeline(Stages, *Link, Tess, *VertexOutputs, {});
  ASSERT_THAT_ERROR(Result.takeError(), Failed());
}

} // namespace
