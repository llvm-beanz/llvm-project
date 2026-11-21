//===- RootBindingPlan.cpp - Checked DirectX root layouts ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/DXIL/RootBindingPlan.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/bit.h"
#include "llvm/IR/Function.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"

#include <type_traits>

using namespace llvm;
using namespace feme::dxil;
using llvm::dxil::ResourceClass;

namespace {

constexpr uint32_t Unbounded = UINT32_MAX;

Error invalid(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(),
                           "invalid root signature: " + Message);
}

bool overlaps(uint64_t A, uint64_t AEnd, uint64_t B, uint64_t BEnd) {
  return A < BEnd && B < AEnd;
}

uint64_t endRegister(const RootBinding &Binding) {
  return Binding.Count == Unbounded
             ? uint64_t(1) << 32
             : uint64_t(Binding.Register) + Binding.Count;
}

bool visibilityOverlaps(dxbc::ShaderVisibility A, dxbc::ShaderVisibility B) {
  return A == dxbc::ShaderVisibility::All || B == dxbc::ShaderVisibility::All ||
         A == B;
}

bool bindingOverlaps(const RootBinding &A, const RootBinding &B) {
  return A.Class == B.Class && A.Space == B.Space &&
         overlaps(A.Register, endRegister(A), B.Register, endRegister(B));
}

bool validDataFlags(uint32_t Flags) {
  uint32_t Data = Flags & 0xe;
  return Data == 0 || has_single_bit(Data);
}

StringRef className(ResourceClass Class) {
  switch (Class) {
  case ResourceClass::SRV:
    return "srv";
  case ResourceClass::UAV:
    return "uav";
  case ResourceClass::CBuffer:
    return "cbv";
  case ResourceClass::Sampler:
    return "sampler";
  }
  llvm_unreachable("invalid resource class");
}

} // namespace

