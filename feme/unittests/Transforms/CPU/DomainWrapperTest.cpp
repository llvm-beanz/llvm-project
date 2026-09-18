//===- DomainWrapperTest.cpp - Tests for DomainWrapperPass ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/DomainWrapper.h"

#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/Linearize.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/WaveLowering.h"
#include "feme/Transforms/DXIL/SignatureImport.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("DomainWrapperTest", errs());
  return M;
}

SignatureElement makeFloatElement(uint32_t ElementID,
                                  SignatureDirection Direction) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = Direction;
  Elt.ComponentType = SignatureComponentType::Float;
  if (Direction == SignatureDirection::PatchInput ||
      Direction == SignatureDirection::PatchOutput)
    Elt.Frequency = SignatureFrequency::PerPatch;
  return Elt;
}

SignatureElement makeDomainLocationInput(uint32_t ElementID) {
  SignatureElement Elt = makeFloatElement(ElementID, SignatureDirection::Input);
  Elt.SystemValue = SignatureSystemValue::DomainLocation;
  Elt.ComponentCount = 3;
  return Elt;
}

/// (Roadmap H4a) `SV_DomainLocation`'s SPIR-V-native sibling system value:
/// `gl_PatchVerticesIn`, i.e. how many input control points the completed
/// patch this domain invocation evaluates was built from.
SignatureElement makePatchVerticesInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::PatchVertices;
  Elt.ComponentType = SignatureComponentType::UInt;
  Elt.Frequency = SignatureFrequency::PerPatch;
  return Elt;
}

/// (Roadmap L81) This patch's own `SV_PrimitiveID`: a genuine,
/// pipeline-supplied per-patch scalar, classified `SignatureDirection::
/// Input`/`SignatureFrequency::PerPatch` by `classifySPIRVElement` even
/// though a real DXC/SPIR-V compile decorates it `Patch` (uniform per
/// patch) -- the same decoration a genuine patch-constant-forwarded
/// tessellation factor carries.
SignatureElement makePrimitiveIDInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::PrimitiveID;
  Elt.ComponentType = SignatureComponentType::UInt;
  Elt.Frequency = SignatureFrequency::PerPatch;
  return Elt;
}

/// (Roadmap H51/L109) `gl_ViewIndex` read from a domain-stage entry: unlike
/// `PrimitiveID` above, a real SPIR-V compile never decorates this one
/// `Patch` (confirmed via `glslang`+`spirv-dis`), so it already falls
/// through to plain `SignatureDirection::Input`/
/// `SignatureFrequency::PerVertex` classification without needing
/// `classifySPIRVElement` changes -- only `lowerDomainInputLoad` itself
/// needed a new case (`lowerDomainViewIndex`).
SignatureElement makeViewIndexInput(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.SystemValue = SignatureSystemValue::ViewIndex;
  Elt.ComponentType = SignatureComponentType::UInt;
  Elt.Frequency = SignatureFrequency::PerVertex;
  return Elt;
}

