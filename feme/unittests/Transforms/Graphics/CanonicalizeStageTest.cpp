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
#include "feme/Graphics/StageStorage.h"
#include "feme/Transforms/DXIL/SignatureImport.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
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

/// A DXIL-shaped `dx.op.loadInput` call in a compute entry point is left
/// untouched: `canonicalizeDXILStage` (which alone understands that
/// intrinsic family) only ever runs for the vertex/fragment stages, and
/// (roadmap L69) compute's own participation in `canonicalizeSPIRVStage`
/// below only rewrites SPIR-V-raised stage-IO/discard/derivative/quad-read
/// constructs, none of which this DXIL-shaped input uses -- see
/// spirv-canonicalize-stage-raised-compute.ll for a compute entry point
/// whose SPIR-V-raised derivative calls *do* get rewritten.
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

/// (roadmap L69) A compute entry point's own `llvm.spv.ddx`/`.ddy` calls
/// (the raised form of `OpDPdx`/`OpDPdy`, which `VK_KHR_compute_shader_
/// derivatives` lets a compute shader use) now get the same
/// `feme.stage.derivative.*` rewrite a fragment entry's already did --
/// mirroring spirv-canonicalize-stage-raised-compute.ll's lit-level
/// coverage of the same fix, at the `CanonicalizeStagePass::run` dispatch
/// level this file's other tests exercise directly.
TEST(CanonicalizeStageTest, RewritesSPIRVDerivativesInComputeStage) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      %v = fadd float 1.0, 2.0
      %ddx = call float @llvm.spv.ddx.f32(float %v)
      ret void
    }
    declare float @llvm.spv.ddx.f32(float)
    attributes #0 = { "feme.shader.stage"="compute" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  bool SawDerivative = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      StageOpKind Kind;
      if (isStageOpCall(*CI, &Kind)) {
        EXPECT_EQ(Kind, StageOpKind::DerivativeXFine);
        SawDerivative = true;
      }
    }
  EXPECT_TRUE(SawDerivative);
}

/// (roadmap H101a) A GLSL/glslang-sourced helper function -- i.e. one with
/// no recognized `feme.shader.stage` attribute of its own, reached from a
/// fragment entry point via an ordinary `call`, not inlined -- still has
/// its own `llvm.spv.discard` rewritten to `feme.stage.discard`. Before
/// this fix, `CanonicalizeStagePass::run`'s dispatch loop skipped any
/// function without a recognized stage attribute entirely, so `@helper`'s
/// raw `llvm.spv.discard` survived unrewritten through
/// `feme::cpu::InlineHelperFunctionsPass`'s later inlining into `@main`
/// and reached instruction selection unconverted, crashing with `LLVM
/// ERROR: Cannot select: intrinsic %llvm.spv.discard` -- exactly the
/// `dEQP-VK.graphicsfuzz.call-function-with-discard` failure this
/// milestone fixes.
TEST(CanonicalizeStageTest, RewritesSPIRVDiscardInNonEntryHelperFunction) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @helper() {
      call void @llvm.spv.discard()
      ret void
    }
    define void @main() #0 {
      call void @helper()
      ret void
    }
    declare void @llvm.spv.discard()
    attributes #0 = { "feme.shader.stage"="fragment" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *Helper = M->getFunction("helper");
  ASSERT_TRUE(Helper);
  bool SawDiscard = false;
  for (Instruction &I : instructions(Helper))
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      EXPECT_FALSE(CI->getCalledFunction() &&
                   CI->getCalledFunction()->getIntrinsicID() ==
                       Intrinsic::spv_discard);
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

/// (Roadmap H21a) `VK_EXT_transform_feedback`'s own `XfbBuffer` (36),
/// `Offset` (35, reused for its transform-feedback meaning) and
/// `XfbStride` (37) decorations map onto `SignatureElement::XfbBuffer`/
/// `XfbOffset`/`XfbStride` for an `Output` variable that carries them, and
/// leave `XfbBuffer` unset (`std::nullopt`) for one that does not (e.g.
/// `gl_Position`, decorated only with `BuiltIn` here, never captured by a
/// real shader). This test only exercises the reflection plumbing itself
/// -- no feature bit is flipped and no buffer is actually written yet
/// (that is a later roadmap H21 row).
TEST(CanonicalizeStageTest, MapsXfbDecorationsToTransformFeedbackCapture) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_Position = external addrspace(8) global <4 x float>, !spirv.Decorations !0
    @out_var = external addrspace(8) global <4 x float>, !spirv.Decorations !1
    define void @main() #0 {
      %v = load <4 x float>, ptr addrspace(8) @gl_Position
      store <4 x float> %v, ptr addrspace(8) @gl_Position
      store <4 x float> %v, ptr addrspace(8) @out_var
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!2}
    !1 = !{!3, !4, !5, !6}
    !2 = !{i32 11, i32 0}
    !3 = !{i32 30, i32 0}
    !4 = !{i32 36, i32 1}
    !5 = !{i32 35, i32 16}
    !6 = !{i32 37, i32 32}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 2u);

  const SignatureElement &Position = Sig->Elements[0];
  EXPECT_EQ(Position.SystemValue, SignatureSystemValue::Position);
  EXPECT_FALSE(Position.XfbBuffer.has_value());
  EXPECT_EQ(Position.XfbOffset, 0u);
  EXPECT_EQ(Position.XfbStride, 0u);

  const SignatureElement &Varying = Sig->Elements[1];
  ASSERT_TRUE(Varying.Location.has_value());
  EXPECT_EQ(*Varying.Location, 0u);
  ASSERT_TRUE(Varying.XfbBuffer.has_value());
  EXPECT_EQ(*Varying.XfbBuffer, 1u);
  EXPECT_EQ(Varying.XfbOffset, 16u);
  EXPECT_EQ(Varying.XfbStride, 32u);
}

/// (Roadmap H101c) GLSL's own "array of block instances" syntax
/// (`layout(xfb_buffer=0, ...) out Block { uvec4 a; } block[3];`) is
/// GLSL/SPIR-V's own model for 3 *independently*-captured transform-
/// feedback streams sharing one interface-block type -- each `block[k]`
/// targets its own buffer `XfbBuffer + k` (the Vulkan spec's own model
/// for this shape), never `RowCount`-many rows packed back-to-back
/// within one buffer the way a real matrix or a block member's own
/// array member already is. `SignatureElement::XfbBufferArrayStride`
/// records the block's one member's own row count (1 here, for a plain
/// `uvec4` member) so `Executor.cpp`'s `captureTransformFeedback` can
/// recover each instance's own row and route it to its own buffer. This
/// case's `RowCount` (3, the instance count) already equals
/// `XfbBufferArrayStride` (1) times the instance count, so this is the
/// simplest sub-case: no matrix-row splitting is needed within one
/// instance.
TEST(CanonicalizeStageTest,
    MapsArrayOfBlockInstancesWithSimpleMemberToXfbBufferArrayStride) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @block = external addrspace(8) global [3 x { <4 x i32> }], !spirv.Decorations !4, !feme.spirv.MemberDecorations !8
    define void @main() #0 {
      store <4 x i32> <i32 1, i32 2, i32 3, i32 4>, ptr addrspace(8) @block
      store <4 x i32> <i32 5, i32 6, i32 7, i32 8>, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @block, i64 16)
      store <4 x i32> <i32 9, i32 10, i32 11, i32 12>, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @block, i64 32)
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !1 = !{i32 30, i32 0}
    !2 = !{i32 36, i32 0}
    !3 = !{i32 37, i32 16}
    !4 = !{!1, !2, !3}
    !5 = !{i32 35, i32 0}
    !6 = !{!5}
    !7 = !{i32 0, !6}
    !8 = !{!7}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);

  const SignatureElement &Elt = Sig->Elements[0];
  EXPECT_EQ(Elt.RowCount, 3u);
  EXPECT_EQ(Elt.XfbBufferArrayStride, 1u);
  ASSERT_TRUE(Elt.XfbBuffer.has_value());
  EXPECT_EQ(*Elt.XfbBuffer, 0u);

  // Each instance's own store resolves to its own `Row` (0, 1, 2) --
  // `Executor.cpp` recovers the buffer index directly from `Row` here
  // (`XfbBufferArrayStride == 1` means `Row` already *is* the instance
  // index).
  std::set<uint64_t> SeenRows;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    std::optional<uint64_t> Row = getStageOpConstantOperand(*CI, 1);
    ASSERT_TRUE(Row.has_value());
    SeenRows.insert(*Row);
  }
  EXPECT_EQ(SeenRows, (std::set<uint64_t>{0, 1, 2}));
}

/// (Roadmap H101c) The harder sub-case of the same "array of block
/// instances" shape: the block's one member is itself a matrix
/// (`layout(xfb_buffer=0, ...) out Block { mat4 var; } block[3];`, the
/// exact shape `dEQP-VK.transform_feedback.fuzz.
/// instance_array_basic_type.mat4.{geometry,vertex}` take). Now
/// `getStageIORowShape` flattens `RowCount` to 12 (3 instances * 4
/// matrix rows/columns), conflating the instance index and the matrix's
/// own column index into one `Row` -- `XfbBufferArrayStride` (4, the
/// matrix's own column count) is what lets `Executor.cpp` split `Row`
/// back into `(Instance, InnerRow) = (Row / 4, Row % 4)`.
///
/// Before this milestone's own `resolveRowComponent`/`storeStageIOValue`
/// fix, a whole-matrix store's own base `Row` (the instance index,
/// resolved by `resolveRowComponent`) was double-counted against the
/// matrix's own column index a second time by `storeStageIOValue`'s
/// array-decomposition recursion (which unconditionally overwrote,
/// rather than combined with, its own incoming `Row`), producing rows
/// far out of `RowCount`'s own range (e.g. 16-19 instead of 4-7 for
/// instance 1) -- `dEQP-VK.transform_feedback.fuzz.
/// instance_array_basic_type.mat4.vertex`'s own `feme-graphics-
/// validate-stage` failure ("row 19 is out of range for element 0")
/// exposed this.
TEST(CanonicalizeStageTest,
    MapsArrayOfBlockInstancesWithMatrixMemberToXfbBufferArrayStride) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @block = external addrspace(8) global [3 x { [4 x <4 x float>] }], !spirv.Decorations !4, !feme.spirv.MemberDecorations !8
    define void @main() #0 {
      store [4 x <4 x float>] [<4 x float> <float 1.0, float 2.0, float 3.0, float 4.0>, <4 x float> <float 5.0, float 6.0, float 7.0, float 8.0>, <4 x float> <float 9.0, float 10.0, float 11.0, float 12.0>, <4 x float> <float 13.0, float 14.0, float 15.0, float 16.0>], ptr addrspace(8) @block
      store [4 x <4 x float>] [<4 x float> <float 17.0, float 18.0, float 19.0, float 20.0>, <4 x float> <float 21.0, float 22.0, float 23.0, float 24.0>, <4 x float> <float 25.0, float 26.0, float 27.0, float 28.0>, <4 x float> <float 29.0, float 30.0, float 31.0, float 32.0>], ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @block, i64 64)
      store [4 x <4 x float>] [<4 x float> <float 33.0, float 34.0, float 35.0, float 36.0>, <4 x float> <float 37.0, float 38.0, float 39.0, float 40.0>, <4 x float> <float 41.0, float 42.0, float 43.0, float 44.0>, <4 x float> <float 45.0, float 46.0, float 47.0, float 48.0>], ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @block, i64 128)
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !1 = !{i32 30, i32 0}
    !2 = !{i32 36, i32 0}
    !3 = !{i32 37, i32 64}
    !4 = !{!1, !2, !3}
    !5 = !{i32 35, i32 0}
    !6 = !{!5}
    !7 = !{i32 0, !6}
    !8 = !{!7}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);

  const SignatureElement &Elt = Sig->Elements[0];
  EXPECT_EQ(Elt.RowCount, 12u);
  EXPECT_EQ(Elt.XfbBufferArrayStride, 4u);
  ASSERT_TRUE(Elt.XfbBuffer.has_value());
  EXPECT_EQ(*Elt.XfbBuffer, 0u);

  // Instance 0's own 4 columns resolve to rows 0-3, instance 1's to
  // rows 4-7, instance 2's to rows 8-11 -- never double-counted (e.g.
  // instance 1 at rows 16-19) and never collapsed onto instance 0's own
  // rows.
  std::set<uint64_t> SeenRows;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    std::optional<uint64_t> Row = getStageOpConstantOperand(*CI, 1);
    ASSERT_TRUE(Row.has_value());
    SeenRows.insert(*Row);
  }
  EXPECT_EQ(SeenRows,
           (std::set<uint64_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}));
}

/// (Roadmap H101j) The same "array of block instances with a matrix
/// member" shape as
/// `MapsArrayOfBlockInstancesWithMatrixMemberToXfbBufferArrayStride`
/// above, but with the block's matrix member declared using the *tight*
/// `[4 x [4 x float]]` array-of-scalar-array shape
/// `SPIRVToLLVMPatterns.cpp`'s "tight vector" retry (roadmap H101i)
/// substitutes for the ordinary `[4 x <4 x float>]` array-of-real-vector
/// shape whenever the real vector's own ABI-aligned placement can't
/// reproduce a struct member's actual, tightly-packed declared offset --
/// exactly the shape `dEQP-VK.transform_feedback.fuzz.*instance_array*`
/// hits at pipeline creation (`feme-graphics-validate-stage`'s own
/// "component N is out of range for element M" diagnostic). Each whole-
/// matrix store's own *value* type remains the ordinary, real
/// `[4 x <4 x float>]` -- SPIRVToLLVM's independent, unmodified
/// `spirv::MatrixType` conversion registration produces this for any
/// standalone matrix value, regardless of what a struct member
/// substitutes its own declared type to -- so this deliberately exercises
/// the exact type-identity split (declared member type vs. real value
/// type) `getStageIORowShape`/`resolveRowComponent`'s own H101j fix
/// reconciles. The declared member type wraps its tight `[4 x [4 x
/// float]]` array in the named `%feme.tight_vector` struct
/// `SPIRVToLLVMPatterns.cpp`'s own `getTightVectorArrayType` marker uses,
/// mirroring the real shape that fix produces; without recognizing this
/// marker, this signature computed a bogus `RowCount` of 48 (12 real rows
/// * 4 more, the tight array's own inner scalar level folded into
/// `RowCount` a second time instead of treated as `ComponentCount`), and
/// `resolveRowComponent`'s own then-`PerRowTy == ValueTy` exact-type check
/// never matched the real, value-side `[4 x <4 x float>]`, producing
/// wildly wrong `(Row, Component)` pairs -- and, before the marker itself,
/// a plain (unmarked) `[4 x [4 x float]]` was indistinguishable from a
/// genuinely-declared 2-level scalar array, regressing
/// `dEQP-VK.transform_feedback.fuzz.2_level_array.*`.
TEST(CanonicalizeStageTest,
    MapsArrayOfBlockInstancesWithTightMatrixMemberToXfbBufferArrayStride) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    %feme.tight_vector = type { [4 x float] }
    @block = external addrspace(8) global [3 x { [4 x %feme.tight_vector] }], !spirv.Decorations !4, !feme.spirv.MemberDecorations !8
    define void @main() #0 {
      store [4 x <4 x float>] [<4 x float> <float 1.0, float 2.0, float 3.0, float 4.0>, <4 x float> <float 5.0, float 6.0, float 7.0, float 8.0>, <4 x float> <float 9.0, float 10.0, float 11.0, float 12.0>, <4 x float> <float 13.0, float 14.0, float 15.0, float 16.0>], ptr addrspace(8) @block
      store [4 x <4 x float>] [<4 x float> <float 17.0, float 18.0, float 19.0, float 20.0>, <4 x float> <float 21.0, float 22.0, float 23.0, float 24.0>, <4 x float> <float 25.0, float 26.0, float 27.0, float 28.0>, <4 x float> <float 29.0, float 30.0, float 31.0, float 32.0>], ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @block, i64 64)
      store [4 x <4 x float>] [<4 x float> <float 33.0, float 34.0, float 35.0, float 36.0>, <4 x float> <float 37.0, float 38.0, float 39.0, float 40.0>, <4 x float> <float 41.0, float 42.0, float 43.0, float 44.0>, <4 x float> <float 45.0, float 46.0, float 47.0, float 48.0>], ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @block, i64 128)
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !1 = !{i32 30, i32 0}
    !2 = !{i32 36, i32 0}
    !3 = !{i32 37, i32 64}
    !4 = !{!1, !2, !3}
    !5 = !{i32 35, i32 0}
    !6 = !{!5}
    !7 = !{i32 0, !6}
    !8 = !{!7}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);

  const SignatureElement &Elt = Sig->Elements[0];
  // Exactly like the real-vector-column shape above: 3 instances * 4
  // matrix rows/columns = 12, not 48 (the bogus row-count a "fold every
  // array level into RowCount" reading of the tight shape would produce).
  EXPECT_EQ(Elt.RowCount, 12u);
  EXPECT_EQ(Elt.ComponentCount, 4u);
  EXPECT_EQ(Elt.XfbBufferArrayStride, 4u);
  ASSERT_TRUE(Elt.XfbBuffer.has_value());
  EXPECT_EQ(*Elt.XfbBuffer, 0u);

  // Instance 0's own 4 columns resolve to rows 0-3, instance 1's to
  // rows 4-7, instance 2's to rows 8-11 -- the same result as the
  // real-vector-column shape, despite the member's own declared type
  // differing.
  std::set<uint64_t> SeenRows;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    std::optional<uint64_t> Row = getStageOpConstantOperand(*CI, 1);
    ASSERT_TRUE(Row.has_value());
    SeenRows.insert(*Row);
  }
  EXPECT_EQ(SeenRows,
           (std::set<uint64_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}));
}

/// (Roadmap H101j) The tight-array shape as it arises for a *non-matrix*
/// array-of-vectors member (e.g. `layout(xfb_buffer=0, ...) out Block {
/// ivec2 var[2]; } block;`, a single block instance -- no array-of-
/// instances wrapper -- with one member that is itself an array of
/// 2-component vectors): `SPIRVToLLVMPatterns.cpp`'s tight-vector retry
/// can substitute the member's own declared type with a marker-wrapped
/// `[2 x %feme.tight_vector]` in place of the ordinary `[2 x <2 x i32>]`,
/// exactly like the matrix case above but without any surrounding
/// array-of-block-instances dimension. A single whole-array store's own
/// value type remains the real `[2 x <2 x i32>]`.
TEST(CanonicalizeStageTest,
    MapsTightArrayOfVectorsMemberToRowAndComponentCount) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    %feme.tight_vector = type { [2 x i32] }
    @block = external addrspace(8) global { [2 x %feme.tight_vector] }, !spirv.Decorations !4, !feme.spirv.MemberDecorations !8
    define void @main() #0 {
      store [2 x <2 x i32>] [<2 x i32> <i32 1, i32 2>, <2 x i32> <i32 3, i32 4>], ptr addrspace(8) @block
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !1 = !{i32 30, i32 0}
    !2 = !{i32 36, i32 0}
    !3 = !{i32 37, i32 8}
    !4 = !{!1, !2, !3}
    !5 = !{i32 35, i32 0}
    !6 = !{!5}
    !7 = !{i32 0, !6}
    !8 = !{!7}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);

  const SignatureElement &Elt = Sig->Elements[0];
  EXPECT_EQ(Elt.RowCount, 2u);
  EXPECT_EQ(Elt.ComponentCount, 2u);

  std::set<uint64_t> SeenRows;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    std::optional<uint64_t> Row = getStageOpConstantOperand(*CI, 1);
    ASSERT_TRUE(Row.has_value());
    SeenRows.insert(*Row);
  }
  EXPECT_EQ(SeenRows, (std::set<uint64_t>{0, 1}));
}

