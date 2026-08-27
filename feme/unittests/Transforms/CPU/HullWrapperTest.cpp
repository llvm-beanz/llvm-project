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

TEST(HullWrapperTest, DiagnosesCrossControlPointInputLoad) {
  LLVMContext Ctx;
  // Reads control point 1's input unconditionally -- not this invocation's
  // own control point -- which HullWrapperPass does not yet support (see
  // its file comment).
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @hs_main() #0 {
      %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 1)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %in, i32 0)
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

} // namespace
