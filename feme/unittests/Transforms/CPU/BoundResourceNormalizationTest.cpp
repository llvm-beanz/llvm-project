//===- BoundResourceNormalizationTest.cpp - Tests for the pass -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/BoundResourceNormalization.h"

#include "feme/Target/CPU/ResourceInfo.h"

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
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources.v2"));
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

  NamedMDNode *MD = M->getNamedMetadata("feme.cpu.bound_resources.v2");
  ASSERT_TRUE(MD);
  ASSERT_EQ(MD->getNumOperands(), 1u);
  MDNode *Entry = MD->getOperand(0);
  // {name, resource-/image-/sampler-prefix-size,
  //  (space, register, range-size, heap-base, class, namespace)...}.
  ASSERT_EQ(Entry->getNumOperands(), 10u);
  EXPECT_EQ(cast<MDString>(Entry->getOperand(0))->getString(), "main");
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(1))->getZExtValue(),
            8u);
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(2))->getZExtValue(),
            0u); // image-heap prefix size: this pass binds no image
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(3))->getZExtValue(),
            0u); // sampler-heap prefix size
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(4))->getZExtValue(),
            2u); // space
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(5))->getZExtValue(),
            1u); // register
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(6))->getZExtValue(),
            8u); // range size
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(7))->getZExtValue(),
            0u); // heap base
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(8))->getZExtValue(),
            static_cast<uint64_t>(feme::cpu::BoundResourceClass::Buffer));
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(9))->getZExtValue(),
            static_cast<uint64_t>(feme::cpu::BoundResourceNamespace::UAV));
}

TEST(BoundResourceNormalizationTest, KeepsSRVAndUAVNamespacesDistinct) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %srv = call target("dx.TypedBuffer", i32, 0, 0, 0)
        @llvm.dx.resource.handlefrombinding.srv(i32 7, i32 3, i32 2, i32 %idx, ptr null)
      %uav = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding.uav(i32 7, i32 3, i32 2, i32 %idx, ptr null)
      ret void
    }
    declare target("dx.TypedBuffer", i32, 0, 0, 0)
      @llvm.dx.resource.handlefrombinding.srv(i32, i32, i32, i32, ptr)
    declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding.uav(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);
  auto *MD = M->getNamedMetadata("feme.cpu.bound_resources.v2");
  ASSERT_TRUE(MD);
  auto *Entry = MD->getOperand(0);
  ASSERT_EQ(Entry->getNumOperands(), 16u);
  auto Get = [&](unsigned I) {
    return mdconst::extract<ConstantInt>(Entry->getOperand(I))->getZExtValue();
  };
  EXPECT_EQ(Get(1), 4u);
  EXPECT_EQ(Get(7), 0u);
  EXPECT_EQ(Get(9), uint32_t(feme::cpu::BoundResourceNamespace::SRV));
  EXPECT_EQ(Get(13), 2u);
  EXPECT_EQ(Get(15), uint32_t(feme::cpu::BoundResourceNamespace::UAV));
  EXPECT_FALSE(M->getFunction("llvm.dx.resource.handlefrombinding.srv"));
  EXPECT_FALSE(M->getFunction("llvm.dx.resource.handlefrombinding.uav"));
}

TEST(BoundResourceNormalizationTest, RejectsOverflowingCombinedPrefix) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %srv = call target("dx.RawBuffer", i8, 0, 0)
        @llvm.dx.resource.handlefrombinding.srv(i32 0, i32 0, i32 2147483648, i32 %idx, ptr null)
      %uav = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding.uav(i32 0, i32 0, i32 2147483648, i32 %idx, ptr null)
      ret void
    }
    declare target("dx.RawBuffer", i8, 0, 0)
      @llvm.dx.resource.handlefrombinding.srv(i32, i32, i32, i32, ptr)
    declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding.uav(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);
  EXPECT_FALSE(hasHandleFromHeapCall(*M->getFunction("main")));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources.v2"));
}

