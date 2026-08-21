//===- Format.cpp - VkFormat -> feme::cpu::ResourceFormat mapping --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Format.h"

#include "llvm/Support/ErrorHandling.h"

using namespace feme::cpu;

std::optional<ResourceFormat> feme::vulkan::mapVkFormat(VkFormat Format) {
  switch (Format) {
  case VK_FORMAT_R32_SFLOAT:
    return ResourceFormat::R32_FLOAT;
  case VK_FORMAT_R32G32_SFLOAT:
    return ResourceFormat::R32G32_FLOAT;
  case VK_FORMAT_R32G32B32_SFLOAT:
    return ResourceFormat::R32G32B32_FLOAT;
  case VK_FORMAT_R32G32B32A32_SFLOAT:
    return ResourceFormat::R32G32B32A32_FLOAT;
  case VK_FORMAT_R32_UINT:
    return ResourceFormat::R32_UINT;
  case VK_FORMAT_R32G32_UINT:
    return ResourceFormat::R32G32_UINT;
  case VK_FORMAT_R32G32B32_UINT:
    return ResourceFormat::R32G32B32_UINT;
  case VK_FORMAT_R32G32B32A32_UINT:
    return ResourceFormat::R32G32B32A32_UINT;
  case VK_FORMAT_R32_SINT:
    return ResourceFormat::R32_SINT;
  case VK_FORMAT_R32G32_SINT:
    return ResourceFormat::R32G32_SINT;
  case VK_FORMAT_R32G32B32_SINT:
    return ResourceFormat::R32G32B32_SINT;
  case VK_FORMAT_R32G32B32A32_SINT:
    return ResourceFormat::R32G32B32A32_SINT;
  case VK_FORMAT_R8G8B8A8_UNORM:
    return ResourceFormat::R8G8B8A8_UNORM;
  case VK_FORMAT_R8G8B8A8_SNORM:
    return ResourceFormat::R8G8B8A8_SNORM;
  case VK_FORMAT_R8G8B8A8_UINT:
    return ResourceFormat::R8G8B8A8_UINT;
  case VK_FORMAT_R8G8B8A8_SINT:
    return ResourceFormat::R8G8B8A8_SINT;
  case VK_FORMAT_R8G8B8A8_SRGB:
    return ResourceFormat::R8G8B8A8_UNORM_SRGB;
  case VK_FORMAT_B8G8R8A8_UNORM:
    return ResourceFormat::B8G8R8A8_UNORM;
  case VK_FORMAT_R16G16B16A16_SFLOAT:
    return ResourceFormat::R16G16B16A16_FLOAT;
  case VK_FORMAT_R16G16B16A16_UNORM:
    return ResourceFormat::R16G16B16A16_UNORM;
  case VK_FORMAT_R16G16B16A16_SNORM:
    return ResourceFormat::R16G16B16A16_SNORM;
  case VK_FORMAT_R16G16B16A16_UINT:
    return ResourceFormat::R16G16B16A16_UINT;
  case VK_FORMAT_R16G16B16A16_SINT:
    return ResourceFormat::R16G16B16A16_SINT;
  case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    return ResourceFormat::R11G11B10_FLOAT;
  case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    return ResourceFormat::R10G10B10A2_UNORM;
  case VK_FORMAT_A2B10G10R10_UINT_PACK32:
    return ResourceFormat::R10G10B10A2_UINT;
  case VK_FORMAT_D16_UNORM:
    return ResourceFormat::D16_UNORM;
  case VK_FORMAT_D32_SFLOAT:
    return ResourceFormat::D32_FLOAT;
  case VK_FORMAT_D24_UNORM_S8_UINT:
    return ResourceFormat::D24_UNORM_S8_UINT;
  case VK_FORMAT_D32_SFLOAT_S8_UINT:
    return ResourceFormat::D32_FLOAT_S8X24_UINT;
  case VK_FORMAT_S8_UINT:
    return ResourceFormat::S8_UINT;
  // (Roadmap E5) `VK_KHR_maintenance5`'s two new formats.
  case VK_FORMAT_A8_UNORM_KHR:
    return ResourceFormat::A8_UNORM;
  case VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR:
    return ResourceFormat::A1B5G5R5_UNORM;
  // (Roadmap E20) The 14 LDR-only ASTC block footprints.
  case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
    return ResourceFormat::ASTC_4x4_UNORM;
  case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
    return ResourceFormat::ASTC_4x4_SRGB;
  case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
    return ResourceFormat::ASTC_5x4_UNORM;
  case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
    return ResourceFormat::ASTC_5x4_SRGB;
  case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
    return ResourceFormat::ASTC_5x5_UNORM;
  case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
    return ResourceFormat::ASTC_5x5_SRGB;
  case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
    return ResourceFormat::ASTC_6x5_UNORM;
  case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
    return ResourceFormat::ASTC_6x5_SRGB;
  case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
    return ResourceFormat::ASTC_6x6_UNORM;
  case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
    return ResourceFormat::ASTC_6x6_SRGB;
  case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
    return ResourceFormat::ASTC_8x5_UNORM;
  case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
    return ResourceFormat::ASTC_8x5_SRGB;
  case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
    return ResourceFormat::ASTC_8x6_UNORM;
  case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
    return ResourceFormat::ASTC_8x6_SRGB;
  case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
    return ResourceFormat::ASTC_8x8_UNORM;
  case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
    return ResourceFormat::ASTC_8x8_SRGB;
  case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
    return ResourceFormat::ASTC_10x5_UNORM;
  case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
    return ResourceFormat::ASTC_10x5_SRGB;
  case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
    return ResourceFormat::ASTC_10x6_UNORM;
  case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
    return ResourceFormat::ASTC_10x6_SRGB;
  case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
    return ResourceFormat::ASTC_10x8_UNORM;
  case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
    return ResourceFormat::ASTC_10x8_SRGB;
  case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
    return ResourceFormat::ASTC_10x10_UNORM;
  case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
    return ResourceFormat::ASTC_10x10_SRGB;
  case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
    return ResourceFormat::ASTC_12x10_UNORM;
  case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
    return ResourceFormat::ASTC_12x10_SRGB;
  case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
    return ResourceFormat::ASTC_12x12_UNORM;
  case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
    return ResourceFormat::ASTC_12x12_SRGB;
  default:
    return std::nullopt;
  }
}

