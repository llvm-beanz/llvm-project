//===- InstructionTest.cpp - Unit tests for the opcode/operand tables ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Instruction.h"
#include "gtest/gtest.h"

#include <set>

using namespace feme::dxbc;

namespace {

TEST(InstructionTest, LookupOpcodeFindsKnownMnemonics) {
  const Opcode *Mov = lookupOpcode("mov");
  ASSERT_NE(Mov, nullptr);
  const OpcodeInfo &Info = getOpcodeInfo(*Mov);
  EXPECT_EQ(Info.Mnemonic, "mov");
  EXPECT_EQ(Info.Value, 54u); // D3D10_SB_OPCODE_MOV
  EXPECT_EQ(Info.NumDst, 1u);
  EXPECT_EQ(Info.NumSrc, 1u);
  EXPECT_EQ(Info.Kind, InstructionKind::Generic);
  EXPECT_TRUE(Info.Flags & OF_Saturable);
}

TEST(InstructionTest, LookupOpcodeRejectsUnknownMnemonics) {
  EXPECT_EQ(lookupOpcode("definitely_not_an_opcode"), nullptr);
  EXPECT_EQ(lookupOpcode(""), nullptr);
  // '_sat' is a suffix the Parser strips, not a mnemonic of its own.
  EXPECT_EQ(lookupOpcode("mov_sat"), nullptr);
}

TEST(InstructionTest, FeedbackVariantsAddAStatusDestination) {
  // Every SM5.1 `_s` opcode takes the same sources as the opcode it
  // shadows plus a second destination for the residency status.
  for (auto [Base, Feedback] : {std::pair{"sample_l", "sample_l_s"},
                                {"sample_c_lz", "sample_c_lz_s"},
                                {"gather4", "gather4_s"},
                                {"gather4_c", "gather4_c_s"},
                                {"gather4_po", "gather4_po_s"},
                                {"gather4_po_c", "gather4_po_c_s"},
                                {"ld", "ld_s"},
                                {"ld2dms", "ld2dms_s"},
                                {"ld_uav_typed", "ld_uav_typed_s"},
                                {"ld_raw", "ld_raw_s"},
                                {"ld_structured", "ld_structured_s"}}) {
    const Opcode *B = lookupOpcode(Base);
    const Opcode *F = lookupOpcode(Feedback);
    ASSERT_NE(B, nullptr) << Base;
    ASSERT_NE(F, nullptr) << Feedback;
    EXPECT_EQ(getOpcodeInfo(*F).NumDst, getOpcodeInfo(*B).NumDst + 1)
        << Feedback;
    EXPECT_EQ(getOpcodeInfo(*F).NumSrc, getOpcodeInfo(*B).NumSrc) << Feedback;
  }
}

TEST(InstructionTest, MnemonicVariantsShareAnOpcodeValueButDifferInControls) {
  const Opcode *Z = lookupOpcode("callc_z");
  const Opcode *NZ = lookupOpcode("callc_nz");
  ASSERT_NE(Z, nullptr);
  ASSERT_NE(NZ, nullptr);
  EXPECT_EQ(getOpcodeInfo(*Z).Value, getOpcodeInfo(*NZ).Value);
  EXPECT_EQ(getOpcodeInfo(*Z).Controls, 0u);
  EXPECT_EQ(getOpcodeInfo(*NZ).Controls, 0x00040000u); // test-boolean bit 18
}

TEST(InstructionTest, ResourceDimensionsAreEncodedInTheMnemonic) {
  const Opcode *Tex2D = lookupOpcode("dcl_resource_texture2d");
  const Opcode *Tex3D = lookupOpcode("dcl_resource_texture3d");
  ASSERT_NE(Tex2D, nullptr);
  ASSERT_NE(Tex3D, nullptr);
  // D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D == 3, TEXTURE3D == 5, both in
  // the opcode-specific control field starting at bit 11.
  EXPECT_EQ(getOpcodeInfo(*Tex2D).Controls, 3u << 11);
  EXPECT_EQ(getOpcodeInfo(*Tex3D).Controls, 5u << 11);
  EXPECT_EQ(getOpcodeInfo(*Tex2D).Kind, InstructionKind::DclTypedResource);
}

TEST(InstructionTest, MnemonicsAreUnique) {
  std::set<llvm::StringRef> Seen;
#define DXBC_OPCODE(EnumName, Mnemonic, Value, NumDst, NumSrc, Controls, Kind, \
                    Flags)                                                     \
  EXPECT_TRUE(Seen.insert(Mnemonic).second) << "duplicate mnemonic " Mnemonic;
#include "feme/DXBC/Assembler/Opcodes.def"
}

TEST(InstructionTest, OperandKindSpellingsRoundTrip) {
#define DXBC_OPERAND_KIND(EnumName, Spelling, Value)                           \
  {                                                                            \
    const OperandKind *Kind = lookupOperandKind(Spelling);                     \
    ASSERT_NE(Kind, nullptr);                                                  \
    EXPECT_EQ(*Kind, OperandKind::EnumName);                                   \
    EXPECT_EQ(getOperandKindSpelling(OperandKind::EnumName), Spelling);        \
    EXPECT_EQ(static_cast<unsigned>(OperandKind::EnumName), unsigned(Value));  \
  }
#include "feme/DXBC/Assembler/OperandKinds.def"
  EXPECT_EQ(lookupOperandKind("not_a_register_file"), nullptr);
}

TEST(InstructionTest, DefaultComponentCounts) {
  // Handles (samplers, resources, labels) carry no component data...
  EXPECT_EQ(getDefaultComponentCount(OperandKind::Sampler),
            ComponentCount::Zero);
  EXPECT_EQ(getDefaultComponentCount(OperandKind::Label), ComponentCount::Zero);
  // ...scalar system values carry exactly one...
  EXPECT_EQ(getDefaultComponentCount(OperandKind::OutputDepth),
            ComponentCount::One);
  // ...and everything else is a four-component vector register.
  EXPECT_EQ(getDefaultComponentCount(OperandKind::Temp), ComponentCount::Four);
  EXPECT_EQ(getDefaultComponentCount(OperandKind::ConstantBuffer),
            ComponentCount::Four);
}

} // namespace
