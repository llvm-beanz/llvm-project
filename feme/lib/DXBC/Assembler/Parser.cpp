//===- Parser.cpp - DXBC assembler parser --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the DXBC assembly grammar. Everything a mnemonic's grammar
// says about the *meaning* of a keyword (which control bit a global flag
// sets, which DWORD value a system-value name has, ...) is resolved here,
// so Encoder.cpp only has to lay out tokens; the original keyword spellings
// are kept in Instruction::Keywords so AsmPrinter.cpp can reproduce the
// source text without inverting any of these tables.
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Parser.h"

#include "feme/DXBC/Assembler/Lexer.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"

#include <cstring>

using namespace feme::dxbc;

namespace {

/// A named value in one of the enumerated fields the grammar spells as a
/// keyword (global flags, system-value names, interpolation modes, ...).
struct KeywordValue {
  llvm::StringRef Name;
  uint32_t Value;
};

// Opcode-specific control bits are all in the [23:11] range, so every table
// below stores values pre-shifted into place where it is a control field,
// and stores the plain DWORD value where it is a trailing token field.
constexpr unsigned ControlShift = 11;
/// Widest value the opcode-specific control range ([23:11]) can hold.
constexpr uint64_t MaxControlValue = 0x1FFF;

// D3D10_SB_GLOBAL_FLAG_* / D3D11[_1]_SB_GLOBAL_FLAG_* /
// D3D12_SB_GLOBAL_FLAG_ALL_RESOURCES_BOUND.
constexpr KeywordValue GlobalFlags[] = {
    {"refactoringAllowed", 1u << 11},
    {"enableDoublePrecisionFloatOps", 1u << 12},
    {"forceEarlyDepthStencil", 1u << 13},
    {"enableRawAndStructuredBuffers", 1u << 14},
    {"skipOptimization", 1u << 15},
    {"enableMinimumPrecision", 1u << 16},
    {"enableDoubleExtensions", 1u << 17},
    {"enableShaderExtensions", 1u << 18},
    {"allResourcesBound", 1u << 19},
    // `fxc` spells the two SM5.1 extension flags with an "11_1" infix.
    {"enable11_1DoubleExtensions", 1u << 17},
    {"enable11_1ShaderExtensions", 1u << 18},
};

// D3D11_SB_SYNC_*.
constexpr KeywordValue SyncFlags[] = {
    {"threads", 1u << 11},
    {"tgsm", 1u << 12},
    {"uav_group", 1u << 13},
    {"uav_global", 1u << 14},
};

// D3D11_SB_GLOBALLY_COHERENT_ACCESS / D3D11_SB_RASTERIZER_ORDERED_ACCESS /
// D3D11_SB_UAV_HAS_ORDER_PRESERVING_COUNTER.
constexpr KeywordValue UAVFlags[] = {
    {"globallyCoherent", 0x00010000},
    {"rasterizerOrdered", 0x00020000},
    {"hasOrderPreservingCounter", 0x00800000},
};

// D3D10_SB_INTERPOLATION_MODE.
constexpr KeywordValue InterpolationModes[] = {
    {"undefined", 0},
    {"constant", 1},
    {"linear", 2},
    {"linearCentroid", 3},
    {"linearNoPerspective", 4},
    {"linearNoPerspectiveCentroid", 5},
    {"linearSample", 6},
    {"linearNoPerspectiveSample", 7},
};

// D3D10_SB_NAME.
constexpr KeywordValue SystemValueNames[] = {
    {"position", 1},
    {"clipDistance", 2},
    {"cullDistance", 3},
    {"renderTargetArrayIndex", 4},
    {"viewportArrayIndex", 5},
    {"vertexID", 6},
    {"primitiveID", 7},
    {"instanceID", 8},
    {"isFrontFace", 9},
    {"sampleIndex", 10},
    {"finalQuadUeq0EdgeTessFactor", 11},
    {"finalQuadVeq0EdgeTessFactor", 12},
    {"finalQuadUeq1EdgeTessFactor", 13},
    {"finalQuadVeq1EdgeTessFactor", 14},
    {"finalQuadUInsideTessFactor", 15},
    {"finalQuadVInsideTessFactor", 16},
    {"finalTriUeq0EdgeTessFactor", 17},
    {"finalTriVeq0EdgeTessFactor", 18},
    {"finalTriWeq0EdgeTessFactor", 19},
    {"finalTriInsideTessFactor", 20},
    {"finalLineDetailTessFactor", 21},
    {"finalLineDensityTessFactor", 22},
    {"barycentrics", 23},
    {"shadingRate", 24},
    {"cullPrimitive", 25},
    // `fxc` disassembly spells the same D3D10_SB_NAME values in snake_case.
    {"clip_distance", 2},
    {"cull_distance", 3},
    {"rendertarget_array_index", 4},
    {"viewport_array_index", 5},
    {"vertex_id", 6},
    {"primitive_id", 7},
    {"instance_id", 8},
    {"is_front_face", 9},
    {"sample_index", 10},
};

// D3D10_SB_PRIMITIVE. The `patchN` entries (N control points) are contiguous
// from D3D11_SB_PRIMITIVE_1_CONTROL_POINT_PATCH and are handled separately
// in parseControlEnum rather than spelled out 32 times here.
constexpr KeywordValue InputPrimitives[] = {
    {"point", 1},        {"line", 2},         {"triangle", 3},
    {"line_adj", 6},     {"triangle_adj", 7},
    // `fxc` spells the adjacency primitives without a separator.
    {"lineadj", 6},      {"triangleadj", 7},
};

// D3D10_SB_PRIMITIVE_TOPOLOGY.
constexpr KeywordValue OutputTopologies[] = {
    {"pointlist", 1},      {"linelist", 2},          {"linestrip", 3},
    {"trianglelist", 4},   {"trianglestrip", 5},     {"linelist_adj", 10},
    {"linestrip_adj", 11}, {"trianglelist_adj", 12}, {"trianglestrip_adj", 13},
};

// D3D11_SB_TESSELLATOR_DOMAIN.
constexpr KeywordValue TessellatorDomains[] = {
    {"domain_isoline", 1},
    {"domain_tri", 2},
    {"domain_quad", 3},
};

// D3D11_SB_TESSELLATOR_PARTITIONING.
constexpr KeywordValue TessellatorPartitionings[] = {
    {"partitioning_integer", 1},
    {"partitioning_pow2", 2},
    {"partitioning_fractional_odd", 3},
    {"partitioning_fractional_even", 4},
};

// D3D11_SB_TESSELLATOR_OUTPUT_PRIMITIVE.
constexpr KeywordValue TessellatorOutputPrimitives[] = {
    {"output_point", 1},
    {"output_line", 2},
    {"output_triangle_cw", 3},
    {"output_triangle_ccw", 4},
};

// D3D10_SB_RESOURCE_RETURN_TYPE.
constexpr KeywordValue ResourceReturnTypes[] = {
    {"unorm", 1}, {"snorm", 2},  {"sint", 3},      {"uint", 4},   {"float", 5},
    {"mixed", 6}, {"double", 7}, {"continued", 8}, {"unused", 9},
};

// D3D10_SB_RESOURCE_DIMENSION, for the `resource_dim(...)` extended opcode
// token. The declaration mnemonics spell the same values as suffixes.
constexpr KeywordValue ResourceDimensions[] = {
    {"unknown", 0},
    {"buffer", 1},
    {"texture1d", 2},
    {"texture2d", 3},
    {"texture2dms", 4},
    {"texture3d", 5},
    {"texturecube", 6},
    {"texture1darray", 7},
    {"texture2darray", 8},
    {"texture2dmsarray", 9},
    {"texturecubearray", 10},
    {"raw_buffer", 11},
    {"structured_buffer", 12},
};

// D3D10_SB_TOKENIZED_PROGRAM_TYPE, for the `.shader_model` directive.
constexpr KeywordValue ProgramTypes[] = {
    {"pixel", 0}, {"vertex", 1}, {"geometry", 2},
    {"hull", 3},  {"domain", 4}, {"compute", 5},
};

// The same D3D10_SB_TOKENIZED_PROGRAM_TYPE values, spelled the way `fxc`
// disassembly names a profile (`ps_5_0`, `cs_5_1`, ...).
constexpr KeywordValue ProfilePrefixes[] = {
    {"ps", 0}, {"vs", 1}, {"gs", 2}, {"hs", 3}, {"ds", 4}, {"cs", 5},
};

// The `sync` flag suffixes `fxc` folds into the mnemonic, in the order it
// spells them; the values match SyncFlags above.
constexpr KeywordValue SyncSuffixes[] = {
    {"_sat_ugroup", 1u << 13}, {"_ugroup", 1u << 13},
    {"_uglobal", 1u << 14},    {"_g", 1u << 12},
    {"_t", 1u << 11},
};

/// A control-field keyword `fxc` writes after a declaration's operand,
/// paired with the dxbc-as mnemonic that spells the same control bits.
struct KeywordMnemonic {
  llvm::StringRef Name;
  llvm::StringRef Mnemonic;
};

// D3D10_SB_SAMPLER_MODE.
constexpr KeywordMnemonic SamplerModes[] = {
    {"mode_default", "dcl_sampler"},
    {"mode_comparison", "dcl_sampler_comparison"},
    {"mode_mono", "dcl_sampler_mono"},
};

// D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN.
constexpr KeywordMnemonic ConstantBufferAccessPatterns[] = {
    {"immediateIndexed", "dcl_constantbuffer"},
    {"dynamicIndexed", "dcl_constantbuffer_dynamicIndexed"},
};

/// Operand storage classes `fxc` spells differently to dxbc-as. `fxc`
/// also upper-cases the four bindable classes in SM5.1 disassembly
/// (`CB0[0:0]`, `T0[3:7]`, `U0[0]`, `S0[2:4]`).
const llvm::StringRef *lookupOperandKindAlias(llvm::StringRef Spelling) {
  static const llvm::StringMap<llvm::StringRef> Aliases = {
      {"CB", "cb"},   {"T", "t"},           {"U", "u"},
      {"S", "s"},     {"G", "g"},           {"this", "thisPtr"},
      {"vCycleCounter", "cycleCounter"},
  };
  auto It = Aliases.find(Spelling);
  if (It == Aliases.end())
    return nullptr;
  return &It->second;
}

/// Looks up an operand storage class by either its dxbc-as spelling or one
/// of the `fxc` aliases above.
const OperandKind *lookupOperandKindOrAlias(llvm::StringRef Spelling) {
  if (const OperandKind *Kind = lookupOperandKind(Spelling))
    return Kind;
  if (const llvm::StringRef *Alias = lookupOperandKindAlias(Spelling))
    return lookupOperandKind(*Alias);
  return nullptr;
}

/// True for the system-generated registers that name a whole object in a
/// declaration but read as a single scalar value elsewhere. The remaining
/// zero-component kinds (`vForkInstanceID`, `vJoinInstanceID`,
/// `vThreadIDInGroupFlattened`, ...) keep their component count in both
/// positions, and `fxc` writes an explicit `.x` on them when read.
bool isScalarSystemValue(OperandKind Kind) {
  switch (Kind) {
  case OperandKind::InputPrimitiveID:
  case OperandKind::OutputControlPointID:
    return true;
  default:
    return false;
  }
}

const KeywordValue *findKeyword(llvm::ArrayRef<KeywordValue> Table,
                                llvm::StringRef Name) {
  for (const KeywordValue &Entry : Table)
    if (Entry.Name == Name)
      return &Entry;
  return nullptr;
}

/// Recursive-descent parser for one DXBC assembly translation unit.
/// Statement-oriented: each source line is exactly one instruction or
/// directive, so error recovery is not attempted mid-statement -- the first
/// error found anywhere aborts the whole parse (see parseAssembly), which is
/// appropriate for a tool whose job is validating/encoding a single,
/// deliberately-authored test fixture rather than an IDE-style parser that
/// must keep going after an error.
class ParserImpl {
public:
  explicit ParserImpl(llvm::StringRef Source) : Lex(Source) {
    Current = Lex.next();
  }

