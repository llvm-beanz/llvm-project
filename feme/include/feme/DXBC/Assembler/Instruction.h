//===- Instruction.h - DXBC assembler instruction model ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the in-memory representation `dxbc-as`'s Parser builds and its
// Encoder/AsmPrinter consume: an Opcode enum generated from Opcodes.def, and
// the Operand/Instruction structs making up the "instruction stack" (a flat
// list of parsed instructions) that sits between parsing and emission, per
// the traditional lex -> parse -> encode pipeline described in
// feme/docs/Design.md's "dxbc-as" section.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DXBC_ASSEMBLER_INSTRUCTION_H
#define FEME_DXBC_ASSEMBLER_INSTRUCTION_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <string>

namespace feme {
namespace dxbc {

/// Identifies a mnemonic `dxbc-as` understands. Generated from Opcodes.def
/// so the enum, mnemonic spelling, and real D3D10/11 opcode token value stay
/// in one place.
enum class Opcode {
#define DXBC_OPCODE(EnumName, Mnemonic, RealOpcodeValue, Kind) EnumName,
#include "feme/DXBC/Assembler/Opcodes.def"
};

/// Groups mnemonics by operand-encoding shape. Parser.cpp switches on this
/// to know what grammar to expect after a mnemonic, and Encoder.cpp switches
/// on it to know how to lay out operand tokens; see Opcodes.def for which
/// mnemonics fall in each group.
enum class InstructionKind {
  ALU1,           // dest, src0
  ALU2,           // dest, src0, src1
  ALU3,           // dest, src0, src1, src2
  NoOperand,      // (none)
  Discard,        // src0 (test boolean fixed by mnemonic)
  Sample,         // dest, address, resource, sampler
  Load,           // dest, address, resource
  DclGlobalFlags, // <flag>[ | <flag>]*
  DclTemps,       // <count>
  DclResource,    // (returnType,returnType,returnType,returnType) resource
  DclSampler,     // sampler [comparison]
  DclInput,       // input[.mask]
  DclInputPS,     // [interpolation] input[.mask]
  DclOutput,      // output[.mask]
};

/// Static, per-mnemonic metadata: the real opcode token value to encode and
/// which grammar/encoding shape it uses.
struct OpcodeInfo {
  llvm::StringRef Mnemonic;
  uint16_t RealOpcodeValue;
  InstructionKind Kind;
};

/// Returns the static metadata for \p Op.
const OpcodeInfo &getOpcodeInfo(Opcode Op);

/// Looks up an Opcode by its exact mnemonic spelling (e.g. "mov",
/// "dcl_resource_texture2d"), returning nullptr if \p Mnemonic is not a
/// recognized mnemonic.
const Opcode *lookupOpcode(llvm::StringRef Mnemonic);

/// The register file (and other operand storage classes) an Operand refers
/// to, matching a subset of D3D10_SB_OPERAND_TYPE (see
/// d3d11TokenizedProgramFormat.hpp) relevant to the mnemonics in
/// Opcodes.def.
enum class OperandKind {
  Temp,       // rN
  Input,      // vN
  Output,     // oN
  Resource,   // tN
  Sampler,    // sN
  Immediate32 // l(...) or l(x)
};

/// How an operand selects components out of a 4-component vector register,
/// matching D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE.
enum class ComponentSelectMode {
  None,   // operand carries no per-component data (e.g. a sampler operand)
  Mask,   // destination write mask, e.g. r0.xyz
  Swizzle // source component swizzle, e.g. v1.xyxx
};

/// A single operand of an Instruction: a register reference plus how it
/// selects/writes vector components, and (for source operands) the
/// modifiers ('-' negate, '| |' absolute value) `dxbc-as` supports.
struct Operand {
  OperandKind Kind = OperandKind::Temp;
  unsigned RegisterIndex = 0;

  ComponentSelectMode SelectMode = ComponentSelectMode::None;
  /// Mask mode: one bit per written component (bit0=x .. bit3=w).
  uint8_t WriteMask = 0xF;
  /// Swizzle mode: source component index (0=x..3=w) selected for each of
  /// the operand's x/y/z/w slots.
  uint8_t Swizzle[4] = {0, 1, 2, 3};

  bool Negate = false;
  bool Abs = false;

  /// Values for OperandKind::Immediate32: either a single value (scalar
  /// immediate, e.g. `l(1.0)`) or four (vector immediate, e.g.
  /// `l(1.0, 2.0, 3.0, 4.0)`), stored as the raw bit pattern of the parsed
  /// float (or integer, reinterpreted) literal.
  llvm::SmallVector<uint32_t, 4> ImmediateValues;
};

/// A single parsed DXBC assembly instruction: an opcode plus whatever
/// operands/attributes its InstructionKind grammar requires. Kept
/// deliberately generic (one struct for every InstructionKind) rather than a
/// tagged union per kind, since Encoder.cpp already must switch on Kind to
/// interpret these fields correctly, and a single shape keeps Parser.cpp's
/// per-kind parsing functions simple to add to.
struct Instruction {
  Opcode Op;

  /// True if the mnemonic had a `_sat` suffix (clamp result to [0,1]); only
  /// meaningful for floating-point ALU kinds, see Parser.cpp.
  bool Saturate = false;

  /// Register-style operands, in source order (destination(s) first).
  llvm::SmallVector<Operand, 4> Operands;

  /// Bare unsigned integer immediates appearing directly in the statement,
  /// not wrapped in an Operand (e.g. `dcl_temps 4`'s `4`).
  llvm::SmallVector<uint64_t, 1> Immediates;

  /// Bare identifier keywords appearing in the statement, in source order
  /// (e.g. dcl_globalFlags's flag names, dcl_input_ps's interpolation mode,
  /// dcl_resource's per-component return types).
  llvm::SmallVector<std::string, 4> Keywords;
};

} // namespace dxbc
} // namespace feme

#endif // FEME_DXBC_ASSEMBLER_INSTRUCTION_H
