//===- Pipeline.cpp - Shader module / pipeline layout / pipeline ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Pipeline.h"
#include "Descriptor.h"
#include "GroupSize.h"
#include "Icd.h"
#include "Objects.h"
#include "PipelineCache.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Import/SPIRV/SPIRVImporter.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/JITEngine.h"
#include "feme/Target/CPU/ResourceInfo.h"
#include "feme/Translate/SPIRV/SPIRVToLLVMTranslator.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TargetParser/Triple.h"

#include <cstring>

using namespace feme::vulkan;
using namespace llvm;

namespace {

/// Clears the module-level attributes SPIR-V import/translation leaves
/// that have no meaning to `feme::cpu`'s JIT -- the same normalization
/// `feme-run`'s `clearHostAgnosticMetadata` applies (see that tool's own
/// comment): the FeMe CPU target compiles against the host, not whatever
/// SPIR-V's own addressing-model-derived triple happened to be.
void clearHostAgnosticMetadata(llvm::Module &M) {
  M.setTargetTriple(llvm::Triple());
  M.setDataLayout(llvm::DataLayout());
  if (NamedMDNode *ModuleFlags = M.getNamedMetadata("llvm.module.flags"))
    M.eraseNamedMetadata(ModuleFlags);
}

/// Builds the `GroupSize.h` override list from \p Info
/// (`VkSpecializationInfo`), validating every map entry's `(offset, size)`
/// against the supplied data blob before reading it (see "Error Handling
/// and Security": malformed/hostile sizes must fail cleanly, not read out
/// of bounds). Only 4-byte entries are meaningful group-size overrides
/// (`LocalSizeId`/`BuiltIn WorkgroupSize` constants are always 32-bit
/// integers); anything else is recorded as an unmatched/zero override,
/// which is harmless since `resolveComputeGroupSize` only ever consults an
/// override for a `SpecId` it actually depends on.
Expected<SmallVector<SpecializationOverride, 4>>
buildSpecializationOverrides(const VkSpecializationInfo *Info) {
  SmallVector<SpecializationOverride, 4> Overrides;
  if (!Info)
    return Overrides;
  const auto *Data = static_cast<const uint8_t *>(Info->pData);
  for (uint32_t I = 0; I != Info->mapEntryCount; ++I) {
    const VkSpecializationMapEntry &Entry = Info->pMapEntries[I];
    if (Entry.offset > Info->dataSize ||
        Entry.size > Info->dataSize - Entry.offset)
      return createStringError(inconvertibleErrorCode(),
                               "VkSpecializationMapEntry %u is out of bounds "
                               "of its VkSpecializationInfo::dataSize",
                               I);
    uint32_t Value = 0;
    if (Entry.size >= sizeof(uint32_t))
      std::memcpy(&Value, Data + Entry.offset, sizeof(uint32_t));
    Overrides.push_back(SpecializationOverride{Entry.constantID, Value});
  }
  return Overrides;
}

} // namespace

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice, const VkShaderModuleCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule) {
  if (pCreateInfo->codeSize == 0 ||
      pCreateInfo->codeSize % sizeof(uint32_t) != 0)
    return VK_ERROR_INITIALIZATION_FAILED;

  size_t WordCount = pCreateInfo->codeSize / sizeof(uint32_t);
  std::vector<uint32_t> Words(WordCount);
  std::memcpy(Words.data(), pCreateInfo->pCode, pCreateInfo->codeSize);

  Allocator Alloc(pAllocator);
  vulkan::ShaderModule *Obj = Alloc.create<vulkan::ShaderModule>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::move(Words));
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pShaderModule = toHandle<VkShaderModule>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyShaderModule(VkDevice, VkShaderModule shaderModule,
                      const VkAllocationCallbacks *pAllocator) {
  if (!shaderModule)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<vulkan::ShaderModule>(shaderModule));
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreatePipelineLayout(VkDevice, const VkPipelineLayoutCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkPipelineLayout *pPipelineLayout) {
  std::vector<const DescriptorSetLayout *> SetLayouts;
  SetLayouts.reserve(pCreateInfo->setLayoutCount);
  for (uint32_t I = 0; I != pCreateInfo->setLayoutCount; ++I)
    SetLayouts.push_back(
        fromHandle<DescriptorSetLayout>(pCreateInfo->pSetLayouts[I]));

  std::vector<VkPushConstantRange> PushConstantRanges(
      pCreateInfo->pPushConstantRanges,
      pCreateInfo->pPushConstantRanges + pCreateInfo->pushConstantRangeCount);

  Allocator Alloc(pAllocator);
  PipelineLayout *Obj = Alloc.create<PipelineLayout>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::move(SetLayouts),
      std::move(PushConstantRanges));
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pPipelineLayout = toHandle<VkPipelineLayout>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineLayout(VkDevice, VkPipelineLayout pipelineLayout,
                        const VkAllocationCallbacks *pAllocator) {
  if (!pipelineLayout)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<PipelineLayout>(pipelineLayout));
}

