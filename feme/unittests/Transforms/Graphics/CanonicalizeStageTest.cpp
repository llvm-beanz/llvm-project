//===- CanonicalizeStageTest.cpp - Tests for CanonicalizeStagePass -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/Graphics/CanonicalizeStage.h"

#include "feme/Core/StageOps.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::graphics;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("CanonicalizeStageTest", errs());
  return M;
}

bool run(Module &M) {
  ModuleAnalysisManager MAM;
  PreservedAnalyses PA = CanonicalizeStagePass().run(M, MAM);
  return !PA.areAllPreserved();
}

/// Non-vertex/fragment entry points (here, compute) are left untouched: G0
/// scopes `feme.stage.*` to the vertex/fragment stages only.
TEST(CanonicalizeStageTest, LeavesNonGraphicsStagesAlone) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %v = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 2, i32 0)
      ret void
    }
    declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32)
    attributes #0 = { "feme.shader.stage"="compute" }
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(run(*M));
  Function *F = M->getFunction("main");
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI));
}

/// `loadInput`/`storeOutput` whose signature-ID operand cannot be resolved
/// (e.g. a fragment entry point with no `!feme.signature` at all) is left
/// unmodified, rather than crashing or guessing an ElementID.
TEST(CanonicalizeStageTest, UnresolvableLoadInputIsLeftAlone) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %v = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 2, i32 0)
      ret void
    }
    declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(run(*M));
  Function *F = M->getFunction("main");
  bool SawLoadInput = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (CI->getCalledFunction() &&
          CI->getCalledFunction()->getName().starts_with("dx.op.loadInput"))
        SawLoadInput = true;
  EXPECT_TRUE(SawLoadInput);
}

/// `llvm.dx.discard` (already raised by `feme::dxil::OpRaisingPass`) becomes
/// `feme.stage.discard` in a fragment entry point, needing no signature at
/// all.
TEST(CanonicalizeStageTest, RaisesAlreadyRaisedDiscard) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      call void @llvm.dx.discard(i1 true)
      ret void
    }
    declare void @llvm.dx.discard(i1)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  bool SawDiscard = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      StageOpKind Kind;
      if (isStageOpCall(*CI, &Kind)) {
        EXPECT_EQ(Kind, StageOpKind::Discard);
        SawDiscard = true;
      }
    }
  EXPECT_TRUE(SawDiscard);
}

/// A non-builtin SPIR-V `Input`/`Output` global's load/store rewrites to
/// `feme.stage.input.load`/`output.store`, and an `EntrySignature` is
/// attached recording its `Location`.
TEST(CanonicalizeStageTest, RewritesSPIRVStageIOAndBuildsSignature) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @in_var = external addrspace(7) constant float, !spirv.Decorations !0
    @out_var = external addrspace(8) global float, !spirv.Decorations !1
    define void @main() #0 {
      %v = load float, ptr addrspace(7) @in_var
      store float %v, ptr addrspace(8) @out_var
      ret void
    }
    attributes #0 = { "feme.shader.stage"="fragment" }
    !0 = !{!2}
    !1 = !{!3}
    !2 = !{i32 30, i32 2}
    !3 = !{i32 30, i32 5}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F->getMetadata("feme.signature"));
  unsigned SawLoad = 0, SawStore = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind))
      continue;
    if (Kind == StageOpKind::InputLoad)
      ++SawLoad;
    if (Kind == StageOpKind::OutputStore)
      ++SawStore;
  }
  EXPECT_EQ(SawLoad, 1u);
  EXPECT_EQ(SawStore, 1u);
}

} // namespace
