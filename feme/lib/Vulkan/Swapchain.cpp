//===- Swapchain.cpp - VkSwapchainKHR implementations --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Swapchain.h"
#include "EntryPoints.h"
#include "Format.h"
#include "Icd.h"
#include "Objects.h"
#include "Surface.h"
#include "Sync.h"

#include <cstdlib>
#include <cstring>

using namespace feme::vulkan;
using namespace feme::cpu;

namespace feme::vulkan {

Swapchain::Swapchain(const Allocator &Alloc, const PhysicalDeviceInfo &,
                     feme::cpu::ResourceFormat Format, uint32_t Width,
                     uint32_t Height, uint32_t ArrayLayers,
                     VkImageUsageFlags Usage, uint32_t ImageCount,
                     Surface *Surf)
    : Alloc(Alloc), Surf(Surf) {
  // A swapchain image is always 2D, single-mip, single-sample -- see
  // Swapchain.h's own comment on this ICD's initial `maxImageArrayLayers ==
  // 1` scope (`ArrayLayers` is threaded through anyway so a future increase
  // needs no signature change here).
  ImageDimension Dimension = ArrayLayers > 1 ? ImageDimension::Texture2DArray
                                             : ImageDimension::Texture2D;

  Images.reserve(ImageCount);
  Backing.reserve(ImageCount);
  States.reserve(ImageCount);
  for (uint32_t I = 0; I != ImageCount && Valid; ++I) {
    Image *Img = Alloc.create<Image>(
        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, VK_IMAGE_TYPE_2D, Dimension, Format,
        Width, Height, /*Depth=*/1, /*MipLevels=*/1, ArrayLayers,
        /*SampleCount=*/1, Usage);
    if (!Img) {
      Valid = false;
      break;
    }
    Images.push_back(Img);

    // Mirrors `vkAllocateMemory` (Memory.cpp) + `vkBindImageMemory`
    // (Image.cpp) combined: the application never calls either itself for
    // a swapchain image (see this file's own header comment), so this ICD
    // must perform both here.
    void *Data = allocateDeviceMemory(static_cast<size_t>(Img->sizeInBytes()),
                                      alignof(std::max_align_t));
    if (!Data) {
      Valid = false;
      break;
    }
    DeviceMemory *Mem = Alloc.create<DeviceMemory>(
        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, Data, Img->sizeInBytes());
    if (!Mem) {
      std::free(Data);
      Valid = false;
      break;
    }
    Backing.push_back(Mem);
    Img->bind(Mem, 0);
    States.push_back(ImageState::Available);
  }
}

Swapchain::~Swapchain() {
  for (Image *Img : Images)
    Alloc.destroy(Img);
  for (DeviceMemory *Mem : Backing) {
    std::free(Mem->data());
    Alloc.destroy(Mem);
  }
}

std::optional<uint32_t> Swapchain::acquireNextImage() {
  for (size_t I = 0; I != States.size(); ++I) {
    if (States[I] == ImageState::Available) {
      States[I] = ImageState::Acquired;
      return static_cast<uint32_t>(I);
    }
  }
  return std::nullopt;
}

bool Swapchain::presentImage(uint32_t Index) {
  if (Index >= States.size() || States[Index] != ImageState::Acquired)
    return false;
  States[Index] = ImageState::Available;
  return true;
}

namespace {

/// Roadmap H10's own initial cap on `imageArrayLayers`, matching
/// `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`'s `maxImageArrayLayers`
/// (Surface.cpp) -- kept as one named constant so the two never silently
/// drift apart.
constexpr uint32_t MaxSwapchainImageArrayLayers = 1;

} // namespace

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) {
  const PhysicalDeviceInfo &Info =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();

  // Validated the same way `vkCreateImage` validates its own shape
  // (Image.cpp): a parameter this ICD's own advertised capabilities
  // (Surface.cpp) rule out is rejected with `VK_ERROR_INITIALIZATION_FAILED`
  // rather than silently clamped, since `VkSwapchainCreateInfoKHR`'s fields
  // are not optional hints.
  if (pCreateInfo->imageArrayLayers == 0 ||
      pCreateInfo->imageArrayLayers > MaxSwapchainImageArrayLayers)
    return VK_ERROR_INITIALIZATION_FAILED;
  if (pCreateInfo->imageExtent.width == 0 ||
      pCreateInfo->imageExtent.height == 0 ||
      pCreateInfo->imageExtent.width >
          Info.Properties.limits.maxImageDimension2D ||
      pCreateInfo->imageExtent.height >
          Info.Properties.limits.maxImageDimension2D)
    return VK_ERROR_INITIALIZATION_FAILED;

  std::optional<feme::cpu::ResourceFormat> Format =
      mapVkFormat(pCreateInfo->imageFormat);
  if (!Format)
    return VK_ERROR_INITIALIZATION_FAILED;

  uint32_t ImageCount = pCreateInfo->minImageCount;

  Allocator Alloc(pAllocator);
  Swapchain *Obj = Alloc.create<Swapchain>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, Alloc, Info, *Format,
      pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height,
      pCreateInfo->imageArrayLayers, pCreateInfo->imageUsage, ImageCount,
      fromHandle<Surface>(pCreateInfo->surface));
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  if (!Obj->isValid()) {
    Alloc.destroy(Obj);
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
  }

  // (roadmap H10) An `oldSwapchain` is retired immediately: its
  // already-acquired images remain valid to present/destroy through until
  // the application itself calls `vkDestroySwapchainKHR` on it, but no
  // further `vkAcquireNextImageKHR` against it may succeed -- see
  // `Swapchain::retire`'s own comment.
  if (pCreateInfo->oldSwapchain != VK_NULL_HANDLE)
    fromHandle<Swapchain>(pCreateInfo->oldSwapchain)->retire();

  *pSwapchain = toHandle<VkSwapchainKHR>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySwapchainKHR(VkDevice, VkSwapchainKHR swapchain,
                      const VkAllocationCallbacks *pAllocator) {
  if (!swapchain)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<Swapchain>(swapchain));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount,
    VkImage *pSwapchainImages) {
  Swapchain *SC = fromHandle<Swapchain>(swapchain);
  uint32_t TrueCount = SC->imageCount();
  if (!pSwapchainImages) {
    *pSwapchainImageCount = TrueCount;
    return VK_SUCCESS;
  }
  uint32_t ToCopy =
      *pSwapchainImageCount < TrueCount ? *pSwapchainImageCount : TrueCount;
  for (uint32_t I = 0; I != ToCopy; ++I)
    pSwapchainImages[I] = SC->image(I);
  *pSwapchainImageCount = ToCopy;
  return ToCopy < TrueCount ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice, VkSwapchainKHR swapchain, uint64_t /*timeout*/,
    VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex) {
  Swapchain *SC = fromHandle<Swapchain>(swapchain);
  if (SC->isRetired())
    return VK_ERROR_OUT_OF_DATE_KHR;

  std::optional<uint32_t> Index = SC->acquireNextImage();
  if (!Index)
    // No image could ever become available without an intervening
    // `vkQueuePresentKHR` from this same, single calling thread (see
    // Swapchain.h's own comment) -- so, exactly like an unresolvable
    // semaphore wait in `vkQueueSubmit` (Sync.h), no `timeout` however long
    // could let this call make progress; report `VK_TIMEOUT` immediately
    // rather than perform a real, blocking sleep.
    return VK_TIMEOUT;

  *pImageIndex = *Index;
  // Every acquired image is immediately ready: this ICD's queue executes
  // synchronously (Sync.h), so there is no real "GPU still using this
  // image" latency to wait out -- signal both objects up front, matching
  // `vkQueueSubmit`'s own synchronous fence/semaphore signaling.
  if (semaphore != VK_NULL_HANDLE)
    fromHandle<Semaphore>(semaphore)->signalBinary();
  if (fence != VK_NULL_HANDLE)
    fromHandle<Fence>(fence)->signal();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueuePresentKHR(VkQueue, const VkPresentInfoKHR *pPresentInfo) {
  // Present's own wait semaphores are consumed the same way
  // `vkQueueSubmit`'s are (Sync.cpp) -- an unresolved one here is a real
  // application ordering error, not something this synchronous, single-
  // queue driver could ever wait out (see Swapchain.h's own comment).
  for (uint32_t I = 0; I != pPresentInfo->waitSemaphoreCount; ++I) {
    if (!fromHandle<Semaphore>(pPresentInfo->pWaitSemaphores[I])
             ->waitAndConsumeBinary())
      return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkResult FinalResult = VK_SUCCESS;
  for (uint32_t I = 0; I != pPresentInfo->swapchainCount; ++I) {
    Swapchain *SC = fromHandle<Swapchain>(pPresentInfo->pSwapchains[I]);
    VkResult ThisResult = VK_SUCCESS;
    if (SC->isRetired())
      ThisResult = VK_ERROR_OUT_OF_DATE_KHR;
    else if (!SC->presentImage(pPresentInfo->pImageIndices[I]))
      // Presenting an image that was never acquired (or already presented)
      // is invalid application usage the real spec leaves undefined; this
      // ICD instead reports it the same defensive way `vkQueueSubmit`
      // reports any other detected ordering error, rather than silently
      // accepting or crashing on it.
      ThisResult = VK_ERROR_INITIALIZATION_FAILED;
    else if (Image *Img = SC->imageObject(pPresentInfo->pImageIndices[I])) {
      // Roadmap H10a: copy this now-presented image's real pixels to its
      // surface's real backing window, if any (Surface.h's own
      // `presentToSurface` -- a no-op for a non-`Xcb`-kind surface). A
      // genuine presentation failure here (e.g. a lost X connection) is a
      // real, spec-defined `VK_ERROR_SURFACE_LOST_KHR` -- a different
      // failure class than the application-ordering errors above (external
      // resource loss, not application misuse), so it is never conflated
      // with `VK_ERROR_INITIALIZATION_FAILED`.
      bool SwapRedBlue = Img->format() == feme::cpu::ResourceFormat::
                             R8G8B8A8_UNORM;
      if (!presentToSurface(SC->surface(), Img->data(), Img->width(),
                            Img->height(), SwapRedBlue))
        ThisResult = VK_ERROR_SURFACE_LOST_KHR;
    }

    if (pPresentInfo->pResults)
      pPresentInfo->pResults[I] = ThisResult;
    if (ThisResult != VK_SUCCESS)
      FinalResult = ThisResult;
  }
  return FinalResult;
}

// Roadmap H10c: `VK_KHR_swapchain`'s own device-group companion commands
// (see EntryPoints.h's own comment on why these were missing entirely
// until now). Every value below reflects the same fact
// `vkEnumeratePhysicalDeviceGroups` (EntryPoints.cpp) does: this ICD's one
// physical-device group has exactly one member, so
// `VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR` is the only present mode
// that can ever apply, and every device index an application legally
// passes is device 0.
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupPresentCapabilitiesKHR(
    VkDevice, VkDeviceGroupPresentCapabilitiesKHR
                  *pDeviceGroupPresentCapabilities) {
  std::memset(pDeviceGroupPresentCapabilities->presentMask, 0,
             sizeof(pDeviceGroupPresentCapabilities->presentMask));
  pDeviceGroupPresentCapabilities->presentMask[0] = 1;
  pDeviceGroupPresentCapabilities->modes =
      VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModesKHR(
    VkDevice, VkSurfaceKHR, VkDeviceGroupPresentModeFlagsKHR *pModes) {
  *pModes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDevicePresentRectanglesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pRectCount, VkRect2D *pRects) {
  // Only meaningful when `VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_MULTI_DEVICE_BIT_KHR`
  // is set (never true here, see `vkGetDeviceGroupSurfacePresentModesKHR`
  // above) -- still expected to report one rectangle covering the whole
  // surface, matching a single-device group's one implicit rectangle.
  VkSurfaceCapabilitiesKHR Caps{};
  VkResult Result = feme::vulkan::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      physicalDevice, surface, &Caps);
  if (Result != VK_SUCCESS)
    return Result;
  if (!pRects) {
    *pRectCount = 1;
    return VK_SUCCESS;
  }
  if (*pRectCount < 1) {
    *pRectCount = 0;
    return VK_INCOMPLETE;
  }
  pRects[0] = VkRect2D{{0, 0}, Caps.currentExtent};
  *pRectCount = 1;
  return VK_SUCCESS;
}

} // namespace feme::vulkan
