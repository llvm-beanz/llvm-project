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
#include "feme/Transforms/CPU/WaveCalls.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
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

/// Runs `SIMDizePass`+`WaveLoweringPass` over \p Assembly at \p WaveSize,
/// asserting the module still verifies and every `feme.cpu.wave.*`/
/// `feme.cpu.builtin.*` call is gone (fully lowered), and returns the
/// resulting module for a test to inspect further.
std::unique_ptr<Module> lowerWaveOps(LLVMContext &Ctx, StringRef Assembly,
                                     unsigned WaveSize = 4) {
  std::unique_ptr<Module> M = parseIR(Ctx, Assembly);
  if (!M)
    return nullptr;

  ModuleAnalysisManager MAM;
  SIMDizePass(WaveSize).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  for (Function &F : *M)
    for (const Instruction &I : instructions(F))
      if (const auto *CI = dyn_cast<CallInst>(&I))
        EXPECT_FALSE(matchWaveCall(*CI))
            << "left an unlowered feme.cpu.wave.* call behind";

  EXPECT_FALSE(verifyModule(*M, &errs()));
  return M;
}

TEST(WaveLoweringTest, LowersGetLaneCountToConstant) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %n = call i32 @llvm.dx.wave.get.lane.count()
      ret void
    }
    declare i32 @llvm.dx.wave.get.lane.count()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(getWaveBodyEnv(*F));
}

TEST(WaveLoweringTest, LowersIsFirstLaneToDivergentMaskArithmetic) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %first = call i1 @llvm.dx.wave.is.first.lane()
      %sel = select i1 %first, i32 1, i32 0
      ret void
    }
    declare i1 @llvm.dx.wave.is.first.lane()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundCttz = false;
  for (const Instruction &I : instructions(F))
    if (const auto *II = dyn_cast<IntrinsicInst>(&I))
      FoundCttz |= II->getIntrinsicID() == Intrinsic::cttz;
  EXPECT_TRUE(FoundCttz);
}

TEST(WaveLoweringTest, LowersAnyAllToVectorReductions) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %pred = icmp eq i32 %tid, 0
      %any = call i1 @llvm.dx.wave.any(i1 %pred)
      %all = call i1 @llvm.dx.wave.all(i1 %pred)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i1 @llvm.dx.wave.any(i1)
    declare i1 @llvm.dx.wave.all(i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundOrReduce = false, FoundAndReduce = false;
  for (const Instruction &I : instructions(F))
    if (const auto *II = dyn_cast<IntrinsicInst>(&I)) {
      FoundOrReduce |= II->getIntrinsicID() == Intrinsic::vector_reduce_or;
      FoundAndReduce |= II->getIntrinsicID() == Intrinsic::vector_reduce_and;
    }
  EXPECT_TRUE(FoundOrReduce);
  EXPECT_TRUE(FoundAndReduce);
}

TEST(WaveLoweringTest, LowersAllEqualAtSeveralWaveSizes) {
  for (unsigned W : {4u, 8u, 16u}) {
    LLVMContext Ctx;
    std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
      define void @main() #0 {
        %tid = call i32 @llvm.dx.thread.id(i32 0)
        %eq = call i1 @llvm.dx.wave.all.equal.i32(i32 %tid)
        ret void
      }
      declare i32 @llvm.dx.thread.id(i32)
      declare i1 @llvm.dx.wave.all.equal.i32(i32)
      attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
    )",
                                             W);
    ASSERT_TRUE(M) << "wave size " << W;
  }
}

TEST(WaveLoweringTest, LowersReadLaneToGuardedExtract) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %val = call i32 @llvm.dx.wave.readlane.i32(i32 %tid, i32 0)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.readlane.i32(i32, i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundSelect = false;
  for (const Instruction &I : instructions(F))
    FoundSelect |= isa<SelectInst>(&I);
  EXPECT_TRUE(FoundSelect);
}

TEST(WaveLoweringTest, LowersActiveCountBitsToCtpop) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %pred = icmp eq i32 %tid, 0
      %cnt = call i32 @llvm.dx.wave.active.countbits(i1 %pred)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.active.countbits(i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundCtpop = false;
  for (const Instruction &I : instructions(F))
    if (const auto *II = dyn_cast<IntrinsicInst>(&I))
      FoundCtpop |= II->getIntrinsicID() == Intrinsic::ctpop;
  EXPECT_TRUE(FoundCtpop);
}

