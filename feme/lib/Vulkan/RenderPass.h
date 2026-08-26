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
  /// Input attachments are retained in the object model even though shader-
  /// side `subpassInput` consumption is still a separate follow-up: a
  /// `VkRenderPass` must not reject the mandatory attachment references it can
  /// already validate and carry forward.
  std::vector<uint32_t> InputAttachments;
  std::vector<uint32_t> ColorAttachments;
  /// Empty, or exactly as long as `ColorAttachments`; an entry may be
  /// `VK_ATTACHMENT_UNUSED` for a color attachment that is not resolved.
  std::vector<uint32_t> ResolveAttachments;
  uint32_t DepthStencilAttachment = VK_ATTACHMENT_UNUSED;
  /// (Roadmap H2) `VkRenderPassCreateInfo2::pSubpasses[i].viewMask`, or the
  /// classic `vkCreateRenderPass`'s own `VkRenderPassMultiviewCreateInfo::
  /// pViewMasks[i]` when chained -- 0 for a non-multiview subpass. Each set
  /// bit `V` is one view this subpass's every draw runs once for, writing
  /// array layer `V` of every attachment (`CommandBuffer.cpp`'s `runDraw`),
  /// unless a stage explicitly writes `RenderTargetArrayIndex` (not yet
  /// reachable without a geometry stage or `shaderOutputLayer` -- roadmap
  /// H3/H5).
  uint32_t ViewMask = 0;
};

/// A `VkRenderPass`: its attachment descriptions plus one compiled
/// `SubpassDescription` per subpass. Subpass *dependencies* carry no payload
/// here for the same reason `vkCmdPipelineBarrier` does not (see
/// CommandBuffer.h's `pipelineBarrier` comment): this ICD executes every
/// command to completion in record order, so each subpass boundary's join is
/// already satisfied by construction. They are still validated at creation
/// time, though, so out-of-range subpass references do not slip through
/// silently. (Roadmap H2) A view-local dependency
/// (`VK_DEPENDENCY_VIEW_LOCAL_BIT`) is accepted for the same reason: the
/// per-view join it describes is also already satisfied by this ICD's
/// strictly sequential execution.
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
/// plus the extent they were created against. (Roadmap C6) A framebuffer
/// created with `VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT` defers its attachment
/// views to `vkCmdBeginRenderPass` time (`VkRenderPassAttachmentBeginInfo`)
/// instead: `Attachments` is empty and `isImageless()` is true. (Roadmap H2)
/// `layers() > 1` (layered rendering/multiview) is accepted either way: an
/// imageless framebuffer validates its per-instance views' `layerCount`
/// against it the same way `isCompatibleAttachmentView` validates a
/// concrete one's at creation time.
class Framebuffer {
public:
  Framebuffer(std::vector<ImageView *> Attachments, uint32_t Width,
              uint32_t Height, uint32_t Layers, bool Imageless = false)
      : Attachments(std::move(Attachments)), Width(Width), Height(Height),
        Layers(Layers), Imageless(Imageless) {}

  llvm::ArrayRef<ImageView *> attachments() const { return Attachments; }
  uint32_t width() const { return Width; }
  uint32_t height() const { return Height; }
  uint32_t layers() const { return Layers; }
  bool isImageless() const { return Imageless; }

private:
  std::vector<ImageView *> Attachments;
  uint32_t Width;
  uint32_t Height;
  uint32_t Layers;
  bool Imageless;
};

