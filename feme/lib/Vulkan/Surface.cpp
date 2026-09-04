//===- Surface.cpp - VkSurfaceKHR implementations ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Surface.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"
#include "PhysicalDeviceInfo.h"

#include <cstdint>
#include <iterator>

#if FEME_VULKAN_HAVE_XCB
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan_xcb.h>
#include <xcb/xcb.h>
#endif

using namespace feme::vulkan;

namespace {

/// The same "null `pOut` reports the true count; non-null copies up to
/// `*pCount` entries, reporting `VK_INCOMPLETE` if there were more" pattern
/// EntryPoints.cpp's own (internal-linkage, so not reusable from here)
/// `enumerate` implements, needed again for this file's two
/// `vkGetPhysicalDeviceSurface{Formats,PresentModes}KHR` queries.
template <typename T>
VkResult enumerate(uint32_t TrueCount, const T *Source, uint32_t *pCount,
                   T *pOut) {
  if (!pOut) {
    *pCount = TrueCount;
    return VK_SUCCESS;
  }
  uint32_t ToCopy = *pCount < TrueCount ? *pCount : TrueCount;
  for (uint32_t I = 0; I < ToCopy; ++I)
    pOut[I] = Source[I];
  *pCount = ToCopy;
  return ToCopy < TrueCount ? VK_INCOMPLETE : VK_SUCCESS;
}

/// This ICD's own answer to `vkGetPhysicalDeviceSurfaceFormatsKHR`: two
/// widely-required 8-bit UNORM formats, both already implemented by
/// Format.cpp's `mapVkFormat` and usable as a color attachment
/// (GraphicsPipeline.cpp/CommandBuffer.cpp's dynamic-rendering color
/// attachment path) -- the only capability a swapchain image actually
/// needs (see Swapchain.h). `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR` is the one
/// color space every conformant implementation must support for at least
/// one format pair.
constexpr VkSurfaceFormatKHR SupportedSurfaceFormats[] = {
    {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
};

/// This ICD's own answer to `vkGetPhysicalDeviceSurfacePresentModesKHR`:
/// `VK_PRESENT_MODE_FIFO_KHR` only -- the one mode every surface must
/// support, and the only one this ICD's synchronous execution model (see
/// Sync.h's file comment) can honestly claim a *distinct* meaning for.
/// There is no real display and no real presentation engine backing this
/// headless surface for `IMMEDIATE`/`MAILBOX`/`FIFO_RELAXED` to actually
/// behave differently by, so advertising them would claim behavior this
/// driver cannot tell apart -- a deliberate, honest scope limit
/// (`FeMeVulkanDesign.md`'s "advertise only what's genuinely and distinctly
/// implemented" precedent), not an oversight.
constexpr VkPresentModeKHR SupportedPresentModes[] = {
    VK_PRESENT_MODE_FIFO_KHR,
};

} // namespace

namespace feme::vulkan {

llvm::ArrayRef<VkExtensionProperties> getSupportedInstanceExtensions() {
  static const VkExtensionProperties Extensions[] = {
      {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_SURFACE_SPEC_VERSION},
      {VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME,
       VK_EXT_HEADLESS_SURFACE_SPEC_VERSION},
#if FEME_VULKAN_HAVE_XCB
      {VK_KHR_XCB_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_SPEC_VERSION},
#endif
  };
  return Extensions;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateHeadlessSurfaceEXT(
    VkInstance, const VkHeadlessSurfaceCreateInfoEXT *,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) {
  // `VkHeadlessSurfaceCreateInfoEXT` carries nothing beyond `sType`/`pNext`/
  // `flags` (no real window handle exists to validate), so there is
  // nothing in `pCreateInfo` for this ICD to inspect.
  Allocator Alloc(pAllocator);
  Surface *Obj = Alloc.create<Surface>(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pSurface = toHandle<VkSurfaceKHR>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(
    VkInstance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator) {
  if (!surface)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<Surface>(surface));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32 *pSupported) {
  // Every queue family can present to a headless surface: there is no real
  // presentation engine restricting which family may submit to it (see
  // "Queue families" -- the same "one worker pool, capability flags only
  // narrow what it promises" precedent `queueFlags` itself follows).
  *pSupported = VK_TRUE;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) {
  const PhysicalDeviceInfo &Info =
      fromHandle<PhysicalDevice>(physicalDevice)->getInfo();
  VkSurfaceCapabilitiesKHR &Caps = *pSurfaceCapabilities;
  Caps = VkSurfaceCapabilitiesKHR{};
  Caps.minImageCount = 1;
  // No fixed upper bound: a headless surface has no real display memory
  // budget to exhaust (`0` is the spec's own "no maximum" sentinel).
  Caps.maxImageCount = 0;
  // `{UINT32_MAX, UINT32_MAX}` is the spec's own sentinel for "the surface
  // has no fixed size of its own -- the application picks
  // `imageExtent` freely within `min`/`maxImageExtent` below", the
  // conventional answer every real headless-surface implementation gives
  // since there is no real window to report a size from. A real xcb
  // window (roadmap H10a) does have a genuine current size, so report
  // that live instead -- and (roadmap H10g) a *failure* to query it is a
  // real, spec-legal `VK_ERROR_SURFACE_LOST_KHR` (e.g. a lost X
  // connection), never the same "undefined size" sentinel: conflating the
  // two previously let a real geometry-query failure masquerade as a
  // successful, plausible-looking capabilities query, silently deferring
  // the actual failure to a later `vkCreateSwapchainKHR` rejection with no
  // diagnostic of its own (see `currentSurfaceExtent`'s own comment).
  Surface *Surf = fromHandle<Surface>(surface);
  if (Surf->kind() == SurfaceKind::Xcb) {
    std::optional<VkExtent2D> Extent = currentSurfaceExtent(Surf);
    if (!Extent)
      return VK_ERROR_SURFACE_LOST_KHR;
    Caps.currentExtent = *Extent;
  } else {
    Caps.currentExtent = {UINT32_MAX, UINT32_MAX};
  }
  Caps.minImageExtent = {1, 1};
  Caps.maxImageExtent = {Info.Properties.limits.maxImageDimension2D,
                         Info.Properties.limits.maxImageDimension2D};
  // Roadmap H10's initial scope keeps a swapchain image single-layered
  // (Swapchain.h); a future increment may raise this once a multi-layer
  // swapchain's present semantics (which layer is actually "presented"?)
  // are worked out, but `Image` itself already supports more.
  Caps.maxImageArrayLayers = 1;
  Caps.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  Caps.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  // No real compositor exists to blend a headless surface's image against
  // anything else, so `OPAQUE` -- ignore alpha entirely -- is the only
  // honest claim (see `SupportedPresentModes`'s own comment on this file's
  // "advertise only what's genuinely, distinctly implemented" precedent).
  Caps.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  Caps.supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice, VkSurfaceKHR, uint32_t *pSurfaceFormatCount,
    VkSurfaceFormatKHR *pSurfaceFormats) {
  return enumerate<VkSurfaceFormatKHR>(
      static_cast<uint32_t>(std::size(SupportedSurfaceFormats)),
      SupportedSurfaceFormats, pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice, VkSurfaceKHR, uint32_t *pPresentModeCount,
    VkPresentModeKHR *pPresentModes) {
  return enumerate<VkPresentModeKHR>(
      static_cast<uint32_t>(std::size(SupportedPresentModes)),
      SupportedPresentModes, pPresentModeCount, pPresentModes);
}

} // namespace feme::vulkan
