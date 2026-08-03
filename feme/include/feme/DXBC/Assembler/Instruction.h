//===- Instruction.h - DXBC assembler instruction model ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the in-memory representation `dxbc-as`'s Parser builds and its
// Encoder/AsmPrinter consume: an Opcode enum generated from Opcodes.def, an
// OperandKind enum generated from OperandKinds.def, and the
// Operand/Instruction structs making up the "instruction stack" (a flat list
// of parsed instructions) that sits between parsing and emission, per the
// traditional lex -> parse -> encode pipeline described in
// feme/docs/Design.md's "dxbc-as" section.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DXBC_ASSEMBLER_INSTRUCTION_H
#define FEME_DXBC_ASSEMBLER_INSTRUCTION_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <memory>
#include <string>

namespace feme {
namespace dxbc {

/// Identifies a mnemonic `dxbc-as` understands. Generated from Opcodes.def
/// so the enum, mnemonic spelling, real D3D10/11 opcode token value, and
/// operand-count metadata stay in one place.
enum class Opcode : uint16_t {
#define DXBC_OPCODE(EnumName, Mnemonic, Value, NumDst, NumSrc, Controls, Kind, \
                    Flags)                                                     \
  EnumName,
#include "feme/DXBC/Assembler/Opcodes.def"
};

/// Groups mnemonics by the grammar that follows them. Parser.cpp switches on
/// this to know what to expect after a mnemonic, and Encoder.cpp switches on
/// it to know how to lay out the instruction's tokens.
///
/// Every kind encodes to the same overall shape -- an opcode token carrying
/// opcode-specific control bits, followed by operand tokens, followed by
/// trailing raw DWORDs -- so the kinds differ only in how the assembly text
/// spells those control bits and trailing DWORDs.
enum class InstructionKind {
  /// dst..., src...: counts come from OpcodeInfo's NumDst/NumSrc.
  Generic,
  /// A '|'-separated list of flag names OR-ed into the control bits.
  FlagList,
  /// A single keyword naming an enumerated value in the control bits.
  ControlEnum,
  /// A single unsigned integer stored directly in the control bits.
  ControlCount,
  /// A comma-separated list of unsigned integers emitted as trailing DWORDs.
  Counts,
  /// A single float emitted as a trailing DWORD (dcl_hs_max_tessfactor).
  Float,
  /// `x<id>[<size>], <components>`, emitted as three trailing DWORDs.
  DclIndexableTemp,
  /// One operand, then optional trailing unsigned integers.
  Operand,
  /// One operand, then a system-value name emitted as a trailing DWORD.
  OperandSystemValue,
  /// An interpolation-mode keyword, then one operand.
  DclInputPS,
  /// An interpolation-mode keyword, one operand, then a system-value name.
  DclInputPSSystemValue,
  /// A resource return-type quadruple, one operand, then optional trailing
  /// unsigned integers (dcl_resource_*, dcl_uav_typed_*).
  DclTypedResource,
  /// `{ <value>, ... }`: a CUSTOMDATA-encoded immediate constant buffer.
  DclImmediateConstantBuffer,
  /// `.dword <value>, ...`: raw tokens emitted verbatim, used to build
  /// deliberately malformed fixtures no other grammar can express.
  RawTokens,
};

/// Per-mnemonic flags that do not fit the other OpcodeInfo fields.
enum OpcodeFlags : uint8_t {
  OF_None = 0,
  /// The mnemonic accepts a `_sat` suffix (bit 13 of the opcode token).
  OF_Saturable = 1 << 0,
  /// The mnemonic accepts UAV access-flag keywords (`globallyCoherent`,
  /// `rasterizerOrdered`, `hasOrderPreservingCounter`).
  OF_UAVFlags = 1 << 1,
  /// The mnemonic accepts a parenthesized multisample count suffix, e.g.
  /// `dcl_resource_texture2dms(4)`.
  OF_SampleCount = 1 << 2,
};

/// Static, per-mnemonic metadata: the real opcode token value to encode,
/// how many destination/source operands its grammar expects, the fixed
/// opcode-specific control bits implied by the mnemonic spelling (e.g.
/// `callc_nz`'s test-boolean bit), and which grammar/encoding shape it uses.
struct OpcodeInfo {
  llvm::StringRef Mnemonic;
  uint16_t Value;
  uint8_t NumDst;
  uint8_t NumSrc;
  uint32_t Controls;
  InstructionKind Kind;
  uint8_t Flags;
};

/// Returns the static metadata for \p Op.
const OpcodeInfo &getOpcodeInfo(Opcode Op);

/// Looks up an Opcode by its exact mnemonic spelling (e.g. "mov",
/// "dcl_resource_texture2d"), returning nullptr if \p Mnemonic is not a
/// recognized mnemonic.
const Opcode *lookupOpcode(llvm::StringRef Mnemonic);

/// The register file (and other operand storage classes) an Operand refers
/// to, matching D3D10_SB_OPERAND_TYPE.
enum class OperandKind : uint8_t {
#define DXBC_OPERAND_KIND(EnumName, Spelling, Value) EnumName = Value,
#include "feme/DXBC/Assembler/OperandKinds.def"
};

/// Returns the assembly spelling of \p Kind (e.g. "r", "cb", "vThreadID").
llvm::StringRef getOperandKindSpelling(OperandKind Kind);

/// Looks up an OperandKind by its assembly spelling, returning nullptr if
/// \p Spelling names no known storage class.
const OperandKind *lookupOperandKind(llvm::StringRef Spelling);

/// How many of an operand's four components the token describes, matching
/// D3D10_SB_OPERAND_NUM_COMPONENTS. Operands that name a whole object
/// rather than a value (samplers, resources, labels) carry zero components.
enum class ComponentCount : uint8_t { Zero = 0, One = 1, Four = 4 };

/// The component count \p Kind uses when the assembly writes neither a
/// component suffix nor an explicit `{comp0}`/`{comp1}`/`{comp4}` override.
ComponentCount getDefaultComponentCount(OperandKind Kind);

/// How a four-component operand selects components, matching
/// D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE.
enum class ComponentSelectMode : uint8_t {
  Mask,    // destination write mask, e.g. r0.xz
  Swizzle, // source component swizzle, e.g. v1.xyxx
  Select1  // single source component, e.g. r1.x
};

/// The reduced-precision hint an operand may carry, matching
/// D3D11_SB_OPERAND_MIN_PRECISION.
enum class MinPrecision : uint8_t {
  Default = 0,
  Float16 = 1,
  Float2_8 = 2,
  SInt16 = 4,
  UInt16 = 5,
};

struct Operand;

/// One entry of an operand's index list, matching
/// D3D10_SB_OPERAND_INDEX_REPRESENTATION. `r0` has a single immediate
/// index; `cb0[3]` has two; `cb0[r1.x + 3]` has an immediate index and an
/// immediate-plus-relative index, whose relative part is itself an operand.
struct OperandIndex {
  enum class Representation : uint8_t {
    Immediate32 = 0,
    Immediate64 = 1,
    Relative = 2,
    Immediate32PlusRelative = 3,
  };