  llvm::Expected<Program> parseProgram() {
    Program Result;
    while (true) {
      if (Current.Kind == TokenKind::EndOfStatement) {
        advance();
        continue;
      }
      if (Current.Kind == TokenKind::Eof)
        break;

      if (Current.Kind == TokenKind::Dot) {
        if (llvm::Error E = parseDirective(Result))
          return std::move(E);
      } else if (parseShaderProfile(Result)) {
        // Consumed an `fxc`-style profile line; nothing more on this line.
      } else {
        llvm::Expected<Instruction> Inst = parseInstruction();
        if (!Inst)
          return Inst.takeError();
        Result.Instructions.push_back(std::move(*Inst));
      }

      if (Current.Kind != TokenKind::EndOfStatement &&
          Current.Kind != TokenKind::Eof)
        return error("expected end of line after statement");
      if (Current.Kind == TokenKind::EndOfStatement)
        advance();
    }
    return Result;
  }

private:
  Lexer Lex;
  Token Current;

  void advance() { Current = Lex.next(); }

  bool isIdentifier(llvm::StringRef Text) const {
    return Current.Kind == TokenKind::Identifier && Current.Spelling == Text;
  }

  bool consumeIdentifier(llvm::StringRef Text) {
    if (!isIdentifier(Text))
      return false;
    advance();
    return true;
  }

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

  llvm::Error expectToken(TokenKind Kind, const llvm::Twine &What) {
    return expect(Kind, What).takeError();
  }

  /// True if the '(' at \c Current opens a parenthesized integer rather
  /// than a resource return-type quadruple. Lexer is a value type, so this
  /// looks ahead by copying and restoring the whole lexing position.
  bool startsSampleCount() {
    Lexer SavedLex = Lex;
    Token SavedToken = Current;
    advance();
    bool IsInteger = Current.Kind == TokenKind::Integer;
    Lex = SavedLex;
    Current = SavedToken;
    return IsInteger;
  }

  //===--------------------------------------------------------------------===//
  // Literals
  //===--------------------------------------------------------------------===//

  /// Drops the type suffix Lexer allows on a numeric literal ("1.0f",
  /// "-4.35l"); the suffix only records how the source spelled the value.
  static llvm::StringRef dropNumericSuffix(llvm::StringRef Text) {
    // A hexadecimal literal's trailing 'f' is a digit, not a suffix.
    if (Text.starts_with("0x") || Text.starts_with("0X"))
      return Text;
    if (!Text.empty() && strchr("fFlL", Text.back()))
      Text = Text.drop_back();
    return Text;
  }

  /// Parses an unsigned integer literal (decimal or 0x-prefixed hex).
  llvm::Expected<uint64_t> parseInteger(const llvm::Twine &What) {
    if (Current.Kind != TokenKind::Integer)
      return error("expected " + What);
    llvm::StringRef Text = dropNumericSuffix(Current.Spelling);
    unsigned Radix = 10;
    if (Text.consume_front("0x") || Text.consume_front("0X"))
      Radix = 16;
    uint64_t Value;
    if (Text.getAsInteger(Radix, Value))
      return error("malformed integer literal '" + Current.Spelling + "'");
    advance();
    return Value;
  }

  /// Parses a signed integer literal.
  llvm::Expected<int64_t> parseSignedInteger(const llvm::Twine &What) {
    bool Negate = false;
    if (Current.Kind == TokenKind::Minus) {
      Negate = true;
      advance();
    }
    llvm::Expected<uint64_t> Value = parseInteger(What);
    if (!Value)
      return Value.takeError();
    int64_t Signed = static_cast<int64_t>(*Value);
    return Negate ? -Signed : Signed;
  }

  /// Parses a numeric literal, yielding its 32-bit encoding: a hexadecimal
  /// or decimal integer keeps its integer value, while a literal spelled
  /// with a '.' or an exponent is converted to its float32 bit pattern.
  /// That split is what lets a fixture say either `l(1.0)` or the exact
  /// `l(0x3F800000)` bit pattern it encodes to.
  llvm::Expected<uint32_t> parseValue32() {
    bool Negate = false;
    if (Current.Kind == TokenKind::Minus) {
      Negate = true;
      advance();
    }
    if (Current.Kind == TokenKind::Integer) {
      llvm::Expected<uint64_t> Value = parseInteger("a numeric literal");
      if (!Value)
        return Value.takeError();
      uint32_t Bits = static_cast<uint32_t>(*Value);
      return Negate ? static_cast<uint32_t>(-static_cast<int32_t>(Bits)) : Bits;
    }
    if (Current.Kind != TokenKind::Float)
      return error("expected a numeric literal");
    llvm::StringRef Text = dropNumericSuffix(Current.Spelling);
    double Value;
    if (Text.getAsDouble(Value))
      return error("malformed numeric literal '" + Current.Spelling + "'");
    advance();
    float F = static_cast<float>(Negate ? -Value : Value);
    return llvm::bit_cast<uint32_t>(F);
  }

