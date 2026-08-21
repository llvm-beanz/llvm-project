//===- Image.cpp - VkImage/VkImageView/VkSampler implementations --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Image.h"
#include "Format.h"
#include "Icd.h"
#include "Objects.h"

#include "llvm/Support/ErrorHandling.h"

#include <algorithm>

using namespace feme::vulkan;
using namespace feme::cpu;

namespace {

/// The `feme::cpu::ImageDimension` \p Type/\p ArrayLayers/\p Depth
/// corresponds to. `vkCreateImage` never sees a `VkImageViewType`, only a
/// `VkImageType` plus the array-layer/depth counts that distinguish an array
/// image from a plain one and a 3D image from a 2D one.
ImageDimension mapImageDimension(VkImageType Type, uint32_t ArrayLayers) {
  switch (Type) {
  case VK_IMAGE_TYPE_1D:
    return ArrayLayers > 1 ? ImageDimension::Texture1DArray
                           : ImageDimension::Texture1D;
  case VK_IMAGE_TYPE_2D:
    return ArrayLayers > 1 ? ImageDimension::Texture2DArray
                           : ImageDimension::Texture2D;
  case VK_IMAGE_TYPE_3D:
    return ImageDimension::Texture3D;
  default:
    llvm_unreachable("unhandled VkImageType");
  }
}

/// Computes a packed, mip-major subresource layout table for an image of
/// \p Dimension with the given extent/mip/array counts, \p SampleCount and
/// \p BlockWidth/\p BlockHeight/\p BytesPerBlock (roadmap E20; `{1, 1,
/// formatElementSize(Format)}` for a non-block-compressed format, so this
/// generalizes the original per-texel math rather than replacing it -- see
/// Format.h's file comment) -- see Image.h's file comment on why tiling is
/// not distinguished. Mip level `L`'s slice count is `max(1, Depth >> L)`
/// for a 3D image (its depth halves per mip, per Vulkan's mip-chain rules)
/// or `ArrayLayers` for every other dimension (an array image's layer
/// count is constant across mips). A row is `ceil(Width / BlockWidth)`
/// blocks wide, `BytesPerBlock` bytes each; `ceil` rather than plain
/// division because Vulkan requires only the whole *block* grid to be
/// stored, so a mip level whose texel extent isn't itself a multiple of
/// the block size (every level below the base one, for almost any
/// non-power-of-two-friendly footprint) still occupies one full row/column
/// of blocks at its edge, per `VkImageFormatProperties`' own
/// `VK_ERROR_FORMAT_NOT_SUPPORTED`-avoiding compressed-format extent
/// rules. Every sample of one non-block-compressed texel is stored
/// contiguously (`SampleStride == BytesPerBlock`), so a texel's
/// `SampleCount` samples occupy `SampleCount * BytesPerBlock` bytes and a
/// row is `Width` texels wide of that; a block-compressed format is never
/// multisampled in real Vulkan, so `SampleCount` is always 1 for one.
std::pair<std::vector<FemeImageSubresourceLayout>, VkDeviceSize>
computeSubresourceLayouts(VkImageType Type, uint32_t Width, uint32_t Height,
                          uint32_t Depth, uint32_t MipLevels,
                          uint32_t ArrayLayers, uint32_t SampleCount,
                          uint32_t BlockWidth, uint32_t BlockHeight,
                          uint32_t BytesPerBlock) {
  std::vector<FemeImageSubresourceLayout> Layouts(MipLevels);
  uint64_t Offset = 0;
  for (uint32_t Level = 0; Level != MipLevels; ++Level) {
    uint32_t LevelWidth = std::max(1u, Width >> Level);
    uint32_t LevelHeight = std::max(1u, Height >> Level);
    uint32_t LevelDepth =
        Type == VK_IMAGE_TYPE_3D ? std::max(1u, Depth >> Level) : 1;
    uint32_t SliceCount = Type == VK_IMAGE_TYPE_3D ? LevelDepth : ArrayLayers;
    uint32_t BlocksWide = (LevelWidth + BlockWidth - 1) / BlockWidth;
    uint32_t BlocksHigh = (LevelHeight + BlockHeight - 1) / BlockHeight;

    FemeImageSubresourceLayout &L = Layouts[Level];
    L.Offset = Offset;
    L.SampleStride = SampleCount > 1 ? BytesPerBlock : 0;
    L.RowPitch = uint64_t(BlocksWide) * BytesPerBlock * SampleCount;
    L.SlicePitch = L.RowPitch * BlocksHigh;
    Offset += L.SlicePitch * SliceCount;
  }
  return {std::move(Layouts), Offset};
}

} // namespace

