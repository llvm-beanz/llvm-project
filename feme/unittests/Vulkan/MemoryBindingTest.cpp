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

TEST(ResourceBindingsTest, BindRangeAccumulatesRatherThanReplacing) {
  uint8_t FirstBacking[64];
  uint8_t SecondBacking[64];
  DeviceMemory First(FirstBacking, sizeof(FirstBacking));
  DeviceMemory Second(SecondBacking, sizeof(SecondBacking));

  // Mirrors two `VkSparseMemoryBind`s from one (or more) `vkQueueBindSparse`
  // calls, binding two disjoint resource-space ranges of one sparse
  // resource to two different `VkDeviceMemory` allocations.
  ResourceBindings Bindings;
  Bindings.bindRange(/*ResourceOffset=*/0, &First, /*MemoryOffset=*/0,
                     /*Size=*/32);
  Bindings.bindRange(/*ResourceOffset=*/32, &Second, /*MemoryOffset=*/16,
                     /*Size=*/32);

  EXPECT_TRUE(Bindings.isBound());
  ASSERT_EQ(Bindings.ranges().size(), 2u);
  EXPECT_EQ(Bindings.ranges()[0].ResourceOffset, 0u);
  EXPECT_EQ(Bindings.ranges()[0].Memory, &First);
  EXPECT_EQ(Bindings.ranges()[0].MemoryOffset, 0u);
  EXPECT_EQ(Bindings.ranges()[0].Size, 32u);
  EXPECT_EQ(Bindings.ranges()[1].ResourceOffset, 32u);
  EXPECT_EQ(Bindings.ranges()[1].Memory, &Second);
  EXPECT_EQ(Bindings.ranges()[1].MemoryOffset, 16u);
  EXPECT_EQ(Bindings.ranges()[1].Size, 32u);
}

TEST(ResourceBindingsTest, BindRangeWithNullMemoryRecordsAnUnboundRange) {
  // A `VkSparseMemoryBind` with a null `memory` handle "unbinds" the
  // described resource-space range per the Vulkan spec; `bindRange`
  // records this as a null-memory range rather than silently dropping
  // it, so a future range-aware caller still sees the full addressable
  // extent (bound and unbound alike).
  ResourceBindings Bindings;
  Bindings.bindRange(/*ResourceOffset=*/0, /*Memory=*/nullptr,
                     /*MemoryOffset=*/0, /*Size=*/64);

  EXPECT_TRUE(Bindings.isBound());
  ASSERT_EQ(Bindings.ranges().size(), 1u);
  EXPECT_EQ(Bindings.ranges()[0].Memory, nullptr);
}

TEST(ResourceBindingsTest, BindRangeDoesNotDisturbDataForTheWholeRangeCase) {
  // `bindWhole` followed by `bindRange` calls is not a shape any
  // producer in this ICD creates today, but confirms `bindRange`
  // genuinely appends rather than interacting with `bindWhole`'s
  // existing range.
  uint8_t Backing[64];
  uint8_t ExtraBacking[64];
  DeviceMemory Memory(Backing, sizeof(Backing));
  DeviceMemory Extra(ExtraBacking, sizeof(ExtraBacking));

  ResourceBindings Bindings;
  Bindings.bindWhole(&Memory, 0, sizeof(Backing));
  Bindings.bindRange(sizeof(Backing), &Extra, 0, sizeof(ExtraBacking));

  EXPECT_EQ(Bindings.data(), static_cast<void *>(Backing));
  ASSERT_EQ(Bindings.ranges().size(), 2u);
}

} // namespace
