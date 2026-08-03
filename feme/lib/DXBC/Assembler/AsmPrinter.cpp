//===- AsmPrinter.cpp - DXBC assembly text emission ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements printAssembly. Everything printed here comes either from a
// numeric field of Instruction/Operand or from Instruction::Keywords, which
// records the source spelling of each keyword Parser folded into the
// control bits -- so no keyword table has to be inverted, and re-parsing the
// output reproduces the same Program.
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/AsmPrinter.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace feme::dxbc;

static constexpr char ComponentNames[] = "xyzw";

namespace {
/// Where in an instruction's operand list an operand appears, which decides
/// whether a component suffix reads back as a write mask or as a source
/// swizzle/single-component select.
enum class OperandRole { Destination, Source };
} // namespace

static void printOperand(const Operand &Op, OperandRole Role,
                         llvm::raw_ostream &OS);

/// Prints an operand's `{...}` modifier list, which carries everything the
/// bare `-`/`| |`/`.xyzw` syntax cannot express: an explicit component
/// count, an explicit selection mode, minimum precision, and non-uniform
/// indexing.
static void printOperandModifiers(const Operand &Op, OperandRole Role,
                                  bool PrintedComponentSuffix,
                                  llvm::raw_ostream &OS) {
  llvm::SmallVector<llvm::StringRef, 4> Modifiers;

  // An immediate's component count and selection mode both follow from how
  // many values it lists, so neither is ever spelled out.
  bool IsImmediate = Op.Kind == OperandKind::Immediate32 ||
                     Op.Kind == OperandKind::Immediate64;

  // A printed component suffix already implies a four-component operand, so
  // only spell the count out when it cannot be inferred.
  if (!IsImmediate && !PrintedComponentSuffix &&
      Op.Components != getDefaultComponentCount(Op.Kind)) {
    switch (Op.Components) {
    case ComponentCount::Zero:
      Modifiers.push_back("comp0");
      break;
    case ComponentCount::One:
      Modifiers.push_back("comp1");
      break;
    case ComponentCount::Four:
      Modifiers.push_back("comp4");
      break;
    }
  }

  if (!IsImmediate && Op.Components == ComponentCount::Four) {
    ComponentSelectMode Implied = Role == OperandRole::Destination
                                      ? ComponentSelectMode::Mask
                                      : ComponentSelectMode::Swizzle;
    if (Role == OperandRole::Source &&
        Op.SelectMode == ComponentSelectMode::Select1)
      Implied = ComponentSelectMode::Select1;
    if (Op.SelectMode != Implied) {
      switch (Op.SelectMode) {
      case ComponentSelectMode::Mask:
        Modifiers.push_back("mask");
        break;
      case ComponentSelectMode::Swizzle:
        Modifiers.push_back("swizzle");
        break;
      case ComponentSelectMode::Select1:
        Modifiers.push_back("select1");
        break;
      }
    }
  }

  switch (Op.Precision) {
  case MinPrecision::Default:
    break;
  case MinPrecision::Float16:
    Modifiers.push_back("min16f");
    break;
  case MinPrecision::Float2_8:
    Modifiers.push_back("min2_8f");
    break;
  case MinPrecision::SInt16:
    Modifiers.push_back("min16i");
    break;
  case MinPrecision::UInt16:
    Modifiers.push_back("min16u");
    break;
  }
  if (Op.NonUniform)
    Modifiers.push_back("nonuniform");

  if (Modifiers.empty())
    return;
  OS << '{';
  llvm::ListSeparator Sep(",");
  for (llvm::StringRef Modifier : Modifiers)
    OS << Sep << Modifier;
  OS << '}';
}

/// Prints an operand's `.xyzw` component suffix, returning true if one was
/// printed (an empty write mask has no spelling).
static bool printComponents(const Operand &Op, llvm::raw_ostream &OS) {
  if (Op.Components != ComponentCount::Four)
    return false;
  switch (Op.SelectMode) {
  case ComponentSelectMode::Mask:
    if (Op.WriteMask == 0)
      return false;
    OS << '.';
    for (unsigned I = 0; I < 4; ++I)
      if (Op.WriteMask & (1 << I))
        OS << ComponentNames[I];
    return true;
  case ComponentSelectMode::Swizzle:
    OS << '.';
    for (unsigned I = 0; I < 4; ++I)
      OS << ComponentNames[Op.Swizzle[I] & 3];
    return true;
  case ComponentSelectMode::Select1:
    OS << '.' << ComponentNames[Op.SelectedComponent & 3];
    return true;
  }
  llvm_unreachable("unhandled ComponentSelectMode");
}

