//===- RuntimeABI.h - FeMe CPU target C ABI ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the C ABI the FeMe CPU target's compiled shaders and
// their host share: the descriptor layout a bindless resource/sampler heap
// is made of (`FemeDescriptor`), the single argument every compiled entry
// point takes (`FemeDispatchArgs`), and the flag/format/kind values that
// give the descriptor's fields meaning. See the "Resource Model" and
// "Kernel ABI" sections of feme/docs/FeMeCPUDesign.md, which this header is
// the literal transcription of -- every field, bit and enumerator value
// here is part of that design and must stay in sync with it.
//
// This header is plain C-compatible data only (no functions, no C++
// features besides `enum class`/namespacing): both feme::cpu's own
// compiler-side code and a host embedding FeMe link against it, and a
// compiled shader's object file refers to these layouts without itself
// depending on any FeMe library.
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

/// The single argument every compiled entry point takes:
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