Expected<RootBindingPlan> RootBindingPlan::parse(ArrayRef<uint8_t> Bytes) {
  object::DirectX::RootSignature Signature(
      StringRef(reinterpret_cast<const char *>(Bytes.data()), Bytes.size()));
  if (Error E = Signature.parse())
    return E;
  if (Signature.getVersion() != 1 && Signature.getVersion() != 2)
    return invalid("only versions 1.0 and 1.1 are supported");

  RootBindingPlan Plan;
  Plan.Version = Signature.getVersion();
  Plan.Flags = Signature.getFlags();
  uint32_t KnownFlags = 0;
#define ROOT_SIGNATURE_FLAG(Value, Name) KnownFlags |= Value;
#include "llvm/BinaryFormat/DXContainerConstants.def"
  if (Plan.Flags & ~KnownFlags)
    return invalid("unknown root flags");
  uint64_t Cost = 0;

  for (const auto &Header : Signature.param_headers()) {
    if (!dxbc::isValidShaderVisibility(Header.ShaderVisibility))
      return invalid("unknown shader visibility");
    auto View = Signature.getParameter(Header);
    if (!View)
      return View.takeError();
    RootParameter Parameter{
        static_cast<dxbc::RootParameterType>(Header.ParameterType),
        static_cast<dxbc::ShaderVisibility>(Header.ShaderVisibility),
        0,
        {}};
    if (auto *Constants = dyn_cast<object::DirectX::RootConstantView>(&*View)) {
      auto Data = Constants->read();
      if (!Data)
        return Data.takeError();
      if (!Data->Num32BitValues)
        return invalid("root constants must be nonempty");
      Parameter.ConstantCount = Data->Num32BitValues;
      Parameter.Bindings.push_back({ResourceClass::CBuffer, Data->RegisterSpace,
                                    Data->ShaderRegister, 1});
      Cost += Data->Num32BitValues;
    } else if (auto *Descriptor =
                   dyn_cast<object::DirectX::RootDescriptorView>(&*View)) {
      auto Data = Descriptor->read(Plan.Version);
      if (!Data)
        return Data.takeError();
      if (!dxbc::isValidRootDesciptorFlags(Data->Flags) ||
          !validDataFlags(Data->Flags))
        return invalid("invalid root descriptor flags");
      ResourceClass Class = Parameter.Kind == dxbc::RootParameterType::CBV
                                ? ResourceClass::CBuffer
                            : Parameter.Kind == dxbc::RootParameterType::SRV
                                ? ResourceClass::SRV
                                : ResourceClass::UAV;
      Parameter.Bindings.push_back({Class, Data->RegisterSpace,
                                    Data->ShaderRegister, 1, 0, Data->Flags});
      Cost += 2;
    } else {
      auto *Table = cast<object::DirectX::DescriptorTableView>(&*View);
      uint64_t NextOffset = 0;
      auto AddRanges = [&](const auto &Ranges) -> Error {
        if (!Ranges.NumRanges)
          return invalid("descriptor tables must be nonempty");
        for (const auto &Range : Ranges) {
          if (!dxbc::isValidRangeType(Range.RangeType) || !Range.NumDescriptors)
            return invalid("invalid descriptor range type or count");
          uint32_t RangeFlags = 0;
          if constexpr (std::is_same_v<std::decay_t<decltype(Range)>,
                                       dxbc::RTS0::v2::DescriptorRange>)
            RangeFlags = Range.Flags;
          if (!dxbc::isValidDescriptorRangeFlags(RangeFlags) ||
              !validDataFlags(RangeFlags) ||
              ((RangeFlags & 1) && (RangeFlags & 0x10000)) ||
              (Range.RangeType == 3 && (RangeFlags & ~1u)))
            return invalid("invalid descriptor range flags");
          uint64_t Offset = Range.OffsetInDescriptorsFromTableStart;
          if (Offset == Unbounded)
            Offset = NextOffset;
          if (Offset >= Unbounded)
            return invalid(
                "APPEND offset follows an unbounded or overflowing range");
          RootBinding Binding{static_cast<ResourceClass>(Range.RangeType),
                              Range.RegisterSpace,
                              Range.BaseShaderRegister,
                              Range.NumDescriptors,
                              static_cast<uint32_t>(Offset),
                              RangeFlags};
          if (endRegister(Binding) > (uint64_t(1) << 32))
            return invalid("descriptor register range overflows");
          NextOffset = Range.NumDescriptors == Unbounded
                           ? Unbounded
                           : Offset + Range.NumDescriptors;
          if (NextOffset > Unbounded)
            return invalid("descriptor table offset overflows");
          for (const RootBinding &Other : Parameter.Bindings) {
            if ((Other.Class == ResourceClass::Sampler) !=
                (Binding.Class == ResourceClass::Sampler))
              return invalid("sampler and resource ranges share a table");
            uint64_t OtherEnd = Other.Count == Unbounded
                                    ? Unbounded
                                    : uint64_t(Other.TableOffset) + Other.Count;
            if (overlaps(Offset, NextOffset, Other.TableOffset, OtherEnd))
              return invalid("descriptor table offsets overlap");
          }
          Parameter.Bindings.push_back(Binding);
        }
        return Error::success();
      };
      if (Plan.Version == 1) {
        auto Ranges = Table->read<dxbc::RTS0::v1::DescriptorRange>();
        if (!Ranges)
          return Ranges.takeError();
        if (Error E = AddRanges(*Ranges))
          return E;
      } else {
        auto Ranges = Table->read<dxbc::RTS0::v2::DescriptorRange>();
        if (!Ranges)
          return Ranges.takeError();
        if (Error E = AddRanges(*Ranges))
          return E;
      }
      ++Cost;
    }
    if (Cost > 64)
      return invalid("root arguments exceed 64 DWORDs");
    for (auto [Index, Binding] : enumerate(Parameter.Bindings)) {
      for (const RootBinding &Other :
           ArrayRef(Parameter.Bindings).take_front(Index))
        if (bindingOverlaps(Binding, Other))
          return invalid("shader register bindings overlap");
      for (const RootParameter &OtherParameter : Plan.Parameters)
        if (visibilityOverlaps(Parameter.Visibility, OtherParameter.Visibility))
          for (const RootBinding &Other : OtherParameter.Bindings)
            if (bindingOverlaps(Binding, Other))
              return invalid("shader register bindings overlap");
    }
    Plan.Parameters.push_back(std::move(Parameter));
  }
  Plan.DWORDCost = static_cast<uint32_t>(Cost);
  for (const auto &Sampler : Signature.samplers()) {
    if (!dxbc::isValidShaderVisibility(Sampler.ShaderVisibility) ||
        !dxbc::isValidSamplerFilter(Sampler.Filter) ||
        !dxbc::isValidAddress(Sampler.AddressU) ||
        !dxbc::isValidAddress(Sampler.AddressV) ||
        !dxbc::isValidAddress(Sampler.AddressW) ||
        !dxbc::isValidComparisonFunc(Sampler.ComparisonFunc) ||
        !dxbc::isValidBorderColor(Sampler.BorderColor))
      return invalid("invalid static sampler enum");
    auto Visibility =
        static_cast<dxbc::ShaderVisibility>(Sampler.ShaderVisibility);
    RootBinding Binding{ResourceClass::Sampler, Sampler.RegisterSpace,
                        Sampler.ShaderRegister, 1};
    for (const RootParameter &Parameter : Plan.Parameters)
      if (visibilityOverlaps(Visibility, Parameter.Visibility))
        for (const RootBinding &Other : Parameter.Bindings)
          if (bindingOverlaps(Binding, Other))
            return invalid("static sampler overlaps a descriptor range");
    for (const auto &Other : Plan.StaticSamplers)
      if (Other.ShaderRegister == Sampler.ShaderRegister &&
          Other.RegisterSpace == Sampler.RegisterSpace &&
          visibilityOverlaps(Visibility, static_cast<dxbc::ShaderVisibility>(
                                             Other.ShaderVisibility)))
        return invalid("static sampler bindings overlap");
    Plan.StaticSamplers.push_back(Sampler);
  }
  return Plan;
}

