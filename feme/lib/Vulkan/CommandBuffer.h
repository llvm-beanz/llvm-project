//===- CommandBuffer.h - VkCommandPool/VkCommandBuffer ------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The command-buffer object model (see "Command Buffers" in
// feme/docs/FeMeVulkanDesign.md). V1 restricted the command set to bind
// compute pipeline, dispatch, dispatch base, and dispatch indirect; V2
// ("Storage buffers and descriptors") adds bind descriptor sets (with
// dynamic offsets), buffer copy/fill/update, and pipeline barriers. V3
// ("Uniform data, push constants, and synchronization") adds push
// constants, events, query pools, and secondary command buffers.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_COMMANDBUFFER_H
#define FEME_LIB_VULKAN_COMMANDBUFFER_H

#include "Icd.h"
#include "PhysicalDeviceInfo.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace feme::vulkan {

class Buffer;
class CommandBuffer;
class ComputePipeline;
class DescriptorSet;
class Event;
class QueryPool;

/// One recorded command. A compact tagged record rather than a class
/// hierarchy, matching "Command Buffers": "record a compact typed stream".
struct RecordedCommand {
  enum class Kind {
    BindPipeline,
    BindDescriptorSets,
    Dispatch,
    DispatchBase,
    DispatchIndirect,
    CopyBuffer,
    FillBuffer,
    UpdateBuffer,
    PipelineBarrier,
    PushConstants,
    SetEvent,
    ResetEvent,
    WaitEvents,
    ResetQueryPool,
    BeginQuery,
    EndQuery,
    WriteTimestamp,
    CopyQueryPoolResults,
    ExecuteCommands,
  };

  Kind Op;
  /// `BindPipeline`: the pipeline to bind.
  ComputePipeline *Pipeline = nullptr;
  /// `Dispatch`/`DispatchBase`: the group-id base (`{0,0,0}` for a plain
  /// `vkCmdDispatch`) and group count.
  std::array<uint32_t, 3> Base{0, 0, 0};
  std::array<uint32_t, 3> Count{0, 0, 0};
  /// `DispatchIndirect`: the buffer/offset a `VkDispatchIndirectCommand`'s
  /// three `uint32_t`s are read from at execution time.
  Buffer *IndirectBuffer = nullptr;
  uint64_t IndirectOffset = 0;
  /// `BindDescriptorSets`: the first bound set index, the sets themselves,
  /// and the flat dynamic-offset array consumed across them in ascending
  /// (set, binding) order (see `DescriptorSetLayout::dynamicOffsetCount`).
  uint32_t FirstSet = 0;
  std::vector<DescriptorSet *> DescriptorSets;
  std::vector<uint32_t> DynamicOffsets;
  /// `CopyBuffer`: source/destination buffers and the copy regions.
  Buffer *SrcBuffer = nullptr;
  Buffer *DstBuffer = nullptr;
  std::vector<VkBufferCopy> CopyRegions;
  /// `FillBuffer`/`UpdateBuffer`: destination offset/size (`UpdateBuffer`'s
  /// payload is captured below; `FillBuffer`'s repeating word is `FillData`).
  /// `DstBuffer` above is shared by both.
  VkDeviceSize DstOffset = 0;
  VkDeviceSize DstSize = 0;
  uint32_t FillData = 0;
  /// `UpdateBuffer`: an owned copy of the source data, captured at record
  /// time (see "Command Buffers": "owned copies of variable-sized data
  /// where Vulkan requires recording-time capture").
  std::vector<uint8_t> UpdateData;
  /// `PushConstants`: the byte range `vkCmdPushConstants` writes into the
  /// command buffer's push-constant state (`DstOffset` reused as the byte
  /// offset, `UpdateData` reused as the owned payload copy -- see "Command
  /// Buffers": "Push constants" is its own row of the first command set,
  /// but needs no new payload shape beyond what `UpdateBuffer` already
  /// carries).
  /// `SetEvent`/`ResetEvent`: the single target event.
  /// `WaitEvents`: every event this command waits on.
  std::vector<Event *> Events;
  /// `ResetQueryPool`/`BeginQuery`/`EndQuery`/`WriteTimestamp`/
  /// `CopyQueryPoolResults`: the target query pool.
  QueryPool *TargetQueryPool = nullptr;
  /// `ResetQueryPool`: `[FirstQuery, FirstQuery+QueryCount)` (`Count[0]`
  /// reused for `QueryCount`). `BeginQuery`/`EndQuery`/`WriteTimestamp`: the
  /// single query index (`FirstQuery`). `CopyQueryPoolResults`: the same
  /// range as `ResetQueryPool`, plus `DstBuffer`/`DstOffset` (reused above)
  /// and `DstSize` (reused for `stride`) and `FillData` (reused for
  /// `VkQueryResultFlags`).
  uint32_t FirstQuery = 0;
  /// `ExecuteCommands`: the secondary command buffers to interpret into
  /// this (primary) command buffer's own execution state, in order (see
  /// "Command Buffers": "Secondary command buffers are interpreted into
  /// the primary execution state").
  std::vector<const CommandBuffer *> SecondaryBuffers;
};

