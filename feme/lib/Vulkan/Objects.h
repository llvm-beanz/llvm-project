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
#include "PipelineCache.h"
#include "Sync.h"

#include <atomic>
#include <memory>
#include <vector>

namespace feme::vulkan {

class Instance;
class Device;

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

/// A `VkQueue`. Owns its own dedicated `QueueExecutor` worker thread
/// (roadmap L228(h)/(i)): every `vkQueueSubmit`/`vkQueueSubmit2` task
/// enqueued to this queue runs there, in submission order, while the
/// calling host thread returns immediately (see `Sync.h`'s file comment).
class Queue : public DispatchableBase {
public:
  Queue(Device &Owner, uint32_t FamilyIndex, uint32_t QueueIndex)
      : Owner(Owner), FamilyIndex(FamilyIndex), QueueIndex(QueueIndex) {}

  Device &getDevice() const { return Owner; }
  uint32_t getFamilyIndex() const { return FamilyIndex; }
  uint32_t getQueueIndex() const { return QueueIndex; }
  QueueExecutor &getExecutor() { return Executor; }

private:
  Device &Owner;
  uint32_t FamilyIndex;
  uint32_t QueueIndex;
  QueueExecutor Executor;
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
      auto Q = std::make_unique<Queue>(*this, FamilyIndex, I);
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

  /// (roadmap L89c) The device's implicit pipeline cache: consulted by
  /// `vkCreate{Compute,Graphics}Pipelines` on *every* creation, including
  /// the common one where the app supplies no `VkPipelineCache` at all.
  ///
  /// A `VkPipelineCache` is opt-in in Vulkan, and many real applications
  /// (and much of the CTS) never create one -- which for a GPU driver only
  /// costs a comparatively cheap native shader compile, but for this
  /// CPU/JIT-based ICD costs seconds of LLVM codegen per repeat. The spec
  /// explicitly anticipates implementations keeping caches of their own
  /// beyond the app's: an implicit hit is indistinguishable from a fast
  /// compile, so it changes no observable behavior except that it must not
  /// report `VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_
  /// BIT`, which specifically means the *application's* cache.
  PipelineCache &getImplicitPipelineCache() { return ImplicitCache; }

  /// (Roadmap L228(h)/(i)) Blocks the calling thread until every queue's
  /// own `QueueExecutor` reports idle (`vkDeviceWaitIdle`). Returns false
  /// only if some queue's own `waitIdle` timed out (a FeMe bug -- see
  /// `Sync.h`'s file comment).
  bool waitIdle() {
    bool AllIdle = true;
    for (auto &Q : Queues)
      AllIdle &= Q->getExecutor().waitIdle();
    return AllIdle;
  }

  /// (Roadmap L228(h)/(i)) Latches device loss: called by a
  /// `QueueExecutor` task that fails (an unmet wait past its own safety
  /// net, or a command-buffer execution failure) since it cannot report
  /// that failure back through the `vkQueueSubmit` call that enqueued it,
  /// which has already returned by the time the task runs. See "Queues,
  /// Scheduling, and Synchronization"'s "Device loss is latched once".
  void markLost() { Lost.store(true, std::memory_order_relaxed); }
  /// Whether `markLost` has ever been called for this device. Checked by
  /// every synchronization entry point that can legally return
  /// `VK_ERROR_DEVICE_LOST` (`vkQueueSubmit`/`vkQueueSubmit2`/
  /// `vkQueueWaitIdle`/`vkDeviceWaitIdle`/`vkWaitForFences`).
  bool isLost() const { return Lost.load(std::memory_order_relaxed); }

private:
  /// How many artifacts the implicit cache retains per table before
  /// evicting; see `PipelineCache`'s constructor. Sized far above any
  /// plausible per-frame working set, since the point is only to stop
  /// unbounded growth over a long-lived device.
  static constexpr size_t ImplicitPipelineCacheMaxEntries = 256;

  PhysicalDevice &Owner;
  Allocator Alloc;
  std::atomic<bool> Lost{false};
  PipelineCache ImplicitCache{/*InitialKeys=*/{},
                              /*ExternallySynchronized=*/false,
                              ImplicitPipelineCacheMaxEntries};
  // Declared last: destroyed first, so every `QueueExecutor`'s worker
  // thread (which may still be running a task that touches `Lost`/
  // `ImplicitCache` above) is always stopped and joined before those
  // members are torn down, even if an application destroys this device
  // without first draining it (real usage must not, but this ordering
  // costs nothing and avoids a use-after-destroy race if it ever
  // happens).
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