  Representation Rep = Representation::Immediate32;
  uint64_t Value = 0;
  /// The register the index is relative to; null unless \c Rep is
  /// Relative or Immediate32PlusRelative. Held by pointer because an
  /// Operand's indices may themselves contain Operands.
  std::shared_ptr<Operand> Relative;
};

/// A single operand of an Instruction: a storage class plus the indices
/// selecting a register within it, how it selects/writes vector components,
/// and the modifiers ('-' negate, '| |' absolute value, minimum precision,
/// non-uniform) it carries.
struct Operand {
  OperandKind Kind = OperandKind::Temp;
  ComponentCount Components = ComponentCount::Four;

  ComponentSelectMode SelectMode = ComponentSelectMode::Mask;
  /// Mask mode: one bit per written component (bit0=x .. bit3=w).
  uint8_t WriteMask = 0xF;
  /// Swizzle mode: source component index (0=x..3=w) per x/y/z/w slot.
  uint8_t Swizzle[4] = {0, 1, 2, 3};
  /// Select-1 mode: the single selected component index (0=x..3=w).
  uint8_t SelectedComponent = 0;

  llvm::SmallVector<OperandIndex, 2> Indices;

  bool Negate = false;
  bool Abs = false;
  bool NonUniform = false;
  MinPrecision Precision = MinPrecision::Default;

  /// Raw words for OperandKind::Immediate32 (one word per component) and
  /// OperandKind::Immediate64 (two words per component, high word first).
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

  /// True if the mnemonic had a `_sat` suffix (clamp result to [0,1]).
  bool Saturate = false;
  /// `precise(...)` component mask, in bits [3:0]; zero if absent.
  uint8_t PreciseMask = 0;

  /// `aoffimmi(u, v, w)` immediate texture-address offsets, if present.
  bool HasSampleOffsets = false;
  int8_t SampleOffsets[3] = {0, 0, 0};
  /// `resource_dim(<dim>[, <stride>])`, if present.
  bool HasResourceDim = false;
  uint8_t ResourceDim = 0;
  uint16_t ResourceStride = 0;
  /// `resource_return_type(x, y, z, w)`, if present.
  bool HasResourceReturnType = false;
  uint8_t ResourceReturnTypes[4] = {0, 0, 0, 0};

  /// Opcode-specific control bits ([23:11]) computed from the mnemonic and
  /// from any keyword modifiers the grammar allows.
  uint32_t Controls = 0;

  /// Register-style operands, in source order (destination(s) first).
  llvm::SmallVector<Operand, 4> Operands;

  /// Raw DWORDs emitted after the operand tokens (declaration payloads such
  /// as `dcl_temps`'s count, or a `.dword` directive's tokens).
  llvm::SmallVector<uint32_t, 4> ExtraDWords;

  /// Source spelling of the keyword modifiers folded into \c Controls, kept
  /// so AsmPrinter can re-emit the original text without having to invert
  /// each keyword table.
  llvm::SmallVector<std::string, 4> Keywords;
};

} // namespace dxbc
} // namespace feme

#endif // FEME_DXBC_ASSEMBLER_INSTRUCTION_H
