//===- Encoder.cpp - DXBC tokenized bytecode encoder ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements Encoder, translating the parsed instruction stack to raw DXBC
// tokenized shader bytecode. Bit layouts below are transcribed from
// Microsoft's public `d3d11TokenizedProgramFormat.hpp` (see the comment on
// each ENCODE_* helper for the specific field it implements) for every
// field this tool populates; fields it never populates (e.g. relative
// addressing, double-precision immediates) are simply never emitted, not
// approximated.
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Encoder.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>

using namespace feme::dxbc;

//===----------------------------------------------------------------------===//
// Opcode token (OpcodeToken0) field encoders.
//===----------------------------------------------------------------------===//

static constexpr uint32_t OpcodeTypeMask = 0x000007ff;
static constexpr uint32_t SaturateMask = 0x00002000;    // bit 13
static constexpr uint32_t TestBooleanMask = 0x00040000; // bit 18
static constexpr uint32_t InstructionLengthShift = 24;
static constexpr uint32_t InstructionLengthMask = 0x7f000000;

static uint32_t encodeOpcodeToken0(const Instruction &Inst,
                                   uint32_t OpcodeSpecificControls,
                                   uint32_t LengthInDWords) {
  const OpcodeInfo &Info = getOpcodeInfo(Inst.Op);
  uint32_t Token = Info.RealOpcodeValue & OpcodeTypeMask;
  Token |= OpcodeSpecificControls;
  if (Inst.Saturate)
    Token |= SaturateMask;
  Token |= (LengthInDWords << InstructionLengthShift) & InstructionLengthMask;
  return Token;
}

//===----------------------------------------------------------------------===//
// Operand token (OperandToken0/1) field encoders.
//===----------------------------------------------------------------------===//

enum class RealOperandType : uint32_t {
  Temp = 0,
  Input = 1,
  Output = 2,
  Immediate32 = 4,
  Sampler = 6,
  Resource = 7,
};

static RealOperandType toRealOperandType(OperandKind Kind) {
  switch (Kind) {
  case OperandKind::Temp:
    return RealOperandType::Temp;
  case OperandKind::Input:
    return RealOperandType::Input;
  case OperandKind::Output:
    return RealOperandType::Output;
  case OperandKind::Resource:
    return RealOperandType::Resource;
  case OperandKind::Sampler:
    return RealOperandType::Sampler;
  case OperandKind::Immediate32:
    return RealOperandType::Immediate32;
  }
  llvm_unreachable("unhandled OperandKind");
}

