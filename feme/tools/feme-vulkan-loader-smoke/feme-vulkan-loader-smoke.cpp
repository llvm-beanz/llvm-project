//===- feme-vulkan-loader-smoke.cpp - Loader smoke-test client -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A tiny Vulkan client, linked against the real Khronos Vulkan loader (not
// `libfeme_vulkan` directly), for the "V0: Loader-visible skeleton"
// milestone's lit tests (see "Testing Strategy" in
// feme/docs/FeMeVulkanDesign.md: "Lit tests invoking tiny Vulkan clients
// with `VK_DRIVER_FILES` set to the build-tree manifest"). Exercises the
// first acceptance-test scenario up through V0's scope: create instance,
// enumerate physical devices, find the FeMe device (identified by its
// reserved vendor ID -- see "Device identity"), create a compute-only
// device and queue, wait idle, and destroy every object.
//
// This intentionally finds the FeMe device *among whatever the loader
// reports*, rather than assuming it is the only one, so the same binary
// serves both the single-ICD loader-smoke test and the two-ICD coexistence
// test (feme/test/Vulkan/two-icd-coexistence.test).
//
//===----------------------------------------------------------------------===//

#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

// The Khronos "not yet assigned an official vendor ID" reserved value FeMe
// reports (see "Device identity" in feme/docs/FeMeVulkanDesign.md).
constexpr uint32_t FeMeVendorID = 0x10000;

[[noreturn]] void fail(const char *Step, VkResult Result) {
  std::fprintf(stderr, "FAIL: %s (VkResult = %d)\n", Step,
               static_cast<int>(Result));
  std::exit(1);
}

} // namespace

int main() {
  VkApplicationInfo AppInfo{};
  AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  AppInfo.pApplicationName = "feme-vulkan-loader-smoke";
  AppInfo.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo InstanceInfo{};
  InstanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  InstanceInfo.pApplicationInfo = &AppInfo;

  VkInstance Instance;
  if (VkResult R = vkCreateInstance(&InstanceInfo, nullptr, &Instance))
    fail("vkCreateInstance", R);

  uint32_t DeviceCount = 0;
  vkEnumeratePhysicalDevices(Instance, &DeviceCount, nullptr);
  if (DeviceCount == 0) {
    std::fprintf(stderr, "FAIL: vkEnumeratePhysicalDevices found no "
                         "devices at all\n");
    return 1;
  }
  std::vector<VkPhysicalDevice> Devices(DeviceCount);
  vkEnumeratePhysicalDevices(Instance, &DeviceCount, Devices.data());

  VkPhysicalDevice FeMeDevice = VK_NULL_HANDLE;
  for (VkPhysicalDevice Device : Devices) {
    VkPhysicalDeviceProperties Props;
    vkGetPhysicalDeviceProperties(Device, &Props);
    std::printf("found device: %s (vendorID=0x%x, apiVersion=%u.%u.%u)\n",
                Props.deviceName, Props.vendorID,
                VK_API_VERSION_MAJOR(Props.apiVersion),
                VK_API_VERSION_MINOR(Props.apiVersion),
                VK_API_VERSION_PATCH(Props.apiVersion));
    if (Props.vendorID == FeMeVendorID)
      FeMeDevice = Device;
  }
  if (!FeMeDevice) {
    std::fprintf(stderr, "FAIL: no device reported FeMe's vendor ID\n");
    return 1;
  }

  uint32_t FamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(FeMeDevice, &FamilyCount, nullptr);
  std::vector<VkQueueFamilyProperties> Families(FamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(FeMeDevice, &FamilyCount,
                                           Families.data());
  uint32_t ComputeFamily = FamilyCount;
  for (uint32_t I = 0; I < FamilyCount; ++I)
    if (Families[I].queueFlags & VK_QUEUE_COMPUTE_BIT) {
      ComputeFamily = I;
      break;
    }
  if (ComputeFamily == FamilyCount) {
    std::fprintf(stderr, "FAIL: FeMe device reported no compute queue "
                         "family\n");
    return 1;
  }
  std::printf("compute queue family: %u (flags=0x%x)\n", ComputeFamily,
              Families[ComputeFamily].queueFlags);

  float Priority = 1.0f;
  VkDeviceQueueCreateInfo QueueInfo{};
  QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  QueueInfo.queueFamilyIndex = ComputeFamily;
  QueueInfo.queueCount = 1;
  QueueInfo.pQueuePriorities = &Priority;

  VkDeviceCreateInfo DeviceInfo{};
  DeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  DeviceInfo.queueCreateInfoCount = 1;
  DeviceInfo.pQueueCreateInfos = &QueueInfo;

  VkDevice Device;
  if (VkResult R = vkCreateDevice(FeMeDevice, &DeviceInfo, nullptr, &Device))
    fail("vkCreateDevice", R);

  VkQueue Queue;
  vkGetDeviceQueue(Device, ComputeFamily, 0, &Queue);
  if (!Queue) {
    std::fprintf(stderr, "FAIL: vkGetDeviceQueue returned VK_NULL_HANDLE\n");
    return 1;
  }

  if (VkResult R = vkDeviceWaitIdle(Device))
    fail("vkDeviceWaitIdle", R);

  vkDestroyDevice(Device, nullptr);
  vkDestroyInstance(Instance, nullptr);

  std::printf("PASS\n");
  return 0;
}
