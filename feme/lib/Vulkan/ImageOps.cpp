//===- ImageOps.cpp - Attachment clears, blits, and resolves -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ImageOps.h"
#include "Format.h"
#include "Image.h"

#include "feme/Graphics/ImageFixture.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace feme::vulkan;
using namespace llvm;

namespace {

/// Packs \p Color into one texel of \p Format, or fails for a format the
/// central pack table cannot express.
Expected<std::vector<uint8_t>> packTexel(feme::cpu::ResourceFormat Format,
                                         ArrayRef<double> Color) {
  Expected<uint32_t> ElemSize =
      feme::graphics::getFixtureFormatElementSize(Format);
  if (!ElemSize)
    return ElemSize.takeError();
  std::vector<uint8_t> Texel(*ElemSize);
  if (Error E = feme::graphics::packClearColor(Format, Color, Texel))
    return std::move(E);
  return Texel;
}

/// The clear value for one texel of \p Format, from either a color or a
/// depth/stencil clear value. `S8_UINT` is a raw byte rather than a
/// normalized component, so it bypasses the pack table.
Expected<std::vector<uint8_t>> buildClearTexel(feme::cpu::ResourceFormat Format,
                                               const VkClearValue &Value,
                                               bool DepthStencil) {
  if (DepthStencil && isSupportedStencilAttachmentFormat(Format))
    return std::vector<uint8_t>{
        static_cast<uint8_t>(Value.depthStencil.stencil)};
  if (DepthStencil)
    return packTexel(Format, {Value.depthStencil.depth});
  return packTexel(Format, {Value.color.float32[0], Value.color.float32[1],
                            Value.color.float32[2], Value.color.float32[3]});
}

/// Fills every texel (and every sample of it) of subresource
/// (\p Level, \p Layer) of \p Img with \p Texel.
void fillSubresource(Image &Img, uint32_t Level, uint32_t Layer,
                     ArrayRef<uint8_t> Texel) {
  uint32_t Width = std::max(1u, Img.width() >> Level);
  uint32_t Height = std::max(1u, Img.height() >> Level);
  uint32_t Depth = std::max(1u, Img.depth() >> Level);
  for (uint32_t Z = 0; Z != Depth; ++Z)
    for (uint32_t Y = 0; Y != Height; ++Y)
      for (uint32_t X = 0; X != Width; ++X)
        for (uint32_t S = 0; S != Img.sampleCount(); ++S)
          std::memcpy(Img.texelPointer(Level, Layer, X, Y, Z, S), Texel.data(),
                      Texel.size());
}

Error clearImageRanges(Image *Img, const VkClearValue &Value,
                       ArrayRef<VkImageSubresourceRange> Ranges,
                       bool DepthStencil) {
  if (!Img || !Img->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "the cleared image is not bound to memory");
  Expected<std::vector<uint8_t>> Texel =
      buildClearTexel(Img->format(), Value, DepthStencil);
  if (!Texel)
    return Texel.takeError();

  for (const VkImageSubresourceRange &Range : Ranges) {
    uint32_t LevelCount = Range.levelCount == VK_REMAINING_MIP_LEVELS
                              ? Img->mipLevels() - Range.baseMipLevel
                              : Range.levelCount;
    uint32_t LayerCount = Range.layerCount == VK_REMAINING_ARRAY_LAYERS
                              ? Img->arrayLayers() - Range.baseArrayLayer
                              : Range.layerCount;
    if (Range.baseMipLevel + LevelCount > Img->mipLevels() ||
        Range.baseArrayLayer + LayerCount > Img->arrayLayers())
      return createStringError(inconvertibleErrorCode(),
                               "a cleared subresource range is out of range "
                               "of its image");
    for (uint32_t L = 0; L != LevelCount; ++L)
      for (uint32_t A = 0; A != LayerCount; ++A)
        fillSubresource(*Img, Range.baseMipLevel + L, Range.baseArrayLayer + A,
                        *Texel);
  }
  return Error::success();
}

/// The two images a resolve names must agree on format and be bound; a
/// resolve, unlike a blit, does not convert between formats (see "Texture
/// layout and formats" in feme/docs/FeMeGraphicsDesign.md for the
/// conversion scope).
Error checkImagePair(Image *Src, Image *Dst, const char *What) {
  if (!Src || !Dst || !Src->isBound() || !Dst->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "a %s image is not bound to memory", What);
  if (Src->format() != Dst->format())
    return createStringError(inconvertibleErrorCode(),
                             "%s between differing formats is not implemented",
                             What);
  return Error::success();
}

/// Whether \p Offsets describes a region this driver can address: a
/// nonzero extent on X and Y (their sign selects mirroring, handled by the
/// caller) and no 3D depth range beyond one slice.
bool isSimpleRegion(const VkOffset3D Offsets[2]) {
  return Offsets[1].x != Offsets[0].x && Offsets[1].y != Offsets[0].y &&
         Offsets[1].z - Offsets[0].z == 1;
}

} // namespace