  /// Parses a 64-bit numeric literal, yielding its encoding as a pair of
  /// DWORDs (high word first, matching the tokenized format).
  llvm::Error parseValue64(llvm::SmallVectorImpl<uint32_t> &Out) {
    bool Negate = false;
    if (Current.Kind == TokenKind::Minus) {
      Negate = true;
      advance();
    }
    uint64_t Bits;
    if (Current.Kind == TokenKind::Integer) {
      llvm::Expected<uint64_t> Value = parseInteger("a numeric literal");
      if (!Value)
        return Value.takeError();
      Bits = Negate ? static_cast<uint64_t>(-static_cast<int64_t>(*Value))
                    : *Value;
    } else if (Current.Kind == TokenKind::Float) {
      llvm::StringRef Text = dropNumericSuffix(Current.Spelling);
      double Value;
      if (Text.getAsDouble(Value))
        return error("malformed numeric literal '" + Current.Spelling + "'");
      advance();
      Bits = llvm::bit_cast<uint64_t>(Negate ? -Value : Value);
    } else {
      return error("expected a numeric literal");
    }
    Out.push_back(static_cast<uint32_t>(Bits >> 32));
    Out.push_back(static_cast<uint32_t>(Bits));
    return llvm::Error::success();
  }

  //===--------------------------------------------------------------------===//
  // Directives
  //===--------------------------------------------------------------------===//

  /// directive := '.shader_model' <type> <major> <minor>
  ///            | '.dword' <value> (',' <value>)*
  llvm::Error parseDirective(Program &Result) {
    advance(); // '.'
    if (Current.Kind != TokenKind::Identifier)
      return error("expected a directive name after '.'");
    llvm::StringRef Name = Current.Spelling;

    if (Name == "shader_model") {
      advance();
      if (Current.Kind != TokenKind::Identifier)
        return error("expected a shader stage name");
      const KeywordValue *Type = findKeyword(ProgramTypes, Current.Spelling);
      if (!Type)
        return error("unknown shader stage '" + Current.Spelling + "'");
      advance();
      llvm::Expected<uint64_t> Major = parseInteger("a major version");
      if (!Major)
        return Major.takeError();
      llvm::Expected<uint64_t> Minor = parseInteger("a minor version");
      if (!Minor)
        return Minor.takeError();
      Result.HasHeader = true;
      Result.ProgramType = static_cast<uint16_t>(Type->Value);
      Result.MajorVersion = static_cast<uint8_t>(*Major);
      Result.MinorVersion = static_cast<uint8_t>(*Minor);
      return llvm::Error::success();
    }

    if (Name == "dword") {
      advance();
      Instruction Inst;
      Inst.Op = Opcode::RawDWords;
      while (true) {
        llvm::Expected<uint32_t> Value = parseValue32();
        if (!Value)
          return Value.takeError();
        Inst.ExtraDWords.push_back(*Value);
        if (Current.Kind != TokenKind::Comma)
          break;
        advance();
      }
      Result.Instructions.push_back(std::move(Inst));
      return llvm::Error::success();
    }

    return error("unknown directive '." + Name + "'");
  }

  /// profile := ('ps'|'vs'|'gs'|'hs'|'ds'|'cs') '_' <major> '_' <minor>
  ///
  /// `fxc` disassembly opens with a bare profile name rather than the
  /// `.shader_model` directive `dxbc-as` defines, so accept both spellings
  /// of the program header. Returns false (consuming nothing) if the token
  /// at \c Current is not a profile name, in which case it is a mnemonic.
  bool parseShaderProfile(Program &Result) {
    if (Current.Kind != TokenKind::Identifier)
      return false;
    llvm::StringRef Text = Current.Spelling;
    auto [Stage, Version] = Text.split('_');
    auto [Major, Minor] = Version.split('_');
    const KeywordValue *Type = findKeyword(ProfilePrefixes, Stage);
    unsigned MajorValue, MinorValue;
    if (!Type || Major.empty() || Minor.empty() ||
        Major.getAsInteger(10, MajorValue) || Minor.getAsInteger(10, MinorValue))
      return false;

    Result.HasHeader = true;
    Result.ProgramType = static_cast<uint16_t>(Type->Value);
    Result.MajorVersion = static_cast<uint8_t>(MajorValue);
    Result.MinorVersion = static_cast<uint8_t>(MinorValue);
    advance();
    return true;
  }

  //===--------------------------------------------------------------------===//
  // Instructions
  //===--------------------------------------------------------------------===//

  /// Resolves an `fxc`-spelled mnemonic whose suffixes stand in for control
  /// bits or extended opcode tokens that dxbc-as spells separately:
  ///
  ///   - `sync_uglobal_g_t`      -> `sync` with the flags OR-ed into
  ///                                \p SuffixControls,
  ///   - `dcl_uav_structured_opc`/`dcl_interface_dynamicindexed` -> a
  ///                                declaration with one control bit set,
  ///   - `<op>[_aoffimmi][_indexable]` -> \p HasAoffimmi / \p HasIndexable,
  ///                                telling the caller that the extended
  ///                                opcode tokens' parenthesized arguments
  ///                                follow the mnemonic.
  ///
  /// Returns nullptr if \p Mnemonic is not such a spelling.
  static const Opcode *lookupFxcMnemonic(llvm::StringRef Mnemonic,
                                         uint32_t &SuffixControls,
                                         bool &HasAoffimmi,
                                         bool &HasIndexable) {
    if (Mnemonic.consume_front("sync")) {
      for (const KeywordValue &Suffix : SyncSuffixes)
        if (Mnemonic.consume_front(Suffix.Name))
          SuffixControls |= Suffix.Value;
      if (!Mnemonic.empty() || SuffixControls == 0)
        return nullptr;
      return lookupOpcode("sync");
    }
    if (Mnemonic == "dcl_uav_structured_opc") {
      // D3D11_SB_UAV_HAS_ORDER_PRESERVING_COUNTER.
      SuffixControls |= 0x00800000;
      return lookupOpcode("dcl_uav_structured");
    }
    if (Mnemonic == "dcl_interface_dynamicindexed") {
      // D3D11_SB_INTERFACE_INDEXED_BIT.
      SuffixControls |= 1u << 11;
      return lookupOpcode("dcl_interface");
    }

    HasIndexable = Mnemonic.consume_back("_indexable");
    HasAoffimmi = Mnemonic.consume_back("_aoffimmi");
    // `fxc` spells D3D10_SB_OPCODE_LD_MS as `ldms`, not `ld2dms`.
    if (Mnemonic == "ldms")
      return lookupOpcode("ld2dms");
    if (Mnemonic == "ldms_s")
      return lookupOpcode("ld2dms_s");
    if (!HasIndexable && !HasAoffimmi)
      return nullptr;
    return lookupOpcode(Mnemonic);
  }

