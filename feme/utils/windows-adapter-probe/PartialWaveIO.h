//===- PartialWaveIO.h - FeMe runner wave-oracle interchange -------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UTILS_WINDOWS_ADAPTER_PROBE_PARTIALWAVEIO_H
#define FEME_UTILS_WINDOWS_ADAPTER_PROBE_PARTIALWAVEIO_H

#include "PartialWaveValidation.h"

#include <cctype>
#include <limits>

namespace feme::probe {
inline bool writePartialWaveHeap(FILE *Output, unsigned Space) {
  if (!Output || (Space != 0 && Space != 7))
    return partialWaveFailure(L"invalid heap output or register space");
  std::fprintf(Output,
               "bindings:\n  - space: %u\n    register: 3\n    entries:\n"
               "      - index: 0\n        size: %zu\n        data: [",
               Space, BufferBytes);
  for (std::size_t I = 0; I < BufferBytes / 4; ++I) {
    std::uint32_t Word = 0;
    for (unsigned Byte = 0; Byte < 4; ++Byte)
      Word |= std::uint32_t(baselineByte(I * 4 + Byte)) << (Byte * 8);
    std::fprintf(Output, "%s%u", I ? ", " : "", Word);
  }
  std::fprintf(Output, "]\n");
  if (std::ferror(Output) || std::fflush(Output) != 0)
    return partialWaveFailure(L"heap output write failed");
  return true;
}

/// Accept exactly one complete binding dump. Reject truncation, overflow and
/// extra output rather than validating a zero-filled or partially parsed
/// buffer.
inline bool readPartialWaveOutput(FILE *Input, unsigned Space,
                                  std::uint8_t *Data, std::size_t Size) {
  if (!Input || !Data || Size != BufferBytes || (Space != 0 && Space != 7))
    return partialWaveFailure(L"invalid runner readback arguments");
  char Header[32];
  std::snprintf(Header, sizeof(Header), "binding[%u:3][0]:", Space);
  for (const char *P = Header; *P; ++P)
    if (std::fgetc(Input) != *P)
      return partialWaveFailure(L"incorrect runner binding header");

  int Ch = std::fgetc(Input);
  for (std::size_t I = 0; I < Size / 4; ++I) {
    if (Ch == EOF || !std::isspace(Ch))
      return partialWaveFailure(L"missing runner word separator", I);
    do {
      Ch = std::fgetc(Input);
    } while (Ch != EOF && std::isspace(Ch));
    if (Ch < '0' || Ch > '9')
      return partialWaveFailure(L"missing or invalid runner word", I);
    std::uint32_t Word = 0;
    do {
      unsigned Digit = static_cast<unsigned>(Ch - '0');
      if (Word > (std::numeric_limits<std::uint32_t>::max() - Digit) / 10)
        return partialWaveFailure(L"runner word overflow", I);
      Word = Word * 10 + Digit;
      Ch = std::fgetc(Input);
    } while (Ch >= '0' && Ch <= '9');
    std::memcpy(Data + I * 4, &Word, sizeof(Word));
  }
  while (Ch != EOF && std::isspace(Ch))
    Ch = std::fgetc(Input);
  if (Ch != EOF || std::ferror(Input))
    return partialWaveFailure(L"extra runner output or read failure");
  return true;
}
} // namespace feme::probe

#endif
