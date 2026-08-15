//===- BinaryWriter.cpp - Serialize the dxsa dialect to DXBC binary ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements feme::dxsa::serialize, the `dxsa` dialect's DXBC *export* path
// (see feme/docs/Design.md's DXBC section and Roadmap.md's "BinaryWriter"
// item): the inverse of BinaryParser.cpp's `deserialize`.
//
// Rather than re-deriving the SM4/SM5 opcode table, this reuses
// `feme::dxbc::lookupOpcode`/`getOpcodeInfo` -- the same mnemonic-to-token
// table `dxbc-as`'s text assembler already builds from Opcodes.def -- so a
// `dxsa` op's mnemonic (its unqualified operation name, e.g. "add") maps
// straight onto the real D3D10/11 opcode token `feme::dxbc::encodeProgram`
// (also reused directly) needs. This only covers instructions built from
// DXSAOpBase.td's five generic shapes (`DXSA_NoOperandOp`/`UnaryOp`/
// `BinaryOp`/`TernaryOp`/`MultiplyAddOp`, plus `DXSA_MovConditionalOp`'s
// `movc`/`dmovc` family) -- i.e. the arithmetic/logic/comparison/conversion
// core of the ISA, which is what every opcode `getOpcodeInfo` reports as
// `InstructionKind::Generic` with a `NumDst`/`NumSrc` shape this file
// recognizes actually is. Control flow, declarations, and resource/texture
// ops (which carry their own custom MLIR attribute shapes) are not yet
// covered: converting one of those returns a diagnostic instead of
// mis-encoding it, matching Design.md's "continue extending opcode coverage
// incrementally" plan for this dialect (see Roadmap.md's own note on this
// narrowing).
//
//===----------------------------------------------------------------------===//

#include "feme/Target/DXSA/BinaryParser.h"

#include "feme/DXBC/Assembler/Encoder.h"
#include "feme/DXBC/Assembler/Instruction.h"
#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LogicalResult.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <optional>

using namespace mlir;
using namespace llvm;
using namespace feme;