  /// Parses one full statement: a mnemonic (with optional `_sat` suffix)
  /// followed by whatever modifiers and operands its InstructionKind
  /// grammar allows.
  llvm::Expected<Instruction> parseInstruction() {
    if (Current.Kind != TokenKind::Identifier)
      return error("expected instruction mnemonic");

    llvm::StringRef Mnemonic = Current.Spelling;
    bool Saturate = false;
    uint32_t SuffixControls = 0;
    bool HasAoffimmi = false;
    bool HasIndexable = false;
    const Opcode *Op = lookupOpcode(Mnemonic);
    if (!Op)
      Op = lookupFxcMnemonic(Mnemonic, SuffixControls, HasAoffimmi,
                             HasIndexable);
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
    Inst.Controls = Info.Controls | SuffixControls;

    if (Saturate && !(Info.Flags & OF_Saturable))
      return error("'_sat' is not valid on '" + Info.Mnemonic + "'");

    // `fxc` writes the precise-component mask as a bracketed group right
    // after the mnemonic; dxbc-as spells it `precise(<components>)`.
    if (Current.Kind == TokenKind::LBracket) {
      advance();
      if (!consumeIdentifier("precise"))
        return error("expected 'precise'");
      if (Current.Kind == TokenKind::LParen) {
        if (llvm::Error E = parsePreciseMask(Inst))
          return std::move(E);
      } else {
        Inst.PreciseMask = 0xF;
      }
      if (llvm::Error E = expectToken(TokenKind::RBracket, "']'"))
        return std::move(E);
    }

    if (HasAoffimmi) {
      if (llvm::Error E = parseSampleOffsets(Inst))
        return std::move(E);
    }
    if (HasIndexable) {
      if (llvm::Error E = parseResourceDim(Inst))
        return std::move(E);
      if (llvm::Error E = parseResourceReturnTypes(Inst))
        return std::move(E);
      Inst.HasResourceReturnType = true;
      // `resinfo_indexable(...)(...)_uint`: the return-format suffix trails
      // the extended opcode tokens' arguments rather than the base mnemonic.
      if (Current.Kind == TokenKind::Identifier &&
          Current.Spelling.starts_with("_")) {
        std::string Full = (Info.Mnemonic + Current.Spelling).str();
        if (const Opcode *Suffixed = lookupOpcode(Full)) {
          Inst.Op = *Suffixed;
          Inst.Controls = getOpcodeInfo(*Suffixed).Controls | SuffixControls;
          advance();
        }
      }
    }

    if (llvm::Error E = parseModifiers(Inst, Info))
      return std::move(E);

    switch (Info.Kind) {
    case InstructionKind::Generic:
      if (llvm::Error E = parseOperandList(Inst, Info.NumDst, Info.NumSrc))
        return std::move(E);
      // Some real shaders carry DWORDs past an instruction's operands that
      // the tokenized format does not describe; allowing them keeps such
      // bytecode expressible as assembly.
      if (llvm::Error E = parseTrailingCounts(Inst, /*AtLeastOne=*/false))
        return std::move(E);
      break;
    case InstructionKind::FlagList:
      // `sync_uglobal_g_t` already carries its flags in the mnemonic.
      if (SuffixControls == 0)
        if (llvm::Error E = parseFlagList(Inst))
          return std::move(E);
      break;
    case InstructionKind::ControlEnum:
      if (llvm::Error E = parseControlEnum(Inst))
        return std::move(E);
      break;
    case InstructionKind::ControlCount: {
      llvm::Expected<uint64_t> Count = parseInteger("a count");
      if (!Count)
        return Count.takeError();
      // The opcode-specific control range is [23:11]; a wider value would
      // silently corrupt the instruction length field above it.
      if (*Count > MaxControlValue)
        return error("count must fit in the 13-bit control field");
      Inst.Controls |= static_cast<uint32_t>(*Count) << ControlShift;
      break;
    }
    case InstructionKind::Counts:
      // The interface declarations name their operands symbolically in
      // `fxc` output (`dcl_function_table ft0 = {fb0}`) where dxbc-as
      // spells the same tokens as bare DWORDs.
      if (Current.Kind == TokenKind::Identifier) {
        if (llvm::Error E = parseInterfaceDeclaration(Inst))
          return std::move(E);
        break;
      }
      if (llvm::Error E = parseTrailingCounts(Inst, /*AtLeastOne=*/true))
        return std::move(E);
      break;
    case InstructionKind::Float: {
      // `fxc` wraps the value in the immediate-operand syntax even though
      // the tokenized format stores a bare DWORD.
      bool Wrapped = isIdentifier("l");
      if (Wrapped) {
        advance();
        if (llvm::Error E = expectToken(TokenKind::LParen, "'('"))
          return std::move(E);
      }
      llvm::Expected<uint32_t> Value = parseValue32();
      if (!Value)
        return Value.takeError();
      Inst.ExtraDWords.push_back(*Value);
      if (Wrapped)
        if (llvm::Error E = expectToken(TokenKind::RParen, "')'"))
          return std::move(E);
      break;
    }
    case InstructionKind::DclIndexableTemp:
      if (llvm::Error E = parseDclIndexableTemp(Inst))
        return std::move(E);
      break;
    case InstructionKind::Operand:
      if (llvm::Error E = parseOperandList(Inst, Info.NumDst, Info.NumSrc))
        return std::move(E);
      if (llvm::Error E = parseTrailingCounts(Inst, /*AtLeastOne=*/false))
        return std::move(E);
      break;
    case InstructionKind::OperandSystemValue:
      if (llvm::Error E = parseOperandList(Inst, Info.NumDst, Info.NumSrc))
        return std::move(E);
      if (llvm::Error E = expectToken(TokenKind::Comma, "','"))
        return std::move(E);
      if (llvm::Error E = parseSystemValueName(Inst))
        return std::move(E);
      break;
    case InstructionKind::DclInputPS:
    case InstructionKind::DclInputPSSystemValue: {
      if (llvm::Error E = parseInterpolationMode(Inst))
        return std::move(E);
      if (llvm::Error E = parseOperandList(Inst, Info.NumDst, Info.NumSrc))
        return std::move(E);
      if (Info.Kind == InstructionKind::DclInputPSSystemValue) {
        if (llvm::Error E = expectToken(TokenKind::Comma, "','"))
          return std::move(E);
        if (llvm::Error E = parseSystemValueName(Inst))
          return std::move(E);
      }
      break;
    }
    case InstructionKind::DclTypedResource: {
      if (llvm::Error E = parseResourceReturnTypes(Inst))
        return std::move(E);
      if (llvm::Error E = parseOperandList(Inst, Info.NumDst, Info.NumSrc))
        return std::move(E);
      // The four return types share one trailing DWORD, four bits each.
      uint32_t Packed = 0;
      for (unsigned I = 0; I < 4; ++I)
        Packed |= (Inst.ResourceReturnTypes[I] & 0xF) << (4 * I);
      Inst.ExtraDWords.push_back(Packed);
      if (llvm::Error E = parseTrailingCounts(Inst, /*AtLeastOne=*/false))
        return std::move(E);
      break;
    }
    case InstructionKind::DclImmediateConstantBuffer:
      if (llvm::Error E = parseImmediateConstantBuffer(Inst))
        return std::move(E);
      break;
    case InstructionKind::RawTokens:
      return error("'.dword' is a directive, not a mnemonic");
    }
    return Inst;
  }

  /// Parses the keyword and parenthesized modifiers that may follow a
  /// mnemonic, in any order, before its operands.
  llvm::Error parseModifiers(Instruction &Inst, const OpcodeInfo &Info) {
    // `dcl_resource_texture2dms(4)`: the multisample count is spelled as a
    // suffix on the mnemonic rather than as a separate token. A '(' also
    // opens the return-type quadruple, so only claim it when a number
    // follows.
    if ((Info.Flags & OF_SampleCount) && Current.Kind == TokenKind::LParen &&
        startsSampleCount()) {
      advance();
      llvm::Expected<uint64_t> Count = parseInteger("a sample count");
      if (!Count)
        return Count.takeError();
      // [22:16] D3D10_SB_RESOURCE_SAMPLE_COUNT.
      if (*Count > 0x7F)
        return error("sample count must fit in 7 bits");
      Inst.Controls |= static_cast<uint32_t>(*Count) << 16;
      Inst.Keywords.push_back(llvm::utostr(*Count));
      if (llvm::Error E = expectToken(TokenKind::RParen, "')'"))
        return E;
    }

    while (Current.Kind == TokenKind::Identifier) {
      llvm::StringRef Name = Current.Spelling;
      if ((Info.Flags & OF_UAVFlags) != 0) {
        if (const KeywordValue *Flag = findKeyword(UAVFlags, Name)) {
          Inst.Controls |= Flag->Value;
          Inst.Keywords.push_back(Name.str());
          advance();
          continue;
        }
      }
      if (Name == "precise") {
        advance();
        if (llvm::Error E = parsePreciseMask(Inst))
          return E;
        continue;
      }
      if (Name == "aoffimmi") {
        advance();
        if (llvm::Error E = parseSampleOffsets(Inst))
          return E;
        continue;
      }
      if (Name == "resource_dim") {
        advance();
        if (llvm::Error E = parseResourceDim(Inst))
          return E;
        continue;
      }
      if (Name == "resource_return_type") {
        advance();
        if (llvm::Error E = parseResourceReturnTypes(Inst))
          return E;
        Inst.HasResourceReturnType = true;
        continue;
      }
      break;
    }
    return llvm::Error::success();
  }

