//===- DXSAToLLVMIRTranslatorTest.cpp - unit tests ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the dxsa dialect -> DXIL translation. The end-to-end
// DXBC-assembly-in, DXIL-out behaviour is covered by the lit tests under
// feme/test/Translate/DXBC; these cases pin down the pieces that are
// awkward to observe from there: the translation's diagnostics, and the
// module-level metadata a DXIL consumer needs.
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/DXSA/DXSAToLLVMIRTranslator.h"

#include "feme/Dialect/DXSA/IR/DXSA.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include "gtest/gtest.h"

using namespace feme;

namespace {

/// A parsed `dxsa` module plus everything it borrows, so that a test can
/// keep it alive for the duration of a translation.
class Fixture {
public:
  Fixture() {
    MLIR.getOrLoadDialect<dxsa::DXSADialect>();
    MLIR.getDiagEngine().registerHandler([this](mlir::Diagnostic &Diag) {
      Diagnostics += Diag.str();
      Diagnostics += '\n';
    });
  }

  /// Parses \p Source and translates it, returning the printed LLVM IR.
  /// Returns nullopt if either step failed.
  std::optional<std::string>
  translate(llvm::StringRef Source,
            llvm::ArrayRef<dxsa::ContainerSignatureElement> Inputs = {}) {
    Parsed = mlir::parseSourceString<mlir::ModuleOp>(Source, &MLIR);
    if (!Parsed)
      return std::nullopt;
    std::unique_ptr<llvm::Module> Result =
        dxsa::translateToLLVMIR(*Parsed, LLVM, Inputs);
    if (!Result)
      return std::nullopt;
    std::string Text;
    llvm::raw_string_ostream OS(Text);
    Result->print(OS, /*AAW=*/nullptr);
    return Text;
  }

  llvm::StringRef diagnostics() const { return Diagnostics; }

private:
  mlir::MLIRContext MLIR;
  llvm::LLVMContext LLVM;
  mlir::OwningOpRef<mlir::ModuleOp> Parsed;
  std::string Diagnostics;
};

/// The number of (non-overlapping) times \p Needle appears in \p Haystack.
unsigned occurrences(llvm::StringRef Haystack, llvm::StringRef Needle) {
  unsigned Count = 0;
  for (size_t Pos = Haystack.find(Needle); Pos != llvm::StringRef::npos;
       Pos = Haystack.find(Needle, Pos + Needle.size()))
    ++Count;
  return Count;
}

constexpr llvm::StringRef PassthroughShader = R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps linear v<0, <x, y>>
  dxsa.dcl_output o<0, <x, y>>
  dxsa.mov o<0, <x, y>>, v<0, <x, y, x, x>>
  dxsa.ret
}
)mlir";

