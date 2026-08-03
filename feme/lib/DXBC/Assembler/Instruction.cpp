//===- Instruction.cpp - DXBC assembler instruction model ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Instruction.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/ErrorHandling.h"

using namespace feme::dxbc;

static constexpr OpcodeInfo OpcodeTable[] = {
#define DXBC_OPCODE(EnumName, Mnemonic, Value, NumDst, NumSrc, Controls, Kind, \
                    Flags)                                                     \
  {Mnemonic, Value, NumDst, NumSrc, Controls, InstructionKind::Kind, Flags},
#include "feme/DXBC/Assembler/Opcodes.def"
};

const OpcodeInfo &feme::dxbc::getOpcodeInfo(Opcode Op) {
  return OpcodeTable[static_cast<size_t>(Op)];
}

const Opcode *feme::dxbc::lookupOpcode(llvm::StringRef Mnemonic) {
  static const llvm::StringMap<Opcode> MnemonicToOpcode = [] {
    llvm::StringMap<Opcode> Map;
#define DXBC_OPCODE(EnumName, Mnemonic, Value, NumDst, NumSrc, Controls, Kind, \
                    Flags)                                                     \
  Map[Mnemonic] = Opcode::EnumName;
#include "feme/DXBC/Assembler/Opcodes.def"
    return Map;
  }();

  auto It = MnemonicToOpcode.find(Mnemonic);
  if (It == MnemonicToOpcode.end())
    return nullptr;
  return &It->second;
}

llvm::StringRef feme::dxbc::getOperandKindSpelling(OperandKind Kind) {
  switch (Kind) {
#define DXBC_OPERAND_KIND(EnumName, Spelling, Value)                           \
  case OperandKind::EnumName:                                                  \
    return Spelling;
#include "feme/DXBC/Assembler/OperandKinds.def"
  }
  llvm_unreachable("unhandled OperandKind");
}

const OperandKind *feme::dxbc::lookupOperandKind(llvm::StringRef Spelling) {
  static const llvm::StringMap<OperandKind> SpellingToKind = [] {
    llvm::StringMap<OperandKind> Map;
#define DXBC_OPERAND_KIND(EnumName, Spelling, Value)                           \
  Map[Spelling] = OperandKind::EnumName;
#include "feme/DXBC/Assembler/OperandKinds.def"
    return Map;
  }();

  auto It = SpellingToKind.find(Spelling);
  if (It == SpellingToKind.end())
    return nullptr;
  return &It->second;
}

ComponentCount feme::dxbc::getDefaultComponentCount(OperandKind Kind) {
  switch (Kind) {
  // Operands naming a whole object rather than a value carry no
  // per-component data at all.
  case OperandKind::Sampler:
  case OperandKind::Resource:
  case OperandKind::UnorderedAccessView:
  case OperandKind::ThreadGroupSharedMemory:
  case OperandKind::Label:
  case OperandKind::Null:
  case OperandKind::Rasterizer:
  case OperandKind::Stream:
  case OperandKind::FunctionBody:
  case OperandKind::FunctionTable:
  case OperandKind::Interface:
  case OperandKind::ThisPointer:
  case OperandKind::InputPrimitiveID:
  case OperandKind::InputForkInstanceID:
  case OperandKind::InputJoinInstanceID:
  case OperandKind::OutputControlPointID:
  case OperandKind::InputThreadIDInGroupFlattened:
    return ComponentCount::Zero;
  // Scalar system-generated values.
  case OperandKind::OutputDepth:
  case OperandKind::OutputDepthGreaterEqual:
  case OperandKind::OutputDepthLessEqual:
  case OperandKind::OutputStencilRef:
  case OperandKind::OutputCoverageMask:
  case OperandKind::InputCoverageMask:
  case OperandKind::InnerCoverage:
    return ComponentCount::One;
  default:
    return ComponentCount::Four;
  }
}
