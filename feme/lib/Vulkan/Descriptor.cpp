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
         Type == VK_DESCRIPTOR_TYPE_SAMPLER ||
         isInlineUniformBlockDescriptorType(Type);
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

bool feme::vulkan::isInlineUniformBlockDescriptorType(VkDescriptorType Type) {
  return Type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK;
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
         Type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ||
         // (roadmap E14) Vulkan treats an inline uniform block's contents
         // as an implicit uniform buffer -- read-only from the shader,
         // exactly like `UNIFORM_BUFFER` above -- even though no dispatch
         // consumes one yet (see Descriptor.h's file comment).
         isInlineUniformBlockDescriptorType(Type);
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

DescriptorSet::DescriptorSet(const DescriptorSetLayout &Layout,
                             std::optional<uint32_t> VariableDescriptorCount)
    : Layout(&Layout) {
  for (const DescriptorSetLayoutBinding &B : Layout.bindings()) {
    // (roadmap L12c) Only the layout's own `VariableCount`-flagged binding
    // (at most one, and always the highest-numbered one -- enforced by
    // `vkCreateDescriptorSetLayout`) is ever sized to anything other than
    // its own declared `Count`.
    uint32_t RealCount =
        (B.VariableCount && VariableDescriptorCount) ? *VariableDescriptorCount
                                                     : B.Count;
    if (isInlineUniformBlockDescriptorType(B.Type))
      InlineUniformBlockBindings[B.Binding].resize(RealCount);
    else if (isImageDescriptorType(B.Type) || isSamplerDescriptorType(B.Type))
      ImageBindings[B.Binding].resize(RealCount);
    else
      Bindings[B.Binding].resize(RealCount);
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

void DescriptorSet::writeInlineUniformBlock(uint32_t Binding,
                                            uint32_t ByteOffset,
                                            uint32_t DataSize,
                                            const void *Data) {
  auto It = InlineUniformBlockBindings.find(Binding);
  if (It == InlineUniformBlockBindings.end())
    return;
  std::vector<uint8_t> &Blob = It->second;
  if (ByteOffset > Blob.size() || DataSize > Blob.size() - ByteOffset)
    return;
  std::memcpy(Blob.data() + ByteOffset, Data, DataSize);
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

llvm::ArrayRef<uint8_t>
DescriptorSet::inlineUniformBlockData(uint32_t Binding) const {
  auto It = InlineUniformBlockBindings.find(Binding);
  if (It == InlineUniformBlockBindings.end())
    return {};
  return It->second;
}

DescriptorSet *
DescriptorPool::allocate(const DescriptorSetLayout &Layout,
                         std::optional<uint32_t> VariableDescriptorCount) {
  if (RemainingSets == 0)
    return nullptr;
  auto Set =
      std::make_unique<DescriptorSet>(Layout, VariableDescriptorCount);
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
  // (roadmap L12c) `VkDescriptorSetLayoutBindingFlagsCreateInfo`'s
  // `pBindingFlags[I]` corresponds index-for-index to
  // `pCreateInfo->pBindings[I]` (not to that binding's own `.binding`
  // number), per spec. Per spec, `VK_DESCRIPTOR_BINDING_VARIABLE_
  // DESCRIPTOR_COUNT_BIT` may only be set for the layout's own
  // highest-numbered binding -- checked below by comparing each flagged
  // index's own `.binding` against the largest one seen.
  const VkDescriptorBindingFlags *BindingFlags = nullptr;
  for (const auto *Next =
           static_cast<const VkBaseInStructure *>(pCreateInfo->pNext);
       Next; Next = Next->pNext) {
    if (Next->sType ==
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO) {
      const auto *Flags =
          reinterpret_cast<const VkDescriptorSetLayoutBindingFlagsCreateInfo *>(
              Next);
      if (Flags->bindingCount != pCreateInfo->bindingCount)
        return VK_ERROR_INITIALIZATION_FAILED;
      BindingFlags = Flags->pBindingFlags;
      break;
    }
  }

  uint32_t HighestBinding = 0;
  for (uint32_t I = 0; I != pCreateInfo->bindingCount; ++I)
    HighestBinding = std::max(HighestBinding, pCreateInfo->pBindings[I].binding);

  std::vector<DescriptorSetLayoutBinding> Bindings;
  Bindings.reserve(pCreateInfo->bindingCount);
  for (uint32_t I = 0; I != pCreateInfo->bindingCount; ++I) {
    const VkDescriptorSetLayoutBinding &Binding = pCreateInfo->pBindings[I];
    if (!isSupportedDescriptorType(Binding.descriptorType))
      return VK_ERROR_INITIALIZATION_FAILED;
    bool VariableCount =
        BindingFlags && (BindingFlags[I] &
                        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT);
    if (VariableCount && Binding.binding != HighestBinding)
      return VK_ERROR_INITIALIZATION_FAILED;
    Bindings.push_back(DescriptorSetLayoutBinding{
        Binding.binding, Binding.descriptorType, Binding.descriptorCount,
        VariableCount});
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
  // (roadmap L12c) `VkDescriptorSetVariableDescriptorCountAllocateInfo`'s
  // `pDescriptorCounts[I]` corresponds index-for-index to
  // `pAllocateInfo->pSetLayouts[I]`; an entry is only meaningful (and, per
  // spec, only consulted) for a set whose own layout has a
  // `VariableCount`-flagged binding at all -- ignored otherwise.
  const uint32_t *DescriptorCounts = nullptr;
  for (const auto *Next =
           static_cast<const VkBaseInStructure *>(pAllocateInfo->pNext);
       Next; Next = Next->pNext) {
    if (Next->sType ==
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO) {
      const auto *Counts = reinterpret_cast<
          const VkDescriptorSetVariableDescriptorCountAllocateInfo *>(Next);
      if (Counts->descriptorSetCount != pAllocateInfo->descriptorSetCount)
        return VK_ERROR_INITIALIZATION_FAILED;
      DescriptorCounts = Counts->pDescriptorCounts;
      break;
    }
  }

  auto *Pool = fromHandle<DescriptorPool>(pAllocateInfo->descriptorPool);
  for (uint32_t I = 0; I != pAllocateInfo->descriptorSetCount; ++I) {
    auto *Layout =
        fromHandle<DescriptorSetLayout>(pAllocateInfo->pSetLayouts[I]);

    std::optional<uint32_t> VariableDescriptorCount;
    if (DescriptorCounts && !Layout->bindings().empty()) {
      const DescriptorSetLayoutBinding &LastBinding = Layout->bindings().back();
      if (LastBinding.VariableCount) {
        if (DescriptorCounts[I] > LastBinding.Count) {
          for (uint32_t J = 0; J != I; ++J)
            Pool->free(fromHandle<DescriptorSet>(pDescriptorSets[J]));
          for (uint32_t J = 0; J != pAllocateInfo->descriptorSetCount; ++J)
            pDescriptorSets[J] = VK_NULL_HANDLE;
          return VK_ERROR_INITIALIZATION_FAILED;
        }
        VariableDescriptorCount = DescriptorCounts[I];
      }
    }

    DescriptorSet *Set = Pool->allocate(*Layout, VariableDescriptorCount);
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
/// (roadmap E14) Never called for `VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK`:
/// unlike every type this dispatches, one inline-uniform-block write covers
/// a caller-chosen byte range in a single call rather than one array
/// element at a time, so both call sites special-case it themselves before
/// reaching here, calling `DescriptorSet::writeInlineUniformBlock` directly.
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

/// (roadmap E14) The `VkWriteDescriptorSetInlineUniformBlock` chained onto
/// \p pNext, or null if none is -- the same "walk `pNext` for a specific
/// `sType`" pattern `EntryPoints.cpp`'s feature/property chain walkers use.
const VkWriteDescriptorSetInlineUniformBlock *
findInlineUniformBlockInfo(const void *pNext) {
  for (const auto *Base = static_cast<const VkBaseInStructure *>(pNext); Base;
       Base = Base->pNext)
    if (Base->sType ==
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK)
      return reinterpret_cast<const VkWriteDescriptorSetInlineUniformBlock *>(
          Base);
  return nullptr;
}

/// Applies one `VkWriteDescriptorSet` entry to \p Set, exactly as
/// `vkUpdateDescriptorSets` applies each of its own `pDescriptorWrites`
/// entries -- \p Write's own `dstSet` is never consulted here (the caller
/// resolves which `DescriptorSet` this entry targets). Shared by
/// `vkUpdateDescriptorSets`'s loop below and the exported
/// `applyDescriptorWrites` (roadmap F12), whose only caller,
/// `CommandBuffer::pushDescriptorSet`, needs exactly this per-entry
/// dispatch applied to one fixed target set instead.
void applyDescriptorWrite(DescriptorSet &Set,
                          const VkWriteDescriptorSet &Write) {
  if (!isSupportedDescriptorType(Write.descriptorType))
    return;
  // (roadmap E14) See `writeDescriptorFromRaw`'s own comment: an inline
  // uniform block write covers a caller-chosen byte range in one call
  // rather than one array element at a time, so it is special-cased here
  // too instead of falling into the per-element loop below.
  if (isInlineUniformBlockDescriptorType(Write.descriptorType)) {
    if (const VkWriteDescriptorSetInlineUniformBlock *Inline =
            findInlineUniformBlockInfo(Write.pNext))
      Set.writeInlineUniformBlock(Write.dstBinding, Write.dstArrayElement,
                                  Inline->dataSize, Inline->pData);
    return;
  }
  for (uint32_t J = 0; J != Write.descriptorCount; ++J) {
    const void *Data;
    if (isTexelBufferDescriptorType(Write.descriptorType))
      Data = &Write.pTexelBufferView[J];
    else if (isImageDescriptorType(Write.descriptorType) ||
             isSamplerDescriptorType(Write.descriptorType))
      Data = &Write.pImageInfo[J];
    else
      Data = &Write.pBufferInfo[J];
    writeDescriptorFromRaw(Set, Write.descriptorType, Write.dstBinding,
                           Write.dstArrayElement + J, Data);
  }
}

} // namespace

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice, uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
    const VkCopyDescriptorSet *pDescriptorCopies) {
  for (uint32_t I = 0; I != descriptorWriteCount; ++I) {
    const VkWriteDescriptorSet &Write = pDescriptorWrites[I];
    auto *Set = fromHandle<DescriptorSet>(Write.dstSet);
    applyDescriptorWrite(*Set, Write);
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

    // (roadmap E14) An inline uniform block binding lives in its own byte
    // blob (see `DescriptorSet`'s constructor), and per spec `descriptorCount`/
    // `srcArrayElement`/`dstArrayElement` here are all byte counts/offsets
    // rather than array element counts/indices -- a single ranged copy,
    // not a per-element loop like the two above.
    llvm::ArrayRef<uint8_t> SrcInlineData =
        Src->inlineUniformBlockData(Copy.srcBinding);
    if (Copy.srcArrayElement < SrcInlineData.size()) {
      uint32_t Available =
          static_cast<uint32_t>(SrcInlineData.size() - Copy.srcArrayElement);
      uint32_t CopyCount = std::min(Copy.descriptorCount, Available);
      Dst->writeInlineUniformBlock(Copy.dstBinding, Copy.dstArrayElement,
                                   CopyCount,
                                   SrcInlineData.data() + Copy.srcArrayElement);
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

  // (roadmap L12c) This ICD advertises no descriptor-count limit beyond a
  // binding's own declared `descriptorCount` (see this function's own file
  // comment above), so the flagged binding's own maximum real count is
  // exactly that same value -- no narrower cap exists to report here.
  for (auto *Next = static_cast<VkBaseOutStructure *>(pSupport->pNext); Next;
       Next = Next->pNext) {
    if (Next->sType ==
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT) {
      auto *Out = reinterpret_cast<
          VkDescriptorSetVariableDescriptorCountLayoutSupport *>(Next);
      Out->maxVariableDescriptorCount = 0;
      const VkDescriptorBindingFlags *BindingFlags = nullptr;
      for (const auto *In =
               static_cast<const VkBaseInStructure *>(pCreateInfo->pNext);
           In; In = In->pNext) {
        if (In->sType ==
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO) {
          const auto *Flags = reinterpret_cast<
              const VkDescriptorSetLayoutBindingFlagsCreateInfo *>(In);
          if (Flags->bindingCount == pCreateInfo->bindingCount)
            BindingFlags = Flags->pBindingFlags;
          break;
        }
      }
      if (BindingFlags)
        for (uint32_t I = 0; I != pCreateInfo->bindingCount; ++I)
          if (BindingFlags[I] &
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)
            Out->maxVariableDescriptorCount =
                pCreateInfo->pBindings[I].descriptorCount;
      break;
    }
  }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(
    VkDevice, const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate) {
  // (roadmap F12) `_PUSH_DESCRIPTORS` templates are now accepted alongside
  // plain `_DESCRIPTOR_SET` ones: `DescriptorUpdateTemplate`'s own entry
  // list needs no distinction between the two (see its class comment), and
  // `vkCmdPushDescriptorSetWithTemplate` (CommandBuffer.cpp) is this
  // template type's one real consumer.
  if (pCreateInfo->templateType !=
          VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET &&
      pCreateInfo->templateType !=
          VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS)
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

void applyDescriptorUpdateTemplate(DescriptorSet &Set,
                                   const DescriptorUpdateTemplate &Template,
                                   const void *Data) {
  const auto *Bytes = static_cast<const uint8_t *>(Data);
  for (const VkDescriptorUpdateTemplateEntry &Entry : Template.entries()) {
    // (roadmap E14) Per spec, an inline uniform block entry updates
    // `descriptorCount` contiguous bytes starting at byte offset
    // `dstArrayElement`, reading from `offset` bytes into the source
    // buffer with `stride` ignored -- the same single-ranged-write shape
    // `vkUpdateDescriptorSets`'s own inline-uniform-block case uses,
    // rather than the per-element `offset + i * stride` loop below.
    if (isInlineUniformBlockDescriptorType(Entry.descriptorType)) {
      Set.writeInlineUniformBlock(Entry.dstBinding, Entry.dstArrayElement,
                                  Entry.descriptorCount, Bytes + Entry.offset);
      continue;
    }
    for (uint32_t J = 0; J != Entry.descriptorCount; ++J)
      writeDescriptorFromRaw(Set, Entry.descriptorType, Entry.dstBinding,
                             Entry.dstArrayElement + J,
                             Bytes + Entry.offset + J * Entry.stride);
  }
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(
    VkDevice, VkDescriptorSet descriptorSet,
    VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData) {
  auto *Set = fromHandle<DescriptorSet>(descriptorSet);
  const auto *Template =
      fromHandle<DescriptorUpdateTemplate>(descriptorUpdateTemplate);
  applyDescriptorUpdateTemplate(*Set, *Template, pData);
}

void applyDescriptorWrites(DescriptorSet &Set,
                           llvm::ArrayRef<VkWriteDescriptorSet> Writes) {
  for (const VkWriteDescriptorSet &Write : Writes)
    applyDescriptorWrite(Set, Write);
}

} // namespace feme::vulkan
