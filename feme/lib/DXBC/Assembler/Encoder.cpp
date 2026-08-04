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
// Microsoft's public `d3d11TokenizedProgramFormat.hpp`.
//
// Parser has already resolved every keyword to the bits it stands for, so
// this file only has to lay tokens out: an opcode token carrying the
// opcode-specific control bits, any extended opcode tokens, the operand
// tokens, then the instruction's trailing raw DWORDs.
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Encoder.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/MC/DXContainerPSVInfo.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>

using namespace feme::dxbc;

//===----------------------------------------------------------------------===//
// Opcode token (OpcodeToken0) field encoders.
//===----------------------------------------------------------------------===//

static constexpr uint32_t OpcodeTypeMask = 0x000007ff;
static constexpr uint32_t SaturateMask = 0x00002000;   // bit 13
static constexpr uint32_t PreciseValuesShift = 19;     // bits [22:19]
static constexpr uint32_t InstructionLengthShift = 24; // bits [30:24]
static constexpr uint32_t MaxInstructionLength = 127;
static constexpr uint32_t ExtendedMask = 0x80000000u; // bit 31

//===----------------------------------------------------------------------===//
// Extended opcode tokens (OpcodeToken1).
//===----------------------------------------------------------------------===//

namespace {
enum class ExtendedOpcodeType : uint32_t {
  SampleControls = 1,
  ResourceDim = 2,
  ResourceReturnType = 3,
};
} // namespace

/// Appends the extended opcode tokens \p Inst needs, marking each token that
/// is followed by another. Returns true if any were emitted (in which case
/// the opcode token itself must set bit 31).
static bool encodeExtendedOpcodeTokens(const Instruction &Inst,
                                       llvm::SmallVectorImpl<uint32_t> &Out) {
  llvm::SmallVector<uint32_t, 3> Tokens;

  if (Inst.HasSampleOffsets) {
    // [5:0] type, then three 4-bit signed texel offsets at bits 9, 13, 17.
    uint32_t Token = static_cast<uint32_t>(ExtendedOpcodeType::SampleControls);
    for (unsigned I = 0; I < 3; ++I) {
      uint32_t Offset = static_cast<uint32_t>(Inst.SampleOffsets[I]) & 0xF;
      Token |= Offset << (9 + 4 * I);
    }
    Tokens.push_back(Token);
  }
  if (Inst.HasResourceDim) {
    // [5:0] type, [10:6] dimension, [21:11] structure stride.
    uint32_t Token = static_cast<uint32_t>(ExtendedOpcodeType::ResourceDim);
    Token |= (static_cast<uint32_t>(Inst.ResourceDim) & 0x1F) << 6;
    Token |= (static_cast<uint32_t>(Inst.ResourceStride) & 0x7FF) << 11;
    Tokens.push_back(Token);
  }
  if (Inst.HasResourceReturnType) {
    // [5:0] type, then four 4-bit return types at bits 6, 10, 14, 18.
    uint32_t Token =
        static_cast<uint32_t>(ExtendedOpcodeType::ResourceReturnType);
    for (unsigned I = 0; I < 4; ++I)
      Token |= (static_cast<uint32_t>(Inst.ResourceReturnTypes[I]) & 0xF)
               << (6 + 4 * I);
    Tokens.push_back(Token);
  }

  for (size_t I = 0, E = Tokens.size(); I != E; ++I)
    Out.push_back(I + 1 == E ? Tokens[I] : Tokens[I] | ExtendedMask);
  return !Tokens.empty();
}

//===----------------------------------------------------------------------===//
// Operand tokens (OperandToken0/OperandToken1).
//===----------------------------------------------------------------------===//

