//===- RenderPass.cpp - VkRenderPass/VkFramebuffer ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RenderPass.h"
#include "Format.h"
#include "Icd.h"
#include "Image.h"
#include "Objects.h"

#include <algorithm>

using namespace feme::vulkan;

namespace feme::vulkan {

bool isSupportedColorAttachmentFormat(feme::cpu::ResourceFormat Format) {
  switch (Format) {
  case feme::cpu::ResourceFormat::R32_FLOAT:
  case feme::cpu::ResourceFormat::R32G32_FLOAT:
  case feme::cpu::ResourceFormat::R32G32B32_FLOAT:
  case feme::cpu::ResourceFormat::R32G32B32A32_FLOAT:
  case feme::cpu::ResourceFormat::R8G8B8A8_UNORM:
  case feme::cpu::ResourceFormat::R8G8B8A8_UNORM_SRGB:
  case feme::cpu::ResourceFormat::B8G8R8A8_UNORM:
  case feme::cpu::ResourceFormat::R10G10B10A2_UNORM:
  case feme::cpu::ResourceFormat::R16G16B16A16_FLOAT:
  case feme::cpu::ResourceFormat::R16G16B16A16_UNORM:
  case feme::cpu::ResourceFormat::R16G16B16A16_SNORM:
    // `B8G8R8A8_UNORM`, `R10G10B10A2_UNORM` (`A2B10G10R10_UNORM_PACK32`)
    // and `R16G16B16A16_FLOAT` (`R16G16B16A16_SFLOAT`), together with
    // `R8G8B8A8_UNORM`, are the Vulkan 1.2 mandatory
    // `COLOR_ATTACHMENT_BIT | COLOR_ATTACHMENT_BLEND_BIT` format set that
    // every conformant implementation must support (roadmap C1) --
    // backed by real pack/unpack paths in `feme::graphics`.
    return true;
  default:
    // Every other format is either unknown to the executor's own
    // pack/unpack table (`feme::graphics::packClearColor`) or an integer
    // format no fragment output writes yet; rejecting it here is what
    // "A pipeline whose state combination has no implemented path must
    // also fail at creation" requires.
    return false;
  }
}

bool isSupportedDepthAttachmentFormat(feme::cpu::ResourceFormat Format) {
  return Format == feme::cpu::ResourceFormat::D16_UNORM ||
         Format == feme::cpu::ResourceFormat::D32_FLOAT ||
         Format == feme::cpu::ResourceFormat::D24_UNORM_S8_UINT;
}

bool isSupportedStencilAttachmentFormat(feme::cpu::ResourceFormat Format) {
  return Format == feme::cpu::ResourceFormat::S8_UINT ||
         Format == feme::cpu::ResourceFormat::D24_UNORM_S8_UINT;
}

bool isSupportedAttachmentSampleCount(uint32_t SampleCount) {
  return SampleCount == 1 || SampleCount == 2 || SampleCount == 4;
}

llvm::Expected<feme::graphics::AttachmentView>
resolveAttachmentView(ImageView *View) {
  if (!View || !View->image() || !View->image()->isBound())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "a render target attachment is not bound "
                                   "to memory");
  Image &Img = *View->image();
  const VkImageSubresourceRange &Range = View->range();
  if (View->dimension() != feme::cpu::ImageDimension::Texture2D ||
      Range.baseArrayLayer != 0 ||
      (Range.layerCount != VK_REMAINING_ARRAY_LAYERS && Range.layerCount != 1))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "only a single-layer 2D image view may be "
                                   "a render target (layered rendering is V7)");
  if (Range.baseMipLevel >= Img.mipLevels())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "a render target view's base mip level is "
                                   "out of range");

  uint32_t Level = Range.baseMipLevel;
  const feme::cpu::FemeImageSubresourceLayout &Layout = Img.mipLayouts()[Level];
  feme::graphics::AttachmentView Result;
  Result.Format = View->format();
  Result.Width = std::max(1u, Img.width() >> Level);
  Result.Height = std::max(1u, Img.height() >> Level);
  Result.ArrayLayers = 1;
  auto *Base = static_cast<uint8_t *>(Img.data()) + Layout.Offset;
  Result.Data = llvm::MutableArrayRef<uint8_t>(
      Base, static_cast<size_t>(Layout.SlicePitch));
  return Result;
}

} // namespace feme::vulkan