Image::Image(VkImageType Type, ImageDimension Dimension, ResourceFormat Format,
             uint32_t Width, uint32_t Height, uint32_t Depth,
             uint32_t MipLevels, uint32_t ArrayLayers, uint32_t SampleCount,
             VkImageUsageFlags Usage)
    : Type(Type), Dimension(Dimension), Format(Format), Width(Width),
      Height(Height), Depth(Depth), MipLevels(MipLevels),
      ArrayLayers(ArrayLayers), SampleCount(SampleCount), Usage(Usage) {
  std::tie(MipLayouts, TotalSize) = computeSubresourceLayouts(
      Type, Width, Height, Depth, MipLevels, ArrayLayers, SampleCount,
      blockWidth(Format), blockHeight(Format), bytesPerBlock(Format));
  Layouts.assign(size_t(MipLevels) * std::max(ArrayLayers, Depth),
                 VK_IMAGE_LAYOUT_UNDEFINED);
}

void *Image::texelPointer(uint32_t MipLevel, uint32_t ArrayLayer, uint32_t X,
                          uint32_t Y, uint32_t Z, uint32_t Sample) const {
  if (!isBound())
    return nullptr;
  assert(!feme::cpu::isBlockCompressedFormat(Format) &&
         "block-compressed images are not addressable per texel -- use "
         "blockPointer instead (see Image.h's file comment)");
  const FemeImageSubresourceLayout &L = MipLayouts[MipLevel];
  uint64_t SliceIndex = uint64_t(ArrayLayer) + Z;
  uint64_t TexelStride = formatElementSize(Format) * SampleCount;
  uint64_t ByteOffset = L.Offset + SliceIndex * L.SlicePitch +
                        uint64_t(Y) * L.RowPitch + uint64_t(X) * TexelStride +
                        uint64_t(Sample) * formatElementSize(Format);
  return static_cast<uint8_t *>(data()) + ByteOffset;
}

void *Image::blockPointer(uint32_t MipLevel, uint32_t ArrayLayer,
                          uint32_t BlockX, uint32_t BlockY, uint32_t Z) const {
  if (!isBound())
    return nullptr;
  assert(feme::cpu::isBlockCompressedFormat(Format) &&
         "blockPointer is for a block-compressed Format only -- use "
         "texelPointer for any other one");
  const FemeImageSubresourceLayout &L = MipLayouts[MipLevel];
  uint64_t SliceIndex = uint64_t(ArrayLayer) + Z;
  uint64_t ByteOffset = L.Offset + SliceIndex * L.SlicePitch +
                        uint64_t(BlockY) * L.RowPitch +
                        uint64_t(BlockX) * bytesPerBlock(Format);
  return static_cast<uint8_t *>(data()) + ByteOffset;
}

VkImageLayout Image::layout(uint32_t MipLevel, uint32_t ArrayLayer) const {
  size_t SlicesPerLevel = std::max(ArrayLayers, Depth);
  return Layouts[size_t(MipLevel) * SlicesPerLevel + ArrayLayer];
}

