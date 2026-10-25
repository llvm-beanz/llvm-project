//===- DXContainer.cpp - Legacy DXIL container assembly
//---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DXContainer.h"
#include "dxil/Bitcode/ReaderWriter.h"
#include "dxil/IR/Constants.h"
#include "dxil/IR/Function.h"
#include "dxil/IR/Module.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/MC/DXContainerPSVInfo.h"
#include "llvm/MC/MCDXContainerWriter.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace dxil {
namespace {

Error metadataError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(),
                           "cannot emit DXContainer: " + Message);
}

Expected<const MDTuple *> tuple(const Metadata *MD, unsigned Size,
                                StringRef Name) {
  auto *Node = dyn_cast_or_null<MDTuple>(MD);
  if (!Node || Node->getNumOperands() != Size)
    return metadataError(Name + " must be a tuple with " + Twine(Size) +
                         " operands");
  return Node;
}

Expected<const MDTuple *> namedTuple(const Module &M, StringRef Name,
                                     unsigned Size) {
  auto *Named = M.getNamedMetadata(Name);
  if (!Named || Named->getNumOperands() != 1)
    return metadataError(Name + " must contain exactly one tuple");
  return tuple(Named->getOperand(0), Size, Name);
}

Expected<uint64_t> integer(const Metadata *MD, unsigned Width, StringRef Name) {
  auto *Value = mdconst::dyn_extract_or_null<ConstantInt>(MD);
  if (!Value || Value->getBitWidth() != Width)
    return metadataError(Name + " must be an i" + Twine(Width) + " constant");
  return Value->getZExtValue();
}

Expected<unsigned> version(const Module &M, StringRef Name, unsigned Major,
                           unsigned MaxMinor) {
  auto Node = namedTuple(M, Name, 2);
  if (!Node)
    return Node.takeError();
  auto MajorValue = integer((*Node)->getOperand(0), 32, Name);
  if (!MajorValue)
    return MajorValue.takeError();
  auto MinorValue = integer((*Node)->getOperand(1), 32, Name);
  if (!MinorValue)
    return MinorValue.takeError();
  if (*MajorValue != Major || *MinorValue > MaxMinor)
    return metadataError("unsupported " + Name + " version");
  return *MinorValue;
}

