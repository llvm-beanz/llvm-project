//===- PipelineCache.h - VkPipelineCache object model -----------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// V4 ("Add persistent pipeline cache object-code serialization, with
// header, UUID, and digest validation and a fuzzer over the blob parser",
// see "Pipeline Cache" in feme/docs/FeMeVulkanDesign.md): `VkPipelineCache`.
//
// A `VkPipelineCache` stores, in process, the strong hash key (see
// `computePipelineCacheKey`) of every `VkComputePipelineCreateInfo` this
// process has successfully compiled through it, and a shared handle to that
// compiled artifact so a repeated identical creation -- the same SPIR-V
// bytes/entry point, specialization data, and pipeline-layout binding map/
// push-constant ranges, against the same device -- reuses it instead of
// recompiling (`lookup`/`insert`).
//
// `vkGetPipelineCacheData`'s blob is the specification-mandated
// `VkPipelineCacheHeaderVersionOne` header, this ICD's own recorded key set,
// and a SHA-256 digest over both -- *not* relocatable object code: per the
// design doc, that depends on a FeMe API this milestone does not yet have
// ("emits relocatable object code plus complete ArtifactInfo"), so
// `vkCreatePipelineCache`'s initial data can only teach a fresh
// `VkPipelineCache` which keys were known-good in some earlier process, not
// skip recompiling them -- `serializePipelineCacheBlob`/
// `parsePipelineCacheBlob`'s own comments detail why this is still useful
// (it round-trips correctly and is exercised by the fuzzer) despite that.
//
// The blob handed to `vkCreatePipelineCache` is fully attacker-controlled
// (see "Pipeline Cache"'s security list): `parsePipelineCacheBlob` validates
// the header's length/version/vendor/device/UUID fields, the digest, and
// every internal count with checked arithmetic before trusting a single
// key, and treats any failure as an empty cache rather than an error.
// `FEME_VULKAN_TRUST_PIPELINE_CACHE_DATA=OFF` (a build-time option, see
// feme/CMakeLists.txt) removes `vkCreatePipelineCache`'s initial data from
// the trust boundary entirely -- every cache then starts empty regardless
// of what a caller supplies, for embedders who do not want to parse
// arbitrary bytes from disk at all.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_PIPELINECACHE_H
#define FEME_LIB_VULKAN_PIPELINECACHE_H

