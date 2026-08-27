//===- PipelineCache.cpp - VkPipelineCache implementation ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PipelineCache.h"
#include "Descriptor.h"
#include "GroupSize.h"

#include "llvm/Support/SHA256.h"

#include <cstring>

using namespace feme::vulkan;
using namespace llvm;

namespace {

/// `VkPipelineCacheHeaderVersionOne`'s on-the-wire size: four `uint32_t`
/// fields plus the 16-byte UUID. Not `sizeof(VkPipelineCacheHeaderVersionOne)`
/// directly: that type may carry compiler-inserted padding this format must
/// not depend on.
constexpr uint32_t kHeaderSize = 4 * sizeof(uint32_t) + VK_UUID_SIZE;
/// The digest this format appends after the header and key list -- see the
/// header comment.
constexpr uint32_t kDigestSize = 32;

/// Appends \p V to \p Out in native-endian order -- the same convention
/// every other FeMe binary format (e.g. `feme::dxsa::BinaryWriter`) that
/// need not be portable across endianness uses for an in-process,
/// same-machine-only artifact.
template <typename T> void appendPOD(std::vector<uint8_t> &Out, const T &V) {
  const auto *Bytes = reinterpret_cast<const uint8_t *>(&V);
  Out.insert(Out.end(), Bytes, Bytes + sizeof(T));
}

/// Hashes \p SetLayouts' binding maps and \p PushConstantRanges into
/// \p Hash, the part of a pipeline's identity every stage's key (compute or
/// graphics) shares: the pipeline layout's own binding/push-constant shape.
void hashSetLayoutsAndPushConstants(
    SHA256 &Hash, ArrayRef<const DescriptorSetLayout *> SetLayouts,
    ArrayRef<VkPushConstantRange> PushConstantRanges) {
  for (const DescriptorSetLayout *Layout : SetLayouts) {
    for (const DescriptorSetLayoutBinding &Binding : Layout->bindings()) {
      Hash.update(ArrayRef(reinterpret_cast<const uint8_t *>(&Binding.Binding),
                           sizeof(Binding.Binding)));
      Hash.update(ArrayRef(reinterpret_cast<const uint8_t *>(&Binding.Type),
                           sizeof(Binding.Type)));
      Hash.update(ArrayRef(reinterpret_cast<const uint8_t *>(&Binding.Count),
                           sizeof(Binding.Count)));
    }
  }
  for (const VkPushConstantRange &Range : PushConstantRanges) {
    Hash.update(ArrayRef(reinterpret_cast<const uint8_t *>(&Range.stageFlags),
                         sizeof(Range.stageFlags)));
    Hash.update(ArrayRef(reinterpret_cast<const uint8_t *>(&Range.offset),
                         sizeof(Range.offset)));
    Hash.update(ArrayRef(reinterpret_cast<const uint8_t *>(&Range.size),
                         sizeof(Range.size)));
  }
}

} // namespace

PipelineCacheKey feme::vulkan::computePipelineCacheKey(
    const uint8_t (&DeviceUUID)[VK_UUID_SIZE], ArrayRef<uint32_t> ShaderWords,
    StringRef EntryPoint, ArrayRef<SpecializationOverride> Overrides,
    ArrayRef<const DescriptorSetLayout *> SetLayouts,
    ArrayRef<VkPushConstantRange> PushConstantRanges,
    uint32_t RequiredSubgroupSize,
    VkPipelineShaderStageCreateFlags StageCreateFlags) {
  SHA256 Hash;
  Hash.update(ArrayRef(DeviceUUID, VK_UUID_SIZE));
  Hash.update(ArrayRef(reinterpret_cast<const uint8_t *>(ShaderWords.data()),
                       ShaderWords.size() * sizeof(uint32_t)));
  Hash.update(EntryPoint);
  for (const SpecializationOverride &Override : Overrides) {
    Hash.update(
        ArrayRef(reinterpret_cast<const uint8_t *>(&Override.ConstantID),
                 sizeof(Override.ConstantID)));
    Hash.update(ArrayRef(reinterpret_cast<const uint8_t *>(&Override.Value),
                         sizeof(Override.Value)));
  }
  hashSetLayoutsAndPushConstants(Hash, SetLayouts, PushConstantRanges);
  // (roadmap E7) `requiredSubgroupSize`/`VK_PIPELINE_SHADER_STAGE_CREATE_
  // REQUIRE_FULL_SUBGROUPS_BIT` both change what `compileComputePipeline`
  // compiles, so they must fold into the key like every other input above.
  Hash.update(ArrayRef(reinterpret_cast<const uint8_t *>(&RequiredSubgroupSize),
                       sizeof(RequiredSubgroupSize)));
  Hash.update(ArrayRef(reinterpret_cast<const uint8_t *>(&StageCreateFlags),
                       sizeof(StageCreateFlags)));
  return Hash.final();
}

