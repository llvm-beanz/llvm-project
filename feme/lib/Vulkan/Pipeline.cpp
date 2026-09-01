//===- Pipeline.cpp - Shader module / pipeline layout / pipeline ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Pipeline.h"
#include "Descriptor.h"
#include "Diagnostics.h"
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

#include <algorithm>
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

/// The explicit subgroup size a `VkPipelineShaderStageRequiredSubgroupSize
/// CreateInfo` chained onto \p Next requests (roadmap E7,
/// `VK_EXT_subgroup_size_control`/`subgroupSizeControl`), or 0 if none is
/// chained -- the same "0 resolves from the shader/host, else forces this
/// value" convention `feme::cpu::JITOptions::WaveSize` already uses.
uint32_t findRequiredSubgroupSize(const void *Next) {
  for (const auto *Header = static_cast<const VkBaseInStructure *>(Next);
       Header; Header = Header->pNext)
    if (Header->sType ==
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO)
      return reinterpret_cast<
                 const VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *>(
                 Header)
          ->requiredSubgroupSize;
  return 0;
}

/// The `VkPipelineRobustnessCreateInfo` chained onto \p Next, or `nullptr`
/// if none is -- the same pNext-walk shape `findRequiredSubgroupSize` above
/// uses.
const VkPipelineRobustnessCreateInfo *
findPipelineRobustnessCreateInfo(const void *Next) {
  for (const auto *Header = static_cast<const VkBaseInStructure *>(Next);
       Header; Header = Header->pNext)
    if (Header->sType == VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO)
      return reinterpret_cast<const VkPipelineRobustnessCreateInfo *>(Header);
  return nullptr;
}

bool isValidRobustnessBufferBehavior(VkPipelineRobustnessBufferBehavior V) {
  switch (V) {
  case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DEVICE_DEFAULT:
  case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED:
  case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS:
  case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2:
    return true;
  default:
    return false;
  }
}

bool isValidRobustnessImageBehavior(VkPipelineRobustnessImageBehavior V) {
  switch (V) {
  case VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DEVICE_DEFAULT:
  case VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED:
  case VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS:
  case VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_2:
    return true;
  default:
    return false;
  }
}

} // namespace

namespace feme::vulkan {

llvm::Expected<PipelineRobustness>
resolvePipelineRobustness(const void *PipelinePNext, const void *StagePNext) {
  const VkPipelineRobustnessCreateInfo *Info =
      findPipelineRobustnessCreateInfo(StagePNext);
  if (!Info)
    Info = findPipelineRobustnessCreateInfo(PipelinePNext);
  if (!Info)
    return PipelineRobustness{};
  if (!isValidRobustnessBufferBehavior(Info->storageBuffers) ||
      !isValidRobustnessBufferBehavior(Info->uniformBuffers) ||
      !isValidRobustnessBufferBehavior(Info->vertexInputs) ||
      !isValidRobustnessImageBehavior(Info->images))
    return createStringError(
        inconvertibleErrorCode(),
        "VkPipelineRobustnessCreateInfo names an out-of-range "
        "VkPipelineRobustnessBufferBehavior/VkPipelineRobustnessImageBehavior "
        "value");
  PipelineRobustness Result;
  Result.StorageBuffers = Info->storageBuffers;
  Result.UniformBuffers = Info->uniformBuffers;
  Result.VertexInputs = Info->vertexInputs;
  Result.Images = Info->images;
  return Result;
}

void fillPipelineCreationFeedback(const void *pNext, uint32_t StageCount,
                                  bool CacheHit) {
  for (const auto *Header = static_cast<const VkBaseInStructure *>(pNext);
       Header; Header = Header->pNext) {
    if (Header->sType !=
        VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO)
      continue;
    const auto *Info =
        reinterpret_cast<const VkPipelineCreationFeedbackCreateInfo *>(Header);
    VkPipelineCreationFeedbackFlags Flags =
        VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
    if (CacheHit)
      Flags |= VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
    if (Info->pPipelineCreationFeedback) {
      Info->pPipelineCreationFeedback->flags = Flags;
      Info->pPipelineCreationFeedback->duration = 0;
    }
    uint32_t StageFeedbackCount =
        std::min(Info->pipelineStageCreationFeedbackCount, StageCount);
    for (uint32_t I = 0; I != StageFeedbackCount; ++I) {
      Info->pPipelineStageCreationFeedbacks[I].flags =
          VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
      Info->pPipelineStageCreationFeedbacks[I].duration = 0;
    }
    return;
  }
}

Pipeline::~Pipeline() = default;

Expected<feme::Module> importShaderModule(feme::Context &Ctx,
                                          llvm::ArrayRef<uint32_t> Words) {
  MemoryBufferRef Buffer(StringRef(reinterpret_cast<const char *>(Words.data()),
                                   Words.size() * sizeof(uint32_t)),
                         "shader-module");
  feme::SPIRVImporter Importer;
  feme::ImportOptions ImportOpts;
  Expected<feme::Module> Imported = Importer.import(Buffer, ImportOpts, Ctx);
  if (!Imported)
    return Imported.takeError();

  feme::SPIRVToLLVMTranslator ToLLVMIR;
  Expected<feme::Module> AsLLVMIR =
      ToLLVMIR.translate(std::move(*Imported), Ctx);
  if (!AsLLVMIR)
    return AsLLVMIR.takeError();

  clearHostAgnosticMetadata(AsLLVMIR->getLLVMModule());
  return AsLLVMIR;
}

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

/// Whether \p Layout's \p StageFlags-visible push-constant ranges fully cover
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
                                        uint32_t MaxPushConstantsSize,
                                        VkShaderStageFlags StageFlags) {
  if (RootConstantSize == 0)
    return true;
  if (RootConstantSize > MaxPushConstantsSize)
    return false;
  std::vector<bool> Covered(RootConstantSize, false);
  for (const VkPushConstantRange &Range : Layout.pushConstantRanges()) {
    if ((Range.stageFlags & StageFlags) == 0)
      continue;
    uint32_t Begin = std::min(Range.offset, RootConstantSize);
    uint32_t End = std::min(Range.offset + Range.size, RootConstantSize);
    for (uint32_t I = Begin; I != End; ++I)
      Covered[I] = true;
  }
  return llvm::all_of(Covered, [](bool B) { return B; });
}

