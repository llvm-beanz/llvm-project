//===- PatchPipelineTest.cpp - Tests for feme::graphics::runPatchPipeline ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Covers `feme::graphics::runPatchPipeline` chaining a compiled hull
// control-point phase, a compiled patch-constant phase, the fixed-function
// tessellator, and a compiled domain stage together for one patch -- the
// tessellation half of roadmap R34's remaining "actually chaining the ...
// compiled stage invocations together per patch" deferred item.
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/PatchPipeline.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Core/Signature.h"
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

// Doubles the output control point's own input control point -- the common
// per-control-point-independent shape (see HullWrapper.cpp).
constexpr char HullShaderIR[] = R"(
  define void @hs_main() #0 {
    %id = call i32 @feme.stage.input.load.i32(i32 1, i32 0, i32 0, i32 0)
    %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 %id)
    %doubled = fmul float %in, 2.0
    call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %doubled, i32 0)
    ret void
  }
  declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="hull" }
)";

// Reads the completed `OutputPatch`'s two control points (element 2) and
// writes an isoline `TessFactorEdge` pair (element 3: row 0 is a constant
// line density, row 1 is the two control points' sum, proving the factor
// really is computed from the hull stage's own output) plus a plain
// per-patch constant (element 4) the domain stage below uses as a scale.
constexpr char PatchConstantShaderIR[] = R"(
  define void @pc_main() #0 {
    %a = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
    %b = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 1)
    %sum = fadd float %a, %b
    call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float 1.0, i32 0)
    call void @feme.stage.output.store.f32(i32 3, i32 1, i32 0, float %sum, i32 0)
    call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float 3.0, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="hull" }
)";

// Evaluates the completed patch at this invocation's own domain location:
// linearly blends control points 0 and 1 (element 2) by `u` (element 5) and
// scales the result by the patch constant (element 4).
constexpr char DomainShaderIR[] = R"(
  define void @ds_main() #0 {
    %u = call float @feme.stage.input.load.f32(i32 5, i32 0, i32 0, i32 0)
    %p0 = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
    %p1 = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 1)
    %k = call float @feme.stage.input.load.f32(i32 4, i32 0, i32 0, i32 0)
    %d = fsub float %p1, %p0
    %s = fmul float %d, %u
    %b = fadd float %p0, %s
    %r = fmul float %b, %k
    call void @feme.stage.output.store.f32(i32 6, i32 0, i32 0, float %r, i32 0)
    ret void
  }
  declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
  declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
  attributes #0 = { "feme.shader.stage"="domain" }
)";

SignatureElement makeFloatInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
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

SignatureElement makeFloatOutput(uint32_t ElementID) {
  SignatureElement Elt = makeFloatInput(ElementID);
  Elt.Direction = SignatureDirection::Output;
  return Elt;
}

SignatureElement makeTessFactorEdgePatchOutput(uint32_t ElementID,
                                               uint32_t RowCount) {
  SignatureElement Elt = makeFloatInput(ElementID);
  Elt.Direction = SignatureDirection::PatchOutput;
  Elt.SystemValue = SignatureSystemValue::TessFactorEdge;
  Elt.RowCount = RowCount;
  Elt.Frequency = SignatureFrequency::PerPatch;
  return Elt;
}

SignatureElement makeFloatPatchOutput(uint32_t ElementID) {
  SignatureElement Elt = makeFloatInput(ElementID);
  Elt.Direction = SignatureDirection::PatchOutput;
  Elt.Frequency = SignatureFrequency::PerPatch;
  return Elt;
}

