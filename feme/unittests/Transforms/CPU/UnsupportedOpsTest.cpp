//===- UnsupportedOpsTest.cpp - Tests for checkSupportedRaisedOps --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/UnsupportedOps.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("UnsupportedOpsTest", errs());
  return M;
}

TEST(UnsupportedOpsTest, AcceptsOrdinaryModule) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      ret void
    }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
}

TEST(UnsupportedOpsTest, AdmitsOnlySupportedFragmentComparison) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define float @main(<2 x i32> %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32 0)
      %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
      %a = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> zeroinitializer, float 0.5, <2 x i32> zeroinitializer)
      %b = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> zeroinitializer, float 0.5, <2 x i32> zeroinitializer, float 0.0)
      %r = fadd float %a, %b
      ret float %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32)
    declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
    declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
    declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  Function *Main = M->getFunction("main");
  CallInst *Sample = nullptr;
  for (Instruction &I : instructions(Main))
    if (I.getName() == "a")
      Sample = dyn_cast<CallInst>(&I);
  ASSERT_NE(Sample, nullptr);
  Value *Offset = Sample->getArgOperand(4);
  Sample->setArgOperand(4, Main->getArg(0));
  EXPECT_THAT_ERROR(
      checkSupportedRaisedOps(*M),
      Failed<StringError>(testing::Property(
          &StringError::getMessage,
          testing::HasSubstr(
              "1D/2D/cube (including array) fragment comparison sampling"))));
  Sample->setArgOperand(4, Offset);
  Main->addFnAttr("feme.shader.stage", "compute");
  EXPECT_THAT_ERROR(
      checkSupportedRaisedOps(*M),
      Failed<StringError>(testing::Property(
          &StringError::getMessage,
          testing::HasSubstr("other stages/shapes is not yet supported"))));
  Main->removeFnAttr("feme.shader.stage");
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
}

TEST(UnsupportedOpsTest, AdmitsOnlySupportedFragmentArrayComparison) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define float @main(<2 x i32> %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32 0)
      %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
      %r = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <3 x float> zeroinitializer, float 0.5, <2 x i32> zeroinitializer, float 2.0)
      ret float %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32)
    declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
    declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7), target("dx.Sampler", 1), <3 x float>, float, <2 x i32>, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  Function *Main = M->getFunction("main");
  auto *Sample =
      dyn_cast<CallInst>(Main->back().getTerminator()->getOperand(0));
  ASSERT_NE(Sample, nullptr);
  Value *Offset = Sample->getArgOperand(4);
  Sample->setArgOperand(4, Main->getArg(0));
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  Sample->setArgOperand(4, Offset);
  Main->addFnAttr("feme.shader.stage", "compute");
  EXPECT_THAT_ERROR(
      checkSupportedRaisedOps(*M),
      Failed<StringError>(testing::Property(
          &StringError::getMessage,
          testing::HasSubstr("other stages/shapes is not yet supported"))));
  Main->removeFnAttr("feme.shader.stage");
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
}

TEST(UnsupportedOpsTest, AdmitsOnlySupportedFragment1DComparisons) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define float @plain(i32 %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32 0)
      %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
      %r = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, float 0.0, float 0.5, i32 -1)
      ret float %r
    }
    define float @array(i32 %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32 0)
      %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
      %r = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %t, target("dx.Sampler", 1) %s, <2 x float> zeroinitializer, float 0.5, i32 1, float 2.0)
      ret float %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32)
    declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
    declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1), target("dx.Sampler", 1), float, float, i32)
    declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6), target("dx.Sampler", 1), <2 x float>, float, i32, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  for (StringRef Name : {"plain", "array"}) {
    SCOPED_TRACE(Name.str());
    Function *F = M->getFunction(Name);
    auto *Sample = dyn_cast<CallInst>(F->back().getTerminator()->getOperand(0));
    ASSERT_NE(Sample, nullptr);
    Value *Offset = Sample->getArgOperand(4);
    Sample->setArgOperand(4, F->getArg(0));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(4, Offset);
    F->addFnAttr("feme.shader.stage", "compute");
    EXPECT_THAT_ERROR(
        checkSupportedRaisedOps(*M),
        Failed<StringError>(testing::Property(
            &StringError::getMessage,
            testing::HasSubstr("other stages/shapes is not yet supported"))));
    F->removeFnAttr("feme.shader.stage");
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    F->addFnAttr("feme.shader.stage", "fragment");
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  }
}