TEST(BoundResourceNormalizationTest, SeparatesImageSamplerAndBufferPrefixes) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %buffer = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding.buffer(i32 7, i32 0, i32 4, i32 %idx, ptr null)
      %srv = call target("dx.Texture", <4 x i32>, 0, 0, 0, 2)
        @llvm.dx.resource.handlefrombinding.srv(i32 7, i32 3, i32 2, i32 %idx, ptr null)
      %uav = call target("dx.Texture", <4 x i32>, 1, 0, 0, 2)
        @llvm.dx.resource.handlefrombinding.uav(i32 7, i32 3, i32 3, i32 %idx, ptr null)
      %sampler = call target("dx.Sampler", 0)
        @llvm.dx.resource.handlefrombinding.sampler(i32 7, i32 3, i32 6, i32 %idx, ptr null)
      %native_buffer = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap.buffer(i32 5)
      %native_image = call target("dx.Texture", <4 x i32>, 0, 0, 0, 2)
        @llvm.dx.resource.handlefromheap.image(i32 5)
      %native_sampler = call target("dx.Sampler", 0)
        @llvm.dx.resource.handlefromheap.sampler(i32 5)
      ret void
    }
    declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding.buffer(i32, i32, i32, i32, ptr)
    declare target("dx.Texture", <4 x i32>, 0, 0, 0, 2)
      @llvm.dx.resource.handlefrombinding.srv(i32, i32, i32, i32, ptr)
    declare target("dx.Texture", <4 x i32>, 1, 0, 0, 2)
      @llvm.dx.resource.handlefrombinding.uav(i32, i32, i32, i32, ptr)
    declare target("dx.Sampler", 0)
      @llvm.dx.resource.handlefrombinding.sampler(i32, i32, i32, i32, ptr)
    declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap.buffer(i32)
    declare target("dx.Texture", <4 x i32>, 0, 0, 0, 2)
      @llvm.dx.resource.handlefromheap.image(i32)
    declare target("dx.Sampler", 0)
      @llvm.dx.resource.handlefromheap.sampler(i32)
  )");
  ASSERT_TRUE(M);
  runPass(*M);
  auto *MD = M->getNamedMetadata("feme.cpu.bound_resources.v2");
  ASSERT_TRUE(MD);
  auto *Entry = MD->getOperand(0);
  ASSERT_EQ(Entry->getNumOperands(), 28u);
  auto Get = [&](unsigned I) {
    return mdconst::extract<ConstantInt>(Entry->getOperand(I))->getZExtValue();
  };
  EXPECT_EQ(Get(1), 4u);
  EXPECT_EQ(Get(2), 5u);
  EXPECT_EQ(Get(3), 6u);
  EXPECT_EQ(Get(8), uint32_t(BoundResourceClass::Buffer));
  EXPECT_EQ(Get(14), uint32_t(BoundResourceClass::Sampler));
  EXPECT_EQ(Get(15), uint32_t(BoundResourceNamespace::Unspecified));
  EXPECT_EQ(Get(20), uint32_t(BoundResourceClass::Image));
  EXPECT_EQ(Get(21), uint32_t(BoundResourceNamespace::SRV));
  EXPECT_EQ(Get(25), 2u);
  EXPECT_EQ(Get(26), uint32_t(BoundResourceClass::Image));
  EXPECT_EQ(Get(27), uint32_t(BoundResourceNamespace::UAV));
  unsigned HeapCalls = 0;
  for (Instruction &I : instructions(M->getFunction("main"))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    EXPECT_NE(CI->getCalledFunction()->getIntrinsicID(),
              Intrinsic::dx_resource_handlefrombinding);
    if (CI->getCalledFunction()->getIntrinsicID() !=
        Intrinsic::dx_resource_handlefromheap)
      continue;
    ++HeapCalls;
    if (CI->getName() == "native_buffer")
      EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(), 9u);
    if (CI->getName() == "native_image")
      EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(), 10u);
    if (CI->getName() == "native_sampler")
      EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(), 11u);
  }
  EXPECT_EQ(HeapCalls, 7u);
}

