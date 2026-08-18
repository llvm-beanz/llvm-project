//===- Descriptor.cpp - VkDescriptorSetLayout/Pool/Set implementations --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Descriptor.h"
#include "Buffer.h"
#include "Icd.h"
#include "Objects.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>

using namespace feme::vulkan;
using namespace llvm;

bool feme::vulkan::isSupportedDescriptorType(VkDescriptorType Type) {
  return Type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
         Type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC ||
         Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
         Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
         isTexelBufferDescriptorType(Type);
}

bool feme::vulkan::isTexelBufferDescriptorType(VkDescriptorType Type) {
  return Type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
         Type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
}

bool feme::vulkan::isDynamicDescriptorType(VkDescriptorType Type) {
  return Type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC ||
         Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
}

bool feme::vulkan::isReadOnlyDescriptorType(VkDescriptorType Type) {
  return Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
         Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
         Type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
}

DescriptorSetLayout::DescriptorSetLayout(
    std::vector<DescriptorSetLayoutBinding> Bindings)
    : Bindings(std::move(Bindings)) {
  llvm::sort(this->Bindings, [](const DescriptorSetLayoutBinding &A,
                                const DescriptorSetLayoutBinding &B) {
    return A.Binding < B.Binding;
  });
}

const DescriptorSetLayoutBinding *
DescriptorSetLayout::find(uint32_t Binding) const {
  for (const DescriptorSetLayoutBinding &B : Bindings)
    if (B.Binding == Binding)
      return &B;
  return nullptr;
}

uint32_t DescriptorSetLayout::dynamicOffsetCount() const {
  uint32_t Count = 0;
  for (const DescriptorSetLayoutBinding &B : Bindings)
    if (isDynamicDescriptorType(B.Type))
      Count += B.Count;
  return Count;
}

DescriptorSet::DescriptorSet(const DescriptorSetLayout &Layout)
    : Layout(&Layout) {
  for (const DescriptorSetLayoutBinding &B : Layout.bindings())
    Bindings[B.Binding].resize(B.Count);
}

void DescriptorSet::write(uint32_t Binding, uint32_t ArrayElement, Buffer *Buf,
                          VkDeviceSize Offset, VkDeviceSize Range) {
  auto It = Bindings.find(Binding);
  if (It == Bindings.end() || ArrayElement >= It->second.size())
    return;
  It->second[ArrayElement] = DescriptorBufferBinding{Buf, Offset, Range,
                                                     /*View=*/nullptr};
}

void DescriptorSet::write(uint32_t Binding, uint32_t ArrayElement,
                          BufferView *View) {
  auto It = Bindings.find(Binding);
  if (It == Bindings.end() || ArrayElement >= It->second.size())
    return;
  It->second[ArrayElement] =
      DescriptorBufferBinding{/*Buf=*/nullptr, /*Offset=*/0, /*Range=*/0, View};
}

llvm::ArrayRef<DescriptorBufferBinding>
DescriptorSet::bindingArray(uint32_t Binding) const {
  auto It = Bindings.find(Binding);
  if (It == Bindings.end())
    return {};
  return It->second;
}

DescriptorSet *DescriptorPool::allocate(const DescriptorSetLayout &Layout) {
  if (RemainingSets == 0)
    return nullptr;
  auto Set = std::make_unique<DescriptorSet>(Layout);
  DescriptorSet *Result = Set.get();
  Sets.push_back(std::move(Set));
  --RemainingSets;
  return Result;
}

void DescriptorPool::free(DescriptorSet *Set) {
  size_t SizeBefore = Sets.size();
  llvm::erase_if(Sets, [&](const std::unique_ptr<DescriptorSet> &Owned) {
    return Owned.get() == Set;
  });
  if (Sets.size() != SizeBefore)
    ++RemainingSets;
}

