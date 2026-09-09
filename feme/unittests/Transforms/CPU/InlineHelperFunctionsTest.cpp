//===- InlineHelperFunctionsTest.cpp - Tests for InlineHelperFunctionsPass ==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/InlineHelperFunctions.h"

#include "feme/Core/ShaderStage.h"

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("InlineHelperFunctionsTest", errs());
  return M;
}

// `llvm::AlwaysInlinerPass` (which `InlineHelperFunctionsPass` delegates
// to) queries function/CGSCC-level analyses through the `ModuleAnalysisManager`'s
// proxy, so -- exactly like the real CPU pipeline's own `MAM` setup in
// Pipeline.cpp -- every analysis manager tier needs to be built and
// cross-registered, not just a bare `ModuleAnalysisManager`.
PreservedAnalyses runPass(Module &M) {
  PassBuilder PB;
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
  return InlineHelperFunctionsPass().run(M, MAM);
}

// Roadmap L76b: a GLSL/glslang-sourced module's user-defined helper
// function -- unlike an HLSL/DXIL-sourced one's, always fully inlined by
// `dxc` before this pipeline ever sees it -- can survive as its own
// separate, uninlined `llvm::Function`; every later CPU-pipeline pass only
// walks the one function `feme::isShaderEntryPoint` flags, so calling it
// (rather than having its body inlined into the entry point) previously
// left it completely untransformed by everything downstream. This test
// covers the base case: a single, non-recursive helper called once from the
// entry point disappears entirely, its body ending up inlined into the
// caller.
TEST(InlineHelperFunctionsTest, InlinesSingleHelperCall) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @helper(float %x) {
      %r = fadd float %x, 1.0
      ret float %r
    }
    define void @main(ptr %out) #0 {
      %v = call float @helper(float 2.0)
      store float %v, ptr %out
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  // The helper function itself should be gone (inlined away, then removed
  // by `GlobalDCEPass` as callerless), leaving only the entry point.
  EXPECT_FALSE(M->getFunction("helper"));
  Function *Main = M->getFunction("main");
  ASSERT_TRUE(Main);
  for (const Instruction &I : instructions(Main))
    EXPECT_FALSE(isa<CallInst>(I))
        << "expected no surviving call after inlining: " << I;
}

// A helper function called from more than one call site (e.g. a GLSL
// shader's own helper invoked once per finite-difference sample, as
// roadmap L76b's own real repro does for `dPdx`/`dPdy` reconstruction)
// must have every one of its call sites inlined, not just the first.
TEST(InlineHelperFunctionsTest, InlinesEveryCallSiteOfAMultiplyCalledHelper) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @helper(float %x) {
      %r = fmul float %x, 2.0
      ret float %r
    }
    define void @main(ptr %out0, ptr %out1, ptr %out2) #0 {
      %v0 = call float @helper(float 1.0)
      store float %v0, ptr %out0
      %v1 = call float @helper(float 2.0)
      store float %v1, ptr %out1
      %v2 = call float @helper(float 3.0)
      store float %v2, ptr %out2
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  EXPECT_FALSE(M->getFunction("helper"));
  Function *Main = M->getFunction("main");
  ASSERT_TRUE(Main);
  for (const Instruction &I : instructions(Main))
    EXPECT_FALSE(isa<CallInst>(I))
        << "expected no surviving call after inlining: " << I;
}

// A helper that itself calls another (non-entry-point) helper -- a nested
// call chain -- must have both levels fully inlined into the entry point,
// not just the outermost call.
TEST(InlineHelperFunctionsTest, InlinesNestedHelperCalls) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @inner(float %x) {
      %r = fadd float %x, 1.0
      ret float %r
    }
    define float @outer(float %x) {
      %r = call float @inner(float %x)
      %r2 = fmul float %r, 2.0
      ret float %r2
    }
    define void @main(ptr %out) #0 {
      %v = call float @outer(float 3.0)
      store float %v, ptr %out
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  EXPECT_FALSE(M->getFunction("inner"));
  EXPECT_FALSE(M->getFunction("outer"));
  Function *Main = M->getFunction("main");
  ASSERT_TRUE(Main);
  for (const Instruction &I : instructions(Main))
    EXPECT_FALSE(isa<CallInst>(I))
        << "expected no surviving call after inlining: " << I;
}

// A module with no helper function at all (the common case for an
// HLSL/DXIL-sourced module, which `dxc` always fully inlines before this
// pipeline ever sees it) is left completely unchanged: no spurious
// transformation, and `PreservedAnalyses::all()` reported so callers don't
// pay for invalidating analyses that were never touched.
TEST(InlineHelperFunctionsTest, NoOpWhenNoHelperFunctionExists) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr %out) #0 {
      store float 1.0, ptr %out
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  PreservedAnalyses PA = runPass(*M);
  EXPECT_TRUE(PA.areAllPreserved());

  Function *Main = M->getFunction("main");
  ASSERT_TRUE(Main);
  EXPECT_EQ(Main->size(), 1u);
}

} // namespace
