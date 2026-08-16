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

// Roadmap R27: one invocation at a time has no mask to narrow, so
// `feme.stage.discard` becomes a real conditional early return instead.
TEST(ReferenceLoweringTest, DiscardBecomesRealConditionalReturn) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i1 %cond) #0 {
    entry:
      call void @feme.stage.discard(i1 %cond)
      ret void
    }
    declare void @feme.stage.discard(i1)
    attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  ReferenceLoweringPass().run(*M, MAM);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(F->hasFnAttribute(ReferenceLoweredAttrName));
  EXPECT_EQ(F->arg_size(), 1u);

  unsigned NumCondBr = 0, NumRet = 0;
  for (BasicBlock &BB : *F) {
    if (isa<CondBrInst>(BB.getTerminator()))
      ++NumCondBr;
    if (isa<ReturnInst>(BB.getTerminator()))
      ++NumRet;
  }
  EXPECT_EQ(NumCondBr, 1u);
  EXPECT_EQ(NumRet, 2u) << "both the discard-taken and fallthrough paths "
                           "should return";
}

// `feme.stage.demote` narrows only a per-invocation `helper` flag, not
// control flow (see the header comment's Deviation note), and
// `feme.stage.is_helper` reads it back.
TEST(ReferenceLoweringTest, DemoteAndIsHelperUseAPerInvocationFlag) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i1 @main(i1 %cond) #0 {
    entry:
      call void @feme.stage.demote(i1 %cond)
      %h = call i1 @feme.stage.is_helper()
      ret i1 %h
    }
    declare void @feme.stage.demote(i1)
    declare i1 @feme.stage.is_helper()
    attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  ReferenceLoweringPass().run(*M, MAM);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundAlloca = false;
  for (Instruction &I : instructions(F))
    if (isa<AllocaInst>(I))
      FoundAlloca = true;
  EXPECT_TRUE(FoundAlloca) << "demote/is_helper should use a per-invocation "
                              "helper flag";
  auto *Ret = dyn_cast<ReturnInst>(F->getEntryBlock().getTerminator());
  ASSERT_TRUE(Ret);
  EXPECT_TRUE(isa<LoadInst>(Ret->getReturnValue()))
      << "is_helper should read the helper flag back";
}

} // namespace
