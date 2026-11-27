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

#include <cstdint>

#ifdef FEME_WARP_COMPILER_EXPORTS
#define FEME_WARP_COMPILER_API __declspec(dllexport)
#else
#define FEME_WARP_COMPILER_API __declspec(dllimport)
#endif

/// POD boundary between the standalone DDI prototype and the shared compiler.
/// The compiler owns and releases its opaque CompiledStage, including its JIT.
struct FeMeWarpCompiledShaderInfo {
  std::uint32_t Size = sizeof(FeMeWarpCompiledShaderInfo);
  std::uint32_t BridgeVersion = 1;
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

/// Borrowed buffer storage; namespace 1 is SRV and 2 is UAV.
/// Kind 0 is raw and kind 1 is structured. Storage must outlive dispatch.
struct FeMeWarpBufferBinding {
  void *Data = nullptr;
  std::uint64_t SizeInBytes = 0;
  std::uint32_t Space = 0;
  std::uint32_t Register = 0;
  std::uint32_t Namespace = 2;
  std::uint32_t Kind = 0;
  std::uint32_t Stride = 0;
};

struct FeMeWarpDispatchDesc {
  std::uint32_t Size = sizeof(FeMeWarpDispatchDesc);
  std::uint32_t Version = 1;
  std::uint32_t GroupCount[3] = {1, 1, 1};
  std::uint32_t BufferCount = 0;
  const FeMeWarpBufferBinding *Buffers = nullptr;
};

extern "C" {
FEME_WARP_COMPILER_API const char *FeMeWarpCompilerRevision();
FEME_WARP_COMPILER_API std::int32_t
FeMeWarpCompileRawDXBC(const std::uint32_t *Tokens, std::uint32_t Count,
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
