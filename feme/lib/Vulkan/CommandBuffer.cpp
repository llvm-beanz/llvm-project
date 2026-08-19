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
#include "Format.h"
#include "GraphicsPipeline.h"
#include "Icd.h"
#include "Image.h"
#include "ImageOps.h"
#include "Objects.h"
#include "Pipeline.h"
#include "QueryPool.h"
#include "RenderPass.h"
#include "Sync.h"

#include "feme/Graphics/Executor.h"
#include "feme/Graphics/ImageFixture.h"
#include "feme/Graphics/Pipeline.h"
#include "feme/Graphics/PreparedDraw.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/ResourceHeap.h"
#include "feme/Target/CPU/ResourceInfo.h"

#include <algorithm>
#include <cstring>
#include <optional>

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

/// Owns the descriptor arrays `buildBoundResources` materializes,
/// referenced by `Bindings`' `llvm::ArrayRef`s. Kept alive for exactly as
/// long as the dispatch that consumes them runs.
struct MaterializedBoundResources {
  std::vector<std::vector<feme::cpu::FemeDescriptor>> Storage;
  std::vector<feme::cpu::BoundResourceBinding> Bindings;
  std::vector<std::vector<feme::cpu::FemeImageDescriptor>> ImageStorage;
  std::vector<feme::cpu::BoundImageBinding> ImageBindings;
  std::vector<std::vector<feme::cpu::FemeSamplerDescriptor>> SamplerStorage;
  std::vector<feme::cpu::BoundSamplerBinding> SamplerBindings;
};

/// Resolves one `VkImageView` binding into the `feme::cpu::
/// FemeImageDescriptor` a compiled stage's image heap holds (V5's remaining
/// deviation, now closed by roadmap R30's SPIR-V image lowering), or leaves
/// \p Dst zero-filled -- an empty image, which every runtime helper reads
/// as the robust all-zero result -- when the binding names something the
/// shader-side image path cannot address.
///
/// A view's mip subrange is expressed by handing the shader a *slice* of
/// the image's own mip-layout table while keeping `Data` at the image base:
/// a `FemeImageSubresourceLayout::Offset` is relative to that base, so the
/// view's base level simply becomes level 0 of the descriptor.
/// `baseArrayLayer > 0` has no such expression -- the layer offset differs
/// per mip level, and the ABI has no base-layer field -- so it, like every
/// non-2D view, is left unwritten rather than silently addressed as layer 0
/// (see FeMeVulkanDesign.md's V5 status note).
void materializeImageDescriptor(const DescriptorImageBinding &Src,
                                VkDescriptorType Type,
                                feme::cpu::FemeImageDescriptor &Dst) {
  ImageView *View = Src.View;
  if (!View || !View->image() || !View->image()->isBound())
    return;
  Image *Img = View->image();
  if (View->dimension() != feme::cpu::ImageDimension::Texture2D)
    return;

  const VkImageSubresourceRange &Range = View->range();
  if (Range.baseArrayLayer != 0)
    return;
  if (Range.baseMipLevel >= Img->mipLevels())
    return;
  uint32_t LevelCount = Range.levelCount == VK_REMAINING_MIP_LEVELS
                            ? Img->mipLevels() - Range.baseMipLevel
                            : Range.levelCount;
  LevelCount = std::min(LevelCount, Img->mipLevels() - Range.baseMipLevel);
  if (LevelCount == 0)
    return;

  Dst.Data = Img->data();
  Dst.SizeInBytes = Img->sizeInBytes();
  Dst.Dimension = static_cast<uint32_t>(feme::cpu::ImageDimension::Texture2D);
  Dst.Format = static_cast<uint32_t>(View->format());
  Dst.Width = std::max(1u, Img->width() >> Range.baseMipLevel);
  Dst.Height = std::max(1u, Img->height() >> Range.baseMipLevel);
  Dst.Depth = 1;
  Dst.MipLevels = LevelCount;
  Dst.ArrayLayers = 1;
  Dst.PlaneCount = 1;
  Dst.SampleCount = Img->sampleCount();
  Dst.Flags = isReadOnlyDescriptorType(Type) ? feme::cpu::FEME_IMAGE_SAMPLED
                                             : feme::cpu::FEME_IMAGE_STORAGE;
  Dst.MipLayouts = Img->mipLayouts().data() + Range.baseMipLevel;
  Dst.MipLayoutCount = LevelCount;
}

