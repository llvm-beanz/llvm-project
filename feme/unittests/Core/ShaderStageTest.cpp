//===- ShaderStageTest.cpp - feme::ShaderStage unit tests ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/ShaderStage.h"

#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "gtest/gtest.h"

#include <vector>

using namespace feme;
using namespace llvm;

namespace {

/// Every stage, so each test can assert over the whole enumeration rather
/// than a hand-picked subset that a later stage addition would not extend.
std::vector<ShaderStage> allStages() {
  std::vector<ShaderStage> Stages;
  for (uint8_t I = 0; I != static_cast<uint8_t>(ShaderStage::NumStages); ++I)
    Stages.push_back(static_cast<ShaderStage>(I));
  return Stages;
}

TEST(ShaderStageTest, NameRoundTrip) {
  for (ShaderStage Stage : allStages()) {
    StringRef Name = getShaderStageName(Stage);
    EXPECT_FALSE(Name.empty());
    EXPECT_EQ(parseShaderStage(Name), Stage);
  }
}

TEST(ShaderStageTest, NamesAreDistinct) {
  StringSet<> Seen;
  for (ShaderStage Stage : allStages())
    EXPECT_TRUE(Seen.insert(getShaderStageName(Stage)).second)
        << "duplicate spelling: " << getShaderStageName(Stage).str();
}

TEST(ShaderStageTest, ParseRejectsNonStages) {
  EXPECT_EQ(parseShaderStage(""), std::nullopt);
  EXPECT_EQ(parseShaderStage("kernel"), std::nullopt);
  EXPECT_EQ(parseShaderStage("rootsignature"), std::nullopt);
  EXPECT_EQ(parseShaderStage("Compute"), std::nullopt);
}

TEST(ShaderStageTest, ParseAcceptsTripleEnvironmentSpelling) {
  // The one stage whose triple environment name differs from FeMe's own.
  EXPECT_EQ(parseShaderStage("pixel"), ShaderStage::Fragment);
  EXPECT_EQ(getShaderStageName(ShaderStage::Fragment), "fragment");
}

TEST(ShaderStageTest, EnvironmentRoundTrip) {
  for (ShaderStage Stage : allStages()) {
    Triple::EnvironmentType Env = getEnvironmentForShaderStage(Stage);
    EXPECT_EQ(getShaderStageForEnvironment(Env), Stage);
    // Every stage's environment is one a target triple can actually spell,
    // which is what keeps a raised module's triple and its entry points'
    // attributes in the same vocabulary.
    EXPECT_EQ(parseShaderStage(Triple::getEnvironmentTypeName(Env)), Stage);
  }
}

TEST(ShaderStageTest, EnvironmentsWithoutAStage) {
  EXPECT_EQ(getShaderStageForEnvironment(Triple::UnknownEnvironment),
            std::nullopt);
  EXPECT_EQ(getShaderStageForEnvironment(Triple::GNU), std::nullopt);
  // A root-signature profile is a container, not a pipeline stage.
  EXPECT_EQ(getShaderStageForEnvironment(Triple::RootSignature), std::nullopt);
}

TEST(ShaderStageTest, CompatibilityWithEnvironment) {
  EXPECT_TRUE(isShaderStageCompatibleWithEnvironment(ShaderStage::Compute,
                                                     Triple::Compute));
  EXPECT_FALSE(isShaderStageCompatibleWithEnvironment(ShaderStage::Vertex,
                                                      Triple::Compute));
  EXPECT_TRUE(isShaderStageCompatibleWithEnvironment(ShaderStage::Fragment,
                                                     Triple::Pixel));

  // A library module's entry points each declare their own stage.
  for (ShaderStage Stage : allStages())
    EXPECT_TRUE(isShaderStageCompatibleWithEnvironment(Stage, Triple::Library));

  // A triple that names no stage constrains nothing.
  for (ShaderStage Stage : allStages())
    EXPECT_TRUE(isShaderStageCompatibleWithEnvironment(
        Stage, Triple::UnknownEnvironment));
}

/// Creates an empty function named \p Name in \p M, the shape an entry point
/// attribute test needs.
Function *makeFunction(Module &M, StringRef Name) {
  return Function::Create(FunctionType::get(Type::getVoidTy(M.getContext()),
                                            /*isVarArg=*/false),
                          GlobalValue::ExternalLinkage, Name, M);
}

TEST(ShaderStageTest, AttributeRoundTrip) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  for (ShaderStage Stage : allStages()) {
    Function *F = makeFunction(M, getShaderStageName(Stage));
    setShaderStage(*F, Stage);
    EXPECT_EQ(F->getFnAttribute(getShaderStageAttrName()).getValueAsString(),
              getShaderStageName(Stage));
    EXPECT_EQ(getShaderStage(*F), Stage);
  }
}

TEST(ShaderStageTest, SettingAStageReplacesTheRecordedOne) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "entry");
  setShaderStage(*F, ShaderStage::Vertex);
  setShaderStage(*F, ShaderStage::Fragment);
  EXPECT_EQ(getShaderStage(*F), ShaderStage::Fragment);
}

TEST(ShaderStageTest, NoStageOnANonEntryPoint) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  EXPECT_EQ(getShaderStage(*makeFunction(M, "helper")), std::nullopt);
}

TEST(ShaderStageTest, HLSLShaderAttributeIsStillAccepted) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "entry");
  F->addFnAttr("hlsl.shader", "compute");
  EXPECT_EQ(getShaderStage(*F), ShaderStage::Compute);

  // ... including the `pixel` spelling DXIL raising gives a fragment shader.
  Function *G = makeFunction(M, "frag");
  G->addFnAttr("hlsl.shader", "pixel");
  EXPECT_EQ(getShaderStage(*G), ShaderStage::Fragment);
}

TEST(ShaderStageTest, ShaderStageAttributeWinsOverHLSLShader) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "entry");
  F->addFnAttr("hlsl.shader", "pixel");
  setShaderStage(*F, ShaderStage::Vertex);
  EXPECT_EQ(getShaderStage(*F), ShaderStage::Vertex);
}

TEST(ShaderStageTest, EntryPointPredicate) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  EXPECT_FALSE(isShaderEntryPoint(*makeFunction(M, "helper")));

  Function *Raised = makeFunction(M, "raised");
  Raised->addFnAttr("hlsl.shader", "compute");
  EXPECT_TRUE(isShaderEntryPoint(*Raised));

  Function *Vertex = makeFunction(M, "vertex_main");
  setShaderStage(*Vertex, ShaderStage::Vertex);
  EXPECT_TRUE(isShaderEntryPoint(*Vertex));
}

TEST(ShaderStageTest, UnknownAttributeValueIsNotAStage) {
  LLVMContext Ctx;
  Module M("m", Ctx);
  Function *F = makeFunction(M, "entry");
  F->addFnAttr(getShaderStageAttrName(), "nonsense");
  EXPECT_EQ(getShaderStage(*F), std::nullopt);
}

} // namespace
