//===- BC6HDecode.cpp - BC6H (BPTC HDR) block decoder --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap WS1(a): the real implementation now lives in
// feme/lib/Image/BC6HDecode.cpp (feme::image::decodeBC6HBlock); this file
// is only a thin forwarder so every existing Vulkan caller is unaffected.
//
//===----------------------------------------------------------------------===//

#include "BC6HDecode.h"

#include "feme/Image/BC6HDecode.h"

void feme::vulkan::decodeBC6HBlock(const uint8_t Block[16], bool Signed,
                                    uint16_t *Output) {
  feme::image::decodeBC6HBlock(Block, Signed, Output);
}