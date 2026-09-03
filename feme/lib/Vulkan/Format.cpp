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
  // (Roadmap H8) `VK_FORMAT_A8B8G8R8_*_PACK32`'s own spec definition lays
  // R in bits 0-7, G in 8-15, B in 16-23, A in 24-31 -- byte-for-byte
  // identical in linear memory to `R8G8B8A8_*`'s own R/G/B/A byte order
  // above, so these four packed `VkFormat` enum values map onto the exact
  // same `ResourceFormat` values with no new decode/pack logic needed.
  case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    return ResourceFormat::R8G8B8A8_UNORM;
  case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
    return ResourceFormat::R8G8B8A8_SNORM;
  case VK_FORMAT_A8B8G8R8_UINT_PACK32:
    return ResourceFormat::R8G8B8A8_UINT;
  case VK_FORMAT_A8B8G8R8_SINT_PACK32:
    return ResourceFormat::R8G8B8A8_SINT;
  case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
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
  // (Roadmap H19n) The two-channel `R16G16` mandatory
  // `shaderStorageImageExtendedFormats` formats.
  case VK_FORMAT_R16G16_SFLOAT:
    return ResourceFormat::R16G16_FLOAT;
  case VK_FORMAT_R16G16_UNORM:
    return ResourceFormat::R16G16_UNORM;
  case VK_FORMAT_R16G16_SNORM:
    return ResourceFormat::R16G16_SNORM;
  case VK_FORMAT_R16G16_UINT:
    return ResourceFormat::R16G16_UINT;
  case VK_FORMAT_R16G16_SINT:
    return ResourceFormat::R16G16_SINT;
  case VK_FORMAT_B8G8R8A8_UNORM:
    return ResourceFormat::B8G8R8A8_UNORM;
  // (Roadmap H8r) `VK_FORMAT_B8G8R8A8_SRGB`: the sRGB-encoded sibling of
  // `B8G8R8A8_UNORM` above, byte-for-byte the same B/G/R/A memory order
  // as `R8G8B8A8_UNORM_SRGB` with R and B swapped -- a CTS-confirmed
  // genuine gap (the entirely unmapped format H8g's own audit split
  // off), not merely a reporting one: unlike H8's other packed-format
  // aliases, this needs its own `ResourceFormat` (not a byte-identical
  // reuse of `R8G8B8A8_UNORM_SRGB`) since the runtime's sRGB decode/encode
  // helpers also swap the R/B channel order, mirroring how
  // `B8G8R8A8_UNORM` already gets its own `ResourceFormat` distinct from
  // `R8G8B8A8_UNORM` for the same reason.
  case VK_FORMAT_B8G8R8A8_SRGB:
    return ResourceFormat::B8G8R8A8_UNORM_SRGB;
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
  // (Roadmap H19o) The signed siblings of the two `A2B10G10R10` formats
  // above -- the final two formats in the real Vulkan spec's own
  // mandatory `shaderStorageImageExtendedFormats` list.
  case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
    return ResourceFormat::R10G10B10A2_SNORM;
  case VK_FORMAT_A2B10G10R10_SINT_PACK32:
    return ResourceFormat::R10G10B10A2_SINT;
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
  // (Roadmap H8n) The 16 `VK_FORMAT_BC*` block footprints.
  case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    return ResourceFormat::BC1_RGB_UNORM;
  case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    return ResourceFormat::BC1_RGB_SRGB;
  case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    return ResourceFormat::BC1_RGBA_UNORM;
  case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    return ResourceFormat::BC1_RGBA_SRGB;
  case VK_FORMAT_BC2_UNORM_BLOCK:
    return ResourceFormat::BC2_UNORM;
  case VK_FORMAT_BC2_SRGB_BLOCK:
    return ResourceFormat::BC2_SRGB;
  case VK_FORMAT_BC3_UNORM_BLOCK:
    return ResourceFormat::BC3_UNORM;
  case VK_FORMAT_BC3_SRGB_BLOCK:
    return ResourceFormat::BC3_SRGB;
  case VK_FORMAT_BC4_UNORM_BLOCK:
    return ResourceFormat::BC4_UNORM;
  case VK_FORMAT_BC4_SNORM_BLOCK:
    return ResourceFormat::BC4_SNORM;
  case VK_FORMAT_BC5_UNORM_BLOCK:
    return ResourceFormat::BC5_UNORM;
  case VK_FORMAT_BC5_SNORM_BLOCK:
    return ResourceFormat::BC5_SNORM;
  case VK_FORMAT_BC6H_UFLOAT_BLOCK:
    return ResourceFormat::BC6H_UFLOAT;
  case VK_FORMAT_BC6H_SFLOAT_BLOCK:
    return ResourceFormat::BC6H_SFLOAT;
  case VK_FORMAT_BC7_UNORM_BLOCK:
    return ResourceFormat::BC7_UNORM;
  case VK_FORMAT_BC7_SRGB_BLOCK:
    return ResourceFormat::BC7_SRGB;
  // (Roadmap H8j) The 10 `VK_FORMAT_ETC2_*`/`VK_FORMAT_EAC_*` block
  // footprints.
  case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
    return ResourceFormat::ETC2_RGB8_UNORM;
  case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
    return ResourceFormat::ETC2_RGB8_SRGB;
  case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
    return ResourceFormat::ETC2_RGB8A1_UNORM;
  case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
    return ResourceFormat::ETC2_RGB8A1_SRGB;
  case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
    return ResourceFormat::ETC2_RGBA8_UNORM;
  case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
    return ResourceFormat::ETC2_RGBA8_SRGB;
  case VK_FORMAT_EAC_R11_UNORM_BLOCK:
    return ResourceFormat::EAC_R11_UNORM;
  case VK_FORMAT_EAC_R11_SNORM_BLOCK:
    return ResourceFormat::EAC_R11_SNORM;
  case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
    return ResourceFormat::EAC_R11G11_UNORM;
  case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
    return ResourceFormat::EAC_R11G11_SNORM;
  case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
    return ResourceFormat::E5B9G9R9_UFLOAT;
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
  // (Roadmap H8r) `B8G8R8A8_UNORM_SRGB`: the sRGB sibling of
  // `B8G8R8A8_UNORM` above, packed the same 4-byte way.
  case ResourceFormat::B8G8R8A8_UNORM_SRGB:
  case ResourceFormat::R11G11B10_FLOAT:
  case ResourceFormat::R10G10B10A2_UNORM:
  case ResourceFormat::R10G10B10A2_UINT:
  // (Roadmap H19o) `R10G10B10A2_{SNORM,SINT}`: packed into the same
  // single 4-byte word as their unsigned siblings above.
  case ResourceFormat::R10G10B10A2_SNORM:
  case ResourceFormat::R10G10B10A2_SINT:
  // (Roadmap H8q) `E5B9G9R9_UFLOAT`: also packed into a single 4-byte
  // word, like `R11G11B10_FLOAT` above.
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
  // (Roadmap H19n) `R16G16_{FLOAT,UNORM,SNORM,UINT,SINT}`: four bytes,
  // two components.
  case ResourceFormat::R16G16_FLOAT:
  case ResourceFormat::R16G16_UNORM:
  case ResourceFormat::R16G16_SNORM:
  case ResourceFormat::R16G16_UINT:
  case ResourceFormat::R16G16_SINT:
    return 4;
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
  // (Roadmap H8n) The 16 `VK_FORMAT_BC*` block footprints: no
  // single-texel size either, for the same reason as ASTC above.
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
  // (Roadmap H8j) The 10 `VK_FORMAT_ETC2_*`/`VK_FORMAT_EAC_*` block
  // footprints: no single-texel size either, for the same reason as
  // ASTC/BC above.
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
  // (Roadmap H8n) All 16 `VK_FORMAT_BC*` formats share the same 4x4
  // texel footprint (unlike ASTC's own per-format footprint) -- only
  // their per-block byte count differs (`bytesPerBlock` below).
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
  // (Roadmap H8j) All 10 `VK_FORMAT_ETC2_*`/`VK_FORMAT_EAC_*` formats
  // also share the same 4x4 texel footprint -- only their per-block byte
  // count differs (`bytesPerBlock` below).
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

