//===- EncoderTest.cpp - Unit tests for feme::dxbc::encodeProgram --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The expected token values in these tests are the ones a real DXBC shader
// carries, taken from `fxc`-produced bytecode, so they double as a check
// that dxbc-as agrees with the tokenized format rather than only with
// itself.
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Encoder.h"

#include "feme/DXBC/Assembler/AsmPrinter.h"
#include "feme/DXBC/Assembler/Parser.h"
#include "feme/DXBC/Assembler/SignatureComments.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace feme::dxbc;

namespace {

std::vector<uint32_t> assemble(llvm::StringRef Source) {
  llvm::Expected<Program> Parsed = parseAssembly(Source);
  if (!Parsed) {
    ADD_FAILURE() << "parseAssembly failed: "
                  << llvm::toString(Parsed.takeError());
    return {};
  }
  llvm::Expected<llvm::SmallVector<uint32_t, 64>> Bytecode =
      encodeProgram(*Parsed);
  if (!Bytecode) {
    ADD_FAILURE() << "encodeProgram failed: "
                  << llvm::toString(Bytecode.takeError());
    return {};
  }
  return std::vector<uint32_t>(Bytecode->begin(), Bytecode->end());
}

using Tokens = std::vector<uint32_t>;

TEST(EncoderTest, ProgramHeader) {
  EXPECT_EQ(assemble("nop"), Tokens({0x0100003a}));
  EXPECT_EQ(assemble(".shader_model pixel 5 0\nnop"),
            Tokens({0x00000050, 0x00000003, 0x0100003a}));
  EXPECT_EQ(assemble(".shader_model compute 5 1\nnop"),
            Tokens({0x00050051, 0x00000003, 0x0100003a}));
}

TEST(EncoderTest, AluInstructionsAndOperandModifiers) {
  // mov r0.x, -|r1.yzwy|
  EXPECT_EQ(assemble("mov r0.x, -|r1.yzwy|"),
            Tokens({0x06000036, 0x00100012, 0x00000000, 0x80100796, 0x000000c1,
                    0x00000001}));
  // The saturate bit and the precise-component mask both live in the
  // opcode token.
  EXPECT_EQ(
      assemble("mov_sat precise(xy) r0.xyzw, r1.xyzw"),
      Tokens({0x05182036, 0x001000f2, 0x00000000, 0x00100e46, 0x00000001}));
}

TEST(EncoderTest, ImmediateOperands) {
  // Scalar immediates encode as one-component operands, vector immediates
  // as four-component ones.
  EXPECT_EQ(assemble("case l(0x2A)"),
            Tokens({0x03000006, 0x00004001, 0x0000002a}));
  EXPECT_EQ(assemble("mov r0.xyzw, l(1.0, 2.0, 3.0, 4.0)"),
            Tokens({0x08000036, 0x001000f2, 0x00000000, 0x00004002, 0x3f800000,
                    0x40000000, 0x40400000, 0x40800000}));
}

TEST(EncoderTest, MultiDimensionalAndRelativeIndices) {
  // cb1[3] is a two-dimensional, all-immediate index...
  EXPECT_EQ(assemble("mov r0.xyzw, cb1[3].xyzw"),
            Tokens({0x06000036, 0x001000f2, 0x00000000, 0x00208e46, 0x00000001,
                    0x00000003}));
  // ...while cb1[3 + r2.x] makes the second dimension immediate-plus-
  // relative, which appends the relative operand's own tokens.
  EXPECT_EQ(assemble("mov r0.xyzw, cb1[3 + r2.x].xyzw"),
            Tokens({0x08000036, 0x001000f2, 0x00000000, 0x06208e46, 0x00000001,
                    0x00000003, 0x0010000a, 0x00000002}));
}

TEST(EncoderTest, TextureInstructionsAndExtendedOpcodeTokens) {
  EXPECT_EQ(assemble("sample r0.xyzw, v0.xyxx, t3.xyzw, s5.x"),
            Tokens({0x09000045, 0x001000f2, 0x00000000, 0x00101046, 0x00000000,
                    0x00107e46, 0x00000003, 0x0010600a, 0x00000005}));
  // An `aoffimmi` modifier prepends an extended opcode token and sets bit
  // 31 of the opcode token.
  EXPECT_EQ(
      assemble("sample aoffimmi(-5, 7, 0) r1.xyzw, v0.xyxx, t3.xyzw, "
               "s5.x"),
      Tokens({0x8a000045, 0x0000f601, 0x001000f2, 0x00000001, 0x00101046,
              0x00000000, 0x00107e46, 0x00000003, 0x0010600a, 0x00000005}));
}

TEST(EncoderTest, Declarations) {
  EXPECT_EQ(assemble("dcl_globalFlags refactoringAllowed"),
            Tokens({0x0100086a}));
  EXPECT_EQ(assemble("dcl_temps 4"), Tokens({0x02000068, 0x00000004}));
  EXPECT_EQ(assemble("dcl_indexableTemp x1[23], 2"),
            Tokens({0x04000069, 0x00000001, 0x00000017, 0x00000002}));
  EXPECT_EQ(assemble("dcl_input_ps linear v0.xy"),
            Tokens({0x03001062, 0x00101032, 0x00000000}));
  EXPECT_EQ(assemble("dcl_resource_buffer (unorm, snorm, sint, uint) t0"),
            Tokens({0x04000858, 0x00107000, 0x00000000, 0x00004321}));
  EXPECT_EQ(assemble("dcl_constantbuffer cb0[1]"),
            Tokens({0x04000059, 0x00208e46, 0x00000000, 0x00000001}));
  EXPECT_EQ(
      assemble("dcl_tgsm_structured g0, 16, 64"),
      Tokens({0x050000a0, 0x0011f000, 0x00000000, 0x00000010, 0x00000040}));
  EXPECT_EQ(assemble("sync uav_global | threads"), Tokens({0x010048be}));
}

TEST(EncoderTest, ImmediateConstantBufferUsesCustomData) {
  // CUSTOMDATA instructions carry their length in the token after the
  // opcode token rather than in the opcode token's length field.
  EXPECT_EQ(assemble("dcl_immediateConstantBuffer {0x3F800000, 0x00000000, "
                     "0x00000000, 0x3F800000}"),
            Tokens({0x00001835, 0x00000006, 0x3f800000, 0x00000000, 0x00000000,
                    0x3f800000}));
}

TEST(EncoderTest, RawDWordsAreEmittedVerbatim) {
  EXPECT_EQ(assemble(".dword 0x030007FF, 0xDEADBEEF, 0x12345678"),
            Tokens({0x030007FF, 0xDEADBEEF, 0x12345678}));
}

TEST(EncoderTest, OverlongInstructionIsRejected) {
  // The instruction length field is 7 bits wide, so a single instruction
  // cannot exceed 127 DWORDs.
  std::string Source = ".dword 1\ndcl_temps";
  for (unsigned I = 0; I < 200; ++I)
    Source += (I ? ", 1" : " 1");
  llvm::Expected<Program> Parsed = parseAssembly(Source);
  ASSERT_TRUE(static_cast<bool>(Parsed));
  llvm::Expected<llvm::SmallVector<uint32_t, 64>> Bytecode =
      encodeProgram(*Parsed);
  ASSERT_FALSE(static_cast<bool>(Bytecode));
  EXPECT_NE(llvm::toString(Bytecode.takeError()).find("length limit"),
            std::string::npos);
}

TEST(EncoderTest, ContainerWrapping) {
  llvm::SmallVector<uint32_t, 4> Bytecode = {0x01000058};
  llvm::SmallVector<char, 64> Container;
  wrapInContainer(Bytecode, Signatures(), Container);
  ASSERT_GT(Container.size(), 4u);
  llvm::StringRef Bytes(Container.data(), Container.size());
  EXPECT_EQ(Bytes.take_front(4), "DXBC");
  EXPECT_NE(Bytes.find("SHEX"), llvm::StringRef::npos);
  // A shader whose disassembly printed no signature tables gets no
  // signature parts.
  EXPECT_EQ(Bytes.find("ISGN"), llvm::StringRef::npos);
  EXPECT_EQ(Bytes.find("OSGN"), llvm::StringRef::npos);
}

TEST(EncoderTest, ContainerCarriesTheSignatureParts) {
  llvm::SmallVector<uint32_t, 4> Bytecode = {0x01000058};
  Signatures Sig;
  Sig.SeenInput = true;
  Sig.SeenOutput = true;
  Sig.Output.push_back({/*Name=*/"SV_Target", /*Index=*/0,
                        llvm::dxbc::D3DSystemValue::Target,
                        llvm::dxbc::SigComponentType::Float32, /*Register=*/0,
                        /*Mask=*/0xF, /*ExclusiveMask=*/0});
  llvm::SmallVector<char, 64> Container;
  wrapInContainer(Bytecode, Sig, Container);
  llvm::StringRef Bytes(Container.data(), Container.size());
  // An empty table still produces a part, which is what `fxc` emits.
  EXPECT_NE(Bytes.find("ISGN"), llvm::StringRef::npos);
  EXPECT_NE(Bytes.find("OSGN"), llvm::StringRef::npos);
  EXPECT_NE(Bytes.find("SV_Target"), llvm::StringRef::npos);
}

/// Re-printing a parsed program and re-assembling the result must produce
/// the same tokens; that is what makes `dxbc-as --emit asm` a usable
/// normalizer rather than a lossy pretty-printer.
void expectAsmRoundTrip(llvm::StringRef Source) {
  llvm::Expected<Program> Parsed = parseAssembly(Source);
  ASSERT_TRUE(static_cast<bool>(Parsed)) << llvm::toString(Parsed.takeError());
  std::string Printed;
  llvm::raw_string_ostream OS(Printed);
  printAssembly(*Parsed, OS);
  EXPECT_EQ(assemble(Source), assemble(Printed)) << "printed as:\n" << Printed;
}

TEST(EncoderTest, AsmPrinterRoundTrips) {
  expectAsmRoundTrip(".shader_model vertex 5 1\n"
                     "dcl_globalFlags refactoringAllowed | skipOptimization\n"
                     "dcl_temps 4\n"
                     "dcl_indexableTemp x1[23], 2\n"
                     "dcl_input_ps linearCentroid v0.xy\n"
                     "dcl_input_ps_siv linear v1.x, position\n"
                     "dcl_inputprimitive patch3\n"
                     "dcl_input_control_point_count 4\n"
                     "dcl_hs_max_tessfactor 64.0\n"
                     "dcl_resource_texture2dms(4) (float, float, float, "
                     "float) t5\n"
                     "dcl_uav_typed_texture2d globallyCoherent (uint, uint, "
                     "uint, uint) u6\n"
                     "dcl_immediateConstantBuffer {0x3F800000, 0x00000000}\n"
                     "sync uav_global | threads\n"
                     "mov_sat precise(xy) r0.xyzw, -|r1.yzwy|\n"
                     "mov r0.x, cb1[3 + r2.x].xyzw\n"
                     "mov r0.xyzw{min16f}, l(1.0, 2.0, 3.0, 4.0)\n"
                     "dmov r0.xyzw, d(0x3FF0000000000000)\n"
                     "sample aoffimmi(-5, 7, 0) r1.xyzw, v0.xyxx, t3.xyzw, "
                     "s5.x\n"
                     "bufinfo resource_dim(structured_buffer, 52) "
                     "resource_return_type(uint, uint, uint, uint) r0.x, "
                     "t0.xyzw\n"
                     "callc_nz r0.x, label3\n"
                     "ret, 5\n"
                     ".dword 0x030007FF, 0xDEADBEEF\n"
                     "ret\n");
}

} // namespace
