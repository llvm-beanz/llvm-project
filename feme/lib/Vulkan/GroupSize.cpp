//===- GroupSize.cpp - SPIR-V compute group-size resolution ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GroupSize.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <cstring>

using namespace llvm;

namespace {

// SPIR-V opcode/enum numeric values this scanner needs, matching
// mlir/include/mlir/Dialect/SPIRV/IR/SPIRVBase.td (kept as plain constants
// rather than depending on the `spirv` MLIR dialect at all, since this is a
// raw-word scan over the same bytes `feme::SPIRVImporter` reads -- see
// GroupSize.h's file comment for why this deliberately does not use MLIR's
// structured API).
enum : uint32_t {
  OpEntryPoint = 15,
  OpExecutionMode = 16,
  OpConstant = 43,
  OpConstantComposite = 44,
  OpSpecConstant = 50,
  OpSpecConstantComposite = 51,
  OpDecorate = 71,
  OpExecutionModeId = 331,
};

enum : uint32_t {
  ExecutionModelGLCompute = 5,
};

enum : uint32_t {
  ExecutionModeLocalSize = 17,
  ExecutionModeLocalSizeId = 38,
};

enum : uint32_t {
  DecorationSpecId = 1,
  DecorationBuiltIn = 11,
};

enum : uint32_t {
  BuiltInWorkgroupSize = 25,
};

/// One decoded instruction: its opcode and operand words (excluding the
/// leading `(wordCount << 16) | opcode` word).
struct Instruction {
  uint32_t Opcode;
  ArrayRef<uint32_t> Operands;
};

/// Splits \p Words (the module body, after the 5-word header) into
/// individual instructions. Malformed length-prefixed streams (a
/// instruction claiming to extend past the end of \p Words) stop the scan
/// rather than reading out of bounds.
SmallVector<Instruction, 64> decodeInstructions(ArrayRef<uint32_t> Words) {
  SmallVector<Instruction, 64> Result;
  size_t I = 0;
  while (I < Words.size()) {
    uint32_t Header = Words[I];
    uint32_t WordCount = Header >> 16;
    uint32_t Opcode = Header & 0xFFFFu;
    if (WordCount == 0 || I + WordCount > Words.size())
      break;
    Result.push_back(Instruction{Opcode, Words.slice(I + 1, WordCount - 1)});
    I += WordCount;
  }
  return Result;
}

/// Decodes a SPIR-V literal string starting at \p Operands[Index], and
/// advances \p Index past it (past the null terminator's own word, per the
/// SPIR-V specification's string encoding).
std::string decodeLiteralString(ArrayRef<uint32_t> Operands, size_t &Index) {
  std::string Result;
  for (; Index < Operands.size(); ++Index) {
    uint32_t Word = Operands[Index];
    char Bytes[4] = {static_cast<char>(Word & 0xFF),
                    static_cast<char>((Word >> 8) & 0xFF),
                    static_cast<char>((Word >> 16) & 0xFF),
                    static_cast<char>((Word >> 24) & 0xFF)};
    bool Terminated = false;
    for (char C : Bytes) {
      if (C == '\0') {
        Terminated = true;
        break;
      }
      Result.push_back(C);
    }
    if (Terminated) {
      ++Index;
      break;
    }
  }
  return Result;
}

/// A scalar constant's resolved value (`OpConstant`/`OpSpecConstant`), plus
/// the `SpecId` it was decorated with, if any (0 is itself a valid `SpecId`,
/// so this is tracked separately rather than folded into `Id`).
struct ScalarConstant {
  uint32_t DefaultValue = 0;
  bool HasSpecId = false;
  uint32_t SpecId = 0;
};

} // namespace

