//===- BufferCopyTests.cpp - Buffer-copy oracle negative controls
//----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BufferCopyValidation.h"

#include <array>
#include <iterator>

int wmain(int Argc, wchar_t **Argv) {
  using namespace feme::probe;
  constexpr const wchar_t *Names[] = {
      L"success",    L"wrong-size", L"missing-data", L"prefix",  L"patch-first",
      L"patch-last", L"tail-first", L"last-byte",    L"patterns"};
  std::size_t Index = 0;
  if (Argc == 2)
    for (; Index < std::size(Names); ++Index)
      if (std::wcscmp(Argv[1], Names[Index]) == 0)
        break;
  if (Argc != 2 || Index == std::size(Names)) {
    std::fwprintf(stderr, L"Expected a buffer-copy fixture scenario.\n");
    return 1;
  }
  std::array<std::uint8_t, BufferBytes> Data{};
  for (std::size_t I = 0; I < Data.size(); ++I)
    Data[I] = expectedCopyByte(I);
  bool Passed = false;
  if (Index == 0)
    Passed = validateBufferCopy(Data.data(), Data.size());
  else if (Index == 1)
    Passed = !validateBufferCopy(Data.data(), Data.size() - 1);
  else if (Index == 2)
    Passed = !validateBufferCopy(nullptr, Data.size());
  else if (Index == 8) {
    std::array<std::uint8_t, 2 * BufferBytes> Upload{};
    fillCopyUpload(Upload.data());
    Passed = Upload[0] == 0x17 && Upload[256] == 0x16 &&
             Upload[BufferBytes + 113] == 0x97 && Data[257] == 0x97 &&
             Data[60269] == 0x1e && Data[60270] == 0x8e && Data.back() == 0xf9;
  } else {
    constexpr std::size_t Offsets[] = {0, CopyTargetOffset, CopyTailOffset - 1,
                                       CopyTailOffset, BufferBytes - 1};
    Data[Offsets[Index - 3]] ^= 1;
    Passed = !validateBufferCopy(Data.data(), Data.size());
  }
  if (!Passed)
    std::fwprintf(stderr, L"Buffer-copy fixture %ls failed.\n", Names[Index]);
  return Passed ? 0 : 1;
}
