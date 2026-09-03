//===- Surface.h - VkSurfaceKHR object model --------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The roadmap H10 `VkSurfaceKHR` object model (see "Window-system
// integration" in feme/docs/FeMeVulkanDesign.md): `VK_KHR_surface`'s object
// and query commands, plus `VK_EXT_headless_surface`'s one creation command
// -- the design doc's own "headless first" decision, since a headless
// surface has no real window to depend on and exercises every later
// swapchain state transition (Swapchain.h) without one.
//
// This is also the first extension pair this ICD advertises at the
// *instance* level (`getSupportedInstanceExtensions` below): `vk.xml` marks
// both `VK_KHR_surface` and `VK_EXT_headless_surface` `type="instance"`,
// unlike every extension `getSupportedDeviceExtensions`
// (PhysicalDeviceInfo.h) reports, which are all `type="device"`. Before
// this, `vkEnumerateInstanceExtensionProperties` and `vkCreateInstance`'s
// enabled-extension validation wrongly reused the device list (there was
// no instance-level extension to report), which is the literal gap the
// roadmap called out.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_SURFACE_H
#define FEME_LIB_VULKAN_SURFACE_H

#include "llvm/ADT/ArrayRef.h"

#include <vulkan/vulkan_core.h>

namespace feme::vulkan {

/// The instance-level extensions this ICD implements and advertises via
/// `vkEnumerateInstanceExtensionProperties`/validates against in
/// `vkCreateInstance` -- see this file's own comment on why this is
/// distinct from `getSupportedDeviceExtensions`.
llvm::ArrayRef<VkExtensionProperties> getSupportedInstanceExtensions();

/// A `VkSurfaceKHR`. Not dispatchable (see "Object Model"). Roadmap H10
/// implements exactly one surface kind so far -- headless
/// (`VK_EXT_headless_surface`), created with no real window to depend on
/// -- so this class carries no per-kind state today; a future platform
/// surface (roadmap H10a) is expected to add a `Kind`/backing-handle
/// discriminator here rather than a new sibling class, since every
/// `VkSurfaceKHR` shares the same `vkDestroySurfaceKHR`/
/// `vkGetPhysicalDeviceSurface*KHR` query entry points regardless of how it
/// was created.
class Surface {
public:
  Surface() = default;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_SURFACE_H
