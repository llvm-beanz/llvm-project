//===- Parser.cpp - DXBC assembler parser --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Parser.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>

using namespace feme::dxbc;

namespace {

/// Recursive-descent parser for one DXBC assembly translation unit.
/// Statement-oriented: each source line is exactly one Instruction, so
/// error recovery is not attempted mid-statement -- the first error found
/// anywhere aborts the whole parse (see parseAssembly), which is
/// appropriate for a tool whose job is validating/encoding a single,
/// deliberately-authored test fixture rather than an IDE-style parser that
/// must keep going after an error.
class ParserImpl {
public:
  explicit ParserImpl(llvm::StringRef Source) : Lex(Source) {
    Current = Lex.next();
  }

  llvm::Expected<std::vector<Instruction>> parseProgram() {
    std::vector<Instruction> Program;
    while (true) {
      if (Current.Kind == TokenKind::EndOfStatement) {
        advance();
        continue;
      }
      if (Current.Kind == TokenKind::Eof)
        break;

      llvm::Expected<Instruction> Inst = parseInstruction();
      if (!Inst)
        return Inst.takeError();
      Program.push_back(std::move(*Inst));

      if (Current.Kind != TokenKind::EndOfStatement &&
          Current.Kind != TokenKind::Eof)
        return error("expected end of line after instruction");
      if (Current.Kind == TokenKind::EndOfStatement)
        advance();
    }
    return Program;
  }

private:
  Lexer Lex;
  Token Current;

  void advance() { Current = Lex.next(); }

  llvm::Error error(const llvm::Twine &Message) {
    return llvm::createStringError(
        llvm::formatv("{0}:{1}: error: {2} (near '{3}')", Current.Line,
                      Current.Column, Message.str(), Current.Spelling)
            .str());
  }

  llvm::Expected<Token> expect(TokenKind Kind, const llvm::Twine &What) {
    if (Current.Kind != Kind)
      return error("expected " + What);
    Token Tok = Current;
    advance();
    return Tok;
  }

  /// Parses one full statement: a mnemonic (with optional `_sat` suffix)
  /// followed by whatever operand grammar its InstructionKind requires.
  llvm::Expected<Instruction> parseInstruction() {
    if (Current.Kind != TokenKind::Identifier)
      return error("expected instruction mnemonic");

    llvm::StringRef Mnemonic = Current.Spelling;
    bool Saturate = false;
    const Opcode *Op = lookupOpcode(Mnemonic);
    if (!Op && Mnemonic.consume_back("_sat")) {
      Op = lookupOpcode(Mnemonic);
      Saturate = Op != nullptr;
    }
    if (!Op)
      return error("unknown mnemonic '" + Current.Spelling + "'");
    advance();

    Instruction Inst;
    Inst.Op = *Op;
    Inst.Saturate = Saturate;
    const OpcodeInfo &Info = getOpcodeInfo(Inst.Op);

    if (Saturate && !supportsSaturate(Inst.Op))
      return error("'_sat' is only valid on floating-point ALU instructions");

    switch (Info.Kind) {
    case InstructionKind::ALU1:
      if (llvm::Error E = parseOperandList(Inst, 2))
        return std::move(E);
      break;
    case InstructionKind::ALU2:
      if (llvm::Error E = parseOperandList(Inst, 3))
        return std::move(E);
      break;
    case InstructionKind::ALU3:
      if (llvm::Error E = parseOperandList(Inst, 4))
        return std::move(E);
      break;
    case InstructionKind::NoOperand:
      break;
    case InstructionKind::Discard:
      if (llvm::Error E = parseOperandList(Inst, 1))
        return std::move(E);
      break;
    case InstructionKind::Sample:
      if (llvm::Error E = parseOperandList(Inst, 4))
        return std::move(E);
      break;
    case InstructionKind::Load:
      if (llvm::Error E = parseOperandList(Inst, 3))
        return std::move(E);
      break;
    case InstructionKind::DclGlobalFlags:
      if (llvm::Error E = parseGlobalFlags(Inst))
        return std::move(E);
      break;
    case InstructionKind::DclTemps: {
      llvm::Expected<Token> Count = expect(TokenKind::Integer, "temp count");
      if (!Count)
        return Count.takeError();
      Inst.Immediates.push_back(
          llvm::APInt(64, Count->Spelling, 10).getZExtValue());
      break;
    }
    case InstructionKind::DclResource:
      if (llvm::Error E = parseDclResource(Inst))
        return std::move(E);
      break;
    case InstructionKind::DclSampler:
      if (llvm::Error E = parseDclSampler(Inst))
        return std::move(E);
      break;
    case InstructionKind::DclInput:
      if (llvm::Error E = parseOperandList(Inst, 1))
        return std::move(E);
      break;
    case InstructionKind::DclInputPS:
      if (llvm::Error E = parseDclInputPS(Inst))
        return std::move(E);
      break;
    case InstructionKind::DclOutput:
      if (llvm::Error E = parseOperandList(Inst, 1))
        return std::move(E);
      break;
    }
    return Inst;
  }

