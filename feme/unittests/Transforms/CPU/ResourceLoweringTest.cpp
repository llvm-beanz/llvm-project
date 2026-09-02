//===- ResourceLoweringTest.cpp - Tests for ResourceLoweringPass ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ResourceLowering.h"

#include "feme/Transforms/CPU/ImageCalls.h"
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
  // root-constant-register, root-constant-min-offset, ...heap indices}.
  ASSERT_EQ(Entry->getNumOperands(), 7u);
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
            0u);
  EXPECT_EQ(mdconst::extract<ConstantInt>(Entry->getOperand(6))->getZExtValue(),
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

// Regression test for roadmap H3a: addResourceEnvParams() (the DXIL-oriented
// twin of SPIRVResourceLowering.cpp's identically-named helper) has the same
// GlobalObject::copyAttributesFrom() metadata-copying gap. Verify
// function-attached metadata (e.g. !feme.signature) survives the
// resource-env-parameter rewrite here too.
TEST(ResourceLoweringTest, PreservesFunctionMetadataAcrossEnvParamRewrite) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(i32 %idx) !feme.signature !0 {
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

    !0 = !{!"fragment-signature-placeholder"}
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  MDNode *Signature = F->getMetadata("feme.signature");
  ASSERT_TRUE(Signature) << "!feme.signature metadata was lost when the "
                             "function was rewritten to add resource-env "
                             "parameters";
  ASSERT_EQ(Signature->getNumOperands(), 1u);
  EXPECT_EQ(cast<MDString>(Signature->getOperand(0))->getString(),
            "fragment-signature-placeholder");
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

/// Finds \p F's call to the canonical `feme.cpu.image.*` entry point named
/// \p Name, or null if none exists -- mirrors
/// `SPIRVResourceLoweringTest.cpp`'s own helper of the same name.
CallInst *findImageCall(Function &F, StringRef Name) {
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getName() == Name)
          return CI;
  return nullptr;
}

// The following image-lowering tests (roadmap H7b-a) are the first
// dedicated coverage for `ResourceLowering.cpp`'s `classifyImageHandle`/
// `lowerImageAccesses` -- the DXIL mirror of `SPIRVResourceLoweringTest.cpp`
// -- since the pre-existing `Texture2D`-only support had none of its own;
// each test below establishes both the pre-existing plain-2D shape's
// coverage and H7b-a's new Array2D/Cube/CubeArray widening in one pass.

TEST(ResourceLoweringTest, LowersTexture2DSampleToImageSample) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x float> %uv) {
      %tex = call target("dx.Texture", <4 x float>, 0, 0, 0, 2)
          @llvm.dx.resource.handlefromheap.timg2d(i32 0, i1 false)
      %samp = call target("dx.Sampler", 0)
          @llvm.dx.resource.handlefromheap.tsamp2d(i32 1, i1 false)
      %r = call <4 x float> @llvm.dx.resource.sample.timg2d(
          target("dx.Texture", <4 x float>, 0, 0, 0, 2) %tex,
          target("dx.Sampler", 0) %samp, <2 x float> %uv,
          <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 2)
        @llvm.dx.resource.handlefromheap.timg2d(i32, i1)
    declare target("dx.Sampler", 0)
        @llvm.dx.resource.handlefromheap.tsamp2d(i32, i1)
    declare <4 x float> @llvm.dx.resource.sample.timg2d(
        target("dx.Texture", <4 x float>, 0, 0, 0, 2),
        target("dx.Sampler", 0), <2 x float>, <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.2d.v4f32");
  ASSERT_TRUE(Sample);
  std::optional<MatchedImageCall> Matched = matchImageCall(*Sample);
  ASSERT_TRUE(Matched);
  EXPECT_EQ(Matched->Kind, ImageCallKind::Sample2D);
}

TEST(ResourceLoweringTest, LowersTexture2DLoadToImageLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<2 x i32> %xy) {
      %tex = call target("dx.Texture", <4 x float>, 0, 0, 0, 2)
          @llvm.dx.resource.handlefromheap.timg2dl(i32 0, i1 false)
      %r = call <4 x float> @llvm.dx.resource.load.level.timg2dl(
          target("dx.Texture", <4 x float>, 0, 0, 0, 2) %tex,
          <2 x i32> %xy, i32 0, <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 2)
        @llvm.dx.resource.handlefromheap.timg2dl(i32, i1)
    declare <4 x float> @llvm.dx.resource.load.level.timg2dl(
        target("dx.Texture", <4 x float>, 0, 0, 0, 2), <2 x i32>, i32,
        <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.2d.v4f32");
  ASSERT_TRUE(Load);
}

TEST(ResourceLoweringTest, LowersTexture2DArraySampleToImageSampleArray) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %uvw) {
      %tex = call target("dx.Texture", <4 x float>, 0, 0, 0, 7)
          @llvm.dx.resource.handlefromheap.timg2darr(i32 0, i1 false)
      %samp = call target("dx.Sampler", 0)
          @llvm.dx.resource.handlefromheap.tsamp2darr(i32 1, i1 false)
      %r = call <4 x float> @llvm.dx.resource.sample.timg2darr(
          target("dx.Texture", <4 x float>, 0, 0, 0, 7) %tex,
          target("dx.Sampler", 0) %samp, <3 x float> %uvw,
          <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 7)
        @llvm.dx.resource.handlefromheap.timg2darr(i32, i1)
    declare target("dx.Sampler", 0)
        @llvm.dx.resource.handlefromheap.tsamp2darr(i32, i1)
    declare <4 x float> @llvm.dx.resource.sample.timg2darr(
        target("dx.Texture", <4 x float>, 0, 0, 0, 7),
        target("dx.Sampler", 0), <3 x float>, <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample =
      findImageCall(*F, "feme.cpu.image.sample.2darray.v4f32");
  ASSERT_TRUE(Sample);
}

TEST(ResourceLoweringTest, LowersTexture2DArrayLoadToImageLoadArray) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x i32> %xyz) {
      %tex = call target("dx.Texture", <4 x float>, 0, 0, 0, 7)
          @llvm.dx.resource.handlefromheap.timg2darrl(i32 0, i1 false)
      %r = call <4 x float> @llvm.dx.resource.load.level.timg2darrl(
          target("dx.Texture", <4 x float>, 0, 0, 0, 7) %tex,
          <3 x i32> %xyz, i32 0, <3 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 7)
        @llvm.dx.resource.handlefromheap.timg2darrl(i32, i1)
    declare <4 x float> @llvm.dx.resource.load.level.timg2darrl(
        target("dx.Texture", <4 x float>, 0, 0, 0, 7), <3 x i32>, i32,
        <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Load = findImageCall(*F, "feme.cpu.image.load.2darray.v4f32");
  ASSERT_TRUE(Load);
}

TEST(ResourceLoweringTest, LowersTextureCubeSampleToImageSampleCube) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x float> %dir) {
      %tex = call target("dx.Texture", <4 x float>, 0, 0, 0, 5)
          @llvm.dx.resource.handlefromheap.timgcube(i32 0, i1 false)
      %samp = call target("dx.Sampler", 0)
          @llvm.dx.resource.handlefromheap.tsampcube(i32 1, i1 false)
      %r = call <4 x float> @llvm.dx.resource.sample.timgcube(
          target("dx.Texture", <4 x float>, 0, 0, 0, 5) %tex,
          target("dx.Sampler", 0) %samp, <3 x float> %dir,
          <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 5)
        @llvm.dx.resource.handlefromheap.timgcube(i32, i1)
    declare target("dx.Sampler", 0)
        @llvm.dx.resource.handlefromheap.tsampcube(i32, i1)
    declare <4 x float> @llvm.dx.resource.sample.timgcube(
        target("dx.Texture", <4 x float>, 0, 0, 0, 5),
        target("dx.Sampler", 0), <3 x float>, <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample = findImageCall(*F, "feme.cpu.image.sample.cube.v4f32");
  ASSERT_TRUE(Sample);
}

TEST(ResourceLoweringTest, LowersTextureCubeArraySampleToImageSampleCubeArray) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<4 x float> %dirandlayer) {
      %tex = call target("dx.Texture", <4 x float>, 0, 0, 0, 9)
          @llvm.dx.resource.handlefromheap.timgcubearr(i32 0, i1 false)
      %samp = call target("dx.Sampler", 0)
          @llvm.dx.resource.handlefromheap.tsampcubearr(i32 1, i1 false)
      %r = call <4 x float> @llvm.dx.resource.sample.timgcubearr(
          target("dx.Texture", <4 x float>, 0, 0, 0, 9) %tex,
          target("dx.Sampler", 0) %samp, <4 x float> %dirandlayer,
          <2 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 9)
        @llvm.dx.resource.handlefromheap.timgcubearr(i32, i1)
    declare target("dx.Sampler", 0)
        @llvm.dx.resource.handlefromheap.tsampcubearr(i32, i1)
    declare <4 x float> @llvm.dx.resource.sample.timgcubearr(
        target("dx.Texture", <4 x float>, 0, 0, 0, 9),
        target("dx.Sampler", 0), <4 x float>, <2 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  CallInst *Sample =
      findImageCall(*F, "feme.cpu.image.sample.cubearray.v4f32");
  ASSERT_TRUE(Sample);
}

// `TextureCube` has no `Load` method in HLSL/DXIL at all (mirroring
// `OpImageFetch`'s identical restriction against SPIR-V's `Dim::Cube` --
// see `SPIRVResourceLoweringTest.cpp`'s `LeavesACubeImageFetchAlone`), so a
// `load.level` call against a `TextureCube` handle -- however unusual --
// must be left entirely unrewritten rather than lowered.
TEST(ResourceLoweringTest, LeavesATextureCubeLoadUnchanged) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x float> @main(<3 x i32> %xyz) {
      %tex = call target("dx.Texture", <4 x float>, 0, 0, 0, 5)
          @llvm.dx.resource.handlefromheap.timgcubel(i32 0, i1 false)
      %r = call <4 x float> @llvm.dx.resource.load.level.timgcubel(
          target("dx.Texture", <4 x float>, 0, 0, 0, 5) %tex,
          <3 x i32> %xyz, i32 0, <3 x i32> zeroinitializer)
      ret <4 x float> %r
    }
    declare target("dx.Texture", <4 x float>, 0, 0, 0, 5)
        @llvm.dx.resource.handlefromheap.timgcubel(i32, i1)
    declare <4 x float> @llvm.dx.resource.load.level.timgcubel(
        target("dx.Texture", <4 x float>, 0, 0, 0, 5), <3 x i32>, i32,
        <3 x i32>)
  )");
  ASSERT_TRUE(M);
  runPass(*M);

  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.load.2darray.v4f32"));
  EXPECT_FALSE(findImageCall(*F, "feme.cpu.image.load.2d.v4f32"));
  // The un-lowered call itself is still present, referencing the original
  // (never-heap-widened) `main` signature.
  bool FoundOriginalLoad = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (Function *Callee = CI->getCalledFunction())
        if (Callee->getName().starts_with("llvm.dx.resource.load.level"))
          FoundOriginalLoad = true;
  EXPECT_TRUE(FoundOriginalLoad);
}

} // namespace
