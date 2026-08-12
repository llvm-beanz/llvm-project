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
    define void @main() {
      %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
          @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
      ret void
    }
    declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
        @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  Error E = checkSupportedRaisedOps(*M);
  EXPECT_THAT_ERROR(
      std::move(E),
      Failed<StringError>(testing::Property(
          &StringError::getMessage,
          testing::HasSubstr("register-bound resource handle"))));
}

TEST(UnsupportedOpsTest, RejectsRegisterBoundSPIRVHandle) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      %h = call i32 @llvm.spv.resource.handlefrombinding.i32(i32 0, i32 0, i32 1, i32 0, ptr null)
      ret void
    }
    declare i32 @llvm.spv.resource.handlefrombinding.i32(i32, i32, i32, i32, ptr)
  )");
  ASSERT_TRUE(M);
  Error E = checkSupportedRaisedOps(*M);
  EXPECT_THAT_ERROR(
      std::move(E),
      Failed<StringError>(testing::Property(
          &StringError::getMessage,
          testing::HasSubstr("register-bound resource handle"))));
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

} // namespace
