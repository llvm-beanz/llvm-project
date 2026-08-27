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

} // namespace