  static bool supportsSaturate(Opcode Op) {
    // Only floating-point-result ALU mnemonics accept `_sat`; the
    // integer/bitwise ones in Opcodes.def do not saturate in real DXBC
    // assembly, since clamping to [0,1] is only meaningful for floating
    // point results.
    switch (Op) {
    case Opcode::Not:
    case Opcode::INeg:
    case Opcode::ItoF:
    case Opcode::FtoI:
    case Opcode::UtoF:
    case Opcode::FtoU:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor:
    case Opcode::IEq:
    case Opcode::INe:
    case Opcode::IGe:
    case Opcode::ILt:
    case Opcode::IAdd:
    case Opcode::IMad:
      return false;
    default:
      return true;
    }
  }

  /// Parses exactly \p Count comma-separated register operands.
  llvm::Error parseOperandList(Instruction &Inst, unsigned Count) {
    for (unsigned I = 0; I < Count; ++I) {
      if (I != 0) {
        if (llvm::Error E = expect(TokenKind::Comma, "','").takeError())
          return E;
      }
      llvm::Expected<Operand> Op = parseOperand();
      if (!Op)
        return Op.takeError();
      Inst.Operands.push_back(std::move(*Op));
    }
    return llvm::Error::success();
  }

  /// operand := ['-'] ('|' register '|' | register)
  llvm::Expected<Operand> parseOperand() {
    bool Negate = false;
    bool Abs = false;
    if (Current.Kind == TokenKind::Minus) {
      Negate = true;
      advance();
    }
    if (Current.Kind == TokenKind::Pipe) {
      Abs = true;
      advance();
    }

    llvm::Expected<Operand> Op = parseRegister();
    if (!Op)
      return Op.takeError();
    Op->Negate = Negate;
    Op->Abs = Abs;

    if (Abs) {
      if (llvm::Error E = expect(TokenKind::Pipe, "closing '|'").takeError())
        return std::move(E);
    }
    return Op;
  }

  /// register := identifier ['.' identifier] | 'l' '(' float [',' float]*3
  /// ')'
  llvm::Expected<Operand> parseRegister() {
    if (Current.Kind != TokenKind::Identifier)
      return error("expected a register or immediate operand");

    if (Current.Spelling == "l")
      return parseImmediate();

    llvm::StringRef Name = Current.Spelling;
    OperandKind Kind;
    switch (Name.front()) {
    case 'r':
      Kind = OperandKind::Temp;
      break;
    case 'v':
      Kind = OperandKind::Input;
      break;
    case 'o':
      Kind = OperandKind::Output;
      break;
    case 't':
      Kind = OperandKind::Resource;
      break;
    case 's':
      Kind = OperandKind::Sampler;
      break;
    default:
      return error("expected register name (r/v/o/t/s followed by a digit)");
    }
    llvm::StringRef IndexText = Name.drop_front();
    unsigned Index;
    if (IndexText.empty() || IndexText.getAsInteger(10, Index))
      return error("expected a numeric register index after '" +
                   llvm::Twine(Name.front()) + "'");
    advance();

    Operand Op;
    Op.Kind = Kind;
    Op.RegisterIndex = Index;

    if (Current.Kind == TokenKind::Dot) {
      advance();
      llvm::Expected<Token> Comp =
          expect(TokenKind::Identifier, "swizzle/mask (e.g. 'xyzw')");
      if (!Comp)
        return Comp.takeError();
      if (llvm::Error E = applyComponents(Op, Comp->Spelling))
        return std::move(E);
    }
    return Op;
  }

  /// Interprets a component suffix (e.g. "xyzw", "xyxx", "x") as either a
  /// destination write mask (every letter distinct, in x/y/z/w order,
  /// matching D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) or a source swizzle
  /// (any order/repeats, matching …SWIZZLE_MODE); Encoder.cpp picks whichever
  /// is valid based on whether the operand is used as a destination.
  llvm::Error applyComponents(Operand &Op, llvm::StringRef Components) {
    if (Components.empty() || Components.size() > 4)
      return error("swizzle/mask must have between 1 and 4 components");

    static const char *Names = "xyzw";
    uint8_t Mask = 0;
    uint8_t Swizzle[4] = {0, 1, 2, 3};
    bool Monotonic = true; // true if this could be interpreted as a mask
    int PrevComp = -1;
    for (unsigned I = 0; I < Components.size(); ++I) {
      const char *Where = strchr(Names, Components[I]);
      if (!Where || Components[I] == '\0')
        return error("invalid swizzle/mask component '" +
                     llvm::Twine(Components[I]) + "' (expected x/y/z/w)");
      int Comp = static_cast<int>(Where - Names);
      Mask |= (1 << Comp);
      Swizzle[I] = static_cast<uint8_t>(Comp);
      if (Comp <= PrevComp)
        Monotonic = false;
      PrevComp = Comp;
    }
    // A single-component suffix (e.g. ".x") reads as every slot replicating
    // that one component when used as a source, matching how `fxc`-style
    // assembly represents scalar sources; Encoder.cpp/AsmPrinter.cpp
    // consult NumExplicitComponents to tell this apart from a
    // single-component *mask*.
    if (Components.size() == 1) {
      for (uint8_t &S : Swizzle)
        S = Swizzle[0];
    }

    Op.WriteMask = Components.size() == 1 ? (1 << Swizzle[0]) : Mask;
    for (unsigned I = 0; I < 4; ++I)
      Op.Swizzle[I] = Swizzle[I];
    // A valid destination write mask is always a strictly-increasing,
    // repeat-free subset of x/y/z/w (matching D3D10_SB_OPERAND_4_COMPONENT_
    // MASK_MODE), so Monotonic alone is enough to tell a mask ('.xz') from
    // a swizzle ('.xx', '.yx') without needing to know whether this operand
    // will be used as a destination or a source.
    Op.SelectMode =
        Monotonic ? ComponentSelectMode::Mask : ComponentSelectMode::Swizzle;
    return llvm::Error::success();
  }

