//===- CommandBuffer.h - VkCommandPool/VkCommandBuffer ------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The V1 command-buffer object model (see "Command Buffers" in
// feme/docs/FeMeVulkanDesign.md). V1's own command set is restricted to
// exactly what the roadmap's V1 bullet asks for: bind compute pipeline,
// dispatch, dispatch base, and dispatch indirect (buffer copies, barriers,
// queries, events, and secondary command buffers are V2 and later).
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

/// One recorded command. A compact tagged record rather than a class
/// hierarchy, matching "Command Buffers": "record a compact typed stream".
struct RecordedCommand {
  enum class Kind {
    BindPipeline,
    Dispatch,
    DispatchBase,
    DispatchIndirect,
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
    Commands.push_back(RecordedCommand{RecordedCommand::Kind::BindPipeline,
                                       Pipeline});
  }
  void dispatch(std::array<uint32_t, 3> Count) {
    RecordedCommand Cmd{RecordedCommand::Kind::Dispatch};
    Cmd.Count = Count;
    Commands.push_back(Cmd);
  }
  void dispatchBase(std::array<uint32_t, 3> Base,
                    std::array<uint32_t, 3> Count) {
    RecordedCommand Cmd{RecordedCommand::Kind::DispatchBase};
    Cmd.Base = Base;
    Cmd.Count = Count;
    Commands.push_back(Cmd);
  }
  void dispatchIndirect(Buffer *IndirectBuffer, uint64_t Offset) {
    RecordedCommand Cmd{RecordedCommand::Kind::DispatchIndirect};
    Cmd.IndirectBuffer = IndirectBuffer;
    Cmd.IndirectOffset = Offset;
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