SignatureElement makeFloatPatchInput(uint32_t ElementID) {
  SignatureElement Elt = makeFloatInput(ElementID);
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

TEST(PatchPipelineTest, ChainsHullPatchConstantTessellatorAndDomain) {
  Context Ctx;

  EntrySignature HullSig;
  HullSig.Elements = {makeFloatInput(0), makeOutputControlPointIDInput(1),
                      makeFloatOutput(2)};
  Expected<std::unique_ptr<CompiledStage>> Hull = compileGraphicsStage(
      Ctx, HullShaderIR, "hs_main", HullSig, ShaderStage::Hull);
  ASSERT_THAT_EXPECTED(Hull, Succeeded());

  EntrySignature PatchConstantSig;
  PatchConstantSig.Elements = {makeFloatInput(2),
                               makeTessFactorEdgePatchOutput(3, 2),
                               makeFloatPatchOutput(4)};
  Expected<std::unique_ptr<CompiledStage>> PatchConstant =
      compileGraphicsStage(Ctx, PatchConstantShaderIR, "pc_main",
                           PatchConstantSig, ShaderStage::Hull);
  ASSERT_THAT_EXPECTED(PatchConstant, Succeeded());

  EntrySignature DomainSig;
  DomainSig.Elements = {makeFloatInput(2), makeDomainLocationInput(5),
                        makeFloatPatchInput(4), makeFloatOutput(6)};
  Expected<std::unique_ptr<CompiledStage>> Domain = compileGraphicsStage(
      Ctx, DomainShaderIR, "ds_main", DomainSig, ShaderStage::Domain);
  ASSERT_THAT_EXPECTED(Domain, Succeeded());

  // Hull: element 0 (own input control point), element 1
  // (OutputControlPointID, no storage), element 2 (unused here).
  FemeStageElement HullInputElements[2] = {};
  HullInputElements[0].ElementID = 0;
  HullInputElements[0].FirstComponent = 0;
  HullInputElements[0].ComponentCount = 1;
  HullInputElements[0].RowCount = 1;
  HullInputElements[0].InvocationStride = 4;
  HullInputElements[1].ElementID = 1;
  FemeStageLayout HullInputLayout{};
  HullInputLayout.Elements = HullInputElements;
  HullInputLayout.ElementCount = 2;

  // The completed `OutputPatch`'s control points (element 2), shared by the
  // hull phase's own output, the patch-constant phase's own input, and the
  // domain stage's own input (see PatchPipeline.h's file comment).
  FemeStageElement OutputPatchElements[3] = {};
  OutputPatchElements[2].ElementID = 2;
  OutputPatchElements[2].FirstComponent = 0;
  OutputPatchElements[2].ComponentCount = 1;
  OutputPatchElements[2].RowCount = 1;
  OutputPatchElements[2].InvocationStride = 4;
  FemeStageLayout OutputPatchLayout{};
  OutputPatchLayout.Elements = OutputPatchElements;
  OutputPatchLayout.ElementCount = 3;

  // The per-patch tessellation factors (element 3, two rows) and plain
  // patch constant (element 4), shared by the patch-constant phase's own
  // output and the domain stage's own patch-constant input.
  FemeStageElement PatchConstantElements[5] = {};
  PatchConstantElements[3].ElementID = 3;
  PatchConstantElements[3].FirstComponent = 0;
  PatchConstantElements[3].ComponentCount = 1;
  PatchConstantElements[3].RowCount = 2;
  PatchConstantElements[3].RowStride = 4;
  PatchConstantElements[3].DataOffset = 0;
  PatchConstantElements[4].ElementID = 4;
  PatchConstantElements[4].FirstComponent = 0;
  PatchConstantElements[4].ComponentCount = 1;
  PatchConstantElements[4].RowCount = 1;
  PatchConstantElements[4].DataOffset = 8;
  FemeStageLayout PatchConstantsLayout{};
  PatchConstantsLayout.Elements = PatchConstantElements;
  PatchConstantsLayout.ElementCount = 5;

  // The domain stage's own output (element 6): one scalar per domain point.
  FemeStageElement DomainOutputElements[7] = {};
  DomainOutputElements[6].ElementID = 6;
  DomainOutputElements[6].FirstComponent = 0;
  DomainOutputElements[6].ComponentCount = 1;
  DomainOutputElements[6].RowCount = 1;
  DomainOutputElements[6].InvocationStride = 4;
  FemeStageLayout DomainOutputLayout{};
  DomainOutputLayout.Elements = DomainOutputElements;
  DomainOutputLayout.ElementCount = 7;

  PatchPipelineStages Stages{**Hull, **PatchConstant, **Domain};
  PatchPipelineLayouts Layouts{HullInputLayout, OutputPatchLayout,
                               /*PatchConstantInputPatch=*/nullptr,
                               PatchConstantsLayout, DomainOutputLayout};

  // Input control points 1.0 and 3.0: the hull phase doubles them to 2.0
  // and 6.0, so the patch-constant phase's `sum` factor (isoline detail,
  // row 1) is 8.0, and the domain stage should blend 2.0..6.0 scaled by 3.0
  // (the plain patch constant).
  std::vector<float> InputControlPoints = {1.0f, 3.0f};

  Expected<PatchPipelineResult> Result = runPatchPipeline(
      Stages, Layouts, TessellatorDomain::Isoline, TessPartitioning::Integer,
      TessOutputPrimitive::Line, InputControlPoints,
      /*InputControlPointCount=*/2, /*OutputControlPointCount=*/2,
      /*OutputControlPointScalarCount=*/1, /*PatchConstantScalarCount=*/3,
      /*DomainOutputScalarsPerVertex=*/1);
  ASSERT_THAT_EXPECTED(Result, Succeeded());

  EXPECT_EQ(Result->OutputControlPoints, (std::vector<float>{2.0f, 6.0f}));
  // Row 0 (density) is the constant 1.0; row 1 (detail) is 2.0 + 6.0.
  EXPECT_EQ(Result->PatchConstants[0], 1.0f);
  EXPECT_EQ(Result->PatchConstants[1], 8.0f);
  EXPECT_EQ(Result->PatchConstants[2], 3.0f);

  ASSERT_FALSE(Result->Tessellated.Points.empty());
  ASSERT_EQ(Result->DomainOutputs.size(), Result->Tessellated.Points.size());
  for (size_t I = 0; I < Result->Tessellated.Points.size(); ++I) {
    float U = Result->Tessellated.Points[I].U;
    float Expected = (2.0f + (6.0f - 2.0f) * U) * 3.0f;
    EXPECT_NEAR(Result->DomainOutputs[I], Expected, 1e-4f);
  }
}

TEST(PatchPipelineTest, RejectsOutOfRangeControlPointCounts) {
  Context Ctx;
  EntrySignature HullSig;
  HullSig.Elements = {makeFloatInput(0), makeOutputControlPointIDInput(1),
                      makeFloatOutput(2)};
  Expected<std::unique_ptr<CompiledStage>> Hull = compileGraphicsStage(
      Ctx, HullShaderIR, "hs_main", HullSig, ShaderStage::Hull);
  ASSERT_THAT_EXPECTED(Hull, Succeeded());

  EntrySignature PatchConstantSig;
  PatchConstantSig.Elements = {makeFloatInput(2),
                               makeTessFactorEdgePatchOutput(3, 2),
                               makeFloatPatchOutput(4)};
  Expected<std::unique_ptr<CompiledStage>> PatchConstant =
      compileGraphicsStage(Ctx, PatchConstantShaderIR, "pc_main",
                           PatchConstantSig, ShaderStage::Hull);
  ASSERT_THAT_EXPECTED(PatchConstant, Succeeded());

  EntrySignature DomainSig;
  DomainSig.Elements = {makeFloatInput(2), makeDomainLocationInput(5),
                        makeFloatPatchInput(4), makeFloatOutput(6)};
  Expected<std::unique_ptr<CompiledStage>> Domain = compileGraphicsStage(
      Ctx, DomainShaderIR, "ds_main", DomainSig, ShaderStage::Domain);
  ASSERT_THAT_EXPECTED(Domain, Succeeded());

  FemeStageLayout Empty{};
  PatchPipelineStages Stages{**Hull, **PatchConstant, **Domain};
  PatchPipelineLayouts Layouts{
      Empty, Empty, /*PatchConstantInputPatch=*/nullptr, Empty, Empty};
  std::vector<float> InputControlPoints;

  Expected<PatchPipelineResult> Result = runPatchPipeline(
      Stages, Layouts, TessellatorDomain::Isoline, TessPartitioning::Integer,
      TessOutputPrimitive::Line, InputControlPoints,
      /*InputControlPointCount=*/0, /*OutputControlPointCount=*/2,
      /*OutputControlPointScalarCount=*/1, /*PatchConstantScalarCount=*/3,
      /*DomainOutputScalarsPerVertex=*/1);
  ASSERT_THAT_ERROR(Result.takeError(), Failed());
}

} // namespace