/// Appends the token(s) for a single Operand to \p Out, following
/// "Instruction Operand Format (OperandToken0)" /
/// "Extended Instruction Operand Format (OperandToken1)" in
/// `d3d11TokenizedProgramFormat.hpp`.
static void encodeOperand(const Operand &Op,
                          llvm::SmallVectorImpl<uint32_t> &Out) {
  bool IsImmediate = Op.Kind == OperandKind::Immediate32 ||
                     Op.Kind == OperandKind::Immediate64;

  // [01:00] NUM_COMPONENTS.
  uint32_t Token0 = 0;
  switch (Op.Components) {
  case ComponentCount::Zero:
    Token0 = 0;
    break;
  case ComponentCount::One:
    Token0 = 1;
    break;
  case ComponentCount::Four:
    Token0 = 2;
    break;
  }

  if (Op.Components == ComponentCount::Four && !IsImmediate) {
    switch (Op.SelectMode) {
    case ComponentSelectMode::Mask:
      // [03:02] = MASK_MODE (0), [07:04] = mask.
      Token0 |= static_cast<uint32_t>(Op.WriteMask & 0xF) << 4;
      break;
    case ComponentSelectMode::Swizzle: {
      // [03:02] = SWIZZLE_MODE (1), [11:04] = 2 bits/component.
      Token0 |= 1u << 2;
      uint32_t Swizzle = 0;
      for (unsigned I = 0; I < 4; ++I)
        Swizzle |= (static_cast<uint32_t>(Op.Swizzle[I]) & 0x3) << (2 * I);
      Token0 |= Swizzle << 4;
      break;
    }
    case ComponentSelectMode::Select1:
      // [03:02] = SELECT_1_MODE (2), [05:04] = component.
      Token0 |= 2u << 2;
      Token0 |= (static_cast<uint32_t>(Op.SelectedComponent) & 0x3) << 4;
      break;
    }
  }

  // [19:12] OPERAND_TYPE.
  Token0 |= (static_cast<uint32_t>(Op.Kind) & 0xff) << 12;

  // [21:20] INDEX_DIMENSION, then one 3-bit index representation per
  // dimension at bits 22, 25 and 28.
  if (!IsImmediate) {
    Token0 |= (static_cast<uint32_t>(Op.Indices.size()) & 0x3) << 20;
    for (unsigned I = 0, E = Op.Indices.size(); I != E; ++I)
      Token0 |= (static_cast<uint32_t>(Op.Indices[I].Rep) & 0x7)
                << (22 + 3 * I);
  }

  bool HasModifier = Op.Negate || Op.Abs || Op.NonUniform ||
                     Op.Precision != MinPrecision::Default;
  if (HasModifier)
    Token0 |= ExtendedMask;

  Out.push_back(Token0);

  if (HasModifier) {
    // [05:00] = D3D10_SB_EXTENDED_OPERAND_MODIFIER (1)
    // [13:06] = D3D10_SB_OPERAND_MODIFIER (NEG=1, ABS=2, ABSNEG=3)
    // [16:14] = D3D11_SB_OPERAND_MIN_PRECISION
    // [17:17] = D3D12_SB_OPERAND_NON_UNIFORM
    uint32_t Modifier = (Op.Negate ? 1u : 0u) | (Op.Abs ? 2u : 0u);
    uint32_t Token1 = 1u | (Modifier << 6);
    Token1 |= (static_cast<uint32_t>(Op.Precision) & 0x7) << 14;
    if (Op.NonUniform)
      Token1 |= 1u << 17;
    Out.push_back(Token1);
  }

  for (const OperandIndex &Index : Op.Indices) {
    switch (Index.Rep) {
    case OperandIndex::Representation::Immediate32:
      Out.push_back(static_cast<uint32_t>(Index.Value));
      break;
    case OperandIndex::Representation::Immediate64:
      Out.push_back(static_cast<uint32_t>(Index.Value >> 32));
      Out.push_back(static_cast<uint32_t>(Index.Value));
      break;
    case OperandIndex::Representation::Relative:
      encodeOperand(*Index.Relative, Out);
      break;
    case OperandIndex::Representation::Immediate32PlusRelative:
      Out.push_back(static_cast<uint32_t>(Index.Value));
      encodeOperand(*Index.Relative, Out);
      break;
    }
  }

  llvm::append_range(Out, Op.ImmediateValues);
}

//===----------------------------------------------------------------------===//
// Instruction encoding.
//===----------------------------------------------------------------------===//

static llvm::Error encodeInstruction(const Instruction &Inst,
                                     llvm::SmallVectorImpl<uint32_t> &Out) {
  const OpcodeInfo &Info = getOpcodeInfo(Inst.Op);

  if (Info.Kind == InstructionKind::RawTokens) {
    llvm::append_range(Out, Inst.ExtraDWords);
    return llvm::Error::success();
  }

  if (Info.Kind == InstructionKind::DclImmediateConstantBuffer) {
    // CUSTOMDATA instructions carry their total token count in the token
    // after the opcode token instead of in the opcode token's length field.
    Out.push_back(Info.Value | Inst.Controls);
    Out.push_back(static_cast<uint32_t>(Inst.ExtraDWords.size()) + 2);
    llvm::append_range(Out, Inst.ExtraDWords);
    return llvm::Error::success();
  }

  size_t OpcodeTokenIndex = Out.size();
  Out.push_back(0); // placeholder, patched below
  bool Extended = encodeExtendedOpcodeTokens(Inst, Out);
  for (const Operand &Op : Inst.Operands)
    encodeOperand(Op, Out);
  llvm::append_range(Out, Inst.ExtraDWords);

  size_t Length = Out.size() - OpcodeTokenIndex;
  if (Length > MaxInstructionLength)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "'%s' encodes to %zu DWORDs, exceeding the %u-DWORD instruction "
        "length limit",
        Info.Mnemonic.str().c_str(), Length, MaxInstructionLength);

  uint32_t Token = (Info.Value & OpcodeTypeMask) | Inst.Controls;
  if (Inst.Saturate)
    Token |= SaturateMask;
  Token |= (static_cast<uint32_t>(Inst.PreciseMask) & 0xF)
           << PreciseValuesShift;
  Token |= static_cast<uint32_t>(Length) << InstructionLengthShift;
  if (Extended)
    Token |= ExtendedMask;
  Out[OpcodeTokenIndex] = Token;
  return llvm::Error::success();
}