Expected<RootBindingPlan> RootBindingPlan::read(StringRef Input) {
  if (!Input.starts_with("DXBC"))
    return parse(arrayRefFromStringRef(Input));
  auto Container =
      object::DXContainer::create(MemoryBufferRef(Input, "root signature"));
  if (!Container)
    return Container.takeError();
  std::optional<StringRef> Root;
  if (Container->getHeader().PartCount)
    for (const auto &Part : *Container)
      if (toStringRef(ArrayRef<uint8_t>(Part.Part.Name)) == "RTS0") {
        if (Root)
          return invalid("duplicate RTS0 parts");
        Root = Part.Data;
      }
  if (!Root)
    return invalid("container has no RTS0 part");
  return parse(arrayRefFromStringRef(*Root));
}

const RootBinding *
RootBindingPlan::findBinding(ResourceClass Class, uint32_t Space,
                             uint32_t Register,
                             dxbc::ShaderVisibility Stage) const {
  for (const RootParameter &Parameter : Parameters)
    if (Parameter.Visibility == dxbc::ShaderVisibility::All ||
        Parameter.Visibility == Stage)
      for (const RootBinding &Binding : Parameter.Bindings)
        if (Binding.Class == Class && Binding.Space == Space &&
            Binding.Register <= Register && Register < endRegister(Binding))
          return &Binding;
  return nullptr;
}

void RootBindingPlan::print(raw_ostream &OS) const {
  OS << "root-signature: version=1." << Version - 1 << " flags=" << Flags
     << " dwords=" << DWORDCost << " parameters=" << Parameters.size()
     << " static-samplers=" << StaticSamplers.size() << '\n';
  for (auto [Index, Parameter] : enumerate(Parameters)) {
    OS << "parameter[" << Index << "]: kind=" << uint32_t(Parameter.Kind)
       << " visibility=" << uint32_t(Parameter.Visibility)
       << " constants=" << Parameter.ConstantCount << '\n';
    for (const RootBinding &Binding : Parameter.Bindings)
      OS << "  " << className(Binding.Class) << ": space=" << Binding.Space
         << " register=" << Binding.Register << " count=" << Binding.Count
         << " offset=" << Binding.TableOffset << " flags=" << Binding.Flags
         << '\n';
  }
  for (auto [Index, Sampler] : enumerate(StaticSamplers))
    OS << "static-sampler[" << Index << "]: space=" << Sampler.RegisterSpace
       << " register=" << Sampler.ShaderRegister
       << " visibility=" << Sampler.ShaderVisibility
       << " filter=" << Sampler.Filter << " address=" << Sampler.AddressU << ','
       << Sampler.AddressV << ',' << Sampler.AddressW
       << " mip-bias=" << Sampler.MipLODBias
       << " anisotropy=" << Sampler.MaxAnisotropy
       << " comparison=" << Sampler.ComparisonFunc
       << " border=" << Sampler.BorderColor << " lod=" << Sampler.MinLOD << ','
       << Sampler.MaxLOD << '\n';
}

Expected<std::optional<RootBindingPlan>>
feme::dxil::getRootBindingPlan(const Function &F) {
  if (!F.getMetadata(getRootSignatureMDKind()))
    return std::optional<RootBindingPlan>();
  auto Bytes = getRootSignature(F);
  if (!Bytes)
    return invalid("malformed canonical root signature metadata");
  auto Plan = RootBindingPlan::read(toStringRef(*Bytes));
  if (!Plan)
    return Plan.takeError();
  return std::optional<RootBindingPlan>(std::move(*Plan));
}
