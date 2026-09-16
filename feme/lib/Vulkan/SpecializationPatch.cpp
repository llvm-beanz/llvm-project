//===- SpecializationPatch.cpp - Raw SPIR-V spec-constant override patch ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SpecializationPatch.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

using namespace llvm;

namespace {

// The same opcode/enum numeric values GroupSize.cpp's own scanner uses
// (matching mlir/include/mlir/Dialect/SPIRV/IR/SPIRVBase.td), kept as plain
// constants here too rather than pulling in the `spirv` MLIR dialect (see
// SpecializationPatch.h's own file comment for why this operates on raw
// words at all).
enum : uint32_t {
  OpSpecConstantTrue = 48,
  OpSpecConstantFalse = 49,
  OpSpecConstant = 50,
  OpDecorate = 71,
};

enum : uint32_t {
  DecorationSpecId = 1,
};

/// One decoded instruction: its opcode, a *mutable* view of its operand
/// words (excluding the leading `(wordCount << 16) | opcode` word), and a
/// *mutable* reference to that leading header word itself. Unlike
/// GroupSize.cpp's own read-only `Instruction`, this scanner needs to write
/// back into an `OpSpecConstant`'s own literal-value operand -- or, for a
/// boolean spec constant, to rewrite the header word's own low 16 bits,
/// since `OpSpecConstantTrue`/`OpSpecConstantFalse` (SPIR-V's only
/// `OpTypeBool`-typed spec-constant encoding) carry no literal-value
/// operand at all, just a Result Type and a Result <id> -- the boolean
/// value is the opcode itself, so overriding one requires swapping True
/// for False (or vice versa) in place. `Operands`/`Header` are therefore
/// both `MutableArrayRef`/reference here. Duplicated rather than shared
/// with GroupSize.cpp's own decoder, mirroring that file's own established
/// "each raw-word scanner stays self-contained" convention (see
/// `resolveComputeDerivativeGroupMode`'s comment there).
struct Instruction {
  uint32_t Opcode;
  MutableArrayRef<uint32_t> Operands;
  uint32_t *Header;
};

/// Splits \p Words (the module body, after the 5-word header) into
/// individual instructions. Malformed length-prefixed streams (an
/// instruction claiming to extend past the end of \p Words) stop the scan
/// rather than reading out of bounds.
SmallVector<Instruction, 64>
decodeInstructions(MutableArrayRef<uint32_t> Words) {
  SmallVector<Instruction, 64> Result;
  size_t I = 0;
  while (I < Words.size()) {
    uint32_t Header = Words[I];
    uint32_t WordCount = Header >> 16;
    uint32_t Opcode = Header & 0xFFFFu;
    if (WordCount == 0 || I + WordCount > Words.size())
      break;
    Result.push_back(
        Instruction{Opcode, Words.slice(I + 1, WordCount - 1), &Words[I]});
    I += WordCount;
  }
  return Result;
}

} // namespace

void feme::vulkan::patchSpecializationConstants(
    MutableArrayRef<uint32_t> Words,
    ArrayRef<SpecializationOverride> Overrides) {
  if (Overrides.empty() || Words.size() < 5)
    return; // Nothing to apply, or too short to contain a header at all --
            // `feme::SPIRVImporter::import`'s own deserialization reports
            // that error later; this scanner only needs to avoid
            // reading/writing out of bounds itself.

  SmallVector<Instruction, 64> Instructions =
      decodeInstructions(Words.drop_front(5));

  // Pass 1: collect every `SpecId` decoration (target <id> -> SpecId),
  // exactly the same scan `feme::vulkan::resolveComputeGroupSize` performs
  // (duplicated rather than shared -- see this file's own header comment).
  DenseMap<uint32_t, uint32_t> SpecIds;
  for (const Instruction &Insn : Instructions) {
    if (Insn.Opcode != OpDecorate || Insn.Operands.size() < 3)
      continue;
    if (Insn.Operands[1] == DecorationSpecId)
      SpecIds[Insn.Operands[0]] = Insn.Operands[2];
  }

  // Pass 2: overwrite every `OpSpecConstant`'s own literal value word whose
  // `SpecId` decoration matches a real override. `OpSpecConstant`'s layout
  // is `ResultType, Result, Value...`.
  for (Instruction &Insn : Instructions) {
    if (Insn.Opcode != OpSpecConstant || Insn.Operands.size() < 3)
      continue;
    auto SpecIdIt = SpecIds.find(Insn.Operands[1]);
    if (SpecIdIt == SpecIds.end())
      continue;
    for (const SpecializationOverride &Override : Overrides)
      if (Override.ConstantID == SpecIdIt->second) {
        Insn.Operands[2] = Override.Value;
        break;
      }
  }

  // Pass 3: a boolean spec constant is encoded as `OpSpecConstantTrue`/
  // `OpSpecConstantFalse` (layout `ResultType, Result`, no literal-value
  // operand at all -- the boolean value *is* the opcode), so overriding one
  // means rewriting the header word's own opcode field in place rather than
  // an operand. The override's `Value` is treated as a `VkBool32` (matching
  // the offloader's own `VkSpecializationMapEntry::size` for `DataFormat::
  // Bool`, see `parseSpecializationConstant` in offload-test-suite):
  // nonzero selects `OpSpecConstantTrue`, zero selects `OpSpecConstantFalse`.
  for (Instruction &Insn : Instructions) {
    if ((Insn.Opcode != OpSpecConstantTrue &&
         Insn.Opcode != OpSpecConstantFalse) ||
        Insn.Operands.size() < 2)
      continue;
    auto SpecIdIt = SpecIds.find(Insn.Operands[1]);
    if (SpecIdIt == SpecIds.end())
      continue;
    for (const SpecializationOverride &Override : Overrides)
      if (Override.ConstantID == SpecIdIt->second) {
        uint32_t WordCount = *Insn.Header >> 16;
        uint32_t NewOpcode =
            Override.Value != 0 ? OpSpecConstantTrue : OpSpecConstantFalse;
        *Insn.Header = (WordCount << 16) | NewOpcode;
        break;
      }
  }
}
