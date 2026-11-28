//===- WarpShaderCompilerABI.h - Owned compiler bridge ------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPSHADERCOMPILERABI_H
#define FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPSHADERCOMPILERABI_H

#include <cmath>
#include <cstdint>

inline constexpr std::uint32_t FeMeWarpCompilerBridgeVersion = 5;
inline constexpr std::uint32_t FeMeWarpMaxImageMipLevels = 15;
/// D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 1, 2, 3), without SDK headers.
inline constexpr std::uint32_t FeMeWarpDefaultComponentMapping = 0x1688;

constexpr bool FeMeWarpValidComponentMapping(std::uint32_t Mapping) {
  if (!(Mapping & 0x1000) || (Mapping & ~0x1fffu))
    return false;
  for (unsigned I = 0; I != 4; ++I)
    if (((Mapping >> (I * 3)) & 7) > 5)
      return false;
  return true;
}

#ifdef FEME_WARP_COMPILER_EXPORTS
#define FEME_WARP_COMPILER_API __declspec(dllexport)
#else
#define FEME_WARP_COMPILER_API __declspec(dllimport)
#endif

/// POD boundary between the standalone DDI prototype and the shared compiler.
/// The compiler owns and releases its opaque CompiledStage, including its JIT.
struct FeMeWarpCompiledShaderInfo {
  std::uint32_t Size = sizeof(FeMeWarpCompiledShaderInfo);
  std::uint32_t BridgeVersion = FeMeWarpCompilerBridgeVersion;
  std::uint32_t ArtifactVersion = 0;
  std::uint32_t Stage = 0;
  std::uint32_t WaveSize = 0;
  std::uint32_t HasNativeEntry = 0;
};

/// Normalized vertex-only pipeline with no attachment or fragment stage.
struct FeMeWarpVertexPipelineDesc {
  std::uint32_t Size = sizeof(FeMeWarpVertexPipelineDesc);
  std::uint32_t Topology = 2; // Point, line, triangle lists.
  std::uint32_t Cull = 0;     // None, front, back.
  std::uint32_t FrontCounterClockwise = 0;
  std::uint32_t Wireframe = 0;
  std::uint32_t DepthClamp = 0;
  std::uint32_t SampleMask = 0xffffffffu;
};

/// Borrowed buffer storage; namespaces 1/2/3 are SRV/UAV/CBV.
/// Kind 0 is raw, kind 1 is structured and kind 2 is typed.
/// Storage must outlive dispatch. Format is a DXGI format for typed buffers,
/// not a FeMe runtime format, and must be zero for other kinds.
/// Register identifies an individual descriptor, including array elements.
/// Counter storage is an optional aligned four-byte extent for structured UAVs.
/// Non-default D3D component mappings are supported only for typed SRVs.
struct FeMeWarpBufferBinding {
  void *Data = nullptr;
  std::uint64_t SizeInBytes = 0;
  std::uint32_t Space = 0;
  std::uint32_t Register = 0;
  std::uint32_t Namespace = 2;
  std::uint32_t Kind = 0;
  std::uint32_t Stride = 0;
  std::uint32_t Format = 0;
  void *CounterData = nullptr;
  std::uint64_t CounterSizeInBytes = 0;
  std::uint32_t Shader4ComponentMapping = FeMeWarpDefaultComponentMapping;
};

/// Values match the corresponding D3D12 SRV dimensions without SDK types.
enum class FeMeWarpImageDimension : std::uint32_t {
  Texture1D = 2,
  Texture1DArray = 3,
  Texture2D = 4,
  Texture2DArray = 5,
  Texture3D = 8,
  TextureCube = 9,
  TextureCubeArray = 10,
};

/// Checked byte geometry within the image binding's borrowed storage.
struct FeMeWarpImageMipLayout {
  std::uint64_t Offset = 0;
  std::uint64_t RowPitch = 0;
  std::uint64_t SlicePitch = 0;
};

/// Borrowed single-plane/single-sample image view, dense by mip level.
/// Namespace 1/2 is SRV/UAV; Format and mapping use Direct3D values.
/// Register identifies an individual array descriptor. Storage and layouts
/// must outlive dispatch. Null images have zero extents/mips and no layouts.
struct FeMeWarpImageBinding {
  void *Data = nullptr;
  std::uint64_t SizeInBytes = 0;
  std::uint32_t Space = 0;
  std::uint32_t Register = 0;
  std::uint32_t Namespace = 1;
  FeMeWarpImageDimension Dimension = FeMeWarpImageDimension::Texture2D;
  std::uint32_t Format = 0;
  std::uint32_t Width = 0;
  std::uint32_t Height = 0;
  std::uint32_t Depth = 0;
  std::uint32_t MipLevels = 0;
  std::uint32_t ArrayLayers = 1;
  std::uint32_t PlaneCount = 1;
  std::uint32_t SampleCount = 1;
  std::uint32_t MipLayoutCount = 0;
  const FeMeWarpImageMipLayout *MipLayouts = nullptr;
  std::uint32_t Shader4ComponentMapping = FeMeWarpDefaultComponentMapping;
};

