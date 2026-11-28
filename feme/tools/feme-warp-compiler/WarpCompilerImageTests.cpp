//===- WarpCompilerImageTests.cpp - Borrowed image/sampler execution tests
//--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "..\..\utils\windows-adapter-bootstrap\WarpShaderCompilerABI.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include <dxgiformat.h>

bool testWarpCompilerImages(const std::vector<std::uint32_t> &Tokens,
                            bool Sampling, bool NativeHeap) {
  void *Shader = nullptr, *Original = nullptr, *Pipeline = nullptr;
  FeMeWarpCompiledShaderInfo Info;
  if (FeMeWarpCompileDXILProgram(Tokens.data(),
                                 static_cast<std::uint32_t>(Tokens.size()),
                                 &Shader, &Info))
    return false;
  auto Result = FeMeWarpCreateComputePipeline(Shader, &Original);
  FeMeWarpDestroyCompiledShader(Shader);
  if (Result)
    return false;
  Result = FeMeWarpRetainComputePipeline(Original, &Pipeline);
  FeMeWarpDestroyComputePipeline(Original);
  if (Result)
    return false;

  std::array<std::uint32_t, 48> Output;
  Output.fill(0x5a5a5a5a);
  FeMeWarpBufferBinding Buffer;
  Buffer.Data = Output.data();
  Buffer.SizeInBytes = 32 * sizeof(Output[0]);
  FeMeWarpDispatchDesc Desc;
  Desc.BufferCount = 1;
  Desc.Buffers = &Buffer;
  std::array<FeMeWarpImageBinding, 4> Images{};
  std::array<std::array<FeMeWarpImageMipLayout, 2>, 4> Layouts{};
  std::array<std::vector<unsigned char>, 4> Storage;
  for (unsigned I = 0; I < Images.size(); ++I) {
    auto &Image = Images[I];
    Storage[I].resize(4096, 0x5a);
    Image.Data = Storage[I].data();
    Image.SizeInBytes = Storage[I].size();
    Image.Space = 7;
    Image.Register = 3 + I % 2;
    Image.Namespace = I < 2 ? 1 : 2;
    Image.Format = DXGI_FORMAT_R32G32B32A32_UINT;
    Image.Dimension = FeMeWarpImageDimension::Texture2DArray;
    Image.Width = Image.Height = I < 2 ? 4 : 2;
    Image.Depth = 1;
    Image.ArrayLayers = 2;
    Image.MipLevels = Image.MipLayoutCount = I < 2 ? 2 : 1;
    Layouts[I][0] = {32, 256, 1024};
    Layouts[I][1] = {2112, 256, 512};
    Image.MipLayouts = Layouts[I].data();
    for (unsigned Mip = 0; Mip < Image.MipLevels; ++Mip)
      for (unsigned Layer = 0; Layer < Image.ArrayLayers; ++Layer)
        for (unsigned Y = 0; Y < Image.Height >> Mip; ++Y)
          for (unsigned X = 0; X < Image.Width >> Mip; ++X) {
            std::array<std::uint32_t, 4> Texel{};
            for (unsigned C = 0; C < Texel.size(); ++C)
              Texel[C] =
                  1000 * I + 100 * Mip + 40 * Layer + 10 * Y + X + 10000 * C;
            auto Offset = Layouts[I][Mip].Offset +
                          Layer * Layouts[I][Mip].SlicePitch +
                          Y * Layouts[I][Mip].RowPitch + X * sizeof(Texel);
            std::memcpy(Storage[I].data() + Offset, Texel.data(),
                        sizeof(Texel));
          }
  }
  Images[0].Shader4ComponentMapping = 0x1000 | 2 | (5 << 6) | (4 << 9);
  std::array<FeMeWarpSamplerBinding, 2> Samplers{};
  std::array<float, 4> Samples{2, 6, 10, 14};
  if (Sampling) {
    for (unsigned I = 0; I < 2; ++I) {
      Images[I] = {};
      auto &Image = Images[I];
      Image.Data = Samples.data();
      Image.SizeInBytes = sizeof(Samples);
      Image.Space = 7;
      Image.Register = 3 + I;
      Image.Format = DXGI_FORMAT_R32_FLOAT;
      Image.Width = Image.Height = 2;
      Image.Depth = 1;
      Image.MipLevels = Image.MipLayoutCount = 1;
      Layouts[I][0] = {0, 8, 16};
      Image.MipLayouts = Layouts[I].data();
      Samplers[I].Space = 7;
      Samplers[I].Register = 3 + I;
      Samplers[I].Filter = I ? 0x15 : 0;
    }
    Desc.SamplerCount = Samplers.size();
    Desc.Samplers = Samplers.data();
  }
  Desc.ImageCount = Sampling ? 2 : 4;
  Desc.Images = Images.data();
  bool Good = true;
  if (NativeHeap) {
    Good = FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
    FeMeWarpDestroyComputePipeline(Pipeline);
    return Good;
  }
  auto Bad = Desc;
  Bad.Version = 5;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Bad) < 0;
  Bad = Desc;
  Bad.ImageCount = 65;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Bad) < 0;
  Bad = Desc;
  Bad.Images = nullptr;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Bad) < 0;
  Bad = Desc;
  --Bad.ImageCount;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Bad) < 0;
  auto Saved = Images[0];
  Images[0] = Images[1];
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  Images[0] = Saved;
  for (auto Namespace : {0u, 3u, 4u}) {
    Images[0].Namespace = Namespace;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  }
  Images[0] = Saved;
  Images[0].Data = nullptr;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  Images[0] = Saved;
  Images[0].SampleCount = 2;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  Images[0] = Saved;
  Images[0].FirstDepthSlice = 1;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  Images[0] = Saved;
  Images[0].PlaneCount = 2;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  Images[0] = Saved;
  Images[0].Format = DXGI_FORMAT_R32_TYPELESS;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  Images[0] = Saved;
  Images[0].Width = 16385;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  Images[0] = Saved;
  Images[0].MipLayouts = nullptr;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  Images[0] = Saved;
  ++Images[0].MipLayoutCount;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  Images[0] = Saved;
  Images[0].MipLevels = Images[0].MipLayoutCount = 16;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  Images[0] = Saved;
  Images[0].Dimension = static_cast<FeMeWarpImageDimension>(0);
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  Images[0] = Saved;
  auto SavedLayout = Layouts[0][0];
  for (auto Offset :
       {Saved.SizeInBytes, std::numeric_limits<std::uint64_t>::max()}) {
    Layouts[0][0].Offset = Offset;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
  }
  Layouts[0][0] = SavedLayout;
  for (auto Pitch : {1ull, std::numeric_limits<std::uint64_t>::max()}) {
    Layouts[0][0].RowPitch = Pitch;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
    Layouts[0][0] = SavedLayout;
    Layouts[0][0].SlicePitch = Pitch;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
    Layouts[0][0] = SavedLayout;
  }
  if (Sampling) {
    Bad = Desc;
    Bad.SamplerCount = 65;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Bad) < 0;
    Bad = Desc;
    Bad.Samplers = nullptr;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Bad) < 0;
    Bad = Desc;
    --Bad.SamplerCount;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Bad) < 0;
    auto SavedSampler = Samplers[0];
    Samplers[0] = Samplers[1];
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
    Samplers[0] = SavedSampler;
    for (auto Filter : {2u, 0x40u, 0x55u | 0x200u}) {
      Samplers[0].Filter = Filter;
      Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
    }
    Samplers[0] = SavedSampler;
    Samplers[0].AddressU = 0;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
    Samplers[0] = SavedSampler;
    Samplers[0].ComparisonFunc = 9;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
    Samplers[0] = SavedSampler;
    Samplers[0].MaxAnisotropy = 17;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
    Samplers[0] = SavedSampler;
    Samplers[0].MaxLOD = -1;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
    Samplers[0] = SavedSampler;
    Samplers[0].BorderColor[0] = std::numeric_limits<float>::quiet_NaN();
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) < 0;
    Samplers[0] = SavedSampler;
  }
  auto Before = Storage;
  for (unsigned Repeat = 0; Repeat < 3 && Good; ++Repeat) {
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) == 0;
    for (unsigned I = 0; I < (Sampling ? 8u : 32u); ++I) {
      std::uint32_t Expected;
      if (Sampling) {
        float Value = I % 2 ? 8 : 14;
        std::memcpy(&Expected, &Value, sizeof(Expected));
      } else {
        unsigned Thread = I / 8, C = I % 4, Index = Thread % 2;
        Expected = 1000 * (Index + (I % 8 >= 4 ? 2 : 0)) + 40 + Thread / 2 +
                   10000 * C + (I % 8 < 4 ? 110 : 0);
        if (I % 8 < 4 && !Index)
          Expected = C == 0   ? Expected + 20000
                     : C == 1 ? Expected - 10000
                     : C == 2 ? 1
                              : 0;
      }
      if (Output[I] != Expected) {
        std::fprintf(stderr, "image/sampler word %u: got %u, expected %u\n", I,
                     Output[I], Expected);
        Good = false;
      }
    }
    for (unsigned I = Sampling ? 8 : 32; I < Output.size(); ++I)
      Good &= Output[I] == 0x5a5a5a5a;
    Good &= Storage == Before;
  }
  if (Sampling) {
    Samplers[0].Filter = 0x115;
    Samplers[1].Filter = 0x195;
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) == 0;
    for (unsigned I = 0; I < 8; ++I) {
      float Value;
      std::memcpy(&Value, &Output[I], sizeof(Value));
      Good &= Value == (I % 2 ? 14 : 2);
    }
    for (unsigned I = 0; I < 2; ++I) {
      Samplers[I].AddressU = Samplers[I].AddressV = 4;
      Samplers[I].BorderColor[0] = I ? 30 : 20;
    }
    Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) == 0;
    for (unsigned I = 4; I < 8; ++I) {
      float Value;
      std::memcpy(&Value, &Output[I], sizeof(Value));
      Good &= Value == (I % 2 ? 30 : 20);
    }
  }
  Images[0] = {};
  Images[0].Space = 7;
  Images[0].Register = 3;
  Good &= FeMeWarpDispatchCompute(Pipeline, &Desc) == 0;
  for (unsigned Thread : {0u, 2u})
    for (unsigned C = 0; C < (Sampling ? 1u : 4u); ++C)
      Good &= Output[Sampling ? Thread : Thread * 8 + C] == 0;
  FeMeWarpDestroyComputePipeline(Pipeline);
  return Good;
}
