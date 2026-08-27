//===- PhysicalDeviceInfo.cpp - Truthful device capabilities ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PhysicalDeviceInfo.h"

#include "feme/Target/CPU/WaveSize.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/MD5.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unistd.h>

using namespace feme::vulkan;

namespace {

// Vulkan's own reserved value for "implementations that have not yet been
// assigned an official Khronos vendor ID" (Vulkan-Docs' vendor ID
// conventions; see "Device identity"'s note that FeMe has no registered
// vendor/driver ID of its own yet).
constexpr uint32_t FeMeVendorID = 0x10000;
constexpr uint32_t FeMeDeviceID = 0x00000001;

/// Best-effort host SIMD vector width in bits, used only to pick a default
/// wave size (see `feme::cpu::resolveWaveSize`); this deliberately avoids
/// standing up a full `TargetMachine`/`TargetTransformInfo` (unlike
/// `feme::Driver`'s `getHostVectorBits`) since V0 does no shader compilation
/// at all yet and so has nothing to initialize LLVM's target registry for
/// (see "Process Coexistence and Symbol Visibility"'s one-shot
/// `std::once_flag` requirement, which becomes relevant once a later
/// milestone JIT-compiles a pipeline). Correctness never depends on this
/// value, only which default wave size a host with no other opinion gets.
unsigned detectHostVectorBits() {
  constexpr unsigned Fallback = 128;
  llvm::StringMap<bool> Features = llvm::sys::getHostCPUFeatures();
  auto Has = [&](llvm::StringRef Feature) {
    auto It = Features.find(Feature);
    return It != Features.end() && It->second;
  };
  if (Has("avx512f"))
    return 512;
  if (Has("avx2") || Has("avx"))
    return 256;
  return Fallback;
}

/// The host's total physical memory in bytes, used for the single reported
/// memory heap ("Physical Device and Capabilities" requires limits to
/// "match the host allocator's real guarantees ... not the specification
/// minima copied verbatim"). Falls back to a conservative 1 GiB if the host
/// can't answer (e.g. an unexpected `sysconf` failure).
VkDeviceSize detectHostMemorySize() {
  long Pages = sysconf(_SC_PHYS_PAGES);
  long PageSize = sysconf(_SC_PAGE_SIZE);
  if (Pages <= 0 || PageSize <= 0)
    return VkDeviceSize{1} << 30;
  return static_cast<VkDeviceSize>(Pages) * static_cast<VkDeviceSize>(PageSize);
}

template <size_t N> void copyStringField(char (&Dst)[N], llvm::StringRef Src) {
  size_t Count = std::min(Src.size(), N - 1);
  std::memcpy(Dst, Src.data(), Count);
  Dst[Count] = '\0';
}

/// Fills the device and pipeline cache UUIDs (see "Device identity": both
/// "must be deterministic for the FeMe ABI version, LLVM version, target
/// triple, host CPU feature policy, selected wave size, and driver build"
/// and "must change whenever any of those can change generated code or
/// serialized data"). `VK_UUID_SIZE` is 16 bytes, exactly an MD5 digest.
void fillUUID(uint8_t (&UUID)[VK_UUID_SIZE], llvm::StringRef Tag,
              unsigned SubgroupSize) {
  llvm::MD5 Hash;
  // A fixed tag identifying which UUID this is and this as a FeMe Vulkan V0
  // ABI, so a future milestone that changes what the cache key must cover
  // (e.g. once pipelines are actually compiled) can change this tag to
  // invalidate every existing cache entry, and so the device and pipeline
  // cache UUIDs are never accidentally equal.
  Hash.update("feme-vulkan-v0-");
  Hash.update(Tag);
  Hash.update(LLVM_VERSION_STRING);
  Hash.update(llvm::sys::getProcessTriple());
  Hash.update(llvm::sys::getHostCPUName());
  Hash.update(llvm::StringRef(reinterpret_cast<const char *>(&SubgroupSize),
                              sizeof(SubgroupSize)));
  llvm::MD5::MD5Result Result;
  Hash.final(Result);
  static_assert(sizeof(Result) == VK_UUID_SIZE,
                "MD5 digest size must match VK_UUID_SIZE");
  std::memcpy(UUID, Result.data(), VK_UUID_SIZE);
}

} // namespace