/// A `VkCommandBuffer`: an append-only typed command stream while
/// recording (see "Command Buffers"). Dispatchable, since the loader
/// intercepts every `vkCmd*`/`vkBeginCommandBuffer` call through its own
/// per-command-buffer dispatch table.
class CommandBuffer : public DispatchableBase {
public:
  explicit CommandBuffer(VkCommandBufferLevel Level = VK_COMMAND_BUFFER_LEVEL_PRIMARY)
      : Level(Level) {}

  VkCommandBufferLevel level() const { return Level; }

  void begin() {
    Commands.clear();
    Recording = true;
  }
  void end() { Recording = false; }
  /// `vkResetCommandBuffer`/pool-wide reset: drops every recorded command,
  /// matching Vulkan's "reset to the initial state" semantics.
  void reset() {
    Commands.clear();
    Recording = false;
  }

  bool isRecording() const { return Recording; }

  void bindPipeline(ComputePipeline *Pipeline) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::BindPipeline;
    Cmd.Pipeline = Pipeline;
    Commands.push_back(Cmd);
  }
  void bindDescriptorSets(uint32_t FirstSet, std::vector<DescriptorSet *> Sets,
                          std::vector<uint32_t> DynamicOffsets) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::BindDescriptorSets;
    Cmd.FirstSet = FirstSet;
    Cmd.DescriptorSets = std::move(Sets);
    Cmd.DynamicOffsets = std::move(DynamicOffsets);
    Commands.push_back(std::move(Cmd));
  }
  void dispatch(std::array<uint32_t, 3> Count) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::Dispatch;
    Cmd.Count = Count;
    Commands.push_back(Cmd);
  }
  void dispatchBase(std::array<uint32_t, 3> Base,
                    std::array<uint32_t, 3> Count) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::DispatchBase;
    Cmd.Base = Base;
    Cmd.Count = Count;
    Commands.push_back(Cmd);
  }
  void dispatchIndirect(Buffer *IndirectBuffer, uint64_t Offset) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::DispatchIndirect;
    Cmd.IndirectBuffer = IndirectBuffer;
    Cmd.IndirectOffset = Offset;
    Commands.push_back(Cmd);
  }
  void copyBuffer(Buffer *Src, Buffer *Dst, std::vector<VkBufferCopy> Regions) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::CopyBuffer;
    Cmd.SrcBuffer = Src;
    Cmd.DstBuffer = Dst;
    Cmd.CopyRegions = std::move(Regions);
    Commands.push_back(std::move(Cmd));
  }
  void fillBuffer(Buffer *Dst, VkDeviceSize Offset, VkDeviceSize Size,
                  uint32_t Data) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::FillBuffer;
    Cmd.DstBuffer = Dst;
    Cmd.DstOffset = Offset;
    Cmd.DstSize = Size;
    Cmd.FillData = Data;
    Commands.push_back(Cmd);
  }
  void updateBuffer(Buffer *Dst, VkDeviceSize Offset,
                    std::vector<uint8_t> Data) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::UpdateBuffer;
    Cmd.DstBuffer = Dst;
    Cmd.DstOffset = Offset;
    Cmd.UpdateData = std::move(Data);
    Commands.push_back(std::move(Cmd));
  }
  /// `vkCmdPipelineBarrier`: see "Queues, Scheduling, and Synchronization"'s
  /// join semantics. Recorded as a plain marker with no payload: this
  /// milestone's execution model already runs every command to completion
  /// strictly in record order on a single thread (see `runDispatch`'s own
  /// comment), so every earlier command's effects are always visible to
  /// every later one -- a barrier's join is therefore already satisfied by
  /// construction, and this command exists so applications that correctly
  /// insert one (as the specification requires) are not rejected.
  void pipelineBarrier() {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::PipelineBarrier;
    Commands.push_back(Cmd);
  }
  /// `vkCmdPushConstants`: records \p Offset and an owned copy of \p Data,
  /// consumed at execution time into the command buffer's push-constant
  /// state (see "Descriptor Model": "Push constants are copied into
  /// command-buffer state by `vkCmdPushConstants`").
  void pushConstants(uint32_t Offset, std::vector<uint8_t> Data) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::PushConstants;
    Cmd.DstOffset = Offset;
    Cmd.UpdateData = std::move(Data);
    Commands.push_back(std::move(Cmd));
  }
  /// `vkCmdSetEvent`/`vkCmdResetEvent`.
  void setEvent(Event *Ev) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::SetEvent;
    Cmd.Events.push_back(Ev);
    Commands.push_back(std::move(Cmd));
  }
  void resetEvent(Event *Ev) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::ResetEvent;
    Cmd.Events.push_back(Ev);
    Commands.push_back(std::move(Cmd));
  }
  /// `vkCmdWaitEvents`: see "Queues, Scheduling, and Synchronization": "The
  /// same join applies ... at `vkCmdWaitEvents`" -- already satisfied by
  /// this milestone's strictly-sequential execution, exactly like
  /// `pipelineBarrier`, so the memory-barrier arrays a real
  /// `vkCmdWaitEvents` call also carries need no payload here either.
  void waitEvents(std::vector<Event *> Events) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::WaitEvents;
    Cmd.Events = std::move(Events);
    Commands.push_back(std::move(Cmd));
  }
  /// `vkCmdResetQueryPool`.
  void resetQueryPool(QueryPool *Pool, uint32_t FirstQuery,
                      uint32_t QueryCount) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::ResetQueryPool;
    Cmd.TargetQueryPool = Pool;
    Cmd.FirstQuery = FirstQuery;
    Cmd.Count[0] = QueryCount;
    Commands.push_back(Cmd);
  }
  /// `vkCmdBeginQuery`.
  void beginQuery(QueryPool *Pool, uint32_t Query) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::BeginQuery;
    Cmd.TargetQueryPool = Pool;
    Cmd.FirstQuery = Query;
    Commands.push_back(Cmd);
  }
  /// `vkCmdEndQuery`.
  void endQuery(QueryPool *Pool, uint32_t Query) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::EndQuery;
    Cmd.TargetQueryPool = Pool;
    Cmd.FirstQuery = Query;
    Commands.push_back(Cmd);
  }
  /// `vkCmdWriteTimestamp`.
  void writeTimestamp(QueryPool *Pool, uint32_t Query) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::WriteTimestamp;
    Cmd.TargetQueryPool = Pool;
    Cmd.FirstQuery = Query;
    Commands.push_back(Cmd);
  }
  /// `vkCmdCopyQueryPoolResults`.
  void copyQueryPoolResults(QueryPool *Pool, uint32_t FirstQuery,
                            uint32_t QueryCount, Buffer *Dst,
                            VkDeviceSize DstOffset, VkDeviceSize Stride,
                            VkQueryResultFlags Flags) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::CopyQueryPoolResults;
    Cmd.TargetQueryPool = Pool;
    Cmd.FirstQuery = FirstQuery;
    Cmd.Count[0] = QueryCount;
    Cmd.DstBuffer = Dst;
    Cmd.DstOffset = DstOffset;
    Cmd.DstSize = Stride;
    Cmd.FillData = Flags;
    Commands.push_back(Cmd);
  }
  /// `vkCmdExecuteCommands`: see "Command Buffers": "Secondary command
  /// buffers are interpreted into the primary execution state." The
  /// secondary buffers' own recorded streams are referenced, not copied --
  /// they must stay alive and unmodified (simultaneous-use requires
  /// immutable command streams, per that same section) through this
  /// (primary) buffer's own execution.
  void executeCommands(std::vector<const CommandBuffer *> Secondary) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::ExecuteCommands;
    Cmd.SecondaryBuffers = std::move(Secondary);
    Commands.push_back(std::move(Cmd));
  }

  llvm::ArrayRef<RecordedCommand> commands() const { return Commands; }

  void setPhysicalDeviceInfo(const PhysicalDeviceInfo *Info) {
    this->Info = Info;
  }
  const PhysicalDeviceInfo *getPhysicalDeviceInfo() const { return Info; }

