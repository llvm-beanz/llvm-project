//===- Signature.cpp - Source-independent signature reflection ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Signature.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cstring>

using namespace llvm;
using namespace feme;

namespace {

/// Reports \p Message to \p ErrOS, if given.
void report(raw_ostream *ErrOS, const Twine &Message) {
  if (ErrOS)
    *ErrOS << "feme-verify-signature: " << Message << "\n";
}

/// Every `ElementID` is unique within \p Sig.
bool checkUniqueElementIDs(const EntrySignature &Sig, raw_ostream *ErrOS) {
  bool Ok = true;
  DenseSet<uint32_t> Seen;
  for (const SignatureElement &Elt : Sig.Elements) {
    if (!Seen.insert(Elt.ElementID).second) {
      report(ErrOS, "duplicate element ID " + Twine(Elt.ElementID));
      Ok = false;
    }
  }
  return Ok;
}

/// `FirstComponent`/`ComponentCount`/`RowCount` describe a range that fits
/// within one register: at most 4 components, a non-empty count, and at
/// least one row.
bool checkShape(const EntrySignature &Sig, raw_ostream *ErrOS) {
  bool Ok = true;
  for (const SignatureElement &Elt : Sig.Elements) {
    if (Elt.FirstComponent >= 4) {
      report(ErrOS, "element " + Twine(Elt.ElementID) + ": first component " +
                        Twine(Elt.FirstComponent) + " is out of range");
      Ok = false;
    }
    if (Elt.ComponentCount < 1 || Elt.ComponentCount > 4) {
      report(ErrOS, "element " + Twine(Elt.ElementID) + ": component count " +
                        Twine(Elt.ComponentCount) + " is out of range");
      Ok = false;
    }
    if (Elt.FirstComponent + Elt.ComponentCount > 4) {
      report(ErrOS, "element " + Twine(Elt.ElementID) + ": first component " +
                        Twine(Elt.FirstComponent) + " plus component count " +
                        Twine(Elt.ComponentCount) + " exceeds one register");
      Ok = false;
    }
    if (Elt.RowCount < 1) {
      report(ErrOS,
             "element " + Twine(Elt.ElementID) + ": row count must be >= 1");
      Ok = false;
    }
  }
  return Ok;
}

/// `BitWidth` is one of the widths FeMe's component types support.
bool checkBitWidth(const EntrySignature &Sig, raw_ostream *ErrOS) {
  bool Ok = true;
  for (const SignatureElement &Elt : Sig.Elements) {
    if (Elt.BitWidth != 8 && Elt.BitWidth != 16 && Elt.BitWidth != 32 &&
        Elt.BitWidth != 64) {
      report(ErrOS, "element " + Twine(Elt.ElementID) + ": bit width " +
                        Twine(Elt.BitWidth) + " is not one of 8/16/32/64");
      Ok = false;
    }
  }
  return Ok;
}

/// `SemanticIndex` is only meaningful alongside a non-empty `SemanticName`.
bool checkSemanticIndex(const EntrySignature &Sig, raw_ostream *ErrOS) {
  bool Ok = true;
  for (const SignatureElement &Elt : Sig.Elements) {
    if (Elt.SemanticName.empty() && Elt.SemanticIndex != 0) {
      report(ErrOS, "element " + Twine(Elt.ElementID) +
                        ": semantic index is set without a semantic name");
      Ok = false;
    }
  }
  return Ok;
}

/// `Direction` and `Frequency` agree: patch elements are per-patch, and
/// non-patch elements are not.
bool checkDirectionFrequency(const EntrySignature &Sig, raw_ostream *ErrOS) {
  bool Ok = true;
  for (const SignatureElement &Elt : Sig.Elements) {
    bool IsPatch = Elt.Direction == SignatureDirection::PatchInput ||
                   Elt.Direction == SignatureDirection::PatchOutput;
    bool IsPerPatch = Elt.Frequency == SignatureFrequency::PerPatch;
    if (IsPatch != IsPerPatch) {
      report(ErrOS, "element " + Twine(Elt.ElementID) +
                        ": patch direction and per-patch frequency must "
                        "agree");
      Ok = false;
    }
  }
  return Ok;
}

} // namespace

bool feme::verifySignature(const EntrySignature &Sig, raw_ostream *ErrOS) {
  // Each check runs (and reports) independently rather than short-circuiting
  // on the first failure, so a single verification reports every rule a
  // signature violates, not just the first one found (matching
  // feme::cpu::verifyStructured's convention).
  bool Ok = checkUniqueElementIDs(Sig, ErrOS);
  Ok = checkShape(Sig, ErrOS) && Ok;
  Ok = checkBitWidth(Sig, ErrOS) && Ok;
  Ok = checkSemanticIndex(Sig, ErrOS) && Ok;
  Ok = checkDirectionFrequency(Sig, ErrOS) && Ok;
  return Ok;
}

