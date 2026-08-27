//===- RuntimeABI.h - FeMe CPU target C ABI ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the C ABI the FeMe CPU target's compiled shaders and
// their host share. Today that covers the compute-dispatch ABI
// (`FemeDispatchArgs`), roadmap R28's graphics-stage batch ABI
// (`FemeShaderResources`, `FemeStageLayout`, `FemeVertexArgs`, and
// `FemeFragmentArgs`), and roadmap R29's image/sampler descriptors
// (`FemeImageDescriptor`, `FemeSamplerDescriptor`), and roadmap R34's
// control-stage control-point batch ABI (`FemePatchArgs`) and, added after
// R34's initial landing, the patch-constant phase's own single-invocation
// batch ABI (`FemePatchConstantArgs`), the domain/evaluation stage's
// per-domain-point batch ABI (`FemeDomainArgs`), and the geometry stage's
// per-primitive batch ABI (`FemeGeometryArgs`), plus the
// descriptor/layout/system-value enumerators that give those structs' fields
// meaning. R29 also folded `FemeShaderResources` into `FemeDispatchArgs` and
// retyped `FemeShaderResources::SamplerHeap`, breaking the ABI on purpose
// (see "Relationship to the compute ABI" in feme/docs/FeMeGraphicsDesign.md):
// an artifact compiled before that change no longer matches this header.
//
// This header is plain C-compatible data only (no functions, no C++ features
// besides `enum class`/namespacing): both feme::cpu's own compiler-side code
// and a host embedding FeMe link against it, and a compiled shader's object
// file refers to these layouts without itself depending on any FeMe library.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_RUNTIMEABI_H
#define FEME_TARGET_CPU_RUNTIMEABI_H

#include <cstdint>

namespace feme::cpu {

/// What a `FemeDescriptor` refers to. `None` is the all-zero descriptor's
/// kind -- the state of a heap slot the host never wrote -- and is always
/// treated as out of bounds regardless of any other field, including
/// `FEME_DESCRIPTOR_TRUSTED` (see "Per-descriptor control" in
/// feme/docs/FeMeCPUDesign.md).
enum class ResourceKind : uint32_t {
  None = 0,
  Typed = 1,
  Structured = 2,
  Raw = 3,
  CBuffer = 4,
};

/// The runtime storage format of a typed-buffer descriptor (`Kind::Typed`);
/// meaningless for every other kind. Values are FeMe's own numbering, not
/// DXGI's or Vulkan's -- the importer/translator is responsible for mapping
/// each source format to one of these. See "Descriptor formats" in
/// feme/docs/FeMeCPUDesign.md for which formats the runtime helper library
/// implements a conversion for.
enum class ResourceFormat : uint32_t {
  Unknown = 0,

  // 32-bit-per-component formats: the identity case, no conversion needed.
  R32_FLOAT,
  R32G32_FLOAT,
  R32G32B32_FLOAT,
  R32G32B32A32_FLOAT,
  R32_UINT,
  R32G32_UINT,
  R32G32B32_UINT,
  R32G32B32A32_UINT,
  R32_SINT,
  R32G32_SINT,
  R32G32B32_SINT,
  R32G32B32A32_SINT,

  // Packed/narrow formats needing an explicit scalar conversion helper.
  R8G8B8A8_UNORM,
  R8G8B8A8_SNORM,
  R8G8B8A8_UINT,
  R8G8B8A8_SINT,
  R8G8B8A8_UNORM_SRGB,
  R16G16B16A16_FLOAT,
  R16G16B16A16_UNORM,
  R16G16B16A16_SNORM,
  R16G16B16A16_UINT,
  R16G16B16A16_SINT,
  R11G11B10_FLOAT,
  R10G10B10A2_UNORM,
  R10G10B10A2_UINT,
  B8G8R8A8_UNORM,

  // (Roadmap E5, `VK_KHR_maintenance5`) `VK_FORMAT_A8_UNORM`: a single
  // 8-bit alpha-only component, no color components at all.
  A8_UNORM,
  // (Roadmap E5) `VK_FORMAT_A1B5G5R5_UNORM_PACK16`: the same 1-bit-alpha,
  // 5-bit-per-color-component packing `B5G5R5A1_UNORM` already has, but
  // with alpha moved to the most significant bit instead of the least
  // significant one (legacy D3D/OpenGL "ABGR1555" compatibility).
  A1B5G5R5_UNORM,

  // (Roadmap E19, `VK_EXT_4444_formats`) Two 16-bit-packed, 4-bit-per-
  // component formats -- recognized as a legal `VkFormat` (so an image may
  // be created with one, and copied/blitted like any other recognized
  // format) but not yet backed by a `feme::graphics::packClearColor`/
  // `unpackColor` case, so `isSupportedColorAttachmentFormat` and
  // `formatFeatureFlags`'s sampled-image bits correctly leave both unset
  // (see Format.cpp's `formatFeatureFlags` comment).
  A4R4G4B4_UNORM,
  A4B4G4R4_UNORM,

  // Depth/stencil formats (roadmap R33, "the format expansion the first
  // advertised profile needs"): used only for a `GraphicsPipeline`/
  // `PreparedDraw` depth/stencil attachment, never for a typed buffer.
  D16_UNORM,
  D32_FLOAT,
  D24_UNORM_S8_UINT,
  D32_FLOAT_S8X24_UINT,
  S8_UINT,

  // (Roadmap E20) The 14 LDR-only ASTC block footprints, each with a
  // `_UNORM`/`_SRGB` pair -- see "Block-compressed formats" below for what
  // makes these different from every format above. Ordered by block
  // footprint the same way `VK_FORMAT_ASTC_*` is, smallest (most texels
  // per byte) first.
  ASTC_4x4_UNORM,
  ASTC_4x4_SRGB,
  ASTC_5x4_UNORM,
  ASTC_5x4_SRGB,
  ASTC_5x5_UNORM,
  ASTC_5x5_SRGB,
  ASTC_6x5_UNORM,
  ASTC_6x5_SRGB,
  ASTC_6x6_UNORM,
  ASTC_6x6_SRGB,
  ASTC_8x5_UNORM,
  ASTC_8x5_SRGB,
  ASTC_8x6_UNORM,
  ASTC_8x6_SRGB,
  ASTC_8x8_UNORM,
  ASTC_8x8_SRGB,
  ASTC_10x5_UNORM,
  ASTC_10x5_SRGB,
  ASTC_10x6_UNORM,
  ASTC_10x6_SRGB,
  ASTC_10x8_UNORM,
  ASTC_10x8_SRGB,
  ASTC_10x10_UNORM,
  ASTC_10x10_SRGB,
  ASTC_12x10_UNORM,
  ASTC_12x10_SRGB,
  ASTC_12x12_UNORM,
  ASTC_12x12_SRGB,

