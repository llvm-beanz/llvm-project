//===- CanonicalizeStageTest.cpp - Tests for CanonicalizeStagePass -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/Graphics/CanonicalizeStage.h"

#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/DXIL/SignatureImport.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

#include <set>
#include <utility>

using namespace feme;
using namespace feme::graphics;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("CanonicalizeStageTest", errs());
  return M;
}

bool run(Module &M) {
  ModuleAnalysisManager MAM;
  PreservedAnalyses PA = CanonicalizeStagePass().run(M, MAM);
  return !PA.areAllPreserved();
}

/// Non-vertex/fragment entry points (here, compute) are left untouched: G0
/// scopes `feme.stage.*` to the vertex/fragment stages only.
TEST(CanonicalizeStageTest, LeavesNonGraphicsStagesAlone) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %v = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 2, i32 0)
      ret void
    }
    declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32)
    attributes #0 = { "feme.shader.stage"="compute" }
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(run(*M));
  Function *F = M->getFunction("main");
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI));
}

/// `loadInput`/`storeOutput` whose signature-ID operand cannot be resolved
/// (e.g. a fragment entry point with no `!feme.signature` at all) is left
/// unmodified, rather than crashing or guessing an ElementID.
TEST(CanonicalizeStageTest, UnresolvableLoadInputIsLeftAlone) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %v = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 2, i32 0)
      ret void
    }
    declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_FALSE(run(*M));
  Function *F = M->getFunction("main");
  bool SawLoadInput = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (CI->getCalledFunction() &&
          CI->getCalledFunction()->getName().starts_with("dx.op.loadInput"))
        SawLoadInput = true;
  EXPECT_TRUE(SawLoadInput);
}

/// `llvm.dx.discard` (already raised by `feme::dxil::OpRaisingPass`) becomes
/// `feme.stage.discard` in a fragment entry point, needing no signature at
/// all.
TEST(CanonicalizeStageTest, RaisesAlreadyRaisedDiscard) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      call void @llvm.dx.discard(i1 true)
      ret void
    }
    declare void @llvm.dx.discard(i1)
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  bool SawDiscard = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      StageOpKind Kind;
      if (isStageOpCall(*CI, &Kind)) {
        EXPECT_EQ(Kind, StageOpKind::Discard);
        SawDiscard = true;
      }
    }
  EXPECT_TRUE(SawDiscard);
}

/// A non-builtin SPIR-V `Input`/`Output` global's load/store rewrites to
/// `feme.stage.input.load`/`output.store`, and an `EntrySignature` is
/// attached recording its `Location`.
TEST(CanonicalizeStageTest, RewritesSPIRVStageIOAndBuildsSignature) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @in_var = external addrspace(7) constant float, !spirv.Decorations !0
    @out_var = external addrspace(8) global float, !spirv.Decorations !1
    define void @main() #0 {
      %v = load float, ptr addrspace(7) @in_var
      store float %v, ptr addrspace(8) @out_var
      ret void
    }
    attributes #0 = { "feme.shader.stage"="fragment" }
    !0 = !{!2}
    !1 = !{!3}
    !2 = !{i32 30, i32 2}
    !3 = !{i32 30, i32 5}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F->getMetadata("feme.signature"));
  unsigned SawLoad = 0, SawStore = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind))
      continue;
    if (Kind == StageOpKind::InputLoad)
      ++SawLoad;
    if (Kind == StageOpKind::OutputStore)
      ++SawStore;
  }
  EXPECT_EQ(SawLoad, 1u);
  EXPECT_EQ(SawStore, 1u);
}

