//===- WaveLoweringTest.cpp - Tests for WaveLoweringPass -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/WaveLowering.h"

#include "feme/Transforms/CPU/BuiltinCalls.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
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
    Err.print("WaveLoweringTest", errs());
  return M;
}

TEST(WaveLoweringTest, LowersThreadIdAndRemovesBuiltinCalls) {
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
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  for (const Instruction &I : instructions(F))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(matchBuiltinCall(*CI));

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(WaveLoweringTest, LowersLaneIndexToConstantIota) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %lane = call i32 @llvm.dx.wave.getlaneindex()
      %doubled = mul i32 %lane, 2
      ret void
    }
    declare i32 @llvm.dx.wave.getlaneindex()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundConstantIota = false;
  for (const Instruction &I : instructions(F)) {
    if (const auto *BO = dyn_cast<BinaryOperator>(&I)) {
      if (auto *CV = dyn_cast<Constant>(BO->getOperand(0))) {
        if (!CV->getType()->isVectorTy())
          continue;
        EXPECT_EQ(
            cast<ConstantInt>(CV->getAggregateElement(0u))->getZExtValue(), 0u);
        EXPECT_EQ(
            cast<ConstantInt>(CV->getAggregateElement(1u))->getZExtValue(), 1u);
        FoundConstantIota = true;
      }
    }
  }
  EXPECT_TRUE(FoundConstantIota);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

} // namespace
