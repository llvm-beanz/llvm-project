//===- BCSamplingBridge.cpp - VK_FORMAT_BC* decode-on-read bridge --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BCSamplingBridge.h"
#include "BC6HDecode.h"
#include "BC7Decode.h"
#include "BCDecode.h"

#include "llvm/Support/ErrorHandling.h"

using namespace feme::cpu;

feme::vulkan::BCSamplingTarget
feme::vulkan::bcSamplingTarget(ResourceFormat Format) {
  switch (Format) {
  case ResourceFormat::BC1_RGB_UNORM:
  case ResourceFormat::BC1_RGBA_UNORM:
  case ResourceFormat::BC2_UNORM:
  case ResourceFormat::BC3_UNORM:
  case ResourceFormat::BC7_UNORM:
    return {ResourceFormat::R8G8B8A8_UNORM, 4};
  case ResourceFormat::BC1_RGB_SRGB:
  case ResourceFormat::BC1_RGBA_SRGB:
  case ResourceFormat::BC2_SRGB:
  case ResourceFormat::BC3_SRGB:
  case ResourceFormat::BC7_SRGB:
    return {ResourceFormat::R8G8B8A8_UNORM_SRGB, 4};
  // (Roadmap H8n) BC4 decodes a single interpolated channel -- targets
  // the single-channel 8-bit formats roadmap H19j added.
  case ResourceFormat::BC4_UNORM:
    return {ResourceFormat::R8_UNORM, 1};
  case ResourceFormat::BC4_SNORM:
    return {ResourceFormat::R8_SNORM, 1};
  // (Roadmap H8n) BC5 decodes two independent interpolated channels --
  // targets the two-channel 8-bit formats roadmap H19n added.
  case ResourceFormat::BC5_UNORM:
    return {ResourceFormat::R8G8_UNORM, 2};
  case ResourceFormat::BC5_SNORM:
    return {ResourceFormat::R8G8_SNORM, 2};
  // (Roadmap H8n) BC6H decodes RGB half-float with no alpha channel of
  // its own -- targets `R16G16B16A16_FLOAT` (already runtime-supported),
  // padding the missing alpha channel to half-float `1.0` (bit pattern
  // `0x3C00`) in `decodeBCBlock` below.
  case ResourceFormat::BC6H_UFLOAT:
  case ResourceFormat::BC6H_SFLOAT:
    return {ResourceFormat::R16G16B16A16_FLOAT, 8};
  default:
    llvm_unreachable("not a VK_FORMAT_BC* ResourceFormat");
  }
}

void feme::vulkan::decodeBCBlock(ResourceFormat Format, const uint8_t *Block,
                                 uint8_t *Output) {
  switch (Format) {
  case ResourceFormat::BC1_RGB_UNORM:
  case ResourceFormat::BC1_RGB_SRGB:
    decodeBC1Block(Block, /*HasAlpha=*/false, Output);
    return;
  case ResourceFormat::BC1_RGBA_UNORM:
  case ResourceFormat::BC1_RGBA_SRGB:
    decodeBC1Block(Block, /*HasAlpha=*/true, Output);
    return;
  case ResourceFormat::BC2_UNORM:
  case ResourceFormat::BC2_SRGB:
    decodeBC2Block(Block, Output);
    return;
  case ResourceFormat::BC3_UNORM:
  case ResourceFormat::BC3_SRGB:
    decodeBC3Block(Block, Output);
    return;
  case ResourceFormat::BC7_UNORM:
  case ResourceFormat::BC7_SRGB:
    decodeBC7Block(Block, Output);
    return;
  case ResourceFormat::BC4_UNORM:
    decodeBC4Block(Block, /*Signed=*/false, Output);
    return;
  case ResourceFormat::BC4_SNORM:
    decodeBC4Block(Block, /*Signed=*/true, Output);
    return;
  case ResourceFormat::BC5_UNORM:
    decodeBC5Block(Block, /*Signed=*/false, Output);
    return;
  case ResourceFormat::BC5_SNORM:
    decodeBC5Block(Block, /*Signed=*/true, Output);
    return;
  case ResourceFormat::BC6H_UFLOAT:
  case ResourceFormat::BC6H_SFLOAT: {
    // `decodeBC6HBlock` produces 16 RGB half-float texels (3 `uint16_t`
    // each, 48 total); the sampling target is `R16G16B16A16_FLOAT` (4
    // `uint16_t` each) -- interleave in the missing alpha channel as
    // half-float `1.0` (`0x3C00`) while expanding.
    uint16_t RGB[16 * 3];
    decodeBC6HBlock(Block, Format == ResourceFormat::BC6H_SFLOAT, RGB);
    auto *Out16 = reinterpret_cast<uint16_t *>(Output);
    for (uint32_t T = 0; T != 16; ++T) {
      Out16[T * 4 + 0] = RGB[T * 3 + 0];
      Out16[T * 4 + 1] = RGB[T * 3 + 1];
      Out16[T * 4 + 2] = RGB[T * 3 + 2];
      Out16[T * 4 + 3] = 0x3C00;
    }
    return;
  }
  default:
    llvm_unreachable("not a VK_FORMAT_BC* ResourceFormat");
  }
}
