//===- Image.h - VkImage/VkImageView/VkSampler object model ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The V5 `VkImage`/`VkImageView`/`VkSampler` object model (see "V5: Images
// and sampling" in feme/docs/FeMeVulkanDesign.md): image memory requirements,
// a packed CPU-side subresource layout (no real hardware tiling exists to
// model, so `VK_IMAGE_TILING_LINEAR` and `_OPTIMAL` resolve to the same
// layout -- see `Image`'s own comment), per-subresource `VkImageLayout`
// bookkeeping, and the `feme::cpu::FemeSamplerDescriptor` a `VkSampler`
// translates to.
//
// As with every other object model file here, only what a real application
// needs to avoid misbehaving is validated (bounds, a supported format,
// binding state); a `VkImageMemoryBarrier`'s layout transition is *tracked*
// but a copy or dispatch does not re-validate that a resource's current
// layout is one the operation permits -- the same "validation layers, not
// this ICD, own precondition checking" precedent every other command here
// follows (see e.g. Buffer.h's `bind`/`isBound` comment and CommandBuffer.h's
// `pipelineBarrier` comment on this ICD's single-threaded, strictly
// sequential execution model).
//
// A multisample image (`samples > VK_SAMPLE_COUNT_1_BIT`) is accepted at the
// object-model level -- every sample of a texel is stored contiguously
// (`FemeImageSubresourceLayout::SampleStride`, R29's own ABI, anticipated
// exactly this), and `vkCmdCopyImage` may copy one whole multisample image
// to another of the same sample count. Nothing can *read* an individual
// sample from a shader or resolve one to single-sample, though: there is no
// render-target/rasterizer path (`VK_QUEUE_GRAPHICS_BIT` is V6+) and no
// `OpImageFetch`-with-sample-index raising yet (R30's own remaining scope).
// A multisample image is therefore only ever useful as an opaque copy
// source/destination today, same as a single-sample one before V5's shader
// consumption gap closes.
//
// Roadmap E20 ("Block-compressed image groundwork + ASTC LDR decode")
// generalized `computeSubresourceLayouts`' per-mip math from a per-texel
// stride to a block-based one (`Format.h`'s `blockWidth`/`blockHeight`/
// `bytesPerBlock`, which fall back to `{1, 1, formatElementSize(Format)}`
// for a non-block-compressed format, so the same formula now covers both).
//
// Roadmap E22 ("ASTC LDR copy/sampling pipeline wiring") finishes the rest:
// `blockPointer` addresses a block-compressed image's storage a whole
// block at a time (`texelPointer`, which still addresses one texel and
// still asserts against a block-compressed `Format`, is meaningless for
// one -- see its own comment), `CommandBuffer.cpp`'s `vkCmdCopyImage`/
// `vkCmdCopyBufferToImage`/`vkCmdCopyImageToBuffer` now use it for a
// block-compressed image's bit-for-bit copy, `ImageOps.cpp`'s
// `runBlitImage` decodes an LDR ASTC source through
// `feme::vulkan::decodeASTCBlock` to resample it (no encoder exists, so a
// block-compressed *destination* is still rejected), and `vkCreateImage`
// no longer rejects a block-compressed `VkFormat` outright. This is
// enough to flip `textureCompressionASTC_LDR` (PhysicalDeviceInfo.cpp)
// from `VK_FALSE` to `VK_TRUE` -- but not to make an ASTC image's shader
// *sampling* path decode real data: `CommandBuffer.cpp`'s
// `materializeImageDescriptor` still hands the CPU runtime
// (`feme/runtime/CPU/FeMeRuntimeCPU.c`) a raw `Image::data()` pointer for
// its own per-texel `femeRTFetchTexel2D` to read directly, and that
// runtime's `femeRTImageFormatElementSize` has no block-compressed case
// (returns 0, so a sample safely reads as all-zero rather than
// misinterpreting compressed bytes as uncompressed ones) -- porting a
// decoder into that separate C runtime module, or bridging it back into
// this C++ one, is genuinely separate follow-up work this milestone's own
// file scope (`Image.{h,cpp}`, `ImageOps.{h,cpp}`) never listed.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_IMAGE_H
#define FEME_LIB_VULKAN_IMAGE_H

#include "Memory.h"

#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace feme::vulkan {

/// A `VkImage`. Not dispatchable. Owns the packed subresource layout table
/// (`feme::cpu::FemeImageSubresourceLayout`, one entry per mip level) its
/// bound storage is laid out with, computed once at creation time from the
/// image's dimensions -- see `computeSubresourceLayouts`.
class Image {
public:
  Image(VkImageType Type, feme::cpu::ImageDimension Dimension,
        feme::cpu::ResourceFormat Format, uint32_t Width, uint32_t Height,
        uint32_t Depth, uint32_t MipLevels, uint32_t ArrayLayers,
        uint32_t SampleCount, VkImageUsageFlags Usage);

  VkImageType type() const { return Type; }
  feme::cpu::ImageDimension dimension() const { return Dimension; }
  feme::cpu::ResourceFormat format() const { return Format; }
  uint32_t width() const { return Width; }
  uint32_t height() const { return Height; }
  uint32_t depth() const { return Depth; }
  uint32_t mipLevels() const { return MipLevels; }
  uint32_t arrayLayers() const { return ArrayLayers; }
  /// Samples per texel (`VkSampleCountFlagBits`'s numeric value, e.g. 1, 2,
  /// 4...). Only meaningful as an object-model/copy-source-of-truth today
  /// -- no shader or render-target path consumes a sample index yet (see
  /// this file's own comment) -- but `vkCmdCopyImage` needs it to require a
  /// matching sample count between its two images, per real Vulkan.
  uint32_t sampleCount() const { return SampleCount; }
  VkImageUsageFlags usage() const { return Usage; }

