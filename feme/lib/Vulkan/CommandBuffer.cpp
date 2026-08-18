//===- CommandBuffer.cpp - VkCommandPool/VkCommandBuffer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandBuffer.h"
#include "Buffer.h"
#include "Descriptor.h"
#include "Icd.h"
#include "Objects.h"
#include "Pipeline.h"

#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/ResourceHeap.h"
#include "feme/Target/CPU/ResourceInfo.h"

#include <cstring>

using namespace feme::vulkan;
using namespace llvm;

namespace {

/// Validates \p Count against `maxComputeWorkGroupCount`, per "Limits and
/// features": "must be checked both at pipeline creation and dispatch".
Error validateGroupCount(const PhysicalDeviceInfo *Info,
                         std::array<uint32_t, 3> Count) {
  if (!Info)
    return Error::success();
  const VkPhysicalDeviceLimits &Limits = Info->Properties.limits;
  if (Count[0] > Limits.maxComputeWorkGroupCount[0] ||
      Count[1] > Limits.maxComputeWorkGroupCount[1] ||
      Count[2] > Limits.maxComputeWorkGroupCount[2])
    return createStringError(inconvertibleErrorCode(),
                             "dispatch group count exceeds "
                             "maxComputeWorkGroupCount");
  return Error::success();
}

/// One currently-bound `VkDescriptorSet` slot, as `vkCmdBindDescriptorSets`
/// leaves it (see "Descriptor Model"): the set itself and the dynamic
/// offsets supplied for it in that call, consumed in ascending
/// (set, binding) order by `buildBoundResources` below.
struct BoundSetState {
  DescriptorSet *Set = nullptr;
  std::vector<uint32_t> DynamicOffsets;
};

/// Owns the `FemeDescriptor` arrays `buildBoundResources` materializes,
/// referenced by `Bindings`' `llvm::ArrayRef`s. Kept alive for exactly as
/// long as the dispatch that consumes them runs.
struct MaterializedBoundResources {
  std::vector<std::vector<feme::cpu::FemeDescriptor>> Storage;
  std::vector<feme::cpu::BoundResourceBinding> Bindings;
};

/// Builds the `FemeDescriptor` arrays a dispatch's currently bound
/// descriptor sets resolve to: one array per (set, binding) with a
/// non-empty declared array, applying a dynamic binding's offset from its
/// set's captured `DynamicOffsets` (see "Memory and Buffers": "Data =
/// memory allocation base + buffer binding offset + descriptor offset").
/// An unwritten array element, an unbound buffer, or an out-of-range
/// offset/range resolves to the all-zero (`Kind::None`) descriptor rather
/// than a wild pointer, per "Error Handling and Security".
MaterializedBoundResources
buildBoundResources(llvm::ArrayRef<BoundSetState> BoundSets) {
  MaterializedBoundResources Result;
  for (uint32_t SetIdx = 0; SetIdx != BoundSets.size(); ++SetIdx) {
    const BoundSetState &State = BoundSets[SetIdx];
    if (!State.Set)
      continue;
    const DescriptorSetLayout &Layout = State.Set->getLayout();
    uint32_t DynamicOffsetCursor = 0;
    for (const DescriptorSetLayoutBinding &BindingDecl : Layout.bindings()) {
      llvm::ArrayRef<DescriptorBufferBinding> Array =
          State.Set->bindingArray(BindingDecl.Binding);
      bool Dynamic = isDynamicDescriptorType(BindingDecl.Type);
      if (Array.empty())
        continue;

      std::vector<feme::cpu::FemeDescriptor> Descriptors(Array.size());
      for (size_t J = 0; J != Array.size(); ++J) {
        const DescriptorBufferBinding &Src = Array[J];
        VkDeviceSize DynOffset = 0;
        if (Dynamic) {
          if (DynamicOffsetCursor < State.DynamicOffsets.size())
            DynOffset = State.DynamicOffsets[DynamicOffsetCursor];
          ++DynamicOffsetCursor;
        }
        if (!Src.Buf || !Src.Buf->isBound())
          continue; // Kind::None (never written).

        VkDeviceSize BufSize = Src.Buf->size();
        VkDeviceSize Base = Src.Offset + DynOffset;
        if (Base < Src.Offset || Base > BufSize)
          continue; // Overflow, or the dynamic offset alone overruns it.
        VkDeviceSize Range =
            Src.Range == VK_WHOLE_SIZE ? BufSize - Base : Src.Range;
        if (Range > BufSize - Base)
          continue; // Declared range overruns the buffer.

        feme::cpu::FemeDescriptor &Dst = Descriptors[J];
        Dst.Data = static_cast<uint8_t *>(Src.Buf->data()) + Base;
        Dst.SizeInBytes = Range;
        Dst.Kind = static_cast<uint32_t>(feme::cpu::ResourceKind::Raw);
        Dst.Flags = feme::cpu::FEME_DESCRIPTOR_UAV; // Storage buffers are
                                                     // always read-write.
      }
      Result.Storage.push_back(std::move(Descriptors));
      Result.Bindings.push_back(feme::cpu::BoundResourceBinding{
          SetIdx, BindingDecl.Binding, Result.Storage.back()});
    }
  }
  return Result;
}

/// Runs one dispatch: materializes the currently bound descriptor sets'
/// physical resource heap (see "Descriptor Model"), allocates private
/// groupshared storage per group (see "Implement ... private groupshared
/// allocation"), and calls `CompiledStage::invokeGroup` once per group in
/// `[Base, Base+Count)`, sequentially. Parallelizing independent groups
/// across a worker pool is a later performance enhancement (see
/// feme::cpu::JITEngine, which this ICD deliberately bypasses for direct
/// control over `GroupID` offsetting and indirect argument reads -- see
/// "Command Buffers"'s Deviation note in FeMeVulkanDesign.md's V1 status).
Error runDispatch(ComputePipeline &Pipeline, std::array<uint32_t, 3> Base,
                  std::array<uint32_t, 3> Count,
                  llvm::ArrayRef<BoundSetState> BoundSets) {
  feme::cpu::CompiledStage &Stage = Pipeline.getStage();
  feme::cpu::StageArtifactInfo Artifact = Stage.getArtifactInfo();

  MaterializedBoundResources Materialized = buildBoundResources(BoundSets);
  feme::cpu::DispatchResources Resources;
  Resources.BoundResources = Materialized.Bindings;
  feme::cpu::PreparedDispatch Prepared = feme::cpu::PreparedDispatch::create(
      Stage.getResourceInfo(), Resources, Count);

  std::vector<uint8_t> GroupShared(Artifact.GroupSharedSize);
  for (uint32_t Z = 0; Z != Count[2]; ++Z)
    for (uint32_t Y = 0; Y != Count[1]; ++Y)
      for (uint32_t X = 0; X != Count[0]; ++X) {
        std::array<uint32_t, 3> GroupID{Base[0] + X, Base[1] + Y, Base[2] + Z};
        if (Error E = Stage.invokeGroup(Prepared, GroupID, GroupShared))
          return E;
      }
  return Error::success();
}

/// `vkCmdCopyBuffer`: copies each region from \p Src to \p Dst, per
/// "Command Buffers". Every region is bounds-checked against both
/// buffers' sizes before any copy runs (see "Error Handling and
/// Security").
Error runCopyBuffer(Buffer *Src, Buffer *Dst,
                    llvm::ArrayRef<VkBufferCopy> Regions) {
  if (!Src || !Src->isBound() || !Dst || !Dst->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "buffer copy source/destination is not bound");
  for (const VkBufferCopy &Region : Regions) {
    if (Region.srcOffset + Region.size > Src->size() ||
        Region.dstOffset + Region.size > Dst->size())
      return createStringError(inconvertibleErrorCode(),
                               "buffer copy region is out of range");
    std::memcpy(static_cast<uint8_t *>(Dst->data()) + Region.dstOffset,
               static_cast<const uint8_t *>(Src->data()) + Region.srcOffset,
               Region.size);
  }
  return Error::success();
}

/// `vkCmdFillBuffer`: repeats \p Data (a 4-byte word) across
/// `[Offset, Offset+Size)` of \p Dst.
Error runFillBuffer(Buffer *Dst, VkDeviceSize Offset, VkDeviceSize Size,
                    uint32_t Data) {
  if (!Dst || !Dst->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "fill buffer destination is not bound");
  VkDeviceSize ResolvedSize = Size == VK_WHOLE_SIZE ? Dst->size() - Offset : Size;
  if (Offset + ResolvedSize > Dst->size())
    return createStringError(inconvertibleErrorCode(),
                             "fill buffer region is out of range");
  auto *Words = static_cast<uint32_t *>(
      static_cast<void *>(static_cast<uint8_t *>(Dst->data()) + Offset));
  std::fill_n(Words, ResolvedSize / sizeof(uint32_t), Data);
  return Error::success();
}

/// `vkCmdUpdateBuffer`: copies the recorded payload into \p Dst at
/// \p Offset.
Error runUpdateBuffer(Buffer *Dst, VkDeviceSize Offset,
                     llvm::ArrayRef<uint8_t> Data) {
  if (!Dst || !Dst->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "update buffer destination is not bound");
  if (Offset + Data.size() > Dst->size())
    return createStringError(inconvertibleErrorCode(),
                             "update buffer region is out of range");
  std::memcpy(static_cast<uint8_t *>(Dst->data()) + Offset, Data.data(),
             Data.size());
  return Error::success();
}

} // namespace

