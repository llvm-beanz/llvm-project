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
// discovery" in feme/docs/FeMeCPUDesign.md), and `feme::cpu::
// StageArtifactInfo`, the versioned, object-file-friendly form of the same
// information plus the execution-shape fields ("Kernel ABI") an AOT host
// needs before it can dispatch a compiled entry point at all.
//
// Two representations exist because they serve different consumers at
// different times, per "Heap usage discovery":
//
//  - `ResourceInfo::fromModule` reads the `!feme.cpu.resources` named
//    metadata node directly, which only exists while the module is still
//    LLVM IR -- this is what the JIT path uses, since it never loses the
//    module.
//  - `StageArtifactInfo` is what survives into an object file: a versioned,
//    read-only byte layout, exposed under the module as a data symbol named
//    `feme_cpu_info_<entry>` (`getArtifactSymbolName`/`emitArtifactGlobal`).
//    An AOT host that only has the compiled object reads this back with
//    `parseArtifact`; there is no metadata left for it to read at that
//    point. `readArtifactGlobal` is the JIT-adjacent, still-in-IR analogue,
//    used to test the byte format's round trip without going through actual
//    object-file codegen.
//
// Both report the same fields (`ResourceInfo`'s are a strict subset of
// `StageArtifactInfo`'s), so a host is never told less because it chose the
// object-file path over the JIT one.
//
// `StageArtifactInfo` (roadmap R22) generalizes what was, before this
// milestone, a compute-only `ArtifactInfo`: it is tagged with the
// `feme::ShaderStage` that produced it, carries the entry point's serialized
// `feme::EntrySignature` (feme/include/feme/Core/Signature.h), and summarizes
// stage side effects so the same structure and serialization can describe a
// compute, vertex, or fragment artifact. Roadmap R28 now populates `Stage`
// and `Signature` for stage-aware `CompiledStage`s; the layout existed from the
// start so that milestone did not need another ABI break.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_RESOURCEINFO_H
#define FEME_TARGET_CPU_RESOURCEINFO_H

#include "feme/Core/ShaderStage.h"

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
/// shape for `StageArtifactInfo` reflection, see "Heap usage discovery" in
/// feme/docs/FeMeCPUDesign.md), so both stay in agreement about what a
/// missing/malformed attribute means.
std::array<uint32_t, 3> getDeclaredGroupSize(const llvm::Function &F);

/// The side-effect-summary bits of `StageArtifactInfo::Flags` that
/// \p F's use of the `feme.stage.*` family (feme/include/feme/Core/
/// StageOps.h) implies: whether it calls `feme.stage.discard`/`.demote`/
/// `.is_helper` anywhere in its body. Every compute entry point today
/// reports none of these (only R20's vertex/fragment canonicalization ever
/// introduces such a call), but the scan itself is stage-agnostic, so this
/// is ready for roadmap R27/R28 to reuse once `CompiledStage` compiles
/// those stages too.
uint32_t computeSideEffectFlags(const llvm::Function &F);

/// Which of the three physical heaps a `BoundResourceRange`'s slots belong
/// to. The three are separate arrays with independently numbered slots
/// (`FemeShaderResources::ResourceHeap`/`ImageHeap`/`SamplerHeap`), so a
/// range's `HeapBase` is only meaningful together with its class -- an
/// image range's base 0 and a buffer range's base 0 name different storage.
enum class BoundResourceClass : uint32_t {
  /// A `FemeDescriptor` in the resource heap: every buffer kind.
  Buffer = 0,
  /// A `FemeImageDescriptor` in the image heap.
  Image = 1,
  /// A `FemeSamplerDescriptor` in the sampler heap.
  Sampler = 2,
};

/// One traditionally-bound resource range's assignment in the reserved heap
/// prefix `feme::cpu::BoundResourceNormalizationPass` builds (see
/// "Bound-resource normalization" in feme/docs/FeMeCPUDesign.md): source
/// register space and base register, the range's declared array length, the
/// heap its slots live in, and the contiguous base slot it was assigned
/// there. A host materializing a physical heap for a dispatch matches its
/// own bound resources to one of these by (Space, BaseRegister), then writes
/// array element `j`'s descriptor at index `HeapBase + j` of the heap
/// `Class` names.
struct BoundResourceRange {
  uint32_t Space = 0;
  uint32_t BaseRegister = 0;
  uint32_t RangeSize = 0;
  uint32_t HeapBase = 0;
  BoundResourceClass Class = BoundResourceClass::Buffer;
};

