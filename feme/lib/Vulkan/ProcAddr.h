//===- ProcAddr.h - Vulkan entrypoint lookup --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the entrypoint-lookup helpers backing `vk_icdGetInstanceProcAddr`,
// `vk_icdGetPhysicalDeviceProcAddr`, and `vkGetDeviceProcAddr` (see "Loader
// Integration" in feme/docs/FeMeVulkanDesign.md), built from the generated
// `VulkanEntrypoints.inc` table (feme/utils/vk_gen_entrypoints.py).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_PROCADDR_H
#define FEME_LIB_VULKAN_PROCADDR_H

#include <vulkan/vulkan_core.h>

namespace feme::vulkan {

/// Resolves \p pName for `vk_icdGetInstanceProcAddr`/`vkGetInstanceProcAddr`.
/// With a null \p instance, only global commands resolve (a command needing
/// no instance to dispatch, e.g. `vkCreateInstance`); with a non-null one,
/// every implemented command name resolves, matching the Vulkan
/// specification's "may be used to query ... device-level functions too"
/// allowance for `vkGetInstanceProcAddr`.
PFN_vkVoidFunction getInstanceProcAddr(VkInstance instance, const char *pName);

/// Resolves \p pName for `vk_icdGetPhysicalDeviceProcAddr`: this milestone
/// implements every physical-device command it advertises directly, so
/// there is no *unknown* physical-device extension command to resolve here
/// (see loader-driver interface version 4's own description of this
/// function's purpose); it always returns null.
PFN_vkVoidFunction getPhysicalDeviceProcAddr(VkInstance instance,
                                             const char *pName);

/// Resolves \p pName for `vkGetDeviceProcAddr`: only device-dispatched
/// commands resolve, matching the Vulkan specification's requirement that
/// `vkGetDeviceProcAddr` not be used to query instance-level functions.
PFN_vkVoidFunction getDeviceProcAddr(const char *pName);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_PROCADDR_H
