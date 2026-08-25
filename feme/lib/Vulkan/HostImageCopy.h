//===- HostImageCopy.h - Command-buffer-free image copy/transition ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap F11: `VK_EXT_host_image_copy`/`hostImageCopy` (promoted to core
// Vulkan 1.4), the largest new mechanism in the F series -- a whole family
// of commands that move image data or transition an image's layout
// entirely on the host, with no `VkCommandBuffer`/`vkQueueSubmit` involved
// at all:
//
//  - `vkCopyMemoryToImage`/`vkCopyImageToMemory` are this mechanism's own
//    peer of `vkCmdCopyBufferToImage`/`vkCmdCopyImageToBuffer`, except the
//    "buffer" side is a raw host pointer (`VkMemoryToImageCopy::
//    pHostPointer`/`VkImageToMemoryCopy::pHostPointer`) with no `VkBuffer`
//    object or bound size of its own -- reusing `ImageOps.h`'s
//    `copyBufferImageRegion` directly, since that helper already took a
//    `(void *, VkDeviceSize)` region rather than a `Buffer&`.
//  - `vkCopyImageToImage` is `vkCmdCopyImage`/`vkCmdCopyImage2` run
//    immediately rather than recorded, reusing `ImageOps.h`'s
//    `runCopyImage` directly (an `Image*`/`Image*` copy needs no
//    `CommandBuffer`/executor to begin with).
//  - `vkTransitionImageLayout` is one or more `VkImageMemoryBarrier`-shaped
//    layout transitions applied immediately, reusing `Image::setLayout`
//    directly (`CommandBuffer.cpp`'s own `vkCmdPipelineBarrier` applies the
//    identical call at `executeCommandBuffer` time).
//
// None of these four commands re-validates a VUID this ICD's own recorded
// counterparts don't already validate either (see Image.h's file comment):
// a copy's declared `srcImageLayout`/`dstImageLayout` is *never* consulted
// by the byte-for-byte copy underneath it, only reported back through
// `getSupportedHostImageCopySrcLayouts`/`DstLayouts` below for
// `VkPhysicalDeviceHostImageCopyProperties`'s own `pCopySrcLayouts`/
// `pCopyDstLayouts` list -- exactly the same "validation layers, not this
// ICD, own precondition checking" precedent every other command here
// follows.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_HOSTIMAGECOPY_H
#define FEME_LIB_VULKAN_HOSTIMAGECOPY_H

#include "llvm/ADT/ArrayRef.h"

#include <vulkan/vulkan_core.h>

namespace feme::vulkan {

/// `VkPhysicalDeviceHostImageCopyProperties::pCopySrcLayouts`: the
/// `VkImageLayout`s `vkCopyImageToMemory`/`vkCopyImageToImage` accept as a
/// source image's declared layout. `VK_IMAGE_LAYOUT_GENERAL` is the one the
/// spec always requires; `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` is this
/// list's other, natural member for a mechanism that is fundamentally a
/// copy. Since `VK_IMAGE_TILING_OPTIMAL`/`_LINEAR` already resolve to one
/// identical packed layout on this software rasterizer (Image.h's own file
/// comment) and this copy never actually branches on the declared layout
/// at all (see this file's own comment), a real driver-level restriction
/// does not exist here to shrink this list further -- these two are the
/// conservative, spec-minimum choice rather than every defined
/// `VkImageLayout`, matching this codebase's own "claim only what has been
/// deliberately scoped, not everything that happens to already work"
/// convention elsewhere (e.g. `defaultRobustness*`, EntryPoints.cpp).
llvm::ArrayRef<VkImageLayout> getSupportedHostImageCopySrcLayouts();

/// `VkPhysicalDeviceHostImageCopyProperties::pCopyDstLayouts`: the same
/// list for a destination image's declared layout, using
/// `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` in `VK_IMAGE_LAYOUT_TRANSFER_
/// SRC_OPTIMAL`'s place -- see `getSupportedHostImageCopySrcLayouts`'s own
/// comment.
llvm::ArrayRef<VkImageLayout> getSupportedHostImageCopyDstLayouts();

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_HOSTIMAGECOPY_H
