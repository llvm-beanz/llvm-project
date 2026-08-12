//===- CFGGenTest.cpp - Tests for generateCFGIR --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/CFGGen.h"

#include "feme/Transforms/CPU/VerifyStructured.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/Scalar/StructurizeCFG.h"
#include "llvm/Transforms/Utils/BreakCriticalEdges.h"
#include "llvm/Transforms/Utils/FixIrreducible.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/UnifyLoopExits.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

/// Runs the same sequence `feme::cpu::PreparePass` does, minus entry-point
/// selection (`generateCFGIR`'s output always has exactly one function).
void structurize(Function &F) {
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

  FunctionPassManager FPM;
  FPM.addPass(PromotePass());
  FPM.addPass(FixIrreduciblePass());
  FPM.addPass(UnifyLoopExitsPass());
  FPM.addPass(StructurizeCFGPass());
  FPM.addPass(BreakCriticalEdgesPass());
  FPM.run(F, FAM);
}

/// Generates with \p Opts, parses the result, restructures it the same way
/// `feme::cpu::PreparePass` does, and asserts both that the parse/verify
/// succeeded and that `verifyStructured` accepts the result -- the same
/// property the `feme-cfg-gen-verify-structured.test` `lit` test checks
/// through the command-line tools, exercised here at the library level with
/// gtest's own assertion/message support.
void expectGeneratesStructuredIR(const CFGGenOptions &Opts) {
  std::string IR = generateCFGIR(Opts);

  LLVMContext Ctx;
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(IR, Err, Ctx);
  ASSERT_TRUE(M) << "seed " << Opts.Seed << " failed to parse: " << [&] {
    std::string S;
    raw_string_ostream OS(S);
    Err.print("CFGGenTest", OS);
    return S;
  }();
  ASSERT_FALSE(verifyModule(*M, &errs()))
      << "seed " << Opts.Seed << " produced an invalid module";

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  structurize(*F);
  EXPECT_TRUE(verifyStructured(*F, &errs())) << "seed " << Opts.Seed;
}

TEST(CFGGenTest, GeneratesStructuredIRAcrossManySeeds) {
  for (uint64_t Seed = 0; Seed < 64; ++Seed) {
    CFGGenOptions Opts;
    Opts.Seed = Seed;
    Opts.MaxDepth = 3;
    Opts.MaxConstructs = 10;
    expectGeneratesStructuredIR(Opts);
  }
}

TEST(CFGGenTest, GeneratesStructuredIRWithUnstructuredEdgesEnabled) {
  for (uint64_t Seed = 0; Seed < 64; ++Seed) {
    CFGGenOptions Opts;
    Opts.Seed = Seed;
    Opts.MaxDepth = 4;
    Opts.MaxConstructs = 16;
    Opts.AllowUnstructured = true;
    expectGeneratesStructuredIR(Opts);
  }
}

TEST(CFGGenTest, IsDeterministicForAGivenSeed) {
  CFGGenOptions Opts;
  Opts.Seed = 1234;
  EXPECT_EQ(generateCFGIR(Opts), generateCFGIR(Opts));
}

TEST(CFGGenTest, DifferentSeedsProduceDifferentOutput) {
  CFGGenOptions OptsA;
  OptsA.Seed = 1;
  CFGGenOptions OptsB;
  OptsB.Seed = 2;
  EXPECT_NE(generateCFGIR(OptsA), generateCFGIR(OptsB));
}

} // namespace