  /// precise := 'precise' '(' [xyzw]{1,4} ')'
  llvm::Error parsePreciseMask(Instruction &Inst) {
    if (getOpcodeInfo(Inst.Op).Kind != InstructionKind::Generic)
      return error("'precise' is not valid on '" +
                   getOpcodeInfo(Inst.Op).Mnemonic + "'");
    if (llvm::Error E = expectToken(TokenKind::LParen, "'('"))
      return E;
    llvm::Expected<Token> Comps =
        expect(TokenKind::Identifier, "a component list (e.g. 'xy')");
    if (!Comps)
      return Comps.takeError();
    uint8_t Mask = 0;
    for (char C : Comps->Spelling) {
      const char *Where = strchr("xyzw", C);
      if (!Where || C == '\0')
        return error("invalid component '" + llvm::Twine(C) + "'");
      Mask |= 1 << (Where - "xyzw");
    }
    Inst.PreciseMask = Mask;
    return expectToken(TokenKind::RParen, "')'");
  }

  /// aoffimmi := 'aoffimmi' '(' <int> ',' <int> ',' <int> ')'
  llvm::Error parseSampleOffsets(Instruction &Inst) {
    if (llvm::Error E = expectToken(TokenKind::LParen, "'('"))
      return E;
    for (unsigned I = 0; I < 3; ++I) {
      if (I != 0) {
        if (llvm::Error E = expectToken(TokenKind::Comma, "','"))
          return E;
      }
      llvm::Expected<int64_t> Value = parseSignedInteger("an address offset");
      if (!Value)
        return Value.takeError();
      if (*Value < -8 || *Value > 7)
        return error("address offset must be in [-8, 7]");
      Inst.SampleOffsets[I] = static_cast<int8_t>(*Value);
    }
    Inst.HasSampleOffsets = true;
    return expectToken(TokenKind::RParen, "')'");
  }

  /// resource_dim := 'resource_dim' '(' <dim> [',' <stride>] ')'
  llvm::Error parseResourceDim(Instruction &Inst) {
    if (llvm::Error E = expectToken(TokenKind::LParen, "'('"))
      return E;
    if (Current.Kind != TokenKind::Identifier)
      return error("expected a resource dimension");
    const KeywordValue *Dim = findKeyword(ResourceDimensions, Current.Spelling);
    if (!Dim)
      return error("unknown resource dimension '" + Current.Spelling + "'");
    Inst.Keywords.push_back(Current.Spelling.str());
    advance();
    Inst.ResourceDim = static_cast<uint8_t>(Dim->Value);
    Inst.ResourceStride = 0;
    if (Current.Kind == TokenKind::Comma) {
      advance();
      // `fxc` names the field (`stride=52`); dxbc-as spells it positionally.
      if (consumeIdentifier("stride")) {
        if (llvm::Error E = expectToken(TokenKind::Equals, "'='"))
          return E;
      }
      llvm::Expected<uint64_t> Stride = parseInteger("a structure stride");
      if (!Stride)
        return Stride.takeError();
      // [21:11] D3D11_SB_EXTENDED_RESOURCE_DIMENSION_STRUCTURE_STRIDE.
      if (*Stride > 0x7FF)
        return error("structure stride must fit in 11 bits");
      Inst.ResourceStride = static_cast<uint16_t>(*Stride);
    }
    Inst.HasResourceDim = true;
    return expectToken(TokenKind::RParen, "')'");
  }

  /// return-types := '(' <type> ',' <type> ',' <type> ',' <type> ')'
  llvm::Error parseResourceReturnTypes(Instruction &Inst) {
    if (llvm::Error E = expectToken(TokenKind::LParen, "'('"))
      return E;
    for (unsigned I = 0; I < 4; ++I) {
      if (I != 0) {
        if (llvm::Error E = expectToken(TokenKind::Comma, "','"))
          return E;
      }
      if (Current.Kind != TokenKind::Identifier)
        return error("expected a resource return type");
      const KeywordValue *Type =
          findKeyword(ResourceReturnTypes, Current.Spelling);
      if (!Type)
        return error("unknown resource return type '" + Current.Spelling + "'");
      Inst.ResourceReturnTypes[I] = static_cast<uint8_t>(Type->Value);
      Inst.Keywords.push_back(Current.Spelling.str());
      advance();
    }
    return expectToken(TokenKind::RParen, "')'");
  }

  /// flags := <flag> ('|' <flag>)*
  llvm::Error parseFlagList(Instruction &Inst) {
    llvm::ArrayRef<KeywordValue> Table =
        Inst.Op == Opcode::Sync ? llvm::ArrayRef<KeywordValue>(SyncFlags)
                                : llvm::ArrayRef<KeywordValue>(GlobalFlags);
    while (true) {
      if (Current.Kind != TokenKind::Identifier)
        return error("expected a flag name");
      const KeywordValue *Flag = findKeyword(Table, Current.Spelling);
      if (!Flag)
        return error("unknown flag '" + Current.Spelling + "'");
      Inst.Controls |= Flag->Value;
      Inst.Keywords.push_back(Current.Spelling.str());
      advance();
      if (Current.Kind != TokenKind::Pipe)
        return llvm::Error::success();
      advance();
    }
  }

  /// Parses the single keyword naming an enumerated control field.
  llvm::Error parseControlEnum(Instruction &Inst) {
    llvm::ArrayRef<KeywordValue> Table;
    switch (Inst.Op) {
    case Opcode::DclInputprimitive:
      Table = InputPrimitives;
      break;
    case Opcode::DclOutputtopology:
      Table = OutputTopologies;
      break;
    case Opcode::DclTessellatorDomain:
      Table = TessellatorDomains;
      break;
    case Opcode::DclTessellatorPartitioning:
      Table = TessellatorPartitionings;
      break;
    case Opcode::DclTessellatorOutputPrimitive:
      Table = TessellatorOutputPrimitives;
      break;
    default:
      llvm_unreachable("opcode has no enumerated control field");
    }

    if (Current.Kind != TokenKind::Identifier)
      return error("expected a keyword");
    llvm::StringRef Name = Current.Spelling;
    uint32_t Value;
    if (const KeywordValue *Entry = findKeyword(Table, Name)) {
      Value = Entry->Value;
    } else if (Inst.Op == Opcode::DclInputprimitive &&
               Name.starts_with("patch")) {
      // D3D11_SB_PRIMITIVE_<N>_CONTROL_POINT_PATCH values are contiguous
      // from N == 1, so spell them as `patch<N>` rather than as 32 rows.
      unsigned Points;
      if (Name.drop_front(5).getAsInteger(10, Points) || Points < 1 ||
          Points > 32)
        return error("expected 'patch<N>' with N in [1, 32]");
      Value = 7 + Points;
    } else {
      return error("unknown keyword '" + Name + "'");
    }
    Inst.Controls |= Value << ControlShift;
    Inst.Keywords.push_back(Name.str());
    advance();
    return llvm::Error::success();
  }

  /// interpolation-mode := <mode>
  ///                      | 'linear' ['noperspective'] ['centroid'|'sample']
  ///
  /// dxbc-as spells D3D10_SB_INTERPOLATION_MODE as a single camel-cased
  /// keyword; `fxc` spells the same values as up to three words.
  llvm::Error parseInterpolationMode(Instruction &Inst) {
    if (Current.Kind != TokenKind::Identifier)
      return error("expected an interpolation mode");
    // `linear` alone is a valid mode but is also the prefix of `fxc`'s
    // multi-word spellings, so it has to fall through to those.
    if (const KeywordValue *Mode =
            Current.Spelling == "linear"
                ? nullptr
                : findKeyword(InterpolationModes, Current.Spelling)) {
      Inst.Controls |= Mode->Value << ControlShift;
      Inst.Keywords.push_back(Current.Spelling.str());
      advance();
      return llvm::Error::success();
    }
    if (!consumeIdentifier("linear"))
      return error("unknown interpolation mode '" + Current.Spelling + "'");

    std::string Spelling = "linear";
    bool NoPerspective = consumeIdentifier("noperspective");
    if (NoPerspective)
      Spelling += " noperspective";
    bool Centroid = consumeIdentifier("centroid");
    bool Sample = !Centroid && consumeIdentifier("sample");
    if (Centroid)
      Spelling += " centroid";
    else if (Sample)
      Spelling += " sample";

    // The `linear` half of D3D10_SB_INTERPOLATION_MODE is not laid out as
    // independent bits, so map the qualifier combination directly.
    uint32_t Mode;
    if (Sample)
      Mode = NoPerspective ? 7 : 6;
    else
      Mode = (NoPerspective ? 4 : 2) + (Centroid ? 1 : 0);
    Inst.Controls |= Mode << ControlShift;
    Inst.Keywords.push_back(std::move(Spelling));
    return llvm::Error::success();
  }

