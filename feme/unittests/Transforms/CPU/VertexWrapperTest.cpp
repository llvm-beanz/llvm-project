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

} // namespace
