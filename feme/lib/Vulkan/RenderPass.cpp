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
  // (Roadmap H8r) `B8G8R8A8_UNORM_SRGB`: the sRGB sibling of
  // `B8G8R8A8_UNORM` above, also backed by a real `packClearColor`/
  // `unpackColor` case (ImageFixture.cpp).
  case feme::cpu::ResourceFormat::B8G8R8A8_UNORM_SRGB:
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
  case feme::cpu::ResourceFormat::R8_UNORM:
  case feme::cpu::ResourceFormat::R8G8_UNORM:
  case feme::cpu::ResourceFormat::R16_FLOAT:
  case feme::cpu::ResourceFormat::R16G16_FLOAT:
    // (Roadmap H8s) A real CTS re-run found these four non-integer
    // formats still missing `COLOR_ATTACHMENT_BIT`/`_BLEND_BIT`, a gap
    // H8e/H8p's own integer-only framing never covered -- but each
    // already has a real `feme::graphics::packClearColor`/`unpackColor`
    // case (used already by the vertex-fetch/texel-buffer/sampled-image
    // paths), and `readFragmentColor` (`Executor.cpp`) is generic over
    // any `Float`-typed output width, so no new pack/unpack or
    // executor code is needed here, unlike the integer cluster below.
    return true;
  case feme::cpu::ResourceFormat::A8_UNORM:
  case feme::cpu::ResourceFormat::A1B5G5R5_UNORM:
    // (Roadmap E5) `VK_KHR_maintenance5`'s two new formats are both
    // `COLOR_ATTACHMENT_BIT | COLOR_ATTACHMENT_BLEND_BIT` capable, backed
    // by their own `feme::graphics::packClearColor`/`unpackColor` cases.
    return true;
  case feme::cpu::ResourceFormat::R4G4B4A4_UNORM:
  case feme::cpu::ResourceFormat::B4G4R4A4_UNORM:
  case feme::cpu::ResourceFormat::R5G6B5_UNORM:
  case feme::cpu::ResourceFormat::B5G6R5_UNORM:
  case feme::cpu::ResourceFormat::R5G5B5A1_UNORM:
  case feme::cpu::ResourceFormat::B5G5R5A1_UNORM:
  case feme::cpu::ResourceFormat::A1R5G5B5_UNORM:
    // (Roadmap H7r) Also backed by a real `packClearColor`/`unpackColor`
    // case each, unlike their `A4R4G4B4_UNORM`/`A4B4G4R4_UNORM` (E19)
    // neighbors, which recognize the `VkFormat` but implement neither.
    return true;
  case feme::cpu::ResourceFormat::R8G8B8A8_UINT:
  case feme::cpu::ResourceFormat::R8G8B8A8_SINT:
  case feme::cpu::ResourceFormat::R10G10B10A2_UINT:
  case feme::cpu::ResourceFormat::R16_UINT:
  case feme::cpu::ResourceFormat::R16_SINT:
  case feme::cpu::ResourceFormat::R16G16_UINT:
  case feme::cpu::ResourceFormat::R16G16_SINT:
    // (Roadmap H8p) The 7 real integer color-attachment formats: a real
    // `UInt`/`SInt` fragment output can now be validated
    // (`Executor.cpp`'s `executeDraws`) and written
    // (`readFragmentColorInt`/`packClearColor`/`unpackColor`) to one of
    // these, unlike every other still-`false` integer format below --
    // but note these are `COLOR_ATTACHMENT_BIT`-only, never
    // `COLOR_ATTACHMENT_BLEND_BIT` (`Format.cpp`'s `formatFeatureFlags`
    // gates that bit off separately for `isIntegerColorAttachmentFormat`),
    // since blending is undefined for an integer format per spec.
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
  return SampleCount == 1 || SampleCount == 2 || SampleCount == 4 ||
         SampleCount == 8;
}