namespace {

/// Which of DXSAOpBase.td's generic shapes (plus `DXSA_MovConditionalOp`)
/// this operation was built from, i.e. which fixed set of attribute names
/// its operands live under.
enum class Shape { NoOperand, Unary, Binary, Ternary, MultiplyAdd, MovCond };

/// The handful of `Generic`-kind, four-operand (dst plus three source)
/// mnemonics that are *not* `DXSA_TernaryOp` (`dst, src0, src1, src2`):
/// the multiply-add family (`dst, lhs, rhs, acc`) and the conditional-move
/// family (`dst, condition, src1, src2`). Every other four-operand mnemonic
/// -- `ibfe`/`ubfe`/`msad`/`lod` -- is a plain `DXSA_TernaryOp`.
StringRef stripSat(StringRef Mnemonic) {
  StringRef Base = Mnemonic;
  Base.consume_back("_sat");
  return Base;
}

std::optional<Shape> classifyShape(StringRef Mnemonic,
                                   const feme::dxbc::OpcodeInfo &Info) {
  if (Info.Kind != feme::dxbc::InstructionKind::Generic)
    return std::nullopt;

  static const llvm::StringSet<> MultiplyAddMnemonics = {"mad", "imad", "umad",
                                                    "dfma"};
  static const llvm::StringSet<> MovCondMnemonics = {"movc", "dmovc"};
  if (MultiplyAddMnemonics.contains(Mnemonic))
    return Shape::MultiplyAdd;
  if (MovCondMnemonics.contains(Mnemonic))
    return Shape::MovCond;

  if (Info.NumDst == 0 && Info.NumSrc == 0)
    return Shape::NoOperand;
  if (Info.NumDst == 1 && Info.NumSrc == 1)
    return Shape::Unary;
  if (Info.NumDst == 1 && Info.NumSrc == 2)
    return Shape::Binary;
  if (Info.NumDst == 1 && Info.NumSrc == 3)
    return Shape::Ternary;
  return std::nullopt;
}

feme::dxbc::OperandKind convertKind(dxsa::OperandType Type) {
  return static_cast<feme::dxbc::OperandKind>(static_cast<uint32_t>(Type));
}

std::optional<feme::dxbc::ComponentCount>
convertComponents(dxsa::OperandComponents Components) {
  switch (Components) {
  case dxsa::OperandComponents::none:
    return feme::dxbc::ComponentCount::Zero;
  case dxsa::OperandComponents::scalar:
    return feme::dxbc::ComponentCount::One;
  case dxsa::OperandComponents::vector:
    return feme::dxbc::ComponentCount::Four;
  case dxsa::OperandComponents::reserved:
    // Documented as unused (see DXSAOperand.td); nothing produces it, and
    // feme::dxbc::ComponentCount has no matching case to round-trip it through.
    return std::nullopt;
  }
  return std::nullopt;
}

feme::dxbc::MinPrecision
convertPrecision(dxsa::OperandMinPrecisionAttr Precision) {
  if (!Precision)
    return feme::dxbc::MinPrecision::Default;
  // The two enums share the same numeric encoding (see DXSAOperand.td's
  // `DXSA_OperandMinPrecision_*` case values vs. D3D11_SB_OPERAND_MIN_
  // PRECISION), so this is a direct cast, not a lookup table.
  return static_cast<feme::dxbc::MinPrecision>(
      static_cast<uint32_t>(Precision.getValue()));
}

FailureOr<feme::dxbc::Operand> convertSrcOperand(dxsa::SrcOperandAttr Attr);

/// Converts one `#dxsa.index` slot into the `feme::dxbc::OperandIndex` it
/// describes, recursing into `convertSrcOperand` for a relative index's
/// nested register operand.
FailureOr<feme::dxbc::OperandIndex> convertIndexEntry(dxsa::IndexAttr Entry) {
  IntegerAttr Imm = Entry.getImm();
  dxsa::SrcOperandAttr Relative = Entry.getRelative();
  if (!Imm && !Relative)
    return failure();

  feme::dxbc::OperandIndex Index;
  if (Relative) {
    FailureOr<feme::dxbc::Operand> RelativeOperand = convertSrcOperand(Relative);
    if (failed(RelativeOperand))
      return failure();
    Index.Relative =
        std::make_shared<feme::dxbc::Operand>(std::move(*RelativeOperand));
  }

  if (Imm && Relative) {
    // `feme::dxbc::OperandIndex` only has a combined 32-bit-immediate-plus-
    // relative representation (matching D3D10_SB_OPERAND_INDEX_
    // REPRESENTATION, which has no 64-bit counterpart), so a 64-bit
    // immediate paired with a relative operand cannot be represented.
    if (Imm.getValue().getBitWidth() != 32)
      return failure();
    Index.Rep = feme::dxbc::OperandIndex::Representation::Immediate32PlusRelative;
    Index.Value = static_cast<uint32_t>(Imm.getInt());
  } else if (Relative) {
    Index.Rep = feme::dxbc::OperandIndex::Representation::Relative;
  } else {
    bool Is64 = Imm.getValue().getBitWidth() == 64;
    Index.Rep = Is64 ? feme::dxbc::OperandIndex::Representation::Immediate64
                     : feme::dxbc::OperandIndex::Representation::Immediate32;
    Index.Value = Is64 ? static_cast<uint64_t>(Imm.getInt())
                       : static_cast<uint64_t>(
                             static_cast<uint32_t>(Imm.getInt()));
  }
  return Index;
}

/// Converts the shared fields of a destination/source operand attribute
/// (type, index, component count, min-precision) -- everything but the
/// mask/swizzle and modifier fields, which the two attribute kinds diverge
/// on.
template <typename OperandAttrT>
FailureOr<feme::dxbc::Operand> convertCommonOperandFields(OperandAttrT Attr) {
  feme::dxbc::Operand Op;
  Op.Kind = convertKind(Attr.getType());

  std::optional<feme::dxbc::ComponentCount> Components =
      convertComponents(Attr.getComponents().getValue());
  if (!Components)
    return failure();
  Op.Components = *Components;

  if (dxsa::OperandIndexAttr Indices = Attr.getIndex())
    for (dxsa::IndexAttr Entry : Indices) {
      FailureOr<feme::dxbc::OperandIndex> Converted = convertIndexEntry(Entry);
      if (failed(Converted))
        return failure();
      Op.Indices.push_back(*Converted);
    }

  Op.Precision = convertPrecision(Attr.getMinPrecision());
  return Op;
}

FailureOr<feme::dxbc::Operand> convertSrcOperand(dxsa::SrcOperandAttr Attr) {
  FailureOr<feme::dxbc::Operand> Converted = convertCommonOperandFields(Attr);
  if (failed(Converted))
    return failure();
  feme::dxbc::Operand Op = std::move(*Converted);

  Op.NonUniform = static_cast<bool>(Attr.getNonUniform());
  if (dxsa::OperandModifierAttr Modifier = Attr.getModifier()) {
    switch (Modifier.getValue()) {
    case dxsa::OperandModifier::neg:
      Op.Negate = true;
      break;
    case dxsa::OperandModifier::abs:
      Op.Abs = true;
      break;
    case dxsa::OperandModifier::abs_neg:
      Op.Negate = true;
      Op.Abs = true;
      break;
    }
  }

  if (dxsa::SwizzleAttr Swizzle = Attr.getSwizzle()) {
    ArrayRef<unsigned> Components = Swizzle.getComponents();
    if (Components.size() == 1) {
      Op.SelectMode = feme::dxbc::ComponentSelectMode::Select1;
      Op.SelectedComponent = static_cast<uint8_t>(Components[0]);
    } else if (Components.size() == 4) {
      Op.SelectMode = feme::dxbc::ComponentSelectMode::Swizzle;
      for (unsigned I = 0; I != 4; ++I)
        Op.Swizzle[I] = static_cast<uint8_t>(Components[I]);
    } else {
      return failure();
    }
  }

  if (DenseI32ArrayAttr Values32 = Attr.getValues32())
    llvm::append_range(Op.ImmediateValues, Values32.asArrayRef());
  if (DenseI64ArrayAttr Values64 = Attr.getValues64())
    // `Operand::ImmediateValues`' comment: two words per component, high
    // word first.
    for (int64_t Value : Values64.asArrayRef()) {
      Op.ImmediateValues.push_back(
          static_cast<uint32_t>(static_cast<uint64_t>(Value) >> 32));
      Op.ImmediateValues.push_back(static_cast<uint32_t>(Value));
    }

  return Op;
}

FailureOr<feme::dxbc::Operand> convertDstOperand(dxsa::DstOperandAttr Attr) {
  FailureOr<feme::dxbc::Operand> Converted = convertCommonOperandFields(Attr);
  if (failed(Converted))
    return failure();
  feme::dxbc::Operand Op = std::move(*Converted);

  Op.SelectMode = feme::dxbc::ComponentSelectMode::Mask;
  if (dxsa::ComponentMaskAttr Mask = Attr.getMask())
    Op.WriteMask = static_cast<uint8_t>(Mask.getValue());
  else
    Op.WriteMask = 0xF;
  return Op;
}

/// Looks up \p Name's `dxsa.dst_operand`/`dxsa.src_operand` attribute on
/// \p Op and converts it, or fails if the attribute is absent (the op does
/// not have the shape `classifyShape` said it did) or unconvertible.
FailureOr<feme::dxbc::Operand> getDst(Operation &Op, StringRef Name) {
  auto Attr = Op.getAttrOfType<dxsa::DstOperandAttr>(Name);
  if (!Attr)
    return failure();
  return convertDstOperand(Attr);
}
FailureOr<feme::dxbc::Operand> getSrc(Operation &Op, StringRef Name) {
  auto Attr = Op.getAttrOfType<dxsa::SrcOperandAttr>(Name);
  if (!Attr)
    return failure();
  return convertSrcOperand(Attr);
}

/// Appends the operands named by \p Names (in order) to \p Operands,
/// reading destination operands for \p NumDst of them and source operands
/// for the rest.
LogicalResult collectOperands(Operation &Op, ArrayRef<StringRef> Names,
                              unsigned NumDst,
                              SmallVectorImpl<feme::dxbc::Operand> &Operands) {
  for (auto [Index, Name] : llvm::enumerate(Names)) {
    FailureOr<feme::dxbc::Operand> Converted =
        Index < NumDst ? getDst(Op, Name) : getSrc(Op, Name);
    if (failed(Converted))
      return failure();
    Operands.push_back(*Converted);
  }
  return success();
}

/// Converts one `dxsa` operation into the `feme::dxbc::Instruction`
/// `encodeProgram` consumes, or fails if it is not one of the shapes this
/// file recognizes (see the file comment).
FailureOr<feme::dxbc::Instruction> convertInstruction(Operation &Op) {
  StringRef Mnemonic = Op.getName().stripDialect();
  bool Saturate = false;
  const feme::dxbc::Opcode *Opcode = feme::dxbc::lookupOpcode(Mnemonic);
  if (!Opcode) {
    // `_sat` is a separate dxsa op (e.g. `dxsa.add_sat`), not an attribute
    // on the base op, but dxbc-as's own grammar spells it as a suffix
    // keyword on the base mnemonic instead (see Opcodes.def's `OF_
    // Saturable` flag) -- strip it back off before the opcode lookup.
    StringRef Base = stripSat(Mnemonic);
    if (Base.size() != Mnemonic.size())
      Opcode = feme::dxbc::lookupOpcode(Base);
    if (!Opcode)
      return failure();
    Saturate = true;
  }

  const feme::dxbc::OpcodeInfo &Info = feme::dxbc::getOpcodeInfo(*Opcode);
  if (Saturate && !(Info.Flags & feme::dxbc::OF_Saturable))
    return failure();

  std::optional<Shape> ShapeOpt = classifyShape(stripSat(Mnemonic), Info);
  if (!ShapeOpt)
    return failure();

  feme::dxbc::Instruction Inst;
  Inst.Op = *Opcode;
  Inst.Saturate = Saturate;
  if (auto Precise = Op.getAttrOfType<dxsa::ComponentMaskAttr>("precise"))
    Inst.PreciseMask = static_cast<uint8_t>(Precise.getValue());

  LogicalResult Result = success();
  switch (*ShapeOpt) {
  case Shape::NoOperand:
    break;
  case Shape::Unary:
    Result = collectOperands(Op, {"dst", "src"}, 1, Inst.Operands);
    break;
  case Shape::Binary:
    Result = collectOperands(Op, {"dst", "lhs", "rhs"}, 1, Inst.Operands);
    break;
  case Shape::Ternary:
    Result = collectOperands(Op, {"dst", "src0", "src1", "src2"}, 1,
                             Inst.Operands);
    break;
  case Shape::MultiplyAdd:
    Result = collectOperands(Op, {"dst", "lhs", "rhs", "acc"}, 1,
                             Inst.Operands);
    break;
  case Shape::MovCond:
    Result = collectOperands(Op, {"dst", "condition", "src1", "src2"}, 1,
                             Inst.Operands);
    break;
  }
  if (failed(Result))
    return failure();
  return Inst;
}

} // namespace