llvm::Error feme::vulkan::executeCommandBuffer(const CommandBuffer &CmdBuf) {
  ComputePipeline *BoundPipeline = nullptr;
  std::vector<BoundSetState> BoundSets;
  for (const RecordedCommand &Cmd : CmdBuf.commands()) {
    switch (Cmd.Op) {
    case RecordedCommand::Kind::BindPipeline:
      BoundPipeline = Cmd.Pipeline;
      break;
    case RecordedCommand::Kind::BindDescriptorSets: {
      uint32_t Required = Cmd.FirstSet + Cmd.DescriptorSets.size();
      if (BoundSets.size() < Required)
        BoundSets.resize(Required);
      uint32_t OffsetCursor = 0;
      for (size_t I = 0; I != Cmd.DescriptorSets.size(); ++I) {
        DescriptorSet *Set = Cmd.DescriptorSets[I];
        uint32_t Consumed = Set ? Set->getLayout().dynamicOffsetCount() : 0;
        std::vector<uint32_t> Offsets;
        if (OffsetCursor + Consumed <= Cmd.DynamicOffsets.size())
          Offsets.assign(Cmd.DynamicOffsets.begin() + OffsetCursor,
                        Cmd.DynamicOffsets.begin() + OffsetCursor + Consumed);
        OffsetCursor += Consumed;
        BoundSets[Cmd.FirstSet + I] = BoundSetState{Set, std::move(Offsets)};
      }
      break;
    }
    case RecordedCommand::Kind::Dispatch:
    case RecordedCommand::Kind::DispatchBase: {
      if (!BoundPipeline)
        return createStringError(inconvertibleErrorCode(),
                                 "dispatch with no bound compute pipeline");
      if (Error E =
              validateGroupCount(CmdBuf.getPhysicalDeviceInfo(), Cmd.Count))
        return E;
      if (Error E = runDispatch(*BoundPipeline, Cmd.Base, Cmd.Count, BoundSets))
        return E;
      break;
    }
    case RecordedCommand::Kind::DispatchIndirect: {
      if (!BoundPipeline)
        return createStringError(inconvertibleErrorCode(),
                                 "dispatch with no bound compute pipeline");
      if (!Cmd.IndirectBuffer || !Cmd.IndirectBuffer->isBound())
        return createStringError(inconvertibleErrorCode(),
                                 "dispatch indirect buffer is not bound");
      if (Cmd.IndirectOffset + 3 * sizeof(uint32_t) >
          Cmd.IndirectBuffer->size())
        return createStringError(inconvertibleErrorCode(),
                                 "dispatch indirect offset is out of range "
                                 "of its buffer");
      std::array<uint32_t, 3> Count{};
      std::memcpy(Count.data(),
                  static_cast<const uint8_t *>(Cmd.IndirectBuffer->data()) +
                      Cmd.IndirectOffset,
                  sizeof(Count));
      if (Error E = validateGroupCount(CmdBuf.getPhysicalDeviceInfo(), Count))
        return E;
      if (Error E = runDispatch(*BoundPipeline, {0, 0, 0}, Count, BoundSets))
        return E;
      break;
    }
    case RecordedCommand::Kind::CopyBuffer:
      if (Error E = runCopyBuffer(Cmd.SrcBuffer, Cmd.DstBuffer, Cmd.CopyRegions))
        return E;
      break;
    case RecordedCommand::Kind::FillBuffer:
      if (Error E = runFillBuffer(Cmd.DstBuffer, Cmd.DstOffset, Cmd.DstSize,
                                  Cmd.FillData))
        return E;
      break;
    case RecordedCommand::Kind::UpdateBuffer:
      if (Error E =
              runUpdateBuffer(Cmd.DstBuffer, Cmd.DstOffset, Cmd.UpdateData))
        return E;
      break;
    case RecordedCommand::Kind::PipelineBarrier:
      // See `pipelineBarrier`'s own comment: already a no-op join under
      // this milestone's strictly-sequential execution model.
      break;
    }
  }
  return Error::success();
}

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool) {
  // Only the single compute/transfer queue family (index 0) exists.
  if (pCreateInfo->queueFamilyIndex != 0)
    return VK_ERROR_INITIALIZATION_FAILED;

  const PhysicalDeviceInfo &Info =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  Allocator Alloc(pAllocator);
  vulkan::CommandPool *Obj = Alloc.create<vulkan::CommandPool>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, Info);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pCommandPool = toHandle<VkCommandPool>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyCommandPool(VkDevice, VkCommandPool commandPool,
                     const VkAllocationCallbacks *pAllocator) {
  if (!commandPool)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<vulkan::CommandPool>(commandPool));
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice,
                                                  VkCommandPool commandPool,
                                                  VkCommandPoolResetFlags) {
  fromHandle<vulkan::CommandPool>(commandPool)->reset();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice, const VkCommandBufferAllocateInfo *pAllocateInfo,
    VkCommandBuffer *pCommandBuffers) {
  // Secondary command buffers are not implemented yet (see "Command
  // Buffers": that command set is V2+); only primary is available.
  if (pAllocateInfo->level != VK_COMMAND_BUFFER_LEVEL_PRIMARY)
    return VK_ERROR_INITIALIZATION_FAILED;

  auto *Pool = fromHandle<vulkan::CommandPool>(pAllocateInfo->commandPool);
  for (uint32_t I = 0; I != pAllocateInfo->commandBufferCount; ++I)
    pCommandBuffers[I] = toHandle<VkCommandBuffer>(Pool->allocate());
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice, VkCommandPool commandPool, uint32_t commandBufferCount,
    const VkCommandBuffer *pCommandBuffers) {
  auto *Pool = fromHandle<vulkan::CommandPool>(commandPool);
  for (uint32_t I = 0; I != commandBufferCount; ++I)
    if (pCommandBuffers[I])
      Pool->free(fromHandle<vulkan::CommandBuffer>(pCommandBuffers[I]));
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->begin();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->end();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->reset();
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdBindPipeline(VkCommandBuffer commandBuffer,
                  VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
  if (pipelineBindPoint != VK_PIPELINE_BIND_POINT_COMPUTE)
    return; // No other bind point is implemented yet (graphics is V6+).
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->bindPipeline(fromHandle<ComputePipeline>(pipeline));
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout, uint32_t firstSet, uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
    const uint32_t *pDynamicOffsets) {
  if (pipelineBindPoint != VK_PIPELINE_BIND_POINT_COMPUTE)
    return; // No other bind point is implemented yet (graphics is V6+).
  std::vector<DescriptorSet *> Sets;
  Sets.reserve(descriptorSetCount);
  for (uint32_t I = 0; I != descriptorSetCount; ++I)
    Sets.push_back(fromHandle<DescriptorSet>(pDescriptorSets[I]));
  std::vector<uint32_t> Offsets(pDynamicOffsets,
                                pDynamicOffsets + dynamicOffsetCount);
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->bindDescriptorSets(firstSet, std::move(Sets), std::move(Offsets));
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer commandBuffer,
                                         uint32_t groupCountX,
                                         uint32_t groupCountY,
                                         uint32_t groupCountZ) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->dispatch({groupCountX, groupCountY, groupCountZ});
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBase(
    VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY,
    uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY,
    uint32_t groupCountZ) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->dispatchBase({baseGroupX, baseGroupY, baseGroupZ},
                     {groupCountX, groupCountY, groupCountZ});
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer commandBuffer,
                                                 VkBuffer buffer,
                                                 VkDeviceSize offset) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->dispatchIndirect(fromHandle<vulkan::Buffer>(buffer), offset);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer,
                                           VkBuffer srcBuffer,
                                           VkBuffer dstBuffer,
                                           uint32_t regionCount,
                                           const VkBufferCopy *pRegions) {
  std::vector<VkBufferCopy> Regions(pRegions, pRegions + regionCount);
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->copyBuffer(fromHandle<vulkan::Buffer>(srcBuffer),
                  fromHandle<vulkan::Buffer>(dstBuffer), std::move(Regions));
}

VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer commandBuffer,
                                           VkBuffer dstBuffer,
                                           VkDeviceSize dstOffset,
                                           VkDeviceSize size, uint32_t data) {
  // "Command Buffers": "`vkCmdFillBuffer` has the same alignment rule" as
  // `vkCmdUpdateBuffer` -- a 4-byte aligned offset and size.
  if (dstOffset % 4 != 0 || (size != VK_WHOLE_SIZE && size % 4 != 0))
    return;
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->fillBuffer(fromHandle<vulkan::Buffer>(dstBuffer), dstOffset, size,
                  data);
}

VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(VkCommandBuffer commandBuffer,
                                             VkBuffer dstBuffer,
                                             VkDeviceSize dstOffset,
                                             VkDeviceSize dataSize,
                                             const void *pData) {
  // "Command Buffers": "`vkCmdUpdateBuffer` is capped at 65536 bytes and
  // requires 4-byte aligned offset and size".
  if (dataSize == 0 || dataSize > 65536 || dstOffset % 4 != 0 ||
      dataSize % 4 != 0)
    return;
  const auto *Bytes = static_cast<const uint8_t *>(pData);
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->updateBuffer(fromHandle<vulkan::Buffer>(dstBuffer), dstOffset,
                    std::vector<uint8_t>(Bytes, Bytes + dataSize));
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
    VkDependencyFlags, uint32_t, const VkMemoryBarrier *, uint32_t,
    const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *) {
  // Image memory barriers are unreachable today (no VkImage exists yet, V5),
  // so nothing here needs to inspect the barrier arrays themselves -- see
  // `CommandBuffer::pipelineBarrier`'s own comment for why this is a plain
  // join marker.
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->pipelineBarrier();
}

} // namespace feme::vulkan
