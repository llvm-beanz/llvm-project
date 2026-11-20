//===- ComputeShaderTests.cpp - Dispatch oracle negative controls --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ComputeShaderValidation.h"

#include <array>
#include <iterator>

int wmain(int Argc, wchar_t **Argv) {
  using namespace feme::probe;
  constexpr const wchar_t *Names[] = {
      L"success",    L"wrong-size",  L"missing-data",
      L"first-byte", L"last-value",  L"guard-first",
      L"guard-last", L"no-dispatch", L"patterns"};
  std::size_t Index = 0;
  if (Argc == 2)
    for (; Index < std::size(Names); ++Index)
      if (std::wcscmp(Argv[1], Names[Index]) == 0)
        break;
  if (Argc != 2 || Index == std::size(Names)) {
    std::fwprintf(stderr, L"Expected a compute-shader fixture scenario.\n");
    return 1;
  }
  std::array<std::uint8_t, BufferBytes> Data{};
  for (std::size_t I = 0; I < Data.size(); ++I)
    Data[I] = expectedComputeByte(I);
  bool Passed = false;
  if (Index == 0)
    Passed = validateComputeShader(Data.data(), Data.size());
  else if (Index == 1)
    Passed = !validateComputeShader(Data.data(), Data.size() - 1) &&
             !validateComputeShader(Data.data(), Data.size() + 1);
  else if (Index == 2)
    Passed = !validateComputeShader(nullptr, Data.size());
  else if (Index == 7) {
    for (std::size_t I = 0; I < Data.size(); ++I)
      Data[I] = baselineByte(I);
    Passed = !validateComputeShader(Data.data(), Data.size());
  } else if (Index == 8)
    Passed = expectedComputeValue(0) == 23 && expectedComputeValue(32) == 563 &&
             expectedComputeValue(255) == 4377 && Data[0] == 0x17 &&
             Data[1] == 0 && Data[1020] == 0x19 && Data[1021] == 0x11 &&
             Data[ComputeOutputBytes] == 0x13 && Data.back() == 0xf9;
  else {
    constexpr std::size_t Offsets[] = {0, ComputeOutputBytes - 1,
                                       ComputeOutputBytes, BufferBytes - 1};
    Data[Offsets[Index - 3]] ^= 1;
    Passed = !validateComputeShader(Data.data(), Data.size());
  }
  if (!Passed)
    std::fwprintf(stderr, L"Compute-shader fixture %ls failed.\n",
                  Names[Index]);
  return Passed ? 0 : 1;
}