uint32_t feme::vulkan::blockWidth(ResourceFormat Format) {
  return blockShape(Format).Width;
}

uint32_t feme::vulkan::blockHeight(ResourceFormat Format) {
  return blockShape(Format).Height;
}

uint32_t feme::vulkan::bytesPerBlock(ResourceFormat Format) {
  // (Roadmap H8n) Unlike every ASTC footprint (always a 128-bit block
  // regardless of width/height), BC1 and BC4 each pack into a 64-bit (8
  // byte) block -- half the size of every other BC format's 128-bit (16
  // byte) block -- per the real `S3TC`/`RGTC` bit layout `BCDecode.h`
  // decodes: BC1 stores two 16-bit endpoint colors plus a 2-bit-per-texel
  // index (64 bits total, no separate alpha plane), and BC4 stores two
  // 8-bit endpoints plus a 3-bit-per-texel index (also 64 bits, one
  // channel only) -- while BC2/BC3/BC5/BC6H/BC7 each add a second 64-bit
  // plane (explicit or interpolated alpha, a second interpolated
  // channel, or extra endpoint/partition bits), doubling their footprint
  // to 128 bits. (Roadmap H8j) Likewise, ETC2's opaque and
  // punchthrough-alpha RGB formats and EAC's single-channel format each
  // pack into a single 64-bit block (`ETC2Decode.h`'s own
  // `decodeETC2Block`/`decodeETC2PunchthroughAlphaBlock`/`decodeEACBlock`
  // each take exactly one 8-byte block), while ETC2's explicit-alpha
  // RGBA format and EAC's dual-channel format each compose two 64-bit
  // blocks (one `decodeEACBlock` call for alpha plus one
  // `decodeETC2Block` call for RGB, or two `decodeEACBlock` calls, one
  // per channel), doubling their footprint to 128 bits -- the same
  // "single-plane formats are half the size of dual-plane ones" pattern
  // BC1/BC4 vs. everything else already established.
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
  // (Roadmap L9) The single-channel 32-bit identity formats
  // (`RWBuffer<float>`/`RWBuffer<int>`/`RWBuffer<uint>`'s own shape):
  // `femeCpuResourceLoadTypedF32`/`StoreTypedF32` and `...I32` (added
  // alongside `SPIRVResourceLoweringPass::isSupportedTexelElementType`'s
  // scalar-element acceptance) reinterpret the scalar element directly
  // via the same `femeRTImageFormatElementSize`/`UnpackImageTexel`/
  // `PackImageTexel` tables the image-sampling path already uses.
  case ResourceFormat::R32_FLOAT:
  case ResourceFormat::R32_UINT:
  case ResourceFormat::R32_SINT:
  // (Roadmap H8d) A real `dEQP-VK.api.info.format_properties.*` re-run
  // found 21 further mandatory texel-buffer formats this project's own
  // `femeCpuResourceLoadTypedV4F32`/`V4I32` (roadmap H8d refactor: both
  // now dispatch through the same `femeRTImageFormatElementSize`/
  // `UnpackImageTexel(I32)`/`PackImageTexel(I32)` tables the storage/
  // sampled-image path already shares) can already read losslessly, but
  // this predicate had never been widened to cover. Two-component and
  // packed formats below only ever need read support here --
  // `isStorageTexelBufferFormatSupported` immediately below narrows to
  // the read+write-capable subset for `VK_FORMAT_FEATURE_STORAGE_
  // TEXEL_BUFFER_BIT` specifically.
  case ResourceFormat::R10G10B10A2_UNORM:
  case ResourceFormat::R10G10B10A2_UINT:
  case ResourceFormat::R11G11B10_FLOAT:
  case ResourceFormat::B8G8R8A8_UNORM:
  case ResourceFormat::R16_FLOAT:
  case ResourceFormat::R16_UINT:
  case ResourceFormat::R16_SINT:
  case ResourceFormat::R16G16B16A16_FLOAT:
  case ResourceFormat::R16G16B16A16_UINT:
  case ResourceFormat::R16G16B16A16_SINT:
  case ResourceFormat::R32G32_FLOAT:
  case ResourceFormat::R32G32_UINT:
  case ResourceFormat::R32G32_SINT:
  case ResourceFormat::R8_UNORM:
  case ResourceFormat::R8_SNORM:
  case ResourceFormat::R8_UINT:
  case ResourceFormat::R8_SINT:
  case ResourceFormat::R8G8_UNORM:
  case ResourceFormat::R8G8_SNORM:
  case ResourceFormat::R8G8_UINT:
  case ResourceFormat::R8G8_SINT:
  // (Roadmap H8s) `R16G16_UINT`/`_SINT`: a real
  // `dEQP-VK.api.info.format_properties.*` re-run against feme's own ICD
  // (not lavapipe -- see H8g) found this pair still missing
  // `VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT`, a plain omission from
  // this switch's own two-channel-16-bit-family coverage above --
  // `femeRTUnpackImageTexelI32`/`PackImageTexelI32` (FeMeRuntimeCPU.c,
  // roadmap H19n) already decode/encode both losslessly, reused already
  // by the storage-image and vertex-fetch paths.
  case ResourceFormat::R16G16_UINT:
  case ResourceFormat::R16G16_SINT:
  // (Roadmap H8s) `R16G16_FLOAT`: a follow-up CTS re-run (after the
  // `R16G16_UINT`/`_SINT` fix above) found this third two-channel 16-bit
  // sibling also still missing `UNIFORM_TEXEL_BUFFER_BIT` -- the same
  // plain omission, `femeRTUnpackImageTexel` (FeMeRuntimeCPU.c, roadmap
  // H19n) already decoding it losslessly.
  case ResourceFormat::R16G16_FLOAT:
    return true;
  default:
    return false;
  }
}