/// (Roadmap H101j) The critical negative case the marker struct exists
/// to distinguish from the tight-vector cases above: a genuinely
/// directly-declared 2-level scalar array (e.g. `float xs[2][2]`,
/// `dEQP-VK.transform_feedback.fuzz.2_level_array.float`'s own shape --
/// discovered, during this milestone's own closing regression sweep, to
/// be an actual real-world shape this codebase's own CTS suite exercises,
/// not merely a hypothetical). Bit-for-bit, `[2 x [2 x float]]` here is
/// identical to the tight-matrix-column case above's own inner shape --
/// only the *absence* of the `%feme.tight_vector` marker tells them
/// apart. Before the marker fix (an earlier, purely positional "first
/// array is a row, any later array-of-scalar is a component" heuristic),
/// this shape was misclassified as `RowCount=2, ComponentCount=2` instead
/// of the correct `RowCount=4, ComponentCount=1` -- a regression this
/// test guards against.
TEST(CanonicalizeStageTest,
    DoesNotMisclassifyGenuineTwoLevelScalarArrayAsTightVector) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @xs = external addrspace(8) global [2 x [2 x float]], !spirv.Decorations !0
    define void @main() #0 {
      store [2 x [2 x float]] [[2 x float] [float 1.0, float 2.0], [2 x float] [float 3.0, float 4.0]], ptr addrspace(8) @xs
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

  const SignatureElement &Elt = Sig->Elements[0];
  EXPECT_EQ(Elt.RowCount, 4u);
  EXPECT_EQ(Elt.ComponentCount, 1u);

  std::set<uint64_t> SeenRows;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    std::optional<uint64_t> Row = getStageOpConstantOperand(*CI, 1);
    ASSERT_TRUE(Row.has_value());
    SeenRows.insert(*Row);
  }
  EXPECT_EQ(SeenRows, (std::set<uint64_t>{0, 1, 2, 3}));
}

/// (Roadmap H101h) The same "array of block instances" shape as
/// `MapsArrayOfBlockInstancesWithSimpleMemberToXfbBufferArrayStride`
/// above, but with the block's one member a *narrow* (3-wide,
/// non-power-of-two) vector (`layout(xfb_buffer=0, ...) out Block {
/// ivec3 var; } block[3];`, the exact shape `dEQP-VK.transform_feedback.
/// fuzz.instance_array_basic_type.ivec3.{geometry,vertex}` take) rather
/// than a 4-wide one. `{<3 x i32>}`'s own `DataLayout::
/// getTypeAllocSize` is 16 (LLVM pads a 3-wide vector up to a 4-wide
/// SIMD register's worth), but the real SPIR-V-imported array addresses
/// each instance only 12 bytes apart -- `ivec3`'s own tightly packed
/// 3-`i32` size, with no trailing padding between instances. Before this
/// row's own fix, `resolveRowComponent`'s array-peeling loop divided
/// each instance's own real byte offset (0, 12, 24) by the padded 16
/// instead of the packed 12, resolving instances 1 and 2 to `Row`s 0 and
/// 1 respectively instead of 1 and 2 -- instance 2's own store silently
/// overwrote instance 0's own row instead of landing in its own, exactly
/// this milestone's own "received a different row's own value" symptom.
/// Fixed by reusing `getPackedElementSize` (originally added for
/// `FoldsConstantVertexIndexIntoMultiMemberInterfaceBlockOutputStore`'s
/// mesh-specific gap, roadmap H6l) for this array-peeling loop's own
/// `RowSize`, since the same ABI-padding-vs-tight-packing gap turns out
/// not to be mesh-specific after all.
TEST(CanonicalizeStageTest,
    MapsArrayOfBlockInstancesWithNarrowVectorMemberToDistinctRows) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @block = external addrspace(8) global [3 x { <3 x i32> }], !spirv.Decorations !4, !feme.spirv.MemberDecorations !8
    define void @main() #0 {
      store <3 x i32> <i32 1, i32 2, i32 3>, ptr addrspace(8) @block
      store <3 x i32> <i32 4, i32 5, i32 6>, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @block, i64 12)
      store <3 x i32> <i32 7, i32 8, i32 9>, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @block, i64 24)
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !1 = !{i32 30, i32 0}
    !2 = !{i32 36, i32 0}
    !3 = !{i32 37, i32 12}
    !4 = !{!1, !2, !3}
    !5 = !{i32 35, i32 0}
    !6 = !{!5}
    !7 = !{i32 0, !6}
    !8 = !{!7}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);

  const SignatureElement &Elt = Sig->Elements[0];
  EXPECT_EQ(Elt.RowCount, 3u);
  EXPECT_EQ(Elt.XfbBufferArrayStride, 1u);
  ASSERT_TRUE(Elt.XfbBuffer.has_value());
  EXPECT_EQ(*Elt.XfbBuffer, 0u);

  // Each instance's own store must resolve to its own distinct `Row`
  // (0, 1, 2) -- not 0, 0, 1, the wrong result `getTypeAllocSize`'s
  // 16-byte padded stride produced before this fix.
  std::set<uint64_t> SeenRows;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    std::optional<uint64_t> Row = getStageOpConstantOperand(*CI, 1);
    ASSERT_TRUE(Row.has_value());
    SeenRows.insert(*Row);
  }
  EXPECT_EQ(SeenRows, (std::set<uint64_t>{0, 1, 2}));
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

/// (Roadmap H7e) `BuiltIn PointSize` (SPIR-V code 1, `gl_PointSize`) maps
/// to `SignatureSystemValue::PointSize` -- the last pre-rasterization
/// stage's own vertex output the executor's point-topology quad expansion
/// reads to derive a point primitive's screen-space size (`largePoints`).
TEST(CanonicalizeStageTest, MapsSPIRVPointSizeBuiltInToSystemValue) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_PointSize = external addrspace(8) global float, !spirv.Decorations !0
    @gl_Position = external addrspace(8) global <4 x float>, !spirv.Decorations !1
    define void @main() #0 {
      store float 4.0, ptr addrspace(8) @gl_PointSize
      store <4 x float> zeroinitializer, ptr addrspace(8) @gl_Position
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!2}
    !1 = !{!3}
    !2 = !{i32 11, i32 1}
    !3 = !{i32 11, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 2u);

  const SignatureElement &PointSize = Sig->Elements[0];
  EXPECT_EQ(PointSize.Direction, SignatureDirection::Output);
  EXPECT_EQ(PointSize.SystemValue, SignatureSystemValue::PointSize);
  EXPECT_FALSE(PointSize.Location.has_value());
}

/// (Roadmap H7h) `BuiltIn ClipDistance`/`CullDistance` (SPIR-V codes 3/4,
/// `gl_ClipDistance`/`gl_CullDistance`) map to
/// `SignatureSystemValue::ClipDistance`/`CullDistance` -- each a real,
/// array-shaped (`RowCount` many planes) vertex-stage output
/// `Executor.cpp`'s clip/cull consumers read.
TEST(CanonicalizeStageTest, MapsSPIRVClipCullDistanceBuiltInsToSystemValues) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_ClipDistance = external addrspace(8) global [3 x float], !spirv.Decorations !0
    @gl_CullDistance = external addrspace(8) global [2 x float], !spirv.Decorations !1
    define void @main() #0 {
      store [3 x float] zeroinitializer, ptr addrspace(8) @gl_ClipDistance
      store [2 x float] zeroinitializer, ptr addrspace(8) @gl_CullDistance
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!2}
    !1 = !{!3}
    !2 = !{i32 11, i32 3}
    !3 = !{i32 11, i32 4}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 2u);

  const SignatureElement &ClipDistance = Sig->Elements[0];
  EXPECT_EQ(ClipDistance.Direction, SignatureDirection::Output);
  EXPECT_EQ(ClipDistance.SystemValue, SignatureSystemValue::ClipDistance);
  EXPECT_EQ(ClipDistance.RowCount, 3u);
  EXPECT_EQ(ClipDistance.ComponentType, SignatureComponentType::Float);

  const SignatureElement &CullDistance = Sig->Elements[1];
  EXPECT_EQ(CullDistance.Direction, SignatureDirection::Output);
  EXPECT_EQ(CullDistance.SystemValue, SignatureSystemValue::CullDistance);
  EXPECT_EQ(CullDistance.RowCount, 2u);
  EXPECT_EQ(CullDistance.ComponentType, SignatureComponentType::Float);
}

/// (Roadmap H2a) glslang emits `gl_Position`/`gl_PointSize`/
/// `gl_ClipDistance`/`gl_CullDistance` as members of an implicit
/// `gl_PerVertex` interface *block* (a struct-typed `Output` variable)
/// rather than as their own standalone globals -- unlike
/// `MapsSPIRVBuiltInsToSystemValues`'s idealized `@gl_Position` above, which
/// (incorrectly, per this finding) modeled it as one. A struct-typed
/// stage-IO global carrying *no* decoration metadata at all -- neither a
/// whole-variable `!spirv.Decorations` nor (roadmap H2c) a per-member
/// `feme.spirv.MemberDecorations` -- is still not a recognized stage-IO
/// global (`isSPIRVStageIOGlobal` requires one or the other), so its store
/// is left untouched; see `RecognizesMemberDecoratedInterfaceBlockAsStageIO`
/// below for the real `gl_PerVertex` shape H2c's own SPIR-V import
/// actually produces (per-member decorations present), which H2d now does
/// decompose.
TEST(CanonicalizeStageTest,
     DoesNotRecognizeUndecoratedInterfaceBlockAsStageIO) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_PerVertex = external addrspace(8) global { <4 x float>, float, [1 x float], [1 x float] }
    define void @main() #0 {
      %pos = load <4 x float>, ptr addrspace(7) @in_pos
      store <4 x float> %pos, ptr addrspace(8) @gl_PerVertex
      ret void
    }
    @in_pos = external addrspace(7) constant <4 x float>, !spirv.Decorations !0
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  // `in_pos` is still legalized (a plain, whole-variable-decorated `Input`),
  // so *something* changes -- but the `gl_PerVertex` store itself must not.
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  bool SawRawGlPerVertexStore = false;
  for (Instruction &I : instructions(F))
    if (auto *SI = dyn_cast<StoreInst>(&I))
      if (auto *GV = dyn_cast<GlobalVariable>(SI->getPointerOperand()))
        if (GV->getName() == "gl_PerVertex")
          SawRawGlPerVertexStore = true;
  EXPECT_TRUE(SawRawGlPerVertexStore);
}

/// (Roadmap H2d) The real shape H2c's own SPIR-V import produces for
/// `gl_PerVertex`: a struct-typed `Output` global carrying no
/// whole-variable `!spirv.Decorations` but a per-member
/// `feme.spirv.MemberDecorations` one (see StageIODecorations.cpp),
/// decoding member 0 as `BuiltIn Position` (11, 0), member 1 as `BuiltIn
/// PointSize` (11, 1), member 2 as `BuiltIn ClipDistance` (11, 3) and
/// member 3 as `BuiltIn CullDistance` (11, 4) -- the same four `OpMember
/// Decorate`s glslang always emits. `isSPIRVStageIOGlobal` now recognizes
/// this shape, and `canonicalizeSPIRVStage` decomposes it into one
/// `SignatureElement` per member (closing the gap
/// `DoesNotRecognizeUndecoratedInterfaceBlockAsStageIO` used to document
/// as `DoesNotRecognizeMemberDecoratedInterfaceBlockAsStageIO`, before this
/// fixture was corrected to carry the metadata H2c's own writer actually
/// attaches).
TEST(CanonicalizeStageTest, RecognizesMemberDecoratedInterfaceBlockAsStageIO) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_PerVertex = external addrspace(8) global { <4 x float>, float, [1 x float], [1 x float] }, !feme.spirv.MemberDecorations !10
    @in_pos = external addrspace(7) constant <4 x float>, !spirv.Decorations !0
    define void @main() #0 {
      %pos = load <4 x float>, ptr addrspace(7) @in_pos
      %agg0 = insertvalue { <4 x float>, float, [1 x float], [1 x float] } poison, <4 x float> %pos, 0
      %agg1 = insertvalue { <4 x float>, float, [1 x float], [1 x float] } %agg0, float 1.000000e+00, 1
      store { <4 x float>, float, [1 x float], [1 x float] } %agg1, ptr addrspace(8) @gl_PerVertex
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
    !10 = !{!11, !12, !13, !14}
    !11 = !{i32 0, !15}
    !12 = !{i32 1, !16}
    !13 = !{i32 2, !17}
    !14 = !{i32 3, !18}
    !15 = !{!19}
    !19 = !{i32 11, i32 0}
    !16 = !{!20}
    !20 = !{i32 11, i32 1}
    !17 = !{!21}
    !21 = !{i32 11, i32 3}
    !18 = !{!22}
    !22 = !{i32 11, i32 4}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  // The raw struct store on `gl_PerVertex` must be gone.
  for (Instruction &I : instructions(F))
    if (auto *SI = dyn_cast<StoreInst>(&I))
      if (auto *GV = dyn_cast<GlobalVariable>(SI->getPointerOperand()))
        EXPECT_NE(GV->getName(), "gl_PerVertex");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  // `in_pos` (1) + one element per `gl_PerVertex` member (4).
  ASSERT_EQ(Sig->Elements.size(), 5u);

  const SignatureElement &Position = Sig->Elements[1];
  EXPECT_EQ(Position.Direction, SignatureDirection::Output);
  EXPECT_EQ(Position.SystemValue, SignatureSystemValue::Position);
  EXPECT_EQ(Position.ComponentCount, 4u);

  // (Roadmap H7e) `PointSize` (member 1) maps to
  // `SignatureSystemValue::PointSize`. (Roadmap H7h) `ClipDistance`/
  // `CullDistance` (members 2/3) now map to their own system values too,
  // each a real `Executor.cpp` clip-plane/cull-plane consumer
  // (`shaderClipDistance`/`shaderCullDistance`, `PhysicalDeviceInfo.cpp`,
  // are now `VK_TRUE`).
  EXPECT_EQ(Sig->Elements[2].SystemValue, SignatureSystemValue::PointSize);
  EXPECT_EQ(Sig->Elements[3].SystemValue, SignatureSystemValue::ClipDistance);
  EXPECT_EQ(Sig->Elements[4].SystemValue, SignatureSystemValue::CullDistance);

  unsigned StoreCount = 0;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      StageOpKind Kind;
      if (isStageOpCall(*CI, &Kind) && Kind == StageOpKind::OutputStore)
        ++StoreCount;
    }
  // Position (a 4-component vector) decomposes into 4 stores; PointSize
  // (a scalar) and ClipDistance/CullDistance (each a 1-element array) into
  // 1 each -- see `loadStageIOValue`/`storeStageIOValue`'s own row/
  // component recursion.
  EXPECT_EQ(StoreCount, 7u);
}

/// (Roadmap H2d) The shape a real `dEQP-VK.multiview` vertex shader's
/// `gl_PerVertex` access actually takes, confirmed by inspecting the IR
/// `canonicalizeSPIRVStage` receives for a real `deqp-vk` shader (not the
/// whole-struct aggregate `RecognizesMemberDecoratedInterfaceBlockAsStageIO`
/// above exercises, which never occurs in practice): each member -- and
/// even each individual component of `gl_Position` -- is addressed by its
/// own scalar load/store, either a bare `@gl_PerVertex` global (member 0,
/// component 0 -- SPIR-V's own offset-0 access, which LLVM's constant-
/// `getelementptr` folding erases entirely) or a `getelementptr (i8, ptr
/// @gl_PerVertex, i64 ByteOffset)` `ConstantExpr` (every other member/
/// component -- LLVM's own canonical byte-offset form, not the struct-
/// member-indexed shape `getelementptr StructTy, ptr @block, i32 0, i32 M`
/// might suggest). `resolveStageIOAccess`/`getStageIOBaseAndOffset` resolve
/// each of these back to its own `ElementID` and `(Row, Component)` pair
/// via the block's own `StructLayout` (`{<4 x float>, float, [1 x float],
/// [1 x float]}`: `Position` at byte 0, `PointSize` at 16, `ClipDistance`
/// at 20, `CullDistance` at 24).
TEST(CanonicalizeStageTest, RecognizesInterfaceBlockPerMemberByteOffsetAccess) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_PerVertex = external addrspace(8) global { <4 x float>, float, [1 x float], [1 x float] }, !feme.spirv.MemberDecorations !10
    define void @main() #0 {
      ; gl_Position.x = 1.0 (member 0, component 0 -- the offset-0 access
      ; that folds down to a bare global).
      store float 1.000000e+00, ptr addrspace(8) @gl_PerVertex
      ; gl_Position.y = 2.0 (member 0, component 1 -- byte offset 4).
      store float 2.000000e+00, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @gl_PerVertex, i64 4)
      ; gl_PointSize = 3.0 (member 1, whole value -- byte offset 16).
      store float 3.000000e+00, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @gl_PerVertex, i64 16)
      ; gl_ClipDistance[0] = 4.0 (member 2, row 0 -- byte offset 20).
      store float 4.000000e+00, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @gl_PerVertex, i64 20)
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !10 = !{!11, !12, !13, !14}
    !11 = !{i32 0, !15}
    !12 = !{i32 1, !16}
    !13 = !{i32 2, !17}
    !14 = !{i32 3, !18}
    !15 = !{!19}
    !19 = !{i32 11, i32 0}
    !16 = !{!20}
    !20 = !{i32 11, i32 1}
    !17 = !{!21}
    !21 = !{i32 11, i32 3}
    !18 = !{!22}
    !22 = !{i32 11, i32 4}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  // No raw store on `gl_PerVertex` (bare or via `getelementptr`) survives.
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));

  struct Store {
    uint64_t ElementID, Row, Component;
  };
  SmallVector<Store> Stores;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      StageOpKind Kind;
      if (!isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
        continue;
      Stores.push_back(
          {cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(),
           cast<ConstantInt>(CI->getArgOperand(1))->getZExtValue(),
           cast<ConstantInt>(CI->getArgOperand(2))->getZExtValue()});
    }
  ASSERT_EQ(Stores.size(), 4u);
  // `gl_Position` (element 0), `PointSize` (1), `ClipDistance` (2).
  EXPECT_EQ(Stores[0].ElementID, 0u);
  EXPECT_EQ(Stores[0].Component, 0u);
  EXPECT_EQ(Stores[1].ElementID, 0u);
  EXPECT_EQ(Stores[1].Component, 1u);
  EXPECT_EQ(Stores[2].ElementID, 1u);
  EXPECT_EQ(Stores[3].ElementID, 2u);
  EXPECT_EQ(Stores[3].Row, 0u);
}

/// (Roadmap H4d) A bare (non-block, single-`ElementID`) array-typed
/// stage-IO global -- exactly `gl_TessLevelInner`/`gl_TessLevelOuter`'s own
/// shape, a real GLSL-compiled tessellation-control shader's `[2 x float]`/
/// `[4 x float]` `BuiltIn`+`Patch`-decorated `Output` globals, confirmed
/// against `dEQP-VK.tessellation.winding.*`'s own compiled SPIR-V -- gets
/// one scalar store per array element, each at its own constant byte
/// offset into the global (mirroring the interface-block member case
/// `RecognizesInterfaceBlockPerMemberByteOffsetAccess` above, but with a
/// single `ElementID` shared by every row instead of one `ElementID` per
/// struct member). Before this milestone's fix, `resolveStageIOAccess`
/// treated any single-`ElementID` global as whole-value-only and rejected
/// every nonzero-byte-offset access outright, leaving every row but the
/// first (byte offset 0) an unrewritten raw store on the still-`external`
/// global -- an unresolvable symbol at JIT time (`LLJIT`'s own "Symbols not
/// found: [ ... ]").
TEST(CanonicalizeStageTest, RewritesSPIRVArrayOutputStorePerElementByteOffset) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_arr = external addrspace(8) global [4 x float], !spirv.Decorations !0
    define void @main() #0 {
      ; out_arr[0] = 1.0 (row 0 -- the offset-0 access that folds to a bare
      ; global, exactly like member 0 above).
      store float 1.000000e+00, ptr addrspace(8) @out_arr
      ; out_arr[1] = 2.0 (row 1 -- byte offset 4).
      store float 2.000000e+00, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @out_arr, i64 4)
      ; out_arr[2] = 3.0 (row 2 -- byte offset 8).
      store float 3.000000e+00, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @out_arr, i64 8)
      ; out_arr[3] = 4.0 (row 3 -- byte offset 12).
      store float 4.000000e+00, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @out_arr, i64 12)
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  // No raw store on `out_arr` (bare or via `getelementptr`) survives: this
  // is the exact defect roadmap H4d fixes -- every row must be rewritten,
  // not just row 0.
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));

  std::set<uint64_t> SeenRows;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(), 0u);
    SeenRows.insert(cast<ConstantInt>(CI->getArgOperand(1))->getZExtValue());
  }
  EXPECT_EQ(SeenRows.size(), 4u);
  for (uint64_t Row = 0; Row != 4; ++Row)
    EXPECT_TRUE(SeenRows.count(Row)) << "row " << Row;
}

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

