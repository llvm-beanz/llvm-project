//===- ResourceLoweringTest.cpp - Tests for ResourceLoweringPass ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ResourceLowering.h"

#include "feme/Transforms/CPU/ResourceCalls.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
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
    Err.print("ResourceLoweringTest", errs());
  return M;
}

void runPass(Module &M) {
  ModuleAnalysisManager MAM;
  ResourceLoweringPass().run(M, MAM);
}

TEST(ResourceLoweringTest, LeavesModuleWithNoHandlesUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      ret void
    }
  )");
  ASSERT_TRUE(M);
  runPass(*M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_EQ(F->arg_size(), 0u);
}

TEST(ResourceLoweringTest, CanonicalizesTypedBufferLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(i32 %idx) {
      %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
          @llvm.dx.resource.handlefromheap(i32 3, i1 false)
      %loaded = call {<4 x float>, i1} @llvm.dx.resource.load.typedbuffer(
          target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %idx)
      %val = extractvalue {<4 x float>, i1} %loaded, 0
      ret <4 x float> %val
    }
    declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
        @llvm.dx.resource.handlefromheap(i32, i1)
    declare {<4 x float>, i1} @llvm.dx.resource.load.typedbuffer(
        target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  // The original parameter plus the eight resource/root-constant/image ABI
  // params (roadmap R30 added the trailing image_heap/image_heap_count
  // pair to the original six).
  EXPECT_EQ(F->arg_size(), 9u);

  bool FoundCanonicalCall = false;
  for (const Instruction &I : instructions(F)) {
    if (const auto *CI = dyn_cast<CallInst>(&I)) {
      std::optional<MatchedResourceCall> Matched = matchResourceCall(*CI);
      if (Matched) {
        FoundCanonicalCall = true;
        EXPECT_EQ(Matched->Kind, ResourceCallKind::LoadTyped);
        EXPECT_EQ(Matched->Env.ResourceHeap, F->getArg(1));
      }
    }
  }
  EXPECT_TRUE(FoundCanonicalCall);

  // The raised handle-creation and access declarations are cleaned up once
  // unused.
  EXPECT_FALSE(M->getFunction("llvm.dx.resource.handlefromheap"));
}

TEST(ResourceLoweringTest, RecordsStaticHeapIndexMetadata) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(i32 %idx) {
      %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
          @llvm.dx.resource.handlefromheap(i32 5, i1 false)
      %loaded = call {<4 x float>, i1} @llvm.dx.resource.load.typedbuffer(
          target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %idx)
      %val = extractvalue {<4 x float>, i1} %loaded, 0
      ret <4 x float> %val
    }
    declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
        @llvm.dx.resource.handlefromheap(i32, i1)
    declare {<4 x float>, i1} @llvm.dx.resource.load.typedbuffer(
        target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  NamedMDNode *MD = M->getNamedMetadata("feme.cpu.resources");
  ASSERT_TRUE(MD);
  ASSERT_EQ(MD->getNumOperands(), 1u);
  MDNode *Entry = MD->getOperand(0);
  // {name, root-constant-size, uses-sampler-heap, root-constant-space,
  // root-constant-register, ...heap indices}.
  ASSERT_EQ(Entry->getNumOperands(), 6u);
  EXPECT_EQ(cast<MDString>(Entry->getOperand(0))->getString(), "main");
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(1))->getZExtValue(),
            0u);
  EXPECT_FALSE(
      mdconst::extract<ConstantInt>(Entry->getOperand(2))->getZExtValue());
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(3))->getZExtValue(),
            0u);
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(4))->getZExtValue(),
            0u);
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(5))->getZExtValue(),
            5u);
}

TEST(ResourceLoweringTest, LeavesUnsupportedResourceKindUnchanged) {
  // A constant buffer reached through the heap isn't canonicalized yet (see
  // ResourceLowering.h's Scope note): the function is left entirely alone.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      %h = call target("dx.CBuffer", [16 x i8])
          @llvm.dx.resource.handlefromheap(i32 0, i1 false)
      ret void
    }
    declare target("dx.CBuffer", [16 x i8])
        @llvm.dx.resource.handlefromheap(i32, i1)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_EQ(F->arg_size(), 0u);
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.resources"));
}

TEST(ResourceLoweringTest, LeavesRegisterBoundHandleUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      ret void
    }
    declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_EQ(F->arg_size(), 0u);
}

TEST(ResourceLoweringTest, CanonicalizesStructuredBufferByteOffset) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main() {
      %h = call target("dx.RawBuffer", [16 x i8], 1, 0)
          @llvm.dx.resource.handlefromheap(i32 2, i1 false)
      %loaded = call {float, i1} @llvm.dx.resource.load.rawbuffer(
          target("dx.RawBuffer", [16 x i8], 1, 0) %h, i32 3, i32 4)
      %val = extractvalue {float, i1} %loaded, 0
      ret float %val
    }
    declare target("dx.RawBuffer", [16 x i8], 1, 0)
        @llvm.dx.resource.handlefromheap(i32, i1)
    declare {float, i1} @llvm.dx.resource.load.rawbuffer(
        target("dx.RawBuffer", [16 x i8], 1, 0), i32, i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool FoundCanonicalCall = false;
  for (const Instruction &I : instructions(F)) {
    if (const auto *CI = dyn_cast<CallInst>(&I)) {
      std::optional<MatchedResourceCall> Matched = matchResourceCall(*CI);
      if (Matched) {
        FoundCanonicalCall = true;
        EXPECT_EQ(Matched->Kind, ResourceCallKind::LoadRaw);
        // element index 3 * stride 16 + sub-offset 4 == 52.
        if (auto *OffsetConst = dyn_cast<ConstantInt>(Matched->Offset))
          EXPECT_EQ(OffsetConst->getZExtValue(), 52u);
      }
    }
  }
  EXPECT_TRUE(FoundCanonicalCall);
}

} // namespace
