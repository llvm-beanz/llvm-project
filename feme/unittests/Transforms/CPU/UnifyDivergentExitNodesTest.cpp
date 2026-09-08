//===- UnifyDivergentExitNodesTest.cpp - Tests for unifyDivergentExitNodes ==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/UnifyDivergentExitNodes.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
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
    Err.print("UnifyDivergentExitNodesTest", errs());
  return M;
}

/// Counts every block in \p F whose terminator is a `ret`.
unsigned countReturningBlocks(Function &F) {
  unsigned Count = 0;
  for (BasicBlock &BB : F)
    if (isa<ReturnInst>(BB.getTerminator()))
      ++Count;
  return Count;
}

TEST(UnifyDivergentExitNodesTest, LeavesASingleReturnBlockUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
    entry:
      ret void
    }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");

  EXPECT_FALSE(unifyDivergentExitNodes(*F));
  EXPECT_EQ(countReturningBlocks(*F), 1u);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(UnifyDivergentExitNodesTest, MergesAnEarlyReturnIntoTheNormalExit) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i1 %cond) {
    entry:
      br i1 %cond, label %early_ret, label %body
    early_ret:
      ret void
    body:
      ret void
    }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_EQ(countReturningBlocks(*F), 2u);

  EXPECT_TRUE(unifyDivergentExitNodes(*F));
  EXPECT_EQ(countReturningBlocks(*F), 1u);
  // Every original `ret` became an unconditional branch to the one shared
  // exit block that now holds the only `ret void` left.
  EXPECT_TRUE(isa<UncondBrInst>(
      F->getEntryBlock().getTerminator()->getSuccessor(0)->getTerminator()));
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(UnifyDivergentExitNodesTest, MergesMoreThanTwoReturnBlocks) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i2 %which) {
    entry:
      switch i2 %which, label %c [ i2 0, label %a
                                   i2 1, label %b ]
    a:
      ret void
    b:
      ret void
    c:
      ret void
    }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_EQ(countReturningBlocks(*F), 3u);

  EXPECT_TRUE(unifyDivergentExitNodes(*F));
  EXPECT_EQ(countReturningBlocks(*F), 1u);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(UnifyDivergentExitNodesTest, MergesANonVoidReturnWithAPhi) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @f(i1 %cond) {
    entry:
      br i1 %cond, label %t, label %f
    t:
      ret i32 1
    f:
      ret i32 2
    }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("f");
  ASSERT_EQ(countReturningBlocks(*F), 2u);

  EXPECT_TRUE(unifyDivergentExitNodes(*F));
  EXPECT_EQ(countReturningBlocks(*F), 1u);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  for (BasicBlock &BB : *F) {
    if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator()))
      EXPECT_TRUE(isa<PHINode>(RI->getReturnValue()));
  }
}

} // namespace