bool feme::vulkan::isStorageTexelBufferFormatSupported(ResourceFormat Format) {
  switch (Format) {
  // The original 10-format scope: already read+write-capable (see
  // `isTexelBufferFormatSupported` above for why).
  case ResourceFormat::R32G32B32A32_FLOAT:
  case ResourceFormat::R32G32B32A32_UINT:
  case ResourceFormat::R32G32B32A32_SINT:
  case ResourceFormat::R8G8B8A8_UNORM:
  case ResourceFormat::R8G8B8A8_SNORM:
  case ResourceFormat::R8G8B8A8_UINT:
  case ResourceFormat::R8G8B8A8_SINT:
  case ResourceFormat::R32_FLOAT:
  case ResourceFormat::R32_UINT:
  case ResourceFormat::R32_SINT:
  // (Roadmap H8d) Of the 21 new read-only formats
  // `isTexelBufferFormatSupported` above gained, only these 6 are also
  // real mandatory `STORAGE_TEXEL_BUFFER_BIT` entries per a real
  // `dEQP-VK.api.info.format_properties.*` re-run -- the rest (e.g.
  // `R8_UNORM`, `B8G8R8A8_UNORM`) only ever need the uniform (read-only)
  // bit. `femeRTPackImageTexel`/`PackImageTexelI32` (the write-side
  // tables) already implement all six losslessly.
  case ResourceFormat::R16G16B16A16_FLOAT:
  case ResourceFormat::R16G16B16A16_UINT:
  case ResourceFormat::R16G16B16A16_SINT:
  case ResourceFormat::R32G32_FLOAT:
  case ResourceFormat::R32G32_UINT:
  case ResourceFormat::R32G32_SINT:
    return true;
  default:
    return false;
  }
}