TEST(UnsupportedOpsTest, AdmitsOnlySupportedFragmentCubeComparisons) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define float @cube(i32 %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32 0)
      %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
      %r = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5) %t, target("dx.Sampler", 1) %s, <3 x float> zeroinitializer, float 0.5, i32 poison)
      ret float %r
    }
    define float @array(i32 %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_9t(i32 0)
      %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
      %r = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9) %t, target("dx.Sampler", 1) %s, <4 x float> zeroinitializer, float 0.5, i32 0, float 2.0)
      ret float %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_9t(i32)
    declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
    declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5), target("dx.Sampler", 1), <3 x float>, float, i32)
    declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9), target("dx.Sampler", 1), <4 x float>, float, i32, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  for (StringRef Name : {"cube", "array"}) {
    SCOPED_TRACE(Name.str());
    Function *F = M->getFunction(Name);
    auto *Sample = dyn_cast<CallInst>(F->back().getTerminator()->getOperand(0));
    ASSERT_NE(Sample, nullptr);
    Value *Offset = Sample->getArgOperand(4);
    Sample->setArgOperand(4, ConstantInt::get(Offset->getType(), 1));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(4, F->getArg(0));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(4, UndefValue::get(Offset->getType()));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
    Sample->setArgOperand(4, Offset);
    F->addFnAttr("feme.shader.stage", "compute");
    EXPECT_THAT_ERROR(
        checkSupportedRaisedOps(*M),
        Failed<StringError>(testing::Property(
            &StringError::getMessage,
            testing::HasSubstr("other stages/shapes is not yet supported"))));
    F->removeFnAttr("feme.shader.stage");
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    F->addFnAttr("feme.shader.stage", "fragment");
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  }
}

TEST(UnsupportedOpsTest, AdmitsOnlySupportedFragmentBiasedSampling) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 1.0, <2 x i32> <i32 -1, i32 2>, float 0.0)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  Function *F = M->getFunction("main");
  auto *Sample = cast<CallInst>(F->back().getTerminator()->getOperand(0));
  Value *Offset = Sample->getArgOperand(4);
  Sample->setArgOperand(4, F->getArg(0));
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  Sample->setArgOperand(4, UndefValue::get(Offset->getType()));
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  Sample->setArgOperand(4, Offset);
  F->addFnAttr("feme.shader.stage", "compute");
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  F->removeFnAttr("feme.shader.stage");
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  F->addFnAttr("feme.shader.stage", "fragment");
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
}

TEST(UnsupportedOpsTest, AdmitsOnlySupportedFragmentArrayBiasedSampling) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 1.0, <2 x i32> <i32 -1, i32 2>, float 0.0)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  Function *F = M->getFunction("main");
  auto *Sample = cast<CallInst>(F->back().getTerminator()->getOperand(0));
  Value *Offset = Sample->getArgOperand(4);
  Sample->setArgOperand(4, F->getArg(0));
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  Sample->setArgOperand(4, UndefValue::get(Offset->getType()));
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  Sample->setArgOperand(4, Offset);
  F->addFnAttr("feme.shader.stage", "compute");
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  F->removeFnAttr("feme.shader.stage");
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  F->addFnAttr("feme.shader.stage", "fragment");
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
}

