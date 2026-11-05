//===- BlockGeometry.cpp - API-neutral format/block geometry ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See BlockGeometry.h's file comment: this is roadmap WS0's first
// extraction out of `feme::vulkan::Format.cpp`, moved verbatim (only the
// namespace changed) since none of it ever depended on Vulkan.
//
//===----------------------------------------------------------------------===//

#include "feme/Image/BlockGeometry.h"

#include "llvm/Support/ErrorHandling.h"

using feme::cpu::ResourceFormat;
using feme::cpu::isBlockCompressedFormat;

uint32_t feme::image::formatElementSize(ResourceFormat Format) {
  switch (Format) {
  case ResourceFormat::Unknown:
    return 0;
  case ResourceFormat::R32_FLOAT:
  case ResourceFormat::R32_UINT:
  case ResourceFormat::R32_SINT:
  case ResourceFormat::R8G8B8A8_UNORM:
  case ResourceFormat::R8G8B8A8_SNORM:
  case ResourceFormat::R8G8B8A8_UINT:
  case ResourceFormat::R8G8B8A8_SINT:
  case ResourceFormat::R8G8B8A8_UNORM_SRGB:
  case ResourceFormat::B8G8R8A8_UNORM:
  case ResourceFormat::B8G8R8A8_UNORM_SRGB:
  case ResourceFormat::R11G11B10_FLOAT:
  case ResourceFormat::R10G10B10A2_UNORM:
  case ResourceFormat::R10G10B10A2_UINT:
  case ResourceFormat::R10G10B10A2_SNORM:
  case ResourceFormat::R10G10B10A2_SINT:
  case ResourceFormat::E5B9G9R9_UFLOAT:
  case ResourceFormat::D32_FLOAT:
  case ResourceFormat::D24_UNORM_S8_UINT:
    return 4;
  case ResourceFormat::R32G32_FLOAT:
  case ResourceFormat::R32G32_UINT:
  case ResourceFormat::R32G32_SINT:
  case ResourceFormat::R16G16B16A16_FLOAT:
  case ResourceFormat::R16G16B16A16_UNORM:
  case ResourceFormat::R16G16B16A16_SNORM:
  case ResourceFormat::R16G16B16A16_UINT:
  case ResourceFormat::R16G16B16A16_SINT:
  case ResourceFormat::D32_FLOAT_S8X24_UINT:
    return 8;
  case ResourceFormat::R32G32B32_FLOAT:
  case ResourceFormat::R32G32B32_UINT:
  case ResourceFormat::R32G32B32_SINT:
    return 12;
  case ResourceFormat::R32G32B32A32_FLOAT:
  case ResourceFormat::R32G32B32A32_UINT:
  case ResourceFormat::R32G32B32A32_SINT:
    return 16;
  case ResourceFormat::D16_UNORM:
    return 2;
  case ResourceFormat::S8_UINT:
    return 1;
  case ResourceFormat::R8_UNORM:
  case ResourceFormat::R8_SNORM:
  case ResourceFormat::R8_UINT:
  case ResourceFormat::R8_SINT:
    return 1;
  case ResourceFormat::R8G8_UNORM:
  case ResourceFormat::R8G8_SNORM:
  case ResourceFormat::R8G8_UINT:
  case ResourceFormat::R8G8_SINT:
    return 2;
  case ResourceFormat::R16_FLOAT:
  case ResourceFormat::R16_UNORM:
  case ResourceFormat::R16_SNORM:
  case ResourceFormat::R16_UINT:
  case ResourceFormat::R16_SINT:
    return 2;
  case ResourceFormat::R16G16_FLOAT:
  case ResourceFormat::R16G16_UNORM:
  case ResourceFormat::R16G16_SNORM:
  case ResourceFormat::R16G16_UINT:
  case ResourceFormat::R16G16_SINT:
    return 4;
  case ResourceFormat::A8_UNORM:
    return 1;
  case ResourceFormat::A1B5G5R5_UNORM:
    return 2;
  case ResourceFormat::A4R4G4B4_UNORM:
  case ResourceFormat::A4B4G4R4_UNORM:
    return 2;
  case ResourceFormat::R4G4B4A4_UNORM:
  case ResourceFormat::B4G4R4A4_UNORM:
  case ResourceFormat::R5G6B5_UNORM:
  case ResourceFormat::B5G6R5_UNORM:
  case ResourceFormat::R5G5B5A1_UNORM:
  case ResourceFormat::B5G5R5A1_UNORM:
  case ResourceFormat::A1R5G5B5_UNORM:
    return 2;
  // Block-compressed formats have no single-texel size -- see
  // `bytesPerBlock` for their whole-block size instead.
  case ResourceFormat::ASTC_4x4_UNORM:
  case ResourceFormat::ASTC_4x4_SRGB:
  case ResourceFormat::ASTC_5x4_UNORM:
  case ResourceFormat::ASTC_5x4_SRGB:
  case ResourceFormat::ASTC_5x5_UNORM:
  case ResourceFormat::ASTC_5x5_SRGB:
  case ResourceFormat::ASTC_6x5_UNORM:
  case ResourceFormat::ASTC_6x5_SRGB:
  case ResourceFormat::ASTC_6x6_UNORM:
  case ResourceFormat::ASTC_6x6_SRGB:
  case ResourceFormat::ASTC_8x5_UNORM:
  case ResourceFormat::ASTC_8x5_SRGB:
  case ResourceFormat::ASTC_8x6_UNORM:
  case ResourceFormat::ASTC_8x6_SRGB:
  case ResourceFormat::ASTC_8x8_UNORM:
  case ResourceFormat::ASTC_8x8_SRGB:
  case ResourceFormat::ASTC_10x5_UNORM:
  case ResourceFormat::ASTC_10x5_SRGB:
  case ResourceFormat::ASTC_10x6_UNORM:
  case ResourceFormat::ASTC_10x6_SRGB:
  case ResourceFormat::ASTC_10x8_UNORM:
  case ResourceFormat::ASTC_10x8_SRGB:
  case ResourceFormat::ASTC_10x10_UNORM:
  case ResourceFormat::ASTC_10x10_SRGB:
  case ResourceFormat::ASTC_12x10_UNORM:
  case ResourceFormat::ASTC_12x10_SRGB:
  case ResourceFormat::ASTC_12x12_UNORM:
  case ResourceFormat::ASTC_12x12_SRGB:
  case ResourceFormat::ASTC_4x4_SFLOAT:
  case ResourceFormat::ASTC_5x4_SFLOAT:
  case ResourceFormat::ASTC_5x5_SFLOAT:
  case ResourceFormat::ASTC_6x5_SFLOAT:
  case ResourceFormat::ASTC_6x6_SFLOAT:
  case ResourceFormat::ASTC_8x5_SFLOAT:
  case ResourceFormat::ASTC_8x6_SFLOAT:
  case ResourceFormat::ASTC_8x8_SFLOAT:
  case ResourceFormat::ASTC_10x5_SFLOAT:
  case ResourceFormat::ASTC_10x6_SFLOAT:
  case ResourceFormat::ASTC_10x8_SFLOAT:
  case ResourceFormat::ASTC_10x10_SFLOAT:
  case ResourceFormat::ASTC_12x10_SFLOAT:
  case ResourceFormat::ASTC_12x12_SFLOAT:
    return 0;
  case ResourceFormat::BC1_RGB_UNORM:
  case ResourceFormat::BC1_RGB_SRGB:
  case ResourceFormat::BC1_RGBA_UNORM:
  case ResourceFormat::BC1_RGBA_SRGB:
  case ResourceFormat::BC2_UNORM:
  case ResourceFormat::BC2_SRGB:
  case ResourceFormat::BC3_UNORM:
  case ResourceFormat::BC3_SRGB:
  case ResourceFormat::BC4_UNORM:
  case ResourceFormat::BC4_SNORM:
  case ResourceFormat::BC5_UNORM:
  case ResourceFormat::BC5_SNORM:
  case ResourceFormat::BC6H_UFLOAT:
  case ResourceFormat::BC6H_SFLOAT:
  case ResourceFormat::BC7_UNORM:
  case ResourceFormat::BC7_SRGB:
    return 0;
  case ResourceFormat::ETC2_RGB8_UNORM:
  case ResourceFormat::ETC2_RGB8_SRGB:
  case ResourceFormat::ETC2_RGB8A1_UNORM:
  case ResourceFormat::ETC2_RGB8A1_SRGB:
  case ResourceFormat::ETC2_RGBA8_UNORM:
  case ResourceFormat::ETC2_RGBA8_SRGB:
  case ResourceFormat::EAC_R11_UNORM:
  case ResourceFormat::EAC_R11_SNORM:
  case ResourceFormat::EAC_R11G11_UNORM:
  case ResourceFormat::EAC_R11G11_SNORM:
    return 0;
  }
  llvm_unreachable("unhandled ResourceFormat");
}

