//===- SPIRVToLLVMTest.cpp - Tests for FeMe's SPIR-V -> LLVM conversion --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Conversion/SPIRVToLLVM/SPIRVToLLVM.h"

#include "feme/Core/ShaderStage.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

/// Parses \p Source, which must be a single top-level `spirv.module`, and
/// returns the target triple FeMe's conversion would record for it.
std::string getTargetTripleFor(llvm::StringRef Source) {
  mlir::MLIRContext Ctx;
  Ctx.loadDialect<mlir::spirv::SPIRVDialect>();
  mlir::OwningOpRef<mlir::spirv::ModuleOp> Module =
      mlir::parseSourceString<mlir::spirv::ModuleOp>(Source, &Ctx);
  if (!Module)
    return "<parse failed>";
  return spirv::getTargetTriple(*Module);
}

/// Returns a `spirv.module` whose single entry point uses \p ExecutionModel.
std::string makeShaderModule(llvm::StringRef ExecutionModel) {
  return ("spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], "
          "[]> { spirv.func @entry() -> () \"None\" { spirv.Return } "
          "spirv.EntryPoint \"" +
          ExecutionModel + "\" @entry }")
      .str();
}

TEST(SPIRVToLLVMTest, TargetTripleNamesTheShaderStage) {
  EXPECT_EQ(getTargetTripleFor(makeShaderModule("GLCompute")),
            "spirv-unknown-vulkan-compute");
  EXPECT_EQ(getTargetTripleFor(makeShaderModule("Vertex")),
            "spirv-unknown-vulkan-vertex");
  EXPECT_EQ(getTargetTripleFor(makeShaderModule("Fragment")),
            "spirv-unknown-vulkan-pixel");
  EXPECT_EQ(getTargetTripleFor(makeShaderModule("Geometry")),
            "spirv-unknown-vulkan-geometry");
  EXPECT_EQ(getTargetTripleFor(makeShaderModule("TessellationControl")),
            "spirv-unknown-vulkan-hull");
  EXPECT_EQ(getTargetTripleFor(makeShaderModule("TessellationEvaluation")),
            "spirv-unknown-vulkan-domain");
}

// The `feme.shader.stage` attribute the conversion records on each entry
// point is a projection of the same information the triple's environment
// carries, so every stage a `spirv.module` can name has to survive both
// spellings and map back to one enumerator ("Stage identity" in
// feme/docs/FeMeGraphicsDesign.md).
TEST(SPIRVToLLVMTest, EveryStageTripleNamesAShaderStage) {
  for (llvm::StringRef Model :
       {"GLCompute", "Vertex", "Fragment", "Geometry", "TessellationControl",
        "TessellationEvaluation", "TaskEXT", "MeshEXT", "RayGenerationKHR",
        "IntersectionKHR", "AnyHitKHR", "ClosestHitKHR", "MissKHR",
        "CallableKHR"}) {
    llvm::Triple Triple(getTargetTripleFor(makeShaderModule(Model)));
    std::optional<ShaderStage> Stage =
        getShaderStageForEnvironment(Triple.getEnvironment());
    ASSERT_TRUE(Stage.has_value()) << Model.str();
    EXPECT_EQ(getEnvironmentForShaderStage(*Stage), Triple.getEnvironment())
        << Model.str();
  }
}

TEST(SPIRVToLLVMTest, TargetTripleWithoutEntryPointNamesNoStage) {
  EXPECT_EQ(getTargetTripleFor(
                "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, "
                "[Shader], []> { }"),
            "spirv-unknown-vulkan");
}

// An OpenCL kernel is not a graphics pipeline stage, so it gets the physical
// triple LLVM's SPIRV backend keys its `Kernel` environment off instead, with
// the bitness taken from the module's addressing model.
TEST(SPIRVToLLVMTest, TargetTripleForKernelsIsPhysical) {
  EXPECT_EQ(getTargetTripleFor(
                "spirv.module Physical32 OpenCL requires #spirv.vce<v1.0, "
                "[Kernel, Addresses], []> { spirv.func @entry() -> () \"None\" "
                "{ spirv.Return } spirv.EntryPoint \"Kernel\" @entry }"),
            "spirv32-unknown-unknown");
  EXPECT_EQ(getTargetTripleFor(
                "spirv.module Physical64 OpenCL requires #spirv.vce<v1.0, "
                "[Kernel, Addresses], []> { spirv.func @entry() -> () \"None\" "
                "{ spirv.Return } spirv.EntryPoint \"Kernel\" @entry }"),
            "spirv64-unknown-unknown");
}

/// Parses \p Source (a single top-level `spirv.module`), runs
/// feme::spirv::createConvertSPIRVToLLVMPass on it, and returns the
/// resulting `llvm` dialect module printed to a string, or "<failed>" if
/// either step fails.
std::string convertToLLVMDialect(llvm::StringRef Source) {
  mlir::MLIRContext Ctx;
  Ctx.loadDialect<mlir::spirv::SPIRVDialect, mlir::LLVM::LLVMDialect>();
  mlir::OwningOpRef<mlir::spirv::ModuleOp> SPIRVModule =
      mlir::parseSourceString<mlir::spirv::ModuleOp>(Source, &Ctx);
  if (!SPIRVModule)
    return "<failed>";

  mlir::OwningOpRef<mlir::ModuleOp> Outer =
      mlir::ModuleOp::create(mlir::UnknownLoc::get(&Ctx));
  Outer->push_back(SPIRVModule.release());

  mlir::PassManager PM(&Ctx);
  PM.addPass(feme::spirv::createConvertSPIRVToLLVMPass());
  if (mlir::failed(PM.run(*Outer)))
    return "<failed>";

  std::string Result;
  llvm::raw_string_ostream OS(Result);
  Outer->print(OS);
  return Result;
}

// A non-builtin `Input`/`Output` variable (an ordinary stage-IO variable)
// converts to a real `llvm.mlir.global` in the address space LLVM's SPIRV
// backend expects that storage class to use, instead of failing to legalize
// (roadmap R19) -- see test/Conversion/SPIRVToLLVM/spirv-to-llvm-stage-io.mlir
// for the FileCheck-based version of this same check.
TEST(SPIRVToLLVMTest, NonBuiltinInputOutputConvertsInsteadOfFailing) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @in_var {location = 0 : i32} : "
      "!spirv.ptr<i32, Input> spirv.GlobalVariable @out_var {location = 1 : "
      "i32} : !spirv.ptr<f32, Output> }");
  EXPECT_NE(Result, "<failed>");
  EXPECT_NE(Result.find("addr_space = 7"), std::string::npos) << Result;
  EXPECT_NE(Result.find("addr_space = 8"), std::string::npos) << Result;
  EXPECT_NE(Result.find(feme::spirv::getStageIODecorationsAttrName().str()),
            std::string::npos)
      << Result;
}

// (Roadmap L101) `spirv.VectorExtractDynamic` (indexing a vector value
// with a runtime, non-constant index -- e.g. GLSL `v[i]` where `i` is a
// loop variable, the shape `dEQP-VK.pipeline.pipeline_library.
// spec_constant.*.composite.vector.*` compiles down to) converts directly
// to `llvm.extractelement` instead of failing to legalize (no conversion
// pattern for this op existed at all before this fix, upstream or in this
// file).
TEST(SPIRVToLLVMTest, VectorExtractDynamicConvertsInsteadOfFailing) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.func @entry() -> () \"None\" { %0 = spirv.Constant "
      "dense<[1, 2, 3]> : vector<3xi32> %1 = spirv.Constant 1 : i32 %2 = "
      "spirv.VectorExtractDynamic %0[%1] : vector<3xi32>, i32 spirv.Return "
      "} spirv.EntryPoint \"GLCompute\" @entry }");
  EXPECT_NE(Result, "<failed>");
  EXPECT_NE(Result.find("llvm.extractelement"), std::string::npos) << Result;
}

// (Roadmap H111) A function-local `spirv.Variable` initialized with an
// array (as opposed to a scalar or vector) constant -- e.g. GLSL's `const
// vec4 positions[4] = vec4[](...)`, the shape
// `dEQP-VK.mesh_shader.ext.smoke.*.fullscreen_gradient`'s own mesh shader
// declares -- converts to a real `llvm.alloca` + `llvm.store` instead of
// failing to legalize, the same way upstream MLIR's own `VariablePattern`
// already handles a scalar/vector initializer.
TEST(SPIRVToLLVMTest, LocalArrayInitializedVariableConvertsInsteadOfFailing) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.func @entry() -> () \"None\" { %0 = spirv.Constant "
      "[1 : i32, 2 : i32] : !spirv.array<2 x i32> %1 = spirv.Variable "
      "init(%0) : "
      "!spirv.ptr<!spirv.array<2 x i32>, Function> spirv.Return } "
      "spirv.EntryPoint \"GLCompute\" @entry }");
  EXPECT_NE(Result, "<failed>");
  EXPECT_NE(Result.find("llvm.alloca"), std::string::npos) << Result;
  EXPECT_NE(Result.find("llvm.store"), std::string::npos) << Result;
}

// (Roadmap H2c) A builtin interface block (a struct-typed `Output`
// variable with no whole-variable `BuiltIn` attribute of its own, e.g.
// glslang's implicit `gl_PerVertex`) still converts through the ordinary
// stage-IO path -- its per-member `BuiltIn` decorations are preserved as a
// distinct `feme.spirv.member.decorations` attribute rather than being
// silently dropped, since buildStageIODecorationsAttr's whole-variable read
// never sees them (see spirv-to-llvm-stage-io.mlir's own test for the exact
// attribute shape).
TEST(SPIRVToLLVMTest, BuiltinInterfaceBlockPreservesMemberDecorations) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @gl_PerVertex : "
      "!spirv.ptr<!spirv.struct<(vector<4xf32> [BuiltIn=0 : i32], f32 "
      "[BuiltIn=1 : i32])>, Output> }");
  EXPECT_NE(Result, "<failed>");
  EXPECT_NE(
      Result.find(feme::spirv::getStageIOMemberDecorationsAttrName().str()),
      std::string::npos)
      << Result;
  // No whole-variable decoration to preserve: `gl_PerVertex` itself carries
  // no `built_in`/`location` attribute, only its members do.
  EXPECT_EQ(Result.find(feme::spirv::getStageIODecorationsAttrName().str()),
            std::string::npos)
      << Result;
}