uint32_t feme::vulkan::formatElementSize(ResourceFormat Format) {
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
  case ResourceFormat::R11G11B10_FLOAT:
  case ResourceFormat::R10G10B10A2_UNORM:
  case ResourceFormat::R10G10B10A2_UINT:
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
  // (Roadmap E5) `VK_FORMAT_A8_UNORM`: one byte, one component.
  case ResourceFormat::A8_UNORM:
    return 1;
  // (Roadmap E5) `VK_FORMAT_A1B5G5R5_UNORM_PACK16`: packed into 2 bytes.
  case ResourceFormat::A1B5G5R5_UNORM:
    return 2;
  // (Roadmap E20) Block-compressed formats have no single-texel size --
  // see `bytesPerBlock` for their whole-block size instead.
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
    return 0;
  }
  llvm_unreachable("unhandled ResourceFormat");
}

namespace {

/// One block-compressed format's block footprint, in texels -- see
/// Format.h's file comment. Every ASTC block is 128 bits regardless of its
/// footprint, so only width/height vary here.
struct BlockShape {
  uint32_t Width;
  uint32_t Height;
};

/// Returns \p Format's `BlockShape`, or `{1, 1}` for a non-block-compressed
/// format (including one `mapVkFormat` does not recognize) -- see
/// `blockWidth`/`blockHeight`'s "always call this" comment in Format.h.
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
  default:
    return {1, 1};
  }
}

} // namespace

uint32_t feme::vulkan::blockWidth(ResourceFormat Format) {
  return blockShape(Format).Width;
}

uint32_t feme::vulkan::blockHeight(ResourceFormat Format) {
  return blockShape(Format).Height;
}

uint32_t feme::vulkan::bytesPerBlock(ResourceFormat Format) {
  if (isBlockCompressedFormat(Format))
    // Every ASTC footprint packs into the same 128-bit (16-byte) block
    // regardless of its width/height -- a wider/taller block just means
    // more texels share those same 16 bytes, i.e. a better compression
    // ratio, not a bigger block.
    return 16;
  return formatElementSize(Format);
}

bool feme::vulkan::isTexelBufferFormatSupported(ResourceFormat Format) {
  switch (Format) {
  // The identity 32-bit-per-component formats: the CPU runtime's
  // `femeCpuResourceLoadTypedV4I32`/`StoreTypedV4I32` (`R32G32B32A32_UINT`/
  // `_SINT`) and `femeCpuResourceLoadTypedV4F32`/`StoreTypedV4F32`
  // (`R32G32B32A32_FLOAT`) reinterpret the full 16-byte element directly,
  // with no scalar conversion needed.
  case ResourceFormat::R32G32B32A32_FLOAT:
  case ResourceFormat::R32G32B32A32_UINT:
  case ResourceFormat::R32G32B32A32_SINT:
  // The packed 8-bit-per-component formats `femeCpuResourceLoadTypedV4F32`/
  // `StoreTypedV4F32` (`_UNORM`/`_SNORM`) and
  // `femeCpuResourceLoadTypedV4I32`/`StoreTypedV4I32` (`_UINT`/`_SINT`)
  // implement a scalar conversion for.
  case ResourceFormat::R8G8B8A8_UNORM:
  case ResourceFormat::R8G8B8A8_SNORM:
  case ResourceFormat::R8G8B8A8_UINT:
  case ResourceFormat::R8G8B8A8_SINT:
    return true;
  default:
    return false;
  }
}
