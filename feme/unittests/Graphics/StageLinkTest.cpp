//===- StageLinkTest.cpp - Tests for the cross-stage attribute linker -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Covers `feme::graphics::linkStageElements`/`copyLinkedElements` on their
// own, independently of any compiled stage: the roadmap H4 replacement for
// the hand-built shared `FemeStageLayout` roadmap G5/R34 documented as a
// stand-in for a real cross-stage attribute linker.
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/StageLink.h"

#include "feme/Core/Signature.h"
#include "feme/Graphics/StageStorage.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::graphics;
using namespace llvm;

namespace {

SignatureElement makeElement(uint32_t ElementID, SignatureDirection Dir,
                             std::optional<uint32_t> Location,
                             uint32_t ComponentCount = 1) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = Dir;
  Elt.Location = Location;
  Elt.ComponentType = SignatureComponentType::Float;
  Elt.ComponentCount = ComponentCount;
  Elt.BitWidth = 32;
  if (Dir == SignatureDirection::PatchInput ||
      Dir == SignatureDirection::PatchOutput)
    Elt.Frequency = SignatureFrequency::PerPatch;
  return Elt;
}

TEST(StageLinkTest, LinksByLocationNotByElementID) {
  EntrySignature Producer;
  Producer.Elements = {makeElement(7, SignatureDirection::Output, 1),
                       makeElement(9, SignatureDirection::Output, 0)};
  EntrySignature Consumer;
  Consumer.Elements = {makeElement(0, SignatureDirection::Input, 0),
                       makeElement(1, SignatureDirection::Input, 1)};

  Expected<SmallVector<LinkedStageElement, 4>> Links = linkStageElements(
      Producer, SignatureDirection::Output, Consumer, SignatureDirection::Input,
      "producer output -> consumer input");
  ASSERT_THAT_EXPECTED(Links, Succeeded());
  ASSERT_EQ(Links->size(), 2u);
  // Location 0 is producer element 9, consumer element 0.
  EXPECT_EQ((*Links)[0].SourceElementID, 9u);
  EXPECT_EQ((*Links)[0].DestElementID, 0u);
  // Location 1 is producer element 7, consumer element 1.
  EXPECT_EQ((*Links)[1].SourceElementID, 7u);
  EXPECT_EQ((*Links)[1].DestElementID, 1u);
}

TEST(StageLinkTest, LinksSystemValuesBySystemValue) {
  EntrySignature Producer;
  SignatureElement Factor =
      makeElement(3, SignatureDirection::PatchOutput, std::nullopt);
  Factor.SystemValue = SignatureSystemValue::TessFactorEdge;
  Factor.RowCount = 4;
  Producer.Elements = {Factor};

  EntrySignature Consumer;
  SignatureElement ConsumedFactor =
      makeElement(0, SignatureDirection::PatchInput, std::nullopt);
  ConsumedFactor.SystemValue = SignatureSystemValue::TessFactorEdge;
  ConsumedFactor.RowCount = 4;
  Consumer.Elements = {ConsumedFactor};

  Expected<SmallVector<LinkedStageElement, 4>> Links = linkStageElements(
      Producer, SignatureDirection::PatchOutput, Consumer,
      SignatureDirection::PatchInput, "patch constants -> domain");
  ASSERT_THAT_EXPECTED(Links, Succeeded());
  ASSERT_EQ(Links->size(), 1u);
  EXPECT_EQ((*Links)[0].SourceElementID, 3u);
  EXPECT_EQ((*Links)[0].DestElementID, 0u);
  EXPECT_EQ((*Links)[0].RowCount, 4u);
}

TEST(StageLinkTest, RejectsAConsumerInputWithNoProducer) {
  EntrySignature Producer;
  Producer.Elements = {makeElement(0, SignatureDirection::Output, 0)};
  EntrySignature Consumer;
  Consumer.Elements = {makeElement(0, SignatureDirection::Input, 4)};

  Expected<SmallVector<LinkedStageElement, 4>> Links = linkStageElements(
      Producer, SignatureDirection::Output, Consumer, SignatureDirection::Input,
      "producer output -> consumer input");
  ASSERT_THAT_ERROR(Links.takeError(), Failed());
}

