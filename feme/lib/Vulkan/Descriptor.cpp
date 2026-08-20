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
#include "Image.h"
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
         isTexelBufferDescriptorType(Type) || isImageDescriptorType(Type) ||
         Type == VK_DESCRIPTOR_TYPE_SAMPLER;
}

bool feme::vulkan::isTexelBufferDescriptorType(VkDescriptorType Type) {
  return Type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
         Type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
}

bool feme::vulkan::isImageDescriptorType(VkDescriptorType Type) {
  return Type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
         Type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
         Type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
         Type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
}

bool feme::vulkan::isSamplerDescriptorType(VkDescriptorType Type) {
  return Type == VK_DESCRIPTOR_TYPE_SAMPLER ||
         Type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

bool feme::vulkan::isDynamicDescriptorType(VkDescriptorType Type) {
  return Type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC ||
         Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
}

bool feme::vulkan::isReadOnlyDescriptorType(VkDescriptorType Type) {
  return Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
         Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
         Type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
         Type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
         Type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
         Type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
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
  for (const DescriptorSetLayoutBinding &B : Layout.bindings()) {
    if (isImageDescriptorType(B.Type) || isSamplerDescriptorType(B.Type))
      ImageBindings[B.Binding].resize(B.Count);
    else
      Bindings[B.Binding].resize(B.Count);
  }
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

void DescriptorSet::write(uint32_t Binding, uint32_t ArrayElement,
                          ImageView *View, Sampler *Samp,
                          VkImageLayout Layout) {
  auto It = ImageBindings.find(Binding);
  if (It == ImageBindings.end() || ArrayElement >= It->second.size())
    return;
  It->second[ArrayElement] = DescriptorImageBinding{View, Samp, Layout};
}

llvm::ArrayRef<DescriptorBufferBinding>
DescriptorSet::bindingArray(uint32_t Binding) const {
  auto It = Bindings.find(Binding);
  if (It == Bindings.end())
    return {};
  return It->second;
}

llvm::ArrayRef<DescriptorImageBinding>
DescriptorSet::imageBindingArray(uint32_t Binding) const {
  auto It = ImageBindings.find(Binding);
  if (It == ImageBindings.end())
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

namespace {

/// Writes one descriptor array element into \p Set from a raw pointer to
/// its Vulkan info struct (`VkDescriptorBufferInfo`, `VkDescriptorImageInfo`,
/// or `VkBufferView`), dispatching on \p Type exactly as
/// `vkUpdateDescriptorSets` does. Shared with
/// `vkUpdateDescriptorSetWithTemplate` (Descriptor.cpp), whose source data
/// is an arbitrary caller-supplied byte layout rather than one of the
/// three typed arrays `VkWriteDescriptorSet` itself carries -- both need
/// the exact same per-descriptor-type switch, just addressed differently.
void writeDescriptorFromRaw(DescriptorSet &Set, VkDescriptorType Type,
                            uint32_t Binding, uint32_t ArrayElement,
                            const void *Data) {
  if (!isSupportedDescriptorType(Type))
    return;
  if (isTexelBufferDescriptorType(Type)) {
    VkBufferView View;
    std::memcpy(&View, Data, sizeof(View));
    Set.write(Binding, ArrayElement, fromHandle<BufferView>(View));
    return;
  }
  if (isImageDescriptorType(Type) || isSamplerDescriptorType(Type)) {
    VkDescriptorImageInfo Info;
    std::memcpy(&Info, Data, sizeof(Info));
    bool WantsImage = isImageDescriptorType(Type);
    bool WantsSampler = isSamplerDescriptorType(Type);
    Set.write(Binding, ArrayElement,
              WantsImage ? fromHandle<ImageView>(Info.imageView) : nullptr,
              WantsSampler ? fromHandle<Sampler>(Info.sampler) : nullptr,
              Info.imageLayout);
    return;
  }
  VkDescriptorBufferInfo Info;
  std::memcpy(&Info, Data, sizeof(Info));
  Set.write(Binding, ArrayElement, fromHandle<Buffer>(Info.buffer), Info.offset,
            Info.range);
}

} // namespace

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice, uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
    const VkCopyDescriptorSet *pDescriptorCopies) {
  for (uint32_t I = 0; I != descriptorWriteCount; ++I) {
    const VkWriteDescriptorSet &Write = pDescriptorWrites[I];
    if (!isSupportedDescriptorType(Write.descriptorType))
      continue;
    auto *Set = fromHandle<DescriptorSet>(Write.dstSet);
    for (uint32_t J = 0; J != Write.descriptorCount; ++J) {
      const void *Data;
      if (isTexelBufferDescriptorType(Write.descriptorType))
        Data = &Write.pTexelBufferView[J];
      else if (isImageDescriptorType(Write.descriptorType) ||
               isSamplerDescriptorType(Write.descriptorType))
        Data = &Write.pImageInfo[J];
      else
        Data = &Write.pBufferInfo[J];
      writeDescriptorFromRaw(*Set, Write.descriptorType, Write.dstBinding,
                             Write.dstArrayElement + J, Data);
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

    // (V5) A binding this set's layout declared as an image/sampler type
    // lives in `ImageBindings` instead (see `DescriptorSet`'s constructor);
    // `bindingArray` above returns empty for one, so it needs its own copy
    // loop rather than falling out of the buffer one above.
    llvm::ArrayRef<DescriptorImageBinding> SrcImageArray =
        Src->imageBindingArray(Copy.srcBinding);
    for (uint32_t J = 0; J != Copy.descriptorCount; ++J) {
      uint32_t SrcElement = Copy.srcArrayElement + J;
      if (SrcElement >= SrcImageArray.size())
        break;
      const DescriptorImageBinding &B = SrcImageArray[SrcElement];
      Dst->write(Copy.dstBinding, Copy.dstArrayElement + J, B.View, B.Samp,
                 B.Layout);
    }
  }
}

/// Reports whether \p pCreateInfo could actually be used to create a
/// `VkDescriptorSetLayout` on this device, per the same limits
/// `vkCreateDescriptorSetLayout` itself enforces (this ICD advertises no
/// further descriptor-count or layout limits beyond "every binding's type is
/// one this ICD implements" -- see `isSupportedDescriptorType`), rather than
/// actually creating anything.
VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSupport(
    VkDevice, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
    VkDescriptorSetLayoutSupport *pSupport) {
  bool Supported = true;
  for (uint32_t I = 0; Supported && I != pCreateInfo->bindingCount; ++I)
    Supported =
        isSupportedDescriptorType(pCreateInfo->pBindings[I].descriptorType);
  pSupport->supported = Supported ? VK_TRUE : VK_FALSE;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(
    VkDevice, const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate) {
  // `PUSH_DESCRIPTORS_KHR` templates target `VK_KHR_push_descriptor`, which
  // this ICD does not implement (every other push-descriptor entry point
  // already rejects with "VK_KHR_push_descriptor is not supported" --
  // ProcAddr.cpp's table simply has no such command to resolve at all, so
  // rejecting the template type here is this command's own version of
  // that same rejection).
  if (pCreateInfo->templateType !=
      VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET)
    return VK_ERROR_INITIALIZATION_FAILED;
  std::vector<VkDescriptorUpdateTemplateEntry> Entries(
      pCreateInfo->pDescriptorUpdateEntries,
      pCreateInfo->pDescriptorUpdateEntries +
          pCreateInfo->descriptorUpdateEntryCount);
  for (const VkDescriptorUpdateTemplateEntry &Entry : Entries)
    if (!isSupportedDescriptorType(Entry.descriptorType))
      return VK_ERROR_INITIALIZATION_FAILED;

  Allocator Alloc(pAllocator);
  DescriptorUpdateTemplate *Obj = Alloc.create<DescriptorUpdateTemplate>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::move(Entries));
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pDescriptorUpdateTemplate = toHandle<VkDescriptorUpdateTemplate>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(
    VkDevice, VkDescriptorUpdateTemplate descriptorUpdateTemplate,
    const VkAllocationCallbacks *pAllocator) {
  if (!descriptorUpdateTemplate)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<DescriptorUpdateTemplate>(descriptorUpdateTemplate));
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(
    VkDevice, VkDescriptorSet descriptorSet,
    VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData) {
  auto *Set = fromHandle<DescriptorSet>(descriptorSet);
  const auto *Template =
      fromHandle<DescriptorUpdateTemplate>(descriptorUpdateTemplate);
  const auto *Bytes = static_cast<const uint8_t *>(pData);
  for (const VkDescriptorUpdateTemplateEntry &Entry : Template->entries())
    for (uint32_t J = 0; J != Entry.descriptorCount; ++J)
      writeDescriptorFromRaw(*Set, Entry.descriptorType, Entry.dstBinding,
                             Entry.dstArrayElement + J,
                             Bytes + Entry.offset + J * Entry.stride);
}

} // namespace feme::vulkan