void DescriptorPool::reset() {
  Sets.clear();
  RemainingSets = MaxSets;
}

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDescriptorSetLayout *pSetLayout) {
  std::vector<DescriptorSetLayoutBinding> Bindings;
  Bindings.reserve(pCreateInfo->bindingCount);
  for (uint32_t I = 0; I != pCreateInfo->bindingCount; ++I) {
    const VkDescriptorSetLayoutBinding &Binding = pCreateInfo->pBindings[I];
    if (!isSupportedDescriptorType(Binding.descriptorType))
      return VK_ERROR_INITIALIZATION_FAILED;
    Bindings.push_back(DescriptorSetLayoutBinding{
        Binding.binding, Binding.descriptorType, Binding.descriptorCount});
  }

  Allocator Alloc(pAllocator);
  DescriptorSetLayout *Obj = Alloc.create<DescriptorSetLayout>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::move(Bindings));
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pSetLayout = toHandle<VkDescriptorSetLayout>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice, VkDescriptorSetLayout descriptorSetLayout,
    const VkAllocationCallbacks *pAllocator) {
  if (!descriptorSetLayout)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<DescriptorSetLayout>(descriptorSetLayout));
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkDescriptorPool *pDescriptorPool) {
  for (uint32_t I = 0; I != pCreateInfo->poolSizeCount; ++I)
    if (!isSupportedDescriptorType(pCreateInfo->pPoolSizes[I].type))
      return VK_ERROR_INITIALIZATION_FAILED;

  Allocator Alloc(pAllocator);
  DescriptorPool *Obj = Alloc.create<DescriptorPool>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, pCreateInfo->maxSets);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pDescriptorPool = toHandle<VkDescriptorPool>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorPool(VkDevice, VkDescriptorPool descriptorPool,
                        const VkAllocationCallbacks *pAllocator) {
  if (!descriptorPool)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<DescriptorPool>(descriptorPool));
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(
    VkDevice, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags) {
  fromHandle<DescriptorPool>(descriptorPool)->reset();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice, const VkDescriptorSetAllocateInfo *pAllocateInfo,
    VkDescriptorSet *pDescriptorSets) {
  auto *Pool = fromHandle<DescriptorPool>(pAllocateInfo->descriptorPool);
  for (uint32_t I = 0; I != pAllocateInfo->descriptorSetCount; ++I) {
    auto *Layout =
        fromHandle<DescriptorSetLayout>(pAllocateInfo->pSetLayouts[I]);
    DescriptorSet *Set = Pool->allocate(*Layout);
    if (!Set) {
      // Per spec: on failure, every set successfully allocated by this
      // call is freed back to the pool and every element of
      // pDescriptorSets is set to VK_NULL_HANDLE, not just the ones from
      // the failing allocation onward.
      for (uint32_t J = 0; J != I; ++J)
        Pool->free(fromHandle<DescriptorSet>(pDescriptorSets[J]));
      for (uint32_t J = 0; J != pAllocateInfo->descriptorSetCount; ++J)
        pDescriptorSets[J] = VK_NULL_HANDLE;
      return VK_ERROR_OUT_OF_POOL_MEMORY;
    }
    pDescriptorSets[I] = toHandle<VkDescriptorSet>(Set);
  }
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(
    VkDevice, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets) {
  auto *Pool = fromHandle<DescriptorPool>(descriptorPool);
  for (uint32_t I = 0; I != descriptorSetCount; ++I)
    if (pDescriptorSets[I])
      Pool->free(fromHandle<DescriptorSet>(pDescriptorSets[I]));
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice, uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
    const VkCopyDescriptorSet *pDescriptorCopies) {
  for (uint32_t I = 0; I != descriptorWriteCount; ++I) {
    const VkWriteDescriptorSet &Write = pDescriptorWrites[I];
    if (!isSupportedDescriptorType(Write.descriptorType))
      continue;
    auto *Set = fromHandle<DescriptorSet>(Write.dstSet);
    if (isTexelBufferDescriptorType(Write.descriptorType)) {
      for (uint32_t J = 0; J != Write.descriptorCount; ++J)
        Set->write(Write.dstBinding, Write.dstArrayElement + J,
                   fromHandle<BufferView>(Write.pTexelBufferView[J]));
      continue;
    }
    for (uint32_t J = 0; J != Write.descriptorCount; ++J) {
      const VkDescriptorBufferInfo &Info = Write.pBufferInfo[J];
      Set->write(Write.dstBinding, Write.dstArrayElement + J,
                 fromHandle<Buffer>(Info.buffer), Info.offset, Info.range);
    }
  }

  for (uint32_t I = 0; I != descriptorCopyCount; ++I) {
    const VkCopyDescriptorSet &Copy = pDescriptorCopies[I];
    auto *Src = fromHandle<DescriptorSet>(Copy.srcSet);
    auto *Dst = fromHandle<DescriptorSet>(Copy.dstSet);
    llvm::ArrayRef<DescriptorBufferBinding> SrcArray =
        Src->bindingArray(Copy.srcBinding);
    for (uint32_t J = 0; J != Copy.descriptorCount; ++J) {
      uint32_t SrcElement = Copy.srcArrayElement + J;
      if (SrcElement >= SrcArray.size())
        break;
      const DescriptorBufferBinding &B = SrcArray[SrcElement];
      if (B.View)
        Dst->write(Copy.dstBinding, Copy.dstArrayElement + J, B.View);
      else
        Dst->write(Copy.dstBinding, Copy.dstArrayElement + J, B.Buf, B.Offset,
                   B.Range);
    }
  }
}

} // namespace feme::vulkan