// (Roadmap H5g) A geometry entry's own per-vertex builtin interface block
// (`gl_in[]`) is shaped one array dimension further out than
// `gl_PerVertex` above -- an `Input` array-of-struct, since SPIR-V still
// wraps `Triangles`/etc.'s "one block per vertex" shape in an outer
// `OpTypeArray` -- but the member decorations that matter live on the
// inner struct exactly as they do for the bare-block case, and
// `CanonicalizeStage.cpp`'s own `addElements` (roadmap H5b) already knows
// how to peel that outer array dimension back off once this metadata is
// present to peel in front of.
TEST(SPIRVToLLVMTest, PerVertexArrayInterfaceBlockPreservesMemberDecorations) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, "
      "Geometry], []> "
      "{ spirv.GlobalVariable @gl_in : "
      "!spirv.ptr<!spirv.array<3 x !spirv.struct<(vector<4xf32> "
      "[BuiltIn=0 : i32], f32 [BuiltIn=1 : i32])>>, Input> }");
  EXPECT_NE(Result, "<failed>");
  EXPECT_NE(
      Result.find(feme::spirv::getStageIOMemberDecorationsAttrName().str()),
      std::string::npos)
      << Result;
  // No whole-variable decoration to preserve, same as the bare-block case:
  // `gl_in` itself carries no `built_in`/`location` attribute, only the
  // inner per-vertex block's own members do.
  EXPECT_EQ(Result.find(feme::spirv::getStageIODecorationsAttrName().str()),
            std::string::npos)
      << Result;
}

// A member decoration this milestone does not model (e.g.
// `RelaxedPrecision`, which an ordinary block's members can carry but no
// downstream consumer of `feme.spirv.member.decorations` ever reads) is
// filtered out of the attribute's per-member tuple list rather than
// corrupting the encoding, matching buildStageIODecorationsAttr's own
// "unrecognized decoration is simply not preserved" behavior for a
// whole-variable attribute. This member also carries no explicit `Offset`
// (unlike `PlainMultiMemberInterfaceBlockSynthesizesOffsetDecoration`
// below), so `buildMemberDecorationsAttr` has nothing to synthesize either
// -- the whole `feme.spirv.member.decorations` attribute is absent, not
// merely empty for this member.
TEST(SPIRVToLLVMTest, UnrecognizedMemberDecorationIsFilteredOut) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @block : "
      "!spirv.ptr<!spirv.struct<(f32 [RelaxedPrecision])>, Output> }");
  EXPECT_NE(Result, "<failed>");
  EXPECT_EQ(
      Result.find(feme::spirv::getStageIOMemberDecorationsAttrName().str()),
      std::string::npos)
      << Result;
}

// (Roadmap H101b) A plain (non-builtin), multi-member interface block's
// members typically carry no recognized decoration of their own beyond an
// explicit byte `Offset` (e.g. `layout(xfb_offset = 0) mediump uvec4 a;`) --
// unlike `Offset`.  MLIR's own `spirv::StructType` never surfaces `Offset`
// through the generic `getMemberDecorations` list `buildMemberDecorationTuple`
// reads (it's tracked as a distinct first-class `OffsetInfo` field instead),
// so without an explicit synthesis step `buildMemberDecorationsAttr` would
// return a null attribute for such a block -- exactly as it correctly does
// for `UnrecognizedMemberDecorationIsFilteredOut` above, only wrongly so, for
// a struct that genuinely does need to disambiguate more than one member.
// `buildMemberDecorationsAttr` unconditionally synthesizes a `(35 /*Offset*/,
// memberOffset)` tuple per member whenever the struct itself
// `hasOffset()`, ensuring the resulting attribute is never empty for a
// genuinely multi-member, explicitly-offset-laid-out struct like this one.
TEST(SPIRVToLLVMTest,
     PlainMultiMemberInterfaceBlockSynthesizesOffsetDecoration) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @block : "
      "!spirv.ptr<!spirv.struct<(f32 [0], f32 [4])>, Output> }");
  EXPECT_NE(Result, "<failed>");
  EXPECT_NE(
      Result.find(feme::spirv::getStageIOMemberDecorationsAttrName().str()),
      std::string::npos)
      << Result;
}

// (Roadmap H114) A genuinely multi-member struct used directly as a
// `patch`/per-vertex stage-IO variable's own type -- e.g. a tessellation
// entry's `patch out S { int x; vec4 y; } s;` -- can carry *neither* an
// explicit member `Offset` (that's only ever emitted for a `Block`-
// decorated interface block, unlike this plain, non-`Block` struct) *nor*
// any per-member `Location` (SPIR-V leaves every member's own location to
// be derived sequentially from the whole variable's single `Location`
// instead): every member decoration this test's struct type could
// contribute is empty. Before this fix, `buildMemberDecorationsAttr`
// treated that exactly like `UnrecognizedMemberDecorationIsFilteredOut`
// above's genuinely single-member case, returning a null attribute, so no
// `feme.spirv.member.decorations` metadata was ever attached and
// `CanonicalizeStage.cpp`'s `addElements` fell through to its plain,
// single-`SignatureElement` path -- silently merging this struct's two
// differently-typed members (`i32`/`vec4`) into one shadow-alloca slot,
// tripping `PromoteMem2Reg`'s `isAllocaPromotable` assertion downstream --
// found via `dEQP-VK.tessellation.user_defined_io.per_patch.
// vertex_io_array_size_implicit.isolines`. A genuinely multi-member
// struct's own member count alone (regardless of whether any member has a
// decoration of its own to report) is now enough to synthesize a
// (possibly decoration-less) entry per member, so the block-decomposition
// path downstream can still tell this struct's two members apart.
TEST(SPIRVToLLVMTest,
     PlainMultiMemberInterfaceBlockWithNoMemberDecorationsStillDecomposes) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @block : "
      "!spirv.ptr<!spirv.struct<(i32, vector<4xf32>)>, Output> }");
  EXPECT_NE(Result, "<failed>");
  EXPECT_NE(
      Result.find(feme::spirv::getStageIOMemberDecorationsAttrName().str()),
      std::string::npos)
      << Result;
}

// (Roadmap H101p) A multi-member interface block whose members are
// *declared* out of ascending-`Offset` order (GLSL's own
// `all_unordered_and_instance_array` fuzz-test family deliberately emits
// exactly this shape, e.g. a `mat4x2` member declared first but placed at
// the higher byte offset, with a `vector<3xsi32>` member declared second
// but placed at byte 0) used to fail `spirv.GlobalVariable` legalization
// outright: layOutStructIfOffsetsMatch's natural-ABI-layout cursor walk
// only ever increases, so a declared offset smaller than an
// already-consumed cursor position could never re-match. Both
// layOutStructIfOffsetsMatch (struct-type legalization) and
// OffsetStructMemberReorderAccessChainPattern (its own member-selecting
// access-chain rewrite) now consult getOffsetSortedMemberIndices to lay
// out, and address, the struct's members in ascending-offset (physical)
// order regardless of declaration order, so this now legalizes cleanly
// instead of failing.
TEST(SPIRVToLLVMTest, OutOfOrderOffsetInterfaceBlockLegalizes) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @block : "
      "!spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<2xf32>> [12, "
      "ColMajor, MatrixStride=8 : i32], vector<3xsi32> [0]), Block>, "
      "Output> }");
  EXPECT_NE(Result, "<failed>");
  // The physically-first (lowest-offset) member is the `vector<3xsi32>`
  // declared second, so it must be laid out as the LLVM struct's first
  // field, ahead of the `mat4x2` declared first.
  EXPECT_NE(Result.find("!llvm.struct<packed (struct<\"feme.tight_vector\""),
            std::string::npos)
      << Result;
}

// (Roadmap H101s) A multi-member interface block whose members include a
// *nested*, single-member struct wrapping a plain vector (e.g.
// `!spirv.struct<(vector<4xf32> [RelaxedPrecision])>` used as one member
// of an outer block -- `all_unordered_and_instance_array`'s own fuzz-test
// family emits exactly this shape) used to fail `spirv.GlobalVariable`
// legalization outright, distinct from H101p's reordering-only bug: the
// nested struct converts fine on its own (its one member's offset is
// always 0 relative to its own start, trivially matching LLVM's natural,
// real-vector layout), but its own natural *size*/*alignment* is still
// driven by that one vector's ABI-rounded footprint (e.g. a 3-lane vector
// rounds up to 16 bytes), which the *outer* struct's declared, tightly
// packed offset for this member (or the gap to its next sibling) does not
// reserve room for. getTightNestedStructType now lets the existing
// tight-vector retry recognize and substitute this shape too, rebuilding
// the nested struct's own body with its vector member tightened while
// keeping its own member count (here, one) unchanged, so an access
// chain's existing "member I, then member 0" index pair still resolves
// correctly.
TEST(SPIRVToLLVMTest, NestedSingleMemberVectorStructInterfaceBlockLegalizes) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @block : "
      "!spirv.ptr<!spirv.struct<(f32 [20], vector<2xsi32> [0], "
      "!spirv.struct<(vector<3xf32> [0])> [8]), Block>, Output> }");
  EXPECT_NE(Result, "<failed>");
  // The nested struct's own tight-substituted marker takes the place of
  // the plain `struct<(vector<3xf32>)>` its own recursive conversion
  // would otherwise have produced.
  EXPECT_NE(Result.find("struct<\"feme.tight_vector"), std::string::npos)
      << Result;
  EXPECT_EQ(Result.find("struct<(vector<3xf32>)>"), std::string::npos)
      << Result;
}

