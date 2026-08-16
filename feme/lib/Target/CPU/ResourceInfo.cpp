//===- ResourceInfo.cpp - FeMe CPU target resource-usage info -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/ResourceInfo.h"

#include "feme/Core/StageOps.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
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

uint32_t feme::cpu::computeSideEffectFlags(const Function &F) {
  uint32_t Flags = 0;
  for (const BasicBlock &BB : F) {
    for (const Instruction &I : BB) {
      const auto *CI = dyn_cast<CallInst>(&I);
      if (!CI)
        continue;
      StageOpKind Kind;
      if (!isStageOpCall(*CI, &Kind))
        continue;
      switch (Kind) {
      case StageOpKind::Discard:
        Flags |= FEME_CPU_ARTIFACT_USES_DISCARD;
        break;
      case StageOpKind::Demote:
        Flags |= FEME_CPU_ARTIFACT_USES_DEMOTE;
        break;
      case StageOpKind::IsHelper:
        Flags |= FEME_CPU_ARTIFACT_USES_HELPER;
        break;
      default:
        break;
      }
    }
  }
  return Flags;
}

std::optional<ResourceInfo> ResourceInfo::fromModule(const Module &M,
                                                     StringRef EntryName) {
  const NamedMDNode *MD = M.getNamedMetadata("feme.cpu.resources");
  if (!MD)
    return std::nullopt;

  // See `attachResourceMetadata` in ResourceLowering.cpp for the node shape
  // this reads: {name, root-constant-size, uses-sampler-heap,
  // root-constant-space, root-constant-register, indices...}.
  for (const MDNode *Entry : MD->operands()) {
    if (Entry->getNumOperands() < 5)
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
    Info.RootConstantSpace = static_cast<uint32_t>(
        mdconst::extract<ConstantInt>(Entry->getOperand(3))->getZExtValue());
    Info.RootConstantRegister = static_cast<uint32_t>(
        mdconst::extract<ConstantInt>(Entry->getOperand(4))->getZExtValue());
    for (unsigned I = 5, E = Entry->getNumOperands(); I != E; ++I)
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

StageArtifactInfo
StageArtifactInfo::fromResourceInfo(const ResourceInfo &Info) {
  StageArtifactInfo Artifact;
  Artifact.RootConstantSize = Info.RootConstantSize;
  Artifact.RootConstantSpace = Info.RootConstantSpace;
  Artifact.RootConstantRegister = Info.RootConstantRegister;
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
/// the three counted tails: version, stage, wave size, 3 group-size
/// dimensions, groupshared size/align, root constant size, root constant
/// space/register, flags, the `StaticHeapIndices` tail's own count, the
/// reserved resource-heap prefix size, the `BoundRanges` tail's own count,
/// and the `Signature` tail's own byte length.
constexpr size_t NumFixedFields = 16;

/// The number of `uint32_t` fields one `BoundResourceRange` serializes to
/// (its four fields, in declaration order).
constexpr size_t FieldsPerBoundRange = 4;

std::vector<uint8_t>
feme::cpu::serializeArtifact(const StageArtifactInfo &Info) {
  std::vector<uint8_t> Bytes(NumFixedFields * sizeof(uint32_t) +
                             Info.StaticHeapIndices.size() * sizeof(uint32_t) +
                             Info.BoundRanges.size() * FieldsPerBoundRange *
                                 sizeof(uint32_t) +
                             Info.Signature.size());
  uint8_t *P = Bytes.data();
  auto WriteNext = [&](uint32_t V) {
    support::endian::write32le(P, V);
    P += sizeof(uint32_t);
  };
  WriteNext(ArtifactAbiVersion);
  WriteNext(static_cast<uint32_t>(Info.Stage));
  WriteNext(Info.WaveSize);
  WriteNext(Info.GroupSize[0]);
  WriteNext(Info.GroupSize[1]);
  WriteNext(Info.GroupSize[2]);
  WriteNext(Info.GroupSharedSize);
  WriteNext(Info.GroupSharedAlign);
  WriteNext(Info.RootConstantSize);
  WriteNext(Info.RootConstantSpace);
  WriteNext(Info.RootConstantRegister);
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
  WriteNext(static_cast<uint32_t>(Info.Signature.size()));
  llvm::copy(Info.Signature, P);
  P += Info.Signature.size();
  return Bytes;
}

Expected<StageArtifactInfo> feme::cpu::parseArtifact(ArrayRef<uint8_t> Bytes) {
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

  uint32_t StageValue = ReadNext();
  if (StageValue >= static_cast<uint32_t>(ShaderStage::NumStages))
    return createStringError(inconvertibleErrorCode(),
                             "FeMe CPU artifact names an unknown shader "
                             "stage (%u)",
                             StageValue);

  StageArtifactInfo Info;
  Info.Stage = static_cast<ShaderStage>(StageValue);
  Info.WaveSize = ReadNext();
  Info.GroupSize[0] = ReadNext();
  Info.GroupSize[1] = ReadNext();
  Info.GroupSize[2] = ReadNext();
  Info.GroupSharedSize = ReadNext();
  Info.GroupSharedAlign = ReadNext();
  Info.RootConstantSize = ReadNext();
  Info.RootConstantSpace = ReadNext();
  Info.RootConstantRegister = ReadNext();
  Info.Flags = ReadNext();
  uint32_t NumIndices = ReadNext();

  // Three variable-length tails are laid out back to back
  // (`StaticHeapIndices`, then `BoundRanges`, then `Signature`), each
  // preceded by its own count -- validate just enough to safely read up to
  // and including the next tail's own count field before computing the
  // final expected size below.
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

  size_t MinSizeForSignatureLength =
      (NumFixedFields + static_cast<size_t>(NumIndices) +
       static_cast<size_t>(NumRanges) * FieldsPerBoundRange) *
      sizeof(uint32_t);
  if (Bytes.size() < MinSizeForSignatureLength)
    return createStringError(inconvertibleErrorCode(),
                             "FeMe CPU artifact's declared bound-range count "
                             "(%u) is inconsistent with its length: expected "
                             "at least %zu bytes, got %zu",
                             NumRanges, MinSizeForSignatureLength,
                             Bytes.size());

  Info.BoundRanges.reserve(NumRanges);
  for (uint32_t I = 0; I != NumRanges; ++I) {
    BoundResourceRange Range;
    Range.Space = ReadNext();
    Range.BaseRegister = ReadNext();
    Range.RangeSize = ReadNext();
    Range.HeapBase = ReadNext();
    Info.BoundRanges.push_back(Range);
  }

  uint32_t SignatureLength = ReadNext();
  size_t ExpectedSize =
      MinSizeForSignatureLength + static_cast<size_t>(SignatureLength);
  if (Bytes.size() != ExpectedSize)
    return createStringError(inconvertibleErrorCode(),
                             "FeMe CPU artifact's declared signature length "
                             "(%u) is inconsistent with its length: expected "
                             "%zu bytes, got %zu",
                             SignatureLength, ExpectedSize, Bytes.size());

  Info.Signature.assign(P, P + SignatureLength);
  return Info;
}

GlobalVariable *feme::cpu::emitArtifactGlobal(Module &M, StringRef EntryName,
                                              const StageArtifactInfo &Info) {
  std::vector<uint8_t> Bytes = serializeArtifact(Info);
  Constant *Init = ConstantDataArray::get(M.getContext(), Bytes);
  auto *GV = new GlobalVariable(M, Init->getType(), /*isConstant=*/true,
                                GlobalValue::ExternalLinkage, Init,
                                getArtifactSymbolName(EntryName));
  GV->setAlignment(Align(4));
  return GV;
}

std::optional<StageArtifactInfo>
feme::cpu::readArtifactGlobal(const Module &M, StringRef EntryName) {
  const GlobalVariable *GV =
      M.getGlobalVariable(getArtifactSymbolName(EntryName));
  if (!GV || !GV->hasInitializer())
    return std::nullopt;
  const auto *Init = dyn_cast<ConstantDataArray>(GV->getInitializer());
  if (!Init)
    return std::nullopt;

  StringRef Data = Init->getRawDataValues();
  std::vector<uint8_t> Bytes(Data.begin(), Data.end());
  Expected<StageArtifactInfo> Info = parseArtifact(Bytes);
  if (!Info) {
    consumeError(Info.takeError());
    return std::nullopt;
  }
  return *Info;
}