/// Appends the token(s) for a single Operand to \p Out, following
/// "Instruction Operand Format (OperandToken0)" /
/// "Extended Instruction Operand Format (OperandToken1)" in
/// d3d11TokenizedProgramFormat.hpp.
static void encodeOperand(const Operand &Op,
                          llvm::SmallVectorImpl<uint32_t> &Out) {
  RealOperandType Type = toRealOperandType(Op.Kind);

  // [01:00] NUM_COMPONENTS: sampler operands carry no per-component data
  // (0), immediates are 1 or 4 components depending on how many literal
  // values were given, every other register we support is always
  // (4-component).
  uint32_t NumComponents = 2; // D3D10_SB_OPERAND_4_COMPONENT
  if (Op.Kind == OperandKind::Sampler)
    NumComponents = 0; // D3D10_SB_OPERAND_0_COMPONENT
  else if (Op.Kind == OperandKind::Immediate32)
    NumComponents = Op.ImmediateValues.size() == 1 ? 1 : 2;

  uint32_t Token0 = NumComponents;
  if (NumComponents == 2) {
    if (Op.SelectMode == ComponentSelectMode::Mask) {
      // [03:02] = MASK_MODE (0), [07:04] = mask.
      Token0 |= (0u << 2);
      Token0 |= (static_cast<uint32_t>(Op.WriteMask) & 0xF) << 4;
    } else {
      // [03:02] = SWIZZLE_MODE (1), [11:04] = 2 bits/component swizzle.
      Token0 |= (1u << 2);
      uint32_t Swizzle = 0;
      for (unsigned I = 0; I < 4; ++I)
        Swizzle |= (static_cast<uint32_t>(Op.Swizzle[I]) & 0x3) << (2 * I);
      Token0 |= Swizzle << 4;
    }
  }

  // [19:12] OPERAND_TYPE.
  Token0 |= (static_cast<uint32_t>(Type) & 0xff) << 12;

  // [21:20] INDEX_DIMENSION, [24:22] index[0] representation: every
  // register operand `dxbc-as` emits uses a single, immediate (not
  // relative) index; immediates carry no register index at all.
  bool HasIndex = Op.Kind != OperandKind::Immediate32;
  if (HasIndex) {
    Token0 |= (1u << 20); // D3D10_SB_OPERAND_INDEX_1D
    Token0 |= (0u << 22); // D3D10_SB_OPERAND_INDEX_IMMEDIATE32
  }

  bool HasModifier = Op.Negate || Op.Abs;
  if (HasModifier)
    Token0 |= 0x80000000u; // bit 31: extended operand token follows.

  Out.push_back(Token0);

  if (HasModifier) {
    // Extended Instruction Operand Format (OperandToken1):
    // [05:00] = D3D10_SB_EXTENDED_OPERAND_MODIFIER (1)
    // [13:06] = D3D10_SB_OPERAND_MODIFIER (NEG=1, ABS=2, ABSNEG=3)
    uint32_t Modifier = (Op.Negate ? 1u : 0u) | (Op.Abs ? 2u : 0u);
    uint32_t Token1 = 1u | (Modifier << 6);
    Out.push_back(Token1);
  }

  if (HasIndex)
    Out.push_back(Op.RegisterIndex);

  if (Op.Kind == OperandKind::Immediate32)
    llvm::append_range(Out, Op.ImmediateValues);
}

//===----------------------------------------------------------------------===//
// Declaration-specific keyword tables.
//===----------------------------------------------------------------------===//

namespace {
struct KeywordValue {
  llvm::StringRef Name;
  uint32_t Value;
};
} // namespace

static llvm::Expected<uint32_t>
lookupKeyword(llvm::ArrayRef<KeywordValue> Table, llvm::StringRef Name,
              llvm::StringRef WhatFor) {
  for (const KeywordValue &Entry : Table)
    if (Entry.Name == Name)
      return Entry.Value;
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "unknown %s '%s'", WhatFor.str().c_str(),
                                 Name.str().c_str());
}

// D3D10_SB_RESOURCE_RETURN_TYPE.
static constexpr KeywordValue ResourceReturnTypes[] = {
    {"unorm", 1}, {"snorm", 2}, {"sint", 3},
    {"uint", 4},  {"float", 5}, {"mixed", 6},
};

// D3D10_SB_RESOURCE_DIMENSION.
static uint32_t resourceDimension(Opcode Op) {
  switch (Op) {
  case Opcode::DclResourceTexture1D:
    return 2;
  case Opcode::DclResourceTexture2D:
    return 3;
  case Opcode::DclResourceTexture3D:
    return 5;
  case Opcode::DclResourceTextureCube:
    return 6;
  default:
    llvm_unreachable("not a dcl_resource_* opcode");
  }
}

// D3D10_SB_INTERPOLATION_MODE.
static constexpr KeywordValue InterpolationModes[] = {
    {"constant", 1},
    {"linear", 2},
    {"linear_centroid", 3},
    {"linear_noperspective", 4},
    {"linear_noperspective_centroid", 5},
    {"linear_sample", 6},
    {"linear_noperspective_sample", 7},
};

//===----------------------------------------------------------------------===//
// Per-InstructionKind encoders.
//===----------------------------------------------------------------------===//

