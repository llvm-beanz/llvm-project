//===- ResourceInfo.h - FeMe CPU target resource-usage info -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares `feme::cpu::ResourceInfo`, the reader for the heap-usage
// information `feme::cpu::ResourceLoweringPass` records (see "Heap usage
// discovery" in feme/docs/FeMeCPUDesign.md), and `feme::cpu::ArtifactInfo`,
// the versioned, object-file-friendly form of the same information plus the
// execution-shape fields ("Kernel ABI") an AOT host needs before it can
// dispatch a compiled entry point at all.
//
// Two representations exist because they serve different consumers at
// different times, per "Heap usage discovery":
//
//  - `ResourceInfo::fromModule` reads the `!feme.cpu.resources` named
//    metadata node directly, which only exists while the module is still
//    LLVM IR -- this is what the JIT path uses, since it never loses the
//    module.
//  - `ArtifactInfo` is what survives into an object file: a versioned,
//    read-only byte layout, exposed under the module as a data symbol named
//    `feme_cpu_info_<entry>` (`getArtifactSymbolName`/`emitArtifactGlobal`).
//    An AOT host that only has the compiled object reads this back with
//    `parseArtifact`; there is no metadata left for it to read at that
//    point. `readArtifactGlobal` is the JIT-adjacent, still-in-IR analogue,
//    used to test the byte format's round trip without going through actual
//    object-file codegen.
//
// Both report the same fields (`ResourceInfo`'s are a strict subset of
// `ArtifactInfo`'s), so a host is never told less because it chose the
// object-file path over the JIT one.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_RESOURCEINFO_H
#define FEME_TARGET_CPU_RESOURCEINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
class Function;
class GlobalVariable;
class Module;
} // namespace llvm

namespace feme::cpu {

/// The declared thread-group dimensions (an entry point's `hlsl.numthreads`
/// function attribute), or `{1, 1, 1}` if \p F declares none, or the
/// attribute is not exactly three comma-separated integers. Shared by
/// `feme::cpu::CompiledStage::create` (resolving the shape it compiles
/// against) and `feme::Driver`'s AOT retargeting path (resolving the same
/// shape for `ArtifactInfo` reflection, see "Heap usage discovery" in
/// feme/docs/FeMeCPUDesign.md), so both stay in agreement about what a
/// missing/malformed attribute means.
std::array<uint32_t, 3> getDeclaredGroupSize(const llvm::Function &F);

/// One traditionally-bound resource range's assignment in the reserved heap
/// prefix `feme::cpu::BoundResourceNormalizationPass` builds (see
/// "Bound-resource normalization" in feme/docs/FeMeCPUDesign.md): source
/// register space and base register, the range's declared array length, and
/// the contiguous base slot it was assigned in the resource heap. A host
/// materializing a physical heap for a dispatch matches its own bound
/// resources to one of these by (Space, BaseRegister), then writes array
/// element `j`'s descriptor at heap index `HeapBase + j`.
struct BoundResourceRange {
  uint32_t Space = 0;
  uint32_t BaseRegister = 0;
  uint32_t RangeSize = 0;
  uint32_t HeapBase = 0;
};

/// One entry point's descriptor-heap usage, as
/// `feme::cpu::ResourceLoweringPass` discovers it and records in the
/// `!feme.cpu.resources` named metadata node (see "Heap usage discovery" in
/// feme/docs/FeMeCPUDesign.md).
struct ResourceInfo {
  std::string EntryName;
  /// The root constant block's required byte span, or 0 if the shader reads
  /// none. Always 0 for now -- root constants are not yet implemented (see
  /// the Status section's Deviation note in feme/docs/FeMeCPUDesign.md).
  uint32_t RootConstantSize = 0;
  /// Whether the shader reads the sampler heap. Always false for now --
  /// `feme::dxil::OpRaisingPass` does not yet reconstruct a sampler handle
  /// from the heap (see `raiseResourceHandleFromHeap`'s comment).
  bool UsesSamplerHeap = false;
  /// The heap indices the shader reads through a compile-time-constant
  /// descriptor index, sorted and deduplicated. A dynamically-indexed
  /// access contributes nothing here -- it is still recorded as heap usage
  /// (`RootConstantSize`/`UsesSamplerHeap` cover the parts that are always
  /// knowable), just not as one of these specific indices.
  std::vector<uint32_t> StaticHeapIndices;
  /// The total size of the reserved resource-heap prefix
  /// `feme::cpu::BoundResourceNormalizationPass` builds for this shader's
  /// traditionally-bound resources, or 0 if it uses none (see
  /// "Bound-resource normalization"). A host materializing a physical
  /// resource heap must place its logical dynamic heap starting at this
  /// index (see "Descriptor heaps").
  uint32_t ReservedResourceHeapSize = 0;
  /// Each traditionally-bound range's assignment within the reserved
  /// prefix above.
  std::vector<BoundResourceRange> BoundRanges;