TEST(DXSAToLLVMIRTranslatorTest, RequiresExactlyOneShaderModule) {
  Fixture Empty;
  EXPECT_FALSE(Empty.translate("module { }").has_value());
  EXPECT_NE(Empty.diagnostics().find("expected a dxsa.module"),
            llvm::StringRef::npos);

  Fixture Two;
  EXPECT_FALSE(Two.translate("dxsa.module { }\ndxsa.module { }").has_value());
  EXPECT_NE(Two.diagnostics().find("expected exactly one dxsa.module"),
            llvm::StringRef::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, EmitsAnEntryPointAndModuleMetadata) {
  Fixture F;
  std::optional<std::string> IR = F.translate(PassthroughShader);
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();

  EXPECT_NE(IR->find("define void @main()"), std::string::npos);
  // A DXIL consumer keys off these named metadata nodes.
  EXPECT_NE(IR->find("!dx.version = !"), std::string::npos);
  EXPECT_NE(IR->find("!dx.shaderModel = !"), std::string::npos);
  EXPECT_NE(IR->find("!dx.entryPoints = !"), std::string::npos);
  EXPECT_NE(IR->find(R"(!{!"ps", i32 6, i32 0})"), std::string::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, ShaderModelNamesTheStage) {
  for (auto [Source, Stage] :
       {std::pair{"dxsa.module vertex_shader 5 0 { dxsa.ret }", "vs"},
        {"dxsa.module geometry_shader 5 0 { dxsa.ret }", "gs"},
        {"dxsa.module hull_shader 5 0 { dxsa.ret }", "hs"},
        {"dxsa.module domain_shader 5 0 { dxsa.ret }", "ds"},
        {"dxsa.module compute_shader 5 0 { dxsa.ret }", "cs"}}) {
    Fixture F;
    std::optional<std::string> IR = F.translate(Source);
    ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
    EXPECT_NE(
        IR->find((llvm::Twine("!{!\"") + Stage + "\", i32 6, i32 0}").str()),
        std::string::npos)
        << Source;
  }
}

TEST(DXSAToLLVMIRTranslatorTest, HeaderlessModuleDefaultsToPixelShader) {
  Fixture F;
  std::optional<std::string> IR = F.translate("dxsa.module { dxsa.ret }");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  EXPECT_NE(IR->find(R"(!{!"ps", i32 6, i32 0})"), std::string::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, SignatureElementsComeFromDeclarations) {
  Fixture F;
  std::optional<std::string> IR = F.translate(PassthroughShader);
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();

  // One element per declaration: two columns starting at column zero of
  // row zero, an arbitrary (0) input semantic and a render target (16)
  // output semantic.
  EXPECT_NE(IR->find(R"(!{i32 0, !"IN0", i8 9, i8 0,)"), std::string::npos);
  EXPECT_NE(IR->find(R"(!{i32 0, !"SV_Target", i8 9, i8 16,)"),
            std::string::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, ReportsUnsupportedInstructions) {
  Fixture F;
  // `dxsa.dfma` is a double-precision operation, which is not translated
  // yet.
  EXPECT_FALSE(F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_output o<0, <x, y>>
  dxsa.dfma o<0, <x, y>>, r<0>, r<1>, r<2>
  dxsa.ret
}
)mlir")
                   .has_value());
  EXPECT_NE(F.diagnostics().find("does not support"), llvm::StringRef::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, ResourcesAreBoundInResourceClassOrder) {
  // DXIL binds the classes in order -- SRVs, UAVs, constant buffers,
  // samplers -- whatever order the declarations appear in, and a handle
  // names its resource by the index of the declaration within its class
  // rather than by the register it binds to.
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_sampler <id = 5, mode = default>
  dxsa.dcl_resource <id = 3>, <dim = texture2d>,
      <x = float, y = float, z = float, w = float>
  dxsa.dcl_input_ps linear v<0, <x, y>>
  dxsa.dcl_output o<0>
  dxsa.sample o<0>, v<0, <x, y, x, x>>, t<3, vector>, s<5>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  EXPECT_NE(IR->find("%0 = call %dx.types.Handle @dx.op.createHandle(i32 57, "
                     "i8 0, i32 0, i32 3, i1 false)"),
            std::string::npos)
      << *IR;
  EXPECT_NE(IR->find("%1 = call %dx.types.Handle @dx.op.createHandle(i32 57, "
                     "i8 3, i32 0, i32 5, i1 false)"),
            std::string::npos)
      << *IR;
  // A two-dimensional texture leaves the last two coordinates and the
  // third texel offset undefined.
  EXPECT_NE(IR->find("@dx.op.sample.f32(i32 60,"), std::string::npos) << *IR;
  EXPECT_NE(IR->find("float undef, float undef, i32 0, i32 0, i32 undef, "
                     "float 0.000000e+00)"),
            std::string::npos)
      << *IR;
  // DXIL::ResourceKind::Texture2D is 2, ComponentType::F32 is 9.
  EXPECT_NE(IR->find(R"(!"T0", i32 0, i32 3, i32 1, i32 2, i32 0,)"),
            std::string::npos)
      << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, ReportsUndeclaredSignatureRegisters) {
  Fixture F;
  EXPECT_FALSE(F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_output o<0, <x>>
  dxsa.mov o<0, <x>>, v<3, <x>>
  dxsa.ret
}
)mlir")
                   .has_value());
  EXPECT_NE(F.diagnostics().find("undeclared input register"),
            llvm::StringRef::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, SystemValuesGetTheirDXILSemanticKind) {
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module vertex_shader 5 0 {
  dxsa.dcl_input_sgv v<0, <x>>, <vertexID>
  dxsa.dcl_output_siv o<0, <x, y, z, w>>, <position>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  // DXBC and DXIL number their system values differently: vertexID is 6 in
  // D3D10_SB_NAME and 1 in DXIL::SemanticKind, position 1 and 3.
  EXPECT_NE(IR->find(R"(!{i32 0, !"SV_VertexID", i8 9, i8 1,)"),
            std::string::npos);
  EXPECT_NE(IR->find(R"(!{i32 0, !"SV_Position", i8 9, i8 3,)"),
            std::string::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, MinimumPrecisionNarrowsToHalf) {
  // DXIL holds a `min16f` value in a `half` and records that the shader
  // uses low-precision data types in its shader flags -- which is a
  // different flag from the one asking for *native* 16-bit types.
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_global_flags <refactoringAllowed|enableMinimumPrecision>
  dxsa.dcl_input_ps linear v<0, min16f, <x>>
  dxsa.dcl_output o<0, <x>>
  dxsa.dcl_temps 1
  dxsa.add r<0, min16f, <x>>, v<0, min16f, <x>>, l(0x40000000)
  dxsa.mov o<0, <x>>, r<0, min16f, <x>>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  EXPECT_NE(IR->find("call half @dx.op.loadInput.f16"), std::string::npos)
      << *IR;
  EXPECT_NE(IR->find("fadd fast half"), std::string::npos) << *IR;
  // `mov` is a 32-bit copy, so the result is widened for the output.
  EXPECT_NE(IR->find("fpext half"), std::string::npos) << *IR;
  // 0x100 is AllResourcesBound, 0x20 LowPrecisionPresent.
  EXPECT_NE(IR->find("!{i32 0, i64 288}"), std::string::npos) << *IR;
  // DXIL::ComponentType::F16 is 8.
  EXPECT_NE(IR->find(R"(!"IN0", i8 8,)"), std::string::npos) << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, MinimumPrecisionIntegersKeepTheirSign) {
  Fixture Signed;
  std::optional<std::string> IR = Signed.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps constant v<0, min16i, <x>>
  dxsa.dcl_output o<0, <x>>
  dxsa.mov o<0, <x>>, v<0, min16i, <x>>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << Signed.diagnostics().str();
  EXPECT_NE(IR->find("sext i16"), std::string::npos) << *IR;

  Fixture Unsigned;
  IR = Unsigned.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps constant v<0, min16u, <x>>
  dxsa.dcl_output o<0, <x>>
  dxsa.mov o<0, <x>>, v<0, min16u, <x>>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << Unsigned.diagnostics().str();
  EXPECT_NE(IR->find("zext i16"), std::string::npos) << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, MinimumPrecisionTempsAreTheirOwnBank) {
  // `r0.x` read at `min16f` is not the `r0.x` a 32-bit instruction wrote,
  // so the narrow read of a register only ever written wide is undefined.
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps constant v<0, <x>>
  dxsa.dcl_output o<0, min16f, <x>>
  dxsa.dcl_temps 1
  dxsa.mov r<0, <x>>, v<0, <x>>
  dxsa.add o<0, min16f, <x>>, r<0, min16f, <x>>, r<0, min16f, <x>>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  EXPECT_NE(IR->find("fadd fast half undef, undef"), std::string::npos) << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, ThirtyTwoBitOnlyOperationsNarrowTheirResult) {
  // DXIL defines `Ubfe` only at 32 bits, so a minimum-precision
  // destination truncates what it computed rather than narrowing the
  // operation.
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps constant v<0, min16u, <x>>
  dxsa.dcl_output o<0, min16u, <x>>
  dxsa.ubfe o<0, min16u, <x>>, l(0x1B), l(0x5), v<0, min16u, <x>>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  EXPECT_NE(IR->find("call i32 @dx.op.tertiary.i32(i32 52,"), std::string::npos)
      << *IR;
  EXPECT_NE(IR->find("trunc i32"), std::string::npos) << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, IndexedInputsNameTheirRangesFirstRegister) {
  // A signature register read at a run-time row loads the element the
  // declared index range starts at, with the row taken relative to it.
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps linear v<3, <x>>
  dxsa.dcl_input_ps linear v<4, <x>>
  dxsa.dcl_input_ps constant v<9, <x>>
  dxsa.dcl_output o<0, <x>>
  dxsa.dcl_index_range v<3, <x>>, 2
  dxsa.dcl_temps 1
  dxsa.mov r<0, <x>>, v<9, <x>>
  dxsa.mov o<0, <x>>, v<3 + r<0, <x>>, <x>>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  EXPECT_NE(IR->find("sub i32"), std::string::npos) << *IR;
  EXPECT_NE(IR->find("@dx.op.loadInput.f32(i32 4, i32 0, i32 %"),
            std::string::npos)
      << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, IndexRangesCollapseSignatureElements) {
  Fixture F;
  const dxsa::ContainerSignatureElement Inputs[] = {
      {"A", 0, 0, 0x3, 0, 3}, {"B", 0, 0, 0xC, 0, 3}, {"A", 1, 1, 0x3, 0, 3},
      {"A", 2, 2, 0x3, 0, 3}, {"D", 0, 3, 0x1, 0, 2},
  };
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps linear v<0, <x, y>>
  dxsa.dcl_input_ps linear v<0, <z, w>>
  dxsa.dcl_input_ps linear v<1, <x, y>>
  dxsa.dcl_input_ps linear v<2, <x, y>>
  dxsa.dcl_input_ps constant v<3, <x>>
  dxsa.dcl_output o<0, <x>>
  dxsa.dcl_index_range v<0, <x, y>>, 3
  dxsa.dcl_temps 1
  dxsa.mov r<0, <x>>, v<3, <x>>
  dxsa.mov o<0, <x>>, v<r<0>, <x>>
  dxsa.ret
}
)mlir",
                                              Inputs);
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();

  EXPECT_NE(IR->find(R"(!{i32 0, !"A", i8 9, i8 0,)"), std::string::npos)
      << *IR;
  EXPECT_NE(IR->find("!{i32 0, i32 1, i32 2}"), std::string::npos) << *IR;
  EXPECT_NE(IR->find("i8 2, i32 3, i8 2, i32 0, i8 0, null}"),
            std::string::npos)
      << *IR;
  EXPECT_NE(IR->find(R"(!{i32 1, !"B", i8 9, i8 0,)"), std::string::npos)
      << *IR;
  EXPECT_NE(IR->find(R"(!{i32 2, !"D", i8 4, i8 0,)"), std::string::npos)
      << *IR;
  EXPECT_NE(IR->find("@dx.op.loadInput.f32(i32 4, i32 0, i32 %"),
            std::string::npos)
      << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, TexelOffsetsAreSigned) {
  // A texel offset is a four-bit two's complement number with the range
  // [-8, 7], so it has to be sign-extended into the 32-bit argument DXIL
  // passes it in.
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_sampler <id = 0, mode = default>
  dxsa.dcl_resource <id = 0>, <dim = texture2d>,
      <x = float, y = float, z = float, w = float>
  dxsa.dcl_input_ps linear v<0, <x, y>>
  dxsa.dcl_output o<0>
  dxsa.sample o<0>, v<0, <x, y, x, x>>, t<0, vector>, s<0>, <u = -5, v = 7, w = 0>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  EXPECT_NE(IR->find("i32 -5, i32 7, i32 undef"), std::string::npos) << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, EachSourceOperandIsReadSeparately) {
  // MLIR uniques attributes, so an instruction naming the same register
  // through the same swizzle twice carries one attribute for both
  // operands. DXBC still reads each operand in its own right, and dxilconv
  // emits one `loadInput` per operand.
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps linear v<0, <x>>
  dxsa.dcl_output o<0, <x>>
  dxsa.add o<0, <x>>, v<0, <x>>, v<0, <x>>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  EXPECT_EQ(occurrences(*IR, "call float @dx.op.loadInput.f32"), 2u) << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, SaturationAppliesToTheLoweredMnemonic) {
  // The `_sat` suffix names the same operation as the unsuffixed mnemonic,
  // so it must not be looked up as an opcode of its own.
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps linear v<0, <x>>
  dxsa.dcl_output o<0, <x>>
  dxsa.add_sat o<0, <x>>, v<0, <x>>, v<0, <x>>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  EXPECT_NE(IR->find("fadd fast float"), std::string::npos) << *IR;
  // DXIL::OpCode::Saturate is 7.
  EXPECT_NE(IR->find("@dx.op.unary.f32(i32 7,"), std::string::npos) << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, IndexableTempsAreFlatStackArrays) {
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps constant v<0, <x>>
  dxsa.dcl_output o<0, <x>>
  dxsa.dcl_indexable_temp x<0>[4], 2
  dxsa.mov x<[0, 1], <y>>, v<0, <x>>
  dxsa.mov o<0, <x>>, x<[0, 1], <y>>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  // Four elements of two components each, and the (1, y) slot is at
  // 1 * 2 + 1.
  EXPECT_NE(IR->find("alloca [8 x i32]"), std::string::npos) << *IR;
  EXPECT_EQ(occurrences(*IR, "i32 0, i32 3"), 2u) << *IR;
  // The array is not promoted, unlike a plain temp register.
  EXPECT_NE(IR->find("load i32"), std::string::npos) << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, DynamicIndexableTempIndicesScale) {
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps constant v<0, <x>>
  dxsa.dcl_output o<0, <x>>
  dxsa.dcl_indexable_temp x<0>[4], 4
  dxsa.mov r<0, <x>>, v<0, <x>>
  dxsa.mov o<0, <x>>, x<[0, 2 + r<0, <x>>], <x>>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  // The element index is the operand's own (register + offset), scaled by
  // the declared component count, plus the component.
  EXPECT_NE(IR->find("add i32 %1, 2"), std::string::npos) << *IR;
  EXPECT_NE(IR->find("mul i32 %2, 4"), std::string::npos) << *IR;
  EXPECT_NE(IR->find("add i32 %3, 0"), std::string::npos) << *IR;
}

TEST(DXSAToLLVMIRTranslatorTest, UndeclaredIndexableTempsAreRejected) {
  Fixture F;
  EXPECT_FALSE(F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_output o<0, <x>>
  dxsa.mov o<0, <x>>, x<[0, 0], <x>>
  dxsa.ret
}
)mlir")
                   .has_value());
  EXPECT_NE(F.diagnostics().find("undeclared indexable temp"),
            llvm::StringRef::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, UAVResourceMetadataHasTheUAVOnlyFields) {
  // Unlike an SRV's `!dx.resources` entry (9 operands), a UAV's carries
  // three extra `i1` flags after its resource kind -- globally-coherent,
  // has-counter, rasterizer-ordered -- before the trailing extended
  // properties list (see `llvm::dxil::ResourceInfo::write` in
  // llvm/lib/Analysis/DXILResource.cpp). `feme::dxil::ResourceMetadata`
  // (and any real DXIL consumer) rejects a UAV entry that's the wrong
  // (SRV-like) shape outright, so this pins down the fix for that bug.
  Fixture F;
  std::optional<std::string> IR = F.translate(R"mlir(
dxsa.module compute_shader 5 0 {
  dxsa.dcl_uav_typed <id = 0, dim = buffer>,
      <x = float, y = float, z = float, w = float>
  dxsa.dcl_input vThreadID<<x>>
  dxsa.ld_uav_typed r<0>, vThreadID<<x, x, x, x>>, u<0, vector>
  dxsa.store_uav_typed u<0, vector>, vThreadID<<x, x, x, x>>, r<0>
  dxsa.ret
}
)mlir");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  // DXIL::ResourceKind::TypedBuffer is 10; the three `i1 false` that follow
  // are the UAV-only globally-coherent/has-counter/rasterizer-ordered
  // flags this translation does not yet track off the declaration (see
  // agent_thoughts.md), so they are conservatively always false.
  EXPECT_NE(IR->find(R"(!"U0", i32 0, i32 0, i32 1, i32 10, i1 false, )"
                     R"(i1 false, i1 false, )"),
            std::string::npos)
      << *IR;
}

} // namespace
