//===- PartialWaveTests.cpp - Wave oracle negative controls
//----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PartialWaveValidation.h"

#include <iterator>

using namespace feme::probe;

static void setWord(std::uint8_t *Data, std::size_t Index, std::size_t Word,
                    std::uint32_t Value) {
  std::memcpy(Data + (Index * PartialRecordWords + Word) * sizeof(Value),
              &Value, sizeof(Value));
}

static void fillData(std::uint8_t *Data, std::uint32_t WaveSize, bool Permuted,
                     bool ShiftedLanes = false) {
  for (std::size_t I = 0; I < BufferBytes; ++I)
    Data[I] = baselineByte(I);
  for (std::size_t I = 0; I < PartialRecords; ++I) {
    std::size_t Local = I % PartialThreads;
    std::size_t Ordinal = Permuted ? Local * 7 % PartialThreads : Local;
    std::size_t Begin = Ordinal / WaveSize * WaveSize;
    std::size_t End = std::min(Begin + WaveSize, PartialThreads);
    std::size_t LaneBase = ShiftedLanes ? WaveSize - (End - Begin) : 0;
    std::uint32_t Sum = 0;
    for (std::size_t J = Begin; J < End; ++J)
      Sum += static_cast<std::uint32_t>((Permuted ? J * 13 % 30 : J) + 1);
    const std::uint32_t Words[] = {
        expectedPartialValue(I),
        expectedPartialDispatchID(I),
        WaveSize,
        static_cast<std::uint32_t>(LaneBase + Ordinal - Begin),
        static_cast<std::uint32_t>(Permuted ? Begin * 13 % 30 : Begin),
        static_cast<std::uint32_t>(End - Begin),
        Sum,
        static_cast<std::uint32_t>(I / PartialThreads)};
    for (std::size_t W = 0; W < PartialRecordWords; ++W)
      setWord(Data, I, W, Words[W]);
  }
}

int wmain(int Argc, wchar_t **Argv) {
  constexpr const wchar_t *Names[] = {
      L"success",     L"wrong-size",    L"missing-data",     L"value",
      L"dispatch-id", L"wave-size",     L"lane-range",       L"duplicate-lane",
      L"first-range", L"missing-first", L"wrong-first",      L"active-count",
      L"reduction",   L"group-marker",  L"guard-first",      L"guard-last",
      L"no-dispatch", L"unsupported",   L"policy-range",     L"policy-order",
      L"policy-size", L"wave-policy",   L"wave-consistency", L"patterns"};
  std::size_t Scenario = 0;
  if (Argc == 2)
    for (; Scenario < std::size(Names); ++Scenario)
      if (std::wcscmp(Argv[1], Names[Scenario]) == 0)
        break;
  if (Argc != 2 || Scenario == std::size(Names)) {
    std::fwprintf(stderr, L"Expected a partial-wave fixture scenario.\n");
    return 1;
  }
  std::array<std::uint8_t, BufferBytes> Data{};
  WaveObservation Observed;
  WavePolicy Policy{true, 4, 128};
  bool Passed = false;
  if (Scenario == 0) {
    Passed = true;
    for (std::uint32_t Size : {4u, 8u, 16u, 32u, 64u, 128u})
      for (bool Permuted : {false, true})
        for (bool ShiftedLanes : {false, true}) {
          fillData(Data.data(), Size, Permuted, ShiftedLanes);
          std::size_t Waves =
              PartialGroups * ((PartialThreads + Size - 1) / Size);
          Passed &= validatePartialWave(Data.data(), Data.size(),
                                        {true, Size, Size}, Observed) &&
                    Observed.Min == Size && Observed.Max == Size &&
                    Observed.Waves == Waves &&
                    Observed.PartialWaves == PartialGroups;
        }
  } else {
    fillData(Data.data(), 8, false);
    const std::uint8_t *Input = Data.data();
    std::size_t Size = Data.size();
    switch (Scenario) {
    case 1:
      Passed = !validatePartialWave(Input, Size - 1, Policy, Observed) &&
               !validatePartialWave(Input, Size + 1, Policy, Observed);
      break;
    case 2:
      Input = nullptr;
      break;
    case 3:
      setWord(Data.data(), 0, 0, 0);
      break;
    case 4:
      setWord(Data.data(), PartialRecords - 1, 1, 0);
      break;
    case 5:
      setWord(Data.data(), 0, 2, 6);
      break;
    case 6:
      setWord(Data.data(), 0, 3, 8);
      break;
    case 7:
      setWord(Data.data(), 1, 3, 0);
      break;
    case 8:
      setWord(Data.data(), 0, 4, 30);
      break;
    case 9:
      for (std::size_t I = 0; I < 8; ++I)
        setWord(Data.data(), I, 4, 29);
      break;
    case 10:
      for (std::size_t I = 0; I < 8; ++I)
        setWord(Data.data(), I, 4, 1);
      break;
    case 11:
    case 12:
      for (std::size_t I = 0; I < 8; ++I)
        setWord(Data.data(), I, Scenario == 11 ? 5 : 6, 7);
      break;
    case 13:
      setWord(Data.data(), 30, 7, 0);
      break;
    case 14:
      Data[PartialOutputBytes] ^= 1;
      break;
    case 15:
      Data.back() ^= 1;
      break;
    case 16:
      for (std::size_t I = 0; I < Data.size(); ++I)
        Data[I] = baselineByte(I);
      break;
    case 17:
      Policy.WaveOps = false;
      break;
    case 18:
      Policy.Max = 256;
      break;
    case 19:
      Policy.Min = 32;
      Policy.Max = 8;
      break;
    case 20:
      Policy.Min = 3;
      break;
    case 21:
      Policy.Min = 16;
      break;
    case 22:
      setWord(Data.data(), 0, 2, 16);
      break;
    case 23:
      Passed =
          expectedPartialValue(0) == 211 && expectedPartialDispatchID(0) == 0 &&
          expectedPartialDispatchID(179) == 179 && PartialOutputBytes == 5760;
      break;
    }
    if (Scenario != 1 && Scenario != 23)
      Passed = !validatePartialWave(Input, Size, Policy, Observed);
  }
  if (!Passed)
    std::fwprintf(stderr, L"Partial-wave fixture %ls failed.\n",
                  Names[Scenario]);
  return Passed ? 0 : 1;
}
