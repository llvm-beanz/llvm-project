//===- BoundResourceNormalizationTest.cpp - Tests for the pass -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/BoundResourceNormalization.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
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
    Err.print("BoundResourceNormalizationTest", errs());
  return M;
}

void runPass(Module &M) {
  ModuleAnalysisManager MAM;
  BoundResourceNormalizationPass().run(M, MAM);
}

/// Returns whether \p F contains a call to a `handlefromheap` intrinsic.
bool hasHandleFromHeapCall(Function &F) {
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getIntrinsicID() == Intrinsic::dx_resource_handlefromheap)
          return true;
  return false;
}

TEST(BoundResourceNormalizationTest, LeavesModuleWithNoBoundHandlesUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      ret void
    }
  )");
  ASSERT_TRUE(M);
  runPass(*M);
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(BoundResourceNormalizationTest, RewritesFiniteRangeToHandleFromHeap) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 4, i32 %idx, ptr null)
      ret void
    }
    declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_TRUE(hasHandleFromHeapCall(*F));
  EXPECT_FALSE(M->getFunction("llvm.dx.resource.handlefrombinding"));
}

TEST(BoundResourceNormalizationTest, RecordsBoundResourceMetadata) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %h = call target("dx.RawBuffer", i8, 1, 0)
          @llvm.dx.resource.handlefrombinding(i32 2, i32 1, i32 8, i32 %idx, ptr null)
      ret void
    }
    declare target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  NamedMDNode *MD = M->getNamedMetadata("feme.cpu.bound_resources");
  ASSERT_TRUE(MD);
  ASSERT_EQ(MD->getNumOperands(), 1u);
  MDNode *Entry = MD->getOperand(0);
  // {name, prefix-size, (space, register, range-size, heap-base)...}.
  ASSERT_EQ(Entry->getNumOperands(), 6u);
  EXPECT_EQ(cast<MDString>(Entry->getOperand(0))->getString(), "main");
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(1))->getZExtValue(),
            8u);
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(2))->getZExtValue(),
            2u); // space
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(3))->getZExtValue(),
            1u); // register
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(4))->getZExtValue(),
            8u); // range size
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(5))->getZExtValue(),
            0u); // heap base
}

TEST(BoundResourceNormalizationTest, OffsetsNativeDynamicHeapIndex) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx, i32 %dyn) {
      %h = call target("dx.RawBuffer", i8, 1, 0)
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 4, i32 %idx, ptr null)
      %h2 = call target("dx.RawBuffer", i8, 1, 0)
          @llvm.dx.resource.handlefromheap(i32 %dyn, i1 false)
      ret void
    }
    declare target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32, i1)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  unsigned NumHeapCalls = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || !CI->getCalledFunction() ||
        CI->getCalledFunction()->getIntrinsicID() !=
            Intrinsic::dx_resource_handlefromheap)
      continue;
    ++NumHeapCalls;
    // Neither call's index operand is the bare, un-offset `%dyn`/`%idx`
    // argument any more -- both go through the clamp-and-offset arithmetic
    // this pass inserts.
    EXPECT_FALSE(isa<Argument>(CI->getArgOperand(0)));
  }
  EXPECT_EQ(NumHeapCalls, 2u);
}

TEST(BoundResourceNormalizationTest, LeavesUnboundedRangeUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %h = call target("dx.RawBuffer", i8, 1, 0)
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 0, i32 %idx, ptr null)
      ret void
    }
    declare target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(hasHandleFromHeapCall(*F));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(BoundResourceNormalizationTest, LeavesConflictingDeclarationUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx, i32 %idx2) {
      %h1 = call target("dx.RawBuffer", i8, 1, 0)
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 4, i32 %idx, ptr null)
      %h2 = call target("dx.RawBuffer", i8, 1, 0)
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 8, i32 %idx2, ptr null)
      ret void
    }
    declare target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(hasHandleFromHeapCall(*F));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

TEST(BoundResourceNormalizationTest, LeavesUnsupportedResourceKindUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      %h = call target("dx.CBuffer", [16 x i8])
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      ret void
    }
    declare target("dx.CBuffer", [16 x i8])
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(hasHandleFromHeapCall(*F));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources"));
}

} // namespace
