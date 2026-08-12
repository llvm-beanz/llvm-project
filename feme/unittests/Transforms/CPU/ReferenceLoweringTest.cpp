//===- ReferenceLoweringTest.cpp - Tests for ReferenceLoweringPass -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ReferenceLowering.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("ReferenceLoweringTest", errs());
  return M;
}

TEST(ReferenceLoweringTest, LowersThreadIdAndMarksTheFunction) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %doubled = mul i32 %tid, 2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  ReferenceLoweringPass().run(*M, MAM);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(F->hasFnAttribute(ReferenceLoweredAttrName));
  for (const Instruction &I : instructions(F))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(CI->getCalledFunction() &&
                  CI->getCalledFunction()->getName().starts_with("llvm.dx."));

  EXPECT_TRUE(M->getGlobalVariable(ReferenceThreadIndexInGroupGlobalName,
                                   /*AllowInternal=*/true));
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(ReferenceLoweringTest, MarksAFunctionWithNoBuiltinsToo) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  ReferenceLoweringPass().run(*M, MAM);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(F->hasFnAttribute(ReferenceLoweredAttrName));
}

TEST(ReferenceLoweringTest, RejectsWaveIntrinsics) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %lane = call i32 @llvm.dx.wave.getlaneindex()
      ret void
    }
    declare i32 @llvm.dx.wave.getlaneindex()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  bool SawError = false;
  M->getContext().setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Ctx) {
        if (DI->getSeverity() == DS_Error)
          *reinterpret_cast<bool *>(Ctx) = true;
      },
      &SawError);

  ModuleAnalysisManager MAM;
  ReferenceLoweringPass().run(*M, MAM);

  EXPECT_TRUE(SawError);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(F->hasFnAttribute(ReferenceLoweredAttrName));
}

} // namespace