TEST(UnsupportedOpsTest, AdmitsOnlySupportedFragmentCubeBiasedSampling) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define float @cube(i32 %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 1.0, i32 poison)
      ret float %r
    }
    define float @array(i32 %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call float @llvm.dx.resource.samplebias.clamp.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %t, target("dx.Sampler", 0) %s, <4 x float> zeroinitializer, float -1.0, i32 0, float 2.0)
      ret float %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, float, i32)
    declare float @llvm.dx.resource.samplebias.clamp.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, float, i32, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  for (StringRef Name : {"cube", "array"}) {
    SCOPED_TRACE(Name.str());
    Function *F = M->getFunction(Name);
    auto *Sample = cast<CallInst>(F->back().getTerminator()->getOperand(0));
    Value *Offset = Sample->getArgOperand(4);
    Sample->setArgOperand(4, ConstantInt::get(Offset->getType(), 1));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(4, F->getArg(0));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(4, UndefValue::get(Offset->getType()));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
    Sample->setArgOperand(4, Offset);
    F->addFnAttr("feme.shader.stage", "compute");
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    F->removeFnAttr("feme.shader.stage");
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    F->addFnAttr("feme.shader.stage", "fragment");
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  }
}

TEST(UnsupportedOpsTest, AdmitsOnlySupportedFragment1DBiasedSampling) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define float @plain(i32 %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.5, float 1.0, i32 -1)
      ret float %r
    }
    define <2 x float> @array(i32 %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float -1.0, i32 1, float 2.0)
      ret <2 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, i32)
    declare <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, i32, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  for (StringRef Name : {"plain", "array"}) {
    SCOPED_TRACE(Name.str());
    Function *F = M->getFunction(Name);
    auto *Sample = cast<CallInst>(F->back().getTerminator()->getOperand(0));
    Value *Offset = Sample->getArgOperand(4);
    Sample->setArgOperand(4, F->getArg(0));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(4, UndefValue::get(Offset->getType()));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(4, PoisonValue::get(Offset->getType()));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(4, Offset);
    F->addFnAttr("feme.shader.stage", "compute");
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    F->removeFnAttr("feme.shader.stage");
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    F->addFnAttr("feme.shader.stage", "fragment");
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  }
}

TEST(UnsupportedOpsTest, AdmitsOnlySupportedFragment3DBiasedSampling) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define <3 x float> @main(<3 x i32> %offset) #0 {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call <3 x float> @llvm.dx.resource.samplebias.clamp.v3f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 1.0, <3 x i32> <i32 1, i32 -1, i32 2>, float 0.0)
      ret <3 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare <3 x float> @llvm.dx.resource.samplebias.clamp.v3f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, float, <3 x i32>, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  Function *F = M->getFunction("main");
  auto *Sample = cast<CallInst>(F->back().getTerminator()->getOperand(0));
  Value *Offset = Sample->getArgOperand(4);
  Sample->setArgOperand(4, F->getArg(0));
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  Sample->setArgOperand(4, UndefValue::get(Offset->getType()));
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  Sample->setArgOperand(4, PoisonValue::get(Offset->getType()));
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  for (unsigned I = 0; I < 3; ++I) {
    SmallVector<Constant *, 3> Components(
        3, ConstantInt::get(Type::getInt32Ty(Ctx), 0));
    Components[I] = UndefValue::get(Type::getInt32Ty(Ctx));
    Sample->setArgOperand(4, ConstantVector::get(Components));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  }
  Sample->setArgOperand(4, Offset);
  F->addFnAttr("feme.shader.stage", "compute");
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  F->removeFnAttr("feme.shader.stage");
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
  F->addFnAttr("feme.shader.stage", "fragment");
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
}