/// A SPIR-V *graphics* builtin interface variable (a `BuiltIn` decoration,
/// code 11, rather than a `Location` one) becomes a system-value signature
/// element -- the identity the software rasterizer needs to find a vertex
/// stage's `SV_Position` output and a fragment stage's `SV_Depth` one
/// (roadmap V6's graphics stage compilation).
TEST(CanonicalizeStageTest, MapsSPIRVBuiltInsToSystemValues) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_VertexIndex = external addrspace(7) constant i32, !spirv.Decorations !0
    @gl_Position = external addrspace(8) global <4 x float>, !spirv.Decorations !1
    @out_var = external addrspace(8) global <4 x float>, !spirv.Decorations !2
    define void @main() #0 {
      %vid = load i32, ptr addrspace(7) @gl_VertexIndex
      %f = sitofp i32 %vid to float
      %v = insertelement <4 x float> poison, float %f, i32 0
      store <4 x float> %v, ptr addrspace(8) @gl_Position
      store <4 x float> %v, ptr addrspace(8) @out_var
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!3}
    !1 = !{!4}
    !2 = !{!5}
    !3 = !{i32 11, i32 42}
    !4 = !{i32 11, i32 0}
    !5 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 3u);

  const SignatureElement &VertexIndex = Sig->Elements[0];
  EXPECT_EQ(VertexIndex.Direction, SignatureDirection::Input);
  EXPECT_EQ(VertexIndex.SystemValue, SignatureSystemValue::VertexID);
  EXPECT_FALSE(VertexIndex.Location.has_value());

  const SignatureElement &Position = Sig->Elements[1];
  EXPECT_EQ(Position.Direction, SignatureDirection::Output);
  EXPECT_EQ(Position.SystemValue, SignatureSystemValue::Position);
  EXPECT_EQ(Position.ComponentCount, 4u);

  const SignatureElement &Varying = Sig->Elements[2];
  EXPECT_EQ(Varying.Direction, SignatureDirection::Output);
  EXPECT_EQ(Varying.SystemValue, SignatureSystemValue::None);
  ASSERT_TRUE(Varying.Location.has_value());
  EXPECT_EQ(*Varying.Location, 0u);
}

/// (Roadmap H2) `BuiltIn ViewIndex` (SPIR-V code 4440, `gl_ViewIndex`) maps
/// to `SignatureSystemValue::ViewIndex` -- the multiview render-pass
/// instance view a vertex/fragment invocation runs for, readable from
/// either stage (unlike `RenderTargetArrayIndex`/`gl_Layer`, a
/// vertex/geometry *output*).
TEST(CanonicalizeStageTest, MapsSPIRVViewIndexBuiltInToSystemValue) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_ViewIndex = external addrspace(7) constant i32, !spirv.Decorations !0
    @gl_Position = external addrspace(8) global <4 x float>, !spirv.Decorations !1
    define void @main() #0 {
      %vi = load i32, ptr addrspace(7) @gl_ViewIndex
      %f = sitofp i32 %vi to float
      %v = insertelement <4 x float> poison, float %f, i32 0
      store <4 x float> %v, ptr addrspace(8) @gl_Position
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!2}
    !1 = !{!3}
    !2 = !{i32 11, i32 4440}
    !3 = !{i32 11, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 2u);

  const SignatureElement &ViewIndex = Sig->Elements[0];
  EXPECT_EQ(ViewIndex.Direction, SignatureDirection::Input);
  EXPECT_EQ(ViewIndex.SystemValue, SignatureSystemValue::ViewIndex);
  EXPECT_FALSE(ViewIndex.Location.has_value());
}

/// (Roadmap C8) A matrix-typed `Output` global -- the `!llvm.array<Columns x
/// VectorType>` shape SPIRVToLLVM's `spirv.MatrixType` conversion produces
/// (see SPIRVToLLVMPatterns.cpp) -- gets a signature element with
/// `RowCount` set to its column count, and its store decomposes into one
/// `feme.stage.output.store` per (row, component) pair, each carrying the
/// matching constant `Row`/`Component` operand.
TEST(CanonicalizeStageTest, RewritesSPIRVMatrixOutputStoreOneRowAtATime) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_mat = external addrspace(8) global [3 x <3 x float>], !spirv.Decorations !0
    define void @main([3 x <3 x float>] %m) #0 {
      store [3 x <3 x float>] %m, ptr addrspace(8) @out_mat
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  const SignatureElement &Mat = Sig->Elements[0];
  EXPECT_EQ(Mat.Direction, SignatureDirection::Output);
  EXPECT_EQ(Mat.ComponentCount, 3u);
  EXPECT_EQ(Mat.RowCount, 3u);
  EXPECT_EQ(Mat.ComponentType, SignatureComponentType::Float);
  EXPECT_EQ(Mat.BitWidth, 32u);

  std::set<std::pair<uint64_t, uint64_t>> SeenRowComponent;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    std::optional<uint64_t> Row = getStageOpConstantOperand(*CI, 1);
    std::optional<uint64_t> Component = getStageOpConstantOperand(*CI, 2);
    ASSERT_TRUE(Row.has_value());
    ASSERT_TRUE(Component.has_value());
    SeenRowComponent.insert({*Row, *Component});
  }
  // Every one of the 3 rows' 3 components is stored exactly once.
  EXPECT_EQ(SeenRowComponent.size(), 9u);
  for (uint64_t Row = 0; Row != 3; ++Row)
    for (uint64_t Component = 0; Component != 3; ++Component)
      EXPECT_TRUE(SeenRowComponent.count({Row, Component}))
          << "row " << Row << " component " << Component;
}