bool feme::vulkan::isStorageTexelBufferAtomicFormatSupported(
    ResourceFormat Format) {
  // (Roadmap H8w) `R32_{UINT,SINT}`: the only two formats Vulkan's own
  // mandatory-format table requires `STORAGE_TEXEL_BUFFER_ATOMIC_BIT` for
  // that this project can actually honor a texel-buffer atomic against
  // today -- `SPIRVResourceLowering.cpp`'s `hasOnlySupportedPointerUses`
  // only accepts an `AtomicRMWInst`/`AtomicCmpXchgInst` `getpointer` user
  // for a `HandleKind::TexelStorage` handle whose RMW/xchg value is a
  // scalar `i32` (SPIR-V itself disallows an atomic against a
  // float-channel texel buffer outright, so `R32_FLOAT` never qualifies
  // regardless of this project's own coverage), lowered to a real
  // hardware atomic (`feme.cpu.resource.atomic.*.typed.i32`,
  // FeMeRuntimeCPU.c) rather than a plain load-modify-store -- mirroring
  // `formatFeatureFlags`'s own identical `R32_{UINT,SINT}`-only
  // `STORAGE_IMAGE_ATOMIC_BIT` scope (roadmap H8v).
  switch (Format) {
  case ResourceFormat::R32_UINT:
  case ResourceFormat::R32_SINT:
    return true;
  default:
    return false;
  }
}