TEST(UnsupportedOpsTest, RejectsNonHeapBiasedSamplingInEveryStage) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define float @plain(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
      %r = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 1.0, <2 x i32> zeroinitializer)
      ret float %r
    }
    define float @clamped(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
      %r = call float @llvm.dx.resource.samplebias.clamp.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float -1.0, <2 x i32> zeroinitializer, float 0.0)
      ret float %r
    }
    declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
    declare float @llvm.dx.resource.samplebias.clamp.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>, float)
  )");
  ASSERT_TRUE(M);
  for (StringRef Name : {"plain", "clamped"}) {
    Function *F = M->getFunction(Name);
    for (StringRef Stage : {"", "fragment", "compute"}) {
      SCOPED_TRACE((Name + ": " + Stage).str());
      if (!Stage.empty())
        F->addFnAttr("feme.shader.stage", Stage);
      EXPECT_THAT_ERROR(
          checkSupportedRaisedOps(*M),
          Failed<StringError>(testing::Property(
              &StringError::getMessage,
              testing::HasSubstr("requires heap-backed FP32 "
                                 "1D/2D/3D/cube (including array) fragment "
                                 "biased sampling"))));
    }
    F->eraseFromParent();
  }
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
}

TEST(UnsupportedOpsTest, AdmitsOnlySupported2DGradientSamplingInEveryStage) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %offset, <2 x float> %dx, <2 x float> %dy) {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 -1, i32 2>, float 0.0)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>, float)
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  auto *Sample = cast<CallInst>(F->back().getTerminator()->getOperand(0));
  Value *Offset = Sample->getArgOperand(5);
  for (StringRef Stage : {"", "fragment", "vertex", "compute"}) {
    SCOPED_TRACE(Stage.str());
    if (!Stage.empty())
      F->addFnAttr("feme.shader.stage", Stage);
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
    Sample->setArgOperand(5, F->getArg(0));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(5, UndefValue::get(Offset->getType()));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(5, PoisonValue::get(Offset->getType()));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(5, Offset);
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  }
}

TEST(UnsupportedOpsTest, AdmitsOnlySupportedArrayGradientSamplingInEveryStage) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %offset, <3 x float> %coord, <2 x float> %dx, <2 x float> %dy, float %clamp) {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 -1, i32 2>, float %clamp)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, <2 x float>, <2 x float>, <2 x i32>, float)
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  auto *Sample = cast<CallInst>(F->back().getTerminator()->getOperand(0));
  Value *Offset = Sample->getArgOperand(5);
  for (StringRef Stage : {"", "fragment", "vertex", "compute"}) {
    SCOPED_TRACE(Stage.str());
    if (!Stage.empty())
      F->addFnAttr("feme.shader.stage", Stage);
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
    Sample->setArgOperand(5, F->getArg(0));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(5, UndefValue::get(Offset->getType()));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(5, PoisonValue::get(Offset->getType()));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(
        5, ConstantVector::get({ConstantInt::get(Type::getInt32Ty(Ctx), 1),
                                UndefValue::get(Type::getInt32Ty(Ctx))}));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(5, Offset);
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  }
}

TEST(UnsupportedOpsTest, AdmitsOnlySupportedCubeGradientSamplingInEveryStage) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define float @cube(i32 %offset, <3 x float> %dx, <3 x float> %dy) {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> <float 1.0, float 0.0, float 0.0>, <3 x float> %dx, <3 x float> %dy, i32 poison)
      ret float %r
    }
    define <4 x float> @array(i32 %offset, <3 x float> %dx, <3 x float> %dy, <4 x float> %coord, float %clamp) {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %t, target("dx.Sampler", 0) %s, <4 x float> %coord, <3 x float> %dx, <3 x float> %dy, i32 0, float %clamp)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, i32)
    declare <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, <3 x float>, <3 x float>, i32, float)
  )");
  ASSERT_TRUE(M);
  for (StringRef Name : {"cube", "array"}) {
    Function *F = M->getFunction(Name);
    auto *Sample = cast<CallInst>(F->back().getTerminator()->getOperand(0));
    Type *OffsetTy = Sample->getArgOperand(5)->getType();
    Constant *Offsets[] = {ConstantInt::get(OffsetTy, 0),
                           UndefValue::get(OffsetTy),
                           PoisonValue::get(OffsetTy)};
    for (StringRef Stage : {"", "fragment", "vertex", "compute"}) {
      SCOPED_TRACE((Name + ": " + Stage).str());
      if (!Stage.empty())
        F->addFnAttr("feme.shader.stage", Stage);
      for (Constant *Offset : Offsets) {
        Sample->setArgOperand(5, Offset);
        EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
      }
      Sample->setArgOperand(5, ConstantInt::get(OffsetTy, 1));
      EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
      Sample->setArgOperand(5, F->getArg(0));
      EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
      Sample->setArgOperand(5, PoisonValue::get(OffsetTy));
    }
  }
}