TEST(StageLinkTest, RejectsAComponentCountMismatch) {
  EntrySignature Producer;
  Producer.Elements = {
      makeElement(0, SignatureDirection::Output, 0, /*ComponentCount=*/2)};
  EntrySignature Consumer;
  Consumer.Elements = {
      makeElement(0, SignatureDirection::Input, 0, /*ComponentCount=*/4)};

  Expected<SmallVector<LinkedStageElement, 4>> Links = linkStageElements(
      Producer, SignatureDirection::Output, Consumer, SignatureDirection::Input,
      "producer output -> consumer input");
  ASSERT_THAT_ERROR(Links.takeError(), Failed());
}

// (Roadmap H9b) A genuine `RowCount` mismatch -- neither side's own
// dimension is a per-vertex array's own extent -- must still be rejected;
// this row's own `effectiveRowCount` fold only applies when
// `RowCountIsVertexArray` is actually set.
TEST(StageLinkTest, RejectsARowCountMismatchWhenNeitherSideIsAVertexArray) {
  EntrySignature Producer;
  SignatureElement ProducerElt =
      makeElement(0, SignatureDirection::Output, 0, /*ComponentCount=*/4);
  ProducerElt.RowCount = 3;
  Producer.Elements = {ProducerElt};
  EntrySignature Consumer;
  SignatureElement ConsumerElt =
      makeElement(0, SignatureDirection::Input, 0, /*ComponentCount=*/4);
  ConsumerElt.RowCount = 2;
  Consumer.Elements = {ConsumerElt};

  Expected<SmallVector<LinkedStageElement, 4>> Links = linkStageElements(
      Producer, SignatureDirection::Output, Consumer, SignatureDirection::Input,
      "producer output -> consumer input");
  ASSERT_THAT_ERROR(Links.takeError(), Failed());
}

// (Roadmap H9b) The real gap: a geometry (or hull/domain) entry's own
// plain per-vertex-arrayed varying input -- `layout(location=0) in vec4
// in_color[];`, folded by `CanonicalizeStage.cpp`'s `addElements` into
// `RowCount == 3` (a triangle's own vertex count) with
// `RowCountIsVertexArray` set, per roadmap H5f -- must still link
// successfully against the vertex stage's own plain, unarrayed `vec4`
// output at the same location (`RowCount == 1`): the two describe the
// same single-vertex attribute, linked once per assembled primitive's own
// vertex (`feme::graphics::executeDraws`'s own per-vertex expansion into
// separate producer invocations, not a same-invocation multi-row copy).
// Before this row's fix, comparing the two `RowCount`s directly (3 vs 1)
// always disagreed for a real vertex+geometry pipeline, i.e. this exact
// shape reproduces the `vkQueueSubmit`-time "disagree on component/row
// count or type" diagnostic end to end.
TEST(StageLinkTest,
     LinksAGeometryPerVertexArrayInputAgainstAnUnarrayedProducer) {
  EntrySignature Producer;
  Producer.Elements = {
      makeElement(6, SignatureDirection::Output, 0, /*ComponentCount=*/4)};

  EntrySignature Consumer;
  SignatureElement GeomInColor =
      makeElement(0, SignatureDirection::Input, 0, /*ComponentCount=*/4);
  GeomInColor.RowCount = 3;
  GeomInColor.RowCountIsVertexArray = true;
  Consumer.Elements = {GeomInColor};

  Expected<SmallVector<LinkedStageElement, 4>> Links = linkStageElements(
      Producer, SignatureDirection::Output, Consumer, SignatureDirection::Input,
      "vertex/domain stage output -> geometry stage input");
  ASSERT_THAT_EXPECTED(Links, Succeeded());
  ASSERT_EQ(Links->size(), 1u);
  EXPECT_EQ((*Links)[0].SourceElementID, 6u);
  EXPECT_EQ((*Links)[0].DestElementID, 0u);
  // The link's own `RowCount` is the real, single-vertex shape (1), not
  // the consumer's folded per-vertex-array extent (3): `copyLinkedElements`
  // must not walk 3 "rows" out of a producer whose own storage only ever
  // has 1.
  EXPECT_EQ((*Links)[0].RowCount, 1u);
}