/// Encodes a plain instruction: opcode token, followed by each Operand's
/// tokens in order. Used for every InstructionKind whose only per-opcode
/// state is its operand list (ALU*, Discard, Sample, Load, DclInput,
/// DclOutput).
static void encodeSimple(const Instruction &Inst, uint32_t Controls,
                         llvm::SmallVectorImpl<uint32_t> &Out) {
  size_t OpcodeTokenIndex = Out.size();
  Out.push_back(0); // placeholder, patched below
  for (const Operand &Op : Inst.Operands)
    encodeOperand(Op, Out);
  uint32_t Length = Out.size() - OpcodeTokenIndex;
  Out[OpcodeTokenIndex] = encodeOpcodeToken0(Inst, Controls, Length);
}

static llvm::Error encodeInstruction(const Instruction &Inst,
                                     llvm::SmallVectorImpl<uint32_t> &Out) {
  const OpcodeInfo &Info = getOpcodeInfo(Inst.Op);
  switch (Info.Kind) {
  case InstructionKind::ALU1:
  case InstructionKind::ALU2:
  case InstructionKind::ALU3:
  case InstructionKind::Sample:
  case InstructionKind::Load:
  case InstructionKind::DclInput:
  case InstructionKind::DclOutput:
    encodeSimple(Inst, 0, Out);
    return llvm::Error::success();

  case InstructionKind::NoOperand:
    Out.push_back(encodeOpcodeToken0(Inst, 0, 1));
    return llvm::Error::success();

  case InstructionKind::Discard: {
    // [18] test boolean: discard_z tests for zero (0), discard_nz for
    // non-zero (1).
    uint32_t TestNonZero = Inst.Op == Opcode::DiscardNZ ? 1u : 0u;
    encodeSimple(Inst, TestNonZero ? TestBooleanMask : 0, Out);
    return llvm::Error::success();
  }

  case InstructionKind::DclGlobalFlags: {
    // Deviation: real DCL_GLOBAL_FLAGS bit assignments for each flag name
    // are not published by Microsoft alongside the token format header, so
    // (since this tool has no downstream consumer yet to match) this uses
    // its own stable, but not Microsoft-verified, bit-per-flag assignment
    // within the opcode-specific control range ([23:11]); see
    // feme/docs/Design.md's "dxbc-as" section.
    static constexpr KeywordValue Flags[] = {
        {"refactoringAllowed", 1u << 11},
        {"enableDoublePrecisionFloatOps", 1u << 12},
        {"forceEarlyDepthStencil", 1u << 13},
        {"enableRawAndStructuredBuffers", 1u << 14},
        {"skipOptimization", 1u << 15},
    };
    uint32_t Controls = 0;
    for (const std::string &Flag : Inst.Keywords) {
      llvm::Expected<uint32_t> Bit =
          lookupKeyword(Flags, Flag, "dcl_globalFlags flag");
      if (!Bit)
        return Bit.takeError();
      Controls |= *Bit;
    }
    Out.push_back(encodeOpcodeToken0(Inst, Controls, 1));
    return llvm::Error::success();
  }

  case InstructionKind::DclTemps:
    // DCL_TEMPS carries its count as a raw DWORD, not an operand.
    Out.push_back(encodeOpcodeToken0(Inst, 0, 2));
    Out.push_back(static_cast<uint32_t>(Inst.Immediates[0]));
    return llvm::Error::success();

  case InstructionKind::DclResource: {
    // [15:11] D3D10_SB_RESOURCE_DIMENSION.
    uint32_t Controls = resourceDimension(Inst.Op) << 11;
    size_t OpcodeTokenIndex = Out.size();
    Out.push_back(0);
    encodeOperand(Inst.Operands[0], Out);
    uint32_t ReturnTypes = 0;
    for (unsigned I = 0; I < 4; ++I) {
      llvm::Expected<uint32_t> Ty = lookupKeyword(
          ResourceReturnTypes, Inst.Keywords[I], "resource return type");
      if (!Ty)
        return Ty.takeError();
      ReturnTypes |= (*Ty & 0xF) << (4 * I);
    }
    Out.push_back(ReturnTypes);
    uint32_t Length = Out.size() - OpcodeTokenIndex;
    Out[OpcodeTokenIndex] = encodeOpcodeToken0(Inst, Controls, Length);
    return llvm::Error::success();
  }

  case InstructionKind::DclSampler: {
    // [14:11] D3D10_SB_SAMPLER_MODE (DEFAULT=0, COMPARISON=1).
    bool Comparison = llvm::is_contained(Inst.Keywords, "comparison");
    uint32_t Controls = (Comparison ? 1u : 0u) << 11;
    encodeSimple(Inst, Controls, Out);
    return llvm::Error::success();
  }

  case InstructionKind::DclInputPS: {
    // [16:11] D3D10_SB_INTERPOLATION_MODE.
    uint32_t Controls = 0;
    if (!Inst.Keywords.empty()) {
      llvm::Expected<uint32_t> Mode = lookupKeyword(
          InterpolationModes, Inst.Keywords[0], "interpolation mode");
      if (!Mode)
        return Mode.takeError();
      Controls = *Mode << 11;
    }
    encodeSimple(Inst, Controls, Out);
    return llvm::Error::success();
  }
  }
  llvm_unreachable("unhandled InstructionKind");
}