/// Builds the image and sampler descriptor arrays one binding of a bound
/// set resolves to. A `COMBINED_IMAGE_SAMPLER` contributes to *both*, at
/// the same (set, binding) identity: FeMe keeps the two descriptors
/// separate throughout (see FeMeGraphicsDesign.md's "Combined image
/// samplers remain two logical descriptors paired by lowering"), and the
/// compiled stage asks for whichever class its own reflection named.
void buildImageAndSamplerBinding(uint32_t SetIdx,
                                 const DescriptorSetLayoutBinding &BindingDecl,
                                 llvm::ArrayRef<DescriptorImageBinding> Array,
                                 MaterializedBoundResources &Result) {
  if (isImageDescriptorType(BindingDecl.Type)) {
    std::vector<feme::cpu::FemeImageDescriptor> Descriptors(Array.size());
    for (size_t J = 0; J != Array.size(); ++J)
      materializeImageDescriptor(Array[J], BindingDecl.Type, Descriptors[J]);
    Result.ImageStorage.push_back(std::move(Descriptors));
    Result.ImageBindings.push_back(feme::cpu::BoundImageBinding{
        SetIdx, BindingDecl.Binding, Result.ImageStorage.back()});
  }

  if (!isSamplerDescriptorType(BindingDecl.Type))
    return;
  std::vector<feme::cpu::FemeSamplerDescriptor> Samplers(Array.size());
  for (size_t J = 0; J != Array.size(); ++J)
    if (Array[J].Samp)
      Samplers[J] = Array[J].Samp->descriptor();
  Result.SamplerStorage.push_back(std::move(Samplers));
  Result.SamplerBindings.push_back(feme::cpu::BoundSamplerBinding{
      SetIdx, BindingDecl.Binding, Result.SamplerStorage.back()});
}

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
      if (isImageDescriptorType(BindingDecl.Type) ||
          isSamplerDescriptorType(BindingDecl.Type)) {
        llvm::ArrayRef<DescriptorImageBinding> ImageArray =
            State.Set->imageBindingArray(BindingDecl.Binding);
        if (!ImageArray.empty())
          buildImageAndSamplerBinding(SetIdx, BindingDecl, ImageArray, Result);
        continue;
      }
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

        // (V4) A texel buffer descriptor resolves through its
        // `VkBufferView` to a `Kind::Typed` descriptor instead of the
        // `Kind::Raw` one every other supported descriptor type below
        // produces -- see Descriptor.h's file comment.
        if (isTexelBufferDescriptorType(BindingDecl.Type)) {
          if (!Src.View || !Src.View->buffer() ||
              !Src.View->buffer()->isBound())
            continue; // Kind::None (never written).
          Buffer *Buf = Src.View->buffer();
          VkDeviceSize BufSize = Buf->size();
          VkDeviceSize Base = Src.View->offset();
          if (Base > BufSize || Src.View->range() > BufSize - Base)
            continue; // Overrun (should not happen: validated at view
                      // creation), treated as never-written for safety.

          feme::cpu::FemeDescriptor &Dst = Descriptors[J];
          Dst.Data = static_cast<uint8_t *>(Buf->data()) + Base;
          Dst.SizeInBytes = Src.View->range();
          Dst.Stride = formatElementSize(Src.View->format());
          Dst.Format = static_cast<uint32_t>(Src.View->format());
          Dst.Kind = static_cast<uint32_t>(feme::cpu::ResourceKind::Typed);
          Dst.Flags = isReadOnlyDescriptorType(BindingDecl.Type)
                          ? 0
                          : feme::cpu::FEME_DESCRIPTOR_UAV;
          continue;
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
  Resources.BoundImages = Materialized.ImageBindings;
  Resources.BoundSamplers = Materialized.SamplerBindings;
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

/// (V5) One `VkBufferImageCopy` region's texel-per-texel byte copy between
/// \p Img's own packed subresource layout and a flat buffer region, in
/// either direction (\p ToImage selects which). `bufferRowLength`/
/// `bufferImageHeight` of 0 mean "tightly packed to the copy's own extent",
/// per the specification. Copies row by row rather than as one contiguous
/// `memcpy`, since the image's row/slice pitch need not match the buffer's
/// (a non-zero `bufferRowLength`/`bufferImageHeight`, or simply a
/// mip level narrower than level 0, both make them differ).
Error copyBufferImageRegion(Image &Img, bool ToImage, void *BufferBase,
                            VkDeviceSize BufferSize,
                            const VkBufferImageCopy &Region) {
  uint32_t TexelSize = formatElementSize(Img.format());
  uint32_t RowLength = Region.bufferRowLength ? Region.bufferRowLength
                                              : Region.imageExtent.width;
  uint32_t ImageHeight = Region.bufferImageHeight ? Region.bufferImageHeight
                                                  : Region.imageExtent.height;
  uint64_t BufferRowBytes = uint64_t(RowLength) * TexelSize;
  uint64_t BufferSliceBytes = BufferRowBytes * ImageHeight;
  uint32_t MipLevel = Region.imageSubresource.mipLevel;
  if (MipLevel >= Img.mipLevels())
    return createStringError(inconvertibleErrorCode(),
                             "buffer/image copy mip level is out of range");

  for (uint32_t Layer = 0; Layer != Region.imageSubresource.layerCount;
       ++Layer) {
    uint32_t ArrayLayer = Region.imageSubresource.baseArrayLayer + Layer;
    for (uint32_t Z = 0; Z != Region.imageExtent.depth; ++Z) {
      uint64_t SliceIndex = uint64_t(Layer) * Region.imageExtent.depth + Z;
      for (uint32_t Y = 0; Y != Region.imageExtent.height; ++Y) {
        uint64_t BufferOffset = Region.bufferOffset +
                                SliceIndex * BufferSliceBytes +
                                uint64_t(Y) * BufferRowBytes;
        uint64_t RowBytes = uint64_t(Region.imageExtent.width) * TexelSize;
        if (BufferOffset + RowBytes > BufferSize)
          return createStringError(inconvertibleErrorCode(),
                                   "buffer/image copy region is out of "
                                   "range of its buffer");
        auto *BufferRow = static_cast<uint8_t *>(BufferBase) + BufferOffset;
        void *ImageRow = Img.texelPointer(
            MipLevel, ArrayLayer, Region.imageOffset.x,
            Region.imageOffset.y + Y, Region.imageOffset.z + Z);
        if (ToImage)
          std::memcpy(ImageRow, BufferRow, RowBytes);
        else
          std::memcpy(BufferRow, ImageRow, RowBytes);
      }
    }
  }
  return Error::success();
}

/// `vkCmdCopyBufferToImage`.
Error runCopyBufferToImage(Buffer *Src, Image *Dst,
                           llvm::ArrayRef<VkBufferImageCopy> Regions) {
  if (!Src || !Src->isBound() || !Dst || !Dst->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "buffer-to-image copy source/destination is "
                             "not bound");
  // A multisample image's per-sample data has no linear-buffer layout for
  // this command to target (`VUID-vkCmdCopyBufferToImage-srcImage-07973`'s
  // real-Vulkan equivalent): only `vkCmdCopyImage` moves one.
  if (Dst->sampleCount() != 1)
    return createStringError(inconvertibleErrorCode(),
                             "buffer-to-image copy destination must be "
                             "single-sample");
  for (const VkBufferImageCopy &Region : Regions)
    if (Error E = copyBufferImageRegion(*Dst, /*ToImage=*/true, Src->data(),
                                        Src->size(), Region))
      return E;
  return Error::success();
}

/// `vkCmdCopyImageToBuffer`.
Error runCopyImageToBuffer(Image *Src, Buffer *Dst,
                           llvm::ArrayRef<VkBufferImageCopy> Regions) {
  if (!Src || !Src->isBound() || !Dst || !Dst->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "image-to-buffer copy source/destination is "
                             "not bound");
  if (Src->sampleCount() != 1)
    return createStringError(inconvertibleErrorCode(),
                             "image-to-buffer copy source must be "
                             "single-sample");
  for (const VkBufferImageCopy &Region : Regions)
    if (Error E = copyBufferImageRegion(*Src, /*ToImage=*/false, Dst->data(),
                                        Dst->size(), Region))
      return E;
  return Error::success();
}

/// `vkCmdCopyImage`: copies each region's texels from \p Src to \p Dst.
/// Both images must share the same texel size and sample count, matching
/// real Vulkan's own "compatible formats" copy rule
/// (`VUID-vkCmdCopyImage-srcImage-01548`): no value conversion takes place
/// on either side, in this ICD or a real one -- `vkCmdCopyImage` reinterprets
/// bits, it never converts them (that is what a shader's format-aware
/// load/store or a blit does). Every sample of a multisample region is
/// copied verbatim; there is no resolve here either (that is
/// `vkCmdResolveImage`, not yet implemented -- V6+).
Error runCopyImage(Image *Src, Image *Dst,
                   llvm::ArrayRef<VkImageCopy> Regions) {
  if (!Src || !Src->isBound() || !Dst || !Dst->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "image copy source/destination is not bound");
  uint32_t TexelSize = formatElementSize(Src->format());
  if (TexelSize != formatElementSize(Dst->format()))
    return createStringError(inconvertibleErrorCode(),
                             "vkCmdCopyImage between formats of differing "
                             "texel size is not supported");
  if (Src->sampleCount() != Dst->sampleCount())
    return createStringError(inconvertibleErrorCode(),
                             "vkCmdCopyImage between images of differing "
                             "sample counts is not supported");
  // Every sample of one texel is stored contiguously
  // (`FemeImageSubresourceLayout::SampleStride == TexelSize`, see Image.cpp's
  // `computeSubresourceLayouts`), so one row's `SampleCount` samples of a
  // region's texels are themselves one contiguous span -- a single
  // `memcpy` per row moves every sample, there is no need to loop over
  // samples separately the way looping over `Y`/`Z`/array layer does.
  uint32_t SampleCount = Src->sampleCount();
  for (const VkImageCopy &Region : Regions) {
    if (Region.srcSubresource.mipLevel >= Src->mipLevels() ||
        Region.dstSubresource.mipLevel >= Dst->mipLevels())
      return createStringError(inconvertibleErrorCode(),
                               "image copy mip level is out of range");
    uint64_t RowBytes = uint64_t(Region.extent.width) * TexelSize * SampleCount;
    for (uint32_t Layer = 0; Layer != Region.srcSubresource.layerCount;
         ++Layer) {
      for (uint32_t Z = 0; Z != Region.extent.depth; ++Z) {
        for (uint32_t Y = 0; Y != Region.extent.height; ++Y) {
          void *SrcRow = Src->texelPointer(
              Region.srcSubresource.mipLevel,
              Region.srcSubresource.baseArrayLayer + Layer, Region.srcOffset.x,
              Region.srcOffset.y + Y, Region.srcOffset.z + Z);
          void *DstRow = Dst->texelPointer(
              Region.dstSubresource.mipLevel,
              Region.dstSubresource.baseArrayLayer + Layer, Region.dstOffset.x,
              Region.dstOffset.y + Y, Region.dstOffset.z + Z);
          std::memcpy(DstRow, SrcRow, RowBytes);
        }
      }
    }
  }
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

//===----------------------------------------------------------------------===//
// Graphics: render-target binding and draws (V6)
//===----------------------------------------------------------------------===//

/// The command buffer's graphics execution state: what `vkCmdBeginRenderPass`/
/// `vkCmdBeginRendering` bound, what `vkCmdBindVertexBuffers`/
/// `vkCmdBindIndexBuffer` bound, and every piece of dynamic state a
/// `vkCmdSet*` recorded. A draw snapshots all of it (see "Dynamic state is
/// what makes the prepared draw a snapshot rather than a pipeline pointer"
/// in feme/docs/FeMeVulkanDesign.md).
struct GraphicsState {
  const RenderPass *Pass = nullptr;
  const Framebuffer *Fb = nullptr;
  uint32_t Subpass = 0;
  VkRect2D RenderArea{};
  std::vector<VkClearValue> ClearValues;
  bool Rendering = false;
  RenderTargetBinding Binding;

  std::vector<Buffer *> VertexBuffers;
  std::vector<VkDeviceSize> VertexBufferOffsets;
  Buffer *IndexBuffer = nullptr;
  VkDeviceSize IndexBufferOffset = 0;
  VkIndexType IndexType = VK_INDEX_TYPE_UINT32;

  DynamicGraphicsState Dynamic;
};

/// Builds the normalized render-target binding \p Subpass of \p Pass
/// resolves to against \p Fb's views -- the single internal shape
/// `vkCmdBeginRendering` also produces (see RenderPass.h).
Expected<RenderTargetBinding>
buildRenderTargetBinding(const RenderPass &Pass, const Framebuffer &Fb,
                         uint32_t Subpass, VkRect2D RenderArea,
                         llvm::ArrayRef<VkClearValue> ClearValues) {
  if (Subpass >= Pass.subpasses().size())
    return createStringError(inconvertibleErrorCode(),
                             "subpass %u is out of range of its render pass",
                             Subpass);
  const SubpassDescription &Desc = Pass.subpasses()[Subpass];
  RenderTargetBinding Binding;
  Binding.RenderArea = RenderArea;

  auto makeView = [&](uint32_t Index) -> RenderTargetView {
    const AttachmentDescription &Attachment = Pass.attachments()[Index];
    RenderTargetView View;
    View.View = Fb.attachments()[Index];
    View.Format = Attachment.Format;
    View.SampleCount = Attachment.SampleCount;
    View.LoadOp = isSupportedStencilAttachmentFormat(Attachment.Format)
                      ? Attachment.StencilLoadOp
                      : Attachment.LoadOp;
    View.StoreOp = isSupportedStencilAttachmentFormat(Attachment.Format)
                       ? Attachment.StencilStoreOp
                       : Attachment.StoreOp;
    if (Index < ClearValues.size())
      View.ClearValue = ClearValues[Index];
    return View;
  };

  for (size_t I = 0; I != Desc.ColorAttachments.size(); ++I) {
    uint32_t Index = Desc.ColorAttachments[I];
    if (Index == VK_ATTACHMENT_UNUSED)
      return createStringError(inconvertibleErrorCode(),
                               "an unused color attachment slot is not "
                               "implemented");
    RenderTargetView View = makeView(Index);
    if (I < Desc.ResolveAttachments.size() &&
        Desc.ResolveAttachments[I] != VK_ATTACHMENT_UNUSED)
      View.ResolveView = Fb.attachments()[Desc.ResolveAttachments[I]];
    Binding.Colors.push_back(View);
  }
  if (Desc.DepthStencilAttachment != VK_ATTACHMENT_UNUSED) {
    RenderTargetView View = makeView(Desc.DepthStencilAttachment);
    if (isSupportedStencilAttachmentFormat(View.Format))
      Binding.Stencil = View;
    else
      Binding.Depth = View;
  }
  return Binding;
}

/// Applies one attachment's `VK_ATTACHMENT_LOAD_OP_CLEAR` over \p Area,
/// which is the render area rather than the whole attachment: Vulkan clears
/// exactly what the render pass instance covers.
Error applyClear(const RenderTargetView &View, uint32_t SampleCount,
                 const VkRect2D &Area) {
  if (View.LoadOp != VK_ATTACHMENT_LOAD_OP_CLEAR)
    return Error::success();
  Expected<feme::graphics::AttachmentView> Attachment =
      resolveAttachmentView(View.View);
  if (!Attachment)
    return Attachment.takeError();
  Expected<uint32_t> ElemSize =
      feme::graphics::getFixtureFormatElementSize(Attachment->Format);
  if (!ElemSize)
    return ElemSize.takeError();

  std::vector<uint8_t> Texel(*ElemSize);
  if (isSupportedDepthAttachmentFormat(Attachment->Format)) {
    if (Error E = feme::graphics::packClearColor(
            Attachment->Format, {View.ClearValue.depthStencil.depth}, Texel))
      return E;
  } else if (isSupportedStencilAttachmentFormat(Attachment->Format)) {
    Texel[0] = static_cast<uint8_t>(View.ClearValue.depthStencil.stencil);
  } else {
    std::array<double, 4> Color{
        View.ClearValue.color.float32[0], View.ClearValue.color.float32[1],
        View.ClearValue.color.float32[2], View.ClearValue.color.float32[3]};
    if (Error E =
            feme::graphics::packClearColor(Attachment->Format, Color, Texel))
      return E;
  }

  uint32_t MinX = std::max<int32_t>(0, Area.offset.x);
  uint32_t MinY = std::max<int32_t>(0, Area.offset.y);
  uint32_t MaxX =
      std::min<uint64_t>(Attachment->Width, uint64_t(MinX) + Area.extent.width);
  uint32_t MaxY = std::min<uint64_t>(Attachment->Height,
                                     uint64_t(MinY) + Area.extent.height);
  for (uint32_t Y = MinY; Y < MaxY; ++Y)
    for (uint32_t X = MinX; X < MaxX; ++X)
      for (uint32_t S = 0; S != SampleCount; ++S) {
        size_t Offset =
            (((size_t)Y * Attachment->Width + X) * SampleCount + S) * *ElemSize;
        std::memcpy(Attachment->Data.data() + Offset, Texel.data(), *ElemSize);
      }
  return Error::success();
}

/// Applies every attachment's load op when a render pass instance begins.
Error applyLoadOps(const RenderTargetBinding &Binding) {
  for (const RenderTargetView &View : Binding.Colors)
    if (Error E = applyClear(View, View.SampleCount, Binding.RenderArea))
      return E;
  if (Binding.Depth)
    if (Error E = applyClear(*Binding.Depth, Binding.Depth->SampleCount,
                             Binding.RenderArea))
      return E;
  if (Binding.Stencil)
    if (Error E = applyClear(*Binding.Stencil, Binding.Stencil->SampleCount,
                             Binding.RenderArea))
      return E;
  return Error::success();
}

/// Runs one draw command: materializes the bound descriptor sets, resolves
/// the pipeline's static state against the command buffer's dynamic state,
/// converts the render-target binding and vertex/index bindings into a
/// `feme::graphics::PreparedDraw`, and hands the pair to the software
/// graphics executor. The ICD never acquires knowledge of rasterization,
/// and `FeMeGraphics` never acquires knowledge of `VkRenderPass` (see
/// "Ownership boundary").
Error runDraw(const GraphicsPipeline &Pipeline, const GraphicsState &Gfx,
              const feme::graphics::DrawCommand &Draw,
              llvm::ArrayRef<BoundSetState> BoundSets,
              llvm::ArrayRef<uint8_t> PushConstants) {
  if (!Gfx.Rendering)
    return createStringError(inconvertibleErrorCode(),
                             "a draw must be recorded inside a render pass "
                             "instance");
  if (Gfx.Binding.Colors.size() != Pipeline.colorAttachmentCount())
    return createStringError(inconvertibleErrorCode(),
                             "the bound pipeline has %u color attachment(s) "
                             "but the render target has %zu",
                             Pipeline.colorAttachmentCount(),
                             Gfx.Binding.Colors.size());
  if (Pipeline.needsDepthAttachment() && !Gfx.Binding.Depth)
    return createStringError(inconvertibleErrorCode(),
                             "the bound pipeline tests/writes depth but the "
                             "render target has no depth attachment");
  if (Pipeline.needsStencilAttachment() && !Gfx.Binding.Stencil)
    return createStringError(inconvertibleErrorCode(),
                             "the bound pipeline tests stencil but the "
                             "render target has no stencil attachment");

  std::vector<feme::graphics::AttachmentView> Attachments;
  std::vector<feme::graphics::AttachmentView> ResolveAttachments;
  bool AnyResolve = false;
  for (const RenderTargetView &View : Gfx.Binding.Colors) {
    Expected<feme::graphics::AttachmentView> Attachment =
        resolveAttachmentView(View.View);
    if (!Attachment)
      return Attachment.takeError();
    if (View.SampleCount != Pipeline.sampleCount())
      return createStringError(inconvertibleErrorCode(),
                               "the render target's sample count disagrees "
                               "with the bound pipeline's");
    Attachments.push_back(*Attachment);
    AnyResolve |= View.ResolveView != nullptr;
  }
  if (AnyResolve)
    for (const RenderTargetView &View : Gfx.Binding.Colors) {
      if (!View.ResolveView)
        return createStringError(inconvertibleErrorCode(),
                                 "either every color attachment resolves or "
                                 "none does");
      Expected<feme::graphics::AttachmentView> Attachment =
          resolveAttachmentView(View.ResolveView);
      if (!Attachment)
        return Attachment.takeError();
      ResolveAttachments.push_back(*Attachment);
    }

  feme::graphics::DepthStencilAttachment DepthStencil;
  if (Gfx.Binding.Depth) {
    Expected<feme::graphics::AttachmentView> Attachment =
        resolveAttachmentView(Gfx.Binding.Depth->View);
    if (!Attachment)
      return Attachment.takeError();
    DepthStencil.Depth = *Attachment;
  }
  if (Gfx.Binding.Stencil) {
    Expected<feme::graphics::AttachmentView> Attachment =
        resolveAttachmentView(Gfx.Binding.Stencil->View);
    if (!Attachment)
      return Attachment.takeError();
    DepthStencil.Stencil = *Attachment;
  }

  // Vertex fetch: one `VertexBufferBinding` per bound buffer the pipeline
  // declares, carrying the attributes that binding supplies.
  std::vector<std::vector<feme::graphics::VertexAttribute>> AttributeStorage;
  std::vector<feme::graphics::VertexBufferBinding> VertexBuffers;
  for (const VertexInputBinding &BindingDecl : Pipeline.vertexBindings()) {
    if (BindingDecl.Binding >= Gfx.VertexBuffers.size() ||
        !Gfx.VertexBuffers[BindingDecl.Binding] ||
        !Gfx.VertexBuffers[BindingDecl.Binding]->isBound())
      return createStringError(inconvertibleErrorCode(),
                               "vertex binding %u is not bound to a buffer",
                               BindingDecl.Binding);
    Buffer &Buf = *Gfx.VertexBuffers[BindingDecl.Binding];
    VkDeviceSize Offset = Gfx.VertexBufferOffsets[BindingDecl.Binding];
    if (Offset > Buf.size())
      return createStringError(inconvertibleErrorCode(),
                               "vertex binding %u's offset is out of range "
                               "of its buffer",
                               BindingDecl.Binding);

    std::vector<feme::graphics::VertexAttribute> Attributes;
    for (const VertexInputAttribute &Attr : Pipeline.vertexAttributes())
      if (Attr.Binding == BindingDecl.Binding)
        Attributes.push_back(feme::graphics::VertexAttribute{
            Attr.Location, Attr.Format, Attr.Offset});
    AttributeStorage.push_back(std::move(Attributes));

    feme::graphics::VertexBufferBinding VB;
    VB.Binding = BindingDecl.Binding;
    VB.Stride = BindingDecl.Stride;
    VB.Data = llvm::ArrayRef<uint8_t>(static_cast<const uint8_t *>(Buf.data()) +
                                          Offset,
                                      static_cast<size_t>(Buf.size() - Offset));
    VB.Attributes = AttributeStorage.back();
    VB.PerInstance = BindingDecl.PerInstance;
    VertexBuffers.push_back(VB);
  }

  feme::graphics::IndexBufferBinding IndexBinding;
  if (Draw.Indexed) {
    if (!Gfx.IndexBuffer || !Gfx.IndexBuffer->isBound())
      return createStringError(inconvertibleErrorCode(),
                               "an indexed draw has no bound index buffer");
    if (Gfx.IndexType != VK_INDEX_TYPE_UINT16 &&
        Gfx.IndexType != VK_INDEX_TYPE_UINT32)
      return createStringError(inconvertibleErrorCode(),
                               "only 16- and 32-bit index types are "
                               "implemented");
    if (Gfx.IndexBufferOffset > Gfx.IndexBuffer->size())
      return createStringError(inconvertibleErrorCode(),
                               "the index buffer's offset is out of range of "
                               "its buffer");
    IndexBinding.Type = Gfx.IndexType == VK_INDEX_TYPE_UINT16
                            ? feme::graphics::IndexType::UInt16
                            : feme::graphics::IndexType::UInt32;
    IndexBinding.Data = llvm::ArrayRef<uint8_t>(
        static_cast<const uint8_t *>(Gfx.IndexBuffer->data()) +
            Gfx.IndexBufferOffset,
        static_cast<size_t>(Gfx.IndexBuffer->size() - Gfx.IndexBufferOffset));
  }

  MaterializedBoundResources Materialized = buildBoundResources(BoundSets);
  feme::cpu::DispatchResources Resources;
  Resources.BoundResources = Materialized.Bindings;
  Resources.BoundImages = Materialized.ImageBindings;
  Resources.BoundSamplers = Materialized.SamplerBindings;
  Resources.RootConstants = PushConstants;

  // The scissor a draw actually rasterizes against is the intersection of
  // the pipeline's (or `vkCmdSetScissor`'s) rectangle and the render area:
  // "the render area" is part of the render-target binding, and nothing
  // outside it may be written.
  feme::graphics::ScissorRect Scissor = Pipeline.resolveScissor(Gfx.Dynamic);
  const VkRect2D &Area = Gfx.Binding.RenderArea;
  int32_t MinX = std::max(Scissor.X, Area.offset.x);
  int32_t MinY = std::max(Scissor.Y, Area.offset.y);
  int64_t MaxX = std::min<int64_t>(int64_t(Scissor.X) + Scissor.Width,
                                   int64_t(Area.offset.x) + Area.extent.width);
  int64_t MaxY = std::min<int64_t>(int64_t(Scissor.Y) + Scissor.Height,
                                   int64_t(Area.offset.y) + Area.extent.height);
  Scissor.X = MinX;
  Scissor.Y = MinY;
  Scissor.Width = MaxX > MinX ? uint32_t(MaxX - MinX) : 0;
  Scissor.Height = MaxY > MinY ? uint32_t(MaxY - MinY) : 0;

  feme::graphics::PreparedDraw Prepared;
  Prepared.Attachments = Attachments;
  Prepared.DepthStencil = DepthStencil;
  Prepared.Viewport = Pipeline.resolveViewport(Gfx.Dynamic);
  Prepared.Scissor = Scissor;
  Prepared.VertexBuffers = VertexBuffers;
  Prepared.IndexBuffer = IndexBinding;
  Prepared.Resources = Resources;
  Prepared.Draws = llvm::ArrayRef<feme::graphics::DrawCommand>(Draw);
  Prepared.ResolveAttachments = ResolveAttachments;

  return feme::graphics::executeDraws(
      Pipeline.buildExecutorPipeline(Gfx.Dynamic), Prepared);
}

/// Validates that every byte a draw's vertex/index fetch may read lies
/// inside the bound buffers: "read once, bounds-checked against the bound
/// buffers and the advertised limits, and rejected rather than clamped when
/// they cannot be honored. `firstInstance`/`vertexOffset` participate in the
/// fetch bounds check, not only in the index arithmetic."
///
/// An indexed draw's vertex reach depends on index values this does not
/// read, so its index *range* is what is checked here; the executor's own
/// fetch is bounds-checked against the same buffer sizes for the values it
/// then reads.
Error validateDrawFetchBounds(const GraphicsPipeline &Pipeline,
                              const GraphicsState &Gfx,
                              const feme::graphics::DrawCommand &Draw) {
  if (Draw.Indexed) {
    if (!Gfx.IndexBuffer || !Gfx.IndexBuffer->isBound())
      return createStringError(inconvertibleErrorCode(),
                               "an indexed draw has no bound index buffer");
    uint64_t IndexSize = Gfx.IndexType == VK_INDEX_TYPE_UINT16 ? 2 : 4;
    uint64_t End = (uint64_t(Draw.FirstIndex) + Draw.VertexCount) * IndexSize +
                   Gfx.IndexBufferOffset;
    if (End > Gfx.IndexBuffer->size())
      return createStringError(inconvertibleErrorCode(),
                               "the indexed draw's index range overruns its "
                               "bound index buffer");
    return Error::success();
  }

  if (Draw.VertexCount == 0 || Draw.InstanceCount == 0)
    return Error::success();
  uint64_t LastVertex = uint64_t(Draw.FirstVertex) + Draw.VertexCount - 1;
  uint64_t LastInstance = uint64_t(Draw.FirstInstance) + Draw.InstanceCount - 1;
  for (const VertexInputBinding &BindingDecl : Pipeline.vertexBindings()) {
    if (BindingDecl.Binding >= Gfx.VertexBuffers.size() ||
        !Gfx.VertexBuffers[BindingDecl.Binding] ||
        !Gfx.VertexBuffers[BindingDecl.Binding]->isBound())
      return createStringError(inconvertibleErrorCode(),
                               "vertex binding %u is not bound to a buffer",
                               BindingDecl.Binding);
    // A per-instance binding's reach depends on the instance range, not the
    // vertex range: it is read once per instance, not once per vertex.
    uint64_t LastIndex = BindingDecl.PerInstance ? LastInstance : LastVertex;
    uint64_t Base = Gfx.VertexBufferOffsets[BindingDecl.Binding] +
                    LastIndex * BindingDecl.Stride;
    for (const VertexInputAttribute &Attr : Pipeline.vertexAttributes()) {
      if (Attr.Binding != BindingDecl.Binding)
        continue;
      uint64_t End = Base + Attr.Offset + formatElementSize(Attr.Format);
      if (End > Gfx.VertexBuffers[BindingDecl.Binding]->size())
        return createStringError(inconvertibleErrorCode(),
                                 "the draw's vertex fetch of location %u "
                                 "overruns its bound vertex buffer",
                                 Attr.Location);
    }
  }
  return Error::success();
}

/// Validates a draw's own counts against the advertised limits, per "every
/// one of them is checked at pipeline creation and at draw time".
Error validateDrawCounts(const PhysicalDeviceInfo *Info,
                         const feme::graphics::DrawCommand &Draw) {
  if (!Info)
    return Error::success();
  const VkPhysicalDeviceLimits &Limits = Info->Properties.limits;
  if (!Draw.Indexed && Draw.VertexCount > Limits.maxDrawIndexedIndexValue)
    return createStringError(inconvertibleErrorCode(),
                             "the draw's vertex count exceeds the advertised "
                             "maximum");
  if (Draw.Indexed) {
    uint64_t LastIndex = uint64_t(Draw.FirstIndex) + Draw.VertexCount;
    if (LastIndex > Limits.maxDrawIndexedIndexValue)
      return createStringError(inconvertibleErrorCode(),
                               "the indexed draw's index range exceeds "
                               "maxDrawIndexedIndexValue");
  }
  return Error::success();
}

/// Runs one draw after checking its counts against the advertised limits
/// and its fetches against the bound buffers -- the single path every
/// direct and indirect draw goes through, so an indirect command's
/// attacker-controlled arguments are validated exactly like a direct one's.
Error runValidatedDraw(const GraphicsPipeline &Pipeline,
                       const GraphicsState &Gfx,
                       const feme::graphics::DrawCommand &Draw,
                       const PhysicalDeviceInfo *DeviceInfo,
                       llvm::ArrayRef<BoundSetState> BoundSets,
                       llvm::ArrayRef<uint8_t> PushConstants) {
  if (Error E = validateDrawCounts(DeviceInfo, Draw))
    return E;
  if (Error E = validateDrawFetchBounds(Pipeline, Gfx, Draw))
    return E;
  return runDraw(Pipeline, Gfx, Draw, BoundSets, PushConstants);
}

/// Reads \p DrawCount `VkDrawIndirectCommand`/`VkDrawIndexedIndirectCommand`
/// structures from \p Buf at \p Offset with \p Stride, bounds-checking the
/// whole span before reading any of it (see "Error Handling and Security":
/// indirect arguments are attacker-controlled). Each command is read exactly
/// once, so a concurrent write cannot make a validated argument differ from
/// the one used.
Expected<std::vector<feme::graphics::DrawCommand>>
readIndirectDraws(Buffer *Buf, uint64_t Offset, uint32_t DrawCount,
                  uint32_t Stride, bool Indexed) {
  if (!Buf || !Buf->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "the indirect draw buffer is not bound");
  uint32_t CommandSize = Indexed ? sizeof(VkDrawIndexedIndirectCommand)
                                 : sizeof(VkDrawIndirectCommand);
  if (Stride == 0)
    Stride = CommandSize;
  if (Stride < CommandSize || Stride % 4 != 0)
    return createStringError(inconvertibleErrorCode(),
                             "the indirect draw stride is smaller than its "
                             "command or is not 4-byte aligned");
  if (DrawCount != 0) {
    uint64_t End = Offset + uint64_t(Stride) * (DrawCount - 1) + CommandSize;
    if (End > Buf->size() || End < Offset)
      return createStringError(inconvertibleErrorCode(),
                               "the indirect draw commands overrun their "
                               "buffer");
  }

  std::vector<feme::graphics::DrawCommand> Draws;
  Draws.reserve(DrawCount);
  const auto *Base = static_cast<const uint8_t *>(Buf->data());
  for (uint32_t I = 0; I != DrawCount; ++I) {
    const uint8_t *Src = Base + Offset + uint64_t(Stride) * I;
    feme::graphics::DrawCommand Draw;
    if (Indexed) {
      VkDrawIndexedIndirectCommand Args{};
      std::memcpy(&Args, Src, sizeof(Args));
      Draw.VertexCount = Args.indexCount;
      Draw.InstanceCount = Args.instanceCount;
      Draw.FirstIndex = Args.firstIndex;
      Draw.VertexOffset = Args.vertexOffset;
      Draw.FirstInstance = Args.firstInstance;
      Draw.Indexed = true;
    } else {
      VkDrawIndirectCommand Args{};
      std::memcpy(&Args, Src, sizeof(Args));
      Draw.VertexCount = Args.vertexCount;
      Draw.InstanceCount = Args.instanceCount;
      Draw.FirstVertex = Args.firstVertex;
      Draw.FirstInstance = Args.firstInstance;
    }
    Draws.push_back(Draw);
  }
  return Draws;
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
                          GraphicsPipeline *&BoundGraphicsPipeline,
                          GraphicsState &Gfx,
                          std::vector<BoundSetState> &BoundSets,
                          std::vector<uint8_t> &PushConstants) {
  for (const RecordedCommand &Cmd : Commands) {
    switch (Cmd.Op) {
    case RecordedCommand::Kind::BindPipeline:
      if (Cmd.Pipeline && Cmd.Pipeline->kind() == Pipeline::Kind::Graphics)
        BoundGraphicsPipeline = static_cast<GraphicsPipeline *>(Cmd.Pipeline);
      else
        BoundPipeline = static_cast<ComputePipeline *>(Cmd.Pipeline);
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
      // See `pipelineBarrier`'s own comment: the join itself is a no-op
      // under this milestone's strictly-sequential execution model, but
      // (V5) an image memory barrier's layout transition is real state.
      for (const ImageLayoutTransition &Barrier : Cmd.ImageBarriers)
        Barrier.Img->setLayout(Barrier.Range.baseMipLevel,
                               Barrier.Range.levelCount,
                               Barrier.Range.baseArrayLayer,
                               Barrier.Range.layerCount, Barrier.NewLayout);
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
        if (Error E = executeCommandsInto(Secondary->commands(), DeviceInfo,
                                          BoundPipeline, BoundGraphicsPipeline,
                                          Gfx, BoundSets, PushConstants))
          return E;
      break;
    case RecordedCommand::Kind::CopyBufferToImage:
      if (Error E = runCopyBufferToImage(Cmd.SrcBuffer, Cmd.DstImage,
                                         Cmd.BufferImageCopyRegions))
        return E;
      break;
    case RecordedCommand::Kind::CopyImageToBuffer:
      if (Error E = runCopyImageToBuffer(Cmd.SrcImage, Cmd.DstBuffer,
                                         Cmd.BufferImageCopyRegions))
        return E;
      break;
    case RecordedCommand::Kind::CopyImage:
      if (Error E =
              runCopyImage(Cmd.SrcImage, Cmd.DstImage, Cmd.ImageCopyRegions))
        return E;
      break;
    case RecordedCommand::Kind::BeginRenderPass: {
      if (!Cmd.BeginPass || !Cmd.BeginFramebuffer)
        return createStringError(inconvertibleErrorCode(),
                                 "vkCmdBeginRenderPass needs both a render "
                                 "pass and a framebuffer");
      Gfx.Pass = Cmd.BeginPass;
      Gfx.Fb = Cmd.BeginFramebuffer;
      Gfx.Subpass = 0;
      Gfx.RenderArea = Cmd.RenderArea;
      Gfx.ClearValues = Cmd.ClearValues;
      Expected<RenderTargetBinding> Binding = buildRenderTargetBinding(
          *Gfx.Pass, *Gfx.Fb, Gfx.Subpass, Gfx.RenderArea, Gfx.ClearValues);
      if (!Binding)
        return Binding.takeError();
      Gfx.Binding = std::move(*Binding);
      Gfx.Rendering = true;
      if (Error E = applyLoadOps(Gfx.Binding))
        return E;
      break;
    }
    case RecordedCommand::Kind::BeginRendering:
      Gfx.Pass = nullptr;
      Gfx.Fb = nullptr;
      Gfx.Subpass = 0;
      Gfx.Binding = Cmd.RenderingBinding;
      Gfx.RenderArea = Gfx.Binding.RenderArea;
      Gfx.Rendering = true;
      if (Error E = applyLoadOps(Gfx.Binding))
        return E;
      break;
    case RecordedCommand::Kind::NextSubpass: {
      if (!Gfx.Rendering || !Gfx.Pass || !Gfx.Fb)
        return createStringError(inconvertibleErrorCode(),
                                 "vkCmdNextSubpass outside a render pass "
                                 "instance");
      // A subpass boundary is a full join, which this ICD's strictly
      // sequential execution already satisfies; the next subpass's own
      // attachment references simply become the render-target binding.
      ++Gfx.Subpass;
      Expected<RenderTargetBinding> Binding = buildRenderTargetBinding(
          *Gfx.Pass, *Gfx.Fb, Gfx.Subpass, Gfx.RenderArea, Gfx.ClearValues);
      if (!Binding)
        return Binding.takeError();
      Gfx.Binding = std::move(*Binding);
      break;
    }
    case RecordedCommand::Kind::EndRenderPass:
      Gfx.Rendering = false;
      Gfx.Binding = RenderTargetBinding{};
      Gfx.Pass = nullptr;
      Gfx.Fb = nullptr;
      break;
    case RecordedCommand::Kind::BindVertexBuffers: {
      size_t Required = Cmd.FirstSet + Cmd.VertexBuffers.size();
      if (Gfx.VertexBuffers.size() < Required) {
        Gfx.VertexBuffers.resize(Required, nullptr);
        Gfx.VertexBufferOffsets.resize(Required, 0);
      }
      for (size_t I = 0; I != Cmd.VertexBuffers.size(); ++I) {
        Gfx.VertexBuffers[Cmd.FirstSet + I] = Cmd.VertexBuffers[I];
        Gfx.VertexBufferOffsets[Cmd.FirstSet + I] =
            I < Cmd.VertexBufferOffsets.size() ? Cmd.VertexBufferOffsets[I] : 0;
      }
      break;
    }
    case RecordedCommand::Kind::BindIndexBuffer:
      Gfx.IndexBuffer = Cmd.SrcBuffer;
      Gfx.IndexBufferOffset = Cmd.IndirectOffset;
      Gfx.IndexType = Cmd.IndexType;
      break;
    case RecordedCommand::Kind::SetViewport:
      Gfx.Dynamic.Viewport = feme::graphics::ViewportState{
          Cmd.ViewportValue.x,        Cmd.ViewportValue.y,
          Cmd.ViewportValue.width,    Cmd.ViewportValue.height,
          Cmd.ViewportValue.minDepth, Cmd.ViewportValue.maxDepth};
      break;
    case RecordedCommand::Kind::SetScissor:
      Gfx.Dynamic.Scissor = feme::graphics::ScissorRect{
          Cmd.ScissorValue.offset.x, Cmd.ScissorValue.offset.y,
          Cmd.ScissorValue.extent.width, Cmd.ScissorValue.extent.height};
      break;
    case RecordedCommand::Kind::SetBlendConstants:
      Gfx.Dynamic.BlendConstants = Cmd.BlendConstants;
      break;
    case RecordedCommand::Kind::SetStencilReference:
    case RecordedCommand::Kind::SetStencilCompareMask:
    case RecordedCommand::Kind::SetStencilWriteMask:
      for (unsigned Face = 0; Face != 2; ++Face) {
        VkStencilFaceFlags Bit =
            Face == 0 ? VK_STENCIL_FACE_FRONT_BIT : VK_STENCIL_FACE_BACK_BIT;
        if ((Cmd.StencilFaceMask & Bit) == 0)
          continue;
        if (Cmd.Op == RecordedCommand::Kind::SetStencilReference)
          Gfx.Dynamic.StencilReference[Face] = Cmd.StencilValue;
        else if (Cmd.Op == RecordedCommand::Kind::SetStencilCompareMask)
          Gfx.Dynamic.StencilCompareMask[Face] = Cmd.StencilValue;
        else
          Gfx.Dynamic.StencilWriteMask[Face] = Cmd.StencilValue;
      }
      break;
    case RecordedCommand::Kind::Draw:
    case RecordedCommand::Kind::DrawIndexed: {
      if (!BoundGraphicsPipeline)
        return createStringError(inconvertibleErrorCode(),
                                 "draw with no bound graphics pipeline");
      feme::graphics::DrawCommand Draw;
      Draw.VertexCount = Cmd.VertexOrIndexCount;
      Draw.InstanceCount = Cmd.InstanceCount;
      Draw.FirstInstance = Cmd.FirstInstance;
      Draw.Indexed = Cmd.Op == RecordedCommand::Kind::DrawIndexed;
      if (Draw.Indexed) {
        Draw.FirstIndex = Cmd.FirstVertexOrIndex;
        Draw.VertexOffset = Cmd.VertexOffset;
      } else {
        Draw.FirstVertex = Cmd.FirstVertexOrIndex;
      }
      if (Error E = runValidatedDraw(*BoundGraphicsPipeline, Gfx, Draw,
                                     DeviceInfo, BoundSets, PushConstants))
        return E;
      break;
    }
    case RecordedCommand::Kind::ClearColorImage:
      if (Error E = runClearColorImage(Cmd.DstImage, Cmd.ClearValues[0].color,
                                       Cmd.ClearRanges))
        return E;
      break;
    case RecordedCommand::Kind::ClearDepthStencilImage:
      if (Error E = runClearDepthStencilImage(
              Cmd.DstImage, Cmd.ClearValues[0].depthStencil, Cmd.ClearRanges))
        return E;
      break;
    case RecordedCommand::Kind::ClearAttachments:
      if (!Gfx.Rendering)
        return createStringError(inconvertibleErrorCode(),
                                 "vkCmdClearAttachments outside a render pass "
                                 "instance");
      if (Error E = runClearAttachments(Gfx.Binding, Cmd.ClearAttachments,
                                        Cmd.ClearRects))
        return E;
      break;
    case RecordedCommand::Kind::BlitImage:
      if (Error E = runBlitImage(Cmd.SrcImage, Cmd.DstImage, Cmd.BlitRegions,
                                 Cmd.BlitFilter))
        return E;
      break;
    case RecordedCommand::Kind::ResolveImage:
      if (Error E =
              runResolveImage(Cmd.SrcImage, Cmd.DstImage, Cmd.ResolveRegions))
        return E;
      break;
    case RecordedCommand::Kind::DrawIndirect:
    case RecordedCommand::Kind::DrawIndexedIndirect: {
      if (!BoundGraphicsPipeline)
        return createStringError(inconvertibleErrorCode(),
                                 "draw with no bound graphics pipeline");
      bool Indexed = Cmd.Op == RecordedCommand::Kind::DrawIndexedIndirect;
      Expected<std::vector<feme::graphics::DrawCommand>> Draws =
          readIndirectDraws(Cmd.IndirectBuffer, Cmd.IndirectOffset,
                            Cmd.Count[0], static_cast<uint32_t>(Cmd.DstSize),
                            Indexed);
      if (!Draws)
        return Draws.takeError();
      for (const feme::graphics::DrawCommand &Draw : *Draws)
        if (Error E = runValidatedDraw(*BoundGraphicsPipeline, Gfx, Draw,
                                       DeviceInfo, BoundSets, PushConstants))
          return E;
      break;
    }
    }
  }
  return Error::success();
}

} // namespace

llvm::Error feme::vulkan::executeCommandBuffer(const CommandBuffer &CmdBuf) {
  ComputePipeline *BoundPipeline = nullptr;
  GraphicsPipeline *BoundGraphicsPipeline = nullptr;
  GraphicsState Gfx;
  std::vector<BoundSetState> BoundSets;
  // Push-constant state, sized to the device's full advertised
  // `maxPushConstantsSize` and zero-initialized: a byte a `vkCmdPushConstants`
  // never wrote reads as zero, matching every other "declared but never
  // written" resource in this ICD (see "Descriptor Model").
  const PhysicalDeviceInfo *DeviceInfo = CmdBuf.getPhysicalDeviceInfo();
  std::vector<uint8_t> PushConstants(
      DeviceInfo ? DeviceInfo->Properties.limits.maxPushConstantsSize : 0, 0);
  return executeCommandsInto(CmdBuf.commands(), DeviceInfo, BoundPipeline,
                             BoundGraphicsPipeline, Gfx, BoundSets,
                             PushConstants);
}

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool) {
  // Only the single universal (graphics/compute/transfer) queue family
  // (index 0) exists -- see "Graphics queue family".
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
  if (pipelineBindPoint != VK_PIPELINE_BIND_POINT_COMPUTE &&
      pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS)
    return; // Ray tracing is V8.
  // Which bind point the object belongs to is recorded on the object itself
  // (`Pipeline::kind`), so the bind point argument only selects which
  // pipelines this ICD accepts at all.
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->bindPipeline(fromHandle<Pipeline>(pipeline));
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout, uint32_t firstSet, uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
    const uint32_t *pDynamicOffsets) {
  if (pipelineBindPoint != VK_PIPELINE_BIND_POINT_COMPUTE &&
      pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS)
    return; // Ray tracing is V8.
  // Both bind points share one set of bound descriptor sets here, matching
  // how a draw and a dispatch both materialize their resources from the
  // same `buildBoundResources` (see "Descriptor Model").
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

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(
    VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
    VkImageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions) {
  std::vector<VkBufferImageCopy> Regions(pRegions, pRegions + regionCount);
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->copyBufferToImage(fromHandle<vulkan::Buffer>(srcBuffer),
                          fromHandle<vulkan::Image>(dstImage),
                          std::move(Regions));
}

VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage,
                       VkImageLayout, VkBuffer dstBuffer, uint32_t regionCount,
                       const VkBufferImageCopy *pRegions) {
  std::vector<VkBufferImageCopy> Regions(pRegions, pRegions + regionCount);
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->copyImageToBuffer(fromHandle<vulkan::Image>(srcImage),
                          fromHandle<vulkan::Buffer>(dstBuffer),
                          std::move(Regions));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer commandBuffer,
                                          VkImage srcImage, VkImageLayout,
                                          VkImage dstImage, VkImageLayout,
                                          uint32_t regionCount,
                                          const VkImageCopy *pRegions) {
  std::vector<VkImageCopy> Regions(pRegions, pRegions + regionCount);
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->copyImage(fromHandle<vulkan::Image>(srcImage),
                  fromHandle<vulkan::Image>(dstImage), std::move(Regions));
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
    VkDependencyFlags, uint32_t, const VkMemoryBarrier *, uint32_t,
    const VkBufferMemoryBarrier *, uint32_t imageMemoryBarrierCount,
    const VkImageMemoryBarrier *pImageMemoryBarriers) {
  // (V5) Each image memory barrier's layout transition is recorded for
  // `executeCommandBuffer` to apply to its target image's tracked layout;
  // see `ImageLayoutTransition`'s comment for why the buffer/memory
  // barrier arrays still carry no payload.
  std::vector<ImageLayoutTransition> ImageBarriers;
  ImageBarriers.reserve(imageMemoryBarrierCount);
  for (uint32_t I = 0; I != imageMemoryBarrierCount; ++I) {
    const VkImageMemoryBarrier &Barrier = pImageMemoryBarriers[I];
    ImageBarriers.push_back(ImageLayoutTransition{
        fromHandle<vulkan::Image>(Barrier.image), Barrier.oldLayout,
        Barrier.newLayout, Barrier.subresourceRange});
  }
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->pipelineBarrier(std::move(ImageBarriers));
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
    const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t,
    const VkImageMemoryBarrier *) {
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
vkCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
                     const VkCommandBuffer *pCommandBuffers) {
  std::vector<const vulkan::CommandBuffer *> Secondary;
  Secondary.reserve(commandBufferCount);
  for (uint32_t I = 0; I != commandBufferCount; ++I)
    Secondary.push_back(fromHandle<vulkan::CommandBuffer>(pCommandBuffers[I]));
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->executeCommands(std::move(Secondary));
}

