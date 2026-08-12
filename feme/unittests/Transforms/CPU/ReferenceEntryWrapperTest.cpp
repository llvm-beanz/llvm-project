//===- ReferenceEntryWrapperTest.cpp - Tests for ReferenceEntryWrapperPass ==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ReferenceEntryWrapper.h"

#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/ReferenceLowering.h"

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
    Err.print("ReferenceEntryWrapperTest", errs());
  return M;
}

TEST(ReferenceEntryWrapperTest, BuildsTheExportedEntrySymbol) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %tid = call i32 @llvm.dx.thread.id(i32 0)
      ret void
    }
    declare i32 @llvm.dx.thread.id(i32)
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  ReferenceLoweringPass().run(*M, MAM);
  ReferenceEntryWrapperPass().run(*M, MAM);

  Function *Wrapper = M->getFunction(getEntrySymbolName("main"));
  ASSERT_TRUE(Wrapper);
  EXPECT_EQ(Wrapper->arg_size(), 1u);
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

TEST(ReferenceEntryWrapperTest, DoesNotWrapAFunctionRejectedByLowering) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %lane = call i32 @llvm.dx.wave.getlaneindex()
      ret void
    }
    declare i32 @llvm.dx.wave.getlaneindex()
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  ModuleAnalysisManager MAM;
  ReferenceLoweringPass().run(*M, MAM);
  ReferenceEntryWrapperPass().run(*M, MAM);

  EXPECT_FALSE(M->getFunction(getEntrySymbolName("main")));
}

} // namespace
