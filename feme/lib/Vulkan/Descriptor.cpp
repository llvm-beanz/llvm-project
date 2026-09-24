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
         // (roadmap L180) Vulkan treats an inline uniform block's contents
         // as an implicit uniform buffer -- read-only from the shader,
         // exactly like `UNIFORM_BUFFER` above. `CommandBuffer.cpp`'s
         // `buildBoundResources` consumes it via
         // `DescriptorSet::inlineUniformBlockData`, resolving to this same
         // `Flags = 0` (never `FEME_DESCRIPTOR_UAV`) shape.
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
    uint32_t RealCount = (B.VariableCount && VariableDescriptorCount)
                             ? *VariableDescriptorCount
                             : B.Count;
    if (isInlineUniformBlockDescriptorType(B.Type))
      InlineUniformBlockBindings[B.Binding].resize(RealCount);
    else if (isImageDescriptorType(B.Type) || isSamplerDescriptorType(B.Type)) {
      std::vector<DescriptorImageBinding> &Array = ImageBindings[B.Binding];
      Array.resize(RealCount);
      // (roadmap L153) Seed each array element's sampler half from the
      // layout's own immutable-sampler list up front -- the only way an
      // immutable-sampler binding's sampler half is ever populated, since
      // `vkUpdateDescriptorSets` never applies one (see `write`'s own
      // comment below).
      for (uint32_t I = 0, E = std::min<uint32_t>(RealCount,
                                                  B.ImmutableSamplers.size());
           I != E; ++I)
        Array[I].Samp = B.ImmutableSamplers[I];
    } else
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
  // (roadmap L153) An immutable-sampler binding's sampler half is fixed at
  // layout-creation time; per spec, `vkUpdateDescriptorSets` still applies
  // the rest of a `COMBINED_IMAGE_SAMPLER` write (the image half) to such a
  // binding, but its own `VkDescriptorImageInfo::sampler` field is
  // ignored, not applied -- so the incoming `Samp` here must not clobber
  // the one this set's constructor already seeded from the layout.
  const DescriptorSetLayoutBinding *LayoutBinding = this->Layout->find(Binding);
  bool HasImmutableSampler =
      LayoutBinding && !LayoutBinding->ImmutableSamplers.empty();
  Sampler *ResolvedSamp =
      HasImmutableSampler ? It->second[ArrayElement].Samp : Samp;
  It->second[ArrayElement] = DescriptorImageBinding{View, ResolvedSamp, Layout};
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
  auto Set = std::make_unique<DescriptorSet>(Layout, VariableDescriptorCount);
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
    HighestBinding =
        std::max(HighestBinding, pCreateInfo->pBindings[I].binding);

  std::vector<DescriptorSetLayoutBinding> Bindings;
  Bindings.reserve(pCreateInfo->bindingCount);
  for (uint32_t I = 0; I != pCreateInfo->bindingCount; ++I) {
    const VkDescriptorSetLayoutBinding &Binding = pCreateInfo->pBindings[I];
    if (!isSupportedDescriptorType(Binding.descriptorType))
      return VK_ERROR_INITIALIZATION_FAILED;
    bool VariableCount =
        BindingFlags &&
        (BindingFlags[I] & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT);
    if (VariableCount && Binding.binding != HighestBinding)
      return VK_ERROR_INITIALIZATION_FAILED;
    DescriptorSetLayoutBinding NewBinding{/*Binding=*/Binding.binding,
                                          /*Type=*/Binding.descriptorType,
                                          /*Count=*/Binding.descriptorCount,
                                          /*VariableCount=*/VariableCount,
                                          /*ImmutableSamplers=*/{}};
    // (roadmap L153) `pImmutableSamplers`, when non-null, supplies exactly
    // `descriptorCount` sampler handles baked into this binding for its
    // whole lifetime -- only legal for `SAMPLER`/`COMBINED_IMAGE_SAMPLER`
    // per spec, but guard on `isSamplerDescriptorType` anyway rather than
    // trusting the application not to set it elsewhere.
    if (Binding.pImmutableSamplers &&
        isSamplerDescriptorType(Binding.descriptorType)) {
      NewBinding.ImmutableSamplers.reserve(Binding.descriptorCount);
      for (uint32_t J = 0; J != Binding.descriptorCount; ++J)
        NewBinding.ImmutableSamplers.push_back(
            fromHandle<Sampler>(Binding.pImmutableSamplers[J]));
    }
    Bindings.push_back(std::move(NewBinding));
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

/// (L154) A cursor walking one side (src or dst) of a `VkCopyDescriptorSet`
/// copy. Per spec, if `descriptorCount` is greater than the number of
/// elements (or, for an inline uniform block, bytes) remaining in the
/// binding the copy starts at, the copy continues into the next
/// consecutively-numbered binding, and so on -- the same rule a
/// `VkWriteDescriptorSet` whose own `descriptorCount` overruns one binding
/// follows. `Binding`/`Element` track the binding number and index/byte
/// offset within it the copy is currently reading or writing.
struct BindingCursor {
  uint32_t Binding;
  uint32_t Element;

  /// Normalizes this cursor so `Element` is in-bounds for `Binding`,
  /// spanning forward into later binding numbers (resetting `Element` to
  /// count from each new binding's own start) as needed. \p Size returns
  /// a given binding's declared array size (element count, or byte count
  /// for an inline uniform block) -- 0 for a binding this set's layout
  /// does not declare, which this cursor treats as nothing left to span
  /// into. Returns false if the walk runs off the end of every
  /// consecutively-declared binding before `Element` lands in bounds (an
  /// undeclared/empty binding, or an already-out-of-bounds starting
  /// element) -- the caller should stop its own copy loop there, exactly
  /// like the single-binding bounds check this generalizes.
  bool normalize(llvm::function_ref<size_t(uint32_t)> Size) {
    for (;;) {
      size_t N = Size(Binding);
      if (Element < N)
        return true;
      if (N == 0)
        return false;
      Element -= static_cast<uint32_t>(N);
      ++Binding;
    }
  }
};

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
    // (L154) Both cursors may span into consecutively-numbered bindings as
    // `Copy.descriptorCount` elements are consumed -- see `BindingCursor`'s
    // own comment. `Src`/`Dst` here refer to the same-named `DescriptorSet`s
    // captured above, not the `Src`/`Dst` sets of any other loop.
    BindingCursor SrcCursor{Copy.srcBinding, Copy.srcArrayElement};
    BindingCursor DstCursor{Copy.dstBinding, Copy.dstArrayElement};
    for (uint32_t J = 0; J != Copy.descriptorCount; ++J) {
      if (!SrcCursor.normalize(
              [&](uint32_t B) { return Src->bindingArray(B).size(); }))
        break;
      if (!DstCursor.normalize(
              [&](uint32_t B) { return Dst->bindingArray(B).size(); }))
        break;
      const DescriptorBufferBinding &B =
          Src->bindingArray(SrcCursor.Binding)[SrcCursor.Element];
      if (B.View)
        Dst->write(DstCursor.Binding, DstCursor.Element, B.View);
      else
        Dst->write(DstCursor.Binding, DstCursor.Element, B.Buf, B.Offset,
                   B.Range);
      ++SrcCursor.Element;
      ++DstCursor.Element;
    }

    // (V5) A binding this set's layout declared as an image/sampler type
    // lives in `ImageBindings` instead (see `DescriptorSet`'s constructor);
    // `bindingArray` above returns empty for one, so it needs its own copy
    // loop rather than falling out of the buffer one above. (L154) Spans
    // consecutive bindings exactly like the buffer loop above.
    BindingCursor SrcImageCursor{Copy.srcBinding, Copy.srcArrayElement};
    BindingCursor DstImageCursor{Copy.dstBinding, Copy.dstArrayElement};
    for (uint32_t J = 0; J != Copy.descriptorCount; ++J) {
      if (!SrcImageCursor.normalize(
              [&](uint32_t B) { return Src->imageBindingArray(B).size(); }))
        break;
      if (!DstImageCursor.normalize(
              [&](uint32_t B) { return Dst->imageBindingArray(B).size(); }))
        break;
      const DescriptorImageBinding &B = Src->imageBindingArray(
          SrcImageCursor.Binding)[SrcImageCursor.Element];
      Dst->write(DstImageCursor.Binding, DstImageCursor.Element, B.View, B.Samp,
                 B.Layout);
      ++SrcImageCursor.Element;
      ++DstImageCursor.Element;
    }

    // (roadmap E14) An inline uniform block binding lives in its own byte
    // blob (see `DescriptorSet`'s constructor), and per spec `descriptorCount`/
    // `srcArrayElement`/`dstArrayElement` here are all byte counts/offsets
    // rather than array element counts/indices. (L154) Spans consecutive
    // bindings like the two loops above, but copies one contiguous run of
    // bytes at a time (bounded by whichever of the current src/dst
    // binding's remaining bytes or the overall remaining count is
    // smallest) rather than one byte at a time, since an inline-uniform-
    // block copy is typically far larger than a single byte.
    BindingCursor SrcInlineCursor{Copy.srcBinding, Copy.srcArrayElement};
    BindingCursor DstInlineCursor{Copy.dstBinding, Copy.dstArrayElement};
    uint32_t InlineRemaining = Copy.descriptorCount;
    while (InlineRemaining != 0) {
      if (!SrcInlineCursor.normalize([&](uint32_t B) {
            return Src->inlineUniformBlockData(B).size();
          }))
        break;
      if (!DstInlineCursor.normalize([&](uint32_t B) {
            return Dst->inlineUniformBlockData(B).size();
          }))
        break;
      llvm::ArrayRef<uint8_t> SrcBlob =
          Src->inlineUniformBlockData(SrcInlineCursor.Binding);
      uint32_t DstBlobSize = static_cast<uint32_t>(
          Dst->inlineUniformBlockData(DstInlineCursor.Binding).size());
      uint32_t SrcAvail =
          static_cast<uint32_t>(SrcBlob.size()) - SrcInlineCursor.Element;
      uint32_t DstAvail = DstBlobSize - DstInlineCursor.Element;
      uint32_t Chunk = std::min({InlineRemaining, SrcAvail, DstAvail});
      Dst->writeInlineUniformBlock(DstInlineCursor.Binding,
                                   DstInlineCursor.Element, Chunk,
                                   SrcBlob.data() + SrcInlineCursor.Element);
      SrcInlineCursor.Element += Chunk;
      DstInlineCursor.Element += Chunk;
      InlineRemaining -= Chunk;
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
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::move(Entries),
      pCreateInfo->pipelineBindPoint);
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