private:
  std::vector<RecordedCommand> Commands;
  bool Recording = false;
  const PhysicalDeviceInfo *Info = nullptr;
  VkCommandBufferLevel Level;
};

/// A `VkCommandPool`: allocator and reset domain for the command buffers
/// allocated from it (see "Object Model"). Owns every `CommandBuffer`
/// allocated from it, matching Vulkan's pool-owns-its-buffers lifetime.
class CommandPool {
public:
  explicit CommandPool(const PhysicalDeviceInfo &Info) : Info(&Info) {}

  CommandBuffer *allocate(VkCommandBufferLevel Level) {
    auto Buf = std::make_unique<CommandBuffer>(Level);
    Buf->setPhysicalDeviceInfo(Info);
    CommandBuffer *Result = Buf.get();
    Buffers.push_back(std::move(Buf));
    return Result;
  }

  void free(CommandBuffer *Buf) {
    llvm::erase_if(Buffers, [&](const std::unique_ptr<CommandBuffer> &Owned) {
      return Owned.get() == Buf;
    });
  }

  /// `vkResetCommandPool`: resets every command buffer allocated from this
  /// pool to the initial state, without freeing them (see "Object Model":
  /// "Accounting and storage ownership").
  void reset() {
    for (auto &Buf : Buffers)
      Buf->reset();
  }

private:
  const PhysicalDeviceInfo *Info;
  std::vector<std::unique_ptr<CommandBuffer>> Buffers;
};

/// Executes \p CmdBuf's recorded commands in order, per "Queues,
/// Scheduling, and Synchronization": queue order -> submission order ->
/// command-buffer order -> command order. Each dispatch command's group
/// dimensions are validated against `maxComputeWorkGroupCount` here, at
/// execution time uniformly for direct, base, and indirect dispatch (see
/// "Command Buffers": "validates indirect arguments if applicable"), since
/// an indirect dispatch's counts are not known until its source buffer is
/// read.
llvm::Error executeCommandBuffer(const CommandBuffer &CmdBuf);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_COMMANDBUFFER_H
