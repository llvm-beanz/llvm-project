//===- PartialWaveValidation.h - Reference wave dispatch oracle ---*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UTILS_WINDOWS_ADAPTER_PROBE_PARTIALWAVEVALIDATION_H
#define FEME_UTILS_WINDOWS_ADAPTER_PROBE_PARTIALWAVEVALIDATION_H

#include "ComputeShaderValidation.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace feme::probe {
constexpr std::size_t PartialThreads = 30;
constexpr std::size_t PartialGroups = 6;
constexpr std::size_t PartialRecords = PartialThreads * PartialGroups;
constexpr std::size_t PartialRecordWords = 8;
constexpr ComputeDispatch PartialDispatch{3, 2, 1, PartialThreads,
                                          PartialRecords * PartialRecordWords};
constexpr std::size_t PartialOutputBytes =
    PartialDispatch.Values * sizeof(std::uint32_t);
static_assert(PartialOutputBytes < BufferBytes);

struct WavePolicy {
  bool WaveOps;
  std::uint32_t Min, Max;
};

struct WaveObservation {
  std::uint32_t Min = 128, Max = 0;
  std::size_t Waves = 0, PartialWaves = 0;
};

constexpr bool validWaveSize(std::uint32_t Size) {
  return Size >= 4 && Size <= 128 && (Size & (Size - 1)) == 0;
}

constexpr std::uint32_t expectedPartialValue(std::size_t Index) {
  std::size_t Group = Index / PartialThreads;
  return expectedComputeValue(Group * PartialThreads +
                              (Index % PartialThreads + 11) % PartialThreads) +
         static_cast<std::uint32_t>(Group * 101);
}

constexpr std::uint32_t expectedPartialDispatchID(std::size_t Index) {
  std::size_t Group = Index / PartialThreads, Local = Index % PartialThreads;
  return static_cast<std::uint32_t>(
      Group % 3 * 5 + Local % 5 +
      15 * (Group / 3 * 3 + Local / 5 % 3 + 6 * (Local / 15)));
}

inline std::uint32_t partialRecordWord(const std::uint8_t *Data,
                                       std::size_t Index, std::size_t Word) {
  std::uint32_t Value;
  std::memcpy(&Value,
              Data + (Index * PartialRecordWords + Word) * sizeof(Value),
              sizeof(Value));
  return Value;
}

inline bool partialWaveFailure(const wchar_t *Reason, std::size_t Index = 0) {
  std::fwprintf(stderr, L"Partial-wave validation failed: %ls index=%zu\n",
                Reason, Index);
  return false;
}

/// Reconstruct wave membership from first-lane identities, without assuming a
/// correspondence between local thread indices and lane indices.
inline bool validatePartialWave(const std::uint8_t *Data, std::size_t Size,
                                WavePolicy Policy, WaveObservation &Observed) {
  if (!Data || Size != BufferBytes)
    return partialWaveFailure(L"missing data or incorrect readback size", Size);
  if (!Policy.WaveOps || !validWaveSize(Policy.Min) ||
      !validWaveSize(Policy.Max) || Policy.Min > Policy.Max)
    return partialWaveFailure(L"invalid or unsupported runtime wave policy");

  WaveObservation Result;
  for (std::size_t Group = 0; Group < PartialGroups; ++Group) {
    struct Wave {
      std::uint32_t Size = 0, Active = 0, Sum = 0;
      std::uint32_t Members = 0, MemberSum = 0, MinLane = 128;
      std::array<bool, 128> Lanes{};
      bool HasFirst = false;
      std::uint32_t FirstLane = 0;
    };
    std::array<Wave, PartialThreads> Waves{};
    for (std::size_t Local = 0; Local < PartialThreads; ++Local) {
      std::size_t Index = Group * PartialThreads + Local;
      std::uint32_t Value = partialRecordWord(Data, Index, 0);
      std::uint32_t ID = partialRecordWord(Data, Index, 1);
      std::uint32_t Count = partialRecordWord(Data, Index, 2);
      std::uint32_t Lane = partialRecordWord(Data, Index, 3);
      std::uint32_t First = partialRecordWord(Data, Index, 4);
      std::uint32_t Active = partialRecordWord(Data, Index, 5);
      std::uint32_t Sum = partialRecordWord(Data, Index, 6);
      std::uint32_t FirstGroup = partialRecordWord(Data, Index, 7);
      if (Value != expectedPartialValue(Index) ||
          ID != expectedPartialDispatchID(Index))
        return partialWaveFailure(L"shared-memory value or dispatch ID", Index);
      if (!validWaveSize(Count) || Count < Policy.Min || Count > Policy.Max ||
          Lane >= Count || First >= PartialThreads || FirstGroup != Group ||
          Active == 0 || Active > Count)
        return partialWaveFailure(L"wave query out of range", Index);
      Wave &W = Waves[First];
      if (W.Members == 0) {
        W.Size = Count;
        W.Active = Active;
        W.Sum = Sum;
      }
      if (W.Size != Count || W.Active != Active || W.Sum != Sum ||
          W.Lanes[Lane])
        return partialWaveFailure(L"inconsistent wave or duplicate lane",
                                  Index);
      W.Lanes[Lane] = true;
      ++W.Members;
      W.MemberSum += static_cast<std::uint32_t>(Local + 1);
      W.MinLane = std::min(W.MinLane, Lane);
      if (Local == First) {
        W.HasFirst = true;
        W.FirstLane = Lane;
      }
    }
    for (const Wave &W : Waves) {
      if (!W.Members)
        continue;
      if (W.Members != W.Active || W.MemberSum != W.Sum || !W.HasFirst ||
          W.FirstLane != W.MinLane)
        return partialWaveFailure(L"wave membership or reduction", Group);
      Result.Min = std::min(Result.Min, W.Size);
      Result.Max = std::max(Result.Max, W.Size);
      ++Result.Waves;
      Result.PartialWaves += W.Members < W.Size;
    }
  }
  if (Result.PartialWaves < PartialGroups)
    return partialWaveFailure(L"missing partial waves");
  for (std::size_t I = PartialOutputBytes; I < Size; ++I)
    if (Data[I] != baselineByte(I))
      return partialWaveFailure(L"guard byte changed", I);
  Observed = Result;
  return true;
}
} // namespace feme::probe

#endif