/// (Roadmap H5b) A geometry entry point's own per-vertex inputs
/// (`gl_in[]`-shaped) are read via `gl_in[i]` for a loop-carried, genuinely
/// non-constant `i` -- unlike a matrix's `Row` dimension
/// (`RewritesSPIRVMatrixInputLoadOneRowAtATime` above), which is always
/// indexed by a constant that folds down to a plain byte offset.
/// `getStageIOBaseAndOffset`'s `stripAndAccumulateConstantOffsets` walk
/// cannot fold a non-constant GEP index at all, so before this it left
/// `gl_in[i]`-shaped loads entirely unresolved (an unrewritten raw load on
/// a still-`external` global, undefined at JIT time). This exercises the
/// new `getDynamicVertexIndexedAccess` path on a plain (non-block)
/// per-vertex-arrayed varying -- one `feme.stage.input.load` per vector
/// component, each carrying `%i` itself (not a constant) as its own
/// `Vertex` operand (argument 3), `Row` left at the default constant 0
/// (the dynamic-index path resolves the remaining access starting one
/// array dimension in, so there is no further row within that one
/// vertex's own `<4 x float>` value). `Sig->Elements[0].RowCount` itself
/// still reports 3 -- `getStageIORowShape`'s own whole-global type
/// recursion is untouched by this milestone, so it still sees the same
/// outer `[3 x <4 x float>]` a genuine 3-row matrix would -- but nothing
/// in the rewritten IR ever actually addresses a nonzero `Row` on this
/// global; only the dynamic `Vertex` operand does. Reconciling that
/// signature-level mislabeling (a per-vertex-array `RowCount` that a
/// consumer must not confuse with a real matrix row count) is left to a
/// later roadmap row, once H5c starts routing real geometry entries
/// through this pass and a real consumer needs to tell the two apart.
/// (Roadmap L24(a)) Uses a real `geometry`-stage function attribute
/// (rather than the placeholder `vertex` this test originally used, back
/// when no real per-vertex-arrayed-`Input` stage existed to test against
/// yet): `isPerVertexArrayInputGlobal`'s constant-index counterpart is now
/// scoped to `Hull`/`Domain`/`Geometry` only (see that function's own
/// comment), and `Sig->Elements[0].RowCountIsVertexArray`'s own
/// computation reuses it regardless of whether a given load's own index
/// happened to be constant or dynamic.
TEST(CanonicalizeStageTest, ThreadsDynamicVertexIndexIntoInputLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @in_texcoord = external addrspace(7) constant [3 x <4 x float>], !spirv.Decorations !0
    define <4 x float> @main(i32 %i) #0 {
      %p = getelementptr inbounds [3 x <4 x float>], ptr addrspace(7) @in_texcoord, i32 0, i32 %i
      %v = load <4 x float>, ptr addrspace(7) %p
      ret <4 x float> %v
    }
    attributes #0 = { "feme.shader.stage"="geometry" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *IArg = F->getArg(0);

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  // Unchanged from the ordinary (constant-index) matrix-row case: the
  // per-vertex array dimension still becomes `RowCount` in the signature
  // (`getStageIORowShape`'s own type-driven shape computation, untouched
  // by this milestone) -- H5b only changes which *operand*
  // (`feme.stage.input.load`'s `Vertex`, not `Row`) a *dynamically*
  // indexed access threads that dimension's own index through as.
  EXPECT_EQ(Sig->Elements[0].RowCount, 3u);
  EXPECT_EQ(Sig->Elements[0].ComponentCount, 4u);
  // (Roadmap H5f) The signature marks that `RowCount` as a per-vertex
  // array's own extent, not a real matrix's row count.
  EXPECT_TRUE(Sig->Elements[0].RowCountIsVertexArray);

  unsigned SeenLoads = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::InputLoad)
      continue;
    ++SeenLoads;
    EXPECT_EQ(getStageOpConstantOperand(*CI, /*Row=*/1), 0u);
    EXPECT_EQ(CI->getArgOperand(3), IArg);
    EXPECT_FALSE(isa<Constant>(CI->getArgOperand(3)));
  }
  EXPECT_EQ(SeenLoads, 4u);

  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<LoadInst>(&I));
}

/// (Roadmap H5b) The builtin-interface-block-array shape a geometry
/// entry's own `gl_in[]` genuinely takes (mirroring
/// `RecognizesInterfaceBlockPerMemberByteOffsetAccess`'s non-arrayed
/// `gl_PerVertex`, but with the per-member metadata one array dimension
/// further out): `gl_in[i].gl_Position` decomposes into member 0's own
/// `ElementID`, `Row`/`Component` both left at their default constant 0
/// (a whole-vector access), and `%i` itself threaded through as `Vertex`.
TEST(CanonicalizeStageTest,
     ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_in = external addrspace(7) global [3 x { <4 x float>, float, [1 x float], [1 x float] }], !feme.spirv.MemberDecorations !10
    define <4 x float> @main(i32 %i) #0 {
      %p = getelementptr inbounds [3 x { <4 x float>, float, [1 x float], [1 x float] }], ptr addrspace(7) @gl_in, i32 0, i32 %i, i32 0
      %v = load <4 x float>, ptr addrspace(7) %p
      ret <4 x float> %v
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !10 = !{!11, !12, !13, !14}
    !11 = !{i32 0, !15}
    !12 = !{i32 1, !16}
    !13 = !{i32 2, !17}
    !14 = !{i32 3, !18}
    !15 = !{!19}
    !19 = !{i32 11, i32 0}
    !16 = !{!20}
    !20 = !{i32 11, i32 1}
    !17 = !{!21}
    !21 = !{i32 11, i32 3}
    !18 = !{!22}
    !22 = !{i32 11, i32 4}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *IArg = F->getArg(0);

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 4u);
  EXPECT_EQ(Sig->Elements[0].SystemValue, SignatureSystemValue::Position);
  // Member 0's (`gl_Position`) own shape is a bare `<4 x float>` -- the
  // per-vertex array dimension is not folded into it.
  EXPECT_EQ(Sig->Elements[0].RowCount, 1u);
  EXPECT_EQ(Sig->Elements[0].ComponentCount, 4u);
  // (Roadmap H5f) Unlike the plain (non-block) case, a builtin interface
  // block's own per-member `RowCount` is never a per-vertex array's own
  // extent to begin with -- the outer array dimension is peeled off
  // before `addElement` ever sees this member's type.
  EXPECT_FALSE(Sig->Elements[0].RowCountIsVertexArray);

  unsigned SeenLoads = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::InputLoad)
      continue;
    ++SeenLoads;
    EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(),
              Sig->Elements[0].ElementID);
    EXPECT_EQ(getStageOpConstantOperand(*CI, /*Row=*/1), 0u);
    EXPECT_EQ(CI->getArgOperand(3), IArg);
  }
  EXPECT_EQ(SeenLoads, 4u);
}

/// (Roadmap H5f) The *constant*-index counterpart of
/// `ThreadsDynamicVertexIndexIntoInputLoad`: `gl_in[k]`-shaped (or any
/// other per-vertex-arrayed `Input` global's) access with a compile-time
/// constant `k` used to fold entirely into `Row` via the ordinary
/// `getStageIOBaseAndOffset`/`resolveRowComponent` byte-offset path,
/// indistinguishable in the rewritten IR from a real matrix's constant row
/// index (`RewritesSPIRVMatrixInputLoadOneRowAtATime` above). This exact
/// same plain (non-block) per-vertex-arrayed varying, now indexed by a
/// constant `1` instead of a loop-carried `%i`, must be threaded through
/// as `Vertex` (a constant `i32 1`) exactly like the non-constant case is,
/// not folded into `Row` (which must stay the default constant 0, there
/// being no further row within one vertex's own `<4 x float>` value) --
/// for consistency with `ThreadsDynamicVertexIndexIntoInputLoad`, and so
/// `Sig->Elements[0].RowCountIsVertexArray` (also asserted here) lets a
/// consumer recognize this element's `RowCount` as the per-vertex array's
/// own extent regardless of how the shader happens to index it.
/// (Roadmap L24(a)) Uses a real `geometry`-stage function attribute for
/// the same reason `ThreadsDynamicVertexIndexIntoInputLoad` now does --
/// `isPerVertexArrayInputGlobal` is scoped to `Hull`/`Domain`/`Geometry`
/// only, since every other stage's own array-typed `Input` global is an
/// ordinary multi-element varying with no per-vertex/control-point
/// dimension to fold a constant index into (roadmap L24(a)'s own
/// `ArraySemantics.test` regression, a fragment stage's plain
/// `float arr[4]` input).
TEST(CanonicalizeStageTest, FoldsConstantVertexIndexIntoVertexOperand) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @in_texcoord = external addrspace(7) constant [3 x <4 x float>], !spirv.Decorations !0
    define <4 x float> @main() #0 {
      %p = getelementptr inbounds [3 x <4 x float>], ptr addrspace(7) @in_texcoord, i32 0, i32 1
      %v = load <4 x float>, ptr addrspace(7) %p
      ret <4 x float> %v
    }
    attributes #0 = { "feme.shader.stage"="geometry" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  EXPECT_EQ(Sig->Elements[0].RowCount, 3u);
  EXPECT_EQ(Sig->Elements[0].ComponentCount, 4u);
  EXPECT_TRUE(Sig->Elements[0].RowCountIsVertexArray);

  unsigned SeenLoads = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::InputLoad)
      continue;
    ++SeenLoads;
    EXPECT_EQ(getStageOpConstantOperand(*CI, /*Row=*/1), 0u);
    std::optional<uint64_t> Vertex =
        getStageOpConstantOperand(*CI, /*Vertex=*/3);
    ASSERT_TRUE(Vertex.has_value());
    EXPECT_EQ(*Vertex, 1u);
  }
  EXPECT_EQ(SeenLoads, 4u);

  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<LoadInst>(&I));
}

/// (Roadmap H5f) The constant-index counterpart of
/// `ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberLoad`: a builtin
/// interface block's own per-vertex-arrayed access (`gl_in[k].
/// gl_Position`) with a constant `k` folds into `Vertex` the same way the
/// dynamic case does, not into an ordinary `Row`. (Roadmap L24(a)) Uses a
/// real `geometry`-stage function attribute, matching
/// `FoldsConstantVertexIndexIntoVertexOperand`'s own reasoning.
TEST(CanonicalizeStageTest,
     FoldsConstantVertexIndexIntoInterfaceBlockArrayMemberVertexOperand) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_in = external addrspace(7) global [3 x { <4 x float>, float, [1 x float], [1 x float] }], !feme.spirv.MemberDecorations !10
    define <4 x float> @main() #0 {
      %p = getelementptr inbounds [3 x { <4 x float>, float, [1 x float], [1 x float] }], ptr addrspace(7) @gl_in, i32 0, i32 2, i32 0
      %v = load <4 x float>, ptr addrspace(7) %p
      ret <4 x float> %v
    }
    attributes #0 = { "feme.shader.stage"="geometry" }
    !10 = !{!11, !12, !13, !14}
    !11 = !{i32 0, !15}
    !12 = !{i32 1, !16}
    !13 = !{i32 2, !17}
    !14 = !{i32 3, !18}
    !15 = !{!19}
    !19 = !{i32 11, i32 0}
    !16 = !{!20}
    !20 = !{i32 11, i32 1}
    !17 = !{!21}
    !21 = !{i32 11, i32 3}
    !18 = !{!22}
    !22 = !{i32 11, i32 4}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 4u);
  EXPECT_EQ(Sig->Elements[0].SystemValue, SignatureSystemValue::Position);
  EXPECT_EQ(Sig->Elements[0].RowCount, 1u);
  EXPECT_FALSE(Sig->Elements[0].RowCountIsVertexArray);

  unsigned SeenLoads = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::InputLoad)
      continue;
    ++SeenLoads;
    EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(),
              Sig->Elements[0].ElementID);
    EXPECT_EQ(getStageOpConstantOperand(*CI, /*Row=*/1), 0u);
    std::optional<uint64_t> Vertex =
        getStageOpConstantOperand(*CI, /*Vertex=*/3);
    ASSERT_TRUE(Vertex.has_value());
    EXPECT_EQ(*Vertex, 2u);
  }
  EXPECT_EQ(SeenLoads, 4u);
}

/// (Roadmap H6b) The store-side, `Output`-storage-class (address space 8)
/// mirror of `ThreadsDynamicVertexIndexIntoInputLoad`: a mesh entry's own
/// per-vertex/per-primitive output array
/// (`gl_MeshVerticesEXT[i]`/`gl_MeshPrimitivesEXT[i]`-shaped, or any other
/// per-vertex/per-primitive-arrayed plain `Output` varying) is *written*
/// through a genuinely dynamic index -- the invocation's own per-vertex/
/// per-primitive output slot -- rather than geometry's loop-carried read
/// index, but hits the exact same `getStageIOBaseAndOffset` limitation:
/// before generalizing `getDynamicVertexIndexedAccess` to `Output` globals
/// too (previously `Input`-only, address space 7, via
/// `isDynamicIndexedArrayGlobal`), this shape resolved to `std::nullopt`
/// and was left an unrewritten raw store on a still-`external` global.
/// Unlike `Input`'s H5f, this deliberately does *not* extend to a
/// *constant* `Output`-array index (see `isPerVertexArrayInputGlobal`'s
/// own comment: a constant index into an `Output` array already has a
/// real, different meaning in production use, an ordinary matrix
/// output's own per-row store --
/// `RewritesSPIRVArrayOutputStorePerElementByteOffset` above), so
/// `RowCountIsVertexArray` stays `false` here even though this global is
/// structurally identical to `ThreadsDynamicVertexIndexIntoInputLoad`'s own
/// `Input` one; only the *access itself* routes through `Vertex`, not (yet) the
/// signature's own description of the element. See "Roadmap H6: what H6b found,
/// and why it stops here" in VulkanCTSReport.md.
TEST(CanonicalizeStageTest, ThreadsDynamicVertexIndexIntoOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_verts = external addrspace(8) global [3 x <4 x float>], !spirv.Decorations !0
    define void @main(i32 %i, <4 x float> %v) #0 {
      %p = getelementptr inbounds [3 x <4 x float>], ptr addrspace(8) @out_verts, i32 0, i32 %i
      store <4 x float> %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *IArg = F->getArg(0);

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  EXPECT_EQ(Sig->Elements[0].RowCount, 3u);
  EXPECT_EQ(Sig->Elements[0].ComponentCount, 4u);
  EXPECT_FALSE(Sig->Elements[0].RowCountIsVertexArray);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    EXPECT_EQ(getStageOpConstantOperand(*CI, /*Row=*/1), 0u);
    EXPECT_EQ(CI->getArgOperand(4), IArg);
    EXPECT_FALSE(isa<Constant>(CI->getArgOperand(4)));
  }
  EXPECT_EQ(SeenStores, 4u);

  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));
}

/// (Roadmap H6b) The builtin-interface-block-array shape a mesh entry's
/// own `gl_MeshPrimitivesEXT[]` genuinely takes (mirroring
/// `ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberLoad`'s `gl_in[]`
/// but on the `Output`/store side): `gl_MeshPrimitivesEXT[i].
/// gl_PrimitiveID = %v` decomposes into member 0's own `ElementID`,
/// `Row`/`Component` both left at their default constant 0, and `%i`
/// itself threaded through as `Vertex` -- here standing in for the
/// per-primitive output slot, not a per-vertex one, since this helper's
/// own structural recognition (deliberately, matching
/// `isPerVertexArrayInputGlobal`'s own precedent) does not distinguish the
/// two.
TEST(CanonicalizeStageTest,
     ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_mesh_prims = external addrspace(8) global [4 x { i32, i32 }], !feme.spirv.MemberDecorations !10
    define void @main(i32 %i, i32 %v) #0 {
      %p = getelementptr inbounds [4 x { i32, i32 }], ptr addrspace(8) @gl_mesh_prims, i32 0, i32 %i, i32 0
      store i32 %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !10 = !{!11, !12}
    !11 = !{i32 0, !13}
    !12 = !{i32 1, !14}
    !13 = !{!15}
    !15 = !{i32 11, i32 7}
    !14 = !{!16}
    !16 = !{i32 5271}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *IArg = F->getArg(0);

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 2u);
  EXPECT_EQ(Sig->Elements[0].SystemValue, SignatureSystemValue::PrimitiveID);
  EXPECT_FALSE(Sig->Elements[0].RowCountIsVertexArray);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(),
              Sig->Elements[0].ElementID);
    EXPECT_EQ(getStageOpConstantOperand(*CI, /*Row=*/1), 0u);
    EXPECT_EQ(CI->getArgOperand(4), IArg);
    EXPECT_FALSE(isa<Constant>(CI->getArgOperand(4)));
  }
  EXPECT_EQ(SeenStores, 1u);
}

/// (Roadmap H7w) `gl_ClipDistance[i]`/`gl_CullDistance[i]` with a
/// non-constant (loop-carried) index `i` -- the shape a real
/// `dEQP-VK.clipping.user_defined.clip_distance_dynamic_index`/
/// `clip_cull_distance_dynamic_index` vertex shader compiles a
/// `for (int i = ...; ...) gl_ClipDistance[i] = ...;`-style write into,
/// confirmed via a real `glslangValidator`/`feme-translate` IR reduction.
/// Unlike `ThreadsDynamicVertexIndexIntoOutputStore`/
/// `ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberStore`'s own
/// non-constant index into a stage-IO global's *outer* per-vertex/
/// per-primitive array dimension (roadmap H5b/H6b, threaded through as
/// `Vertex`), this is a non-constant index into a builtin interface
/// block *member*'s own inner array dimension (`ClipDistance`, a plain
/// `[N x float]`) -- threaded through as `Row` instead, via
/// `getDynamicRowIndexedAccess`. Before this row,
/// `getStageIOBaseAndOffset`'s `stripAndAccumulateConstantOffsets` walk
/// could not fold a non-constant index at all, leaving this shape an
/// unrewritten raw store on a still-`external` global, diagnosed by
/// `ValidateStagePass` as an "unresolved stage-IO global-variable access."
TEST(CanonicalizeStageTest, ThreadsDynamicRowIndexIntoClipDistanceOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_PerVertex = external addrspace(8) global { <4 x float>, float, [3 x float], [2 x float] }, !feme.spirv.MemberDecorations !10
    define void @main(i32 %i, float %v) #0 {
      %p = getelementptr inbounds { <4 x float>, float, [3 x float], [2 x float] }, ptr addrspace(8) @gl_PerVertex, i32 0, i32 2, i32 %i
      store float %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !10 = !{!11, !12, !13, !14}
    !11 = !{i32 0, !15}
    !12 = !{i32 1, !16}
    !13 = !{i32 2, !17}
    !14 = !{i32 3, !18}
    !15 = !{!19}
    !19 = !{i32 11, i32 0}
    !16 = !{!20}
    !20 = !{i32 11, i32 1}
    !17 = !{!21}
    !21 = !{i32 11, i32 3}
    !18 = !{!22}
    !22 = !{i32 11, i32 4}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *IArg = F->getArg(0);
  Argument *VArg = F->getArg(1);

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 4u);
  EXPECT_EQ(Sig->Elements[2].SystemValue, SignatureSystemValue::ClipDistance);
  EXPECT_EQ(Sig->Elements[2].RowCount, 3u);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(),
              Sig->Elements[2].ElementID);
    // `Row` (operand 1) is the non-constant loop index itself, not a
    // constant -- unlike every access this pass recognized before H7w.
    EXPECT_EQ(CI->getArgOperand(1), IArg);
    EXPECT_FALSE(isa<Constant>(CI->getArgOperand(1)));
    EXPECT_EQ(getStageOpConstantOperand(*CI, /*Component=*/2), 0u);
    EXPECT_EQ(CI->getArgOperand(3), VArg);
  }
  EXPECT_EQ(SeenStores, 1u);

  // No store targets `@gl_PerVertex` directly anymore -- the only
  // `StoreInst` left is the shadow alloca's own write-through (roadmap
  // H2e/H7w), an ordinary local, not the original stage-IO global.
  for (Instruction &I : instructions(F))
    if (auto *SI = dyn_cast<StoreInst>(&I))
      EXPECT_FALSE(isa<GlobalVariable>(SI->getPointerOperand()));
}

