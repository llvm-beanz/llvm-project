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

#include "llvm/ADT/StringSet.h"

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
///
/// (Roadmap L236) \p EnabledExtensions, when non-null, additionally gates a
/// command whose only reason for existing is a single extension (per
/// `vk_gen_entrypoints.py`'s own `owning_extension` -- e.g.
/// `vkCmdPushDescriptorSetKHR`, which exists only because
/// `VK_KHR_push_descriptor` does) on that extension's name being present in
/// the set: a `VkDevice` that never enabled the extension must not resolve
/// its command, matching `dEQP-VK.api.get_device_proc_addr.non_enabled`'s
/// expectation. A core command (no owning extension) always resolves
/// regardless of \p EnabledExtensions. A null \p EnabledExtensions applies
/// no such gating at all, preserving this function's original, ungated
/// behavior for callers (chiefly unit tests) that only care whether the
/// dispatch table carries a real symbol for a name at all, independent of
/// any particular device's enabled-extension set.
PFN_vkVoidFunction getDeviceProcAddr(
    const char *pName, const llvm::StringSet<> *EnabledExtensions = nullptr);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_PROCADDR_H
