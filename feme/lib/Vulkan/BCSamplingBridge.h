//===- BCSamplingBridge.h - VK_FORMAT_BC* decode-on-read bridge --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H8n ("Wire the now-complete BC1-7 decoders into a real consumer
// and flip textureCompressionBC"): the small per-format dispatch table
// `CommandBuffer.cpp`'s `materializeImageDescriptor` (shader sampling) and
// `ImageOps.cpp`'s `runBlitImage` (blit source) both need to decode one
// `VK_FORMAT_BC*` block into whichever already-runtime-supported
// `feme::cpu::ResourceFormat` matches that format's own decoded channel
// count/precision -- factored out here, rather than duplicated in both
// files, since both call sites need the identical answer.
//
// Unlike every ASTC footprint (which all decode into the same RGBA8 shape,
// `R8G8B8A8_UNORM`/`_UNORM_SRGB`, so `ASTCDecode.h`'s `decodeASTCBlock`
// needs no per-format dispatch of its own), BC's decoders in `BCDecode.h`/
// `BC7Decode.h`/`BC6HDecode.h` each produce a different output shape per
// sub-family: BC1/BC2/BC3/BC7 decode to RGBA8 like ASTC, BC4 decodes to a
// single interpolated channel, BC5 decodes to two independent
// interpolated channels, and BC6H decodes to RGB half-float with no alpha
// channel at all -- `bcSamplingTarget` below is what maps each of the 16
// formats to its own single/dual-channel-or-RGBA8, 8-bit-or-half-float
// already-runtime-supported target, and `decodeBCBlock` is what actually
// drives the right one of the five decode functions (padding BC6H's
// missing alpha channel to half-float `1.0`, `0x3C00`, along the way).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_BCSAMPLINGBRIDGE_H
#define FEME_LIB_VULKAN_BCSAMPLINGBRIDGE_H

#include "feme/Target/CPU/RuntimeABI.h"

#include <cstdint>

namespace feme::vulkan {

/// The already-runtime-supported `feme::cpu::ResourceFormat` and per-texel
/// byte size \p Format's own blocks should be decoded into for shader
/// sampling or a blit source -- see this file's own header comment for why
/// each BC sub-family needs a different target rather than sharing one the
/// way every ASTC format does.
struct BCSamplingTarget {
  feme::cpu::ResourceFormat Format;
  uint32_t BytesPerTexel;
};

/// \p Format must be one of the 16 `VK_FORMAT_BC*`-mapped `ResourceFormat`
/// values (`feme::cpu::isBCFormat(Format)`).
BCSamplingTarget bcSamplingTarget(feme::cpu::ResourceFormat Format);

/// Decodes one BC-format 4x4 texel block at \p Block into \p Output,
/// which must have room for `16 * bcSamplingTarget(Format).BytesPerTexel`
/// bytes. \p Format must be one of the 16 `VK_FORMAT_BC*`-mapped
/// `ResourceFormat` values.
void decodeBCBlock(feme::cpu::ResourceFormat Format, const uint8_t *Block,
                   uint8_t *Output);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_BCSAMPLINGBRIDGE_H