TEST(BoundResourceNormalizationTest, ImageOverflowDoesNotConsumeBufferPrefix) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %buffer = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding.buffer(i32 0, i32 0, i32 2, i32 %idx, ptr null)
      %srv = call target("dx.Texture", <4 x i32>, 0, 0, 0, 2)
        @llvm.dx.resource.handlefrombinding.srv(i32 0, i32 3, i32 2147483648, i32 %idx, ptr null)
      %uav = call target("dx.Texture", <4 x i32>, 1, 0, 0, 2)
        @llvm.dx.resource.handlefrombinding.uav(i32 0, i32 3, i32 2147483648, i32 %idx, ptr null)
      %bad_registers = call target("dx.Texture", <4 x i32>, 0, 0, 0, 2)
        @llvm.dx.resource.handlefrombinding.srv(i32 1, i32 -1, i32 2, i32 %idx, ptr null)
      ret void
    }
    declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding.buffer(i32, i32, i32, i32, ptr)
    declare target("dx.Texture", <4 x i32>, 0, 0, 0, 2)
      @llvm.dx.resource.handlefrombinding.srv(i32, i32, i32, i32, ptr)
    declare target("dx.Texture", <4 x i32>, 1, 0, 0, 2)
      @llvm.dx.resource.handlefrombinding.uav(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);
  auto *MD = M->getNamedMetadata("feme.cpu.bound_resources.v2");
  ASSERT_TRUE(MD);
  auto *Entry = MD->getOperand(0);
  ASSERT_EQ(Entry->getNumOperands(), 10u);
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(1))->getZExtValue(),
            2u);
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(2))->getZExtValue(),
            0u);
  unsigned BoundCalls = 0;
  for (Instruction &I : instructions(M->getFunction("main")))
    if (auto *CI = dyn_cast<CallInst>(&I))
      BoundCalls += CI->getCalledFunction()->getIntrinsicID() ==
                    Intrinsic::dx_resource_handlefrombinding;
  EXPECT_EQ(BoundCalls, 3u);
}

TEST(BoundResourceNormalizationTest,
     RegisterOverflowDoesNotConsumeImagePrefix) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define void @main(i32 %idx) {
      %image = call target("dx.MSTexture", <4 x float>, 0, 4, 0, 3)
        @llvm.dx.resource.handlefrombinding.ms(i32 0, i32 0, i32 2, i32 %idx, ptr null)
      %sampler = call target("dx.Sampler", 1)
        @llvm.dx.resource.handlefrombinding.sampler(i32 0, i32 3, i32 1, i32 %idx, ptr null)
      %bad_registers = call target("dx.Texture", <4 x i32>, 0, 0, 0, 2)
        @llvm.dx.resource.handlefrombinding.srv(i32 0, i32 -1, i32 2, i32 %idx, ptr null)
      ret void
    }
    declare target("dx.MSTexture", <4 x float>, 0, 4, 0, 3)
      @llvm.dx.resource.handlefrombinding.ms(i32, i32, i32, i32, ptr)
    declare target("dx.Sampler", 1)
      @llvm.dx.resource.handlefrombinding.sampler(i32, i32, i32, i32, ptr)
    declare target("dx.Texture", <4 x i32>, 0, 0, 0, 2)
      @llvm.dx.resource.handlefrombinding.srv(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);
  auto *MD = M->getNamedMetadata("feme.cpu.bound_resources.v2");
  ASSERT_TRUE(MD);
  auto *Entry = MD->getOperand(0);
  ASSERT_EQ(Entry->getNumOperands(), 16u);
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(1))->getZExtValue(),
            0u);
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(2))->getZExtValue(),
            2u);
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(3))->getZExtValue(),
            1u);
  unsigned BoundCalls = 0, HeapCalls = 0;
  for (Instruction &I : instructions(M->getFunction("main")))
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      BoundCalls += CI->getCalledFunction()->getIntrinsicID() ==
                    Intrinsic::dx_resource_handlefrombinding;
      HeapCalls += CI->getCalledFunction()->getIntrinsicID() ==
                   Intrinsic::dx_resource_handlefromheap;
    }
  EXPECT_EQ(BoundCalls, 1u);
  EXPECT_EQ(HeapCalls, 2u);
}

