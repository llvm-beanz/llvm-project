//===- GroupSizeTest.cpp - SPIR-V compute group-size resolution tests ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GroupSize.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"
#include "llvm/Testing/Support/Error.h"

#include "gtest/gtest.h"

using namespace feme::vulkan;
using namespace llvm;

namespace {

// The same opcode/enum values GroupSize.cpp itself scans for; duplicated
// here (rather than shared) so this test builds its fixtures independently
// of that file's internal constants, the same way a real SPIR-V producer
// would.
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

constexpr uint32_t ExecutionModelGLCompute = 5;
constexpr uint32_t ExecutionModeLocalSize = 17;
constexpr uint32_t ExecutionModeLocalSizeId = 38;
constexpr uint32_t DecorationSpecId = 1;
constexpr uint32_t DecorationBuiltIn = 11;
constexpr uint32_t BuiltInWorkgroupSize = 25;

/// Builds a well-formed SPIR-V binary word stream incrementally: a 5-word
/// header followed by appended instructions, each length-prefixed per the
/// SPIR-V specification.
class ModuleBuilder {
public:
  ModuleBuilder() {
    Words = {0x07230203u, 0x00010000u, 0u, /*bound=*/100u, 0u};
  }

  void addInstruction(uint32_t Opcode, ArrayRef<uint32_t> Operands) {
    Words.push_back(((1 + Operands.size()) << 16) | Opcode);
    llvm::append_range(Words, Operands);
  }

  /// Appends a literal string operand, padded with a null-terminated word
  /// the way SPIR-V's `LiteralString` encoding requires.
  static SmallVector<uint32_t, 4> literalString(StringRef S) {
    SmallVector<uint32_t, 4> Result;
    size_t I = 0;
    for (; I + 4 <= S.size() + 1; I += 4) {
      uint32_t Word = 0;
      for (unsigned B = 0; B != 4 && I + B < S.size(); ++B)
        Word |= static_cast<uint32_t>(static_cast<unsigned char>(S[I + B]))
                << (8 * B);
      Result.push_back(Word);
    }
    if (Result.empty() || (S.size() % 4 == 0))
      Result.push_back(0);
    return Result;
  }

  void addEntryPoint(uint32_t FnId, StringRef Name,
                     uint32_t ExecutionModel = ExecutionModelGLCompute) {
    SmallVector<uint32_t, 8> Operands{ExecutionModel, FnId};
    llvm::append_range(Operands, literalString(Name));
    addInstruction(OpEntryPoint, Operands);
  }

  void addLocalSize(uint32_t FnId, std::array<uint32_t, 3> Size) {
    addInstruction(OpExecutionMode,
                   {FnId, ExecutionModeLocalSize, Size[0], Size[1], Size[2]});
  }

  void addLocalSizeId(uint32_t FnId, std::array<uint32_t, 3> Ids) {
    addInstruction(OpExecutionModeId,
                   {FnId, ExecutionModeLocalSizeId, Ids[0], Ids[1], Ids[2]});
  }

  void addSpecId(uint32_t TargetId, uint32_t SpecId) {
    addInstruction(OpDecorate, {TargetId, DecorationSpecId, SpecId});
  }

  void addBuiltInWorkgroupSize(uint32_t TargetId) {
    addInstruction(OpDecorate,
                   {TargetId, DecorationBuiltIn, BuiltInWorkgroupSize});
  }

  void addSpecConstant(uint32_t ResultId, uint32_t Value) {
    addInstruction(OpSpecConstant, {/*type=*/1u, ResultId, Value});
  }

  void addConstant(uint32_t ResultId, uint32_t Value) {
    addInstruction(OpConstant, {/*type=*/1u, ResultId, Value});
  }

  void addSpecConstantComposite(uint32_t ResultId,
                                std::array<uint32_t, 3> Constituents) {
    addInstruction(OpSpecConstantComposite,
                   {/*type=*/2u, ResultId, Constituents[0], Constituents[1],
                    Constituents[2]});
  }