namespace {

/// Normalizes one `VkAttachmentDescription`, or returns `std::nullopt` for a
/// format/sample count this driver cannot honor -- which fails
/// `vkCreateRenderPass` outright, per "a layout the driver cannot honor
/// fails at render-pass creation, not at draw time".
std::optional<AttachmentDescription>
normalizeAttachment(const VkAttachmentDescription &Src) {
  std::optional<feme::cpu::ResourceFormat> Format = mapVkFormat(Src.format);
  if (!Format)
    return std::nullopt;
  if (!isSupportedColorAttachmentFormat(*Format) &&
      !isSupportedDepthAttachmentFormat(*Format) &&
      !isSupportedStencilAttachmentFormat(*Format))
    return std::nullopt;

  uint32_t SampleCount = static_cast<uint32_t>(Src.samples);
  if (!isSupportedAttachmentSampleCount(SampleCount))
    return std::nullopt;

  AttachmentDescription Result;
  Result.Format = *Format;
  Result.SampleCount = SampleCount;
  Result.LoadOp = Src.loadOp;
  Result.StoreOp = Src.storeOp;
  Result.StencilLoadOp = Src.stencilLoadOp;
  Result.StencilStoreOp = Src.stencilStoreOp;
  Result.InitialLayout = Src.initialLayout;
  Result.FinalLayout = Src.finalLayout;
  return Result;
}

} // namespace

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(
    VkDevice, const VkRenderPassCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
  std::vector<AttachmentDescription> Attachments;
  Attachments.reserve(pCreateInfo->attachmentCount);
  for (uint32_t I = 0; I != pCreateInfo->attachmentCount; ++I) {
    std::optional<AttachmentDescription> Normalized =
        normalizeAttachment(pCreateInfo->pAttachments[I]);
    if (!Normalized)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
    Attachments.push_back(*Normalized);
  }

  std::vector<SubpassDescription> Subpasses;
  Subpasses.reserve(pCreateInfo->subpassCount);
  for (uint32_t I = 0; I != pCreateInfo->subpassCount; ++I) {
    const VkSubpassDescription &Src = pCreateInfo->pSubpasses[I];
    if (Src.pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS)
      return VK_ERROR_INITIALIZATION_FAILED;
    // Input attachments would need either tile-local subpass merging or a
    // sampled-image round trip through memory; neither is implemented, and
    // "Input attachments ... [are] not permitted to be exposed as
    // VK_SUBPASS_DESCRIPTION_* behavior it does not implement".
    if (Src.inputAttachmentCount != 0)
      return VK_ERROR_INITIALIZATION_FAILED;

    SubpassDescription Subpass;
    for (uint32_t J = 0; J != Src.colorAttachmentCount; ++J) {
      uint32_t Index = Src.pColorAttachments[J].attachment;
      if (Index != VK_ATTACHMENT_UNUSED &&
          (Index >= Attachments.size() ||
           !isSupportedColorAttachmentFormat(Attachments[Index].Format)))
        return VK_ERROR_INITIALIZATION_FAILED;
      Subpass.ColorAttachments.push_back(Index);
    }
    if (Src.pResolveAttachments) {
      for (uint32_t J = 0; J != Src.colorAttachmentCount; ++J) {
        uint32_t Index = Src.pResolveAttachments[J].attachment;
        if (Index != VK_ATTACHMENT_UNUSED &&
            (Index >= Attachments.size() ||
             Attachments[Index].SampleCount != 1))
          return VK_ERROR_INITIALIZATION_FAILED;
        Subpass.ResolveAttachments.push_back(Index);
      }
    }
    if (Src.pDepthStencilAttachment) {
      uint32_t Index = Src.pDepthStencilAttachment->attachment;
      if (Index != VK_ATTACHMENT_UNUSED) {
        if (Index >= Attachments.size())
          return VK_ERROR_INITIALIZATION_FAILED;
        feme::cpu::ResourceFormat Format = Attachments[Index].Format;
        if (!isSupportedDepthAttachmentFormat(Format) &&
            !isSupportedStencilAttachmentFormat(Format))
          return VK_ERROR_FORMAT_NOT_SUPPORTED;
      }
      Subpass.DepthStencilAttachment = Index;
    }
    if (Src.preserveAttachmentCount != 0)
      // Preserved attachments are a no-op under this ICD's
      // execute-in-record-order model (nothing discards an attachment
      // between subpasses), but their indices must still be in range.
      for (uint32_t J = 0; J != Src.preserveAttachmentCount; ++J)
        if (Src.pPreserveAttachments[J] >= Attachments.size())
          return VK_ERROR_INITIALIZATION_FAILED;

    Subpasses.push_back(std::move(Subpass));
  }

  Allocator Alloc(pAllocator);
  RenderPass *Obj =
      Alloc.create<RenderPass>(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                               std::move(Attachments), std::move(Subpasses));
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pRenderPass = toHandle<VkRenderPass>(Obj);
  return VK_SUCCESS;
}