/// One entry point's descriptor-heap usage, as
/// `feme::cpu::ResourceLoweringPass` discovers it and records in the
/// `!feme.cpu.resources` named metadata node (see "Heap usage discovery" in
/// feme/docs/FeMeCPUDesign.md).
struct ResourceInfo {
  std::string EntryName;
  /// The root constant block's required byte span, or 0 if the shader reads
  /// none.
  uint32_t RootConstantSize = 0;
  /// The root constant binding's source register space and base register
  /// (roadmap R25: any single binding is recognized, not just the default
  /// `(b0, space0)`), meaningful only when `RootConstantSize != 0`.
  uint32_t RootConstantSpace = 0;
  uint32_t RootConstantRegister = 0;
  /// Whether the shader reads the sampler heap: set when the shader samples
  /// through a bound (`BoundResourceClass::Sampler`) or heap-indexed
  /// sampler descriptor.
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
  /// The same, for the image and sampler heaps: the reserved prefix each
  /// needs for this shader's bound (`BoundResourceClass::Image`/`Sampler`)
  /// ranges, or 0 if it binds none. A host's logical dynamic image/sampler
  /// heap starts at these indices, exactly as it does for buffers.
  uint32_t ReservedImageHeapSize = 0;
  uint32_t ReservedSamplerHeapSize = 0;
  /// Each traditionally-bound range's assignment within the reserved
  /// prefix of the heap its `Class` names.
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

/// The current version of the `StageArtifactInfo` byte layout. Bumped
/// whenever that layout changes incompatibly; `parseArtifact` rejects any
/// other value rather than guessing at a different field order.
///
/// Version 2 (roadmap milestone 11) added `ReservedResourceHeapSize` and the
/// `BoundRanges` counted tail (see "Bound-resource normalization" in
/// feme/docs/FeMeCPUDesign.md): an AOT host materializing a physical
/// resource heap for a bound-resource shader needs both to place its bound
/// descriptors and its logical dynamic heap correctly.
///
/// Version 3 (roadmap R22) generalized the compute-only `ArtifactInfo` into
/// `StageArtifactInfo`, adding `Stage` and `Signature`: a stage-tagged
/// artifact and the entry point's serialized `feme::EntrySignature` (empty
/// for a stage/milestone that does not populate one yet), plus new
/// `Flags` bits summarizing the entry's use of `feme.stage.discard`/
/// `.demote`/`.is_helper`.
///
/// Version 4 (roadmap R25) added `RootConstantSpace`/`RootConstantRegister`:
/// root-constant support now recognizes any single register binding rather
/// than only the default `(b0, space0)`, so a host needs to be told which
/// one a given `RootConstantSize` corresponds to.
///
/// Version 5 (roadmap R30's SPIR-V image/sampler completion) added
/// `ReservedImageHeapSize`/`ReservedSamplerHeapSize` and gave every
/// `BoundResourceRange` a `BoundResourceClass`: a bound sampled image or
/// sampler is assigned a slot in the image or sampler heap, not the
/// buffer-oriented resource heap, so a host cannot place either from the
/// version 4 layout's fields alone.
constexpr uint32_t ArtifactAbiVersion = 5;

/// Bits of `StageArtifactInfo::Flags`, mirrored in the serialized byte
/// layout.
enum ArtifactFlagBits : uint32_t {
  FEME_CPU_ARTIFACT_USES_SAMPLER_HEAP = 1u << 0,
  /// Set if the entry point calls `feme.stage.discard` anywhere in its
  /// body (see `computeSideEffectFlags`).
  FEME_CPU_ARTIFACT_USES_DISCARD = 1u << 1,
  /// Set if the entry point calls `feme.stage.demote` anywhere in its
  /// body.
  FEME_CPU_ARTIFACT_USES_DEMOTE = 1u << 2,
  /// Set if the entry point calls `feme.stage.is_helper` anywhere in its
  /// body.
  FEME_CPU_ARTIFACT_USES_HELPER = 1u << 3,
};

/// The versioned, object-file-friendly artifact `emitArtifactGlobal` writes
/// and `parseArtifact` reads back (see the file comment above and "Heap
/// usage discovery" in feme/docs/FeMeCPUDesign.md). Every field here is
/// part of the CPU target's AOT ABI, in the same sense
/// feme/include/feme/Target/CPU/RuntimeABI.h's structs are: layout and field
/// order must stay in sync with `serializeArtifact`/`parseArtifact`.
///
/// `WaveSize`, `GroupSize`, `GroupSharedSize` and `GroupSharedAlign` were
/// part of the versioned layout from the start, but left populated with 0
/// until roadmap milestone R22: `feme::cpu::CompiledStage::getArtifactInfo`
/// (the JIT-adjacent path) and `feme::Driver`'s CPU-target retargeting path
/// (the AOT path, ResourceInfo.cpp -> `emitArtifactGlobal`) now both supply
/// them from the same already-resolved wave size/thread-group size and
/// `feme::cpu::getGroupSharedRequirements` (feme/include/feme/Transforms/
/// CPU/GroupSharedInfo.h), so a host sees identical reflection regardless
/// of which path produced the compiled code.
///
/// `Stage` and `Signature` generalize this compute-only structure into a
/// stage-tagged one (roadmap R22); stage-aware `CompiledStage`s populate both,
/// while the compute-only compatibility paths leave the defaults (`Compute` /
/// empty) unless a caller sets them explicitly.
struct StageArtifactInfo {
  ShaderStage Stage = ShaderStage::Compute;
  uint32_t WaveSize = 0;
  uint32_t GroupSize[3] = {0, 0, 0};
  uint32_t GroupSharedSize = 0;
  uint32_t GroupSharedAlign = 0;
  uint32_t RootConstantSize = 0;
  /// See `ResourceInfo::RootConstantSpace`/`RootConstantRegister`.
  uint32_t RootConstantSpace = 0;
  uint32_t RootConstantRegister = 0;
  uint32_t Flags = 0;
  std::vector<uint32_t> StaticHeapIndices;
  /// See `ResourceInfo::ReservedResourceHeapSize`/`ReservedImageHeapSize`/
  /// `ReservedSamplerHeapSize`/`BoundRanges`.
  uint32_t ReservedResourceHeapSize = 0;
  uint32_t ReservedImageHeapSize = 0;
  uint32_t ReservedSamplerHeapSize = 0;
  std::vector<BoundResourceRange> BoundRanges;
  /// The entry point's serialized `feme::EntrySignature`
  /// (`feme::serializeSignature`), or empty if none is attached.
  std::vector<uint8_t> Signature;