//===----------------------------------------------------------------------===//
// Program-level encoding.
//===----------------------------------------------------------------------===//

llvm::Expected<llvm::SmallVector<uint32_t, 64>>
feme::dxbc::encodeProgram(const Program &Program) {
  llvm::SmallVector<uint32_t, 64> Out;

  if (Program.HasHeader) {
    // Version Token (VerTok): [31:16] program type, [07:04] major version,
    // [03:00] minor version, followed by the length of the whole program in
    // DWORDs (patched in below).
    Out.push_back((static_cast<uint32_t>(Program.ProgramType) << 16) |
                  (static_cast<uint32_t>(Program.MajorVersion) << 4) |
                  static_cast<uint32_t>(Program.MinorVersion));
    Out.push_back(0);
  }

  for (const Instruction &Inst : Program.Instructions)
    if (llvm::Error E = encodeInstruction(Inst, Out))
      return std::move(E);

  if (Program.HasHeader)
    Out[1] = static_cast<uint32_t>(Out.size());
  return Out;
}

//===----------------------------------------------------------------------===//
// DXContainer wrapping.
//===----------------------------------------------------------------------===//

/// Serializes \p Elements as a legacy `ISGN`/`OSGN`/`PCSG` part body.
static std::string
encodeSignature(llvm::ArrayRef<feme::dxbc::SignatureElement> Elements) {
  llvm::mcdxbc::LegacySignature Sig;
  for (const feme::dxbc::SignatureElement &Element : Elements)
    Sig.addParam(Element.Name, Element.Index, Element.SystemValue,
                 Element.CompType, Element.Register, Element.Mask,
                 Element.ExclusiveMask);
  std::string Data;
  llvm::raw_string_ostream OS(Data);
  Sig.write(OS);
  return Data;
}

void feme::dxbc::wrapInContainer(llvm::ArrayRef<uint32_t> Bytecode,
                                 const Signatures &Sig,
                                 llvm::SmallVectorImpl<char> &Out) {
  using namespace llvm::dxbc;

  // Part order follows `fxc`: the signatures, then the shader body.
  llvm::SmallVector<std::pair<llvm::StringRef, std::string>, 4> Parts;
  if (Sig.SeenInput)
    Parts.emplace_back("ISGN", encodeSignature(Sig.Input));
  if (Sig.SeenOutput)
    Parts.emplace_back("OSGN", encodeSignature(Sig.Output));
  if (Sig.SeenPatchConstant)
    Parts.emplace_back("PCSG", encodeSignature(Sig.PatchConstant));

  std::string ShaderData;
  {
    llvm::raw_string_ostream PartOS(ShaderData);
    llvm::support::endian::Writer W(PartOS, llvm::endianness::little);
    for (uint32_t Word : Bytecode)
      W.write(Word);
  }
  Parts.emplace_back("SHEX", std::move(ShaderData));

  Header FileHeader;
  memcpy(FileHeader.Magic, "DXBC", 4);
  memset(FileHeader.FileHash.Digest, 0, sizeof(FileHeader.FileHash.Digest));
  FileHeader.Version.Major = 1;
  FileHeader.Version.Minor = 0;
  FileHeader.PartCount = Parts.size();

  llvm::SmallVector<uint32_t, 4> Offsets;
  uint32_t Offset =
      sizeof(Header) + static_cast<uint32_t>(Parts.size()) * sizeof(uint32_t);
  for (const auto &[Name, Data] : Parts) {
    Offsets.push_back(Offset);
    Offset += sizeof(PartHeader) + static_cast<uint32_t>(Data.size());
  }
  FileHeader.FileSize = Offset;

  llvm::raw_svector_ostream OS(Out);
  llvm::support::endian::Writer W(OS, llvm::endianness::little);
  W.write(llvm::ArrayRef<uint8_t>(FileHeader.Magic, 4));
  W.write(llvm::ArrayRef<uint8_t>(FileHeader.FileHash.Digest, 16));
  W.write(FileHeader.Version.Major);
  W.write(FileHeader.Version.Minor);
  W.write(FileHeader.FileSize);
  W.write(FileHeader.PartCount);
  for (uint32_t PartOffset : Offsets)
    W.write(PartOffset);
  for (const auto &[Name, Data] : Parts) {
    PartHeader Part;
    memcpy(Part.Name, Name.data(), 4);
    Part.Size = Data.size();
    W.write(llvm::ArrayRef<uint8_t>(Part.Name, 4));
    W.write(Part.Size);
    OS << Data;
  }
}
