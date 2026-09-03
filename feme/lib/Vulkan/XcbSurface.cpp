//===- XcbSurface.cpp - VK_KHR_xcb_surface implementation ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H10a's real, CI-exercisable platform surface (see
// feme/docs/FeMeVulkanDesign.md's "Window-system integration" and its Open
// Question 13): `VK_KHR_xcb_surface`'s two entry points, plus the
// `presentToSurface`/`currentSurfaceExtent` dispatch helpers `Surface.cpp`/
// `Swapchain.cpp` call for any surface kind (declared in Surface.h).
//
// This file is always compiled into `FeMeVulkanCore` regardless of whether
// `libxcb` was found at configure time (`FEME_HAVE_XCB`, see
// feme/CMakeLists.txt and lib/Vulkan/CMakeLists.txt) -- its own body picks
// between a real implementation and a trivial always-failing stub via
// `#if FEME_VULKAN_HAVE_XCB`, so the CMake source list itself never needs a
// conditional. This is safe because the *entry points* are only ever
// resolvable at all in a build with `FEME_VULKAN_HAVE_XCB` set (see
// EntryPoints.h's own guarded declarations and
// `ImplementedEntrypointsXcb.txt`'s conditional inclusion in the generated
// dispatch table) -- a build without xcb support simply never constructs a
// `Surface` with `SurfaceKind::Xcb` in the first place, so the stub bodies
// below are unreachable dead code, not a correctness risk.
//
// Presentation ("blit"): FeMeVulkanDesign.md describes presenting a
// host-memory swapchain image as "a blit reusing vkCmdCopyImage's own copy
// path" -- interpreted here as the conceptual parallel of an ICD-internal
// pixel copy (not literally `vkCmdCopyImage`, which operates on `VkImage`
// handles inside a command buffer, not a raw host pointer into a real
// window): `vkQueuePresentKHR`'s synchronous execution model (see Sync.h)
// makes present time the natural place to copy a swapchain image's already-
// rendered host memory (`Image::data()`) into the real X window via
// `xcb_put_image`, one scanline at a time to stay under the X protocol's
// own per-request size cap (`max-request-size`) regardless of image size.
//
//===----------------------------------------------------------------------===//

#include "Surface.h"

#if FEME_VULKAN_HAVE_XCB
#define VK_USE_PLATFORM_XCB_KHR
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
#endif

#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace feme::vulkan;

namespace feme::vulkan {

#if FEME_VULKAN_HAVE_XCB

VKAPI_ATTR VkResult VKAPI_CALL vkCreateXcbSurfaceKHR(
    VkInstance, const VkXcbSurfaceCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) {
  if (!pCreateInfo->connection || !pCreateInfo->window)
    return VK_ERROR_INITIALIZATION_FAILED;
  Allocator Alloc(pAllocator);
  Surface *Obj = Alloc.create<Surface>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
      static_cast<void *>(pCreateInfo->connection),
      static_cast<uint32_t>(pCreateInfo->window));
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pSurface = toHandle<VkSurfaceKHR>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice, uint32_t, xcb_connection_t *, xcb_visualid_t) {
  // Same "one worker pool, no real presentation-engine restriction" answer
  // `vkGetPhysicalDeviceSurfaceSupportKHR` gives (Surface.cpp).
  return VK_TRUE;
}

namespace {

xcb_connection_t *connection(Surface *Surf) {
  return static_cast<xcb_connection_t *>(Surf->xcbConnection());
}

xcb_window_t window(Surface *Surf) {
  return static_cast<xcb_window_t>(Surf->xcbWindow());
}

/// `Surf`'s real window's current geometry, or null if the query itself
/// fails (e.g. a lost connection).
std::unique_ptr<xcb_get_geometry_reply_t, void (*)(void *)>
queryGeometry(Surface *Surf) {
  xcb_connection_t *Conn = connection(Surf);
  xcb_get_geometry_cookie_t Cookie =
      xcb_get_geometry(Conn, window(Surf));
  return {xcb_get_geometry_reply(Conn, Cookie, nullptr), std::free};
}

} // namespace