// End-to-end companion to the above: confirms `copyLinkedElements` (given
// the peeled `RowCount == 1` the previous test checked) actually copies
// the right scalars for each of a triangle's 3 vertices, each sourced
// from its own separate vertex-stage invocation via `SourceInvocations`
// -- exactly `feme::graphics::executeDraws`'s own geometry-input
// expansion (one storage "invocation" per (primitive, vertex-in-primitive)
// pair, not a `Row`-indexed walk within one).
TEST(StageLinkTest, CopiesLinkedGeometryPerVertexArrayInput) {
  EntrySignature Producer;
  Producer.Elements = {
      makeElement(0, SignatureDirection::Output, 0, /*ComponentCount=*/1)};

  EntrySignature Consumer;
  SignatureElement GeomInColor =
      makeElement(0, SignatureDirection::Input, 0, /*ComponentCount=*/1);
  GeomInColor.RowCount = 3;
  GeomInColor.RowCountIsVertexArray = true;
  Consumer.Elements = {GeomInColor};

  Expected<SmallVector<LinkedStageElement, 4>> Links = linkStageElements(
      Producer, SignatureDirection::Output, Consumer, SignatureDirection::Input,
      "vertex/domain stage output -> geometry stage input");
  ASSERT_THAT_EXPECTED(Links, Succeeded());

  // 3 real vertex-shader invocations (one triangle's worth).
  Expected<StageStorage> From = buildStageStorage(
      Producer, SignatureDirection::Output, /*InvocationCount=*/3);
  ASSERT_THAT_EXPECTED(From, Succeeded());
  for (uint32_t V = 0; V != 3; ++V)
    From->writeFloat(0, 0, V, static_cast<float>(V) + 10.0f);

  // 3 expanded "invocations" (one per vertex-in-primitive slot), matching
  // `Executor.cpp`'s own `RowCount * GeomVerticesPerPrimitive`-sized
  // `GSInput` storage for a single triangle primitive.
  Expected<StageStorage> To = buildStageStorage(
      Consumer, SignatureDirection::Input, /*InvocationCount=*/3);
  ASSERT_THAT_EXPECTED(To, Succeeded());
  std::vector<uint32_t> SourceInvocations = {0, 1, 2};
  copyLinkedElements(*From, *To, *Links, /*InvocationCount=*/3,
                     SourceInvocations);
  EXPECT_FLOAT_EQ(To->readFloat(0, 0, 0), 10.0f);
  EXPECT_FLOAT_EQ(To->readFloat(0, 0, 1), 11.0f);
  EXPECT_FLOAT_EQ(To->readFloat(0, 0, 2), 12.0f);
}

TEST(StageLinkTest, HonorsAConsumerFilter) {
  EntrySignature Producer;
  Producer.Elements = {makeElement(0, SignatureDirection::Output, 0),
                       makeElement(1, SignatureDirection::Output, 1)};
  EntrySignature Consumer;
  SignatureElement FromPatch = makeElement(5, SignatureDirection::Input, 1);
  FromPatch.FromInputPatch = true;
  Consumer.Elements = {makeElement(4, SignatureDirection::Input, 0), FromPatch};

  Expected<SmallVector<LinkedStageElement, 4>> Links = linkStageElements(
      Producer, SignatureDirection::Output, Consumer, SignatureDirection::Input,
      "producer output -> input patch",
      [](const SignatureElement &Elt) { return Elt.FromInputPatch; });
  ASSERT_THAT_EXPECTED(Links, Succeeded());
  ASSERT_EQ(Links->size(), 1u);
  EXPECT_EQ((*Links)[0].DestElementID, 5u);
  EXPECT_EQ((*Links)[0].SourceElementID, 1u);
}

