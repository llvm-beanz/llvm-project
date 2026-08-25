//===- CommandBuffer.cpp - VkCommandPool/VkCommandBuffer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandBuffer.h"
#include "ASTCDecode.h"
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

/// (roadmap C4c) `vkCmdSetCullModeEXT`'s payload, converted the same way
/// `GraphicsPipeline.cpp`'s (static, creation-time) `mapCullMode` converts
/// `VkPipelineRasterizationStateCreateInfo::cullMode` -- duplicated rather
/// than shared, since that function is file-local to GraphicsPipeline.cpp
/// and a dynamic-state setter has no `Error` return to report an
/// unrecognized value through (matching `VK_EXT_extended_dynamic_state`'s
/// own contract: `vkCmdSetCullModeEXT` cannot fail).
feme::graphics::CullMode toCullMode(VkCullModeFlags Cull) {
  switch (Cull) {
  case VK_CULL_MODE_FRONT_BIT:
    return feme::graphics::CullMode::Front;
  case VK_CULL_MODE_BACK_BIT:
    return feme::graphics::CullMode::Back;
  case VK_CULL_MODE_FRONT_AND_BACK:
    return feme::graphics::CullMode::FrontAndBack;
  case VK_CULL_MODE_NONE:
  default:
    return feme::graphics::CullMode::None;
  }
}

feme::graphics::FrontFace toFrontFace(VkFrontFace Front) {
  return Front == VK_FRONT_FACE_CLOCKWISE
             ? feme::graphics::FrontFace::Clockwise
             : feme::graphics::FrontFace::CounterClockwise;
}

/// (roadmap C4c) `vkCmdSetDepthCompareOpEXT`'s payload, converted the same
/// way `GraphicsPipeline.cpp`'s (static) `mapCompareOp` converts
/// `VkPipelineDepthStencilStateCreateInfo::depthCompareOp` -- see
/// `toCullMode`'s comment on why this is a duplicate rather than a shared
/// helper. An unrecognized value (impossible from a conformant caller)
/// falls back to `Always`, matching every comparison passing.
feme::graphics::CompareOp toCompareOp(VkCompareOp Op) {
  switch (Op) {
  case VK_COMPARE_OP_NEVER:
    return feme::graphics::CompareOp::Never;
  case VK_COMPARE_OP_LESS:
    return feme::graphics::CompareOp::Less;
  case VK_COMPARE_OP_EQUAL:
    return feme::graphics::CompareOp::Equal;
  case VK_COMPARE_OP_LESS_OR_EQUAL:
    return feme::graphics::CompareOp::LessEqual;
  case VK_COMPARE_OP_GREATER:
    return feme::graphics::CompareOp::Greater;
  case VK_COMPARE_OP_NOT_EQUAL:
    return feme::graphics::CompareOp::NotEqual;
  case VK_COMPARE_OP_GREATER_OR_EQUAL:
    return feme::graphics::CompareOp::GreaterEqual;
  case VK_COMPARE_OP_ALWAYS:
  default:
    return feme::graphics::CompareOp::Always;
  }
}

/// (roadmap C4c) `vkCmdSetStencilOpEXT`'s payload, converted the same way
/// `GraphicsPipeline.cpp`'s (static) `mapStencilOp` converts a
/// `VkStencilOpState`'s op fields -- see `toCullMode`'s comment.
feme::graphics::StencilOp toStencilOp(VkStencilOp Op) {
  switch (Op) {
  case VK_STENCIL_OP_ZERO:
    return feme::graphics::StencilOp::Zero;
  case VK_STENCIL_OP_REPLACE:
    return feme::graphics::StencilOp::Replace;
  case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
    return feme::graphics::StencilOp::IncrementClamp;
  case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
    return feme::graphics::StencilOp::DecrementClamp;
  case VK_STENCIL_OP_INVERT:
    return feme::graphics::StencilOp::Invert;
  case VK_STENCIL_OP_INCREMENT_AND_WRAP:
    return feme::graphics::StencilOp::IncrementWrap;
  case VK_STENCIL_OP_DECREMENT_AND_WRAP:
    return feme::graphics::StencilOp::DecrementWrap;
  case VK_STENCIL_OP_KEEP:
  default:
    return feme::graphics::StencilOp::Keep;
  }
}

/// (roadmap C4c) `vkCmdSetPrimitiveTopologyEXT`'s payload, converted the
/// same triangle-class-only way `GraphicsPipeline.cpp`'s (static)
/// `mapTopology` converts `VkPipelineInputAssemblyStateCreateInfo::
/// topology` -- see `toCullMode`'s comment on why this is a duplicate.
/// `std::nullopt` here means "outside the triangle class this executor
/// implements"; see `DynamicGraphicsState::Topology`'s own comment on why
/// that is only ever a defensive case, never one a conformant caller
/// actually reaches.
std::optional<feme::graphics::PrimitiveTopology>
toDynamicTopology(VkPrimitiveTopology Topology) {
  switch (Topology) {
  case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
    return feme::graphics::PrimitiveTopology::PointList;
  case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
    return feme::graphics::PrimitiveTopology::LineList;
  case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
    return feme::graphics::PrimitiveTopology::LineStrip;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
    return feme::graphics::PrimitiveTopology::TriangleList;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
    return feme::graphics::PrimitiveTopology::TriangleStrip;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
    return feme::graphics::PrimitiveTopology::TriangleFan;
  default:
    return std::nullopt;
  }
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

  /// (Roadmap E23) Per-texel RGBA8 storage `materializeImageDescriptor`
  /// decodes an ASTC LDR-format image into, since the CPU runtime
  /// (feme/runtime/CPU/FeMeRuntimeCPU.c) has no block-compressed case of
  /// its own. Element order has no relationship to `ImageStorage`'s; a
  /// descriptor's `Dst.Data` simply points into whichever entry here
  /// backs it. `std::vector`'s move (on this outer vector's own growth)
  /// never invalidates a pointer into an inner vector's buffer, so an
  /// already-materialized descriptor stays valid regardless of how many
  /// more entries are appended afterward.
  std::vector<std::vector<uint8_t>> DecodedImageStorage;
  std::vector<std::vector<feme::cpu::FemeImageSubresourceLayout>>
      DecodedImageLayoutStorage;
};

/// Whether \p Format is one of the 14 `_SRGB` ASTC LDR footprints rather
/// than its `_UNORM` counterpart -- decides which already-supported
/// `feme::cpu::ResourceFormat` `decodeASTCImageForSampling` below reports
/// its decoded, per-texel RGBA8 output as (`R8G8B8A8_UNORM_SRGB` applies
/// the sRGB decode curve at sample time the same way it already does for a
/// real `R8G8B8A8_UNORM_SRGB` image; `R8G8B8A8_UNORM` does not).
bool isASTCSRGBFormat(feme::cpu::ResourceFormat Format) {
  switch (Format) {
  case feme::cpu::ResourceFormat::ASTC_4x4_SRGB:
  case feme::cpu::ResourceFormat::ASTC_5x4_SRGB:
  case feme::cpu::ResourceFormat::ASTC_5x5_SRGB:
  case feme::cpu::ResourceFormat::ASTC_6x5_SRGB:
  case feme::cpu::ResourceFormat::ASTC_6x6_SRGB:
  case feme::cpu::ResourceFormat::ASTC_8x5_SRGB:
  case feme::cpu::ResourceFormat::ASTC_8x6_SRGB:
  case feme::cpu::ResourceFormat::ASTC_8x8_SRGB:
  case feme::cpu::ResourceFormat::ASTC_10x5_SRGB:
  case feme::cpu::ResourceFormat::ASTC_10x6_SRGB:
  case feme::cpu::ResourceFormat::ASTC_10x8_SRGB:
  case feme::cpu::ResourceFormat::ASTC_10x10_SRGB:
  case feme::cpu::ResourceFormat::ASTC_12x10_SRGB:
  case feme::cpu::ResourceFormat::ASTC_12x12_SRGB:
    return true;
  default:
    return false;
  }
}

/// The per-texel RGBA8 image a `decodeASTCImageForSampling` call produces:
/// decoded texel bytes plus the per-texel `FemeImageSubresourceLayout`
/// table describing them, both owned by whichever
/// `MaterializedBoundResources` storage vector they get moved into.
struct DecodedASTCImage {
  std::vector<uint8_t> Texels;
  std::vector<feme::cpu::FemeImageSubresourceLayout> MipLayouts;
};

