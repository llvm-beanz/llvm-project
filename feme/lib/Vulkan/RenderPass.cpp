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
  case feme::cpu::ResourceFormat::R16G16B16A16_FLOAT:
  case feme::cpu::ResourceFormat::R16G16B16A16_UNORM:
  case feme::cpu::ResourceFormat::R16G16B16A16_SNORM:
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
         Format == feme::cpu::ResourceFormat::D32_FLOAT;
}

bool isSupportedStencilAttachmentFormat(feme::cpu::ResourceFormat Format) {
  return Format == feme::cpu::ResourceFormat::S8_UINT;
}

bool isSupportedAttachmentSampleCount(uint32_t SampleCount) {
  return SampleCount == 1 || SampleCount == 2 || SampleCount == 4;
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

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateRenderPass(VkDevice, const VkRenderPassCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkRenderPass *pRenderPass) {
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

VKAPI_ATTR void VKAPI_CALL
vkDestroyRenderPass(VkDevice, VkRenderPass renderPass,
                    const VkAllocationCallbacks *pAllocator) {
  if (!renderPass)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<RenderPass>(renderPass));
}

VKAPI_ATTR void VKAPI_CALL vkGetRenderAreaGranularity(
    VkDevice, VkRenderPass, VkExtent2D *pGranularity) {
  // A software rasterizer has no tile-alignment requirement an application
  // could exploit: any render area is exactly as efficient as any other.
  pGranularity->width = 1;
  pGranularity->height = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFramebuffer(VkDevice, const VkFramebufferCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkFramebuffer *pFramebuffer) {
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