namespace {

/// One block-compressed format's block footprint, in texels -- see
/// BlockGeometry.h's file comment. Every ASTC block is 128 bits regardless
/// of its footprint, so only width/height vary here.
struct BlockShape {
  uint32_t Width;
  uint32_t Height;
};

/// Returns \p Format's `BlockShape`, or `{1, 1}` for a non-block-compressed
/// format (including one unrecognized by the caller) -- see
/// `blockWidth`/`blockHeight`'s "always call this" comment in
/// BlockGeometry.h.
BlockShape blockShape(ResourceFormat Format) {
  switch (Format) {
  case ResourceFormat::ASTC_4x4_UNORM:
  case ResourceFormat::ASTC_4x4_SRGB:
    return {4, 4};
  case ResourceFormat::ASTC_5x4_UNORM:
  case ResourceFormat::ASTC_5x4_SRGB:
    return {5, 4};
  case ResourceFormat::ASTC_5x5_UNORM:
  case ResourceFormat::ASTC_5x5_SRGB:
    return {5, 5};
  case ResourceFormat::ASTC_6x5_UNORM:
  case ResourceFormat::ASTC_6x5_SRGB:
    return {6, 5};
  case ResourceFormat::ASTC_6x6_UNORM:
  case ResourceFormat::ASTC_6x6_SRGB:
    return {6, 6};
  case ResourceFormat::ASTC_8x5_UNORM:
  case ResourceFormat::ASTC_8x5_SRGB:
    return {8, 5};
  case ResourceFormat::ASTC_8x6_UNORM:
  case ResourceFormat::ASTC_8x6_SRGB:
    return {8, 6};
  case ResourceFormat::ASTC_8x8_UNORM:
  case ResourceFormat::ASTC_8x8_SRGB:
    return {8, 8};
  case ResourceFormat::ASTC_10x5_UNORM:
  case ResourceFormat::ASTC_10x5_SRGB:
    return {10, 5};
  case ResourceFormat::ASTC_10x6_UNORM:
  case ResourceFormat::ASTC_10x6_SRGB:
    return {10, 6};
  case ResourceFormat::ASTC_10x8_UNORM:
  case ResourceFormat::ASTC_10x8_SRGB:
    return {10, 8};
  case ResourceFormat::ASTC_10x10_UNORM:
  case ResourceFormat::ASTC_10x10_SRGB:
    return {10, 10};
  case ResourceFormat::ASTC_12x10_UNORM:
  case ResourceFormat::ASTC_12x10_SRGB:
    return {12, 10};
  case ResourceFormat::ASTC_12x12_UNORM:
  case ResourceFormat::ASTC_12x12_SRGB:
    return {12, 12};
  case ResourceFormat::ASTC_4x4_SFLOAT:
    return {4, 4};
  case ResourceFormat::ASTC_5x4_SFLOAT:
    return {5, 4};
  case ResourceFormat::ASTC_5x5_SFLOAT:
    return {5, 5};
  case ResourceFormat::ASTC_6x5_SFLOAT:
    return {6, 5};
  case ResourceFormat::ASTC_6x6_SFLOAT:
    return {6, 6};
  case ResourceFormat::ASTC_8x5_SFLOAT:
    return {8, 5};
  case ResourceFormat::ASTC_8x6_SFLOAT:
    return {8, 6};
  case ResourceFormat::ASTC_8x8_SFLOAT:
    return {8, 8};
  case ResourceFormat::ASTC_10x5_SFLOAT:
    return {10, 5};
  case ResourceFormat::ASTC_10x6_SFLOAT:
    return {10, 6};
  case ResourceFormat::ASTC_10x8_SFLOAT:
    return {10, 8};
  case ResourceFormat::ASTC_10x10_SFLOAT:
    return {10, 10};
  case ResourceFormat::ASTC_12x10_SFLOAT:
    return {12, 10};
  case ResourceFormat::ASTC_12x12_SFLOAT:
    return {12, 12};
  case ResourceFormat::BC1_RGB_UNORM:
  case ResourceFormat::BC1_RGB_SRGB:
  case ResourceFormat::BC1_RGBA_UNORM:
  case ResourceFormat::BC1_RGBA_SRGB:
  case ResourceFormat::BC2_UNORM:
  case ResourceFormat::BC2_SRGB:
  case ResourceFormat::BC3_UNORM:
  case ResourceFormat::BC3_SRGB:
  case ResourceFormat::BC4_UNORM:
  case ResourceFormat::BC4_SNORM:
  case ResourceFormat::BC5_UNORM:
  case ResourceFormat::BC5_SNORM:
  case ResourceFormat::BC6H_UFLOAT:
  case ResourceFormat::BC6H_SFLOAT:
  case ResourceFormat::BC7_UNORM:
  case ResourceFormat::BC7_SRGB:
    return {4, 4};
  case ResourceFormat::ETC2_RGB8_UNORM:
  case ResourceFormat::ETC2_RGB8_SRGB:
  case ResourceFormat::ETC2_RGB8A1_UNORM:
  case ResourceFormat::ETC2_RGB8A1_SRGB:
  case ResourceFormat::ETC2_RGBA8_UNORM:
  case ResourceFormat::ETC2_RGBA8_SRGB:
  case ResourceFormat::EAC_R11_UNORM:
  case ResourceFormat::EAC_R11_SNORM:
  case ResourceFormat::EAC_R11G11_UNORM:
  case ResourceFormat::EAC_R11G11_SNORM:
    return {4, 4};
  default:
    return {1, 1};
  }
}

} // namespace

