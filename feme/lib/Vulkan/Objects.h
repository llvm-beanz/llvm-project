//===- Objects.h - Instance/PhysicalDevice/Device/Queue --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The V0 object model (see "Object Model" in
// feme/docs/FeMeVulkanDesign.md): `VkInstance`, one software
// `VkPhysicalDevice`, `VkDevice`, and `VkQueue`. Every other row of that
// table (memory, buffers, descriptors, pipelines, command buffers, ...) is
// out of scope until V1 and later, matching "No shader execution is
// required in this milestone".
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_OBJECTS_H
#define FEME_LIB_VULKAN_OBJECTS_H

#include "Icd.h"
#include "PhysicalDeviceInfo.h"

#include <memory>
#include <vector>

namespace feme::vulkan {

class Instance;

/// One software `VkPhysicalDevice`. Owned by its `Instance` and never
/// outlives it (Vulkan physical device handles are not destroyed directly).
class PhysicalDevice : public DispatchableBase {
public:
  explicit PhysicalDevice(Instance &Owner)
      : Owner(Owner), Info(computePhysicalDeviceInfo()) {}

  Instance &getInstance() const { return Owner; }
  const PhysicalDeviceInfo &getInfo() const { return Info; }

private:
  Instance &Owner;
  PhysicalDeviceInfo Info;
};

/// A `VkQueue`. V0 exposes exactly one, on the single compute/transfer queue
/// family; it carries no submission machinery yet (see V1's "Implement
/// queue submit, fences, queue/device idle").
class Queue : public DispatchableBase {
public:
  Queue(uint32_t FamilyIndex, uint32_t QueueIndex)
      : FamilyIndex(FamilyIndex), QueueIndex(QueueIndex) {}

  uint32_t getFamilyIndex() const { return FamilyIndex; }
  uint32_t getQueueIndex() const { return QueueIndex; }

private:
  uint32_t FamilyIndex;
  uint32_t QueueIndex;
};

/// A `VkDevice`. Owns its allocator, its allocation-callbacks-aware
/// `Allocator`, and its (single, in V0) queue.
class Device : public DispatchableBase {
public:
  Device(PhysicalDevice &Owner, const Allocator &Alloc)
      : Owner(Owner), Alloc(Alloc) {}

  PhysicalDevice &getPhysicalDevice() const { return Owner; }
  const Allocator &getAllocator() const { return Alloc; }

  /// Creates the device's queues eagerly at `vkCreateDevice` time, matching
  /// every other Vulkan implementation's convention that `vkGetDeviceQueue`
  /// never fails for a valid (family, index) pair requested at device
  /// creation.
  bool createQueues(uint32_t FamilyIndex, uint32_t QueueCount) {
    Queues.reserve(QueueCount);
    for (uint32_t I = 0; I < QueueCount; ++I) {
      auto Q = std::make_unique<Queue>(FamilyIndex, I);
      if (!Q)
        return false;
      Queues.push_back(std::move(Q));
    }
    return true;
  }

  Queue *getQueue(uint32_t FamilyIndex, uint32_t QueueIndex) const {
    for (const auto &Q : Queues)
      if (Q->getFamilyIndex() == FamilyIndex &&
          Q->getQueueIndex() == QueueIndex)
        return Q.get();
    return nullptr;
  }

private:
  PhysicalDevice &Owner;
  Allocator Alloc;
  std::vector<std::unique_ptr<Queue>> Queues;
};

/// A `VkInstance`. Owns the allocator and the single `PhysicalDevice` this
/// ICD ever reports (see "Summary": "exposes one software
/// `VkPhysicalDevice`").
class Instance : public DispatchableBase {
public:
  explicit Instance(const Allocator &Alloc)
      : Alloc(Alloc), Physical(std::make_unique<PhysicalDevice>(*this)) {}

  const Allocator &getAllocator() const { return Alloc; }
  PhysicalDevice &getPhysicalDevice() const { return *Physical; }

private:
  Allocator Alloc;
  std::unique_ptr<PhysicalDevice> Physical;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_OBJECTS_H
