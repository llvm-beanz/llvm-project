//===- WaveUniformityTest.cpp - Tests for feme::cpu::computeWaveUniformity =//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Constructs small raised-IR-shaped functions and asserts which values
// `computeWaveUniformity` (backed by `WaveTTIImpl`) reports as divergent,
// covering the classification described in "Phase 2: Uniformity Analysis" of
// feme/docs/FeMeCPUDesign.md: lane-varying builtins are divergence sources,
// `WaveActive*`/`WaveReadLaneAt`-style reductions are uniform, and ordinary
// values are divergent only if they transitively depend on a divergent one
// (including through a divergent branch's control dependence).
//
//===----------------------------------------------------------------------===//

#include "feme/Analysis/CPU/WaveUniformity.h"

#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Parses \p Assembly, runs `computeWaveUniformity` over its sole function,
/// and returns both for the caller to inspect.
std::pair<std::unique_ptr<Module>, UniformityInfo>
computeUniformityFor(LLVMContext &Context, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Context);
  if (!M)
    Err.print("WaveUniformityTest", errs());
  EXPECT_TRUE(M != nullptr);

  Function *F = M->getFunction("main");
  EXPECT_TRUE(F != nullptr);
  DominatorTree DT(*F);
  CycleInfo CI;
  CI.compute(*F);
  UniformityInfo UI = computeWaveUniformity(*F, DT, CI);
  return {std::move(M), std::move(UI)};
}

Instruction *findInstructionNamed(Function &F, StringRef Name) {
  for (Instruction &I : instructions(F))
    if (I.getName() == Name)
      return &I;
  return nullptr;
}

TEST(WaveUniformityTest, ThreadIdIsDivergent) {
  LLVMContext Context;
  auto [M, UI] = computeUniformityFor(Context, R"(
    declare i32 @llvm.dx.thread.id(i32)
    define void @main() {
      %id = call i32 @llvm.dx.thread.id(i32 0)
      ret void
    }
  )");
  Instruction *Id = findInstructionNamed(*M->getFunction("main"), "id");
  ASSERT_TRUE(Id);
  EXPECT_TRUE(UI.isDivergentAtDef(Id)) << "llvm.dx.thread.id should diverge";
}

TEST(WaveUniformityTest, FlattenedThreadIdInGroupIsDivergent) {
  LLVMContext Context;
  auto [M, UI] = computeUniformityFor(Context, R"(
    declare i32 @llvm.dx.flattened.thread.id.in.group()
    define void @main() {
      %id = call i32 @llvm.dx.flattened.thread.id.in.group()
      ret void
    }
  )");
  Instruction *Id = findInstructionNamed(*M->getFunction("main"), "id");
  ASSERT_TRUE(Id);
  EXPECT_TRUE(UI.isDivergentAtDef(Id))
      << "llvm.dx.flattened.thread.id.in.group should diverge";
}

TEST(WaveUniformityTest, WaveGetLaneIndexIsDivergent) {
  LLVMContext Context;
  auto [M, UI] = computeUniformityFor(Context, R"(
    declare i32 @llvm.dx.wave.getlaneindex()
    define void @main() {
      %lane = call i32 @llvm.dx.wave.getlaneindex()
      ret void
    }
  )");
  Instruction *Lane = findInstructionNamed(*M->getFunction("main"), "lane");
  ASSERT_TRUE(Lane);
  EXPECT_TRUE(UI.isDivergentAtDef(Lane))
      << "llvm.dx.wave.getlaneindex should diverge";
}

TEST(WaveUniformityTest, WavePrefixSumIsDivergent) {
  LLVMContext Context;
  auto [M, UI] = computeUniformityFor(Context, R"(
    declare i32 @llvm.dx.wave.prefix.usum.i32(i32)
    define void @main() {
      %sum = call i32 @llvm.dx.wave.prefix.usum.i32(i32 1)
      ret void
    }
  )");
  Instruction *Sum = findInstructionNamed(*M->getFunction("main"), "sum");
  ASSERT_TRUE(Sum);
  EXPECT_TRUE(UI.isDivergentAtDef(Sum))
      << "llvm.dx.wave.prefix.usum should diverge: each lane sums a "
         "different prefix";
}