//===----------------------------------------------------------------------===//
// V6: render pass instances, vertex/index binding, dynamic state, draws
//===----------------------------------------------------------------------===//

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(
    VkCommandBuffer commandBuffer,
    const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents) {
  std::vector<VkClearValue> ClearValues(pRenderPassBegin->pClearValues,
                                        pRenderPassBegin->pClearValues +
                                            pRenderPassBegin->clearValueCount);
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->beginRenderPass(fromHandle<RenderPass>(pRenderPassBegin->renderPass),
                        fromHandle<Framebuffer>(pRenderPassBegin->framebuffer),
                        pRenderPassBegin->renderArea, std::move(ClearValues));
}

namespace {

/// Normalizes one `VkRenderingAttachmentInfo` into the render-target view
/// the internal binding holds. The format and sample count come from the
/// view's own image, so a dynamic-rendering attachment needs no separate
/// format declaration the way a `VkRenderPass` attachment does.
RenderTargetView
normalizeRenderingAttachment(const VkRenderingAttachmentInfo &Src) {
  RenderTargetView View;
  View.View = fromHandle<ImageView>(Src.imageView);
  if (View.View) {
    View.Format = View.View->format();
    if (View.View->image())
      View.SampleCount = View.View->image()->sampleCount();
  }
  View.LoadOp = Src.loadOp;
  View.StoreOp = Src.storeOp;
  View.ClearValue = Src.clearValue;
  if (Src.resolveMode != VK_RESOLVE_MODE_NONE)
    View.ResolveView = fromHandle<ImageView>(Src.resolveImageView);
  return View;
}

} // namespace

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderingKHR(
    VkCommandBuffer commandBuffer, const VkRenderingInfo *pRenderingInfo) {
  RenderTargetBinding Binding;
  Binding.RenderArea = pRenderingInfo->renderArea;
  Binding.Layers = pRenderingInfo->layerCount;
  for (uint32_t I = 0; I != pRenderingInfo->colorAttachmentCount; ++I)
    Binding.Colors.push_back(
        normalizeRenderingAttachment(pRenderingInfo->pColorAttachments[I]));
  if (pRenderingInfo->pDepthAttachment &&
      pRenderingInfo->pDepthAttachment->imageView)
    Binding.Depth =
        normalizeRenderingAttachment(*pRenderingInfo->pDepthAttachment);
  if (pRenderingInfo->pStencilAttachment &&
      pRenderingInfo->pStencilAttachment->imageView)
    Binding.Stencil =
        normalizeRenderingAttachment(*pRenderingInfo->pStencilAttachment);
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->beginRendering(std::move(Binding));
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderingKHR(VkCommandBuffer commandBuffer) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->endRenderPass();
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer commandBuffer,
                                            VkSubpassContents) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->nextSubpass();
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer commandBuffer) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->endRenderPass();
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(
    VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
    const VkBuffer *pBuffers, const VkDeviceSize *pOffsets) {
  std::vector<vulkan::Buffer *> Buffers;
  std::vector<VkDeviceSize> Offsets;
  Buffers.reserve(bindingCount);
  Offsets.reserve(bindingCount);
  for (uint32_t I = 0; I != bindingCount; ++I) {
    Buffers.push_back(fromHandle<vulkan::Buffer>(pBuffers[I]));
    Offsets.push_back(pOffsets ? pOffsets[I] : 0);
  }
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->bindVertexBuffers(firstBinding, std::move(Buffers), std::move(Offsets));
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                                                VkBuffer buffer,
                                                VkDeviceSize offset,
                                                VkIndexType indexType) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->bindIndexBuffer(fromHandle<vulkan::Buffer>(buffer), offset, indexType);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer commandBuffer,
                                            uint32_t firstViewport,
                                            uint32_t viewportCount,
                                            const VkViewport *pViewports) {
  // Only one viewport is advertised (`maxViewports` is 1), so a call naming
  // any other slot has nothing to set.
  if (firstViewport != 0 || viewportCount == 0)
    return;
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->setViewport(pViewports[0]);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer commandBuffer,
                                           uint32_t firstScissor,
                                           uint32_t scissorCount,
                                           const VkRect2D *pScissors) {
  if (firstScissor != 0 || scissorCount == 0)
    return;
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->setScissor(pScissors[0]);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(
    VkCommandBuffer commandBuffer, const float blendConstants[4]) {
  std::array<float, 4> Constants{blendConstants[0], blendConstants[1],
                                 blendConstants[2], blendConstants[3]};
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setBlendConstants(Constants);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilReference(VkCommandBuffer commandBuffer,
                         VkStencilFaceFlags faceMask, uint32_t reference) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setStencilState(RecordedCommand::Kind::SetStencilReference, faceMask,
                        reference);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                           VkStencilFaceFlags faceMask, uint32_t compareMask) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setStencilState(RecordedCommand::Kind::SetStencilCompareMask, faceMask,
                        compareMask);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                         VkStencilFaceFlags faceMask, uint32_t writeMask) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setStencilState(RecordedCommand::Kind::SetStencilWriteMask, faceMask,
                        writeMask);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer commandBuffer,
                                     uint32_t vertexCount,
                                     uint32_t instanceCount,
                                     uint32_t firstVertex,
                                     uint32_t firstInstance) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(
    VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
    uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset,
                    firstInstance);
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout,
    const VkClearColorValue *pColor, uint32_t rangeCount,
    const VkImageSubresourceRange *pRanges) {
  VkClearValue Value{};
  Value.color = *pColor;
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->clearImage(
          RecordedCommand::Kind::ClearColorImage, fromHandle<Image>(image),
          Value,
          std::vector<VkImageSubresourceRange>(pRanges, pRanges + rangeCount));
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout,
    const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount,
    const VkImageSubresourceRange *pRanges) {
  VkClearValue Value{};
  Value.depthStencil = *pDepthStencil;
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->clearImage(
          RecordedCommand::Kind::ClearDepthStencilImage,
          fromHandle<Image>(image), Value,
          std::vector<VkImageSubresourceRange>(pRanges, pRanges + rangeCount));
}

