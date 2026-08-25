//===- ImageOps.h - Attachment clears, blits, and resolves -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (V6) The image operations "Draw commands and vertex data" adds alongside
// draws: "Clear attachments, blit, and resolve image", plus the two
// whole-image clears (`vkCmdClearColorImage`/`vkCmdClearDepthStencilImage`)
// that are the outside-a-render-pass form of the same thing.
//
// Every one of these is texel arithmetic over the packed subresource layout
// `Image` already owns, expressed through the same central format table the
// executor's own pack/unpack uses -- a format combination that table cannot
// express fails the command rather than writing a wrong value.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_IMAGEOPS_H
#define FEME_LIB_VULKAN_IMAGEOPS_H

#include "RenderPass.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <vulkan/vulkan_core.h>

namespace feme::vulkan {

class Buffer;
class Image;

/// (V5) One `VkBufferImageCopy`-shaped region's byte copy between \p Img's
/// own packed subresource layout and a flat host memory region starting at
/// \p BufferBase, in either direction (\p ToImage selects which).
/// \p BufferSize bounds-checks every row this copies against -- a bound
/// `VkBuffer`'s own size for `runCopyBufferToImage`/`runCopyImageToBuffer`
/// below, or (roadmap F11's `HostImageCopy.cpp`, which has no `VkBuffer` to
/// query a size from at all) `UINT64_MAX` so a host-side copy is never
/// spuriously rejected -- the application, not this ICD, owns the
/// guarantee that a raw host pointer has enough space, exactly as real
/// Vulkan's own host-image-copy VUIDs place that obligation on the caller
/// rather than the driver. `bufferRowLength`/`bufferImageHeight` of 0 mean
/// "tightly packed to the copy's own extent", per the specification.
/// Copies row by row rather than as one contiguous `memcpy`, since the
/// image's row/slice pitch need not match the buffer's (a non-zero
/// `bufferRowLength`/`bufferImageHeight`, or simply a mip level narrower
/// than level 0, both make them differ).
///
/// Roadmap E22: for a block-compressed `Img`, a "row" is a row of whole
/// blocks rather than of texels -- `UnitSize` (`bytesPerBlock`, Format.h)
/// stands in for a texel's `formatElementSize` either way (the two are
/// equal for a non-block-compressed format, so this generalizes rather
/// than branches: `blockWidth`/`blockHeight` fall back to `{1, 1}`, making
/// every `.../BlockWidth`-shaped ceiling division below a no-op), and
/// `Img.blockPointer` addresses each row's first block instead of
/// `texelPointer`'s first texel. Real Vulkan requires a block-compressed
/// copy's own offset/extent to already be block-aligned
/// (`VUID-vkCmdCopyBufferToImage-imageOffset-07738`'s family); this ICD
/// does not re-validate that any more than it validates other VUIDs (see
/// Image.h's file comment on that precedent), so the ceiling division here
/// is exact for spec-conformant input and only "generously" rounds up an
/// out-of-spec one rather than under-copying it.
llvm::Error copyBufferImageRegion(Image &Img, bool ToImage, void *BufferBase,
                                  VkDeviceSize BufferSize,
                                  const VkBufferImageCopy &Region);

/// `vkCmdCopyBufferToImage`.
llvm::Error runCopyBufferToImage(Buffer *Src, Image *Dst,
                                 llvm::ArrayRef<VkBufferImageCopy> Regions);

/// `vkCmdCopyImageToBuffer`.
llvm::Error runCopyImageToBuffer(Image *Src, Buffer *Dst,
                                 llvm::ArrayRef<VkBufferImageCopy> Regions);

/// `vkCmdCopyImage`: copies each region's texels from \p Src to \p Dst.
/// Both images must share the same texel/block size and sample count,
/// matching real Vulkan's own "compatible formats" copy rule
/// (`VUID-vkCmdCopyImage-srcImage-01548`): no value conversion takes place
/// on either side, in this ICD or a real one -- `vkCmdCopyImage` reinterprets
/// bits, it never converts them (that is what a shader's format-aware
/// load/store or a blit does). Every sample of a multisample region is
/// copied verbatim; there is no resolve here either (that is
/// `runResolveImage` below). Shared (roadmap F11) by the command-buffer-
/// recorded `vkCmdCopyImage`/`vkCmdCopyImage2` and the host-side, no-
/// command-buffer `vkCopyImageToImage` (`HostImageCopy.cpp`), which copies
/// between two already-bound images exactly the same way, just without an
/// executor to queue the copy through.
llvm::Error runCopyImage(Image *Src, Image *Dst,
                         llvm::ArrayRef<VkImageCopy> Regions);

/// `vkCmdClearColorImage`: fills every texel of every named subresource
/// with \p Color, converted into each subresource's own format.
llvm::Error runClearColorImage(Image *Img, const VkClearColorValue &Color,
                               llvm::ArrayRef<VkImageSubresourceRange> Ranges);

/// `vkCmdClearDepthStencilImage`: the depth/stencil peer of
/// `runClearColorImage`, writing whichever of the two \p Img's format holds
/// (depth and stencil are separate images here -- see RenderPass.h).
llvm::Error
runClearDepthStencilImage(Image *Img, const VkClearDepthStencilValue &Value,
                          llvm::ArrayRef<VkImageSubresourceRange> Ranges);

/// `vkCmdClearAttachments`: clears the named attachments of the render pass
/// instance currently in flight, restricted to \p Rects.
llvm::Error runClearAttachments(const RenderTargetBinding &Binding,
                                llvm::ArrayRef<VkClearAttachment> Attachments,
                                llvm::ArrayRef<VkClearRect> Rects);

/// `vkCmdBlitImage`: a scaled copy between two single-sample images of the
/// same format, with `VK_FILTER_NEAREST` or `VK_FILTER_LINEAR` sampling of
/// the source region.
llvm::Error runBlitImage(Image *Src, Image *Dst,
                         llvm::ArrayRef<VkImageBlit> Regions, VkFilter Filter);

/// `vkCmdResolveImage`: averages a multisample image's samples into a
/// single-sample destination of the same format, the same box filter the
/// executor's own resolve-attachment path uses.
llvm::Error runResolveImage(Image *Src, Image *Dst,
                            llvm::ArrayRef<VkImageResolve> Regions);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_IMAGEOPS_H