  // (Roadmap E21) The 14 HDR-only ASTC block footprints
  // (`VK_FORMAT_ASTC_*_SFLOAT_BLOCK_EXT`, `VK_EXT_texture_compression_
  // astc_hdr`) -- the same 14 footprints as the LDR pairs above, each
  // with a single `_SFLOAT` variant instead of an `_UNORM`/`_SRGB` pair
  // (HDR data has no sRGB curve to apply). Ordered the same
  // smallest-footprint-first way.
  ASTC_4x4_SFLOAT,
  ASTC_5x4_SFLOAT,
  ASTC_5x5_SFLOAT,
  ASTC_6x5_SFLOAT,
  ASTC_6x6_SFLOAT,
  ASTC_8x5_SFLOAT,
  ASTC_8x6_SFLOAT,
  ASTC_8x8_SFLOAT,
  ASTC_10x5_SFLOAT,
  ASTC_10x6_SFLOAT,
  ASTC_10x8_SFLOAT,
  ASTC_10x10_SFLOAT,
  ASTC_12x10_SFLOAT,
  ASTC_12x12_SFLOAT,
};

/// Whether \p Format is one of the ASTC block-compressed formats above.
/// Every format before this point in the enum addresses one texel at a
/// time (`formatElementSize` bytes each); a block-compressed format
/// instead packs a whole `blockWidth(Format) x blockHeight(Format)` tile
/// of texels into one fixed-size `bytesPerBlock(Format)` block, so
/// `formatElementSize` (a *texel*'s size) is meaningless for it -- see
/// "Block-compressed formats" in feme/lib/Vulkan/Format.h for the
/// block-aware layout math this distinction feeds.
constexpr bool isBlockCompressedFormat(ResourceFormat Format) {
  return Format >= ResourceFormat::ASTC_4x4_UNORM &&
         Format <= ResourceFormat::ASTC_12x12_SFLOAT;
}

/// Whether \p Format is one of the 28 LDR-only ASTC formats (roadmap E20)
/// rather than one of the 14 HDR-only `_SFLOAT` ones (roadmap E21) --
/// `feme::vulkan::decodeASTCBlock` (ASTCDecode.h) only decodes this half;
/// the HDR half needs `decodeASTCBlockHDR`'s float-producing interface
/// instead (roadmap E22, `ImageOps.cpp`'s `runBlitImage`).
constexpr bool isASTCLdrFormat(ResourceFormat Format) {
  return Format >= ResourceFormat::ASTC_4x4_UNORM &&
         Format <= ResourceFormat::ASTC_12x12_SRGB;
}

/// Bits of `FemeDescriptor::Flags`.
enum FemeDescriptorFlagBits : uint32_t {
  /// Set if the descriptor is a UAV (read-write); clear for an SRV
  /// (read-only). Constant buffers are always read-only regardless of this
  /// bit.
  FEME_DESCRIPTOR_UAV = 1u << 0,

  /// Set if the descriptor is a rasterizer-ordered view. Not meaningful for
  /// the CPU target's compute-only v1 (see "Limitations" in
  /// feme/docs/FeMeCPUDesign.md) but reserved so the bit layout does not
  /// change when graphics support arrives.
  FEME_DESCRIPTOR_ROV = 1u << 1,

  /// Set if `Counter` is non-null, i.e. this is an append/consume/counter
  /// UAV.
  FEME_DESCRIPTOR_HAS_COUNTER = 1u << 2,