/// (Roadmap H6k) A multi-`ElementID` builtin interface block whose own
/// value type is an arrayed `StructType` (a mesh entry's own
/// `PerPrimitiveEXT`/`PerVertexEXT`-decorated block, e.g.
/// `gl_MeshPrimitivesEXT[]`), accessed through a *constant* array index
/// rather than `ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberStore`'s
/// dynamic one -- the exact shape a real, glslang-compiled
/// `dEQP-VK.mesh_shader.ext.in_out.*` mesh entry's own
/// `gl_MeshVerticesEXT[k].gl_Position = ...`-style compile-time-unrolled
/// per-vertex output store takes. Before this row, a constant `Output`
/// array index was deliberately *not* folded into `Vertex` (roadmap H6b,
/// to avoid misrouting a real matrix output's own constant row index) --
/// safe for every stage this implementation gives a genuine per-row
/// matrix output *except* mesh (`isPerVertexArrayMeshOutputGlobal`'s own
/// comment) -- so this shape used to reach `resolveOffsetWithinElement`
/// with the whole array-of-struct (`GV->getValueType()`) as `ElemTy` and
/// more than one `ElementID`, which cannot itself be cast to a
/// `StructType` and so resolved to `std::nullopt` (left unrewritten). A
/// mesh entry's own constant-indexed access now takes the identical fold
/// `ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberStore`'s dynamic
/// one already did, peeling the constant array index into `Vertex` before
/// `resolveOffsetWithinElement` picks the right member's own `ElementID`
/// out of the (now correctly per-element, not per-array) struct type --
/// exactly what stopped the real CTS case's own `StageStorage` corruption
/// (this row's own crash, `feme-graphics-validate-stage`'s "row is out of
/// range" diagnostic converted what used to be silent heap corruption
/// into a compile-time-visible one before this fix).
TEST(CanonicalizeStageTest,
     FoldsConstantIndexIntoArrayedBuiltinInterfaceBlockMemberStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_mesh_prims = external addrspace(8) global [4 x { i32, i32 }], !feme.spirv.MemberDecorations !10
    define void @main(i32 %v) #0 {
      %p = getelementptr inbounds [4 x { i32, i32 }], ptr addrspace(8) @gl_mesh_prims, i32 0, i32 1, i32 0
      store i32 %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
    !10 = !{!11, !12}
    !11 = !{i32 0, !13}
    !12 = !{i32 1, !14}
    !13 = !{!15}
    !15 = !{i32 11, i32 7}
    !14 = !{!16}
    !16 = !{i32 5271}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 2u);
  EXPECT_EQ(Sig->Elements[0].SystemValue, SignatureSystemValue::PrimitiveID);
  EXPECT_FALSE(Sig->Elements[0].RowCountIsVertexArray);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(),
              Sig->Elements[0].ElementID);
    EXPECT_EQ(getStageOpConstantOperand(*CI, /*Row=*/1), 0u);
    EXPECT_EQ(getStageOpConstantOperand(*CI, /*Vertex=*/4), 1u);
  }
  EXPECT_EQ(SeenStores, 1u);

  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));
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

/// (Roadmap H2e) An `Output`-direction global read back after being
/// written earlier in the same, straight-line invocation (unlike DXIL's
/// genuinely write-only `storeOutput`, SPIR-V's `Output` storage class
/// permits this) resolves directly to the stored value -- no
/// `feme.stage.input.load` at all, since the read is not a genuine input.
TEST(CanonicalizeStageTest, OutputReadBackResolvesToStoredValueStraightLine) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_var = external addrspace(8) global float, !spirv.Decorations !0
    define void @main() #0 {
      store float 1.000000e+00, ptr addrspace(8) @out_var
      %v = load float, ptr addrspace(8) @out_var
      %v2 = fadd float %v, 1.000000e+00
      store float %v2, ptr addrspace(8) @out_var
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  // No `feme.stage.input.load` at all: the read-back is not a genuine
  // input, and no leftover `alloca`/load/store of a shadow value survives
  // `PromoteMemToReg`.
  unsigned SawStore = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(isa<AllocaInst>(&I));
    EXPECT_FALSE(isa<LoadInst>(&I));
    EXPECT_FALSE(isa<StoreInst>(&I));
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind))
      continue;
    EXPECT_NE(Kind, StageOpKind::InputLoad);
    if (Kind == StageOpKind::OutputStore)
      ++SawStore;
  }
  EXPECT_EQ(SawStore, 2u);
}

/// The same read-back, but across a real control-flow join -- the shape
/// `dEQP-VK.multiview.input_instance`'s own vertex shader takes (a
/// compound `gl_Position.y += 1.0f;` guarded by an `if`): the read-back
/// inside the conditional block resolves to the value stored in the
/// dominating entry block, without any `feme.stage.input.load`.
TEST(CanonicalizeStageTest, OutputReadBackResolvesAcrossControlFlow) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_var = external addrspace(8) global float, !spirv.Decorations !0
    define void @main(i1 %cond) #0 {
    entry:
      store float 1.000000e+00, ptr addrspace(8) @out_var
      br i1 %cond, label %if.then, label %if.end
    if.then:
      %v = load float, ptr addrspace(8) @out_var
      %v2 = fadd float %v, 1.000000e+00
      store float %v2, ptr addrspace(8) @out_var
      br label %if.end
    if.end:
      ret void
    }
    attributes #0 = { "feme.shader.stage"="vertex" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  unsigned SawStore = 0;
  for (Instruction &I : instructions(F)) {
    EXPECT_FALSE(isa<AllocaInst>(&I));
    EXPECT_FALSE(isa<LoadInst>(&I));
    EXPECT_FALSE(isa<StoreInst>(&I));
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind))
      continue;
    EXPECT_NE(Kind, StageOpKind::InputLoad);
    if (Kind == StageOpKind::OutputStore)
      ++SawStore;
  }
  EXPECT_EQ(SawStore, 2u);
}

/// (Roadmap H4a) A SPIR-V `TessellationControl` entry point with no
/// `OpControlBarrier` (`llvm.spv.group.memory.barrier.with.group.sync`) at
/// all needs no splitting: `canonicalizeSPIRVHullStage` treats the whole
/// body as the control-point phase.
TEST(CanonicalizeStageTest, HullStageWithNoBarrierIsNotSplit) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_out_pos = external addrspace(8) global <4 x float>, !spirv.Decorations !0
    @gl_in_pos = external addrspace(7) constant <4 x float>, !spirv.Decorations !1
    define void @main() #0 {
      %v = load <4 x float>, ptr addrspace(7) @gl_in_pos
      store <4 x float> %v, ptr addrspace(8) @gl_out_pos
      ret void
    }
    attributes #0 = { "feme.shader.stage"="hull" }
    !0 = !{!2}
    !1 = !{!2}
    !2 = !{i32 11, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  EXPECT_TRUE(M->getFunction("main"));
  EXPECT_FALSE(M->getFunction("main.patchconstant"));
  std::optional<EntrySignature> Sig =
      dxil::getEntrySignature(*M->getFunction("main"));
  ASSERT_TRUE(Sig.has_value());
  EXPECT_EQ(Sig->Elements.size(), 2u);
}

/// (Roadmap H9c) `classifyTessControlOutputs`'s own scan (used by
/// `splitBarrierlessTessellationControlEntry` above) must not miss a
/// genuine per-vertex output written through a *dynamic* vertex index --
/// `out_color[gl_InvocationID] = ...`, the shape
/// `ThreadsDynamicVertexIndexIntoOutputStore` above already covers for
/// `resolveStageIOAccess`'s own store-rewriting, and every real GLSL
/// tessellation-control shader's own `out_color[]`/`gl_out[]` write --
/// alongside a `Patch`-decorated tessellation-factor write, with no
/// group-sync barrier separating them (legal: neither invocation's write
/// here ever depends on another's). Before this row's own fix,
/// `isPatchConstantOnlyEntry` (this scan's own predecessor) resolved a
/// store's target purely via `getStageIOBaseAndOffset` (constant-offset
/// only), so this dynamically-indexed store was invisible to it: the scan
/// only ever saw the `TessLevelOuter` write, wrongly concluded the whole
/// entry was patch-constant-only, and cloned the *entire* body --
/// dynamically-indexed per-vertex store included -- into a new
/// `.patchconstant` function, where `classifySPIRVElement`'s
/// `HullPatchConstant`-phase branch misclassifies that non-`patch`-
/// decorated store as a captured-value read-back (`Direction::Input`,
/// never a store): the exact gap
/// `GraphicsPipelineTest.AcceptsTessellationControlBarrierlessDynamic
/// VertexIndexedMixedStoreSource` reproduces end to end via
/// `PatchConstantWrapper.cpp`'s own "masked output store references an
/// unknown patch-output signature element" error. Now that
/// `classifyTessControlOutputs` also resolves a dynamically-indexed store
/// via `getStageIOGlobal`, it correctly sees both the patch- and
/// non-patch-frequency writes: `splitBarrierlessTessellationControlEntry`
/// recognizes this as a genuine mix and splits it via
/// `pruneStageIOStoresByFrequency`, producing a real, correctly-pruned
/// `.patchconstant` sibling (only the `TessLevelOuter` write survives in
/// it, as a `PatchOutput` signature element) with the original `main`
/// pruned down to only its own `out_color` write (an ordinary `Output`
/// element, its `TessLevelOuter` write removed).
TEST(
    CanonicalizeStageTest,
    NoBarrierMixedFrequencyEntryWithDynamicVertexIndexedStoreIsSplitAndPruned) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_color = external addrspace(8) global [3 x <4 x float>], !spirv.Decorations !0
    @tess_outer = external addrspace(8) global [4 x float], !spirv.Decorations !1
    define void @main(i32 %i, <4 x float> %v) #0 {
      %tp = getelementptr inbounds [4 x float], ptr addrspace(8) @tess_outer, i32 0, i32 0
      store float 4.000000e+00, ptr addrspace(8) %tp
      %cp = getelementptr inbounds [3 x <4 x float>], ptr addrspace(8) @out_color, i32 0, i32 %i
      store <4 x float> %v, ptr addrspace(8) %cp
      ret void
    }
    attributes #0 = { "feme.shader.stage"="hull" }
    !0 = !{!2}
    !1 = !{!3}
    !2 = !{i32 30, i32 0}
    !3 = !{i32 11, i32 11}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *ControlPoint = M->getFunction("main");
  Function *PatchConstant = M->getFunction("main.patchconstant");
  ASSERT_TRUE(ControlPoint);
  ASSERT_TRUE(PatchConstant);

  // The control-point phase keeps only its own `out_color` write --
  // its own `TessLevelOuter` store is pruned away.
  std::optional<EntrySignature> CPSig = dxil::getEntrySignature(*ControlPoint);
  ASSERT_TRUE(CPSig.has_value());
  ASSERT_EQ(CPSig->Elements.size(), 1u);
  EXPECT_EQ(CPSig->Elements[0].Direction, SignatureDirection::Output);
  EXPECT_EQ(CPSig->Elements[0].Location, 0u);

  // The patch-constant phase keeps only its own `TessLevelOuter` write --
  // its own (dynamically-vertex-indexed) `out_color` store is pruned
  // away.
  std::optional<EntrySignature> PCSig = dxil::getEntrySignature(*PatchConstant);
  ASSERT_TRUE(PCSig.has_value());
  ASSERT_EQ(PCSig->Elements.size(), 1u);
  EXPECT_EQ(PCSig->Elements[0].Direction, SignatureDirection::PatchOutput);
  EXPECT_EQ(PCSig->Elements[0].SystemValue,
            SignatureSystemValue::TessFactorEdge);
}

/// (Roadmap H4a) The real shape a GLSL tessellation-control shader's
/// SPIR-V compiles to: one entry point writing its per-vertex outputs,
/// then an `OpControlBarrier`, then the `Patch`-decorated tessellation-
/// factor/patch-constant writes. `canonicalizeSPIRVHullStage` must split
/// this into FeMe's two separately compiled phases -- the control-point
/// phase (`HullWrapperPass`'s ABI) keeping the pre-barrier code under the
/// original name, and a new `<name>.patchconstant` function (
/// `PatchConstantWrapperPass`'s ABI) cloned from the post-barrier code --
/// discriminated by `feme::cpu::isPatchConstantPhase`'s
/// `SignatureDirection::PatchOutput` test.
TEST(CanonicalizeStageTest, SplitsHullEntryAtGroupSyncBarrier) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_out_pos = external addrspace(8) global <4 x float>, !spirv.Decorations !0
    @gl_in_pos = external addrspace(7) constant <4 x float>, !spirv.Decorations !1
    @gl_TessLevelOuter = external addrspace(8) global [4 x float], !spirv.Decorations !3
    define void @main() #0 {
      %v = load <4 x float>, ptr addrspace(7) @gl_in_pos
      store <4 x float> %v, ptr addrspace(8) @gl_out_pos
      call void @llvm.spv.group.memory.barrier.with.group.sync()
      store float 1.000000e+00, ptr addrspace(8) @gl_TessLevelOuter
      ret void
    }
    declare void @llvm.spv.group.memory.barrier.with.group.sync()
    attributes #0 = { "feme.shader.stage"="hull" }
    !0 = !{!2}
    !1 = !{!2}
    !2 = !{i32 11, i32 0}
    !3 = !{!4}
    !4 = !{i32 11, i32 11}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *ControlPoint = M->getFunction("main");
  Function *PatchConstant = M->getFunction("main.patchconstant");
  ASSERT_TRUE(ControlPoint);
  ASSERT_TRUE(PatchConstant);

  // The control-point phase keeps only the pre-barrier control-point
  // output; the barrier call itself is gone.
  std::optional<EntrySignature> CPSig = dxil::getEntrySignature(*ControlPoint);
  ASSERT_TRUE(CPSig.has_value());
  ASSERT_EQ(CPSig->Elements.size(), 2u);
  for (Instruction &I : instructions(ControlPoint))
    if (auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(CI->getCalledFunction() &&
                   CI->getCalledFunction()->getIntrinsicID() ==
                       Intrinsic::spv_group_memory_barrier_with_group_sync);

  // The patch-constant phase carries the `TessLevelOuter` write as a
  // `SignatureDirection::PatchOutput` element.
  std::optional<EntrySignature> PCSig = dxil::getEntrySignature(*PatchConstant);
  ASSERT_TRUE(PCSig.has_value());
  ASSERT_EQ(PCSig->Elements.size(), 1u);
  EXPECT_EQ(PCSig->Elements[0].Direction, SignatureDirection::PatchOutput);
  EXPECT_EQ(PCSig->Elements[0].SystemValue,
            SignatureSystemValue::TessFactorEdge);
}

/// (Roadmap H4b) A genuine MLIR-imported SPIR-V module's own
/// `spirv.ControlBarrier` does not lower to the
/// `llvm.spv.group.memory.barrier.with.group.sync` intrinsic the test
/// above uses -- MLIR upstream's own `ControlBarrierPattern`
/// (`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`) instead lowers it
/// to a call to the mangled external declaration
/// `_Z22__spirv_ControlBarrieriii` (`__spirv_ControlBarrier(int, int,
/// int)`). `isSPIRVGroupSyncBarrier` must recognize this call shape too,
/// or a real SPIR-V-imported tessellation-control module's own group sync
/// would never split into a control-point/patch-constant phase pair at
/// all -- this is the same source shape as
/// `SplitsHullEntryAtGroupSyncBarrier` above, but with the barrier call
/// replaced by the mangled-name form instead of the intrinsic.
TEST(CanonicalizeStageTest, SplitsHullEntryAtMangledSPIRVControlBarrierCall) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_out_pos = external addrspace(8) global <4 x float>, !spirv.Decorations !0
    @gl_in_pos = external addrspace(7) constant <4 x float>, !spirv.Decorations !1
    @gl_TessLevelOuter = external addrspace(8) global [4 x float], !spirv.Decorations !3
    define void @main() #0 {
      %v = load <4 x float>, ptr addrspace(7) @gl_in_pos
      store <4 x float> %v, ptr addrspace(8) @gl_out_pos
      call void @_Z22__spirv_ControlBarrieriii(i32 2, i32 2, i32 264)
      store float 1.000000e+00, ptr addrspace(8) @gl_TessLevelOuter
      ret void
    }
    declare void @_Z22__spirv_ControlBarrieriii(i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="hull" }
    !0 = !{!2}
    !1 = !{!2}
    !2 = !{i32 11, i32 0}
    !3 = !{!4}
    !4 = !{i32 11, i32 11}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *ControlPoint = M->getFunction("main");
  Function *PatchConstant = M->getFunction("main.patchconstant");
  ASSERT_TRUE(ControlPoint);
  ASSERT_TRUE(PatchConstant);

  // The control-point phase keeps only the pre-barrier control-point
  // output; the barrier call itself is gone.
  std::optional<EntrySignature> CPSig = dxil::getEntrySignature(*ControlPoint);
  ASSERT_TRUE(CPSig.has_value());
  ASSERT_EQ(CPSig->Elements.size(), 2u);
  for (Instruction &I : instructions(ControlPoint))
    if (auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(CI->getCalledFunction() &&
                   CI->getCalledFunction()->getName() ==
                       "_Z22__spirv_ControlBarrieriii");

  // The patch-constant phase carries the `TessLevelOuter` write as a
  // `SignatureDirection::PatchOutput` element.
  std::optional<EntrySignature> PCSig = dxil::getEntrySignature(*PatchConstant);
  ASSERT_TRUE(PCSig.has_value());
  ASSERT_EQ(PCSig->Elements.size(), 1u);
  EXPECT_EQ(PCSig->Elements[0].Direction, SignatureDirection::PatchOutput);
  EXPECT_EQ(PCSig->Elements[0].SystemValue,
            SignatureSystemValue::TessFactorEdge);
}

/// (Roadmap H13b) A patch-constant phase reading a per-control-point
/// builtin -- `gl_in[i].gl_Position`/`gl_ClipDistance`/`gl_CullDistance`,
/// each a real array indexed by control point, not the single, current-
/// invocation-implicit scalar `OutputControlPointID` is -- must classify
/// that read as `FromInputPatch` (routed through `PatchConstantWrapper
/// .cpp`'s generic `lowerPatchConstantInputLoad`, not the narrow, two-case
/// `lowerPatchConstantSystemValue`) exactly like an ordinary varying,
/// despite carrying a `SystemValue` of its own (these builtins have no
/// `Location`). `OutputControlPointID` remains the one system value this
/// phase reads that is *not* `FromInputPatch` -- verified alongside, to
/// pin the boundary this fix drew precisely.
TEST(CanonicalizeStageTest,
     PatchConstantPhaseMarksPositionAndClipDistanceFromInputPatch) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_in_pos = external addrspace(7) constant <4 x float>, !spirv.Decorations !0
    @gl_in_clip = external addrspace(7) constant [1 x float], !spirv.Decorations !1
    @gl_InvocationID = external addrspace(7) constant i32, !spirv.Decorations !2
    @gl_TessLevelOuter = external addrspace(8) global [4 x float], !spirv.Decorations !3
    define void @main() #0 {
      call void @llvm.spv.group.memory.barrier.with.group.sync()
      %pos = load <4 x float>, ptr addrspace(7) @gl_in_pos
      %clip = load [1 x float], ptr addrspace(7) @gl_in_clip
      %id = load i32, ptr addrspace(7) @gl_InvocationID
      %x = extractelement <4 x float> %pos, i32 0
      %c = extractvalue [1 x float] %clip, 0
      %sum = fadd float %x, %c
      %idf = uitofp i32 %id to float
      %total = fadd float %sum, %idf
      store float %total, ptr addrspace(8) @gl_TessLevelOuter
      ret void
    }
    declare void @llvm.spv.group.memory.barrier.with.group.sync()
    attributes #0 = { "feme.shader.stage"="hull" }
    !0 = !{!4}
    !1 = !{!5}
    !2 = !{!6}
    !3 = !{!7}
    !4 = !{i32 11, i32 0}
    !5 = !{i32 11, i32 3}
    !6 = !{i32 11, i32 8}
    !7 = !{i32 11, i32 11}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *PatchConstant = M->getFunction("main.patchconstant");
  ASSERT_TRUE(PatchConstant);
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*PatchConstant);
  ASSERT_TRUE(Sig.has_value());

  const SignatureElement *PositionElt = nullptr;
  const SignatureElement *ClipDistanceElt = nullptr;
  const SignatureElement *InvocationIDElt = nullptr;
  for (const SignatureElement &Elt : Sig->Elements) {
    if (Elt.SystemValue == SignatureSystemValue::Position)
      PositionElt = &Elt;
    else if (Elt.SystemValue == SignatureSystemValue::ClipDistance)
      ClipDistanceElt = &Elt;
    else if (Elt.SystemValue == SignatureSystemValue::OutputControlPointID)
      InvocationIDElt = &Elt;
  }
  ASSERT_TRUE(PositionElt);
  ASSERT_TRUE(ClipDistanceElt);
  ASSERT_TRUE(InvocationIDElt);
  EXPECT_TRUE(PositionElt->FromInputPatch);
  EXPECT_TRUE(ClipDistanceElt->FromInputPatch);
  EXPECT_FALSE(InvocationIDElt->FromInputPatch);
}

