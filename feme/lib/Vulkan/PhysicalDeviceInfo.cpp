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

  VkPhysicalDeviceProperties &Props = Info.Properties;
  // Illustrative per "Loader Integration": "The exact advertised API version
  // is selected during implementation from the core command and CTS
  // coverage actually achieved." Development checkpoint version, not a claim
  // of Vulkan 1.2 conformance (see "Initial Non-Goals": zero
  // `VkConformanceVersion`). V3 bumped this from 1.1 to 1.2 for
  // `vkWaitSemaphores`/`vkSignalSemaphore`/`vkGetSemaphoreCounterValue`'s
  // core (non-`KHR`) names -- see vk_gen_entrypoints.py's `CORE_FEATURES`
  // comment.
  Props.apiVersion = VK_API_VERSION_1_2;
  Props.driverVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
  Props.vendorID = FeMeVendorID;
  Props.deviceID = FeMeDeviceID;
  Props.deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
  std::strncpy(Props.deviceName, "FeMe CPU Vulkan Device",
               sizeof(Props.deviceName) - 1);
  fillUUID(Props.pipelineCacheUUID, "pipeline-cache-uuid", Info.SubgroupSize);
  fillUUID(Info.DeviceUUID, "device-uuid", Info.SubgroupSize);

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
  Limits.subPixelPrecisionBits = 4;
  Limits.subTexelPrecisionBits = 4;
  Limits.mipmapPrecisionBits = 4;
  Limits.maxDrawIndexedIndexValue = (1u << 24) - 1;
  Limits.maxDrawIndirectCount = 1;
  Limits.maxSamplerLodBias = 2.0f;
  Limits.maxSamplerAnisotropy = 1.0f;
  Limits.maxViewports = 1;
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
  // likewise honest -- `largePoints`/`wideLines` are left `VK_FALSE`
  // (unlike `dualSrcBlend`, the executor's point/line quad expansion
  // hardcodes a fixed 1-pixel size/width rather than reading a
  // `SV_PointSize` output or `vkCmdSetLineWidth` value, see Executor.cpp's
  // own comment).
  Info.Features = VkPhysicalDeviceFeatures{};
  Info.Features.robustBufferAccess = VK_TRUE;
  Info.Features.dualSrcBlend = VK_TRUE;

  VkPhysicalDeviceMemoryProperties &MemProps = Info.MemoryProperties;
  MemProps.memoryTypeCount = 1;
  MemProps.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                          VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  MemProps.memoryTypes[0].heapIndex = 0;
  MemProps.memoryHeapCount = 1;
  MemProps.memoryHeaps[0].size = detectHostMemorySize();
  MemProps.memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

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
  };
  return Extensions;
}
