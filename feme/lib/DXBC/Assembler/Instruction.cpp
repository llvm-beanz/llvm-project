//===- Instruction.cpp - DXBC assembler instruction model ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Instruction.h"

#include "llvm/ADT/StringMap.h"

using namespace feme::dxbc;

static constexpr OpcodeInfo OpcodeTable[] = {
#define DXBC_OPCODE(EnumName, Mnemonic, RealOpcodeValue, Kind)                 \
  {Mnemonic, RealOpcodeValue, InstructionKind::Kind},
#include "feme/DXBC/Assembler/Opcodes.def"
};

const OpcodeInfo &feme::dxbc::getOpcodeInfo(Opcode Op) {
  return OpcodeTable[static_cast<size_t>(Op)];
}

const Opcode *feme::dxbc::lookupOpcode(llvm::StringRef Mnemonic) {
  static const llvm::StringMap<Opcode> MnemonicToOpcode = [] {
    llvm::StringMap<Opcode> Map;
#define DXBC_OPCODE(EnumName, Mnemonic, RealOpcodeValue, Kind)                 \
  Map[Mnemonic] = Opcode::EnumName;
#include "feme/DXBC/Assembler/Opcodes.def"
    return Map;
  }();

  auto It = MnemonicToOpcode.find(Mnemonic);
  if (It == MnemonicToOpcode.end())
    return nullptr;
  return &It->second;
}
