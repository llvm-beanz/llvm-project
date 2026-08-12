//===- SIMDizeTest.cpp - Tests for SIMDizePass ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SIMDize.h"

#include "feme/Transforms/CPU/BuiltinCalls.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/SourceMgr.h"
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
      if (BO->getOpcode() == Instruction::Mul &&
          BO->getType()->isVectorTy())
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

} // namespace