  /// The total packed byte size every mip level/array layer/depth slice of
  /// this image occupies, i.e. what `vkGetImageMemoryRequirements` reports.
  VkDeviceSize sizeInBytes() const { return TotalSize; }

  llvm::ArrayRef<feme::cpu::FemeImageSubresourceLayout> mipLayouts() const {
    return MipLayouts;
  }

  /// Records the `(VkDeviceMemory, offset)` pair `vkBindImageMemory` binds
  /// this image to. Must be called exactly once, mirroring `Buffer::bind`.
  void bind(DeviceMemory *Memory, VkDeviceSize Offset) {
    BoundMemory = Memory;
    BoundOffset = Offset;
  }

  bool isBound() const { return BoundMemory != nullptr; }

  /// The image's base data pointer, or null if unbound.
  void *data() const {
    if (!BoundMemory)
      return nullptr;
    return static_cast<uint8_t *>(BoundMemory->data()) + BoundOffset;
  }

  /// A pointer to sample \p Sample (0 for a single-sample image) of texel
  /// `(X, Y, Z)` of mip level \p MipLevel, array layer \p ArrayLayer (0 for
  /// a non-array, non-3D image). `Z` and `ArrayLayer` are never both
  /// nonzero for a supported dimension (a 3D image has exactly one array
  /// layer, and an array image has depth 1), so their sum is always the
  /// correct slice index into `MipLayouts[MipLevel]`'s `SlicePitch`-derived
  /// stride. Null if the image is unbound.
  void *texelPointer(uint32_t MipLevel, uint32_t ArrayLayer, uint32_t X,
                     uint32_t Y, uint32_t Z, uint32_t Sample = 0) const;

  /// A pointer to the whole compressed block at block-grid coordinates
  /// (\p BlockX, \p BlockY) -- *not* texel coordinates, unlike
  /// `texelPointer`; a caller divides by `blockWidth`/`blockHeight`
  /// (Format.h) itself -- covering array layer/depth slice \p ArrayLayer/
  /// \p Z of mip level \p MipLevel, for a block-compressed `Format`
  /// (roadmap E22: `CommandBuffer.cpp`'s `vkCmdCopyImage`/
  /// `vkCmdCopyBufferToImage`/`vkCmdCopyImageToBuffer` and `ImageOps.cpp`'s
  /// `runBlitImage` are its only callers, since no other operation
  /// supports a block-compressed image at all). Null if the image is
  /// unbound.
  void *blockPointer(uint32_t MipLevel, uint32_t ArrayLayer, uint32_t BlockX,
                     uint32_t BlockY, uint32_t Z) const;

  /// The current `VkImageLayout` of subresource (\p MipLevel, \p ArrayLayer),
  /// or `VK_IMAGE_LAYOUT_UNDEFINED` if never transitioned.
  VkImageLayout layout(uint32_t MipLevel, uint32_t ArrayLayer) const;

  /// Applies a `VkImageMemoryBarrier`'s layout transition to every
  /// subresource in `[BaseMip, BaseMip+MipCount) x [BaseLayer,
  /// BaseLayer+LayerCount)`, per `vkCmdPipelineBarrier`.
  void setLayout(uint32_t BaseMip, uint32_t MipCount, uint32_t BaseLayer,
                 uint32_t LayerCount, VkImageLayout NewLayout);

private:
  VkImageType Type;
  feme::cpu::ImageDimension Dimension;
  feme::cpu::ResourceFormat Format;
  uint32_t Width;
  uint32_t Height;
  uint32_t Depth;
  uint32_t MipLevels;
  uint32_t ArrayLayers;
  uint32_t SampleCount;
  VkImageUsageFlags Usage;

  std::vector<feme::cpu::FemeImageSubresourceLayout> MipLayouts;
  VkDeviceSize TotalSize = 0;

  DeviceMemory *BoundMemory = nullptr;
  VkDeviceSize BoundOffset = 0;

  /// Flat `[MipLevel * SlicesPerLevel + Slice]` tracked layout, where
  /// `SlicesPerLevel` is `max(ArrayLayers, Depth)` -- see `texelPointer`'s
  /// comment for why a single slice index always suffices.
  std::vector<VkImageLayout> Layouts;
};

/// A `VkImageView`: an `Image` plus a view type, format, and subresource
/// range. Not dispatchable.
class ImageView {
public:
  ImageView(Image *Img, VkImageViewType ViewType,
            feme::cpu::ResourceFormat Format,
            const VkImageSubresourceRange &Range)
      : Img(Img), ViewType(ViewType), Format(Format), Range(Range) {}

  Image *image() const { return Img; }
  VkImageViewType viewType() const { return ViewType; }
  feme::cpu::ResourceFormat format() const { return Format; }
  const VkImageSubresourceRange &range() const { return Range; }

  /// The `feme::cpu::ImageDimension` this view's `VkImageViewType`
  /// corresponds to.
  feme::cpu::ImageDimension dimension() const;

private:
  Image *Img;
  VkImageViewType ViewType;
  feme::cpu::ResourceFormat Format;
  VkImageSubresourceRange Range;
};

/// A `VkSampler`: pure filtering/addressing state translated once, at
/// creation time, into a `feme::cpu::FemeSamplerDescriptor`. Not
/// dispatchable.
class Sampler {
public:
  explicit Sampler(const VkSamplerCreateInfo &CreateInfo);

  const feme::cpu::FemeSamplerDescriptor &descriptor() const {
    return Descriptor;
  }

private:
  feme::cpu::FemeSamplerDescriptor Descriptor;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_IMAGE_H
