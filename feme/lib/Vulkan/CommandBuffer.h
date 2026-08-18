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
// dynamic offsets), buffer copy/fill/update, and pipeline barriers.
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
class ComputePipeline;
class DescriptorSet;

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
};

/// A `VkCommandBuffer`: an append-only typed command stream while
/// recording (see "Command Buffers"). Dispatchable, since the loader
/// intercepts every `vkCmd*`/`vkBeginCommandBuffer` call through its own
/// per-command-buffer dispatch table.
class CommandBuffer : public DispatchableBase {
public:
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

  llvm::ArrayRef<RecordedCommand> commands() const { return Commands; }

  void setPhysicalDeviceInfo(const PhysicalDeviceInfo *Info) {
    this->Info = Info;
  }
  const PhysicalDeviceInfo *getPhysicalDeviceInfo() const { return Info; }

private:
  std::vector<RecordedCommand> Commands;
  bool Recording = false;
  const PhysicalDeviceInfo *Info = nullptr;
};

/// A `VkCommandPool`: allocator and reset domain for the command buffers
/// allocated from it (see "Object Model"). Owns every `CommandBuffer`
/// allocated from it, matching Vulkan's pool-owns-its-buffers lifetime.
class CommandPool {
public:
  explicit CommandPool(const PhysicalDeviceInfo &Info) : Info(&Info) {}

  CommandBuffer *allocate() {
    auto Buf = std::make_unique<CommandBuffer>();
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
