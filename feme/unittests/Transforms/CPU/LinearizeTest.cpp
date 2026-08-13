//===- LinearizeTest.cpp - Tests for LinearizePass ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/Linearize.h"

#include "feme/Transforms/CPU/ResourceCalls.h"
#include "llvm/AsmParser/Parser.h"
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
    Err.print("LinearizeTest", errs());
  return M;
}

bool run(Module &M) {
  ModuleAnalysisManager MAM;
  PreservedAnalyses PA = LinearizePass().run(M, MAM);
  return !PA.areAllPreserved();
}

TEST(LinearizeTest, FlattensDivergentDiamondAndReplacesPhiWithSelect) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      %a = add i32 %tid, 1
      br label %end
    f:
      %b = add i32 %tid, 2
      br label %end
    end:
      %v = phi i32 [%a, %t], [%b, %f]
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(verifyModule(*M, &errs()));

  bool FoundSelect = false;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(isa<PHINode>(I)) << "phi should have become a select";
    if (isa<SelectInst>(I))
      FoundSelect = true;
    if (auto *Br = dyn_cast<CondBrInst>(&I))
      ADD_FAILURE() << "no conditional branch should survive: "
                    << Br->getCondition()->getName();
  }
  EXPECT_TRUE(FoundSelect);
}

TEST(LinearizeTest, LeavesUniformDiamondUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %uniform_cond) #0 {
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
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(run(*M));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundCondBr = false;
  for (Instruction &I : instructions(F))
    if (isa<CondBrInst>(I))
      FoundCondBr = true;
  EXPECT_TRUE(FoundCondBr);
}

TEST(LinearizeTest, MasksResourceCallUnderDivergentBranch) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %heap, i32 %heap_count, i32 %desc) #0 {
    entry:
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %c = icmp eq i32 %tid, 0
      br i1 %c, label %t, label %f
    t:
      %off = sext i32 %tid to i64
      %loaded = call float @feme.cpu.resource.load.raw.f32(ptr %heap, i32 %heap_count, i32 %desc, i64 %off, i1 true)
      br label %end
    f:
      br label %end
    end:
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundMaskedCall = false;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    std::optional<MatchedResourceCall> Matched = matchResourceCall(*CI);
    if (!Matched)
      continue;
    FoundMaskedCall = true;
    EXPECT_FALSE(isa<Constant>(Matched->Mask))
        << "mask should have been rewritten away from the constant `true` "
           "feme::cpu::ResourceLoweringPass left it as";
  }
  EXPECT_TRUE(FoundMaskedCall);
}

} // namespace
