//===- ContextTest.cpp - Tests for feme::Context -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Context.h"

#include "mlir/IR/MLIRContext.h"
#include "llvm/IR/LLVMContext.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

TEST(ContextTest, ConstructDoesNotCrash) { Context Ctx; }

TEST(ContextTest, ExposesDistinctUnderlyingContexts) {
  Context Ctx;
  EXPECT_NE(&Ctx.getLLVMContext(), nullptr);
  EXPECT_NE(&Ctx.getMLIRContext(), nullptr);
}

TEST(ContextTest, EachContextOwnsIndependentLLVMContext) {
  // Two independent Contexts must never share mutable state (see the "No
  // Global State" principle in feme/docs/Design.md): in particular, each
  // must own its own LLVMContext/MLIRContext instance.
  Context A;
  Context B;
  EXPECT_NE(&A.getLLVMContext(), &B.getLLVMContext());
  EXPECT_NE(&A.getMLIRContext(), &B.getMLIRContext());
}

TEST(ContextTest, WrapsExternallyOwnedMLIRContext) {
  mlir::MLIRContext External;
  Context Ctx(External);
  EXPECT_EQ(&Ctx.getMLIRContext(), &External);
}

} // namespace
