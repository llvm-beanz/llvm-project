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
#include "QueryPool.h"
#include "Sync.h"

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
        // A uniform buffer's descriptor never carries the UAV flag (see
        // Descriptor.h's file comment); a storage buffer's always does --
        // it is always read-write.
        Dst.Flags = isReadOnlyDescriptorType(BindingDecl.Type)
                       ? 0
                       : feme::cpu::FEME_DESCRIPTOR_UAV;
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
                  llvm::ArrayRef<BoundSetState> BoundSets,
                  llvm::ArrayRef<uint8_t> PushConstants) {
  feme::cpu::CompiledStage &Stage = Pipeline.getStage();
  feme::cpu::StageArtifactInfo Artifact = Stage.getArtifactInfo();

  MaterializedBoundResources Materialized = buildBoundResources(BoundSets);
  feme::cpu::DispatchResources Resources;
  Resources.BoundResources = Materialized.Bindings;
  // Every dispatch snapshots the command buffer's current push-constant
  // bytes as `RootConstants`, regardless of whether this pipeline's shader
  // actually reads any of them (see "Descriptor Model": "Each dispatch
  // snapshots the bytes visible through its pipeline layout and passes
  // them as RootConstants"); a shader with no root-constant access simply
  // never reads through the pointer.
  Resources.RootConstants = PushConstants;
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
  VkDeviceSize ResolvedSize =
      Size == VK_WHOLE_SIZE ? Dst->size() - Offset : Size;
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

/// `vkCmdWaitEvents`: see "Queues, Scheduling, and Synchronization": "The
/// same join applies ... at `vkCmdWaitEvents`". Under this ICD's strictly
/// sequential execution model every event this could observe is already
/// in its final state (see `Event`'s own comment): one still unsignaled
/// here is a real application ordering error, exactly like an unsignaled
/// semaphore wait (see Sync.h's file comment).
Error runWaitEvents(llvm::ArrayRef<Event *> Events) {
  for (Event *Ev : Events)
    if (!Ev || !Ev->isSignaled())
      return createStringError(inconvertibleErrorCode(),
                               "vkCmdWaitEvents observed an unsignaled event");
  return Error::success();
}

/// `vkCmdCopyQueryPoolResults`: writes `[FirstQuery, FirstQuery+QueryCount)`
/// of \p Pool into \p Dst starting at \p DstOffset, honoring \p Flags
/// exactly as `vkGetQueryPoolResults` does (see QueryPool.h's file comment:
/// every value is zero).
Error runCopyQueryPoolResults(QueryPool *Pool, uint32_t FirstQuery,
                              uint32_t QueryCount, Buffer *Dst,
                              VkDeviceSize DstOffset, VkDeviceSize Stride,
                              VkQueryResultFlags Flags) {
  if (!Pool)
    return createStringError(inconvertibleErrorCode(),
                             "copy query pool results with no query pool");
  if (!Dst || !Dst->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "copy query pool results destination is not "
                             "bound");
  bool Is64Bit = (Flags & VK_QUERY_RESULT_64_BIT) != 0;
  bool WithAvailability = (Flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != 0;
  VkDeviceSize ResultWidth = Is64Bit ? sizeof(uint64_t) : sizeof(uint32_t);
  for (uint32_t I = 0; I != QueryCount; ++I) {
    VkDeviceSize Offset = DstOffset + Stride * I;
    VkDeviceSize EntrySize = ResultWidth * (WithAvailability ? 2 : 1);
    if (Offset + EntrySize > Dst->size())
      return createStringError(inconvertibleErrorCode(),
                               "copy query pool results region is out of "
                               "range");
    auto *Out = static_cast<uint8_t *>(Dst->data()) + Offset;
    std::memset(Out, 0, ResultWidth); // Every value this ICD ever writes is
                                     // zero (see QueryPool.h's file
                                     // comment).
    if (WithAvailability) {
      uint64_t AvailFlag = Pool->isAvailable(FirstQuery + I) ? 1 : 0;
      if (Is64Bit)
        std::memcpy(Out + ResultWidth, &AvailFlag, sizeof(AvailFlag));
      else {
        uint32_t AvailFlag32 = static_cast<uint32_t>(AvailFlag);
        std::memcpy(Out + ResultWidth, &AvailFlag32, sizeof(AvailFlag32));
      }
    }
  }
  return Error::success();
}

/// Interprets \p Commands into \p BoundPipeline/\p BoundSets/
/// \p PushConstants -- shared, mutable execution state a primary command
/// buffer's own commands and every `vkCmdExecuteCommands`-referenced
/// secondary command buffer's commands are interpreted into alike, per
/// "Command Buffers": "Secondary command buffers are interpreted into the
/// primary execution state ... no cursor or bound state may be stored back
/// into the command buffer during execution." \p DeviceInfo is threaded
/// through for `validateGroupCount`, which does not otherwise have access
/// to a secondary command buffer's own (possibly null, if never set)
/// `PhysicalDeviceInfo`.
Error executeCommandsInto(llvm::ArrayRef<RecordedCommand> Commands,
                          const PhysicalDeviceInfo *DeviceInfo,
                          ComputePipeline *&BoundPipeline,
                          std::vector<BoundSetState> &BoundSets,
                          std::vector<uint8_t> &PushConstants) {
  for (const RecordedCommand &Cmd : Commands) {
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
      if (Error E = validateGroupCount(DeviceInfo, Cmd.Count))
        return E;
      if (Error E = runDispatch(*BoundPipeline, Cmd.Base, Cmd.Count, BoundSets,
                                PushConstants))
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
      if (Error E = validateGroupCount(DeviceInfo, Count))
        return E;
      if (Error E = runDispatch(*BoundPipeline, {0, 0, 0}, Count, BoundSets,
                                PushConstants))
        return E;
      break;
    }
    case RecordedCommand::Kind::CopyBuffer:
      if (Error E =
              runCopyBuffer(Cmd.SrcBuffer, Cmd.DstBuffer, Cmd.CopyRegions))
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
    case RecordedCommand::Kind::PushConstants:
      if (Cmd.DstOffset + Cmd.UpdateData.size() > PushConstants.size())
        return createStringError(inconvertibleErrorCode(),
                                 "push constant range is out of range of "
                                 "maxPushConstantsSize");
      std::memcpy(PushConstants.data() + Cmd.DstOffset, Cmd.UpdateData.data(),
                  Cmd.UpdateData.size());
      break;
    case RecordedCommand::Kind::SetEvent:
      Cmd.Events[0]->set();
      break;
    case RecordedCommand::Kind::ResetEvent:
      Cmd.Events[0]->reset();
      break;
    case RecordedCommand::Kind::WaitEvents:
      if (Error E = runWaitEvents(Cmd.Events))
        return E;
      break;
    case RecordedCommand::Kind::ResetQueryPool:
      Cmd.TargetQueryPool->reset(Cmd.FirstQuery, Cmd.Count[0]);
      break;
    case RecordedCommand::Kind::BeginQuery:
      // See QueryPool.h's file comment: there is no real counter to start
      // sampling, so beginning a query has nothing to record; only ending
      // one (or a timestamp write) marks it available.
      break;
    case RecordedCommand::Kind::EndQuery:
      Cmd.TargetQueryPool->markAvailable(Cmd.FirstQuery);
      break;
    case RecordedCommand::Kind::WriteTimestamp:
      Cmd.TargetQueryPool->markAvailable(Cmd.FirstQuery);
      break;
    case RecordedCommand::Kind::CopyQueryPoolResults:
      if (Error E = runCopyQueryPoolResults(
              Cmd.TargetQueryPool, Cmd.FirstQuery, Cmd.Count[0], Cmd.DstBuffer,
              Cmd.DstOffset, Cmd.DstSize, Cmd.FillData))
        return E;
      break;
    case RecordedCommand::Kind::ExecuteCommands:
      for (const CommandBuffer *Secondary : Cmd.SecondaryBuffers)
        if (Error E =
                executeCommandsInto(Secondary->commands(), DeviceInfo,
                                    BoundPipeline, BoundSets, PushConstants))
          return E;
      break;
    }
  }
  return Error::success();
}

} // namespace

