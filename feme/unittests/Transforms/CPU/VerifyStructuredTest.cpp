//===- VerifyStructuredTest.cpp - Tests for verifyStructured -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/VerifyStructured.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("VerifyStructuredTest", errs());
  return M;
}

TEST(VerifyStructuredTest, AcceptsStraightLineCode) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      ret void
    }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(verifyStructured(*M->getFunction("main")));
}

TEST(VerifyStructuredTest, AcceptsUniformDiamond) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %uniform_cond) {
    entry:
      %c = icmp sgt i32 %uniform_cond, 0
      br i1 %c, label %t, label %f
    t:
      br label %end
    f:
      br label %end
    end:
      ret void
    }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(verifyStructured(*M->getFunction("main")));
}

TEST(VerifyStructuredTest, AcceptsDivergentDiamondThatReconverges) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      br label %end
    f:
      br label %end
    end:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(verifyStructured(*M->getFunction("main")));
}

TEST(VerifyStructuredTest, RejectsSwitch) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %v) {
    entry:
      switch i32 %v, label %default [ i32 0, label %zero ]
    default:
      br label %end
    zero:
      br label %end
    end:
      ret void
    }
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(verifyStructured(*M->getFunction("main")));
}

TEST(VerifyStructuredTest, RejectsCriticalEdge) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i1 %c1, i1 %c2) {
    entry:
      br i1 %c1, label %a, label %b
    a:
      br label %merge
    b:
      br i1 %c2, label %merge, label %other
    merge:
      ret void
    other:
      ret void
    }
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(verifyStructured(*M->getFunction("main")));
}

TEST(VerifyStructuredTest, RejectsIrreducibleCycle) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i1 %c) {
    entry:
      br i1 %c, label %a, label %b
    a:
      br label %b
    b:
      br label %a
    }
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(verifyStructured(*M->getFunction("main")));
}

TEST(VerifyStructuredTest, RejectsLoopWithTwoExitBlocks) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %n) {
    entry:
      br label %loop
    loop:
      %i = phi i32 [0, %entry], [%inc, %latch]
      %c1 = icmp eq i32 %i, 5
      br i1 %c1, label %exit1, label %latch
    latch:
      %inc = add i32 %i, 1
      %c2 = icmp sge i32 %inc, %n
      br i1 %c2, label %exit2, label %loop
    exit1:
      ret void
    exit2:
      ret void
    }
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(verifyStructured(*M->getFunction("main")));
}

TEST(VerifyStructuredTest, RejectsDivergentBranchWithNoReconvergence) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      ret void
    f:
      br label %f
    }
    declare i32 @llvm.dx.thread.id(i32)
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(verifyStructured(*M->getFunction("main")));
}

} // namespace
