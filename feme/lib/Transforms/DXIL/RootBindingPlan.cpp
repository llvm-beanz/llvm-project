//===- RootBindingPlan.cpp - Checked DirectX root layouts ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/DXIL/RootBindingPlan.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/bit.h"
#include "llvm/Analysis/DXILResource.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
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

Expected<std::optional<StringRef>> getContainerRootSignature(StringRef Input) {
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
  return Root;
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
  auto Root = getContainerRootSignature(Input);
  if (!Root)
    return Root.takeError();
  if (!*Root)
    return invalid("container has no RTS0 part");
  return parse(arrayRefFromStringRef(**Root));
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

Error RootBindingPlan::checkBindingCoverage(
    ResourceClass Class, uint32_t Space, uint32_t Register, uint32_t Count,
    dxbc::ShaderVisibility Stage) const {
  if (uint32_t(Class) > uint32_t(ResourceClass::LastEntry) ||
      !dxbc::isValidShaderVisibility(uint32_t(Stage)) || !Count)
    return invalid("invalid shader binding class, visibility or empty range");
  uint64_t RequiredEnd =
      Count == Unbounded ? uint64_t(1) << 32 : uint64_t(Register) + Count;
  if (RequiredEnd > (uint64_t(1) << 32))
    return invalid("shader binding register range overflows");
  uint64_t Cursor = Register;
  for (const RootParameter &Parameter : Parameters) {
    if (Parameter.Visibility != dxbc::ShaderVisibility::All &&
        Parameter.Visibility != Stage)
      continue;
    for (const RootBinding &Binding : Parameter.Bindings)
      if (Binding.Class == Class && Binding.Space == Space &&
          Binding.Register <= Register && Register < endRegister(Binding)) {
        uint64_t End = endRegister(Binding);
        if (End >= RequiredEnd)
          return Error::success();
        Cursor = std::max(Cursor, End);
      }
  }
  if (Class == ResourceClass::Sampler)
    for (const auto &Sampler : StaticSamplers)
      if (Sampler.RegisterSpace == Space &&
          (Sampler.ShaderVisibility == uint32_t(dxbc::ShaderVisibility::All) ||
           Sampler.ShaderVisibility == uint32_t(Stage)) &&
          Register == Sampler.ShaderRegister) {
        if (Count == 1)
          return Error::success();
        Cursor = std::max(Cursor, uint64_t(Sampler.ShaderRegister) + 1);
      }
  return createStringError(
      inconvertibleErrorCode(),
      "root binding mismatch: %s space=%u register=%u count=%u is not covered "
      "at register=%llu for visibility=%u (one root range must cover the "
      "whole declaration)",
      className(Class).data(), Space, Register, Count,
      static_cast<unsigned long long>(Cursor), uint32_t(Stage));
}

Error RootBindingPlan::checkShaderBindings(const Function &Entry,
                                           dxbc::ShaderVisibility Stage) const {
  if (!dxbc::isValidShaderVisibility(uint32_t(Stage)))
    return invalid("invalid shader binding visibility");
  SmallVector<const Function *, 8> Worklist{&Entry};
  SmallPtrSet<const Function *, 8> Seen;
  while (!Worklist.empty()) {
    const Function *F = Worklist.pop_back_val();
    if (!Seen.insert(F).second)
      continue;
    for (const Instruction &I : instructions(F)) {
      const auto *Call = dyn_cast<CallBase>(&I);
      if (!Call)
        continue;
      const auto *Callee = dyn_cast<Function>(
          Call->getCalledOperand()->stripPointerCastsAndAliases());
      if (!Callee)
        return createStringError(
            inconvertibleErrorCode(),
            "cannot validate indirect call in root-signature binding mode "
            "in '%s'",
            F->getName().str().c_str());
      if (!Callee->isDeclaration())
        Worklist.push_back(Callee);
      if (Callee->getIntrinsicID() !=
              Intrinsic::dx_resource_handlefrombinding ||
          Call->use_empty())
        continue;
      auto *Ty = dyn_cast<TargetExtType>(Call->getType());
      if (!Ty || !isa<llvm::dxil::AnyResourceExtType>(Ty))
        return invalid("unsupported resource type in shader binding");
      ResourceClass Class;
      if (isa<llvm::dxil::CBufferExtType>(Ty)) {
        if (Ty->getNumTypeParameters() != 1)
          return invalid("malformed cbuffer shader binding type");
        Class = ResourceClass::CBuffer;
      } else if (isa<llvm::dxil::SamplerExtType>(Ty)) {
        if (!Ty->getNumIntParameters())
          return invalid("malformed sampler shader binding type");
        Class = ResourceClass::Sampler;
      } else if (isa<llvm::dxil::FeedbackTextureExtType>(Ty)) {
        if (Ty->getNumIntParameters() < 2)
          return invalid("malformed feedback shader binding type");
        Class = ResourceClass::UAV;
      } else {
        unsigned RequiredInts = isa<llvm::dxil::RawBufferExtType>(Ty)     ? 2
                                : isa<llvm::dxil::TypedBufferExtType>(Ty) ? 3
                                                                          : 4;
        if (Ty->getNumTypeParameters() != 1 ||
            Ty->getNumIntParameters() < RequiredInts ||
            Ty->getIntParameter(0) > 1)
          return invalid("malformed writable shader binding type");
        Class =
            Ty->getIntParameter(0) ? ResourceClass::UAV : ResourceClass::SRV;
      }
      if (Call->arg_size() < 3)
        return invalid("malformed shader binding operands");
      uint32_t Values[3];
      for (unsigned N = 0; N != 3; ++N) {
        const auto *Value = dyn_cast<ConstantInt>(Call->getArgOperand(N));
        if (!Value || !Value->getType()->isIntegerTy(32))
          return invalid("shader binding space, register and count must be "
                         "constant i32 values");
        Values[N] = Value->getZExtValue();
      }
      if (Error E = checkBindingCoverage(Class, Values[0], Values[1], Values[2],
                                         Stage))
        return createStringError(inconvertibleErrorCode(), "in '%s': %s",
                                 F->getName().str().c_str(),
                                 toString(std::move(E)).c_str());
    }
  }
  return Error::success();
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

Error feme::dxil::importContainerRootSignature(Module &M, StringRef Input) {
  if (!Input.starts_with("DXBC"))
    return Error::success();
  auto Root = getContainerRootSignature(Input);
  if (!Root)
    return Root.takeError();
  if (*Root)
    for (Function &F : M)
      if (feme::isShaderEntryPoint(F) &&
          !F.getMetadata(getRootSignatureMDKind()))
        setRootSignature(F, arrayRefFromStringRef(**Root));
  return Error::success();
}
