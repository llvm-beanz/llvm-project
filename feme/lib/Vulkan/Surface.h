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

#include <cstdint>
#include <optional>
#include <vulkan/vulkan_core.h>

namespace feme::vulkan {

/// The instance-level extensions this ICD implements and advertises via
/// `vkEnumerateInstanceExtensionProperties`/validates against in
/// `vkCreateInstance` -- see this file's own comment on why this is
/// distinct from `getSupportedDeviceExtensions`.
llvm::ArrayRef<VkExtensionProperties> getSupportedInstanceExtensions();

/// Which backing kind a `Surface` was created as -- see `Surface`'s own
/// comment for why every kind shares one class rather than a class
/// hierarchy.
enum class SurfaceKind {
  /// `VK_EXT_headless_surface` (roadmap H10): no real window at all.
  Headless,
  /// `VK_KHR_xcb_surface` (roadmap H10a): a real X11 window, reached via
  /// `libxcb` -- see XcbSurface.cpp and FeMeVulkanDesign.md's
  /// "Window-system integration" for why xcb (backed by Xvfb, needing no
  /// real display hardware) is this tree's own answer to "which platform
  /// surface can this project's CI genuinely exercise".
  Xcb,
};

/// A `VkSurfaceKHR`. Not dispatchable (see "Object Model"). Roadmap H10
/// implemented exactly one surface kind -- headless
/// (`VK_EXT_headless_surface`), created with no real window to depend on;
/// roadmap H10a adds a second, xcb-backed kind (`VK_KHR_xcb_surface`)
/// reusing this same class with a `Kind` discriminator plus opaque
/// per-kind state, rather than a new sibling class, since every
/// `VkSurfaceKHR` shares the same `vkDestroySurfaceKHR`/
/// `vkGetPhysicalDeviceSurface*KHR` query entry points regardless of how it
/// was created.
///
/// The xcb connection/window are stored as opaque `void *`/`uint32_t`
/// (not `xcb_connection_t *`/`xcb_window_t`) so this header itself never
/// needs to include `<xcb/xcb.h>` -- every other TU that includes
/// `Surface.h` (most of `FeMeVulkanCore`) stays buildable whether or not
/// `libxcb` was found at configure time (see `feme/CMakeLists.txt`'s
/// `FEME_HAVE_XCB`). Only XcbSurface.cpp, which does include the real
/// headers, ever `static_cast`s them back.
class Surface {
public:
  Surface() : Kind(SurfaceKind::Headless) {}
  Surface(void *XcbConnection, uint32_t XcbWindow)
      : Kind(SurfaceKind::Xcb), XcbConnection(XcbConnection),
        XcbWindow(XcbWindow) {}

  SurfaceKind kind() const { return Kind; }
  void *xcbConnection() const { return XcbConnection; }
  uint32_t xcbWindow() const { return XcbWindow; }

private:
  SurfaceKind Kind;
  void *XcbConnection = nullptr;
  uint32_t XcbWindow = 0;
};

/// Copies `Width`x`Height` tightly-packed RGBA8-family pixels starting at
/// `PixelData` (`Image::data()`'s own "packed CPU-side subresource layout",
/// see Image.h) into `Surf`'s real backing window, if any. A no-op that
/// returns `true` for a non-`Xcb`-kind surface (headless has nothing to
/// present to). `SwapRedBlue` requests an R/B channel swap first --
/// needed for `VK_FORMAT_R8G8B8A8_UNORM` since a typical X11 TrueColor
/// visual's own in-memory byte order already matches
/// `VK_FORMAT_B8G8R8A8_UNORM` directly (see XcbSurface.cpp). Returns
/// `false` only for a genuine presentation failure (e.g. a lost X
/// connection), for `vkQueuePresentKHR` to report as
/// `VK_ERROR_SURFACE_LOST_KHR`.
bool presentToSurface(Surface *Surf, const void *PixelData, uint32_t Width,
                      uint32_t Height, bool SwapRedBlue);

/// `Surf`'s real backing window's current size, queried live (not
/// cached) -- used by `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` to report
/// a real `currentExtent` instead of headless's `{UINT32_MAX, UINT32_MAX}`
/// sentinel. `std::nullopt` means two genuinely different things
/// depending on `Surf->kind()`, which callers must not conflate (roadmap
/// H10g): for a non-`Xcb`-kind surface it means "not applicable" (headless
/// has no real window to query, and the caller should report the
/// `{UINT32_MAX, UINT32_MAX}` sentinel instead); for an `Xcb`-kind surface
/// it means the live geometry query itself failed (e.g. a lost X
/// connection) -- a real, spec-legal `VK_ERROR_SURFACE_LOST_KHR` condition
/// the caller must report as such, *not* silently degrade into the same
/// headless sentinel (a real window's surface never has an "undefined"
/// size the way a headless one legitimately does).
std::optional<VkExtent2D> currentSurfaceExtent(Surface *Surf);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_SURFACE_H