/// (Roadmap C8) The load side of the same matrix shape: a matrix-typed
/// `Input` global's load decomposes into one `feme.stage.input.load` per
/// (row, component) pair, reassembled into the original `[Rows x VecTy]`
/// value with `insertvalue`/`insertelement`.
TEST(CanonicalizeStageTest, RewritesSPIRVMatrixInputLoadOneRowAtATime) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @in_mat = external addrspace(7) constant [2 x <4 x float>], !spirv.Decorations !0
    define [2 x <4 x float>] @main() #0 {
      %m = load [2 x <4 x float>], ptr addrspace(7) @in_mat
      ret [2 x <4 x float>] %m
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  const SignatureElement &Mat = Sig->Elements[0];
  EXPECT_EQ(Mat.Direction, SignatureDirection::Input);
  EXPECT_EQ(Mat.ComponentCount, 4u);
  EXPECT_EQ(Mat.RowCount, 2u);

  std::set<std::pair<uint64_t, uint64_t>> SeenRowComponent;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::InputLoad)
      continue;
    std::optional<uint64_t> Row = getStageOpConstantOperand(*CI, 1);
    std::optional<uint64_t> Component = getStageOpConstantOperand(*CI, 2);
    ASSERT_TRUE(Row.has_value());
    ASSERT_TRUE(Component.has_value());
    SeenRowComponent.insert({*Row, *Component});
  }
  EXPECT_EQ(SeenRowComponent.size(), 8u);
  for (uint64_t Row = 0; Row != 2; ++Row)
    for (uint64_t Component = 0; Component != 4; ++Component)
      EXPECT_TRUE(SeenRowComponent.count({Row, Component}))
          << "row " << Row << " component " << Component;

  // No raw array-typed load/store instruction should remain (it must have
  // been fully replaced by the per-row/component `feme.stage.input.load`
  // calls above, reassembled with insertvalue/insertelement).
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<LoadInst>(&I));
}

/// (Roadmap C8) glslang wraps a `varying`-block *member* -- even a matrix
/// one -- in an outer single-member struct at the SPIR-V level
/// (`dEQP-VK.glsl.linkage.varying.struct.*`'s own shape: a `mat4x2` member
/// becomes the LLVM type `{ [4 x <2 x float>] }`, confirmed against a real
/// `deqp-vk` run). This is the "aggregate" half of this milestone's
/// "matrix/aggregate stage IO" bucket the plain-matrix (bare `ArrayType`)
/// handling above does not by itself cover: the struct wrapper must be
/// peeled before the matrix inside it is recognized, or the whole struct
/// is treated as one opaque, wrongly-shaped "scalar" element.
TEST(CanonicalizeStageTest,
     RewritesSPIRVSingleMemberStructWrappedMatrixOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_wrapped = external addrspace(8) global { [4 x <2 x float>] }, !spirv.Decorations !0
    define void @main([4 x <2 x float>] %m) #0 {
      %wrapped = insertvalue { [4 x <2 x float>] } poison, [4 x <2 x float>] %m, 0
      store { [4 x <2 x float>] } %wrapped, ptr addrspace(8) @out_wrapped
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!1}
    !1 = !{i32 30, i32 1}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  const SignatureElement &Mat = Sig->Elements[0];
  EXPECT_EQ(Mat.Direction, SignatureDirection::Output);
  EXPECT_EQ(Mat.ComponentCount, 2u);
  EXPECT_EQ(Mat.RowCount, 4u);

  std::set<std::pair<uint64_t, uint64_t>> SeenRowComponent;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    std::optional<uint64_t> Row = getStageOpConstantOperand(*CI, 1);
    std::optional<uint64_t> Component = getStageOpConstantOperand(*CI, 2);
    ASSERT_TRUE(Row.has_value());
    ASSERT_TRUE(Component.has_value());
    SeenRowComponent.insert({*Row, *Component});
  }
  EXPECT_EQ(SeenRowComponent.size(), 8u);
  for (uint64_t Row = 0; Row != 4; ++Row)
    for (uint64_t Component = 0; Component != 2; ++Component)
      EXPECT_TRUE(SeenRowComponent.count({Row, Component}))
          << "row " << Row << " component " << Component;
}

} // namespace
