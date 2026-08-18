//===- Format.h - VkFormat -> feme::cpu::ResourceFormat mapping -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// V4 ("Map supported VkFormat values to ResourceFormat", see "V4: Typed
// buffers and broader compute" in feme/docs/FeMeVulkanDesign.md): a table
// from every `VkFormat` a texel buffer or (V5+) image descriptor may name to
// the corresponding `feme::cpu::ResourceFormat` the CPU target's descriptor
// heap already carries -- see `feme::cpu::ResourceFormat` in
// feme/include/feme/Target/CPU/RuntimeABI.h. Only the identity 32-bit
// formats and `R8G8B8A8_UNORM` are actually *consumed* by a texel buffer in
// this milestone (see Descriptor.h's file comment for why); this table maps
// every format `ResourceFormat` itself lists, ahead of the images work that
// will need the rest of them (V5), so it does not have to be revisited
// piecemeal later.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_FORMAT_H
#define FEME_LIB_VULKAN_FORMAT_H

#include "feme/Target/CPU/RuntimeABI.h"

#include <vulkan/vulkan_core.h>

#include <optional>

namespace feme::vulkan {

/// Maps \p Format to the `feme::cpu::ResourceFormat` it corresponds to, or
/// `std::nullopt` if this ICD does not recognize/support \p Format at all.
std::optional<feme::cpu::ResourceFormat> mapVkFormat(VkFormat Format);

/// The size in bytes of one texel of \p Format, as this ICD lays it out in
/// linear buffer/image memory -- the same value `mapVkFormat`'s caller needs
/// to compute a texel buffer's element stride. Returns 0 for a format
/// `mapVkFormat` does not recognize.
uint32_t formatElementSize(feme::cpu::ResourceFormat Format);

/// Returns whether \p Format is one of the formats the CPU runtime's
/// typed-load/store helpers (feme/runtime/CPU/FeMeRuntimeCPU.c) actually
/// implement a conversion for, and so may legally back a texel buffer's
/// `VkBufferView` (see Descriptor.h's file comment). `vkCreateBufferView`
/// rejects every other format -- including one `mapVkFormat` itself maps
/// successfully -- with `VK_ERROR_FORMAT_NOT_SUPPORTED` rather than silently
/// misconverting it.
bool isTexelBufferFormatSupported(feme::cpu::ResourceFormat Format);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_FORMAT_H
