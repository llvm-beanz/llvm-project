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
// constants, events, query pools, and secondary command buffers. V5
// ("Images and sampling") adds `vkCmdCopyBufferToImage`/
// `vkCmdCopyImageToBuffer`/`vkCmdCopyImage`, and gives
// `vkCmdPipelineBarrier` real payload for the first time: an image memory
// barrier's layout transition (see `ImageLayoutTransition`).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_COMMANDBUFFER_H
#define FEME_LIB_VULKAN_COMMANDBUFFER_H

#include "Icd.h"
#include "PhysicalDeviceInfo.h"
#include "RenderPass.h"

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
class Framebuffer;
class GraphicsPipeline;
class Image;
class ImageView;
class Pipeline;
class QueryPool;
class RenderPass;

/// (V5) One `VkImageMemoryBarrier`'s layout-transition payload, recorded by
/// `vkCmdPipelineBarrier` and applied to its target `Image`'s tracked
/// per-subresource layout at execution time (see `Image::setLayout` and
/// Image.h's file comment on why this is bookkeeping only, not a
/// precondition re-checked by a later command).
struct ImageLayoutTransition {
  Image *Img = nullptr;
  VkImageLayout OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageLayout NewLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageSubresourceRange Range{};
};

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
    CopyBufferToImage,
    CopyImageToBuffer,
    CopyImage,
    BeginRenderPass,
    BeginRendering,
    NextSubpass,
    EndRenderPass,
    BindVertexBuffers,
    BindIndexBuffer,
    SetViewport,
    SetScissor,
    SetBlendConstants,
    SetStencilReference,
    SetStencilCompareMask,
    SetStencilWriteMask,
    SetCullMode,
    SetFrontFace,
    SetDepthTestEnable,
    SetDepthWriteEnable,
    SetDepthCompareOp,
    SetDepthBoundsTestEnable,
    SetStencilTestEnable,
    SetStencilOp,
    SetPrimitiveTopology,
    Draw,
    DrawIndexed,
    DrawIndirect,
    DrawIndexedIndirect,
    ClearColorImage,
    ClearDepthStencilImage,
    ClearAttachments,
    BlitImage,
    ResolveImage,
  };

  Kind Op;
  /// `BindPipeline`: the pipeline to bind, of either bind point.
  vulkan::Pipeline *Pipeline = nullptr;
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
  /// (V5) `PipelineBarrier`: every `VkImageMemoryBarrier`'s layout
  /// transition this call carries, applied to its target image's tracked
  /// layout at execution time (see `ImageLayoutTransition`'s comment). The
  /// buffer/memory barrier arrays carry no payload here, for the same
  /// reason the barrier itself did not before V5 -- see `pipelineBarrier`'s
  /// comment.
  std::vector<ImageLayoutTransition> ImageBarriers;
  /// (V5) `CopyBufferToImage`/`CopyImageToBuffer`: the buffer half of the
  /// copy (`SrcBuffer` reused for `CopyImageToBuffer`'s source buffer,
  /// `DstBuffer` reused for `CopyBufferToImage`'s destination buffer). The
  /// image half is `SrcImage`/`DstImage` below.
  Image *SrcImage = nullptr;
  Image *DstImage = nullptr;
  std::vector<VkBufferImageCopy> BufferImageCopyRegions;
  /// (V5) `CopyImage`: the copy regions between `SrcImage` and `DstImage`.
  std::vector<VkImageCopy> ImageCopyRegions;
  /// (V6) `BeginRenderPass`: the render pass, the framebuffer supplying its
  /// attachments' views, the render area, and one clear value per
  /// attachment (consumed only by an attachment whose load op is
  /// `VK_ATTACHMENT_LOAD_OP_CLEAR`).
  const vulkan::RenderPass *BeginPass = nullptr;
  const vulkan::Framebuffer *BeginFramebuffer = nullptr;
  /// (Roadmap C6) `BeginRenderPass`'s own attachment views, when
  /// `BeginFramebuffer` is imageless (`VkRenderPassAttachmentBeginInfo`);
  /// empty otherwise, in which case `BeginFramebuffer->attachments()` is
  /// used instead (see `buildRenderTargetBinding`'s comment).
  std::vector<vulkan::ImageView *> BeginAttachments;
  VkRect2D RenderArea{};
  std::vector<VkClearValue> ClearValues;
  /// (V6) `BeginRendering`: the render-target binding `vkCmdBeginRendering`
  /// builds directly, with no render pass or framebuffer in between (see
  /// RenderPass.h: both entry points normalize into this one shape).
  RenderTargetBinding RenderingBinding;
  /// (V6) `BindVertexBuffers`: the buffers bound at `[FirstSet, FirstSet +
  /// VertexBuffers.size())` (`FirstSet` reused as `firstBinding`) and their
  /// byte offsets. `BindIndexBuffer` reuses `SrcBuffer`/`IndirectOffset`
  /// plus `IndexType` below.
  std::vector<Buffer *> VertexBuffers;
  std::vector<VkDeviceSize> VertexBufferOffsets;
  /// (roadmap C4c) `vkCmdBindVertexBuffers2EXT`'s optional `pStrides`:
  /// empty when unused (a plain `vkCmdBindVertexBuffers`, or
  /// `vkCmdBindVertexBuffers2EXT` called with `pStrides == nullptr`),
  /// otherwise one entry per `VertexBuffers` slot.
  std::vector<VkDeviceSize> VertexBufferStrides;
  VkIndexType IndexType = VK_INDEX_TYPE_UINT32;
  /// (V6) `ClearColorImage`/`ClearDepthStencilImage`: the cleared value
  /// (`ClearValues[0]`, shared with `BeginRenderPass`'s own list) and the
  /// subresource ranges it covers; the target image is `DstImage`.
  std::vector<VkImageSubresourceRange> ClearRanges;
  /// (V6) `ClearAttachments`: the attachments to clear and the rectangles
  /// to clear them over.
  std::vector<VkClearAttachment> ClearAttachments;
  std::vector<VkClearRect> ClearRects;
  /// (V6) `BlitImage`/`ResolveImage`: the regions between `SrcImage` and
  /// `DstImage`, plus a blit's filter.
  std::vector<VkImageBlit> BlitRegions;
  std::vector<VkImageResolve> ResolveRegions;
  VkFilter BlitFilter = VK_FILTER_NEAREST;
  /// (V6) `SetViewport`/`SetScissor`/`SetBlendConstants`/`SetStencil*`: the
  /// dynamic state this command records, snapshotted into the next draw
  /// (see "Dynamic state is what makes the prepared draw a snapshot").
  VkViewport ViewportValue{};
  VkRect2D ScissorValue{};
  std::array<float, 4> BlendConstants{0.0f, 0.0f, 0.0f, 0.0f};
  VkStencilFaceFlags StencilFaceMask = 0;
  uint32_t StencilValue = 0;
  /// (roadmap C4c) `SetCullMode`/`SetFrontFace`:
  /// `VK_EXT_extended_dynamic_state`'s `vkCmdSetCullModeEXT`/
  /// `vkCmdSetFrontFaceEXT`, the same dynamic-state-snapshot shape as
  /// `SetStencil*` above.
  VkCullModeFlags CullModeValue = VK_CULL_MODE_NONE;
  VkFrontFace FrontFaceValue = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  /// (roadmap C4c) `SetDepthTestEnable`/`SetDepthWriteEnable`/
  /// `SetDepthBoundsTestEnable`: `vkCmdSetDepthTestEnableEXT`/
  /// `vkCmdSetDepthWriteEnableEXT`/`vkCmdSetDepthBoundsTestEnableEXT`'s
  /// boolean payload (`Bool32Value`, shared across the three since only
  /// one is ever meaningful per recorded command). `SetDepthCompareOp`:
  /// `vkCmdSetDepthCompareOpEXT`'s raw `VkCompareOp` (`DepthCompareOpValue`,
  /// mapped through the same `mapCompareOp` the static path uses when this
  /// command replays into `Gfx.Dynamic`).
  VkBool32 Bool32Value = VK_FALSE;
  VkCompareOp DepthCompareOpValue = VK_COMPARE_OP_ALWAYS;
  /// (roadmap C4c) `SetStencilOp`: `vkCmdSetStencilOpEXT`'s payload, for
  /// the faces named by `StencilFaceMask` (reused from `SetStencil*`
  /// above). `SetStencilTestEnable` reuses `Bool32Value`.
  VkStencilOp StencilFailOpValue = VK_STENCIL_OP_KEEP;
  VkStencilOp StencilPassOpValue = VK_STENCIL_OP_KEEP;
  VkStencilOp StencilDepthFailOpValue = VK_STENCIL_OP_KEEP;
  VkCompareOp StencilCompareOpValue = VK_COMPARE_OP_ALWAYS;
  /// (roadmap C4c) `SetPrimitiveTopology`: `vkCmdSetPrimitiveTopologyEXT`'s
  /// raw payload, mapped through the same triangle-class-only conversion
  /// `mapTopology` (GraphicsPipeline.cpp) uses statically when this
  /// command replays into `Gfx.Dynamic`.
  VkPrimitiveTopology PrimitiveTopologyValue =
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  /// (V6) `Draw`/`DrawIndexed`: the draw's own arguments, in the same shape
  /// `feme::graphics::DrawCommand` uses (`FirstQuery` above is reused for
  /// neither -- a draw needs all six of these at once).
  uint32_t VertexOrIndexCount = 0;
  uint32_t InstanceCount = 1;
  uint32_t FirstVertexOrIndex = 0;
  uint32_t FirstInstance = 0;
  int32_t VertexOffset = 0;
};