llvm::Expected<feme::graphics::AttachmentView>
resolveAttachmentView(ImageView *View) {
  if (!View || !View->image() || !View->image()->isBound())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "a render target attachment is not bound "
                                   "to memory");
  Image &Img = *View->image();
  const VkImageSubresourceRange &Range = View->range();
  // (Roadmap H2) A layered render target's view is `VK_IMAGE_VIEW_TYPE_
  // 2D_ARRAY` (`ImageDimension::Texture2DArray`), not `_2D`; both are
  // otherwise addressed identically here (2D, one array-layer stride
  // apart), so both are accepted. (Roadmap H5e-c) Vulkan also permits a
  // `VK_IMAGE_VIEW_TYPE_1D`/`_1D_ARRAY` view as a render target (e.g.
  // `dEQP-VK.geometry.layered.1d_array.*`'s own framebuffer, one row tall
  // by construction since a 1D image's `extent.height` must be 1) and a
  // `VK_IMAGE_VIEW_TYPE_CUBE`/`_CUBE_ARRAY` view (`dEQP-VK.geometry.
  // layered.cube{,_array}.*`'s own framebuffer -- the six faces of a cube,
  // or six-face groups of a cube array, are simply the backing image's own
  // array layers to `Image`, which never itself carries a `Cube` dimension
  // (`mapImageDimension` only ever produces `Texture2D(Array)`/`Texture1D
  // (Array)`/`Texture3D`; "cube" is purely an addressing convention this
  // view's own `ImageDimension::TextureCube(Array)` records, not a
  // different physical layout) -- addressed identically to the 1D/2D cases
  // below, so all six dimensions are accepted.
  switch (View->dimension()) {
  case feme::cpu::ImageDimension::Texture1D:
  case feme::cpu::ImageDimension::Texture1DArray:
  case feme::cpu::ImageDimension::Texture2D:
  case feme::cpu::ImageDimension::Texture2DArray:
  case feme::cpu::ImageDimension::TextureCube:
  case feme::cpu::ImageDimension::TextureCubeArray:
    break;
  default:
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "only a 1D, 1D-array, 2D, 2D-array, cube, or cube-array image view "
        "may be a render target");
  }
  if (Range.baseMipLevel >= Img.mipLevels())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "a render target view's base mip level is "
                                   "out of range");
  // (Roadmap H2) A layered render target: `LayerCount` array layers
  // starting at `baseArrayLayer`, stored consecutively (layer-major, see
  // `AttachmentView::ArrayLayers`'s own comment) at this mip level's
  // `SlicePitch` stride -- exactly the addressing `getAttachmentLayerByte
  // Offset` (LayeredRendering.h) assumes.
  uint32_t LayerCount =
      Img.resolvedLayerCount(Range.baseArrayLayer, Range.layerCount);
  if (LayerCount == 0)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "a render target view has zero array "
                                   "layers");

  uint32_t Level = Range.baseMipLevel;
  const feme::cpu::FemeImageSubresourceLayout &Layout = Img.mipLayouts()[Level];
  feme::graphics::AttachmentView Result;
  Result.Format = View->format();
  Result.Width = std::max(1u, Img.width() >> Level);
  Result.Height = std::max(1u, Img.height() >> Level);
  Result.ArrayLayers = LayerCount;
  auto *Base = static_cast<uint8_t *>(Img.data()) + Layout.Offset +
               (uint64_t)Range.baseArrayLayer * Layout.SlicePitch;
  Result.Data = llvm::MutableArrayRef<uint8_t>(
      Base, static_cast<size_t>(Layout.SlicePitch) * LayerCount);
  return Result;
}

bool isCompatibleAttachmentView(const AttachmentDescription &Attachment,
                                ImageView *View, uint32_t Width,
                                uint32_t Height, uint32_t Layers) {
  if (!View || !View->image())
    return false;
  const VkImageSubresourceRange &Range = View->range();
  uint32_t ViewLayers = View->image()->resolvedLayerCount(
      Range.baseArrayLayer, Range.layerCount);
  return View->format() == Attachment.Format &&
         View->image()->sampleCount() == Attachment.SampleCount &&
         View->image()->width() >= Width && View->image()->height() >= Height &&
         ViewLayers >= Layers;
}

} // namespace feme::vulkan

namespace {

bool isValidSubpassIndex(uint32_t Subpass, uint32_t SubpassCount) {
  return Subpass == VK_SUBPASS_EXTERNAL || Subpass < SubpassCount;
}

VkResult validateSubpassDependency(const VkSubpassDependency &Dep,
                                   uint32_t SubpassCount) {
  if (!isValidSubpassIndex(Dep.srcSubpass, SubpassCount) ||
      !isValidSubpassIndex(Dep.dstSubpass, SubpassCount))
    return VK_ERROR_INITIALIZATION_FAILED;
  // (Roadmap H2) `VK_DEPENDENCY_VIEW_LOCAL_BIT` describes a per-view
  // dependency a multiview render pass may declare; like every other
  // subpass dependency here, it needs no tracking of its own, since this
  // ICD's strictly sequential, execute-in-record-order model already
  // satisfies any join it could describe (see this file's class comment).
  return VK_SUCCESS;
}

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

