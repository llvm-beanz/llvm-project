//===- MemoryBindingTest.cpp - ResourceBindings tests ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (Roadmap H46) Standalone tests for `ResourceBindings`, deliberately
// independent of any `VkBuffer`/`VkImage`/`VkDevice` machinery: this class
// only ever needs a `DeviceMemory*` to operate on, so these tests
// construct `DeviceMemory` objects directly over plain stack buffers
// rather than going through `vkAllocateMemory`.
//
//===----------------------------------------------------------------------===//

#include "MemoryBinding.h"

#include "gtest/gtest.h"

#include <cstdint>

using namespace feme::vulkan;

namespace {

TEST(ResourceBindingsTest, DefaultConstructedIsUnbound) {
  ResourceBindings Bindings;
  EXPECT_FALSE(Bindings.isBound());
  EXPECT_EQ(Bindings.data(), nullptr);
  EXPECT_TRUE(Bindings.ranges().empty());
}

TEST(ResourceBindingsTest, BindWholeMakesDataReturnMemoryBase) {
  uint8_t Backing[64];
  DeviceMemory Memory(Backing, sizeof(Backing));

  ResourceBindings Bindings;
  Bindings.bindWhole(&Memory, /*MemoryOffset=*/0, /*Size=*/sizeof(Backing));

  EXPECT_TRUE(Bindings.isBound());
  EXPECT_EQ(Bindings.data(), static_cast<void *>(Backing));
  ASSERT_EQ(Bindings.ranges().size(), 1u);
  EXPECT_EQ(Bindings.ranges()[0].ResourceOffset, 0u);
  EXPECT_EQ(Bindings.ranges()[0].Memory, &Memory);
  EXPECT_EQ(Bindings.ranges()[0].MemoryOffset, 0u);
  EXPECT_EQ(Bindings.ranges()[0].Size, sizeof(Backing));
}

TEST(ResourceBindingsTest, BindWholeHonorsNonzeroMemoryOffset) {
  uint8_t Backing[64];
  DeviceMemory Memory(Backing, sizeof(Backing));

  // Mirrors a buffer bound partway into a larger `VkDeviceMemory`
  // allocation, e.g. a suballocated buffer sharing one allocation with
  // others (the ordinary, non-sparse `vkBindBufferMemory` binding shape
  // this models, same as `Buffer`/`Image`'s own pre-H46 `bind()`).
  ResourceBindings Bindings;
  Bindings.bindWhole(&Memory, /*MemoryOffset=*/16, /*Size=*/32);

  EXPECT_TRUE(Bindings.isBound());
  EXPECT_EQ(Bindings.data(), static_cast<void *>(Backing + 16));
}

TEST(ResourceBindingsTest, BindWholeReplacesAnyExistingBinding) {
  uint8_t FirstBacking[64];
  uint8_t SecondBacking[64];
  DeviceMemory First(FirstBacking, sizeof(FirstBacking));
  DeviceMemory Second(SecondBacking, sizeof(SecondBacking));

  ResourceBindings Bindings;
  Bindings.bindWhole(&First, 0, sizeof(FirstBacking));
  ASSERT_EQ(Bindings.data(), static_cast<void *>(FirstBacking));

  // Not valid usage for a real application (Vulkan forbids rebinding a
  // bound resource), but `ResourceBindings` itself does not re-validate
  // this -- matching `Buffer`/`Image`'s own pre-H46 `bind()` contract --
  // so confirm it cleanly replaces rather than appending.
  Bindings.bindWhole(&Second, 0, sizeof(SecondBacking));
  EXPECT_EQ(Bindings.data(), static_cast<void *>(SecondBacking));
  EXPECT_EQ(Bindings.ranges().size(), 1u);
}

} // namespace
