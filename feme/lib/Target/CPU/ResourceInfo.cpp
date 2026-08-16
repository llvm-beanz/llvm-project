//===- ResourceInfo.cpp - FeMe CPU target resource-usage info -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/ResourceInfo.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Reads \p EntryName's entry from \p M's `!feme.cpu.bound_resources`
/// metadata (see `attachBoundResourceMetadata` in
/// BoundResourceNormalization.cpp for the node shape: {name, prefix-size,
/// (space, register, range-size, heap-base)...}), or `std::nullopt` if that
/// entry (or the node itself) isn't present -- e.g. because the shader uses
/// no traditionally-bound resource, so that pass never rewrote it.
std::optional<std::pair<uint32_t, std::vector<BoundResourceRange>>>
readBoundResourceMetadata(const Module &M, StringRef EntryName) {
  const NamedMDNode *MD = M.getNamedMetadata("feme.cpu.bound_resources");
  if (!MD)
    return std::nullopt;

  for (const MDNode *Entry : MD->operands()) {
    if (Entry->getNumOperands() < 2)
      continue;
    const auto *Name = dyn_cast<MDString>(Entry->getOperand(0));
    if (!Name || Name->getString() != EntryName)
      continue;

    uint32_t PrefixSize = static_cast<uint32_t>(
        mdconst::extract<ConstantInt>(Entry->getOperand(1))->getZExtValue());
    std::vector<BoundResourceRange> Ranges;
    for (unsigned I = 2, E = Entry->getNumOperands(); I + 4 <= E; I += 4) {
      auto GetField = [&](unsigned Offset) {
        return static_cast<uint32_t>(
            mdconst::extract<ConstantInt>(Entry->getOperand(I + Offset))
                ->getZExtValue());
      };
      Ranges.push_back(BoundResourceRange{GetField(0), GetField(1), GetField(2),
                                          GetField(3)});
    }
    return std::make_pair(PrefixSize, std::move(Ranges));
  }
  return std::nullopt;
}

} // namespace

std::array<uint32_t, 3> feme::cpu::getDeclaredGroupSize(const Function &F) {
  std::array<uint32_t, 3> Size{1, 1, 1};
  if (!F.hasFnAttribute("hlsl.numthreads"))
    return Size;
  StringRef NumThreads = F.getFnAttribute("hlsl.numthreads").getValueAsString();
  SmallVector<StringRef, 3> Components;
  NumThreads.split(Components, ',');
  if (Components.size() != 3)
    return Size;
  std::array<uint32_t, 3> Result;
  for (unsigned I = 0; I != 3; ++I)
    if (!llvm::to_integer(Components[I], Result[I], 10))
      return Size;
  return Result;
}

std::optional<ResourceInfo> ResourceInfo::fromModule(const Module &M,
                                                     StringRef EntryName) {
  const NamedMDNode *MD = M.getNamedMetadata("feme.cpu.resources");
  if (!MD)
    return std::nullopt;

  // See `attachResourceMetadata` in ResourceLowering.cpp for the node shape
  // this reads: {name, root-constant-size, uses-sampler-heap, indices...}.
  for (const MDNode *Entry : MD->operands()) {
    if (Entry->getNumOperands() < 3)
      continue;
    const auto *Name = dyn_cast<MDString>(Entry->getOperand(0));
    if (!Name || Name->getString() != EntryName)
      continue;

    ResourceInfo Info;
    Info.EntryName = EntryName.str();
    Info.RootConstantSize = static_cast<uint32_t>(
        mdconst::extract<ConstantInt>(Entry->getOperand(1))->getZExtValue());
    Info.UsesSamplerHeap =
        mdconst::extract<ConstantInt>(Entry->getOperand(2))->getZExtValue() !=
        0;
    for (unsigned I = 3, E = Entry->getNumOperands(); I != E; ++I)
      Info.StaticHeapIndices.push_back(static_cast<uint32_t>(
          mdconst::extract<ConstantInt>(Entry->getOperand(I))->getZExtValue()));

    if (auto BoundInfo = readBoundResourceMetadata(M, EntryName)) {
      Info.ReservedResourceHeapSize = BoundInfo->first;
      Info.BoundRanges = std::move(BoundInfo->second);
    }
    return Info;
  }
  return std::nullopt;
}

