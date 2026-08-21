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
  }
  llvm_unreachable("unhandled ResourceFormat");
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
