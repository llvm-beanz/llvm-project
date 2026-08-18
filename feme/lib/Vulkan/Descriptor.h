//===- Descriptor.h - VkDescriptorSetLayout/Pool/Set object model -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The descriptor object model (see "Descriptor Model" in
// feme/docs/FeMeVulkanDesign.md): `VkDescriptorSetLayout`, `VkDescriptorPool`,
// and `VkDescriptorSet`. A descriptor set stores source Vulkan records --
// bound buffer, offset, and range per array element -- rather than a
// `feme::cpu::FemeDescriptor` directly, exactly as that section requires:
// buffers can be rebound, dynamic offsets are supplied at command
// recording/execution time, and the same set may be consumed by pipelines
// with different heap layouts. `Pipeline.cpp`'s dispatch preparation walks
// only the bindings a given pipeline's `feme::cpu::ResourceInfo::BoundRanges`
// actually reference and builds the physical `FemeDescriptor` heap from
// them there.
//
// `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`/`_DYNAMIC` and (V3)
// `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`/`_DYNAMIC` are accepted (see the
// Descriptor Model table's "Required first"/"Required after base buffers"
// rows); every other descriptor type is rejected at `VkDescriptorSetLayout`
// creation. A uniform buffer resolves to the same read-only
// `feme::cpu::FemeDescriptor` shape a non-writable `StructuredBuffer<T>`
// already does -- see `isReadOnlyDescriptorType` -- since this milestone's
// object-model support is deliberately ahead of the SPIR-V shader-compiler
// side: no SPIR-V `Uniform` storage-class lowering into a bound resource
// exists yet (`feme::cpu::SPIRVResourceLoweringPass`'s own header comment
// still only normalizes a `StorageBuffer`-storage-class handle), so a
// uniform-buffer descriptor cannot yet be consumed by a real compiled
// shader; only the Vulkan object model itself -- pool/set/dynamic-offset
// accounting and `FemeDescriptor` materialization -- is this milestone's
// own scope, exercised directly rather than through a compiled pipeline
// (see DescriptorTest.cpp/CommandBufferTest.cpp). A (descriptor set,
// binding) identity is exactly `feme::cpu::BoundResourceRange`'s
// `(Space, BaseRegister)` (see `feme::cpu::SPIRVResourceLoweringPass`'s file
// comment): no translation table is needed between the two.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_DESCRIPTOR_H
#define FEME_LIB_VULKAN_DESCRIPTOR_H

#include "llvm/ADT/ArrayRef.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace feme::vulkan {

class Buffer;

/// Whether \p Type is one of the four descriptor types this milestone
/// supports (see the file comment).
bool isSupportedDescriptorType(VkDescriptorType Type);

/// Whether \p Type consumes a dynamic offset supplied at
/// `vkCmdBindDescriptorSets` time.
bool isDynamicDescriptorType(VkDescriptorType Type);

/// Whether \p Type's materialized `FemeDescriptor` must never carry
/// `FEME_DESCRIPTOR_UAV` -- true for a uniform buffer, matching Vulkan's
/// own read-only restriction on that descriptor type.
bool isReadOnlyDescriptorType(VkDescriptorType Type);

/// One `VkDescriptorSetLayoutBinding`, retained for later validation
/// (pipeline-layout compatibility, `vkAllocateDescriptorSets`'s implicit
/// per-binding array size) and for computing how many dynamic offsets a
/// bound instance of this layout consumes.
struct DescriptorSetLayoutBinding {
  uint32_t Binding = 0;
  VkDescriptorType Type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
  uint32_t Count = 0;
};

/// A `VkDescriptorSetLayout`: an ordered (ascending by binding number) list
/// of bindings, matching the order Vulkan specifies for consuming
/// `vkCmdBindDescriptorSets`'s dynamic offsets ("Descriptor Model":
/// contiguous heap ranges per binding array).
class DescriptorSetLayout {
public:
  explicit DescriptorSetLayout(
      std::vector<DescriptorSetLayoutBinding> Bindings);

  llvm::ArrayRef<DescriptorSetLayoutBinding> bindings() const {
    return Bindings;
  }

  /// The binding numbered \p Binding, or null if this layout declares none.
  const DescriptorSetLayoutBinding *find(uint32_t Binding) const;

  /// The number of dynamic offsets a `vkCmdBindDescriptorSets` call binding
  /// this layout must supply: one per array element of every dynamic-type
  /// binding, in ascending binding-number order.
  uint32_t dynamicOffsetCount() const;

private:
  std::vector<DescriptorSetLayoutBinding> Bindings;
};

/// One descriptor array element's source Vulkan record: the buffer it
/// refers to, plus the offset/range `vkUpdateDescriptorSets` wrote (see
/// "Memory and Buffers": "Data = memory allocation base + buffer binding
/// offset + descriptor offset"). `Buf == nullptr` means never written --
/// treated as an out-of-bounds access, matching a zero-filled
/// `FemeDescriptor`.
struct DescriptorBufferBinding {
  Buffer *Buf = nullptr;
  VkDeviceSize Offset = 0;
  /// The declared range, or `VK_WHOLE_SIZE`; resolved against the bound
  /// buffer's size when the physical heap is materialized.
  VkDeviceSize Range = 0;
};

/// A `VkDescriptorSet`: per-binding arrays of `DescriptorBufferBinding`,
/// sized from its `DescriptorSetLayout` at allocation time. Not
/// dispatchable.
class DescriptorSet {
public:
  explicit DescriptorSet(const DescriptorSetLayout &Layout);

  const DescriptorSetLayout &getLayout() const { return *Layout; }

  /// Writes array element \p ArrayElement of binding \p Binding, per
  /// `vkUpdateDescriptorSets`. A binding or array element this set's layout
  /// does not declare is silently ignored, matching invalid usage the
  /// application is responsible for avoiding (Vulkan's host synchronization
  /// and valid-usage rules, not a runtime-checked error path).
  void write(uint32_t Binding, uint32_t ArrayElement, Buffer *Buf,
             VkDeviceSize Offset, VkDeviceSize Range);

  /// The full declared array for \p Binding, or empty if this set's layout
  /// declares no such binding.
  llvm::ArrayRef<DescriptorBufferBinding> bindingArray(uint32_t Binding) const;

private:
  const DescriptorSetLayout *Layout;
  std::map<uint32_t, std::vector<DescriptorBufferBinding>> Bindings;
};

/// A `VkDescriptorPool`: owns every `DescriptorSet` allocated from it and
/// accounts for `maxSets`, per "Object Model". Per-descriptor-type pool
/// size accounting is intentionally not modeled -- only `maxSets` is
/// enforced -- since this milestone's four descriptor types share one
/// simple accounting rule and a real application's `VkDescriptorPoolSize`
/// list is otherwise unchecked upstream validation's job, not this ICD's.
class DescriptorPool {
public:
  explicit DescriptorPool(uint32_t MaxSets)
      : MaxSets(MaxSets), RemainingSets(MaxSets) {}

  /// Allocates one set from \p Layout, or null if the pool has no
  /// remaining set slots (`VK_ERROR_OUT_OF_POOL_MEMORY`).
  DescriptorSet *allocate(const DescriptorSetLayout &Layout);

  /// `vkFreeDescriptorSets`: returns \p Set's slot to the pool.
  void free(DescriptorSet *Set);

  /// `vkResetDescriptorPool`: destroys every set allocated from this pool
  /// and restores its full `maxSets` capacity.
  void reset();

private:
  uint32_t MaxSets;
  uint32_t RemainingSets;
  std::vector<std::unique_ptr<DescriptorSet>> Sets;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_DESCRIPTOR_H
