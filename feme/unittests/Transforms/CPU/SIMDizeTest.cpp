//===- SIMDizeTest.cpp - Tests for SIMDizePass ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SIMDize.h"

#include "feme/Transforms/CPU/BuiltinCalls.h"
#include "feme/Transforms/CPU/Linearize.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("SIMDizeTest", errs());
  return M;
}

void runPass(Module &M, unsigned WaveSize = 4) {
  ModuleAnalysisManager MAM;
  SIMDizePass(WaveSize).run(M, MAM);
}

TEST(SIMDizeTest, AppendsWaveBodyInterfaceParams) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  std::optional<WaveBodyEnv> Env = getWaveBodyEnv(*F);
  ASSERT_TRUE(Env);
  EXPECT_TRUE(Env->GroupIDX);
  EXPECT_TRUE(Env->GroupIDY);
  EXPECT_TRUE(Env->GroupIDZ);
  EXPECT_TRUE(Env->WaveIndex);
  EXPECT_TRUE(Env->EntryMask);
  EXPECT_TRUE(Env->GroupShared);
  EXPECT_EQ(cast<FixedVectorType>(Env->EntryMask->getType())->getNumElements(),
            4u);
}

TEST(SIMDizeTest, WidensDivergentThreadId) {
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
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundBuiltinCall = false;
  bool FoundWideMul = false;
  for (Instruction &I : instructions(F)) {
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      if (std::optional<MatchedBuiltinCall> Matched = matchBuiltinCall(*CI)) {
        FoundBuiltinCall = true;
        EXPECT_EQ(Matched->Kind, BuiltinCallKind::ThreadId);
        EXPECT_EQ(Matched->WaveSize, 4u);
      }
    }
    if (auto *BO = dyn_cast<BinaryOperator>(&I))
      if (BO->getOpcode() == Instruction::Mul && BO->getType()->isVectorTy())
        FoundWideMul = true;
  }
  EXPECT_TRUE(FoundBuiltinCall);
  EXPECT_TRUE(FoundWideMul);
}

TEST(SIMDizeTest, LeavesUniformFunctionUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %v) #0 {
      %r = add i32 %v, 1
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  for (Instruction &I : instructions(F))
    if (auto *BO = dyn_cast<BinaryOperator>(&I))
      EXPECT_FALSE(BO->getType()->isVectorTy());
}

TEST(SIMDizeTest, WidensMaskedLoopBackedge) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n) #0 {
    entry:
      br label %loop
    loop:
      %i = phi i32 [0, %entry], [%inc, %latch]
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %break.cond = icmp eq i32 %tid, %i
      br i1 %break.cond, label %exit, label %latch
    latch:
      %inc = add i32 %i, 1
      %loop.cond = icmp slt i32 %inc, %n
      br i1 %loop.cond, label %loop, label %exit
    exit:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  feme::cpu::LinearizePass().run(*M, MAM);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundWidePHI = false;
  bool FoundReduceOr = false;
  for (Instruction &I : instructions(F)) {
    if (auto *PN = dyn_cast<PHINode>(&I))
      if (PN->getType()->isVectorTy())
        FoundWidePHI = true;
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (CI->getCalledFunction() &&
          CI->getCalledFunction()->getIntrinsicID() ==
              Intrinsic::vector_reduce_or)
        FoundReduceOr = true;
  }
  EXPECT_TRUE(FoundWidePHI);
  EXPECT_TRUE(FoundReduceOr);
}

TEST(SIMDizeTest, ScalarizesAtomicRMWFallback) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %p) #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tid64 = zext i32 %tid to i64
      %ptr = getelementptr i32, ptr %p, i64 %tid64
      %old = atomicrmw add ptr %ptr, i32 1 monotonic
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  unsigned AtomicRMWCount = 0;
  for (Instruction &I : instructions(F))
    if (isa<AtomicRMWInst>(&I))
      ++AtomicRMWCount;
  EXPECT_EQ(AtomicRMWCount, 4u);
}

} // namespace