/// (Roadmap E23) Decodes mip levels `[BaseMip, BaseMip + LevelCount)`,
/// array layer 0 only (matching `materializeImageDescriptor`'s own
/// Texture2D-only, layer-0-only scope), of ASTC LDR-format image \p Img
/// into a per-texel RGBA8 buffer `feme::vulkan::decodeASTCBlock` produces
/// one block at a time -- the "bridge the image-descriptor-materialization
/// path back into ASTCDecode.h" option this row's own roadmap text
/// describes, chosen over porting a second decoder into the CPU runtime
/// (feme/runtime/CPU/FeMeRuntimeCPU.c) since that runtime's existing
/// `R8G8B8A8_UNORM`/`_UNORM_SRGB` unpack path already reads exactly this
/// shape of data unmodified.
DecodedASTCImage decodeASTCImageForSampling(const Image *Img, uint32_t BaseMip,
                                            uint32_t LevelCount) {
  DecodedASTCImage Result;
  uint32_t BlockW = blockWidth(Img->format());
  uint32_t BlockH = blockHeight(Img->format());

  // First pass: lay out every level's offset/pitch so `Texels` can be
  // allocated once, rather than grown level by level.
  std::vector<std::pair<uint32_t, uint32_t>> LevelExtents(LevelCount);
  Result.MipLayouts.resize(LevelCount);
  uint64_t Offset = 0;
  for (uint32_t L = 0; L != LevelCount; ++L) {
    uint32_t Level = BaseMip + L;
    uint32_t W = std::max(1u, Img->width() >> Level);
    uint32_t H = std::max(1u, Img->height() >> Level);
    LevelExtents[L] = {W, H};
    uint64_t RowPitch = uint64_t(W) * 4;
    uint64_t SlicePitch = RowPitch * H;
    Result.MipLayouts[L] = {Offset, RowPitch, SlicePitch, SlicePitch};
    Offset += SlicePitch;
  }
  Result.Texels.resize(Offset);

  // Reused across every block decoded below, rather than reallocated per
  // block -- the same pattern `ImageOps.cpp`'s `runBlitImage` uses for its
  // own `decodeASTCBlock` calls.
  std::vector<uint8_t> BlockBuf(size_t(BlockW) * BlockH * 4);
  for (uint32_t L = 0; L != LevelCount; ++L) {
    auto [W, H] = LevelExtents[L];
    uint32_t BlocksX = (W + BlockW - 1) / BlockW;
    uint32_t BlocksY = (H + BlockH - 1) / BlockH;
    uint8_t *LevelBase = Result.Texels.data() + Result.MipLayouts[L].Offset;
    uint64_t RowPitch = Result.MipLayouts[L].RowPitch;
    for (uint32_t BY = 0; BY != BlocksY; ++BY) {
      for (uint32_t BX = 0; BX != BlocksX; ++BX) {
        const auto *Block = static_cast<const uint8_t *>(
            Img->blockPointer(BaseMip + L, /*ArrayLayer=*/0, BX, BY, /*Z=*/0));
        decodeASTCBlock(Block, BlockW, BlockH, BlockBuf.data());
        // A non-integer-multiple mip extent's rightmost/bottommost block
        // only partially covers the image -- copy just the in-bounds
        // rows/columns of it, per the specification's own "a block may
        // extend past the image edge" allowance.
        uint32_t CopyW = std::min(BlockW, W - BX * BlockW);
        uint32_t CopyH = std::min(BlockH, H - BY * BlockH);
        for (uint32_t Y = 0; Y != CopyH; ++Y) {
          uint8_t *DstRow = LevelBase + uint64_t(BY * BlockH + Y) * RowPitch +
                            uint64_t(BX) * BlockW * 4;
          const uint8_t *SrcRow = &BlockBuf[size_t(Y) * BlockW * 4];
          std::memcpy(DstRow, SrcRow, size_t(CopyW) * 4);
        }
      }
    }
  }
  return Result;
}

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
///
/// Roadmap E23: an ASTC LDR-format image is decoded whole (every sampled
/// mip level) into `Result`'s own per-texel RGBA8 storage before this
/// function returns, and \p Dst points into *that* rather than \p Img's
/// own raw block-compressed bytes -- the CPU runtime
/// (feme/runtime/CPU/FeMeRuntimeCPU.c) that eventually reads \p Dst has no
/// block-compressed case of its own (see this file's header comment), so a
/// shader-visible descriptor must already be decoded before it gets there.
/// An HDR ASTC format (`feme::cpu::isASTCLdrFormat` false) is left
/// unsupported the same "reads as all-zero" way it already was -- outside
/// this row's own LDR-only scope (`decodeASTCBlockHDR`'s float-producing
/// interface does not fit this RGBA8 bridge).
void materializeImageDescriptor(const DescriptorImageBinding &Src,
                                VkDescriptorType Type,
                                MaterializedBoundResources &Result,
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

  Dst.Dimension = static_cast<uint32_t>(feme::cpu::ImageDimension::Texture2D);
  Dst.Width = std::max(1u, Img->width() >> Range.baseMipLevel);
  Dst.Height = std::max(1u, Img->height() >> Range.baseMipLevel);
  Dst.Depth = 1;
  Dst.MipLevels = LevelCount;
  Dst.ArrayLayers = 1;
  Dst.PlaneCount = 1;
  Dst.SampleCount = Img->sampleCount();
  Dst.Flags = isReadOnlyDescriptorType(Type) ? feme::cpu::FEME_IMAGE_SAMPLED
                                             : feme::cpu::FEME_IMAGE_STORAGE;

  if (feme::cpu::isASTCLdrFormat(Img->format())) {
    DecodedASTCImage Decoded =
        decodeASTCImageForSampling(Img, Range.baseMipLevel, LevelCount);
    Result.DecodedImageStorage.push_back(std::move(Decoded.Texels));
    Result.DecodedImageLayoutStorage.push_back(std::move(Decoded.MipLayouts));
    Dst.Data = Result.DecodedImageStorage.back().data();
    Dst.SizeInBytes = Result.DecodedImageStorage.back().size();
    Dst.Format = static_cast<uint32_t>(
        isASTCSRGBFormat(Img->format())
            ? feme::cpu::ResourceFormat::R8G8B8A8_UNORM_SRGB
            : feme::cpu::ResourceFormat::R8G8B8A8_UNORM);
    Dst.MipLayouts = Result.DecodedImageLayoutStorage.back().data();
    Dst.MipLayoutCount = LevelCount;
    return;
  }

  Dst.Data = Img->data();
  Dst.SizeInBytes = Img->sizeInBytes();
  Dst.Format = static_cast<uint32_t>(View->format());
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
      materializeImageDescriptor(Array[J], BindingDecl.Type, Result,
                                 Descriptors[J]);
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

/// (V5) One `VkBufferImageCopy` region's byte copy between \p Img's own
/// packed subresource layout and a flat buffer region, in either direction
/// (\p ToImage selects which). `bufferRowLength`/`bufferImageHeight` of 0
/// mean "tightly packed to the copy's own extent", per the specification.
/// Copies row by row rather than as one contiguous `memcpy`, since the
/// image's row/slice pitch need not match the buffer's (a non-zero
/// `bufferRowLength`/`bufferImageHeight`, or simply a mip level narrower
/// than level 0, both make them differ).
///
/// Roadmap E22: for a block-compressed `Img`, a "row" is a row of whole
/// blocks rather than of texels -- `UnitSize` (`bytesPerBlock`, Format.h)
/// stands in for a texel's `formatElementSize` either way (the two are
/// equal for a non-block-compressed format, so this generalizes rather
/// than branches: `blockWidth`/`blockHeight` fall back to `{1, 1}`, making
/// every `.../BlockWidth`-shaped ceiling division below a no-op), and
/// `Img.blockPointer` addresses each row's first block instead of
/// `texelPointer`'s first texel. Real Vulkan requires a block-compressed
/// copy's own offset/extent to already be block-aligned
/// (`VUID-vkCmdCopyBufferToImage-imageOffset-07738`'s family); this ICD
/// does not re-validate that any more than it validates other VUIDs (see
/// Image.h's file comment on that precedent), so the ceiling division here
/// is exact for spec-conformant input and only "generously" rounds up an
/// out-of-spec one rather than under-copying it.
Error copyBufferImageRegion(Image &Img, bool ToImage, void *BufferBase,
                            VkDeviceSize BufferSize,
                            const VkBufferImageCopy &Region) {
  bool Compressed = feme::cpu::isBlockCompressedFormat(Img.format());
  uint32_t BlockW = blockWidth(Img.format());
  uint32_t BlockH = blockHeight(Img.format());
  uint32_t UnitSize = bytesPerBlock(Img.format());
  uint32_t RowLength = Region.bufferRowLength ? Region.bufferRowLength
                                              : Region.imageExtent.width;
  uint32_t ImageHeight = Region.bufferImageHeight ? Region.bufferImageHeight
                                                  : Region.imageExtent.height;
  uint32_t RowUnits = (RowLength + BlockW - 1) / BlockW;
  uint32_t HeightUnits = (ImageHeight + BlockH - 1) / BlockH;
  uint32_t ExtentWidthUnits = (Region.imageExtent.width + BlockW - 1) / BlockW;
  uint32_t ExtentHeightUnits =
      (Region.imageExtent.height + BlockH - 1) / BlockH;
  uint32_t OffsetXUnits = uint32_t(Region.imageOffset.x) / BlockW;
  uint32_t OffsetYUnits = uint32_t(Region.imageOffset.y) / BlockH;

  uint64_t BufferRowBytes = uint64_t(RowUnits) * UnitSize;
  uint64_t BufferSliceBytes = BufferRowBytes * HeightUnits;
  uint32_t MipLevel = Region.imageSubresource.mipLevel;
  if (MipLevel >= Img.mipLevels())
    return createStringError(inconvertibleErrorCode(),
                             "buffer/image copy mip level is out of range");

  uint32_t LayerCount =
      Img.resolvedLayerCount(Region.imageSubresource.baseArrayLayer,
                             Region.imageSubresource.layerCount);
  for (uint32_t Layer = 0; Layer != LayerCount; ++Layer) {
    uint32_t ArrayLayer = Region.imageSubresource.baseArrayLayer + Layer;
    for (uint32_t Z = 0; Z != Region.imageExtent.depth; ++Z) {
      uint64_t SliceIndex = uint64_t(Layer) * Region.imageExtent.depth + Z;
      for (uint32_t Y = 0; Y != ExtentHeightUnits; ++Y) {
        uint64_t BufferOffset = Region.bufferOffset +
                                SliceIndex * BufferSliceBytes +
                                uint64_t(Y) * BufferRowBytes;
        uint64_t RowBytes = uint64_t(ExtentWidthUnits) * UnitSize;
        if (BufferOffset + RowBytes > BufferSize)
          return createStringError(inconvertibleErrorCode(),
                                   "buffer/image copy region is out of "
                                   "range of its buffer");
        auto *BufferRow = static_cast<uint8_t *>(BufferBase) + BufferOffset;
        void *ImageRow =
            Compressed
                ? Img.blockPointer(MipLevel, ArrayLayer, OffsetXUnits,
                                   OffsetYUnits + Y, Region.imageOffset.z + Z)
                : Img.texelPointer(MipLevel, ArrayLayer, OffsetXUnits,
                                   OffsetYUnits + Y, Region.imageOffset.z + Z);
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
/// Both images must share the same texel/block size and sample count,
/// matching real Vulkan's own "compatible formats" copy rule
/// (`VUID-vkCmdCopyImage-srcImage-01548`): no value conversion takes place
/// on either side, in this ICD or a real one -- `vkCmdCopyImage` reinterprets
/// bits, it never converts them (that is what a shader's format-aware
/// load/store or a blit does). Every sample of a multisample region is
/// copied verbatim; there is no resolve here either (that is
/// `vkCmdResolveImage`, not yet implemented -- V6+).
///
/// Roadmap E22: `UnitSize` (`bytesPerBlock`, Format.h) replaces
/// `formatElementSize` so a block-compressed pair copies correctly (its
/// `formatElementSize` is meaningless, always 0 -- see
/// `isBlockCompressedFormat`'s comment); `blockWidth`/`blockHeight` convert
/// \p Region's texel extent/offset to the block grid `blockPointer`
/// addresses, falling back to `{1, 1}` (a no-op ceiling division) for a
/// non-block-compressed format, so this one implementation now covers
/// both families, exactly the pattern Format.h's own helpers are designed
/// for.
Error runCopyImage(Image *Src, Image *Dst,
                   llvm::ArrayRef<VkImageCopy> Regions) {
  if (!Src || !Src->isBound() || !Dst || !Dst->isBound())
    return createStringError(inconvertibleErrorCode(),
                             "image copy source/destination is not bound");
  // Roadmap E24: `SrcCompressed`/`DstCompressed` are now tracked
  // independently -- a single `Compressed` flag derived from `Src` alone
  // and applied to *both* sides asserted inside `Dst->blockPointer` the
  // moment a real `dEQP-VK.api.copy_and_blit.*` case (unreachable before
  // E24 made `vkGetPhysicalDeviceImageFormatProperties` answer honestly
  // enough for CTS to create a compressed/uncompressed image pair at all)
  // copied a block-compressed source into an uncompressed destination of
  // matching block/texel byte size (e.g. one ASTC block <-> one
  // `R32G32B32A32_UINT` texel, both 16 bytes) -- a real Vulkan-legal copy
  // this ICD's own "compatible formats" support (this file's own comment)
  // already claimed to allow.
  bool SrcCompressed = feme::cpu::isBlockCompressedFormat(Src->format());
  bool DstCompressed = feme::cpu::isBlockCompressedFormat(Dst->format());
  uint32_t UnitSize = bytesPerBlock(Src->format());
  if (UnitSize != bytesPerBlock(Dst->format()))
    return createStringError(inconvertibleErrorCode(),
                             "vkCmdCopyImage between formats of differing "
                             "texel/block size is not supported");
  if (Src->sampleCount() != Dst->sampleCount())
    return createStringError(inconvertibleErrorCode(),
                             "vkCmdCopyImage between images of differing "
                             "sample counts is not supported");
  // `Region.extent`/`srcOffset` are always in the source image's own
  // texel/block units (Vulkan's own rule for a copy between a compressed
  // and an uncompressed format); `dstOffset` is in the destination's own
  // units, which only differ from the source's when the two block shapes
  // differ (e.g. a block-compressed source paired with an uncompressed,
  // or differently-shaped block-compressed, destination).
  uint32_t SrcBlockW = blockWidth(Src->format());
  uint32_t SrcBlockH = blockHeight(Src->format());
  uint32_t DstBlockW = blockWidth(Dst->format());
  uint32_t DstBlockH = blockHeight(Dst->format());
  // Every sample of one texel is stored contiguously
  // (`FemeImageSubresourceLayout::SampleStride == TexelSize`, see Image.cpp's
  // `computeSubresourceLayouts`), so one row's `SampleCount` samples of a
  // region's texels are themselves one contiguous span -- a single
  // `memcpy` per row moves every sample, there is no need to loop over
  // samples separately the way looping over `Y`/`Z`/array layer does. A
  // block-compressed image's `SampleCount` is always 1 (never
  // multisampled in real Vulkan), so this multiplies by 1 for one.
  uint32_t SampleCount = Src->sampleCount();
  bool SrcIs3D = Src->type() == VK_IMAGE_TYPE_3D;
  bool DstIs3D = Dst->type() == VK_IMAGE_TYPE_3D;
  for (const VkImageCopy &Region : Regions) {
    if (Region.srcSubresource.mipLevel >= Src->mipLevels() ||
        Region.dstSubresource.mipLevel >= Dst->mipLevels())
      return createStringError(inconvertibleErrorCode(),
                               "image copy mip level is out of range");
    uint32_t WidthUnits = (Region.extent.width + SrcBlockW - 1) / SrcBlockW;
    uint32_t HeightUnits = (Region.extent.height + SrcBlockH - 1) / SrcBlockH;
    uint32_t SrcOffsetXUnits = uint32_t(Region.srcOffset.x) / SrcBlockW;
    uint32_t SrcOffsetYUnits = uint32_t(Region.srcOffset.y) / SrcBlockH;
    uint32_t DstOffsetXUnits = uint32_t(Region.dstOffset.x) / DstBlockW;
    uint32_t DstOffsetYUnits = uint32_t(Region.dstOffset.y) / DstBlockH;
    uint64_t RowBytes = uint64_t(WidthUnits) * UnitSize * SampleCount;
    uint32_t LayerCount = Src->resolvedLayerCount(
        Region.srcSubresource.baseArrayLayer, Region.srcSubresource.layerCount);
    // A copy between a 3D image and a 2D (array) image treats the 3D
    // image's `extent.depth` slices as the 2D image's `layerCount` layers
    // (real Vulkan's own "Image Copies" rule): whichever side is 3D steps
    // through `Region.*Offset.z`, and whichever side is not steps through
    // its own `baseArrayLayer` instead -- never both, and never neither,
    // since exactly one of `LayerCount`/`Region.extent.depth` is greater
    // than 1 for any legal region. A same-dimensionality (2D-to-2D or
    // 3D-to-3D) copy is the special case where both sides agree, which
    // this same formula also computes correctly.
    uint32_t SliceCount =
        (SrcIs3D || DstIs3D) ? Region.extent.depth : LayerCount;
    for (uint32_t S = 0; S != SliceCount; ++S) {
      uint32_t SrcLayer =
          Region.srcSubresource.baseArrayLayer + (SrcIs3D ? 0 : S);
      uint32_t SrcZ = Region.srcOffset.z + (SrcIs3D ? S : 0);
      uint32_t DstLayer =
          Region.dstSubresource.baseArrayLayer + (DstIs3D ? 0 : S);
      uint32_t DstZ = Region.dstOffset.z + (DstIs3D ? S : 0);
      for (uint32_t Y = 0; Y != HeightUnits; ++Y) {
        void *SrcRow =
            SrcCompressed
                ? Src->blockPointer(Region.srcSubresource.mipLevel, SrcLayer,
                                    SrcOffsetXUnits, SrcOffsetYUnits + Y, SrcZ)
                : Src->texelPointer(Region.srcSubresource.mipLevel, SrcLayer,
                                    SrcOffsetXUnits, SrcOffsetYUnits + Y, SrcZ);
        void *DstRow =
            DstCompressed
                ? Dst->blockPointer(Region.dstSubresource.mipLevel, DstLayer,
                                    DstOffsetXUnits, DstOffsetYUnits + Y, DstZ)
                : Dst->texelPointer(Region.dstSubresource.mipLevel, DstLayer,
                                    DstOffsetXUnits, DstOffsetYUnits + Y, DstZ);
        std::memcpy(DstRow, SrcRow, RowBytes);
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
/// exactly as `vkGetQueryPoolResults` does.
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
    uint64_t Value = Pool->value(FirstQuery + I);
    if (Is64Bit)
      std::memcpy(Out, &Value, sizeof(Value));
    else {
      uint32_t Value32 = static_cast<uint32_t>(Value);
      std::memcpy(Out, &Value32, sizeof(Value32));
    }
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
  /// The attachment views the current render-pass instance actually binds:
  /// `Fb->attachments()`, unless `Fb` is imageless (roadmap C6), in which
  /// case this is `vkCmdBeginRenderPass`'s own
  /// `VkRenderPassAttachmentBeginInfo` views instead.
  llvm::ArrayRef<ImageView *> FbAttachments;
  uint32_t Subpass = 0;
  VkRect2D RenderArea{};
  std::vector<VkClearValue> ClearValues;
  bool Rendering = false;
  RenderTargetBinding Binding;

  std::vector<Buffer *> VertexBuffers;
  std::vector<VkDeviceSize> VertexBufferOffsets;
  // (roadmap C4c) `vkCmdBindVertexBuffers2EXT`'s optional `pStrides`:
  // populated (and consulted) only when the bound pipeline declared
  // `VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE` -- see
  // `runDraw`'s vertex-fetch loop.
  std::vector<VkDeviceSize> VertexBufferStrides;
  Buffer *IndexBuffer = nullptr;
  VkDeviceSize IndexBufferOffset = 0;
  /// (Roadmap E5) `vkCmdBindIndexBuffer2`'s `size`, or `VK_WHOLE_SIZE` for
  /// a plain `vkCmdBindIndexBuffer` -- matching that command's own "bind
  /// through the end of the buffer" behavior.
  VkDeviceSize IndexBufferSize = VK_WHOLE_SIZE;
  VkIndexType IndexType = VK_INDEX_TYPE_UINT32;

  DynamicGraphicsState Dynamic;
};

/// Builds the normalized render-target binding \p Subpass of \p Pass
/// resolves to against \p Attachments -- the single internal shape
/// `vkCmdBeginRendering` also produces (see RenderPass.h). \p Attachments is
/// \p Fb's own views, unless \p Fb is imageless (roadmap C6), in which case
/// it is whatever `vkCmdBeginRenderPass`'s `VkRenderPassAttachmentBeginInfo`
/// supplied for this render-pass instance -- validated here, since an
/// imageless framebuffer has no views of its own to have validated at
/// creation time.
Expected<RenderTargetBinding>
buildRenderTargetBinding(const RenderPass &Pass, const Framebuffer &Fb,
                         llvm::ArrayRef<ImageView *> Attachments,
                         uint32_t Subpass, VkRect2D RenderArea,
                         llvm::ArrayRef<VkClearValue> ClearValues) {
  if (Subpass >= Pass.subpasses().size())
    return createStringError(inconvertibleErrorCode(),
                             "subpass %u is out of range of its render pass",
                             Subpass);
  if (Fb.isImageless()) {
    if (Attachments.size() != Pass.attachments().size())
      return createStringError(inconvertibleErrorCode(),
                               "an imageless framebuffer's render pass "
                               "instance did not supply one image view per "
                               "attachment");
    for (size_t I = 0; I != Attachments.size(); ++I)
      if (!isCompatibleAttachmentView(Pass.attachments()[I], Attachments[I],
                                      Fb.width(), Fb.height()))
        return createStringError(inconvertibleErrorCode(),
                                 "an imageless framebuffer's render pass "
                                 "instance supplied an image view "
                                 "incompatible with attachment %zu",
                                 I);
  }
  const SubpassDescription &Desc = Pass.subpasses()[Subpass];
  RenderTargetBinding Binding;
  Binding.RenderArea = RenderArea;

  auto makeView = [&](uint32_t Index, bool UseStencilOps) -> RenderTargetView {
    const AttachmentDescription &Attachment = Pass.attachments()[Index];
    RenderTargetView View;
    View.View = Attachments[Index];
    View.Format = Attachment.Format;
    View.SampleCount = Attachment.SampleCount;
    View.LoadOp = UseStencilOps ? Attachment.StencilLoadOp : Attachment.LoadOp;
    View.StoreOp =
        UseStencilOps ? Attachment.StencilStoreOp : Attachment.StoreOp;
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
    RenderTargetView View = makeView(Index, /*UseStencilOps=*/false);
    if (I < Desc.ResolveAttachments.size() &&
        Desc.ResolveAttachments[I] != VK_ATTACHMENT_UNUSED)
      View.ResolveView = Attachments[Desc.ResolveAttachments[I]];
    Binding.Colors.push_back(View);
  }
  if (Desc.DepthStencilAttachment != VK_ATTACHMENT_UNUSED) {
    uint32_t Index = Desc.DepthStencilAttachment;
    feme::cpu::ResourceFormat Format = Pass.attachments()[Index].Format;
    // A combined format (`D24_UNORM_S8_UINT`, roadmap C1) binds both
    // halves, each with its own load/store op but sharing the same
    // underlying image; a pure depth or pure stencil format binds only
    // the matching half.
    if (isSupportedDepthAttachmentFormat(Format))
      Binding.Depth = makeView(Index, /*UseStencilOps=*/false);
    if (isSupportedStencilAttachmentFormat(Format))
      Binding.Stencil = makeView(Index, /*UseStencilOps=*/true);
  }
  return Binding;
}

/// Which half of a render-target attachment `applyClear`/`applyStoreOps`
/// operate on -- distinct from the attachment's `Format` alone once a
/// combined depth+stencil format (`D24_UNORM_S8_UINT`, roadmap C1) means
/// the same format backs two different `RenderTargetView`s sharing one
/// underlying image.
enum class AttachmentKind { Color, Depth, Stencil };

/// Applies one attachment's `VK_ATTACHMENT_LOAD_OP_CLEAR` over \p Area,
/// which is the render area rather than the whole attachment: Vulkan clears
/// exactly what the render pass instance covers.
Error applyClear(const RenderTargetView &View, uint32_t SampleCount,
                 const VkRect2D &Area, AttachmentKind Kind) {
  if (!View.View)
    // (Roadmap E5) `VK_KHR_maintenance5`: an unused
    // (`VK_NULL_HANDLE`-imageView) dynamic-rendering attachment performs no
    // load, regardless of what `LoadOp` it was given.
    return Error::success();
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

  // For a combined depth+stencil format, `Depth`/`Stencil` clears must be a
  // per-pixel read-modify-write of the shared word (a precomputed, uniform
  // texel would clobber whatever the other aspect already wrote to that
  // pixel); pack directly into each pixel's own bytes instead.
  std::vector<uint8_t> UniformTexel;
  if (Kind == AttachmentKind::Color) {
    UniformTexel.resize(*ElemSize);
    std::array<double, 4> Color{
        View.ClearValue.color.float32[0], View.ClearValue.color.float32[1],
        View.ClearValue.color.float32[2], View.ClearValue.color.float32[3]};
    if (Error E = feme::graphics::packClearColor(Attachment->Format, Color,
                                                 UniformTexel))
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
        llvm::MutableArrayRef<uint8_t> Texel(Attachment->Data.data() + Offset,
                                             *ElemSize);
        switch (Kind) {
        case AttachmentKind::Color:
          std::memcpy(Texel.data(), UniformTexel.data(), *ElemSize);
          break;
        case AttachmentKind::Depth:
          if (Error E = feme::graphics::packDepthClear(
                  Attachment->Format, View.ClearValue.depthStencil.depth,
                  Texel))
            return E;
          break;
        case AttachmentKind::Stencil:
          if (Error E = feme::graphics::packStencilClear(
                  Attachment->Format, View.ClearValue.depthStencil.stencil,
                  Texel))
            return E;
          break;
        }
      }
  return Error::success();
}

/// Applies every attachment's load op when a render pass instance begins.
Error applyLoadOps(const RenderTargetBinding &Binding) {
  for (const RenderTargetView &View : Binding.Colors)
    if (Error E = applyClear(View, View.SampleCount, Binding.RenderArea,
                             AttachmentKind::Color))
      return E;
  if (Binding.Depth)
    if (Error E = applyClear(*Binding.Depth, Binding.Depth->SampleCount,
                             Binding.RenderArea, AttachmentKind::Depth))
      return E;
  if (Binding.Stencil)
    if (Error E = applyClear(*Binding.Stencil, Binding.Stencil->SampleCount,
                             Binding.RenderArea, AttachmentKind::Stencil))
      return E;
  return Error::success();
}

/// (roadmap C4c) `BindingDecl`'s effective stride for this draw: its own
/// static `Stride` unless \p Pipeline declared
/// `VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE` dynamic *and*
/// `vkCmdBindVertexBuffers2EXT` actually supplied one for this binding
/// (`VK_WHOLE_SIZE` marks a slot it didn't -- see the `BindVertexBuffers`
/// replay case). Shared by the vertex-fetch loop and its own bounds check
/// below, so the two can never disagree about which stride a draw used.
uint32_t resolveVertexBindingStride(const GraphicsPipeline &Pipeline,
                                    const GraphicsState &Gfx,
                                    const VertexInputBinding &BindingDecl) {
  if (Pipeline.isDynamic(DynamicStateVertexInputBindingStride) &&
      BindingDecl.Binding < Gfx.VertexBufferStrides.size() &&
      Gfx.VertexBufferStrides[BindingDecl.Binding] != VK_WHOLE_SIZE)
    return static_cast<uint32_t>(Gfx.VertexBufferStrides[BindingDecl.Binding]);
  return BindingDecl.Stride;
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
              llvm::ArrayRef<uint8_t> PushConstants,
              llvm::ArrayRef<QueryPool *> ActiveOcclusionQueries) {
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
    if (!View.View) {
      // (Roadmap E5) `VK_KHR_maintenance5`: a `VkRenderingAttachmentInfo`
      // with `imageView == VK_NULL_HANDLE` is a color slot that is present
      // (it still counts against the pipeline's `colorAttachmentCount`)
      // but unused -- nothing is read from or written to it, so it needs
      // neither a bound image nor a sample-count match with the pipeline.
      // An empty `Data` member is this executor's existing "not bound"
      // convention (see PreparedDraw.h's `DepthStencilAttachment` comment).
      Attachments.push_back(feme::graphics::AttachmentView{});
      continue;
    }
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
      if (!View.View) {
        // An unused attachment slot never resolves either way (its
        // `resolveMode` is ignored, per `VkRenderingAttachmentInfo`'s
        // spec), regardless of whether the others do.
        ResolveAttachments.push_back(feme::graphics::AttachmentView{});
        continue;
      }
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
    VB.Stride = resolveVertexBindingStride(Pipeline, Gfx, BindingDecl);
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
    // (Roadmap E5) `vkCmdBindIndexBuffer2`'s `size` bounds how much of the
    // buffer past `offset` is actually bound; `VK_WHOLE_SIZE` (also what a
    // plain `vkCmdBindIndexBuffer` bind always uses) means "through the
    // end of the buffer", matching that command's pre-existing "whole
    // buffer" assumption.
    VkDeviceSize BoundSize =
        Gfx.IndexBufferSize == VK_WHOLE_SIZE
            ? Gfx.IndexBuffer->size() - Gfx.IndexBufferOffset
            : Gfx.IndexBufferSize;
    if (Gfx.IndexBufferOffset + BoundSize > Gfx.IndexBuffer->size())
      return createStringError(inconvertibleErrorCode(),
                               "the index buffer's bound offset/size range "
                               "is out of range of its buffer");
    IndexBinding.Type = Gfx.IndexType == VK_INDEX_TYPE_UINT16
                            ? feme::graphics::IndexType::UInt16
                            : feme::graphics::IndexType::UInt32;
    IndexBinding.Data = llvm::ArrayRef<uint8_t>(
        static_cast<const uint8_t *>(Gfx.IndexBuffer->data()) +
            Gfx.IndexBufferOffset,
        static_cast<size_t>(BoundSize));
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

  uint64_t PassedSamples = 0;
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
  Prepared.PassedSampleCounter = &PassedSamples;

  if (Error E = feme::graphics::executeDraws(
          Pipeline.buildExecutorPipeline(Gfx.Dynamic), Prepared))
    return E;
  for (QueryPool *Pool : ActiveOcclusionQueries)
    Pool->accumulateActiveOcclusionSamples(PassedSamples);
  return Error::success();
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
    // (Roadmap E5) A `vkCmdBindIndexBuffer2` bind narrows the readable
    // range to `offset + size` rather than the whole buffer; `VK_WHOLE_
    // SIZE` (also what a plain `vkCmdBindIndexBuffer` bind always uses)
    // keeps this the buffer's own end.
    uint64_t BoundEnd = Gfx.IndexBufferSize == VK_WHOLE_SIZE
                            ? Gfx.IndexBuffer->size()
                            : Gfx.IndexBufferOffset + Gfx.IndexBufferSize;
    uint64_t IndexSize = Gfx.IndexType == VK_INDEX_TYPE_UINT16 ? 2 : 4;
    uint64_t End = (uint64_t(Draw.FirstIndex) + Draw.VertexCount) * IndexSize +
                   Gfx.IndexBufferOffset;
    if (End > BoundEnd)
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
    uint32_t Stride = resolveVertexBindingStride(Pipeline, Gfx, BindingDecl);
    uint64_t Base =
        Gfx.VertexBufferOffsets[BindingDecl.Binding] + LastIndex * Stride;
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
                       llvm::ArrayRef<uint8_t> PushConstants,
                       llvm::ArrayRef<QueryPool *> ActiveOcclusionQueries) {
  if (Error E = validateDrawCounts(DeviceInfo, Draw))
    return E;
  if (Error E = validateDrawFetchBounds(Pipeline, Gfx, Draw))
    return E;
  return runDraw(Pipeline, Gfx, Draw, BoundSets, PushConstants,
                 ActiveOcclusionQueries);
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
                          std::vector<uint8_t> &PushConstants,
                          std::vector<QueryPool *> &ActiveOcclusionQueries) {
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
      Cmd.TargetQueryPool->begin(Cmd.FirstQuery);
      if (Cmd.TargetQueryPool->queryType() == VK_QUERY_TYPE_OCCLUSION &&
          llvm::find(ActiveOcclusionQueries, Cmd.TargetQueryPool) ==
              ActiveOcclusionQueries.end())
        ActiveOcclusionQueries.push_back(Cmd.TargetQueryPool);
      break;
    case RecordedCommand::Kind::EndQuery:
      Cmd.TargetQueryPool->markAvailable(Cmd.FirstQuery);
      if (Cmd.TargetQueryPool->queryType() == VK_QUERY_TYPE_OCCLUSION &&
          !Cmd.TargetQueryPool->hasActiveQueries())
        llvm::erase(ActiveOcclusionQueries, Cmd.TargetQueryPool);
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
                                          Gfx, BoundSets, PushConstants,
                                          ActiveOcclusionQueries))
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
      // (Roadmap C6) An imageless framebuffer's own attachments are empty;
      // `Cmd.BeginAttachments` (`VkRenderPassAttachmentBeginInfo`) supplies
      // them for this render-pass instance instead.
      Gfx.FbAttachments = Gfx.Fb->isImageless()
                              ? llvm::ArrayRef(Cmd.BeginAttachments)
                              : Gfx.Fb->attachments();
      Gfx.Subpass = 0;
      Gfx.RenderArea = Cmd.RenderArea;
      Gfx.ClearValues = Cmd.ClearValues;
      Expected<RenderTargetBinding> Binding = buildRenderTargetBinding(
          *Gfx.Pass, *Gfx.Fb, Gfx.FbAttachments, Gfx.Subpass, Gfx.RenderArea,
          Gfx.ClearValues);
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
      Gfx.FbAttachments = {};
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
          *Gfx.Pass, *Gfx.Fb, Gfx.FbAttachments, Gfx.Subpass, Gfx.RenderArea,
          Gfx.ClearValues);
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
      Gfx.FbAttachments = {};
      break;
    case RecordedCommand::Kind::BindVertexBuffers: {
      // `VK_WHOLE_SIZE` (`UINT64_MAX`) is reused here as "no stride
      // override at this slot", since `VkDeviceSize`'s own zero value is a
      // legal (if unusual) stride -- see `runDraw`'s vertex-fetch loop.
      constexpr VkDeviceSize NoStrideOverride = VK_WHOLE_SIZE;
      size_t Required = Cmd.FirstSet + Cmd.VertexBuffers.size();
      if (Gfx.VertexBuffers.size() < Required) {
        Gfx.VertexBuffers.resize(Required, nullptr);
        Gfx.VertexBufferOffsets.resize(Required, 0);
        Gfx.VertexBufferStrides.resize(Required, NoStrideOverride);
      }
      for (size_t I = 0; I != Cmd.VertexBuffers.size(); ++I) {
        Gfx.VertexBuffers[Cmd.FirstSet + I] = Cmd.VertexBuffers[I];
        Gfx.VertexBufferOffsets[Cmd.FirstSet + I] =
            I < Cmd.VertexBufferOffsets.size() ? Cmd.VertexBufferOffsets[I] : 0;
        Gfx.VertexBufferStrides[Cmd.FirstSet + I] =
            I < Cmd.VertexBufferStrides.size() ? Cmd.VertexBufferStrides[I]
                                               : NoStrideOverride;
      }
      break;
    }
    case RecordedCommand::Kind::BindIndexBuffer:
      Gfx.IndexBuffer = Cmd.SrcBuffer;
      Gfx.IndexBufferOffset = Cmd.IndirectOffset;
      Gfx.IndexType = Cmd.IndexType;
      Gfx.IndexBufferSize = Cmd.DstSize;
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
    case RecordedCommand::Kind::SetCullMode:
      Gfx.Dynamic.Cull = toCullMode(Cmd.CullModeValue);
      break;
    case RecordedCommand::Kind::SetFrontFace:
      Gfx.Dynamic.Front = toFrontFace(Cmd.FrontFaceValue);
      break;
    case RecordedCommand::Kind::SetDepthTestEnable:
      Gfx.Dynamic.DepthTestEnable = Cmd.Bool32Value != VK_FALSE;
      break;
    case RecordedCommand::Kind::SetDepthWriteEnable:
      Gfx.Dynamic.DepthWriteEnable = Cmd.Bool32Value != VK_FALSE;
      break;
    case RecordedCommand::Kind::SetDepthCompareOp:
      Gfx.Dynamic.DepthCompare = toCompareOp(Cmd.DepthCompareOpValue);
      break;
    case RecordedCommand::Kind::SetDepthBoundsTestEnable:
      Gfx.Dynamic.DepthBoundsTestEnable = Cmd.Bool32Value != VK_FALSE;
      break;
    case RecordedCommand::Kind::SetStencilTestEnable:
      Gfx.Dynamic.StencilTestEnable = Cmd.Bool32Value != VK_FALSE;
      break;
    case RecordedCommand::Kind::SetStencilOp:
      for (unsigned Face = 0; Face != 2; ++Face) {
        VkStencilFaceFlags Bit =
            Face == 0 ? VK_STENCIL_FACE_FRONT_BIT : VK_STENCIL_FACE_BACK_BIT;
        if ((Cmd.StencilFaceMask & Bit) == 0)
          continue;
        DynamicGraphicsState::StencilOpState &Op = Gfx.Dynamic.StencilOps[Face];
        Op.FailOp = toStencilOp(Cmd.StencilFailOpValue);
        Op.PassOp = toStencilOp(Cmd.StencilPassOpValue);
        Op.DepthFailOp = toStencilOp(Cmd.StencilDepthFailOpValue);
        Op.Compare = toCompareOp(Cmd.StencilCompareOpValue);
      }
      break;
    case RecordedCommand::Kind::SetPrimitiveTopology:
      Gfx.Dynamic.Topology = toDynamicTopology(Cmd.PrimitiveTopologyValue);
      break;
    case RecordedCommand::Kind::SetLineWidth:
      Gfx.Dynamic.LineWidth = Cmd.LineWidthValue;
      break;
    case RecordedCommand::Kind::SetLineStipple:
      Gfx.Dynamic.StippleFactor = Cmd.LineStippleFactorValue;
      Gfx.Dynamic.StipplePattern = Cmd.LineStipplePatternValue;
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
                                     DeviceInfo, BoundSets, PushConstants,
                                     ActiveOcclusionQueries))
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
                                       DeviceInfo, BoundSets, PushConstants,
                                       ActiveOcclusionQueries))
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
  std::vector<QueryPool *> ActiveOcclusionQueries;
  return executeCommandsInto(CmdBuf.commands(), DeviceInfo, BoundPipeline,
                             BoundGraphicsPipeline, Gfx, BoundSets,
                             PushConstants, ActiveOcclusionQueries);
}

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool) {
  // Only `PhysicalDeviceInfo::NumQueueFamilies` families exist -- the
  // universal (graphics/compute/transfer) family and the dedicated
  // transfer-only family (see "Graphics queue family" and roadmap C7).
  if (pCreateInfo->queueFamilyIndex >= PhysicalDeviceInfo::NumQueueFamilies)
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

VKAPI_ATTR void VKAPI_CALL vkTrimCommandPool(VkDevice, VkCommandPool,
                                             VkCommandPoolTrimFlags) {
  // A core VK_VERSION_1_1 command this ICD must at least resolve: the spec
  // only requires that trimming *may* reduce a pool's memory usage, never
  // that it does, so a no-op is a conformant implementation. Without this,
  // `vkGetDeviceProcAddr` returned null for a command every loader-linked
  // client's dispatch table expects to resolve for a core-1.1 device,
  // which crashed (not merely returned an error) the first caller found by
  // a Vulkan-CTS run (`dEQP-VK.api.command_buffers.trim_command_pool`).
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

// (roadmap E6) `VK_KHR_maintenance6`'s `vkCmdBindDescriptorSets2`: the same
// arguments as `vkCmdBindDescriptorSets` above, wrapped in a single
// `pNext`-extensible `VkBindDescriptorSetsInfo` in place of a
// `pipelineBindPoint` argument plus five flat array arguments. Unlike
// `vkCmdBindDescriptorSets`'s own `pipelineBindPoint`, this struct instead
// carries a `stageFlags` mask -- but `CommandBuffer::bindDescriptorSets`
// already stores one shared set of bound descriptor sets for every bind
// point (see the non-`2` command's own comment above), so neither
// `stageFlags` nor `layout` changes what gets recorded here, the same way
// `vkCmdPushConstants`'s own `stageFlags`/`layout` need no validation.
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets2(
    VkCommandBuffer commandBuffer,
    const VkBindDescriptorSetsInfo *pBindDescriptorSetsInfo) {
  std::vector<DescriptorSet *> Sets;
  Sets.reserve(pBindDescriptorSetsInfo->descriptorSetCount);
  for (uint32_t I = 0; I != pBindDescriptorSetsInfo->descriptorSetCount; ++I)
    Sets.push_back(
        fromHandle<DescriptorSet>(pBindDescriptorSetsInfo->pDescriptorSets[I]));
  std::vector<uint32_t> Offsets(
      pBindDescriptorSetsInfo->pDynamicOffsets,
      pBindDescriptorSetsInfo->pDynamicOffsets +
          pBindDescriptorSetsInfo->dynamicOffsetCount);
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->bindDescriptorSets(pBindDescriptorSetsInfo->firstSet, std::move(Sets),
                           std::move(Offsets));
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

// The `vkCmd*2` wrappers below each unwrap a `VK_KHR_copy_commands2`
// `..Info2` struct's own `pNext`-extensible region array (every element the
// same fields as its non-`2` counterpart, only prefixed by `sType`/`pNext`,
// which the Vulkan spec guarantees for every one of these region types) and
// delegate to the identical `vulkan::CommandBuffer` method its non-`2`
// counterpart above already calls; see "V5: image/buffer copies".

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2(
    VkCommandBuffer commandBuffer, const VkCopyBufferInfo2 *pCopyBufferInfo) {
  std::vector<VkBufferCopy> Regions;
  Regions.reserve(pCopyBufferInfo->regionCount);
  for (uint32_t I = 0; I < pCopyBufferInfo->regionCount; ++I) {
    const VkBufferCopy2 &R = pCopyBufferInfo->pRegions[I];
    Regions.push_back(VkBufferCopy{R.srcOffset, R.dstOffset, R.size});
  }
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->copyBuffer(fromHandle<vulkan::Buffer>(pCopyBufferInfo->srcBuffer),
                   fromHandle<vulkan::Buffer>(pCopyBufferInfo->dstBuffer),
                   std::move(Regions));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2(
    VkCommandBuffer commandBuffer, const VkCopyImageInfo2 *pCopyImageInfo) {
  std::vector<VkImageCopy> Regions;
  Regions.reserve(pCopyImageInfo->regionCount);
  for (uint32_t I = 0; I < pCopyImageInfo->regionCount; ++I) {
    const VkImageCopy2 &R = pCopyImageInfo->pRegions[I];
    Regions.push_back(VkImageCopy{R.srcSubresource, R.srcOffset,
                                  R.dstSubresource, R.dstOffset, R.extent});
  }
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->copyImage(fromHandle<vulkan::Image>(pCopyImageInfo->srcImage),
                  fromHandle<vulkan::Image>(pCopyImageInfo->dstImage),
                  std::move(Regions));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2(
    VkCommandBuffer commandBuffer,
    const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo) {
  std::vector<VkBufferImageCopy> Regions;
  Regions.reserve(pCopyBufferToImageInfo->regionCount);
  for (uint32_t I = 0; I < pCopyBufferToImageInfo->regionCount; ++I) {
    const VkBufferImageCopy2 &R = pCopyBufferToImageInfo->pRegions[I];
    Regions.push_back(VkBufferImageCopy{R.bufferOffset, R.bufferRowLength,
                                        R.bufferImageHeight, R.imageSubresource,
                                        R.imageOffset, R.imageExtent});
  }
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->copyBufferToImage(
          fromHandle<vulkan::Buffer>(pCopyBufferToImageInfo->srcBuffer),
          fromHandle<vulkan::Image>(pCopyBufferToImageInfo->dstImage),
          std::move(Regions));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2(
    VkCommandBuffer commandBuffer,
    const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo) {
  std::vector<VkBufferImageCopy> Regions;
  Regions.reserve(pCopyImageToBufferInfo->regionCount);
  for (uint32_t I = 0; I < pCopyImageToBufferInfo->regionCount; ++I) {
    const VkBufferImageCopy2 &R = pCopyImageToBufferInfo->pRegions[I];
    Regions.push_back(VkBufferImageCopy{R.bufferOffset, R.bufferRowLength,
                                        R.bufferImageHeight, R.imageSubresource,
                                        R.imageOffset, R.imageExtent});
  }
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->copyImageToBuffer(
          fromHandle<vulkan::Image>(pCopyImageToBufferInfo->srcImage),
          fromHandle<vulkan::Buffer>(pCopyImageToBufferInfo->dstBuffer),
          std::move(Regions));
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

// (Roadmap E3) `VkDependencyInfo`'s per-resource `VkMemoryBarrier2`/
// `VkBufferMemoryBarrier2`/`VkImageMemoryBarrier2` (2-stage-mask,
// 2-access-mask shape) translate down to the same `ImageLayoutTransition`
// payload `vkCmdPipelineBarrier` already produces above: only the image
// barriers' layout transitions carry any state this ICD tracks, for the
// same reason that command's own comment gives, and a 2-mask barrier's
// extra stage/access precision has nothing more to add to that state.
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(
    VkCommandBuffer commandBuffer, const VkDependencyInfo *pDependencyInfo) {
  std::vector<ImageLayoutTransition> ImageBarriers;
  ImageBarriers.reserve(pDependencyInfo->imageMemoryBarrierCount);
  for (uint32_t I = 0; I != pDependencyInfo->imageMemoryBarrierCount; ++I) {
    const VkImageMemoryBarrier2 &Barrier =
        pDependencyInfo->pImageMemoryBarriers[I];
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

// (roadmap E6) `VK_KHR_maintenance6`'s `vkCmdPushConstants2`: the same
// `offset`/`size`/`pValues` triple as `vkCmdPushConstants` above, wrapped in
// a single `pNext`-extensible `VkPushConstantsInfo` in place of the
// `layout`/`stageFlags` argument pair -- both of which need no validation
// here for the same reason the non-`2` command's own comment gives.
VKAPI_ATTR void VKAPI_CALL
vkCmdPushConstants2(VkCommandBuffer commandBuffer,
                    const VkPushConstantsInfo *pPushConstantsInfo) {
  if (pPushConstantsInfo->size == 0 || pPushConstantsInfo->offset % 4 != 0 ||
      pPushConstantsInfo->size % 4 != 0)
    return;
  const auto *Bytes = static_cast<const uint8_t *>(pPushConstantsInfo->pValues);
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->pushConstants(
          pPushConstantsInfo->offset,
          std::vector<uint8_t>(Bytes, Bytes + pPushConstantsInfo->size));
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

// (Roadmap E3) `VK_KHR_synchronization2`'s event commands: `vkCmdSetEvent2`/
// `vkCmdWaitEvents2` carry a `VkDependencyInfo` (per-event, for
// `vkCmdWaitEvents2`) in place of the mask arguments above, and
// `vkCmdResetEvent2` a 2-stage-mask in place of a 1-mask one; none of that
// extra precision has anything to add to the `Event` state this ICD
// tracks, for the same reason `vkCmdPipelineBarrier2` gives, so each
// translates straight down to its non-`2` counterpart's identical payload.
VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2(VkCommandBuffer commandBuffer,
                                          VkEvent event,
                                          const VkDependencyInfo *) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setEvent(fromHandle<Event>(event));
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2(VkCommandBuffer commandBuffer,
                                            VkEvent event,
                                            VkPipelineStageFlags2) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->resetEvent(fromHandle<Event>(event));
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2(VkCommandBuffer commandBuffer,
                                            uint32_t eventCount,
                                            const VkEvent *pEvents,
                                            const VkDependencyInfo *) {
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

// (Roadmap E3) `VK_KHR_synchronization2`'s `vkCmdWriteTimestamp2`: a
// 2-stage-mask `stage` in place of `vkCmdWriteTimestamp`'s single
// `VkPipelineStageFlagBits`, otherwise identical.
VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer,
                                                VkPipelineStageFlags2,
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
  // (Roadmap C6) An imageless framebuffer supplies its attachment views
  // here rather than at `vkCreateFramebuffer` time; format/sample-count/
  // size compatibility is validated later, once the render pass and
  // framebuffer are both known (`buildRenderTargetBinding`).
  std::vector<ImageView *> Attachments;
  for (auto *Base =
           static_cast<const VkBaseInStructure *>(pRenderPassBegin->pNext);
       Base; Base = Base->pNext)
    if (Base->sType == VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO) {
      const auto *Info =
          reinterpret_cast<const VkRenderPassAttachmentBeginInfo *>(Base);
      Attachments.reserve(Info->attachmentCount);
      for (uint32_t I = 0; I != Info->attachmentCount; ++I)
        Attachments.push_back(fromHandle<ImageView>(Info->pAttachments[I]));
      break;
    }
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->beginRenderPass(fromHandle<RenderPass>(pRenderPassBegin->renderPass),
                        fromHandle<Framebuffer>(pRenderPassBegin->framebuffer),
                        pRenderPassBegin->renderArea, std::move(ClearValues),
                        std::move(Attachments));
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

// core VK_VERSION_1_2's `*RenderPass2` commands add only a `pNext`-chained
// depth/stencil resolve mode (unimplemented -- multisample depth/stencil
// resolve is roadmap V7) to their classic counterparts' begin/end
// semantics; this driver's own render-pass instance state
// (`vulkan::CommandBuffer::beginRenderPass`/`nextSubpass`/`endRenderPass`)
// already carries everything else either variant needs, so these three
// commands are pure signature adapters onto the exact same state machine.
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2(
    VkCommandBuffer commandBuffer,
    const VkRenderPassBeginInfo *pRenderPassBegin, const VkSubpassBeginInfo *) {
  feme::vulkan::vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin,
                                     VK_SUBPASS_CONTENTS_INLINE);
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2(VkCommandBuffer commandBuffer,
                                             const VkSubpassBeginInfo *,
                                             const VkSubpassEndInfo *) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->nextSubpass();
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2(VkCommandBuffer commandBuffer,
                                               const VkSubpassEndInfo *) {
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

// (roadmap C4c) `vkCmdBindVertexBuffers2EXT`: `VK_EXT_extended_dynamic_
// state`'s last state, VERTEX_INPUT_BINDING_STRIDE, is only reachable
// through this call's optional `pStrides` (there is no separate
// `vkCmdSetVertexInputBindingStride*` command). `pSizes` is not modeled --
// this ICD tracks no notion of a vertex buffer's bound "range" narrower
// than its own size, matching `vkCmdBindVertexBuffers`' pre-existing scope.
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers2EXT(
    VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
    const VkBuffer *pBuffers, const VkDeviceSize *pOffsets,
    const VkDeviceSize * /*pSizes*/, const VkDeviceSize *pStrides) {
  std::vector<vulkan::Buffer *> Buffers;
  std::vector<VkDeviceSize> Offsets;
  std::vector<VkDeviceSize> Strides;
  Buffers.reserve(bindingCount);
  Offsets.reserve(bindingCount);
  if (pStrides)
    Strides.reserve(bindingCount);
  for (uint32_t I = 0; I != bindingCount; ++I) {
    Buffers.push_back(fromHandle<vulkan::Buffer>(pBuffers[I]));
    Offsets.push_back(pOffsets ? pOffsets[I] : 0);
    if (pStrides)
      Strides.push_back(pStrides[I]);
  }
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->bindVertexBuffers(firstBinding, std::move(Buffers), std::move(Offsets),
                          std::move(Strides));
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                                                VkBuffer buffer,
                                                VkDeviceSize offset,
                                                VkIndexType indexType) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->bindIndexBuffer(fromHandle<vulkan::Buffer>(buffer), offset, indexType);
}

// (Roadmap E5) `vkCmdBindIndexBuffer2`: the same bind as
// `vkCmdBindIndexBuffer` above, plus a `size` bounding how much of the
// buffer past `offset` is actually readable -- sharing every other piece
// of `bindIndexBuffer`'s state and `runDraw`/`validateDrawFetchBounds`'s
// bounds checking, minus `vkCmdBindIndexBuffer`'s "the rest of the
// buffer" assumption (`VK_WHOLE_SIZE` still means exactly that).
VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer2(VkCommandBuffer commandBuffer,
                                                 VkBuffer buffer,
                                                 VkDeviceSize offset,
                                                 VkDeviceSize size,
                                                 VkIndexType indexType) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->bindIndexBuffer(fromHandle<vulkan::Buffer>(buffer), offset, indexType,
                        size);
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

// (roadmap C4c) `vkCmdSetViewportWithCountEXT`/`vkCmdSetScissorWithCountEXT`:
// the "with count" spelling `VK_EXT_extended_dynamic_state` adds, with no
// `first*` parameter (a pipeline may only ever use one or the other of a
// state/its "with count" counterpart, never both). Reuses `setViewport`/
// `setScissor` directly: `maxViewports == 1` means "with count" carries no
// more information than the fixed-count commands above already do.
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCountEXT(
    VkCommandBuffer commandBuffer, uint32_t viewportCount,
    const VkViewport *pViewports) {
  if (viewportCount == 0)
    return;
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->setViewport(pViewports[0]);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdSetScissorWithCountEXT(VkCommandBuffer commandBuffer,
                            uint32_t scissorCount, const VkRect2D *pScissors) {
  if (scissorCount == 0)
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

// (roadmap C4c) `VK_EXT_extended_dynamic_state`'s `vkCmdSetCullModeEXT`/
// `vkCmdSetFrontFaceEXT`: both already have a fully-implemented static
// path (`CullMode`/`FrontFace` in `feme::graphics::RasterState`), so
// making them dynamic needs no new rasterizer machinery, unlike the
// topology/dual-source-blend gaps this milestone's own status note
// documents as still needing it.
VKAPI_ATTR void VKAPI_CALL vkCmdSetCullModeEXT(VkCommandBuffer commandBuffer,
                                               VkCullModeFlags cullMode) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->setCullMode(cullMode);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFaceEXT(VkCommandBuffer commandBuffer,
                                                VkFrontFace frontFace) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->setFrontFace(frontFace);
}

// (roadmap C4c) `vkCmdSetDepthTestEnableEXT`/`vkCmdSetDepthWriteEnableEXT`/
// `vkCmdSetDepthCompareOpEXT`/`vkCmdSetDepthBoundsTestEnableEXT`: the
// remaining `VK_EXT_extended_dynamic_state` states with an existing static
// path (or, for depth bounds, a feature this ICD never advertises as
// enabled -- see `DynamicStateBits`'s comment).
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnableEXT(
    VkCommandBuffer commandBuffer, VkBool32 depthTestEnable) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setDepthBool(RecordedCommand::Kind::SetDepthTestEnable,
                     depthTestEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnableEXT(
    VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setDepthBool(RecordedCommand::Kind::SetDepthWriteEnable,
                     depthWriteEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOpEXT(
    VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setDepthCompareOp(depthCompareOp);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBoundsTestEnableEXT(
    VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setDepthBool(RecordedCommand::Kind::SetDepthBoundsTestEnable,
                     depthBoundsTestEnable);
}

// (roadmap C4c) `vkCmdSetStencilTestEnableEXT`/`vkCmdSetStencilOpEXT`: the
// last two `VK_EXT_extended_dynamic_state` states, both already fully
// implemented statically (`StencilState`).
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnableEXT(
    VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setStencilTestEnable(stencilTestEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOpEXT(VkCommandBuffer commandBuffer,
                                                VkStencilFaceFlags faceMask,
                                                VkStencilOp failOp,
                                                VkStencilOp passOp,
                                                VkStencilOp depthFailOp,
                                                VkCompareOp compareOp) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setStencilOp(faceMask, failOp, passOp, depthFailOp, compareOp);
}

// (roadmap C4c) `vkCmdSetPrimitiveTopologyEXT`: the last
// `VK_EXT_extended_dynamic_state` state with an existing static path
// (restricted to the triangle class this executor already implements --
// see `DynamicStateBits`'s comment on `DynamicStatePrimitiveTopology`).
VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopologyEXT(
    VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setPrimitiveTopology(primitiveTopology);
}

// Four more core commands this ICD must at least resolve (found missing
// entirely -- a segfault through a null device-dispatch-table entry, the
// same root cause as the vkTrimCommandPool/vkCreateRenderPass2/
// vkCreateDescriptorUpdateTemplate fixes -- by the Vulkan-CTS run's
// `dEQP-VK.dynamic_state.*` group). Each is legal to call regardless of
// the currently bound pipeline's state, but only has an observable effect
// when that pipeline both enables the corresponding fixed-function state
// *and* declares it dynamic; every state these four commands govern
// (depth bias/bounds, wide lines, a device mask beyond the one physical
// device this ICD exposes) is already rejected at graphics-pipeline
// creation (see V6's own deviation list in
// feme/docs/FeMeVulkanDesign.md) or, for `vkCmdSetDeviceMask`, has no
// second device to ever mask out -- so no bound pipeline this ICD
// accepted could ever have made any of them anything but a no-op record.
// (roadmap F5) `vkCmdSetLineWidth`: `VK_DYNAMIC_STATE_LINE_WIDTH` already
// has a real static path (`RasterState::LineWidth`), so making it dynamic
// is the same "read from the per-draw snapshot" pattern
// `vkCmdSetCullModeEXT` above already uses, not a new rasterizer
// feature.
VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(VkCommandBuffer commandBuffer,
                                             float lineWidth) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)->setLineWidth(lineWidth);
}

// (roadmap F5) `VK_KHR_line_rasterization`'s one command:
// `VK_DYNAMIC_STATE_LINE_STIPPLE_KHR`'s payload, the same "read from the
// per-draw snapshot" pattern `vkCmdSetLineWidth` above already uses.
VKAPI_ATTR void VKAPI_CALL
vkCmdSetLineStippleKHR(VkCommandBuffer commandBuffer,
                       uint32_t lineStippleFactor,
                       uint16_t lineStipplePattern) {
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->setLineStipple(lineStippleFactor, lineStipplePattern);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer, float, float,
                                             float) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBounds(VkCommandBuffer, float, float) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDeviceMask(VkCommandBuffer, uint32_t) {}

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

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2(
    VkCommandBuffer commandBuffer, const VkBlitImageInfo2 *pBlitImageInfo) {
  std::vector<VkImageBlit> Regions;
  Regions.reserve(pBlitImageInfo->regionCount);
  for (uint32_t I = 0; I < pBlitImageInfo->regionCount; ++I) {
    const VkImageBlit2 &R = pBlitImageInfo->pRegions[I];
    Regions.push_back(VkImageBlit{R.srcSubresource,
                                  {R.srcOffsets[0], R.srcOffsets[1]},
                                  R.dstSubresource,
                                  {R.dstOffsets[0], R.dstOffsets[1]}});
  }
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->blitImage(fromHandle<Image>(pBlitImageInfo->srcImage),
                  fromHandle<Image>(pBlitImageInfo->dstImage),
                  std::move(Regions), pBlitImageInfo->filter);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdResolveImage2(VkCommandBuffer commandBuffer,
                   const VkResolveImageInfo2 *pResolveImageInfo) {
  std::vector<VkImageResolve> Regions;
  Regions.reserve(pResolveImageInfo->regionCount);
  for (uint32_t I = 0; I < pResolveImageInfo->regionCount; ++I) {
    const VkImageResolve2 &R = pResolveImageInfo->pRegions[I];
    Regions.push_back(VkImageResolve{R.srcSubresource, R.srcOffset,
                                     R.dstSubresource, R.dstOffset, R.extent});
  }
  fromHandle<vulkan::CommandBuffer>(commandBuffer)
      ->resolveImage(fromHandle<Image>(pResolveImageInfo->srcImage),
                     fromHandle<Image>(pResolveImageInfo->dstImage),
                     std::move(Regions));
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
