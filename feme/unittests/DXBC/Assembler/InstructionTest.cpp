//===- InstructionTest.cpp - Unit tests for the opcode table -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Instruction.h"
#include "gtest/gtest.h"

using namespace feme::dxbc;

namespace {

TEST(InstructionTest, LookupKnownMnemonic) {
  const Opcode *Op = lookupOpcode("mov");
  ASSERT_NE(Op, nullptr);
  EXPECT_EQ(*Op, Opcode::Mov);
  EXPECT_EQ(getOpcodeInfo(*Op).Mnemonic, "mov");
  EXPECT_EQ(getOpcodeInfo(*Op).Kind, InstructionKind::ALU1);
}

TEST(InstructionTest, LookupUnknownMnemonicReturnsNull) {
  EXPECT_EQ(lookupOpcode("not_a_real_mnemonic"), nullptr);
}

TEST(InstructionTest, RealOpcodeValueMatchesD3D10SBOpcodeType) {
  // D3D10_SB_OPCODE_RET's published value (see
  // d3d11TokenizedProgramFormat.hpp) is 62.
  EXPECT_EQ(getOpcodeInfo(Opcode::Ret).RealOpcodeValue, 62u);
}

} // namespace
