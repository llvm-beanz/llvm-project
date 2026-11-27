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

extern "C" {
FEME_WARP_COMPILER_API std::int32_t
FeMeWarpCompileRawDXBC(const std::uint32_t *Tokens, std::uint32_t Count,
                       void **Compiled, FeMeWarpCompiledShaderInfo *Info);
FEME_WARP_COMPILER_API void FeMeWarpDestroyCompiledShader(void *Compiled);
}

#undef FEME_WARP_COMPILER_API

#endif