TEST(WaveUniformityTest, WaveActiveReductionIsUniform) {
  LLVMContext Context;
  auto [M, UI] = computeUniformityFor(Context, R"(
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.reduce.usum.i32(i32)
    define void @main() {
      %id = call i32 @llvm.dx.thread.id(i32 0)
      %sum = call i32 @llvm.dx.wave.reduce.usum.i32(i32 %id)
      ret void
    }
  )");
  Instruction *Sum = findInstructionNamed(*M->getFunction("main"), "sum");
  ASSERT_TRUE(Sum);
  EXPECT_FALSE(UI.isDivergentAtDef(Sum))
      << "WaveActiveUSum reduces over the whole wave, so its result is "
         "uniform even though its operand (a thread id) is divergent";
}

TEST(WaveUniformityTest, WaveReadLaneIsUniform) {
  LLVMContext Context;
  auto [M, UI] = computeUniformityFor(Context, R"(
    declare i32 @llvm.dx.thread.id(i32)
    declare i32 @llvm.dx.wave.readlane.i32(i32, i32)
    define void @main() {
      %id = call i32 @llvm.dx.thread.id(i32 0)
      %bcast = call i32 @llvm.dx.wave.readlane.i32(i32 %id, i32 0)
      ret void
    }
  )");
  Instruction *Bcast = findInstructionNamed(*M->getFunction("main"), "bcast");
  ASSERT_TRUE(Bcast);
  EXPECT_FALSE(UI.isDivergentAtDef(Bcast))
      << "WaveReadLaneAt broadcasts a single lane's value, so its result is "
         "uniform";
}

TEST(WaveUniformityTest, ConstantIsUniform) {
  LLVMContext Context;
  auto [M, UI] = computeUniformityFor(Context, R"(
    define void @main() {
      %c = add i32 1, 1
      ret void
    }
  )");
  Instruction *C = findInstructionNamed(*M->getFunction("main"), "c");
  ASSERT_TRUE(C);
  EXPECT_FALSE(UI.isDivergentAtDef(C)) << "constants should be uniform";
}

TEST(WaveUniformityTest, ValueDependentOnDivergentValueIsDivergent) {
  LLVMContext Context;
  auto [M, UI] = computeUniformityFor(Context, R"(
    declare i32 @llvm.dx.thread.id(i32)
    define void @main() {
      %id = call i32 @llvm.dx.thread.id(i32 0)
      %doubled = add i32 %id, %id
      ret void
    }
  )");
  Instruction *Doubled =
      findInstructionNamed(*M->getFunction("main"), "doubled");
  ASSERT_TRUE(Doubled);
  EXPECT_TRUE(UI.isDivergentAtDef(Doubled))
      << "a value computed from a divergent thread id should itself diverge";
}

TEST(WaveUniformityTest, DivergentBranchMakesPhiDivergent) {
  LLVMContext Context;
  auto [M, UI] = computeUniformityFor(Context, R"(
    declare i32 @llvm.dx.thread.id(i32)
    define void @main() {
    entry:
      %id = call i32 @llvm.dx.thread.id(i32 0)
      %cond = icmp eq i32 %id, 0
      br i1 %cond, label %if_true, label %if_false
    if_true:
      br label %exit
    if_false:
      br label %exit
    exit:
      %merged = phi i32 [ 1, %if_true ], [ 2, %if_false ]
      ret void
    }
  )");
  Instruction *Merged = findInstructionNamed(*M->getFunction("main"), "merged");
  ASSERT_TRUE(Merged);
  EXPECT_TRUE(UI.isDivergentAtDef(Merged))
      << "a phi merging constants along a divergent branch's arms should "
         "itself diverge, even though neither incoming value is itself "
         "divergent";
}

TEST(WaveUniformityTest, UniformBranchKeepsPhiUniform) {
  LLVMContext Context;
  auto [M, UI] = computeUniformityFor(Context, R"(
    define void @main(i32 %cond) {
    entry:
      %c = icmp eq i32 %cond, 0
      br i1 %c, label %if_true, label %if_false
    if_true:
      br label %exit
    if_false:
      br label %exit
    exit:
      %merged = phi i32 [ 1, %if_true ], [ 2, %if_false ]
      ret void
    }
  )");
  Instruction *Merged = findInstructionNamed(*M->getFunction("main"), "merged");
  ASSERT_TRUE(Merged);
  EXPECT_FALSE(UI.isDivergentAtDef(Merged))
      << "a branch on a uniform value should not make its phi divergent";
}

} // namespace
