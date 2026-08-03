//===- EncoderTest.cpp - Unit tests for feme::dxbc::Encoder --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Encoder.h"
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

llvm::SmallVector<uint32_t, 64>
encodeOrFail(llvm::StringRef Source, ShaderKind Kind = ShaderKind::Pixel) {
  std::vector<Instruction> Program = parseOrFail(Source);
  llvm::Expected<llvm::SmallVector<uint32_t, 64>> Bytecode =
      encodeProgram(Program, Kind);
  if (!Bytecode) {
    ADD_FAILURE() << "encodeProgram failed: "
                  << llvm::toString(Bytecode.takeError());
    return {};
  }
  return std::move(*Bytecode);
}

TEST(EncoderTest, VersionAndLengthTokens) {
  llvm::SmallVector<uint32_t, 64> Bytecode = encodeOrFail("ret");
  ASSERT_GE(Bytecode.size(), 3u);
  // [31:16] program type (0 == pixel), [15:08] major (5), [07:00] minor (0).
  EXPECT_EQ(Bytecode[0], (0u << 16) | (5u << 4) | 0u);
  // Length token counts every DWORD, including itself and the version
  // token.
  EXPECT_EQ(Bytecode[1], Bytecode.size());
}

TEST(EncoderTest, VersionTokenReflectsShaderKind) {
  llvm::SmallVector<uint32_t, 64> Bytecode =
      encodeOrFail("ret", ShaderKind::Vertex);
  EXPECT_EQ((Bytecode[0] >> 16) & 0xffff,
            static_cast<uint32_t>(ShaderKind::Vertex));
}

TEST(EncoderTest, NoOperandInstructionIsOneDWord) {
  llvm::SmallVector<uint32_t, 64> Bytecode = encodeOrFail("ret");
  // version + length + 1 opcode token.
  EXPECT_EQ(Bytecode.size(), 3u);
  uint32_t RetToken = Bytecode[2];
  EXPECT_EQ(RetToken & 0x7ff, getOpcodeInfo(Opcode::Ret).RealOpcodeValue);
  EXPECT_EQ((RetToken >> 24) & 0x7f, 1u); // instruction length
}

TEST(EncoderTest, SaturateBitIsSet) {
  llvm::SmallVector<uint32_t, 64> Bytecode = encodeOrFail("mov_sat r0, r1");
  uint32_t OpcodeToken = Bytecode[2];
  EXPECT_NE(OpcodeToken & 0x00002000, 0u);
}

TEST(EncoderTest, DestinationOperandUsesMaskMode) {
  llvm::SmallVector<uint32_t, 64> Bytecode = encodeOrFail("mov r0.xy, r1");
  uint32_t DestToken = Bytecode[3]; // opcode token, then dest operand token
  EXPECT_EQ(DestToken & 0x3, 2u);   // 4-component
  EXPECT_EQ((DestToken >> 2) & 0x3, 0u);   // mask mode
  EXPECT_EQ((DestToken >> 4) & 0xf, 0x3u); // x | y
}

TEST(EncoderTest, NegateAndAbsSetExtendedOperandToken) {
  llvm::SmallVector<uint32_t, 64> Bytecode = encodeOrFail("mov r0, -|r1|");
  // opcode token, dest operand token+index, then source operand token.
  uint32_t SrcToken0 = Bytecode[5];
  EXPECT_NE(SrcToken0 & 0x80000000u, 0u); // extended operand bit
  uint32_t SrcToken1 = Bytecode[6];
  EXPECT_EQ(SrcToken1 & 0x3f, 1u);        // D3D10_SB_EXTENDED_OPERAND_MODIFIER
  EXPECT_EQ((SrcToken1 >> 6) & 0xff, 3u); // NEG(1) | ABS(2) == ABSNEG(3)
}

TEST(EncoderTest, ScalarImmediateEncodesOneComponent) {
  llvm::SmallVector<uint32_t, 64> Bytecode = encodeOrFail("mov r0, l(1.5)");
  // opcode token, dest operand token, dest index, immediate operand token,
  // then the single immediate value DWORD.
  uint32_t ImmToken = Bytecode[5];
  EXPECT_EQ(ImmToken & 0x3, 1u);          // 1-component
  EXPECT_EQ((ImmToken >> 12) & 0xff, 4u); // OPERAND_TYPE_IMMEDIATE32
  float F;
  uint32_t Bits = Bytecode[6];
  memcpy(&F, &Bits, sizeof(F));
  EXPECT_FLOAT_EQ(F, 1.5f);
}

TEST(EncoderTest, DclTempsEncodesRawCountDWord) {
  llvm::SmallVector<uint32_t, 64> Bytecode = encodeOrFail("dcl_temps 7");
  ASSERT_EQ(Bytecode.size(), 4u); // version + length + opcode + count
  EXPECT_EQ(Bytecode[3], 7u);
  EXPECT_EQ((Bytecode[2] >> 24) & 0x7f, 2u); // instruction length
}

TEST(EncoderTest, DclResourceEncodesDimensionAndReturnTypes) {
  llvm::SmallVector<uint32_t, 64> Bytecode =
      encodeOrFail("dcl_resource_texture2d (float,float,float,float) t0");
  uint32_t OpcodeToken = Bytecode[2];
  EXPECT_EQ((OpcodeToken >> 11) & 0x1f, 3u); // TEXTURE2D
  uint32_t ReturnTypes = Bytecode.back();
  for (unsigned I = 0; I < 4; ++I)
    EXPECT_EQ((ReturnTypes >> (4 * I)) & 0xf, 5u); // FLOAT
}

TEST(EncoderTest, DiscardEncodesTestBoolean) {
  llvm::SmallVector<uint32_t, 64> NZ = encodeOrFail("discard_nz r0.x");
  llvm::SmallVector<uint32_t, 64> Z = encodeOrFail("discard_z r0.x");
  EXPECT_NE(NZ[2] & 0x00040000, 0u);
  EXPECT_EQ(Z[2] & 0x00040000, 0u);
}

TEST(EncoderTest, ContainerHasDXBCMagicAndSHEXPart) {
  llvm::SmallVector<uint32_t, 64> Bytecode = encodeOrFail("ret");
  llvm::SmallVector<char, 256> Container;
  wrapInContainer(Bytecode, ShaderKind::Pixel, Container);

  ASSERT_GE(Container.size(), 32u);
  EXPECT_EQ(llvm::StringRef(Container.data(), 4), "DXBC");
  // PartCount (offset 28, 4 bytes, little endian) should be 1.
  uint32_t PartCount;
  memcpy(&PartCount, Container.data() + 28, sizeof(PartCount));
  EXPECT_EQ(PartCount, 1u);
  // The single part's name (right after Header + PartOffset[0]) is "SHEX".
  uint32_t PartOffset;
  memcpy(&PartOffset, Container.data() + 32, sizeof(PartOffset));
  EXPECT_EQ(llvm::StringRef(Container.data() + PartOffset, 4), "SHEX");
}

} // namespace