bool feme::vulkan::isVertexBufferFormatSupported(ResourceFormat Format) {
  switch (Format) {
  // `decodeAttribute` reinterprets each component's bytes directly, no
  // scalar conversion needed.
  case ResourceFormat::R32_FLOAT:
  case ResourceFormat::R32G32_FLOAT:
  case ResourceFormat::R32G32B32_FLOAT:
  case ResourceFormat::R32G32B32A32_FLOAT:
  case ResourceFormat::R32_UINT:
  case ResourceFormat::R32G32_UINT:
  case ResourceFormat::R32G32B32_UINT:
  case ResourceFormat::R32G32B32A32_UINT:
  case ResourceFormat::R32_SINT:
  case ResourceFormat::R32G32_SINT:
  case ResourceFormat::R32G32B32_SINT:
  case ResourceFormat::R32G32B32A32_SINT:
  // The packed 8-bit-per-component `R8G8B8A8_*` formats `decodeAttribute`
  // implements a scalar conversion (UNORM/SNORM) or direct widen (UINT/SINT)
  // for. `_UNORM_SRGB` decodes bit-for-bit like `_UNORM` (vertex fetch never
  // applies an sRGB->linear conversion, unlike texture sampling), so it is
  // just as legitimate a vertex attribute format.
  case ResourceFormat::R8G8B8A8_UNORM:
  case ResourceFormat::R8G8B8A8_UNORM_SRGB:
  case ResourceFormat::R8G8B8A8_SNORM:
  case ResourceFormat::R8G8B8A8_UINT:
  case ResourceFormat::R8G8B8A8_SINT:
  // (Roadmap H8t) `B8G8R8A8_UNORM`: same 1-byte-per-component UNORM
  // conversion as `R8G8B8A8_UNORM` above, `decodeAttribute` (Executor.cpp)
  // just reorders the B/G/R/A memory bytes back to logical R/G/B/A order.
  case ResourceFormat::B8G8R8A8_UNORM:
  // (Roadmap H8b) The single- and two-channel 8-bit-per-component
  // families -- same conversion rules as `R8G8B8A8_*` above, just fewer
  // channels.
  case ResourceFormat::R8_UNORM:
  case ResourceFormat::R8_SNORM:
  case ResourceFormat::R8_UINT:
  case ResourceFormat::R8_SINT:
  case ResourceFormat::R8G8_UNORM:
  case ResourceFormat::R8G8_SNORM:
  case ResourceFormat::R8G8_UINT:
  case ResourceFormat::R8G8_SINT:
  // (Roadmap H8b) The 16-bit-per-component families: `R16_*`, `R16G16_*`,
  // and `R16G16B16A16_*`, including the `_FLOAT` (binary16) variant --
  // `decodeAttribute` implements a scalar UNORM/SNORM conversion, a direct
  // widen for UINT/SINT, and a half->float32 conversion (via
  // `llvm::APFloat`) for `_FLOAT`.
  case ResourceFormat::R16_UNORM:
  case ResourceFormat::R16_SNORM:
  case ResourceFormat::R16_UINT:
  case ResourceFormat::R16_SINT:
  case ResourceFormat::R16_FLOAT:
  case ResourceFormat::R16G16_UNORM:
  case ResourceFormat::R16G16_SNORM:
  case ResourceFormat::R16G16_UINT:
  case ResourceFormat::R16G16_SINT:
  case ResourceFormat::R16G16_FLOAT:
  case ResourceFormat::R16G16B16A16_UNORM:
  case ResourceFormat::R16G16B16A16_SNORM:
  case ResourceFormat::R16G16B16A16_UINT:
  case ResourceFormat::R16G16B16A16_SINT:
  case ResourceFormat::R16G16B16A16_FLOAT:
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
  // (Roadmap H8o) All 16 `VK_FORMAT_BC*` formats are now a legal blit
  // *source*: `runBlitImage`'s own decode-then-resample pipeline decodes
  // every BC sub-family through `bcSamplingTarget`/`decodeBCBlock`
  // (BCSamplingBridge.h) into whichever already-runtime-supported
  // `ResourceFormat` matches its own shape, and `feme::graphics::
  // unpackColor`/`packClearColor` (ImageFixture.cpp) now have a case for
  // every one of those targets (`R8_UNORM`(`_SNORM`)/`R8G8_UNORM`
  // (`_SNORM`)/`R16G16B16A16_FLOAT`, alongside the RGBA8-shaped targets
  // already supported since H8n) -- this used to be narrower than what
  // `materializeImageDescriptor` supports for sampling; it no longer is.
  // `ImageOps.cpp`'s `runBlitImage` rejects a block-compressed
  // *destination* outright (no ASTC/BC encoder exists to repack into one)
  // and an HDR ASTC *source* (`decodeASTCBlockHDR` produces floats through
  // a different interface than the UNORM8 one this pipeline shares), but
  // accepts every other combination either way.
  if (!BlockCompressed || ASTCLdr || isBCFormat(Format) || isETC2Format(Format))
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
  // (Roadmap H19o) `R10G10B10A2_SNORM`: the signed-normalized sibling of
  // `R10G10B10A2_UNORM` above, decoded by `femeRTUnpackImageTexel`'s own
  // `R10G10B10A2_SNORM` case.
  case ResourceFormat::R10G10B10A2_SNORM:
  case ResourceFormat::B8G8R8A8_UNORM:
  // (Roadmap H8r) `B8G8R8A8_UNORM_SRGB`: the sRGB sibling of
  // `B8G8R8A8_UNORM` above, decoded (with the sRGB gamma curve applied)
  // by `femeRTUnpackImageTexel`'s own `B8G8R8A8_UNORM_SRGB` case, a
  // CTS-confirmed genuine `SAMPLED_IMAGE_BIT`/`BLIT_*` gap: unlike every
  // other H8 packed-format gap, this format wasn't merely under-reported
  // -- `mapVkFormat` had no case for it at all before this row.
  case ResourceFormat::B8G8R8A8_UNORM_SRGB:
  case ResourceFormat::A8_UNORM:
  case ResourceFormat::A1B5G5R5_UNORM:
  // (Roadmap H8e) `B4G4R4A4_UNORM`/`A1R5G5B5_UNORM`: two more of roadmap
  // H7r's own packed 16-bit formats, decoded by
  // `femeRTUnpackImageTexel`'s own `B4G4R4A4_UNORM`/`A1R5G5B5_UNORM`
  // cases, a CTS-confirmed genuine `SAMPLED_IMAGE_BIT` gap rather than a
  // reporting-only one.
  case ResourceFormat::B4G4R4A4_UNORM:
  case ResourceFormat::A1R5G5B5_UNORM:
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
  // (Roadmap H8g) `R5G6B5_UNORM`/`B5G6R5_UNORM`: the last two of roadmap
  // H7r's own packed 16-bit formats without a real runtime sampling case,
  // decoded by `femeRTUnpackImageTexel`'s own `R5G6B5_UNORM`/
  // `B5G6R5_UNORM` cases now (`femeRTUnpackR5G6B5Unorm`), a CTS-confirmed
  // genuine `SAMPLED_IMAGE_BIT` gap mirroring H8e's own precedent for
  // `B4G4R4A4_UNORM`/`A1R5G5B5_UNORM` above.
  case ResourceFormat::R5G6B5_UNORM:
  case ResourceFormat::B5G6R5_UNORM:
    Flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    break;
  // (Roadmap H8q) `E5B9G9R9_UFLOAT`: decoded by
  // `femeRTUnpackImageTexel`'s own `E5B9G9R9_UFLOAT` case
  // (`femeRTUnpackRGB9E5`).
  case ResourceFormat::E5B9G9R9_UFLOAT:
    Flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    break;
  // (Roadmap H19n) `R16G16_{FLOAT,UNORM,SNORM}`: the two-channel
  // analogues of `R16_{FLOAT,UNORM,SNORM}` above.
  case ResourceFormat::R16G16_FLOAT:
  case ResourceFormat::R16G16_UNORM:
  case ResourceFormat::R16G16_SNORM:
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
  // (Roadmap H19o) `R10G10B10A2_SINT`: the signed-integer sibling of
  // `R10G10B10A2_UINT` above, bit-for-bit identical storage (see
  // `femeRTUnpackR10G10B10A2Uint`'s own comment).
  case ResourceFormat::R10G10B10A2_SINT:
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
  // (Roadmap H19n) `R16G16_UINT`/`_SINT`: the two-channel analogues of
  // `R16_UINT`/`_SINT` above.
  case ResourceFormat::R16G16_UINT:
  case ResourceFormat::R16G16_SINT:
  // (Roadmap H8s) `R32_{UINT,SINT}`/`R32G32_{UINT,SINT}`: a real CTS
  // re-run against feme's own ICD found these four still missing
  // `SAMPLED_IMAGE_BIT` -- a plain omission from this switch's own
  // integer-format coverage above (every structurally-similar narrower
  // integer format is already listed), not a runtime gap:
  // `femeRTUnpackImageTexelI32` (FeMeRuntimeCPU.c, roadmap H19a) already
  // has real decode cases for all four, reused already by the
  // storage-image and texel-buffer read paths.
  case ResourceFormat::R32_UINT:
  case ResourceFormat::R32_SINT:
  case ResourceFormat::R32G32_UINT:
  case ResourceFormat::R32G32_SINT:
    Flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    break;
  // (Roadmap H8e) `D16_UNORM`: a CTS-confirmed genuine `SAMPLED_IMAGE_BIT`
  // gap, not a rendering-capability one -- `femeRTFetchTexel2D` already
  // decodes it via `femeRTUnpackImageTexel`'s own `D16_UNORM` case
  // (roadmap F8b), this switch just never advertised it. No
  // `_FILTER_LINEAR_BIT`: the real mandatory-format-table `deqp-vk` run
  // this row's own investigation used does not require it for
  // `d16_unorm`, unlike the packed-16-bit formats above.
  case ResourceFormat::D16_UNORM:
    Flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    break;
  default:
    if (ASTCLdr)
      Flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
               VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    // (Roadmap H8n) Every one of the 16 `VK_FORMAT_BC*` formats samples
    // too -- `materializeImageDescriptor` (CommandBuffer.cpp) decodes
    // each into whichever already-runtime-supported `ResourceFormat`
    // matches its own channel count/precision before the runtime ever
    // sees it, exactly mirroring the ASTC LDR bridge above (see that
    // function's own comment for the per-format target list).
    else if (isBCFormat(Format))
      Flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
               VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    // (Roadmap H8j) Every one of the 10 `VK_FORMAT_ETC2_*`/
    // `VK_FORMAT_EAC_*` formats samples too, mirroring the BC bridge
    // immediately above (`ETC2SamplingBridge.h`'s own per-format target
    // list).
    else if (isETC2Format(Format))
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
  // (Roadmap H19n) `R16G16_{FLOAT,UNORM,SNORM,UINT,SINT}`: the
  // two-channel mandatory-extended-format formats, backed by new
  // `femeRTPackImageTexel`/`femeRTPackImageTexelI32` cases
  // (FeMeRuntimeCPU.c).
  case ResourceFormat::R16G16_FLOAT:
  case ResourceFormat::R16G16_UNORM:
  case ResourceFormat::R16G16_SNORM:
  case ResourceFormat::R16G16_UINT:
  case ResourceFormat::R16G16_SINT:
  // (Roadmap H19n) `R32G32_UINT`/`R32G32_SINT`: the storage-mandatory
  // two-component partial siblings of `R32G32B32A32_{UINT,SINT}`, an
  // identity format needing no scalar conversion, backed by new
  // `femeRTPackImageTexelI32` cases (FeMeRuntimeCPU.c).
  case ResourceFormat::R32G32_UINT:
  case ResourceFormat::R32G32_SINT:
  // (Roadmap H8s) `R32G32_FLOAT`/`R8G8B8A8_{UINT,UNORM}`: a real CTS
  // re-run found these three still missing `STORAGE_IMAGE_BIT`, part of
  // Vulkan's own mandatory storage-image floor regardless of
  // `shaderStorageImageExtendedFormats` -- a plain omission from this
  // switch above, not a runtime gap: `femeRTPackImageTexel`/
  // `PackImageTexelI32` (FeMeRuntimeCPU.c, roadmap H8d) already have real
  // encode cases for all three, reused already by the texel-buffer
  // write path.
  case ResourceFormat::R32G32_FLOAT:
  case ResourceFormat::R8G8B8A8_UINT:
  case ResourceFormat::R8G8B8A8_UNORM:
    Flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    break;
  // (Roadmap H19n) The packed 32-bit formats
  // `A2B10G10R10_{UNORM,UINT}_PACK32`/`B10G11R11_UFLOAT_PACK32`, backed
  // by new `femeRTPackR10G10B10A2Unorm`/`Uint`/`femeRTPackR11G11B10Float`
  // helpers (FeMeRuntimeCPU.c) that are each the mathematical inverse of
  // this project's own existing sampled-image unpack helper for the same
  // format -- confirmed by a real CTS re-run
  // (`dEQP-VK.image.load_store.with_format.*.{a2b10g10r10,b10g11r11}*`)
  // that this project's own storage-image bit layout for these formats
  // does in fact match its sampled-image decode.
  case ResourceFormat::R11G11B10_FLOAT:
  case ResourceFormat::R10G10B10A2_UNORM:
  case ResourceFormat::R10G10B10A2_UINT:
    Flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    break;
  // (Roadmap H19n) `R8G8B8A8_SNORM`/`_SINT`: a real mandatory
  // `shaderStorageImageExtendedFormats` entry discovered via the Vulkan
  // spec's own full mandatory list (Table "Required format support for
  // storage images with extended formats"), distinct from
  // `R8G8B8A8_UNORM`/`_UINT` staying unset -- backed by
  // `femeRTPackR8G8B8A8Snorm`/`Sint`, already defined for this project's
  // own texel-buffer conversion path and reused here as-is.
  case ResourceFormat::R8G8B8A8_SNORM:
  case ResourceFormat::R8G8B8A8_SINT:
    Flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    break;
  // (Roadmap H19o) `R10G10B10A2_{SNORM,SINT}`: the final two formats in
  // the real Vulkan spec's own mandatory `shaderStorageImageExtendedFormats`
  // list, backed by a new `femeRTUnpackR10G10B10A2Snorm`/
  // `femeRTPackR10G10B10A2Snorm` helper pair (`_SNORM`) and reused
  // `femeRTUnpackR10G10B10A2Uint`/`femeRTPackR10G10B10A2Uint` dispatch
  // (`_SINT`, bit-for-bit identical storage to `_UINT`).
  case ResourceFormat::R10G10B10A2_SNORM:
  case ResourceFormat::R10G10B10A2_SINT:
    Flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    break;
  default:
    break;
  }

  // (Roadmap H8v) `R32_{UINT,SINT}`: the only two formats Vulkan's own
  // mandatory-format table requires `STORAGE_IMAGE_ATOMIC_BIT` for that
  // this project can actually honor an image atomic against today --
  // `SPIRVResourceLowering.cpp`'s `hasOnlySupportedStorageImageUses` only
  // accepts an `AtomicRMWInst`/`AtomicCmpXchgInst` `getpointer` user for a
  // `Plain2D`, single-32-bit-scalar-*integer* storage image (SPIR-V
  // itself disallows an atomic against a float-channel image outright, so
  // `R32_FLOAT` never qualifies regardless of this project's own
  // coverage), lowered to a real hardware atomic
  // (`feme.cpu.image.atomic.*.2d.i32`, FeMeRuntimeCPU.c) rather than a
  // plain load-modify-store. Every other shape (arrayed/cube/
  // multisampled/1D/3D) is not yet lowered, but this bit is honestly
  // per-format, not per-view-shape, mirroring `STORAGE_IMAGE_BIT`'s own
  // "Plain2D-only for now" comment above.
  switch (Format) {
  case ResourceFormat::R32_UINT:
  case ResourceFormat::R32_SINT:
    Flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
    break;
  default:
    break;
  }

  if (isSupportedColorAttachmentFormat(Format)) {
    Flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    // (Roadmap H8p) Blending is undefined for an integer color format per
    // spec -- confirmed by the real CTS run this row's own investigation
    // used, which shows all 7 integer color-attachment formats
    // (`isIntegerColorAttachmentFormat`, `RuntimeABI.h`) never advertise
    // `COLOR_ATTACHMENT_BLEND_BIT` -- so only the non-integer formats
    // `isSupportedColorAttachmentFormat` also recognizes get it.
    if (!isIntegerColorAttachmentFormat(Format))
      Flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
  }
  if (isSupportedDepthAttachmentFormat(Format) ||
      isSupportedStencilAttachmentFormat(Format))
    Flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

  return Flags;
}