  llvm::Error parseSystemValueName(Instruction &Inst) {
    if (Current.Kind != TokenKind::Identifier)
      return error("expected a system-value name");
    const KeywordValue *Name = findKeyword(SystemValueNames, Current.Spelling);
    if (!Name)
      return error("unknown system-value name '" + Current.Spelling + "'");
    Inst.ExtraDWords.push_back(Name->Value);
    Inst.Keywords.push_back(Current.Spelling.str());
    advance();
    return llvm::Error::success();
  }

  /// The opcode-specific control keywords `fxc` spells after a
  /// declaration's operand, where dxbc-as's own grammar folds them into the
  /// mnemonic. Empty for mnemonics that have no such keyword.
  static llvm::ArrayRef<KeywordMnemonic> getTrailerKeywords(Opcode Op) {
    switch (Op) {
    case Opcode::DclSampler:
      return SamplerModes;
    case Opcode::DclConstantbuffer:
      return ConstantBufferAccessPatterns;
    default:
      return {};
    }
  }

  /// counts := <item> (',' <item>)*
  /// item   := <int> | <control-keyword> | ('space' | 'stride') '=' <int>
  ///
  /// The plain-integer form is dxbc-as's own spelling of a declaration's
  /// trailing DWORDs; the other two are `fxc`'s, which names the register
  /// space it emits and spells enumerated control fields as keywords.
  llvm::Error parseTrailingCounts(Instruction &Inst, bool AtLeastOne) {
    if (!AtLeastOne) {
      // `fxc` writes a constant buffer's size as a bracketed group directly
      // after the operand (`CB0[5:5][1]`) and `dcl_indexrange`'s count with
      // no separator at all; dxbc-as's own spelling is comma-separated.
      while (Current.Kind == TokenKind::LBracket) {
        advance();
        llvm::Expected<uint64_t> Value = parseInteger("an integer");
        if (!Value)
          return Value.takeError();
        Inst.ExtraDWords.push_back(static_cast<uint32_t>(*Value));
        if (llvm::Error E = expectToken(TokenKind::RBracket, "']'"))
          return E;
      }
      if (Current.Kind == TokenKind::Comma)
        advance();
      else if (Current.Kind != TokenKind::Integer)
        return llvm::Error::success();
    }
    llvm::ArrayRef<KeywordMnemonic> Keywords = getTrailerKeywords(Inst.Op);
    while (true) {
      if (Current.Kind == TokenKind::Identifier) {
        if (llvm::Error E = parseTrailingKeyword(Inst, Keywords))
          return E;
      } else {
        llvm::Expected<uint64_t> Value = parseInteger("an integer");
        if (!Value)
          return Value.takeError();
        Inst.ExtraDWords.push_back(static_cast<uint32_t>(*Value));
      }
      if (Current.Kind != TokenKind::Comma)
        return llvm::Error::success();
      advance();
    }
  }

  llvm::Error parseTrailingKeyword(Instruction &Inst,
                                   llvm::ArrayRef<KeywordMnemonic> Keywords) {
    llvm::StringRef Name = Current.Spelling;
    if (Name == "space" || Name == "stride") {
      advance();
      if (llvm::Error E = expectToken(TokenKind::Equals, "'='"))
        return E;
      llvm::Expected<uint64_t> Value = parseInteger("an integer");
      if (!Value)
        return Value.takeError();
      Inst.ExtraDWords.push_back(static_cast<uint32_t>(*Value));
      return llvm::Error::success();
    }
    // Resolving the keyword to the equivalent mnemonic rather than OR-ing
    // its bits in keeps a single canonical spelling for each control value,
    // which is what AsmPrinter re-emits.
    for (const KeywordMnemonic &Entry : Keywords) {
      if (Entry.Name != Name)
        continue;
      const Opcode *Canonical = lookupOpcode(Entry.Mnemonic);
      assert(Canonical && "keyword names an unknown mnemonic");
      Inst.Op = *Canonical;
      Inst.Controls |= getOpcodeInfo(*Canonical).Controls;
      advance();
      return llvm::Error::success();
    }
    return error("unknown keyword '" + Name + "'");
  }

  /// Parses an identifier of the form `<prefix><id>` (e.g. `ft3`),
  /// returning the numeric id.
  llvm::Expected<uint32_t> parseSymbolicId(llvm::StringRef Prefix) {
    if (Current.Kind != TokenKind::Identifier ||
        !Current.Spelling.starts_with(Prefix))
      return error("expected a '" + Prefix + "<n>' identifier");
    uint32_t Id;
    if (Current.Spelling.drop_front(Prefix.size()).getAsInteger(10, Id))
      return error("expected a numeric id after '" + Prefix + "'");
    advance();
    return Id;
  }

  /// list := '=' '{' <prefix><id> (',' <prefix><id>)* '}'
  llvm::Error parseSymbolicIdList(llvm::StringRef Prefix,
                                  llvm::SmallVectorImpl<uint32_t> &Out) {
    if (llvm::Error E = expectToken(TokenKind::Equals, "'='"))
      return E;
    if (llvm::Error E = expectToken(TokenKind::LBrace, "'{'"))
      return E;
    while (true) {
      llvm::Expected<uint32_t> Id = parseSymbolicId(Prefix);
      if (!Id)
        return Id.takeError();
      Out.push_back(*Id);
      if (Current.Kind != TokenKind::Comma)
        break;
      advance();
    }
    return expectToken(TokenKind::RBrace, "'}'");
  }

  /// dcl_function_body  := 'dcl_function_body' 'fb'<id>
  /// dcl_function_table := 'dcl_function_table' 'ft'<id> '=' '{' 'fb'<id>,* '}'
  /// dcl_interface      := 'dcl_interface' 'fp'<id> '[' <len> ']'
  ///                       '[' <table-len> ']' '=' '{' 'ft'<id>,* '}'
  ///
  /// All three encode to the flat DWORD sequences dxbc-as's own grammar
  /// spells directly; the symbolic form is what `fxc` emits.
  llvm::Error parseInterfaceDeclaration(Instruction &Inst) {
    if (Inst.Op == Opcode::DclFunctionBody) {
      llvm::Expected<uint32_t> Id = parseSymbolicId("fb");
      if (!Id)
        return Id.takeError();
      Inst.ExtraDWords.push_back(*Id);
      return llvm::Error::success();
    }

    if (Inst.Op == Opcode::DclFunctionTable) {
      llvm::Expected<uint32_t> Id = parseSymbolicId("ft");
      if (!Id)
        return Id.takeError();
      llvm::SmallVector<uint32_t, 4> Bodies;
      if (llvm::Error E = parseSymbolicIdList("fb", Bodies))
        return E;
      Inst.ExtraDWords.push_back(*Id);
      Inst.ExtraDWords.push_back(Bodies.size());
      llvm::append_range(Inst.ExtraDWords, Bodies);
      return llvm::Error::success();
    }

    if (Inst.Op != Opcode::DclInterface)
      return error("unexpected identifier");

    llvm::Expected<uint32_t> Id = parseSymbolicId("fp");
    if (!Id)
      return Id.takeError();
    llvm::Expected<uint64_t> ArrayLength = parseBracketedInteger();
    if (!ArrayLength)
      return ArrayLength.takeError();
    llvm::Expected<uint64_t> TableLength = parseBracketedInteger();
    if (!TableLength)
      return TableLength.takeError();
    llvm::SmallVector<uint32_t, 4> Tables;
    if (llvm::Error E = parseSymbolicIdList("ft", Tables))
      return E;
    if (*ArrayLength > 0xFFFF || Tables.size() > 0xFFFF)
      return error("interface array/table count must fit in 16 bits");

    Inst.ExtraDWords.push_back(*Id);
    Inst.ExtraDWords.push_back(static_cast<uint32_t>(*TableLength));
    Inst.ExtraDWords.push_back(static_cast<uint32_t>(*ArrayLength) << 16 |
                               static_cast<uint32_t>(Tables.size()));
    llvm::append_range(Inst.ExtraDWords, Tables);
    return llvm::Error::success();
  }

