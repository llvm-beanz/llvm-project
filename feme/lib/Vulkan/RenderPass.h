//===- RenderPass.h - VkRenderPass/VkFramebuffer and render targets -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (V6) The render-target object model (see "Render passes and dynamic
// rendering" in feme/docs/FeMeVulkanDesign.md): "The ICD normalizes both
// into one internal *render-target binding*: an ordered attachment list with
// format, sample count, load/store or resolve behavior, clear value, and
// read-only-ness, plus the render area."
//
// `VkRenderPass` is compiled at creation time into a sequence of
// `SubpassDescription`s -- one per subpass -- which, combined with a
// `Framebuffer`'s image views at `vkCmdBeginRenderPass` time, produce a
// `RenderTargetBinding`. `vkCmdBeginRendering` builds the same
// `RenderTargetBinding` directly, so nothing downstream of this file (the
// draw path in CommandBuffer.cpp) knows which of the two entry points was
// used.
//
// Every format/sample-count combination a render pass names is validated
// here, at creation time: "a layout the driver cannot honor fails at
// render-pass creation, not at draw time".
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_RENDERPASS_H
#define FEME_LIB_VULKAN_RENDERPASS_H

#include "feme/Graphics/PreparedDraw.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace feme::vulkan {

class ImageView;

/// One `VkAttachmentDescription`, normalized: the `feme::cpu::ResourceFormat`
/// its `VkFormat` maps to plus the load/store behavior and sample count a
/// render-target binding needs.
struct AttachmentDescription {
  feme::cpu::ResourceFormat Format = feme::cpu::ResourceFormat::Unknown;
  uint32_t SampleCount = 1;
  VkAttachmentLoadOp LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  VkAttachmentStoreOp StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  VkAttachmentLoadOp StencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  VkAttachmentStoreOp StencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  VkImageLayout InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageLayout FinalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

/// One compiled `VkSubpassDescription`: indices into the render pass's own
/// attachment list. `VK_ATTACHMENT_UNUSED` keeps its Vulkan meaning.
struct SubpassDescription {
  std::vector<uint32_t> ColorAttachments;
  /// Empty, or exactly as long as `ColorAttachments`; an entry may be
  /// `VK_ATTACHMENT_UNUSED` for a color attachment that is not resolved.
  std::vector<uint32_t> ResolveAttachments;
  uint32_t DepthStencilAttachment = VK_ATTACHMENT_UNUSED;
};

/// A `VkRenderPass`: its attachment descriptions plus one compiled
/// `SubpassDescription` per subpass. Subpass *dependencies* carry no payload
/// here for the same reason `vkCmdPipelineBarrier` does not (see
/// CommandBuffer.h's `pipelineBarrier` comment): this ICD executes every
/// command to completion in record order, so each subpass boundary's join is
/// already satisfied by construction.
class RenderPass {
public:
  RenderPass(std::vector<AttachmentDescription> Attachments,
             std::vector<SubpassDescription> Subpasses)
      : Attachments(std::move(Attachments)), Subpasses(std::move(Subpasses)) {}

  llvm::ArrayRef<AttachmentDescription> attachments() const {
    return Attachments;
  }
  llvm::ArrayRef<SubpassDescription> subpasses() const { return Subpasses; }

private:
  std::vector<AttachmentDescription> Attachments;
  std::vector<SubpassDescription> Subpasses;
};

/// A `VkFramebuffer`: the image views a render pass's attachments resolve to,
/// plus the extent they were created against.
class Framebuffer {
public:
  Framebuffer(std::vector<ImageView *> Attachments, uint32_t Width,
              uint32_t Height, uint32_t Layers)
      : Attachments(std::move(Attachments)), Width(Width), Height(Height),
        Layers(Layers) {}

  llvm::ArrayRef<ImageView *> attachments() const { return Attachments; }
  uint32_t width() const { return Width; }
  uint32_t height() const { return Height; }
  uint32_t layers() const { return Layers; }

private:
  std::vector<ImageView *> Attachments;
  uint32_t Width;
  uint32_t Height;
  uint32_t Layers;
};

/// One attachment of a `RenderTargetBinding`: the view it renders into plus
/// the behavior the render pass (or `VkRenderingAttachmentInfo`) declared
/// for it.
struct RenderTargetView {
  ImageView *View = nullptr;
  feme::cpu::ResourceFormat Format = feme::cpu::ResourceFormat::Unknown;
  uint32_t SampleCount = 1;
  VkAttachmentLoadOp LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  VkAttachmentStoreOp StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  VkClearValue ClearValue{};
  /// The single-sample attachment this one resolves into once the render
  /// pass/rendering ends, or null (the common single-sample case).
  ImageView *ResolveView = nullptr;
};

/// The single internal render-target binding both `VkRenderPass` and
/// `vkCmdBeginRendering` normalize into -- see the file comment above. Built
/// at `vkCmdBeginRenderPass`/`vkCmdBeginRendering` execution time and
/// consumed by every draw recorded inside it.
struct RenderTargetBinding {
  std::vector<RenderTargetView> Colors;
  /// The depth and stencil attachments, at most one of which is bound: this
  /// milestone's depth/stencil support is `feme::graphics`' own two separate
  /// single-component images (`D16_UNORM`/`D32_FLOAT` and `S8_UINT`), never a
  /// packed `D24_UNORM_S8_UINT` surface -- see `PreparedDraw.h`'s
  /// `DepthStencilAttachment` and the V6 status note in
  /// feme/docs/FeMeVulkanDesign.md.
  std::optional<RenderTargetView> Depth;
  std::optional<RenderTargetView> Stencil;
  VkRect2D RenderArea{};
  uint32_t Layers = 1;
};

/// One attachment's linear host storage, as the software graphics executor
/// addresses it: the mip level's own tightly packed `width * height *
/// samples` texels. `Image`'s packed subresource layout is exactly the
/// layout `feme::graphics::AttachmentView` assumes, so this is a pointer
/// and an extent, never a copy. Fails for a view this driver cannot render
/// into (an unbound image, a non-2D view, or a layered one -- layered
/// rendering is V7).
llvm::Expected<feme::graphics::AttachmentView>
resolveAttachmentView(ImageView *View);

/// Whether \p Format may back a color attachment: the executor's own
/// supported color format subset (see "Texture layout and formats" in
/// feme/docs/FeMeGraphicsDesign.md and `feme::graphics::packClearColor`).
bool isSupportedColorAttachmentFormat(feme::cpu::ResourceFormat Format);

/// Whether \p Format may back the depth half of a depth/stencil attachment
/// (`D16_UNORM`/`D32_FLOAT`).
bool isSupportedDepthAttachmentFormat(feme::cpu::ResourceFormat Format);

/// Whether \p Format may back the stencil half of a depth/stencil attachment
/// (`S8_UINT`).
bool isSupportedStencilAttachmentFormat(feme::cpu::ResourceFormat Format);

/// Whether \p SampleCount is one the executor rasterizes (1, 2, or 4 --
/// roadmap R33).
bool isSupportedAttachmentSampleCount(uint32_t SampleCount);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_RENDERPASS_H