LogicalResult feme::dxsa::serialize(mlir::ModuleOp Source,
                                    llvm::raw_ostream &Output) {
  // `Source` is the implicit top-level `builtin.module` the generic MLIR
  // parser wraps every input in (see `TranslateFromMLIRRegistration`'s own
  // callback signature in TranslateRegistration.cpp); the real shader is
  // the single `dxsa.module` operation nested inside it, mirroring
  // `deserialize`'s own top-level result.
  auto Modules = Source.getOps<feme::dxsa::ModuleOp>();
  if (!llvm::hasSingleElement(Modules))
    return Source.emitError(
        "expected exactly one 'dxsa.module' operation to serialize");
  feme::dxsa::ModuleOp Module = *Modules.begin();

  feme::dxbc::Program Program;
  if (std::optional<dxsa::ProgramType> ProgramType = Module.getProgramType()) {
    Program.HasHeader = true;
    Program.ProgramType = static_cast<uint16_t>(*ProgramType);
    Program.MajorVersion = static_cast<uint8_t>(*Module.getMajorVersion());
    Program.MinorVersion = static_cast<uint8_t>(*Module.getMinorVersion());
  }

  for (Operation &Op : Module.getBodyBlock()->getOperations()) {
    FailureOr<feme::dxbc::Instruction> Inst = convertInstruction(Op);
    if (failed(Inst))
      return Op.emitError(
          "cannot serialize this 'dxsa' operation to DXBC binary: only "
          "the generic arithmetic/logic/comparison/conversion opcode "
          "shapes are supported today");
    Program.Instructions.push_back(std::move(*Inst));
  }

  llvm::Expected<llvm::SmallVector<uint32_t, 64>> Encoded =
      feme::dxbc::encodeProgram(Program);
  if (!Encoded)
    return Module.emitError(llvm::toString(Encoded.takeError()));

  llvm::support::endian::Writer Writer(Output, llvm::endianness::little);
  for (uint32_t Word : *Encoded)
    Writer.write(Word);
  return success();
}