TEST(DomainWrapperTest, LowersAllThreeInputSourcesAndBuildsWrapper) {
  LLVMContext Ctx;
  // The canonical evaluation shape: blend two control points of the
  // completed patch (element 0, any control-point index) using this
  // invocation's own domain coordinate (element 1, `SV_DomainLocation`),
  // scaled by a patch constant (element 2, `PatchInput`).
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ds_main() #0 {
      %u = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      %p0 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %p1 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 1)
      %k = call float @feme.stage.input.load.f32(i32 2, i32 0, i32 0, i32 0)
      %d = fsub float %p1, %p0
      %s = fmul float %d, %u
      %b = fadd float %p0, %s
      %r = fmul float %b, %k
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %r, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="domain" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {makeFloatElement(0, SignatureDirection::Input),
                  makeDomainLocationInput(1),
                  makeFloatElement(2, SignatureDirection::PatchInput),
                  makeFloatElement(3, SignatureDirection::Output)};
  dxil::setEntrySignature(*M->getFunction("ds_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  DomainWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ds_main"));
  for (const Instruction &I : instructions(*M->getFunction("ds_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(DomainWrapperTest, DiagnosesDynamicDomainLocationComponent) {
  LLVMContext Ctx;
  // A domain-location component chosen at runtime: the record is a
  // fixed-size ABI struct, so this wrapper requires a constant component
  // (see DomainWrapper.cpp's file comment).
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ds_main(i32 %c) #0 {
      %u = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 %c, i32 0)
      call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %u, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="domain" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {makeDomainLocationInput(1),
                  makeFloatElement(3, SignatureDirection::Output)};
  dxil::setEntrySignature(*M->getFunction("ds_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  DomainWrapperPass().run(*M, MAM);

  // The wrapper is not built for a diagnosed shader.
  EXPECT_FALSE(M->getFunction("feme_cpu_entry_ds_main"));
}

TEST(DomainWrapperTest, DiagnosesGroupSyncBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ds_main() #0 {
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      ret void
    }
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "feme.shader.stage"="domain" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  DomainWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_ds_main"));
}

/// (Roadmap H4a) A domain stage reading `gl_PatchVerticesIn` (`SV_
/// DomainLocation`'s SPIR-V-native sibling `PatchVertices` system value)
/// lowers without diagnosing, and the wrapper is still built.
TEST(DomainWrapperTest, LowersPatchVerticesInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ds_main() #0 {
      %pv = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      %pvf = uitofp i32 %pv to float
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %pvf, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="domain" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {makePatchVerticesInput(0),
                  makeFloatElement(1, SignatureDirection::Output)};
  dxil::setEntrySignature(*M->getFunction("ds_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  DomainWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ds_main"));
  for (const Instruction &I : instructions(*M->getFunction("ds_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// (Roadmap H21k) A domain stage reading its paired hull stage's own
/// per-control-point `Position` output (`gl_in[].gl_Position`/`SV_Position`,
/// forwarded as an ordinary control-point attribute -- not a synthesized
/// value like `DomainLocation`/`PatchVertices`) previously hit "unsupported
/// domain system value": the input-load switch only routed
/// `SignatureSystemValue::None` through the generic, storage-addressed
/// `lowerDomainControlPointLoad`, rejecting every other input system value
/// even though that same generic load addresses any control-point element
/// identically regardless of which (if any) system value it represents.
/// Shares its root cause with roadmap H29e's identical hull-stage gap: a
/// real CTS reduction of
/// `dEQP-VK.transform_feedback.primitives_generated_query.*.tese.*` found
/// this default had wrongly diagnosed exactly this shape.
TEST(DomainWrapperTest, LowersPositionInputSystemValue) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ds_main() #0 {
      %p0 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %p0, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="domain" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  SignatureElement Position = makeFloatElement(0, SignatureDirection::Input);
  Position.SystemValue = SignatureSystemValue::Position;

  EntrySignature Sig;
  Sig.Elements = {Position, makeFloatElement(1, SignatureDirection::Output)};
  dxil::setEntrySignature(*M->getFunction("ds_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  DomainWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ds_main"));
  for (const Instruction &I : instructions(*M->getFunction("ds_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// (Roadmap L81) A domain stage reading its own `SV_PrimitiveID` lowers
/// without diagnosing (no patch-constant producer required) and the
/// wrapper is still built. Before this fix, `classifySPIRVElement`
/// misclassified this element `SignatureDirection::PatchInput` (mistaking
/// its real `Patch` SPIR-V decoration for patch-constant-forwarded data),
/// which `PatchPipeline.cpp`'s `linkStageElements` then rejected at
/// pipeline-link time with "patch-constant output -> domain stage patch
/// input: element N has no matching producer element" since no
/// patch-constant-stage output ever produces this pipeline-supplied value.
TEST(DomainWrapperTest, LowersPrimitiveIDInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ds_main() #0 {
      %pid = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      %pidf = uitofp i32 %pid to float
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %pidf, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="domain" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {makePrimitiveIDInput(0),
                  makeFloatElement(1, SignatureDirection::Output)};
  dxil::setEntrySignature(*M->getFunction("ds_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  DomainWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ds_main"));
  for (const Instruction &I : instructions(*M->getFunction("ds_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// (Roadmap H51/L109) A domain stage reading its own `gl_ViewIndex` lowers
/// via `lowerDomainViewIndex`, mirroring `LowersPrimitiveIDInput` above.
TEST(DomainWrapperTest, LowersViewIndexInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ds_main() #0 {
      %vidx = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      %vidxf = uitofp i32 %vidx to float
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %vidxf, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="domain" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  Sig.Elements = {makeViewIndexInput(0),
                  makeFloatElement(1, SignatureDirection::Output)};
  dxil::setEntrySignature(*M->getFunction("ds_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  DomainWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ds_main"));
  for (const Instruction &I : instructions(*M->getFunction("ds_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

} // namespace