PipelineCacheKey feme::vulkan::computeGraphicsPipelineCacheKey(
    const uint8_t (&DeviceUUID)[VK_UUID_SIZE],
    ArrayRef<uint32_t> VertexShaderWords, StringRef VertexEntry,
    ArrayRef<uint32_t> FragmentShaderWords, StringRef FragmentEntry,
    ArrayRef<const DescriptorSetLayout *> SetLayouts,
    ArrayRef<VkPushConstantRange> PushConstantRanges,
    ArrayRef<uint8_t> FixedFunctionState,
    ArrayRef<uint32_t> TessControlShaderWords, StringRef TessControlEntry,
    ArrayRef<uint32_t> TessEvalShaderWords, StringRef TessEvalEntry) {
  SHA256 Hash;
  Hash.update(ArrayRef(DeviceUUID, VK_UUID_SIZE));
  Hash.update(
      ArrayRef(reinterpret_cast<const uint8_t *>(VertexShaderWords.data()),
               VertexShaderWords.size() * sizeof(uint32_t)));
  Hash.update(VertexEntry);
  Hash.update(
      ArrayRef(reinterpret_cast<const uint8_t *>(FragmentShaderWords.data()),
               FragmentShaderWords.size() * sizeof(uint32_t)));
  Hash.update(FragmentEntry);
  // (roadmap H4b) Empty for a pipeline with no tessellation stages, exactly
  // like `FragmentShaderWords`/`FragmentEntry` are for a fragment-less one.
  Hash.update(ArrayRef(
      reinterpret_cast<const uint8_t *>(TessControlShaderWords.data()),
      TessControlShaderWords.size() * sizeof(uint32_t)));
  Hash.update(TessControlEntry);
  Hash.update(
      ArrayRef(reinterpret_cast<const uint8_t *>(TessEvalShaderWords.data()),
               TessEvalShaderWords.size() * sizeof(uint32_t)));
  Hash.update(TessEvalEntry);
  hashSetLayoutsAndPushConstants(Hash, SetLayouts, PushConstantRanges);
  Hash.update(FixedFunctionState);
  return Hash.final();
}

bool feme::vulkan::pipelineCacheDataIsTrusted() {
#ifdef FEME_VULKAN_TRUST_PIPELINE_CACHE_DATA
  return FEME_VULKAN_TRUST_PIPELINE_CACHE_DATA;
#else
  return true;
#endif
}

std::vector<uint8_t>
feme::vulkan::serializePipelineCacheBlob(ArrayRef<PipelineCacheKey> Keys,
                                         const uint8_t (&UUID)[VK_UUID_SIZE],
                                         uint32_t VendorID, uint32_t DeviceID) {
  std::vector<uint8_t> Out;
  appendPOD(Out, kHeaderSize);
  appendPOD(Out, static_cast<uint32_t>(VK_PIPELINE_CACHE_HEADER_VERSION_ONE));
  appendPOD(Out, VendorID);
  appendPOD(Out, DeviceID);
  Out.insert(Out.end(), UUID, UUID + VK_UUID_SIZE);

  appendPOD(Out, static_cast<uint64_t>(Keys.size()));
  for (const PipelineCacheKey &Key : Keys)
    Out.insert(Out.end(), Key.begin(), Key.end());

  std::array<uint8_t, kDigestSize> Digest = SHA256::hash(Out);
  Out.insert(Out.end(), Digest.begin(), Digest.end());
  return Out;
}

