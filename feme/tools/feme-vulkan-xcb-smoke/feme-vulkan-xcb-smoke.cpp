//===- feme-vulkan-xcb-smoke.cpp - xcb surface smoke-test client -*- C++ -==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H10a's own end-to-end verification (see "Window-system
// integration" in feme/docs/FeMeVulkanDesign.md): a tiny Vulkan client,
// linked against the real Khronos Vulkan loader (like
// feme-vulkan-graphics-smoke) *and* a real xcb connection, that creates a
// real X window, a `VkSurfaceKHR`/`VkSwapchainKHR` against it, clears one
// swapchain image to a distinctive color, presents it, and then reads the
// real window's pixels back via `xcb_get_image` -- confirming this ICD's
// `presentToSurface` (XcbSurface.cpp) genuinely copied real pixel data into
// the real window, not merely that every Vulkan call returned `VK_SUCCESS`.
//
// The clear color is chosen as pure, saturated red (`float32 = {1,0,0,1}`)
// specifically so a broken R/B channel swap (`SwapRedBlue`, needed for
// `VK_FORMAT_R8G8B8A8_UNORM`, see XcbSurface.cpp's own comment) would be
// caught as pure *blue* on screen instead -- an unambiguous, rounding-proof
// mismatch, unlike a partial color a rounding difference could plausibly
// also produce.
//
// This assumes the common Xvfb-default 24-bit-in-32bpp TrueColor visual
// (byte order B, G, R, X in memory) that XcbSurface.cpp's own
// `presentToSurface` comment documents as its target -- exactly the
// environment `system-xcb`'s own lit feature (feme/test/lit.cfg.py) is
// gated on.
//
//===----------------------------------------------------------------------===//

#include <vulkan/vulkan.h>

#define VK_USE_PLATFORM_XCB_KHR
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <vector>

namespace {

constexpr uint32_t FeMeVendorID = 0x10000;
constexpr uint32_t WindowSize = 64;

[[noreturn]] void fail(const char *Step, VkResult Result) {
  std::fprintf(stderr, "FAIL: %s (VkResult = %d)\n", Step,
               static_cast<int>(Result));
  std::exit(1);
}

[[noreturn]] void failMsg(const char *Msg) {
  std::fprintf(stderr, "FAIL: %s\n", Msg);
  std::exit(1);
}

} // namespace