  llvm::Expected<uint64_t> parseBracketedInteger() {
    if (llvm::Error E = expectToken(TokenKind::LBracket, "'['"))
      return std::move(E);
    llvm::Expected<uint64_t> Value = parseInteger("an integer");
    if (!Value)
      return Value;
    if (llvm::Error E = expectToken(TokenKind::RBracket, "']'"))
      return std::move(E);
    return *Value;
  }

  /// dcl_indexableTemp := 'x' <id> '[' <size> ']' ',' <components>
  llvm::Error parseDclIndexableTemp(Instruction &Inst) {
    if (Current.Kind != TokenKind::Identifier ||
        !Current.Spelling.starts_with("x"))
      return error("expected an indexable temp register (e.g. 'x0[4]')");
    unsigned Id;
    if (Current.Spelling.drop_front(1).getAsInteger(10, Id))
      return error("expected a numeric indexable temp id");
    advance();
    if (llvm::Error E = expectToken(TokenKind::LBracket, "'['"))
      return E;
    llvm::Expected<uint64_t> Size = parseInteger("an array size");
    if (!Size)
      return Size.takeError();
    if (llvm::Error E = expectToken(TokenKind::RBracket, "']'"))
      return E;
    if (llvm::Error E = expectToken(TokenKind::Comma, "','"))
      return E;
    llvm::Expected<uint64_t> Components = parseInteger("a component count");
    if (!Components)
      return Components.takeError();
    Inst.ExtraDWords.push_back(Id);
    Inst.ExtraDWords.push_back(static_cast<uint32_t>(*Size));
    Inst.ExtraDWords.push_back(static_cast<uint32_t>(*Components));
    return llvm::Error::success();
  }

  /// icb := '{' <element> (',' <element>)* '}'
  ///
  /// `fxc` groups the elements into one brace-delimited row per float4 and
  /// spreads them over several lines; the tokenized format stores one flat
  /// DWORD sequence, so both the row braces and the line breaks are just
  /// separators here.
  llvm::Error parseImmediateConstantBuffer(Instruction &Inst) {
    if (llvm::Error E = expectToken(TokenKind::LBrace, "'{'"))
      return E;
    unsigned Depth = 0;
    auto SkipSeparators = [&] {
      while (true) {
        if (Current.Kind == TokenKind::EndOfStatement ||
            Current.Kind == TokenKind::Comma) {
          advance();
        } else if (Current.Kind == TokenKind::LBrace) {
          ++Depth;
          advance();
        } else if (Current.Kind == TokenKind::RBrace && Depth != 0) {
          --Depth;
          advance();
        } else {
          return;
        }
      }
    };

    SkipSeparators();
    while (Current.Kind != TokenKind::RBrace) {
      llvm::Expected<uint32_t> Value = parseValue32();
      if (!Value)
        return Value.takeError();
      Inst.ExtraDWords.push_back(*Value);
      SkipSeparators();
    }
    return expectToken(TokenKind::RBrace, "'}'");
  }

  //===--------------------------------------------------------------------===//
  // Operands
  //===--------------------------------------------------------------------===//

  /// Parses \p NumDst destination operands followed by \p NumSrc source
  /// operands, comma-separated.
  llvm::Error parseOperandList(Instruction &Inst, unsigned NumDst,
                               unsigned NumSrc) {
    bool IsDeclaration = getOpcodeInfo(Inst.Op).Mnemonic.starts_with("dcl_");
    for (unsigned I = 0, E = NumDst + NumSrc; I != E; ++I) {
      if (I != 0) {
        if (llvm::Error Err = expectToken(TokenKind::Comma, "','"))
          return Err;
      }
      llvm::Expected<Operand> Op =
          parseOperand(/*IsDestination=*/I < NumDst, IsDeclaration);
      if (!Op)
        return Op.takeError();
      Inst.Operands.push_back(std::move(*Op));
    }
    return llvm::Error::success();
  }

  /// operand := ['-'] ('|' register '|' | register)
  llvm::Expected<Operand> parseOperand(bool IsDestination,
                                       bool IsDeclaration = false) {
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

    llvm::Expected<Operand> Op = parseRegister(IsDestination, IsDeclaration);
    if (!Op)
      return Op.takeError();
    Op->Negate = Negate;
    Op->Abs = Abs;

    if (Abs) {
      if (llvm::Error E = expectToken(TokenKind::Pipe, "closing '|'"))
        return std::move(E);
    }
    return Op;
  }

  /// register := 'l' '(' value (',' value){0,3} ')'
  ///           | 'd' '(' value (',' value)? ')'
  ///           | <kind> [<index>] ('[' index ']')* ['.' components]
  ///             ['{' modifiers '}']
  llvm::Expected<Operand> parseRegister(bool IsDestination,
                                        bool IsDeclaration) {
    if (Current.Kind != TokenKind::Identifier)
      return error("expected a register or immediate operand");

    if (Current.Spelling == "l" || Current.Spelling == "d")
      return parseImmediate();

    llvm::StringRef Name = Current.Spelling;
    Operand Op;
    const OperandKind *Kind = lookupOperandKindOrAlias(Name);
    unsigned FirstIndex = 0;
    bool HasFirstIndex = false;
    if (!Kind) {
      // `r0`, `cb2`, `label7`: a storage-class spelling immediately
      // followed by that operand's first (immediate) index.
      size_t DigitsAt = Name.find_first_of("0123456789");
      if (DigitsAt == llvm::StringRef::npos || DigitsAt == 0)
        return error("unknown operand storage class '" + Name + "'");
      Kind = lookupOperandKindOrAlias(Name.take_front(DigitsAt));
      if (!Kind || Name.drop_front(DigitsAt).getAsInteger(10, FirstIndex))
        return error("unknown operand storage class '" + Name + "'");
      HasFirstIndex = true;
    }
    advance();

    Op.Kind = *Kind;
    Op.Components = getDefaultComponentCount(*Kind);
    // The scalar system-generated registers name a whole object in a
    // declaration but carry one component when read as a value, and `fxc`
    // writes no component suffix in either position.
    if (!IsDeclaration && Op.Components == ComponentCount::Zero &&
        isScalarSystemValue(*Kind))
      Op.Components = ComponentCount::One;
    if (HasFirstIndex) {
      OperandIndex Index;
      Index.Value = FirstIndex;
      Op.Indices.push_back(std::move(Index));
    }

    bool SawRange = false;
    while (Current.Kind == TokenKind::LBracket) {
      advance();
      llvm::Expected<OperandIndex> Index = parseIndex();
      if (!Index)
        return Index.takeError();
      Op.Indices.push_back(std::move(*Index));
      // `fxc` writes an SM5.1 binding range as `[lower:upper]`, which the
      // tokenized format encodes as two consecutive immediate indices. It
      // is always the last bracketed group on the operand: anything after
      // it (a constant buffer's size) belongs to the declaration, not to
      // the operand.
      if (Current.Kind == TokenKind::Colon) {
        advance();
        llvm::Expected<OperandIndex> Upper = parseIndex();
        if (!Upper)
          return Upper.takeError();
        Op.Indices.push_back(std::move(*Upper));
        SawRange = true;
      }
      if (llvm::Error E = expectToken(TokenKind::RBracket, "']'"))
        return std::move(E);
      if (SawRange)
        break;
    }
    if (Op.Indices.size() > 3)
      return error("an operand can have at most three indices");

    // An SM5.1 binding-range declaration operand always describes all four
    // components, which `fxc` leaves implicit.
    if (SawRange) {
      Op.Components = ComponentCount::Four;
      Op.SelectMode = ComponentSelectMode::Swizzle;
    }

    bool HasComponents = false;
    if (Current.Kind == TokenKind::Dot) {
      advance();
      llvm::Expected<Token> Comps =
          expect(TokenKind::Identifier, "a swizzle/mask (e.g. 'xyzw')");
      if (!Comps)
        return Comps.takeError();
      if (llvm::Error E = applyComponents(Op, Comps->Spelling, IsDestination))
        return std::move(E);
      HasComponents = true;
    } else if (!SawRange && Op.Components == ComponentCount::Four) {
      // No suffix on a four-component operand means "all of it": a full
      // write mask for a destination, an identity swizzle for a source.
      Op.SelectMode = IsDestination ? ComponentSelectMode::Mask
                                    : ComponentSelectMode::Swizzle;
    }

    if (Current.Kind == TokenKind::LBrace) {
      advance();
      if (llvm::Error E = parseOperandModifiers(Op, HasComponents))
        return std::move(E);
    }
    return Op;
  }

