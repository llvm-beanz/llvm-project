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
