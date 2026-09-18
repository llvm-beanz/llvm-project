//===- HullWrapperTest.cpp - Tests for HullWrapperPass --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/HullWrapper.h"

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
    Err.print("HullWrapperTest", errs());
  return M;
}

TEST(HullWrapperTest, LowersSelfIndexedStageIOAndBuildsWrapper) {
  LLVMContext Ctx;
  // The common per-control-point-independent shape: reads its own
  // `OutputControlPointID`, uses it only to validate the input load refers
  // to its own control point (see HullWrapper.cpp's file comment), and
  // copies the input attribute through with a per-control-point offset.
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @hs_main() #0 {
      %id = call i32 @feme.stage.input.load.i32(i32 2, i32 0, i32 0, i32 0)
      %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 %id)
      %idf = uitofp i32 %id to float
      %sum = fadd float %in, %idf
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %sum, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  SignatureElement Out = In;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  SignatureElement ID;
  ID.ElementID = 2;
  ID.Direction = SignatureDirection::Input;
  ID.SystemValue = SignatureSystemValue::OutputControlPointID;
  ID.ComponentType = SignatureComponentType::UInt;
  Sig.Elements = {In, Out, ID};
  dxil::setEntrySignature(*M->getFunction("hs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  HullWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_hs_main"));
  for (const Instruction &I : instructions(*M->getFunction("hs_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(HullWrapperTest, LowersRepeatedOutputControlPointIDReads) {
  LLVMContext Ctx;
  // (roadmap H29g) A real SPIR-V hull shader reads `gl_InvocationID` once
  // per use rather than once per function, so an entry point indexing two
  // attributes by it produces two separate `OutputControlPointID` loads.
  // Both are the invocation's own control point index and both attribute
  // loads must therefore be accepted.
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @hs_main() #0 {
      %id0 = call i32 @feme.stage.input.load.i32(i32 2, i32 0, i32 0, i32 0)
      %pos = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 %id0)
      %id1 = call i32 @feme.stage.input.load.i32(i32 2, i32 0, i32 0, i32 0)
      %col = call float @feme.stage.input.load.f32(i32 3, i32 0, i32 0, i32 %id1)
      %sum = fadd float %pos, %col
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %sum, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  SignatureElement Color = In;
  Color.ElementID = 3;
  SignatureElement Out = In;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  SignatureElement ID;
  ID.ElementID = 2;
  ID.Direction = SignatureDirection::Input;
  ID.SystemValue = SignatureSystemValue::OutputControlPointID;
  ID.ComponentType = SignatureComponentType::UInt;
  Sig.Elements = {In, Out, ID, Color};
  dxil::setEntrySignature(*M->getFunction("hs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  HullWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_hs_main"));
  for (const Instruction &I : instructions(*M->getFunction("hs_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// (roadmap L37) Reads control point 1's input unconditionally, by a
/// literal constant rather than this invocation's own
/// `OutputControlPointID` -- always legal for an **input** read (see
/// `lowerHullInputLoad`'s own comment), and exactly the shape a real
/// dynamically-indexed `InputPatch<T, N>` read unrolls into once SPIR-V
/// import/legalization has "materialized" every constant-indexed
/// possibility up front for the shader's own code to `select` among after
/// loading.
TEST(HullWrapperTest, LowersLiteralConstantControlPointInputLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @hs_main() #0 {
      %cp0 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %cp1 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 1)
      %sum = fadd float %cp0, %cp1
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %sum, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  SignatureElement Out = In;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("hs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  HullWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_hs_main"));
  for (const Instruction &I : instructions(*M->getFunction("hs_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// A genuinely dynamic, non-self, non-constant control-point index (here,
/// `gl_PatchVerticesIn`'s own runtime value, not `OutputControlPointID`)
/// still needs a real cross-lane gather this milestone does not build --
/// distinct from the literal-constant case
/// `LowersLiteralConstantControlPointInputLoad` above now supports.
TEST(HullWrapperTest, DiagnosesDynamicNonSelfControlPointInputLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @hs_main() #0 {
      %pv = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      %in = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 %pv)
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %in, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement PatchVertices;
  PatchVertices.ElementID = 0;
  PatchVertices.Direction = SignatureDirection::Input;
  PatchVertices.SystemValue = SignatureSystemValue::PatchVertices;
  PatchVertices.ComponentType = SignatureComponentType::UInt;
  PatchVertices.Frequency = SignatureFrequency::PerPatch;
  SignatureElement In;
  In.ElementID = 1;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  SignatureElement Out = In;
  Out.ElementID = 2;
  Out.Direction = SignatureDirection::Output;
  Sig.Elements = {PatchVertices, In, Out};
  dxil::setEntrySignature(*M->getFunction("hs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  HullWrapperPass().run(*M, MAM);

  // The wrapper is not built for a diagnosed shader.
  EXPECT_FALSE(M->getFunction("feme_cpu_entry_hs_main"));
}

TEST(HullWrapperTest, DiagnosesGroupSyncBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @hs_main() #0 {
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      ret void
    }
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  HullWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_hs_main"));
}

/// (Roadmap H4a) `gl_PatchVerticesIn` (`SignatureSystemValue::PatchVertices`)
/// read from the hull control-point phase: `lowerPatchVerticesIn` reports
/// `HullStageEnv::InputPatchControlPointCount`, distinct from
/// `OutputControlPointID` (this invocation's own index, already covered by
/// `LowersSelfIndexedStageIOAndBuildsWrapper` above).
TEST(HullWrapperTest, LowersPatchVerticesInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @hs_main() #0 {
      %pv = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      %pvf = uitofp i32 %pv to float
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %pvf, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement PatchVertices;
  PatchVertices.ElementID = 0;
  PatchVertices.Direction = SignatureDirection::Input;
  PatchVertices.SystemValue = SignatureSystemValue::PatchVertices;
  PatchVertices.ComponentType = SignatureComponentType::UInt;
  PatchVertices.Frequency = SignatureFrequency::PerPatch;
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Out.ComponentType = SignatureComponentType::Float;
  Sig.Elements = {PatchVertices, Out};
  dxil::setEntrySignature(*M->getFunction("hs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  HullWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_hs_main"));
  for (const Instruction &I : instructions(*M->getFunction("hs_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// (Roadmap L82) `gl_PrimitiveID`/`SV_PrimitiveID`
/// (`SignatureSystemValue::PrimitiveID`) read from the hull control-point
/// phase: this patch's own index within the draw, uniform across every
/// control point, with no per-control-point storage of its own. A real IR
/// reduction of `HullSystemValues.test` (roadmap L77-L81's own chain) found
/// this previously fell through to the generic, storage-addressed
/// `lowerHullInputLoad` default case (see that function's own file
/// comment): since `feme::graphics::buildStageStorage` never allocates a
/// slot for this system value, its layout-table entry stayed all-zero,
/// which `computeStageStorageAddress` then silently resolved to byte offset
/// 0 of `Inputs` -- aliasing whatever real element happened to occupy that
/// offset (that test's own `POSITION`) instead of diagnosing the mistake.
/// `lowerHullPrimitiveID` now reports `HullStageEnv::PrimitiveID` instead,
/// mirroring `LowersPatchVerticesInput`'s own `InputPatchControlPointCount`
/// case just above.
TEST(HullWrapperTest, LowersPrimitiveIDInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @hs_main() #0 {
      %pid = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      %pidf = uitofp i32 %pid to float
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %pidf, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement PrimitiveID;
  PrimitiveID.ElementID = 0;
  PrimitiveID.Direction = SignatureDirection::Input;
  PrimitiveID.SystemValue = SignatureSystemValue::PrimitiveID;
  PrimitiveID.ComponentType = SignatureComponentType::UInt;
  PrimitiveID.Frequency = SignatureFrequency::PerPatch;
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Out.ComponentType = SignatureComponentType::Float;
  Sig.Elements = {PrimitiveID, Out};
  dxil::setEntrySignature(*M->getFunction("hs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  HullWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_hs_main"));
  for (const Instruction &I : instructions(*M->getFunction("hs_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// (Roadmap H51/L109) `gl_ViewIndex` read from the hull control-point
/// phase: mirrors `LowersPrimitiveIDInput` immediately above, but for
/// `HullStageEnv::ViewIndex`/`FemePatchArgs::ViewIndex` instead of
/// `PrimitiveID` -- another pipeline-supplied, per-draw scalar with no
/// per-control-point storage of its own.
TEST(HullWrapperTest, LowersViewIndexInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @hs_main() #0 {
      %vidx = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      %vidxf = uitofp i32 %vidx to float
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %vidxf, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement ViewIndex;
  ViewIndex.ElementID = 0;
  ViewIndex.Direction = SignatureDirection::Input;
  ViewIndex.SystemValue = SignatureSystemValue::ViewIndex;
  ViewIndex.ComponentType = SignatureComponentType::UInt;
  ViewIndex.Frequency = SignatureFrequency::PerPatch;
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Out.ComponentType = SignatureComponentType::Float;
  Sig.Elements = {ViewIndex, Out};
  dxil::setEntrySignature(*M->getFunction("hs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  HullWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_hs_main"));
  for (const Instruction &I : instructions(*M->getFunction("hs_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// (Roadmap H29e) A real CTS-shaped hull entry reading its own input
/// control point's `Position` (`gl_in[gl_InvocationID].gl_Position` /
/// `SV_Position`, an ordinary per-control-point attribute the previous
/// stage produced, tagged `SignatureSystemValue::Position` -- not a
/// synthesized invocation-metadata value like `PatchVertices`/
/// `OutputControlPointID`) previously hit "unsupported hull input system
/// value": the input-load switch only routed `SignatureSystemValue::None`
/// through the generic, storage-addressed `lowerHullInputLoad`, rejecting
/// every other input system value outright even though that same generic
/// load addresses any element identically regardless of which (if any)
/// system value it represents. Mirrors the exact shape
/// `vktPipelineCacheTests.cpp`'s own `basic_tcs` shader emits (root cause
/// of the `dEQP-VK.pipeline.pipeline_library.cache.*` re-run's own
/// tessellation-stage failures) and, sharing this same root cause, roadmap
/// H21k's `primitives_generated_query.*.tese.*`.
TEST(HullWrapperTest, LowersPositionInputSystemValue) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @hs_main() #0 {
      %id = call i32 @feme.stage.input.load.i32(i32 2, i32 0, i32 0, i32 0)
      %pos = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 %id)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %pos, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement Position;
  Position.ElementID = 0;
  Position.Direction = SignatureDirection::Input;
  Position.SystemValue = SignatureSystemValue::Position;
  Position.ComponentType = SignatureComponentType::Float;
  SignatureElement Out = Position;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Out.SystemValue = SignatureSystemValue::None;
  SignatureElement ID;
  ID.ElementID = 2;
  ID.Direction = SignatureDirection::Input;
  ID.SystemValue = SignatureSystemValue::OutputControlPointID;
  ID.ComponentType = SignatureComponentType::UInt;
  Sig.Elements = {Position, Out, ID};
  dxil::setEntrySignature(*M->getFunction("hs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  HullWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_hs_main"));
  for (const Instruction &I : instructions(*M->getFunction("hs_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

} // namespace
