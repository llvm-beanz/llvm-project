//===- BCDecode.cpp - BC1-5 (S3TC/RGTC) block decoder --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap WS1(a): the real implementation now lives in
// feme/lib/Image/BCDecode.cpp (feme::image::decodeBC*Block); this file is
// only a thin forwarder so every existing Vulkan caller is unaffected.
//
//===----------------------------------------------------------------------===//

#include "BCDecode.h"

#include "feme/Image/BCDecode.h"

void feme::vulkan::decodeBC1Block(const uint8_t Block[8], bool HasAlpha,
                                   uint8_t *Output) {
  feme::image::decodeBC1Block(Block, HasAlpha, Output);
}

void feme::vulkan::decodeBC2Block(const uint8_t Block[16], uint8_t *Output) {
  feme::image::decodeBC2Block(Block, Output);
}

void feme::vulkan::decodeBC3Block(const uint8_t Block[16], uint8_t *Output) {
  feme::image::decodeBC3Block(Block, Output);
}

void feme::vulkan::decodeBC4Block(const uint8_t Block[8], bool Signed,
                                   uint8_t *Output) {
  feme::image::decodeBC4Block(Block, Signed, Output);
}

void feme::vulkan::decodeBC5Block(const uint8_t Block[16], bool Signed,
                                   uint8_t *Output) {
  feme::image::decodeBC5Block(Block, Signed, Output);
}