void Image::setLayout(uint32_t BaseMip, uint32_t MipCount, uint32_t BaseLayer,
                      uint32_t LayerCount, VkImageLayout NewLayout) {
  size_t SlicesPerLevel = std::max(ArrayLayers, Depth);
  uint32_t EndMip =
      MipCount == VK_REMAINING_MIP_LEVELS ? MipLevels : BaseMip + MipCount;
  uint32_t EndLayer = LayerCount == VK_REMAINING_ARRAY_LAYERS
                          ? std::max(ArrayLayers, Depth)
                          : BaseLayer + LayerCount;
  for (uint32_t Mip = BaseMip; Mip != EndMip; ++Mip)
    for (uint32_t Layer = BaseLayer; Layer != EndLayer; ++Layer)
      Layouts[size_t(Mip) * SlicesPerLevel + Layer] = NewLayout;
}

ImageDimension ImageView::dimension() const {
  switch (ViewType) {
  case VK_IMAGE_VIEW_TYPE_1D:
    return ImageDimension::Texture1D;
  case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
    return ImageDimension::Texture1DArray;
  case VK_IMAGE_VIEW_TYPE_2D:
    return ImageDimension::Texture2D;
  case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
    return ImageDimension::Texture2DArray;
  case VK_IMAGE_VIEW_TYPE_3D:
    return ImageDimension::Texture3D;
  case VK_IMAGE_VIEW_TYPE_CUBE:
    return ImageDimension::TextureCube;
  case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
    return ImageDimension::TextureCubeArray;
  default:
    llvm_unreachable("unhandled VkImageViewType");
  }
}

namespace {

SamplerFilter mapFilter(VkFilter Filter) {
  return Filter == VK_FILTER_LINEAR ? SamplerFilter::Linear
                                    : SamplerFilter::Nearest;
}

SamplerFilter mapMipmapMode(VkSamplerMipmapMode Mode) {
  return Mode == VK_SAMPLER_MIPMAP_MODE_LINEAR ? SamplerFilter::Linear
                                               : SamplerFilter::Nearest;
}

SamplerAddressMode mapAddressMode(VkSamplerAddressMode Mode) {
  switch (Mode) {
  case VK_SAMPLER_ADDRESS_MODE_REPEAT:
    return SamplerAddressMode::Repeat;
  case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
    return SamplerAddressMode::MirroredRepeat;
  case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
    return SamplerAddressMode::ClampToEdge;
  case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
    return SamplerAddressMode::ClampToBorder;
  case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
    return SamplerAddressMode::MirrorClampToEdge;
  default:
    llvm_unreachable("unhandled VkSamplerAddressMode");
  }
}

SamplerCompareFunc mapCompareOp(VkCompareOp Op) {
  switch (Op) {
  case VK_COMPARE_OP_NEVER:
    return SamplerCompareFunc::Never;
  case VK_COMPARE_OP_LESS:
    return SamplerCompareFunc::Less;
  case VK_COMPARE_OP_EQUAL:
    return SamplerCompareFunc::Equal;
  case VK_COMPARE_OP_LESS_OR_EQUAL:
    return SamplerCompareFunc::LessEqual;
  case VK_COMPARE_OP_GREATER:
    return SamplerCompareFunc::Greater;
  case VK_COMPARE_OP_NOT_EQUAL:
    return SamplerCompareFunc::NotEqual;
  case VK_COMPARE_OP_GREATER_OR_EQUAL:
    return SamplerCompareFunc::GreaterEqual;
  case VK_COMPARE_OP_ALWAYS:
    return SamplerCompareFunc::Always;
  default:
    llvm_unreachable("unhandled VkCompareOp");
  }
}

/// `VK_EXT_border_color_swizzle`-less border-color resolution: only the
/// four float/int enumerators every core `VkBorderColor` covers are mapped
/// (`vkCreateSampler` rejects `..._CUSTOM_EXT`, since this ICD advertises no
/// custom-border-color extension -- see `vkCreateSampler`'s own check).
void mapBorderColor(VkBorderColor Color, float (&Out)[4]) {
  switch (Color) {
  case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
  case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
    Out[0] = Out[1] = Out[2] = Out[3] = 0.0f;
    return;
  case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
  case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
    Out[0] = Out[1] = Out[2] = 0.0f;
    Out[3] = 1.0f;
    return;
  case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
  case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
    Out[0] = Out[1] = Out[2] = Out[3] = 1.0f;
    return;
  default:
    llvm_unreachable("unhandled VkBorderColor");
  }
}

} // namespace