ArtifactInfo ArtifactInfo::fromResourceInfo(const ResourceInfo &Info) {
  ArtifactInfo Artifact;
  Artifact.RootConstantSize = Info.RootConstantSize;
  Artifact.Flags =
      Info.UsesSamplerHeap ? FEME_CPU_ARTIFACT_USES_SAMPLER_HEAP : 0u;
  Artifact.StaticHeapIndices = Info.StaticHeapIndices;
  Artifact.ReservedResourceHeapSize = Info.ReservedResourceHeapSize;
  Artifact.BoundRanges = Info.BoundRanges;
  return Artifact;
}

std::string feme::cpu::getArtifactSymbolName(StringRef EntryName) {
  return ("feme_cpu_info_" + EntryName).str();
}

/// The number of fixed (non-tail) `uint32_t` fields the layout has, ahead of
/// the two counted tails: version, wave size, 3 group-size dimensions,
/// groupshared size/align, root constant size, flags, the
/// `StaticHeapIndices` tail's own count, the reserved resource-heap prefix
/// size, and the `BoundRanges` tail's own count.
constexpr size_t NumFixedFields = 12;

/// The number of `uint32_t` fields one `BoundResourceRange` serializes to
/// (its four fields, in declaration order).
constexpr size_t FieldsPerBoundRange = 4;

std::vector<uint8_t> feme::cpu::serializeArtifact(const ArtifactInfo &Info) {
  std::vector<uint8_t> Bytes((NumFixedFields + Info.StaticHeapIndices.size() +
                              Info.BoundRanges.size() * FieldsPerBoundRange) *
                             sizeof(uint32_t));
  uint8_t *P = Bytes.data();
  auto WriteNext = [&](uint32_t V) {
    support::endian::write32le(P, V);
    P += sizeof(uint32_t);
  };
  WriteNext(ArtifactAbiVersion);
  WriteNext(Info.WaveSize);
  WriteNext(Info.GroupSize[0]);
  WriteNext(Info.GroupSize[1]);
  WriteNext(Info.GroupSize[2]);
  WriteNext(Info.GroupSharedSize);
  WriteNext(Info.GroupSharedAlign);
  WriteNext(Info.RootConstantSize);
  WriteNext(Info.Flags);
  WriteNext(static_cast<uint32_t>(Info.StaticHeapIndices.size()));
  for (uint32_t Idx : Info.StaticHeapIndices)
    WriteNext(Idx);
  WriteNext(Info.ReservedResourceHeapSize);
  WriteNext(static_cast<uint32_t>(Info.BoundRanges.size()));
  for (const BoundResourceRange &Range : Info.BoundRanges) {
    WriteNext(Range.Space);
    WriteNext(Range.BaseRegister);
    WriteNext(Range.RangeSize);
    WriteNext(Range.HeapBase);
  }
  return Bytes;
}