  // (Roadmap H2) The classic `vkCreateRenderPass` gets multiview
  // (`viewMask`/`pCorrelationMasks`) through a chained
  // `VkRenderPassMultiviewCreateInfo` rather than `VkRenderPassCreateInfo2`'s
  // own per-subpass field; `pCorrelationMasks` is a batching hint this
  // single-threaded executor never needs (every view already renders in the
  // same record order everything else does), so it is read for its
  // `subpassCount`/`viewMask` array alone.
  const VkRenderPassMultiviewCreateInfo *Multiview = nullptr;
  for (auto *Base = static_cast<const VkBaseInStructure *>(pCreateInfo->pNext);
       Base; Base = Base->pNext)
    if (Base->sType == VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO) {
      Multiview =
          reinterpret_cast<const VkRenderPassMultiviewCreateInfo *>(Base);
      break;
    }
  if (Multiview && Multiview->subpassCount != 0 &&
      Multiview->subpassCount != pCreateInfo->subpassCount)
    return VK_ERROR_INITIALIZATION_FAILED;

  std::vector<SubpassDescription> Subpasses;
  Subpasses.reserve(pCreateInfo->subpassCount);
  for (uint32_t I = 0; I != pCreateInfo->subpassCount; ++I) {
    const VkSubpassDescription &Src = pCreateInfo->pSubpasses[I];
    if (Src.pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS)
      return VK_ERROR_INITIALIZATION_FAILED;

    SubpassDescription Subpass;
    if (Multiview)
      Subpass.ViewMask = Multiview->pViewMasks[I];
    for (uint32_t J = 0; J != Src.inputAttachmentCount; ++J) {
      uint32_t Index = Src.pInputAttachments[J].attachment;
      if (Index != VK_ATTACHMENT_UNUSED && Index >= Attachments.size())
        return VK_ERROR_INITIALIZATION_FAILED;
      Subpass.InputAttachments.push_back(Index);
    }
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
  for (uint32_t I = 0; I != pCreateInfo->dependencyCount; ++I)
    if (VkResult Result = validateSubpassDependency(
            pCreateInfo->pDependencies[I], pCreateInfo->subpassCount);
        Result != VK_SUCCESS)
      return Result;

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
/// (no packed depth/stencil-aspect selection, and shader-side input-
/// attachment lowering still ignores the distinction).
VkAttachmentReference toAttachmentReference(const VkAttachmentReference2 &Src) {
  return {Src.attachment, Src.layout};
}

} // namespace

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(
    VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
  // (Roadmap H2) `pCorrelatedViewMasks` is the same batching-only hint
  // `VkRenderPassMultiviewCreateInfo::pCorrelationMasks` is (see
  // `vkCreateRenderPass`'s own comment): never consulted, since a
  // single-threaded executor gains nothing from knowing which views may
  // render concurrently.

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
  std::vector<VkSubpassDependency> Dependencies;
  Dependencies.reserve(pCreateInfo->dependencyCount);
  for (uint32_t I = 0; I != pCreateInfo->dependencyCount; ++I) {
    const VkSubpassDependency2 &Src = pCreateInfo->pDependencies[I];
    // (Roadmap H2) `viewOffset` is a per-view memory-dependency offset;
    // like `VK_DEPENDENCY_VIEW_LOCAL_BIT` above, it needs no tracking of
    // its own under this ICD's strictly sequential execution model.
    Dependencies.push_back({Src.srcSubpass, Src.dstSubpass, Src.srcStageMask,
                            Src.dstStageMask, Src.srcAccessMask,
                            Src.dstAccessMask, Src.dependencyFlags});
  }

  std::vector<VkSubpassDescription> Subpasses;
  Subpasses.reserve(pCreateInfo->subpassCount);
  // (Roadmap H2) `VkRenderPassCreateInfo2` carries `viewMask` per subpass
  // directly rather than through a chained `VkRenderPassMultiviewCreateInfo`
  // the way the classic structure does; collected here so it can be handed
  // to `vkCreateRenderPass` through that same chained structure below,
  // reusing its own multiview handling rather than duplicating it.
  std::vector<uint32_t> ViewMasks(pCreateInfo->subpassCount);
  bool AnyMultiview = false;
  for (uint32_t I = 0; I != pCreateInfo->subpassCount; ++I) {
    const VkSubpassDescription2 &Src = pCreateInfo->pSubpasses[I];
    ViewMasks[I] = Src.viewMask;
    AnyMultiview |= Src.viewMask != 0;
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

  VkRenderPassMultiviewCreateInfo MultiviewInfo{
      VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO,
      nullptr,
      static_cast<uint32_t>(ViewMasks.size()),
      ViewMasks.data(),
      0,
      nullptr,
      0,
      nullptr,
  };
  VkRenderPassCreateInfo ClassicInfo{
      VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      AnyMultiview ? &MultiviewInfo : nullptr,
      pCreateInfo->flags,
      static_cast<uint32_t>(Attachments.size()),
      Attachments.data(),
      static_cast<uint32_t>(Subpasses.size()),
      Subpasses.data(),
      static_cast<uint32_t>(Dependencies.size()),
      Dependencies.empty() ? nullptr : Dependencies.data(),
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

VKAPI_ATTR void VKAPI_CALL
vkGetRenderingAreaGranularityKHR(VkDevice, const VkRenderingAreaInfo *,
                                VkExtent2D *pGranularity) {
  // Same answer as vkGetRenderAreaGranularity above, for the same reason: a
  // software rasterizer has no tile-alignment requirement, dynamic-rendering
  // or otherwise.
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
  // (Roadmap H2) A framebuffer must declare at least one layer; beyond
  // that, `layers > 1` (layered rendering/multiview) is now accepted --
  // `isCompatibleAttachmentView`'s own `Layers` check below requires each
  // bound view to actually cover that many array layers.
  if (pCreateInfo->layers == 0)
    return VK_ERROR_INITIALIZATION_FAILED;

  // (Roadmap C6) `VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT`: the concrete image
  // views are supplied per render-pass instance instead
  // (`VkRenderPassAttachmentBeginInfo` at `vkCmdBeginRenderPass`), so there
  // is nothing to resolve or validate against real images yet -- only that
  // the chained `VkFramebufferAttachmentsCreateInfo` names the same
  // attachment count and, where it names candidate view formats at all, at
  // least one of them is format-compatible with the render pass's own
  // attachment. `isCompatibleAttachmentView`'s format/sample-count/size
  // check happens later, once a real view is bound at
  // `vkCmdBeginRenderPass` time.
  if (pCreateInfo->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT) {
    const VkFramebufferAttachmentsCreateInfo *AttachmentsInfo = nullptr;
    for (auto *Base =
             static_cast<const VkBaseInStructure *>(pCreateInfo->pNext);
         Base; Base = Base->pNext)
      if (Base->sType ==
          VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO) {
        AttachmentsInfo =
            reinterpret_cast<const VkFramebufferAttachmentsCreateInfo *>(Base);
        break;
      }
    if (!AttachmentsInfo || AttachmentsInfo->attachmentImageInfoCount !=
                                pCreateInfo->attachmentCount)
      return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t I = 0; I != AttachmentsInfo->attachmentImageInfoCount; ++I) {
      const VkFramebufferAttachmentImageInfo &ImageInfo =
          AttachmentsInfo->pAttachmentImageInfos[I];
      if (ImageInfo.width < pCreateInfo->width ||
          ImageInfo.height < pCreateInfo->height ||
          ImageInfo.layerCount < pCreateInfo->layers)
        return VK_ERROR_INITIALIZATION_FAILED;
      bool FormatCompatible = ImageInfo.viewFormatCount == 0;
      for (uint32_t J = 0; J != ImageInfo.viewFormatCount; ++J)
        if (mapVkFormat(ImageInfo.pViewFormats[J]) ==
            Pass.attachments()[I].Format) {
          FormatCompatible = true;
          break;
        }
      if (!FormatCompatible)
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    Allocator ImagelessAlloc(pAllocator);
    Framebuffer *ImagelessObj = ImagelessAlloc.create<Framebuffer>(
        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::vector<ImageView *>{},
        pCreateInfo->width, pCreateInfo->height, pCreateInfo->layers,
        /*Imageless=*/true);
    if (!ImagelessObj)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
    *pFramebuffer = toHandle<VkFramebuffer>(ImagelessObj);
    return VK_SUCCESS;
  }

  std::vector<ImageView *> Attachments;
  Attachments.reserve(pCreateInfo->attachmentCount);
  for (uint32_t I = 0; I != pCreateInfo->attachmentCount; ++I) {
    auto *View = fromHandle<ImageView>(pCreateInfo->pAttachments[I]);
    if (!isCompatibleAttachmentView(Pass.attachments()[I], View,
                                    pCreateInfo->width, pCreateInfo->height,
                                    pCreateInfo->layers))
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
