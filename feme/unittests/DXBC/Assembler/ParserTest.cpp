//===- ParserTest.cpp - Unit tests for feme::dxbc::parseAssembly ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Parser.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme::dxbc;

namespace {

Program parseOrFail(llvm::StringRef Source) {
  llvm::Expected<Program> Parsed = parseAssembly(Source);
  if (!Parsed) {
    ADD_FAILURE() << "parseAssembly failed: "
                  << llvm::toString(Parsed.takeError());
    return {};
  }
  return std::move(*Parsed);
}

std::string parseErrorMessage(llvm::StringRef Source) {
  llvm::Expected<Program> Parsed = parseAssembly(Source);
  if (Parsed) {
    ADD_FAILURE() << "expected parseAssembly to fail on: " << Source;
    return {};
  }
  return llvm::toString(Parsed.takeError());
}

TEST(ParserTest, EmptyAndWhitespaceOnly) {
  EXPECT_TRUE(parseOrFail("").Instructions.empty());
  EXPECT_TRUE(parseOrFail("\n\n  \n").Instructions.empty());
  EXPECT_TRUE(
      parseOrFail("// just a comment\n; and another\n").Instructions.empty());
}

TEST(ParserTest, ProgramHeaderIsOptOut) {
  EXPECT_FALSE(parseOrFail("nop").HasHeader);

  Program P = parseOrFail(".shader_model compute 5 1\nnop");
  EXPECT_TRUE(P.HasHeader);
  EXPECT_EQ(P.ProgramType, 5u); // D3D11_SB_COMPUTE_SHADER
  EXPECT_EQ(P.MajorVersion, 5u);
  EXPECT_EQ(P.MinorVersion, 1u);
}

TEST(ParserTest, FxcProfileLineIsAProgramHeader) {
  // `fxc` disassembly opens with a bare profile name instead of the
  // `.shader_model` directive.
  Program P = parseOrFail("ps_5_1\nnop");
  EXPECT_TRUE(P.HasHeader);
  EXPECT_EQ(P.ProgramType, 0u); // D3D10_SB_PIXEL_SHADER
  EXPECT_EQ(P.MajorVersion, 5u);
  EXPECT_EQ(P.MinorVersion, 1u);
  EXPECT_EQ(P.Instructions.size(), 1u);

  for (auto [Text, Type] : {std::pair{"vs_4_0", 1u}, {"gs_4_1", 2u},
                            {"hs_5_0", 3u}, {"ds_5_0", 4u}, {"cs_5_0", 5u}})
    EXPECT_EQ(parseOrFail(Text).ProgramType, Type);

  // Mnemonics that merely start with a profile prefix are not headers.
  EXPECT_FALSE(parseOrFail("hs_decls").HasHeader);
  EXPECT_NE(parseErrorMessage("ps_5").find("unknown mnemonic"),
            std::string::npos);
}

/// Returns the opcode-specific control bits the single instruction in
/// \p Source parses to.
uint32_t parseControls(llvm::StringRef Source) {
  Program P = parseOrFail(Source);
  if (P.Instructions.size() != 1) {
    ADD_FAILURE() << "expected exactly one instruction in: " << Source;
    return 0;
  }
  return P.Instructions[0].Controls;
}

TEST(ParserTest, FxcKeywordSpellings) {
  // `fxc` spells enumerated control fields as trailing keywords where
  // dxbc-as folds them into the mnemonic, and names the extension global
  // flags and system values differently.
  EXPECT_EQ(parseControls("dcl_sampler s0, mode_comparison"),
            parseControls("dcl_sampler_comparison s0"));
  EXPECT_EQ(parseControls("dcl_sampler s0, mode_default"),
            parseControls("dcl_sampler s0"));
  EXPECT_EQ(parseControls("dcl_constantbuffer cb0[1], dynamicIndexed"),
            parseControls("dcl_constantbuffer_dynamicIndexed cb0[1]"));
  EXPECT_EQ(parseControls("dcl_globalFlags enable11_1DoubleExtensions"),
            parseControls("dcl_globalFlags enableDoubleExtensions"));
  EXPECT_EQ(parseControls("dcl_inputprimitive triangleadj"),
            parseControls("dcl_inputprimitive triangle_adj"));
  EXPECT_EQ(parseOrFail("dcl_output_siv o0.x, rendertarget_array_index")
                .Instructions[0]
                .ExtraDWords,
            parseOrFail("dcl_output_siv o0.x, renderTargetArrayIndex")
                .Instructions[0]
                .ExtraDWords);
}

TEST(ParserTest, FxcRegisterSpaceTrailer) {
  // SM5.1 declarations carry a register space, which `fxc` spells
  // `space=<n>` where dxbc-as spells it as a bare trailing DWORD.
  Program P = parseOrFail("dcl_uav_raw u1[1][1].xyzw{swizzle}, space=3");
  ASSERT_EQ(P.Instructions.size(), 1u);
  EXPECT_EQ(P.Instructions[0].ExtraDWords,
            (llvm::SmallVector<uint32_t, 4>{3u}));

  EXPECT_NE(parseErrorMessage("dcl_uav_raw u1, bogus").find(
                "unknown keyword 'bogus'"),
            std::string::npos);
}

TEST(ParserTest, SaturateSuffix) {
  Program P = parseOrFail("mul_sat r0.xyzw, r1.xyzw, r2.xyzw");
  ASSERT_EQ(P.Instructions.size(), 1u);
  EXPECT_TRUE(P.Instructions[0].Saturate);
  EXPECT_EQ(P.Instructions[0].Op, *lookupOpcode("mul"));

  // Integer/bitwise opcodes have no saturating form.
  EXPECT_NE(parseErrorMessage("and_sat r0.xyzw, r1.xyzw, r2.xyzw")
                .find("'_sat' is not valid"),
            std::string::npos);
}

TEST(ParserTest, DestinationMasksVersusSourceSelects) {
  Program P = parseOrFail("mov r0.x, r1.x");
  ASSERT_EQ(P.Instructions.size(), 1u);
  const Instruction &Inst = P.Instructions[0];
  ASSERT_EQ(Inst.Operands.size(), 2u);
  // The same `.x` spelling is a single-bit write mask on the destination
  // and a single-component select on the source.
  EXPECT_EQ(Inst.Operands[0].SelectMode, ComponentSelectMode::Mask);
  EXPECT_EQ(Inst.Operands[0].WriteMask, 0x1);
  EXPECT_EQ(Inst.Operands[1].SelectMode, ComponentSelectMode::Select1);
  EXPECT_EQ(Inst.Operands[1].SelectedComponent, 0);
}

TEST(ParserTest, SourceSwizzle) {
  Program P = parseOrFail("mov r0.xy, r1.yzwy");
  const Instruction &Inst = P.Instructions[0];
  EXPECT_EQ(Inst.Operands[0].WriteMask, 0x3);
  EXPECT_EQ(Inst.Operands[1].SelectMode, ComponentSelectMode::Swizzle);
  EXPECT_EQ(Inst.Operands[1].Swizzle[0], 1);
  EXPECT_EQ(Inst.Operands[1].Swizzle[1], 2);
  EXPECT_EQ(Inst.Operands[1].Swizzle[2], 3);
  EXPECT_EQ(Inst.Operands[1].Swizzle[3], 1);

  EXPECT_NE(parseErrorMessage("mov r0.xyzw, r1.xy").find("exactly one or four"),
            std::string::npos);
}

TEST(ParserTest, OperandModifiers) {
  Program P = parseOrFail("mov r0.xyzw, -|r1.xyzw|");
  const Operand &Src = P.Instructions[0].Operands[1];
  EXPECT_TRUE(Src.Negate);
  EXPECT_TRUE(Src.Abs);
}

TEST(ParserTest, MinimumPrecisionAndNonUniform) {
  Program P = parseOrFail("mov r0.xyzw{min16f}, r1.xyzw{min16u,nonuniform}");
  const Instruction &Inst = P.Instructions[0];
  EXPECT_EQ(Inst.Operands[0].Precision, MinPrecision::Float16);
  EXPECT_EQ(Inst.Operands[1].Precision, MinPrecision::UInt16);
  EXPECT_TRUE(Inst.Operands[1].NonUniform);

  EXPECT_NE(parseErrorMessage("mov r0.xyzw, r1.xyzw{bogus}")
                .find("unknown operand modifier"),
            std::string::npos);
}

TEST(ParserTest, ComponentCountOverrides) {
  Program P = parseOrFail("dcl_input vPrim{comp1}");
  EXPECT_EQ(P.Instructions[0].Operands[0].Components, ComponentCount::One);
  // Without the override vPrim is a bare handle.
  P = parseOrFail("dcl_input vPrim");
  EXPECT_EQ(P.Instructions[0].Operands[0].Components, ComponentCount::Zero);
}

TEST(ParserTest, MultiDimensionalIndices) {
  Program P = parseOrFail("mov r0.xyzw, cb1[3].xyzw");
  const Operand &Src = P.Instructions[0].Operands[1];
  EXPECT_EQ(Src.Kind, OperandKind::ConstantBuffer);
  ASSERT_EQ(Src.Indices.size(), 2u);
  EXPECT_EQ(Src.Indices[0].Value, 1u);
  EXPECT_EQ(Src.Indices[1].Value, 3u);
  EXPECT_EQ(Src.Indices[1].Rep, OperandIndex::Representation::Immediate32);
}

TEST(ParserTest, RelativeAddressing) {
  Program P = parseOrFail("mov r0.xyzw, cb1[3 + r2.x].xyzw");
  const Operand &Src = P.Instructions[0].Operands[1];
  ASSERT_EQ(Src.Indices.size(), 2u);
  EXPECT_EQ(Src.Indices[1].Rep,
            OperandIndex::Representation::Immediate32PlusRelative);
  EXPECT_EQ(Src.Indices[1].Value, 3u);
  ASSERT_NE(Src.Indices[1].Relative, nullptr);
  EXPECT_EQ(Src.Indices[1].Relative->Kind, OperandKind::Temp);
  EXPECT_EQ(Src.Indices[1].Relative->SelectMode, ComponentSelectMode::Select1);

  // Purely relative indices carry no immediate part.
  P = parseOrFail("mov r0.xyzw, icb[r2.x].xyzw");
  const Operand &Icb = P.Instructions[0].Operands[1];
  ASSERT_EQ(Icb.Indices.size(), 1u);
  EXPECT_EQ(Icb.Indices[0].Rep, OperandIndex::Representation::Relative);
}

TEST(ParserTest, Immediates) {
  Program P = parseOrFail("mov r0.xyzw, l(1.0, 0, 0x3F800000, -1)");
  const Operand &Imm = P.Instructions[0].Operands[1];
  EXPECT_EQ(Imm.Kind, OperandKind::Immediate32);
  EXPECT_EQ(Imm.Components, ComponentCount::Four);
  ASSERT_EQ(Imm.ImmediateValues.size(), 4u);
  // A literal spelled with a '.' is a float32 bit pattern; an integer
  // literal keeps its integer value.
  EXPECT_EQ(Imm.ImmediateValues[0], 0x3F800000u);
  EXPECT_EQ(Imm.ImmediateValues[1], 0u);
  EXPECT_EQ(Imm.ImmediateValues[2], 0x3F800000u);
  EXPECT_EQ(Imm.ImmediateValues[3], 0xFFFFFFFFu);

  P = parseOrFail("mov r0.xyzw, l(42)");
  EXPECT_EQ(P.Instructions[0].Operands[1].Components, ComponentCount::One);

  EXPECT_NE(parseErrorMessage("mov r0.xyzw, l(1, 2, 3)").find("exactly 1 or 4"),
            std::string::npos);
}

TEST(ParserTest, DoubleImmediates) {
  Program P = parseOrFail("dmov r0.xyzw, d(0x3FF0000000000000)");
  const Operand &Imm = P.Instructions[0].Operands[1];
  EXPECT_EQ(Imm.Kind, OperandKind::Immediate64);
  ASSERT_EQ(Imm.ImmediateValues.size(), 2u);
  EXPECT_EQ(Imm.ImmediateValues[0], 0x3FF00000u);
  EXPECT_EQ(Imm.ImmediateValues[1], 0u);
}

TEST(ParserTest, InstructionModifiers) {
  Program P = parseOrFail(
      "sample precise(xy) aoffimmi(-5, 7, 0) r0.xyzw, v0.xyxx, t3.xyzw, s5.x");
  const Instruction &Inst = P.Instructions[0];
  EXPECT_EQ(Inst.PreciseMask, 0x3);
  EXPECT_TRUE(Inst.HasSampleOffsets);
  EXPECT_EQ(Inst.SampleOffsets[0], -5);
  EXPECT_EQ(Inst.SampleOffsets[1], 7);
  EXPECT_EQ(Inst.SampleOffsets[2], 0);

  EXPECT_NE(parseErrorMessage("dcl_temps precise(x) 4")
                .find("'precise' is not valid"),
            std::string::npos);
  EXPECT_NE(parseErrorMessage("sample aoffimmi(-9, 0, 0) r0.xyzw, v0.xyzw, "
                              "t0.xyzw, s0.x")
                .find("[-8, 7]"),
            std::string::npos);
}

TEST(ParserTest, ExtendedResourceOpcodeTokens) {
  Program P = parseOrFail("bufinfo resource_dim(structured_buffer, 52) "
                          "resource_return_type(uint, uint, uint, uint) "
                          "r0.x, t0.xyzw");
  const Instruction &Inst = P.Instructions[0];
  EXPECT_TRUE(Inst.HasResourceDim);
  EXPECT_EQ(Inst.ResourceDim, 12); // D3D11_SB_RESOURCE_DIMENSION_STRUCTURED
  EXPECT_EQ(Inst.ResourceStride, 52);
  EXPECT_TRUE(Inst.HasResourceReturnType);
  EXPECT_EQ(Inst.ResourceReturnTypes[0], 4); // D3D10_SB_RETURN_TYPE_UINT

  EXPECT_NE(parseErrorMessage("bufinfo resource_dim(structured_buffer, 4096) "
                              "r0.x, t0.xyzw")
                .find("structure stride must fit"),
            std::string::npos);
}

TEST(ParserTest, FlagLists) {
  Program P = parseOrFail("dcl_globalFlags refactoringAllowed | "
                          "skipOptimization");
  EXPECT_EQ(P.Instructions[0].Controls, (1u << 11) | (1u << 15));

  P = parseOrFail("sync uav_global | threads");
  EXPECT_EQ(P.Instructions[0].Controls, (1u << 14) | (1u << 11));

  EXPECT_NE(parseErrorMessage("dcl_globalFlags nope").find("unknown flag"),
            std::string::npos);
}

TEST(ParserTest, EnumeratedControlFields) {
  Program P = parseOrFail("dcl_inputprimitive triangle");
  EXPECT_EQ(P.Instructions[0].Controls, 3u << 11);

  // `patch<N>` covers the 32 contiguous control-point-patch values.
  P = parseOrFail("dcl_inputprimitive patch3");
  EXPECT_EQ(P.Instructions[0].Controls, 10u << 11);
  EXPECT_NE(parseErrorMessage("dcl_inputprimitive patch33").find("patch<N>"),
            std::string::npos);

  P = parseOrFail("dcl_input_control_point_count 4");
  EXPECT_EQ(P.Instructions[0].Controls, 4u << 11);
  // A wider count would shift out of the control range and corrupt the
  // instruction length field above it.
  EXPECT_NE(parseErrorMessage("dcl_input_control_point_count 10000")
                .find("13-bit control field"),
            std::string::npos);
}

TEST(ParserTest, DeclarationsWithTrailingValues) {
  Program P = parseOrFail("dcl_temps 4");
  EXPECT_EQ(P.Instructions[0].ExtraDWords, llvm::SmallVector<uint32_t>({4}));

  P = parseOrFail("dcl_thread_group 8, 8, 1");
  EXPECT_EQ(P.Instructions[0].ExtraDWords,
            llvm::SmallVector<uint32_t>({8, 8, 1}));

  P = parseOrFail("dcl_indexableTemp x1[23], 2");
  EXPECT_EQ(P.Instructions[0].ExtraDWords,
            llvm::SmallVector<uint32_t>({1, 23, 2}));

  P = parseOrFail("dcl_tgsm_structured g0, 16, 64");
  EXPECT_EQ(P.Instructions[0].Operands[0].Kind,
            OperandKind::ThreadGroupSharedMemory);
  EXPECT_EQ(P.Instructions[0].ExtraDWords,
            llvm::SmallVector<uint32_t>({16, 64}));
}

TEST(ParserTest, SystemValueDeclarations) {
  Program P = parseOrFail("dcl_input_siv v1.xyz, clipDistance");
  EXPECT_EQ(P.Instructions[0].ExtraDWords,
            llvm::SmallVector<uint32_t>({2})); // D3D10_SB_NAME_CLIP_DISTANCE

  P = parseOrFail("dcl_input_ps_siv linear v0.xy, position");
  EXPECT_EQ(P.Instructions[0].Controls, 2u << 11); // LINEAR
  EXPECT_EQ(P.Instructions[0].ExtraDWords, llvm::SmallVector<uint32_t>({1}));

  EXPECT_NE(parseErrorMessage("dcl_input_siv v1.xyz, nope")
                .find("unknown system-value name"),
            std::string::npos);
}

TEST(ParserTest, TypedResourceDeclarations) {
  Program P = parseOrFail("dcl_resource_buffer (unorm, snorm, sint, uint) t0");
  const Instruction &Inst = P.Instructions[0];
  EXPECT_EQ(Inst.ResourceReturnTypes[0], 1);
  EXPECT_EQ(Inst.ResourceReturnTypes[3], 4);
  EXPECT_EQ(Inst.Operands[0].Kind, OperandKind::Resource);

  // Multisampled dimensions take their sample count as a mnemonic suffix.
  P = parseOrFail(
      "dcl_resource_texture2dms(4) (float, float, float, float) t5");
  EXPECT_EQ(P.Instructions[0].Controls, (4u << 11) | (4u << 16));

  EXPECT_NE(parseErrorMessage("dcl_resource_texture2dms(200) (float, float, "
                              "float, float) t5")
                .find("sample count must fit"),
            std::string::npos);

  // UAV declarations additionally accept access-flag keywords.
  P = parseOrFail("dcl_uav_typed_texture2d globallyCoherent "
                  "(float, float, float, float) u6");
  EXPECT_EQ(P.Instructions[0].Controls, (3u << 11) | 0x00010000u);
}

TEST(ParserTest, TrailingDWordsOnGenericInstructions) {
  // Some real shaders carry DWORDs past an instruction's operands that the
  // tokenized format does not describe.
  Program P = parseOrFail("samplepos r0.xy, t0.xyzw, r0.x, 0");
  ASSERT_EQ(P.Instructions[0].Operands.size(), 3u);
  EXPECT_EQ(P.Instructions[0].ExtraDWords, llvm::SmallVector<uint32_t>({0}));

  P = parseOrFail("ret, 5");
  EXPECT_TRUE(P.Instructions[0].Operands.empty());
  EXPECT_EQ(P.Instructions[0].ExtraDWords, llvm::SmallVector<uint32_t>({5}));
}

TEST(ParserTest, RawDWordDirective) {
  Program P = parseOrFail(".dword 0x030007FF, 0xDEADBEEF, 0x12345678");
  ASSERT_EQ(P.Instructions.size(), 1u);
  EXPECT_EQ(P.Instructions[0].Op, Opcode::RawDWords);
  EXPECT_EQ(P.Instructions[0].ExtraDWords,
            llvm::SmallVector<uint32_t>({0x030007FF, 0xDEADBEEF, 0x12345678}));

  EXPECT_NE(parseErrorMessage(".nope 1").find("unknown directive"),
            std::string::npos);
}

TEST(ParserTest, Diagnostics) {
  EXPECT_NE(parseErrorMessage("bogus r0.xyzw").find("unknown mnemonic"),
            std::string::npos);
  EXPECT_NE(parseErrorMessage("mov q0.xyzw, r1.xyzw")
                .find("unknown operand storage class"),
            std::string::npos);
  EXPECT_NE(parseErrorMessage("mov r0.xyzw r1.xyzw").find("expected ','"),
            std::string::npos);
  EXPECT_NE(parseErrorMessage("mov r0.xq, r1.xyzw")
                .find("invalid swizzle/mask component"),
            std::string::npos);
  // Diagnostics carry a line/column prefix.
  EXPECT_EQ(parseErrorMessage("nop\nbogus").substr(0, 4), "2:1:");
}

} // namespace
