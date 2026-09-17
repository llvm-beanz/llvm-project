//===- LocalNarrowVectorArrayInitTest.cpp - Tests for the pass ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/LocalNarrowVectorArrayInit.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
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
    Err.print("LocalNarrowVectorArrayInitTest", errs());
  return M;
}

PreservedAnalyses runPass(Module &M) {
  ModuleAnalysisManager MAM;
  return LocalNarrowVectorArrayInitPass().run(M, MAM);
}

// Roadmap H69: a `Private`-storage local array of a narrow (3-wide, so
// non-power-of-2) vector -- e.g. a mesh entry's own local `uint3 idx[2]`
// built up before being copied out to `gl_PrimitiveTriangleIndicesEXT` --
// has its single aggregate "whole array" initializing store rewritten into
// one store per array element, each addressed at the very same *tightly
// packed* (12-byte, not 16-byte-ABI-padded) byte offset the access-chain-
// converted `getelementptr`s elsewhere in the same function already use
// to read those elements back, so both sides finally agree.
TEST(LocalNarrowVectorArrayInitTest, DecomposesAggregateInitStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @spirv_var_25 = private global [2 x <3 x i32>] undef

    define <3 x i32> @main() {
      store [2 x <3 x i32>] [<3 x i32> <i32 0, i32 1, i32 2>, <3 x i32> <i32 2, i32 3, i32 1>], ptr @spirv_var_25, align 4
      %e0 = load <3 x i32>, ptr @spirv_var_25, align 4
      %e1 = load <3 x i32>, ptr getelementptr inbounds nuw (i8, ptr @spirv_var_25, i64 12), align 4
      %sum = add <3 x i32> %e0, %e1
      ret <3 x i32> %sum
    }
  )");
  ASSERT_TRUE(M);

  PreservedAnalyses PA = runPass(*M);
  EXPECT_TRUE(PA.areAllPreserved() == false);

  // The single aggregate store is gone, replaced by one store per element.
  Function *F = M->getFunction("main");
  unsigned NumStores = 0;
  for (Instruction &I : instructions(F))
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
      ++NumStores;
      // Every store should target either the global directly (element 0,
      // offset 0) or a tight (12-byte) `getelementptr` into it (element
      // 1) -- never a plain aggregate `[2 x <3 x i32>]`-typed store.
      EXPECT_TRUE(isa<FixedVectorType>(SI->getValueOperand()->getType()));
    }
  EXPECT_EQ(NumStores, 2u);

  // Re-running the pass on the now-fixed module is a no-op: there is no
  // longer any aggregate "whole array" store left to decompose.
  PreservedAnalyses PA2 = runPass(*M);
  EXPECT_TRUE(PA2.areAllPreserved());
}

// A stage-IO output array (address space 8, mirroring
// `feme::spirv::StageIOGlobalVariablePattern`'s own address-space
// assignment for `Output` storage) must never be touched by this pass,
// even though it shares the exact same array-of-narrow-vector shape --
// `CanonicalizeStage.cpp`'s own `getPackedMeshElementSize` already relies
// on such an array's accesses staying exactly as `feme`'s SPIR-V-to-LLVM
// conversion produces them.
TEST(LocalNarrowVectorArrayInitTest, IgnoresNonPrivateAddressSpace) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @spirv_var_56 = external addrspace(8) global [2 x <3 x i32>]

    define void @main() {
      store [2 x <3 x i32>] [<3 x i32> <i32 0, i32 1, i32 2>, <3 x i32> <i32 2, i32 3, i32 1>], ptr addrspace(8) @spirv_var_56, align 4
      ret void
    }
  )");
  ASSERT_TRUE(M);

  PreservedAnalyses PA = runPass(*M);
  EXPECT_TRUE(PA.areAllPreserved());

  Function *F = M->getFunction("main");
  unsigned NumStores = 0;
  for (Instruction &I : instructions(F))
    if (isa<StoreInst>(&I))
      ++NumStores;
  EXPECT_EQ(NumStores, 1u);
}

// Roadmap L99: a `spirv.MatrixType`-turned array of columns (e.g. a
// `mat2x3`'s `!llvm.array<2 x <3 x float>>`) is read back by `m[i][j]`
// indexing lowered into an ordinary, natural-ABI-strided `getelementptr`
// -- never a tight `i8`-offset GEP -- so this pass must leave its
// aggregate init store alone: rewriting it into a tight-offset store
// would make it disagree with every real read, the exact corruption this
// pass exists to prevent, not cause.
TEST(LocalNarrowVectorArrayInitTest, IgnoresGlobalWithOnlyNaturalGEPReaders) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @spirv_var_10 = private global [2 x <3 x float>] undef

    define float @main(i32 %col, i32 %row) {
      store [2 x <3 x float>] [<3 x float> <float 1.0, float 2.0, float 3.0>, <3 x float> <float 4.0, float 5.0, float 6.0>], ptr @spirv_var_10, align 4
      %p = getelementptr [2 x <3 x float>], ptr @spirv_var_10, i32 0, i32 %col, i32 %row
      %v = load float, ptr %p, align 4
      ret float %v
    }
  )");
  ASSERT_TRUE(M);

  PreservedAnalyses PA = runPass(*M);
  EXPECT_TRUE(PA.areAllPreserved());

  Function *F = M->getFunction("main");
  unsigned NumStores = 0;
  for (Instruction &I : instructions(F))
    if (isa<StoreInst>(&I))
      ++NumStores;
  EXPECT_EQ(NumStores, 1u);
}

// A local array whose element type has no ABI-padding gap at all (a
// 4-wide, already-power-of-2 vector) needs no fixup: an ordinary
// aggregate store already agrees with every tightly packed offset into
// it, since the two coincide. The pass must leave it alone.
TEST(LocalNarrowVectorArrayInitTest, IgnoresPowerOfTwoWidthVector) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @spirv_var_local = private global [2 x <4 x i32>] undef

    define void @main() {
      store [2 x <4 x i32>] [<4 x i32> <i32 0, i32 1, i32 2, i32 3>, <4 x i32> <i32 4, i32 5, i32 6, i32 7>], ptr @spirv_var_local, align 4
      ret void
    }
  )");
  ASSERT_TRUE(M);

  PreservedAnalyses PA = runPass(*M);
  EXPECT_TRUE(PA.areAllPreserved());

  Function *F = M->getFunction("main");
  unsigned NumStores = 0;
  for (Instruction &I : instructions(F))
    if (isa<StoreInst>(&I))
      ++NumStores;
  EXPECT_EQ(NumStores, 1u);
}

} // namespace
