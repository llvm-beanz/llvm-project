//===- RootSignature.cpp - D3D root-signature binding plan ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#include "feme/Direct3D/RootSignature.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

#include <set>
#include <tuple>

namespace feme::direct3d {

llvm::Error validateRootSignaturePlan(const RootSignaturePlan &Plan) {
  std::set<std::tuple<RootDescriptorClass, uint32_t, uint32_t>> RootDescriptors;
  std::set<std::tuple<uint32_t, uint32_t>> RootConstantBindings;

  for (const RootParameter &Parameter : Plan.Parameters) {
    if (Parameter.Kind == RootParameterKind::Constants) {
      if (Parameter.Constants.Num32BitValues == 0 ||
          Parameter.Constants.Num32BitValues > 64)
        return llvm::createStringError(
            llvm::errc::invalid_argument,
            "root constants must contain 1-64 32-bit values");
      auto Key = std::make_tuple(Parameter.Constants.RegisterSpace,
                                 Parameter.Constants.ShaderRegister);
      if (!RootConstantBindings.insert(Key).second)
        return llvm::createStringError(
            llvm::errc::invalid_argument,
            "duplicate root constant binding at register/space");
    } else if (Parameter.Kind == RootParameterKind::Descriptor) {
      auto Key = std::make_tuple(Parameter.Descriptor.Class,
                                 Parameter.Descriptor.RegisterSpace,
                                 Parameter.Descriptor.ShaderRegister);
      if (!RootDescriptors.insert(Key).second)
        return llvm::createStringError(
            llvm::errc::invalid_argument,
            "duplicate root descriptor binding at register/space");
    } else {
      std::set<std::tuple<RootDescriptorClass, uint32_t, uint32_t>>
          TableRegisters;
      for (const DescriptorRange &Range : Parameter.Table.Ranges) {
        if (Range.NumDescriptors == 0)
          return llvm::createStringError(
              llvm::errc::invalid_argument,
              "descriptor range must contain at least one descriptor");
        uint64_t End = static_cast<uint64_t>(Range.BaseShaderRegister) +
                       static_cast<uint64_t>(Range.NumDescriptors);
        if (End > UINT32_MAX)
          return llvm::createStringError(
              llvm::errc::invalid_argument,
              "descriptor range register arithmetic overflow");
        for (uint64_t Register = Range.BaseShaderRegister; Register < End;
             ++Register) {
          auto Key = std::make_tuple(Range.Class, Range.RegisterSpace,
                                     static_cast<uint32_t>(Register));
          if (!TableRegisters.insert(Key).second)
            return llvm::createStringError(
                llvm::errc::invalid_argument,
                "overlapping descriptors in descriptor table");
        }
      }
    }
  }

  std::set<std::tuple<uint32_t, uint32_t>> StaticSamplerBindings;
  for (const StaticSampler &Sampler : Plan.StaticSamplers) {
    auto Key = std::make_tuple(Sampler.RegisterSpace, Sampler.ShaderRegister);
    if (!StaticSamplerBindings.insert(Key).second)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "duplicate static sampler binding at register/space");
  }
  return llvm::Error::success();
}

std::optional<uint32_t> resolveDescriptorIndex(const DescriptorTable &Table,
                                               RootDescriptorClass Class,
                                               uint32_t RegisterSpace,
                                               uint32_t ShaderRegister) {
  for (const DescriptorRange &Range : Table.Ranges) {
    if (Range.Class != Class || Range.RegisterSpace != RegisterSpace)
      continue;
    uint64_t End = static_cast<uint64_t>(Range.BaseShaderRegister) +
                   static_cast<uint64_t>(Range.NumDescriptors);
    if (ShaderRegister >= Range.BaseShaderRegister &&
        static_cast<uint64_t>(ShaderRegister) < End) {
      uint64_t Offset = Range.OffsetFromTableStart == static_cast<uint32_t>(-1)
                            ? 0
                            : Range.OffsetFromTableStart;
      uint64_t Index = Offset + (ShaderRegister - Range.BaseShaderRegister);
      if (Index <= UINT32_MAX)
        return static_cast<uint32_t>(Index);
    }
  }
  return std::nullopt;
}

} // namespace feme::direct3d
