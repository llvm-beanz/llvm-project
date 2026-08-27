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

// A member decoration this milestone does not model (e.g. `Offset`, which
// an ordinary uniform block's members carry but a stage-IO struct never
// does) is filtered out rather than corrupting the encoding, matching
// buildStageIODecorationsAttr's own "unrecognized decoration is simply not
// preserved" behavior for a whole-variable attribute.
TEST(SPIRVToLLVMTest, UnrecognizedMemberDecorationIsFilteredOut) {
  std::string Result = convertToLLVMDialect(
      "spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> "
      "{ spirv.GlobalVariable @block : "
      "!spirv.ptr<!spirv.struct<(f32 [0])>, Output> }");
  EXPECT_NE(Result, "<failed>");
  EXPECT_EQ(
      Result.find(feme::spirv::getStageIOMemberDecorationsAttrName().str()),
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

} // namespace