static void printIndex(const OperandIndex &Index, llvm::raw_ostream &OS) {
  switch (Index.Rep) {
  case OperandIndex::Representation::Immediate32:
  case OperandIndex::Representation::Immediate64:
    OS << Index.Value;
    return;
  case OperandIndex::Representation::Relative:
    printOperand(*Index.Relative, OperandRole::Source, OS);
    return;
  case OperandIndex::Representation::Immediate32PlusRelative:
    OS << Index.Value << " + ";
    printOperand(*Index.Relative, OperandRole::Source, OS);
    return;
  }
}

static void printOperand(const Operand &Op, OperandRole Role,
                         llvm::raw_ostream &OS) {
  bool PrintedComponentSuffix = false;
  if (Op.Negate)
    OS << '-';
  if (Op.Abs)
    OS << '|';

  if (Op.Kind == OperandKind::Immediate32) {
    OS << "l(";
    llvm::ListSeparator Sep(", ");
    for (uint32_t Bits : Op.ImmediateValues)
      OS << Sep << llvm::format("0x%08X", Bits);
    OS << ')';
  } else if (Op.Kind == OperandKind::Immediate64) {
    OS << "d(";
    llvm::ListSeparator Sep(", ");
    for (size_t I = 0; I + 1 < Op.ImmediateValues.size(); I += 2) {
      uint64_t Bits = (static_cast<uint64_t>(Op.ImmediateValues[I]) << 32) |
                      Op.ImmediateValues[I + 1];
      OS << Sep << llvm::format("0x%016llX", Bits);
    }
    OS << ')';
  } else {
    OS << getOperandKindSpelling(Op.Kind);
    size_t First = 0;
    // A leading immediate index is spelled as part of the register name
    // (`r0`, `cb2`); anything else goes in brackets.
    if (!Op.Indices.empty() &&
        Op.Indices[0].Rep == OperandIndex::Representation::Immediate32) {
      OS << Op.Indices[0].Value;
      First = 1;
    }
    for (size_t I = First, E = Op.Indices.size(); I != E; ++I) {
      OS << '[';
      printIndex(Op.Indices[I], OS);
      OS << ']';
    }
    PrintedComponentSuffix = printComponents(Op, OS);
  }

  printOperandModifiers(Op, Role, PrintedComponentSuffix, OS);

  if (Op.Abs)
    OS << '|';
}

static void printOperands(const Instruction &Inst, llvm::raw_ostream &OS) {
  unsigned NumDst = getOpcodeInfo(Inst.Op).NumDst;
  llvm::ListSeparator Sep(", ");
  for (size_t I = 0, E = Inst.Operands.size(); I != E; ++I) {
    OS << Sep;
    printOperand(Inst.Operands[I],
                 I < NumDst ? OperandRole::Destination : OperandRole::Source,
                 OS);
  }
}

static void printTrailingCounts(llvm::ArrayRef<uint32_t> Values,
                                bool AlreadyPrintedSomething,
                                llvm::raw_ostream &OS) {
  for (uint32_t Value : Values) {
    if (AlreadyPrintedSomething)
      OS << ", ";
    AlreadyPrintedSomething = true;
    OS << Value;
  }
}

/// Prints the modifiers that appear between a mnemonic and its operands.
static void printInstructionModifiers(const Instruction &Inst,
                                      llvm::ArrayRef<std::string> Prefix,
                                      llvm::raw_ostream &OS) {
  const OpcodeInfo &Info = getOpcodeInfo(Inst.Op);
  for (const std::string &Keyword : Prefix) {
    // A multisample count is spelled as `(N)` directly after the mnemonic;
    // Parser records it in Keywords as the bare number.
    if ((Info.Flags & OF_SampleCount) && llvm::all_of(Keyword, llvm::isDigit))
      OS << '(' << Keyword << ')';
    else
      OS << ' ' << Keyword;
  }

  if (Inst.PreciseMask) {
    OS << " precise(";
    for (unsigned I = 0; I < 4; ++I)
      if (Inst.PreciseMask & (1 << I))
        OS << ComponentNames[I];
    OS << ')';
  }
  if (Inst.HasSampleOffsets)
    OS << " aoffimmi(" << int(Inst.SampleOffsets[0]) << ", "
       << int(Inst.SampleOffsets[1]) << ", " << int(Inst.SampleOffsets[2])
       << ')';
}

/// Prints a `(x, y, z, w)` resource return-type quadruple from the last four
/// entries of \p Keywords.
static void printReturnTypes(llvm::ArrayRef<std::string> Keywords,
                             llvm::raw_ostream &OS) {
  OS << '(';
  llvm::ListSeparator Sep(", ");
  for (const std::string &Type : Keywords.take_back(4))
    OS << Sep << Type;
  OS << ')';
}

