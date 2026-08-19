//===- RaisedIRVerifierTest.cpp - Tests for feme::verifyNoRaisedIRRemains ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/RaisedIRVerifier.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("RaisedIRVerifierTest", errs());
  return M;
}

TEST(RaisedIRVerifierTest, AcceptsModuleWithNoRaisedIR) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr addrspace(1) %buf) {
      %v = load i32, ptr addrspace(1) %buf
      store i32 %v, ptr addrspace(1) %buf
      ret void
    }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(verifyNoRaisedIRRemains(*M, "amdgcn-amd-amdhsa"),
                    Succeeded());
}

// A typed buffer binding `feme::amdgpu::ResourceLoweringPass` fully lowers
// (see amdgpu-lower-resources.ll) leaves no `target("dx.")` handle and no
// `llvm.dx.resource.*` call behind, so this should look the same as any
// other already-lowered module to the check.
TEST(RaisedIRVerifierTest, AcceptsModuleWithLoweredTypedBuffer) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main(ptr addrspace(1) %res.space0.reg0) {
      %e = getelementptr float, ptr addrspace(1) %res.space0.reg0, i32 0
      %v = load float, ptr addrspace(1) %e
      store float %v, ptr addrspace(1) %e
      ret void
    }
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(verifyNoRaisedIRRemains(*M, "amdgcn-amd-amdhsa"),
                    Succeeded());
}

// A resource kind `ResourceLoweringPass` cannot model (e.g. a `cbuffer`,
// `target("dx.CBuffer", ...)`, or a texture, `target("dx.Texture", ...)`,
// once raised to that form) is left entirely unrewritten, per its own
// documented "leave what it cannot model alone" precedent -- this is the
// shape that should now be caught here instead of reaching real ISel and
// hitting `llvm::MVT::getVT`'s `llvm_unreachable` (the bug this check
// fixes; `Tools/feme/feme-dxil-to-amdgpu-unsupported-resource.ll` exercises
// the same shape end to end through the real `feme` CLI, with a `cbuffer`,
// which is what the real HLSL shader this check was written for actually
// hits -- see that test's own comment).
TEST(RaisedIRVerifierTest, RejectsLeftoverResourceHandleType) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define <4 x half> @main() {
      %tex = call target("dx.Texture", <4 x half>, 0, 0, 0, 2)
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      %texel = call <4 x half> @llvm.dx.resource.load.level(
          target("dx.Texture", <4 x half>, 0, 0, 0, 2) %tex, <2 x i32> zeroinitializer,
          i32 0, <2 x i32> zeroinitializer)
      ret <4 x half> %texel
    }
    declare target("dx.Texture", <4 x half>, 0, 0, 0, 2)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
    declare <4 x half> @llvm.dx.resource.load.level(
        target("dx.Texture", <4 x half>, 0, 0, 0, 2), <2 x i32>, i32, <2 x i32>)
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(verifyNoRaisedIRRemains(*M, "amdgpu9.0a-amd-amdhsa"),
                    FailedWithMessage(testing::HasSubstr("dx.Texture")));
}

// `feme::dxil::OpRaisingPass` does not yet raise every resource kind's
// access ops for the non-bindless (`handlefrombinding`, not
// `handlefromheap`) binding path (see Design.md's "Decision: texture and
// sampler handle kinds") -- a bound `Texture2D`/`RWTexture2D` is the
// concrete gap: its handle/access calls are left in the raw, un-raised
// `dx.op.*` DXIL calling convention entirely (`dx.op.createHandleFromBinding`/
// `dx.op.textureLoad.*`, not `llvm.dx.resource.*`), which is just as
// unusable to a real target's ISel as a leftover `target("dx.")` type, so
// this check rejects it the same way.
TEST(RaisedIRVerifierTest, RejectsLeftoverUnraisedDXILOpCall) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    %dx.types.Handle = type { ptr }
    %dx.types.ResBind = type { i32, i32, i32, i8 }

    define i32 @main() {
      %h = call %dx.types.Handle @dx.op.createHandleFromBinding(
          i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)
      %id = call i32 @dx.op.threadId.i32(i32 93, i32 0)
      ret i32 %id
    }
    declare %dx.types.Handle @dx.op.createHandleFromBinding(
        i32, %dx.types.ResBind, i32, i1)
    declare i32 @dx.op.threadId.i32(i32, i32)
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(
      verifyNoRaisedIRRemains(*M, "amdgcn-amd-amdhsa"),
      FailedWithMessage(testing::HasSubstr("dx.op.createHandleFromBinding")));
}

// A raised op neither AMDGPU lowering pass covers (see
// `feme::amdgpu::RaisedLoweringPass`'s "not yet covered" list, e.g. a wave
// op) is likewise left as an unmodified call to the format-agnostic
// intrinsic, which this check should also reject rather than let it reach
// ISel as a call to an unresolved external symbol.
TEST(RaisedIRVerifierTest, RejectsLeftoverRaisedIntrinsicCall) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define float @main(float %v) {
      %r = call float @llvm.dx.wave.reduce.sum.f32(float %v)
      ret float %r
    }
    declare float @llvm.dx.wave.reduce.sum.f32(float)
  )");
  ASSERT_TRUE(M);
  EXPECT_THAT_ERROR(
      verifyNoRaisedIRRemains(*M, "amdgcn-amd-amdhsa"),
      FailedWithMessage(testing::HasSubstr("llvm.dx.wave.reduce.sum.f32")));
}

} // namespace
