//===- ImageOps.cpp - Attachment clears, blits, and resolves -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ImageOps.h"
#include "ASTCDecode.h"
#include "Format.h"
#include "Image.h"

#include "feme/Graphics/ImageFixture.h"

#include "llvm/ADT/STLFunctionalExtras.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

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

/// Fills the aspect(s) \p AspectMask selects (`DEPTH_BIT`/`STENCIL_BIT`) of
/// every texel (and sample) of subresource (\p Level, \p Layer) of \p Img
/// with \p Value, as a per-texel read-modify-write: a combined
/// depth+stencil format (`D24_UNORM_S8_UINT`, roadmap C1) shares one word
/// of storage between both aspects, so clearing only one must leave the
/// other's existing bits untouched.
Error fillDepthStencilSubresource(Image &Img, uint32_t Level, uint32_t Layer,
                                  VkImageAspectFlags AspectMask,
                                  const VkClearDepthStencilValue &Value) {
  uint32_t Width = std::max(1u, Img.width() >> Level);
  uint32_t Height = std::max(1u, Img.height() >> Level);
  uint32_t Depth = std::max(1u, Img.depth() >> Level);
  uint32_t ElemSize = formatElementSize(Img.format());
  for (uint32_t Z = 0; Z != Depth; ++Z)
    for (uint32_t Y = 0; Y != Height; ++Y)
      for (uint32_t X = 0; X != Width; ++X)
        for (uint32_t S = 0; S != Img.sampleCount(); ++S) {
          MutableArrayRef<uint8_t> Texel(
              static_cast<uint8_t *>(
                  Img.texelPointer(Level, Layer, X, Y, Z, S)),
              ElemSize);
          if (AspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
            if (Error E = feme::graphics::packDepthClear(
                    Img.format(), Value.depth, Texel))
              return E;
          if (AspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
            if (Error E = feme::graphics::packStencilClear(
                    Img.format(), Value.stencil, Texel))
              return E;
        }
  return Error::success();
}

Error clearColorImageRanges(Image *Img, const VkClearColorValue &Color,
                            ArrayRef<VkImageSubresourceRange> Ranges) {
  if (!Img || !Img->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "the cleared image is not bound to memory");
  Expected<std::vector<uint8_t>> Texel = packTexel(
      Img->format(), {Color.float32[0], Color.float32[1], Color.float32[2],
                      Color.float32[3]});
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

Error clearDepthStencilImageRanges(Image *Img,
                                   const VkClearDepthStencilValue &Value,
                                   ArrayRef<VkImageSubresourceRange> Ranges) {
  if (!Img || !Img->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "the cleared image is not bound to memory");

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
        if (Error E = fillDepthStencilSubresource(
                *Img, Range.baseMipLevel + L, Range.baseArrayLayer + A,
                Range.aspectMask, Value))
          return E;
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
  return clearColorImageRanges(Img, Color, Ranges);
}

Error runClearDepthStencilImage(Image *Img,
                                const VkClearDepthStencilValue &DepthStencil,
                                ArrayRef<VkImageSubresourceRange> Ranges) {
  return clearDepthStencilImageRanges(Img, DepthStencil, Ranges);
}

/// Clears one attachment over one rectangle, writing directly into each
/// covered texel's own bytes via \p Write -- a per-pixel read-modify-write
/// rather than a precomputed uniform value, so a partial (single-aspect)
/// clear of a combined depth+stencil attachment never clobbers the other
/// aspect's bits (roadmap C1).
Error clearAttachmentRects(const RenderTargetView &Target,
                          ArrayRef<VkClearRect> Rects,
                          llvm::function_ref<Error(MutableArrayRef<uint8_t>)>
                              Write) {
  Expected<feme::graphics::AttachmentView> View =
      resolveAttachmentView(Target.View);
  if (!View)
    return View.takeError();
  Expected<uint32_t> ElemSize =
      feme::graphics::getFixtureFormatElementSize(View->Format);
  if (!ElemSize)
    return ElemSize.takeError();

  for (const VkClearRect &Rect : Rects) {
    uint32_t MinX = std::max<int32_t>(0, Rect.rect.offset.x);
    uint32_t MinY = std::max<int32_t>(0, Rect.rect.offset.y);
    uint32_t MaxX = std::min<uint64_t>(View->Width,
                                       uint64_t(MinX) + Rect.rect.extent.width);
    uint32_t MaxY = std::min<uint64_t>(
        View->Height, uint64_t(MinY) + Rect.rect.extent.height);
    for (uint32_t Y = MinY; Y < MaxY; ++Y)
      for (uint32_t X = MinX; X < MaxX; ++X)
        for (uint32_t S = 0; S != Target.SampleCount; ++S) {
          size_t Offset =
              (((size_t)Y * View->Width + X) * Target.SampleCount + S) *
              *ElemSize;
          if (Error E =
                  Write(MutableArrayRef<uint8_t>(View->Data.data() + Offset,
                                                 *ElemSize)))
            return E;
        }
  }
  return Error::success();
}

Error runClearAttachments(const RenderTargetBinding &Binding,
                          ArrayRef<VkClearAttachment> Attachments,
                          ArrayRef<VkClearRect> Rects) {
  for (const VkClearAttachment &Clear : Attachments) {
    bool WantColor = Clear.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT;
    bool WantDepth = Clear.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT;
    bool WantStencil = Clear.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT;
    if (!WantColor && !WantDepth && !WantStencil)
      return createStringError(inconvertibleErrorCode(),
                               "vkCmdClearAttachments names no aspect");

    if (WantColor) {
      if (Clear.colorAttachment >= Binding.Colors.size())
        return createStringError(inconvertibleErrorCode(),
                                 "vkCmdClearAttachments names color "
                                 "attachment %u, which is not bound",
                                 Clear.colorAttachment);
      const RenderTargetView &Target = Binding.Colors[Clear.colorAttachment];
      Expected<feme::graphics::AttachmentView> View =
          resolveAttachmentView(Target.View);
      if (!View)
        return View.takeError();
      Expected<std::vector<uint8_t>> Texel = packTexel(
          View->Format, {Clear.clearValue.color.float32[0],
                        Clear.clearValue.color.float32[1],
                        Clear.clearValue.color.float32[2],
                        Clear.clearValue.color.float32[3]});
      if (!Texel)
        return Texel.takeError();
      if (Error E = clearAttachmentRects(
              Target, Rects, [&](MutableArrayRef<uint8_t> Texel_) -> Error {
                std::memcpy(Texel_.data(), Texel->data(), Texel_.size());
                return Error::success();
              }))
        return E;
    }
    // Depth and stencil, unlike color, may be named together in one
    // `VkClearAttachment` (a single combined-format attachment cleared in
    // one call); handle them independently rather than as mutually
    // exclusive so that case clears both halves.
    if (WantDepth) {
      if (!Binding.Depth)
        return createStringError(inconvertibleErrorCode(),
                                 "vkCmdClearAttachments names the depth "
                                 "attachment, which is not bound");
      feme::cpu::ResourceFormat Format = Binding.Depth->Format;
      double Depth = Clear.clearValue.depthStencil.depth;
      if (Error E = clearAttachmentRects(
              *Binding.Depth, Rects, [&](MutableArrayRef<uint8_t> Texel) {
                return feme::graphics::packDepthClear(Format, Depth, Texel);
              }))
        return E;
    }
    if (WantStencil) {
      if (!Binding.Stencil)
        return createStringError(inconvertibleErrorCode(),
                                 "vkCmdClearAttachments names the stencil "
                                 "attachment, which is not bound");
      feme::cpu::ResourceFormat Format = Binding.Stencil->Format;
      uint32_t Stencil = Clear.clearValue.depthStencil.stencil;
      if (Error E = clearAttachmentRects(
              *Binding.Stencil, Rects, [&](MutableArrayRef<uint8_t> Texel) {
                return feme::graphics::packStencilClear(Format, Stencil,
                                                        Texel);
              }))
        return E;
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
  // Roadmap E22: a block-compressed *destination* is rejected outright --
  // this ICD's ASTC support is decode-only (ASTCDecode.h), and a blit's
  // own resample-then-repack pipeline has no encoder to repack into one.
  // A block-compressed *source* is supported, but only the LDR half:
  // `decodeASTCBlock` is LDR-only (its HDR counterpart,
  // `decodeASTCBlockHDR`, produces floats through a different interface
  // than the UNORM8 one every other format's unpack/pack path here
  // shares), so an HDR ASTC source is rejected the same "not implemented"
  // way.
  if (feme::cpu::isBlockCompressedFormat(Dst->format()))
    return createStringError(inconvertibleErrorCode(),
                             "blitting to a block-compressed destination is "
                             "not implemented (no ASTC encoder exists)");
  bool SrcCompressed = feme::cpu::isBlockCompressedFormat(Src->format());
  if (SrcCompressed && !feme::cpu::isASTCLdrFormat(Src->format()))
    return createStringError(inconvertibleErrorCode(),
                             "blitting an HDR ASTC source is not "
                             "implemented (decodeASTCBlock is LDR-only)");
  uint32_t SrcTexelSize = SrcCompressed ? 0 : formatElementSize(Src->format());
  uint32_t DstTexelSize = formatElementSize(Dst->format());
  uint32_t SrcBlockW = blockWidth(Src->format());
  uint32_t SrcBlockH = blockHeight(Src->format());
  // Reused across every decoded source texel a compressed blit's own
  // region loop below fetches, rather than reallocated per texel.
  std::vector<uint8_t> DecodeBuf(size_t(SrcBlockW) * SrcBlockH * 4);
  // A same-format nearest blit copies raw bytes verbatim (no unpack/repack
  // rounding); anything else goes through the central pack table, exactly
  // as the bilinear path always has. Never true when `SrcCompressed` (its
  // destination is always rejected above, so the formats can never match).
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
    // VK_EXT_image_robustness/robustImageAccess (roadmap E16): a region
    // naming a source/destination coordinate beyond this mip level's own
    // declared extent must read a defined value or discard the write,
    // never fault -- the source rectangle's own corners below are clamped
    // into `[0, SrcLevelWidth)`/`[0, SrcLevelHeight)` before every texel
    // fetch, and a destination coordinate outside `[0, DstLevelWidth)`/
    // `[0, DstLevelHeight)` (`texelPointer` returning null) discards that
    // texel's write.
    uint32_t SrcLevelWidth = std::max(1u, Src->width() >> SrcLevel);
    uint32_t SrcLevelHeight = std::max(1u, Src->height() >> SrcLevel);
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
          // Roadmap E16: a destination coordinate outside the image's
          // declared extent discards this texel's write rather than
          // faulting through a null pointer.
          if (!DstTexel)
            continue;

          auto srcTexel = [&](int64_t SX, int64_t SY) {
            SX = std::clamp<int64_t>(SX, SrcMinX, SrcMaxX - 1);
            SY = std::clamp<int64_t>(SY, SrcMinY, SrcMaxY - 1);
            // Roadmap E16: clamp again into the mip's own declared extent,
            // so a source rectangle naming out-of-bounds coordinates still
            // reads a defined (edge-clamped) texel instead of faulting.
            SX = std::clamp<int64_t>(SX, 0, int64_t(SrcLevelWidth) - 1);
            SY = std::clamp<int64_t>(SY, 0, int64_t(SrcLevelHeight) - 1);
            return Src->texelPointer(SrcLevel, SrcLayer, uint32_t(SX),
                                     uint32_t(SY),
                                     uint32_t(Region.srcOffsets[0].z));
          };

          // Unpacks the source texel at (\p SX, \p SY) (clamped into the
          // source rectangle) into a normalized color: through
          // `feme::vulkan::decodeASTCBlock` and then `R8G8B8A8_UNORM`'s
          // own unpack case when `SrcCompressed` (roadmap E22's only
          // caller of `decodeASTCBlock` -- a copy never decodes, it
          // reinterprets whole blocks verbatim; see `runCopyImage`'s
          // comment), or through the ordinary per-format unpack table
          // otherwise.
          auto srcColor = [&](int64_t SX, int64_t SY,
                              std::array<double, 4> &Out) -> Error {
            SX = std::clamp<int64_t>(SX, SrcMinX, SrcMaxX - 1);
            SY = std::clamp<int64_t>(SY, SrcMinY, SrcMaxY - 1);
            // Roadmap E16: same additional clamp into the mip's own
            // declared extent as `srcTexel` above.
            SX = std::clamp<int64_t>(SX, 0, int64_t(SrcLevelWidth) - 1);
            SY = std::clamp<int64_t>(SY, 0, int64_t(SrcLevelHeight) - 1);
            if (!SrcCompressed)
              return feme::graphics::unpackColor(
                  Src->format(),
                  ArrayRef<uint8_t>(
                      static_cast<const uint8_t *>(Src->texelPointer(
                          SrcLevel, SrcLayer, uint32_t(SX), uint32_t(SY),
                          uint32_t(Region.srcOffsets[0].z))),
                      SrcTexelSize),
                  Out);
            uint32_t TX = uint32_t(SX), TY = uint32_t(SY);
            uint32_t BlockX = TX / SrcBlockW, BlockY = TY / SrcBlockH;
            const auto *Block = static_cast<const uint8_t *>(
                Src->blockPointer(SrcLevel, SrcLayer, BlockX, BlockY,
                                  uint32_t(Region.srcOffsets[0].z)));
            decodeASTCBlock(Block, SrcBlockW, SrcBlockH, DecodeBuf.data());
            uint32_t InBlockX = TX % SrcBlockW, InBlockY = TY % SrcBlockH;
            const uint8_t *Texel =
                &DecodeBuf[(size_t(InBlockY) * SrcBlockW + InBlockX) * 4];
            return feme::graphics::unpackColor(
                feme::cpu::ResourceFormat::R8G8B8A8_UNORM,
                ArrayRef<uint8_t>(Texel, 4), Out);
          };

          if (Filter == VK_FILTER_NEAREST) {
            if (SameFormat) {
              std::memcpy(DstTexel, srcTexel(int64_t(U), int64_t(V)),
                          SrcTexelSize);
              continue;
            }
            std::array<double, 4> Sample{};
            if (Error E = srcColor(int64_t(U), int64_t(V), Sample))
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
            if (Error E = srcColor(Neighbors[N].first, Neighbors[N].second,
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
  // Roadmap E22: a block-compressed format is never multisampled in real
  // Vulkan (`Image.h`'s file comment already relied on this to keep
  // `computeSubresourceLayouts`'s `SampleStride` math simple), so a
  // resolve of one is meaningless input rather than a case this ICD
  // silently mishandles by calling the now block-compressed-unaware
  // `texelPointer` below.
  if (feme::cpu::isBlockCompressedFormat(Src->format()))
    return createStringError(inconvertibleErrorCode(),
                             "resolving a block-compressed image is not "
                             "meaningful (never multisampled)");
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
            void *DstTexel = Dst->texelPointer(
                DstLevel, Region.dstSubresource.baseArrayLayer + Layer,
                Region.dstOffset.x + X, Region.dstOffset.y + Y,
                Region.dstOffset.z + Z);
            // Roadmap E16 (VK_EXT_image_robustness/robustImageAccess): a
            // destination coordinate beyond Dst's declared extent
            // discards this whole texel (read and write) rather than
            // faulting through a null pointer.
            if (!DstTexel)
              continue;

            std::array<double, 4> Accum{};
            bool SrcOutOfBounds = false;
            for (uint32_t S = 0; S != Samples; ++S) {
              const void *SrcTexel = Src->texelPointer(
                  SrcLevel, Region.srcSubresource.baseArrayLayer + Layer,
                  Region.srcOffset.x + X, Region.srcOffset.y + Y,
                  Region.srcOffset.z + Z, S);
              // Same discard for a source coordinate beyond Src's own
              // declared extent.
              if (!SrcTexel) {
                SrcOutOfBounds = true;
                break;
              }
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
            if (SrcOutOfBounds)
              continue;
            for (unsigned C = 0; C != 4; ++C)
              Accum[C] /= Samples;
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
