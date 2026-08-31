//===- Format.cpp - VkFormat -> feme::cpu::ResourceFormat mapping --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Format.h"
#include "RenderPass.h"

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
  // (Roadmap H19j) The single-channel `R8` mandatory
  // `shaderStorageImageExtendedFormats` formats.
  case VK_FORMAT_R8_UNORM:
    return ResourceFormat::R8_UNORM;
  case VK_FORMAT_R8_SNORM:
    return ResourceFormat::R8_SNORM;
  case VK_FORMAT_R8_UINT:
    return ResourceFormat::R8_UINT;
  case VK_FORMAT_R8_SINT:
    return ResourceFormat::R8_SINT;
  // (Roadmap H19n) The two-channel `R8G8` mandatory
  // `shaderStorageImageExtendedFormats` formats.
  case VK_FORMAT_R8G8_UNORM:
    return ResourceFormat::R8G8_UNORM;
  case VK_FORMAT_R8G8_SNORM:
    return ResourceFormat::R8G8_SNORM;
  case VK_FORMAT_R8G8_UINT:
    return ResourceFormat::R8G8_UINT;
  case VK_FORMAT_R8G8_SINT:
    return ResourceFormat::R8G8_SINT;
  // (Roadmap H19n) The single-channel `R16` mandatory
  // `shaderStorageImageExtendedFormats` formats.
  case VK_FORMAT_R16_SFLOAT:
    return ResourceFormat::R16_FLOAT;
  case VK_FORMAT_R16_UNORM:
    return ResourceFormat::R16_UNORM;
  case VK_FORMAT_R16_SNORM:
    return ResourceFormat::R16_SNORM;
  case VK_FORMAT_R16_UINT:
    return ResourceFormat::R16_UINT;
  case VK_FORMAT_R16_SINT:
    return ResourceFormat::R16_SINT;
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
  // (Roadmap E19) `VK_EXT_4444_formats`'s two new packed 4-bit-per-
  // component formats.
  case VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT:
    return ResourceFormat::A4R4G4B4_UNORM;
  case VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT:
    return ResourceFormat::A4B4G4R4_UNORM;
  // (Roadmap H7r) The remaining core-1.0 packed 16-bit formats.
  case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
    return ResourceFormat::R4G4B4A4_UNORM;
  case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
    return ResourceFormat::B4G4R4A4_UNORM;
  case VK_FORMAT_R5G6B5_UNORM_PACK16:
    return ResourceFormat::R5G6B5_UNORM;
  case VK_FORMAT_B5G6R5_UNORM_PACK16:
    return ResourceFormat::B5G6R5_UNORM;
  case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
    return ResourceFormat::R5G5B5A1_UNORM;
  case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
    return ResourceFormat::B5G5R5A1_UNORM;
  case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
    return ResourceFormat::A1R5G5B5_UNORM;
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
  // (Roadmap E21) The 14 HDR-only ASTC block footprints.
  case VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_4x4_SFLOAT;
  case VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_5x4_SFLOAT;
  case VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_5x5_SFLOAT;
  case VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_6x5_SFLOAT;
  case VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_6x6_SFLOAT;
  case VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_8x5_SFLOAT;
  case VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_8x6_SFLOAT;
  case VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_8x8_SFLOAT;
  case VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_10x5_SFLOAT;
  case VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_10x6_SFLOAT;
  case VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_10x8_SFLOAT;
  case VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_10x10_SFLOAT;
  case VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_12x10_SFLOAT;
  case VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT:
    return ResourceFormat::ASTC_12x12_SFLOAT;
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
  // (Roadmap H19j) `R8_{UNORM,SNORM,UINT,SINT}`: a single byte, one
  // component.
  case ResourceFormat::R8_UNORM:
  case ResourceFormat::R8_SNORM:
  case ResourceFormat::R8_UINT:
  case ResourceFormat::R8_SINT:
    return 1;
  // (Roadmap H19n) `R8G8_{UNORM,SNORM,UINT,SINT}`: two bytes, two
  // components.
  case ResourceFormat::R8G8_UNORM:
  case ResourceFormat::R8G8_SNORM:
  case ResourceFormat::R8G8_UINT:
  case ResourceFormat::R8G8_SINT:
    return 2;
  // (Roadmap H19n) `R16_{FLOAT,UNORM,SNORM,UINT,SINT}`: two bytes, one
  // component.
  case ResourceFormat::R16_FLOAT:
  case ResourceFormat::R16_UNORM:
  case ResourceFormat::R16_SNORM:
  case ResourceFormat::R16_UINT:
  case ResourceFormat::R16_SINT:
    return 2;
  // (Roadmap E5) `VK_FORMAT_A8_UNORM`: one byte, one component.
  case ResourceFormat::A8_UNORM:
    return 1;
  // (Roadmap E5) `VK_FORMAT_A1B5G5R5_UNORM_PACK16`: packed into 2 bytes.
  case ResourceFormat::A1B5G5R5_UNORM:
    return 2;
  // (Roadmap E19) `VK_EXT_4444_formats`'s two formats: also packed into
  // 2 bytes (4 components x 4 bits each).
  case ResourceFormat::A4R4G4B4_UNORM:
  case ResourceFormat::A4B4G4R4_UNORM:
    return 2;
  // (Roadmap H7r) The remaining core-1.0 packed 16-bit formats: every one
  // of them, alpha component or not, packs into the same 2 bytes.
  case ResourceFormat::R4G4B4A4_UNORM:
  case ResourceFormat::B4G4R4A4_UNORM:
  case ResourceFormat::R5G6B5_UNORM:
  case ResourceFormat::B5G6R5_UNORM:
  case ResourceFormat::R5G5B5A1_UNORM:
  case ResourceFormat::B5G5R5A1_UNORM:
  case ResourceFormat::A1R5G5B5_UNORM:
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
  // (Roadmap E21) The 14 HDR-only ASTC block footprints: no single-texel
  // size either, for the same reason as their LDR counterparts above.
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
  // (Roadmap E21) The 14 HDR-only ASTC block footprints -- same
  // width/height pairing as their LDR counterparts above.
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

