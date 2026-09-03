//===- Swapchain.h - VkSwapchainKHR object model ----------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The roadmap H10 `VkSwapchainKHR` object model (see "Window-system
// integration" in feme/docs/FeMeVulkanDesign.md): `VK_KHR_swapchain`'s
// object, `vkCreateSwapchainKHR`/`vkDestroySwapchainKHR`/
// `vkGetSwapchainImagesKHR`/`vkAcquireNextImageKHR`/`vkQueuePresentKHR` --
// the "full swapchain state machine" the design doc's own "headless first"
// decision calls for, against `Surface.h`'s headless surface.
//
// Unlike an application-created `VkImage` (which the application separately
// calls `vkAllocateMemory`/`vkBindImageMemory` for), a swapchain's own
// images are allocated and bound entirely internally at
// `vkCreateSwapchainKHR` time -- the application only ever receives their
// handles (`vkGetSwapchainImagesKHR`) and must never call `vkDestroyImage`/
// `vkFreeMemory` on them itself (Vulkan forbids it); `Swapchain`'s
// destructor is what frees both, mirroring `vkDestroyImage`/`vkFreeMemory`'s
// combined effect.
//
// This ICD's `vkQueueSubmit` already executes every command buffer
// synchronously on the calling thread (Sync.h's own file comment: a real
// fence/semaphore is always already in its final state by the time
// anything could observe it). Acquire/present inherit that same
// simplification directly: there is no real display and therefore no real
// presentation latency to model, so `vkAcquireNextImageKHR` signals its
// semaphore/fence immediately rather than after any real wait, and
// `vkQueuePresentKHR`'s own wait semaphores are consumed the same way
// `vkQueueSubmit`'s are -- an unresolved one is a genuine application
// ordering error (`VK_ERROR_INITIALIZATION_FAILED`), not something this
// synchronous driver could ever resolve by waiting longer. Out-of-date
// detection is a deliberate non-goal for a headless surface: there is no
// real window to resize and so no real event that could ever produce
// `VK_ERROR_OUT_OF_DATE_KHR` on its own (it is still reported once an
// application retires a swapchain via `oldSwapchain`, the one source of
// out-of-dateness this ICD can genuinely produce).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_SWAPCHAIN_H
#define FEME_LIB_VULKAN_SWAPCHAIN_H

#include "Icd.h"
#include "Image.h"
#include "Memory.h"
#include "PhysicalDeviceInfo.h"
#include "Surface.h"

#include "feme/Target/CPU/RuntimeABI.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace feme::vulkan {

/// A `VkSwapchainKHR`. Not dispatchable (see "Object Model"). Owns
/// `ImageCount` `Image`s, each with its own real backing `DeviceMemory` (see
/// this file's own comment on why a swapchain must allocate and bind that
/// itself), plus each image's `Available`/`Acquired` bookkeeping state.
class Swapchain {
public:
  /// Constructs \p ImageCount images of the given shape/format/usage, each
  /// bound to its own freshly allocated backing store. `isValid()` is false
  /// if any image or its backing memory failed to allocate partway through
  /// (mirroring `Alloc.create`'s own null-on-failure convention, but a
  /// constructor cannot itself return null, so the caller must check this
  /// before publishing the handle). \p Surf is the surface this swapchain
  /// presents to (roadmap H10a: `vkQueuePresentKHR` copies a presented
  /// image's real pixels there via `presentToSurface`, Surface.h); it is
  /// not owned here -- `vkCreateSwapchainKHR`'s own `pCreateInfo->surface`
  /// outlives every swapchain created against it, per the spec's own
  /// destruction-order rules.
  Swapchain(const Allocator &Alloc, const PhysicalDeviceInfo &Info,
            feme::cpu::ResourceFormat Format, uint32_t Width, uint32_t Height,
            uint32_t ArrayLayers, VkImageUsageFlags Usage, uint32_t ImageCount,
            Surface *Surf);
  ~Swapchain();

  Swapchain(const Swapchain &) = delete;
  Swapchain &operator=(const Swapchain &) = delete;

  bool isValid() const { return Valid; }
  uint32_t imageCount() const { return static_cast<uint32_t>(Images.size()); }
  VkImage image(uint32_t Index) const {
    return toHandle<VkImage>(Images[Index]);
  }
  /// The underlying `Image` object itself, rather than its handle -- used
  /// by `vkQueuePresentKHR` to reach `Image::data()`/`width()`/`height()`
  /// directly without a round-trip through `fromHandle`.
  Image *imageObject(uint32_t Index) const { return Images[Index]; }

  /// The surface this swapchain presents to, or null for a swapchain never
  /// given one (not possible via the public API -- `vkCreateSwapchainKHR`
  /// always requires a surface -- but a defensive accessor regardless).
  Surface *surface() const { return Surf; }

  /// `vkAcquireNextImageKHR`'s own image-selection logic: the first
  /// `Available` image, marked `Acquired` in the same step, or
  /// `std::nullopt` if every image is currently `Acquired` -- see this
  /// file's own comment on why no image could ever become available
  /// without an intervening `vkQueuePresentKHR` on this same, single
  /// calling thread.
  std::optional<uint32_t> acquireNextImage();

  /// `vkQueuePresentKHR`'s own per-swapchain validation: whether \p Index
  /// names an image that was actually `Acquired` (an out-of-range or
  /// already-`Available` index is a real application usage error, per the
  /// spec's own "must have been acquired" precondition); marks it
  /// `Available` again on success.
  bool presentImage(uint32_t Index);

  /// Marks this swapchain retired: no further `acquireNextImage` may
  /// succeed, but already-`Acquired` images remain presentable/queryable
  /// until the application itself destroys this swapchain, per the spec's
  /// own "an application may continue presenting already-acquired images
  /// from a retired swapchain" allowance -- `presentImage` above is
  /// unaffected by this flag for exactly that reason.
  void retire() { Retired = true; }
  bool isRetired() const { return Retired; }

private:
  enum class ImageState : uint8_t { Available, Acquired };

  Allocator Alloc;
  std::vector<Image *> Images;
  std::vector<DeviceMemory *> Backing;
  std::vector<ImageState> States;
  Surface *Surf = nullptr;
  bool Valid = true;
  bool Retired = false;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_SWAPCHAIN_H
