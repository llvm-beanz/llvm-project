//===- SpecializationPatchTest.cpp - Raw spec-constant patch tests -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SpecializationPatch.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include "gtest/gtest.h"

using namespace feme::vulkan;
using namespace llvm;

namespace {

// The same opcode/enum values GroupSizeTest.cpp's own fixtures use;
// duplicated here (rather than shared) so this test builds its fixtures
// independently, the same way a real SPIR-V producer would (see that
// file's own comment).
enum : uint32_t {
  OpTypeInt = 21,
  OpConstant = 43,
  OpSpecConstant = 50,
  OpDecorate = 71,
};

// The `OpSpecConstantTrue`/`OpSpecConstantFalse` opcodes SpecializationPatch.cpp's
// own "Pass 3" rewrites in place -- SPIR-V's only `OpTypeBool`-typed
// spec-constant encoding, which carries no literal-value operand at all
// (the boolean value *is* the opcode).
enum : uint32_t {
  OpSpecConstantTrue = 48,
  OpSpecConstantFalse = 49,
};

constexpr uint32_t DecorationSpecId = 1;

/// Builds a well-formed SPIR-V binary word stream incrementally: a 5-word
/// header followed by appended instructions, each length-prefixed per the
/// SPIR-V specification. A stripped-down sibling of GroupSizeTest.cpp's own
/// `ModuleBuilder`, scoped to just what this file's tests need.
class ModuleBuilder {
public:
  ModuleBuilder() {
    Words = {0x07230203u, 0x00010000u, 0u, /*bound=*/100u, 0u};
  }

  void addInstruction(uint32_t Opcode, ArrayRef<uint32_t> Operands) {
    Words.push_back(((1 + Operands.size()) << 16) | Opcode);
    llvm::append_range(Words, Operands);
  }

  void addSpecId(uint32_t TargetId, uint32_t SpecId) {
    addInstruction(OpDecorate, {TargetId, DecorationSpecId, SpecId});
  }

  void addSpecConstant(uint32_t ResultId, uint32_t Value,
                       uint32_t TypeId = 1) {
    addInstruction(OpSpecConstant, {TypeId, ResultId, Value});
  }

  void addConstant(uint32_t ResultId, uint32_t Value, uint32_t TypeId = 1) {
    addInstruction(OpConstant, {TypeId, ResultId, Value});
  }

  void addSpecConstantBool(uint32_t ResultId, bool Value,
                           uint32_t TypeId = 2) {
    addInstruction(Value ? OpSpecConstantTrue : OpSpecConstantFalse,
                   {TypeId, ResultId});
  }

