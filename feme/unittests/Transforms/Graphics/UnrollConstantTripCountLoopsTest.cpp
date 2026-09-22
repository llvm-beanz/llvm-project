//===- UnrollConstantTripCountLoopsTest.cpp - Tests for
// UnrollConstantTripCountStageLoopsPass -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/Graphics/UnrollConstantTripCountLoops.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::graphics;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("UnrollConstantTripCountLoopsTest", errs());
  return M;
}

std::string run(Module &M) {
  ModuleAnalysisManager MAM;
  UnrollConstantTripCountStageLoopsPass().run(M, MAM);
  std::string IR;
  raw_string_ostream OS(IR);
  M.print(OS, nullptr);
  return IR;
}

/// Roadmap L150: a Vertex entry point's `[3 x <3 x float>]` stage-IO
/// temporary (one `<3 x float>` "column" per loop iteration, matching a
/// `matNx3`'s real shape, addressed by a loop this pass's own
/// `ScalarEvolution`-provable-constant-trip-count unrolling is written to
/// force-unroll -- see this pass's own header comment) must, once this
/// pass's internal `SROAPass`/`InstCombinePass` split the unrolled,
/// per-iteration stores into separate, literal-byte-offset
/// `getelementptr`s, end up with the *padded* (16-byte, matching every
/// real target's -- and LLVM's own built-in default rule's -- vector
/// alignment for a 3-element vector, and std140/std430's own vec3-padded-
/// to-vec4 rule `CanonicalizeStagePass` and the real host `DataLayout`
/// substituted later in the pipeline both already agree on) 16-byte
/// stride between "columns", not the *tightly-packed* (12-byte) stride
/// `importShaderModule`'s own placeholder `DataLayout` (still attached to
/// \p M when this pass runs, see this pass's own implementation comment)
/// would otherwise silently bake in as a literal, no-longer-type-tagged
/// byte offset.
TEST(UnrollConstantTripCountLoopsTest, MatrixColumnStrideIsPadded) {
  LLVMContext Ctx;
  // `target datalayout` here is deliberately `importShaderModule`'s own
  // real placeholder (MLIR's generic default), reproducing exactly what
  // this pass sees when it runs in the real pipeline -- see this pass's
  // implementation comment for why that, and not any real target's own
  // `DataLayout`, is what is attached to \p M at this point.
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    target datalayout = "e-ve-i64:64-n8:16:32:64-G10"

    declare void @sink(ptr)

    define void @main() #0 {
    entry:
      %m = alloca [3 x <3 x float>], align 4
      br label %loop

    loop:
      %i = phi i32 [ 0, %entry ], [ %inext, %loop ]
      %v = phi <3 x float> [ zeroinitializer, %entry ], [ %vnext, %loop ]
      %p = getelementptr [3 x <3 x float>], ptr %m, i32 0, i32 %i
      store <3 x float> %v, ptr %p, align 4
      %vnext = fadd <3 x float> %v, <float 1.0, float 1.0, float 1.0>
      %inext = add i32 %i, 1
      %cond = icmp slt i32 %inext, 3
      br i1 %cond, label %loop, label %exit

    exit:
      call void @sink(ptr %m)
      ret void
    }

    attributes #0 = { "feme.shader.stage"="vertex" "hlsl.shader"="vertex" }
  )");
  ASSERT_TRUE(M);

  std::string IR = run(*M);
  // The bug this pass's `DataLayout` substitution (see its own
  // implementation comment) fixes: without it, `InstCombine` computes
  // `<3 x float>`'s array-element stride under the placeholder
  // `DataLayout` above (which has no explicit vector-alignment spec at
  // all), silently baking in the *tightly-packed* 12/24-byte offsets
  // instead of the correct, padded 16/32-byte ones every other stage of
  // the pipeline (and every real target) actually uses for this shape.
  EXPECT_EQ(IR.find("i64 12"), std::string::npos)
      << "matrix column stride baked in under the wrong (tightly-packed) "
         "DataLayout; full IR:\n"
      << IR;
  EXPECT_EQ(IR.find("i64 24"), std::string::npos)
      << "matrix column stride baked in under the wrong (tightly-packed) "
         "DataLayout; full IR:\n"
      << IR;
  EXPECT_NE(IR.find("i64 16"), std::string::npos)
      << "expected the correct, padded 16-byte column-1 stride; full IR:\n"
      << IR;
  EXPECT_NE(IR.find("i64 32"), std::string::npos)
      << "expected the correct, padded 32-byte column-2 stride; full IR:\n"
      << IR;

  // This pass must not leave its own temporary `DataLayout` substitution
  // (see its implementation comment) attached to \p M once it returns --
  // `CanonicalizeStagePass`, run immediately after this pass in the real
  // pipeline, needs the original, SPIR-V-execution-model one still in
  // place (roadmap H82).
  EXPECT_EQ(M->getDataLayout().getStringRepresentation(),
            "e-ve-i64:64-n8:16:32:64-G10");
}

} // namespace