/// Direct3D sampler state without SDK types, identified by space/register.
/// Filter, addressing and comparison values use their D3D12 encodings.
struct FeMeWarpSamplerBinding {
  std::uint32_t Space = 0;
  std::uint32_t Register = 0;
  std::uint32_t Filter = 0;
  std::uint32_t AddressU = 1;
  std::uint32_t AddressV = 1;
  std::uint32_t AddressW = 1;
  float MipLODBias = 0;
  std::uint32_t MaxAnisotropy = 1;
  std::uint32_t ComparisonFunc = 8;
  float BorderColor[4] = {};
  float MinLOD = 0;
  float MaxLOD = 3.402823466e38f;
};

/// Validate canonical sampler state shared by the native DDI and private
/// bridge.
inline bool FeMeWarpValidSampler(const FeMeWarpSamplerBinding &Sampler) {
  if ((Sampler.Filter & ~0x1d5u) ||
      ((Sampler.Filter & 0x40) && (Sampler.Filter & 0x15) != 0x15) ||
      Sampler.AddressU < 1 || Sampler.AddressU > 5 || Sampler.AddressV < 1 ||
      Sampler.AddressV > 5 || Sampler.AddressW < 1 || Sampler.AddressW > 5 ||
      Sampler.ComparisonFunc < 1 || Sampler.ComparisonFunc > 8 ||
      !Sampler.MaxAnisotropy || Sampler.MaxAnisotropy > 16 ||
      !std::isfinite(Sampler.MipLODBias) || Sampler.MipLODBias < -16 ||
      Sampler.MipLODBias > 15.99f || !std::isfinite(Sampler.MinLOD) ||
      !std::isfinite(Sampler.MaxLOD) || Sampler.MinLOD > Sampler.MaxLOD)
    return false;
  for (float Channel : Sampler.BorderColor)
    if (!std::isfinite(Channel))
      return false;
  return true;
}

struct FeMeWarpDispatchDesc {
  std::uint32_t Size = sizeof(FeMeWarpDispatchDesc);
  std::uint32_t Version = FeMeWarpCompilerBridgeVersion;
  std::uint32_t GroupCount[3] = {1, 1, 1};
  std::uint32_t BufferCount = 0;
  const FeMeWarpBufferBinding *Buffers = nullptr;
  std::uint32_t ImageCount = 0;
  const FeMeWarpImageBinding *Images = nullptr;
  std::uint32_t SamplerCount = 0;
  const FeMeWarpSamplerBinding *Samplers = nullptr;
};

extern "C" {
FEME_WARP_COMPILER_API const char *FeMeWarpCompilerRevision();
FEME_WARP_COMPILER_API std::int32_t
FeMeWarpCompileRawDXBC(const std::uint32_t *Tokens, std::uint32_t Count,
                       void **Compiled, FeMeWarpCompiledShaderInfo *Info);
FEME_WARP_COMPILER_API std::int32_t
FeMeWarpCompileDXILProgram(const std::uint32_t *Program, std::uint32_t Count,
                           void **Compiled, FeMeWarpCompiledShaderInfo *Info);
FEME_WARP_COMPILER_API void FeMeWarpDestroyCompiledShader(void *Compiled);
FEME_WARP_COMPILER_API std::int32_t
FeMeWarpCreateVertexPipeline(void *CompiledShader,
                             const FeMeWarpVertexPipelineDesc *Desc,
                             void **Pipeline);
FEME_WARP_COMPILER_API void FeMeWarpDestroyVertexPipeline(void *Pipeline);
FEME_WARP_COMPILER_API std::int32_t
FeMeWarpCreateComputePipeline(void *CompiledShader, void **Pipeline);
FEME_WARP_COMPILER_API void FeMeWarpDestroyComputePipeline(void *Pipeline);
FEME_WARP_COMPILER_API std::int32_t
FeMeWarpRetainComputePipeline(void *Pipeline, void **Retained);
FEME_WARP_COMPILER_API std::int32_t
FeMeWarpDispatchCompute(void *Pipeline, const FeMeWarpDispatchDesc *Desc);
}

#undef FEME_WARP_COMPILER_API

#endif