Expected<std::array<uint32_t, 3>> feme::vulkan::resolveComputeGroupSize(
    ArrayRef<uint32_t> Words, StringRef EntryPoint,
    ArrayRef<SpecializationOverride> Overrides) {
  // 5-word SPIR-V header: magic, version, generator, bound, schema (see the
  // SPIR-V specification's "Physical Layout of a SPIR-V Module and
  // Instruction"). `feme::SPIRVImporter::import` already validated the
  // module round-trips through `mlir::spirv::deserialize` before this
  // scanner ever runs, so only the minimum defensive checks needed to avoid
  // reading out of bounds are repeated here.
  if (Words.size() < 5)
    return createStringError(inconvertibleErrorCode(),
                             "SPIR-V module is too short to contain a header");

  SmallVector<Instruction, 64> Instructions =
      decodeInstructions(Words.drop_front(5));

  // Pass 1: find the target entry point's function id.
  uint32_t EntryFn = 0;
  bool FoundEntry = false;
  for (const Instruction &Insn : Instructions) {
    if (Insn.Opcode != OpEntryPoint || Insn.Operands.size() < 3)
      continue;
    if (Insn.Operands[0] != ExecutionModelGLCompute)
      continue;
    size_t NameIndex = 2;
    std::string Name = decodeLiteralString(Insn.Operands, NameIndex);
    if (Name != EntryPoint)
      continue;
    EntryFn = Insn.Operands[1];
    FoundEntry = true;
    break;
  }
  if (!FoundEntry)
    return createStringError(inconvertibleErrorCode(),
                             "no GLCompute OpEntryPoint named '%s'",
                             EntryPoint.str().c_str());

  // Pass 2: collect every constant's default value and, for
  // constant-composite ids, their constituent ids; collect SpecId and
  // BuiltIn decorations; collect this entry point's LocalSize/LocalSizeId
  // execution modes.
  DenseMap<uint32_t, ScalarConstant> Scalars;
  DenseMap<uint32_t, SmallVector<uint32_t, 3>> Composites;
  DenseMap<uint32_t, uint32_t> SpecIds;    // target id -> SpecId
  uint32_t WorkgroupSizeComposite = 0;
  bool HasWorkgroupSizeComposite = false;
  std::array<uint32_t, 3> LocalSize{1, 1, 1};
  bool HasLocalSize = false;
  SmallVector<uint32_t, 3> LocalSizeIdOperands;
  bool HasLocalSizeId = false;

  for (const Instruction &Insn : Instructions) {
    switch (Insn.Opcode) {
    case OpDecorate: {
      if (Insn.Operands.size() < 2)
        continue;
      uint32_t Target = Insn.Operands[0];
      uint32_t Decoration = Insn.Operands[1];
      if (Decoration == DecorationSpecId && Insn.Operands.size() >= 3)
        SpecIds[Target] = Insn.Operands[2];
      else if (Decoration == DecorationBuiltIn && Insn.Operands.size() >= 3 &&
              Insn.Operands[2] == BuiltInWorkgroupSize) {
        WorkgroupSizeComposite = Target;
        HasWorkgroupSizeComposite = true;
      }
      break;
    }
    case OpConstant:
    case OpSpecConstant: {
      // ResultType, Result, then >=1 literal value words; only the first
      // matters (group-size components are always plain 32-bit integers).
      if (Insn.Operands.size() < 3)
        continue;
      Scalars[Insn.Operands[1]].DefaultValue = Insn.Operands[2];
      break;
    }
    case OpConstantComposite:
    case OpSpecConstantComposite: {
      if (Insn.Operands.size() < 2)
        continue;
      SmallVector<uint32_t, 3> Constituents(Insn.Operands.drop_front(2));
      Composites[Insn.Operands[1]] = std::move(Constituents);
      break;
    }
    case OpExecutionMode: {
      if (Insn.Operands.size() < 2 || Insn.Operands[0] != EntryFn)
        continue;
      if (Insn.Operands[1] == ExecutionModeLocalSize &&
          Insn.Operands.size() >= 5) {
        LocalSize = {Insn.Operands[2], Insn.Operands[3], Insn.Operands[4]};
        HasLocalSize = true;
      }
      break;
    }
    case OpExecutionModeId: {
      if (Insn.Operands.size() < 2 || Insn.Operands[0] != EntryFn)
        continue;
      if (Insn.Operands[1] == ExecutionModeLocalSizeId &&
          Insn.Operands.size() >= 5) {
        LocalSizeIdOperands.assign(Insn.Operands.begin() + 2,
                                   Insn.Operands.begin() + 5);
        HasLocalSizeId = true;
      }
      break;
    }
    default:
      break;
    }
  }

  // Applies a SpecId's VkSpecializationInfo override, if any, else the
  // constant's own module-declared default.
  auto resolveScalar = [&](uint32_t Id) -> Expected<uint32_t> {
    auto It = Scalars.find(Id);
    if (It == Scalars.end())
      return createStringError(inconvertibleErrorCode(),
                               "group-size component <id> %u is not a "
                               "scalar constant",
                               Id);
    auto SpecIdIt = SpecIds.find(Id);
    if (SpecIdIt != SpecIds.end())
      for (const SpecializationOverride &Override : Overrides)
        if (Override.ConstantID == SpecIdIt->second)
          return Override.Value;
    return It->second.DefaultValue;
  };

  // Priority order per "Input and specialization": `BuiltIn WorkgroupSize`
  // overrides `LocalSize` when present; `LocalSizeId` is the other,
  // non-deprecated specializable spelling.
  if (HasWorkgroupSizeComposite) {
    auto It = Composites.find(WorkgroupSizeComposite);
    if (It == Composites.end() || It->second.size() != 3)
      return createStringError(
          inconvertibleErrorCode(),
          "BuiltIn WorkgroupSize decorates <id> %u, which is not a 3-element "
          "constant composite",
          WorkgroupSizeComposite);
    std::array<uint32_t, 3> Result;
    for (unsigned I = 0; I != 3; ++I) {
      Expected<uint32_t> V = resolveScalar(It->second[I]);
      if (!V)
        return V.takeError();
      Result[I] = *V;
    }
    return Result;
  }

  if (HasLocalSizeId) {
    std::array<uint32_t, 3> Result;
    for (unsigned I = 0; I != 3; ++I) {
      Expected<uint32_t> V = resolveScalar(LocalSizeIdOperands[I]);
      if (!V)
        return V.takeError();
      Result[I] = *V;
    }
    return Result;
  }

  if (HasLocalSize)
    return LocalSize;

  return createStringError(
      inconvertibleErrorCode(),
      "entry point '%s' declares none of LocalSize, LocalSizeId, or a "
      "BuiltIn WorkgroupSize specialization constant",
      EntryPoint.str().c_str());
}