std::optional<std::vector<PipelineCacheKey>>
feme::vulkan::parsePipelineCacheBlob(
    ArrayRef<uint8_t> Data, const uint8_t (&ExpectedUUID)[VK_UUID_SIZE],
    uint32_t VendorID, uint32_t DeviceID) {
  // Every read below is bounds-checked before it happens: "Bounds-check
  // every internal offset and count with checked arithmetic, and treat any
  // inconsistency as a cache miss" (see the file comment).
  if (Data.size() < kHeaderSize)
    return std::nullopt;

  uint32_t HeaderSize, HeaderVersion, VendorIDField, DeviceIDField;
  std::memcpy(&HeaderSize, Data.data(), sizeof(HeaderSize));
  std::memcpy(&HeaderVersion, Data.data() + 4, sizeof(HeaderVersion));
  std::memcpy(&VendorIDField, Data.data() + 8, sizeof(VendorIDField));
  std::memcpy(&DeviceIDField, Data.data() + 12, sizeof(DeviceIDField));
  const uint8_t *UUIDField = Data.data() + 16;

  if (HeaderSize != kHeaderSize)
    return std::nullopt;
  if (HeaderVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
    return std::nullopt;
  if (VendorIDField != VendorID || DeviceIDField != DeviceID)
    return std::nullopt;
  if (std::memcmp(UUIDField, ExpectedUUID, VK_UUID_SIZE) != 0)
    return std::nullopt;

  // The digest covers everything before it; it must be the last
  // `kDigestSize` bytes for the count field below to be at a fixed offset.
  if (Data.size() < kHeaderSize + sizeof(uint64_t) + kDigestSize)
    return std::nullopt;
  size_t PayloadSize = Data.size() - kDigestSize;
  ArrayRef<uint8_t> Payload = Data.take_front(PayloadSize);
  ArrayRef<uint8_t> DigestField = Data.drop_front(PayloadSize);
  std::array<uint8_t, kDigestSize> Computed = SHA256::hash(Payload);
  if (!std::equal(Computed.begin(), Computed.end(), DigestField.begin()))
    return std::nullopt;

  uint64_t KeyCount;
  std::memcpy(&KeyCount, Data.data() + kHeaderSize, sizeof(KeyCount));
  // Checked arithmetic: reject rather than overflow/truncate a hostile
  // count when computing the expected total size.
  uint64_t KeyBytes;
  if (__builtin_mul_overflow(KeyCount, sizeof(PipelineCacheKey), &KeyBytes))
    return std::nullopt;
  uint64_t ExpectedSize;
  if (__builtin_add_overflow(uint64_t(kHeaderSize) + sizeof(uint64_t) +
                                 kDigestSize,
                             KeyBytes, &ExpectedSize) ||
      ExpectedSize != Data.size())
    return std::nullopt;

  std::vector<PipelineCacheKey> Keys;
  Keys.reserve(KeyCount);
  const uint8_t *Cursor = Data.data() + kHeaderSize + sizeof(uint64_t);
  for (uint64_t I = 0; I != KeyCount; ++I) {
    PipelineCacheKey Key;
    std::memcpy(Key.data(), Cursor, Key.size());
    Keys.push_back(Key);
    Cursor += Key.size();
  }
  return Keys;
}

namespace {
/// RAII helper that locks \p M unless \p Skip (the cache was constructed
/// `ExternallySynchronized`, see PipelineCache.h's class comment) -- an
/// `std::unique_lock` constructed with `std::defer_lock` and conditionally
/// `lock()`ed, so the mutex is never touched at all in the skip case rather
/// than merely uncontended.
class ConditionalLock {
public:
  ConditionalLock(std::mutex &M, bool Skip)
      : Lock(M, std::defer_lock) {
    if (!Skip)
      Lock.lock();
  }

private:
  std::unique_lock<std::mutex> Lock;
};
} // namespace

PipelineCache::PipelineCache(std::vector<PipelineCacheKey> InitialKeys,
                             bool ExternallySynchronized)
    : ExternallySynchronized(ExternallySynchronized) {
  // Every initial key is recorded as a placeholder (null artifact) in both
  // tables: a persisted blob does not record whether a key was originally
  // compute's or graphics', and a placeholder never satisfies a lookup (see
  // the file comment's "does not skip recompiling") in either table, so
  // recording it in both cannot manufacture a false hit.
  for (const PipelineCacheKey &Key : InitialKeys) {
    Entries.emplace(Key, nullptr);
    GraphicsEntries.emplace(Key, nullptr);
  }
}

std::shared_ptr<CachedPipelineArtifact>
PipelineCache::lookup(const PipelineCacheKey &Key) const {
  ConditionalLock L(Mutex, ExternallySynchronized);
  auto It = Entries.find(Key);
  return It == Entries.end() ? nullptr : It->second;
}

void PipelineCache::insert(const PipelineCacheKey &Key,
                           std::shared_ptr<CachedPipelineArtifact> Artifact) {
  ConditionalLock L(Mutex, ExternallySynchronized);
  Entries[Key] = std::move(Artifact);
}

std::shared_ptr<GraphicsPipelineArtifact>
PipelineCache::lookupGraphics(const PipelineCacheKey &Key) const {
  ConditionalLock L(Mutex, ExternallySynchronized);
  auto It = GraphicsEntries.find(Key);
  return It == GraphicsEntries.end() ? nullptr : It->second;
}

void PipelineCache::insertGraphics(
    const PipelineCacheKey &Key,
    std::shared_ptr<GraphicsPipelineArtifact> Artifact) {
  ConditionalLock L(Mutex, ExternallySynchronized);
  GraphicsEntries[Key] = std::move(Artifact);
}

void PipelineCache::merge(const PipelineCache &Other) {
  // Unlike `lookup`/`insert` above, `vkMergePipelineCaches`'s `dstCache`
  // (`this`) and `pSrcCaches` (`Other`) are *always* host-synchronized
  // parameters per the base Vulkan spec, with or without
  // `VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT` -- unlike
  // `vkCreate{Graphics,Compute}Pipelines`'s `pipelineCache` parameter, this
  // extension does not relax `vkMergePipelineCaches`'s own synchronization
  // requirement, so no lock is needed (or taken) here.
  for (const auto &[Key, Artifact] : Other.Entries)
    Entries.try_emplace(Key, Artifact);
  for (const auto &[Key, Artifact] : Other.GraphicsEntries)
    GraphicsEntries.try_emplace(Key, Artifact);
}

std::vector<PipelineCacheKey> PipelineCache::keys() const {
  // `vkGetPipelineCacheData`'s `pipelineCache` parameter is likewise always
  // host-synchronized -- see `merge`'s comment above.
  std::vector<PipelineCacheKey> Result;
  Result.reserve(Entries.size() + GraphicsEntries.size());
  for (const auto &[Key, Artifact] : Entries)
    Result.push_back(Key);
  for (const auto &[Key, Artifact] : GraphicsEntries)
    if (!Entries.count(Key))
      Result.push_back(Key);
  return Result;
}