/// One attachment of a `RenderTargetBinding`: the view it renders into plus
/// the behavior the render pass (or `VkRenderingAttachmentInfo`) declared
/// for it.
struct RenderTargetView {
  ImageView *View = nullptr;
  feme::cpu::ResourceFormat Format = feme::cpu::ResourceFormat::Unknown;
  uint32_t SampleCount = 1;
  VkAttachmentLoadOp LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  /// (Roadmap F13) Carried for API completeness only: this is a
  /// real-memory-backed software renderer with no discard-on-store
  /// optimization to skip, so `STORE`/`DONT_CARE`/`NONE`
  /// (`VK_KHR_load_store_op_none`) are all equivalent -- whatever a draw or
  /// `applyClear` wrote is simply left in the attachment's memory.
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
  /// (Roadmap H2h) The current subpass's own input attachments (`Vk
  /// RenderPass`'s `SubpassDescription::InputAttachments`), resolved to the
  /// framebuffer's image views: `Inputs[J]` is exactly the view a shader's
  /// `layout(input_attachment_index = J, ...)` `subpassInput` names, which
  /// may be a *different* attachment than any of this subpass's own
  /// `Colors`/`Depth`/`Stencil` (e.g. a later subpass reading back an
  /// earlier subpass's color output). Null for an unused slot
  /// (`VK_ATTACHMENT_UNUSED`). Always empty for a `vkCmdBeginRendering`
  /// instance, which has no classic input-attachment list of its own --
  /// `VK_KHR_dynamic_rendering_local_read` instead maps `Colors` itself
  /// through `GraphicsState::ColorAttachmentInputIndices`.
  std::vector<ImageView *> Inputs;
  /// The depth and stencil attachments. For a pure depth (`D16_UNORM`/
  /// `D32_FLOAT`) or pure stencil (`S8_UINT`) format, at most one of these
  /// is bound and each owns its own image. For a combined format
  /// (`D24_UNORM_S8_UINT`, roadmap C1), both are bound and share the same
  /// underlying `ImageView`/storage, distinguished only by which of
  /// `AttachmentDescription::{LoadOp,StoreOp}` (depth) or
  /// `{StencilLoadOp,StencilStoreOp}` (stencil) each one carries; see
  /// `feme::graphics::packDepthClear`/`packStencilClear` for how the two
  /// halves of that shared word are written independently.
  std::optional<RenderTargetView> Depth;
  std::optional<RenderTargetView> Stencil;
  VkRect2D RenderArea{};
  uint32_t Layers = 1;
  /// (Roadmap H2) The multiview view mask this render-pass instance's
  /// current subpass declared (`SubpassDescription::ViewMask`), or
  /// `VkRenderingInfo::viewMask` for `vkCmdBeginRendering`; 0 for a
  /// non-multiview instance. `CommandBuffer.cpp`'s `runDraw` iterates one
  /// draw per set bit, each writing that bit's own attachment array layer.
  uint32_t ViewMask = 0;
};

/// One attachment's linear host storage, as the software graphics executor
/// addresses it: the mip level's own tightly packed `width * height *
/// samples` texels, times however many array layers (Roadmap H2) the
/// backing view covers. `Image`'s packed subresource layout is exactly the
/// layout `feme::graphics::AttachmentView` assumes, so this is a pointer
/// and an extent, never a copy. Fails for a view this driver cannot render
/// into (an unbound image or a non-2D view).
llvm::Expected<feme::graphics::AttachmentView>
resolveAttachmentView(ImageView *View);

/// Whether \p Format may back a color attachment: the executor's own
/// supported color format subset (see "Texture layout and formats" in
/// feme/docs/FeMeGraphicsDesign.md and `feme::graphics::packClearColor`).
bool isSupportedColorAttachmentFormat(feme::cpu::ResourceFormat Format);

/// Whether \p Format may back the depth half of a depth/stencil attachment
/// (`D16_UNORM`/`D32_FLOAT`, or the depth half of the combined
/// `D24_UNORM_S8_UINT` format -- roadmap C1).
bool isSupportedDepthAttachmentFormat(feme::cpu::ResourceFormat Format);

/// Whether \p Format may back the stencil half of a depth/stencil attachment
/// (`S8_UINT`, or the stencil half of the combined `D24_UNORM_S8_UINT`
/// format -- roadmap C1).
bool isSupportedStencilAttachmentFormat(feme::cpu::ResourceFormat Format);

/// Whether \p SampleCount is one the executor rasterizes (1, 2, or 4 --
/// roadmap R33).
bool isSupportedAttachmentSampleCount(uint32_t SampleCount);

/// Whether \p View is a legal binding for \p Attachment sized against a
/// framebuffer's \p Width x \p Height x \p Layers: format and sample count
/// match, and the view's image is bound, at least as large, and (roadmap
/// H2) carries at least \p Layers array layers from its own base array
/// layer. Shared by `vkCreateFramebuffer`'s eager per-attachment validation
/// and, for an imageless framebuffer (roadmap C6,
/// `VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT`), `vkCmdBeginRenderPass`'s
/// deferred one via `VkRenderPassAttachmentBeginInfo` -- an imageless
/// framebuffer cannot validate this at creation, since it has no image
/// views yet.
bool isCompatibleAttachmentView(const AttachmentDescription &Attachment,
                                ImageView *View, uint32_t Width,
                                uint32_t Height, uint32_t Layers = 1);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_RENDERPASS_H
