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

// Pins a fixed seed's exact output (rather than merely its shape) so that a
// future change accidentally reintroducing `std::uniform_real_distribution`/
// `std::uniform_int_distribution` (see CFGGen.cpp's `chance`/`randInt` for
// why those are hand-rolled instead: their own algorithm is unspecified by
// the standard, so libc++ and libstdc++ draw different values from the same
// seeded `std::mt19937_64`) is caught here rather than only by a
// platform-dependent `feme-run-differential.py` failure.
TEST(CFGGenTest, MatchesGoldenOutputForAFixedSeed) {
  CFGGenOptions Opts;
  Opts.Seed = 1234;
  Opts.MaxDepth = 1;
  Opts.MaxConstructs = 1;
  Opts.AllowDivergent = true;
  Opts.AllowLoops = false;
  Opts.AllowUnstructured = false;

  EXPECT_EQ(generateCFGIR(Opts),
            "define void @main() #0 {\n"
            "entry.0:\n"
            "  %acc = alloca i32\n"
            "  store i32 0, ptr %acc\n"
            "  %tid = call i32 @llvm.dx.thread.id(i32 0)\n"
            "  %gid = call i32 @llvm.dx.group.id(i32 0)\n"
            "  %h = call target(\"dx.RawBuffer\", i8, 1, 0) "
            "@llvm.dx.resource.handlefromheap(i32 0, i1 false)\n"
            "  %t0 = load i32, ptr %acc\n"
            "  %t1 = mul i32 %t0, 2654435761\n"
            "  %t2 = add i32 %t1, 0\n"
            "  store i32 %t2, ptr %acc\n"
            "  %t3 = load i32, ptr %acc\n"
            "  %t4 = mul i32 %t3, 2654435761\n"
            "  %t5 = add i32 %t4, 1\n"
            "  store i32 %t5, ptr %acc\n"
            "  %t6 = and i32 %gid, 3\n"
            "  %t7 = icmp eq i32 %t6, 3\n"
            "  br i1 %t7, label %if.then.1, label %if.else.2\n"
            "if.then.1:\n"
            "  %t8 = load i32, ptr %acc\n"
            "  %t9 = mul i32 %t8, 2654435761\n"
            "  %t10 = add i32 %t9, 2\n"
            "  store i32 %t10, ptr %acc\n"
            "  br label %if.end.3\n"
            "if.else.2:\n"
            "  %t11 = load i32, ptr %acc\n"
            "  %t12 = mul i32 %t11, 2654435761\n"
            "  %t13 = add i32 %t12, 3\n"
            "  store i32 %t13, ptr %acc\n"
            "  br label %if.end.3\n"
            "if.end.3:\n"
            "  %t14 = load i32, ptr %acc\n"
            "  %t15 = mul i32 %tid, 4\n"
            "  call void @llvm.dx.resource.store.rawbuffer.i32(target(\"dx."
            "RawBuffer\", i8, 1, 0) %h, i32 %t15, i32 poison, i32 %t14)\n"
            "  ret void\n"
            "}\n"
            "declare i32 @llvm.dx.thread.id(i32)\n"
            "declare i32 @llvm.dx.group.id(i32)\n"
            "declare target(\"dx.RawBuffer\", i8, 1, 0) "
            "@llvm.dx.resource.handlefromheap(i32, i1)\n"
            "declare void @llvm.dx.resource.store.rawbuffer.i32(\n"
            "    target(\"dx.RawBuffer\", i8, 1, 0), i32, i32, i32)\n"
            "attributes #0 = { \"hlsl.shader\"=\"compute\" "
            "\"hlsl.numthreads\"=\"4,1,1\" }\n");
}

} // namespace
