//===- PrepareTest.cpp - Tests for PreparePass ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/Prepare.h"

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
    Err.print("PrepareTest", errs());
  return M;
}

void runPass(Module &M, StringRef EntryPoint = "") {
  ModuleAnalysisManager MAM;
  PreparePass(EntryPoint).run(M, MAM);
}

TEST(PrepareTest, PromotesAllocaAndLowersSwitch) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %v) #0 {
    entry:
      %a = alloca i32
      store i32 %v, ptr %a
      %loaded = load i32, ptr %a
      switch i32 %loaded, label %default [ i32 0, label %zero ]
    default:
      br label %end
    zero:
      br label %end
    end:
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  for (const Instruction &I : instructions(F)) {
    EXPECT_FALSE(isa<AllocaInst>(I));
    EXPECT_FALSE(isa<SwitchInst>(I));
  }
}

TEST(PrepareTest, KeepsOnlySelectedEntryPoint) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    define void @other_entry() #0 {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M, "main");

  EXPECT_TRUE(M->getFunction("main"));
  EXPECT_FALSE(M->getFunction("other_entry"));
}

TEST(PrepareTest, SelectsSoleEntryPointWithoutAnOption) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  EXPECT_TRUE(M->getFunction("main"));
}

TEST(PrepareTest, RemovesUnreachableDefinitions) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      call void @helper()
      ret void
    }
    define void @helper() {
      ret void
    }
    define void @unreachable_helper() {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  EXPECT_TRUE(M->getFunction("helper"));
  EXPECT_FALSE(M->getFunction("unreachable_helper"));
}

} // namespace
