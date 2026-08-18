//===- Pipeline.cpp - Shader module / pipeline layout / pipeline ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Pipeline.h"
#include "GroupSize.h"
#include "Icd.h"
#include "Objects.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Import/SPIRV/SPIRVImporter.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/JITEngine.h"
#include "feme/Target/CPU/ResourceInfo.h"
#include "feme/Translate/SPIRV/SPIRVToLLVMTranslator.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TargetParser/Triple.h"

#include <cstring>

using namespace feme::vulkan;
using namespace llvm;

feme::vulkan::ComputePipeline::~ComputePipeline() = default;
feme::vulkan::ComputePipeline::ComputePipeline(ComputePipeline &&) noexcept =
    default;
feme::vulkan::ComputePipeline &feme::vulkan::ComputePipeline::operator=(
    ComputePipeline &&) noexcept = default;

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
    Overrides.push_back(
        SpecializationOverride{Entry.constantID, Value});
  }
  return Overrides;
}

} // namespace

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice, const VkShaderModuleCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule) {
  if (pCreateInfo->codeSize == 0 || pCreateInfo->codeSize % sizeof(uint32_t) != 0)
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

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice, const VkPipelineLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkPipelineLayout *pPipelineLayout) {
  // V1 only supports a resource-free pipeline layout (see PipelineLayout's
  // own comment): descriptor sets are V2, push constants are V3.
  if (pCreateInfo->setLayoutCount != 0 ||
      pCreateInfo->pushConstantRangeCount != 0)
    return VK_ERROR_INITIALIZATION_FAILED;

  Allocator Alloc(pAllocator);
  PipelineLayout *Obj =
      Alloc.create<PipelineLayout>(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
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

/// Compiles one `VkComputePipelineCreateInfo` end to end: imports its
/// shader module's SPIR-V, translates it to LLVM IR, resolves and stamps
/// its group size (see GroupSize.h), and compiles it with the FeMe CPU
/// pipeline. See "Compilation flow" in feme/docs/FeMeVulkanDesign.md.
Expected<std::unique_ptr<ComputePipeline>>
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
  uint64_t Invocations = uint64_t(GroupSize->at(0)) * GroupSize->at(1) *
                        GroupSize->at(2);
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
    std::string NumThreads = (Twine(GroupSize->at(0)) + "," +
                             Twine(GroupSize->at(1)) + "," +
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

  // V1 is resource-free (see PipelineLayout's own comment): a shader that
  // needs descriptor-heap resources, root constants, or the sampler heap
  // has nothing this pipeline layout could bind them to.
  const feme::cpu::ResourceInfo &Info = (*Stage)->getResourceInfo();
  if (Info.ReservedResourceHeapSize != 0 || Info.RootConstantSize != 0 ||
      Info.UsesSamplerHeap)
    return createStringError(
        inconvertibleErrorCode(),
        "shader uses descriptor/root-constant resources, which this "
        "milestone's resource-free VkPipelineLayout cannot bind (see V2/V3)");

  return std::make_unique<ComputePipeline>(std::move(Ctx),
                                           std::move(*Stage));
}

} // namespace

VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(
    VkDevice device, VkPipelineCache, uint32_t createInfoCount,
    const VkComputePipelineCreateInfo *pCreateInfos,
    const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
  const PhysicalDeviceInfo &DeviceInfo =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  Allocator Alloc(pAllocator);

  VkResult Result = VK_SUCCESS;
  for (uint32_t I = 0; I != createInfoCount; ++I) {
    pPipelines[I] = VK_NULL_HANDLE;
    Expected<std::unique_ptr<ComputePipeline>> Pipeline =
        compileComputePipeline(pCreateInfos[I], DeviceInfo);
    if (!Pipeline) {
      consumeError(Pipeline.takeError());
      Result = VK_ERROR_INITIALIZATION_FAILED;
      continue;
    }
    ComputePipeline *Obj = Alloc.create<ComputePipeline>(
        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::move(**Pipeline));
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

} // namespace feme::vulkan