PhysicalDeviceInfo feme::vulkan::computePhysicalDeviceInfo() {
  PhysicalDeviceInfo Info;

  // "Subgroup size": pin one wave size device-wide, derived from the host's
  // vector width, reusing the CPU target's own resolution table rather than
  // re-deriving its clamping/power-of-two rules.
  llvm::Expected<unsigned> WaveSizeOrErr = feme::cpu::resolveWaveSize(
      /*UserWaveSize=*/std::nullopt, /*ShaderRequirement=*/std::nullopt,
      detectHostVectorBits());
  // `resolveWaveSize` only fails for an invalid explicit request; with no
  // user or shader opinion given, it always succeeds from the host-derived
  // default table, but the failure path is still handled explicitly rather
  // than assumed away.
  if (WaveSizeOrErr) {
    Info.SubgroupSize = *WaveSizeOrErr;
  } else {
    llvm::consumeError(WaveSizeOrErr.takeError());
    Info.SubgroupSize = feme::cpu::MinWaveSize;
  }

  Info.SubgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
  Info.SubgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT;

  // (roadmap E7) `subgroupSizeControl`'s own range: every power-of-two wave
  // size `feme::cpu::resolveWaveSize` itself accepts, reused rather than
  // re-derived (see PhysicalDeviceInfo.h's field comment).
  Info.MinSubgroupSize = feme::cpu::MinWaveSize;
  Info.MaxSubgroupSize = feme::cpu::MaxWaveSize;
  Info.RequiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;

  // (roadmap E14) `inlineUniformBlock`'s own limits: spec-minimum floors
  // (see PhysicalDeviceInfo.h's field comment) since the underlying
  // storage (a plain byte blob per binding, Descriptor.h) has no real
  // hardware-derived cap to report instead.
  Info.MaxInlineUniformBlockSize = 256;
  Info.MaxPerStageDescriptorInlineUniformBlocks = 4;
  Info.MaxDescriptorSetInlineUniformBlocks = 4;
  Info.MaxInlineUniformTotalSize = 1024;

  VkPhysicalDeviceProperties &Props = Info.Properties;
  // Illustrative per "Loader Integration": "The exact advertised API version
  // is selected during implementation from the core command and CTS
  // coverage actually achieved." Development checkpoint version, not a claim
  // of Vulkan 1.4 conformance (see "Initial Non-Goals": zero
  // `VkConformanceVersion`). Roadmap D0 bumped this from 1.2 to 1.4 (see
  // Roadmap.md §1.9.2); VulkanCTSReport.md's "Roadmap D0: measured impact"
  // records the CTS effect of that jump alone, before any 1.3/1.4 mandatory
  // feature work has landed.
  Props.apiVersion = VK_API_VERSION_1_4;
  Props.driverVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
  Props.vendorID = FeMeVendorID;
  Props.deviceID = FeMeDeviceID;
  Props.deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
  std::strncpy(Props.deviceName, "FeMe CPU Vulkan Device",
               sizeof(Props.deviceName) - 1);
  fillUUID(Props.pipelineCacheUUID, "pipeline-cache-uuid", Info.SubgroupSize);
  fillUUID(Info.DeviceUUID, "device-uuid", Info.SubgroupSize);
  fillUUID(Info.OptimalTilingLayoutUUID, "optimal-tiling-layout-uuid",
           Info.SubgroupSize);

  VkPhysicalDeviceLimits &Limits = Props.limits;
  // Vulkan 1.0/1.1 core "Required Limits" minima (vkspec appendix), except
  // where the host can honestly promise more (memory-size-derived fields
  // are computed separately below). A compute-only device with no graphics
  // queue still must report every limit field; the graphics-only ones
  // (tessellation, geometry, viewport, framebuffer, ...) are set to the
  // spec-mandated minimum since no graphics feature that would let an
  // application rely on a larger value is advertised.
  Limits.maxImageDimension1D = 4096;
  Limits.maxImageDimension2D = 4096;
  Limits.maxImageDimension3D = 256;
  Limits.maxImageDimensionCube = 4096;
  Limits.maxImageArrayLayers = 256;
  Limits.maxTexelBufferElements = 65536;
  Limits.maxUniformBufferRange = 16384;
  Limits.maxStorageBufferRange = 134217728;
  Limits.maxPushConstantsSize = 128;
  Limits.maxMemoryAllocationCount = 4096;
  Limits.maxSamplerAllocationCount = 4000;
  Limits.bufferImageGranularity = 131072;
  Limits.sparseAddressSpaceSize = 0; // No sparse binding support.
  Limits.maxBoundDescriptorSets = 4;
  Limits.maxPerStageDescriptorSamplers = 16;
  Limits.maxPerStageDescriptorUniformBuffers = 12;
  Limits.maxPerStageDescriptorStorageBuffers = 4;
  Limits.maxPerStageDescriptorSampledImages = 16;
  Limits.maxPerStageDescriptorStorageImages = 4;
  Limits.maxPerStageDescriptorInputAttachments = 4;
  Limits.maxPerStageResources = 128;
  Limits.maxDescriptorSetSamplers = 96;
  Limits.maxDescriptorSetUniformBuffers = 72;
  Limits.maxDescriptorSetUniformBuffersDynamic = 8;
  Limits.maxDescriptorSetStorageBuffers = 24;
  Limits.maxDescriptorSetStorageBuffersDynamic = 4;
  Limits.maxDescriptorSetSampledImages = 96;
  Limits.maxDescriptorSetStorageImages = 24;
  Limits.maxDescriptorSetInputAttachments = 4;
  Limits.maxVertexInputAttributes = 16;
  Limits.maxVertexInputBindings = 16;
  Limits.maxVertexInputAttributeOffset = 2047;
  Limits.maxVertexInputBindingStride = 2048;
  Limits.maxVertexOutputComponents = 64;
  Limits.maxTessellationGenerationLevel = 64;
  Limits.maxTessellationPatchSize = 32;
  Limits.maxTessellationControlPerVertexInputComponents = 64;
  Limits.maxTessellationControlPerVertexOutputComponents = 64;
  Limits.maxTessellationControlPerPatchOutputComponents = 120;
  Limits.maxTessellationControlTotalOutputComponents = 2048;
  Limits.maxTessellationEvaluationInputComponents = 64;
  Limits.maxTessellationEvaluationOutputComponents = 64;
  Limits.maxGeometryShaderInvocations = 32;
  Limits.maxGeometryInputComponents = 64;
  Limits.maxGeometryOutputComponents = 64;
  Limits.maxGeometryOutputVertices = 256;
  Limits.maxGeometryTotalOutputComponents = 1024;
  Limits.maxFragmentInputComponents = 64;
  Limits.maxFragmentOutputAttachments = 4;
  Limits.maxFragmentDualSrcAttachments = 1;
  Limits.maxFragmentCombinedOutputResources = 4;
  // Roadmap R23 closed `feme::cpu`'s divergent-groupshared-access gap (see
  // "Limits and features"), so this is no longer pinned at the spec
  // minimum: 32768 bytes is a value every groupshared allocation this
  // milestone's host stack/heap can actually satisfy.
  Limits.maxComputeSharedMemorySize = 32768;
  Limits.maxComputeWorkGroupCount[0] = 65535;
  Limits.maxComputeWorkGroupCount[1] = 65535;
  Limits.maxComputeWorkGroupCount[2] = 65535;
  Limits.maxComputeWorkGroupInvocations = 128;
  Limits.maxComputeWorkGroupSize[0] = 128;
  Limits.maxComputeWorkGroupSize[1] = 128;
  Limits.maxComputeWorkGroupSize[2] = 64;
  // (roadmap E7) The worst case for `maxComputeWorkgroupSubgroups`: every
  // subgroup launched at the smallest allowed size still fits within one
  // workgroup's invocation limit.
  Info.MaxComputeWorkgroupSubgroups =
      Limits.maxComputeWorkGroupInvocations / Info.MinSubgroupSize;
  Limits.subPixelPrecisionBits = 4;
  Limits.subTexelPrecisionBits = 4;
  Limits.mipmapPrecisionBits = 4;
  Limits.maxDrawIndexedIndexValue = (1u << 24) - 1;
  Limits.maxDrawIndirectCount = 1;
  Limits.maxSamplerLodBias = 2.0f;
  Limits.maxSamplerAnisotropy = 1.0f;
  // Roadmap H3 implements exactly Vulkan's mandatory multi-viewport floor:
  // 16 independent viewport/scissor slots (`MaxViewportCount`), no larger
  // arbitrary value until a test needs one. Every consuming loop in
  // GraphicsPipeline.cpp/CommandBuffer.cpp/Executor.cpp is bounded by this
  // same constant, so this is a real contract rather than a speculative
  // advertisement.
  Limits.maxViewports = MaxViewportCount;
  Limits.maxViewportDimensions[0] = 4096;
  Limits.maxViewportDimensions[1] = 4096;
  Limits.viewportBoundsRange[0] = -8192.0f;
  Limits.viewportBoundsRange[1] = 8191.0f;
  Limits.viewportSubPixelBits = 0;
  Limits.minMemoryMapAlignment = 64;
  Limits.minTexelBufferOffsetAlignment = 256;
  Limits.minUniformBufferOffsetAlignment = 256;
  Limits.minStorageBufferOffsetAlignment = 256;
  Limits.minTexelOffset = -8;
  Limits.maxTexelOffset = 7;
  Limits.minTexelGatherOffset = -8;
  Limits.maxTexelGatherOffset = 7;
  Limits.minInterpolationOffset = -0.5f;
  Limits.maxInterpolationOffset = 0.4375f;
  Limits.subPixelInterpolationOffsetBits = 4;
  Limits.maxFramebufferWidth = 4096;
  Limits.maxFramebufferHeight = 4096;
  Limits.maxFramebufferLayers = 256;
  // (V6) Framebuffer attachments may now be multisample, up to the 8
  // samples the software rasterizer implements coverage and resolves for
  // (roadmap R33/C4b; `feme::graphics::executeDraws` rejects anything
  // else). These stopped being unreachable numbers with the graphics path
  // and are contracts now: `vkCreateRenderPass` checks every attachment's
  // sample count against them, and so does graphics pipeline creation.
  Limits.framebufferColorSampleCounts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
      VK_SAMPLE_COUNT_8_BIT;
  Limits.framebufferDepthSampleCounts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
      VK_SAMPLE_COUNT_8_BIT;
  Limits.framebufferStencilSampleCounts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
      VK_SAMPLE_COUNT_8_BIT;
  Limits.framebufferNoAttachmentsSampleCounts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
      VK_SAMPLE_COUNT_8_BIT;
  Limits.maxColorAttachments = 4;
  // A sampled/storage image may be created with up to 8 samples (see "V5:
  // Images and sampling"'s multisample-object-model scope note in
  // FeMeVulkanDesign.md): this ICD stores every sample's data, but nothing
  // yet reads a single sample from a shader or resolves one, so there is no
  // reason to advertise a wider count than the CTS/an application might
  // exercise for the object model alone. A *sampled* depth/stencil image
  // stays single-sample: reading an individual sample of one from a shader
  // needs `OpImageFetch`-with-sample-index raising, which R30 left out of
  // scope -- unlike a depth/stencil *attachment*, whose per-sample tests
  // the executor does implement (see the framebuffer counts above).
  Limits.sampledImageColorSampleCounts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
      VK_SAMPLE_COUNT_8_BIT;
  Limits.sampledImageIntegerSampleCounts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
      VK_SAMPLE_COUNT_8_BIT;
  Limits.sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
  Limits.sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
  Limits.storageImageSampleCounts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
      VK_SAMPLE_COUNT_8_BIT;
  Limits.maxSampleMaskWords = 1;
  Limits.timestampComputeAndGraphics = VK_FALSE;
  Limits.timestampPeriod = 1.0f;
  Limits.maxClipDistances = 8;
  Limits.maxCullDistances = 8;
  Limits.maxCombinedClipAndCullDistances = 8;
  Limits.discreteQueuePriorities = 2;
  Limits.pointSizeRange[0] = 1.0f;
  Limits.pointSizeRange[1] = 64.0f;
  Limits.lineWidthRange[0] = 1.0f;
  Limits.lineWidthRange[1] = 1.0f;
  Limits.pointSizeGranularity = 1.0f;
  Limits.lineWidthGranularity = 1.0f;
  Limits.strictLines = VK_FALSE;
  Limits.standardSampleLocations = VK_FALSE;
  // Host memory is plain `malloc`/`VkAllocationCallbacks`-backed storage
  // with no device-side copy engine, so there is no meaningful alignment
  // restriction beyond ordinary object alignment.
  Limits.optimalBufferCopyOffsetAlignment = 1;
  Limits.optimalBufferCopyRowPitchAlignment = 1;
  // A conservative, portable cache-line-sized granularity for host-visible
  // memory coherence, honest for both x86-64 and AArch64 hosts.
  Limits.nonCoherentAtomSize = 64;

  // Vulkan 1.0 core features. Everything defaults false, except (V4)
  // `robustBufferAccess`: `feme::cpu`'s per-descriptor bounds checking (see
  // "Bounds checking" in feme/docs/FeMeCPUDesign.md) is not optional --
  // `feme::cpu::JITOptions::EnableRobustness` defaults true and nothing in
  // this ICD ever overrides it -- so an out-of-bounds buffer access already
  // reads zero / drops the write unconditionally for every dispatch this
  // milestone can run. That is a stronger guarantee than the feature
  // requires (Vulkan allows it to be enabled only per pipeline), so
  // advertising it unconditionally is honest. `dualSrcBlend` (roadmap C4):
  // the executor's dual-source blend path (`feme::graphics::executeDraws`'
  // `FSColor1`) is implemented and `maxFragmentDualSrcAttachments` above
  // is already the honest `1` this feature requires, so advertising it is
  // likewise honest -- `largePoints`/`wideLines` are left `VK_FALSE`: a
  // point's quad expansion still hardcodes a fixed 1-pixel size (roadmap
  // F5 only generalized the *line* path), and although
  // `feme::graphics::RasterState::LineWidth`/`vkCmdSetLineWidth` are now
  // genuinely threaded through the line rasterizer (Executor.cpp), this
  // struct's own `lineWidthRange` stays the honest, degenerate `[1.0,
  // 1.0]` below until a later row claims `wideLines` itself (see
  // Vulkan14FeatureInventory.md's H7 row).
  Info.Features = VkPhysicalDeviceFeatures{};
  Info.Features.robustBufferAccess = VK_TRUE;
  Info.Features.dualSrcBlend = VK_TRUE;
  // Roadmap E20 ("Block-compressed image groundwork + ASTC LDR decode")
  // first tracked this Vulkan 1.0 core feature bit explicitly (previously
  // left implicitly false by the zero-initialization above, unlike
  // `textureCompressionASTC_HDR`'s own dedicated line in EntryPoints.cpp's
  // `VkPhysicalDeviceVulkan13Features` case -- see
  // Vulkan14FeatureInventory.md's own hand-added row), but kept it
  // `VK_FALSE`: `vkCreateImage` rejected every LDR ASTC `VkFormat`
  // outright, and nothing called `ASTCDecode.h`'s `decodeASTCBlock` from
  // any live copy/sampling path. Roadmap E22 closed both gaps
  // (`vkCreateImage` accepts a block-compressed format, and
  // `ImageOps.cpp`'s `runBlitImage` decodes an LDR ASTC source through
  // `decodeASTCBlock`), so this can now honestly flip to `VK_TRUE` -- the
  // same "advertise once the pipeline actually works, not before" gate
  // this ICD's own precedent set for `textureCompressionASTC_LDR`'s
  // sibling bits. `vkCmdCopyImage`/`vkCmdCopyBufferToImage`/
  // `vkCmdCopyImageToBuffer` (`CommandBuffer.cpp`) address a
  // block-compressed image a whole block at a time; a shader's own
  // *sampling* of one still reads all-zero (the separate CPU runtime,
  // `feme/runtime/CPU/FeMeRuntimeCPU.c`, has no block-compressed decode of
  // its own yet -- a real gap, but a safe one, not a crash or silent
  // corruption -- see Image.h's file comment), which is a real content
  // gap this milestone's own file scope did not include closing.
  Info.Features.textureCompressionASTC_LDR = VK_TRUE;
  // Roadmap H3: `multiViewport` gates whether an application may even
  // request more than one viewport/scissor at all -- a real CTS
  // `checkSupport` (e.g. `dEQP-VK.draw.*.shader_viewport_index`) rejects its
  // entire test group as `NotSupported` whenever this bit is false,
  // regardless of `maxViewports`'s value. This ICD's array plumbing (see
  // `MaxViewportCount` above and GraphicsPipeline.cpp/CommandBuffer.cpp/
  // Executor.cpp) now genuinely supports multiple viewports/scissors, so
  // this can honestly flip to `VK_TRUE`.
  Info.Features.multiViewport = VK_TRUE;
  // Roadmap H4b: `GraphicsPipeline.cpp`'s `vkCreateGraphicsPipelines` now
  // accepts `VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT`/`_EVALUATION_BIT`,
  // translates `patchControlPoints`, compiles both stages' modules into
  // the hull/patch-constant/domain `CompiledStage`s
  // `graphics::GraphicsPipeline::setTessellationStages` (which the
  // executor already consumes, roadmap H4) requires, and enforces
  // `VK_PRIMITIVE_TOPOLOGY_PATCH_LIST` exactly when they are present -- so
  // this can honestly flip to `VK_TRUE`. `maxTessellationPatchSize` (32)
  // and `maxTessellationGenerationLevel` (64) above already match this
  // implementation's own honest ceilings, `feme::graphics::
  // MaxPatchControlPoints` (Graphics/Patch.h) and `feme::graphics::
  // DefaultMaxTessFactor` (Graphics/Tessellator.h) respectively, and were
  // not raised for this milestone.
  Info.Features.tessellationShader = VK_TRUE;

  VkPhysicalDeviceMemoryProperties &MemProps = Info.MemoryProperties;
  MemProps.memoryTypeCount = 1;
  MemProps.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                          VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  MemProps.memoryTypes[0].heapIndex = 0;
  MemProps.memoryHeapCount = 1;
  VkDeviceSize HostMemorySize = detectHostMemorySize();
  MemProps.memoryHeaps[0].size = HostMemorySize;
  MemProps.memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

  // Roadmap C6 ("Mandatory 1.2 features and limits"): see the field
  // comments in PhysicalDeviceInfo.h for why each of these is honest
  // rather than merely the spec-mandated floor. `MaxMultiviewViewCount`/
  // `MaxMultiviewInstanceIndex` stay at their required minimum even
  // though `multiview` is now advertised (roadmap H2): nothing about this
  // ICD's per-view draw loop (`CommandBuffer.cpp`'s `runDraw`) caps the
  // view count or instance index below the spec floor, but nothing raises
  // it above that floor either.
  Info.MaxMemoryAllocationSize = HostMemorySize;
  Info.MaxPerSetDescriptors = 1024;
  Info.MaxMultiviewViewCount = 6;
  Info.MaxMultiviewInstanceIndex = (1u << 27) - 1;
  Info.MaxTimelineSemaphoreValueDifference =
      std::numeric_limits<uint64_t>::max();

  // "Graphics queue family": V6 adds `VK_QUEUE_GRAPHICS_BIT` to the one
  // existing universal family rather than inventing a second *graphics*
  // family -- a single software device with one worker pool has no
  // independent graphics engine, and reporting two families would be an
  // untruthful description of the hardware model. The bit commits this
  // queue to accepting every core graphics command, which is why it lands
  // only now that render passes, graphics pipelines, draws, clears, blits
  // and resolves are all implemented (see the V6 status note in
  // FeMeVulkanDesign.md for the state combinations that are rejected at
  // creation rather than at draw time).
  VkQueueFamilyProperties &Universal = Info.QueueFamilies[0];
  Universal.queueFlags =
      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
  Universal.queueCount = 1;
  Universal.timestampValidBits = 0;
  Universal.minImageTransferGranularity = {1, 1, 1};

  // Roadmap C7 ("Queue family capability combinations"): a second,
  // `TRANSFER`-only family. This is distinct from the rejected "separate
  // graphics family" alternative above -- it adds no capability the
  // executor cannot honor and claims no independent engine, it only ever
  // promises *less* than the universal family already does. Its sole
  // purpose is to give applications (and the CTS) a queue that genuinely
  // excludes `GRAPHICS`/`COMPUTE`, which several mandatory CTS cases
  // require (e.g.
  // `dEQP-VK.pipeline.*.timestamp.transfer_tests.*_transfer_queue`) and
  // which no single universal family can ever satisfy by definition.
  VkQueueFamilyProperties &DedicatedTransfer = Info.QueueFamilies[1];
  DedicatedTransfer.queueFlags = VK_QUEUE_TRANSFER_BIT;
  DedicatedTransfer.queueCount = 1;
  DedicatedTransfer.timestampValidBits = 0;
  DedicatedTransfer.minImageTransferGranularity = {1, 1, 1};

  // A third, `COMPUTE | TRANSFER`-only family, excluding `GRAPHICS` --
  // the same reasoning as the dedicated transfer family, for the
  // mandatory CTS cases that specifically need a compute queue that is
  // not also a graphics queue (e.g.
  // `dEQP-VK.api.buffer_marker.compute.*`). `TRANSFER` is included
  // explicitly rather than left implicit (Vulkan guarantees any
  // graphics- or compute-capable queue may also be used for transfer
  // operations even without the bit set) so this family's advertised
  // flags are self-contained and unambiguous to a caller that only reads
  // `queueFlags`.
  VkQueueFamilyProperties &DedicatedCompute = Info.QueueFamilies[2];
  DedicatedCompute.queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
  DedicatedCompute.queueCount = 1;
  DedicatedCompute.timestampValidBits = 0;
  DedicatedCompute.minImageTransferGranularity = {1, 1, 1};

  Info.DriverId = VK_DRIVER_ID_MAX_ENUM;
  Info.ConformanceVersion = {0, 0, 0, 0};
  copyStringField(Info.DriverName, "FeMe Vulkan Driver");
  copyStringField(Info.DriverInfo,
                  "LLVM in-tree development ICD; no Khronos conformance claim");

  return Info;
}