Sampler::Sampler(const VkSamplerCreateInfo &CreateInfo) : Descriptor{} {
  Descriptor.MinFilter = static_cast<uint32_t>(mapFilter(CreateInfo.minFilter));
  Descriptor.MagFilter = static_cast<uint32_t>(mapFilter(CreateInfo.magFilter));
  Descriptor.MipFilter =
      static_cast<uint32_t>(mapMipmapMode(CreateInfo.mipmapMode));
  Descriptor.AddressU =
      static_cast<uint32_t>(mapAddressMode(CreateInfo.addressModeU));
  Descriptor.AddressV =
      static_cast<uint32_t>(mapAddressMode(CreateInfo.addressModeV));
  Descriptor.AddressW =
      static_cast<uint32_t>(mapAddressMode(CreateInfo.addressModeW));
  Descriptor.LodBias = CreateInfo.mipLodBias;
  Descriptor.MinLod = CreateInfo.minLod;
  Descriptor.MaxLod = CreateInfo.maxLod;
  Descriptor.ReductionMode =
      static_cast<uint32_t>(SamplerReductionMode::WeightedAverage);

  uint32_t Flags = 0;
  if (CreateInfo.compareEnable) {
    Flags |= FEME_SAMPLER_COMPARE_ENABLE;
    Descriptor.CompareFunc =
        static_cast<uint32_t>(mapCompareOp(CreateInfo.compareOp));
  }
  if (CreateInfo.anisotropyEnable) {
    Flags |= FEME_SAMPLER_ANISOTROPY_ENABLE;
    Descriptor.MaxAnisotropy = CreateInfo.maxAnisotropy;
  }
  Descriptor.Flags = Flags;
  mapBorderColor(CreateInfo.borderColor, Descriptor.BorderColor);
}