  /// index := <int> | <operand> | <int> '+' <operand> | <operand> '+' <int>
  llvm::Expected<OperandIndex> parseIndex() {
    OperandIndex Index;
    if (Current.Kind == TokenKind::Integer) {
      llvm::Expected<uint64_t> Value = parseInteger("an index");
      if (!Value)
        return Value.takeError();
      Index.Value = *Value;
      if (Current.Kind != TokenKind::Plus) {
        Index.Rep = *Value > std::numeric_limits<uint32_t>::max()
                        ? OperandIndex::Representation::Immediate64
                        : OperandIndex::Representation::Immediate32;
        return Index;
      }
      advance();
      llvm::Expected<Operand> Rel = parseOperand(/*IsDestination=*/false);
      if (!Rel)
        return Rel.takeError();
      Index.Rep = OperandIndex::Representation::Immediate32PlusRelative;
      Index.Relative = std::make_shared<Operand>(std::move(*Rel));
      return Index;
    }

    llvm::Expected<Operand> Rel = parseOperand(/*IsDestination=*/false);
    if (!Rel)
      return Rel.takeError();
    Index.Relative = std::make_shared<Operand>(std::move(*Rel));
    if (Current.Kind != TokenKind::Plus) {
      Index.Rep = OperandIndex::Representation::Relative;
      return Index;
    }
    advance();
    llvm::Expected<uint64_t> Value = parseInteger("an index");
    if (!Value)
      return Value.takeError();
    // `fxc` always prints an immediate alongside a relative index, writing
    // `+ 0` for the purely relative representation.
    Index.Rep = *Value == 0 ? OperandIndex::Representation::Relative
                            : OperandIndex::Representation::Immediate32PlusRelative;
    Index.Value = *Value;
    return Index;
  }

  /// Interprets a component suffix. On a destination it is always a write
  /// mask (D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE); on a source a single
  /// letter selects one component (...SELECT_1_MODE) and four letters give
  /// a swizzle (...SWIZZLE_MODE), matching how `fxc` disassembly spells
  /// them. `{mask}`/`{swizzle}`/`{select1}` override this if a fixture
  /// needs a mode this rule would not pick.
  llvm::Error applyComponents(Operand &Op, llvm::StringRef Components,
                              bool IsDestination) {
    if (Components.empty() || Components.size() > 4)
      return error("swizzle/mask must have between 1 and 4 components");

    uint8_t Indices[4] = {0, 0, 0, 0};
    uint8_t Mask = 0;
    for (unsigned I = 0; I < Components.size(); ++I) {
      const char *Where = strchr("xyzw", Components[I]);
      if (!Where || Components[I] == '\0')
        return error("invalid swizzle/mask component '" +
                     llvm::Twine(Components[I]) + "' (expected x/y/z/w)");
      Indices[I] = static_cast<uint8_t>(Where - "xyzw");
      Mask |= 1 << Indices[I];
    }

    Op.Components = ComponentCount::Four;
    Op.WriteMask = Mask;
    Op.SelectedComponent = Indices[0];
    for (unsigned I = 0; I < 4; ++I)
      Op.Swizzle[I] =
          Indices[I < Components.size() ? I : Components.size() - 1];

    if (IsDestination) {
      Op.SelectMode = ComponentSelectMode::Mask;
      return llvm::Error::success();
    }
    if (Components.size() == 1) {
      Op.SelectMode = ComponentSelectMode::Select1;
      return llvm::Error::success();
    }
    if (Components.size() != 4)
      return error("a source swizzle must name exactly one or four "
                   "components");
    Op.SelectMode = ComponentSelectMode::Swizzle;
    return llvm::Error::success();
  }

  /// modifiers := <modifier> (',' <modifier>)* '}'
  llvm::Error parseOperandModifiers(Operand &Op, bool HasComponents) {
    while (true) {
      if (Current.Kind != TokenKind::Identifier)
        return error("expected an operand modifier");
      llvm::StringRef Name = Current.Spelling;
      if (Name == "comp0") {
        Op.Components = ComponentCount::Zero;
      } else if (Name == "comp1") {
        Op.Components = ComponentCount::One;
      } else if (Name == "comp4") {
        Op.Components = ComponentCount::Four;
        if (!HasComponents)
          Op.SelectMode = ComponentSelectMode::Swizzle;
      } else if (Name == "mask") {
        Op.SelectMode = ComponentSelectMode::Mask;
      } else if (Name == "swizzle") {
        Op.SelectMode = ComponentSelectMode::Swizzle;
      } else if (Name == "select1") {
        Op.SelectMode = ComponentSelectMode::Select1;
      } else if (Name == "nonuniform") {
        Op.NonUniform = true;
      } else if (Name == "def32") {
        Op.Precision = MinPrecision::Default;
      } else if (Name == "min16f") {
        Op.Precision = MinPrecision::Float16;
      } else if (Name == "min2_8f") {
        Op.Precision = MinPrecision::Float2_8;
      } else if (Name == "min16i") {
        Op.Precision = MinPrecision::SInt16;
      } else if (Name == "min16u") {
        Op.Precision = MinPrecision::UInt16;
      } else {
        return error("unknown operand modifier '" + Name + "'");
      }
      advance();
      // `fxc` spells a precision conversion `{<from> as <to>}`. The operand
      // token only records the precision the instruction reads/writes it
      // at, which is the first of the two.
      if (consumeIdentifier("as")) {
        if (Current.Kind != TokenKind::Identifier)
          return error("expected a minimum-precision name after 'as'");
        advance();
      }
      if (Current.Kind != TokenKind::Comma)
        break;
      advance();
    }
    return expectToken(TokenKind::RBrace, "'}'");
  }

  /// immediate := 'l' '(' value (',' value){0,3} ')'
  ///            | 'd' '(' value (',' value)? ')'
  llvm::Expected<Operand> parseImmediate() {
    bool Is64Bit = Current.Spelling == "d";
    advance();
    if (llvm::Error E = expectToken(TokenKind::LParen, "'('"))
      return std::move(E);

    Operand Op;
    Op.Kind = Is64Bit ? OperandKind::Immediate64 : OperandKind::Immediate32;
    unsigned Count = 0;
    while (true) {
      if (Is64Bit) {
        if (llvm::Error E = parseValue64(Op.ImmediateValues))
          return std::move(E);
      } else {
        llvm::Expected<uint32_t> Value = parseValue32();
        if (!Value)
          return Value.takeError();
        Op.ImmediateValues.push_back(*Value);
      }
      ++Count;
      if (Current.Kind != TokenKind::Comma)
        break;
      advance();
    }
    unsigned Max = Is64Bit ? 2u : 4u;
    if (Count != 1 && Count != Max)
      return error(llvm::Twine("an immediate must have exactly 1 or ") +
                   llvm::Twine(Max) + " components");
    Op.Components = Count == 1 ? ComponentCount::One : ComponentCount::Four;
    if (llvm::Error E = expectToken(TokenKind::RParen, "')'"))
      return std::move(E);
    // An immediate carries minimum-precision modifiers just like a
    // register does (`l(2.000000) {def32 as min16f}`).
    if (Current.Kind == TokenKind::LBrace) {
      advance();
      if (llvm::Error E =
              parseOperandModifiers(Op, /*HasComponents=*/true))
        return std::move(E);
    }
    return Op;
  }
};

} // namespace

llvm::Expected<Program> feme::dxbc::parseAssembly(llvm::StringRef Source) {
  ParserImpl Parser(Source);
  return Parser.parseProgram();
}