#include "llvm/ADT/ArrayRef.h"

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace feme::vulkan {

struct CachedPipelineArtifact;
struct GraphicsPipelineArtifact;
class DescriptorSetLayout;
struct SpecializationOverride;

/// A cache key: a SHA-256 digest over every input "Pipeline Cache" lists
/// (see `computePipelineCacheKey`).
using PipelineCacheKey = std::array<uint8_t, 32>;

/// Computes the strong key for one compute pipeline creation, covering
/// exactly the inputs "Pipeline Cache" requires: the shader module's SPIR-V
/// words and entry point, specialization data, the pipeline layout's
/// binding map and push-constant ranges, and \p DeviceUUID (the device's
/// `pipelineCacheUUID`, which already folds in FeMe/LLVM versions, the CPU
/// target triple, CPU feature policy, and wave size -- see
/// `PhysicalDeviceInfo.cpp`'s `fillUUID` -- so this key does not need to
/// duplicate any of that). \p RequiredSubgroupSize (roadmap E7, 0 if no
/// `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` was chained) and
/// \p StageCreateFlags (whose only bit `compileComputePipeline` currently
/// consults is `VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT`)
/// both change what `Pipeline.cpp` compiles this shader into, so two
/// otherwise-identical creations that disagree in either must not collide
/// on the same cached artifact.
PipelineCacheKey
computePipelineCacheKey(const uint8_t (&DeviceUUID)[VK_UUID_SIZE],
                        llvm::ArrayRef<uint32_t> ShaderWords,
                        llvm::StringRef EntryPoint,
                        llvm::ArrayRef<SpecializationOverride> Overrides,
                        llvm::ArrayRef<const DescriptorSetLayout *> SetLayouts,
                        llvm::ArrayRef<VkPushConstantRange> PushConstantRanges,
                        uint32_t RequiredSubgroupSize,
                        VkPipelineShaderStageCreateFlags StageCreateFlags);

/// Computes the strong key for one graphics pipeline creation: the two
/// stages' SPIR-V words and entry points (a graphics stage has no
/// specialization data to fold in -- it is rejected outright at creation,
/// see GraphicsPipeline.cpp's `compileGraphicsStage`), the pipeline
/// layout's binding map and push-constant ranges, \p DeviceUUID (as
/// `computePipelineCacheKey` above), and \p FixedFunctionState -- a
/// caller-serialized encoding of every piece of translated fixed-function
/// pipeline state (topology, vertex input, raster/viewport/depth-stencil/
/// blend state, dynamic-state selection, sample count, and attachment
/// formats): a hit must be identical in everything a draw through either
/// pipeline could observe, not only in the two stages' bytes.
PipelineCacheKey computeGraphicsPipelineCacheKey(
    const uint8_t (&DeviceUUID)[VK_UUID_SIZE],
    llvm::ArrayRef<uint32_t> VertexShaderWords, llvm::StringRef VertexEntry,
    llvm::ArrayRef<uint32_t> FragmentShaderWords, llvm::StringRef FragmentEntry,
    llvm::ArrayRef<const DescriptorSetLayout *> SetLayouts,
    llvm::ArrayRef<VkPushConstantRange> PushConstantRanges,
    llvm::ArrayRef<uint8_t> FixedFunctionState);

/// Whether persistent (serialized) pipeline-cache data is ever trusted as
/// `vkCreatePipelineCache` input, per `FEME_VULKAN_TRUST_PIPELINE_CACHE_DATA`
/// (see the file comment).
bool pipelineCacheDataIsTrusted();

/// Validates \p Data as a `vkCreatePipelineCache`-style blob against
/// \p ExpectedUUID (the device's `pipelineCacheUUID`), returning the key set
/// it records if every check (header, UUID, digest, bounds) passes, or
/// `std::nullopt` -- "an empty cache", never an error -- otherwise. See the
/// file comment's security list.
std::optional<std::vector<PipelineCacheKey>>
parsePipelineCacheBlob(llvm::ArrayRef<uint8_t> Data,
                       const uint8_t (&ExpectedUUID)[VK_UUID_SIZE],
                       uint32_t VendorID, uint32_t DeviceID);

/// Serializes \p Keys into the blob `parsePipelineCacheBlob` reads back.
std::vector<uint8_t>
serializePipelineCacheBlob(llvm::ArrayRef<PipelineCacheKey> Keys,
                           const uint8_t (&UUID)[VK_UUID_SIZE],
                           uint32_t VendorID, uint32_t DeviceID);

/// A `VkPipelineCache`: an in-process key -> compiled-artifact table (see
/// the file comment). Not dispatchable.
///
/// (roadmap E9) `VK_EXT_pipeline_creation_cache_control`'s
/// `VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT` relaxes the
/// default Vulkan rule that an implementation must itself tolerate
/// concurrent host access to the same `VkPipelineCache` from multiple
/// threads (`pipelineCache` is *not* one of `vkCreateGraphicsPipelines`/
/// `vkCreateComputePipelines`'s externally-synchronized parameters, unlike
/// `vkMergePipelineCaches`'s `dstCache`/`pSrcCaches` and
/// `vkGetPipelineCacheData`'s `pipelineCache`, which always are). `lookup`/
/// `insert`/`lookupGraphics`/`insertGraphics` -- the four accessors
/// `vkCreateComputePipelines`/`vkCreateGraphicsPipelines` calls -- therefore
/// take \p Mutex unless \p ExternallySynchronized was set at construction,
/// in which case the caller has promised there is no concurrent access to
/// synchronize against, and the lock is skipped; `merge`/`keys` never lock,
/// since their callers are always externally synchronized regardless of
/// this flag (see their own comments in PipelineCache.cpp).
class PipelineCache {
public:
  explicit PipelineCache(std::vector<PipelineCacheKey> InitialKeys = {},
                         bool ExternallySynchronized = false);

  /// The compiled artifact previously `insert`ed for \p Key, or null on a
  /// cache miss.
  std::shared_ptr<CachedPipelineArtifact>
  lookup(const PipelineCacheKey &Key) const;

  /// Records that \p Key compiled to \p Artifact, for a later `lookup` to
  /// find (including from a different `VkPipeline` creation in the same
  /// cache, or after a `merge`).
  void insert(const PipelineCacheKey &Key,
              std::shared_ptr<CachedPipelineArtifact> Artifact);

  /// The compiled graphics artifact previously `insertGraphics`ed for
  /// \p Key, or null on a cache miss. A separate table from `lookup`'s:
  /// a compute and a graphics pipeline creation never share an artifact
  /// type, even if (improbably) their keys collided.
  std::shared_ptr<GraphicsPipelineArtifact>
  lookupGraphics(const PipelineCacheKey &Key) const;

  /// Records that \p Key compiled to \p Artifact, for a later
  /// `lookupGraphics` to find.
  void insertGraphics(const PipelineCacheKey &Key,
                      std::shared_ptr<GraphicsPipelineArtifact> Artifact);

  /// `vkMergePipelineCaches`: adopts every key/artifact \p Other knows that
  /// this cache does not already have.
  void merge(const PipelineCache &Other);

  /// Every key this cache knows (compute and graphics both), for
  /// `vkGetPipelineCacheData` to serialize. Order is unspecified but stable
  /// across calls absent further `insert`/`insertGraphics` calls.
  std::vector<PipelineCacheKey> keys() const;

private:
  /// Guards `Entries`/`GraphicsEntries` below when `!ExternallySynchronized`
  /// (see the class comment); `mutable` since even `lookup`/`lookupGraphics`
  /// (logically `const`) must take it.
  mutable std::mutex Mutex;
  const bool ExternallySynchronized;
  std::map<PipelineCacheKey, std::shared_ptr<CachedPipelineArtifact>> Entries;
  std::map<PipelineCacheKey, std::shared_ptr<GraphicsPipelineArtifact>>
      GraphicsEntries;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_PIPELINECACHE_H
