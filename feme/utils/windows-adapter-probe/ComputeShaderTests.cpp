//===- ComputeShaderTests.cpp - Dispatch oracle negative controls --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BufferIO.h"
#include "ComputeShaderValidation.h"

#include <array>
#include <iterator>

int wmain(int Argc, wchar_t **Argv) {
  using namespace feme::probe;
  auto Fail = [](const wchar_t *Reason) {
    bufferIOFailure(Reason);
    return 1;
  };
  if (Argc == 3 && std::wcscmp(Argv[1], L"--heap") == 0) {
    if (std::wcscmp(Argv[2], L"0") != 0 && std::wcscmp(Argv[2], L"7") != 0)
      return Fail(L"expected register space 0 or 7");
    return writeBufferHeap(stdout, Argv[2][0] == L'7' ? 7 : 0) ? 0 : 1;
  }
  if (Argc == 5 && std::wcscmp(Argv[1], L"--output") == 0) {
    if (std::wcscmp(Argv[3], L"0") != 0 && std::wcscmp(Argv[3], L"7") != 0)
      return Fail(L"expected register space 0 or 7");
    bool Grouped = std::wcscmp(Argv[4], L"grouped") == 0;
    if (!Grouped && std::wcscmp(Argv[4], L"raw") != 0)
      return Fail(L"expected workload raw or grouped");
    FILE *Input = nullptr;
    if (_wfopen_s(&Input, Argv[2], L"rb") != 0 || !Input)
      return Fail(L"cannot open runner output");
    unsigned Space = Argv[3][0] == L'7' ? 7 : 0;
    std::array<std::uint8_t, BufferBytes> Data{};
    bool Valid = readBufferOutput(Input, Space, Data.data(), Data.size()) &&
                 validateComputeShader(Data.data(), Data.size(), Grouped);
    if (std::fclose(Input) != 0)
      return Fail(L"runner output close failed");
    if (!Valid)
      return 1;
    ComputeDispatch Dispatch = computeDispatch(Grouped);
    std::wprintf(
        L"FeMe CPU compute verified: workload=%ls values=%zu "
        L"bytes=%zu groups=%u,%u,%u threads=%zu space=%u guard-bytes=%zu\n",
        Argv[4], Dispatch.Values, BufferBytes, Dispatch.X, Dispatch.Y,
        Dispatch.Z, Dispatch.Threads, Space,
        BufferBytes - Dispatch.Values * sizeof(std::uint32_t));
    return 0;
  }
  constexpr const wchar_t *Names[] = {
      L"success",    L"wrong-size",  L"missing-data",
      L"first-byte", L"last-value",  L"guard-first",
      L"guard-last", L"no-dispatch", L"patterns"};
  std::size_t Index = 0;
  bool Grouped = Argc == 3 && std::wcscmp(Argv[2], L"grouped") == 0;
  if (Argc == 2 || Grouped)
    for (; Index < std::size(Names); ++Index)
      if (std::wcscmp(Argv[1], Names[Index]) == 0)
        break;
  if ((Argc != 2 && !Grouped) || Index == std::size(Names)) {
    std::fwprintf(stderr, L"Expected a compute-shader fixture scenario.\n");
    return 1;
  }
  std::array<std::uint8_t, BufferBytes> Data{};
  std::size_t OutputBytes =
      computeDispatch(Grouped).Values * sizeof(std::uint32_t);
  for (std::size_t I = 0; I < Data.size(); ++I)
    Data[I] = expectedComputeByte(I, Grouped);
  bool Passed = false;
  if (Index == 0)
    Passed = validateComputeShader(Data.data(), Data.size(), Grouped);
  else if (Index == 1)
    Passed = !validateComputeShader(Data.data(), Data.size() - 1, Grouped) &&
             !validateComputeShader(Data.data(), Data.size() + 1, Grouped);
  else if (Index == 2)
    Passed = !validateComputeShader(nullptr, Data.size(), Grouped);
  else if (Index == 7) {
    for (std::size_t I = 0; I < Data.size(); ++I)
      Data[I] = baselineByte(I);
    Passed = !validateComputeShader(Data.data(), Data.size(), Grouped);
  } else if (Index == 8 && Grouped)
    Passed = expectedComputeValue(0, true) == 588 &&
             expectedComputeValue(8, true) == 1769 &&
             expectedComputeValue(16, true) == 725 &&
             expectedComputeValue(128, true) == 40 &&
             expectedComputeValue(511, true) == 8881 && Data[0] == 0x4c &&
             Data[1] == 0x02 && Data[2044] == 0xb1 && Data[2045] == 0x22 &&
             Data[OutputBytes] == 0x1f && Data.back() == 0xf9;
  else if (Index == 8)
    Passed = expectedComputeValue(0) == 23 && expectedComputeValue(32) == 563 &&
             expectedComputeValue(255) == 4377 && Data[0] == 0x17 &&
             Data[1] == 0 && Data[1020] == 0x19 && Data[1021] == 0x11 &&
             Data[ComputeOutputBytes] == 0x13 && Data.back() == 0xf9;
  else {
    const std::size_t Offsets[] = {0, OutputBytes - 1, OutputBytes,
                                   BufferBytes - 1};
    Data[Offsets[Index - 3]] ^= 1;
    Passed = !validateComputeShader(Data.data(), Data.size(), Grouped);
  }
  if (!Passed)
    std::fwprintf(stderr, L"Compute-shader fixture %ls failed.\n",
                  Names[Index]);
  return Passed ? 0 : 1;
}
