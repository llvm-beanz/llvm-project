//===- BitcodeHeader.h - Bitcode signatures and wrappers ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_BITCODE_BITCODEHEADER_H
#define DXIL_BITCODE_BITCODEHEADER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Endian.h"
#include <cstdint>

namespace dxil {

inline bool isBitcodeWrapper(const unsigned char *Begin,
                             const unsigned char *End) {
  return Begin != End && End - Begin >= 4 &&
         llvm::StringRef(reinterpret_cast<const char *>(Begin), 4) ==
             llvm::StringRef("\xde\xc0\x17\x0b", 4);
}

inline bool isRawBitcode(const unsigned char *Begin, const unsigned char *End) {
  return Begin != End && End - Begin >= 4 &&
         llvm::StringRef(reinterpret_cast<const char *>(Begin), 4) ==
             llvm::StringRef("BC\xc0\xde", 4);
}

inline bool isBitcode(const unsigned char *Begin, const unsigned char *End) {
  return isBitcodeWrapper(Begin, End) || isRawBitcode(Begin, End);
}

/// Skip a legacy bitcode wrapper. Return true on failure without changing the
/// input range. VerifyBufferSize may be disabled by the legacy streaming
/// reader, which initially has only the wrapper header available.
inline bool SkipBitcodeWrapperHeader(const unsigned char *&Begin,
                                     const unsigned char *&End,
                                     bool VerifyBufferSize) {
  if (Begin == End || End - Begin < 16)
    return true;
  uint32_t Offset = llvm::support::endian::read32le(Begin + 8);
  uint32_t Size = llvm::support::endian::read32le(Begin + 12);
  if (VerifyBufferSize && uint64_t(Offset) + Size > uint64_t(End - Begin))
    return true;
  Begin += Offset;
  End = Begin + Size;
  return false;
}

} // namespace dxil

#endif