/// The number of fixed `uint32_t` fields one `SignatureElement` serializes
/// to, ahead of its variable-length `SemanticName` tail: element ID,
/// direction, has-location flag, location, dual-source-blend index,
/// semantic-name length (the tail's own count), semantic index, system
/// value, component type, bit width, first component, component count, row
/// count, interpolation, frequency, stream, from-input-patch flag.
constexpr size_t NumFixedFieldsPerElement = 17;

std::vector<uint8_t> feme::serializeSignature(const EntrySignature &Sig) {
  size_t TotalSemanticBytes = 0;
  for (const SignatureElement &Elt : Sig.Elements)
    TotalSemanticBytes += Elt.SemanticName.size();

  std::vector<uint8_t> Bytes(
      /*version + element count*/ 2 * sizeof(uint32_t) +
      Sig.Elements.size() * NumFixedFieldsPerElement * sizeof(uint32_t) +
      TotalSemanticBytes);
  uint8_t *P = Bytes.data();
  auto WriteNext = [&](uint32_t V) {
    support::endian::write32le(P, V);
    P += sizeof(uint32_t);
  };
  auto WriteBytes = [&](StringRef S) {
    std::memcpy(P, S.data(), S.size());
    P += S.size();
  };

  WriteNext(SignatureAbiVersion);
  WriteNext(static_cast<uint32_t>(Sig.Elements.size()));
  for (const SignatureElement &Elt : Sig.Elements) {
    WriteNext(Elt.ElementID);
    WriteNext(static_cast<uint32_t>(Elt.Direction));
    WriteNext(Elt.Location.has_value() ? 1u : 0u);
    WriteNext(Elt.Location.value_or(0u));
    WriteNext(Elt.Index);
    WriteNext(static_cast<uint32_t>(Elt.SemanticName.size()));
    WriteBytes(Elt.SemanticName);
    WriteNext(Elt.SemanticIndex);
    WriteNext(static_cast<uint32_t>(Elt.SystemValue));
    WriteNext(static_cast<uint32_t>(Elt.ComponentType));
    WriteNext(Elt.BitWidth);
    WriteNext(Elt.FirstComponent);
    WriteNext(Elt.ComponentCount);
    WriteNext(Elt.RowCount);
    WriteNext(static_cast<uint32_t>(Elt.Interpolation));
    WriteNext(static_cast<uint32_t>(Elt.Frequency));
    WriteNext(Elt.Stream);
    WriteNext(Elt.FromInputPatch ? 1u : 0u);
  }
  assert(P == Bytes.data() + Bytes.size() &&
         "computed size did not match bytes actually written");
  return Bytes;
}

namespace {

/// Validates that \p Value names an enumerator of \p NumEnumerators,
/// reading `NumStages`-shaped enumerations (i.e. those whose last value is
/// a sentinel count) as `[0, NumEnumerators)`.
Error checkEnumRange(StringRef FieldName, uint32_t Value,
                     uint32_t NumEnumerators) {
  if (Value >= NumEnumerators)
    return createStringError(inconvertibleErrorCode(),
                             "FeMe signature element names an unknown %s "
                             "(%u)",
                             FieldName.str().c_str(), Value);
  return Error::success();
}

} // namespace