static void printInstruction(const Instruction &Inst, llvm::raw_ostream &OS) {
  const OpcodeInfo &Info = getOpcodeInfo(Inst.Op);
  llvm::ArrayRef<std::string> Keywords(Inst.Keywords);

  if (Info.Kind == InstructionKind::RawTokens) {
    OS << ".dword ";
    llvm::ListSeparator Sep(", ");
    for (uint32_t Word : Inst.ExtraDWords)
      OS << Sep << llvm::format("0x%08X", Word);
    OS << '\n';
    return;
  }

  OS << Info.Mnemonic;
  if (Inst.Saturate)
    OS << "_sat";

  switch (Info.Kind) {
  case InstructionKind::Generic: {
    printInstructionModifiers(Inst, {}, OS);
    if (Inst.HasResourceDim) {
      OS << " resource_dim(" << Keywords.front();
      if (Inst.ResourceStride)
        OS << ", " << Inst.ResourceStride;
      OS << ')';
    }
    if (Inst.HasResourceReturnType) {
      OS << " resource_return_type";
      printReturnTypes(Keywords, OS);
    }
    if (!Inst.Operands.empty()) {
      OS << ' ';
      printOperands(Inst, OS);
    }
    // Trailing DWORDs always follow a comma, whether or not the
    // instruction had operands, so that they re-parse the same way.
    printTrailingCounts(Inst.ExtraDWords, /*AlreadyPrintedSomething=*/true, OS);
    break;
  }
  case InstructionKind::FlagList: {
    OS << ' ';
    llvm::ListSeparator Sep(" | ");
    for (const std::string &Flag : Inst.Keywords)
      OS << Sep << Flag;
    break;
  }
  case InstructionKind::ControlEnum:
    OS << ' ' << Inst.Keywords.front();
    break;
  case InstructionKind::ControlCount:
    OS << ' ' << (Inst.Controls >> 11);
    break;
  case InstructionKind::Counts:
    OS << ' ';
    printTrailingCounts(Inst.ExtraDWords, /*AlreadyPrintedSomething=*/false,
                        OS);
    break;
  case InstructionKind::Float: {
    // Print a spelling that lexes back as a float rather than as an
    // integer, so the value round-trips through its bit pattern.
    std::string Text;
    llvm::raw_string_ostream TextOS(Text);
    TextOS << llvm::format(
        "%.9g", double(llvm::bit_cast<float>(Inst.ExtraDWords.front())));
    if (Text.find_first_of(".eE") == std::string::npos)
      Text += ".0";
    OS << ' ' << Text;
    break;
  }
  case InstructionKind::DclIndexableTemp:
    OS << " x" << Inst.ExtraDWords[0] << '[' << Inst.ExtraDWords[1] << "], "
       << Inst.ExtraDWords[2];
    break;
  case InstructionKind::Operand:
    printInstructionModifiers(Inst, Keywords, OS);
    OS << ' ';
    printOperands(Inst, OS);
    printTrailingCounts(Inst.ExtraDWords, /*AlreadyPrintedSomething=*/true, OS);
    break;
  case InstructionKind::OperandSystemValue:
    OS << ' ';
    printOperands(Inst, OS);
    OS << ", " << Inst.Keywords.back();
    break;
  case InstructionKind::DclInputPS:
    OS << ' ' << Inst.Keywords.front() << ' ';
    printOperands(Inst, OS);
    break;
  case InstructionKind::DclInputPSSystemValue:
    OS << ' ' << Inst.Keywords.front() << ' ';
    printOperands(Inst, OS);
    OS << ", " << Inst.Keywords.back();
    break;
  case InstructionKind::DclTypedResource:
    printInstructionModifiers(Inst, Keywords.drop_back(4), OS);
    OS << ' ';
    printReturnTypes(Keywords, OS);
    OS << ' ';
    printOperands(Inst, OS);
    // The first trailing DWORD is the packed return-type quadruple that was
    // just printed by name.
    printTrailingCounts(llvm::ArrayRef<uint32_t>(Inst.ExtraDWords).drop_front(),
                        /*AlreadyPrintedSomething=*/true, OS);
    break;
  case InstructionKind::DclImmediateConstantBuffer: {
    OS << " {";
    llvm::ListSeparator Sep(", ");
    for (uint32_t Word : Inst.ExtraDWords)
      OS << Sep << llvm::format("0x%08X", Word);
    OS << '}';
    break;
  }
  case InstructionKind::RawTokens:
    llvm_unreachable("handled above");
  }
  OS << '\n';
}

void feme::dxbc::printAssembly(const Program &Program, llvm::raw_ostream &OS) {
  if (Program.HasHeader) {
    static constexpr llvm::StringRef Stages[] = {"pixel", "vertex", "geometry",
                                                 "hull",  "domain", "compute"};
    llvm::StringRef Stage = Program.ProgramType < std::size(Stages)
                                ? Stages[Program.ProgramType]
                                : llvm::StringRef("pixel");
    OS << ".shader_model " << Stage << ' ' << unsigned(Program.MajorVersion)
       << ' ' << unsigned(Program.MinorVersion) << '\n';
  }
  for (const Instruction &Inst : Program.Instructions)
    printInstruction(Inst, OS);
}
