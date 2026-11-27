//===- WarpShaderCompilerTest.cpp - Compiler bridge lifecycle tests
//--------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "..\..\utils\windows-adapter-bootstrap\WarpShaderCompilerABI.h"

#include <cstdio>
#include <cstring>
#include <vector>

bool testWarpCompilerDDIs(const std::vector<std::uint32_t> &Tokens);

int main(int Argc, char **Argv) {
  if (Argc < 2 || Argc > 3)
    return 1;
  FILE *Input = nullptr;
  if (fopen_s(&Input, Argv[1], "rb") || !Input) {
    std::fprintf(stderr, "Cannot open shader fixture\n");
    return 1;
  }
  std::vector<std::uint32_t> Tokens;
  std::uint32_t Token;
  while (std::fread(&Token, sizeof(Token), 1, Input) == 1)
    Tokens.push_back(Token);
  bool ReadFailed = std::ferror(Input);
  std::fclose(Input);
  if (ReadFailed || Tokens.size() < 3) {
    std::fprintf(stderr, "Cannot read shader fixture\n");
    return 1;
  }
  if (Argc == 3 && std::strcmp(Argv[2], "--ddi") == 0)
    return testWarpCompilerDDIs(Tokens) ? 0 : 1;
  bool Reject = Argc == 3;
  if (Reject) {
    if (std::strcmp(Argv[2], "--bad-length") == 0)
      ++Tokens[1];
    else if (std::strcmp(Argv[2], "--bad-opcode") == 0)
      Tokens.back() = 0x010007ff;
    else
      return 1;
  }
  for (unsigned Iteration = 0; Iteration != 2; ++Iteration) {
    void *Compiled = nullptr;
    FeMeWarpCompiledShaderInfo Info;
    std::int32_t Result = FeMeWarpCompileRawDXBC(
        Tokens.data(), static_cast<std::uint32_t>(Tokens.size()), &Compiled,
        &Info);
    if (Reject) {
      if (Result >= 0 || Compiled || Info.HasNativeEntry) {
        std::fprintf(stderr, "Rejected shader acquired an executable object\n");
        return 1;
      }
      continue;
    }
    if (Result || !Compiled || Info.BridgeVersion != 1 ||
        !Info.ArtifactVersion || Info.WaveSize != 4 || !Info.HasNativeEntry) {
      std::fprintf(stderr, "No valid owned native shader: 0x%08x\n",
                   static_cast<unsigned>(Result));
      return 1;
    }
    std::printf("native stage=%u wave=%u artifact=%u\n", Info.Stage,
                Info.WaveSize, Info.ArtifactVersion);
    void *Pipeline = nullptr;
    FeMeWarpVertexPipelineDesc Desc;
    Result = FeMeWarpCreateVertexPipeline(Compiled, &Desc, &Pipeline);
    if (Info.Stage == 0) {
      if (Result || !Pipeline)
        return 1;
    } else if (Result >= 0 || Pipeline)
      return 1;
    if (Info.Stage == 5) {
      Result = FeMeWarpCreateComputePipeline(Compiled, &Pipeline);
      if (Result || !Pipeline)
        return 1;
    }
    FeMeWarpDestroyCompiledShader(Compiled);
    // The shared pipeline retains its native stage after the DDI handle dies.
    if (Pipeline) {
      if (Info.Stage == 0)
        FeMeWarpDestroyVertexPipeline(Pipeline);
      else
        FeMeWarpDestroyComputePipeline(Pipeline);
    }
  }
  std::puts(Reject ? "rejected without object" : "compiled and released twice");
  return 0;
}