// (Roadmap H101s) A nested struct member need not be single-member: a
// real `all_unordered_and_instance_array` case's own nested struct has
// *two* members (a `mat3x3` and a `vector<4xsi32>`) and, unlike the
// single-member case above, declares no per-member `Offset` decoration of
// its own at all (a nested, non-`Block` struct never needs one) -- so its
// own `layOutStructIfOffsetsMatch` call takes the `!Type.hasOffset()`
// early-out and accepts its natural, untightened layout unconditionally,
// with no chance to retry on its own terms; the mismatch, exactly as in
// the single-member case, only surfaces once the *outer* struct tries to
// place this whole nested struct at a tightly packed offset its natural
// (ABI-rounded) alignment cannot reach. getTightNestedStructType rebuilds
// every member of a nested struct like this (not just a lone one),
// preserving member count/order so each of its own members remains
// addressable by the same index it always was.
TEST(SPIRVToLLVMTest, NestedMultiMemberStructInterfaceBlockLegalizes) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @block : !spirv.ptr<!spirv.struct<("
      "!spirv.struct<(!spirv.matrix<3 x vector<3xf32>> [RelaxedPrecision], "
      "vector<4xsi32>)> [36], vector<3xf32> [24, RelaxedPrecision]), "
      "Block>, Output> }");
  EXPECT_NE(Result, "<failed>");
  // Both of the nested struct's own members (the matrix, then the
  // trailing vector) must survive the substitution, each tightened.
  EXPECT_NE(Result.find("struct<\"feme.tight_vector"), std::string::npos)
      << Result;
  EXPECT_EQ(Result.find("vector<3xf32>>, vector<4xi32>)>"), std::string::npos)
      << Result;
}

// (Roadmap H129) A multi-member interface block whose members are
// declared out of physical (ascending-offset) order AND whose
// physically-first member's own natural size undershoots the byte gap to
// its physically-second member -- an *interior* gap, as opposed to a
// *leading* one before the first physical member -- used to fail
// `spirv.GlobalVariable` legalization outright:
// layOutStructIfOffsetsMatch only ever synthesized a pad *before* the
// first physically-ordered member (structHasLeadingOffsetPad's shape),
// never one *between* two already-physically-adjacent members. Real
// `dEQP-VK.ubo.random.all_out_of_order_offsets.*` fuzz cases routinely
// declare a struct shaped exactly like this one. This is distinct from
// OutOfOrderOffsetInterfaceBlockLegalizes above (H101p), which covers
// reordering alone with no gap to fill. layOutStructIfOffsetsMatch now
// accepts an `AllowInteriorPad` retry tier (tried only after every other
// existing retry has already failed) that synthesizes the missing
// interior pad, and OffsetStructMemberReorderAccessChainPattern's own
// member-selecting access-chain rewrite consults the resulting
// declared-to-physical index map (via getStructMemberPhysicalIndex)
// instead of the reordering-only Order/HasPad logic H101p introduced.
TEST(SPIRVToLLVMTest, InteriorOffsetGapInterfaceBlockLegalizes) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @block : "
      "!spirv.ptr<!spirv.struct<(f32 [16], i32 [0]), Block>, Output> }");
  EXPECT_NE(Result, "<failed>");
  // The physically-first (lowest-offset) member is the `i32` declared
  // second; its own 4-byte natural size leaves a 12-byte interior gap
  // before the `f32` declared first, which its own 4-byte alignment can
  // never reach unaided, so an explicit pad must be synthesized between
  // them.
  EXPECT_NE(Result.find("!llvm.struct<packed (i32, array<12 x i8>, f32)>"),
            std::string::npos)
      << Result;
}

/// Builds a one-`llvm.mlir.global` `mlir::ModuleOp` carrying
/// getStageIODecorationsAttrName() with \p Decorations (each inner
/// `ArrayRef<int32_t>` one `(decoration, arg...)` tuple), the shape
/// buildStageIODecorationsAttr (SPIRVToLLVMPatterns.cpp) produces.
mlir::OwningOpRef<mlir::ModuleOp>
buildDecoratedGlobal(mlir::MLIRContext &Ctx, llvm::StringRef Name,
                     llvm::ArrayRef<llvm::ArrayRef<int32_t>> Decorations) {
  Ctx.loadDialect<mlir::LLVM::LLVMDialect>();
  mlir::OpBuilder Builder(&Ctx);
  auto Module = mlir::ModuleOp::create(Builder, mlir::UnknownLoc::get(&Ctx));
  Builder.setInsertionPointToStart(Module.getBody());

  llvm::SmallVector<mlir::Attribute> Outer;
  for (llvm::ArrayRef<int32_t> Inner : Decorations) {
    llvm::SmallVector<mlir::Attribute> InnerAttrs;
    for (int32_t Value : Inner)
      InnerAttrs.push_back(Builder.getI32IntegerAttr(Value));
    Outer.push_back(Builder.getArrayAttr(InnerAttrs));
  }

  auto Global = mlir::LLVM::GlobalOp::create(
      Builder, mlir::UnknownLoc::get(&Ctx), Builder.getI32Type(),
      /*isConstant=*/true, mlir::LLVM::Linkage::External, Name,
      mlir::Attribute(), /*alignment=*/0, /*addrSpace=*/7);
  Global->setAttr(feme::spirv::getStageIODecorationsAttrName(),
                  Builder.getArrayAttr(Outer));
  return mlir::OwningOpRef<mlir::ModuleOp>(Module);
}

TEST(SPIRVToLLVMTest, CollectStageIODecorationsFindsDecoratedGlobals) {
  mlir::MLIRContext Ctx;
  mlir::OwningOpRef<mlir::ModuleOp> Module =
      buildDecoratedGlobal(Ctx, "in_var", {{30, 0}, {14}});

  feme::spirv::StageIODecorationsMap Decorations =
      feme::spirv::collectStageIODecorations(Module.get());
  ASSERT_TRUE(Decorations.count("in_var"));
  EXPECT_EQ(Decorations["in_var"].size(), 2u);
}

// A global with getStageIODecorationsAttrName() but no decorations at all
// (an empty `ArrayAttr`) collects as present but empty, rather than as
// absent -- collectStageIODecorations only checks the attribute exists.
TEST(SPIRVToLLVMTest, CollectStageIODecorationsHandlesNoDecorations) {
  mlir::MLIRContext Ctx;
  mlir::OwningOpRef<mlir::ModuleOp> Module =
      buildDecoratedGlobal(Ctx, "in_var", {});
  feme::spirv::StageIODecorationsMap Decorations =
      feme::spirv::collectStageIODecorations(Module.get());
  ASSERT_TRUE(Decorations.count("in_var"));
  EXPECT_EQ(Decorations["in_var"].size(), 0u);
}

// attachStageIODecorations turns the collected attribute into the same
// `!spirv.Decorations` metadata shape LLVM's SPIRV backend reads (see
// `llvm/lib/Target/SPIRV/SPIRVUtils.cpp`'s `buildOpSpirvDecorations`):
// `!spirv.Decorations = !{!N}`, `!N = !{i32 <decoration>, i32 <arg>...}`.
TEST(SPIRVToLLVMTest, AttachStageIODecorationsBuildsRealMetadata) {
  mlir::MLIRContext Ctx;
  mlir::OwningOpRef<mlir::ModuleOp> Module =
      buildDecoratedGlobal(Ctx, "in_var", {{30, 0}, {14}});
  feme::spirv::StageIODecorationsMap Decorations =
      feme::spirv::collectStageIODecorations(Module.get());

  llvm::LLVMContext LLVMCtx;
  llvm::Module LLVMModule("m", LLVMCtx);
  auto *GV = new llvm::GlobalVariable(
      LLVMModule, llvm::Type::getInt32Ty(LLVMCtx), /*isConstant=*/true,
      llvm::GlobalValue::ExternalLinkage, nullptr, "in_var", nullptr,
      llvm::GlobalValue::NotThreadLocal, /*AddressSpace=*/7);

  feme::spirv::attachStageIODecorations(Decorations, LLVMModule);

  llvm::MDNode *MD = GV->getMetadata("spirv.Decorations");
  ASSERT_NE(MD, nullptr);
  ASSERT_EQ(MD->getNumOperands(), 2u);
  auto *Location = llvm::cast<llvm::MDNode>(MD->getOperand(0));
  ASSERT_EQ(Location->getNumOperands(), 2u);
  EXPECT_EQ(llvm::cast<llvm::ConstantInt>(
                llvm::cast<llvm::ConstantAsMetadata>(Location->getOperand(0))
                    ->getValue())
                ->getSExtValue(),
            30);
  auto *Flat = llvm::cast<llvm::MDNode>(MD->getOperand(1));
  EXPECT_EQ(Flat->getNumOperands(), 1u);
}

TEST(SPIRVToLLVMTest, AttachStageIODecorationsIgnoresMissingGlobals) {
  mlir::MLIRContext Ctx;
  mlir::OwningOpRef<mlir::ModuleOp> Module =
      buildDecoratedGlobal(Ctx, "no_such_global", {{30, 0}});
  feme::spirv::StageIODecorationsMap Decorations =
      feme::spirv::collectStageIODecorations(Module.get());

  llvm::LLVMContext LLVMCtx;
  llvm::Module LLVMModule("m", LLVMCtx);
  // No global named "no_such_global" in this module (e.g. dead-code
  // eliminated during translation) -- must not crash.
  feme::spirv::attachStageIODecorations(Decorations, LLVMModule);
}