bool presentToSurface(Surface *Surf, const void *PixelData, uint32_t Width,
                     uint32_t Height, bool SwapRedBlue) {
  if (Surf->kind() != SurfaceKind::Xcb)
    return true;

  xcb_connection_t *Conn = connection(Surf);
  xcb_window_t Window = window(Surf);
  auto Geometry = queryGeometry(Surf);
  if (!Geometry)
    return false;

  // A typical X11 TrueColor visual's own in-memory pixel layout (little-
  // endian, depth-24-in-32bpp) already matches `VK_FORMAT_B8G8R8A8_UNORM`
  // directly (B, G, R, X byte order); `VK_FORMAT_R8G8B8A8_UNORM` (the
  // other of Surface.cpp's `SupportedSurfaceFormats`) needs an R/B swap
  // into a staging buffer first.
  std::vector<uint8_t> Staging;
  const uint8_t *Bytes = static_cast<const uint8_t *>(PixelData);
  if (SwapRedBlue) {
    Staging.resize(static_cast<size_t>(Width) * Height * 4);
    for (size_t I = 0, N = Staging.size(); I < N; I += 4) {
      Staging[I + 0] = Bytes[I + 2];
      Staging[I + 1] = Bytes[I + 1];
      Staging[I + 2] = Bytes[I + 0];
      Staging[I + 3] = Bytes[I + 3];
    }
    Bytes = Staging.data();
  }

  xcb_gcontext_t Gc = xcb_generate_id(Conn);
  xcb_create_gc(Conn, Gc, Window, 0, nullptr);

  // One scanline at a time: the X protocol's own `max-request-size` (the
  // connection's `Geometry`-independent request-size cap, typically 256KB
  // by default) could otherwise legally reject a single whole-image
  // `xcb_put_image` call for a large enough swapchain image.
  uint32_t RowBytes = Width * 4;
  bool Ok = true;
  for (uint32_t Row = 0; Row < Height; ++Row) {
    xcb_void_cookie_t Cookie = xcb_put_image_checked(
        Conn, XCB_IMAGE_FORMAT_Z_PIXMAP, Window, Gc, Width, 1, 0,
        static_cast<int16_t>(Row), 0, Geometry->depth, RowBytes,
        Bytes + static_cast<size_t>(Row) * RowBytes);
    if (xcb_generic_error_t *Error = xcb_request_check(Conn, Cookie)) {
      std::free(Error);
      Ok = false;
      break;
    }
  }
  xcb_free_gc(Conn, Gc);
  xcb_flush(Conn);
  return Ok && xcb_connection_has_error(Conn) == 0;
}

std::optional<VkExtent2D> currentSurfaceExtent(Surface *Surf) {
  if (Surf->kind() != SurfaceKind::Xcb)
    return std::nullopt;
  auto Geometry = queryGeometry(Surf);
  if (!Geometry)
    return std::nullopt;
  return VkExtent2D{Geometry->width, Geometry->height};
}

#else // !FEME_VULKAN_HAVE_XCB

// No `libxcb` found at configure time (see feme/CMakeLists.txt's
// `FEME_HAVE_XCB`): `Surface::Kind` can never actually become `Xcb` in this
// build (nothing else in this configuration ever constructs one, and
// `vkCreateXcbSurfaceKHR` itself isn't even declared -- see EntryPoints.h),
// so these two definitions just need to exist and link; they are never
// reached in practice.
bool presentToSurface(Surface *, const void *, uint32_t, uint32_t, bool) {
  return true;
}

std::optional<VkExtent2D> currentSurfaceExtent(Surface *) {
  return std::nullopt;
}

#endif // FEME_VULKAN_HAVE_XCB

} // namespace feme::vulkan