TEST(UnsupportedOpsTest, AdmitsOnlySupported1DGradientSamplingInEveryStage) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define float @plain(i32 %offset, float %dx, float %dy) {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.5, float %dx, float %dy, i32 -1)
      ret float %r
    }
    define <4 x float> @array(i32 %offset, float %dx, float %dy, <2 x float> %coord, float %clamp) {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float %dx, float %dy, i32 1, float %clamp)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32)
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, float, i32)
    declare <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, float, i32, float)
  )");
  ASSERT_TRUE(M);
  for (StringRef Name : {"plain", "array"}) {
    Function *F = M->getFunction(Name);
    auto *Sample = cast<CallInst>(F->back().getTerminator()->getOperand(0));
    Value *Offset = Sample->getArgOperand(5);
    for (StringRef Stage : {"", "fragment", "vertex", "compute"}) {
      SCOPED_TRACE((Name + ": " + Stage).str());
      if (!Stage.empty())
        F->addFnAttr("feme.shader.stage", Stage);
      EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
      Sample->setArgOperand(5, F->getArg(0));
      EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
      Sample->setArgOperand(5, UndefValue::get(Offset->getType()));
      EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
      Sample->setArgOperand(5, PoisonValue::get(Offset->getType()));
      EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
      Sample->setArgOperand(5, Offset);
      EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
    }
  }
}

TEST(UnsupportedOpsTest, AdmitsOnlySupported3DGradientSamplingInEveryStage) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x i32> %offset, <3 x float> %coord, <3 x float> %dx, <3 x float> %dy, float %clamp) {
      %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
      %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
      %r = call <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, <3 x float> %dx, <3 x float> %dy, <3 x i32> <i32 1, i32 -1, i32 2>, float %clamp)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32)
    declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
    declare <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, <3 x i32>, float)
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  auto *Sample = cast<CallInst>(F->back().getTerminator()->getOperand(0));
  Value *Offset = Sample->getArgOperand(5);
  for (StringRef Stage : {"", "fragment", "vertex", "compute"}) {
    SCOPED_TRACE(Stage.str());
    if (!Stage.empty())
      F->addFnAttr("feme.shader.stage", Stage);
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
    Sample->setArgOperand(5, F->getArg(0));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(5, UndefValue::get(Offset->getType()));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    Sample->setArgOperand(5, PoisonValue::get(Offset->getType()));
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    for (unsigned I = 0; I < 3; ++I) {
      SmallVector<Constant *, 3> Components(
          3, ConstantInt::get(Type::getInt32Ty(Ctx), 0));
      Components[I] = UndefValue::get(Type::getInt32Ty(Ctx));
      Sample->setArgOperand(5, ConstantVector::get(Components));
      EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Failed());
    }
    Sample->setArgOperand(5, Offset);
    EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
  }
}