/// Builds a one-`llvm.mlir.global` `mlir::ModuleOp` carrying
/// getStageIOMemberDecorationsAttrName() with \p Members (each entry a
/// (memberIndex, tuples) pair, `tuples` in the same shape
/// buildDecoratedGlobal's own `Decorations` parameter uses), the shape
/// buildMemberDecorationsAttr (SPIRVToLLVMPatterns.cpp) produces.
mlir::OwningOpRef<mlir::ModuleOp> buildDecoratedMemberGlobal(
    mlir::MLIRContext &Ctx, llvm::StringRef Name,
    llvm::ArrayRef<std::pair<int32_t, llvm::ArrayRef<llvm::ArrayRef<int32_t>>>>
        Members) {
  Ctx.loadDialect<mlir::LLVM::LLVMDialect>();
  mlir::OpBuilder Builder(&Ctx);
  auto Module = mlir::ModuleOp::create(Builder, mlir::UnknownLoc::get(&Ctx));
  Builder.setInsertionPointToStart(Module.getBody());

  llvm::SmallVector<mlir::Attribute> Outer;
  for (const auto &Member : Members) {
    llvm::SmallVector<mlir::Attribute> Tuples;
    for (llvm::ArrayRef<int32_t> Inner : Member.second) {
      llvm::SmallVector<mlir::Attribute> InnerAttrs;
      for (int32_t Value : Inner)
        InnerAttrs.push_back(Builder.getI32IntegerAttr(Value));
      Tuples.push_back(Builder.getArrayAttr(InnerAttrs));
    }
    Outer.push_back(Builder.getArrayAttr(
        {Builder.getI32IntegerAttr(Member.first), Builder.getArrayAttr(Tuples)}));
  }

  auto Global = mlir::LLVM::GlobalOp::create(
      Builder, mlir::UnknownLoc::get(&Ctx), Builder.getI32Type(),
      /*isConstant=*/false, mlir::LLVM::Linkage::External, Name,
      mlir::Attribute(), /*alignment=*/0, /*addrSpace=*/8);
  Global->setAttr(feme::spirv::getStageIOMemberDecorationsAttrName(),
                  Builder.getArrayAttr(Outer));
  return mlir::OwningOpRef<mlir::ModuleOp>(Module);
}

TEST(SPIRVToLLVMTest, CollectStageIOMemberDecorationsFindsDecoratedGlobals) {
  mlir::MLIRContext Ctx;
  llvm::SmallVector<int32_t, 2> BuiltInTuple = {11, 0};
  mlir::OwningOpRef<mlir::ModuleOp> Module = buildDecoratedMemberGlobal(
      Ctx, "gl_PerVertex", {{0, {llvm::ArrayRef<int32_t>(BuiltInTuple)}}});

  feme::spirv::StageIOMemberDecorationsMap MemberDecorations =
      feme::spirv::collectStageIOMemberDecorations(Module.get());
  ASSERT_TRUE(MemberDecorations.count("gl_PerVertex"));
  EXPECT_EQ(MemberDecorations["gl_PerVertex"].size(), 1u);
}

// attachStageIOMemberDecorations turns the collected attribute into
// `feme.spirv.MemberDecorations` metadata: `!{!{i32 memberIndex,
// !{tuples...}}, ...}`.
TEST(SPIRVToLLVMTest, AttachStageIOMemberDecorationsBuildsMetadata) {
  mlir::MLIRContext Ctx;
  llvm::SmallVector<int32_t, 2> BuiltInTuple = {11, 0};
  mlir::OwningOpRef<mlir::ModuleOp> Module = buildDecoratedMemberGlobal(
      Ctx, "gl_PerVertex", {{0, {llvm::ArrayRef<int32_t>(BuiltInTuple)}}});
  feme::spirv::StageIOMemberDecorationsMap MemberDecorations =
      feme::spirv::collectStageIOMemberDecorations(Module.get());

  llvm::LLVMContext LLVMCtx;
  llvm::Module LLVMModule("m", LLVMCtx);
  auto *GV = new llvm::GlobalVariable(
      LLVMModule, llvm::Type::getInt32Ty(LLVMCtx), /*isConstant=*/false,
      llvm::GlobalValue::ExternalLinkage, nullptr, "gl_PerVertex", nullptr,
      llvm::GlobalValue::NotThreadLocal, /*AddressSpace=*/8);

  feme::spirv::attachStageIOMemberDecorations(MemberDecorations, LLVMModule);

  llvm::MDNode *MD = GV->getMetadata("feme.spirv.MemberDecorations");
  ASSERT_NE(MD, nullptr);
  ASSERT_EQ(MD->getNumOperands(), 1u);
  auto *MemberEntry = llvm::cast<llvm::MDNode>(MD->getOperand(0));
  ASSERT_EQ(MemberEntry->getNumOperands(), 2u);
  EXPECT_EQ(llvm::cast<llvm::ConstantInt>(
                llvm::cast<llvm::ConstantAsMetadata>(MemberEntry->getOperand(0))
                    ->getValue())
                ->getSExtValue(),
            0);
  auto *Decorations = llvm::cast<llvm::MDNode>(MemberEntry->getOperand(1));
  ASSERT_EQ(Decorations->getNumOperands(), 1u);
  auto *BuiltIn = llvm::cast<llvm::MDNode>(Decorations->getOperand(0));
  ASSERT_EQ(BuiltIn->getNumOperands(), 2u);
  EXPECT_EQ(llvm::cast<llvm::ConstantInt>(
                llvm::cast<llvm::ConstantAsMetadata>(BuiltIn->getOperand(0))
                    ->getValue())
                ->getSExtValue(),
            11);
}

TEST(SPIRVToLLVMTest, AttachStageIOMemberDecorationsIgnoresMissingGlobals) {
  mlir::MLIRContext Ctx;
  llvm::SmallVector<int32_t, 2> BuiltInTuple = {11, 0};
  mlir::OwningOpRef<mlir::ModuleOp> Module = buildDecoratedMemberGlobal(
      Ctx, "no_such_global", {{0, {llvm::ArrayRef<int32_t>(BuiltInTuple)}}});
  feme::spirv::StageIOMemberDecorationsMap MemberDecorations =
      feme::spirv::collectStageIOMemberDecorations(Module.get());

  llvm::LLVMContext LLVMCtx;
  llvm::Module LLVMModule("m", LLVMCtx);
  // No global named "no_such_global" in this module -- must not crash.
  feme::spirv::attachStageIOMemberDecorations(MemberDecorations, LLVMModule);
}

/// Builds a `spirv.module` with two `spirv.GlobalVariableOp`s sharing both
/// \p Name and \p Set / \p Binding -- the shape observed from DXC's own
/// bindless-heap (`ResourceDescriptorHeap`/`SamplerDescriptorHeap`) codegen,
/// where more than one `OpVariable` can carry an identical `OpName` and
/// descriptor-set/binding pair. MLIR's SPIR-V deserializer builds such ops
/// directly via `OpBuilder` without re-verifying symbol uniqueness
/// afterward, so this constructs the same (otherwise ill-formed) shape the
/// same way, bypassing the parser's own verifier.
mlir::OwningOpRef<mlir::spirv::ModuleOp>
buildDuplicateHeapGlobals(mlir::MLIRContext &Ctx, llvm::StringRef Name,
                          uint32_t Set, uint32_t Binding) {
  Ctx.loadDialect<mlir::spirv::SPIRVDialect>();
  Ctx.loadDialect<mlir::LLVM::LLVMDialect>();
  mlir::OpBuilder Builder(&Ctx);
  auto Loc = mlir::UnknownLoc::get(&Ctx);
  auto Module = mlir::spirv::ModuleOp::create(
      Builder, Loc, mlir::spirv::AddressingModel::Logical,
      mlir::spirv::MemoryModel::GLSL450);
  Builder.setInsertionPointToStart(Module.getBody());

  auto SamplerTy = mlir::spirv::SamplerType::get(&Ctx);
  auto ArrayTy = mlir::spirv::RuntimeArrayType::get(SamplerTy);
  auto PointerTy = mlir::spirv::PointerType::get(
      ArrayTy, mlir::spirv::StorageClass::UniformConstant);

  for (unsigned I = 0; I != 2; ++I) {
    auto Global = mlir::spirv::GlobalVariableOp::create(
        Builder, Loc, mlir::TypeAttr::get(PointerTy),
        Builder.getStringAttr(Name), mlir::FlatSymbolRefAttr());
    Global.setDescriptorSetAttr(Builder.getI32IntegerAttr(Set));
    Global.setBindingAttr(Builder.getI32IntegerAttr(Binding));
  }

  auto EntryFn = mlir::spirv::FuncOp::create(
      Builder, Loc, "entry",
      Builder.getType<mlir::FunctionType>(
          llvm::ArrayRef<mlir::Type>(), llvm::ArrayRef<mlir::Type>()),
      mlir::spirv::FunctionControl::None);
  EntryFn.addEntryBlock();
  Builder.setInsertionPointToEnd(&EntryFn.getBody().front());
  mlir::spirv::ReturnOp::create(Builder, Loc);
  Builder.setInsertionPointToEnd(Module.getBody());
  mlir::spirv::EntryPointOp::create(Builder, Loc,
                                    mlir::spirv::ExecutionModel::GLCompute,
                                    llvm::StringRef("entry"),
                                    Builder.getArrayAttr({}));

  return mlir::OwningOpRef<mlir::spirv::ModuleOp>(Module);
}

// DXC can emit two `spirv.GlobalVariableOp`s for the same bindless
// descriptor heap (identical `OpName`, `DescriptorSet`, and `Binding`), and
// MLIR's own SPIR-V deserializer does not de-duplicate them, so
// `prepareResourceVariables` must not try to define two colliding
// `<name>.str` LLVM globals for them -- it should recognize the second
// declaration as the same heap and reuse the first one's name-global.
TEST(SPIRVToLLVMTest, PrepareResourceVariablesDedupesDuplicateHeapGlobals) {
  mlir::MLIRContext Ctx;
  mlir::OwningOpRef<mlir::spirv::ModuleOp> Module =
      buildDuplicateHeapGlobals(Ctx, "SamplerDescriptorHeap", /*Set=*/0,
                                /*Binding=*/3);

  feme::spirv::ResourceInfoMap Resources =
      feme::spirv::prepareResourceVariables(*Module);

  ASSERT_TRUE(Resources.count("SamplerDescriptorHeap"));
  const feme::spirv::ResourceInfo &Info =
      Resources["SamplerDescriptorHeap"];
  EXPECT_EQ(Info.DescriptorSet, 0u);
  EXPECT_EQ(Info.Binding, 3u);

  // Only one name-global should have been created for the pair, and it
  // must be a real, unique symbol in the module (no ".str" suffix
  // collision from the second, duplicate declaration).
  unsigned NameGlobalCount = 0;
  for (auto NameGlobal : Module->getOps<mlir::LLVM::GlobalOp>()) {
    EXPECT_EQ(NameGlobal.getSymName(), Info.NameSymbol);
    ++NameGlobalCount;
  }
  EXPECT_EQ(NameGlobalCount, 1u);
}

