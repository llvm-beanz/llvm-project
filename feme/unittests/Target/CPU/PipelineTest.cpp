//===- PipelineTest.cpp - Tests for the stage-aware runPipeline ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap R27: `feme::cpu::StageCompileOptions` and the `runPipeline`
// overload that takes it (feme/docs/FeMeGraphicsDesign.md's "CPU Lowering
// Pipeline"), plus the pre-mutation graphics validation step it runs for a
// non-`Compute` stage. The compute-only compatibility overload is already
// covered end-to-end by AOTDispatchTest/CompiledStageTest; these tests
// focus on what is new: stage selection and the validation gate.
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/Pipeline.h"

#include "feme/Core/ShaderStage.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("PipelineTest", errs());
  return M;
}

// A vertex-stage entry point with the same straight-line, uniform-control-
// flow shape the compute-only pipeline already supports (roadmap milestone
// 4's scope) -- proving the shared middle end runs a non-compute stage
// through unmodified, per "G1 is the design's own discriminating milestone"
// in feme/docs/Roadmap.md's §1.8.3. `hlsl.numthreads` is still required
// because `feme::cpu::EntryWrapperPass` (the compute dispatch wrapper) is
// the only Phase 6 wrapper that exists yet; a dedicated vertex wrapper is
// roadmap R28's job.
constexpr char VertexShaderIR[] = R"(
  define void @vs_main() #0 {
    ret void
  }
  attributes #0 = { "feme.shader.stage"="vertex" "hlsl.numthreads"="4,1,1" }
)";

TEST(PipelineTest, SelectsTheRequestedStage) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, VertexShaderIR);
  ASSERT_TRUE(M);

  StageCompileOptions Opts;
  Opts.Stage = ShaderStage::Vertex;
  Opts.WaveSize = 4;
  Expected<PipelineResult> Result = runPipeline(*M, Opts);
  ASSERT_THAT_EXPECTED(Result, Succeeded());
  EXPECT_EQ(Result->EntryName, "vs_main");
  EXPECT_EQ(Result->Stage, ShaderStage::Vertex);
  EXPECT_TRUE(M->getFunction(Result->WrapperName));
}

TEST(PipelineTest, RejectsAnEntryPointOfAnotherStage) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, VertexShaderIR);
  ASSERT_TRUE(M);

  StageCompileOptions Opts;
  Opts.Stage = ShaderStage::Fragment;
  Opts.WaveSize = 4;
  Expected<PipelineResult> Result = runPipeline(*M, Opts);
  ASSERT_THAT_ERROR(Result.takeError(), Failed());
}

// The compute-only overload is equivalent to the `StageCompileOptions`
// overload with `Stage == ShaderStage::Compute`.
TEST(PipelineTest, ComputeOnlyOverloadSelectsComputeStage) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      ret void
    }
    attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  Expected<PipelineResult> Result = runPipeline(*M, /*EntryPoint=*/"", 4);
  ASSERT_THAT_EXPECTED(Result, Succeeded());
  EXPECT_EQ(Result->Stage, ShaderStage::Compute);
}

// `feme.stage.discard` is only legal for the fragment stage (see
// `feme::graphics::ValidateStagePass`'s `isStageOpLegalForStage`); calling
// it from a vertex entry point is a validation failure the pre-mutation
// gate must catch *before* `feme::cpu::PreparePass` gets a chance to
// restructure the function's control flow.
TEST(PipelineTest, PreMutationValidationRejectsAnIllegalStageOp) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @vs_main() #0 {
      call void @feme.stage.discard(i1 true)
      ret void
    }
    declare void @feme.stage.discard(i1)
    attributes #0 = { "feme.shader.stage"="vertex" "hlsl.numthreads"="4,1,1" }
  )");
  ASSERT_TRUE(M);

  StageCompileOptions Opts;
  Opts.Stage = ShaderStage::Vertex;
  Opts.WaveSize = 4;
  Expected<PipelineResult> Result = runPipeline(*M, Opts);
  ASSERT_THAT_ERROR(Result.takeError(), Failed());
}

} // namespace