  /// A host assertion that accesses through this descriptor never go out
  /// of bounds, skipping the per-access offset check the linked runtime
  /// helper would otherwise perform (see "Per-descriptor control" in
  /// feme/docs/FeMeCPUDesign.md). Setting this on a descriptor whose
  /// resource the shader then over-reads is undefined behaviour: a host
  /// memory access, possibly a wild one. Nothing in FeMe sets this bit;
  /// only a host may, and only when it can prove the shader cannot exceed
  /// `SizeInBytes`. Ignored when `Kind == ResourceKind::None`.
  FEME_DESCRIPTOR_TRUSTED = 1u << 3,
};

/// One descriptor: the unit the resource and sampler heaps are arrays of.
/// Layout is part of the CPU target ABI -- see "Descriptor heaps" in
/// feme/docs/FeMeCPUDesign.md. A descriptor the host has not written is
/// zero-filled (`Kind = ResourceKind::None`, `SizeInBytes = 0`), which the
/// bounds-checking rules turn into "reads zero, writes ignored" rather than
/// undefined behaviour.
struct FemeDescriptor {
  /// Base pointer to the resource's storage, or null for `Kind::None`.
  void *Data;
  /// Total size of the storage `Data` points to, in bytes; used for bounds
  /// checking (see "Bounds checking" in feme/docs/FeMeCPUDesign.md).
  uint64_t SizeInBytes;
  /// Element stride in bytes, for structured/typed buffers. Unused (and
  /// conventionally zero) for raw buffers and constant buffers.
  uint32_t Stride;
  /// The storage format, for typed buffers (`ResourceFormat`); unused for
  /// every other `Kind`.
  uint32_t Format;
  /// What this descriptor refers to (`ResourceKind`).
  uint32_t Kind;
  /// `FemeDescriptorFlagBits` bitmask.
  uint32_t Flags;
  /// Base pointer to this resource's associated counter, for an
  /// append/consume/counter UAV (see `FEME_DESCRIPTOR_HAS_COUNTER`);
  /// null otherwise.
  void *Counter;
};

/// The dimensionality of a `FemeImageDescriptor`, mirroring the DXIL/SPIR-V
/// resource-dimension space (see `feme::dxsa::ResourceDimension`) minus the
/// buffer case, which stays a `FemeDescriptor`.
enum class ImageDimension : uint32_t {
  Texture1D = 0,
  Texture1DArray = 1,
  Texture2D = 2,
  Texture2DArray = 3,
  Texture2DMS = 4,
  Texture2DMSArray = 5,
  Texture3D = 6,
  TextureCube = 7,
  TextureCubeArray = 8,
};

/// Bits of `FemeImageDescriptor::Flags`.
enum FemeImageDescriptorFlagBits : uint32_t {
  /// Set if the image may be sampled (an SRV-like texture view).
  FEME_IMAGE_SAMPLED = 1u << 0,
  /// Set if the image may be read/written by address (a UAV-like storage
  /// image). `FEME_IMAGE_SAMPLED` and `FEME_IMAGE_STORAGE` are not mutually
  /// exclusive: a source API may expose the same allocation both ways.
  FEME_IMAGE_STORAGE = 1u << 1,
  /// Set if the image is a depth/stencil format used as a depth attachment
  /// or depth-comparison sampling source.
  FEME_IMAGE_DEPTH = 1u << 2,
};

/// One subresource's byte layout within a `FemeImageDescriptor::Data`
/// allocation: the base offset of mip level 0 of one array layer, plus the
/// strides needed to address any row, slice (3D depth or array layer), or
/// sample within it. Entries are dense by mip level, i.e.
/// `FemeImageDescriptor::MipLayouts[Level]` describes mip `Level` of every
/// array layer (array layers share one `SlicePitch`-derived stride, so no
/// separate per-layer entry is needed).
struct FemeImageSubresourceLayout {
  /// Byte offset of row 0, slice 0, sample 0 of this mip level within
  /// `FemeImageDescriptor::Data`.
  uint64_t Offset;
  /// Byte distance between two vertically adjacent rows.
  uint64_t RowPitch;
  /// Byte distance between two adjacent slices (a 3D image's depth slices,
  /// or an array image's layers) at this mip level.
  uint64_t SlicePitch;
  /// Byte distance between two adjacent samples of one texel, or 0 for a
  /// single-sample image.
  uint64_t SampleStride;
};

/// One image descriptor: the unit the image heap is an array of. Images do
/// not fit `FemeDescriptor`'s buffer-oriented shape (see "Separate
/// descriptor kinds" in feme/docs/FeMeGraphicsDesign.md), so they get their
/// own descriptor type with an explicit per-mip layout table rather than a
/// single stride. A descriptor the host has not written is zero-filled
/// (`Data = nullptr`, every count 0), which reads as an empty image with no
/// valid subresource.
struct FemeImageDescriptor {
  /// Base pointer to the image's storage, or null if unwritten.
  void *Data;
  /// Total size of the storage `Data` points to, in bytes.
  uint64_t SizeInBytes;
  /// The image's dimensionality (`ImageDimension`).
  uint32_t Dimension;
  /// The storage format (`ResourceFormat`).
  uint32_t Format;
  /// Extent in texels at mip level 0. `Height`/`Depth` are 1 for dimensions
  /// that do not use them.
  uint32_t Width;
  uint32_t Height;
  uint32_t Depth;
  /// Number of mip levels, and therefore the number of entries in
  /// `MipLayouts`.
  uint32_t MipLevels;
  /// Number of array layers, or 1 for a non-array image.
  uint32_t ArrayLayers;
  /// Number of plane sub-images (e.g. separate luma/chroma planes for a
  /// planar format), or 1 for a single-plane format.
  uint32_t PlaneCount;
  /// Number of samples per texel, or 1 for a non-multisampled image.
  uint32_t SampleCount;
  /// `FemeImageDescriptorFlagBits` bitmask.
  uint32_t Flags;
  /// Dense by mip level: `MipLayouts[Level]` describes level `Level`.
  const FemeImageSubresourceLayout *MipLayouts;
  /// Number of entries in `MipLayouts`; always equal to `MipLevels` for a
  /// valid descriptor.
  uint32_t MipLayoutCount;
  /// ABI headroom for later image-descriptor extensions.
  uint32_t Reserved[3];
};

/// The minification/magnification/mip filter a `FemeSamplerDescriptor`
/// applies, mirroring Vulkan's `VkFilter`/`VkSamplerMipmapMode` and Direct3D's
/// filter enumerations without depending on either.
enum class SamplerFilter : uint32_t {
  Nearest = 0,
  Linear = 1,
};

/// The addressing mode a `FemeSamplerDescriptor` applies to one texture
/// coordinate axis outside `[0, 1)`.
enum class SamplerAddressMode : uint32_t {
  Repeat = 0,
  MirroredRepeat = 1,
  ClampToEdge = 2,
  ClampToBorder = 3,
  MirrorClampToEdge = 4,
};

/// The comparison function a depth-comparison sampler applies, mirroring
/// `FemeDescriptor`'s sibling concept for depth-comparison sampling
/// (`SamplerCompareEnable` in `FemeSamplerDescriptor::Flags` gates whether
/// this field is meaningful).
enum class SamplerCompareFunc : uint32_t {
  Never = 0,
  Less = 1,
  Equal = 2,
  LessEqual = 3,
  Greater = 4,
  NotEqual = 5,
  GreaterEqual = 6,
  Always = 7,
};

/// How a multi-sample or anisotropic footprint's texel values are combined
/// into one filtered result.
enum class SamplerReductionMode : uint32_t {
  WeightedAverage = 0,
  Min = 1,
  Max = 2,
};

/// Bits of `FemeSamplerDescriptor::Flags`.
enum FemeSamplerDescriptorFlagBits : uint32_t {
  /// Set if `CompareFunc` is used to produce a comparison result rather
  /// than a filtered value (depth-comparison sampling).
  FEME_SAMPLER_COMPARE_ENABLE = 1u << 0,
  /// Set if anisotropic filtering is enabled, in which case `MaxAnisotropy`
  /// bounds the sample count; clear to use `MinFilter`/`MagFilter` only.
  FEME_SAMPLER_ANISOTROPY_ENABLE = 1u << 1,
};

/// One sampler descriptor: the unit the sampler heap is an array of. Unlike
/// `FemeImageDescriptor`, a sampler owns no host storage -- it is pure
/// filtering/addressing state, always valid regardless of whether the host
/// wrote it (the zero value is `Nearest`/`Repeat` filtering with no
/// comparison or anisotropy, a legal if unhelpful sampler).
struct FemeSamplerDescriptor {
  /// Minification filter (`SamplerFilter`).
  uint32_t MinFilter;
  /// Magnification filter (`SamplerFilter`).
  uint32_t MagFilter;
  /// Mip-level filter (`SamplerFilter`).
  uint32_t MipFilter;
  /// Per-axis addressing modes (`SamplerAddressMode`).
  uint32_t AddressU;
  uint32_t AddressV;
  uint32_t AddressW;
  /// Bias applied to the computed level of detail before clamping.
  float LodBias;
  /// Clamp bounds for the computed level of detail.
  float MinLod;
  float MaxLod;
  /// Comparison function (`SamplerCompareFunc`), meaningful only when
  /// `FEME_SAMPLER_COMPARE_ENABLE` is set.
  uint32_t CompareFunc;
  /// Border color for `SamplerAddressMode::ClampToBorder`, RGBA order.
  float BorderColor[4];
  /// Maximum anisotropy, meaningful only when
  /// `FEME_SAMPLER_ANISOTROPY_ENABLE` is set.
  float MaxAnisotropy;
  /// How a multi-texel footprint reduces to one value (`SamplerReductionMode`).
  uint32_t ReductionMode;
  /// `FemeSamplerDescriptorFlagBits` bitmask.
  uint32_t Flags;
  /// ABI headroom for later sampler-descriptor extensions.
  uint32_t Reserved[3];
};

/// The version written to `FemeVertexArgs::AbiVersion` and
/// `FemeFragmentArgs::AbiVersion`. A host and compiled stage must agree on it
/// before interpreting any graphics-stage batch ABI struct in this header.
constexpr uint32_t StageArgsAbiVersion = 1;

/// The scalar type one `FemeStageElement` stores.
enum class StageLayoutScalarKind : uint32_t {
  Invalid = 0,
  Float = 1,
  SInt = 2,
  UInt = 3,
  Bool = 4,
};

/// The system value a stage-layout element names, mirroring
/// `feme::SignatureSystemValue` without depending on that C++ reflection model.
/// `None` means an ordinary user varying whose bytes live in the stage-storage
/// block `FemeVertexArgs::Inputs`/`Outputs` or `FemeFragmentArgs::Inputs`/
/// `Outputs` point to.
enum class StageLayoutSystemValue : uint32_t {
  None = 0,
  Position = 1,
  ClipDistance = 2,
  CullDistance = 3,
  VertexID = 4,
  InstanceID = 5,
  BaseVertex = 6,
  BaseInstance = 7,
  DrawID = 8,
  PrimitiveID = 9,
  IsFrontFace = 10,
  SampleIndex = 11,
  Coverage = 12,
  IsHelperLane = 13,
  Depth = 14,
  StencilRef = 15,
  RenderTargetArrayIndex = 16,
  ViewportArrayIndex = 17,
  /// The output control point index a hull/control-stage invocation is
  /// computing (roadmap R34's continuation): `feme::cpu::HullWrapperPass`'s
  /// own per-invocation loop index, matching `SignatureSystemValue::
  /// OutputControlPointID`.
  OutputControlPointID = 18,
  /// The tessellator-generated domain coordinate a domain/evaluation-stage
  /// invocation is evaluating, matching `SignatureSystemValue::
  /// DomainLocation`. Sourced from `FemeDomainInvocation::DomainLocation`
  /// rather than from a stage-storage block, so an element carrying this
  /// system value has no meaningful strides or data offset.
  DomainLocation = 19,
  /// (Roadmap H2) `gl_ViewIndex`: the current multiview render-pass
  /// instance view, matching `SignatureSystemValue::ViewIndex`. Sourced
  /// from `FemeVertexInvocation::ViewIndex`/`FemeFragmentInvocation::
  /// ViewIndex`, the same value for every invocation of one draw.
  ViewIndex = 20,
};

/// The interpolation mode recorded for one stage-layout element, mirroring
/// `feme::SignatureInterpolationMode`.
enum class StageLayoutInterpolationMode : uint32_t {
  Flat = 0,
  Perspective = 1,
  PerspectiveCentroid = 2,
  PerspectiveSample = 3,
  NoPerspective = 4,
  NoPerspectiveCentroid = 5,
  NoPerspectiveSample = 6,
};

/// The frequency recorded for one stage-layout element, mirroring
/// `feme::SignatureFrequency`.
enum class StageLayoutFrequency : uint32_t {
  PerVertex = 0,
  PerPrimitive = 1,
  PerPatch = 2,
  PerSample = 3,
};

/// Bits of `FemeStageElement::Flags`.
enum FemeStageElementFlagBits : uint32_t {
  /// Set if `SystemValue != StageLayoutSystemValue::None`. Convenience only;
  /// the two fields must agree.
  FEME_STAGE_ELEMENT_SYSTEM_VALUE = 1u << 0,
};

/// One entry of a stage layout: the byte-addressing recipe for one signature
/// element's structure-of-arrays storage, or the metadata identifying which
/// system value a wrapper should source directly from its invocation record.
/// `Elements` are dense by `ElementID`: `FemeStageLayout::Elements[ElementID]`
/// is the entry the compiled shader expects when it issues
/// `feme.stage.input.load`/`output.store` for that ID.
struct FemeStageElement {
  /// The stable signature element ID this entry describes.
  uint32_t ElementID;
  /// The scalar type stored at this element's addresses
  /// (`StageLayoutScalarKind`).
  uint32_t ScalarKind;
  /// The scalar bit width (8, 16, 32, or 64) stored at this element's
  /// addresses.
  uint32_t BitWidth;
  /// The first declared register component of this element.
  uint32_t FirstComponent;
  /// The number of contiguous declared components starting at
  /// `FirstComponent`.
  uint32_t ComponentCount;
  /// The number of rows this element spans.
  uint32_t RowCount;
  /// How a fragment input is interpolated (`StageLayoutInterpolationMode`).
  uint32_t Interpolation;
  /// How often this element varies (`StageLayoutFrequency`).
  uint32_t Frequency;
  /// Which system value this element names (`StageLayoutSystemValue`), or
  /// `None` for ordinary stage-storage-backed data.
  uint32_t SystemValue;
  /// The byte distance between successive invocations inside one row/component
  /// array. For tightly-packed stage storage this is the scalar size in bytes.
  uint32_t InvocationStride;
  /// The byte distance between adjacent declared components of one row.
  uint32_t ComponentStride;
  /// The byte distance between adjacent rows.
  uint32_t RowStride;
  /// Byte offset of row 0, component `FirstComponent`, invocation 0 within the
  /// stage-storage block `Inputs`/`Outputs` points to. Ignored for system
  /// values, which the wrapper sources from its invocation record instead.
  uint64_t DataOffset;
  /// `FemeStageElementFlagBits` bitmask.
  uint32_t Flags;
  /// ABI headroom for later per-element layout metadata.
  uint32_t Reserved[3];
};

/// One immutable stage layout: the dense `ElementID` -> `FemeStageElement`
/// table a compiled vertex or fragment wrapper uses to interpret the raw
/// `Inputs`/`Outputs` byte blocks it receives.
struct FemeStageLayout {
  /// Dense `ElementID` -> `FemeStageElement` table. Null only when
  /// `ElementCount == 0`.
  const FemeStageElement *Elements;
  /// Number of entries in `Elements`.
  uint32_t ElementCount;
  /// ABI headroom for later whole-layout metadata.
  uint32_t Reserved[7];
};

/// The resources any compiled stage may read: the descriptor heaps and root
/// constants shared by compute, vertex, and fragment stages -- and, since
/// roadmap R29, by `FemeDispatchArgs` too (see "Relationship to the compute
/// ABI" in feme/docs/FeMeGraphicsDesign.md), so there is exactly one
/// resource-binding contract for every stage rather than a compute-only
/// duplicate.
struct FemeShaderResources {
  /// The resource descriptor heap: `ResourceDescriptorHeap[i]` indexes this
  /// array.
  const FemeDescriptor *ResourceHeap;
  /// Number of descriptors in `ResourceHeap`.
  uint32_t ResourceHeapCount;
  /// The image descriptor heap.
  const FemeImageDescriptor *ImageHeap;
  /// Number of descriptors in `ImageHeap`.
  uint32_t ImageHeapCount;
  /// The sampler descriptor heap.
  const FemeSamplerDescriptor *SamplerHeap;
  /// Number of descriptors in `SamplerHeap`.
  uint32_t SamplerHeapCount;
  /// The root constant block, or null if the shader declares none.
  const void *RootConstants;
  /// Size of the root constant block in bytes.
  uint32_t RootConstantSize;
  /// (roadmap F8a) The subpass-input heap: `feme.stage.subpass.load`'s
  /// `attachment_index` operand selects a slot here directly (not through
  /// `feme::cpu::BoundResourceNormalizationPass`'s heap-base/range-size
  /// scheme -- every slot is one currently-bound render-target attachment,
  /// resolved by the CPU executor from `feme::vulkan::GraphicsState`'s
  /// `ColorAttachmentInputIndices`/`DepthInputAttachmentIndex`/
  /// `StencilInputAttachmentIndex` for every draw, not from `VkDescriptorSet`
  /// state -- see "Render passes and dynamic rendering" in
  /// feme/docs/FeMeVulkanDesign.md).
  const FemeImageDescriptor *SubpassInputHeap;
  /// Number of descriptors in `SubpassInputHeap`.
  uint32_t SubpassInputHeapCount;
  /// ABI headroom for resource-model extensions.
  void *Reserved[2];
};

/// One vertex-stage invocation record. The compiled wrapper uses these fields
/// for system-value `feme.stage.input.load`s; user attributes are read from the
/// separate structure-of-arrays `Inputs` block using `InputLayout`.
struct FemeVertexInvocation {
  /// Shader-visible vertex ID.
  uint32_t VertexID;
  /// Shader-visible instance ID.
  uint32_t InstanceID;
  /// Shader-visible base-vertex offset.
  int32_t BaseVertex;
  /// Shader-visible base-instance offset.
  uint32_t BaseInstance;
  /// Shader-visible draw ID.
  uint32_t DrawID;
  /// (Roadmap H2) `gl_ViewIndex`: the current multiview render-pass
  /// instance view, or 0 for a non-multiview draw. The same value for
  /// every invocation of one draw (see `feme::graphics::PreparedDraw::
  /// ViewIndex`).
  uint32_t ViewIndex;
  /// ABI headroom for later vertex-stage invocation metadata.
  uint32_t Reserved[2];
};

/// One fragment-stage quad record. Lane bits `0..3` in `LiveMask` and
/// `SideEffectMask` correspond to quad lanes `(0,0)`, `(1,0)`, `(0,1)`, and
/// `(1,1)` respectively.
struct FemeFragmentInvocation {
  /// `SV_Position`/`FragCoord`-style position values per lane, laid out as
  /// `Position[Lane][Component]` with four components per lane.
  float Position[4][4];
  /// Primitive ID per lane.
  uint32_t PrimitiveID[4];
  /// Sample index per lane.
  uint32_t SampleIndex[4];
  /// Coverage mask per lane.
  uint32_t Coverage[4];
  /// Front-face flag per lane: 0 for back-facing, 1 for front-facing.
  uint32_t IsFrontFace[4];
  /// (Roadmap H3a) `gl_ViewportIndex` read back as a fragment-shader input:
  /// the viewport index the rasterizer resolved for each lane's primitive,
  /// one value per lane (not per quad, unlike `ViewIndex` below) since a
  /// 2x2 quad can straddle a primitive boundary where neighboring
  /// primitives resolved different viewport indices at a silhouette edge.
  uint32_t ViewportIndex[4];
  /// (Roadmap H2) `gl_ViewIndex`: the current multiview render-pass
  /// instance view, or 0 for a non-multiview draw. One value per quad
  /// (not per lane), matching `FemeVertexInvocation::ViewIndex`'s own
  /// "same value for every invocation of one draw" rule.
  uint32_t ViewIndex;
  /// Lanes participating in execution, including helper lanes.
  uint32_t LiveMask;
  /// Lanes allowed to perform side effects.
  uint32_t SideEffectMask;
  /// ABI headroom for later fragment-stage invocation metadata.
  uint32_t Reserved[4];
};

/// One fragment-stage quad's post-shader status. Color/depth/stencil/coverage
/// outputs themselves live in the separate `Outputs` stage-storage block; this
/// record carries only the final execution masks output merge needs.
struct FemeFragmentResult {
  /// Lanes still live when the shader returned.
  uint32_t LiveMask;
  /// Lanes still permitted to perform side effects when the shader returned.
  uint32_t SideEffectMask;
  /// ABI headroom for later fragment result metadata.
  uint32_t Reserved[6];
};

/// The single argument a compiled vertex-stage entry point takes:
///
/// \code
///   void feme_cpu_entry_<name>(const FemeVertexArgs *Args);
/// \endcode
struct FemeVertexArgs {
  /// `StageArgsAbiVersion`.
  uint32_t AbiVersion;
  /// Number of invocation records in `Invocations`, and therefore the number of
  /// structure-of-arrays slots in `Inputs` and `Outputs`.
  uint32_t InvocationCount;
  /// Reserved 32-bit fields to keep pointer fields naturally aligned and leave
  /// room for later scalar metadata.
  uint32_t Reserved32[2];
  /// Resource/root-constant block shared by every stage.
  const FemeShaderResources *Resources;
  /// Layout describing `Inputs`.
  const FemeStageLayout *InputLayout;
  /// Structure-of-arrays input storage for user attributes and any other
  /// non-system-value inputs.
  const void *Inputs;
  /// Layout describing `Outputs`.
  const FemeStageLayout *OutputLayout;
  /// Structure-of-arrays output storage for this batch.
  void *Outputs;
  /// Per-invocation system-value records.
  const FemeVertexInvocation *Invocations;
  /// ABI headroom for later vertex-batch metadata.
  void *Reserved[4];
};

/// The single argument a compiled fragment-stage entry point takes:
///
/// \code
///   void feme_cpu_entry_<name>(const FemeFragmentArgs *Args);
/// \endcode
struct FemeFragmentArgs {
  /// `StageArgsAbiVersion`.
  uint32_t AbiVersion;
  /// Number of quad records in `Invocations` and `Results`. `Inputs` and
  /// `Outputs` hold `4 * QuadCount` invocation slots.
  uint32_t QuadCount;
  /// Reserved 32-bit fields to keep pointer fields naturally aligned and leave
  /// room for later scalar metadata.
  uint32_t Reserved32[2];
  /// Resource/root-constant block shared by every stage.
  const FemeShaderResources *Resources;
  /// Layout describing `Inputs`.
  const FemeStageLayout *InputLayout;
  /// Structure-of-arrays input storage for user varyings and any other
  /// non-system-value inputs.
  const void *Inputs;
  /// Layout describing `Outputs`.
  const FemeStageLayout *OutputLayout;
  /// Structure-of-arrays output storage for this batch.
  void *Outputs;
  /// Per-quad invocation records. Quad lane order is fixed at
  /// `(0,0),(1,0),(0,1),(1,1)`.
  const FemeFragmentInvocation *Invocations;
  /// Per-quad final masks written by the fragment wrapper.
  FemeFragmentResult *Results;
  /// ABI headroom for later fragment-batch metadata.
  void *Reserved[4];
};

/// The single argument a compiled control (hull) stage's control-point entry
/// point takes:
///
/// \code
///   void feme_cpu_entry_<name>(const FemePatchArgs *Args);
/// \endcode
///
/// Roadmap R34's continuation: `feme::cpu::HullWrapperPass` batches the
/// control-point phase of a hull shader -- one invocation per output control
/// point, addressed by `StageLayoutSystemValue::OutputControlPointID` -- the
/// same structure-of-arrays shape `FemeVertexArgs` uses for a vertex batch,
/// with `OutputControlPointCount` invocations rather than an explicit
/// per-invocation record array (a control point's identity *is* its index;
/// unlike a vertex, it has no independent system values of its own). The
/// patch-constant phase (reading the completed `OutputPatch` this phase
/// produces, and writing tessellation factors and patch constants) is
/// `FemePatchConstantArgs` below, added after R34's initial landing -- see
/// PatchConstantWrapper.cpp's file comment for its own scope, including its
/// `InputPatch` parameter support (a further follow-up).
struct FemePatchArgs {
  /// `StageArgsAbiVersion`.
  uint32_t AbiVersion;
  /// Number of output control points this batch computes, and therefore the
  /// number of structure-of-arrays slots in `Outputs`.
  uint32_t OutputControlPointCount;
  /// Number of input control points in the original patch. This may differ
  /// from `OutputControlPointCount`.
  uint32_t InputPatchControlPointCount;
  /// Reserved 32-bit field to keep pointer fields naturally aligned and leave
  /// room for later scalar metadata.
  uint32_t Reserved32;
  /// Resource/root-constant block shared by every stage.
  const FemeShaderResources *Resources;
  /// Layout describing `Inputs`.
  const FemeStageLayout *InputLayout;
  /// Structure-of-arrays input storage for the input control points' user
  /// attributes, addressed the same way `Outputs` is: this milestone's
  /// wrapper only supports a control point reading its own input control
  /// point's attributes (see HullWrapper.cpp), so callers with an unequal
  /// input/output control point count still provide one input slot per
  /// *output* control point (duplicating shared input data across output
  /// slots if the source patch has fewer inputs than outputs).
  const void *Inputs;
  /// Layout describing `Outputs`.
  const FemeStageLayout *OutputLayout;
  /// Structure-of-arrays output storage for this batch's control points.
  void *Outputs;
  /// ABI headroom for later patch-batch metadata.
  void *Reserved[4];
};

/// The single argument a compiled patch-constant entry point takes:
///
/// \code
///   void feme_cpu_entry_<name>(const FemePatchConstantArgs *Args);
/// \endcode
///
/// Added after roadmap R34's initial landing, closing its "patch-constant
/// function" open item: exactly one invocation computes a whole patch's
/// tessellation factors and patch constants, reading the completed
/// `OutputPatch` `feme::cpu::HullWrapperPass`'s control-point phase produced
/// -- so `Inputs`/`InputLayout` here match `FemePatchArgs::Outputs`/
/// `OutputLayout`'s own shape and are addressed the same way, except that
/// this phase's `feme.stage.input.load` control-point-index operand may
/// legally name *any* control point in `[0, OutputControlPointCount)`, not
/// only the invoking lane's own (there is only one invocation, and it alone
/// computes every tessellation factor and patch constant, typically by
/// reading more than one control point to do so -- e.g. an edge factor from
/// two adjacent corners' positions).
///
/// `InputPatch`/`InputPatchLayout`/`InputPatchControlPointCount`, added in a
/// further follow-up, close that milestone's own "InputPatch parameter"
/// deferral: a patch-constant function may also declare an `InputPatch<T,
/// M>` parameter naming the *original*, pre-control-stage input control
/// points (a hull shader's own input, not its output) -- a second, distinct
/// structure-of-arrays block from `Inputs`, addressed the same way but with
/// its own control-point count (`M` need not equal `OutputControlPointCount`
/// any more than a hull shader's own input/output control point counts need
/// to agree). `SignatureElement::FromInputPatch` on a `SignatureDirection::
/// Input` element is what tells `feme::cpu::PatchConstantWrapperPass` which
/// of the two blocks a given `feme.stage.input.load` addresses -- see
/// PatchConstantWrapper.cpp's file comment for this milestone's scope and
/// what remains.
struct FemePatchConstantArgs {
  /// `StageArgsAbiVersion`.
  uint32_t AbiVersion;
  /// Number of output control points in `Inputs`, bounding the
  /// control-point-index operand a `feme.stage.input.load` reading the
  /// completed `OutputPatch` in this phase may legally use.
  uint32_t OutputControlPointCount;
  /// Number of input control points in `InputPatch`, bounding the
  /// control-point-index operand a `feme.stage.input.load` reading the
  /// original `InputPatch` (`SignatureElement::FromInputPatch`) may legally
  /// use. Zero if the patch-constant function declares no `InputPatch`
  /// parameter.
  uint32_t InputPatchControlPointCount;
  /// Reserved 32-bit field to keep pointer fields naturally aligned and
  /// leave room for later scalar metadata.
  uint32_t Reserved32;
  /// Resource/root-constant block shared by every stage.
  const FemeShaderResources *Resources;
  /// Layout describing `Inputs`.
  const FemeStageLayout *InputLayout;
  /// Structure-of-arrays storage for the completed output control points'
  /// attributes, one slot per control point (matching `FemePatchArgs::
  /// Outputs`'s own layout).
  const void *Inputs;
  /// Layout describing `InputPatch`. Null if the patch-constant function
  /// declares no `InputPatch` parameter.
  const FemeStageLayout *InputPatchLayout;
  /// Structure-of-arrays storage for the original, pre-control-stage input
  /// control points' attributes, one slot per input control point. Null if
  /// the patch-constant function declares no `InputPatch` parameter.
  const void *InputPatch;
  /// Layout describing `Outputs`: this phase's `SignatureDirection::
  /// PatchOutput` elements (tessellation factors and patch constants).
  const FemeStageLayout *OutputLayout;
  /// Per-patch scalar storage for this invocation's tessellation-factor and
  /// patch-constant writes. Unlike `Inputs`, this is not structure-of-arrays
  /// over a control-point count: there is exactly one patch's worth of
  /// storage, addressed by row/component alone.
  void *Outputs;
  /// ABI headroom for later patch-constant-batch metadata.
  void *Reserved[2];
};

/// One domain/evaluation-stage invocation record: the tessellator-generated
/// domain coordinate this invocation evaluates the patch at. The compiled
/// wrapper reads `SignatureSystemValue::DomainLocation` from here rather
/// than from a stage-storage block, exactly as `FemeVertexInvocation` serves
/// a vertex batch's own system values.
struct FemeDomainInvocation {
  /// The (u, v, w) domain coordinate `feme::graphics::tessellate` generated
  /// (Tessellator.h). An isoline/quad domain uses the first two components
  /// and leaves the third zero; a triangle domain uses all three as a
  /// barycentric coordinate.
  float DomainLocation[3];
  /// ABI headroom for later domain-stage invocation metadata (a primitive
  /// ID, for instance).
  uint32_t Reserved[5];
};

/// The single argument a compiled domain/evaluation entry point takes:
///
/// \code
///   void feme_cpu_entry_<name>(const FemeDomainArgs *Args);
/// \endcode
///
/// Roadmap R34's continuation, closing its "domain wrapper" open item: one
/// invocation per tessellator-generated domain point, batched over
/// `DomainPointCount` the way `FemeVertexArgs` batches vertices, evaluating
/// the completed patch the hull stage produced. Three input sources meet
/// here, which is what distinguishes this ABI from every earlier stage's:
///
///  - `Inputs`/`InputLayout`: the completed `OutputPatch` control points,
///    the same structure-of-arrays block `FemePatchArgs::Outputs` /
///    `FemePatchConstantArgs::Inputs` describe, and readable at *any*
///    control-point index in `[0, OutputControlPointCount)` -- evaluating a
///    patch means blending its control points, not reading just one.
///  - `PatchConstants`/`PatchConstantLayout`: the per-patch tessellation
///    factors and patch constants `FemePatchConstantArgs::Outputs` received,
///    read through this stage's `SignatureDirection::PatchInput` elements.
///    Per-patch, not per-invocation: addressed by row/component alone.
///  - `Invocations`: this batch's domain coordinates.
///
/// `Outputs`/`OutputLayout` are then ordinary per-vertex outputs, addressed
/// per invocation exactly like a vertex batch's -- a domain shader's result
/// is a vertex, and the rasterizer consumes it as one.
struct FemeDomainArgs {
  /// `StageArgsAbiVersion`.
  uint32_t AbiVersion;
  /// Number of domain points this batch evaluates: the number of records in
  /// `Invocations`, and of structure-of-arrays slots in `Outputs`.
  uint32_t DomainPointCount;
  /// Number of control points in `Inputs`, bounding the control-point-index
  /// operand a `feme.stage.input.load` reading the completed patch may
  /// legally use.
  uint32_t OutputControlPointCount;
  /// Reserved 32-bit field to keep pointer fields naturally aligned and
  /// leave room for later scalar metadata.
  uint32_t Reserved32;
  /// Resource/root-constant block shared by every stage.
  const FemeShaderResources *Resources;
  /// Layout describing `Inputs`.
  const FemeStageLayout *InputLayout;
  /// Structure-of-arrays storage for the completed patch's control points,
  /// one slot per control point.
  const void *Inputs;
  /// Layout describing `PatchConstants`: this stage's
  /// `SignatureDirection::PatchInput` elements. Null if the domain shader
  /// reads no patch constant or tessellation factor.
  const FemeStageLayout *PatchConstantLayout;
  /// Per-patch storage for the tessellation factors and patch constants the
  /// hull shader's patch-constant phase wrote, addressed by row/component
  /// alone. Null if the domain shader reads none.
  const void *PatchConstants;
  /// Layout describing `Outputs`.
  const FemeStageLayout *OutputLayout;
  /// Structure-of-arrays output storage for this batch's evaluated vertices.
  void *Outputs;
  /// Per-invocation domain-coordinate records.
  const FemeDomainInvocation *Invocations;
  /// ABI headroom for later domain-batch metadata.
  void *Reserved[2];
};

/// One geometry-stage invocation's own system value: which assembled input
/// primitive it processes. Mirrors `FemeDomainInvocation`'s role of holding
/// per-invocation system-value data separate from stage storage.
struct FemeGeometryInvocation {
  /// `SV_PrimitiveID`: this invocation's input primitive index within the
  /// draw (`StageLayoutSystemValue::PrimitiveID`), not necessarily the same
  /// as its index within this batch.
  uint32_t PrimitiveID;
  /// ABI headroom for later geometry-invocation metadata (an instance ID,
  /// for instance).
  uint32_t Reserved[7];
};

/// The single argument a compiled geometry entry point takes:
///
/// \code
///   void feme_cpu_entry_<name>(const FemeGeometryArgs *Args);
/// \endcode
///
/// Roadmap R34's continuation, closing its "geometry wrapper" open item: one
/// invocation per assembled input primitive, batched over `PrimitiveCount`
/// the way `FemeVertexArgs` batches vertices. Two things distinguish this
/// ABI from every earlier stage's, both covered in depth by
/// GeometryWrapper.cpp's file comment:
///
///  - `Inputs`/`InputLayout` is structure-of-arrays over `PrimitiveCount *
///    VerticesPerPrimitive` slots, primitive-major (primitive P's vertex V is
///    slot `P * VerticesPerPrimitive + V`), read through ordinary
///    `feme.stage.input.load`s whose vertex-in-primitive operand may name
///    *any* of the primitive's `VerticesPerPrimitive` vertices -- unlike
///    `HullWrapperPass`'s control-point phase, a geometry invocation
///    legitimately reads more than one input vertex (e.g. an adjacency
///    triangle's 6 vertices), so there is no "own index only" restriction
///    here.
///  - Output is not a per-invocation storage slot read back afterward the
///    way every earlier stage's `Outputs` is: a geometry invocation may
///    `emit` zero or more vertices per stream, so `Outputs`/`OutputLayout`
///    are ordinary per-invocation scratch storage that `feme.stage.output.
///    store` writes into, and `feme.stage.stream.emit`
///    (`StageOpKind::StreamEmit`) is what snapshots that scratch storage's
///    *current* values into the bounded `EmittedVertices` record below.
///    This milestone supports exactly one output stream (stream 0); a
///    `feme.stage.stream.emit`/`.cut` naming any other stream is diagnosed.
struct FemeGeometryArgs {
  /// `StageArgsAbiVersion`.
  uint32_t AbiVersion;
  /// Number of input primitives this batch processes: the number of records
  /// in `Invocations`, and (times `VerticesPerPrimitive`) the number of
  /// structure-of-arrays slots in `Inputs`.
  uint32_t PrimitiveCount;
  /// Number of vertices in one input primitive (3 for an ordinary triangle,
  /// 6 for a triangle-with-adjacency, and so on).
  uint32_t VerticesPerPrimitive;
  /// The maximum number of vertices one invocation may `emit` onto stream 0
  /// -- the wrapper checks this bound before every emission, exactly as
  /// `feme::graphics::GeometryStreamBuilder` itself does.
  uint32_t MaxVerticesPerStream;
  /// The number of scalar components one emitted vertex record holds: the
  /// total component count across every `SignatureDirection::Output`
  /// element on stream 0, in signature order. The caller (which already has
  /// the entry point's `EntrySignature`) computes this the same way the
  /// wrapper itself does -- see GeometryWrapper.cpp's file comment.
  uint32_t OutputScalarsPerVertex;
  /// Reserved 32-bit field to keep pointer fields naturally aligned and
  /// leave room for later scalar metadata.
  uint32_t Reserved32;
  /// Resource/root-constant block shared by every stage.
  const FemeShaderResources *Resources;
  /// Layout describing `Inputs`.
  const FemeStageLayout *InputLayout;
  /// Structure-of-arrays storage for the assembled input primitives' vertex
  /// attributes, `PrimitiveCount * VerticesPerPrimitive` slots.
  const void *Inputs;
  /// Layout describing `Outputs`.
  const FemeStageLayout *OutputLayout;
  /// Per-invocation scratch storage for the output signature values in
  /// flight at the moment of the next `emit` snapshot: `PrimitiveCount`
  /// structure-of-arrays slots, addressed exactly like a vertex batch's own
  /// `Outputs`.
  void *Outputs;
  /// Per-invocation system-value records.
  const FemeGeometryInvocation *Invocations;
  /// Flat `PrimitiveCount * MaxVerticesPerStream * OutputScalarsPerVertex`
  /// storage for stream 0's emitted vertex records (primitive-major, then
  /// vertex-slot, then scalar), written by `feme.stage.stream.emit`
  /// lowering.
  float *EmittedVertices;
  /// `PrimitiveCount` counts of how many vertices each invocation actually
  /// emitted onto stream 0, bounded to `MaxVerticesPerStream`.
  uint32_t *EmittedVertexCounts;
  /// Flat `PrimitiveCount * MaxVerticesPerStream` strip-boundary flags:
  /// `StripEndsAfter[P * MaxVerticesPerStream + I]` is nonzero if a
  /// `feme.stage.stream.cut` closed the strip immediately after emitted
  /// vertex `I` of primitive `P` -- see GeometryWrapper.cpp's file comment
  /// for why this flat representation is equivalent to replaying the
  /// invocation's actual `emit`/`cut` call sequence.
  uint8_t *StripEndsAfter;
  /// ABI headroom for later geometry-batch metadata.
  void *Reserved[2];
};

/// The single argument every compiled compute entry point takes:
///
/// \code
///   void feme_cpu_entry_<name>(const FemeDispatchArgs *Args);
/// \endcode
///
/// One exported symbol per entry point, named with a `feme_cpu_entry_`
/// prefix (see "Kernel ABI" in feme/docs/FeMeCPUDesign.md). Everything a
/// shader can ask about its position in the dispatch derives from `GroupID`,
/// `GroupCount` and the wave loop index the entry wrapper introduces, so
/// this struct's shape does not change with the resolved wave size, the
/// shader's resource usage, or between the JIT and object-file paths.
struct FemeDispatchArgs {
  /// The resource/image/sampler descriptor heaps and root constant block,
  /// shared with the vertex and fragment stage argument blocks below (see
  /// `FemeShaderResources` and "Relationship to the compute ABI" in
  /// feme/docs/FeMeGraphicsDesign.md). Roadmap R29 folded what used to be
  /// this struct's own `ResourceHeap`/`SamplerHeap`/`RootConstants` fields
  /// into this embedded block rather than gaining a second, compute-only
  /// spelling of the same resource contract.
  FemeShaderResources Resources;
  /// This dispatch item's 3D group coordinate.
  uint32_t GroupID[3];
  /// The full dispatch's 3D group count.
  uint32_t GroupCount[3];
  /// Group-shared storage for this group, sized/aligned per the shader's
  /// declared groupshared usage, or null if it declares none.
  void *GroupShared;
  /// ABI headroom for fields a future revision may add without breaking
  /// binary compatibility with already-compiled shaders.
  void *Reserved[4];
};

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_RUNTIMEABI_H