TEST(BoundResourceNormalizationTest, OffsetsNativeDynamicHeapIndex) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %idx, i32 %dyn) {
      %h = call target("dx.RawBuffer", i8, 1, 0)
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 4, i32 %idx, ptr null)
      %h2 = call target("dx.RawBuffer", i8, 1, 0)
          @llvm.dx.resource.handlefromheap(i32 %dyn)
      ret void
    }
    declare target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefromheap(i32)
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
      %image = call target("dx.Texture", <4 x i32>, 0, 0, 0, 2)
          @llvm.dx.resource.handlefrombinding.image(i32 0, i32 0, i32 0, i32 %idx, ptr null)
      %sampler = call target("dx.Sampler", 0)
          @llvm.dx.resource.handlefrombinding.sampler(i32 0, i32 0, i32 -1, i32 %idx, ptr null)
      ret void
    }
    declare target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare target("dx.Texture", <4 x i32>, 0, 0, 0, 2)
        @llvm.dx.resource.handlefrombinding.image(i32, i32, i32, i32, ptr)
    declare target("dx.Sampler", 0)
        @llvm.dx.resource.handlefrombinding.sampler(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(hasHandleFromHeapCall(*F));
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources.v2"));
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
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources.v2"));
}

TEST(BoundResourceNormalizationTest, PreservesInferredInlineConstantBinding) {
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
  EXPECT_FALSE(M->getNamedMetadata("feme.cpu.bound_resources.v2"));
}

TEST(BoundResourceNormalizationTest, KeepsCBVSRVAndUAVNamespacesDistinct) {
  LLVMContext C;
  auto M = parseIR(C, R"(
    define void @main(i32 %idx) {
      %a = call target("dx.CBuffer", [16 x i8])
        @llvm.dx.resource.handlefrombinding.cb(i32 7, i32 3, i32 2, i32 %idx, ptr null)
      %b = call target("dx.CBuffer", [16 x i8])
        @llvm.dx.resource.handlefrombinding.cb(i32 7, i32 4, i32 1, i32 0, ptr null)
      %srv = call target("dx.RawBuffer", i8, 0, 0)
        @llvm.dx.resource.handlefrombinding.srv(i32 7, i32 3, i32 1, i32 0, ptr null)
      %uav = call target("dx.RawBuffer", i8, 1, 0)
        @llvm.dx.resource.handlefrombinding.uav(i32 7, i32 3, i32 1, i32 0, ptr null)
      ret void
    }
    declare target("dx.CBuffer", [16 x i8])
      @llvm.dx.resource.handlefrombinding.cb(i32, i32, i32, i32, ptr)
    declare target("dx.RawBuffer", i8, 0, 0)
      @llvm.dx.resource.handlefrombinding.srv(i32, i32, i32, i32, ptr)
    declare target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding.uav(i32, i32, i32, i32, ptr)
    !feme.cpu.resources = !{!0}
    !0 = !{!"main", i32 0, i1 false, i32 0, i32 0, i32 0}
  )");
  ASSERT_TRUE(M);
  runPass(*M);
  auto Info = ResourceInfo::fromModule(*M, "main");
  ASSERT_TRUE(Info);
  ASSERT_EQ(Info->BoundRanges.size(), 4u);
  EXPECT_EQ(Info->ReservedResourceHeapSize, 5u);
  EXPECT_EQ(Info->BoundRanges[0].Namespace, BoundResourceNamespace::SRV);
  EXPECT_EQ(Info->BoundRanges[1].Namespace, BoundResourceNamespace::UAV);
  EXPECT_EQ(Info->BoundRanges[2].Namespace, BoundResourceNamespace::CBV);
  EXPECT_EQ(Info->BoundRanges[2].RangeSize, 2u);
  EXPECT_EQ(Info->BoundRanges[3].Namespace, BoundResourceNamespace::CBV);
  EXPECT_TRUE(hasHandleFromHeapCall(*M->getFunction("main")));
}

TEST(BoundResourceNormalizationTest, DescriptorModeDoesNotInferInlineCBV) {
  LLVMContext C;
  auto M = parseIR(C, R"(
    define void @main() {
      %h = call target("dx.CBuffer", [16 x i8])
        @llvm.dx.resource.handlefrombinding(i32 7, i32 3, i32 1, i32 0, ptr null)
      ret void
    }
    declare target("dx.CBuffer", [16 x i8])
      @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  ModuleAnalysisManager Analyses;
  BoundResourceNormalizationPass(false).run(*M, Analyses);
  EXPECT_TRUE(hasHandleFromHeapCall(*M->getFunction("main")));
}

} // namespace