Expected<EntrySignature> feme::parseSignature(ArrayRef<uint8_t> Bytes) {
  if (Bytes.size() < 2 * sizeof(uint32_t))
    return createStringError(inconvertibleErrorCode(),
                             "FeMe signature too short: expected at least "
                             "%zu bytes, got %zu",
                             2 * sizeof(uint32_t), Bytes.size());

  const uint8_t *P = Bytes.data();
  const uint8_t *End = Bytes.data() + Bytes.size();
  auto ReadNext = [&]() -> Expected<uint32_t> {
    if (static_cast<size_t>(End - P) < sizeof(uint32_t))
      return createStringError(inconvertibleErrorCode(),
                               "FeMe signature truncated");
    uint32_t V = support::endian::read32le(P);
    P += sizeof(uint32_t);
    return V;
  };

  uint32_t Version = support::endian::read32le(P);
  P += sizeof(uint32_t);
  if (Version != SignatureAbiVersion)
    return createStringError(inconvertibleErrorCode(),
                             "FeMe signature has ABI version %u, expected %u",
                             Version, SignatureAbiVersion);

  uint32_t NumElements = support::endian::read32le(P);
  P += sizeof(uint32_t);

  EntrySignature Sig;
  Sig.Elements.reserve(NumElements);
  for (uint32_t I = 0; I != NumElements; ++I) {
    SignatureElement Elt;

    auto ReadField = [&](StringRef Name) -> Expected<uint32_t> {
      Expected<uint32_t> V = ReadNext();
      if (!V) {
        consumeError(V.takeError());
        return createStringError(inconvertibleErrorCode(),
                                 "FeMe signature truncated while reading "
                                 "element %u's %s",
                                 I, Name.str().c_str());
      }
      return V;
    };

    Expected<uint32_t> ElementID = ReadField("element ID");
    if (!ElementID)
      return ElementID.takeError();
    Elt.ElementID = *ElementID;

    Expected<uint32_t> Direction = ReadField("direction");
    if (!Direction)
      return Direction.takeError();
    if (Error E = checkEnumRange("direction", *Direction, 4))
      return std::move(E);
    Elt.Direction = static_cast<SignatureDirection>(*Direction);

    Expected<uint32_t> HasLocation = ReadField("location flag");
    if (!HasLocation)
      return HasLocation.takeError();
    Expected<uint32_t> Location = ReadField("location");
    if (!Location)
      return Location.takeError();
    Elt.Location =
        *HasLocation != 0 ? std::optional<uint32_t>(*Location) : std::nullopt;

    Expected<uint32_t> Index = ReadField("dual-source-blend index");
    if (!Index)
      return Index.takeError();
    Elt.Index = *Index;

    Expected<uint32_t> NameLen = ReadField("semantic name length");
    if (!NameLen)
      return NameLen.takeError();
    if (static_cast<size_t>(End - P) < *NameLen)
      return createStringError(inconvertibleErrorCode(),
                               "FeMe signature truncated while reading "
                               "element %u's semantic name",
                               I);
    Elt.SemanticName.assign(reinterpret_cast<const char *>(P), *NameLen);
    P += *NameLen;

    Expected<uint32_t> SemanticIndex = ReadField("semantic index");
    if (!SemanticIndex)
      return SemanticIndex.takeError();
    Elt.SemanticIndex = *SemanticIndex;

    Expected<uint32_t> SysValue = ReadField("system value");
    if (!SysValue)
      return SysValue.takeError();
    if (Error E = checkEnumRange(
            "system value", *SysValue,
            static_cast<uint32_t>(SignatureSystemValue::NumSystemValues)))
      return std::move(E);
    Elt.SystemValue = static_cast<SignatureSystemValue>(*SysValue);

    Expected<uint32_t> CompType = ReadField("component type");
    if (!CompType)
      return CompType.takeError();
    if (Error E = checkEnumRange("component type", *CompType, 4))
      return std::move(E);
    Elt.ComponentType = static_cast<SignatureComponentType>(*CompType);

    Expected<uint32_t> BitWidth = ReadField("bit width");
    if (!BitWidth)
      return BitWidth.takeError();
    Elt.BitWidth = *BitWidth;

    Expected<uint32_t> FirstComponent = ReadField("first component");
    if (!FirstComponent)
      return FirstComponent.takeError();
    Elt.FirstComponent = *FirstComponent;

    Expected<uint32_t> ComponentCount = ReadField("component count");
    if (!ComponentCount)
      return ComponentCount.takeError();
    Elt.ComponentCount = *ComponentCount;

    Expected<uint32_t> RowCount = ReadField("row count");
    if (!RowCount)
      return RowCount.takeError();
    Elt.RowCount = *RowCount;

    Expected<uint32_t> Interpolation = ReadField("interpolation mode");
    if (!Interpolation)
      return Interpolation.takeError();
    if (Error E = checkEnumRange("interpolation mode", *Interpolation, 7))
      return std::move(E);
    Elt.Interpolation = static_cast<SignatureInterpolationMode>(*Interpolation);

    Expected<uint32_t> Frequency = ReadField("frequency");
    if (!Frequency)
      return Frequency.takeError();
    if (Error E = checkEnumRange("frequency", *Frequency, 4))
      return std::move(E);
    Elt.Frequency = static_cast<SignatureFrequency>(*Frequency);

    Expected<uint32_t> Stream = ReadField("stream");
    if (!Stream)
      return Stream.takeError();
    Elt.Stream = *Stream;

    Expected<uint32_t> FromInputPatch = ReadField("from-input-patch flag");
    if (!FromInputPatch)
      return FromInputPatch.takeError();
    Elt.FromInputPatch = *FromInputPatch != 0;

    Sig.Elements.push_back(std::move(Elt));
  }

  if (P != End)
    return createStringError(inconvertibleErrorCode(),
                             "FeMe signature has %zu trailing bytes",
                             static_cast<size_t>(End - P));

  return Sig;
}