Error readResources(const MDTuple *Resources, mcdxbc::PSVRuntimeInfo &PSV) {
  // PSV bindings are ordered CBVs, samplers, SRVs, then UAVs.
  for (unsigned Class : {2u, 3u, 0u, 1u}) {
    const Metadata *ListMD = Resources->getOperand(Class);
    if (!ListMD)
      continue;
    auto *List = dyn_cast<MDTuple>(ListMD);
    if (!List)
      return metadataError("resource lists must be tuples");
    for (const MDOperand &Op : List->operands()) {
      auto R = tuple(Op, Class == 0 ? 9 : Class == 1 ? 11 : 8, "resource");
      if (!R)
        return R.takeError();
      auto Space = integer((*R)->getOperand(3), 32, "resource space");
      if (!Space)
        return Space.takeError();
      auto Lower = integer((*R)->getOperand(4), 32, "resource lower bound");
      if (!Lower)
        return Lower.takeError();
      auto Size = integer((*R)->getOperand(5), 32, "resource range size");
      if (!Size)
        return Size.takeError();
      if (!*Size || (*Size != UINT32_MAX && *Lower + *Size - 1 > UINT32_MAX))
        return metadataError("invalid resource binding range");

      dxbc::PSV::v2::ResourceBindInfo Binding{};
      Binding.Space = *Space;
      Binding.LowerBound = *Lower;
      Binding.UpperBound =
          *Size == UINT32_MAX ? UINT32_MAX : *Lower + *Size - 1;
      using Kind = dxbc::PSV::ResourceKind;
      using Type = dxbc::PSV::ResourceType;
      if (Class >= 2) {
        Binding.Kind = Class == 2 ? Kind::CBuffer : Kind::Sampler;
        Binding.Type = Class == 2 ? Type::CBV : Type::Sampler;
      } else {
        auto Shape = integer((*R)->getOperand(6), 32, "resource kind");
        if (!Shape)
          return Shape.takeError();
        if (*Shape < 1 || *Shape > 18 || *Shape == 13 || *Shape == 14 ||
            *Shape == 15 ||
            (Class == 1 && (*Shape == 5 || *Shape == 9 || *Shape == 16)))
          return metadataError("unsupported resource kind");
        Binding.Kind = static_cast<Kind>(*Shape);
        switch (Binding.Kind) {
        case Kind::RawBuffer:
        case Kind::RTAccelerationStructure:
          Binding.Type = Class == 1 ? Type::UAVRaw : Type::SRVRaw;
          break;
        case Kind::StructuredBuffer:
          Binding.Type = Class == 1 ? Type::UAVStructured : Type::SRVStructured;
          break;
        default:
          Binding.Type = Class == 1 ? Type::UAVTyped : Type::SRVTyped;
          break;
        }
        if (Class == 1) {
          auto Counter = integer((*R)->getOperand(8), 1, "UAV counter");
          if (!Counter)
            return Counter.takeError();
          if (*Counter) {
            if (Binding.Kind != Kind::StructuredBuffer)
              return metadataError("only structured UAVs can have counters");
            Binding.Type = Type::UAVStructuredWithCounter;
          }
        }
      }

      const Metadata *ExtraMD = (*R)->getOperand((*R)->getNumOperands() - 1);
      if (ExtraMD) {
        auto *Extra = dyn_cast<MDTuple>(ExtraMD);
        if (!Extra || Extra->getNumOperands() % 2)
          return metadataError("resource properties must be tag/value pairs");
        SmallSet<uint64_t, 4> Seen;
        for (unsigned I = 0; I < Extra->getNumOperands(); I += 2) {
          auto Tag = integer(Extra->getOperand(I), 32, "resource property tag");
          if (!Tag)
            return Tag.takeError();
          auto Value =
              integer(Extra->getOperand(I + 1), 32, "resource property value");
          if (!Value)
            return Value.takeError();
          if (*Tag > 3 || !Seen.insert(*Tag).second)
            return metadataError("unsupported or duplicate resource property");
          if (*Tag == 3) {
            if (Class != 1 || *Value > 1)
              return metadataError("invalid resource atomic64 flag");
            Binding.Flags.Bits.UsedByAtomic64 = *Value;
          }
        }
      }
      PSV.Resources.push_back(Binding);
    }
  }
  return Error::success();
}

class ContainerWriter : public MCDXContainerBaseWriter {
  ArrayRef<MCDXContainerPart> Parts;

  ArrayRef<MCDXContainerPart> collectParts() override { return Parts; }

public:
  explicit ContainerWriter(ArrayRef<MCDXContainerPart> Parts) : Parts(Parts) {}
};

} // namespace