// (Roadmap H145) `ResourceDescriptorHeap[Index]` on a block-backed
// resource (`RWStructuredBuffer<T>`/`StructuredBuffer<T>`/
// `ByteAddressBuffer`/`ConstantBuffer<T>`) lowers to an *unbounded*
// `spirv.rtarray` of a `Block`-decorated struct, not the bounded
// `spirv.array` `getArrayedBlockCount`/`ArrayedBlockAccessChainPattern`
// previously required -- `prepareResourceVariables` fell through both of
// its arrayed-count checks (`getArrayedBlockCount` only recognized a
// bounded array; `getArrayedResourceCount` requires an opaque-resource,
// not a block, element) and skipped the variable entirely, and the access
// chain itself fell to a generic pattern that built an ordinary
// `llvm.getelementptr`-based address whose address space disagreed with
// the block's own global -- surfacing as `'llvm.mlir.addressof' op
// pointer address space must match address space of the referenced global
// or alias` at pipeline-creation time (`dyn-res-uav-counter.test` and
// every other `ResourceDescriptorHeap`-indexed `RWStructuredBuffer`/
// `StructuredBuffer` access, found via that test's own triage). Both
// helpers, and `ArrayedBlockAccessChainPattern`, now accept a
// `spirv.rtarray` pointee identically to a `spirv.array` one.
TEST(SPIRVToLLVMTest, UnboundedArrayedBlockConvertsInsteadOfFailing) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, "
      "RuntimeDescriptorArray], [SPV_EXT_descriptor_indexing]> { "
      "spirv.GlobalVariable @heap bind(0, 0) : "
      "!spirv.ptr<!spirv.rtarray<!spirv.struct<(!spirv.rtarray<f32, "
      "stride=4> [0]), Block>>, StorageBuffer> "
      "spirv.func @entry() -> () \"None\" { "
      "%heap = spirv.mlir.addressof @heap : "
      "!spirv.ptr<!spirv.rtarray<!spirv.struct<(!spirv.rtarray<f32, "
      "stride=4> [0]), Block>>, StorageBuffer> "
      "%idx = spirv.Constant 4 : i32 "
      "%zero = spirv.Constant 0 : i32 "
      "%elt = spirv.Constant 0 : i32 "
      "%ptr = spirv.AccessChain %heap[%idx, %zero, %elt] : "
      "!spirv.ptr<!spirv.rtarray<!spirv.struct<(!spirv.rtarray<f32, "
      "stride=4> [0]), Block>>, StorageBuffer>, i32, i32, i32 -> "
      "!spirv.ptr<f32, StorageBuffer> "
      "%val = spirv.Load \"StorageBuffer\" %ptr : f32 "
      "spirv.Return "
      "} spirv.EntryPoint \"GLCompute\" @entry "
      "spirv.ExecutionMode @entry \"LocalSize\", 1, 1, 1 }");
  EXPECT_NE(Result, "<failed>") << Result;
}

// (Roadmap L102) An `AccessChain` selecting a single lane of a `bool`
// vector (e.g. `bvec2`/`bvec3`/`bvec4`) cannot be lowered with upstream's
// generic single-GEP `AccessChainPattern`: the resulting `getelementptr`
// would index into an `i1`-element vector, and LLVM's IR verifier rejects
// any GEP into a non-byte-addressable element type ("GEP into vector with
// non-byte-addressable element type"). Per SPIR-V's own rule that a
// pointer to a vector component is only ever a valid immediate operand of
// `OpLoad`/`OpStore`, the fix fuses the AccessChain into its consuming
// Load/Store: the AccessChain itself converts to a harmless GEP
// addressing the *whole* vector (dropping the final lane index), while
// the Load/Store loads/stores the whole vector and extracts/inserts the
// selected lane with `llvm.extractelement`/`llvm.insertelement`, instead
// of failing to legalize / producing a verifier-rejected GEP.
TEST(SPIRVToLLVMTest, BoolVectorLaneLoadStoreConvertsInsteadOfFailing) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.func @entry() -> () \"None\" { "
      "%0 = spirv.Variable : !spirv.ptr<vector<2xi1>, Function> "
      "%1 = spirv.Constant 0 : i32 "
      "%2 = spirv.AccessChain %0[%1] : "
      "!spirv.ptr<vector<2xi1>, Function>, i32 -> !spirv.ptr<i1, Function> "
      "%3 = spirv.Load \"Function\" %2 : i1 "
      "%4 = spirv.AccessChain %0[%1] : "
      "!spirv.ptr<vector<2xi1>, Function>, i32 -> !spirv.ptr<i1, Function> "
      "spirv.Store \"Function\" %4, %3 : i1 "
      "spirv.Return "
      "} spirv.EntryPoint \"GLCompute\" @entry "
      "spirv.ExecutionMode @entry \"LocalSize\", 1, 1, 1 }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.extractelement"), std::string::npos) << Result;
  EXPECT_NE(Result.find("llvm.insertelement"), std::string::npos) << Result;
  // The illegal single-GEP lowering must not appear: no GEP result type
  // should itself be a vector-of-i1 (i.e. `getelementptr` indexing all the
  // way into the bool lane).
  EXPECT_EQ(Result.find("getelementptr inbounds <2 x i1>"), std::string::npos)
      << Result;
}

// (Roadmap L103) A plain (non-`Block`, no `Offset` decorations) struct
// whose members' own natural ABI alignments require a gap the SPIR-V
// declaration order alone doesn't reserve (e.g. a `bool`/`i1` member
// immediately followed by a wider, more-aligned vector member) used to
// convert as an ordinary, non-packed LLVM struct, leaving that gap's own
// size to whatever `DataLayout` happened to be attached to the
// surrounding `llvm::Module` at the moment a GEP into it was constant-
// folded to a raw byte offset -- a moment that, for `feme::cpu`, precedes
// the later switch to the real host `DataLayout` (see
// layOutStructIfOffsetsMatch's own comment), silently baking in the
// *wrong* byte offset for every member after such a gap. The fix builds
// this struct explicitly `packed`, with every natural-alignment gap
// materialized as its own synthetic `[N x i8]` member (computed via
// `mlir::DataLayout`'s own default rules, which agree with the real
// host's here), so the byte layout is fixed independent of whichever
// `DataLayout` a later GEP fold happens to see.
TEST(SPIRVToLLVMTest, NonOffsetStructWithAlignmentGapBuildsExplicitPadding) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @g : "
      "!spirv.ptr<!spirv.struct<(i1, vector<2xsi32>)>, Private> }");
  EXPECT_NE(Result, "<failed>") << Result;
  // A `<2 x i32>` member's own natural (host) ABI alignment is 8 bytes,
  // so a 1-byte `i1` immediately before it needs a 7-byte pad -- which
  // must be materialized explicitly in an otherwise-`packed` struct.
  EXPECT_NE(Result.find("!llvm.struct<packed (i1, array<7 x i8>, "
                        "vector<2xi32>"),
            std::string::npos)
      << Result;
}

// (Roadmap L103) An `Input`-storage `AccessChain` selecting a member of a
// plain (non-`Block`) struct is converted by StageIOArrayAccessChainPattern
// (despite its name, this handles any composite -- struct or array --
// `Input`-storage pointee, not just arrays), which used to forward the
// SPIR-V AccessChain's declared member index straight through to the
// resulting GEP unchanged. Once layOutStructIfOffsetsMatch's non-offset
// branch (above) can insert a gap member ahead of a later field, that
// field's *physical* LLVM struct index no longer matches its *declared*
// SPIR-V index -- so an unremapped GEP silently selects the wrong field
// (or the gap itself). The fix routes these indices through the same
// remapNestedStructMemberIndices helper OffsetStructMemberReorderAccess-
// ChainPattern already used for offset-decorated structs.
TEST(SPIRVToLLVMTest, InputStorageStructVec3MemberStaysAtDeclaredIndex) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @in_multi_member : "
      "!spirv.ptr<!spirv.struct<(f32, vector<3xf32>, f32)>, Input> "
      "spirv.func @entry() -> () \"None\" { "
      "%0 = spirv.mlir.addressof @in_multi_member : "
      "!spirv.ptr<!spirv.struct<(f32, vector<3xf32>, f32)>, Input> "
      "%1 = spirv.Constant 1 : i32 "
      "%2 = spirv.AccessChain %0[%1] : "
      "!spirv.ptr<!spirv.struct<(f32, vector<3xf32>, f32)>, Input>, i32 -> "
      "!spirv.ptr<vector<3xf32>, Input> "
      "%3 = spirv.Load \"Input\" %2 : vector<3xf32> "
      "spirv.Return "
      "} spirv.EntryPoint \"Fragment\" @entry "
      "spirv.ExecutionMode @entry \"OriginUpperLeft\" }");
  EXPECT_NE(Result, "<failed>") << Result;
  // (Roadmap L104) A `vector<3xf32>` member's own *real* natural
  // alignment -- matching the SPIR-V-logical DataLayout's
  // `vectorsAreElementAligned` rule, see
  // substituteTightVectorMembersIfNeeded's own comment -- is its
  // element's alignment (4 bytes for `f32`), not a rounded-up-to-power-
  // of-two 16 bytes. A leading `f32` member is already 4-byte aligned,
  // so no interior padding is needed before this member at all, and its
  // physical index equals its declared index (1); the GEP must select
  // that index directly, with no remap and no synthetic pad member
  // ahead of it.
  EXPECT_NE(Result.find(", 1] : (!llvm.ptr"), std::string::npos) << Result;
  // The struct's own vec3 member converts to the `feme.tight_vector`
  // marker-wrapped tight array form (roadmap H101j/L104), not a raw
  // `vector<3xf32>`, so its own alloc size stays the tight 12 bytes
  // regardless of whichever DataLayout later reads through it.
  EXPECT_NE(Result.find("feme.tight_vector"), std::string::npos) << Result;
}

