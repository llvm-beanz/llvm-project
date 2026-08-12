//===- OptimizerPipelineTest.cpp - Tests for feme::OptimizerPipeline -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Optimizer/OptimizerPipeline.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace llvm;

namespace {

/// Parses \p Assembly into a Module owned by \p Ctx, failing the test (via
/// a fatal gtest assertion in the caller) rather than returning null if it
/// doesn't parse.
std::unique_ptr<Module> parseAssembly(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    ADD_FAILURE() << "failed to parse test IR: " << Err.getMessage();
  return M;
}

// At `-O0`, OptimizerPipeline runs LLVM's minimal O0 pipeline (see
// `buildO0DefaultPipeline`), which does not run the mid-level optimizer:
// an obviously-foldable `add` should survive untouched.
TEST(OptimizerPipelineTest, O0DoesNotConstantFold) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseAssembly(Ctx, R"(
    define i32 @f() {
      %r = add i32 1, 2
      ret i32 %r
    }
  )");
  ASSERT_TRUE(M);

  OptimizerPipeline().run(*M, OptimizerOptions{OptimizationLevel::O0});

  Function *F = M->getFunction("f");
  ASSERT_NE(F, nullptr);
  bool SawAdd = false;
  for (const Instruction &I : F->getEntryBlock())
    if (I.getOpcode() == Instruction::Add)
      SawAdd = true;
  EXPECT_TRUE(SawAdd);
}

// At `-O2`, the standard per-module pipeline runs InstCombine/etc., which
// folds a constant `add` away entirely.
TEST(OptimizerPipelineTest, O2ConstantFolds) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseAssembly(Ctx, R"(
    define i32 @f() {
      %r = add i32 1, 2
      ret i32 %r
    }
  )");
  ASSERT_TRUE(M);

  OptimizerPipeline().run(*M, OptimizerOptions{OptimizationLevel::O2});

  Function *F = M->getFunction("f");
  ASSERT_NE(F, nullptr);
  for (const Instruction &I : F->getEntryBlock())
    EXPECT_NE(I.getOpcode(), Instruction::Add);
}

} // namespace
