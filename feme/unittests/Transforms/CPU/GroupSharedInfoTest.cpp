//===- GroupSharedInfoTest.cpp - Tests for getGroupSharedRequirements ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/GroupSharedInfo.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("GroupSharedInfoTest", errs());
  return M;
}

TEST(GroupSharedInfoTest, NoGroupSharedGlobalsIsZero) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @g = internal global i32 0
  )");
  ASSERT_TRUE(M);

  GroupSharedRequirements Reqs = getGroupSharedRequirements(*M);
  EXPECT_EQ(Reqs.Size, 0u);
  EXPECT_EQ(Reqs.Alignment, 1u);
}

TEST(GroupSharedInfoTest, SizesAndAlignsAcrossGlobals) {
  LLVMContext Ctx;
  // A 4-byte-aligned `i32` followed by an 8-byte-aligned `double`: the
  // `double` must be padded up to its own alignment, and the buffer's
  // overall alignment is the strictest of the two (8), matching
  // `computeGroupSharedLayout`'s own contract.
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @a = addrspace(3) global i32 0
    @b = addrspace(3) global double 0.0
  )");
  ASSERT_TRUE(M);

  GroupSharedRequirements Reqs = getGroupSharedRequirements(*M);
  EXPECT_EQ(Reqs.Size, 16u);
  EXPECT_EQ(Reqs.Alignment, 8u);
}

} // namespace