// (Roadmap L133) A plain (non-`Block`, no `Offset` decorations) struct
// nested *two* levels deep -- an outer struct's own member is itself a
// struct with a member needing its own interior natural-alignment gap
// (roadmap L103) -- used to compute the wrong physical field index for
// that innermost member, landing squarely inside the nested struct's own
// synthetic pad instead. Modeled directly on the real `dEQP-VK.pipeline.
// monolithic.interface_matching.decoration_mismatch.
// out_flat_in_none_member_of_structure_in_block_vert_geom_out_frag_in`
// CTS repro's own shape: `struct TestStruct { vec2 dummy; vec4
// variableInStruct; }; in block { vec2 dummy; TestStruct structInBlock;
// } testBlock;`, accessed via `testBlock.structInBlock.variableInStruct`.
//
// Root cause: `remapNestedStructMemberIndices`'s own recursive,
// one-level-deeper remap (into `TestStruct`, once `structInBlock` itself
// has already been correctly remapped by the outermost level) calls
// `getStructMemberPhysicalIndexInRealType`, which used to return its
// `DeclaredIndex` argument unchanged whenever the nested struct had no
// explicit SPIR-V `Offset` decorations -- correct for member
// *permutation* (impossible without declared offsets), but wrong for a
// plain natural-alignment gap, which a non-offset struct can still need
// (`layOutStructIfOffsetsMatch`'s own `!Type.hasOffset()` branch, roadmap
// L103). The fix falls back to `getStructMemberPhysicalIndex`'s own
// isolated re-derivation in that case, exactly as the outermost struct
// level's own path already does unconditionally.
TEST(SPIRVToLLVMTest, NestedNonOffsetStructInteriorPadRemapsInnerMember) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @in_block : "
      "!spirv.ptr<!spirv.struct<(vector<2xf32>, "
      "!spirv.struct<(vector<2xf32>, vector<4xf32>)>)>, Input> "
      "spirv.func @entry() -> () \"None\" { "
      "%0 = spirv.mlir.addressof @in_block : "
      "!spirv.ptr<!spirv.struct<(vector<2xf32>, "
      "!spirv.struct<(vector<2xf32>, vector<4xf32>)>)>, Input> "
      "%1 = spirv.Constant 1 : i32 "
      "%2 = spirv.AccessChain %0[%1, %1] : "
      "!spirv.ptr<!spirv.struct<(vector<2xf32>, "
      "!spirv.struct<(vector<2xf32>, vector<4xf32>)>)>, Input>, i32, i32 -> "
      "!spirv.ptr<vector<4xf32>, Input> "
      "%3 = spirv.Load \"Input\" %2 : vector<4xf32> "
      "spirv.Return "
      "} spirv.EntryPoint \"Fragment\" @entry "
      "spirv.ExecutionMode @entry \"OriginUpperLeft\" }");
  EXPECT_NE(Result, "<failed>") << Result;
  // Both the outer struct's own `structInBlock` member and the nested
  // struct's own `variableInStruct` member need an 8-byte interior pad
  // ahead of them (a `vector<4xf32>`'s host-natural 16-byte alignment
  // leaves no room in either struct's own preceding 8-byte `vector<2xf32>`
  // member) -- both must land at *physical* index 2, not the *declared*
  // index 1 either was declared at.
  EXPECT_NE(Result.find("!llvm.struct<packed (vector<2xf32>, "
                        "array<8 x i8>, struct<packed "
                        "(vector<2xf32>, array<8 x i8>, vector<4xf32>)>)>"),
            std::string::npos)
      << Result;
  EXPECT_NE(Result.find("getelementptr %0[%2, 2, 2]"), std::string::npos)
      << Result;
}

// (Roadmap L125s) A directly `spirv.matrix`-typed `Input` variable (the
// shape a vertex-input attribute declared `layout(location = N) in mat2 M`
// takes) failed to legalize its own column- then row-selecting
// `spirv.AccessChain` entirely -- reproducing the real
// `dEQP-VK.pipeline.monolithic.vertex_input.multiple_attributes.
// binding_one_to_many.attributes.float.mat2.mat3` CTS failure
// (`VK_ERROR_INITIALIZATION_FAILED` at pipeline-creation time, from a
// generic MLIR "failed to legalize operation 'spirv.AccessChain' that was
// explicitly marked illegal" diagnostic). Root cause: `isCompositeStageIOType`
// -- which `isInputArrayAccessChain` consults to decide whether
// `StageIOArrayAccessChainPattern` should handle this AccessChain at all --
// only recognized `spirv.array`/`spirv.struct` pointee types, not
// `spirv.matrix`, even though `StageIOAddressOfPattern`'s own
// `isCompositeLLVMType` check (applied to the *converted* LLVM type, which
// a matrix always becomes an `!llvm.array` of column vectors under) already
// kept such a variable's own address as a real pointer rather than an
// eagerly-loaded value -- leaving no pattern able to legalize an
// AccessChain into that real pointer at all.
TEST(SPIRVToLLVMTest, InputStorageMatrixAccessChainConvertsInsteadOfFailing) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @in_mat2 : "
      "!spirv.ptr<!spirv.matrix<2 x vector<2xf32>>, Input> "
      "spirv.func @entry() -> () \"None\" { "
      "%0 = spirv.mlir.addressof @in_mat2 : "
      "!spirv.ptr<!spirv.matrix<2 x vector<2xf32>>, Input> "
      "%1 = spirv.Constant 1 : i32 "
      "%2 = spirv.Constant 0 : i32 "
      "%3 = spirv.AccessChain %0[%1, %2] : "
      "!spirv.ptr<!spirv.matrix<2 x vector<2xf32>>, Input>, i32, i32 -> "
      "!spirv.ptr<f32, Input> "
      "%4 = spirv.Load \"Input\" %3 : f32 "
      "spirv.Return "
      "} spirv.EntryPoint \"Vertex\" @entry }");
  EXPECT_NE(Result, "<failed>") << Result;
  // The matrix's own address stays a real pointer (an `llvm.mlir.addressof`
  // feeding a `getelementptr`), not an eagerly-loaded value: the leading
  // `0` index dereferences the pointer itself, the column index (`1`) and
  // row index (`0`) select the scalar lane, matching the declared SPIR-V
  // indices unchanged (no struct-member remap applies to a standalone
  // matrix -- see remapNestedStructMemberIndices's own "matrix/vector/
  // scalar leaf" break case).
  EXPECT_NE(Result.find("llvm.getelementptr %0[%3, %1, %2]"), std::string::npos)
      << Result;
}

// (Roadmap L104) A non-offset struct with a `vec3` member immediately
// followed by another member needs that trailing member placed 12 bytes
// (the vec3's own real, tight alloc size) after the vec3's own start, not
// 16 (the size `mlir::DataLayout`'s generic, "no explicit vector spec"
// rounding rule -- and this codebase's own pre-L104 behavior -- computes
// for a 3-lane vector). Modeled directly on
// `dEQP-VK.pipeline.pipeline_library.spec_constant.graphics.fragment.
// composite.struct.vec3`'s own real struct shape (`{int, float, bool,
// vec3, uint}`), whose trailing `uint` member's own value was read back
// from the wrong byte range before this fix -- see this roadmap item's
// own writeup for the full store/load `DataLayout` disagreement this
// caused.
TEST(SPIRVToLLVMTest, NonOffsetStructTightlyPacksVec3MemberSize) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @s : "
      "!spirv.ptr<!spirv.struct<(i32, f32, i1, vector<3xf32>, i32)>, "
      "Private> "
      "spirv.func @entry() -> () \"None\" { "
      "%0 = spirv.mlir.addressof @s : "
      "!spirv.ptr<!spirv.struct<(i32, f32, i1, vector<3xf32>, i32)>, "
      "Private> "
      "%1 = spirv.Constant 4 : i32 "
      "%2 = spirv.AccessChain %0[%1] : "
      "!spirv.ptr<!spirv.struct<(i32, f32, i1, vector<3xf32>, i32)>, "
      "Private>, i32 -> !spirv.ptr<i32, Private> "
      "%3 = spirv.Load \"Private\" %2 : i32 "
      "spirv.Return "
      "} spirv.EntryPoint \"GLCompute\" @entry "
      "spirv.ExecutionMode @entry \"LocalSize\", 1, 1, 1 }");
  EXPECT_NE(Result, "<failed>") << Result;
  // The gap between the `i1` and the `vec3` member is only 3 bytes
  // (aligning the vec3's own 1-byte-past-`i1` cursor position up to the
  // vec3's own real 4-byte alignment), not 7 (which would align to a
  // rounded-up-to-16-bytes alignment instead).
  EXPECT_NE(Result.find("array<3 x i8>"), std::string::npos) << Result;
  EXPECT_EQ(Result.find("array<7 x i8>"), std::string::npos) << Result;
  // The vec3 member itself converts to the tight-vector marker form.
  EXPECT_NE(Result.find("feme.tight_vector"), std::string::npos) << Result;
}

