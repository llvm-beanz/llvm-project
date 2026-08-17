//===- ProcAddr.cpp - Vulkan entrypoint lookup ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Builds the generated entrypoint table (feme/utils/vk_gen_entrypoints.py,
// "VulkanEntrypoints.inc") into the lookup functions declared in
// `ProcAddr.h`, used by `vk_icdGetInstanceProcAddr`/
// `vk_icdGetPhysicalDeviceProcAddr`/`vkGetDeviceProcAddr` (see "Loader
// Integration"). The loader-facing symbols themselves -- this ICD's entire
// dynamic export surface -- are defined in `VulkanICD.cpp`, compiled only
// into the `feme_vulkan` shared object rather than the `FeMeVulkanCore`
// static library this file belongs to, so unit tests linking that static
// library directly never see them.
//
//===----------------------------------------------------------------------===//

#include "ProcAddr.h"
#include "EntryPoints.h"

#include <cstring>

using namespace feme::vulkan;

namespace {

enum class DispatchLevel { GLOBAL, INSTANCE, DEVICE };

struct Entry {
  const char *Name;
  DispatchLevel Level;
  PFN_vkVoidFunction Addr;
};

#define FEME_VK_COMMAND(name, level)                                           \
  Entry{#name, DispatchLevel::level, nullptr},
#define FEME_VK_COMMAND_IMPL(name, level)                                      \
  Entry{#name, DispatchLevel::level,                                           \
        reinterpret_cast<PFN_vkVoidFunction>(&feme::vulkan::name)},

// Not `constexpr`: `reinterpret_cast`ing a function pointer isn't a
// constant expression, but this is still a plain read-only array of
// trivially-relocatable data -- initialized by static relocations the
// dynamic linker applies at load time, not by any generated constructor
// function (see feme/.instructions.md's "Do not use static constructors").
const Entry kEntries[] = {
#include "VulkanEntrypoints.inc"
};

#undef FEME_VK_COMMAND
#undef FEME_VK_COMMAND_IMPL

const Entry *findEntry(const char *pName) {
  for (const Entry &E : kEntries)
    if (std::strcmp(E.Name, pName) == 0)
      return &E;
  return nullptr;
}

} // namespace

PFN_vkVoidFunction feme::vulkan::getInstanceProcAddr(VkInstance instance,
                                                     const char *pName) {
  const Entry *E = findEntry(pName);
  if (!E || !E->Addr)
    return nullptr;
  // A null instance may only resolve a command that needs no instance to
  // dispatch (see "Loader Integration": "Global commands needed before an
  // instance exists").
  if (!instance && E->Level != DispatchLevel::GLOBAL)
    return nullptr;
  return E->Addr;
}

PFN_vkVoidFunction feme::vulkan::getPhysicalDeviceProcAddr(VkInstance,
                                                           const char *) {
  // This milestone advertises no unknown physical-device extension command
  // (see ProcAddr.h's doc comment).
  return nullptr;
}

PFN_vkVoidFunction feme::vulkan::getDeviceProcAddr(const char *pName) {
  const Entry *E = findEntry(pName);
  if (!E || !E->Addr || E->Level != DispatchLevel::DEVICE)
    return nullptr;
  return E->Addr;
}
