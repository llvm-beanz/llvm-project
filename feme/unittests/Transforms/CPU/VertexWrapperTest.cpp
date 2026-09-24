//===- VertexWrapperTest.cpp - Tests for VertexWrapperPass ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/VertexWrapper.h"

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
    Err.print("VertexWrapperTest", errs());
  return M;
}

TEST(VertexWrapperTest, LowersStageIOAndBuildsWrapper) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @vs_main() #0 {
      %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %in, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="vertex" "feme.cpu.wavesize"="4" }
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
  dxil::setEntrySignature(*M->getFunction("vs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  VertexWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_vs_main"));
  for (const Instruction &I : instructions(*M->getFunction("vs_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// (Roadmap L98(a)) A `half`-typed varying is widened to a genuine `float32`
// immediately before its `StageStorage` store, and narrowed back
// immediately after its load -- so the compiled wrapper's own IR should
// show an `fpext`/`fptrunc` pair around each stage-storage access, and the
// call's own operand/result types must stay `half`, matching the original
// (unwidened) stage-IO call's own type before lowering.
TEST(VertexWrapperTest, WidensHalfVaryingAroundStageStorageAccess) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @vs_main() #0 {
      %in = call half @feme.stage.input.load.f16(i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.output.store.f16(i32 1, i32 0, i32 0, half %in, i32 0)
      ret void
    }
    declare half @feme.stage.input.load.f16(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f16(i32, i32, i32, half, i32)
    attributes #0 = { "feme.shader.stage"="vertex" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  In.BitWidth = 16;
  SignatureElement Out = In;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("vs_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  VertexWrapperPass().run(*M, MAM);

  Function *Entry = M->getFunction("feme_cpu_entry_vs_main");
  ASSERT_TRUE(Entry);
  for (const Instruction &I : instructions(*M->getFunction("vs_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  // At least one genuine `half`->`float32` widen (feeding a store) and one
  // `float32`->`half` narrow (fed by a load) must appear in the lowered
  // wrapper body -- confirming the fix actually landed, not just that the
  // module still verifies.
  bool SawFPExtToFloat = false, SawFPTruncFromFloat = false;
  for (const Instruction &I : instructions(*M->getFunction("vs_main"))) {
    if (const auto *Ext = dyn_cast<FPExtInst>(&I)) {
      if (Ext->getOperand(0)->getType()->isHalfTy() &&
          Ext->getType()->isFloatTy())
        SawFPExtToFloat = true;
    } else if (const auto *Trunc = dyn_cast<FPTruncInst>(&I)) {
      if (Trunc->getOperand(0)->getType()->isFloatTy() &&
          Trunc->getType()->isHalfTy())
        SawFPTruncFromFloat = true;
    }
  }
  EXPECT_TRUE(SawFPExtToFloat);
  EXPECT_TRUE(SawFPTruncFromFloat);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

} // namespace