/// (Roadmap H4c) The common, real GLSL-compiled shape
/// `SplitsHullEntryAtGroupSyncBarrier` above does not cover: a per-patch
/// tessellation factor computed from an SSA value derived from the
/// control-point body (here, `%scaled`) and read back *after*
/// `OpControlBarrier`, rather than reloaded through a fresh stage-IO
/// access. `splitTessellationControlEntry` must thread `%scaled` through a
/// new synthetic patch-shared global instead of diagnosing it as
/// unsplittable: the control-point phase gains an extra `Output`-direction
/// element (the store this pass inserts right after `%scaled`'s own
/// definition) and the patch-constant phase gains a matching `Input`-
/// direction, non-`FromInputPatch` element (the load this pass inserts at
/// its own new entry block) -- exactly the shape a genuine per-vertex
/// output's own read-back already takes, so no new stage-linking mechanism
/// is needed for `feme::graphics::linkStageElements` to carry the value
/// across.
TEST(CanonicalizeStageTest, SplitsHullEntryThreadingCapturedSSAValue) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_out_pos = external addrspace(8) global <4 x float>, !spirv.Decorations !0
    @gl_in_pos = external addrspace(7) constant <4 x float>, !spirv.Decorations !1
    @gl_TessLevelOuter = external addrspace(8) global [4 x float], !spirv.Decorations !3
    define void @main() #0 {
      %v = load <4 x float>, ptr addrspace(7) @gl_in_pos
      %scaled = fmul <4 x float> %v, %v
      store <4 x float> %scaled, ptr addrspace(8) @gl_out_pos
      call void @llvm.spv.group.memory.barrier.with.group.sync()
      %factor = extractelement <4 x float> %scaled, i32 0
      store float %factor, ptr addrspace(8) @gl_TessLevelOuter
      ret void
    }
    declare void @llvm.spv.group.memory.barrier.with.group.sync()
    attributes #0 = { "feme.shader.stage"="hull" }
    !0 = !{!2}
    !1 = !{!2}
    !2 = !{i32 11, i32 0}
    !3 = !{!4}
    !4 = !{i32 11, i32 11}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *ControlPoint = M->getFunction("main");
  Function *PatchConstant = M->getFunction("main.patchconstant");
  ASSERT_TRUE(ControlPoint);
  ASSERT_TRUE(PatchConstant);

  // The control-point phase keeps its own two real elements, plus one new
  // synthetic `Output` element -- the captured value's own store.
  std::optional<EntrySignature> CPSig = dxil::getEntrySignature(*ControlPoint);
  ASSERT_TRUE(CPSig.has_value());
  ASSERT_EQ(CPSig->Elements.size(), 3u);
  unsigned CPOutputs = 0;
  const SignatureElement *CapturedOutput = nullptr;
  for (const SignatureElement &Elt : CPSig->Elements)
    if (Elt.Direction == SignatureDirection::Output) {
      ++CPOutputs;
      if (Elt.ComponentCount == 4)
        CapturedOutput = &Elt;
    }
  EXPECT_EQ(CPOutputs, 2u);
  ASSERT_TRUE(CapturedOutput);
  EXPECT_EQ(CapturedOutput->ComponentType, SignatureComponentType::Float);
  EXPECT_EQ(CapturedOutput->RowCount, 1u);

  // The patch-constant phase gains a matching `Input`, non-`FromInputPatch`
  // element (the captured value's own read-back) alongside its real
  // `TessLevelOuter` `PatchOutput` write; the captured value's use no
  // longer references anything defined in the control-point phase.
  std::optional<EntrySignature> PCSig = dxil::getEntrySignature(*PatchConstant);
  ASSERT_TRUE(PCSig.has_value());
  ASSERT_EQ(PCSig->Elements.size(), 2u);
  const SignatureElement *CapturedInput = nullptr;
  for (const SignatureElement &Elt : PCSig->Elements)
    if (Elt.Direction == SignatureDirection::Input)
      CapturedInput = &Elt;
  ASSERT_TRUE(CapturedInput);
  EXPECT_FALSE(CapturedInput->FromInputPatch);
  EXPECT_EQ(CapturedInput->ComponentCount, 4u);
  EXPECT_EQ(CapturedInput->ComponentType, SignatureComponentType::Float);

  for (Instruction &I : instructions(PatchConstant))
    for (Value *Op : I.operands())
      if (auto *OpI = dyn_cast<Instruction>(Op))
        EXPECT_EQ(OpI->getFunction(), PatchConstant)
            << "patch-constant phase must not reference any value still "
               "defined in the control-point phase";
}

/// (Roadmap H4f) A no-barrier tessellation-control entry point whose only
/// stage-IO writes are patch-frequency (`Patch`-decorated or a tess-factor
/// `BuiltIn`) is legally the case whenever `OutputVertices == 1`
/// (`dEQP-VK.tessellation.winding.*`'s own `layout(vertices = 1) out;`
/// shape, which writes only `gl_TessLevelInner`/`gl_TessLevelOuter` and
/// never touches `gl_out[]` at all): a single control-point invocation has
/// nothing to distinguish "per control point" from "per patch" here, so
/// the whole entry is semantically already the patch-constant phase.
/// `HullStageWithNoBarrierIsNotSplit` above must keep not splitting a
/// no-barrier entry with an ordinary (non-patch) output write; this shape
/// must split unconditionally instead, moving the whole body into a new
/// `<name>.patchconstant` clone and leaving the original entry point as a
/// trivial, empty control-point phase.
TEST(CanonicalizeStageTest, NoBarrierPatchConstantOnlyEntryIsSplitWhole) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_TessLevelOuter = external addrspace(8) global [4 x float], !spirv.Decorations !0
    define void @main() #0 {
      store float 5.000000e+00, ptr addrspace(8) @gl_TessLevelOuter
      ret void
    }
    attributes #0 = { "feme.shader.stage"="hull" }
    !0 = !{!1}
    !1 = !{i32 11, i32 11}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *ControlPoint = M->getFunction("main");
  Function *PatchConstant = M->getFunction("main.patchconstant");
  ASSERT_TRUE(ControlPoint);
  ASSERT_TRUE(PatchConstant);

  // The control-point phase is left trivial: no stage-IO signature (an
  // absent one is treated identically to an explicitly empty one -- see
  // `feme::cpu::CompiledStage::create`, roadmap H4g), and no instructions
  // beyond its own `ret void`.
  EXPECT_FALSE(dxil::getEntrySignature(*ControlPoint).has_value());
  ASSERT_EQ(ControlPoint->size(), 1u);
  EXPECT_EQ(ControlPoint->front().size(), 1u);
  EXPECT_TRUE(isa<ReturnInst>(ControlPoint->front().front()));

  // The patch-constant phase carries the whole original body, including
  // the `TessLevelOuter` write as a `SignatureDirection::PatchOutput`
  // element.
  std::optional<EntrySignature> PCSig = dxil::getEntrySignature(*PatchConstant);
  ASSERT_TRUE(PCSig.has_value());
  ASSERT_EQ(PCSig->Elements.size(), 1u);
  EXPECT_EQ(PCSig->Elements[0].Direction, SignatureDirection::PatchOutput);
  EXPECT_EQ(PCSig->Elements[0].SystemValue,
            SignatureSystemValue::TessFactorEdge);
}

/// (Roadmap H4a) `BuiltIn InvocationId` (SPIR-V code 8, `gl_InvocationID`)
/// maps to `SignatureSystemValue::InvocationID`, and `BuiltIn
/// PatchVertices` (code 14, `gl_PatchVerticesIn`) to `SignatureSystemValue
/// ::PatchVertices` with `SignatureFrequency::PerPatch` -- the hull
/// control-point phase's own identity and input-patch-size system values.
TEST(CanonicalizeStageTest, HullStageMapsInvocationIdAndPatchVertices) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_InvocationID = external addrspace(7) constant i32, !spirv.Decorations !0
    @gl_PatchVerticesIn = external addrspace(7) constant i32, !spirv.Decorations !1
    @gl_out_pos = external addrspace(8) global <4 x float>, !spirv.Decorations !2
    define void @main() #0 {
      %id = load i32, ptr addrspace(7) @gl_InvocationID
      %pv = load i32, ptr addrspace(7) @gl_PatchVerticesIn
      %f = sitofp i32 %id to float
      %v = insertelement <4 x float> poison, float %f, i32 0
      store <4 x float> %v, ptr addrspace(8) @gl_out_pos
      ret void
    }
    attributes #0 = { "feme.shader.stage"="hull" }
    !0 = !{!3}
    !1 = !{!4}
    !2 = !{!5}
    !3 = !{i32 11, i32 8}
    !4 = !{i32 11, i32 14}
    !5 = !{i32 11, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 3u);

  const SignatureElement &InvocationId = Sig->Elements[0];
  EXPECT_EQ(InvocationId.Direction, SignatureDirection::Input);
  EXPECT_EQ(InvocationId.SystemValue, SignatureSystemValue::InvocationID);

  const SignatureElement &PatchVertices = Sig->Elements[1];
  EXPECT_EQ(PatchVertices.Direction, SignatureDirection::Input);
  EXPECT_EQ(PatchVertices.SystemValue, SignatureSystemValue::PatchVertices);
  EXPECT_EQ(PatchVertices.Frequency, SignatureFrequency::PerPatch);
}

/// (Roadmap H4a) A domain (tessellation-evaluation) stage entry point's
/// `BuiltIn TessCoord` (code 13, `gl_TessCoord`) input maps to
/// `SignatureSystemValue::DomainLocation` (its FeMe-native spelling), and a
/// `Patch`-decorated input -- `gl_TessLevelOuter` here, read back by the
/// domain stage -- becomes a `SignatureDirection::PatchInput` element.
TEST(CanonicalizeStageTest, DomainStageMapsTessCoordAndPatchInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_TessCoord = external addrspace(7) constant <3 x float>, !spirv.Decorations !0
    @gl_TessLevelOuter = external addrspace(7) constant [4 x float], !spirv.Decorations !1
    @gl_out_pos = external addrspace(8) global <4 x float>, !spirv.Decorations !2
    define void @main() #0 {
      %tc = load <3 x float>, ptr addrspace(7) @gl_TessCoord
      %tf = load [4 x float], ptr addrspace(7) @gl_TessLevelOuter
      %tf0 = extractvalue [4 x float] %tf, 0
      %tcx = extractelement <3 x float> %tc, i32 0
      %sum = fadd float %tcx, %tf0
      %v = insertelement <4 x float> poison, float %sum, i32 0
      store <4 x float> %v, ptr addrspace(8) @gl_out_pos
      ret void
    }
    attributes #0 = { "feme.shader.stage"="domain" }
    !0 = !{!3}
    !1 = !{!4}
    !2 = !{!5}
    !3 = !{i32 11, i32 13}
    !4 = !{i32 11, i32 11}
    !5 = !{i32 11, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 3u);

  const SignatureElement &TessCoord = Sig->Elements[0];
  EXPECT_EQ(TessCoord.Direction, SignatureDirection::Input);
  EXPECT_EQ(TessCoord.SystemValue, SignatureSystemValue::DomainLocation);

  const SignatureElement &TessLevelOuter = Sig->Elements[1];
  EXPECT_EQ(TessLevelOuter.Direction, SignatureDirection::PatchInput);
  EXPECT_EQ(TessLevelOuter.SystemValue, SignatureSystemValue::TessFactorEdge);
  EXPECT_EQ(TessLevelOuter.Frequency, SignatureFrequency::PerPatch);
}

/// (Roadmap L81) A domain stage's own `BuiltIn PrimitiveId` (code 7,
/// `gl_PrimitiveID`) read is a genuine, pipeline-supplied system value --
/// never patch-constant-forwarded data -- even though a real DXC/SPIR-V
/// compile decorates it `Patch` (decoration code 15, uniform across the
/// whole patch) the same way a true patch-constant-forwarded tessellation
/// factor is decorated. Before this fix, `isPatchOutputDecoration` alone
/// decided this classification, wrongly routing `gl_PrimitiveID` into
/// `SignatureDirection::PatchInput` (expecting a patch-constant-phase
/// `PatchOutput` producer that never exists for it) -- the domain-stage
/// analog of roadmap L80's identical hull-stage mistake. It must instead be
/// recognized alongside `DomainLocation`/`PatchVertices` as a genuinely
/// synthesized, non-forwarded `SignatureDirection::Input`.
TEST(CanonicalizeStageTest, DomainStageMapsPrimitiveIDAsSynthesizedInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_PrimitiveID = external addrspace(7) constant i32, !spirv.Decorations !0
    @gl_out_pos = external addrspace(8) global <4 x float>, !spirv.Decorations !1
    define void @main() #0 {
      %pid = load i32, ptr addrspace(7) @gl_PrimitiveID
      %pidf = uitofp i32 %pid to float
      %v = insertelement <4 x float> poison, float %pidf, i32 0
      store <4 x float> %v, ptr addrspace(8) @gl_out_pos
      ret void
    }
    attributes #0 = { "feme.shader.stage"="domain" }
    !0 = !{!2, !3}
    !1 = !{!4}
    !2 = !{i32 11, i32 7}
    !3 = !{i32 15}
    !4 = !{i32 11, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 2u);

  const SignatureElement &PrimitiveID = Sig->Elements[0];
  EXPECT_EQ(PrimitiveID.Direction, SignatureDirection::Input);
  EXPECT_EQ(PrimitiveID.SystemValue, SignatureSystemValue::PrimitiveID);
  EXPECT_EQ(PrimitiveID.Frequency, SignatureFrequency::PerPatch);
}

/// (Roadmap H5c) A geometry entry point's `BuiltIn PrimitiveId` (code 7,
/// `gl_PrimitiveIDIn` as an `Input`), `InvocationId` (code 8,
/// `gl_InvocationID`), `Layer`/`ViewportIndex` (codes 9/10,
/// `gl_Layer`/`gl_ViewportIndex` as `Output`s), and `PrimitiveId` again
/// (this time as an `Output`, `gl_PrimitiveID`) all map onto the same
/// `SignatureSystemValue`s `getSystemValueForBuiltIn` already produces for
/// every other stage -- unlike Hull/Domain (`HullStageMapsInvocationIdAnd
/// PatchVertices`/`DomainStageMapsTessCoordAndPatchInput` above), a
/// geometry entry needs no barrier-splitting (`canonicalizeSPIRVHullStage`)
/// and no new system-value work, per `CanonicalizeStagePass::run` now
/// routing `ShaderStage::Geometry` straight through `canonicalizeSPIRVStage`
/// with `SPIRVCanonicalPhase::Ordinary`, exactly like Domain.
TEST(CanonicalizeStageTest, GeometryStageMapsSystemValues) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_PrimitiveIDIn = external addrspace(7) constant i32, !spirv.Decorations !0
    @gl_InvocationID = external addrspace(7) constant i32, !spirv.Decorations !1
    @gl_Layer = external addrspace(8) global i32, !spirv.Decorations !2
    @gl_ViewportIndex = external addrspace(8) global i32, !spirv.Decorations !3
    @gl_PrimitiveID = external addrspace(8) global i32, !spirv.Decorations !4
    define void @main() #0 {
      %pid = load i32, ptr addrspace(7) @gl_PrimitiveIDIn
      %iid = load i32, ptr addrspace(7) @gl_InvocationID
      %sum = add i32 %pid, %iid
      store i32 %sum, ptr addrspace(8) @gl_Layer
      store i32 %sum, ptr addrspace(8) @gl_ViewportIndex
      store i32 %sum, ptr addrspace(8) @gl_PrimitiveID
      ret void
    }
    attributes #0 = { "feme.shader.stage"="geometry" }
    !0 = !{!5}
    !1 = !{!6}
    !2 = !{!7}
    !3 = !{!8}
    !4 = !{!9}
    !5 = !{i32 11, i32 7}
    !6 = !{i32 11, i32 8}
    !7 = !{i32 11, i32 9}
    !8 = !{i32 11, i32 10}
    !9 = !{i32 11, i32 7}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 5u);

  const SignatureElement &PrimitiveIDIn = Sig->Elements[0];
  EXPECT_EQ(PrimitiveIDIn.Direction, SignatureDirection::Input);
  EXPECT_EQ(PrimitiveIDIn.SystemValue, SignatureSystemValue::PrimitiveID);

  const SignatureElement &InvocationId = Sig->Elements[1];
  EXPECT_EQ(InvocationId.Direction, SignatureDirection::Input);
  EXPECT_EQ(InvocationId.SystemValue, SignatureSystemValue::InvocationID);

  const SignatureElement &Layer = Sig->Elements[2];
  EXPECT_EQ(Layer.Direction, SignatureDirection::Output);
  EXPECT_EQ(Layer.SystemValue, SignatureSystemValue::RenderTargetArrayIndex);

  const SignatureElement &ViewportIndex = Sig->Elements[3];
  EXPECT_EQ(ViewportIndex.Direction, SignatureDirection::Output);
  EXPECT_EQ(ViewportIndex.SystemValue,
            SignatureSystemValue::ViewportArrayIndex);

  const SignatureElement &PrimitiveID = Sig->Elements[4];
  EXPECT_EQ(PrimitiveID.Direction, SignatureDirection::Output);
  EXPECT_EQ(PrimitiveID.SystemValue, SignatureSystemValue::PrimitiveID);
}

/// (Roadmap H5e-d) A geometry entry compiled from an `emit`-count shape
/// that ends its primitive without ever emitting on that stream/count
/// combination (e.g. a CTS `dEQP-VK.geometry.emit.*_emit_0_end_1` case)
/// calls only `EndPrimitive` -- already lowered to `feme.stage.stream.cut`
/// by `SPIRVToLLVMPatterns` by the time this pass runs -- and neither
/// reads nor writes a single stage-IO global. Confirm this still gets an
/// (empty) `!feme.signature` attached: `feme::cpu::GeometryWrapperPass`
/// (`GeometryWrapper.cpp`) hard-requires one on any geometry entry using a
/// stage op at all, a stream cut included, and previously never got one
/// here since the signature-building branch was scoped to entries with at
/// least one stage-IO global.
TEST(CanonicalizeStageTest, GeometryStreamCutOnlyEntryStillGetsASignature) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      call void @feme.stage.stream.cut(i32 0)
      ret void
    }
    declare void @feme.stage.stream.cut(i32)
    attributes #0 = { "feme.shader.stage"="geometry" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  EXPECT_TRUE(Sig->Elements.empty());
}