TEST(StageLinkTest, CopiesLinkedElementsRemappingInvocations) {
  EntrySignature Producer;
  Producer.Elements = {makeElement(9, SignatureDirection::Output, 0)};
  EntrySignature Consumer;
  Consumer.Elements = {makeElement(0, SignatureDirection::Input, 0)};

  Expected<SmallVector<LinkedStageElement, 4>> Links = linkStageElements(
      Producer, SignatureDirection::Output, Consumer, SignatureDirection::Input,
      "producer output -> consumer input");
  ASSERT_THAT_EXPECTED(Links, Succeeded());

  Expected<StageStorage> From = buildStageStorage(
      Producer, SignatureDirection::Output, /*InvocationCount=*/6);
  ASSERT_THAT_EXPECTED(From, Succeeded());
  Expected<StageStorage> To = buildStageStorage(
      Consumer, SignatureDirection::Input, /*InvocationCount=*/2);
  ASSERT_THAT_EXPECTED(To, Succeeded());
  for (uint32_t I = 0; I != 6; ++I)
    From->writeFloat(9, 0, I, static_cast<float>(I) + 0.5f);

  // The second patch of a 2-control-point patch list: producer invocations
  // 2 and 3, not 0 and 1.
  std::vector<uint32_t> SourceInvocations = {2, 3};
  copyLinkedElements(*From, *To, *Links, /*InvocationCount=*/2,
                     SourceInvocations);
  EXPECT_FLOAT_EQ(To->readFloat(0, 0, 0), 2.5f);
  EXPECT_FLOAT_EQ(To->readFloat(0, 0, 1), 3.5f);
}

// (roadmap H5h) `buildStageStorage` skips allocating storage for any
// system-value *input* by default (it assumes the compiled wrapper always
// sources it from a fixed invocation-record field instead), which is wrong
// for a geometry entry's own `gl_in[]`-shaped input: `gl_in[].gl_Position`
// is read through a genuinely dynamic per-vertex-in-primitive index
// (`GeometryWrapper.cpp`'s `lowerGeometryInputLoad`), so it needs the same
// real storage an ordinary varying gets. Without
// `AllInputSystemValuesAreStorageBacked`, the `Position` input element
// below gets no storage at all (`Data.size() == 0`), and
// `copyLinkedElements` writes straight past the empty buffer -- this is
// the exact shape that crashed a real `dEQP-VK.geometry.basic.*` case
// (`vkQueueSubmit` heap corruption via `StageStorage::writeRaw`) until
// this row's fix.
TEST(StageLinkTest, BuildStageStorageSkipsSystemValueInputStorageByDefault) {
  EntrySignature Sig;
  SignatureElement PositionIn = makeElement(0, SignatureDirection::Input,
                                            std::nullopt, /*ComponentCount=*/4);
  PositionIn.SystemValue = SignatureSystemValue::Position;
  Sig.Elements = {PositionIn};

  Expected<StageStorage> Storage =
      buildStageStorage(Sig, SignatureDirection::Input, /*InvocationCount=*/4);
  ASSERT_THAT_EXPECTED(Storage, Succeeded());
  EXPECT_EQ(Storage->Data.size(), 0u);
}

TEST(StageLinkTest,
     BuildStageStorageAllocatesGeometryInputSystemValueStorageWhenRequested) {
  EntrySignature Sig;
  SignatureElement PositionIn = makeElement(0, SignatureDirection::Input,
                                            std::nullopt, /*ComponentCount=*/4);
  PositionIn.SystemValue = SignatureSystemValue::Position;
  Sig.Elements = {PositionIn};

  Expected<StageStorage> Storage =
      buildStageStorage(Sig, SignatureDirection::Input, /*InvocationCount=*/4,
                        /*AllInputSystemValuesAreStorageBacked=*/true);
  ASSERT_THAT_EXPECTED(Storage, Succeeded());
  EXPECT_GT(Storage->Data.size(), 0u);
}