llvm::Error feme::vulkan::executeCommandBuffer(const CommandBuffer &CmdBuf) {
  ComputePipeline *BoundPipeline = nullptr;
  std::vector<BoundSetState> BoundSets;
  // Push-constant state, sized to the device's full advertised
  // `maxPushConstantsSize` and zero-initialized: a byte a `vkCmdPushConstants`
  // never wrote reads as zero, matching every other "declared but never
  // written" resource in this ICD (see "Descriptor Model").
  const PhysicalDeviceInfo *DeviceInfo = CmdBuf.getPhysicalDeviceInfo();
  std::vector<uint8_t> PushConstants(
      DeviceInfo ? DeviceInfo->Properties.limits.maxPushConstantsSize : 0, 0);
  return executeCommandsInto(CmdBuf.commands(), DeviceInfo, BoundPipeline,
                             BoundSets, PushConstants);
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
  // V3: secondary command buffers (see "Command Buffers").
  auto *Pool = fromHandle<vulkan::CommandPool>(pAllocateInfo->commandPool);
  for (uint32_t I = 0; I != pAllocateInfo->commandBufferCount; ++I)
    pCommandBuffers[I] =
        toHandle<VkCommandBuffer>(Pool->allocate(pAllocateInfo->level));
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

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer commandBuffer,
                                              VkPipelineLayout, uint32_t,
                                              uint32_t offset, uint32_t size,
                                              const void *pValues) {
  // The Vulkan specification requires both a 4-byte-aligned offset and
  // size (`VUID-vkCmdPushConstants-offset-00368`/`-size-00369`); `layout`
  // and `stageFlags` need no validation here -- V3's single compute stage
  // means every push constant is compute-visible, and coverage against the
  // pipeline layout's declared ranges is instead checked once, at
  // `vkCreateComputePipelines` time (see `pushConstantsCoverRootConstantSize`
  // in Pipeline.cpp), not per push here.
  if (size == 0 || offset % 4 != 0 || size % 4 != 0)
    return;
  const auto *Bytes = static_cast<const uint8_t *>(pValues);
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->pushConstants(offset, std::vector<uint8_t>(Bytes, Bytes + size));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(VkCommandBuffer commandBuffer,
                                         VkEvent event, VkPipelineStageFlags) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setEvent(fromHandle<Event>(event));
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(VkCommandBuffer commandBuffer,
                                           VkEvent event,
                                           VkPipelineStageFlags) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->resetEvent(fromHandle<Event>(event));
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(
    VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
    VkPipelineStageFlags, VkPipelineStageFlags, uint32_t,
    const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *,
    uint32_t, const VkImageMemoryBarrier *) {
  // Image/buffer memory barriers need no inspection here for the same
  // reason `vkCmdPipelineBarrier` does not -- see that command's own
  // comment.
  std::vector<Event *> Events;
  Events.reserve(eventCount);
  for (uint32_t I = 0; I != eventCount; ++I)
    Events.push_back(fromHandle<Event>(pEvents[I]));
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->waitEvents(std::move(Events));
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer commandBuffer,
                                               VkQueryPool queryPool,
                                               uint32_t firstQuery,
                                               uint32_t queryCount) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->resetQueryPool(fromHandle<QueryPool>(queryPool), firstQuery,
                       queryCount);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer commandBuffer,
                                          VkQueryPool queryPool,
                                          uint32_t query,
                                          VkQueryControlFlags) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->beginQuery(fromHandle<QueryPool>(queryPool), query);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer commandBuffer,
                                        VkQueryPool queryPool,
                                        uint32_t query) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->endQuery(fromHandle<QueryPool>(queryPool), query);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer commandBuffer,
                                              VkPipelineStageFlagBits,
                                              VkQueryPool queryPool,
                                              uint32_t query) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->writeTimestamp(fromHandle<QueryPool>(queryPool), query);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(
    VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
    uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset,
    VkDeviceSize stride, VkQueryResultFlags flags) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->copyQueryPoolResults(fromHandle<QueryPool>(queryPool), firstQuery,
                            queryCount, fromHandle<vulkan::Buffer>(dstBuffer),
                            dstOffset, stride, flags);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdExecuteCommands(VkCommandBuffer commandBuffer,
                     uint32_t commandBufferCount,
                     const VkCommandBuffer *pCommandBuffers) {
  std::vector<const vulkan::CommandBuffer *> Secondary;
  Secondary.reserve(commandBufferCount);
  for (uint32_t I = 0; I != commandBufferCount; ++I)
    Secondary.push_back(fromHandle<vulkan::CommandBuffer>(pCommandBuffers[I]));
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->executeCommands(std::move(Secondary));
}

} // namespace feme::vulkan