int main() {
  // ------------------------------------------------------------------
  // A real xcb connection and window (the "real, CI-exercisable platform
  // surface" itself -- see this project's roadmap H10a).
  // ------------------------------------------------------------------
  int Screen = 0;
  xcb_connection_t *Connection = xcb_connect(nullptr, &Screen);
  if (xcb_connection_has_error(Connection))
    failMsg("xcb_connect");

  const xcb_setup_t *Setup = xcb_get_setup(Connection);
  xcb_screen_iterator_t ScreenIter = xcb_setup_roots_iterator(Setup);
  for (int I = 0; I < Screen; ++I)
    xcb_screen_next(&ScreenIter);
  xcb_screen_t *ScreenInfo = ScreenIter.data;

  xcb_window_t Window = xcb_generate_id(Connection);
  xcb_create_window(Connection, XCB_COPY_FROM_PARENT, Window,
                    ScreenInfo->root, 0, 0, WindowSize, WindowSize, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, ScreenInfo->root_visual, 0,
                    nullptr);
  xcb_map_window(Connection, Window);
  xcb_flush(Connection);

  // ------------------------------------------------------------------
  // Instance + FeMe device (mirrors feme-vulkan-loader-smoke).
  // ------------------------------------------------------------------
  const char *InstanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                      VK_KHR_XCB_SURFACE_EXTENSION_NAME};
  VkApplicationInfo AppInfo{};
  AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  AppInfo.pApplicationName = "feme-vulkan-xcb-smoke";
  AppInfo.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo InstanceInfo{};
  InstanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  InstanceInfo.pApplicationInfo = &AppInfo;
  InstanceInfo.enabledExtensionCount =
      static_cast<uint32_t>(std::size(InstanceExtensions));
  InstanceInfo.ppEnabledExtensionNames = InstanceExtensions;

  VkInstance Instance;
  if (VkResult R = vkCreateInstance(&InstanceInfo, nullptr, &Instance))
    fail("vkCreateInstance", R);

  uint32_t DeviceCount = 0;
  vkEnumeratePhysicalDevices(Instance, &DeviceCount, nullptr);
  std::vector<VkPhysicalDevice> Devices(DeviceCount);
  vkEnumeratePhysicalDevices(Instance, &DeviceCount, Devices.data());
  VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
  for (VkPhysicalDevice Device : Devices) {
    VkPhysicalDeviceProperties Props;
    vkGetPhysicalDeviceProperties(Device, &Props);
    if (Props.vendorID == FeMeVendorID)
      PhysicalDevice = Device;
  }
  if (!PhysicalDevice)
    failMsg("no device reported FeMe's vendor ID");

  uint32_t FamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &FamilyCount,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> Families(FamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &FamilyCount,
                                           Families.data());
  uint32_t GraphicsFamily = FamilyCount;
  for (uint32_t I = 0; I < FamilyCount; ++I)
    if (Families[I].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      GraphicsFamily = I;
      break;
    }
  if (GraphicsFamily == FamilyCount)
    failMsg("FeMe device reported no graphics queue family");

  VkBool32 PresentSupported = vkGetPhysicalDeviceXcbPresentationSupportKHR(
      PhysicalDevice, GraphicsFamily, Connection, ScreenInfo->root_visual);
  if (!PresentSupported)
    failMsg("vkGetPhysicalDeviceXcbPresentationSupportKHR reported no "
           "support");

  float Priority = 1.0f;
  VkDeviceQueueCreateInfo QueueInfo{};
  QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  QueueInfo.queueFamilyIndex = GraphicsFamily;
  QueueInfo.queueCount = 1;
  QueueInfo.pQueuePriorities = &Priority;

  const char *DeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo DeviceInfo{};
  DeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  DeviceInfo.queueCreateInfoCount = 1;
  DeviceInfo.pQueueCreateInfos = &QueueInfo;
  DeviceInfo.enabledExtensionCount =
      static_cast<uint32_t>(std::size(DeviceExtensions));
  DeviceInfo.ppEnabledExtensionNames = DeviceExtensions;

  VkDevice Device;
  if (VkResult R =
          vkCreateDevice(PhysicalDevice, &DeviceInfo, nullptr, &Device))
    fail("vkCreateDevice", R);
  VkQueue Queue;
  vkGetDeviceQueue(Device, GraphicsFamily, 0, &Queue);

  // ------------------------------------------------------------------
  // The real xcb-backed surface + swapchain (roadmap H10a itself).
  // ------------------------------------------------------------------
  VkXcbSurfaceCreateInfoKHR SurfaceInfo{};
  SurfaceInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
  SurfaceInfo.connection = Connection;
  SurfaceInfo.window = Window;
  VkSurfaceKHR Surface;
  if (VkResult R =
          vkCreateXcbSurfaceKHR(Instance, &SurfaceInfo, nullptr, &Surface))
    fail("vkCreateXcbSurfaceKHR", R);

  VkSurfaceCapabilitiesKHR Caps;
  if (VkResult R = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          PhysicalDevice, Surface, &Caps))
    fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", R);
  std::printf("currentExtent: %ux%u\n", Caps.currentExtent.width,
              Caps.currentExtent.height);
  // The whole point of roadmap H10a's `currentSurfaceExtent`
  // (XcbSurface.cpp): a real window's genuine current size, not
  // headless's `{UINT32_MAX, UINT32_MAX}` sentinel (Surface.cpp).
  if (Caps.currentExtent.width != WindowSize ||
      Caps.currentExtent.height != WindowSize)
    failMsg("currentExtent did not match the real window's own size");

  VkSwapchainCreateInfoKHR SwapchainInfo{};
  SwapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  SwapchainInfo.surface = Surface;
  SwapchainInfo.minImageCount = 2;
  SwapchainInfo.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
  SwapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  SwapchainInfo.imageExtent = Caps.currentExtent;
  SwapchainInfo.imageArrayLayers = 1;
  SwapchainInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  SwapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  SwapchainInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  SwapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  SwapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  SwapchainInfo.clipped = VK_TRUE;
  VkSwapchainKHR Swapchain;
  if (VkResult R =
          vkCreateSwapchainKHR(Device, &SwapchainInfo, nullptr, &Swapchain))
    fail("vkCreateSwapchainKHR", R);

  uint32_t ImageCount = 0;
  vkGetSwapchainImagesKHR(Device, Swapchain, &ImageCount, nullptr);
  std::vector<VkImage> Images(ImageCount);
  vkGetSwapchainImagesKHR(Device, Swapchain, &ImageCount, Images.data());

  // ------------------------------------------------------------------
  // Acquire, clear to pure red, present.
  // ------------------------------------------------------------------
  VkFenceCreateInfo FenceInfo{};
  FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence AcquireFence;
  if (VkResult R = vkCreateFence(Device, &FenceInfo, nullptr, &AcquireFence))
    fail("vkCreateFence", R);

  uint32_t ImageIndex;
  if (VkResult R = vkAcquireNextImageKHR(Device, Swapchain, UINT64_MAX,
                                        VK_NULL_HANDLE, AcquireFence,
                                        &ImageIndex))
    fail("vkAcquireNextImageKHR", R);
  if (VkResult R = vkWaitForFences(Device, 1, &AcquireFence, VK_TRUE,
                                  UINT64_MAX))
    fail("vkWaitForFences (acquire)", R);

  VkCommandPoolCreateInfo PoolInfo{};
  PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  PoolInfo.queueFamilyIndex = GraphicsFamily;
  VkCommandPool Pool;
  if (VkResult R = vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool))
    fail("vkCreateCommandPool", R);

  VkCommandBufferAllocateInfo CmdAlloc{};
  CmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  CmdAlloc.commandPool = Pool;
  CmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  CmdAlloc.commandBufferCount = 1;
  VkCommandBuffer Cmd;
  if (VkResult R = vkAllocateCommandBuffers(Device, &CmdAlloc, &Cmd))
    fail("vkAllocateCommandBuffers", R);

  VkCommandBufferBeginInfo BeginInfo{};
  BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (VkResult R = vkBeginCommandBuffer(Cmd, &BeginInfo))
    fail("vkBeginCommandBuffer", R);

  VkImageSubresourceRange Range{};
  Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  Range.levelCount = 1;
  Range.layerCount = 1;

  VkImageMemoryBarrier ToTransferDst{};
  ToTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  ToTransferDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  ToTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  ToTransferDst.image = Images[ImageIndex];
  ToTransferDst.subresourceRange = Range;
  vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                      nullptr, 1, &ToTransferDst);

  // Pure, saturated red -- see this file's own comment on why this
  // specific color makes a broken R/B swap unambiguous.
  VkClearColorValue ClearColor{};
  ClearColor.float32[0] = 1.0f;
  ClearColor.float32[1] = 0.0f;
  ClearColor.float32[2] = 0.0f;
  ClearColor.float32[3] = 1.0f;
  vkCmdClearColorImage(Cmd, Images[ImageIndex],
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearColor, 1,
                      &Range);

  VkImageMemoryBarrier ToPresent{};
  ToPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  ToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  ToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  ToPresent.image = Images[ImageIndex];
  ToPresent.subresourceRange = Range;
  vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                      nullptr, 1, &ToPresent);

  if (VkResult R = vkEndCommandBuffer(Cmd))
    fail("vkEndCommandBuffer", R);

  VkSubmitInfo Submit{};
  Submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &Cmd;
  if (VkResult R = vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE))
    fail("vkQueueSubmit", R);
  if (VkResult R = vkQueueWaitIdle(Queue))
    fail("vkQueueWaitIdle", R);

  VkPresentInfoKHR PresentInfo{};
  PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  PresentInfo.swapchainCount = 1;
  PresentInfo.pSwapchains = &Swapchain;
  PresentInfo.pImageIndices = &ImageIndex;
  if (VkResult R = vkQueuePresentKHR(Queue, &PresentInfo))
    fail("vkQueuePresentKHR", R);

  // ------------------------------------------------------------------
  // Read the real window's pixels back and verify the real presented
  // color -- the "real IR reduction"-style end-to-end check itself.
  // ------------------------------------------------------------------
  xcb_get_image_cookie_t Cookie =
      xcb_get_image(Connection, XCB_IMAGE_FORMAT_Z_PIXMAP, Window, 0, 0,
                   WindowSize, WindowSize, ~0u);
  xcb_generic_error_t *Error = nullptr;
  xcb_get_image_reply_t *Reply =
      xcb_get_image_reply(Connection, Cookie, &Error);
  if (!Reply) {
    std::free(Error);
    failMsg("xcb_get_image failed");
  }
  const uint8_t *Pixels =
      static_cast<const uint8_t *>(xcb_get_image_data(Reply));
  // Common Xvfb-default 24-in-32bpp TrueColor layout: B, G, R, X.
  uint8_t Blue = Pixels[0];
  uint8_t Green = Pixels[1];
  uint8_t Red = Pixels[2];
  std::printf("presented pixel: R=%02x G=%02x B=%02x\n", Red, Green, Blue);
  bool ColorMatches = Red == 0xFF && Green == 0x00 && Blue == 0x00;
  std::free(Reply);

  if (!ColorMatches)
    failMsg("presented window pixel was not pure red -- the R/B swap for "
           "VK_FORMAT_R8G8B8A8_UNORM (XcbSurface.cpp) is likely broken");

  vkDestroyFence(Device, AcquireFence, nullptr);
  vkDestroyCommandPool(Device, Pool, nullptr);
  vkDestroySwapchainKHR(Device, Swapchain, nullptr);
  vkDestroySurfaceKHR(Instance, Surface, nullptr);
  vkDestroyDevice(Device, nullptr);
  vkDestroyInstance(Instance, nullptr);
  xcb_destroy_window(Connection, Window);
  xcb_disconnect(Connection);

  std::printf("PASS\n");
  return 0;
}
