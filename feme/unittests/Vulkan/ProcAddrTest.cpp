//===- ProcAddrTest.cpp - Entrypoint lookup tests ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "ProcAddr.h"
#include "EntryPoints.h"

#include "gtest/gtest.h"

using namespace feme::vulkan;

namespace {

// A dummy non-null handle: `getInstanceProcAddr`/`getDeviceProcAddr` never
// dereference their handle argument (only `nullptr`-ness matters for
// dispatch-level filtering), so any non-null value is safe to pass here.
VkInstance dummyInstance() { return reinterpret_cast<VkInstance>(0x1); }

TEST(ProcAddr, NullInstanceOnlyResolvesGlobalCommands) {
  EXPECT_NE(getInstanceProcAddr(nullptr, "vkCreateInstance"), nullptr);
  EXPECT_NE(getInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"),
            nullptr);
  // `vkDestroyInstance` is instance-level; a null instance may not resolve
  // it (see "Loader Integration": "Global commands needed before an
  // instance exists").
  EXPECT_EQ(getInstanceProcAddr(nullptr, "vkDestroyInstance"), nullptr);
  EXPECT_EQ(getInstanceProcAddr(nullptr, "vkGetDeviceQueue"), nullptr);
}

TEST(ProcAddr, NonNullInstanceResolvesEveryImplementedLevel) {
  VkInstance Instance = dummyInstance();
  EXPECT_NE(getInstanceProcAddr(Instance, "vkCreateInstance"), nullptr);
  EXPECT_NE(getInstanceProcAddr(Instance, "vkDestroyInstance"), nullptr);
  EXPECT_NE(getInstanceProcAddr(Instance, "vkEnumeratePhysicalDevices"),
            nullptr);
  EXPECT_NE(getInstanceProcAddr(Instance, "vkCreateDevice"), nullptr);
  // Device-level commands are also resolvable through
  // `vkGetInstanceProcAddr` (the Vulkan specification explicitly allows
  // this), matching real loader/ICD behavior.
  EXPECT_NE(getInstanceProcAddr(Instance, "vkGetDeviceQueue"), nullptr);
}

TEST(ProcAddr, UnimplementedCommandNeverResolves) {
  // `vkCmdSetDepthBias` is a real core Vulkan 1.0 command this driver does
  // not implement (V6 rejects depth bias at pipeline creation rather than
  // silently ignoring it); the generated table still carries an entry for
  // it (mapped to null), matching "Loader Integration"'s requirement that
  // the table cover every known command name.
  EXPECT_EQ(getInstanceProcAddr(dummyInstance(), "vkCmdSetDepthBias"), nullptr);
  EXPECT_EQ(getDeviceProcAddr("vkCmdSetDepthBias"), nullptr);
}

TEST(ProcAddr, UnknownNameNeverResolves) {
  EXPECT_EQ(getInstanceProcAddr(dummyInstance(), "vkThisIsNotACommand"),
            nullptr);
}

TEST(ProcAddr, DeviceProcAddrOnlyResolvesDeviceLevelCommands) {
  EXPECT_NE(getDeviceProcAddr("vkGetDeviceQueue"), nullptr);
  EXPECT_NE(getDeviceProcAddr("vkDestroyDevice"), nullptr);
  // Instance- and global-level commands are not resolvable through
  // `vkGetDeviceProcAddr` (see the Vulkan specification's requirement that
  // it not be used to query those).
  EXPECT_EQ(getDeviceProcAddr("vkCreateDevice"), nullptr);
  EXPECT_EQ(getDeviceProcAddr("vkCreateInstance"), nullptr);
  EXPECT_EQ(getDeviceProcAddr("vkEnumeratePhysicalDevices"), nullptr);
}

TEST(ProcAddr, PhysicalDeviceProcAddrHasNoUnknownExtensionCommand) {
  // This milestone implements every physical-device command it advertises
  // directly, so there is nothing for the "unknown extension" path to
  // resolve (see ProcAddr.h).
  EXPECT_EQ(getPhysicalDeviceProcAddr(dummyInstance(), "vkCreateInstance"),
            nullptr);
  EXPECT_EQ(getPhysicalDeviceProcAddr(dummyInstance(),
                                      "vkGetPhysicalDeviceProperties"),
            nullptr);
}

} // namespace
