//===- UnsupportedOpsTest.cpp - Tests for checkSupportedRaisedOps --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/UnsupportedOps.h"

#include "llvm/AsmParser/Parser.h"
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
          @llvm.dx.resource.handlefromheap(i32 0, i1 false)
      ret void
    }
    declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
        @llvm.dx.resource.handlefromheap(i32, i1)
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

} // namespace