  /// immediate := 'l' '(' float (',' float){0,3} ')'
  llvm::Expected<Operand> parseImmediate() {
    advance(); // 'l'
    if (llvm::Error E = expect(TokenKind::LParen, "'('").takeError())
      return std::move(E);

    Operand Op;
    Op.Kind = OperandKind::Immediate32;
    Op.SelectMode = ComponentSelectMode::None;
    while (true) {
      bool Negate = false;
      if (Current.Kind == TokenKind::Minus) {
        Negate = true;
        advance();
      }
      if (Current.Kind != TokenKind::Float &&
          Current.Kind != TokenKind::Integer)
        return error("expected a numeric literal inside 'l(...)'");
      double Value;
      llvm::StringRef Text = Current.Spelling;
      Text.consume_back("f");
      Text.consume_back("F");
      if (Text.getAsDouble(Value))
        return error("malformed numeric literal '" + Current.Spelling + "'");
      if (Negate)
        Value = -Value;
      float F = static_cast<float>(Value);
      uint32_t Bits;
      static_assert(sizeof(Bits) == sizeof(F));
      memcpy(&Bits, &F, sizeof(Bits));
      Op.ImmediateValues.push_back(Bits);
      advance();

      if (Current.Kind == TokenKind::Comma) {
        advance();
        continue;
      }
      break;
    }
    if (Op.ImmediateValues.size() != 1 && Op.ImmediateValues.size() != 4)
      return error("'l(...)' must have exactly 1 or 4 components");
    if (llvm::Error E = expect(TokenKind::RParen, "')'").takeError())
      return std::move(E);
    return Op;
  }

  /// dcl_globalFlags := identifier ('|' identifier)*
  llvm::Error parseGlobalFlags(Instruction &Inst) {
    while (true) {
      llvm::Expected<Token> Flag =
          expect(TokenKind::Identifier, "a global flag name");
      if (!Flag)
        return Flag.takeError();
      Inst.Keywords.push_back(Flag->Spelling.str());
      if (Current.Kind == TokenKind::Pipe) {
        advance();
        continue;
      }
      break;
    }
    return llvm::Error::success();
  }

  /// dcl_resource_* := '(' identifier (',' identifier){3} ')' register
  llvm::Error parseDclResource(Instruction &Inst) {
    if (llvm::Error E = expect(TokenKind::LParen, "'('").takeError())
      return E;
    for (unsigned I = 0; I < 4; ++I) {
      if (I != 0) {
        if (llvm::Error E = expect(TokenKind::Comma, "','").takeError())
          return E;
      }
      llvm::Expected<Token> Ty =
          expect(TokenKind::Identifier, "a resource return type");
      if (!Ty)
        return Ty.takeError();
      Inst.Keywords.push_back(Ty->Spelling.str());
    }
    if (llvm::Error E = expect(TokenKind::RParen, "')'").takeError())
      return E;
    return parseOperandList(Inst, 1);
  }

  /// dcl_sampler := register ['comparison']
  llvm::Error parseDclSampler(Instruction &Inst) {
    if (llvm::Error E = parseOperandList(Inst, 1))
      return E;
    if (Current.Kind == TokenKind::Identifier &&
        Current.Spelling == "comparison") {
      Inst.Keywords.push_back("comparison");
      advance();
    }
    return llvm::Error::success();
  }

  /// dcl_input_ps := [interpolation-mode] register
  llvm::Error parseDclInputPS(Instruction &Inst) {
    static const llvm::StringRef Modes[] = {
        "constant",
        "linear",
        "linear_centroid",
        "linear_noperspective",
        "linear_noperspective_centroid",
        "linear_sample",
        "linear_noperspective_sample",
    };
    if (Current.Kind == TokenKind::Identifier &&
        llvm::is_contained(Modes, Current.Spelling)) {
      Inst.Keywords.push_back(Current.Spelling.str());
      advance();
    }
    return parseOperandList(Inst, 1);
  }
};

} // namespace

llvm::Expected<std::vector<Instruction>>
feme::dxbc::parseAssembly(llvm::StringRef Source) {
  ParserImpl Parser(Source);
  return Parser.parseProgram();
}
