//===- StageStorageTest.cpp - Tests for feme::graphics::StageStorage ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (Roadmap L98(a)) Covers `buildStageStorage`'s acceptance of a 16-bit
// `float16_t` element (stored as a widened `float32`, per
// `StageStorage.h`'s own "Scope" comment) alongside its continued
// rejection of any other still-unsupported scalar width.
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/StageStorage.h"

#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace feme;
using namespace feme::graphics;

namespace {

SignatureElement makeElement(uint32_t ElementID, SignatureDirection Dir,
                             SignatureComponentType ComponentType,
                             uint32_t BitWidth) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = Dir;
  Elt.Location = 0;
  Elt.ComponentType = ComponentType;
  Elt.ComponentCount = 1;
  Elt.BitWidth = BitWidth;
  return Elt;
}

// A 16-bit `Float` element is accepted -- and, per the widened-storage
// design, reads/writes as an ordinary 32-bit slot: `readRaw`/`writeRaw`
// (and their `readFloat`/`writeFloat` wrappers) require no special-casing
// at all, since the compiled wrapper is responsible for the actual
// `half`<->`float` conversion at its own load/store boundary, not this
// storage layer.
TEST(StageStorageTest, AcceptsWidenedHalfFloatElement) {
  EntrySignature Sig;
  Sig.Elements = {makeElement(0, SignatureDirection::Output,
                              SignatureComponentType::Float,
                              /*BitWidth=*/16)};

  Expected<StageStorage> Storage = buildStageStorage(
      Sig, SignatureDirection::Output, /*InvocationCount=*/2);
  ASSERT_THAT_EXPECTED(Storage, Succeeded());

  Storage->writeFloat(0, 0, /*Invocation=*/0, 0.125f);
  Storage->writeFloat(0, 0, /*Invocation=*/1, 0.25f);
  EXPECT_FLOAT_EQ(Storage->readFloat(0, 0, /*Invocation=*/0), 0.125f);
  EXPECT_FLOAT_EQ(Storage->readFloat(0, 0, /*Invocation=*/1), 0.25f);
}

// A 16-bit element that is *not* `Float`-typed has no widened-storage
// interpretation (there is no analogous "widen a 16-bit int/bool into a
// real 32-bit value" story -- unlike a `half`, a 16-bit integer's own raw
// bits are its whole value, and FeMe's `SignatureComponentType` has no
// such 16-bit-integer shape today regardless), so it is still rejected
// exactly like any other unimplemented width.
TEST(StageStorageTest, RejectsNonFloat16BitElement) {
  EntrySignature Sig;
  Sig.Elements = {makeElement(0, SignatureDirection::Output,
                              SignatureComponentType::SInt,
                              /*BitWidth=*/16)};

  Expected<StageStorage> Storage = buildStageStorage(
      Sig, SignatureDirection::Output, /*InvocationCount=*/1);
  EXPECT_THAT_EXPECTED(Storage, Failed());
}

// A genuinely unsupported width (neither the ordinary 32-bit case nor the
// 16-bit-widened-`Float` exception) is still rejected.
TEST(StageStorageTest, RejectsUnsupportedBitWidth) {
  EntrySignature Sig;
  Sig.Elements = {makeElement(0, SignatureDirection::Output,
                              SignatureComponentType::Float,
                              /*BitWidth=*/64)};

  Expected<StageStorage> Storage = buildStageStorage(
      Sig, SignatureDirection::Output, /*InvocationCount=*/1);
  EXPECT_THAT_EXPECTED(Storage, Failed());
}

} // namespace