  /// Builds the execution-shape-agnostic fields of a `StageArtifactInfo`
  /// from \p Info, leaving `Stage` at its default (`Compute`), `Signature`
  /// empty, and `WaveSize`/`GroupSize`/`GroupSharedSize`/`GroupSharedAlign`
  /// at their default (0) -- a caller that also knows the resolved
  /// execution shape (`feme::cpu::CompiledStage::getArtifactInfo`,
  /// `feme::Driver`'s CPU retargeting path) sets those itself afterward.
  static StageArtifactInfo fromResourceInfo(const ResourceInfo &Info);
};

/// The AOT artifact symbol name for an entry point named \p EntryName (see
/// "Heap usage discovery"): `feme_cpu_info_<entry>`.
std::string getArtifactSymbolName(llvm::StringRef EntryName);

/// Serializes \p Info to the byte layout `parseArtifact` reads back: a
/// little-endian `ArtifactAbiVersion`, then \p Info's fields in declaration
/// order, then \p Info.StaticHeapIndices's/`BoundRanges`'s/`Signature`'s
/// counts followed by each tail's own contents (see "Heap usage
/// discovery").
std::vector<uint8_t> serializeArtifact(const StageArtifactInfo &Info);

/// Parses \p Bytes as a serialized `StageArtifactInfo`, or an `Error` if it
/// is too short, has a heap-index/bound-range/signature-length count
/// inconsistent with its length, declares an ABI version other than
/// `ArtifactAbiVersion`, or names a `feme::ShaderStage` this build does not
/// know.
llvm::Expected<StageArtifactInfo> parseArtifact(llvm::ArrayRef<uint8_t> Bytes);

/// Emits \p Info as a read-only data global named
/// `getArtifactSymbolName(EntryName)` in \p M, containing
/// `serializeArtifact(Info)`'s bytes. This is what survives compilation to
/// an object file for an AOT host to read back with `parseArtifact`; see
/// the file comment above.
llvm::GlobalVariable *emitArtifactGlobal(llvm::Module &M,
                                         llvm::StringRef EntryName,
                                         const StageArtifactInfo &Info);

/// Reads back the artifact global `emitArtifactGlobal` wrote for
/// \p EntryName in \p M, or `std::nullopt` if no such global exists. This is
/// the JIT-adjacent, still-in-IR analogue of an AOT host reading the symbol
/// out of an object file -- see the file comment above for why both exist.
std::optional<StageArtifactInfo> readArtifactGlobal(const llvm::Module &M,
                                                    llvm::StringRef EntryName);

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_RESOURCEINFO_H