namespace feme::vulkan {

Error runClearColorImage(Image *Img, const VkClearColorValue &Color,
                         ArrayRef<VkImageSubresourceRange> Ranges) {
  VkClearValue Value{};
  Value.color = Color;
  return clearImageRanges(Img, Value, Ranges, /*DepthStencil=*/false);
}

Error runClearDepthStencilImage(Image *Img,
                                const VkClearDepthStencilValue &DepthStencil,
                                ArrayRef<VkImageSubresourceRange> Ranges) {
  VkClearValue Value{};
  Value.depthStencil = DepthStencil;
  return clearImageRanges(Img, Value, Ranges, /*DepthStencil=*/true);
}

Error runClearAttachments(const RenderTargetBinding &Binding,
                          ArrayRef<VkClearAttachment> Attachments,
                          ArrayRef<VkClearRect> Rects) {
  for (const VkClearAttachment &Clear : Attachments) {
    const RenderTargetView *Target = nullptr;
    bool DepthStencil = false;
    if (Clear.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
      if (Clear.colorAttachment >= Binding.Colors.size())
        return createStringError(inconvertibleErrorCode(),
                                 "vkCmdClearAttachments names color "
                                 "attachment %u, which is not bound",
                                 Clear.colorAttachment);
      Target = &Binding.Colors[Clear.colorAttachment];
    } else if (Clear.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
      if (!Binding.Depth)
        return createStringError(inconvertibleErrorCode(),
                                 "vkCmdClearAttachments names the depth "
                                 "attachment, which is not bound");
      Target = &*Binding.Depth;
      DepthStencil = true;
    } else if (Clear.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
      if (!Binding.Stencil)
        return createStringError(inconvertibleErrorCode(),
                                 "vkCmdClearAttachments names the stencil "
                                 "attachment, which is not bound");
      Target = &*Binding.Stencil;
      DepthStencil = true;
    } else {
      return createStringError(inconvertibleErrorCode(),
                               "vkCmdClearAttachments names no aspect");
    }

    Expected<feme::graphics::AttachmentView> View =
        resolveAttachmentView(Target->View);
    if (!View)
      return View.takeError();
    Expected<std::vector<uint8_t>> Texel =
        buildClearTexel(View->Format, Clear.clearValue, DepthStencil);
    if (!Texel)
      return Texel.takeError();

    for (const VkClearRect &Rect : Rects) {
      uint32_t MinX = std::max<int32_t>(0, Rect.rect.offset.x);
      uint32_t MinY = std::max<int32_t>(0, Rect.rect.offset.y);
      uint32_t MaxX = std::min<uint64_t>(
          View->Width, uint64_t(MinX) + Rect.rect.extent.width);
      uint32_t MaxY = std::min<uint64_t>(
          View->Height, uint64_t(MinY) + Rect.rect.extent.height);
      for (uint32_t Y = MinY; Y < MaxY; ++Y)
        for (uint32_t X = MinX; X < MaxX; ++X)
          for (uint32_t S = 0; S != Target->SampleCount; ++S) {
            size_t Offset =
                (((size_t)Y * View->Width + X) * Target->SampleCount + S) *
                Texel->size();
            std::memcpy(View->Data.data() + Offset, Texel->data(),
                        Texel->size());
          }
    }
  }
  return Error::success();
}

/// The two images a blit names must be bound; a blit, unlike a copy or
/// resolve, is explicitly permitted to convert between formats.
Error checkBlitImagePair(Image *Src, Image *Dst) {
  if (!Src || !Dst || !Src->isBound() || !Dst->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "a blit image is not bound to memory");
  return Error::success();
}

Error runBlitImage(Image *Src, Image *Dst, ArrayRef<VkImageBlit> Regions,
                   VkFilter Filter) {
  if (Error E = checkBlitImagePair(Src, Dst))
    return E;
  // Real Vulkan itself disallows a multisample source or destination for
  // vkCmdBlitImage (`vkCmdResolveImage` exists for that); this is not a
  // narrower deviation.
  if (Src->sampleCount() != 1 || Dst->sampleCount() != 1)
    return createStringError(inconvertibleErrorCode(),
                             "blitting a multisample image is not "
                             "implemented (use vkCmdResolveImage)");
  if (Filter != VK_FILTER_NEAREST && Filter != VK_FILTER_LINEAR)
    return createStringError(inconvertibleErrorCode(),
                             "only nearest and linear blit filters are "
                             "implemented");
  uint32_t SrcTexelSize = formatElementSize(Src->format());
  uint32_t DstTexelSize = formatElementSize(Dst->format());
  // A same-format nearest blit copies raw bytes verbatim (no unpack/repack
  // rounding); anything else goes through the central pack table, exactly
  // as the bilinear path always has.
  bool SameFormat = Src->format() == Dst->format();

  for (const VkImageBlit &Region : Regions) {
    if (!isSimpleRegion(Region.srcOffsets) ||
        !isSimpleRegion(Region.dstOffsets))
      return createStringError(inconvertibleErrorCode(),
                               "a multi-slice blit region is not implemented");
    uint32_t SrcLevel = Region.srcSubresource.mipLevel;
    uint32_t DstLevel = Region.dstSubresource.mipLevel;
    if (SrcLevel >= Src->mipLevels() || DstLevel >= Dst->mipLevels())
      return createStringError(inconvertibleErrorCode(),
                               "a blit region names a mip level out of range");
    // Signed corner-to-corner extents: a negative one mirrors that axis
    // ("Blits" in feme/docs/FeMeVulkanDesign.md), matching Vulkan's own
    // "opposite corners flip the region" rule.
    int64_t SrcX0 = Region.srcOffsets[0].x, SrcX1 = Region.srcOffsets[1].x;
    int64_t SrcY0 = Region.srcOffsets[0].y, SrcY1 = Region.srcOffsets[1].y;
    int64_t DstX0 = Region.dstOffsets[0].x, DstX1 = Region.dstOffsets[1].x;
    int64_t DstY0 = Region.dstOffsets[0].y, DstY1 = Region.dstOffsets[1].y;
    int64_t SrcMinX = std::min(SrcX0, SrcX1), SrcMaxX = std::max(SrcX0, SrcX1);
    int64_t SrcMinY = std::min(SrcY0, SrcY1), SrcMaxY = std::max(SrcY0, SrcY1);
    uint32_t DstWidth = uint32_t(std::abs(DstX1 - DstX0));
    uint32_t DstHeight = uint32_t(std::abs(DstY1 - DstY0));
    int64_t DstStepX = DstX1 >= DstX0 ? 1 : -1;
    int64_t DstStepY = DstY1 >= DstY0 ? 1 : -1;
    uint32_t LayerCount = std::min(Region.srcSubresource.layerCount,
                                   Region.dstSubresource.layerCount);

    for (uint32_t Layer = 0; Layer != LayerCount; ++Layer) {
      uint32_t SrcLayer = Region.srcSubresource.baseArrayLayer + Layer;
      uint32_t DstLayer = Region.dstSubresource.baseArrayLayer + Layer;
      for (uint32_t Y = 0; Y != DstHeight; ++Y) {
        for (uint32_t X = 0; X != DstWidth; ++X) {
          // Interpolate the destination texel's fraction across [0, 1] of
          // the destination rectangle, then find the source-space position
          // that same fraction names between the source rectangle's own
          // two (possibly reversed) corners -- one formula for every
          // combination of mirrored/unmirrored source and destination.
          double Tx = (X + 0.5) / DstWidth;
          double Ty = (Y + 0.5) / DstHeight;
          double U = SrcX0 + Tx * (SrcX1 - SrcX0);
          double V = SrcY0 + Ty * (SrcY1 - SrcY0);
          int64_t DstX = DstX0 + int64_t(X) * DstStepX;
          int64_t DstY = DstY0 + int64_t(Y) * DstStepY;
          void *DstTexel = Dst->texelPointer(DstLevel, DstLayer, uint32_t(DstX),
                                             uint32_t(DstY),
                                             uint32_t(Region.dstOffsets[0].z));

          auto srcTexel = [&](int64_t SX, int64_t SY) {
            SX = std::clamp<int64_t>(SX, SrcMinX, SrcMaxX - 1);
            SY = std::clamp<int64_t>(SY, SrcMinY, SrcMaxY - 1);
            return Src->texelPointer(SrcLevel, SrcLayer, uint32_t(SX),
                                     uint32_t(SY),
                                     uint32_t(Region.srcOffsets[0].z));
          };

          if (Filter == VK_FILTER_NEAREST) {
            if (SameFormat) {
              std::memcpy(DstTexel, srcTexel(int64_t(U), int64_t(V)),
                          SrcTexelSize);
              continue;
            }
            std::array<double, 4> Sample{};
            if (Error E = feme::graphics::unpackColor(
                    Src->format(),
                    ArrayRef<uint8_t>(static_cast<const uint8_t *>(
                                          srcTexel(int64_t(U), int64_t(V))),
                                      SrcTexelSize),
                    Sample))
              return E;
            MutableArrayRef<uint8_t> Out(static_cast<uint8_t *>(DstTexel),
                                         DstTexelSize);
            if (Error E =
                    feme::graphics::packClearColor(Dst->format(), Sample, Out))
              return E;
            continue;
          }

          // Bilinear: unpack the four neighbors, weight them, repack into
          // the destination's own format.
          double FX = U - 0.5, FY = V - 0.5;
          int64_t X0 = int64_t(std::floor(FX)), Y0 = int64_t(std::floor(FY));
          double WX = FX - X0, WY = FY - Y0;
          std::array<double, 4> Accum{};
          const std::pair<int64_t, int64_t> Neighbors[4] = {
              {X0, Y0}, {X0 + 1, Y0}, {X0, Y0 + 1}, {X0 + 1, Y0 + 1}};
          const double Weights[4] = {(1 - WX) * (1 - WY), WX * (1 - WY),
                                     (1 - WX) * WY, WX * WY};
          for (unsigned N = 0; N != 4; ++N) {
            std::array<double, 4> Sample{};
            if (Error E = feme::graphics::unpackColor(
                    Src->format(),
                    ArrayRef<uint8_t>(
                        static_cast<const uint8_t *>(
                            srcTexel(Neighbors[N].first, Neighbors[N].second)),
                        SrcTexelSize),
                    Sample))
              return E;
            for (unsigned C = 0; C != 4; ++C)
              Accum[C] += Sample[C] * Weights[N];
          }
          MutableArrayRef<uint8_t> Out(static_cast<uint8_t *>(DstTexel),
                                       DstTexelSize);
          if (Error E =
                  feme::graphics::packClearColor(Dst->format(), Accum, Out))
            return E;
        }
      }
    }
  }
  return Error::success();
}

Error runResolveImage(Image *Src, Image *Dst,
                      ArrayRef<VkImageResolve> Regions) {
  if (Error E = checkImagePair(Src, Dst, "resolve"))
    return E;
  if (Dst->sampleCount() != 1)
    return createStringError(inconvertibleErrorCode(),
                             "a resolve destination must be single-sample");
  uint32_t Samples = Src->sampleCount();
  uint32_t TexelSize = formatElementSize(Src->format());

  for (const VkImageResolve &Region : Regions) {
    uint32_t SrcLevel = Region.srcSubresource.mipLevel;
    uint32_t DstLevel = Region.dstSubresource.mipLevel;
    if (SrcLevel >= Src->mipLevels() || DstLevel >= Dst->mipLevels())
      return createStringError(inconvertibleErrorCode(),
                               "a resolve region names a mip level out of "
                               "range");
    uint32_t LayerCount = std::min(Region.srcSubresource.layerCount,
                                   Region.dstSubresource.layerCount);
    for (uint32_t Layer = 0; Layer != LayerCount; ++Layer)
      for (uint32_t Z = 0; Z != Region.extent.depth; ++Z)
        for (uint32_t Y = 0; Y != Region.extent.height; ++Y)
          for (uint32_t X = 0; X != Region.extent.width; ++X) {
            std::array<double, 4> Accum{};
            for (uint32_t S = 0; S != Samples; ++S) {
              const void *SrcTexel = Src->texelPointer(
                  SrcLevel, Region.srcSubresource.baseArrayLayer + Layer,
                  Region.srcOffset.x + X, Region.srcOffset.y + Y,
                  Region.srcOffset.z + Z, S);
              std::array<double, 4> Sample{};
              if (Error E = feme::graphics::unpackColor(
                      Src->format(),
                      ArrayRef<uint8_t>(static_cast<const uint8_t *>(SrcTexel),
                                        TexelSize),
                      Sample))
                return E;
              for (unsigned C = 0; C != 4; ++C)
                Accum[C] += Sample[C];
            }
            for (unsigned C = 0; C != 4; ++C)
              Accum[C] /= Samples;
            void *DstTexel = Dst->texelPointer(
                DstLevel, Region.dstSubresource.baseArrayLayer + Layer,
                Region.dstOffset.x + X, Region.dstOffset.y + Y,
                Region.dstOffset.z + Z);
            MutableArrayRef<uint8_t> Out(static_cast<uint8_t *>(DstTexel),
                                         TexelSize);
            if (Error E =
                    feme::graphics::packClearColor(Src->format(), Accum, Out))
              return E;
          }
  }
  return Error::success();
}

} // namespace feme::vulkan
