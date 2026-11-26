//===- RootSignature.h - D3D root-signature binding plan -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIRECT3D_ROOTSIGNATURE_H
#define FEME_DIRECT3D_ROOTSIGNATURE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace feme::direct3d {

enum class ShaderVisibility : uint32_t {
  All = 0,
  Vertex = 1,
  Hull = 2,
  Domain = 3,
  Geometry = 4,
  Pixel = 5,
  Amplification = 6,
  Mesh = 7,
};

enum class RootDescriptorClass : uint32_t {
  CBV = 0,
  SRV = 1,
  UAV = 2,
  Sampler = 3,
};

enum class RootParameterKind : uint32_t {
  Constants = 0,
  Descriptor = 1,
  DescriptorTable = 2,
};

struct RootConstants {
  uint32_t ShaderRegister = 0;
  uint32_t RegisterSpace = 0;
  uint32_t Num32BitValues = 0;
  ShaderVisibility Visibility = ShaderVisibility::All;
};

struct RootDescriptor {
  RootDescriptorClass Class = RootDescriptorClass::CBV;
  uint32_t ShaderRegister = 0;
  uint32_t RegisterSpace = 0;
  ShaderVisibility Visibility = ShaderVisibility::All;
};

struct DescriptorRange {
  RootDescriptorClass Class = RootDescriptorClass::CBV;
  uint32_t BaseShaderRegister = 0;
  uint32_t NumDescriptors = 0;
  uint32_t RegisterSpace = 0;
  uint32_t OffsetFromTableStart = static_cast<uint32_t>(-1);
  ShaderVisibility Visibility = ShaderVisibility::All;
};

struct DescriptorTable {
  std::vector<DescriptorRange> Ranges;
};

struct RootParameter {
  RootParameterKind Kind = RootParameterKind::Descriptor;
  RootConstants Constants;
  RootDescriptor Descriptor;
  DescriptorTable Table;
};

struct StaticSampler {
  uint32_t ShaderRegister = 0;
  uint32_t RegisterSpace = 0;
  ShaderVisibility Visibility = ShaderVisibility::All;
};

struct RootSignaturePlan {
  std::vector<RootParameter> Parameters;
  std::vector<StaticSampler> StaticSamplers;
};

llvm::Error validateRootSignaturePlan(const RootSignaturePlan &Plan);

std::optional<uint32_t> resolveDescriptorIndex(const DescriptorTable &Table,
                                               RootDescriptorClass Class,
                                               uint32_t RegisterSpace,
                                               uint32_t ShaderRegister);

} // namespace feme::direct3d

#endif // FEME_DIRECT3D_ROOTSIGNATURE_H
