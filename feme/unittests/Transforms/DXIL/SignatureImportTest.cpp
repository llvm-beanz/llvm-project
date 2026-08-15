//===- SignatureImportTest.cpp - Tests for DXIL signature import --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::dxil;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("SignatureImportTest", errs());
  return M;
}

/// A `!dx.entryPoints`-shaped `Signatures` tuple with one arbitrary input
/// element (`POSITION`, float4, register 0) and one `SV_Position` output
/// element, matching the metadata `feme::dxsa::translateToLLVMIR` writes
/// (feme/lib/Translate/DXSA/DXSAToLLVMIRTranslator.cpp) and real DXIL's
/// `DxilMDHelper::EmitSignatureElement` wire format.
constexpr StringRef VertexSignatureIR = R"(
  !test = !{!0}
  !0 = !{!1, !3, null}
  !1 = !{!2}
  !2 = !{i32 0, !"POSITION", i8 9, i8 0, !4, i8 2, i32 1, i8 4, i32 0, i8 0, null}
  !3 = !{!5}
  !4 = !{i32 0}
  !5 = !{i32 0, !"SV_Position", i8 9, i8 3, !4, i8 0, i32 1, i8 4, i32 0, i8 0, null}
)";

TEST(SignatureImportTest, ConvertsInputAndOutputElements) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, VertexSignatureIR);
  ASSERT_TRUE(M);
  auto *Signatures = cast<MDNode>(M->getNamedMetadata("test")->getOperand(0));

  EntrySignature Sig =
      convertEntrySignature(Signatures, ShaderStage::Vertex);
  ASSERT_EQ(Sig.Elements.size(), 2u);

  const SignatureElement &Input = Sig.Elements[0];
  EXPECT_EQ(Input.ElementID, 0u);
  EXPECT_EQ(Input.Direction, SignatureDirection::Input);
  EXPECT_EQ(Input.SemanticName, "POSITION");
  EXPECT_EQ(Input.SemanticIndex, 0u);
  EXPECT_EQ(Input.SystemValue, SignatureSystemValue::None);
  ASSERT_TRUE(Input.Location.has_value());
  EXPECT_EQ(*Input.Location, 0u);
  EXPECT_EQ(Input.ComponentType, SignatureComponentType::Float);
  EXPECT_EQ(Input.BitWidth, 32u);
  EXPECT_EQ(Input.FirstComponent, 0u);
  EXPECT_EQ(Input.ComponentCount, 4u);
  EXPECT_EQ(Input.RowCount, 1u);
  // DXIL InterpMode 2 ("Linear") is a plain perspective-correct varying.
  EXPECT_EQ(Input.Interpolation, SignatureInterpolationMode::Perspective);
  EXPECT_EQ(Input.Frequency, SignatureFrequency::PerVertex);

  const SignatureElement &Output = Sig.Elements[1];
  EXPECT_EQ(Output.Direction, SignatureDirection::Output);
  EXPECT_EQ(Output.SemanticName, "SV_Position");
  EXPECT_EQ(Output.SystemValue, SignatureSystemValue::Position);
  // A system value carries no Vulkan-style location.
  EXPECT_FALSE(Output.Location.has_value());
  // DXIL InterpMode 0 ("Undefined") collapses onto Flat.
  EXPECT_EQ(Output.Interpolation, SignatureInterpolationMode::Flat);

  EXPECT_TRUE(verifySignature(Sig));
}

TEST(SignatureImportTest, NullSignaturesConvertToEmpty) {
  EntrySignature Sig = convertEntrySignature(nullptr, ShaderStage::Vertex);
  EXPECT_TRUE(Sig.Elements.empty());
}

TEST(SignatureImportTest, PatchConstantDirectionDependsOnStage) {
  LLVMContext Ctx;
  // A patch-constant list (`SV_TessFactor`, row 0) in the third Signatures
  // slot, matching a hull shader's patch-constant *output* metadata.
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    !test = !{!0}
    !0 = !{null, null, !1}
    !1 = !{!2}
    !2 = !{i32 0, !"SV_TessFactor", i8 9, i8 25, !3, i8 0, i32 1, i8 1, i32 0, i8 0, null}
    !3 = !{i32 0}
  )");
  ASSERT_TRUE(M);
  auto *Signatures = cast<MDNode>(M->getNamedMetadata("test")->getOperand(0));

  EntrySignature HullSig =
      convertEntrySignature(Signatures, ShaderStage::Hull);
  ASSERT_EQ(HullSig.Elements.size(), 1u);
  EXPECT_EQ(HullSig.Elements[0].Direction, SignatureDirection::PatchOutput);
  EXPECT_EQ(HullSig.Elements[0].Frequency, SignatureFrequency::PerPatch);

  EntrySignature DomainSig =
      convertEntrySignature(Signatures, ShaderStage::Domain);
  ASSERT_EQ(DomainSig.Elements.size(), 1u);
  EXPECT_EQ(DomainSig.Elements[0].Direction, SignatureDirection::PatchInput);
}

TEST(SignatureImportTest, EntrySignatureRoundTripsThroughFunctionMetadata) {
  LLVMContext Ctx;
  std::string Assembly = (VertexSignatureIR + R"(
    define void @main() {
      ret void
    }
  )").str();
  std::unique_ptr<Module> M = parseIR(Ctx, Assembly);
  ASSERT_TRUE(M);
  auto *Signatures = cast<MDNode>(M->getNamedMetadata("test")->getOperand(0));
  EntrySignature Sig =
      convertEntrySignature(Signatures, ShaderStage::Vertex);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);

  EXPECT_FALSE(getEntrySignature(*F).has_value());
  setEntrySignature(*F, Sig);
  EXPECT_TRUE(F->hasMetadata(getEntrySignatureMDKind()));

  std::optional<EntrySignature> RoundTripped = getEntrySignature(*F);
  ASSERT_TRUE(RoundTripped.has_value());
  ASSERT_EQ(RoundTripped->Elements.size(), Sig.Elements.size());
  EXPECT_EQ(RoundTripped->Elements[0].SemanticName, "POSITION");
  EXPECT_EQ(RoundTripped->Elements[1].SystemValue,
           SignatureSystemValue::Position);
}

TEST(SignatureImportTest, RootSignatureRoundTripsThroughFunctionMetadata) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @main() {
      ret void
    }
  )");
  ASSERT_TRUE(M);
  Function *F = M->getFunction("main");
  ASSERT_TRUE(F);

  EXPECT_FALSE(getRootSignature(*F).has_value());
  std::vector<uint8_t> Bytes = {1, 2, 3, 4, 5};
  setRootSignature(*F, Bytes);
  EXPECT_TRUE(F->hasMetadata(getRootSignatureMDKind()));

  std::optional<std::vector<uint8_t>> RoundTripped = getRootSignature(*F);
  ASSERT_TRUE(RoundTripped.has_value());
  EXPECT_EQ(*RoundTripped, Bytes);
}

} // namespace
