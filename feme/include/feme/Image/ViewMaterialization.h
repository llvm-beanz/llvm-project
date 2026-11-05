//===- ViewMaterialization.h - API-neutral image-view normalization -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS1(b) ("Move normalized image-view materialization ... out
// of ImageOps.cpp/CommandBuffer.cpp", feme/docs/WARPRoadmap.md): the two
// pieces of `feme::vulkan::CommandBuffer.cpp`'s `materializeImageDescriptor`
// that only ever examine plain image geometry/subresource-range values --
// never a live `Image`/`ImageView` object, a `VkFormat`, or any other
// Vulkan type -- move here. `resolveImageViewExtent` normalizes a view's
// requested (possibly `VK_REMAINING_*`-sentineled) subresource range
// against the physical image it names into the view's own effective
// width/height/depth/mip-level/array-layer counts, exactly the way
// `materializeImageDescriptor` already computed them (the same clamp
// behavior is preserved verbatim, not "fixed", since this is pure code
// motion). `compressedFormatBorderComponentMask` was already a pure
// function of `feme::cpu::ResourceFormat` before this move; it is promoted
// here unchanged.
//
// `feme::vulkan::CommandBuffer.cpp`'s own copies now forward to these
// functions; every existing Vulkan caller and advertised behavior is
// unaffected by this extraction.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_IMAGE_VIEWMATERIALIZATION_H
#define FEME_IMAGE_VIEWMATERIALIZATION_H

#include "feme/Target/CPU/RuntimeABI.h"

#include <cstdint>

namespace feme::image {

/// Passed as `LevelCount`/`LayerCount` to `resolveImageViewExtent` to mean
/// "every remaining mip level"/"every remaining array layer from
/// `BaseArrayLayer`". Shares `VK_REMAINING_MIP_LEVELS`/
/// `VK_REMAINING_ARRAY_LAYERS`'s numeric value (`~0u`) by design, so a
/// Vulkan caller can pass its own `VkImageSubresourceRange` fields through
/// unchanged.
constexpr uint32_t kRemainingImageViewExtent = ~0u;

/// One view's normalized extent, resolved against the physical image it
/// names. `Valid` is false when the requested `BaseArrayLayer`/
/// `BaseMipLevel` themselves lie outside the image, or the resolved
/// level/layer count would be zero; every other field is left at its
/// default (0) in that case, and the caller should treat the whole view as
/// unaddressable (e.g. produce an empty/zero-filled descriptor) rather than
/// clamp further.
struct ImageViewExtent {
  uint32_t Width = 0;
  uint32_t Height = 0;
  uint32_t Depth = 0;
  uint32_t MipLevels = 0;
  uint32_t ArrayLayers = 0;
  bool Valid = false;
};

/// Resolves a view's requested subresource range (\p BaseMipLevel /
/// \p LevelCount / \p BaseArrayLayer / \p LayerCount, the latter two
/// possibly `kRemainingImageViewExtent`) against \p ImageWidth /
/// \p ImageHeight / \p ImageDepth / \p ImageMipLevels / \p ImageArrayLayers
/// -- the physical image's own extent and subresource counts -- into the
/// view's own effective per-mip width/height/depth and resolved mip-level/
/// array-layer counts. \p ViewIsTexture3D selects the one dimension
/// (`Texture3D`) whose resolved `Depth` instead halves per mip level like
/// `Width`/`Height` do, rather than staying `1`.
ImageViewExtent resolveImageViewExtent(uint32_t ImageWidth,
                                       uint32_t ImageHeight,
                                       uint32_t ImageDepth,
                                       uint32_t ImageMipLevels,
                                       uint32_t ImageArrayLayers,
                                       bool ViewIsTexture3D,
                                       uint32_t BaseMipLevel,
                                       uint32_t LevelCount,
                                       uint32_t BaseArrayLayer,
                                       uint32_t LayerCount);

/// The component mask a `CLAMP_TO_BORDER` fallback should use for
/// \p Format's *own* channel count, or `0` ("no override, derive from the
/// descriptor's `Format` field the normal way") for a format the ASTC/BC/
/// ETC2 decode bridges widen to a 4-channel uncompressed target that
/// already matches (or for any non-block-compressed format). Needed
/// because the BC/ETC2 sampling bridges decode several narrower-than-4
/// -channel source families (`BC1_RGB`/`BC6H`/`ETC2_RGB8`'s opaque RGB,
/// `BC4`/`EAC_R11`'s single channel, `BC5`/`EAC_R11G11`'s two channels)
/// into a wider uncompressed storage format purely for texel-fetch
/// convenience (e.g. `R8G8B8A8_UNORM`) -- once decoded, the descriptor's
/// `Format` field alone can no longer tell the CPU runtime's border-color
/// expansion which components the *original* compressed format actually
/// stored, so this mask travels alongside it. ASTC is not covered here:
/// every ASTC LDR format already stores (or synthesizes, for
/// `void-extent`/border-less blocks) a full 4-channel result, so `0` is
/// already correct for it. A format not covered by either switch
/// (including every already-4-channel BC/ETC2 family: `BC1_RGBA`, `BC2`,
/// `BC3`, `BC7`, `ETC2_RGB8A1`, `ETC2_RGBA8`) returns `0` too, since `0xf`
/// is already what the normal `Format`-derived path would compute for
/// their own (4-channel) decode targets.
uint32_t
compressedFormatBorderComponentMask(feme::cpu::ResourceFormat Format);

} // namespace feme::image

#endif // FEME_IMAGE_VIEWMATERIALIZATION_H