namespace {

/// Converts one `VkAttachmentReference2` to the classic
/// `VkAttachmentReference` `vkCreateRenderPass`'s subpass loop already
/// checks -- dropping only `aspectMask`, which this driver never consults
/// (no packed depth/stencil surface, no input attachments; see
/// `vkCreateRenderPass`'s own rejection of both).
VkAttachmentReference toAttachmentReference(const VkAttachmentReference2 &Src) {
  return {Src.attachment, Src.layout};
}

} // namespace

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(
    VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
  // `VkRenderPassCreateInfo2` adds multiview (`viewMask`/
  // `pCorrelatedViewMasks`) on top of the classic structures' fields --
  // unimplemented (roadmap R34/V7, same as `vkCreateRenderPass`'s own
  // layered-framebuffer rejection) -- so a render pass asking for it fails
  // here rather than being silently flattened to view 0.
  if (pCreateInfo->correlatedViewMaskCount != 0)
    return VK_ERROR_INITIALIZATION_FAILED;
  for (uint32_t I = 0; I != pCreateInfo->subpassCount; ++I)
    if (pCreateInfo->pSubpasses[I].viewMask != 0)
      return VK_ERROR_INITIALIZATION_FAILED;

  // Every other field `VkRenderPassCreateInfo2`/`VkAttachmentDescription2`/
  // `VkSubpassDescription2`/`VkSubpassDependency2` carry has the same name
  // and meaning as their classic counterparts (just with `sType`/`pNext`
  // spliced in for chained extension structures this driver does not
  // consume) -- converting to the classic structures and delegating to
  // `vkCreateRenderPass` reuses its validation and construction rather than
  // duplicating it.
  std::vector<VkAttachmentDescription> Attachments;
  Attachments.reserve(pCreateInfo->attachmentCount);
  for (uint32_t I = 0; I != pCreateInfo->attachmentCount; ++I) {
    const VkAttachmentDescription2 &Src = pCreateInfo->pAttachments[I];
    Attachments.push_back({Src.flags, Src.format, Src.samples, Src.loadOp,
                           Src.storeOp, Src.stencilLoadOp, Src.stencilStoreOp,
                           Src.initialLayout, Src.finalLayout});
  }

  // Each subpass's reference arrays need storage that outlives the
  // conversion loop below (the classic `VkSubpassDescription` only points
  // at them), so every subpass's converted references are kept in their
  // own vector for the whole function's duration.
  std::vector<std::vector<VkAttachmentReference>> InputRefs(
      pCreateInfo->subpassCount);
  std::vector<std::vector<VkAttachmentReference>> ColorRefs(
      pCreateInfo->subpassCount);
  std::vector<std::vector<VkAttachmentReference>> ResolveRefs(
      pCreateInfo->subpassCount);
  std::vector<VkAttachmentReference> DepthStencilRefs(
      pCreateInfo->subpassCount);
  std::vector<VkSubpassDescription> Subpasses;
  Subpasses.reserve(pCreateInfo->subpassCount);
  for (uint32_t I = 0; I != pCreateInfo->subpassCount; ++I) {
    const VkSubpassDescription2 &Src = pCreateInfo->pSubpasses[I];
    for (uint32_t J = 0; J != Src.inputAttachmentCount; ++J)
      InputRefs[I].push_back(toAttachmentReference(Src.pInputAttachments[J]));
    for (uint32_t J = 0; J != Src.colorAttachmentCount; ++J)
      ColorRefs[I].push_back(toAttachmentReference(Src.pColorAttachments[J]));
    if (Src.pResolveAttachments)
      for (uint32_t J = 0; J != Src.colorAttachmentCount; ++J)
        ResolveRefs[I].push_back(
            toAttachmentReference(Src.pResolveAttachments[J]));
    const VkAttachmentReference *DepthStencilPtr = nullptr;
    if (Src.pDepthStencilAttachment) {
      DepthStencilRefs[I] = toAttachmentReference(*Src.pDepthStencilAttachment);
      DepthStencilPtr = &DepthStencilRefs[I];
    }
    Subpasses.push_back(
        {Src.flags, Src.pipelineBindPoint, Src.inputAttachmentCount,
         InputRefs[I].data(), Src.colorAttachmentCount, ColorRefs[I].data(),
         ResolveRefs[I].empty() ? nullptr : ResolveRefs[I].data(),
         DepthStencilPtr, Src.preserveAttachmentCount,
         Src.pPreserveAttachments});
  }

  VkRenderPassCreateInfo ClassicInfo{
      VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      nullptr,
      pCreateInfo->flags,
      static_cast<uint32_t>(Attachments.size()),
      Attachments.data(),
      static_cast<uint32_t>(Subpasses.size()),
      Subpasses.data(),
      // Dependencies carry no payload this ICD's strictly sequential
      // execution needs (see RenderPass.h's `RenderPass` doc comment), so
      // `vkCreateRenderPass` never reads `pCreateInfo->pDependencies`
      // either; converting them would be pure dead weight.
      0u,
      nullptr,
  };
  return feme::vulkan::vkCreateRenderPass(device, &ClassicInfo, pAllocator,
                                          pRenderPass);
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyRenderPass(VkDevice, VkRenderPass renderPass,
                    const VkAllocationCallbacks *pAllocator) {
  if (!renderPass)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<RenderPass>(renderPass));
}