TEST(UnsupportedOpsTest, RejectsNonHeapGradientSamplingInEveryStage) {
  LLVMContext Ctx;
  auto M = parseIR(Ctx, R"(
    define float @plain(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
      %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, <2 x float> zeroinitializer, <2 x float> zeroinitializer, <2 x i32> zeroinitializer)
      ret float %r
    }
    define float @clamped(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
      %r = call float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, <2 x float> zeroinitializer, <2 x float> zeroinitializer, <2 x i32> zeroinitializer, float 0.0)
      ret float %r
    }
    declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>)
    declare float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>, float)
  )");
  ASSERT_TRUE(M);
  for (StringRef Name : {"plain", "clamped"}) {
    Function *F = M->getFunction(Name);
    for (StringRef Stage : {"", "fragment", "vertex", "compute"}) {
      SCOPED_TRACE((Name + ": " + Stage).str());
      if (!Stage.empty())
        F->addFnAttr("feme.shader.stage", Stage);
      EXPECT_THAT_ERROR(
          checkSupportedRaisedOps(*M),
          Failed<StringError>(testing::Property(
              &StringError::getMessage,
              testing::HasSubstr(
                  "requires heap-backed FP32 1D/2D/3D/cube (including array) "
                  "explicit-gradient sampling"))));
    }
    F->eraseFromParent();
  }
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
}

TEST(UnsupportedOpsTest, RejectsUnraisedDXOpCall) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      call void @dx.op.something(i32 1)
      ret void
    }
    declare void @dx.op.something(i32)
  )");
  ASSERT_TRUE(M);
  Error E = checkSupportedRaisedOps(*M);
  EXPECT_THAT_ERROR(std::move(E), Failed<StringError>(testing::Property(
                                      &StringError::getMessage,
                                      testing::HasSubstr("dx.op.something"))));
}

TEST(UnsupportedOpsTest, RejectsRegisterBoundDXHandle) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define target("dx.TypedBuffer", <4 x float>, 1, 0, 0) @main() {
      %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      ret target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h
    }
    declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  Error E = checkSupportedRaisedOps(*M);
  EXPECT_THAT_ERROR(std::move(E),
                    Failed<StringError>(testing::Property(
                        &StringError::getMessage,
                        testing::HasSubstr("register-bound resource handle"))));
}

TEST(UnsupportedOpsTest, RejectsRegisterBoundSPIRVHandle) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define i32 @main() {
      %h = call i32 @llvm.spv.resource.handlefrombinding.i32(i32 0, i32 0, i32 1, i32 0, ptr null)
      ret i32 %h
    }
    declare i32 @llvm.spv.resource.handlefrombinding.i32(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  Error E = checkSupportedRaisedOps(*M);
  EXPECT_THAT_ERROR(std::move(E),
                    Failed<StringError>(testing::Property(
                        &StringError::getMessage,
                        testing::HasSubstr("register-bound resource handle"))));
}

/// (Roadmap F8a) A SPIR-V `subpassInput` variable's own `handlefrombinding`
/// call is always left completely unreferenced by `feme::spirv::
/// SubpassLoadPattern` (SPIRVToLLVMPatterns.cpp): a subpass input is read
/// from the currently-bound render-target attachment, never from that
/// descriptor's own image memory. Such a call is dead code, not an
/// unsupported resource access this target needs to normalize -- it must
/// not fail the whole shader the way an actually-read register-bound
/// handle (the two tests above) does.
TEST(UnsupportedOpsTest, AcceptsUnusedRegisterBoundHandle) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      %h = call i32 @llvm.spv.resource.handlefrombinding.i32(i32 0, i32 0, i32 1, i32 0, ptr null)
      ret void
    }
    declare i32 @llvm.spv.resource.handlefrombinding.i32(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
}

TEST(UnsupportedOpsTest, AcceptsBindlessDXHandle) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
          @llvm.dx.resource.handlefromheap(i32 0)
      ret void
    }
    declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
        @llvm.dx.resource.handlefromheap(i32)
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
}

TEST(UnsupportedOpsTest, IgnoresUnusedDeclarations) {
  // A declaration with no callers (e.g. left behind after some other
  // transform erased its only call) isn't itself a problem.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      ret void
    }
    declare void @dx.op.something(i32)
    declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
}

