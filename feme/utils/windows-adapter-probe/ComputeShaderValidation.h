//===- ComputeShaderValidation.h - Reference dispatch oracle -----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UTILS_WINDOWS_ADAPTER_PROBE_COMPUTESHADERVALIDATION_H
#define FEME_UTILS_WINDOWS_ADAPTER_PROBE_COMPUTESHADERVALIDATION_H

#include "BufferCopyValidation.h"

namespace feme::probe {
constexpr std::size_t ComputeThreads = 8;
constexpr std::size_t ComputeGroups = 32;
constexpr std::size_t ComputeValues = ComputeThreads * ComputeGroups;
constexpr std::size_t ComputeOutputBytes =
    ComputeValues * sizeof(std::uint32_t);
static_assert(ComputeOutputBytes < BufferBytes);

constexpr std::uint32_t expectedComputeValue(std::size_t Index) {
  return static_cast<std::uint32_t>((Index * 17 + 23) ^ (Index >> 3));
}

constexpr std::uint8_t expectedComputeByte(std::size_t Offset) {
  if (Offset >= ComputeOutputBytes)
    return baselineByte(Offset);
  return static_cast<std::uint8_t>(expectedComputeValue(Offset / 4) >>
                                   (8 * (Offset % 4)));
}

inline bool validateComputeShader(const std::uint8_t *Data, std::size_t Size) {
  if (!Data || Size != BufferBytes) {
    std::fwprintf(stderr,
                  L"Compute shader invalid readback: data=%ls size=%zu "
                  L"expected=%zu\n",
                  Data ? L"present" : L"null", Size, BufferBytes);
    return false;
  }
  for (std::size_t I = 0; I < Size; ++I) {
    std::uint8_t Expected = expectedComputeByte(I);
    if (Data[I] != Expected) {
      std::fwprintf(stderr,
                    L"Compute shader mismatch: byte=%zu expected=%02x "
                    L"actual=%02x\n",
                    I, unsigned(Expected), unsigned(Data[I]));
      return false;
    }
  }
  return true;
}
} // namespace feme::probe

#endif
