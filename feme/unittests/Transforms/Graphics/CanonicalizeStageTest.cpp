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
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
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

  // `PointSize`/`ClipDistance`/`CullDistance` have no ABI-field consumer
  // anywhere downstream (roadmap H7's still-`VK_FALSE`
  // `shaderClipDistance`/`shaderCullDistance`), so they map to `None`, the
  // same "unmodeled system value" treatment an unrecognized DXIL semantic
  // already gets.
  for (unsigned I = 2; I != 5; ++I)
    EXPECT_EQ(Sig->Elements[I].SystemValue, SignatureSystemValue::None);

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
TEST(CanonicalizeStageTest, ThreadsDynamicVertexIndexIntoInputLoad) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @in_texcoord = external addrspace(7) constant [3 x <4 x float>], !spirv.Decorations !0
    define <4 x float> @main(i32 %i) #0 {
      %p = getelementptr inbounds [3 x <4 x float>], ptr addrspace(7) @in_texcoord, i32 0, i32 %i
      %v = load <4 x float>, ptr addrspace(7) %p
      ret <4 x float> %v
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
TEST(CanonicalizeStageTest, FoldsConstantVertexIndexIntoVertexOperand) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    @in_texcoord = external addrspace(7) constant [3 x <4 x float>], !spirv.Decorations !0
    define <4 x float> @main() #0 {
      %p = getelementptr inbounds [3 x <4 x float>], ptr addrspace(7) @in_texcoord, i32 0, i32 1
      %v = load <4 x float>, ptr addrspace(7) %p
      ret <4 x float> %v
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
/// dynamic case does, not into an ordinary `Row`.
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
/// output's own per-row store -- `RewritesSPIRVArrayOutputStorePerElementByteOffset`
/// above), so `RowCountIsVertexArray` stays `false` here even though this
/// global is structurally identical to `ThreadsDynamicVertexIndexIntoInputLoad`'s
/// own `Input` one; only the *access itself* routes through `Vertex`, not
/// (yet) the signature's own description of the element. See "Roadmap H6:
/// what H6b found, and why it stops here" in VulkanCTSReport.md.
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

} // namespace
