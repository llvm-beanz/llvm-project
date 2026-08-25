//===- HostImageCopy.cpp - Command-buffer-free image copy/transition ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HostImageCopy.h"
#include "Icd.h"
#include "Image.h"
#include "ImageOps.h"

#include "llvm/Support/Error.h"

#include <limits>
#include <vector>

using namespace feme::vulkan;
using namespace llvm;

namespace feme::vulkan {

ArrayRef<VkImageLayout> getSupportedHostImageCopySrcLayouts() {
  static constexpr VkImageLayout Layouts[] = {
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  };
  return Layouts;
}

ArrayRef<VkImageLayout> getSupportedHostImageCopyDstLayouts() {
  static constexpr VkImageLayout Layouts[] = {
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
  };
  return Layouts;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToImage(
    VkDevice, const VkCopyMemoryToImageInfo *pCopyMemoryToImageInfo) {
  Image *Dst = fromHandle<Image>(pCopyMemoryToImageInfo->dstImage);
  if (!Dst || !Dst->isBound())
    return VK_ERROR_INITIALIZATION_FAILED;
  for (uint32_t I = 0; I != pCopyMemoryToImageInfo->regionCount; ++I) {
    const VkMemoryToImageCopy &R = pCopyMemoryToImageInfo->pRegions[I];
    VkBufferImageCopy Region{
        /*bufferOffset=*/0,  R.memoryRowLength,
        R.memoryImageHeight, R.imageSubresource,
        R.imageOffset,       R.imageExtent};
    // A raw host pointer, unlike a bound `VkBuffer`, has no size of its own
    // to bounds-check against -- the application, not this ICD, owns the
    // guarantee that `pHostPointer` has enough room (see HostImageCopy.h's
    // file comment and `copyBufferImageRegion`'s own).
    if (Error E = copyBufferImageRegion(
            *Dst, /*ToImage=*/true, const_cast<void *>(R.pHostPointer),
            std::numeric_limits<VkDeviceSize>::max(), Region)) {
      consumeError(std::move(E));
      return VK_ERROR_INITIALIZATION_FAILED;
    }
  }
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToMemory(
    VkDevice, const VkCopyImageToMemoryInfo *pCopyImageToMemoryInfo) {
  Image *Src = fromHandle<Image>(pCopyImageToMemoryInfo->srcImage);
  if (!Src || !Src->isBound())
    return VK_ERROR_INITIALIZATION_FAILED;
  for (uint32_t I = 0; I != pCopyImageToMemoryInfo->regionCount; ++I) {
    const VkImageToMemoryCopy &R = pCopyImageToMemoryInfo->pRegions[I];
    VkBufferImageCopy Region{
        /*bufferOffset=*/0,  R.memoryRowLength,
        R.memoryImageHeight, R.imageSubresource,
        R.imageOffset,       R.imageExtent};
    if (Error E = copyBufferImageRegion(
            *Src, /*ToImage=*/false, R.pHostPointer,
            std::numeric_limits<VkDeviceSize>::max(), Region)) {
      consumeError(std::move(E));
      return VK_ERROR_INITIALIZATION_FAILED;
    }
  }
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToImage(
    VkDevice, const VkCopyImageToImageInfo *pCopyImageToImageInfo) {
  // `VkImageCopy2` carries the same fields as `VkImageCopy`, prefixed by
  // `sType`/`pNext` -- the identical conversion `vkCmdCopyImage2`
  // (CommandBuffer.cpp) already does for its own recorded regions.
  std::vector<VkImageCopy> Regions;
  Regions.reserve(pCopyImageToImageInfo->regionCount);
  for (uint32_t I = 0; I != pCopyImageToImageInfo->regionCount; ++I) {
    const VkImageCopy2 &R = pCopyImageToImageInfo->pRegions[I];
    Regions.push_back(VkImageCopy{R.srcSubresource, R.srcOffset,
                                  R.dstSubresource, R.dstOffset, R.extent});
  }
  if (Error E = runCopyImage(fromHandle<Image>(pCopyImageToImageInfo->srcImage),
                             fromHandle<Image>(pCopyImageToImageInfo->dstImage),
                             Regions)) {
    consumeError(std::move(E));
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkTransitionImageLayout(VkDevice, uint32_t transitionCount,
                        const VkHostImageLayoutTransitionInfo *pTransitions) {
  // Applies each transition's own new layout immediately, the same
  // `Image::setLayout` call `executeCommandBuffer`'s `PipelineBarrier` case
  // (CommandBuffer.cpp) makes for a recorded `VkImageMemoryBarrier` -- see
  // that case's own comment for why `oldLayout` is never consulted either
  // way (this ICD trusts, rather than re-validates, a transition's claimed
  // starting layout).
  for (uint32_t I = 0; I != transitionCount; ++I) {
    const VkHostImageLayoutTransitionInfo &T = pTransitions[I];
    fromHandle<Image>(T.image)->setLayout(
        T.subresourceRange.baseMipLevel, T.subresourceRange.levelCount,
        T.subresourceRange.baseArrayLayer, T.subresourceRange.layerCount,
        T.newLayout);
  }
  return VK_SUCCESS;
}

} // namespace feme::vulkan
