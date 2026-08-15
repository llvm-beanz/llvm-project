//===- SPIRVToLLVMTest.cpp - Tests for FeMe's SPIR-V -> LLVM conversion --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Conversion/SPIRVToLLVM/SPIRVToLLVM.h"

#include "feme/Core/ShaderStage.h"

#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
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

} // namespace