//===----------------------------------------------------------------------===//
// Program-level encoding.
//===----------------------------------------------------------------------===//

llvm::Expected<llvm::SmallVector<uint32_t, 64>>
feme::dxbc::encodeProgram(llvm::ArrayRef<Instruction> Program,
                          ShaderKind Kind) {
  llvm::SmallVector<uint32_t, 64> Out;

  // Version Token (VerTok): [31:16] program type, [15:08] major, [07:00]
  // minor. dxbc-as always targets shader model 5.0.
  uint32_t VersionToken = (static_cast<uint32_t>(Kind) << 16) | (5u << 4) | 0u;
  Out.push_back(VersionToken);
  Out.push_back(0); // Length Token placeholder, patched below.

  for (const Instruction &Inst : Program)
    if (llvm::Error E = encodeInstruction(Inst, Out))
      return std::move(E);

  Out[1] = static_cast<uint32_t>(Out.size());
  return Out;
}

//===----------------------------------------------------------------------===//
// DXContainer wrapping.
//===----------------------------------------------------------------------===//

void feme::dxbc::wrapInContainer(llvm::ArrayRef<uint32_t> Bytecode,
                                 ShaderKind Kind,
                                 llvm::SmallVectorImpl<char> &Out) {
  using namespace llvm::dxbc;

  std::string PartData;
  {
    llvm::raw_string_ostream PartOS(PartData);
    llvm::support::endian::Writer W(PartOS, llvm::endianness::little);
    for (uint32_t Word : Bytecode)
      W.write(Word);
  }

  PartHeader Part;
  memcpy(Part.Name, "SHEX", 4);
  Part.Size = PartData.size();

  Header FileHeader;
  memcpy(FileHeader.Magic, "DXBC", 4);
  memset(FileHeader.FileHash.Digest, 0, sizeof(FileHeader.FileHash.Digest));
  FileHeader.Version.Major = 1;
  FileHeader.Version.Minor = 0;
  FileHeader.PartCount = 1;

  uint32_t PartOffset = sizeof(Header) + sizeof(uint32_t) /* PartOffset[0] */;
  FileHeader.FileSize =
      PartOffset + sizeof(PartHeader) + static_cast<uint32_t>(PartData.size());

  llvm::raw_svector_ostream OS(Out);
  llvm::support::endian::Writer W(OS, llvm::endianness::little);
  W.write(llvm::ArrayRef<uint8_t>(FileHeader.Magic, 4));
  W.write(llvm::ArrayRef<uint8_t>(FileHeader.FileHash.Digest, 16));
  W.write(FileHeader.Version.Major);
  W.write(FileHeader.Version.Minor);
  W.write(FileHeader.FileSize);
  W.write(FileHeader.PartCount);
  W.write(PartOffset);
  W.write(llvm::ArrayRef<uint8_t>(Part.Name, 4));
  W.write(Part.Size);
  OS << PartData;
}