TEST(WaveLoweringTest, LowersPrefixBitCountToDivergentLaneLoop) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %pred = icmp eq i32 %tid, 0
      %cnt = call i32 @llvm.dx.wave.prefix.bit.count(i1 %pred)
      %doubled = mul i32 %cnt, 2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.prefix.bit.count(i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  // The result is divergent (see `isDivergentWaveCallResult`), so it
  // widens `%doubled` into a real `<4 x i32>` multiply rather than leaving
  // it scalar.
  bool FoundWideMul = false;
  for (const Instruction &I : instructions(F))
    if (const auto *BO = dyn_cast<BinaryOperator>(&I))
      FoundWideMul |=
          BO->getOpcode() == Instruction::Mul && BO->getType()->isVectorTy();
  EXPECT_TRUE(FoundWideMul);
}

TEST(WaveLoweringTest, LowersBallotToInsertValueChain) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %pred = icmp eq i32 %tid, 0
      %r = call {i32, i32, i32, i32} @llvm.dx.wave.ballot.i32(i1 %pred)
      %x = extractvalue {i32, i32, i32, i32} %r, 0
      %cnt = call i32 @llvm.ctpop.i32(i32 %x)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare {i32, i32, i32, i32} @llvm.dx.wave.ballot.i32(i1)
    declare i32 @llvm.ctpop.i32(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  // The lowered ballot is a 4-field `insertvalue` chain (see `lowerBallot`),
  // built from a masked `bitcast`-to-`iW` word rather than a per-lane
  // vector -- unlike `PrefixBitCount` above, the result is uniform (see
  // `isDivergentWaveCallResult`), so nothing downstream needs widening.
  unsigned InsertValueCount = 0;
  for (const Instruction &I : instructions(F))
    if (isa<InsertValueInst>(&I))
      ++InsertValueCount;
  EXPECT_EQ(InsertValueCount, 4u);
}

TEST(WaveLoweringTest, LowersActiveSumToMaskedVectorReduceAdd) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %sum = call i32 @llvm.dx.wave.reduce.sum.i32(i32 %tid)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.reduce.sum.i32(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundSelect = false, FoundAddReduce = false;
  for (const Instruction &I : instructions(F)) {
    FoundSelect |= isa<SelectInst>(&I);
    if (const auto *II = dyn_cast<IntrinsicInst>(&I))
      FoundAddReduce |= II->getIntrinsicID() == Intrinsic::vector_reduce_add;
  }
  EXPECT_TRUE(FoundSelect);
  EXPECT_TRUE(FoundAddReduce);
}

TEST(WaveLoweringTest, LowersActiveMaxToMaskedFPMaxReduce) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %tidf = uitofp i32 %tid to float
      %m = call float @llvm.dx.wave.reduce.max.f32(float %tidf)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare float @llvm.dx.wave.reduce.max.f32(float)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundFMaxReduce = false;
  for (const Instruction &I : instructions(F))
    if (const auto *II = dyn_cast<IntrinsicInst>(&I))
      FoundFMaxReduce |= II->getIntrinsicID() == Intrinsic::vector_reduce_fmax;
  EXPECT_TRUE(FoundFMaxReduce);
}

TEST(WaveLoweringTest, LowersPrefixSumToDivergentLaneLoop) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = lowerWaveOps(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %sum = call i32 @llvm.dx.wave.prefix.sum.i32(i32 %tid)
      %doubled = mul i32 %sum, 2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.prefix.sum.i32(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  // `PrefixSum` is divergent (see `isDivergentWaveCallResult`), so it
  // widens `%doubled` into a real `<4 x i32>` multiply, the same way
  // `PrefixBitCount` does above.
  bool FoundWideMul = false;
  for (const Instruction &I : instructions(F))
    if (const auto *BO = dyn_cast<BinaryOperator>(&I))
      FoundWideMul |=
          BO->getOpcode() == Instruction::Mul && BO->getType()->isVectorTy();
  EXPECT_TRUE(FoundWideMul);
}

} // namespace