namespace {

/// Whether a binding declared as \p Type can serve a bound range the
/// compiler assigned to \p Class's heap. A buffer range accepts every
/// non-image, non-sampler supported type (the buffer heap holds all of
/// them); an image or sampler range accepts only a descriptor type that
/// actually carries one, so a shader that samples through (set, binding)
/// cannot be handed a storage buffer there. A `COMBINED_IMAGE_SAMPLER`
/// satisfies both an image and a sampler range, matching how
/// `runDispatch` materializes one into both heaps.
bool descriptorTypeMatchesClass(VkDescriptorType Type,
                                feme::cpu::BoundResourceClass Class) {
  switch (Class) {
  case feme::cpu::BoundResourceClass::Buffer:
    return !isImageDescriptorType(Type) && !isSamplerDescriptorType(Type);
  case feme::cpu::BoundResourceClass::Image:
    return isImageDescriptorType(Type);
  case feme::cpu::BoundResourceClass::Sampler:
    return isSamplerDescriptorType(Type);
  }
  return false;
}

} // namespace

// Every bound range the shader reads must have a compatible binding in the
// pipeline layout's descriptor set layouts: the same (set, binding)
// identity (see PipelineLayout's own comment), a descriptor type of the
// matching *class* (a shader that samples through (set, binding) must not
// be handed a storage buffer there), and a declared array big enough to
// cover the shader's range (see "Descriptor Model": "Descriptor arrays
// whose length exceeds what the reserved heap can represent must fail
// pipeline creation rather than silently truncate").
Error validateBoundRanges(const feme::cpu::ResourceInfo &Info,
                          const PipelineLayout &Layout) {
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
        Binding->Count < Range.RangeSize ||
        !descriptorTypeMatchesClass(Binding->Type, Range.Class))
      return createStringError(
          inconvertibleErrorCode(),
          "shader's (set %u, binding %u) requirement is not satisfied by "
          "its VkPipelineLayout",
          Range.Space, Range.BaseRegister);
  }
  return Error::success();
}

namespace {

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

  Expected<feme::Module> AsLLVMIR = importShaderModule(*Ctx, Module->words());
  if (!AsLLVMIR)
    return AsLLVMIR.takeError();

  llvm::Module &LLVMMod = AsLLVMIR->getLLVMModule();

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
  // (roadmap E7) A chained
  // `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` forces this pipeline
  // to compile at that explicit subgroup size instead of the host-derived
  // default `feme::cpu::resolveWaveSize` would otherwise pick;
  // `resolveWaveSize` itself validates it (power of two, in
  // `[MinSubgroupSize, MaxSubgroupSize]`), so no separate check is needed
  // here.
  Opts.WaveSize = findRequiredSubgroupSize(CreateInfo.stage.pNext);
  Expected<std::unique_ptr<feme::cpu::CompiledStage>> Stage =
      feme::cpu::CompiledStage::create(*Ctx, std::move(*AsLLVMIR), Opts);
  if (!Stage)
    return Stage.takeError();

