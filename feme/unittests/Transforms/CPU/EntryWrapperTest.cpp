//===- EntryWrapperTest.cpp - Tests for EntryWrapperPass ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/EntryWrapper.h"

#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/WaveLowering.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
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
    Err.print("EntryWrapperTest", errs());
  return M;
}

TEST(EntryWrapperTest, GetEntrySymbolNameAddsPrefix) {
  EXPECT_EQ(getEntrySymbolName("main"), "feme_cpu_entry_main");
}

TEST(EntryWrapperTest, WrapsWidenedFunction) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      %doubled = mul i32 %tid, 2
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  EntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction("feme_cpu_entry_main");
  ASSERT_TRUE(Wrapper);
  EXPECT_EQ(Wrapper->arg_size(), 1u);
  EXPECT_TRUE(Wrapper->getArg(0)->getType()->isPointerTy());

  Function *Body = M->getFunction("main");
  ASSERT_TRUE(Body);
  EXPECT_EQ(Body->getLinkage(), GlobalValue::InternalLinkage);

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(EntryWrapperTest, LeavesUnwidenedFunctionUnwrapped) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  EntryWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction("feme_cpu_entry_main"));
  EXPECT_TRUE(M->getFunction("main"));
}

} // namespace