VKAPI_ATTR void VKAPI_CALL
vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                      const VkClearAttachment *pAttachments, uint32_t rectCount,
                      const VkClearRect *pRects) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->clearAttachments(std::vector<VkClearAttachment>(
                             pAttachments, pAttachments + attachmentCount),
                         std::vector<VkClearRect>(pRects, pRects + rectCount));
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer commandBuffer,
                                          VkImage srcImage, VkImageLayout,
                                          VkImage dstImage, VkImageLayout,
                                          uint32_t regionCount,
                                          const VkImageBlit *pRegions,
                                          VkFilter filter) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->blitImage(fromHandle<Image>(srcImage), fromHandle<Image>(dstImage),
                  std::vector<VkImageBlit>(pRegions, pRegions + regionCount),
                  filter);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer commandBuffer,
                                             VkImage srcImage, VkImageLayout,
                                             VkImage dstImage, VkImageLayout,
                                             uint32_t regionCount,
                                             const VkImageResolve *pRegions) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->resolveImage(
          fromHandle<Image>(srcImage), fromHandle<Image>(dstImage),
          std::vector<VkImageResolve>(pRegions, pRegions + regionCount));
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer commandBuffer,
                                             VkBuffer buffer,
                                             VkDeviceSize offset,
                                             uint32_t drawCount,
                                             uint32_t stride) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->drawIndirect(RecordedCommand::Kind::DrawIndirect,
                     fromHandle<vulkan::Buffer>(buffer), offset, drawCount,
                     stride);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
    uint32_t drawCount, uint32_t stride) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->drawIndirect(RecordedCommand::Kind::DrawIndexedIndirect,
                     fromHandle<vulkan::Buffer>(buffer), offset, drawCount,
                     stride);
}

} // namespace feme::vulkan