  ArrayRef<uint32_t> words() const { return Words; }

private:
  SmallVector<uint32_t, 32> Words;
};

TEST(GroupSize, ResolvesFromLocalSize) {
  ModuleBuilder Builder;
  Builder.addEntryPoint(/*FnId=*/10, "main");
  Builder.addLocalSize(/*FnId=*/10, {4, 1, 2});

  Expected<std::array<uint32_t, 3>> Result =
      resolveComputeGroupSize(Builder.words(), "main", /*Overrides=*/{});
  ASSERT_THAT_ERROR(Result.takeError(), Succeeded());
  EXPECT_EQ(*Result, (std::array<uint32_t, 3>{4, 1, 2}));
}

TEST(GroupSize, ResolvesFromLocalSizeIdDefaults) {
  ModuleBuilder Builder;
  Builder.addEntryPoint(/*FnId=*/10, "main");
  Builder.addSpecConstant(/*ResultId=*/20, 8);
  Builder.addSpecConstant(/*ResultId=*/21, 1);
  Builder.addSpecConstant(/*ResultId=*/22, 1);
  Builder.addLocalSizeId(/*FnId=*/10, {20, 21, 22});

  Expected<std::array<uint32_t, 3>> Result =
      resolveComputeGroupSize(Builder.words(), "main", /*Overrides=*/{});
  ASSERT_THAT_ERROR(Result.takeError(), Succeeded());
  EXPECT_EQ(*Result, (std::array<uint32_t, 3>{8, 1, 1}));
}

TEST(GroupSize, LocalSizeIdAppliesSpecializationOverride) {
  ModuleBuilder Builder;
  Builder.addEntryPoint(/*FnId=*/10, "main");
  Builder.addSpecConstant(/*ResultId=*/20, 8);
  Builder.addSpecId(/*TargetId=*/20, /*SpecId=*/0);
  Builder.addSpecConstant(/*ResultId=*/21, 1);
  Builder.addSpecConstant(/*ResultId=*/22, 1);
  Builder.addLocalSizeId(/*FnId=*/10, {20, 21, 22});

  SpecializationOverride Override{/*ConstantID=*/0, /*Value=*/64};
  Expected<std::array<uint32_t, 3>> Result =
      resolveComputeGroupSize(Builder.words(), "main", Override);
  ASSERT_THAT_ERROR(Result.takeError(), Succeeded());
  EXPECT_EQ(*Result, (std::array<uint32_t, 3>{64, 1, 1}));
}

TEST(GroupSize, BuiltInWorkgroupSizeOverridesLocalSize) {
  ModuleBuilder Builder;
  Builder.addEntryPoint(/*FnId=*/10, "main");
  Builder.addLocalSize(/*FnId=*/10, {1, 1, 1});
  Builder.addSpecConstant(/*ResultId=*/20, 16);
  Builder.addSpecId(/*TargetId=*/20, /*SpecId=*/0);
  Builder.addSpecConstant(/*ResultId=*/21, 2);
  Builder.addSpecId(/*TargetId=*/21, /*SpecId=*/1);
  Builder.addSpecConstant(/*ResultId=*/22, 1);
  Builder.addSpecId(/*TargetId=*/22, /*SpecId=*/2);
  Builder.addSpecConstantComposite(/*ResultId=*/23, {20, 21, 22});
  Builder.addBuiltInWorkgroupSize(/*TargetId=*/23);

  std::array<SpecializationOverride, 2> Overrides{SpecializationOverride{0, 32},
                                                  SpecializationOverride{1, 4}};
  Expected<std::array<uint32_t, 3>> Result =
      resolveComputeGroupSize(Builder.words(), "main", Overrides);
  ASSERT_THAT_ERROR(Result.takeError(), Succeeded());
  EXPECT_EQ(*Result, (std::array<uint32_t, 3>{32, 4, 1}));
}

TEST(GroupSize, ErrorsWhenEntryPointNotFound) {
  ModuleBuilder Builder;
  Builder.addEntryPoint(/*FnId=*/10, "main");
  Builder.addLocalSize(/*FnId=*/10, {1, 1, 1});

  Expected<std::array<uint32_t, 3>> Result =
      resolveComputeGroupSize(Builder.words(), "other", /*Overrides=*/{});
  ASSERT_THAT_ERROR(Result.takeError(), Failed());
}

TEST(GroupSize, ErrorsWhenNoGroupSizeInformation) {
  ModuleBuilder Builder;
  Builder.addEntryPoint(/*FnId=*/10, "main");

  Expected<std::array<uint32_t, 3>> Result =
      resolveComputeGroupSize(Builder.words(), "main", /*Overrides=*/{});
  ASSERT_THAT_ERROR(Result.takeError(), Failed());
}

// (roadmap H6f) `GraphicsPipeline.cpp`'s `validateMeshOrTaskGroupSize`
// reuses this same scanner for a mesh or task entry point, whose
// `OpEntryPoint` names the `MeshEXT`/`TaskEXT` execution model rather than
// `GLCompute` -- these two tests confirm both are accepted alongside the
// existing `GLCompute` coverage above, with the same `LocalSize` decoding.
TEST(GroupSize, ResolvesFromLocalSizeForMeshEntryPoint) {
  constexpr uint32_t ExecutionModelMeshEXT = 5365;
  ModuleBuilder Builder;
  Builder.addEntryPoint(/*FnId=*/10, "main", ExecutionModelMeshEXT);
  Builder.addLocalSize(/*FnId=*/10, {128, 1, 1});

  Expected<std::array<uint32_t, 3>> Result =
      resolveComputeGroupSize(Builder.words(), "main", /*Overrides=*/{});
  ASSERT_THAT_ERROR(Result.takeError(), Succeeded());
  EXPECT_EQ(*Result, (std::array<uint32_t, 3>{128, 1, 1}));
}

TEST(GroupSize, ResolvesFromLocalSizeForTaskEntryPoint) {
  constexpr uint32_t ExecutionModelTaskEXT = 5364;
  ModuleBuilder Builder;
  Builder.addEntryPoint(/*FnId=*/10, "main", ExecutionModelTaskEXT);
  Builder.addLocalSize(/*FnId=*/10, {32, 2, 1});

  Expected<std::array<uint32_t, 3>> Result =
      resolveComputeGroupSize(Builder.words(), "main", /*Overrides=*/{});
  ASSERT_THAT_ERROR(Result.takeError(), Succeeded());
  EXPECT_EQ(*Result, (std::array<uint32_t, 3>{32, 2, 1}));
}

} // namespace