  SmallVector<uint32_t, 32> Words;
};

// A specialization constant with a matching override has its own literal
// value word overwritten in place -- this is the core L7p fix: applied
// before deserialization, so the deserializer's own default-value fold
// (e.g. `processArrayType`'s `resolveConstantArrayLength`) sees the real,
// pipeline-creation-time value instead of the module's compile-time
// default.
TEST(SpecializationPatch, OverwritesMatchingSpecConstant) {
  ModuleBuilder Builder;
  Builder.addSpecConstant(/*ResultId=*/20, /*Value=*/1);
  Builder.addSpecId(/*TargetId=*/20, /*SpecId=*/0);

  SpecializationOverride Override{/*ConstantID=*/0, /*Value=*/105};
  patchSpecializationConstants(Builder.Words, Override);

  ModuleBuilder Expected;
  Expected.addSpecConstant(/*ResultId=*/20, /*Value=*/105);
  Expected.addSpecId(/*TargetId=*/20, /*SpecId=*/0);
  EXPECT_EQ(Builder.Words, Expected.Words);
}

// A specialization constant with no matching override (a different SpecId
// is overridden) is left at its own module-declared default.
TEST(SpecializationPatch, LeavesUnmatchedSpecConstantAtDefault) {
  ModuleBuilder Builder;
  Builder.addSpecConstant(/*ResultId=*/20, /*Value=*/1);
  Builder.addSpecId(/*TargetId=*/20, /*SpecId=*/0);

  SpecializationOverride Override{/*ConstantID=*/7, /*Value=*/105};
  patchSpecializationConstants(Builder.Words, Override);

  ModuleBuilder Expected;
  Expected.addSpecConstant(/*ResultId=*/20, /*Value=*/1);
  Expected.addSpecId(/*TargetId=*/20, /*SpecId=*/0);
  EXPECT_EQ(Builder.Words, Expected.Words);
}

// A specialization constant with no `SpecId` decoration at all is left
// untouched, even if some override happens to name a matching numeric
// value -- there is nothing to route it through without a decoration.
TEST(SpecializationPatch, LeavesUndecoratedSpecConstantUntouched) {
  ModuleBuilder Builder;
  Builder.addSpecConstant(/*ResultId=*/20, /*Value=*/1);

  SpecializationOverride Override{/*ConstantID=*/0, /*Value=*/105};
  patchSpecializationConstants(Builder.Words, Override);

  ModuleBuilder Expected;
  Expected.addSpecConstant(/*ResultId=*/20, /*Value=*/1);
  EXPECT_EQ(Builder.Words, Expected.Words);
}

// A plain (non-specialization) `OpConstant` is never patched, even when
// decorated with a (meaningless, for a non-spec constant) `SpecId` -- only
// `OpSpecConstant` is ever rewritten.
TEST(SpecializationPatch, NeverPatchesPlainConstant) {
  ModuleBuilder Builder;
  Builder.addConstant(/*ResultId=*/20, /*Value=*/1);
  Builder.addSpecId(/*TargetId=*/20, /*SpecId=*/0);

  SpecializationOverride Override{/*ConstantID=*/0, /*Value=*/105};
  patchSpecializationConstants(Builder.Words, Override);

  ModuleBuilder Expected;
  Expected.addConstant(/*ResultId=*/20, /*Value=*/1);
  Expected.addSpecId(/*TargetId=*/20, /*SpecId=*/0);
  EXPECT_EQ(Builder.Words, Expected.Words);
}

// Multiple specialization constants, each with their own `SpecId`, are all
// patched independently against the matching override in a multi-entry
// `Overrides` list -- the shape a real `LocalSizeId`-style
// `gl_WorkGroupSize.x * gl_WorkGroupSize.y * gl_WorkGroupSize.z`-derived
// array length actually exercises (roadmap L7p's own root cause).
TEST(SpecializationPatch, PatchesMultipleIndependentSpecConstants) {
  ModuleBuilder Builder;
  Builder.addSpecConstant(/*ResultId=*/20, /*Value=*/1);
  Builder.addSpecId(/*TargetId=*/20, /*SpecId=*/0);
  Builder.addSpecConstant(/*ResultId=*/21, /*Value=*/1);
  Builder.addSpecId(/*TargetId=*/21, /*SpecId=*/1);
  Builder.addSpecConstant(/*ResultId=*/22, /*Value=*/1);
  Builder.addSpecId(/*TargetId=*/22, /*SpecId=*/2);

  std::array<SpecializationOverride, 2> Overrides{
      SpecializationOverride{0, 7}, SpecializationOverride{1, 5}};
  patchSpecializationConstants(Builder.Words, Overrides);

  ModuleBuilder Expected;
  Expected.addSpecConstant(/*ResultId=*/20, /*Value=*/7);
  Expected.addSpecId(/*TargetId=*/20, /*SpecId=*/0);
  Expected.addSpecConstant(/*ResultId=*/21, /*Value=*/5);
  Expected.addSpecId(/*TargetId=*/21, /*SpecId=*/1);
  Expected.addSpecConstant(/*ResultId=*/22, /*Value=*/1);
  Expected.addSpecId(/*TargetId=*/22, /*SpecId=*/2);
  EXPECT_EQ(Builder.Words, Expected.Words);
}

// An empty `Overrides` list is a no-op -- guards the fast-return path,
// which also avoids scanning the module at all when nothing could change.
TEST(SpecializationPatch, EmptyOverridesIsNoOp) {
  ModuleBuilder Builder;
  Builder.addSpecConstant(/*ResultId=*/20, /*Value=*/1);
  Builder.addSpecId(/*TargetId=*/20, /*SpecId=*/0);

  ModuleBuilder Expected = Builder;
  patchSpecializationConstants(Builder.Words, /*Overrides=*/{});
  EXPECT_EQ(Builder.Words, Expected.Words);
}

// A boolean specialization constant is encoded as `OpSpecConstantFalse`/
// `OpSpecConstantTrue` -- SPIR-V's only `OpTypeBool`-typed spec-constant
// form, with no literal-value operand of its own (the boolean value *is*
// the opcode) -- so a matching override must rewrite the instruction's own
// opcode word in place, per the offload-test-suite `VkBool32`-sized
// `DataFormat::Bool` map entry convention (nonzero selects
// `OpSpecConstantTrue`).
TEST(SpecializationPatch, OverwritesMatchingBoolSpecConstantToTrue) {
  ModuleBuilder Builder;
  Builder.addSpecConstantBool(/*ResultId=*/20, /*Value=*/false);
  Builder.addSpecId(/*TargetId=*/20, /*SpecId=*/0);

  SpecializationOverride Override{/*ConstantID=*/0, /*Value=*/1};
  patchSpecializationConstants(Builder.Words, Override);

  ModuleBuilder Expected;
  Expected.addSpecConstantBool(/*ResultId=*/20, /*Value=*/true);
  Expected.addSpecId(/*TargetId=*/20, /*SpecId=*/0);
  EXPECT_EQ(Builder.Words, Expected.Words);
}

// The same rewrite in the opposite direction: a module-declared `true`
// default overridden to `false`.
TEST(SpecializationPatch, OverwritesMatchingBoolSpecConstantToFalse) {
  ModuleBuilder Builder;
  Builder.addSpecConstantBool(/*ResultId=*/20, /*Value=*/true);
  Builder.addSpecId(/*TargetId=*/20, /*SpecId=*/0);

  SpecializationOverride Override{/*ConstantID=*/0, /*Value=*/0};
  patchSpecializationConstants(Builder.Words, Override);

  ModuleBuilder Expected;
  Expected.addSpecConstantBool(/*ResultId=*/20, /*Value=*/false);
  Expected.addSpecId(/*TargetId=*/20, /*SpecId=*/0);
  EXPECT_EQ(Builder.Words, Expected.Words);
}

// A boolean specialization constant with no matching override is left at
// its own module-declared default opcode.
TEST(SpecializationPatch, LeavesUnmatchedBoolSpecConstantAtDefault) {
  ModuleBuilder Builder;
  Builder.addSpecConstantBool(/*ResultId=*/20, /*Value=*/false);
  Builder.addSpecId(/*TargetId=*/20, /*SpecId=*/0);

  SpecializationOverride Override{/*ConstantID=*/7, /*Value=*/1};
  patchSpecializationConstants(Builder.Words, Override);

  ModuleBuilder Expected;
  Expected.addSpecConstantBool(/*ResultId=*/20, /*Value=*/false);
  Expected.addSpecId(/*TargetId=*/20, /*SpecId=*/0);
  EXPECT_EQ(Builder.Words, Expected.Words);
}

} // namespace