llvm::ArrayRef<VkExtensionProperties>
feme::vulkan::getSupportedDeviceExtensions() {
  static const VkExtensionProperties Extensions[] = {
      {VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
       VK_KHR_DYNAMIC_RENDERING_SPEC_VERSION},
      // (roadmap C4c) Every one of this extension's 12 dynamic states is
      // implemented (GraphicsPipeline.cpp's DynamicStateBits/
      // mapDynamicState, vkCmdSet*EXT/vkCmdBindVertexBuffers2EXT in
      // CommandBuffer.cpp), closing roadmap C4's "mapDynamicState beyond
      // its six states" -- see FeMeVulkanDesign.md's updated status note.
      {VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
       VK_EXT_EXTENDED_DYNAMIC_STATE_SPEC_VERSION},
      // (roadmap E3) `vkCmdPipelineBarrier2`/`vkCmdWriteTimestamp2`/
      // `vkQueueSubmit2`/`vkCmdSetEvent2`/`vkCmdResetEvent2`/
      // `vkCmdWaitEvents2` (CommandBuffer.cpp/Sync.cpp) are all
      // implemented; unlike `VK_KHR_copy_commands2` (roadmap D0), whose
      // core names alone sufficed once apiVersion reached 1.4,
      // `dEQP-VK.synchronization2`'s own multi-queue/custom-device cases
      // (`vktCustomInstancesDevices.cpp`) explicitly enable this
      // extension by name at `vkCreateDevice` regardless of the
      // advertised `apiVersion`, so it must be listed here too, or every
      // one of them fails with `VK_ERROR_EXTENSION_NOT_PRESENT` (see
      // "Roadmap E3: measured impact" in VulkanCTSReport.md).
      {VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
       VK_KHR_SYNCHRONIZATION_2_SPEC_VERSION},
      // (roadmap E5) A null dynamic-rendering color attachment view
      // (RenderPass.h/CommandBuffer.cpp), `VK_FORMAT_A8_UNORM`/
      // `A1B5G5R5_UNORM_PACK16` (Format.cpp), and `vkCmdBindIndexBuffer2`
      // (CommandBuffer.cpp) are all implemented; like `synchronization2`
      // above (an exception to the "mirrored by `SUPPORTED_EXTENSIONS`"
      // rule, since `vkCmdBindIndexBuffer2` is already a core, non-`KHR`-
      // suffixed `VK_VERSION_1_4` entry `vk_gen_entrypoints.py`'s
      // `CORE_FEATURES` resolves), `dEQP-VK.draw.*maintenance_5` and
      // `dEQP-VK.api.maintenance5.*`'s own `requireDeviceFunctionality
      // ("VK_KHR_maintenance5")` calls enable this extension by name
      // regardless of the advertised `apiVersion`, so it must be listed
      // here too, or every one of them fails `NotSupported` instead of
      // running for real.
      {VK_KHR_MAINTENANCE_5_EXTENSION_NAME, VK_KHR_MAINTENANCE_5_SPEC_VERSION},
      // (roadmap E6, completed by F12) `vkCmdBindDescriptorSets2`/
      // `vkCmdPushConstants2`/`vkCmdPushDescriptorSet2`/`vkCmdPushDescriptor
      // SetWithTemplate2` (CommandBuffer.cpp) are all implemented now --
      // the latter two, sharing `Descriptor.{h,cpp}`'s push-descriptor
      // mechanism with `VK_KHR_push_descriptor` itself, were deferred until
      // roadmap F12 built that mechanism (see this row's own dependency
      // note in Roadmap.md). Like `synchronization2`/`maintenance5` above,
      // this extension's commands are already core, non-`KHR`-suffixed
      // `VK_VERSION_1_4` entries `vk_gen_entrypoints.py`'s `CORE_FEATURES`
      // resolves, but `dEQP-VK.api.maintenance6.*`'s own
      // `requireDeviceFunctionality("VK_KHR_maintenance6")` calls still
      // enable this extension by name regardless of the advertised
      // `apiVersion`, so it must be listed here too, or every one of them
      // fails `NotSupported` instead of running for real.
      {VK_KHR_MAINTENANCE_6_EXTENSION_NAME, VK_KHR_MAINTENANCE_6_SPEC_VERSION},
      // (roadmap E8) `spirv.SDot`/`spirv.UDot`/`spirv.SUDot` and their
      // `*AccSat` counterparts now have real `spirv`->`llvm` conversion
      // patterns (SPIRVToLLVMPatterns.cpp); like `synchronization2`/
      // `maintenance5`/`maintenance6` above,
      // `dEQP-VK.spirv_assembly.instruction.compute`'s own integer-dot-
      // product tests (`vktSpvAsmIntegerDotProductTests.cpp`) enable this
      // extension by name regardless of the advertised `apiVersion`, so it
      // must be listed here too, or every one of them fails
      // `VK_ERROR_EXTENSION_NOT_PRESENT` instead of running for real. None
      // of the 36 `integerDotProduct*Accelerated` limit bits are claimed,
      // however: this is a real CPU multiply-add sequence, not a genuinely
      // accelerated one, and a truthful "supported but not accelerated" is
      // a valid, conformant answer this extension's own spec permits.
      {VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME,
       VK_KHR_SHADER_INTEGER_DOT_PRODUCT_SPEC_VERSION},
      // (roadmap E9) `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_
      // BIT` (Pipeline.cpp/GraphicsPipeline.cpp) and `VK_PIPELINE_CACHE_
      // CREATE_EXTERNALLY_SYNCHRONIZED_BIT` (PipelineCache.{h,cpp}) are
      // both implemented; like `synchronization2`/`maintenance5`/
      // `maintenance6`/`shader_integer_dot_product` above,
      // `dEQP-VK.api.pipeline_creation_cache_control.*` and
      // `dEQP-VK.pipeline.pipeline_creation_cache_control.*` enable this
      // extension by name regardless of the advertised `apiVersion`, so it
      // must be listed here too, or every one of them fails `NotSupported`
      // instead of running for real.
      {VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME,
       VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_SPEC_VERSION},
      // (roadmap E10) `vkCreatePrivateDataSlot`/`vkSetPrivateData`/
      // `vkGetPrivateData`/`vkDestroyPrivateDataSlot` (PrivateData.{h,cpp})
      // are implemented; like `synchronization2`/`maintenance5`/
      // `maintenance6`/`shader_integer_dot_product`/
      // `pipeline_creation_cache_control` above,
      // `dEQP-VK.api.private_data.*` enables this extension by name
      // regardless of the advertised `apiVersion`, so it must be listed
      // here too, or every one of those cases fails `NotSupported` instead
      // of running for real.
      {VK_EXT_PRIVATE_DATA_EXTENSION_NAME, VK_EXT_PRIVATE_DATA_SPEC_VERSION},
      // (roadmap E11) `OpDemoteToHelperInvocation` now converts
      // (SPIRVToLLVMPatterns.cpp/CanonicalizeStage.cpp) to
      // `feme.stage.demote`; like `private_data`/`pipeline_creation_cache_
      // control` above, CTS's `dEQP-VK.spirv_assembly.instruction.graphics.
      // demote_to_helper_invocation.*`/`dEQP-VK.shader_execution_
      // properties.*` enable this extension by name regardless of the
      // advertised `apiVersion`, so it must be listed here too.
      {VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME,
       VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_SPEC_VERSION},
      // (roadmap E12) `OpTerminateInvocation` now converts
      // (SPIRVToLLVMPatterns.cpp) to an unconditional discard-and-return;
      // like `shader_demote_to_helper_invocation` above, CTS's
      // `graphicsfuzz`/`dEQP-VK.spirv_assembly.instruction.graphics.
      // terminate_invocation.*` cases enable this extension by name
      // regardless of the advertised `apiVersion`, so it must be listed
      // here too.
      {VK_KHR_SHADER_TERMINATE_INVOCATION_EXTENSION_NAME,
       VK_KHR_SHADER_TERMINATE_INVOCATION_SPEC_VERSION},
      // (roadmap E13) A `zero_initializer`'d SPIR-V `Workgroup` variable's
      // groupshared buffer is zeroed once per group
      // (GroupShared.h/EntryWrapper.cpp); like
      // `shader_terminate_invocation` above,
      // `dEQP-VK.compute.pipeline.zero_initialize_workgroup_memory.*`
      // enables this extension by name regardless of the advertised
      // `apiVersion`, so it must be listed here too.
      {VK_KHR_ZERO_INITIALIZE_WORKGROUP_MEMORY_EXTENSION_NAME,
       VK_KHR_ZERO_INITIALIZE_WORKGROUP_MEMORY_SPEC_VERSION},
      // (roadmap E14) `VkWriteDescriptorSetInlineUniformBlock` and a
      // per-binding byte-blob descriptor storage (Descriptor.{h,cpp}) are
      // implemented; unlike `zero_initialize_workgroup_memory` above, no
      // CTS case is known to enable this one by name outside apiVersion
      // 1.3 -- it is listed for the same reason every other row here is:
      // this ICD genuinely implements what it declares.
      {VK_EXT_INLINE_UNIFORM_BLOCK_EXTENSION_NAME,
       VK_EXT_INLINE_UNIFORM_BLOCK_SPEC_VERSION},
      // (roadmap E18) `storageTexelBufferOffsetAlignmentBytes`/
      // `uniformTexelBufferOffsetAlignmentBytes` (and their
      // `SingleTexelAlignment` companions) are now real, non-placeholder
      // values, agreeing with the dedicated
      // `VkPhysicalDeviceTexelBufferAlignmentProperties` case
      // (EntryPoints.cpp); like `inline_uniform_block` above, no CTS case
      // is known to enable this one by name outside apiVersion 1.3 -- it
      // is listed for the same reason every other row here is: this ICD
      // genuinely implements what it declares.
      {VK_EXT_TEXEL_BUFFER_ALIGNMENT_EXTENSION_NAME,
       VK_EXT_TEXEL_BUFFER_ALIGNMENT_SPEC_VERSION},
      // (roadmap E19) `VK_FORMAT_A4R4G4B4_UNORM_PACK16`/
      // `A4B4G4R4_UNORM_PACK16` (Format.cpp) are both recognized `VkFormat`
      // values; like `inline_uniform_block`/`texel_buffer_alignment`
      // above, no CTS case is known to enable this one by name outside
      // apiVersion 1.3 -- it is listed for the same reason every other row
      // here is: this ICD genuinely implements what it declares.
      {VK_EXT_4444_FORMATS_EXTENSION_NAME, VK_EXT_4444_FORMATS_SPEC_VERSION},
      // (roadmap E19) `VkPipelineCreationFeedbackCreateInfo`, chained onto
      // `vkCreateGraphicsPipelines`/`vkCreateComputePipelines`, is now
      // filled (Pipeline.cpp's `fillPipelineCreationFeedback`); like
      // `4444_formats` above, no CTS case is known to enable this one by
      // name outside apiVersion 1.3.
      {VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME,
       VK_EXT_PIPELINE_CREATION_FEEDBACK_SPEC_VERSION},
      // (roadmap E19) `VK_KHR_shader_non_semantic_info` adds no new
      // opcode of its own to convert: `SPIRVImporter.cpp`'s
      // `stripNonSemanticExtInst` strips every `NonSemantic.*` `OpExtInst`
      // out of the binary before MLIR's SPIR-V deserializer (which has no
      // case for such a set name at all) ever sees it, honoring the SPIR-V
      // specification's own "instructions with no semantic effect may be
      // ignored" contract for the whole family this extension names, not
      // one opcode at a time.
      {VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,
       VK_KHR_SHADER_NON_SEMANTIC_INFO_SPEC_VERSION},
      // (roadmap E19) `vkGetPhysicalDeviceToolProperties` (EntryPoints.cpp)
      // truthfully reports zero tools: this ICD is not itself a layer or
      // debugging tool, and wraps no such tool internally.
      {VK_EXT_TOOLING_INFO_EXTENSION_NAME, VK_EXT_TOOLING_INFO_SPEC_VERSION},
      // (roadmap F1) `VkDeviceQueueGlobalPriorityCreateInfo`'s `globalPriority`
      // hint (EntryPoints.cpp's `vkCreateDevice`) and `VkQueueFamilyGlobal
      // PriorityProperties` (`vkGetPhysicalDeviceQueueFamilyProperties2`)
      // are both implemented; like `maintenance5`/`maintenance6` above,
      // `dEQP-VK.api.device_init.create_device_with_*_global_priority_khr`
      // and the global-priority-query cases enable this extension by name
      // regardless of the advertised `apiVersion`, so it must be listed
      // here too.
      {VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME,
       VK_KHR_GLOBAL_PRIORITY_SPEC_VERSION},
      // (roadmap F2) `spirv.GroupNonUniformRotateKHR` now converts
      // (SPIRVToLLVMPatterns.cpp's `RotateConversionPattern`); like
      // `VK_KHR_global_priority` above,
      // `dEQP-VK.subgroups.rotate.*`/`dEQP-VK.subgroups.clustered_rotate.*`
      // enable this extension by name regardless of the advertised
      // `apiVersion`, so it must be listed here too.
      {VK_KHR_SHADER_SUBGROUP_ROTATE_EXTENSION_NAME,
       VK_KHR_SHADER_SUBGROUP_ROTATE_SPEC_VERSION},
      // (roadmap F4) `spirv.KHR.AssumeTrue`/`spirv.KHR.Expect` now convert
      // (SPIRVToLLVMPatterns.cpp's `AssumeTrueConversionPattern`/
      // `ExpectConversionPattern`); like `VK_KHR_shader_subgroup_rotate`
      // above, a real shader using either op declares `OpCapability
      // ExpectAssumeKHR`/`OpExtension "SPV_KHR_expect_assume"`, so this
      // extension must be listed here too.
      {VK_KHR_SHADER_EXPECT_ASSUME_EXTENSION_NAME,
       VK_KHR_SHADER_EXPECT_ASSUME_SPEC_VERSION},
      // (roadmap F5) `rectangularLines`/`bresenhamLines`/`smoothLines` and
      // their three `stippled*` variants are all implemented
      // (`feme::graphics::RasterState::LineMode`/`StippledLineEnable`,
      // `GraphicsPipeline.cpp`'s `VkPipelineRasterizationLineStateCreate
      // InfoKHR` translation, `vkCmdSetLineStippleKHR`); like
      // `VK_KHR_shader_subgroup_rotate`/`VK_KHR_shader_expect_assume`
      // above, `dEQP-VK.pipeline.line_rasterization.*` enables this
      // extension by name regardless of the advertised `apiVersion`, so
      // it must be listed here too.
      {VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME,
       VK_KHR_LINE_RASTERIZATION_SPEC_VERSION},
      // (roadmap F6) `VkPipelineVertexInputDivisorStateCreateInfo`'s
      // per-binding instance-rate divisor is implemented
      // (`GraphicsPipeline.cpp`'s `translateVertexInput`, the executor's
      // fetch-index formula in `Executor.cpp`); like
      // `VK_KHR_shader_subgroup_rotate`/`VK_KHR_shader_expect_assume`/
      // `VK_KHR_line_rasterization` above, `dEQP-VK.pipeline.vertex_input.
      // instance_rate_divisor.*` enables this extension by name regardless
      // of the advertised `apiVersion`, so it must be listed here too.
      {VK_KHR_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,
       VK_KHR_VERTEX_ATTRIBUTE_DIVISOR_SPEC_VERSION},
      // (roadmap F7) `vkCmdBindIndexBuffer`'s index read (CommandBuffer.cpp)
      // and the executor's fetch (Executor.cpp) both gained an 8-bit case;
      // like `VK_KHR_shader_subgroup_rotate`/`VK_KHR_shader_expect_assume`/
      // `VK_KHR_line_rasterization`/`VK_KHR_vertex_attribute_divisor` above,
      // `dEQP-VK.pipeline.monolithic.index_type_uint8.*`/
      // `dEQP-VK.api.info.*.index_type_uint8*` enable this extension by
      // name regardless of the advertised `apiVersion`, so it must be
      // listed here too.
      {VK_KHR_INDEX_TYPE_UINT8_EXTENSION_NAME,
       VK_KHR_INDEX_TYPE_UINT8_SPEC_VERSION},
      // (roadmap F8/F8a) `vkCmdSetRenderingAttachmentLocations`/
      // `vkCmdSetRenderingInputAttachmentIndices` are implemented
      // (CommandBuffer.cpp), and a fragment shader's `subpassInput` local
      // read now produces real pixels too (SPIRVToLLVMPatterns.cpp's
      // `SubpassLoadPattern`, FragmentWrapper.cpp's
      // `lowerFragmentSubpassLoad`); like every other post-`maintenance5`
      // entry above, `dEQP-VK.renderpasses.dynamic_rendering.*.local_read.*`
      // enables this extension by name regardless of the advertised
      // `apiVersion`, so it must be listed here too.
      {VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME,
       VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_SPEC_VERSION},
      // (roadmap F9) `vkCmdBindPipeline` (CommandBuffer.cpp) honors
      // `VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT`/`VK_PIPELINE_CREATE_
      // NO_PROTECTED_ACCESS_BIT`; like every other post-`maintenance5` entry
      // above, this extension must be listed here too regardless of the
      // advertised `apiVersion`.
      {VK_EXT_PIPELINE_PROTECTED_ACCESS_EXTENSION_NAME,
       VK_EXT_PIPELINE_PROTECTED_ACCESS_SPEC_VERSION},
      // (roadmap F10) `VkPipelineRobustnessCreateInfo` is accepted and
      // validated at both compute and graphics pipeline creation
      // (`Pipeline.cpp`'s `resolvePipelineRobustness`); like every other
      // post-`maintenance5` entry above, this extension must be listed
      // here too regardless of the advertised `apiVersion`.
      {VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME,
       VK_EXT_PIPELINE_ROBUSTNESS_SPEC_VERSION},
      // (roadmap F11) `vkCopyMemoryToImage`/`vkCopyImageToMemory`/
      // `vkCopyImageToImage`/`vkTransitionImageLayout` (HostImageCopy.cpp)
      // copy/transition images without a command buffer; like every other
      // post-`maintenance5` entry above, this extension must be listed
      // here too regardless of the advertised `apiVersion`.
      {VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME,
       VK_EXT_HOST_IMAGE_COPY_SPEC_VERSION},
      // (roadmap F12) `vkCmdPushDescriptorSet`/`vkCmdPushDescriptorSetWith
      // Template` (CommandBuffer.cpp) write descriptors directly into a
      // command buffer's own recorded state, with no `VkDescriptorSet`
      // object at all, reusing `Descriptor.{h,cpp}`'s existing
      // binding-to-heap-slot translation; like every other
      // post-`maintenance5` entry above, this extension must be listed
      // here too regardless of the advertised `apiVersion`.
      {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
       VK_KHR_PUSH_DESCRIPTOR_SPEC_VERSION},
      // (roadmap F13) `VK_ATTACHMENT_LOAD_OP_NONE`/`STORE_OP_NONE` need no
      // new code of their own: `applyClear`'s existing `LoadOp !=
      // VK_ATTACHMENT_LOAD_OP_CLEAR` check (CommandBuffer.cpp) already
      // treats any non-`CLEAR` load op, `NONE` included, as "do nothing",
      // and `StoreOp` is never read to act on at all -- this ICD writes
      // straight into an attachment's real memory, so `STORE`/`DONT_CARE`/
      // `NONE` are indistinguishable outcomes with no discard-on-store
      // optimization to skip. Like every other post-`maintenance5` entry
      // above, this extension must be listed here too regardless of the
      // advertised `apiVersion`.
      {VK_KHR_LOAD_STORE_OP_NONE_EXTENSION_NAME,
       VK_KHR_LOAD_STORE_OP_NONE_SPEC_VERSION},
      // (roadmap F14) `vkMapMemory2`/`vkUnmapMemory2` (Memory.cpp) are
      // implemented; unlike most post-`maintenance5` entries above, these
      // two commands are already core, non-`KHR`-suffixed `VK_VERSION_1_4`
      // entries `vk_gen_entrypoints.py`'s `CORE_FEATURES` resolves, but
      // `vktMemoryMappingTests.cpp`'s own
      // `requireDeviceFunctionality("VK_KHR_map_memory2")` still enables
      // this extension by name regardless of the advertised `apiVersion`,
      // so it must be listed here too, or every one of those cases fails
      // `NotSupported` instead of running for real.
      {VK_KHR_MAP_MEMORY_2_EXTENSION_NAME, VK_KHR_MAP_MEMORY_2_SPEC_VERSION},
      // (roadmap H2) Layered rendering/multiview: `vkCreateFramebuffer`
      // accepts `layers > 1` and `vkCreateRenderPass`/`vkCreateRenderPass2`
      // accept a nonzero `viewMask` (`VkRenderPassMultiviewCreateInfo`/
      // `VkRenderPassCreateInfo2`, RenderPass.cpp), each set view bit
      // running the bound pipeline once and writing that bit's own
      // attachment array layer (`CommandBuffer.cpp`'s `runDraw`), with
      // `gl_ViewIndex` readable from either stage
      // (`SignatureSystemValue::ViewIndex`). `dEQP-VK.multiview`'s own
      // cases enable this extension by name regardless of the advertised
      // `apiVersion`, so it must be listed here too.
      {VK_KHR_MULTIVIEW_EXTENSION_NAME, VK_KHR_MULTIVIEW_SPEC_VERSION},
  };
  return Extensions;
}
