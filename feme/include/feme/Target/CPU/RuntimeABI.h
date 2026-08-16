//===- RuntimeABI.h - FeMe CPU target C ABI ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the C ABI the FeMe CPU target's compiled shaders and
// their host share. Today that covers both the compute-dispatch ABI
// (`FemeDispatchArgs`) and roadmap R28's graphics-stage batch ABI
// (`FemeShaderResources`, `FemeStageLayout`, `FemeVertexArgs`, and
// `FemeFragmentArgs`), plus the descriptor/layout/system-value enumerators that
// give those structs' fields meaning.
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
};

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
/// constants today shared by compute, vertex, and fragment stages. This is the
/// graphics/runtime analogue of the resource-related prefix of
/// `FemeDispatchArgs`, split out so vertex and fragment batches need not carry
/// compute-specific dispatch state.
struct FemeShaderResources {
  /// The resource descriptor heap: `ResourceDescriptorHeap[i]` indexes this
  /// array.
  const FemeDescriptor *ResourceHeap;
  /// Number of descriptors in `ResourceHeap`.
  uint32_t ResourceHeapCount;
  /// The sampler descriptor heap. It intentionally still reuses
  /// `FemeDescriptor` until roadmap R29 gives samplers their own descriptor
  /// type and folds that change through every stage uniformly.
  const FemeDescriptor *SamplerHeap;
  /// Number of descriptors in `SamplerHeap`.
  uint32_t SamplerHeapCount;
  /// The root constant block, or null if the shader declares none.
  const void *RootConstants;
  /// Size of the root constant block in bytes.
  uint32_t RootConstantSize;
  /// ABI headroom for image/sampler/resource-model extensions.
  void *Reserved[4];
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
  /// ABI headroom for later vertex-stage invocation metadata.
  uint32_t Reserved[3];
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
  /// Lanes participating in execution, including helper lanes.
  uint32_t LiveMask;
  /// Lanes allowed to perform side effects.
  uint32_t SideEffectMask;
  /// ABI headroom for later fragment-stage invocation metadata.
  uint32_t Reserved[6];
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
  /// The resource descriptor heap: `ResourceDescriptorHeap[i]` indexes this
  /// array.
  const FemeDescriptor *ResourceHeap;
  uint32_t ResourceHeapCount;
  /// The sampler descriptor heap: `SamplerDescriptorHeap[i]` indexes this
  /// array. Part of the ABI from the start even though sampling is a
  /// non-goal for v1, so that adding it later does not change the ABI.
  const FemeDescriptor *SamplerHeap;
  uint32_t SamplerHeapCount;
  /// The root constant block (see "Root constants" in
  /// feme/docs/FeMeCPUDesign.md), or null if the shader declares none.
  const void *RootConstants;
  uint32_t RootConstantSize;
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