  /// Reads \p EntryName's entry from \p M's `!feme.cpu.resources` metadata,
  /// or `std::nullopt` if that entry (or the node itself) isn't present --
  /// e.g. because \p EntryName's function doesn't access any descriptor-heap
  /// resource, so `ResourceLoweringPass` never rewrote it. Also merges in
  /// \p EntryName's `!feme.cpu.bound_resources` entry, if
  /// `feme::cpu::BoundResourceNormalizationPass` recorded one.
  static std::optional<ResourceInfo> fromModule(const llvm::Module &M,
                                                llvm::StringRef EntryName);
};

/// The current version of the `ArtifactInfo` byte layout. Bumped whenever
/// that layout changes incompatibly; `parseArtifact` rejects any other
/// value rather than guessing at a different field order.
///
/// Version 2 (roadmap milestone 11) added `ReservedResourceHeapSize` and the
/// `BoundRanges` counted tail (see "Bound-resource normalization" in
/// feme/docs/FeMeCPUDesign.md): an AOT host materializing a physical
/// resource heap for a bound-resource shader needs both to place its bound
/// descriptors and its logical dynamic heap correctly.
constexpr uint32_t ArtifactAbiVersion = 2;

/// Bits of `ArtifactInfo::Flags`, mirrored in the serialized byte layout.
enum ArtifactFlagBits : uint32_t {
  FEME_CPU_ARTIFACT_USES_SAMPLER_HEAP = 1u << 0,
};

/// The versioned, object-file-friendly artifact `emitArtifactGlobal` writes
/// and `parseArtifact` reads back (see the file comment above and "Heap
/// usage discovery" in feme/docs/FeMeCPUDesign.md). Every field here is
/// part of the CPU target's AOT ABI, in the same sense
/// feme/include/feme/Target/CPU/RuntimeABI.h's structs are: layout and field
/// order must stay in sync with `serializeArtifact`/`parseArtifact`.
///
/// `WaveSize`, `GroupSize`, `GroupSharedSize` and `GroupSharedAlign` are
/// part of the versioned layout from the start (so a later milestone that
/// wires wave-size resolution and groupshared allocation into this pass
/// does not need a new artifact version), but nothing populates them with
/// anything but 0 yet: milestone 4 (wave size) and milestone 9
/// (groupshared, `feme::cpu::computeGroupSharedLayout`) both compute the
/// values these fields need, but neither is wired into an AOT-facing
/// `ArtifactInfo` builder yet -- only `feme::cpu::EntryWrapperPass`'s own
/// JIT-adjacent allocation (see "Groupshared memory" in "Phase 6: Group
/// Execution and Barriers" in feme/docs/FeMeCPUDesign.md) consumes them
/// today.
struct ArtifactInfo {
  uint32_t WaveSize = 0;
  uint32_t GroupSize[3] = {0, 0, 0};
  uint32_t GroupSharedSize = 0;
  uint32_t GroupSharedAlign = 0;
  uint32_t RootConstantSize = 0;
  uint32_t Flags = 0;
  std::vector<uint32_t> StaticHeapIndices;
  /// See `ResourceInfo::ReservedResourceHeapSize`/`BoundRanges`.
  uint32_t ReservedResourceHeapSize = 0;
  std::vector<BoundResourceRange> BoundRanges;

  /// Builds the execution-shape-agnostic fields of an `ArtifactInfo` from
  /// \p Info, leaving `WaveSize`/`GroupSize`/`GroupSharedSize`/
  /// `GroupSharedAlign` at their default (0) until a later milestone
  /// supplies them.
  static ArtifactInfo fromResourceInfo(const ResourceInfo &Info);
};

/// The AOT artifact symbol name for an entry point named \p EntryName (see
/// "Heap usage discovery"): `feme_cpu_info_<entry>`.
std::string getArtifactSymbolName(llvm::StringRef EntryName);

/// Serializes \p Info to the byte layout `parseArtifact` reads back: a
/// little-endian `ArtifactAbiVersion`, then \p Info's fields in declaration
/// order, then \p Info.StaticHeapIndices's count followed by the indices
/// themselves as a counted tail (see "Heap usage discovery").
std::vector<uint8_t> serializeArtifact(const ArtifactInfo &Info);

/// Parses \p Bytes as a serialized `ArtifactInfo`, or an `Error` if it is
/// too short, has a heap-index count inconsistent with its length, or
/// declares an ABI version other than `ArtifactAbiVersion`.
llvm::Expected<ArtifactInfo> parseArtifact(llvm::ArrayRef<uint8_t> Bytes);

/// Emits \p Info as a read-only data global named
/// `getArtifactSymbolName(EntryName)` in \p M, containing
/// `serializeArtifact(Info)`'s bytes. This is what survives compilation to
/// an object file for an AOT host to read back with `parseArtifact`; see
/// the file comment above.
llvm::GlobalVariable *emitArtifactGlobal(llvm::Module &M,
                                         llvm::StringRef EntryName,
                                         const ArtifactInfo &Info);

/// Reads back the artifact global `emitArtifactGlobal` wrote for
/// \p EntryName in \p M, or `std::nullopt` if no such global exists. This is
/// the JIT-adjacent, still-in-IR analogue of an AOT host reading the symbol
/// out of an object file -- see the file comment above for why both exist.
std::optional<ArtifactInfo> readArtifactGlobal(const llvm::Module &M,
                                               llvm::StringRef EntryName);

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_RESOURCEINFO_H