namespace feme::vulkan {

/// The `VkSampleCountFlags` mask `pCreateInfo->samples` must intersect for
/// \p Usage, mirroring how real Vulkan intersects the per-usage sample-count
/// limits (`VkPhysicalDeviceLimits`' `sampledImageColorSampleCounts`/
/// `storageImageSampleCounts`/`framebufferColorSampleCounts`/
/// `framebufferDepthSampleCounts`). An image whose usage names none of
/// these (e.g. transfer-only) is conservatively restricted to
/// `VK_SAMPLE_COUNT_1_BIT`: nothing downstream (copy, shader, render target)
/// needs more than one sample for such an image, so there is no limit field
/// to honestly report a wider mask from. Exposed (roadmap E24) so
/// `vkGetPhysicalDeviceImageFormatProperties` (EntryPoints.cpp) reports the
/// same `sampleCounts` mask `vkCreateImage` itself actually honors, rather
/// than a second, independently-maintained guess.
VkSampleCountFlags supportedSampleCounts(const PhysicalDeviceInfo &Info,
                                         VkImageUsageFlags Usage) {
  const VkPhysicalDeviceLimits &Limits = Info.Properties.limits;
  VkSampleCountFlags Mask = ~VkSampleCountFlags(0);
  bool Constrained = false;
  if (Usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
    Mask &= Limits.sampledImageColorSampleCounts;
    Constrained = true;
  }
  if (Usage & VK_IMAGE_USAGE_STORAGE_BIT) {
    Mask &= Limits.storageImageSampleCounts;
    Constrained = true;
  }
  if (Usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
    Mask &= Limits.framebufferColorSampleCounts;
    Constrained = true;
  }
  if (Usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
    Mask &= Limits.framebufferDepthSampleCounts &
            Limits.framebufferStencilSampleCounts;
    Constrained = true;
  }
  return Constrained ? Mask : VkSampleCountFlags(VK_SAMPLE_COUNT_1_BIT);
}

/// Returns whether \p CreateInfo's shape (flags/samples/mips/array layers)
/// is one this ICD can create, *not counting* its `format` -- split out
/// from the format check so `vkCreateImage` can keep reporting the more
/// specific `VK_ERROR_FORMAT_NOT_SUPPORTED` for an unmapped format while
/// still sharing this validation with roadmap E4's `VK_KHR_maintenance4`
/// `vkGetDeviceImageMemoryRequirements`/
/// `vkGetDeviceImageSparseMemoryRequirements`, which report their result
/// through a `void`-returning entrypoint with no error code of their own,
/// and (roadmap E24) `vkGetPhysicalDeviceImageFormatProperties`
/// (EntryPoints.cpp), which needs the same shape check to decide whether a
/// `format`/`type`/`tiling`/`usage`/`flags` combination is supported at all.
/// A multisample `samples` is accepted at the object-model level -- see
/// Image.h's file comment -- as long as it is one this device's limits
/// actually advertise for the image's usage (`supportedSampleCounts`);
/// every other image continues to require exactly one sample, same as
/// before multisample support existed.
bool isValidImageShape(const VkImageCreateInfo &CreateInfo,
                       const PhysicalDeviceInfo &Info) {
  // No sparse binding (see "V5: Images and sampling"'s scope).
  if (CreateInfo.flags &
      ~VkImageCreateFlags(VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT))
    return false;
  if (!(CreateInfo.samples & supportedSampleCounts(Info, CreateInfo.usage)))
    return false;
  if (CreateInfo.mipLevels == 0 || CreateInfo.arrayLayers == 0)
    return false;
  if (CreateInfo.imageType == VK_IMAGE_TYPE_3D && CreateInfo.arrayLayers != 1)
    return false;
  // A multisample image is only ever a flat 2D render-target-shaped
  // resource in real Vulkan (`VUID-VkImageCreateInfo-samples-02257`): no
  // mips, and 2D only.
  if (CreateInfo.samples != VK_SAMPLE_COUNT_1_BIT &&
      (CreateInfo.imageType != VK_IMAGE_TYPE_2D || CreateInfo.mipLevels != 1))
    return false;
  return true;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkImage *pImage) {
  const PhysicalDeviceInfo &Info =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  if (!isValidImageShape(*pCreateInfo, Info))
    return VK_ERROR_INITIALIZATION_FAILED;

  std::optional<feme::cpu::ResourceFormat> Format =
      mapVkFormat(pCreateInfo->format);
  if (!Format)
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
  // Roadmap E22: a block-compressed `*Format` is no longer rejected here --
  // `blockPointer` (Image.h) and its `CommandBuffer.cpp`/`ImageOps.cpp`
  // callers now address one, the last piece `computeSubresourceLayouts`'
  // own E20 block-layout rework above was waiting on (see this file's own
  // header comment).
  feme::cpu::ImageDimension Dimension =
      mapImageDimension(pCreateInfo->imageType, pCreateInfo->arrayLayers);

  Allocator Alloc(pAllocator);
  Image *Obj = Alloc.create<Image>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, pCreateInfo->imageType, Dimension,
      *Format, pCreateInfo->extent.width, pCreateInfo->extent.height,
      pCreateInfo->extent.depth, pCreateInfo->mipLevels,
      pCreateInfo->arrayLayers, static_cast<uint32_t>(pCreateInfo->samples),
      pCreateInfo->usage);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pImage = toHandle<VkImage>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice, VkImage image, const VkAllocationCallbacks *pAllocator) {
  if (!image)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<Image>(image));
}

