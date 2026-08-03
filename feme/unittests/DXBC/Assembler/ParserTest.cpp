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

#include <cstring>

using namespace feme::dxbc;

namespace {

std::vector<Instruction> parseOrFail(llvm::StringRef Source) {
  llvm::Expected<std::vector<Instruction>> Program = parseAssembly(Source);
  if (!Program) {
    ADD_FAILURE() << "parseAssembly failed: "
                  << llvm::toString(Program.takeError());
    return {};
  }
  return std::move(*Program);
}

std::string parseErrorMessage(llvm::StringRef Source) {
  llvm::Expected<std::vector<Instruction>> Program = parseAssembly(Source);
  if (Program) {
    ADD_FAILURE() << "expected parseAssembly to fail on: " << Source;
    return {};
  }
  return llvm::toString(Program.takeError());
}

TEST(ParserTest, EmptyAndWhitespaceOnly) {
  EXPECT_TRUE(parseOrFail("").empty());
  EXPECT_TRUE(parseOrFail("\n\n  \n").empty());
}

TEST(ParserTest, NoOperandInstruction) {
  std::vector<Instruction> P = parseOrFail("ret");
  ASSERT_EQ(P.size(), 1u);
  EXPECT_EQ(P[0].Op, Opcode::Ret);
  EXPECT_TRUE(P[0].Operands.empty());
}

TEST(ParserTest, ALU2WithDestinationMaskAndSourceSwizzle) {
  std::vector<Instruction> P = parseOrFail("add r0.xy, r1.zwxy, r2");
  ASSERT_EQ(P.size(), 1u);
  const Instruction &I = P[0];
  EXPECT_EQ(I.Op, Opcode::Add);
  ASSERT_EQ(I.Operands.size(), 3u);

  const Operand &Dest = I.Operands[0];
  EXPECT_EQ(Dest.Kind, OperandKind::Temp);
  EXPECT_EQ(Dest.RegisterIndex, 0u);
  EXPECT_EQ(Dest.SelectMode, ComponentSelectMode::Mask);
  EXPECT_EQ(Dest.WriteMask, 0x3); // x | y

  const Operand &Src0 = I.Operands[1];
  EXPECT_EQ(Src0.RegisterIndex, 1u);
  EXPECT_EQ(Src0.SelectMode, ComponentSelectMode::Swizzle);
  EXPECT_EQ(Src0.Swizzle[0], 2); // z
  EXPECT_EQ(Src0.Swizzle[1], 3); // w
  EXPECT_EQ(Src0.Swizzle[2], 0); // x
  EXPECT_EQ(Src0.Swizzle[3], 1); // y

  const Operand &Src1 = I.Operands[2];
  EXPECT_EQ(Src1.SelectMode, ComponentSelectMode::None);
}

TEST(ParserTest, SaturateSuffix) {
  std::vector<Instruction> P = parseOrFail("mul_sat r0, r1, r2");
  ASSERT_EQ(P.size(), 1u);
  EXPECT_EQ(P[0].Op, Opcode::Mul);
  EXPECT_TRUE(P[0].Saturate);
}

TEST(ParserTest, SaturateRejectedOnIntegerOpcode) {
  std::string Msg = parseErrorMessage("and_sat r0, r1, r2");
  EXPECT_NE(Msg.find("'_sat'"), std::string::npos);
}

TEST(ParserTest, NegateAndAbsoluteValueModifiers) {
  std::vector<Instruction> P = parseOrFail("mov r0, -|r1|");
  ASSERT_EQ(P.size(), 1u);
  const Operand &Src = P[0].Operands[1];
  EXPECT_TRUE(Src.Negate);
  EXPECT_TRUE(Src.Abs);
}

TEST(ParserTest, ScalarImmediate) {
  std::vector<Instruction> P = parseOrFail("mov r0, l(1.5)");
  ASSERT_EQ(P.size(), 1u);
  const Operand &Imm = P[0].Operands[1];
  EXPECT_EQ(Imm.Kind, OperandKind::Immediate32);
  ASSERT_EQ(Imm.ImmediateValues.size(), 1u);
  float F;
  uint32_t Bits = Imm.ImmediateValues[0];
  memcpy(&F, &Bits, sizeof(F));
  EXPECT_FLOAT_EQ(F, 1.5f);
}

TEST(ParserTest, VectorImmediateWithNegatedComponent) {
  std::vector<Instruction> P = parseOrFail("mov r0, l(1.0, -2.0, 3.0, 4.0)");
  const Operand &Imm = P[0].Operands[1];
  ASSERT_EQ(Imm.ImmediateValues.size(), 4u);
  float F1;
  uint32_t Bits = Imm.ImmediateValues[1];
  memcpy(&F1, &Bits, sizeof(F1));
  EXPECT_FLOAT_EQ(F1, -2.0f);
}

TEST(ParserTest, ImmediateWithWrongComponentCountFails) {
  std::string Msg = parseErrorMessage("mov r0, l(1.0, 2.0)");
  EXPECT_NE(Msg.find("1 or 4 components"), std::string::npos);
}

TEST(ParserTest, SampleInstruction) {
  std::vector<Instruction> P =
      parseOrFail("sample r0.xyzw, v1.xyxx, t0.xyzw, s0");
  ASSERT_EQ(P.size(), 1u);
  const Instruction &I = P[0];
  EXPECT_EQ(I.Op, Opcode::Sample);
  ASSERT_EQ(I.Operands.size(), 4u);
  EXPECT_EQ(I.Operands[2].Kind, OperandKind::Resource);
  EXPECT_EQ(I.Operands[3].Kind, OperandKind::Sampler);
}

TEST(ParserTest, DiscardMnemonicsSelectTestBoolean) {
  std::vector<Instruction> Z = parseOrFail("discard_z r0.x");
  std::vector<Instruction> NZ = parseOrFail("discard_nz r0.x");
  EXPECT_EQ(Z[0].Op, Opcode::DiscardZ);
  EXPECT_EQ(NZ[0].Op, Opcode::DiscardNZ);
}

TEST(ParserTest, DclGlobalFlags) {
  std::vector<Instruction> P = parseOrFail(
      "dcl_globalFlags refactoringAllowed | enableRawAndStructuredBuffers");
  ASSERT_EQ(P.size(), 1u);
  EXPECT_EQ(P[0].Op, Opcode::DclGlobalFlags);
  ASSERT_EQ(P[0].Keywords.size(), 2u);
  EXPECT_EQ(P[0].Keywords[0], "refactoringAllowed");
  EXPECT_EQ(P[0].Keywords[1], "enableRawAndStructuredBuffers");
}

TEST(ParserTest, DclTemps) {
  std::vector<Instruction> P = parseOrFail("dcl_temps 4");
  ASSERT_EQ(P.size(), 1u);
  ASSERT_EQ(P[0].Immediates.size(), 1u);
  EXPECT_EQ(P[0].Immediates[0], 4u);
}

TEST(ParserTest, DclResourceTexture2D) {
  std::vector<Instruction> P =
      parseOrFail("dcl_resource_texture2d (float,float,float,float) t0");
  ASSERT_EQ(P.size(), 1u);
  EXPECT_EQ(P[0].Op, Opcode::DclResourceTexture2D);
  ASSERT_EQ(P[0].Keywords.size(), 4u);
  for (const std::string &Ty : P[0].Keywords)
    EXPECT_EQ(Ty, "float");
  ASSERT_EQ(P[0].Operands.size(), 1u);
  EXPECT_EQ(P[0].Operands[0].Kind, OperandKind::Resource);
  EXPECT_EQ(P[0].Operands[0].RegisterIndex, 0u);
}

TEST(ParserTest, DclSamplerWithComparisonKeyword) {
  std::vector<Instruction> P = parseOrFail("dcl_sampler s0 comparison");
  ASSERT_EQ(P.size(), 1u);
  ASSERT_EQ(P[0].Keywords.size(), 1u);
  EXPECT_EQ(P[0].Keywords[0], "comparison");
}

TEST(ParserTest, DclInputPSWithInterpolationMode) {
  std::vector<Instruction> P = parseOrFail("dcl_input_ps linear v1.xyzw");
  ASSERT_EQ(P.size(), 1u);
  ASSERT_EQ(P[0].Keywords.size(), 1u);
  EXPECT_EQ(P[0].Keywords[0], "linear");
  EXPECT_EQ(P[0].Operands[0].RegisterIndex, 1u);
}

TEST(ParserTest, MultipleInstructionsOnePerLine) {
  std::vector<Instruction> P = parseOrFail("mov r0, r1\nadd r0, r0, r1\nret");
  ASSERT_EQ(P.size(), 3u);
  EXPECT_EQ(P[0].Op, Opcode::Mov);
  EXPECT_EQ(P[1].Op, Opcode::Add);
  EXPECT_EQ(P[2].Op, Opcode::Ret);
}

TEST(ParserTest, UnknownMnemonicFails) {
  std::string Msg = parseErrorMessage("frobnicate r0, r1");
  EXPECT_NE(Msg.find("unknown mnemonic"), std::string::npos);
}

TEST(ParserTest, WrongOperandCountFails) {
  std::string Msg = parseErrorMessage("add r0, r1");
  EXPECT_NE(Msg.find("error"), std::string::npos);
}

TEST(ParserTest, TrailingGarbageOnLineFails) {
  std::string Msg = parseErrorMessage("ret garbage");
  EXPECT_NE(Msg.find("expected end of line"), std::string::npos);
}

TEST(ParserTest, MalformedRegisterFails) {
  std::string Msg = parseErrorMessage("mov r, r1");
  EXPECT_NE(Msg.find("error"), std::string::npos);
}

TEST(ParserTest, InvalidSwizzleComponentFails) {
  std::string Msg = parseErrorMessage("mov r0.q, r1");
  EXPECT_NE(Msg.find("invalid swizzle"), std::string::npos);
}

} // namespace