// (Roadmap L107) An `Output`-storage `AccessChain` reaching a padded
// struct member through *two* outer array dimensions (e.g. a
// tessellation-control shader's own per-control-point-arrayed loose
// output composite, `out TestStruct testStructArray[][3];`, accessed as
// `testStructArray[gl_InvocationID][2].variableInStruct` --
// `dEQP-VK.pipeline.pipeline_library.interface_matching.
// decoration_mismatch.*member_of_array_of_structures_vert_tesc_out_
// tese_in_frag`) used to be missed by OffsetStructMemberReorderAccess-
// ChainPattern entirely: it only ever peeled a *single* outer array
// level before giving up on finding the struct (`MemberIndexPos` 0 or 1
// only), so a struct two array dimensions deep fell through to MLIR's
// generic `AccessChainPattern`, which forwards the declared (pre-remap)
// member index unchanged -- wrongly selecting whatever physical field an
// interior alignment pad shifted into that position instead of the real
// member, corrupting the resulting byte offset
// (`CanonicalizeStage.cpp`'s own consumer-side pad-check assertion). The
// fix generalizes the array-peeling loop to any depth, forwarding one
// array index per level peeled ahead of the (now correctly remapped)
// member selector.
TEST(SPIRVToLLVMTest, OutputStorageTwoArrayDimsStructRemapsMemberIndex) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @out_arr : "
      "!spirv.ptr<!spirv.array<2 x !spirv.array<3 x "
      "!spirv.struct<(f32, vector<4xf32>)>>>, Output> "
      "spirv.func @entry() -> () \"None\" { "
      "%0 = spirv.mlir.addressof @out_arr : "
      "!spirv.ptr<!spirv.array<2 x !spirv.array<3 x "
      "!spirv.struct<(f32, vector<4xf32>)>>>, Output> "
      "%invocation = spirv.Constant 1 : i32 "
      "%elt = spirv.Constant 2 : i32 "
      "%member = spirv.Constant 1 : i32 "
      "%1 = spirv.AccessChain %0[%invocation, %elt, %member] : "
      "!spirv.ptr<!spirv.array<2 x !spirv.array<3 x "
      "!spirv.struct<(f32, vector<4xf32>)>>>, Output>, i32, i32, i32 -> "
      "!spirv.ptr<vector<4xf32>, Output> "
      "%2 = spirv.Load \"Output\" %1 : vector<4xf32> "
      "spirv.Return "
      "} spirv.EntryPoint \"Vertex\" @entry }");
  EXPECT_NE(Result, "<failed>") << Result;
  // The inner struct's own natural-alignment gap (a 16-byte-aligned
  // `<4 x f32>` following a 4-byte `f32`) must still be materialized as
  // an explicit pad, same as any single-array-dimension case.
  EXPECT_NE(Result.find("array<12 x i8>"), std::string::npos) << Result;
  // Both outer array indices (the per-invocation index, then the inner
  // constant array index) must precede the member selector unchanged,
  // and the declared member index (1, the `vector<4xf32>`) must be
  // remapped to its real physical field index (2, after the synthetic
  // pad): the resulting GEP's own trailing indices are `[..., %1, %2, 2]`
  // (a leading zero, then the two array indices, then the remapped
  // member index), not `[..., %1, %2, 1]` (the unremapped, declared
  // index the bug used to forward straight through).
  EXPECT_NE(Result.find("%1, %2, 2] :"), std::string::npos) << Result;
  EXPECT_EQ(Result.find("%1, %2, 1] :"), std::string::npos) << Result;
}


// (Roadmap L183) `spirv.GL.FrexpStruct`'s synthetic, non-memory `ResType`
// result struct must legalize as a tightly packed
// `{significand, exponent}` pair matching `llvm.intr.frexp`'s own always-
// packed intrinsic signature, then be repacked into whatever "canonical"
// (possibly padded, possibly `feme.tight_vector`-marker-wrapped) shape
// `getTypeConverter()->convertType` computes for that same SPIR-V struct
// type -- needed because a `spirv.CompositeExtract` consuming this
// result may see either shape, depending on how the *specific* SPIR-V
// module declares the result struct (see the two tests below, which
// exercise both). This first test uses `FrexpStruct`'s own
// undecorated/no-`Offset` result type (the hand-authored-GLSL-shader
// shape this roadmap item was first diagnosed against) -- a vec2
// significand/exponent pair extracted back out via `CompositeExtract`
// must legalize to plain, marker-free LLVM values with no leftover
// `builtin.unrealized_conversion_cast` (the symptom of the type-converter
// canonical-type mismatch this pattern's own repacking step exists to
// avoid).
TEST(SPIRVToLLVMTest, FrexpStructVec2NoOffsetLegalizesWithoutUnresolvedCast) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, "
      "Float16], []> { spirv.func @entry() -> () \"None\" { "
      "%src = spirv.Constant dense<1.0> : vector<2xf16> "
      "%r = spirv.GL.FrexpStruct %src : vector<2xf16> -> "
      "!spirv.struct<(vector<2xf16>, vector<2xsi32>)> "
      "%sig = spirv.CompositeExtract %r[0 : i32] : "
      "!spirv.struct<(vector<2xf16>, vector<2xsi32>)> "
      "%exp = spirv.CompositeExtract %r[1 : i32] : "
      "!spirv.struct<(vector<2xf16>, vector<2xsi32>)> "
      "spirv.Return } spirv.EntryPoint \"GLCompute\" @entry "
      "spirv.ExecutionMode @entry \"LocalSize\", 1, 1, 1 }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.intr.frexp"), std::string::npos) << Result;
  EXPECT_EQ(Result.find("unrealized_conversion_cast"), std::string::npos)
      << Result;
}

// (Roadmap L183) The real CTS shape (`dEQP-VK.spirv_assembly.instruction.
// compute.float16.arithmetic_2.frexpstructe`/`frexpstructs`, which
// originally crashed with an LLVM `CallInst::init` "bad signature"
// assertion) instead declares `FrexpStruct`'s own result struct with
// explicit `Offset 0`/`Offset 4` member decorations (a genuinely
// `Offset`-decorated, memory-layout-eligible struct, routing through
// `layOutStructIfOffsetsMatch`'s *other* branch than the no-`Offset` test
// above) -- and additionally stores that result into a `Function`-storage
// local variable of the same struct type before a later `CompositeExtract`
// reads a member back out. This exercises `getTightVectorArrayType`'s own
// marker-struct substitution (needed here because a 2-lane `f16` vector's
// own natural ABI alignment cannot reproduce these tight declared
// offsets), so `CompositeExtractMemberReorderPattern`'s tight-vector
// *unwrap* step (this session's own fix, alongside
// `CompositeInsertMemberReorderPattern`'s wrap step for the intervening
// `spirv.Store`) must run, not just `FrexpStructPattern`'s own repacking.
TEST(SPIRVToLLVMTest,
     FrexpStructVec2OffsetDecoratedMemberExtractUnwrapsTightVector) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, "
      "Float16], []> { spirv.func @entry() -> () \"None\" { "
      "%var = spirv.Variable : "
      "!spirv.ptr<!spirv.struct<(vector<2xf16> [0], vector<2xsi32> [4])>, "
      "Function> "
      "%src = spirv.Constant dense<1.0> : vector<2xf16> "
      "%r = spirv.GL.FrexpStruct %src : vector<2xf16> -> "
      "!spirv.struct<(vector<2xf16> [0], vector<2xsi32> [4])> "
      "spirv.Store \"Function\" %var, %r : "
      "!spirv.struct<(vector<2xf16> [0], vector<2xsi32> [4])> "
      "%loaded = spirv.Load \"Function\" %var : "
      "!spirv.struct<(vector<2xf16> [0], vector<2xsi32> [4])> "
      "%sig = spirv.CompositeExtract %loaded[0 : i32] : "
      "!spirv.struct<(vector<2xf16> [0], vector<2xsi32> [4])> "
      "spirv.Return } spirv.EntryPoint \"GLCompute\" @entry "
      "spirv.ExecutionMode @entry \"LocalSize\", 1, 1, 1 }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.intr.frexp"), std::string::npos) << Result;
  // The whole point of this test: no unresolved cast left over from
  // either the FrexpStruct repack or the later CompositeExtract's own
  // tight-vector unwrap.
  EXPECT_EQ(Result.find("unrealized_conversion_cast"), std::string::npos)
      << Result;
  // The final extracted significand must be a plain, marker-free
  // `vector<2xf16>` value (an `llvm.insertelement`-built vector, not a
  // bare `feme.tight_vector` marker struct still needing unwrap).
  EXPECT_NE(Result.find("llvm.insertelement"), std::string::npos) << Result;
}

// (Roadmap L183) A regression guard for the bug this session's own fix
// briefly introduced and then corrected: `CompositeExtractMemberReorder-
// Pattern`/`CompositeInsertMemberReorderPattern` must decline (leaving the
// op to upstream's own generic, `llvm.extractelement`/`insertelement`-
// based pattern) whenever `spirv.CompositeExtract`'s own index selects a
// single lane out of a *plain vector* composite -- `llvm.extractvalue`
// cannot express that at all, and unconditionally attempting it (this
// session's own first, briefly-broken version of this fix) aborts with an
// LLVM `ExtractValueOp::build` "incompatible type" assertion. Modeled
// directly on the exact shape that regression broke, from
// feme-spirv-compute-shader.mlir: extracting a single scalar lane out of a
// bare `vector<3xsi32>` (no enclosing struct or array at all).
TEST(SPIRVToLLVMTest, PlainVectorLaneCompositeExtractStillLegalizes) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.func @entry(%v : vector<3xsi32>) -> si32 \"None\" { "
      "%lane = spirv.CompositeExtract %v[0 : i32] : vector<3xsi32> "
      "spirv.ReturnValue %lane : si32 } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.extractelement"), std::string::npos) << Result;
}

// (Roadmap L184) A regression guard for the `opcompositeextract.
// struct16arr3` crash this session root-caused and fixed:
// `CompositeExtractMemberReorderPattern`'s pre-fix version only ever
// handled a fully struct/array-navigable index path (bottoming out at a
// genuine scalar or a `feme.tight_vector`-marker-wrapped leaf) or declined
// entirely -- it never handled navigating through a struct member and
// *then* landing on a single lane of a genuinely bare (non-marker-
// substituted) vector, which is what a `vector<2xf16>` array element
// converts to whenever its own natural LLVM ABI width already matches its
// SPIR-V `ArrayStride` (so no marker substitution is needed at all).
// Declining this shape deferred to upstream's own generic
// `CompositeExtractPattern`, which also cannot express it (it only
// special-cases a vector at the very top of the whole composite) and
// crashes an internal `ExtractValueOp` assertion instead. Modeled
// directly on that failure's minimal shape: a two-member struct of plain
// `vector<2xsi32>`s, extracting member 1's lane 0 via a single
// `spirv.CompositeExtract` with a two-element index path.
TEST(SPIRVToLLVMTest, StructMemberVectorLaneCompositeExtractLegalizes) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.func @entry() -> si32 \"None\" { "
      "%src = spirv.Undef : "
      "!spirv.struct<(vector<2xsi32>, vector<2xsi32>)> "
      "%lane = spirv.CompositeExtract %src[1 : i32, 0 : i32] : "
      "!spirv.struct<(vector<2xsi32>, vector<2xsi32>)> "
      "spirv.ReturnValue %lane : si32 } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.extractvalue"), std::string::npos) << Result;
  EXPECT_NE(Result.find("llvm.extractelement"), std::string::npos) << Result;
}

