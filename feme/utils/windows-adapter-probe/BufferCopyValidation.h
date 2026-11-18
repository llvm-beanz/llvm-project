//===- BufferCopyValidation.h - Reference buffer-copy data oracle -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UTILS_WINDOWS_ADAPTER_PROBE_BUFFERCOPYVALIDATION_H
#define FEME_UTILS_WINDOWS_ADAPTER_PROBE_BUFFERCOPYVALIDATION_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace feme::probe {
constexpr std::size_t BufferBytes = 65536;
constexpr std::size_t CopySourceOffset = 113;
constexpr std::size_t CopyTargetOffset = 257;
constexpr std::size_t CopyBytes = 60013;
constexpr std::size_t CopyTailOffset = CopyTargetOffset + CopyBytes;
static_assert(CopySourceOffset + CopyBytes < BufferBytes);
static_assert(CopyTailOffset < BufferBytes);

constexpr std::uint8_t baselineByte(std::size_t Offset) {
  return static_cast<std::uint8_t>((Offset * 17 + 23) ^ (Offset >> 8));
}

constexpr std::uint8_t patchByte(std::size_t Offset) {
  return static_cast<std::uint8_t>((Offset * 29 + 101) ^ (Offset >> 7) ^ 0xa5);
}

constexpr std::uint8_t expectedCopyByte(std::size_t Offset) {
  return Offset >= CopyTargetOffset && Offset < CopyTailOffset
             ? patchByte(CopySourceOffset + Offset - CopyTargetOffset)
             : baselineByte(Offset);
}

inline void fillCopyUpload(std::uint8_t *Data) {
  for (std::size_t I = 0; I < BufferBytes; ++I) {
    Data[I] = baselineByte(I);
    Data[BufferBytes + I] = patchByte(I);
  }
}

inline bool validateBufferCopy(const std::uint8_t *Data, std::size_t Size) {
  if (!Data || Size != BufferBytes) {
    std::fwprintf(stderr,
                  L"Buffer copy invalid readback: data=%ls size=%zu "
                  L"expected=%zu\n",
                  Data ? L"present" : L"null", Size, BufferBytes);
    return false;
  }
  for (std::size_t I = 0; I < Size; ++I) {
    std::uint8_t Expected = expectedCopyByte(I);
    if (Data[I] != Expected) {
      std::fwprintf(stderr,
                    L"Buffer copy mismatch: byte=%zu expected=%02x "
                    L"actual=%02x\n",
                    I, unsigned(Expected), unsigned(Data[I]));
      return false;
    }
  }
  return true;
}
} // namespace feme::probe

#endif
