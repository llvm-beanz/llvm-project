//===- VulkanICD.cpp - Loader-facing exported entrypoints -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines this ICD's entire dynamic export surface (see "Process Coexistence
// and Symbol Visibility" in feme/docs/FeMeVulkanDesign.md): the loader-driver
// interface functions themselves. Everything else -- the object model, the
// generated entrypoint table, every `feme::vulkan::vk*` implementation --
// lives in `FeMeVulkanCore` and is never exported. This file is compiled
// only into the `feme_vulkan` shared object, not into `FeMeVulkanCore`, so
// linking that static library into a unit test never pulls these in.
//
//===----------------------------------------------------------------------===//

#include "ProcAddr.h"

#include <vulkan/vk_icd.h>

using namespace feme::vulkan;

// The four functions below are this ICD's entire dynamic export surface;
// `feme_vulkan`'s `CXX_VISIBILITY_PRESET hidden` makes every symbol hidden
// by default, including these, unless explicitly overridden -- a hidden
// (`STV_HIDDEN`) symbol can never be re-exposed by a version script, which
// only filters symbols that already have default visibility.
// `FEME_VULKAN_EXPORT` restores default visibility for exactly these four;
// `libfeme_vulkan.map` then narrows the *dynamic* symbol table to just them,
// so no other externally-visible symbol leaks even by accident.
#if defined(__GNUC__) || defined(__clang__)
#define FEME_VULKAN_EXPORT __attribute__((visibility("default")))
#else
#define FEME_VULKAN_EXPORT
#endif

extern "C" {

FEME_VULKAN_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pVersion) {
  // This ICD implements up to loader-driver interface version 7 (see
  // "Loader Integration": "Supporting loader-driver interface version 7 is
  // preferred"); negotiate down to whatever the loader asks for.
  if (*pVersion > CURRENT_LOADER_ICD_INTERFACE_VERSION)
    *pVersion = CURRENT_LOADER_ICD_INTERFACE_VERSION;
  return VK_SUCCESS;
}

FEME_VULKAN_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName) {
  return getInstanceProcAddr(instance, pName);
}

FEME_VULKAN_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName) {
  return getPhysicalDeviceProcAddr(instance, pName);
}

// The legacy, pre-interface-version-2 entrypoint name, kept exported (see
// "Loader Integration": "Exporting the traditional ICD symbols ... retains
// compatibility with older loaders at little cost").
FEME_VULKAN_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
  return getInstanceProcAddr(instance, pName);
}

} // extern "C"
