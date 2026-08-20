//===- PhysicalDeviceInfo.h - Truthful device capabilities -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Computes the one software `VkPhysicalDevice`'s immutable capabilities (see
// "Physical Device and Capabilities" in feme/docs/FeMeVulkanDesign.md):
// device identity, properties, limits, features, memory properties, and the
// single compute/transfer queue family. Nothing here is process-global
// mutable state -- `computePhysicalDeviceInfo` is a pure function of the
// host it runs on, called once per `VkInstance` (see "Goals": "multiple
// devices, queues, and pipeline compilations without mutable process-global
// FeMe state").
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_PHYSICALDEVICEINFO_H
#define FEME_LIB_VULKAN_PHYSICALDEVICEINFO_H

#include "llvm/ADT/ArrayRef.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace feme::vulkan {

/// Every value this ICD needs to answer `vkGetPhysicalDevice*` with, computed
/// once and held immutably by the `PhysicalDevice` object. See "Physical
/// Device and Capabilities" for the truthfulness rules each field follows.
struct PhysicalDeviceInfo {
  VkPhysicalDeviceProperties Properties{};
  VkPhysicalDeviceFeatures Features{};
  VkPhysicalDeviceMemoryProperties MemoryProperties{};
  /// The single universal queue family (see "Graphics queue family":
  /// adding graphics does *not* add a second family, since a software
  /// device with one worker pool has no independent graphics engine).
  VkQueueFamilyProperties UniversalQueueFamily{};

  /// The `VkPhysicalDeviceIDProperties::deviceUUID` value (a Vulkan 1.1
  /// core `Properties2` pNext struct); kept alongside
  /// `Properties.pipelineCacheUUID` rather than inside it since the two
  /// UUIDs are logically distinct even though both currently derive from
  /// the same inputs (see "Device identity").
  uint8_t DeviceUUID[VK_UUID_SIZE]{};

  /// Pinned device-wide wave size (see "Subgroup size"): FeMe's wave size is
  /// a compile-time constant chosen once per compilation from
  /// `{4, 8, 16, 32, 64, 128}`; Vulkan 1.1 requires a single
  /// `subgroupSize` for the whole device, so this is derived once here
  /// rather than per pipeline.
  uint32_t SubgroupSize = 0;
  /// The stages and operations this ICD truthfully supports for subgroup
  /// operations, reported through both `VkPhysicalDeviceSubgroupProperties`
  /// and the promoted `VkPhysicalDeviceVulkan11Properties`.
  VkShaderStageFlags SubgroupSupportedStages = 0;
  VkSubgroupFeatureFlags SubgroupSupportedOperations = 0;

  /// `VkDriverId` this build would report through
  /// `VkPhysicalDeviceDriverProperties`, were that struct queryable at the
  /// Vulkan 1.1 `apiVersion` this milestone advertises (see "Device
  /// identity" and the Deviation note added there for V0). Recorded now so
  /// the device UUID already accounts for it.
  VkDriverId DriverId = VK_DRIVER_ID_MAX_ENUM;
};

/// Computes the one physical device's capabilities from the host this
/// process is running on. Deterministic for a given host/build (see "Device
/// identity"'s UUID requirement), and safe to call more than once (e.g. once
/// per `VkInstance`).
PhysicalDeviceInfo computePhysicalDeviceInfo();

/// The device extensions this driver implements and advertises. An
/// extension appears here only once every command and feature it declares
/// is implemented -- never merely because Vulkan-Headers declares it (see
/// "Loader Integration"). V6 adds `VK_KHR_dynamic_rendering`, whose two
/// commands are core only in Vulkan 1.3 while this driver advertises 1.2;
/// `feme/utils/vk_gen_entrypoints.py`'s `SUPPORTED_EXTENSIONS` must list
/// exactly the same set.
llvm::ArrayRef<VkExtensionProperties> getSupportedDeviceExtensions();

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_PHYSICALDEVICEINFO_H
