//===- ETC2SamplingBridge.h - VK_FORMAT_ETC2_*/EAC decode bridge --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H8j ("Wire the now-complete ETC2Decode.h decoder into a real
// consumer and flip textureCompressionETC2"): the ETC2/EAC analogue of
// `BCSamplingBridge.h` -- the small per-format dispatch table
// `CommandBuffer.cpp`'s `materializeImageDescriptor` (shader sampling) and
// `ImageOps.cpp`'s `runBlitImage` (blit source) both need to decode one
// `VK_FORMAT_ETC2_*`/`VK_FORMAT_EAC_*` block into whichever
// already-runtime-supported `feme::cpu::ResourceFormat` matches that
// format's own decoded channel count/precision -- factored out here for
// the same "both call sites need the identical answer" reason
// `BCSamplingBridge.h`'s own file comment gives.
//
// Like BC, ETC2/EAC's decoders in `ETC2Decode.h` produce a different
// output shape per sub-family, not one shared shape the way ASTC's own
// `decodeASTCBlock` does: `decodeETC2Block`/`decodeETC2PunchthroughAlphaBlock`
// decode to RGBA8 (opaque or punchthrough-alpha respectively), a
// `VK_FORMAT_ETC2_R8G8B8A8_*` block composes one `decodeETC2Block` call
// (its own RGB half) with one `decodeEACBlock` call (its own separate
// alpha half, per the specification's own "the alpha part is encoded
// separately" design -- the alpha half is stored first in memory, the RGB
// half second), `decodeEACBlock` alone decodes a single 11-bit channel
// (targeting `R16_UNORM`/`_SNORM`, roadmap H8j's own new
// `feme::graphics::packClearColor`/`unpackColor` cases), and a
// `VK_FORMAT_EAC_R11G11_*` block composes two independent `decodeEACBlock`
// calls, one per 64-bit half (R first, G second, targeting
// `R16G16_UNORM`/`_SNORM`) -- `etc2SamplingTarget` below is what maps each
// of the 10 formats to its own target, and `decodeETC2FormatBlock` is what
// actually drives the right combination of `ETC2Decode.h`'s three decode
// functions.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_ETC2SAMPLINGBRIDGE_H
#define FEME_LIB_VULKAN_ETC2SAMPLINGBRIDGE_H

#include "feme/Target/CPU/RuntimeABI.h"

#include <cstdint>

namespace feme::vulkan {

/// The already-runtime-supported `feme::cpu::ResourceFormat` and per-texel
/// byte size \p Format's own blocks should be decoded into for shader
/// sampling or a blit source -- see this file's own header comment for why
/// each ETC2/EAC sub-family needs a different target rather than sharing
/// one the way every ASTC format does.
struct ETC2SamplingTarget {
  feme::cpu::ResourceFormat Format;
  uint32_t BytesPerTexel;
};

/// \p Format must be one of the 10 `VK_FORMAT_ETC2_*`/`VK_FORMAT_EAC_*`-
/// mapped `ResourceFormat` values (`feme::cpu::isETC2Format(Format)`).
ETC2SamplingTarget etc2SamplingTarget(feme::cpu::ResourceFormat Format);

/// Decodes one ETC2/EAC-format 4x4 texel block at \p Block into
/// \p Output, which must have room for
/// `16 * etc2SamplingTarget(Format).BytesPerTexel` bytes. \p Format must
/// be one of the 10 `VK_FORMAT_ETC2_*`/`VK_FORMAT_EAC_*`-mapped
/// `ResourceFormat` values.
void decodeETC2FormatBlock(feme::cpu::ResourceFormat Format,
                           const uint8_t *Block, uint8_t *Output);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_ETC2SAMPLINGBRIDGE_H