namespace {

/// Fills \p Reqs for a `TotalSize`-byte image, mirroring Buffer.cpp's
/// `computeBufferMemoryRequirements`: only memory type 0 exists, and the
/// alignment tracks this ICD's own host allocation granularity since there
/// is no real tiling requirement to report (see Image.h's file comment).
/// Shared by the live `vkGetImageMemoryRequirements(2)` entrypoints (an
/// already-created `Image`'s own `sizeInBytes()`) and roadmap E4's
/// info-only `vkGetDeviceImageMemoryRequirements`.
void fillImageMemoryRequirements(VkDeviceSize TotalSize,
                                 const PhysicalDeviceInfo &Info,
                                 VkMemoryRequirements &Reqs) {
  Reqs.size = TotalSize;
  Reqs.alignment = Info.Properties.limits.minMemoryMapAlignment;
  Reqs.memoryTypeBits = 0x1;
}

} // namespace

/// Computes a `VkImageCreateInfo`'s total packed byte size, without ever
/// constructing an `Image` -- the same `computeSubresourceLayouts` helper
/// `Image`'s own constructor calls, given \p Format's block layout (see
/// Image.h's file comment on why tiling is not distinguished). Exposed
/// (roadmap E24) so `vkGetPhysicalDeviceImageFormatProperties`
/// (EntryPoints.cpp) can report a real `VkImageFormatProperties::
/// maxResourceSize` for the maximal shape it validates through
/// `isValidImageShape`/`supportedSampleCounts` above.
VkDeviceSize computeImageCreateInfoSize(const VkImageCreateInfo &CreateInfo,
                                        feme::cpu::ResourceFormat Format) {
  return computeSubresourceLayouts(
             CreateInfo.imageType, CreateInfo.extent.width,
             CreateInfo.extent.height, CreateInfo.extent.depth,
             CreateInfo.mipLevels, CreateInfo.arrayLayers,
             static_cast<uint32_t>(CreateInfo.samples), blockWidth(Format),
             blockHeight(Format), bytesPerBlock(Format))
      .second;
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements) {
  const PhysicalDeviceInfo &Info =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  fillImageMemoryRequirements(fromHandle<Image>(image)->sizeInBytes(), Info,
                              *pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(
    VkDevice device, const VkImageMemoryRequirementsInfo2 *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements) {
  const PhysicalDeviceInfo &Info =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  fillImageMemoryRequirements(fromHandle<Image>(pInfo->image)->sizeInBytes(),
                              Info, pMemoryRequirements->memoryRequirements);
  fillMemoryRequirements2PNextChain(pMemoryRequirements->pNext);
}

/// (roadmap E4) `VK_KHR_maintenance4`: computes a `VkImage`'s memory
/// requirements from its `VkImageCreateInfo` alone, without ever creating
/// the image -- shares `isValidImageShape`/`computeImageCreateInfoSize`
/// with `vkCreateImage`'s own validation/sizing and
/// `fillImageMemoryRequirements` with the live
/// `vkGetImageMemoryRequirements(2)` entrypoints above. An unsupported shape or
/// format is reported as an all-zero result: unlike `vkCreateImage`, this
/// entrypoint returns no `VkResult` to report such a `VkImageCreateInfo` as
/// invalid through.
VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirements(
    VkDevice device, const VkDeviceImageMemoryRequirements *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements) {
  const PhysicalDeviceInfo &Info =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  const VkImageCreateInfo &CreateInfo = *pInfo->pCreateInfo;
  std::optional<feme::cpu::ResourceFormat> Format =
      mapVkFormat(CreateInfo.format);
  if (!isValidImageShape(CreateInfo, Info) || !Format) {
    pMemoryRequirements->memoryRequirements = VkMemoryRequirements{};
  } else {
    fillImageMemoryRequirements(computeImageCreateInfoSize(CreateInfo, *Format),
                                Info, pMemoryRequirements->memoryRequirements);
  }
  fillMemoryRequirements2PNextChain(pMemoryRequirements->pNext);
}

/// (roadmap E4) `VK_KHR_maintenance4`: no sparse residency is supported
/// (see "Initial Non-Goals"), mirroring
/// `vkGetPhysicalDeviceSparseImageFormatProperties`'s own empty result --
/// no `VkImageCreateInfo` ever reports a sparse memory requirement.
VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirements(
    VkDevice, const VkDeviceImageMemoryRequirements *,
    uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2 *) {
  *pSparseMemoryRequirementCount = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice, VkImage image,
                                                 VkDeviceMemory memory,
                                                 VkDeviceSize memoryOffset) {
  fromHandle<Image>(image)->bind(fromHandle<DeviceMemory>(memory),
                                 memoryOffset);
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                   const VkBindImageMemoryInfo *pBindInfos) {
  for (uint32_t I = 0; I != bindInfoCount; ++I)
    feme::vulkan::vkBindImageMemory(device, pBindInfos[I].image,
                                    pBindInfos[I].memory,
                                    pBindInfos[I].memoryOffset);
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImageView(VkDevice, const VkImageViewCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkImageView *pView) {
  auto *Img = fromHandle<Image>(pCreateInfo->image);
  std::optional<feme::cpu::ResourceFormat> Format =
      mapVkFormat(pCreateInfo->format);
  if (!Format)
    return VK_ERROR_FORMAT_NOT_SUPPORTED;

  const VkImageSubresourceRange &Range = pCreateInfo->subresourceRange;
  uint32_t LevelCount = Range.levelCount == VK_REMAINING_MIP_LEVELS
                            ? Img->mipLevels() - Range.baseMipLevel
                            : Range.levelCount;
  uint32_t LayerCount = Range.layerCount == VK_REMAINING_ARRAY_LAYERS
                            ? Img->arrayLayers() - Range.baseArrayLayer
                            : Range.layerCount;
  if (Range.baseMipLevel + LevelCount > Img->mipLevels() ||
      Range.baseArrayLayer + LayerCount > Img->arrayLayers())
    return VK_ERROR_INITIALIZATION_FAILED;

  Allocator Alloc(pAllocator);
  ImageView *Obj =
      Alloc.create<ImageView>(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, Img,
                              pCreateInfo->viewType, *Format, Range);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pView = toHandle<VkImageView>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(
    VkDevice, VkImageView imageView, const VkAllocationCallbacks *pAllocator) {
  if (!imageView)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<ImageView>(imageView));
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSampler(VkDevice, const VkSamplerCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator, VkSampler *pSampler) {
  // No custom border color (see `mapBorderColor`'s comment): this ICD
  // advertises no `VK_EXT_customBorderColor`/`VK_EXT_border_color_swizzle`.
  if (pCreateInfo->borderColor == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT ||
      pCreateInfo->borderColor == VK_BORDER_COLOR_INT_CUSTOM_EXT)
    return VK_ERROR_FEATURE_NOT_PRESENT;
  // Neither extension's own `pNext` struct is meaningful without its
  // extension enabled either -- reject a chained
  // `VkSamplerCustomBorderColorCreateInfoEXT`/
  // `VkSamplerBorderColorComponentMappingCreateInfoEXT` explicitly rather
  // than silently ignoring it the way an unrecognized `pNext` struct
  // normally is (see e.g. `fillProperties2Chain`'s comment in
  // EntryPoints.cpp): an application that chained one believing either
  // extension were supported would otherwise get a sampler that silently
  // ignores its custom border color/component swizzle, not a diagnosable
  // failure.
  for (const auto *Base =
           static_cast<const VkBaseInStructure *>(pCreateInfo->pNext);
       Base; Base = Base->pNext) {
    if (Base->sType ==
            VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT ||
        Base->sType ==
            VK_STRUCTURE_TYPE_SAMPLER_BORDER_COLOR_COMPONENT_MAPPING_CREATE_INFO_EXT)
      return VK_ERROR_FEATURE_NOT_PRESENT;
  }

  Allocator Alloc(pAllocator);
  Sampler *Obj =
      Alloc.create<Sampler>(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, *pCreateInfo);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pSampler = toHandle<VkSampler>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySampler(
    VkDevice, VkSampler sampler, const VkAllocationCallbacks *pAllocator) {
  if (!sampler)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<Sampler>(sampler));
}

} // namespace feme::vulkan
