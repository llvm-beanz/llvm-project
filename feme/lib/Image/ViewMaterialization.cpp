//===- ViewMaterialization.cpp - API-neutral image-view normalization ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Image/ViewMaterialization.h"

#include <algorithm>

using namespace feme::image;

ImageViewExtent feme::image::resolveImageViewExtent(
    uint32_t ImageWidth, uint32_t ImageHeight, uint32_t ImageDepth,
    uint32_t ImageMipLevels, uint32_t ImageArrayLayers, bool ViewIsTexture3D,
    uint32_t BaseMipLevel, uint32_t LevelCount, uint32_t BaseArrayLayer,
    uint32_t LayerCount) {
  ImageViewExtent Result;

  if (BaseArrayLayer >= ImageArrayLayers)
    return Result;
  uint32_t ResolvedLayerCount = LayerCount == kRemainingImageViewExtent
                                    ? ImageArrayLayers - BaseArrayLayer
                                    : LayerCount;
  ResolvedLayerCount =
      std::min(ResolvedLayerCount, ImageArrayLayers - BaseArrayLayer);
  if (ResolvedLayerCount == 0)
    return Result;

  if (BaseMipLevel >= ImageMipLevels)
    return Result;
  uint32_t ResolvedLevelCount = LevelCount == kRemainingImageViewExtent
                                    ? ImageMipLevels - BaseMipLevel
                                    : LevelCount;
  ResolvedLevelCount =
      std::min(ResolvedLevelCount, ImageMipLevels - BaseMipLevel);
  if (ResolvedLevelCount == 0)
    return Result;

  Result.Width = std::max(1u, ImageWidth >> BaseMipLevel);
  Result.Height = std::max(1u, ImageHeight >> BaseMipLevel);
  Result.Depth =
      ViewIsTexture3D ? std::max(1u, ImageDepth >> BaseMipLevel) : 1;
  Result.MipLevels = ResolvedLevelCount;
  Result.ArrayLayers = ResolvedLayerCount;
  Result.Valid = true;
  return Result;
}

uint32_t feme::image::compressedFormatBorderComponentMask(
    feme::cpu::ResourceFormat Format) {
  switch (Format) {
  case feme::cpu::ResourceFormat::BC1_RGB_UNORM:
  case feme::cpu::ResourceFormat::BC1_RGB_SRGB:
  case feme::cpu::ResourceFormat::BC6H_UFLOAT:
  case feme::cpu::ResourceFormat::BC6H_SFLOAT:
  case feme::cpu::ResourceFormat::ETC2_RGB8_UNORM:
  case feme::cpu::ResourceFormat::ETC2_RGB8_SRGB:
    return 0x7u; // R, G, B.
  case feme::cpu::ResourceFormat::BC4_UNORM:
  case feme::cpu::ResourceFormat::BC4_SNORM:
  case feme::cpu::ResourceFormat::EAC_R11_UNORM:
  case feme::cpu::ResourceFormat::EAC_R11_SNORM:
    return 0x1u; // R only.
  case feme::cpu::ResourceFormat::BC5_UNORM:
  case feme::cpu::ResourceFormat::BC5_SNORM:
  case feme::cpu::ResourceFormat::EAC_R11G11_UNORM:
  case feme::cpu::ResourceFormat::EAC_R11G11_SNORM:
    return 0x3u; // R, G.
  default:
    return 0u; // Already 4-channel (or ASTC/uncompressed): no override.
  }
}

uint32_t feme::image::effectiveViewLayerCount(bool ImageIsTexture3D,
                                              bool ViewIsTexture3D,
                                              uint32_t ImageDepth,
                                              uint32_t ImageArrayLayers) {
  return ImageIsTexture3D && !ViewIsTexture3D ? ImageDepth : ImageArrayLayers;
}

uint32_t feme::image::resolveAttachmentViewLayerCount(
    bool ImageIsTexture3D, bool ViewIsTexture3D, uint32_t ImageDepth,
    uint32_t ImageArrayLayers, uint32_t BaseArrayLayer,
    uint32_t RequestedLayerCount) {
  uint32_t Effective = effectiveViewLayerCount(ImageIsTexture3D, ViewIsTexture3D,
                                               ImageDepth, ImageArrayLayers);
  return RequestedLayerCount == kRemainingImageViewExtent
             ? Effective - BaseArrayLayer
             : RequestedLayerCount;
}
