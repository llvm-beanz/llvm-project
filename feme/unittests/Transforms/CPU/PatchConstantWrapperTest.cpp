//===- PatchConstantWrapperTest.cpp - Tests for PatchConstantWrapperPass -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/PatchConstantWrapper.h"

#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/HullWrapper.h"
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
    Err.print("PatchConstantWrapperTest", errs());
  return M;
}

TEST(PatchConstantWrapperTest, LowersOutputPatchReadsAndBuildsWrapper) {
  LLVMContext Ctx;
  // Reads two output control points (not just "its own" -- see
  // PatchConstantWrapper.cpp's file comment) and writes their sum as a
  // patch-constant output.
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @pc_main() #0 {
      %a = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %b = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 1)
      %sum = fadd float %a, %b
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
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::PatchOutput;
  Out.Frequency = SignatureFrequency::PerPatch;
  Out.ComponentType = SignatureComponentType::Float;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("pc_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  PatchConstantWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_pc_main"));
  for (const Instruction &I : instructions(*M->getFunction("pc_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(PatchConstantWrapperTest, LowersInputPatchAndOutputPatchReadsSeparately) {
  LLVMContext Ctx;
  // Element 0 is an `Input`-direction element read from the *original*
  // `InputPatch` (`FromInputPatch`); element 1 is an ordinary `Input`
  // element read from the completed `OutputPatch`, matching
  // `LowersOutputPatchReadsAndBuildsWrapper` above -- both may legally
  // appear in the same patch-constant function, addressed by two distinct
  // `feme.stage.input.load` element IDs.
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @pc_main() #0 {
      %orig = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %completed = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
      %sum = fadd float %orig, %completed
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %sum, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement InputPatchElt;
  InputPatchElt.ElementID = 0;
  InputPatchElt.Direction = SignatureDirection::Input;
  InputPatchElt.ComponentType = SignatureComponentType::Float;
  InputPatchElt.FromInputPatch = true;
  SignatureElement OutputPatchElt;
  OutputPatchElt.ElementID = 1;
  OutputPatchElt.Direction = SignatureDirection::Input;
  OutputPatchElt.ComponentType = SignatureComponentType::Float;
  SignatureElement Out;
  Out.ElementID = 2;
  Out.Direction = SignatureDirection::PatchOutput;
  Out.Frequency = SignatureFrequency::PerPatch;
  Out.ComponentType = SignatureComponentType::Float;
  Sig.Elements = {InputPatchElt, OutputPatchElt, Out};
  dxil::setEntrySignature(*M->getFunction("pc_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  PatchConstantWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_pc_main");
  ASSERT_TRUE(Wrapper);
  for (const Instruction &I : instructions(*M->getFunction("pc_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  // The compiled body takes both an `InputPatch` block and an `OutputPatch`
  // block as distinct parameters, not a single shared one.
  Function *Body = M->getFunction("pc_main");
  ASSERT_TRUE(Body);
  bool HasInputPatchLayout = false, HasInputPatch = false;
  for (const Argument &Arg : Body->args()) {
    HasInputPatchLayout |= Arg.getName() == "stage_input_patch_layout";
    HasInputPatch |= Arg.getName() == "stage_input_patch";
  }
  EXPECT_TRUE(HasInputPatchLayout);
  EXPECT_TRUE(HasInputPatch);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// (Roadmap H13b) `gl_ClipDistance`/`gl_CullDistance`, read from the
/// *original* input patch (`FromInputPatch`) by a barrier-less
/// tessellation-control entry point whose mixed control-point/patch-
/// constant body ends up compiled as this phase too (see
/// `CanonicalizeStage.cpp`'s `isPatchConstantPhase`/
/// `splitBarrierlessTessellationControlEntry`): despite carrying a
/// `SystemValue` (they have no `Location` of their own -- see
/// `FragmentWrapper.cpp`'s analogous roadmap H7x fix), these are ordinary
/// per-control-point array elements, not scalar system values like
/// `OutputControlPointID`/`PatchVertices`, so they must take the same
/// `InputPatch`-addressed path as any other linked input rather than
/// falling through `lowerPatchConstantSystemValue`'s unsupported-system-
/// value error.
TEST(PatchConstantWrapperTest, LowersClipDistanceAndCullDistanceInputPatchElements) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @pc_main() #0 {
      %clip = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %cull = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 1)
      %sum = fadd float %clip, %cull
      call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %sum, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement ClipDistance;
  ClipDistance.ElementID = 0;
  ClipDistance.Direction = SignatureDirection::Input;
  ClipDistance.SystemValue = SignatureSystemValue::ClipDistance;
  ClipDistance.ComponentType = SignatureComponentType::Float;
  ClipDistance.FromInputPatch = true;
  SignatureElement CullDistance;
  CullDistance.ElementID = 1;
  CullDistance.Direction = SignatureDirection::Input;
  CullDistance.SystemValue = SignatureSystemValue::CullDistance;
  CullDistance.ComponentType = SignatureComponentType::Float;
  CullDistance.FromInputPatch = true;
  SignatureElement Out;
  Out.ElementID = 2;
  Out.Direction = SignatureDirection::PatchOutput;
  Out.Frequency = SignatureFrequency::PerPatch;
  Out.ComponentType = SignatureComponentType::Float;
  Sig.Elements = {ClipDistance, CullDistance, Out};
  dxil::setEntrySignature(*M->getFunction("pc_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  PatchConstantWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_pc_main"));
  for (const Instruction &I : instructions(*M->getFunction("pc_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(PatchConstantWrapperTest, DiagnosesGroupSyncBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @pc_main() #0 {
      call void @llvm.dx.group.memory.barrier.with.group.sync()
      ret void
    }
    declare void @llvm.dx.group.memory.barrier.with.group.sync()
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement Out;
  Out.ElementID = 0;
  Out.Direction = SignatureDirection::PatchOutput;
  Out.Frequency = SignatureFrequency::PerPatch;
  Sig.Elements = {Out};
  dxil::setEntrySignature(*M->getFunction("pc_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  PatchConstantWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_pc_main"));
}

/// (Roadmap H4a) `SV_OutputControlPointID`/`gl_InvocationID`, read from the
/// patch-constant phase (unlike the control-point phase, which addresses
/// its own invocation by that same value): this phase runs once per patch,
/// not once per control point, so there is no "this invocation's own"
/// control point to report -- `lowerPatchConstantSystemValue` always
/// returns 0, matching D3D's own documented behavior for
/// `SV_OutputControlPointID` read from a patch-constant function.
TEST(PatchConstantWrapperTest, LowersOutputControlPointIDAsZero) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @pc_main() #0 {
      %id = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      %idf = uitofp i32 %id to float
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %idf, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="hull" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement ID;
  ID.ElementID = 0;
  ID.Direction = SignatureDirection::Input;
  ID.SystemValue = SignatureSystemValue::OutputControlPointID;
  ID.ComponentType = SignatureComponentType::UInt;
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::PatchOutput;
  Out.Frequency = SignatureFrequency::PerPatch;
  Out.ComponentType = SignatureComponentType::Float;
  Sig.Elements = {ID, Out};
  dxil::setEntrySignature(*M->getFunction("pc_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  PatchConstantWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_pc_main"));
  for (const Instruction &I : instructions(*M->getFunction("pc_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

/// (Roadmap H4a) `gl_PatchVerticesIn` read from the *original* input patch
/// (`FromInputPatch`, distinct from `OutputControlPointID`'s "always 0"):
/// `lowerPatchConstantSystemValue` reports
/// `PatchConstantStageEnv::InputPatchControlPointCount`.
TEST(PatchConstantWrapperTest, LowersInputPatchVerticesCount) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @pc_main() #0 {
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
  PatchVertices.FromInputPatch = true;
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::PatchOutput;
  Out.Frequency = SignatureFrequency::PerPatch;
  Out.ComponentType = SignatureComponentType::Float;
  Sig.Elements = {PatchVertices, Out};
  dxil::setEntrySignature(*M->getFunction("pc_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  PatchConstantWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_pc_main"));
  for (const Instruction &I : instructions(*M->getFunction("pc_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(PatchConstantWrapperTest, HullWrapperSkipsPatchConstantPhase) {
  LLVMContext Ctx;
  // A `PatchOutput`-bearing function is the patch-constant phase, not the
  // control-point phase: `HullWrapperPass` must leave it entirely to
  // `PatchConstantWrapperPass` (see HullPhase.h's discriminator).
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @pc_main() #0 {
      %a = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %a, i32 0)
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
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::PatchOutput;
  Out.Frequency = SignatureFrequency::PerPatch;
  Out.ComponentType = SignatureComponentType::Float;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("pc_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  HullWrapperPass().run(*M, MAM);

  // Untouched: still has its stage ops, no wrapper built.
  EXPECT_FALSE(M->getFunction("feme_cpu_entry_pc_main"));
  bool SawStageOp = false;
  for (const Instruction &I : instructions(*M->getFunction("pc_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      SawStageOp |= isStageOpCall(*CI);
  EXPECT_TRUE(SawStageOp);

  PatchConstantWrapperPass().run(*M, MAM);
  EXPECT_TRUE(M->getFunction("feme_cpu_entry_pc_main"));
}

} // namespace
