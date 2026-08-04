//===- SignatureCommentsTest.cpp - unit tests --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the reader that recovers a shader's signatures from the
// tables `fxc` prints above its disassembly. The end-to-end
// disassembly-in, DXContainer-out behaviour is covered by the lit tests
// under feme/test/Tools/dxbc-as.
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/SignatureComments.h"

#include "gtest/gtest.h"

using namespace feme::dxbc;
using namespace llvm;

namespace {

TEST(SignatureCommentsTest, ReadsBothSignatures) {
  Signatures Sig = parseSignatureComments(R"(//
// Input signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// A                        0   xyzw        0     NONE   float    yz
//
//
// Output signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_Target                0   xyz         0   TARGET   float   xyz
//
ps_5_0
ret
)");
  EXPECT_TRUE(Sig.SeenInput);
  EXPECT_TRUE(Sig.SeenOutput);
  EXPECT_FALSE(Sig.SeenPatchConstant);

  ASSERT_EQ(Sig.Input.size(), 1u);
  EXPECT_EQ(Sig.Input[0].Name, "A");
  EXPECT_EQ(Sig.Input[0].Index, 0u);
  EXPECT_EQ(Sig.Input[0].Register, 0u);
  EXPECT_EQ(Sig.Input[0].Mask, 0xFu);
  // "Always read" for an input signature.
  EXPECT_EQ(Sig.Input[0].ExclusiveMask, 0x6u);
  EXPECT_EQ(Sig.Input[0].SystemValue, dxbc::D3DSystemValue::Undefined);
  EXPECT_EQ(Sig.Input[0].CompType, dxbc::SigComponentType::Float32);

  ASSERT_EQ(Sig.Output.size(), 1u);
  EXPECT_EQ(Sig.Output[0].Name, "SV_Target");
  EXPECT_EQ(Sig.Output[0].Mask, 0x7u);
  // "Never written" for an output signature.
  EXPECT_EQ(Sig.Output[0].ExclusiveMask, 0x0u);
  EXPECT_EQ(Sig.Output[0].SystemValue, dxbc::D3DSystemValue::Target);
}

TEST(SignatureCommentsTest, ReadsMasksPrintedWithGaps) {
  // The mask columns print each component in a fixed position, so a
  // discontiguous one arrives as several whitespace-separated pieces.
  Signatures Sig = parseSignatureComments(R"(// Input signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// A                        0   xyz         0     NONE   float   x z
//
)");
  ASSERT_EQ(Sig.Input.size(), 1u);
  EXPECT_EQ(Sig.Input[0].Mask, 0x7u);
  EXPECT_EQ(Sig.Input[0].ExclusiveMask, 0x5u);
}

TEST(SignatureCommentsTest, ReadsRowsWhoseNameOverflowsItsColumn) {
  Signatures Sig = parseSignatureComments(R"(// Patch Constant signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_FinalQuadEdgeTessFactor     0   x           2 QUADEDGE   float   xyzw
//
)");
  ASSERT_EQ(Sig.PatchConstant.size(), 1u);
  EXPECT_EQ(Sig.PatchConstant[0].Name, "SV_FinalQuadEdgeTessFactor");
  EXPECT_EQ(Sig.PatchConstant[0].Register, 2u);
  EXPECT_EQ(Sig.PatchConstant[0].Mask, 0x1u);
  EXPECT_EQ(Sig.PatchConstant[0].SystemValue,
            dxbc::D3DSystemValue::FinalQuadEdgeTessfactor);
}

TEST(SignatureCommentsTest, RegisterlessElementsOccupyOneComponent) {
  Signatures Sig = parseSignatureComments(R"(// Output signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_Depth                 0    N/A   oDepth    DEPTH   float    YES
//
)");
  ASSERT_EQ(Sig.Output.size(), 1u);
  EXPECT_EQ(Sig.Output[0].Register, SignatureElement::NoRegister);
  EXPECT_EQ(Sig.Output[0].Mask, 0x1u);
  EXPECT_EQ(Sig.Output[0].SystemValue, dxbc::D3DSystemValue::Depth);
}

TEST(SignatureCommentsTest, DropsAGeometryStreamPrefix) {
  Signatures Sig = parseSignatureComments(R"(// Output signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// m2:SV_Position           0   xyzw        0      POS   float   xyzw
//
)");
  ASSERT_EQ(Sig.Output.size(), 1u);
  EXPECT_EQ(Sig.Output[0].Name, "SV_Position");
}

TEST(SignatureCommentsTest, AnEmptyTableIsStillATable) {
  Signatures Sig = parseSignatureComments(R"(// Input signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// no Input
//
)");
  EXPECT_TRUE(Sig.SeenInput);
  EXPECT_TRUE(Sig.Input.empty());
  EXPECT_FALSE(Sig.SeenOutput);
}

TEST(SignatureCommentsTest, MinimumPrecisionFormatsKeepTheirWidth) {
  // A real fxc container records minimum precision in ISG1/OSG1 and writes
  // 32-bit component types into the legacy parts; since the legacy part is
  // the only one this container carries, the 16-bit component types are
  // used, which is the only lossless way to preserve the disassembly.
  Signatures Sig = parseSignatureComments(R"(// Input signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// A                        0   x           0     NONE  min16f   x
// B                        0   x           1     NONE  min16u   x
// C                        0   x           2     NONE  min16i   x
//
)");
  ASSERT_EQ(Sig.Input.size(), 3u);
  EXPECT_EQ(Sig.Input[0].CompType, dxbc::SigComponentType::Float16);
  EXPECT_EQ(Sig.Input[1].CompType, dxbc::SigComponentType::UInt16);
  EXPECT_EQ(Sig.Input[2].CompType, dxbc::SigComponentType::SInt16);
}

TEST(SignatureCommentsTest, IgnoresSourceWithoutSignatureTables) {
  Signatures Sig = parseSignatureComments("ps_5_0\nret\n");
  EXPECT_TRUE(Sig.empty());
}

} // namespace