namespace {

/// Whether \p Layout's compute-visible push-constant ranges fully cover
/// `[0, RootConstantSize)` with no gap -- see "Descriptor Model": "reject a
/// shader whose accessed range is not fully covered by a range declared in
/// the layout with the compute stage bit set". `RootConstantSize` is
/// always a shader's *full* advertised root-constant span (roadmap R25 for
/// DXIL; `feme::cpu::SPIRVPushConstantLoweringPass` for SPIR-V, both
/// starting at byte 0), so coverage is checked byte-by-byte rather than
/// merely comparing against a single range's own offset/size -- multiple
/// declared ranges (e.g. one per shader stage in a shared layout) may
/// jointly cover it with gaps only a full walk catches.
bool pushConstantsCoverRootConstantSize(const PipelineLayout &Layout,
                                        uint32_t RootConstantSize,
                                        uint32_t MaxPushConstantsSize) {
  if (RootConstantSize == 0)
    return true;
  if (RootConstantSize > MaxPushConstantsSize)
    return false;
  std::vector<bool> Covered(RootConstantSize, false);
  for (const VkPushConstantRange &Range : Layout.pushConstantRanges()) {
    if ((Range.stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) == 0)
      continue;
    uint32_t Begin = std::min(Range.offset, RootConstantSize);
    uint32_t End = std::min(Range.offset + Range.size, RootConstantSize);
    for (uint32_t I = Begin; I != End; ++I)
      Covered[I] = true;
  }
  return llvm::all_of(Covered, [](bool B) { return B; });
}

/// Compiles one `VkComputePipelineCreateInfo` end to end: imports its
/// shader module's SPIR-V, translates it to LLVM IR, resolves and stamps
/// its group size (see GroupSize.h), and compiles it with the FeMe CPU
/// pipeline. See "Compilation flow" in feme/docs/FeMeVulkanDesign.md.
Expected<std::shared_ptr<CachedPipelineArtifact>>
compileComputePipeline(const VkComputePipelineCreateInfo &CreateInfo,
                       const PhysicalDeviceInfo &DeviceInfo) {
  if (CreateInfo.stage.stage != VK_SHADER_STAGE_COMPUTE_BIT)
    return createStringError(inconvertibleErrorCode(),
                             "compute pipeline stage must be "
                             "VK_SHADER_STAGE_COMPUTE_BIT");
  if (!CreateInfo.layout)
    return createStringError(inconvertibleErrorCode(),
                             "compute pipeline requires a VkPipelineLayout");

  auto *Module = fromHandle<vulkan::ShaderModule>(CreateInfo.stage.module);
  std::string EntryPoint =
      CreateInfo.stage.pName ? CreateInfo.stage.pName : "main";

  Expected<SmallVector<SpecializationOverride, 4>> Overrides =
      buildSpecializationOverrides(CreateInfo.stage.pSpecializationInfo);
  if (!Overrides)
    return Overrides.takeError();

  Expected<std::array<uint32_t, 3>> GroupSize =
      resolveComputeGroupSize(Module->words(), EntryPoint, *Overrides);
  if (!GroupSize)
    return GroupSize.takeError();

  const VkPhysicalDeviceLimits &Limits = DeviceInfo.Properties.limits;
  uint64_t Invocations =
      uint64_t(GroupSize->at(0)) * GroupSize->at(1) * GroupSize->at(2);
  if ((*GroupSize)[0] > Limits.maxComputeWorkGroupSize[0] ||
      (*GroupSize)[1] > Limits.maxComputeWorkGroupSize[1] ||
      (*GroupSize)[2] > Limits.maxComputeWorkGroupSize[2] ||
      Invocations > Limits.maxComputeWorkGroupInvocations)
    return createStringError(inconvertibleErrorCode(),
                             "resolved group size exceeds "
                             "maxComputeWorkGroupSize/Invocations");

  auto Ctx = std::make_unique<feme::Context>();
  Ctx->setDiagnosticHandler([](const feme::Diagnostic &) {});

  MemoryBufferRef Buffer(
      StringRef(reinterpret_cast<const char *>(Module->words().data()),
                Module->words().size() * sizeof(uint32_t)),
      "shader-module");
  feme::SPIRVImporter Importer;
  feme::ImportOptions ImportOpts;
  Expected<feme::Module> Imported = Importer.import(Buffer, ImportOpts, *Ctx);
  if (!Imported)
    return Imported.takeError();

  feme::SPIRVToLLVMTranslator ToLLVMIR;
  Expected<feme::Module> AsLLVMIR =
      ToLLVMIR.translate(std::move(*Imported), *Ctx);
  if (!AsLLVMIR)
    return AsLLVMIR.takeError();

  llvm::Module &LLVMMod = AsLLVMIR->getLLVMModule();
  clearHostAgnosticMetadata(LLVMMod);

  // Stamp the spec-resolved group size onto the entry point, overriding
  // whatever (if anything) the plain `LocalSize` execution mode already
  // produced -- authoritative for `LocalSizeId`/`BuiltIn WorkgroupSize`,
  // which `feme::spirv::createConvertSPIRVToLLVMPass` does not resolve on
  // its own (see GroupSize.h's file comment).
  if (llvm::Function *EntryFn = LLVMMod.getFunction(EntryPoint)) {
    std::string NumThreads =
        (Twine(GroupSize->at(0)) + "," + Twine(GroupSize->at(1)) + "," +
         Twine(GroupSize->at(2)))
            .str();
    EntryFn->addFnAttr("hlsl.numthreads", NumThreads);
  }

  feme::cpu::JITOptions Opts;
  Opts.EntryPoint = EntryPoint;
  Expected<std::unique_ptr<feme::cpu::CompiledStage>> Stage =
      feme::cpu::CompiledStage::create(*Ctx, std::move(*AsLLVMIR), Opts);
  if (!Stage)
    return Stage.takeError();

  // (V5) The Vulkan object model now supports images, image views, and
  // samplers (see Image.h/Descriptor.h), but consuming one from a real
  // compute dispatch needs `feme::cpu::ResourceLoweringPass`'s SPIR-V path
  // to normalize a descriptor-set-bound image/sampler into the image/
  // sampler heap the same way it already does for a bound buffer -- that
  // reflection does not exist yet (`ResourceInfo::UsesSamplerHeap` is
  // unconditionally false today, see its own comment), so this check
  // remains unreachable until that lands. It stays here rather than being
  // removed so a future shader that does set it is still rejected
  // truthfully instead of silently misdispatching.
  const feme::cpu::ResourceInfo &Info = (*Stage)->getResourceInfo();
  if (Info.UsesSamplerHeap)
    return createStringError(
        inconvertibleErrorCode(),
        "shader uses sampler-heap resources, which this milestone's "
        "VkPipelineLayout cannot bind (see V5)");

  const PipelineLayout &Layout = *fromHandle<PipelineLayout>(CreateInfo.layout);
  if (!pushConstantsCoverRootConstantSize(
          Layout, Info.RootConstantSize,
          DeviceInfo.Properties.limits.maxPushConstantsSize))
    return createStringError(
        inconvertibleErrorCode(),
        "shader's root-constant span is not fully covered by a "
        "VK_SHADER_STAGE_COMPUTE_BIT VkPushConstantRange in its "
        "VkPipelineLayout");

  // Every bound storage-buffer range the shader reads must have a
  // compatible binding in the pipeline layout's descriptor set layouts: the
  // same (set, binding) identity (see PipelineLayout's own comment), a
  // storage-buffer descriptor type, and a declared array big enough to
  // cover the shader's range (see "Descriptor Model": "Descriptor arrays
  // whose length exceeds what the reserved heap can represent must fail
  // pipeline creation rather than silently truncate").
  llvm::ArrayRef<const DescriptorSetLayout *> SetLayouts = Layout.setLayouts();
  for (const feme::cpu::BoundResourceRange &Range : Info.BoundRanges) {
    if (Range.Space >= SetLayouts.size())
      return createStringError(inconvertibleErrorCode(),
                               "shader binds descriptor set %u, which "
                               "VkPipelineLayout does not declare",
                               Range.Space);
    const DescriptorSetLayoutBinding *Binding =
        SetLayouts[Range.Space]->find(Range.BaseRegister);
    if (!Binding || !isSupportedDescriptorType(Binding->Type) ||
        Binding->Count < Range.RangeSize)
      return createStringError(
          inconvertibleErrorCode(),
          "shader's (set %u, binding %u) requirement is not satisfied by "
          "its VkPipelineLayout",
          Range.Space, Range.BaseRegister);
  }

  return std::make_shared<CachedPipelineArtifact>(
      CachedPipelineArtifact{std::move(Ctx), std::move(*Stage)});
}

} // namespace

VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(
    VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkComputePipelineCreateInfo *pCreateInfos,
    const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
  const PhysicalDeviceInfo &DeviceInfo =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  auto *Cache =
      pipelineCache ? fromHandle<PipelineCache>(pipelineCache) : nullptr;
  Allocator Alloc(pAllocator);

  VkResult Result = VK_SUCCESS;
  for (uint32_t I = 0; I != createInfoCount; ++I) {
    pPipelines[I] = VK_NULL_HANDLE;
    const VkComputePipelineCreateInfo &CreateInfo = pCreateInfos[I];

    std::optional<PipelineCacheKey> Key;
    if (Cache && CreateInfo.layout && CreateInfo.stage.module) {
      auto *Module = fromHandle<vulkan::ShaderModule>(CreateInfo.stage.module);
      Expected<SmallVector<SpecializationOverride, 4>> Overrides =
          buildSpecializationOverrides(CreateInfo.stage.pSpecializationInfo);
      if (Overrides) {
        const PipelineLayout &Layout =
            *fromHandle<PipelineLayout>(CreateInfo.layout);
        Key = computePipelineCacheKey(
            DeviceInfo.Properties.pipelineCacheUUID, Module->words(),
            CreateInfo.stage.pName ? CreateInfo.stage.pName : "main",
            *Overrides, Layout.setLayouts(), Layout.pushConstantRanges());
      } else {
        consumeError(Overrides.takeError());
      }
    }

    std::shared_ptr<CachedPipelineArtifact> Artifact =
        Key ? Cache->lookup(*Key) : nullptr;
    if (!Artifact) {
      Expected<std::shared_ptr<CachedPipelineArtifact>> Compiled =
          compileComputePipeline(CreateInfo, DeviceInfo);
      if (!Compiled) {
        consumeError(Compiled.takeError());
        Result = VK_ERROR_INITIALIZATION_FAILED;
        continue;
      }
      Artifact = std::move(*Compiled);
      if (Key)
        Cache->insert(*Key, Artifact);
    }

    ComputePipeline *Obj = Alloc.create<ComputePipeline>(
        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, Artifact);
    if (!Obj) {
      Result = VK_ERROR_OUT_OF_HOST_MEMORY;
      continue;
    }
    pPipelines[I] = toHandle<VkPipeline>(Obj);
  }
  return Result;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(
    VkDevice, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator) {
  if (!pipeline)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<ComputePipeline>(pipeline));
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(
    VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkPipelineCache *pPipelineCache) {
  const PhysicalDeviceInfo &DeviceInfo =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();

  std::vector<PipelineCacheKey> InitialKeys;
  if (pipelineCacheDataIsTrusted() && pCreateInfo->initialDataSize != 0) {
    ArrayRef<uint8_t> Data(
        static_cast<const uint8_t *>(pCreateInfo->pInitialData),
        pCreateInfo->initialDataSize);
    if (std::optional<std::vector<PipelineCacheKey>> Parsed =
            parsePipelineCacheBlob(
                Data, DeviceInfo.Properties.pipelineCacheUUID,
                DeviceInfo.Properties.vendorID, DeviceInfo.Properties.deviceID))
      InitialKeys = std::move(*Parsed);
    // Any validation failure is silently treated as an empty cache -- see
    // "Pipeline Cache": "never as an error and never as a partial load".
  }

  Allocator Alloc(pAllocator);
  PipelineCache *Obj = Alloc.create<PipelineCache>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::move(InitialKeys));
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pPipelineCache = toHandle<VkPipelineCache>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineCache(VkDevice, VkPipelineCache pipelineCache,
                       const VkAllocationCallbacks *pAllocator) {
  if (!pipelineCache)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<PipelineCache>(pipelineCache));
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache,
                       size_t *pDataSize, void *pData) {
  const PhysicalDeviceInfo &DeviceInfo =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  std::vector<uint8_t> Blob = serializePipelineCacheBlob(
      fromHandle<PipelineCache>(pipelineCache)->keys(),
      DeviceInfo.Properties.pipelineCacheUUID, DeviceInfo.Properties.vendorID,
      DeviceInfo.Properties.deviceID);

  if (!pData) {
    *pDataSize = Blob.size();
    return VK_SUCCESS;
  }
  size_t CopySize = std::min(*pDataSize, Blob.size());
  std::memcpy(pData, Blob.data(), CopySize);
  *pDataSize = CopySize;
  return CopySize == Blob.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(
    VkDevice, VkPipelineCache dstCache, uint32_t srcCacheCount,
    const VkPipelineCache *pSrcCaches) {
  auto *Dst = fromHandle<PipelineCache>(dstCache);
  for (uint32_t I = 0; I != srcCacheCount; ++I)
    Dst->merge(*fromHandle<PipelineCache>(pSrcCaches[I]));
  return VK_SUCCESS;
}

} // namespace feme::vulkan
