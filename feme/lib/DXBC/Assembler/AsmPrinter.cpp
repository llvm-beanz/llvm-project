//===- AsmPrinter.cpp - DXBC assembly text emission ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/AsmPrinter.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>

using namespace feme::dxbc;

static char registerPrefix(OperandKind Kind) {
  switch (Kind) {
  case OperandKind::Temp:
    return 'r';
  case OperandKind::Input:
    return 'v';
  case OperandKind::Output:
    return 'o';
  case OperandKind::Resource:
    return 't';
  case OperandKind::Sampler:
    return 's';
  case OperandKind::Immediate32:
    llvm_unreachable("immediates have no register prefix");
  }
  llvm_unreachable("unhandled OperandKind");
}

static void printComponents(const Operand &Op, llvm::raw_ostream &OS) {
  if (Op.SelectMode == ComponentSelectMode::None)
    return;

  static const char Names[] = "xyzw";
  OS << '.';
  if (Op.SelectMode == ComponentSelectMode::Mask) {
    for (unsigned I = 0; I < 4; ++I)
      if (Op.WriteMask & (1 << I))
        OS << Names[I];
    return;
  }
  for (unsigned I = 0; I < 4; ++I)
    OS << Names[Op.Swizzle[I]];
}

static void printOperand(const Operand &Op, llvm::raw_ostream &OS) {
  if (Op.Negate)
    OS << '-';
  if (Op.Abs)
    OS << '|';

  if (Op.Kind == OperandKind::Immediate32) {
    OS << "l(";
    llvm::ListSeparator Sep(", ");
    for (uint32_t Bits : Op.ImmediateValues) {
      float F;
      static_assert(sizeof(F) == sizeof(Bits));
      memcpy(&F, &Bits, sizeof(F));
      OS << Sep << llvm::format("%g", static_cast<double>(F));
    }
    OS << ')';
  } else {
    OS << registerPrefix(Op.Kind) << Op.RegisterIndex;
    printComponents(Op, OS);
  }

  if (Op.Abs)
    OS << '|';
}

void feme::dxbc::printAssembly(llvm::ArrayRef<Instruction> Program,
                               llvm::raw_ostream &OS) {
  for (const Instruction &Inst : Program) {
    const OpcodeInfo &Info = getOpcodeInfo(Inst.Op);
    OS << Info.Mnemonic;
    if (Inst.Saturate)
      OS << "_sat";

    switch (Info.Kind) {
    case InstructionKind::DclGlobalFlags: {
      OS << ' ';
      llvm::ListSeparator Sep(" | ");
      for (const std::string &Flag : Inst.Keywords)
        OS << Sep << Flag;
      break;
    }
    case InstructionKind::DclTemps:
      OS << ' ' << Inst.Immediates[0];
      break;
    case InstructionKind::DclResource: {
      OS << " (";
      llvm::ListSeparator Sep(",");
      for (const std::string &Ty : Inst.Keywords)
        OS << Sep << Ty;
      OS << ") ";
      printOperand(Inst.Operands[0], OS);
      break;
    }
    case InstructionKind::DclSampler:
      OS << ' ';
      printOperand(Inst.Operands[0], OS);
      if (!Inst.Keywords.empty())
        OS << ' ' << Inst.Keywords[0];
      break;
    case InstructionKind::DclInputPS:
      OS << ' ';
      if (!Inst.Keywords.empty())
        OS << Inst.Keywords[0] << ' ';
      printOperand(Inst.Operands[0], OS);
      break;
    case InstructionKind::NoOperand:
      break;
    default: {
      llvm::ListSeparator Sep(", ");
      OS << ' ';
      for (const Operand &Op : Inst.Operands) {
        OS << Sep;
        printOperand(Op, OS);
      }
      break;
    }
    }
    OS << '\n';
  }
}