uint32_t feme::image::blockWidth(ResourceFormat Format) {
  return blockShape(Format).Width;
}

uint32_t feme::image::blockHeight(ResourceFormat Format) {
  return blockShape(Format).Height;
}

uint32_t feme::image::bytesPerBlock(ResourceFormat Format) {
  switch (Format) {
  case ResourceFormat::BC1_RGB_UNORM:
  case ResourceFormat::BC1_RGB_SRGB:
  case ResourceFormat::BC1_RGBA_UNORM:
  case ResourceFormat::BC1_RGBA_SRGB:
  case ResourceFormat::BC4_UNORM:
  case ResourceFormat::BC4_SNORM:
  case ResourceFormat::ETC2_RGB8_UNORM:
  case ResourceFormat::ETC2_RGB8_SRGB:
  case ResourceFormat::ETC2_RGB8A1_UNORM:
  case ResourceFormat::ETC2_RGB8A1_SRGB:
  case ResourceFormat::EAC_R11_UNORM:
  case ResourceFormat::EAC_R11_SNORM:
    return 8;
  case ResourceFormat::BC2_UNORM:
  case ResourceFormat::BC2_SRGB:
  case ResourceFormat::BC3_UNORM:
  case ResourceFormat::BC3_SRGB:
  case ResourceFormat::BC5_UNORM:
  case ResourceFormat::BC5_SNORM:
  case ResourceFormat::BC6H_UFLOAT:
  case ResourceFormat::BC6H_SFLOAT:
  case ResourceFormat::BC7_UNORM:
  case ResourceFormat::BC7_SRGB:
  case ResourceFormat::ETC2_RGBA8_UNORM:
  case ResourceFormat::ETC2_RGBA8_SRGB:
  case ResourceFormat::EAC_R11G11_UNORM:
  case ResourceFormat::EAC_R11G11_SNORM:
    return 16;
  default:
    break;
  }
  if (isBlockCompressedFormat(Format))
    // Every ASTC footprint packs into the same 128-bit (16-byte) block
    // regardless of its width/height -- a wider/taller block just means
    // more texels share those same 16 bytes, i.e. a better compression
    // ratio, not a bigger block.
    return 16;
  return formatElementSize(Format);
}

bool feme::image::isASTCSRGBFormat(ResourceFormat Format) {
  switch (Format) {
  case ResourceFormat::ASTC_4x4_SRGB:
  case ResourceFormat::ASTC_5x4_SRGB:
  case ResourceFormat::ASTC_5x5_SRGB:
  case ResourceFormat::ASTC_6x5_SRGB:
  case ResourceFormat::ASTC_6x6_SRGB:
  case ResourceFormat::ASTC_8x5_SRGB:
  case ResourceFormat::ASTC_8x6_SRGB:
  case ResourceFormat::ASTC_8x8_SRGB:
  case ResourceFormat::ASTC_10x5_SRGB:
  case ResourceFormat::ASTC_10x6_SRGB:
  case ResourceFormat::ASTC_10x8_SRGB:
  case ResourceFormat::ASTC_10x10_SRGB:
  case ResourceFormat::ASTC_12x10_SRGB:
  case ResourceFormat::ASTC_12x12_SRGB:
    return true;
  default:
    return false;
  }
}