TEST(UnsupportedOpsTest, AcceptsRootConstantHandle) {
  // Any single register-bound `dx.CBuffer` handle is not an unsupported
  // operation (roadmap R25 lifted the earlier `(b0, space0)`-only
  // restriction; see RootConstantLowering.h), even though it is still
  // present at this point -- `feme::cpu::RootConstantLoweringPass`/
  // `feme::cpu::ResourceLoweringPass` run after this check (see "Root
  // constants" in feme/docs/FeMeCPUDesign.md).
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %row) {
      %h = call target("dx.CBuffer", [16 x i8])
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %v = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32(
          target("dx.CBuffer", [16 x i8]) %h, i32 0)
      ret void
    }
    declare target("dx.CBuffer", [16 x i8])
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32(
        target("dx.CBuffer", [16 x i8]), i32)
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
}

TEST(UnsupportedOpsTest, AcceptsRootConstantHandleAtNonDefaultBinding) {
  // Roadmap R25: the recognized binding is no longer fixed to
  // `(b0, space0)` -- any single one qualifies.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %row) {
      %h = call target("dx.CBuffer", [16 x i8])
          @llvm.dx.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %v = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32(
          target("dx.CBuffer", [16 x i8]) %h, i32 0)
      ret void
    }
    declare target("dx.CBuffer", [16 x i8])
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32(
        target("dx.CBuffer", [16 x i8]), i32)
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(checkSupportedRaisedOps(*M), Succeeded());
}

TEST(UnsupportedOpsTest, RejectsTwoDistinctRootConstantBindings) {
  // Exactly one register-bound constant buffer is still promoted to root
  // constants; a second, distinct binding remains ambiguous (there is only
  // one root-constant block) and is left as an ordinary register-bound
  // resource this target still has no other way to address.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(i32 %row) {
      %h0 = call target("dx.CBuffer", [16 x i8])
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %h1 = call target("dx.CBuffer", [16 x i8])
          @llvm.dx.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %v0 = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32(
          target("dx.CBuffer", [16 x i8]) %h0, i32 0)
      %v1 = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32(
          target("dx.CBuffer", [16 x i8]) %h1, i32 0)
      ret void
    }
    declare target("dx.CBuffer", [16 x i8])
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32(
        target("dx.CBuffer", [16 x i8]), i32)
  )");
  ASSERT_TRUE(M);
  Error E = checkSupportedRaisedOps(*M);
  EXPECT_THAT_ERROR(std::move(E),
                    Failed<StringError>(testing::Property(
                        &StringError::getMessage,
                        testing::HasSubstr("register-bound resource handle"))));
}

TEST(UnsupportedOpsTest, RegisterBoundHandleDiagnosticWarnsAboutBystanders) {
  // Roadmap L64: this diagnostic names a handle, but the real cause is
  // frequently an unsupported *use* of some other resource in the same
  // function -- one rejected use leaves every handle in that function
  // unlowered, and whichever declaration comes first in the module is
  // what gets reported. Taking the name literally cost several sessions,
  // which mis-filed an over-strict `Grad` derivative-width check against
  // arrayed image samples as an unrelated `spirv.VulkanBuffer`
  // uniform-block defect. The message must say so explicitly.
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      %h0 = call target("dx.CBuffer", [16 x i8])
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %h1 = call target("dx.CBuffer", [16 x i8])
          @llvm.dx.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
      %v0 = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32(
          target("dx.CBuffer", [16 x i8]) %h0, i32 0)
      %v1 = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32(
          target("dx.CBuffer", [16 x i8]) %h1, i32 0)
      ret void
    }
    declare target("dx.CBuffer", [16 x i8])
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32(
        target("dx.CBuffer", [16 x i8]), i32)
  )");
  ASSERT_TRUE(M);
  Error E = checkSupportedRaisedOps(*M);
  EXPECT_THAT_ERROR(
      std::move(E),
      Failed<StringError>(testing::Property(
          &StringError::getMessage,
          testing::AllOf(
              testing::HasSubstr("register-bound resource handle"),
              testing::HasSubstr("unrelated bystander rather than the real "
                                 "cause")))));
}

} // namespace