Expected<ArtifactInfo> feme::cpu::parseArtifact(ArrayRef<uint8_t> Bytes) {
  if (Bytes.size() < NumFixedFields * sizeof(uint32_t))
    return createStringError(inconvertibleErrorCode(),
                             "FeMe CPU artifact too short: expected at "
                             "least %zu bytes, got %zu",
                             NumFixedFields * sizeof(uint32_t), Bytes.size());

  const uint8_t *P = Bytes.data();
  auto ReadNext = [&]() {
    uint32_t V = support::endian::read32le(P);
    P += sizeof(uint32_t);
    return V;
  };

  uint32_t Version = ReadNext();
  if (Version != ArtifactAbiVersion)
    return createStringError(inconvertibleErrorCode(),
                             "FeMe CPU artifact has ABI version %u, expected "
                             "%u",
                             Version, ArtifactAbiVersion);

  ArtifactInfo Info;
  Info.WaveSize = ReadNext();
  Info.GroupSize[0] = ReadNext();
  Info.GroupSize[1] = ReadNext();
  Info.GroupSize[2] = ReadNext();
  Info.GroupSharedSize = ReadNext();
  Info.GroupSharedAlign = ReadNext();
  Info.RootConstantSize = ReadNext();
  Info.Flags = ReadNext();
  uint32_t NumIndices = ReadNext();

  // Two variable-length tails are laid out back to back (`StaticHeapIndices`
  // then `BoundRanges`), each preceded by its own count -- validate just
  // enough to safely read up to and including the second tail's own count
  // field before computing the final expected size below.
  size_t MinSizeForRangeCount =
      (NumFixedFields + static_cast<size_t>(NumIndices)) * sizeof(uint32_t);
  if (Bytes.size() < MinSizeForRangeCount)
    return createStringError(inconvertibleErrorCode(),
                             "FeMe CPU artifact's declared heap-index count "
                             "(%u) is inconsistent with its length: expected "
                             "at least %zu bytes, got %zu",
                             NumIndices, MinSizeForRangeCount, Bytes.size());

  Info.StaticHeapIndices.reserve(NumIndices);
  for (uint32_t I = 0; I != NumIndices; ++I)
    Info.StaticHeapIndices.push_back(ReadNext());

  Info.ReservedResourceHeapSize = ReadNext();
  uint32_t NumRanges = ReadNext();

  size_t ExpectedSize = (NumFixedFields + static_cast<size_t>(NumIndices) +
                         static_cast<size_t>(NumRanges) * FieldsPerBoundRange) *
                        sizeof(uint32_t);
  if (Bytes.size() != ExpectedSize)
    return createStringError(inconvertibleErrorCode(),
                             "FeMe CPU artifact's declared bound-range count "
                             "(%u) is inconsistent with its length: expected "
                             "%zu bytes, got %zu",
                             NumRanges, ExpectedSize, Bytes.size());

  Info.BoundRanges.reserve(NumRanges);
  for (uint32_t I = 0; I != NumRanges; ++I) {
    BoundResourceRange Range;
    Range.Space = ReadNext();
    Range.BaseRegister = ReadNext();
    Range.RangeSize = ReadNext();
    Range.HeapBase = ReadNext();
    Info.BoundRanges.push_back(Range);
  }
  return Info;
}

GlobalVariable *feme::cpu::emitArtifactGlobal(Module &M, StringRef EntryName,
                                              const ArtifactInfo &Info) {
  std::vector<uint8_t> Bytes = serializeArtifact(Info);
  Constant *Init = ConstantDataArray::get(M.getContext(), Bytes);
  auto *GV = new GlobalVariable(M, Init->getType(), /*isConstant=*/true,
                                GlobalValue::ExternalLinkage, Init,
                                getArtifactSymbolName(EntryName));
  GV->setAlignment(Align(4));
  return GV;
}

std::optional<ArtifactInfo> feme::cpu::readArtifactGlobal(const Module &M,
                                                          StringRef EntryName) {
  const GlobalVariable *GV =
      M.getGlobalVariable(getArtifactSymbolName(EntryName));
  if (!GV || !GV->hasInitializer())
    return std::nullopt;
  const auto *Init = dyn_cast<ConstantDataArray>(GV->getInitializer());
  if (!Init)
    return std::nullopt;

  StringRef Data = Init->getRawDataValues();
  std::vector<uint8_t> Bytes(Data.begin(), Data.end());
  Expected<ArtifactInfo> Info = parseArtifact(Bytes);
  if (!Info) {
    consumeError(Info.takeError());
    return std::nullopt;
  }
  return *Info;
}
