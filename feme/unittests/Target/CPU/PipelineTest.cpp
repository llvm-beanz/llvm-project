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
#include "feme/Core/Signature.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

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

// Roadmap C8's stage-IO-raising finding: a raw SPIR-V `Input`/`Output`-
// storage-class global load/store (address space 7/8, the shape
// `feme::spirv::StageIOGlobalVariablePattern`/`attachStageIODecorations`
// produce -- see spirv-canonicalize-stage.ll) reaches this pipeline
// un-canonicalized whenever a shader is imported directly rather than
// routed through the separate Vulkan graphics pipeline first. Before this
// row, `feme::graphics::CanonicalizeStagePass` was never run by
// `runPipeline` at all, so the vector-typed `store` below reached
// `feme::cpu::SIMDizePass` as a plain divergent memory access rather than
// the per-component `feme.stage.output.store` calls it knows how to widen,
// hitting that pass's vector-decomposition diagnostic. Now that this
// pipeline runs `CanonicalizeStagePass` immediately before
// `ValidateStagePass`, this compiles cleanly end to end.
TEST(PipelineTest, CanonicalizesRawSPIRVStageIOBeforeWidening) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    target triple = "spirv-unknown-vulkan1.3-pixel"

    @in_var = external addrspace(7) constant i32, !spirv.Decorations !0
    @out_var = external addrspace(8) global <4 x float>, !spirv.Decorations !0

    define void @ps_main() #0 {
      %v = load i32, ptr addrspace(7) @in_var
      %f = sitofp i32 %v to float
      %vec0 = insertelement <4 x float> poison, float %f, i32 0
      %vec1 = insertelement <4 x float> %vec0, float %f, i32 1
      %vec2 = insertelement <4 x float> %vec1, float %f, i32 2
      %vec3 = insertelement <4 x float> %vec2, float %f, i32 3
      store <4 x float> %vec3, ptr addrspace(8) @out_var
      ret void
    }
    attributes #0 = { "feme.shader.stage"="fragment" }

    !0 = !{!1}
    !1 = !{i32 30, i32 0} ; Location 0
  )");
  ASSERT_TRUE(M);

  StageCompileOptions Opts;
  Opts.Stage = ShaderStage::Fragment;
  Opts.WaveSize = 4;
  Expected<PipelineResult> Result = runPipeline(*M, Opts);
  ASSERT_THAT_EXPECTED(Result, Succeeded());
  EXPECT_EQ(Result->Stage, ShaderStage::Fragment);
  EXPECT_TRUE(M->getFunction(Result->WrapperName));
}

// The same gap's DXIL-derived half: `dx.op.loadInput`/`storeOutput`
// (opcodes 4/5) are never raised by `feme::dxil::OpRaisingPass` (they need
// signature context that context-free pass does not have -- see
// `canonicalizeDXILStage`'s comment in CanonicalizeStage.cpp), so a DXIL
// import reaching this pipeline directly hit the exact same
// un-canonicalized-stage-IO gap SPIR-V import did. This attaches an
// `EntrySignature` the way `feme::dxil::SignatureImport` does at DXIL
// import time, then exercises a vector `storeOutput` the same way the
// SPIR-V test above does, to prove both import paths are now fixed by the
// same one-line pipeline change.
TEST(PipelineTest, CanonicalizesRawDXILStageIOBeforeWidening) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main() #0 {
      %v = call i32 @dx.op.loadInput.i32(i32 4, i32 0, i32 0, i8 0, i32 0)
      %f = sitofp i32 %v to float
      call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %f)
      call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %f)
      call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %f)
      call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %f)
      ret void
    }
    declare i32 @dx.op.loadInput.i32(i32, i32, i32, i8, i32)
    declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);

  Function *PS = M->getFunction("ps_main");
  ASSERT_TRUE(PS);
  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::SInt;
  Sig.Elements.push_back(In);
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Out.ComponentType = SignatureComponentType::Float;
  Out.ComponentCount = 4;
  Sig.Elements.push_back(Out);
  dxil::setEntrySignature(*PS, Sig);

  StageCompileOptions Opts;
  Opts.Stage = ShaderStage::Fragment;
  Opts.WaveSize = 4;
  Expected<PipelineResult> Result = runPipeline(*M, Opts);
  ASSERT_THAT_EXPECTED(Result, Succeeded());
  EXPECT_EQ(Result->Stage, ShaderStage::Fragment);
  EXPECT_TRUE(M->getFunction(Result->WrapperName));
}

} // namespace