// (Roadmap L184) The `CompositeInsert` dual of the test just above --
// `CompositeInsertMemberReorderPattern`'s own symmetric fix, inserting a
// scalar into one lane of a struct member's bare vector via
// `llvm.insertelement` wrapped in an outer `llvm.insertvalue`, rather than
// deferring (and crashing) the same way its extract counterpart did.
TEST(SPIRVToLLVMTest, StructMemberVectorLaneCompositeInsertLegalizes) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.func @entry(%val : si32) -> "
      "!spirv.struct<(vector<2xsi32>, vector<2xsi32>)> \"None\" { "
      "%src = spirv.Undef : "
      "!spirv.struct<(vector<2xsi32>, vector<2xsi32>)> "
      "%result = spirv.CompositeInsert %val, %src[1 : i32, 0 : i32] : si32 "
      "into !spirv.struct<(vector<2xsi32>, vector<2xsi32>)> "
      "spirv.ReturnValue %result : "
      "!spirv.struct<(vector<2xsi32>, vector<2xsi32>)> } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.insertvalue"), std::string::npos) << Result;
  EXPECT_NE(Result.find("llvm.insertelement"), std::string::npos) << Result;
}

// (Roadmap L184) `spirv.VectorInsertDynamic` had no conversion pattern at
// all (neither upstream nor in this file) before this session -- the
// write-side counterpart of `VectorExtractDynamicPattern`, which *was*
// already handled. Converts directly to `llvm.insertelement`.
TEST(SPIRVToLLVMTest, VectorInsertDynamicLegalizesToInsertElement) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.func @entry(%v : vector<2xsi32>, %val : si32, %idx : si32) -> "
      "vector<2xsi32> \"None\" { "
      "%result = spirv.VectorInsertDynamic %val, %v[%idx] : "
      "vector<2xsi32>, si32 "
      "spirv.ReturnValue %result : vector<2xsi32> } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.insertelement"), std::string::npos) << Result;
}

// (Roadmap L184) `spirv.GL.Asinh`/`Acosh`/`Atanh` had no conversion pattern
// at all (neither upstream nor in this file) before this session -- LLVM
// has no `llvm.asinh`/`llvm.acosh`/`llvm.atanh` intrinsic (unlike
// `Sinh`/`Cosh`/`Tanh`, already mapped directly), so each expands
// algebraically via its own standard closed-form identity instead (see
// `InverseHyperbolicPattern`'s own comment). Confirms all three legalize
// to a `log`/`sqrt`-based expansion with no leftover illegal op, one test
// per op given each has a distinct formula.
TEST(SPIRVToLLVMTest, AsinhLegalizesToLogSqrtExpansion) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, "
      "Float16], []> { spirv.func @entry(%v : vector<2xf16>) -> "
      "vector<2xf16> \"None\" { "
      "%r = spirv.GL.Asinh %v : vector<2xf16> "
      "spirv.ReturnValue %r : vector<2xf16> } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.intr.sqrt"), std::string::npos) << Result;
  EXPECT_NE(Result.find("llvm.intr.log"), std::string::npos) << Result;
  EXPECT_EQ(Result.find("spirv.GL.Asinh"), std::string::npos) << Result;
}

TEST(SPIRVToLLVMTest, AcoshLegalizesToLogSqrtExpansion) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, "
      "Float16], []> { spirv.func @entry(%v : vector<2xf16>) -> "
      "vector<2xf16> \"None\" { "
      "%r = spirv.GL.Acosh %v : vector<2xf16> "
      "spirv.ReturnValue %r : vector<2xf16> } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.intr.sqrt"), std::string::npos) << Result;
  EXPECT_NE(Result.find("llvm.intr.log"), std::string::npos) << Result;
  EXPECT_NE(Result.find("llvm.fsub"), std::string::npos) << Result;
  EXPECT_EQ(Result.find("spirv.GL.Acosh"), std::string::npos) << Result;
}

// (Roadmap L90) `spirv.GroupNonUniformShuffleUp`/`Down` (HLSL's
// `WaveReadLaneAt(WaveGetLaneIndex() -/+ delta, value)` idiom, gated by the
// `GroupNonUniformShuffleRelative` capability -- distinct from plain
// `Shuffle`/`ShuffleXor`'s own `GroupNonUniformShuffle`) convert directly to
// `llvm.spv.wave.readlane`, sharing `ShuffleXorConversionPattern`'s own
// "compute a target id, then shuffle" shape: no conversion pattern existed
// for either op at all before this fix.
TEST(SPIRVToLLVMTest, ShuffleUpLegalizesToReadLane) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, ["
      "GroupNonUniform, GroupNonUniformShuffleRelative], []> { spirv.func "
      "@entry(%v : f32, %delta : i32) -> f32 \"None\" { %r = "
      "spirv.GroupNonUniformShuffleUp <Subgroup> %v, %delta : f32, i32 "
      "spirv.ReturnValue %r : f32 } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.spv.wave.readlane"), std::string::npos)
      << Result;
  EXPECT_NE(Result.find("llvm.spv.subgroup.local.invocation.id"),
            std::string::npos)
      << Result;
  EXPECT_NE(Result.find("llvm.sub"), std::string::npos) << Result;
}

TEST(SPIRVToLLVMTest, ShuffleDownLegalizesToReadLane) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, ["
      "GroupNonUniform, GroupNonUniformShuffleRelative], []> { spirv.func "
      "@entry(%v : f32, %delta : i32) -> f32 \"None\" { %r = "
      "spirv.GroupNonUniformShuffleDown <Subgroup> %v, %delta : f32, i32 "
      "spirv.ReturnValue %r : f32 } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.spv.wave.readlane"), std::string::npos)
      << Result;
  EXPECT_NE(Result.find("llvm.spv.subgroup.local.invocation.id"),
            std::string::npos)
      << Result;
  EXPECT_NE(Result.find("llvm.add"), std::string::npos) << Result;
}

// (Roadmap L116(d)) A `spirv.Constant` whose type contains a struct
// constituent anywhere (directly, or nested inside an array) has no legal
// LLVM `ElementsAttr` encoding -- `ArrayConstantPattern` explicitly rejects
// it -- so it must instead be decomposed one aggregate level at a time into
// simpler per-member `spirv.Constant`s feeding a `spirv.CompositeConstruct`,
// which either convert directly or get re-decomposed by this same pattern.
TEST(SPIRVToLLVMTest, StructConstantDecomposesIntoCompositeConstruct) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], "
      "[]> { spirv.func @entry() -> () \"None\" { "
      "%r = spirv.Constant [1 : si32, 2 : si32] : "
      "!spirv.struct<(si32, si32)> "
      "spirv.Return } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.mlir.constant(1 : i32) : i32"), std::string::npos)
      << Result;
  EXPECT_NE(Result.find("llvm.mlir.constant(2 : i32) : i32"), std::string::npos)
      << Result;
  EXPECT_NE(Result.find("llvm.insertvalue"), std::string::npos) << Result;
  EXPECT_EQ(Result.find("spirv.Constant"), std::string::npos) << Result;
}

// Same gap, but for an array whose *element* type is a struct: the
// `spirv.Constant`'s outer type is an `ArrayType`, so `StructConstantPattern`
// must recurse per-element (via `containsStructType` walking through the
// array layer) rather than only handling a top-level `StructType` directly.
TEST(SPIRVToLLVMTest, ArrayOfStructConstantDecomposesRecursively) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], "
      "[]> { spirv.func @entry() -> () \"None\" { "
      "%r = spirv.Constant [[1 : si32], [2 : si32]] : "
      "!spirv.array<2 x !spirv.struct<(si32)>> "
      "spirv.Return } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.mlir.constant(1 : i32) : i32"), std::string::npos)
      << Result;
  EXPECT_NE(Result.find("llvm.mlir.constant(2 : i32) : i32"), std::string::npos)
      << Result;
  EXPECT_EQ(Result.find("spirv.Constant"), std::string::npos) << Result;
}

// (Roadmap L116(d)) `convertArrayTypeIgnoringDecorations` (roadmap L17)
// widens a scalar array element whose declared `ArrayStride` exceeds its
// natural size into an opaque `Stride`-sized byte-array stand-in type, but
// `CompositeConstructPattern::convertArray` previously required every
// constituent's own converted type to match that padded stand-in type
// exactly, which a plain scalar operand never does -- it must instead be
// reinterpreted via a scratch `llvm.alloca` round trip.
TEST(SPIRVToLLVMTest, CompositeConstructReinterpretsStridePaddedArrayElement) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], "
      "[]> { spirv.func @entry(%a : si32, %b : si32) -> () \"None\" { "
      "%r = spirv.CompositeConstruct %a, %b : (si32, si32) -> "
      "!spirv.array<2 x si32, stride=16> "
      "spirv.Return } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.alloca"), std::string::npos) << Result;
  EXPECT_NE(Result.find("llvm.store"), std::string::npos) << Result;
  EXPECT_NE(Result.find("llvm.load"), std::string::npos) << Result;
  EXPECT_NE(Result.find("llvm.insertvalue"), std::string::npos) << Result;
  EXPECT_EQ(Result.find("spirv.CompositeConstruct"), std::string::npos)
      << Result;
}

TEST(SPIRVToLLVMTest, AtanhLegalizesToLogExpansion) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, "
      "Float16], []> { spirv.func @entry(%v : vector<2xf16>) -> "
      "vector<2xf16> \"None\" { "
      "%r = spirv.GL.Atanh %v : vector<2xf16> "
      "spirv.ReturnValue %r : vector<2xf16> } }");
  EXPECT_NE(Result, "<failed>") << Result;
  EXPECT_NE(Result.find("llvm.fdiv"), std::string::npos) << Result;
  EXPECT_NE(Result.find("llvm.intr.log"), std::string::npos) << Result;
  EXPECT_EQ(Result.find("spirv.GL.Atanh"), std::string::npos) << Result;
}

} // namespace
