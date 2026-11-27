//===- WarpShaderCompilerTest.cpp - Compiler bridge lifecycle tests
//--------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "..\..\utils\windows-adapter-bootstrap\WarpShaderCompilerABI.h"

#include "llvm/Object/DXContainer.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

bool testWarpCompilerDDIs(const std::vector<std::uint32_t> &Tokens);

static bool testDispatch(const std::vector<std::uint32_t> &Tokens,
                         bool DXIL = false) {
  void *Shader = nullptr, *Pipeline = nullptr;
  FeMeWarpCompiledShaderInfo Info;
  auto Compile = DXIL ? FeMeWarpCompileDXILProgram : FeMeWarpCompileRawDXBC;
  if (Compile(Tokens.data(), static_cast<std::uint32_t>(Tokens.size()), &Shader,
              &Info) ||
      FeMeWarpCreateComputePipeline(Shader, &Pipeline))
    return false;
  FeMeWarpDestroyCompiledShader(Shader);
  std::vector<std::uint32_t> Words(196, 0x5a5a5a5a);
  for (unsigned I = 0; I != 180; ++I)
    Words[I] = I;
  FeMeWarpBufferBinding Buffer;
  Buffer.Data = Words.data();
  Buffer.SizeInBytes = 180 * sizeof(Words[0]);
  FeMeWarpDispatchDesc Desc;
  Desc.GroupCount[0] = 3;
  Desc.GroupCount[1] = 2;
  Desc.BufferCount = 1;
  Desc.Buffers = &Buffer;
  bool Good = true;
  auto Bad = Desc;
  Bad.Version = 2;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Bad) < 0;
  Bad = Desc;
  Bad.BufferCount = 0;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Bad) < 0;
  Bad = Desc;
  Bad.GroupCount[0] = 65536;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Bad) < 0;
  Bad = Desc;
  Bad.GroupCount[0] = 0;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Bad) == 0;
  for (unsigned I = 0; I != 180; ++I)
    Good &= Words[I] == I;
  for (unsigned Repeat = 1; Repeat != 4 && Good; ++Repeat) {
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) == 0;
    for (unsigned Z = 0; Z != 2; ++Z)
      for (unsigned Y = 0; Y != 6; ++Y)
        for (unsigned X = 0; X != 15; ++X) {
          unsigned Index = (Z * 6 + Y) * 15 + X;
          unsigned Local = (Z * 3 + Y % 3) * 5 + X % 5;
          unsigned Peer = (Local + 1) % 30;
          unsigned PeerIndex =
              ((Peer / 15) * 6 + (Y / 3) * 3 + (Peer / 5) % 3) * 15 +
              (X / 5) * 5 + Peer % 5;
          Good &= Words[Index] == Index + Repeat * (PeerIndex + 23);
        }
    for (unsigned I = 180; I != Words.size(); ++I)
      Good &= Words[I] == 0x5a5a5a5a;
  }
  FeMeWarpDestroyComputePipeline(Pipeline);
  return Good;
}

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
  bool DXIL = Argc == 3 && std::strncmp(Argv[2], "--dxil", 6) == 0;
  if (DXIL) {
    llvm::StringRef Bytes(reinterpret_cast<const char *>(Tokens.data()),
                          Tokens.size() * sizeof(Tokens[0]));
    auto Container = llvm::object::DXContainer::create(
        llvm::MemoryBufferRef(Bytes, "original DXC fixture"));
    if (!Container) {
      llvm::logAllUnhandledErrors(Container.takeError(), llvm::errs(),
                                  "Cannot read DXIL fixture: ");
      return 1;
    }
    std::vector<std::uint32_t> Program;
    for (const auto &Part : *Container) {
      if (std::memcmp(Part.Part.Name, "DXIL", 4))
        continue;
      if (Part.Data.size() % sizeof(Token))
        return 1;
      Program.resize(Part.Data.size() / sizeof(Token));
      std::memcpy(Program.data(), Part.Data.data(), Part.Data.size());
      break;
    }
    if (Program.size() < 6) {
      std::fprintf(stderr, "Missing DXIL program part\n");
      return 1;
    }
    Tokens = std::move(Program);
  }
  if (Argc == 3 && (std::strcmp(Argv[2], "--ddi") == 0 ||
                    std::strcmp(Argv[2], "--dxil-ddi") == 0))
    return testWarpCompilerDDIs(Tokens) ? 0 : 1;
  if (Argc == 3 && (std::strcmp(Argv[2], "--dispatch") == 0 ||
                    std::strcmp(Argv[2], "--dxil-dispatch") == 0)) {
    bool Good[2]{};
    std::thread First([&] { Good[0] = testDispatch(Tokens, DXIL); });
    std::thread Second([&] { Good[1] = testDispatch(Tokens, DXIL); });
    First.join();
    Second.join();
    bool Passed = Good[0] && Good[1];
    std::puts(Passed ? "native grouped dispatch and concurrent PSOs PASS"
                     : "native grouped dispatch and concurrent PSOs FAIL");
    return Passed ? 0 : 1;
  }
  bool Reject = Argc == 3 && std::strcmp(Argv[2], "--dxil") != 0;
  if (Reject) {
    if (std::strcmp(Argv[2], "--bad-length") == 0 ||
        std::strcmp(Argv[2], "--dxil-bad-length") == 0)
      ++Tokens[1];
    else if (std::strcmp(Argv[2], "--bad-opcode") == 0)
      Tokens.back() = 0x010007ff;
    else if (std::strcmp(Argv[2], "--dxil-bad-offset") == 0)
      Tokens[4] = 0xffffffff;
    else if (std::strcmp(Argv[2], "--dxil-bad-size") == 0)
      Tokens[5] = 0xffffffff;
    else if (std::strcmp(Argv[2], "--dxil-bad-kind") == 0)
      Tokens[0] = (Tokens[0] & 0xffff) | (1u << 16);
    else if (std::strcmp(Argv[2], "--dxil-wave-reject") != 0)
      return 1;
  }
  for (unsigned Iteration = 0; Iteration != 2; ++Iteration) {
    void *Compiled = nullptr;
    FeMeWarpCompiledShaderInfo Info;
    auto Compile = DXIL ? FeMeWarpCompileDXILProgram : FeMeWarpCompileRawDXBC;
    std::int32_t Result =
        Compile(Tokens.data(), static_cast<std::uint32_t>(Tokens.size()),
                &Compiled, &Info);
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
    if (DXIL && Info.Stage == 5) {
      FeMeWarpDispatchDesc Dispatch;
      Dispatch.GroupCount[0] = 2;
      Dispatch.GroupCount[1] = 3;
      if (FeMeWarpDispatchCompute(Pipeline, &Dispatch) ||
          FeMeWarpDispatchCompute(Pipeline, &Dispatch))
        return 1;
    }
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