/// (Roadmap H91) A mesh entry that calls `SetMeshOutputsEXT(0, 0)` -- the
/// real shape a CTS `properties.{mesh,task}_{payload,shared_memory,
/// payload_and_shared_memory}_size` case's own mesh shader takes, since its
/// only real work is reading a task payload/shared memory back and writing
/// a pass/fail flag into an ordinary storage-buffer resource, never a
/// single per-vertex/per-primitive `Output` -- reads/writes no stage-IO
/// global at all, hitting the exact same "discovery loop found nothing,
/// signature-building branch above never ran" gap
/// `GeometryStreamCutOnlyEntryStillGetsASignature` above already covers
/// for geometry's own analogous stream-cut-only shape.
/// `feme::cpu::MeshOutputWrapperPass` is exactly as strict about requiring
/// an attached signature as `GeometryWrapperPass` is, so before this row's
/// own fix this left the mesh entry with no `!feme.signature` metadata at
/// all, later hitting `MeshOutputWrapperPass`'s own "requires attached
/// feme.signature metadata" diagnostic instead.
TEST(CanonicalizeStageTest, MeshSetOutputsOnlyEntryStillGetsASignature) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() #0 {
      call void @feme.stage.set_mesh_outputs(i32 0, i32 0)
      ret void
    }
    declare void @feme.stage.set_mesh_outputs(i32, i32)
    attributes #0 = { "feme.shader.stage"="mesh" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  EXPECT_TRUE(Sig->Elements.empty());
}

/// (Roadmap H92) A mesh entry's own per-vertex `Output` block whose one
/// array member is *itself* dynamically indexed by a second, independent
/// loop variable -- `loc[pointIdx].elements[elemIdx] = ...`, the real
/// shape `dEQP-VK.mesh_shader.ext.properties.max_mesh_output_size_
/// with_payload_per_vertex_no_view_index`'s own mesh shader compiles a
/// per-vertex `layout(location=0) out LocationBlock loc[];` (`struct
/// LocationBlock { uvec4 elements[locationCount]; };`, `locationCount` a
/// spec constant already resolved to a compile-time array extent by the
/// time this pass runs) write into -- both the outer per-vertex array
/// dimension (`pointIdx`) and the inner `elements` array dimension
/// (`elemIdx`) are genuinely non-constant, not the single dynamic index
/// every other dynamically-indexed shape this file covers has. Before
/// H92's own fix, `getDynamicVertexIndexedAccess` only allowed the one,
/// outer (vertex) index to be non-constant -- any further non-constant
/// index made it bail out entirely, leaving this access unrewritten and
/// surfacing later as `feme-graphics-validate-stage`'s "unresolved
/// stage-IO global-variable access" diagnostic.
TEST(CanonicalizeStageTest, MeshStageCanonicalizesDoublyDynamicOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    %struct.LocationBlock = type { [4 x <4 x i32>] }
    @loc = external addrspace(8) global [96 x %struct.LocationBlock], !spirv.Decorations !0
    define void @main(i32 %pointIdx, i32 %elemIdx, <4 x i32> %v) #0 {
      %p = getelementptr inbounds [96 x %struct.LocationBlock], ptr addrspace(8) @loc, i32 0, i32 %pointIdx, i32 0, i32 %elemIdx
      store <4 x i32> %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *PointIdxArg = F->getArg(0);
  Argument *ElemIdxArg = F->getArg(1);

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    // Operand order: ElementID, Row, Component, Val, Vertex.
    EXPECT_EQ(CI->getArgOperand(1), ElemIdxArg);
    EXPECT_EQ(CI->getArgOperand(4), PointIdxArg);
  }
  EXPECT_EQ(SeenStores, 4u);
  // Unlike `MeshStageCanonicalizesOutputArrayStore` above (a compile-time
  // constant `Row`), this element's shadow value (roadmap H2e) is keyed
  // on a genuinely dynamic `Row` (`elemIdx`), so `ShadowValueMap::
  // getOrCreate` gives it a non-promotable, `RowCount`-sized array
  // alloca instead of one plain scalar alloca per row -- real `StoreInst`s
  // into that array legitimately remain after `PromoteMemToReg`, unlike
  // every other test in this file that only exercises a constant `Row`.
  // What matters here is that the original store directly through `@loc`
  // itself (the raw SPIR-V stage-IO global) is gone.
  for (Instruction &I : instructions(F)) {
    auto *SI = dyn_cast<StoreInst>(&I);
    if (!SI)
      continue;
    EXPECT_NE(SI->getPointerOperand()->stripPointerCasts(),
             M->getGlobalVariable("loc"));
  }
}

/// (Roadmap H76) The real shape a `dEQP-VK.mesh_shader.ext.smoke.fast_lib.
/// depth_only_points_position_components`/`depth_only_triangles_position_
/// components` mesh entry's own per-component position write compiles
/// into: `gl_MeshVerticesEXT[outIndex].gl_Position.x = ...` (and `.y`/`.z`/
/// `.w` likewise), where `outIndex` -- unlike every other
/// constant-vertex-index test in this file -- is a genuinely dynamic
/// per-invocation value (`col * primitiveVertices + i`, `col` itself
/// `gl_LocalInvocationIndex`). `getDynamicVertexIndexedAccess`'s own
/// constant-index loop (peeling whatever follows the one non-constant
/// vertex index) only ever handled `StructType` (a builtin interface
/// block's own member) and `ArrayType` (a further-nested array
/// dimension) -- a `FixedVectorType` (the vector-component index this
/// per-component write's own trailing `i32 0`/`1`/`2`/`3` GEP index
/// walks into, once `gl_Position`'s own struct member has already been
/// peeled) fell through to the function's final `return std::nullopt`,
/// leaving the whole access unrewritten and surfacing later as
/// `feme-graphics-validate-stage`'s "unresolved stage-IO global-variable
/// access" diagnostic -- exactly this milestone's own real CTS failures.
/// Fixed by adding a `FixedVectorType` case to that loop, mirroring
/// `resolveRowComponent`'s own vector-component byte-offset accumulation.
TEST(CanonicalizeStageTest,
     ThreadsDynamicVertexIndexThroughVectorComponentOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_mesh_verts = external addrspace(8) global [64 x { <4 x float>, float }], !feme.spirv.MemberDecorations !10
    define void @main(i32 %outIndex, float %x, float %y, float %z, float %w) #0 {
      %px = getelementptr inbounds [64 x { <4 x float>, float }], ptr addrspace(8) @gl_mesh_verts, i32 0, i32 %outIndex, i32 0, i32 0
      store float %x, ptr addrspace(8) %px
      %py = getelementptr inbounds [64 x { <4 x float>, float }], ptr addrspace(8) @gl_mesh_verts, i32 0, i32 %outIndex, i32 0, i32 1
      store float %y, ptr addrspace(8) %py
      %pz = getelementptr inbounds [64 x { <4 x float>, float }], ptr addrspace(8) @gl_mesh_verts, i32 0, i32 %outIndex, i32 0, i32 2
      store float %z, ptr addrspace(8) %pz
      %pw = getelementptr inbounds [64 x { <4 x float>, float }], ptr addrspace(8) @gl_mesh_verts, i32 0, i32 %outIndex, i32 0, i32 3
      store float %w, ptr addrspace(8) %pw
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
    !10 = !{!11, !12}
    !11 = !{i32 0, !13}
    !12 = !{i32 1, !14}
    !13 = !{!15}
    !15 = !{i32 11, i32 0}
    !14 = !{!16}
    !16 = !{i32 11, i32 1}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *OutIndexArg = F->getArg(0);

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 2u);
  EXPECT_EQ(Sig->Elements[0].SystemValue, SignatureSystemValue::Position);
  EXPECT_EQ(Sig->Elements[0].ComponentCount, 4u);

  unsigned SeenStores = 0;
  SmallVector<uint64_t, 4> SeenComponents;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(),
              Sig->Elements[0].ElementID);
    auto *Component = dyn_cast<ConstantInt>(CI->getArgOperand(2));
    ASSERT_TRUE(Component);
    SeenComponents.push_back(Component->getZExtValue());
    EXPECT_EQ(CI->getArgOperand(4), OutIndexArg);
  }
  EXPECT_EQ(SeenStores, 4u);
  llvm::sort(SeenComponents);
  EXPECT_EQ(SeenComponents, (SmallVector<uint64_t, 4>{0, 1, 2, 3}));
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));
}

/// (Roadmap H6i) `CanonicalizeStagePass::run`'s stage filter now accepts
/// `ShaderStage::Mesh`, routing it through `canonicalizeSPIRVStage` the
/// same way `ThreadsDynamicVertexIndexIntoOutputStore` above already
/// exercised with a `"vertex"`-tagged stand-in (the filter did not accept
/// `Mesh` yet when that test was written, roadmap H6b). Re-run with a
/// genuine `"mesh"` stage attribute to confirm the whole pass -- not just
/// `canonicalizeSPIRVStage` called directly -- now actually reaches a mesh
/// entry's own dynamically-indexed per-vertex `Output`-array store.
TEST(CanonicalizeStageTest, MeshStageCanonicalizesOutputArrayStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_verts = external addrspace(8) global [3 x <4 x float>], !spirv.Decorations !0
    define void @main(i32 %i, <4 x float> %v) #0 {
      %p = getelementptr inbounds [3 x <4 x float>], ptr addrspace(8) @out_verts, i32 0, i32 %i
      store <4 x float> %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *IArg = F->getArg(0);

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    EXPECT_EQ(CI->getArgOperand(4), IArg);
    EXPECT_FALSE(isa<Constant>(CI->getArgOperand(4)));
  }
  EXPECT_EQ(SeenStores, 4u);
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));
}

/// (Roadmap H6j) A real `dEQP-VK.mesh_shader.ext.in_out.*` case's plain
/// (non-block) per-vertex output varying (e.g. `layout(location=0) out
/// vec4 v_color[];`) reflects `RowCount`/`ComponentCount` for the *fragment-
/// visible* per-vertex shape (1, 4), not the outer per-vertex array extent
/// (`OutputVertices`, here 3) `getStageIORowShape` would otherwise fold in
/// -- unlike `MeshStageCanonicalizesOutputArrayStore` above, which only
/// checks the store rewrite itself and does not assert on the resulting
/// signature's shape. Before this row's own fix, this element's `RowCount`
/// was wrongly 3 (the array extent), disagreeing with the fragment stage's
/// own unarrayed `vec4` input at the same location -- `GraphicsPipeline.
/// cpp`'s `validateStageInterfaces`/`feme::graphics::executeDraws`'s own
/// varying-linking loop, both of which compare `RowCount` by `Location`
/// across stages -- and failing at `vkQueueSubmit` with "vertex output and
/// fragment input at location %u disagree on component/row count or
/// type" despite both sides sharing the same `layout(location=...)`.
/// `RowCountIsVertexArray` stays `false`: the array dimension is peeled
/// off here (like a builtin interface block's own per-member element),
/// not folded into `RowCount` and flagged.
TEST(CanonicalizeStageTest, MeshStagePeelsPerVertexArrayFromOutputRowCount) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_verts = external addrspace(8) global [3 x <4 x float>], !spirv.Decorations !0
    define void @main(i32 %i, <4 x float> %v) #0 {
      %p = getelementptr inbounds [3 x <4 x float>], ptr addrspace(8) @out_verts, i32 0, i32 %i
      store <4 x float> %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  EXPECT_EQ(Sig->Elements[0].Location, 0u);
  EXPECT_EQ(Sig->Elements[0].RowCount, 1u);
  EXPECT_EQ(Sig->Elements[0].ComponentCount, 4u);
  EXPECT_FALSE(Sig->Elements[0].RowCountIsVertexArray);
}

/// (Roadmap H29g) A hull entry's own plain (non-block) per-control-point
/// output varying (e.g. `layout(location=0) out vec4 vtxColor[];` against
/// `layout(vertices = 3) out;`) takes the same treatment as the mesh shape
/// above and for the same reason: its outer array dimension is the output
/// patch's own control point count, not a matrix row count, and the domain
/// stage links against it by `Location` expecting the single control point
/// each of its own inputs describes. Before this row's own fix this
/// element's `RowCount` was wrongly 3, failing at `vkQueueSubmit` with
/// "hull stage output -> domain stage input: element %u and its producer
/// element %u disagree on component/row count or type".
TEST(CanonicalizeStageTest,
     HullStagePeelsPerControlPointArrayFromOutputRowCount) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_cps = external addrspace(8) global [3 x <4 x float>], !spirv.Decorations !0
    define void @main(i32 %i, <4 x float> %v) #0 {
      %p = getelementptr inbounds [3 x <4 x float>], ptr addrspace(8) @out_cps, i32 0, i32 %i
      store <4 x float> %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="hull" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  std::optional<EntrySignature> Sig =
      dxil::getEntrySignature(*M->getFunction("main"));
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  EXPECT_EQ(Sig->Elements[0].Location, 0u);
  EXPECT_EQ(Sig->Elements[0].RowCount, 1u);
  EXPECT_EQ(Sig->Elements[0].ComponentCount, 4u);
  EXPECT_FALSE(Sig->Elements[0].RowCountIsVertexArray);
}

/// (Roadmap L24(b)) `gl_TessLevelOuter`/`gl_TessLevelInner` (`BuiltIn
/// TessLevelOuter`/`TessLevelInner`, always `Patch`-decorated) are *not*
/// shaped like the per-control-point output array
/// `HullStagePeelsPerControlPointArrayFromOutputRowCount` above covers --
/// every one of their (up to four) rows is a genuine, independently
/// meaningful whole-patch tess factor, not one row per control point --
/// so `PerInvocationOutputArray`'s peeling must not apply to them. Before
/// this fix, this element's `RowCount` was wrongly collapsed to `1`
/// (`std::ceil`/`computeSegmentCount`-fed `feme::graphics::TessFactors::
/// Edges[1]` and beyond then silently stuck at its `1.0` default,
/// undercounting real per-line tessellation-factor rows) purely because
/// this global happens to share the same address space (8) and stage
/// (`Hull`) `PerInvocationOutputArray` otherwise recognizes -- exactly the
/// gap `Graphics/IsolineDomainTessellation.test` (roadmap L24(b)) hit.
/// Mixes in a genuine per-control-point output write too (no barrier
/// between them, the same barrierless-mixed-frequency shape
/// `NoBarrierMixedFrequencyEntryWithDynamicVertexIndexedStoreIsSplitAndPruned`
/// above already exercises) so `splitBarrierlessTessellationControlEntry`
/// produces a real `main.patchconstant` sibling to read the `TessFactorEdge`
/// element's `RowCount` off of.
TEST(CanonicalizeStageTest,
     HullStageDoesNotPeelPatchTessFactorOutputRowCount) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_cps = external addrspace(8) global [3 x <4 x float>], !spirv.Decorations !0
    @gl_TessLevelOuter = external addrspace(8) global [4 x float], !spirv.Decorations !1
    define void @main(i32 %i, <4 x float> %v) #0 {
      %p = getelementptr inbounds [3 x <4 x float>], ptr addrspace(8) @out_cps, i32 0, i32 %i
      store <4 x float> %v, ptr addrspace(8) %p
      %p0 = getelementptr inbounds [4 x float], ptr addrspace(8) @gl_TessLevelOuter, i32 0, i32 0
      store float 1.0, ptr addrspace(8) %p0
      %p1 = getelementptr inbounds [4 x float], ptr addrspace(8) @gl_TessLevelOuter, i32 0, i32 1
      store float 4.0, ptr addrspace(8) %p1
      ret void
    }
    attributes #0 = { "feme.shader.stage"="hull" }
    !0 = !{!2}
    !1 = !{!3, !4}
    !2 = !{i32 30, i32 0}
    !3 = !{i32 11, i32 11}
    !4 = !{i32 15}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  Function *PatchConstant = M->getFunction("main.patchconstant");
  ASSERT_TRUE(PatchConstant);
  std::optional<EntrySignature> PCSig =
      dxil::getEntrySignature(*PatchConstant);
  ASSERT_TRUE(PCSig.has_value());
  ASSERT_EQ(PCSig->Elements.size(), 1u);
  EXPECT_EQ(PCSig->Elements[0].SystemValue,
            SignatureSystemValue::TessFactorEdge);
  EXPECT_EQ(PCSig->Elements[0].RowCount, 4u);
  EXPECT_FALSE(PCSig->Elements[0].RowCountIsVertexArray);
}

/// (Roadmap L24(b)) The `Input`-side counterpart of the fix above: a
/// Domain stage's own `gl_TessLevelOuter` read-back must not be flagged
/// `RowCountIsVertexArray` either, even though it is an address-space-7
/// (`Input`) array on a stage (`Domain`) `isPerVertexArrayInputGlobal`
/// otherwise treats as per-vertex-arrayed (e.g. `gl_in[]`-shaped control
/// points) -- it is `Patch`-decorated, the same whole-patch (not
/// per-vertex) shape as the `Output`-side test above. Before this fix,
/// `StageLink.cpp`'s `effectiveRowCount` folded this element's real
/// `RowCount` (4) down to `1` when linking it against the Hull stage's own
/// producer, a spurious `vkQueueSubmit`-time "disagree on component/row
/// count or type" -- `Feature/Semantics/HullSystemValues.test`'s own
/// regression while fixing the `Output`-side row above, since before it,
/// both sides happened to independently (for different reasons)
/// mis-collapse to `RowCount == 1` and coincidentally agree.
TEST(CanonicalizeStageTest,
     DomainStageDoesNotTreatPatchTessFactorInputAsPerVertexArray) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_TessLevelOuter = external addrspace(7) constant [4 x float], !spirv.Decorations !0
    @gl_out_pos = external addrspace(8) global <4 x float>, !spirv.Decorations !1
    define void @main() #0 {
      %tf = load [4 x float], ptr addrspace(7) @gl_TessLevelOuter
      %tf0 = extractvalue [4 x float] %tf, 0
      %v = insertelement <4 x float> poison, float %tf0, i32 0
      store <4 x float> %v, ptr addrspace(8) @gl_out_pos
      ret void
    }
    attributes #0 = { "feme.shader.stage"="domain" }
    !0 = !{!2, !3}
    !1 = !{!4}
    !2 = !{i32 11, i32 11}
    !3 = !{i32 15}
    !4 = !{i32 11, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));

  std::optional<EntrySignature> Sig =
      dxil::getEntrySignature(*M->getFunction("main"));
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 2u);
  const SignatureElement &TessLevelOuter = Sig->Elements[0];
  EXPECT_EQ(TessLevelOuter.SystemValue, SignatureSystemValue::TessFactorEdge);
  EXPECT_EQ(TessLevelOuter.RowCount, 4u);
  EXPECT_FALSE(TessLevelOuter.RowCountIsVertexArray);
}

/// (Roadmap H6k) A real `dEQP-VK.mesh_shader.ext.in_out.*` mesh entry's own
/// per-vertex output store is not always dynamically indexed the way
/// `MeshStageCanonicalizesOutputArrayStore` above models it -- glslang
/// commonly unrolls a small, compile-time-bounded per-vertex output loop
/// into one *constant*-indexed store per vertex instead (`out_verts[1] =
/// ...`, not `out_verts[i] = ...`). Before this row's own fix,
/// `resolveStageIOAccess`'s ordinary constant-offset path folded that
/// constant index into an ordinary `Row` operand rather than `Vertex` --
/// exactly the roadmap H6b-era treatment deliberately kept for every
/// *other* stage's own genuine per-row matrix output store, but wrong here
/// because a mesh entry's plain `Output` global's `RowCount` has been
/// peeled to the fragment-visible per-vertex shape (roadmap H6j, `1` for a
/// `vec4`) rather than the array's own extent: a constant vertex index
/// greater than 0 folded into `Row` this way silently exceeds that
/// `RowCount`, corrupting `feme::graphics::StageStorage`'s own storage
/// past its allocated bounds at runtime (roadmap H6k's own named
/// heap-corruption crash) rather than being rejected or handled correctly.
/// Confirms the fix directly: a constant `out_verts[1]` store's own
/// `feme.stage.output.store` call now threads a constant `1` through the
/// `Vertex` operand (index 4), not `Row` (index 1, which must stay the
/// ordinary default `0`).
TEST(CanonicalizeStageTest, FoldsConstantVertexIndexIntoOutputStoreForMesh) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @out_verts = external addrspace(8) global [3 x <4 x float>], !spirv.Decorations !0
    define void @main(<4 x float> %v) #0 {
      %p = getelementptr inbounds [3 x <4 x float>], ptr addrspace(8) @out_verts, i32 0, i32 1
      store <4 x float> %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  EXPECT_EQ(Sig->Elements[0].RowCount, 1u);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    auto *Row = dyn_cast<ConstantInt>(CI->getArgOperand(1));
    ASSERT_TRUE(Row);
    EXPECT_EQ(Row->getZExtValue(), 0u);
    auto *Vertex = dyn_cast<ConstantInt>(CI->getArgOperand(4));
    ASSERT_TRUE(Vertex);
    EXPECT_EQ(Vertex->getZExtValue(), 1u);
  }
  EXPECT_EQ(SeenStores, 4u);
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));
}

