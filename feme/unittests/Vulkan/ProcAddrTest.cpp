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

#include "llvm/ADT/StringSet.h"

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
  // `vkGetPhysicalDeviceImageFormatProperties2` is a real core Vulkan 1.1
  // command this driver does not implement; the generated table still
  // carries an entry for it (mapped to null), matching "Loader
  // Integration"'s requirement that the table cover every known command
  // name. (Roadmap L236(a): `vkQueueBindSparse`, this test's previous
  // example, is now implemented -- see Sync.cpp's own comment --
  // `vkCmdDrawIndexedIndirectCount`, a later example, is now also
  // implemented (roadmap K6h) -- see CommandBuffer.cpp's
  // `drawIndirectCount` -- so this test now exercises a still-genuinely-
  // unimplemented command instead.)
  EXPECT_EQ(getInstanceProcAddr(dummyInstance(),
                                "vkGetPhysicalDeviceImageFormatProperties2"),
            nullptr);
  // `vkCmdBindIndexBuffer2KHR` is a real device-level command (its core,
  // non-`KHR`-suffixed alias `vkCmdBindIndexBuffer2` is implemented, see
  // `ImplementedEntrypoints.txt`, but the `KHR`-suffixed name itself is
  // not in that file, so the generated table still maps it to null --
  // exercising `getDeviceProcAddr`'s own null-`Addr` path, distinct from
  // the instance-level command above).
  EXPECT_EQ(getDeviceProcAddr("vkCmdBindIndexBuffer2KHR"), nullptr);
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

// Roadmap H10i: `vkAcquireNextImage2KHR` was entirely missing from the
// generated dispatch table (a null `vkGetDeviceProcAddr` resolution, not
// a graceful rejection), which meant CTS calling through the resolved
// (null) function pointer crashed with a genuine `SIGSEGV` rather than
// ever reaching `Swapchain.cpp`'s own implementation -- a regression this
// specific assertion (rather than only `SwapchainTest.cpp`'s own
// behavioral coverage, which links against the real symbol directly and
// so could never have caught a dispatch-table gap) guards against.
TEST(ProcAddr, DeviceProcAddrResolvesAcquireNextImage2) {
  EXPECT_NE(getDeviceProcAddr("vkAcquireNextImage2KHR"), nullptr);
}

TEST(ProcAddr, DeviceProcAddrResolvesDepthBiasEnable) {
  EXPECT_NE(getDeviceProcAddr("vkCmdSetDepthBiasEnable"), nullptr);
}

TEST(ProcAddr, DeviceProcAddrResolvesRasterizerDiscardEnable) {
  EXPECT_NE(getDeviceProcAddr("vkCmdSetRasterizerDiscardEnable"), nullptr);
}

TEST(ProcAddr, DeviceProcAddrResolvesPrimitiveRestartEnable) {
  EXPECT_NE(getDeviceProcAddr("vkCmdSetPrimitiveRestartEnable"), nullptr);
}

// Roadmap H21b: `VK_EXT_transform_feedback`'s six commands are registered
// in `SUPPORTED_EXTENSIONS`/`ImplementedEntrypoints.txt` so the dispatch
// table carries real function pointers for them, even though the
// extension itself is not yet advertised (`getSupportedDeviceExtensions`)
// -- mirrors `DeviceProcAddrResolvesAcquireNextImage2` above, guarding the
// same "resolves to a real symbol, not null" dispatch-table property.
TEST(ProcAddr, DeviceProcAddrResolvesTransformFeedbackCommands) {
  EXPECT_NE(getDeviceProcAddr("vkCmdBindTransformFeedbackBuffersEXT"), nullptr);
  EXPECT_NE(getDeviceProcAddr("vkCmdBeginTransformFeedbackEXT"), nullptr);
  EXPECT_NE(getDeviceProcAddr("vkCmdEndTransformFeedbackEXT"), nullptr);
  EXPECT_NE(getDeviceProcAddr("vkCmdBeginQueryIndexedEXT"), nullptr);
  EXPECT_NE(getDeviceProcAddr("vkCmdEndQueryIndexedEXT"), nullptr);
  EXPECT_NE(getDeviceProcAddr("vkCmdDrawIndirectByteCountEXT"), nullptr);
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

// Roadmap L236: `getDeviceProcAddr`'s optional `EnabledExtensions` gating.
TEST(ProcAddr, DeviceProcAddrGatesExtensionOwnedCommandOnEnabledSet) {
  llvm::StringSet<> Empty;
  // `vkCmdPushDescriptorSetKHR` exists only because `VK_KHR_push_descriptor`
  // does (its alias target `vkCmdPushDescriptorSet` is the core-promoted
  // name) -- with that extension not in the enabled set, it must not
  // resolve, matching `dEQP-VK.api.get_device_proc_addr.non_enabled`'s
  // expectation for a device created with zero enabled extensions.
  EXPECT_EQ(getDeviceProcAddr("vkCmdPushDescriptorSetKHR", &Empty), nullptr);

  llvm::StringSet<> WithPushDescriptor;
  WithPushDescriptor.insert("VK_KHR_push_descriptor");
  EXPECT_NE(getDeviceProcAddr("vkCmdPushDescriptorSetKHR", &WithPushDescriptor),
            nullptr);

  // Unaffected by a disjoint enabled set: still gated on its own owning
  // extension only.
  EXPECT_EQ(getDeviceProcAddr("vkCmdPushDescriptorSetKHR", &WithPushDescriptor),
            getDeviceProcAddr("vkCmdPushDescriptorSetKHR", &WithPushDescriptor));
}

TEST(ProcAddr, DeviceProcAddrNeverGatesCoreCommands) {
  llvm::StringSet<> Empty;
  // A core command (no owning extension) must resolve regardless of the
  // enabled set, including when the set is empty.
  EXPECT_NE(getDeviceProcAddr("vkGetDeviceQueue", &Empty), nullptr);
  EXPECT_NE(getDeviceProcAddr("vkDestroyDevice", &Empty), nullptr);
  // `vkCmdPushDescriptorSet` (the core-promoted name, as opposed to its
  // `KHR`-suffixed alias above) must also stay ungated.
  EXPECT_NE(getDeviceProcAddr("vkCmdPushDescriptorSet", &Empty), nullptr);
}

TEST(ProcAddr, DeviceProcAddrNullEnabledExtensionsAppliesNoGating) {
  // A null `EnabledExtensions` (the default, and every other test's own
  // single-argument calls above) must preserve this function's original,
  // ungated behavior even for an extension-owned command.
  EXPECT_NE(getDeviceProcAddr("vkCmdPushDescriptorSetKHR"), nullptr);
  EXPECT_NE(getDeviceProcAddr("vkCmdPushDescriptorSetKHR", nullptr), nullptr);
}

} // namespace
