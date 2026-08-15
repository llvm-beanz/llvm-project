//===- SignatureTest.cpp - Tests for feme's signature reflection model --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Signature.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace llvm;

namespace {

/// A signature element that satisfies every `verifySignature` rule, so each
/// negative test only needs to override the one field it means to violate.
SignatureElement validInputElement(uint32_t ElementID) {
  SignatureElement Elt;
  Elt.ElementID = ElementID;
  Elt.Direction = SignatureDirection::Input;
  Elt.Location = 0;
  Elt.SemanticName = "TEXCOORD";
  Elt.SemanticIndex = 0;
  Elt.SystemValue = SignatureSystemValue::None;
  Elt.ComponentType = SignatureComponentType::Float;
  Elt.BitWidth = 32;
  Elt.FirstComponent = 0;
  Elt.ComponentCount = 4;
  Elt.RowCount = 1;
  Elt.Interpolation = SignatureInterpolationMode::Perspective;
  Elt.Frequency = SignatureFrequency::PerVertex;
  Elt.Stream = 0;
  return Elt;
}

TEST(SignatureTest, VerifyAcceptsAnEmptySignature) {
  EntrySignature Sig;
  EXPECT_TRUE(verifySignature(Sig));
}

TEST(SignatureTest, VerifyAcceptsAValidSignature) {
  EntrySignature Sig;
  Sig.Elements.push_back(validInputElement(0));
  SignatureElement Output = validInputElement(1);
  Output.Direction = SignatureDirection::Output;
  Output.SystemValue = SignatureSystemValue::Position;
  Output.Location = std::nullopt;
  Sig.Elements.push_back(Output);
  EXPECT_TRUE(verifySignature(Sig));
}

TEST(SignatureTest, VerifyRejectsDuplicateElementIDs) {
  EntrySignature Sig;
  Sig.Elements.push_back(validInputElement(0));
  Sig.Elements.push_back(validInputElement(0));
  std::string Errs;
  raw_string_ostream OS(Errs);
  EXPECT_FALSE(verifySignature(Sig, &OS));
  EXPECT_NE(Errs.find("duplicate element ID"), std::string::npos);
}

TEST(SignatureTest, VerifyRejectsOutOfRangeFirstComponent) {
  EntrySignature Sig;
  SignatureElement Elt = validInputElement(0);
  Elt.FirstComponent = 4;
  Sig.Elements.push_back(Elt);
  EXPECT_FALSE(verifySignature(Sig));
}

TEST(SignatureTest, VerifyRejectsZeroComponentCount) {
  EntrySignature Sig;
  SignatureElement Elt = validInputElement(0);
  Elt.ComponentCount = 0;
  Sig.Elements.push_back(Elt);
  EXPECT_FALSE(verifySignature(Sig));
}

TEST(SignatureTest, VerifyRejectsComponentRangeExceedingARegister) {
  EntrySignature Sig;
  SignatureElement Elt = validInputElement(0);
  Elt.FirstComponent = 2;
  Elt.ComponentCount = 4;
  Sig.Elements.push_back(Elt);
  EXPECT_FALSE(verifySignature(Sig));
}

TEST(SignatureTest, VerifyRejectsZeroRowCount) {
  EntrySignature Sig;
  SignatureElement Elt = validInputElement(0);
  Elt.RowCount = 0;
  Sig.Elements.push_back(Elt);
  EXPECT_FALSE(verifySignature(Sig));
}

TEST(SignatureTest, VerifyRejectsInvalidBitWidth) {
  EntrySignature Sig;
  SignatureElement Elt = validInputElement(0);
  Elt.BitWidth = 24;
  Sig.Elements.push_back(Elt);
  std::string Errs;
  raw_string_ostream OS(Errs);
  EXPECT_FALSE(verifySignature(Sig, &OS));
  EXPECT_NE(Errs.find("bit width"), std::string::npos);
}

TEST(SignatureTest, VerifyAcceptsEveryValidBitWidth) {
  for (uint32_t Width : {8u, 16u, 32u, 64u}) {
    EntrySignature Sig;
    SignatureElement Elt = validInputElement(0);
    Elt.BitWidth = Width;
    Sig.Elements.push_back(Elt);
    EXPECT_TRUE(verifySignature(Sig)) << "width " << Width;
  }
}

TEST(SignatureTest, VerifyRejectsSemanticIndexWithoutAName) {
  EntrySignature Sig;
  SignatureElement Elt = validInputElement(0);
  Elt.SemanticName.clear();
  Elt.SemanticIndex = 1;
  Sig.Elements.push_back(Elt);
  EXPECT_FALSE(verifySignature(Sig));
}

TEST(SignatureTest, VerifyAcceptsNoSemanticNameWithZeroIndex) {
  EntrySignature Sig;
  SignatureElement Elt = validInputElement(0);
  Elt.SemanticName.clear();
  Elt.SemanticIndex = 0;
  Sig.Elements.push_back(Elt);
  EXPECT_TRUE(verifySignature(Sig));
}

TEST(SignatureTest, VerifyRejectsPatchDirectionWithoutPerPatchFrequency) {
  EntrySignature Sig;
  SignatureElement Elt = validInputElement(0);
  Elt.Direction = SignatureDirection::PatchInput;
  Elt.Frequency = SignatureFrequency::PerVertex;
  Sig.Elements.push_back(Elt);
  EXPECT_FALSE(verifySignature(Sig));
}

TEST(SignatureTest, VerifyRejectsPerPatchFrequencyOnANonPatchElement) {
  EntrySignature Sig;
  SignatureElement Elt = validInputElement(0);
  Elt.Direction = SignatureDirection::Input;
  Elt.Frequency = SignatureFrequency::PerPatch;
  Sig.Elements.push_back(Elt);
  EXPECT_FALSE(verifySignature(Sig));
}

TEST(SignatureTest, VerifyAcceptsPatchDirectionWithPerPatchFrequency) {
  EntrySignature Sig;
  SignatureElement Elt = validInputElement(0);
  Elt.Direction = SignatureDirection::PatchOutput;
  Elt.Frequency = SignatureFrequency::PerPatch;
  Sig.Elements.push_back(Elt);
  EXPECT_TRUE(verifySignature(Sig));
}

TEST(SignatureTest, VerifyReportsEveryViolationNotJustTheFirst) {
  EntrySignature Sig;
  SignatureElement Elt = validInputElement(0);
  Elt.BitWidth = 24;
  Elt.RowCount = 0;
  Sig.Elements.push_back(Elt);
  std::string Errs;
  raw_string_ostream OS(Errs);
  EXPECT_FALSE(verifySignature(Sig, &OS));
  EXPECT_NE(Errs.find("bit width"), std::string::npos);
  EXPECT_NE(Errs.find("row count"), std::string::npos);
}

/// A signature exercising every field this model has, so the round trip
/// test below is not vacuous for any of them.
EntrySignature makeRichSignature() {
  EntrySignature Sig;

  SignatureElement Position = validInputElement(0);
  Position.Direction = SignatureDirection::Output;
  Position.Location = std::nullopt;
  Position.SemanticName = "SV_Position";
  Position.SystemValue = SignatureSystemValue::Position;
  Position.ComponentType = SignatureComponentType::Float;
  Position.BitWidth = 32;
  Position.ComponentCount = 4;
  Position.Interpolation = SignatureInterpolationMode::Flat;
  Sig.Elements.push_back(Position);

  SignatureElement Varying = validInputElement(1);
  Varying.Direction = SignatureDirection::Input;
  Varying.Location = 3;
  Varying.SemanticName = "TEXCOORD";
  Varying.SemanticIndex = 2;
  Varying.ComponentType = SignatureComponentType::SInt;
  Varying.BitWidth = 16;
  Varying.FirstComponent = 1;
  Varying.ComponentCount = 2;
  Varying.RowCount = 3;
  Varying.Interpolation = SignatureInterpolationMode::NoPerspectiveSample;
  Varying.Frequency = SignatureFrequency::PerSample;
  Sig.Elements.push_back(Varying);

  SignatureElement Patch = validInputElement(2);
  Patch.Direction = SignatureDirection::PatchOutput;
  Patch.Location = std::nullopt;
  Patch.SemanticName.clear();
  Patch.SemanticIndex = 0;
  Patch.ComponentType = SignatureComponentType::Bool;
  Patch.BitWidth = 8;
  Patch.Frequency = SignatureFrequency::PerPatch;
  Patch.Stream = 0;
  Sig.Elements.push_back(Patch);

  SignatureElement NoName = validInputElement(3);
  NoName.SemanticName.clear();
  NoName.ComponentType = SignatureComponentType::UInt;
  NoName.BitWidth = 64;
  Sig.Elements.push_back(NoName);

  return Sig;
}

TEST(SignatureTest, SerializeParseRoundTrips) {
  EntrySignature Sig = makeRichSignature();
  ASSERT_TRUE(verifySignature(Sig));

  std::vector<uint8_t> Bytes = serializeSignature(Sig);
  Expected<EntrySignature> Parsed = parseSignature(Bytes);
  ASSERT_THAT_EXPECTED(Parsed, Succeeded());
  ASSERT_EQ(Parsed->Elements.size(), Sig.Elements.size());

  for (size_t I = 0, E = Sig.Elements.size(); I != E; ++I) {
    const SignatureElement &Want = Sig.Elements[I];
    const SignatureElement &Got = Parsed->Elements[I];
    EXPECT_EQ(Got.ElementID, Want.ElementID);
    EXPECT_EQ(Got.Direction, Want.Direction);
    EXPECT_EQ(Got.Location, Want.Location);
    EXPECT_EQ(Got.SemanticName, Want.SemanticName);
    EXPECT_EQ(Got.SemanticIndex, Want.SemanticIndex);
    EXPECT_EQ(Got.SystemValue, Want.SystemValue);
    EXPECT_EQ(Got.ComponentType, Want.ComponentType);
    EXPECT_EQ(Got.BitWidth, Want.BitWidth);
    EXPECT_EQ(Got.FirstComponent, Want.FirstComponent);
    EXPECT_EQ(Got.ComponentCount, Want.ComponentCount);
    EXPECT_EQ(Got.RowCount, Want.RowCount);
    EXPECT_EQ(Got.Interpolation, Want.Interpolation);
    EXPECT_EQ(Got.Frequency, Want.Frequency);
    EXPECT_EQ(Got.Stream, Want.Stream);
  }
}

TEST(SignatureTest, SerializeParseRoundTripsAnEmptySignature) {
  EntrySignature Sig;
  std::vector<uint8_t> Bytes = serializeSignature(Sig);
  Expected<EntrySignature> Parsed = parseSignature(Bytes);
  ASSERT_THAT_EXPECTED(Parsed, Succeeded());
  EXPECT_TRUE(Parsed->Elements.empty());
}

TEST(SignatureTest, ParseRejectsTooShort) {
  std::vector<uint8_t> Bytes = {1, 2, 3};
  Expected<EntrySignature> Parsed = parseSignature(Bytes);
  EXPECT_THAT_EXPECTED(std::move(Parsed), Failed());
}

TEST(SignatureTest, ParseRejectsWrongVersion) {
  EntrySignature Sig;
  std::vector<uint8_t> Bytes = serializeSignature(Sig);
  // Corrupt the version field (the first little-endian uint32_t).
  Bytes[0] = 0xFF;
  Expected<EntrySignature> Parsed = parseSignature(Bytes);
  EXPECT_THAT_ERROR(
      Parsed.takeError(),
      Failed<StringError>(testing::Property(
          &StringError::getMessage, testing::HasSubstr("ABI version"))));
}

TEST(SignatureTest, ParseRejectsTruncatedSemanticName) {
  EntrySignature Sig;
  Sig.Elements.push_back(validInputElement(0));
  std::vector<uint8_t> Bytes = serializeSignature(Sig);
  // Truncate the buffer to cut the semantic name short.
  Bytes.resize(Bytes.size() - 4);
  Expected<EntrySignature> Parsed = parseSignature(Bytes);
  EXPECT_THAT_EXPECTED(std::move(Parsed), Failed());
}

TEST(SignatureTest, ParseRejectsTrailingBytes) {
  EntrySignature Sig;
  Sig.Elements.push_back(validInputElement(0));
  std::vector<uint8_t> Bytes = serializeSignature(Sig);
  Bytes.push_back(0);
  Bytes.push_back(0);
  Bytes.push_back(0);
  Bytes.push_back(0);
  Expected<EntrySignature> Parsed = parseSignature(Bytes);
  EXPECT_THAT_EXPECTED(std::move(Parsed), Failed());
}

TEST(SignatureTest, ParseRejectsUnknownSystemValue) {
  EntrySignature Sig;
  Sig.Elements.push_back(validInputElement(0));
  std::vector<uint8_t> Bytes = serializeSignature(Sig);

  // The system value field comes right after the semantic name and index;
  // overwrite it with a value past `NumSystemValues`.
  size_t SysValueOffset =
      2 * sizeof(uint32_t) /* version, element count */ +
      5 * sizeof(uint32_t) /* element ID, direction, has-loc, loc, name len */ +
      Sig.Elements[0].SemanticName.size() + sizeof(uint32_t) /* sem index */;
  uint32_t Bogus = 0xFFFFFFFFu;
  std::memcpy(Bytes.data() + SysValueOffset, &Bogus, sizeof(Bogus));

  Expected<EntrySignature> Parsed = parseSignature(Bytes);
  EXPECT_THAT_ERROR(Parsed.takeError(), Failed<StringError>(testing::Property(
                                            &StringError::getMessage,
                                            testing::HasSubstr("unknown"))));
}

} // namespace