/// (Roadmap H6k) The single-member-block counterpart to
/// `FoldsConstantVertexIndexIntoOutputStoreForMesh` above: a real
/// `dEQP-VK.mesh_shader.ext.in_out.*` mesh entry's `gl_MeshVerticesEXT`
/// (`out gl_MeshPerVertexEXT { vec4 gl_Position; } gl_MeshVerticesEXT[];`)
/// is a single-member builtin interface block (one `!feme.spirv.
/// MemberDecorations`-decorated member, `gl_Position`), not a plain
/// per-vertex array -- `addElements`'s own block-decompose path
/// (`isSPIRVStageIOGlobal`'s `MemberDecorations` branch) still assigns it
/// exactly one `ElementID`, same as a plain global would, but reaches
/// `resolveStageIOAccess` down the "block" side rather than the "plain
/// array" one `FoldsConstantVertexIndexIntoOutputStoreForMesh` exercises.
/// This is the *exact* shape a real, glslang-compiled
/// `dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_1.mesh_only`
/// case's own `gl_MeshVerticesEXT[k].gl_Position = ...` constant-indexed,
/// compile-time-unrolled store takes (confirmed against a real `deqp-vk`
/// run's own SPIR-V disassembly) -- the specific real-world shape this
/// row's own fix (extending `isPerVertexArrayMeshOutputGlobal` to cover a
/// builtin block, not just a plain array) was needed to unblock; the
/// earlier, narrower fix (plain-array-only) left this exact shape still
/// routing through the ordinary matrix-`Row` fold and still corrupting
/// `StageStorage` at runtime for every real mesh case using
/// `gl_MeshVerticesEXT`/`gl_MeshPrimitivesEXT`.
TEST(CanonicalizeStageTest,
     FoldsConstantVertexIndexIntoSingleMemberInterfaceBlockOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_mesh_verts = external addrspace(8) global [4 x { <4 x float> }], !feme.spirv.MemberDecorations !10
    define void @main(<4 x float> %v) #0 {
      %p = getelementptr inbounds [4 x { <4 x float> }], ptr addrspace(8) @gl_mesh_verts, i32 0, i32 1, i32 0
      store <4 x float> %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
    !10 = !{!11}
    !11 = !{i32 0, !12}
    !12 = !{!13}
    !13 = !{i32 11, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  EXPECT_EQ(Sig->Elements[0].SystemValue, SignatureSystemValue::Position);
  EXPECT_EQ(Sig->Elements[0].RowCount, 1u);
  EXPECT_EQ(Sig->Elements[0].ComponentCount, 4u);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    auto *Row = dyn_cast<ConstantInt>(CI->getArgOperand(1));
    ASSERT_TRUE(Row);
    EXPECT_EQ(Row->getZExtValue(), 0u);
    auto *Vertex = dyn_cast<ConstantInt>(CI->getArgOperand(4));
    ASSERT_TRUE(Vertex);
    EXPECT_EQ(Vertex->getZExtValue(), 1u);
  }
  EXPECT_EQ(SeenStores, 4u);
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));
}

/// (Roadmap H6l) The exact shape a real
/// `dEQP-VK.mesh_shader.ext.builtin.cull_primitives` mesh entry's own
/// `gl_MeshVerticesEXT[k].gl_Position = ...` constant-indexed,
/// compile-time-unrolled per-vertex store takes once its own
/// `gl_MeshPerVertexEXT` block carries all four of `gl_PerVertex`'s
/// members (`gl_Position`/`gl_PointSize`/`gl_ClipDistance`/
/// `gl_CullDistance`), not just
/// `FoldsConstantVertexIndexIntoSingleMemberInterfaceBlockOutputStore`'s
/// `gl_Position`-only shape. `{<4 x float>, float, [1 x float], [1 x
/// float]}`'s own `StructLayout` places `gl_Position` at byte 0,
/// `gl_PointSize` at 16, `gl_ClipDistance` at 20 and `gl_CullDistance` at
/// 24 (the same offsets `RecognizesInterfaceBlockPerMemberByteOffsetAccess`
/// above already covers for one, unarrayed instance) -- so *this* struct's
/// own last member ends at byte 28, not the 32 `DataLayout::
/// getTypeAllocSize` reports once the struct's own leading `<4 x float>`
/// member forces 16-byte alignment padding at its tail. Before this row,
/// `resolveStageIOAccess`'s constant-vertex-index fold used that
/// ABI-padded 32 as `VertexSize`, so vertex *1*'s own `gl_Position` (real,
/// SPIR-V-embedded byte offset 28) resolved with `VertexIdx == 0` and
/// `Residual == 28` -- landing past `gl_Position`'s own member (offset 0)
/// and inside `gl_CullDistance`'s (offset 24, one 4-byte element), an
/// out-of-range `Row`/`Component` for that 1-element member, exactly this
/// row's own `feme-graphics-validate-stage` diagnostic. Fixed by
/// `getPackedMeshElementSize`, which reports the tightly packed 28 --
/// matching the real embedded offsets -- instead.
TEST(CanonicalizeStageTest,
     FoldsConstantVertexIndexIntoMultiMemberInterfaceBlockOutputStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_mesh_verts = external addrspace(8) global [2 x { <4 x float>, float, [1 x float], [1 x float] }], !feme.spirv.MemberDecorations !10
    define void @main(<4 x float> %v) #0 {
      %p = getelementptr inbounds [2 x { <4 x float>, float, [1 x float], [1 x float] }], ptr addrspace(8) @gl_mesh_verts, i32 0, i32 1, i32 0
      store <4 x float> %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
    !10 = !{!11, !12, !13, !14}
    !11 = !{i32 0, !15}
    !12 = !{i32 1, !16}
    !13 = !{i32 2, !17}
    !14 = !{i32 3, !18}
    !15 = !{!19}
    !19 = !{i32 11, i32 0}
    !16 = !{!20}
    !20 = !{i32 11, i32 1}
    !17 = !{!21}
    !21 = !{i32 11, i32 3}
    !18 = !{!22}
    !22 = !{i32 11, i32 4}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 4u);
  EXPECT_EQ(Sig->Elements[0].SystemValue, SignatureSystemValue::Position);
  EXPECT_EQ(Sig->Elements[0].ComponentCount, 4u);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(0))->getZExtValue(),
              Sig->Elements[0].ElementID);
    auto *Vertex = dyn_cast<ConstantInt>(CI->getArgOperand(4));
    ASSERT_TRUE(Vertex);
    EXPECT_EQ(Vertex->getZExtValue(), 1u);
  }
  EXPECT_EQ(SeenStores, 4u);
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));
}

/// (Roadmap H6l) The plain-array counterpart of
/// `FoldsConstantVertexIndexIntoMultiMemberInterfaceBlockOutputStore`
/// above: a real `dEQP-VK.mesh_shader.ext.builtin.cull_primitives` mesh
/// entry's own `gl_PrimitiveTriangleIndicesEXT[k] = ...` constant-indexed
/// store addresses a `[N x <3 x i32>]` (`uvec3[]`), whose own element
/// (`uvec3`) allocates to 16 bytes (`DataLayout::getTypeAllocSize` pads a
/// 3-wide vector up to a 4-wide SIMD register) but is addressed 12 bytes
/// apart in the real, SPIR-V-embedded offsets (`uvec3`'s own tightly
/// packed 3-`i32` size) -- the same ABI-vs-packed gap
/// `FoldsConstantVertexIndexIntoMultiMemberInterfaceBlockOutputStore`
/// covers for a struct-shaped element, but for a vector-shaped one
/// instead. Before this row, primitive 2's own real byte offset (24)
/// divided by the ABI-padded 16 misrouted to `VertexIdx == 1` (not 2) --
/// silently misdirecting which primitive's own triangle indices a real
/// mesh entry's store actually lands on, since `Vertex` (unlike `Row`/
/// `Component`) is never range-checked by `ValidateStage.cpp`.
TEST(CanonicalizeStageTest,
     FoldsConstantVertexIndexIntoPlainVectorArrayOutputStoreWithPadding) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_prim_tri_indices = external addrspace(8) global [4 x <3 x i32>], !spirv.Decorations !0
    define void @main(<3 x i32> %v) #0 {
      %p = getelementptr inbounds [4 x <3 x i32>], ptr addrspace(8) @gl_prim_tri_indices, i32 0, i32 2
      store <3 x i32> %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
    !0 = !{!1}
    !1 = !{i32 30, i32 0}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  EXPECT_EQ(Sig->Elements[0].ComponentCount, 3u);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    auto *Vertex = dyn_cast<ConstantInt>(CI->getArgOperand(4));
    ASSERT_TRUE(Vertex);
    EXPECT_EQ(Vertex->getZExtValue(), 2u);
  }
  // `uvec3`'s 3 components each get their own `Row`-0 store (see
  // `loadStageIOValue`/`storeStageIOValue`'s own component recursion),
  // all sharing the one, correctly-folded `Vertex` operand this test
  // exists to check.
  EXPECT_EQ(SeenStores, 3u);
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));
}

/// (Roadmap H29r) The three `VK_EXT_mesh_shader` primitive-index builtins
/// (`gl_PrimitiveTriangleIndicesEXT`, SPIR-V `BuiltIn` 5294, plus its line
/// (5295) and point (5296) siblings) must reflect as
/// `SignatureSystemValue::PrimitiveIndices` at `PerPrimitive` frequency.
/// This is what lets `MeshOutputWrapperPass` route the resulting output
/// store into `FemeMeshArgs::PrimitiveIndices` -- a flat, primitive-major
/// `uint32_t` array of its own -- rather than into the per-vertex/
/// per-primitive *attribute* storage every other mesh output uses. Before
/// this row these three mapped to `None`, so a real
/// `gl_PrimitiveTriangleIndicesEXT[i] = uvec3(...)` write landed in the
/// per-vertex attribute block and `PrimitiveIndices` stayed at its
/// zero-initialized default, degenerating every emitted primitive to
/// "all vertices are vertex 0" (see `MeshOutputWrapper.h`'s file comment,
/// whose "left open by this row" note this row closes).
TEST(CanonicalizeStageTest,
     ClassifiesMeshPrimitiveIndexBuiltinsAsPrimitiveIndices) {
  for (auto [BuiltIn, Components] :
       {std::pair<unsigned, unsigned>{5294, 3}, {5295, 2}, {5296, 1}}) {
    LLVMContext Ctx;
    std::string IR = formatv(R"(
      @gl_prim_indices = external addrspace(8) global [2 x <{0} x i32>], !spirv.Decorations !0
      define void @main(<{0} x i32> %v) #0 {{
        %p = getelementptr inbounds [2 x <{0} x i32>], ptr addrspace(8) @gl_prim_indices, i32 0, i32 1
        store <{0} x i32> %v, ptr addrspace(8) %p
        ret void
      }
      attributes #0 = {{ "feme.shader.stage"="mesh" }
      !0 = !{{!1}
      !1 = !{{i32 11, i32 {1}}
    )",
                             Components, BuiltIn);
    std::unique_ptr<Module> M = parseIR(Ctx, IR);
    ASSERT_TRUE(M) << "BuiltIn " << BuiltIn;
    EXPECT_TRUE(run(*M)) << "BuiltIn " << BuiltIn;
    Function *F = M->getFunction("main");

    std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
    ASSERT_TRUE(Sig.has_value()) << "BuiltIn " << BuiltIn;
    ASSERT_EQ(Sig->Elements.size(), 1u) << "BuiltIn " << BuiltIn;
    EXPECT_EQ(Sig->Elements[0].SystemValue,
              SignatureSystemValue::PrimitiveIndices)
        << "BuiltIn " << BuiltIn;
    // Per-primitive by definition, even though SPIR-V does not also
    // decorate these three `PerPrimitiveEXT`.
    EXPECT_EQ(Sig->Elements[0].Frequency, SignatureFrequency::PerPrimitive)
        << "BuiltIn " << BuiltIn;
    // The topology's own vertices-per-primitive: 3/2/1 for triangles/
    // lines/points.
    EXPECT_EQ(Sig->Elements[0].ComponentCount, Components)
        << "BuiltIn " << BuiltIn;
  }
}

/// (Roadmap H6m) The exact shape a real
/// `dEQP-VK.mesh_shader.ext.builtin.cull_primitives` mesh entry's own
/// `gl_MeshPrimitivesEXT[i].gl_CullPrimitiveEXT = %v` store takes: SPIR-V's
/// `OpTypeBool` (`gl_CullPrimitiveEXT`'s own declared GLSL `bool`,
/// `VK_EXT_mesh_shader`'s per-primitive cull builtin, `BuiltIn` code 4485)
/// is LLVM `i1`, which `StageStorage::buildStageStorage` has no
/// addressable layout for -- its per-element strides are all hard-coded
/// to a 4-byte scalar -- and previously reached it unchanged, failing
/// `vkQueueSubmit` with "stage element N has a 1-bit scalar; only 32-bit
/// elements are implemented yet" now that H6l's own fix lets this case
/// clear `feme-graphics-validate-stage` for the first time. Fixed by
/// canonicalizing the `i1` scalar to an ordinary 32-bit element at this
/// SPIR-V-to-`feme.stage.*` boundary (`getComponentType`, `loadStageIOValue`/
/// `storeStageIOValue`), mirroring how a real GPU driver represents a
/// shader-visible `bool` in memory: the reflected `SignatureElement` is
/// `{Bool, 32}`, the emitted `feme.stage.output.store`'s value operand is
/// the `i1` zero-extended to `i32`, and `buildStageStorage` now succeeds
/// building storage for it instead of erroring.
TEST(CanonicalizeStageTest,
     CanonicalizesBoolPerPrimitiveOutputStoreToA32BitElement) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @gl_mesh_prims = external addrspace(8) global [4 x { i1 }], !feme.spirv.MemberDecorations !10
    define void @main(i32 %i, i1 %v) #0 {
      %p = getelementptr inbounds [4 x { i1 }], ptr addrspace(8) @gl_mesh_prims, i32 0, i32 %i, i32 0
      store i1 %v, ptr addrspace(8) %p
      ret void
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
    !10 = !{!11}
    !11 = !{i32 0, !12}
    !12 = !{!13, !14}
    !13 = !{i32 11, i32 4485}
    !14 = !{i32 5271}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *IArg = F->getArg(0);

  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);
  EXPECT_EQ(Sig->Elements[0].ComponentType, SignatureComponentType::Bool);
  EXPECT_EQ(Sig->Elements[0].BitWidth, 32u);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) || Kind != StageOpKind::OutputStore)
      continue;
    ++SeenStores;
    EXPECT_EQ(CI->getArgOperand(4), IArg);
    Value *StoredVal = CI->getArgOperand(3);
    EXPECT_TRUE(StoredVal->getType()->isIntegerTy(32));
    auto *ZExt = dyn_cast<ZExtInst>(StoredVal);
    ASSERT_TRUE(ZExt);
    EXPECT_TRUE(ZExt->getOperand(0)->getType()->isIntegerTy(1));
  }
  EXPECT_EQ(SeenStores, 1u);

  // `StageStorage::buildStageStorage` -- previously erroring "stage
  // element 0 has a 1-bit scalar; only 32-bit elements are implemented
  // yet" on this exact signature -- now succeeds, confirming the fix all
  // the way through the boundary this row's own scope covers.
  Expected<StageStorage> Storage = buildStageStorage(
      *Sig, SignatureDirection::Output, /*InvocationCount=*/4);
  ASSERT_THAT_EXPECTED(Storage, Succeeded());
  EXPECT_EQ(Storage->Elements[0].BitWidth, 32u);
  EXPECT_EQ(Storage->Elements[0].ScalarKind,
            static_cast<uint32_t>(cpu::StageLayoutScalarKind::Bool));
}

/// through `TaskPayloadGlobalVariablePattern`'s own address-space-14 global
/// import shape (roadmap H6h) -- canonicalizes into
/// `feme.stage.task.payload.store` by its resolved constant byte offset,
/// now that `CanonicalizeStagePass::run`'s stage filter accepts
/// `ShaderStage::Amplification` and routes it through
/// `canonicalizeSPIRVStage`. Unlike a stage-IO store, this carries no
/// `SignatureElement` at all: the payload is raw, task-defined memory, not
/// a signature (`!feme.signature` stays entirely absent -- neither
/// `InputGlobals` nor `OutputGlobals` see this address space at all).
TEST(CanonicalizeStageTest, AmplificationStageCanonicalizesTaskPayloadStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @payload = external addrspace(14) global { i32, [4 x float] }
    define void @main(float %v) #0 {
      store i32 1, ptr addrspace(14) @payload
      store float %v, ptr addrspace(14) getelementptr inbounds nuw (i8, ptr addrspace(14) @payload, i64 4)
      ret void
    }
    attributes #0 = { "feme.shader.stage"="amplification" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *VArg = F->getArg(0);

  EXPECT_FALSE(dxil::getEntrySignature(*F).has_value());

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) ||
        Kind != StageOpKind::TaskPayloadStore)
      continue;
    if (SeenStores == 0) {
      EXPECT_EQ(getStageOpConstantOperand(*CI, /*Offset=*/0), 0u);
      EXPECT_EQ(cast<ConstantInt>(CI->getArgOperand(1))->getZExtValue(), 1u);
    } else {
      EXPECT_EQ(getStageOpConstantOperand(*CI, /*Offset=*/0), 4u);
      EXPECT_EQ(CI->getArgOperand(1), VArg);
    }
    ++SeenStores;
  }
  EXPECT_EQ(SeenStores, 2u);
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));
}

/// (Roadmap L30, updated by H91) A mesh entry's own bounded payload read --
/// the load-side counterpart of `AmplificationStageCanonicalizesTaskPayloadStore`
/// above, through the very same `TaskPayloadGlobalVariablePattern`
/// address-space-14 global import shape, just read by the mesh workgroup a
/// task workgroup's `EmitMeshTasksEXT` dispatched instead of written by the
/// task workgroup itself -- canonicalizes into `feme.stage.task.payload.load`
/// by its resolved constant byte offset, now that `canonicalizeSPIRVStage`'s
/// `LoadInst` branch has a fallback mirroring its `StoreInst` branch's own.
/// This synthetic entry has no stage-IO global reads/writes and (unlike a
/// real mesh entry) no `SetMeshOutputsEXT` call either, but H91's own
/// Mesh-stage empty-signature fallback (mirroring the pre-existing
/// Geometry-stage one) still attaches an *empty* `SignatureElement`-less
/// signature here: `MeshOutputWrapperPass` requires an attached signature
/// for any mesh entry whose body contains a recognized stage op call --
/// which a `TaskPayloadLoad` already is -- regardless of whether that op is
/// itself an output/`SetMeshOutputsEXT` op, so leaving this entry
/// signature-less would reproduce H91's own "requires attached
/// feme.signature metadata" diagnostic the moment such a shape reached that
/// pass in the real pipeline.
TEST(CanonicalizeStageTest, MeshStageCanonicalizesTaskPayloadLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @payload = external addrspace(14) global { i32, [4 x float] }
    define float @main() #0 {
      %tag = load i32, ptr addrspace(14) @payload
      %v = load float, ptr addrspace(14) getelementptr inbounds nuw (i8, ptr addrspace(14) @payload, i64 4)
      ret float %v
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  auto Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  EXPECT_TRUE(Sig->Elements.empty());

  unsigned SeenLoads = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) ||
        Kind != StageOpKind::TaskPayloadLoad)
      continue;
    if (SeenLoads == 0) {
      EXPECT_TRUE(CI->getType()->isIntegerTy(32));
      EXPECT_EQ(getStageOpConstantOperand(*CI, /*Offset=*/0), 0u);
    } else {
      EXPECT_TRUE(CI->getType()->isFloatTy());
      EXPECT_EQ(getStageOpConstantOperand(*CI, /*Offset=*/0), 4u);
    }
    ++SeenLoads;
  }
  EXPECT_EQ(SeenLoads, 2u);
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<LoadInst>(&I));
}