TEST(StageLinkTest,
     BuildStageStorageStillSkipsGeometryInvocationRecordSystemValues) {
  // `PrimitiveID`/`InvocationID` remain sourced from
  // `FemeGeometryInvocation` even for a geometry entry's own input
  // signature, so they must still get no storage regardless of the new
  // flag.
  EntrySignature Sig;
  SignatureElement PrimitiveIDIn =
      makeElement(0, SignatureDirection::Input, std::nullopt);
  PrimitiveIDIn.SystemValue = SignatureSystemValue::PrimitiveID;
  SignatureElement InvocationIDIn =
      makeElement(1, SignatureDirection::Input, std::nullopt);
  InvocationIDIn.SystemValue = SignatureSystemValue::InvocationID;
  Sig.Elements = {PrimitiveIDIn, InvocationIDIn};

  Expected<StageStorage> Storage =
      buildStageStorage(Sig, SignatureDirection::Input, /*InvocationCount=*/4,
                        /*AllInputSystemValuesAreStorageBacked=*/true);
  ASSERT_THAT_EXPECTED(Storage, Succeeded());
  EXPECT_EQ(Storage->Data.size(), 0u);
}

// End-to-end regression test for the crash itself: links a vertex-stage
// `Position` output into a geometry-stage-shaped `gl_in[]` `Position` input
// (`AllInputSystemValuesAreStorageBacked=true`) and confirms
// `copyLinkedElements` writes/reads it correctly rather than writing past
// an unallocated (zero-size) `Data` buffer.
TEST(StageLinkTest, CopiesLinkedGeometryInputSystemValue) {
  EntrySignature Producer;
  SignatureElement PositionOut = makeElement(
      0, SignatureDirection::Output, std::nullopt, /*ComponentCount=*/4);
  PositionOut.SystemValue = SignatureSystemValue::Position;
  Producer.Elements = {PositionOut};

  EntrySignature Consumer;
  SignatureElement PositionIn = makeElement(0, SignatureDirection::Input,
                                            std::nullopt, /*ComponentCount=*/4);
  PositionIn.SystemValue = SignatureSystemValue::Position;
  Consumer.Elements = {PositionIn};

  Expected<SmallVector<LinkedStageElement, 4>> Links = linkStageElements(
      Producer, SignatureDirection::Output, Consumer, SignatureDirection::Input,
      "vertex stage output -> geometry stage input");
  ASSERT_THAT_EXPECTED(Links, Succeeded());
  ASSERT_EQ(Links->size(), 1u);

  Expected<StageStorage> From = buildStageStorage(
      Producer, SignatureDirection::Output, /*InvocationCount=*/3);
  ASSERT_THAT_EXPECTED(From, Succeeded());
  Expected<StageStorage> To = buildStageStorage(
      Consumer, SignatureDirection::Input, /*InvocationCount=*/3,
      /*AllInputSystemValuesAreStorageBacked=*/true);
  ASSERT_THAT_EXPECTED(To, Succeeded());
  ASSERT_GT(To->Data.size(), 0u);

  for (uint32_t I = 0; I != 3; ++I)
    for (uint32_t C = 0; C != 4; ++C)
      From->writeFloat(0, C, I, static_cast<float>(I * 4 + C));

  copyLinkedElements(*From, *To, *Links, /*InvocationCount=*/3, {});
  for (uint32_t I = 0; I != 3; ++I)
    for (uint32_t C = 0; C != 4; ++C)
      EXPECT_FLOAT_EQ(To->readFloat(0, C, I), static_cast<float>(I * 4 + C));
}

} // namespace
