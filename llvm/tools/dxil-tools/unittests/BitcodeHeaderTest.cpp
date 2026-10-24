//===- BitcodeHeaderTest.cpp - Legacy bitcode header tests
//------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/Bitcode/BitcodeHeader.h"
#include "gtest/gtest.h"
#include <string>

using namespace llvm;

namespace {

constexpr StringLiteral Bitcode("BC\xc0\xde");

TEST(BitcodeHeader, SignatureDetection) {
  std::string Wrapper("\xde\xc0\x17\x0b", 4);
  for (StringRef Bytes : {StringRef(Bitcode), StringRef(Wrapper)}) {
    const auto *Begin = reinterpret_cast<const unsigned char *>(Bytes.data());
    for (size_t Size = 0; Size < 4; ++Size) {
      EXPECT_FALSE(dxil::isRawBitcode(Begin, Begin + Size));
      EXPECT_FALSE(dxil::isBitcodeWrapper(Begin, Begin + Size));
      EXPECT_FALSE(dxil::isBitcode(Begin, Begin + Size));
    }
    EXPECT_TRUE(dxil::isBitcode(Begin, Begin + Bytes.size()));
    EXPECT_EQ(Bytes == Bitcode,
              dxil::isRawBitcode(Begin, Begin + Bytes.size()));
    EXPECT_EQ(Bytes == Wrapper,
              dxil::isBitcodeWrapper(Begin, Begin + Bytes.size()));
  }
  EXPECT_FALSE(dxil::isBitcode(nullptr, nullptr));
}

TEST(BitcodeHeader, SkipsLegacyBitcodeWrapper) {
  std::string Wrapper("\xde\xc0\x17\x0b", 4);
  Wrapper.resize(20, '\0');
  Wrapper += Bitcode;
  support::endian::write32le(Wrapper.data() + 8, 20);
  support::endian::write32le(Wrapper.data() + 12, Bitcode.size());
  const auto *Begin = reinterpret_cast<const unsigned char *>(Wrapper.data());
  const auto *End = Begin + Wrapper.size();
  ASSERT_FALSE(dxil::SkipBitcodeWrapperHeader(Begin, End, true));
  EXPECT_EQ(Bitcode,
            StringRef(reinterpret_cast<const char *>(Begin), End - Begin));
}

TEST(BitcodeHeader, RejectsTruncatedBitcodeWrapper) {
  std::string Buffer(16, '\0');
  const auto *Start = reinterpret_cast<const unsigned char *>(Buffer.data());
  for (size_t Size = 0; Size < 16; ++Size) {
    const auto *Begin = Start;
    const auto *End = Begin + Size;
    EXPECT_TRUE(dxil::SkipBitcodeWrapperHeader(Begin, End, true));
    EXPECT_EQ(Start, Begin);
    EXPECT_EQ(Start + Size, End);
  }
}

TEST(BitcodeHeader, RejectsOverflowingBitcodeWrapper) {
  std::string Buffer(32, '\0');
  support::endian::write32le(Buffer.data() + 8, 0xfffffff0);
  support::endian::write32le(Buffer.data() + 12, 32);
  const auto *Start = reinterpret_cast<const unsigned char *>(Buffer.data());
  const auto *Begin = Start;
  const auto *End = Begin + Buffer.size();
  EXPECT_TRUE(dxil::SkipBitcodeWrapperHeader(Begin, End, true));
  EXPECT_EQ(Start, Begin);
  EXPECT_EQ(Start + Buffer.size(), End);
}

} // namespace
