//===- ETC2SamplingBridge.cpp - VK_FORMAT_ETC2_*/EAC decode bridge --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ETC2SamplingBridge.h"
#include "ETC2Decode.h"

#include "llvm/Support/ErrorHandling.h"

using namespace feme::cpu;

feme::vulkan::ETC2SamplingTarget
feme::vulkan::etc2SamplingTarget(ResourceFormat Format) {
  switch (Format) {
  case ResourceFormat::ETC2_RGB8_UNORM:
  case ResourceFormat::ETC2_RGB8A1_UNORM:
  case ResourceFormat::ETC2_RGBA8_UNORM:
    return {ResourceFormat::R8G8B8A8_UNORM, 4};
  case ResourceFormat::ETC2_RGB8_SRGB:
  case ResourceFormat::ETC2_RGB8A1_SRGB:
  case ResourceFormat::ETC2_RGBA8_SRGB:
    return {ResourceFormat::R8G8B8A8_UNORM_SRGB, 4};
  // (Roadmap H8j) EAC's single-channel formats decode one 11-bit value
  // per texel -- target the single-channel 16-bit formats roadmap H19n
  // added (see this file's own header comment for why 16, not 8, bits:
  // the specification's own "some implementations may use 16 bits of
  // accuracy" bit-replication convention `decodeEACBlock` implements).
  case ResourceFormat::EAC_R11_UNORM:
    return {ResourceFormat::R16_UNORM, 2};
  case ResourceFormat::EAC_R11_SNORM:
    return {ResourceFormat::R16_SNORM, 2};
  // (Roadmap H8j) EAC's dual-channel formats decode two independent
  // 11-bit values per texel -- target the two-channel 16-bit formats
  // roadmap H19n added.
  case ResourceFormat::EAC_R11G11_UNORM:
    return {ResourceFormat::R16G16_UNORM, 4};
  case ResourceFormat::EAC_R11G11_SNORM:
    return {ResourceFormat::R16G16_SNORM, 4};
  default:
    llvm_unreachable("not a VK_FORMAT_ETC2_*/VK_FORMAT_EAC_* ResourceFormat");
  }
}

void feme::vulkan::decodeETC2FormatBlock(ResourceFormat Format,
                                         const uint8_t *Block,
                                         uint8_t *Output) {
  switch (Format) {
  case ResourceFormat::ETC2_RGB8_UNORM:
  case ResourceFormat::ETC2_RGB8_SRGB:
    decodeETC2Block(Block, Output);
    return;
  case ResourceFormat::ETC2_RGB8A1_UNORM:
  case ResourceFormat::ETC2_RGB8A1_SRGB:
    decodeETC2PunchthroughAlphaBlock(Block, Output);
    return;
  case ResourceFormat::ETC2_RGBA8_UNORM:
  case ResourceFormat::ETC2_RGBA8_SRGB: {
    // Per the specification's own two-64-bit-halves design: the alpha
    // half (EAC, unsigned) is stored first in memory, the RGB half
    // (ordinary opaque ETC2) second -- `decodeETC2Block` already writes
    // alpha = 255 for every texel, overwritten below with the real
    // decoded alpha, scaled from `decodeEACBlock`'s own 16-bit-accuracy
    // result down to this target's 8-bit alpha channel.
    decodeETC2Block(Block + 8, Output);
    uint16_t Alpha[16];
    decodeEACBlock(Block, /*Signed=*/false, Alpha);
    for (uint32_t T = 0; T != 16; ++T)
      Output[T * 4 + 3] = static_cast<uint8_t>(Alpha[T] >> 8);
    return;
  }
  case ResourceFormat::EAC_R11_UNORM:
    decodeEACBlock(Block, /*Signed=*/false,
                  reinterpret_cast<uint16_t *>(Output));
    return;
  case ResourceFormat::EAC_R11_SNORM:
    decodeEACBlock(Block, /*Signed=*/true,
                  reinterpret_cast<uint16_t *>(Output));
    return;
  case ResourceFormat::EAC_R11G11_UNORM:
  case ResourceFormat::EAC_R11G11_SNORM: {
    // Per the specification's own two-64-bit-halves design for this
    // format: the R channel is stored first in memory, the G channel
    // second -- each decoded independently, exactly as a standalone
    // single-channel EAC block would be, then interleaved into the
    // two-channel target's own R, G, R, G, ... texel layout.
    bool Signed = Format == ResourceFormat::EAC_R11G11_SNORM;
    uint16_t R[16], G[16];
    decodeEACBlock(Block, Signed, R);
    decodeEACBlock(Block + 8, Signed, G);
    auto *Out16 = reinterpret_cast<uint16_t *>(Output);
    for (uint32_t T = 0; T != 16; ++T) {
      Out16[T * 2 + 0] = R[T];
      Out16[T * 2 + 1] = G[T];
    }
    return;
  }
  default:
    llvm_unreachable("not a VK_FORMAT_ETC2_*/VK_FORMAT_EAC_* ResourceFormat");
  }
}