/// A `VkCommandBuffer`: an append-only typed command stream while
/// recording (see "Command Buffers"). Dispatchable, since the loader
/// intercepts every `vkCmd*`/`vkBeginCommandBuffer` call through its own
/// per-command-buffer dispatch table.
class CommandBuffer : public DispatchableBase {
public:
  explicit CommandBuffer(
      VkCommandBufferLevel Level = VK_COMMAND_BUFFER_LEVEL_PRIMARY)
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

  void bindPipeline(vulkan::Pipeline *Pipeline) {
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
  /// join semantics. The buffer/memory barrier arrays carry no payload,
  /// since this milestone's execution model already runs every command to
  /// completion strictly in record order on a single thread (see
  /// `runDispatch`'s own comment), so every earlier command's effects are
  /// always visible to every later one -- a barrier's join is therefore
  /// already satisfied by construction, and this command exists so
  /// applications that correctly insert one (as the specification
  /// requires) are not rejected. (V5) \p ImageBarriers is not similarly
  /// elided: it carries real per-subresource layout-tracking state (see
  /// `ImageLayoutTransition`'s comment), applied to each target image at
  /// execution time.
  void pipelineBarrier(std::vector<ImageLayoutTransition> ImageBarriers) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::PipelineBarrier;
    Cmd.ImageBarriers = std::move(ImageBarriers);
    Commands.push_back(std::move(Cmd));
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
  /// (V5) `vkCmdCopyBufferToImage`.
  void copyBufferToImage(Buffer *Src, Image *Dst,
                         std::vector<VkBufferImageCopy> Regions) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::CopyBufferToImage;
    Cmd.SrcBuffer = Src;
    Cmd.DstImage = Dst;
    Cmd.BufferImageCopyRegions = std::move(Regions);
    Commands.push_back(std::move(Cmd));
  }
  /// (V5) `vkCmdCopyImageToBuffer`.
  void copyImageToBuffer(Image *Src, Buffer *Dst,
                         std::vector<VkBufferImageCopy> Regions) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::CopyImageToBuffer;
    Cmd.SrcImage = Src;
    Cmd.DstBuffer = Dst;
    Cmd.BufferImageCopyRegions = std::move(Regions);
    Commands.push_back(std::move(Cmd));
  }
  /// (V5) `vkCmdCopyImage`.
  void copyImage(Image *Src, Image *Dst, std::vector<VkImageCopy> Regions) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::CopyImage;
    Cmd.SrcImage = Src;
    Cmd.DstImage = Dst;
    Cmd.ImageCopyRegions = std::move(Regions);
    Commands.push_back(std::move(Cmd));
  }

  /// (V6) `vkCmdBeginRenderPass`/`vkCmdNextSubpass`/`vkCmdEndRenderPass`.
  /// The render pass and framebuffer are normalized into one
  /// `RenderTargetBinding` at execution time (see RenderPass.h), so a
  /// subpass boundary is a full join and nothing downstream distinguishes
  /// this from `vkCmdBeginRendering`. \p Attachments is only non-empty for
  /// an imageless framebuffer (roadmap C6); see `BeginAttachments`'s
  /// comment.
  void beginRenderPass(const vulkan::RenderPass *Pass,
                       const vulkan::Framebuffer *Fb, VkRect2D RenderArea,
                       std::vector<VkClearValue> ClearValues,
                       std::vector<vulkan::ImageView *> Attachments = {}) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::BeginRenderPass;
    Cmd.BeginPass = Pass;
    Cmd.BeginFramebuffer = Fb;
    Cmd.RenderArea = RenderArea;
    Cmd.ClearValues = std::move(ClearValues);
    Cmd.BeginAttachments = std::move(Attachments);
    Commands.push_back(std::move(Cmd));
  }
  /// (V6) `vkCmdBeginRendering`: the same render-target binding a
  /// `VkRenderPass` compiles into, supplied directly by the application.
  void beginRendering(RenderTargetBinding Binding) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::BeginRendering;
    Cmd.RenderingBinding = std::move(Binding);
    Commands.push_back(std::move(Cmd));
  }
  void nextSubpass() {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::NextSubpass;
    Commands.push_back(Cmd);
  }
  /// `vkCmdEndRenderPass` and `vkCmdEndRendering` alike: both end the one
  /// render-target binding in flight.
  void endRenderPass() {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::EndRenderPass;
    Commands.push_back(Cmd);
  }
  /// (V6) `vkCmdBindVertexBuffers`. \p Strides is empty unless called from
  /// `vkCmdBindVertexBuffers2EXT` with a non-null `pStrides` (roadmap C4c).
  void bindVertexBuffers(uint32_t FirstBinding, std::vector<Buffer *> Buffers,
                         std::vector<VkDeviceSize> Offsets,
                         std::vector<VkDeviceSize> Strides = {}) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::BindVertexBuffers;
    Cmd.FirstSet = FirstBinding;
    Cmd.VertexBuffers = std::move(Buffers);
    Cmd.VertexBufferOffsets = std::move(Offsets);
    Cmd.VertexBufferStrides = std::move(Strides);
    Commands.push_back(std::move(Cmd));
  }
  /// (V6) `vkCmdBindIndexBuffer`, and (roadmap E5) `vkCmdBindIndexBuffer2`'s
  /// `size`-bounded variant when \p Size is not `VK_WHOLE_SIZE` (`DstSize`
  /// reused, the same way it already is for `FillBuffer`'s size and
  /// `CopyQueryPoolResults`'/`DrawIndirect`'s stride).
  void bindIndexBuffer(Buffer *Buf, VkDeviceSize Offset, VkIndexType Type,
                       VkDeviceSize Size = VK_WHOLE_SIZE) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::BindIndexBuffer;
    Cmd.SrcBuffer = Buf;
    Cmd.IndirectOffset = Offset;
    Cmd.IndexType = Type;
    Cmd.DstSize = Size;
    Commands.push_back(Cmd);
  }
  /// (V6) `vkCmdSetViewport`/`vkCmdSetScissor`.
  void setViewport(const VkViewport &Viewport) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::SetViewport;
    Cmd.ViewportValue = Viewport;
    Commands.push_back(Cmd);
  }
  void setScissor(const VkRect2D &Scissor) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::SetScissor;
    Cmd.ScissorValue = Scissor;
    Commands.push_back(Cmd);
  }
  /// (V6) `vkCmdSetBlendConstants`.
  void setBlendConstants(std::array<float, 4> Constants) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::SetBlendConstants;
    Cmd.BlendConstants = Constants;
    Commands.push_back(Cmd);
  }
  /// (V6) `vkCmdSetStencilReference`/`vkCmdSetStencilCompareMask`/
  /// `vkCmdSetStencilWriteMask`, each applying to the faces named by
  /// \p FaceMask.
  void setStencilState(RecordedCommand::Kind Op, VkStencilFaceFlags FaceMask,
                       uint32_t Value) {
    RecordedCommand Cmd;
    Cmd.Op = Op;
    Cmd.StencilFaceMask = FaceMask;
    Cmd.StencilValue = Value;
    Commands.push_back(Cmd);
  }
  /// (roadmap C4c) `vkCmdSetCullModeEXT`/`vkCmdSetFrontFaceEXT`.
  void setCullMode(VkCullModeFlags Cull) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::SetCullMode;
    Cmd.CullModeValue = Cull;
    Commands.push_back(Cmd);
  }
  void setFrontFace(VkFrontFace Front) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::SetFrontFace;
    Cmd.FrontFaceValue = Front;
    Commands.push_back(Cmd);
  }
  /// (roadmap C4c) `vkCmdSetDepthTestEnableEXT`/`vkCmdSetDepthWriteEnableEXT`/
  /// `vkCmdSetDepthBoundsTestEnableEXT`, sharing one boolean-payload record
  /// shape distinguished only by \p Op.
  void setDepthBool(RecordedCommand::Kind Op, VkBool32 Value) {
    RecordedCommand Cmd;
    Cmd.Op = Op;
    Cmd.Bool32Value = Value;
    Commands.push_back(Cmd);
  }
  /// (roadmap C4c) `vkCmdSetDepthCompareOpEXT`.
  void setDepthCompareOp(VkCompareOp Op) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::SetDepthCompareOp;
    Cmd.DepthCompareOpValue = Op;
    Commands.push_back(Cmd);
  }
  /// (roadmap C4c) `vkCmdSetStencilTestEnableEXT`.
  void setStencilTestEnable(VkBool32 Enable) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::SetStencilTestEnable;
    Cmd.Bool32Value = Enable;
    Commands.push_back(Cmd);
  }
  /// (roadmap C4c) `vkCmdSetStencilOpEXT`, applying to the faces named by
  /// \p FaceMask (like `setStencilState` above).
  void setStencilOp(VkStencilFaceFlags FaceMask, VkStencilOp FailOp,
                    VkStencilOp PassOp, VkStencilOp DepthFailOp,
                    VkCompareOp CompareOp) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::SetStencilOp;
    Cmd.StencilFaceMask = FaceMask;
    Cmd.StencilFailOpValue = FailOp;
    Cmd.StencilPassOpValue = PassOp;
    Cmd.StencilDepthFailOpValue = DepthFailOp;
    Cmd.StencilCompareOpValue = CompareOp;
    Commands.push_back(Cmd);
  }
  /// (roadmap C4c) `vkCmdSetPrimitiveTopologyEXT`.
  void setPrimitiveTopology(VkPrimitiveTopology Topology) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::SetPrimitiveTopology;
    Cmd.PrimitiveTopologyValue = Topology;
    Commands.push_back(Cmd);
  }
  /// (V6) `vkCmdDraw`.
  void draw(uint32_t VertexCount, uint32_t InstanceCount, uint32_t FirstVertex,
            uint32_t FirstInstance) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::Draw;
    Cmd.VertexOrIndexCount = VertexCount;
    Cmd.InstanceCount = InstanceCount;
    Cmd.FirstVertexOrIndex = FirstVertex;
    Cmd.FirstInstance = FirstInstance;
    Commands.push_back(Cmd);
  }
  /// (V6) `vkCmdClearColorImage`/`vkCmdClearDepthStencilImage`.
  void clearImage(RecordedCommand::Kind Op, Image *Img, VkClearValue Value,
                  std::vector<VkImageSubresourceRange> Ranges) {
    RecordedCommand Cmd;
    Cmd.Op = Op;
    Cmd.DstImage = Img;
    Cmd.ClearValues.push_back(Value);
    Cmd.ClearRanges = std::move(Ranges);
    Commands.push_back(std::move(Cmd));
  }
  /// (V6) `vkCmdClearAttachments`.
  void clearAttachments(std::vector<VkClearAttachment> Attachments,
                        std::vector<VkClearRect> Rects) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::ClearAttachments;
    Cmd.ClearAttachments = std::move(Attachments);
    Cmd.ClearRects = std::move(Rects);
    Commands.push_back(std::move(Cmd));
  }
  /// (V6) `vkCmdBlitImage`.
  void blitImage(Image *Src, Image *Dst, std::vector<VkImageBlit> Regions,
                 VkFilter Filter) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::BlitImage;
    Cmd.SrcImage = Src;
    Cmd.DstImage = Dst;
    Cmd.BlitRegions = std::move(Regions);
    Cmd.BlitFilter = Filter;
    Commands.push_back(std::move(Cmd));
  }
  /// (V6) `vkCmdResolveImage`.
  void resolveImage(Image *Src, Image *Dst,
                    std::vector<VkImageResolve> Regions) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::ResolveImage;
    Cmd.SrcImage = Src;
    Cmd.DstImage = Dst;
    Cmd.ResolveRegions = std::move(Regions);
    Commands.push_back(std::move(Cmd));
  }
  /// (V6) `vkCmdDrawIndirect`/`vkCmdDrawIndexedIndirect`: the buffer and
  /// offset the `VkDrawIndirectCommand`/`VkDrawIndexedIndirectCommand`
  /// array is read from at execution time (`IndirectBuffer`/
  /// `IndirectOffset`), the number of commands (`Count[0]`) and the byte
  /// stride between them (`DstSize`). Every argument is read once and
  /// validated then, exactly like an indirect dispatch's group counts.
  void drawIndirect(RecordedCommand::Kind Op, Buffer *IndirectBuffer,
                    uint64_t Offset, uint32_t DrawCount, uint32_t Stride) {
    RecordedCommand Cmd;
    Cmd.Op = Op;
    Cmd.IndirectBuffer = IndirectBuffer;
    Cmd.IndirectOffset = Offset;
    Cmd.Count[0] = DrawCount;
    Cmd.DstSize = Stride;
    Commands.push_back(Cmd);
  }
  /// (V6) `vkCmdDrawIndexed`.
  void drawIndexed(uint32_t IndexCount, uint32_t InstanceCount,
                   uint32_t FirstIndex, int32_t VertexOffset,
                   uint32_t FirstInstance) {
    RecordedCommand Cmd;
    Cmd.Op = RecordedCommand::Kind::DrawIndexed;
    Cmd.VertexOrIndexCount = IndexCount;
    Cmd.InstanceCount = InstanceCount;
    Cmd.FirstVertexOrIndex = FirstIndex;
    Cmd.VertexOffset = VertexOffset;
    Cmd.FirstInstance = FirstInstance;
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