/// (Roadmap L39) A vector-typed task-payload write -- e.g. a real
/// `float3`/`float4` payload member, unlike the plain scalar `float`
/// `AmplificationStageCanonicalizesTaskPayloadStore` above already covers
/// -- decomposes into one scalar `feme.stage.task.payload.store` per
/// vector lane at consecutive byte offsets, rather than reaching
/// `feme.stage.task.payload.store` with the whole `<3 x float>` wrapped
/// verbatim as its `value` operand (the shape that made
/// `feme::cpu::SIMDizePass`'s own generic per-lane widening fail with
/// "has a divergent value '' of vector type", since every other
/// `feme.stage.*` call's operands are always scalar).
TEST(CanonicalizeStageTest, AmplificationStageDecomposesVectorTaskPayloadStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @payload = external addrspace(14) global { <3 x float> }
    define void @main(<3 x float> %v) #0 {
      store <3 x float> %v, ptr addrspace(14) @payload
      ret void
    }
    attributes #0 = { "feme.shader.stage"="amplification" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *VArg = F->getArg(0);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) ||
        Kind != StageOpKind::TaskPayloadStore)
      continue;
    EXPECT_EQ(getStageOpConstantOperand(*CI, /*Offset=*/0), SeenStores * 4);
    Value *Val = CI->getArgOperand(1);
    EXPECT_TRUE(Val->getType()->isFloatTy());
    auto *EEI = dyn_cast<ExtractElementInst>(Val);
    ASSERT_TRUE(EEI);
    EXPECT_EQ(EEI->getVectorOperand(), VArg);
    EXPECT_EQ(cast<ConstantInt>(EEI->getIndexOperand())->getZExtValue(),
              SeenStores);
    ++SeenStores;
  }
  EXPECT_EQ(SeenStores, 3u);
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));
}

/// (Roadmap L39) The load-side mirror of
/// `AmplificationStageDecomposesVectorTaskPayloadStore` above: a
/// vector-typed task-payload read decomposes into one scalar
/// `feme.stage.task.payload.load` per vector lane at consecutive byte
/// offsets, rebuilt with `insertelement` into the original `<3 x float>`
/// result, rather than reaching `feme.stage.task.payload.load` with a
/// whole-vector result type.
TEST(CanonicalizeStageTest, MeshStageDecomposesVectorTaskPayloadLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @payload = external addrspace(14) global { <3 x float> }
    define <3 x float> @main() #0 {
      %v = load <3 x float>, ptr addrspace(14) @payload
      ret <3 x float> %v
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  unsigned SeenLoads = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) ||
        Kind != StageOpKind::TaskPayloadLoad)
      continue;
    EXPECT_TRUE(CI->getType()->isFloatTy());
    EXPECT_EQ(getStageOpConstantOperand(*CI, /*Offset=*/0), SeenLoads * 4);
    ++SeenLoads;
  }
  EXPECT_EQ(SeenLoads, 3u);

  auto *Ret = cast<ReturnInst>(F->back().getTerminator());
  Value *RetVal = Ret->getReturnValue();
  EXPECT_TRUE(RetVal->getType()->isVectorTy());
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<LoadInst>(&I));
}

/// (Roadmap L47) A task entry's own payload write through a genuinely
/// *dynamic* index into one of its own array members -- e.g.
/// `payload.branch[gl_LocalInvocationIndex] = ...`, the exact shape a
/// real `dEQP-VK.mesh_shader.ext.query.*.task_mesh.*` CTS case's own
/// source (`vktMeshShaderQueryTestsEXT.cpp`) compiles into -- unlike
/// `AmplificationStageCanonicalizesTaskPayloadStore` above, whose access
/// is always a compile-time-constant offset. Before this row,
/// `getStageIOBaseAndOffset`'s `stripAndAccumulateConstantOffsets` walk
/// could not fold the non-constant array index at all, so it stopped at
/// (and returned) the GEP itself rather than the `@payload` global,
/// leaving this store entirely unrewritten -- the raw `addrspace(14)`
/// store on the imported global survived all the way to
/// `feme::cpu::SIMDizePass`'s widening and then JIT link time, where the
/// global (never meant to survive this far) had no definition anywhere
/// for the JIT to resolve (`"JIT session error: Symbols not found"`).
/// `getTaskPayloadDynamicOffsetAccess` now recognizes this one additional
/// shape, computing a real dynamic *byte* offset `Value*` (the index
/// multiplied by the array element's own byte size) in its place.
TEST(CanonicalizeStageTest,
    AmplificationStageCanonicalizesDynamicTaskPayloadStore) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @payload = external addrspace(14) global { [4 x i32], i32 }
    define void @main(i32 %v, i32 %idx) #0 {
      %addr = getelementptr { [4 x i32], i32 }, ptr addrspace(14) @payload, i32 0, i32 0, i32 %idx
      store i32 %v, ptr addrspace(14) %addr
      ret void
    }
    attributes #0 = { "feme.shader.stage"="amplification" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *VArg = F->getArg(0);
  Argument *IdxArg = F->getArg(1);

  unsigned SeenStores = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) ||
        Kind != StageOpKind::TaskPayloadStore)
      continue;
    ++SeenStores;
    // The offset operand is not a constant -- it depends on %idx -- so
    // `getStageOpConstantOperand` (which only ever recognizes a literal
    // `ConstantInt`) correctly reports it as unresolvable at compile time.
    EXPECT_FALSE(getStageOpConstantOperand(*CI, /*Offset=*/0).has_value());
    auto *Mul = dyn_cast<BinaryOperator>(CI->getArgOperand(0));
    ASSERT_TRUE(Mul);
    EXPECT_EQ(Mul->getOpcode(), Instruction::Mul);
    EXPECT_EQ(Mul->getOperand(0), IdxArg);
    EXPECT_EQ(cast<ConstantInt>(Mul->getOperand(1))->getZExtValue(), 4u);
    EXPECT_EQ(CI->getArgOperand(1), VArg);
  }
  EXPECT_EQ(SeenStores, 1u);
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I));
}

/// (Roadmap L47) The load-side mirror of
/// `AmplificationStageCanonicalizesDynamicTaskPayloadStore` above -- a
/// mesh entry's own payload read through the same dynamically-indexed
/// array-member shape.
TEST(CanonicalizeStageTest, MeshStageCanonicalizesDynamicTaskPayloadLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @payload = external addrspace(14) global { [4 x i32], i32 }
    define i32 @main(i32 %idx) #0 {
      %addr = getelementptr { [4 x i32], i32 }, ptr addrspace(14) @payload, i32 0, i32 0, i32 %idx
      %v = load i32, ptr addrspace(14) %addr
      ret i32 %v
    }
    attributes #0 = { "feme.shader.stage"="mesh" }
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  Argument *IdxArg = F->getArg(0);

  unsigned SeenLoads = 0;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) ||
        Kind != StageOpKind::TaskPayloadLoad)
      continue;
    ++SeenLoads;
    EXPECT_TRUE(CI->getType()->isIntegerTy(32));
    EXPECT_FALSE(getStageOpConstantOperand(*CI, /*Offset=*/0).has_value());
    auto *Mul = dyn_cast<BinaryOperator>(CI->getArgOperand(0));
    ASSERT_TRUE(Mul);
    EXPECT_EQ(Mul->getOpcode(), Instruction::Mul);
    EXPECT_EQ(Mul->getOperand(0), IdxArg);
    EXPECT_EQ(cast<ConstantInt>(Mul->getOperand(1))->getZExtValue(), 4u);
  }
  EXPECT_EQ(SeenLoads, 1u);
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<LoadInst>(&I));
}

/// (Roadmap L47) A dynamic index that is *not* the final GEP index (e.g.
/// a per-invocation-indexed struct member selector before a further
/// constant component index) is not this shape --
/// `getTaskPayloadDynamicOffsetAccess` only recognizes the array-final-
/// index case real CTS shaders take (matching
/// `getDynamicRowIndexedAccess`'s identical restriction for ordinary
/// stage-IO globals) -- so this store is correctly left unrewritten for
/// `feme::graphics::ValidateStagePass` to diagnose, the same as any other
/// genuinely unsupported shape.
TEST(CanonicalizeStageTest,
    LeavesNonFinalDynamicTaskPayloadIndexUnrewritten) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @payload = external addrspace(14) global { [4 x [2 x i32]], i32 }
    define void @main(i32 %v, i32 %idx) #0 {
      %addr = getelementptr { [4 x [2 x i32]], i32 }, ptr addrspace(14) @payload, i32 0, i32 0, i32 %idx, i32 1
      store i32 %v, ptr addrspace(14) %addr
      ret void
    }
    attributes #0 = { "feme.shader.stage"="amplification" }
  )");
  ASSERT_TRUE(M);
  // Nothing in the module is rewritten at all -- this GEP shape is not
  // recognized by any canonicalization path.
  EXPECT_FALSE(run(*M));
  Function *F = M->getFunction("main");

  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    EXPECT_FALSE(CI && isStageOpCall(*CI, &Kind) &&
                (Kind == StageOpKind::TaskPayloadStore ||
                 Kind == StageOpKind::TaskPayloadLoad));
  }
  bool SawStore = false;
  for (Instruction &I : instructions(F))
    if (isa<StoreInst>(&I))
      SawStore = true;
  EXPECT_TRUE(SawStore);
}

/// (Roadmap H101k) A single-real-member array-of-block-instances global
/// (`layout(..., xfb_offset = 44) out BlockC { mat3x4 d; } blockC[2];`,
/// this test's own `@spirv_var_4`) whose one member's own declared offset
/// (44) is non-zero gets an LLVM-level leading `[44 x i8]` pad field
/// synthesized ahead of it (`layOutStructIfOffsetsMatch`,
/// SPIRVToLLVMPatterns.cpp) purely to reproduce that offset -- growing
/// its per-instance struct's own LLVM field count to 2 (pad + the real,
/// tight-vector-substituted matrix member) despite still declaring only
/// one real GLSL member. Before this fix, `addElements`' own
/// `TakeBlockPath` decision (`PeekedST->getNumElements() > 1`) mistook
/// that padded LLVM field count for "2 real SPIR-V members", taking the
/// wrong (multi-member block) path and misaligning `MemberDecorations`
/// (keyed by real SPIR-V member index) against the padded LLVM struct's
/// own field indices -- and even once excluded from that path,
/// `resolveOffsetWithinElement`'s own recursion had no way to peel a
/// *2*-member struct the way `peelSingleMemberStruct` peels a genuine
/// 1-member one -- so every store to `@spirv_var_4` was left entirely
/// unrewritten, keeping the (never-defined) global itself live all the
/// way to JIT-link time (the `Symbols not found: [ spirv_var_46 ]` crash
/// `dEQP-VK.transform_feedback.fuzz.random_geometry.all_instance_
/// array.11` exposed). A sibling genuinely-multi-member block
/// (`@spirv_var_3`, `BlockB { ivec3 a; vec4 b; uvec2 c[2]; }`, unaffected
/// by this fix -- its own `PeekedMemberDecorations.size() == 3` already
/// correctly took `TakeBlockPath` before and after) is exercised
/// alongside it, confirming this fix does not regress the genuinely
/// multi-member case it must still distinguish this one from.
TEST(CanonicalizeStageTest,
    RewritesArrayOfBlockInstancesWithLeadingPadBeforeTightMatrixMember) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    %feme.tight_vector = type { [3 x i32] }
    %feme.tight_vector.1 = type { [4 x float] }
    %feme.tight_vector.2 = type { [2 x i32] }
    %feme.tight_vector.3 = type { [4 x float] }

    @spirv_var_3 = external addrspace(8) global { %feme.tight_vector, %feme.tight_vector.1, [2 x %feme.tight_vector.2] }, !spirv.Decorations !4, !feme.spirv.MemberDecorations !16
    @spirv_var_4 = external addrspace(8) global [2 x { [44 x i8], [3 x %feme.tight_vector.3] }], !spirv.Decorations !6, !feme.spirv.MemberDecorations !20

    declare void @feme.stage.stream.cut(i32)
    declare void @feme.stage.stream.emit(i32)

    define void @main() #0 {
      store <3 x i32> <i32 -89, i32 30, i32 -70>, ptr addrspace(8) @spirv_var_3, align 4
      store <4 x float> <float -6.000000e+01, float 7.200000e+01, float -1.020000e+02, float -1.000000e+02>, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @spirv_var_3, i64 12), align 4
      store <2 x i32> <i32 84, i32 31>, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @spirv_var_3, i64 28), align 4
      store <2 x i32> <i32 68, i32 104>, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @spirv_var_3, i64 36), align 4
      store [3 x <4 x float>] [<4 x float> <float 1.400000e+01, float 8.300000e+01, float -1.170000e+02, float 7.500000e+01>, <4 x float> <float 1.200000e+01, float -1.800000e+01, float -1.010000e+02, float 1.000000e+02>, <4 x float> <float -3.400000e+01, float -3.500000e+01, float -4.300000e+01, float 4.800000e+01>], ptr addrspace(8) @spirv_var_4, align 4
      store [3 x <4 x float>] [<4 x float> <float 4.000000e+00, float -6.000000e+01, float 3.700000e+01, float 5.200000e+01>, <4 x float> <float 9.800000e+01, float -8.000000e+01, float -6.200000e+01, float -5.400000e+01>, <4 x float> <float -7.900000e+01, float 3.000000e+00, float -9.700000e+01, float -1.180000e+02>], ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @spirv_var_4, i64 92), align 4
      call void @feme.stage.stream.emit(i32 0)
      call void @feme.stage.stream.cut(i32 0)
      ret void
    }

    attributes #0 = { "feme.geometry.input_primitive"="points" "feme.geometry.invocations"="1" "feme.geometry.max_output_vertices"="1" "feme.geometry.output_primitive"="points" "feme.shader.stage"="geometry" "hlsl.shader"="geometry" }

    !1 = !{i32 30, i32 0}
    !2 = !{i32 36, i32 0}
    !3 = !{i32 37, i32 92}
    !4 = !{!1, !2, !3}
    !5 = !{i32 30, i32 4}
    !6 = !{!5, !2, !3}
    !7 = !{i32 35, i32 0}
    !8 = !{!7}
    !9 = !{i32 0, !8}
    !10 = !{i32 35, i32 12}
    !11 = !{!10}
    !12 = !{i32 1, !11}
    !13 = !{i32 35, i32 28}
    !14 = !{!13}
    !15 = !{i32 2, !14}
    !16 = !{!9, !12, !15}
    !17 = !{i32 35, i32 44}
    !18 = !{!17}
    !19 = !{i32 0, !18}
    !20 = !{!19}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");

  // No raw load/store survives against either global: every one of
  // `@spirv_var_3`'s (the genuinely-multi-member block) and
  // `@spirv_var_4`'s (this fix's own single-real-member, leading-pad
  // block) own stores must be rewritten into a `feme.stage.output.store`
  // call, or the global itself remains live and unresolved at JIT-link
  // time.
  for (Instruction &I : instructions(F))
    EXPECT_FALSE(isa<StoreInst>(&I) || isa<LoadInst>(&I));

  // `@spirv_var_4`'s matrix member gets its own distinct `ElementID`
  // (never colliding with `@spirv_var_3`'s three), and every one of its
  // two instances' three rows (columns 0-2 of each `mat3x4`) is captured
  // -- confirming the leading pad neither swallowed a row nor duplicated
  // one from the sibling block. Grouped per-`ElementID` (rather than
  // assumed to be the first float store seen) since `@spirv_var_3`'s own
  // `vec4` member is float-typed too -- only `@spirv_var_4`'s matrix
  // member spans more than one row.
  DenseMap<uint32_t, DenseSet<uint32_t>> RowsByElementID;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    StageOpKind Kind;
    if (!CI || !isStageOpCall(*CI, &Kind) ||
        Kind != StageOpKind::OutputStore)
      continue;
    if (auto *FTy = CI->getArgOperand(3)->getType(); !FTy->isFloatTy())
      continue;
    uint32_t ElementID = getStageOpConstantOperand(*CI, /*Offset=*/0)
                             .value_or(~0u);
    uint32_t Row =
        getStageOpConstantOperand(*CI, /*Offset=*/1).value_or(~0u);
    RowsByElementID[ElementID].insert(Row);
  }
  // Two instances * 3 rows (matrix columns) each = 6 distinct rows for
  // `@spirv_var_4`'s own `ElementID`; `@spirv_var_3`'s `vec4` member
  // (also float-typed) only ever sees row 0.
  uint32_t MaxRowCount = 0;
  for (const auto &KV : RowsByElementID)
    MaxRowCount = std::max(MaxRowCount, (uint32_t)KV.second.size());
  EXPECT_EQ(MaxRowCount, 6u);
}

/// (Roadmap H101l) A single-real-member array-of-block-instances global
/// (`addElements`' plain, non-`TakeBlockPath` path) whose block declares
/// a non-zero `xfb_offset` that glslang encodes *only* as the one real
/// member's own `Offset` decoration (SPIR-V decoration code 35, reused
/// for both a struct member's byte offset and a whole-variable's
/// `XfbOffset`) -- never repeating it at the whole-variable
/// `spirv.Decorations` level the way it always does for `XfbBuffer`/
/// `XfbStride` -- previously left `ParsedSPIRVDecorations::XfbOffset`
/// silently at `std::nullopt` (folded to 0 by `SignatureElement::XfbOffset
/// = D.XfbOffset.value_or(0)`), capturing this element at the wrong
/// (zero) byte offset within its shared `XfbBuffer` and colliding with a
/// sibling element genuinely captured at offset 0
/// (`random_geometry.all_instance_array.11`'s own `Mismatch at offset 0
/// expected -89 received 1096810496`, the received bit pattern being the
/// colliding sibling's own first captured float, `14.0f`). Fixed by
/// folding `PeekedMemberDecorations.lookup(0).XfbOffset` into `D` whenever
/// the whole-variable decoration itself lacks one, mirroring
/// `TakeBlockPath`'s own analogous per-member `XfbOffset` synthesis for
/// the genuinely-multi-member case.
TEST(CanonicalizeStageTest,
    FoldsMemberOffsetIntoXfbOffsetForArrayOfBlockInstances) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @spirv_var = external addrspace(8) global [2 x { <4 x float> }], !spirv.Decorations !0, !feme.spirv.MemberDecorations !4

    define void @main() #0 {
      store <4 x float> <float 1.400000e+01, float 8.300000e+01, float -1.170000e+02, float 7.500000e+01>, ptr addrspace(8) @spirv_var, align 4
      store <4 x float> <float 4.000000e+00, float -6.000000e+01, float 3.700000e+01, float 5.200000e+01>, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @spirv_var, i64 16), align 4
      ret void
    }

    attributes #0 = { "feme.shader.stage"="vertex" }

    !0 = !{!1, !2}
    !1 = !{i32 36, i32 0}
    !2 = !{i32 37, i32 32}
    !3 = !{i32 35, i32 44}
    !4 = !{!5}
    !5 = !{i32 0, !6}
    !6 = !{!3}
  )");
  ASSERT_TRUE(M);
  EXPECT_TRUE(run(*M));
  Function *F = M->getFunction("main");
  std::optional<EntrySignature> Sig = dxil::getEntrySignature(*F);
  ASSERT_TRUE(Sig.has_value());
  ASSERT_EQ(Sig->Elements.size(), 1u);

  const SignatureElement &Elt = Sig->Elements[0];
  ASSERT_TRUE(Elt.XfbBuffer.has_value());
  EXPECT_EQ(*Elt.XfbBuffer, 0u);
  // The whole-variable `spirv.Decorations` (!0) never carries an `Offset`
  // (code 35) entry -- only the member's own `MemberDecorations` (!3)
  // does -- so a correct fold-in is the only way this is 44, not 0.
  EXPECT_EQ(Elt.XfbOffset, 44u);
  EXPECT_EQ(Elt.XfbStride, 32u);
}

} // namespace