VkFormatFeatureFlags feme::vulkan::formatFeatureFlags(ResourceFormat Format) {
  if (Format == ResourceFormat::Unknown)
    return 0;

  // A copy (`vkCmdCopyImage`/`vkCmdCopyBufferToImage`/
  // `vkCmdCopyImageToBuffer`) never converts values, and roadmap E22 made
  // every recognized format -- block-compressed included -- addressable a
  // whole block/texel at a time by those paths, so every recognized format
  // is a legal transfer source and destination.
  VkFormatFeatureFlags Flags =
      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

  bool BlockCompressed = isBlockCompressedFormat(Format);
  bool ASTCLdr = isASTCLdrFormat(Format);
  // `ImageOps.cpp`'s `runBlitImage` rejects a block-compressed
  // *destination* outright (no ASTC encoder exists to repack into one) and
  // an HDR ASTC *source* (`decodeASTCBlock` is LDR-only), but accepts every
  // other combination either way.
  if (!BlockCompressed || ASTCLdr)
    Flags |= VK_FORMAT_FEATURE_BLIT_SRC_BIT;
  if (!BlockCompressed)
    Flags |= VK_FORMAT_FEATURE_BLIT_DST_BIT;

  // The formats the CPU runtime's texel-unpack table
  // (`femeRTImageFormatElementSize`/`femeRTUnpackImageTexel`,
  // feme/runtime/CPU/FeMeRuntimeCPU.c) implements can actually be sampled
  // (with filtering) by a shader; every ASTC LDR format is sampled too,
  // since `materializeImageDescriptor` (CommandBuffer.cpp) decodes one
  // into `R8G8B8A8_UNORM`/`_UNORM_SRGB` before the runtime ever sees it
  // (roadmap E23). An HDR ASTC format samples as all-zero (that bridge is
  // LDR-only), so is honestly left unset like every other unimplemented
  // format.
  switch (Format) {
  case ResourceFormat::R32_FLOAT:
  case ResourceFormat::R32G32_FLOAT:
  case ResourceFormat::R32G32B32_FLOAT:
  case ResourceFormat::R32G32B32A32_FLOAT:
  case ResourceFormat::R8G8B8A8_UNORM:
  case ResourceFormat::R8G8B8A8_SNORM:
  case ResourceFormat::R8G8B8A8_UNORM_SRGB:
  case ResourceFormat::R16G16B16A16_FLOAT:
  case ResourceFormat::R16G16B16A16_UNORM:
  case ResourceFormat::R16G16B16A16_SNORM:
  case ResourceFormat::R11G11B10_FLOAT:
  case ResourceFormat::R10G10B10A2_UNORM:
  case ResourceFormat::B8G8R8A8_UNORM:
  case ResourceFormat::A8_UNORM:
  case ResourceFormat::A1B5G5R5_UNORM:
  // (Roadmap H19j) `R8_UNORM`/`_SNORM`: the single-channel analogues of
  // `R8G8B8A8_UNORM`/`_SNORM` above, both now decoded by
  // `femeRTUnpackImageTexel`'s own `R8_UNORM`/`_SNORM` cases.
  case ResourceFormat::R8_UNORM:
  case ResourceFormat::R8_SNORM:
  // (Roadmap H19n) `R8G8_UNORM`/`_SNORM`: the two-channel analogues of
  // `R8_UNORM`/`_SNORM` above.
  case ResourceFormat::R8G8_UNORM:
  case ResourceFormat::R8G8_SNORM:
  // (Roadmap H19n) `R16_FLOAT`/`_UNORM`/`_SNORM`: the single-channel
  // analogues of `R16G16B16A16_FLOAT`/`_UNORM`/`_SNORM` above.
  case ResourceFormat::R16_FLOAT:
  case ResourceFormat::R16_UNORM:
  case ResourceFormat::R16_SNORM:
    Flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    break;
  // (Roadmap E26) The mandatory-sampled `_UINT`/`_SINT` formats
  // `femeRTUnpackImageTexelI32` (FeMeRuntimeCPU.c) decodes for
  // `feme.cpu.image.load.2d.v4i32`. No `_FILTER_LINEAR_BIT`: SPIR-V never
  // legalizes a filtered `OpImageSample*` against an integer-sampled
  // image, only an unfiltered `OpImageFetch`, and Vulkan's own
  // `VkFormatFeatureFlagBits` follows suit -- `SAMPLED_IMAGE_FILTER_LINEAR`
  // is documented as requiring a format whose numeric type supports
  // filtering, which no integer format's `VkComponentNumericFormat` does.
  case ResourceFormat::R8G8B8A8_UINT:
  case ResourceFormat::R8G8B8A8_SINT:
  case ResourceFormat::R16G16B16A16_UINT:
  case ResourceFormat::R16G16B16A16_SINT:
  case ResourceFormat::R10G10B10A2_UINT:
  case ResourceFormat::R32G32B32A32_UINT:
  case ResourceFormat::R32G32B32A32_SINT:
  // (Roadmap H19j) `R8_UINT`/`_SINT`: the single-channel analogues of
  // `R8G8B8A8_UINT`/`_SINT` above, decoded by
  // `femeRTUnpackImageTexelI32`'s own `R8_UINT`/`_SINT` cases.
  case ResourceFormat::R8_UINT:
  case ResourceFormat::R8_SINT:
  // (Roadmap H19n) `R8G8_UINT`/`_SINT`: the two-channel analogues of
  // `R8_UINT`/`_SINT` above.
  case ResourceFormat::R8G8_UINT:
  case ResourceFormat::R8G8_SINT:
  // (Roadmap H19n) `R16_UINT`/`_SINT`: the single-channel analogues of
  // `R16G16B16A16_UINT`/`_SINT` above.
  case ResourceFormat::R16_UINT:
  case ResourceFormat::R16_SINT:
    Flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    break;
  default:
    if (ASTCLdr)
      Flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
               VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    break;
  }

  // (Roadmap H19a) The mandatory storage-image format floor Vulkan's own
  // spec requires: exactly `R32_{SFLOAT,UINT,SINT}` and
  // `R32G32B32A32_{SFLOAT,UINT,SINT}`, the only formats
  // `femeRTStoreTexel2D`/`femeRTStoreTexel2DI32`'s own
  // `femeRTPackImageTexel`/`femeRTPackImageTexelI32` tables (FeMeRuntimeCPU.c)
  // used to encode. Roadmap H19f added `R16G16B16A16_{SFLOAT,UINT,SINT}`;
  // roadmap H19h adds `R16G16B16A16_{UNORM,SNORM}` -- both a further slice
  // of the full `shaderStorageImageExtendedFormats` list
  // (`VkPhysicalDeviceFeatures::shaderStorageImageExtendedFormats` itself
  // stays `VK_FALSE` until the rest of that list's own pack support
  // lands, see Roadmap.md's H19h); every other format is still honestly
  // left unset until a matching pack case exists. Plain2D-only for now
  // (no arrayed/cube/multisampled storage image lowering exists yet, see
  // `SPIRVResourceLowering.cpp`'s `classifyStorageImage2DHandle`), but
  // this format-feature bit is per-format, not per-view-shape, so it is
  // still honest to set here.
  switch (Format) {
  case ResourceFormat::R32_FLOAT:
  case ResourceFormat::R32G32B32A32_FLOAT:
  case ResourceFormat::R32_UINT:
  case ResourceFormat::R32G32B32A32_UINT:
  case ResourceFormat::R32_SINT:
  case ResourceFormat::R32G32B32A32_SINT:
  case ResourceFormat::R16G16B16A16_FLOAT:
  case ResourceFormat::R16G16B16A16_UNORM:
  case ResourceFormat::R16G16B16A16_SNORM:
  case ResourceFormat::R16G16B16A16_UINT:
  case ResourceFormat::R16G16B16A16_SINT:
  // (Roadmap H19j) `R8_{UNORM,SNORM,UINT,SINT}`: the single-channel
  // mandatory-extended-format formats, backed by new
  // `femeRTPackImageTexel`/`femeRTPackImageTexelI32` cases
  // (FeMeRuntimeCPU.c).
  case ResourceFormat::R8_UNORM:
  case ResourceFormat::R8_SNORM:
  case ResourceFormat::R8_UINT:
  case ResourceFormat::R8_SINT:
  // (Roadmap H19n) `R8G8_{UNORM,SNORM,UINT,SINT}`: the two-channel
  // mandatory-extended-format formats, backed by new
  // `femeRTPackImageTexel`/`femeRTPackImageTexelI32` cases
  // (FeMeRuntimeCPU.c).
  case ResourceFormat::R8G8_UNORM:
  case ResourceFormat::R8G8_SNORM:
  case ResourceFormat::R8G8_UINT:
  case ResourceFormat::R8G8_SINT:
  // (Roadmap H19n) `R16_{FLOAT,UNORM,SNORM,UINT,SINT}`: the
  // single-channel mandatory-extended-format formats, backed by new
  // `femeRTPackImageTexel`/`femeRTPackImageTexelI32` cases
  // (FeMeRuntimeCPU.c).
  case ResourceFormat::R16_FLOAT:
  case ResourceFormat::R16_UNORM:
  case ResourceFormat::R16_SNORM:
  case ResourceFormat::R16_UINT:
  case ResourceFormat::R16_SINT:
    Flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    break;
  default:
    break;
  }

  if (isSupportedColorAttachmentFormat(Format))
    Flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
             VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
  if (isSupportedDepthAttachmentFormat(Format) ||
      isSupportedStencilAttachmentFormat(Format))
    Flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

  return Flags;
}
