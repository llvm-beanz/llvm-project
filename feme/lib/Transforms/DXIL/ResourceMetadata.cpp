//===- ResourceMetadata.cpp - Read DXIL's dx.resources metadata ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ResourceMetadata.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::dxil;

namespace {

/// Reads \p MD as a constant integer, or returns `std::nullopt` if it isn't
/// one (i.e. the metadata isn't shaped the way DXIL's writer produces).
std::optional<uint64_t> getMDInt(const Metadata *MD) {
  const auto *CAM = dyn_cast_or_null<ConstantAsMetadata>(MD);
  if (!CAM)
    return std::nullopt;
  const auto *CI = dyn_cast<ConstantInt>(CAM->getValue());
  if (!CI)
    return std::nullopt;
  return CI->getZExtValue();
}

/// Applies the tag/value pairs of a resource's extended-properties metadata
/// node to \p Binding. See `llvm::dxil::ExtPropTags` for the tag values.
void applyExtProps(const MDNode *Props, ResourceBinding &Binding) {
  if (!Props)
    return;
  for (unsigned I = 0, E = Props->getNumOperands(); I + 1 < E; I += 2) {
    std::optional<uint64_t> Tag = getMDInt(Props->getOperand(I));
    std::optional<uint64_t> Value = getMDInt(Props->getOperand(I + 1));
    if (!Tag || !Value)
      continue;
    switch (static_cast<llvm::dxil::ExtPropTags>(*Tag)) {
    case llvm::dxil::ExtPropTags::ElementType:
      Binding.ElementType = static_cast<llvm::dxil::ElementType>(*Value);
      break;
    case llvm::dxil::ExtPropTags::StructuredBufferStride:
      Binding.StrideOrSize = static_cast<uint32_t>(*Value);
      break;
    default:
      break;
    }
  }
}

/// Reads one entry of a `!dx.resources` class list. Each class has its own
/// operand count and layout, all sharing the leading
/// `{ID, GV, name, space, lowerBound, rangeSize, kind-ish}` prefix; see
/// `DxilMDHelper::EmitDxil*` in the DirectX Shader Compiler.
std::optional<std::pair<uint32_t, ResourceBinding>>
readEntry(const MDNode *Entry, llvm::dxil::ResourceClass Class) {
  static constexpr unsigned SRVOperands = 9;
  static constexpr unsigned UAVOperands = 11;
  static constexpr unsigned CBufferOrSamplerOperands = 8;

  unsigned Expected = 0;
  switch (Class) {
  case llvm::dxil::ResourceClass::SRV:
    Expected = SRVOperands;
    break;
  case llvm::dxil::ResourceClass::UAV:
    Expected = UAVOperands;
    break;
  case llvm::dxil::ResourceClass::CBuffer:
  case llvm::dxil::ResourceClass::Sampler:
    Expected = CBufferOrSamplerOperands;
    break;
  }
  if (!Entry || Entry->getNumOperands() != Expected)
    return std::nullopt;

  std::optional<uint64_t> ID = getMDInt(Entry->getOperand(0));
  std::optional<uint64_t> Space = getMDInt(Entry->getOperand(3));
  std::optional<uint64_t> LowerBound = getMDInt(Entry->getOperand(4));
  std::optional<uint64_t> RangeSize = getMDInt(Entry->getOperand(5));
  std::optional<uint64_t> KindOrSize = getMDInt(Entry->getOperand(6));
  if (!ID || !Space || !LowerBound || !RangeSize || !KindOrSize)
    return std::nullopt;

  ResourceBinding Binding;
  Binding.Class = Class;
  Binding.Space = static_cast<uint32_t>(*Space);
  Binding.LowerBound = static_cast<uint32_t>(*LowerBound);
  Binding.RangeSize = *RangeSize == std::numeric_limits<uint32_t>::max()
                          ? 0
                          : static_cast<uint32_t>(*RangeSize);

  const MDNode *ExtProps = nullptr;
  switch (Class) {
  case llvm::dxil::ResourceClass::SRV:
    Binding.Kind = static_cast<llvm::dxil::ResourceKind>(*KindOrSize);
    ExtProps = dyn_cast_or_null<MDNode>(Entry->getOperand(8).get());
    break;
  case llvm::dxil::ResourceClass::UAV: {
    Binding.Kind = static_cast<llvm::dxil::ResourceKind>(*KindOrSize);
    std::optional<uint64_t> IsROV = getMDInt(Entry->getOperand(9));
    Binding.IsROV = IsROV.value_or(0) != 0;
    ExtProps = dyn_cast_or_null<MDNode>(Entry->getOperand(10).get());
    break;
  }
  case llvm::dxil::ResourceClass::CBuffer:
    // A cbuffer's seventh operand is its size in bytes, not a resource kind.
    Binding.Kind = llvm::dxil::ResourceKind::CBuffer;
    Binding.StrideOrSize = static_cast<uint32_t>(*KindOrSize);
    ExtProps = dyn_cast_or_null<MDNode>(Entry->getOperand(7).get());
    break;
  case llvm::dxil::ResourceClass::Sampler:
    Binding.Kind = llvm::dxil::ResourceKind::Sampler;
    ExtProps = dyn_cast_or_null<MDNode>(Entry->getOperand(7).get());
    break;
  }
  applyExtProps(ExtProps, Binding);

  return std::make_pair(static_cast<uint32_t>(*ID), Binding);
}

uint64_t makeKey(llvm::dxil::ResourceClass Class, uint32_t RangeID) {
  return (static_cast<uint64_t>(Class) << 32) | RangeID;
}

} // namespace

ResourceMetadata ResourceMetadata::read(const Module &M) {
  ResourceMetadata Result;
  const NamedMDNode *ResourcesNode = M.getNamedMetadata("dx.resources");
  if (!ResourcesNode || ResourcesNode->getNumOperands() != 1)
    return Result;
  const MDNode *Resources = ResourcesNode->getOperand(0);
  if (Resources->getNumOperands() != 4)
    return Result;

  static constexpr llvm::dxil::ResourceClass Classes[] = {
      llvm::dxil::ResourceClass::SRV, llvm::dxil::ResourceClass::UAV,
      llvm::dxil::ResourceClass::CBuffer, llvm::dxil::ResourceClass::Sampler};
  for (unsigned I = 0; I != 4; ++I) {
    const auto *List = dyn_cast_or_null<MDNode>(Resources->getOperand(I).get());
    if (!List)
      continue;
    for (const MDOperand &Op : List->operands()) {
      auto Entry = readEntry(dyn_cast_or_null<MDNode>(Op.get()), Classes[I]);
      if (Entry)
        Result.Bindings[makeKey(Classes[I], Entry->first)] = Entry->second;
    }
  }
  return Result;
}

std::optional<ResourceBinding>
ResourceMetadata::lookup(llvm::dxil::ResourceClass Class,
                         uint32_t RangeID) const {
  auto It = Bindings.find(makeKey(Class, RangeID));
  if (It == Bindings.end())
    return std::nullopt;
  return It->second;
}
