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

class Image;

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
