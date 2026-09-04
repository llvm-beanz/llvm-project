//===- feme-vulkan-xcb-surface-lost-smoke.cpp - lost-surface smoke test -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H10g's own end-to-end verification (see Surface.cpp's
// `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`): a real xcb connection, but a
// window ID that is *destroyed* before this ICD ever queries its geometry
// -- deterministically reproducing a genuine `xcb_get_geometry` failure
// (a real `BadWindow` protocol error, not merely a null connection) without
// depending on any timing-sensitive connection flake. `Surface.cpp`'s own
// `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` must report this as
// `VK_ERROR_SURFACE_LOST_KHR`, not silently fall back to headless's
// `{UINT32_MAX, UINT32_MAX}` "undefined size" sentinel and a misleading
// `VK_SUCCESS` -- the exact conflation roadmap H10g's own investigation
// found: a real Xcb surface's geometry-query failure has an entirely
// different meaning (a lost surface) than a headless surface's legitimate
// "no fixed size" answer, and reporting the latter for the former silently
// deferred a real, diagnosable failure to a much later, unexplained
// `vkCreateSwapchainKHR` rejection instead.
//
//===----------------------------------------------------------------------===//

#include <vulkan/vulkan.h>

#define VK_USE_PLATFORM_XCB_KHR
#include <xcb/xcb.h>
// clang-format off
#include <vulkan/vulkan_xcb.h>
// clang-format on

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr uint32_t FeMeVendorID = 0x10000;

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
  int Screen = 0;
  xcb_connection_t *Connection = xcb_connect(nullptr, &Screen);
  if (xcb_connection_has_error(Connection))
    failMsg("xcb_connect");

  const xcb_setup_t *Setup = xcb_get_setup(Connection);
  xcb_screen_iterator_t ScreenIter = xcb_setup_roots_iterator(Setup);
  for (int I = 0; I < Screen; ++I)
    xcb_screen_next(&ScreenIter);
  xcb_screen_t *ScreenInfo = ScreenIter.data;

  // Create a real window, then destroy it immediately: its ID stays a
  // syntactically valid `xcb_window_t` (so `vkCreateXcbSurfaceKHR`'s own
  // `!pCreateInfo->window` rejection never triggers), but any later
  // request against it (this ICD's own `xcb_get_geometry`) now gets a
  // real `BadWindow` protocol error back -- a deterministic stand-in for
  // "this window is no longer valid" that needs no flaky timing at all.
  xcb_window_t Window = xcb_generate_id(Connection);
  xcb_create_window(Connection, XCB_COPY_FROM_PARENT, Window, ScreenInfo->root,
                    0, 0, 64, 64, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    ScreenInfo->root_visual, 0, nullptr);
  xcb_destroy_window(Connection, Window);
  xcb_flush(Connection);

  const char *InstanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                      VK_KHR_XCB_SURFACE_EXTENSION_NAME};
  VkApplicationInfo AppInfo{};
  AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  AppInfo.pApplicationName = "feme-vulkan-xcb-surface-lost-smoke";
  AppInfo.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo InstanceInfo{};
  InstanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  InstanceInfo.pApplicationInfo = &AppInfo;
  InstanceInfo.enabledExtensionCount = 2;
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

  VkXcbSurfaceCreateInfoKHR SurfaceInfo{};
  SurfaceInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
  SurfaceInfo.connection = Connection;
  SurfaceInfo.window = Window;
  VkSurfaceKHR Surface;
  if (VkResult R =
          vkCreateXcbSurfaceKHR(Instance, &SurfaceInfo, nullptr, &Surface))
    fail("vkCreateXcbSurfaceKHR", R);

  VkSurfaceCapabilitiesKHR Caps;
  VkResult Result =
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice, Surface, &Caps);
  std::printf("vkGetPhysicalDeviceSurfaceCapabilitiesKHR: VkResult = %d\n",
              static_cast<int>(Result));
  if (Result != VK_ERROR_SURFACE_LOST_KHR)
    failMsg("a destroyed xcb window's surface capabilities query did not "
            "report VK_ERROR_SURFACE_LOST_KHR");

  vkDestroySurfaceKHR(Instance, Surface, nullptr);
  vkDestroyInstance(Instance, nullptr);
  xcb_disconnect(Connection);

  std::printf("PASS\n");
  return 0;
}