  // (roadmap E7) `VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT`
  // promises every subgroup launched in this stage is fully populated (no
  // masked-off lane); the only way this CPU target's SIMD-widened dispatch
  // can honor that promise is for the workgroup's X dimension to itself be
  // a multiple of the resolved subgroup size, so pipeline creation must
  // reject anything that isn't rather than silently mask partial groups.
  if ((CreateInfo.stage.flags &
       VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT) &&
      GroupSize->at(0) % (*Stage)->getWaveSize() != 0)
    return createStringError(
        inconvertibleErrorCode(),
        "VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT requires "
        "the workgroup's local size in the X dimension (%u) to be a "
        "multiple of the resolved subgroup size (%u)",
        GroupSize->at(0), (*Stage)->getWaveSize());

  // (V5, completed by roadmap R30's SPIR-V image lowering) A shader that
  // samples an image is no longer rejected here: `feme::cpu::
  // SPIRVResourceLoweringPass` now assigns a descriptor-set-bound image and
  // sampler slots in the image and sampler heaps, and `runDispatch`
  // materializes both from the bound sets, exactly as it already did for a
  // bound buffer. What each bound range needs from the pipeline layout is
  // checked per class below instead.
  const feme::cpu::ResourceInfo &Info = (*Stage)->getResourceInfo();

  const PipelineLayout &Layout = *fromHandle<PipelineLayout>(CreateInfo.layout);
  if (!pushConstantsCoverRootConstantSize(
          Layout, Info.RootConstantSize,
          DeviceInfo.Properties.limits.maxPushConstantsSize,
          VK_SHADER_STAGE_COMPUTE_BIT))
    return createStringError(
        inconvertibleErrorCode(),
        "shader's root-constant span is not fully covered by a "
        "VK_SHADER_STAGE_COMPUTE_BIT VkPushConstantRange in its "
        "VkPipelineLayout");

  if (Error E = validateBoundRanges(Info, Layout))
    return std::move(E);

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
            *Overrides, Layout.setLayouts(), Layout.pushConstantRanges(),
            findRequiredSubgroupSize(CreateInfo.stage.pNext),
            CreateInfo.stage.flags);
      } else {
        consumeError(Overrides.takeError());
      }
    }

    std::shared_ptr<CachedPipelineArtifact> Artifact =
        Key ? Cache->lookup(*Key) : nullptr;
    bool CacheHit = Artifact != nullptr;
    if (!Artifact) {
      // (roadmap E9) `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_
      // BIT`: this pipeline missed the cache (or none was given), and the
      // caller asked to be told rather than pay for a real compile here.
      if (CreateInfo.flags &
          VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) {
        if (Result == VK_SUCCESS)
          Result = VK_PIPELINE_COMPILE_REQUIRED;
        continue;
      }
      Expected<std::shared_ptr<CachedPipelineArtifact>> Compiled =
          compileComputePipeline(CreateInfo, DeviceInfo);
      if (!Compiled) {
        logCreationFailure(Compiled.takeError(), "vkCreateComputePipelines");
        Result = VK_ERROR_INITIALIZATION_FAILED;
        continue;
      }
      Artifact = std::move(*Compiled);
      if (Key)
        Cache->insert(*Key, Artifact);
    }

    // (roadmap F10) `VK_EXT_pipeline_robustness`: validated (and, for a
    // cache hit, still validated -- unlike the compile above, this is
    // cheap and independent of the cached artifact) regardless of whether
    // this pipeline missed the cache.
    Expected<PipelineRobustness> Robustness = resolvePipelineRobustness(
        CreateInfo.pNext, CreateInfo.stage.pNext);
    if (!Robustness) {
      consumeError(Robustness.takeError());
      Result = VK_ERROR_INITIALIZATION_FAILED;
      continue;
    }

    // (roadmap E19) `VK_EXT_pipeline_creation_feedback`: a compute pipeline
    // has exactly one stage.
    fillPipelineCreationFeedback(CreateInfo.pNext, /*StageCount=*/1, CacheHit);

    ComputePipeline *Obj = Alloc.create<ComputePipeline>(
        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, Artifact, CreateInfo.flags,
        *Robustness);
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
  // Either bind point's object is freed through the common `Pipeline` base,
  // whose destructor is virtual precisely so this call site does not have to
  // know which kind it holds.
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<Pipeline>(pipeline));
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

  // (roadmap E9) `VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT`:
  // see PipelineCache.h's class comment for what this changes.
  bool ExternallySynchronized =
      pCreateInfo->flags & VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;

  Allocator Alloc(pAllocator);
  PipelineCache *Obj = Alloc.create<PipelineCache>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::move(InitialKeys),
      ExternallySynchronized);
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