VKAPI_ATTR void VKAPI_CALL
vkGetRenderAreaGranularity(VkDevice, VkRenderPass, VkExtent2D *pGranularity) {
  // A software rasterizer has no tile-alignment requirement an application
  // could exploit: any render area is exactly as efficient as any other.
  pGranularity->width = 1;
  pGranularity->height = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(
    VkDevice, const VkFramebufferCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer) {
  if (!pCreateInfo->renderPass)
    return VK_ERROR_INITIALIZATION_FAILED;
  const RenderPass &Pass = *fromHandle<RenderPass>(pCreateInfo->renderPass);
  if (pCreateInfo->attachmentCount != Pass.attachments().size())
    return VK_ERROR_INITIALIZATION_FAILED;
  // Layered rendering is roadmap R34/V7; a framebuffer with more than one
  // layer would silently render into layer 0 only, so it is rejected here.
  if (pCreateInfo->layers != 1)
    return VK_ERROR_INITIALIZATION_FAILED;

  std::vector<ImageView *> Attachments;
  Attachments.reserve(pCreateInfo->attachmentCount);
  for (uint32_t I = 0; I != pCreateInfo->attachmentCount; ++I) {
    auto *View = fromHandle<ImageView>(pCreateInfo->pAttachments[I]);
    if (!View || !View->image())
      return VK_ERROR_INITIALIZATION_FAILED;
    if (View->format() != Pass.attachments()[I].Format ||
        View->image()->sampleCount() != Pass.attachments()[I].SampleCount)
      return VK_ERROR_INITIALIZATION_FAILED;
    if (View->image()->width() < pCreateInfo->width ||
        View->image()->height() < pCreateInfo->height)
      return VK_ERROR_INITIALIZATION_FAILED;
    Attachments.push_back(View);
  }

  Allocator Alloc(pAllocator);
  Framebuffer *Obj = Alloc.create<Framebuffer>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::move(Attachments),
      pCreateInfo->width, pCreateInfo->height, pCreateInfo->layers);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pFramebuffer = toHandle<VkFramebuffer>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyFramebuffer(VkDevice, VkFramebuffer framebuffer,
                     const VkAllocationCallbacks *pAllocator) {
  if (!framebuffer)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<Framebuffer>(framebuffer));
}

} // namespace feme::vulkan