Expected<std::string> assembleDXContainer(const Module &M,
                                          bool PreserveUseListOrder) {
  auto SM = namedTuple(M, "dx.shaderModel", 3);
  if (!SM)
    return SM.takeError();
  auto *Stage = dyn_cast_or_null<MDString>((*SM)->getOperand(0));
  if (!Stage || Stage->getString() != "cs")
    return metadataError("only compute shaders are supported");
  auto Major = integer((*SM)->getOperand(1), 32, "shader model major version");
  if (!Major)
    return Major.takeError();
  auto Minor = integer((*SM)->getOperand(2), 32, "shader model minor version");
  if (!Minor)
    return Minor.takeError();
  if (*Major != 6 || *Minor > 7)
    return metadataError("supported shader models are cs_6_0 through cs_6_7");
  auto DXILMinor = version(M, "dx.version", 1, 7);
  if (!DXILMinor)
    return DXILMinor.takeError();
  if (*DXILMinor != *Minor)
    return metadataError("DXIL version must match the shader model");
  auto ValidatorMinor = version(M, "dx.valver", 1, 8);
  if (!ValidatorMinor)
    return ValidatorMinor.takeError();
  if (*ValidatorMinor < *Minor)
    return metadataError("validator version is older than the shader model");
  for (const NamedMDNode &Named : M.named_metadata())
    if (Named.getName().starts_with("llvm.dbg.") ||
        Named.getName() == "dx.rootSignature" ||
        Named.getName() == "dx.subobjects")
      return metadataError("unsupported container metadata: " +
                           Named.getName());

  auto Entry = namedTuple(M, "dx.entryPoints", 5);
  if (!Entry)
    return Entry.takeError();
  auto *FunctionMD = dyn_cast_or_null<ValueAsMetadata>((*Entry)->getOperand(0));
  auto *F = FunctionMD ? dyn_cast<Function>(FunctionMD->getValue()) : nullptr;
  auto *Name = dyn_cast_or_null<MDString>((*Entry)->getOperand(1));
  if (!F || F->isDeclaration() || !Name || Name->getString().empty() ||
      Name->getString().contains('\0'))
    return metadataError("entry point must name a defined function");
  if (const Metadata *Signatures = (*Entry)->getOperand(2)) {
    auto Sig = tuple(Signatures, 3, "entry signatures");
    if (!Sig)
      return Sig.takeError();
    for (const MDOperand &Op : (*Sig)->operands())
      if (Op) {
        auto *List = dyn_cast<MDTuple>(Op);
        if (!List || List->getNumOperands())
          return metadataError("compute signatures must be empty");
      }
  }

  mcdxbc::PSVRuntimeInfo PSV;
  PSV.EntryName = Name->getString();
  PSV.BaseData.ShaderStage = Triple::Compute - Triple::Pixel;
  PSV.BaseData.MaximumWaveLaneCount = UINT32_MAX;
  auto *Properties = dyn_cast_or_null<MDTuple>((*Entry)->getOperand(4));
  if (!Properties || Properties->getNumOperands() % 2)
    return metadataError("entry properties must be tag/value pairs");
  SmallSet<uint64_t, 4> Seen;
  uint64_t Flags = 0;
  for (unsigned I = 0; I < Properties->getNumOperands(); I += 2) {
    auto Tag = integer(Properties->getOperand(I), 32, "entry property tag");
    if (!Tag)
      return Tag.takeError();
    if (!Seen.insert(*Tag).second)
      return metadataError("duplicate entry property");
    const Metadata *Value = Properties->getOperand(I + 1);
    switch (*Tag) {
    case 0: {
      auto RawFlags = integer(Value, 64, "shader flags");
      if (!RawFlags)
        return RawFlags.takeError();
      Flags = *RawFlags;
      break;
    }
    case 4: {
      auto Threads = tuple(Value, 3, "numthreads");
      if (!Threads)
        return Threads.takeError();
      uint32_t Dims[3];
      for (unsigned D = 0; D < 3; ++D) {
        auto Dim = integer((*Threads)->getOperand(D), 32, "thread count");
        if (!Dim)
          return Dim.takeError();
        if (!*Dim || *Dim > (D == 2 ? 64u : 1024u))
          return metadataError("invalid numthreads dimension");
        Dims[D] = *Dim;
      }
      if (uint64_t(Dims[0]) * Dims[1] * Dims[2] > 1024)
        return metadataError("numthreads product exceeds 1024");
      PSV.BaseData.NumThreadsX = Dims[0];
      PSV.BaseData.NumThreadsY = Dims[1];
      PSV.BaseData.NumThreadsZ = Dims[2];
      break;
    }
    case 11: {
      auto Wave = tuple(Value, 1, "wave size");
      if (!Wave)
        return Wave.takeError();
      auto Size = integer((*Wave)->getOperand(0), 32, "wave size");
      if (!Size)
        return Size.takeError();
      if (*Minor < 6 || *Size < 4 || *Size > 128 || !isPowerOf2_64(*Size))
        return metadataError("unsupported wave size");
      PSV.BaseData.MinimumWaveLaneCount = *Size;
      PSV.BaseData.MaximumWaveLaneCount = *Size;
      break;
    }
    default:
      return metadataError("unsupported entry property tag " + Twine(*Tag));
    }
  }
  if (!Seen.contains(4))
    return metadataError("missing numthreads entry property");

  const Metadata *Resources = (*Entry)->getOperand(3);
  if (M.getNamedMetadata("dx.resources")) {
    auto GlobalResources = namedTuple(M, "dx.resources", 4);
    if (!GlobalResources)
      return GlobalResources.takeError();
    if (Resources && Resources != *GlobalResources)
      return metadataError("entry and module resource metadata differ");
    Resources = *GlobalResources;
  }
  if (Resources) {
    auto R = tuple(Resources, 4, "resources");
    if (!R)
      return R.takeError();
    if (Error E = readResources(*R, PSV))
      return std::move(E);
  }

  uint64_t FeatureFlags = 0;
  uint64_t KnownFlags = 0;
  auto Mask = [](int Bit) -> uint64_t { return Bit < 0 ? 0 : 1ull << Bit; };
#define SHADER_FEATURE_FLAG(FeatureBit, ModuleBit, FlagName, Description)      \
  KnownFlags |= Mask(ModuleBit);                                               \
  if (Flags & Mask(ModuleBit))                                                 \
    FeatureFlags |= Mask(FeatureBit);
#define DXIL_MODULE_FLAG(ModuleBit, FlagName, Description)                     \
  KnownFlags |= Mask(ModuleBit);
#include "llvm/BinaryFormat/DXContainerConstants.def"
  if (Flags & ~(KnownFlags & ((1ull << 36) - 1)))
    return metadataError("unsupported shader flags");
  // LowPrecisionPresent selects minimum or native precision according to
  // bit 23.
  if (Flags & (1ull << 5))
    FeatureFlags |= Mask(Flags & (1ull << 23) ? 18 : 4);

  unsigned PSVVersion = *ValidatorMinor < 1   ? 0
                        : *ValidatorMinor < 6 ? 1
                        : *ValidatorMinor < 8 ? 2
                                              : 3;
  PSV.finalize(Triple::Compute, PSVVersion);
  std::string PSVData, SFIData, SigData, HashData, Bitcode;
  raw_string_ostream PSVOS(PSVData), SFIOS(SFIData), SigOS(SigData),
      HashOS(HashData), BitcodeOS(Bitcode);
  PSV.write(PSVOS, PSVVersion);
  support::endian::write(SFIOS, FeatureFlags, llvm::endianness::little);
  mcdxbc::Signature Signature;
  Signature.write(SigOS, *ValidatorMinor >= 7);
  WriteBitcodeToFile(&M, BitcodeOS, PreserveUseListOrder);
  MD5 Digest;
  Digest.update(Bitcode);
  auto Hash = Digest.final();
  support::endian::write(HashOS, uint32_t(0), llvm::endianness::little);
  HashOS.write(reinterpret_cast<const char *>(Hash.data()), Hash.size());
  SmallVector<MCDXContainerPart> Parts{{"SFI0", SFIData},
                                       {"ISG1", SigData},
                                       {"OSG1", SigData},
                                       {"PSV0", PSVData}};
  if (*ValidatorMinor >= 5)
    Parts.push_back({"HASH", HashData});
  Parts.push_back({"DXIL", Bitcode});
  uint64_t ContainerSize =
      sizeof(dxbc::Header) + Parts.size() * sizeof(uint32_t);
  for (const auto &Part : Parts)
    ContainerSize += sizeof(dxbc::PartHeader) + alignTo(Part.Data.size(), 4);
  ContainerSize += sizeof(dxbc::ProgramHeader);
  if (ContainerSize > UINT32_MAX)
    return metadataError("container exceeds the 32-bit file size limit");
  Triple TT("dxilv1." + Twine(*DXILMinor) + "-pc-shadermodel6." +
            Twine(*Minor) + "-compute");
  std::string Result;
  raw_string_ostream OS(Result);
  ContainerWriter(Parts).write(OS, TT);
  return Result;
}
} // namespace dxil
