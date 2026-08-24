//===- EntryPoints.cpp - Vulkan command implementations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EntryPoints.h"
#include "Format.h"
#include "Icd.h"
#include "Image.h"
#include "Objects.h"
#include "ProcAddr.h"

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <cstring>

using namespace feme::vulkan;

namespace {

/// Implements the standard Vulkan "enumerate" pattern shared by every
/// `vkEnumerate*`/`vkGetPhysicalDevice*QueueFamilyProperties*` command: a
/// null `pProperties` reports the true count; a non-null one copies up to
/// `*pPropertyCount` entries and reports `VK_INCOMPLETE` if there were more.
template <typename T>
VkResult enumerate(uint32_t TrueCount, const T *Source, uint32_t *pCount,
                   T *pOut) {
  if (!pOut) {
    *pCount = TrueCount;
    return VK_SUCCESS;
  }
  uint32_t ToCopy = *pCount < TrueCount ? *pCount : TrueCount;
  for (uint32_t I = 0; I < ToCopy; ++I)
    pOut[I] = Source[I];
  *pCount = ToCopy;
  return ToCopy < TrueCount ? VK_INCOMPLETE : VK_SUCCESS;
}

} // namespace

VKAPI_ATTR VkResult VKAPI_CALL feme::vulkan::vkCreateInstance(
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
  // V0 implements no instance extension (see "Loader Integration": "The
  // driver reports no device extension merely because Vulkan-Headers
  // declares it" -- the same rule applies at the instance level).
  if (pCreateInfo->enabledExtensionCount > 0)
    return VK_ERROR_EXTENSION_NOT_PRESENT;

  Allocator Alloc(pAllocator);
  Instance *Obj =
      Alloc.create<Instance>(VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE, Alloc);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pInstance = toHandle<VkInstance>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkDestroyInstance(
    VkInstance instance, const VkAllocationCallbacks *pAllocator) {
  (void)pAllocator; // The instance's own allocator (recorded at creation) is
                    // authoritative, matching Vulkan's free-with-the-same-
                    // callbacks-used-to-allocate convention.
  if (!instance)
    return;
  Instance *Obj = fromHandle<Instance>(instance);
  Allocator Alloc = Obj->getAllocator();
  Alloc.destroy(Obj);
}

VKAPI_ATTR VkResult VKAPI_CALL
feme::vulkan::vkEnumerateInstanceVersion(uint32_t *pApiVersion) {
  // Roadmap D0 bumped this from 1.2 to 1.4 (see Roadmap.md §1.9.2). This
  // ICD still implements only a growing subset of 1.4's full mandatory
  // surface, exactly as it already did advertising 1.1 and then 1.2 --
  // VulkanCTSReport.md's "Roadmap D0: measured impact" records what
  // advertising the higher version costs against a stock CTS.
  *pApiVersion = VK_API_VERSION_1_4;
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
feme::vulkan::vkEnumerateInstanceExtensionProperties(
    const char *pLayerName, uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties) {
  if (pLayerName)
    return VK_ERROR_LAYER_NOT_PRESENT;
  llvm::ArrayRef<VkExtensionProperties> Extensions =
      getSupportedDeviceExtensions();
  return enumerate<VkExtensionProperties>(
      static_cast<uint32_t>(Extensions.size()), Extensions.data(),
      pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL feme::vulkan::vkEnumerateInstanceLayerProperties(
    uint32_t *pPropertyCount, VkLayerProperties *pProperties) {
  return enumerate<VkLayerProperties>(0, nullptr, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL feme::vulkan::vkEnumeratePhysicalDevices(
    VkInstance instance, uint32_t *pPhysicalDeviceCount,
    VkPhysicalDevice *pPhysicalDevices) {
  Instance *Obj = fromHandle<Instance>(instance);
  VkPhysicalDevice Handle =
      toHandle<VkPhysicalDevice>(&Obj->getPhysicalDevice());
  return enumerate<VkPhysicalDevice>(1, &Handle, pPhysicalDeviceCount,
                                     pPhysicalDevices);
}

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties) {
  *pProperties =
      fromHandle<PhysicalDevice>(physicalDevice)->getInfo().Properties;
}

namespace {

void fillDriverProperties(const PhysicalDeviceInfo &Info,
                          VkPhysicalDeviceDriverProperties &Props) {
  Props.driverID = Info.DriverId;
  Props.conformanceVersion = Info.ConformanceVersion;
  std::memcpy(Props.driverName, Info.DriverName, sizeof(Info.DriverName));
  std::memcpy(Props.driverInfo, Info.DriverInfo, sizeof(Info.DriverInfo));
}

/// Fills every Vulkan 1.1 core `pNext` extension struct this ICD recognizes
/// in a `VkPhysicalDeviceProperties2`/`Features2` chain. An application must
/// not chain a struct for a feature/extension it didn't enable, so any
/// unrecognized `sType` is left untouched, per the Vulkan specification.
void fillProperties2Chain(const PhysicalDeviceInfo &Info, void *pNext) {
  for (auto *Base = static_cast<VkBaseOutStructure *>(pNext); Base;
       Base = Base->pNext) {
    switch (Base->sType) {
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
      auto *Subgroup =
          reinterpret_cast<VkPhysicalDeviceSubgroupProperties *>(Base);
      Subgroup->subgroupSize = Info.SubgroupSize;
      Subgroup->supportedStages = Info.SubgroupSupportedStages;
      Subgroup->supportedOperations = Info.SubgroupSupportedOperations;
      Subgroup->quadOperationsInAllStages = VK_FALSE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES: {
      auto *Props11 =
          reinterpret_cast<VkPhysicalDeviceVulkan11Properties *>(Base);
      // (roadmap C6) The promoted twin of
      // `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES` below; both must
      // agree (`dEQP-VK.api.info.vulkan1p2.property_extensions_consistency`).
      std::memcpy(Props11->deviceUUID, Info.DeviceUUID, VK_UUID_SIZE);
      std::memcpy(Props11->driverUUID, Info.Properties.pipelineCacheUUID,
                  VK_UUID_SIZE);
      std::memset(Props11->deviceLUID, 0, VK_LUID_SIZE);
      Props11->deviceLUIDValid = VK_FALSE;
      Props11->deviceNodeMask = 1;
      Props11->subgroupSize = Info.SubgroupSize;
      Props11->subgroupSupportedStages = Info.SubgroupSupportedStages;
      Props11->subgroupSupportedOperations = Info.SubgroupSupportedOperations;
      Props11->subgroupQuadOperationsInAllStages = VK_FALSE;
      // (roadmap C6) `multiview` itself is not advertised (layered
      // rendering is V7), so these two are set to their required minimum
      // rather than a real capability -- see PhysicalDeviceInfo.h's field
      // comment. `maxMemoryAllocationSize`/`maxPerSetDescriptors` are
      // `VK_KHR_maintenance3`'s own fields, promoted here unchanged; see
      // the `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES`
      // case below for the dedicated-struct twin these must agree with.
      Props11->maxMultiviewViewCount = Info.MaxMultiviewViewCount;
      Props11->maxMultiviewInstanceIndex = Info.MaxMultiviewInstanceIndex;
      Props11->maxMemoryAllocationSize = Info.MaxMemoryAllocationSize;
      Props11->maxPerSetDescriptors = Info.MaxPerSetDescriptors;
      // (roadmap C6) Explicitly written, not merely left at whatever the
      // caller's own buffer held: `dEQP-VK.api.info.vulkan1p2.properties`
      // fills every promoted-struct buffer with a guard pattern before the
      // call and fails if any field the offset table lists is
      // unmodified. `pointClippingBehavior`'s default (`0`, "all clip
      // planes") is already correct for a device advertising no user clip
      // planes; `protectedNoFault` is false since protected memory is not
      // advertised.
      Props11->pointClippingBehavior =
          VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
      Props11->protectedNoFault = VK_FALSE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES: {
      auto *Multiview =
          reinterpret_cast<VkPhysicalDeviceMultiviewProperties *>(Base);
      Multiview->maxMultiviewViewCount = Info.MaxMultiviewViewCount;
      Multiview->maxMultiviewInstanceIndex = Info.MaxMultiviewInstanceIndex;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: {
      auto *Maintenance3 =
          reinterpret_cast<VkPhysicalDeviceMaintenance3Properties *>(Base);
      Maintenance3->maxPerSetDescriptors = Info.MaxPerSetDescriptors;
      Maintenance3->maxMemoryAllocationSize = Info.MaxMemoryAllocationSize;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES: {
      auto *TimelineSemaphore =
          reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreProperties *>(Base);
      TimelineSemaphore->maxTimelineSemaphoreValueDifference =
          Info.MaxTimelineSemaphoreValueDifference;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
      auto *IdProps = reinterpret_cast<VkPhysicalDeviceIDProperties *>(Base);
      std::memcpy(IdProps->deviceUUID, Info.DeviceUUID, VK_UUID_SIZE);
      std::memcpy(IdProps->driverUUID, Info.Properties.pipelineCacheUUID,
                  VK_UUID_SIZE);
      // Only meaningful when `deviceLUIDValid` is true (it is not), but
      // still explicitly zeroed rather than left as whatever the caller's
      // own buffer held -- see the guard-pattern comment on
      // `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES` above.
      std::memset(IdProps->deviceLUID, 0, VK_LUID_SIZE);
      IdProps->deviceLUIDValid = VK_FALSE;
      IdProps->deviceNodeMask = 1;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES: {
      // (roadmap C6) The promoted twin of `Vulkan11Properties.protectedNoFault`
      // above; both must agree (protected memory is not advertised).
      auto *ProtectedMemory =
          reinterpret_cast<VkPhysicalDeviceProtectedMemoryProperties *>(Base);
      ProtectedMemory->protectedNoFault = VK_FALSE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES: {
      auto *DriverProps =
          reinterpret_cast<VkPhysicalDeviceDriverProperties *>(Base);
      fillDriverProperties(Info, *DriverProps);
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES: {
      // (roadmap C6) The promoted twin of
      // `Vulkan11Properties.pointClippingBehavior` above; both must agree.
      auto *PointClipping =
          reinterpret_cast<VkPhysicalDevicePointClippingProperties *>(Base);
      PointClipping->pointClippingBehavior =
          VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES: {
      // (roadmap C6, F3, F15a, F15b) The promoted twin of the float-controls
      // half of `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES`
      // below; both must agree. `DenormPreserve`/`RoundingModeRTE`/
      // `SignedZeroInfNanPreserve` execution modes are honored by
      // construction (see `ExecutionModePattern`/`collectEntryPoints` in
      // feme/lib/Conversion/SPIRVToLLVM), and `FloatControlArithmeticPattern`
      // (SPIRVToLLVMPatterns.cpp) now actually produces truncating-rounding-
      // mode code for `RoundingModeRTZ` and flushed-denormal code for
      // `DenormFlushToZero`, at every width, rather than only diagnosing (or,
      // for `DenormFlushToZero`, rejecting outright) a shader that asks for
      // either -- but every field here stays conservatively `VK_FALSE`,
      // `RoundingModeRTZ`'s three included: a targeted CTS run flipping them
      // to `VK_TRUE` found every
      // `dEQP-VK.spirv_assembly.instruction.compute.float_controls.fp32.*`
      // case that reaches pipeline creation (i.e. is not already
      // `NotSupported` on an unrelated missing feature) fails there instead
      // of passing, on a completely unrelated, pre-existing gap:
      // `feme::cpu`'s resource-lowering cannot yet raise the small,
      // 2-element runtime-sized storage-buffer bindings these generated
      // shaders declare ("register-bound resource handle ... cannot
      // normalize into a heap access or the root-constant block") --
      // nothing to do with float controls at all. Advertising `VK_TRUE`
      // would trade a graceful `NotSupported` skip for an outright CTS
      // `Fail` until that gap closes, so every field remains `VK_FALSE`
      // pending it; see agent_thoughts.md's F15a and F15b entries.
      auto *FloatControls =
          reinterpret_cast<VkPhysicalDeviceFloatControlsProperties *>(Base);
      FloatControls->denormBehaviorIndependence =
          VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
      FloatControls->roundingModeIndependence =
          VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
      FloatControls->shaderSignedZeroInfNanPreserveFloat16 = VK_FALSE;
      FloatControls->shaderSignedZeroInfNanPreserveFloat32 = VK_FALSE;
      FloatControls->shaderSignedZeroInfNanPreserveFloat64 = VK_FALSE;
      FloatControls->shaderDenormPreserveFloat16 = VK_FALSE;
      FloatControls->shaderDenormPreserveFloat32 = VK_FALSE;
      FloatControls->shaderDenormPreserveFloat64 = VK_FALSE;
      FloatControls->shaderDenormFlushToZeroFloat16 = VK_FALSE;
      FloatControls->shaderDenormFlushToZeroFloat32 = VK_FALSE;
      FloatControls->shaderDenormFlushToZeroFloat64 = VK_FALSE;
      FloatControls->shaderRoundingModeRTEFloat16 = VK_FALSE;
      FloatControls->shaderRoundingModeRTEFloat32 = VK_FALSE;
      FloatControls->shaderRoundingModeRTEFloat64 = VK_FALSE;
      FloatControls->shaderRoundingModeRTZFloat16 = VK_FALSE;
      FloatControls->shaderRoundingModeRTZFloat32 = VK_FALSE;
      FloatControls->shaderRoundingModeRTZFloat64 = VK_FALSE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES: {
      // (roadmap C6) The promoted twin of the resolve-mode half of
      // `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES` below;
      // both must agree -- `VK_KHR_depth_stencil_resolve` itself is not
      // implemented (only the ordinary color-attachment resolve this ICD
      // already supports is), so `VK_RESOLVE_MODE_NONE` is the honest
      // value rather than the `SAMPLE_ZERO` bit a real implementation
      // would need.
      auto *DepthStencilResolve =
          reinterpret_cast<VkPhysicalDeviceDepthStencilResolveProperties *>(
              Base);
      DepthStencilResolve->supportedDepthResolveModes = VK_RESOLVE_MODE_NONE;
      DepthStencilResolve->supportedStencilResolveModes = VK_RESOLVE_MODE_NONE;
      DepthStencilResolve->independentResolveNone = VK_FALSE;
      DepthStencilResolve->independentResolve = VK_FALSE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES: {
      auto *Props12 =
          reinterpret_cast<VkPhysicalDeviceVulkan12Properties *>(Base);
      Props12->driverID = Info.DriverId;
      Props12->conformanceVersion = Info.ConformanceVersion;
      std::memcpy(Props12->driverName, Info.DriverName,
                  sizeof(Info.DriverName));
      std::memcpy(Props12->driverInfo, Info.DriverInfo,
                  sizeof(Info.DriverInfo));
      // (roadmap C6) Every remaining field is written explicitly, for the
      // same guard-pattern reason `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_
      // VULKAN_1_2_FEATURES` documents. None of these reflect a real
      // capability this ICD implements (no explicit SPIR-V float-controls
      // handling, no `VK_EXT_descriptor_indexing`, no
      // `VK_KHR_depth_stencil_resolve`), so every one is the conservative
      // "least capable, always-safe" value rather than an aspirational
      // one -- `VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE` (every bit
      // width must share the same float-controls execution mode) and
      // `VK_RESOLVE_MODE_NONE` (no resolve mode beyond the ordinary color
      // resolve this ICD already implements) rather than the `SAMPLE_ZERO`
      // bit a real `VK_KHR_depth_stencil_resolve` implementation would
      // need to support. See `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_
      // FLOAT_CONTROLS_PROPERTIES` above's comment for why
      // `shaderRoundingModeRTZFloat{16,32,64}`/
      // `shaderDenormFlushToZeroFloat{16,32,64}` stay `VK_FALSE` too, despite
      // roadmap F15a/F15b's `FloatControlArithmeticPattern` now genuinely
      // producing that code.
      Props12->denormBehaviorIndependence =
          VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
      Props12->roundingModeIndependence =
          VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
      Props12->shaderSignedZeroInfNanPreserveFloat16 = VK_FALSE;
      Props12->shaderSignedZeroInfNanPreserveFloat32 = VK_FALSE;
      Props12->shaderSignedZeroInfNanPreserveFloat64 = VK_FALSE;
      Props12->shaderDenormPreserveFloat16 = VK_FALSE;
      Props12->shaderDenormPreserveFloat32 = VK_FALSE;
      Props12->shaderDenormPreserveFloat64 = VK_FALSE;
      Props12->shaderDenormFlushToZeroFloat16 = VK_FALSE;
      Props12->shaderDenormFlushToZeroFloat32 = VK_FALSE;
      Props12->shaderDenormFlushToZeroFloat64 = VK_FALSE;
      Props12->shaderRoundingModeRTEFloat16 = VK_FALSE;
      Props12->shaderRoundingModeRTEFloat32 = VK_FALSE;
      Props12->shaderRoundingModeRTEFloat64 = VK_FALSE;
      Props12->shaderRoundingModeRTZFloat16 = VK_FALSE;
      Props12->shaderRoundingModeRTZFloat32 = VK_FALSE;
      Props12->shaderRoundingModeRTZFloat64 = VK_FALSE;
      Props12->maxUpdateAfterBindDescriptorsInAllPools = 0;
      Props12->shaderUniformBufferArrayNonUniformIndexingNative = VK_FALSE;
      Props12->shaderSampledImageArrayNonUniformIndexingNative = VK_FALSE;
      Props12->shaderStorageBufferArrayNonUniformIndexingNative = VK_FALSE;
      Props12->shaderStorageImageArrayNonUniformIndexingNative = VK_FALSE;
      Props12->shaderInputAttachmentArrayNonUniformIndexingNative = VK_FALSE;
      Props12->robustBufferAccessUpdateAfterBind = VK_FALSE;
      Props12->quadDivergentImplicitLod = VK_FALSE;
      Props12->maxPerStageDescriptorUpdateAfterBindSamplers = 0;
      Props12->maxPerStageDescriptorUpdateAfterBindUniformBuffers = 0;
      Props12->maxPerStageDescriptorUpdateAfterBindStorageBuffers = 0;
      Props12->maxPerStageDescriptorUpdateAfterBindSampledImages = 0;
      Props12->maxPerStageDescriptorUpdateAfterBindStorageImages = 0;
      Props12->maxPerStageDescriptorUpdateAfterBindInputAttachments = 0;
      Props12->maxPerStageUpdateAfterBindResources = 0;
      Props12->maxDescriptorSetUpdateAfterBindSamplers = 0;
      Props12->maxDescriptorSetUpdateAfterBindUniformBuffers = 0;
      Props12->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = 0;
      Props12->maxDescriptorSetUpdateAfterBindStorageBuffers = 0;
      Props12->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 0;
      Props12->maxDescriptorSetUpdateAfterBindSampledImages = 0;
      Props12->maxDescriptorSetUpdateAfterBindStorageImages = 0;
      Props12->maxDescriptorSetUpdateAfterBindInputAttachments = 0;
      Props12->supportedDepthResolveModes = VK_RESOLVE_MODE_NONE;
      Props12->supportedStencilResolveModes = VK_RESOLVE_MODE_NONE;
      Props12->independentResolveNone = VK_FALSE;
      Props12->independentResolve = VK_FALSE;
      Props12->filterMinmaxSingleComponentFormats = VK_FALSE;
      Props12->filterMinmaxImageComponentMapping = VK_FALSE;
      // (roadmap C6) The promoted twin of
      // `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES`
      // above; both must agree; see PhysicalDeviceInfo.h's field comment
      // for why this is `UINT64_MAX` rather than the spec's `2^31-1` floor.
      Props12->maxTimelineSemaphoreValueDifference =
          Info.MaxTimelineSemaphoreValueDifference;
      // (roadmap C6) Required unconditionally once apiVersion >= 1.2
      // (`dEQP-VK.api.info.vulkan1p2_limits_validation.general`), and
      // honest at the minimum: no `VK_FORMAT_*_UINT`/`_SINT` color format
      // is an accepted color-attachment format at all yet
      // (`isSupportedColorAttachmentFormat`, `RenderPass.cpp`), so there is
      // no multisample integer color attachment this ICD could claim
      // beyond the trivial single-sample case.
      Props12->framebufferIntegerColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
      break;
    }
    // (roadmap E2) The aggregate `VkPhysicalDeviceVulkan13Properties`
    // struct: every one of its 46 limit fields is written explicitly, for
    // the same guard-pattern reason `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_
    // VULKAN_1_2_PROPERTIES` above documents. Every field here is a
    // conservative, honest `0`/`VK_FALSE`: `dEQP-VK.api.info.vulkan1p3.
    // property_extensions_consistency` cross-checks *each one* of them
    // against its own pre-promotion, per-extension dedicated struct (e.g.
    // `VkPhysicalDeviceSubgroupSizeControlProperties`,
    // `VkPhysicalDeviceTexelBufferAlignmentProperties`,
    // `VkPhysicalDeviceMaintenance4Properties`) once apiVersion >= 1.3,
    // the same way `dEQP-VK.api.info.vulkan1p3.feature_extensions_
    // consistency` cross-checked `dynamicRendering` before E1. None of
    // those dedicated structs has a `vkGetPhysicalDeviceProperties2` case
    // of its own yet, so they all still read as zero -- claiming a real,
    // nonzero value here (e.g. this ICD's actual `minTexelBufferOffset
    // Alignment` for `storageTexelBufferOffsetAlignmentBytes`) would
    // *disagree* with that zero and regress a currently-passing
    // consistency case rather than close one. Each field's own later row
    // (E7's `subgroupSizeControl`, E8's `shaderIntegerDotProduct`, E18's
    // `VK_EXT_texel_buffer_alignment`, E4's `VK_KHR_maintenance4`, ...) is
    // therefore the one that gets to raise it, in lockstep with adding
    // that row's own dedicated-struct case so the two stay honestly in
    // sync -- exactly how `dynamicRendering`'s dedicated
    // `VkPhysicalDeviceDynamicRenderingFeatures` case and the aggregate
    // `VkPhysicalDeviceVulkan13Features` case above agree today.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES: {
      auto *Props13 =
          reinterpret_cast<VkPhysicalDeviceVulkan13Properties *>(Base);
      // (roadmap E7) `subgroupSizeControl` is implemented (Pipeline.cpp's
      // `compileComputePipeline` honors a chained
      // `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo`), so these
      // four agree with the dedicated
      // `VkPhysicalDeviceSubgroupSizeControlProperties` case below, exactly
      // like `maxCombinedImageSamplerDescriptorCount`/E6 already does for
      // its own field.
      Props13->minSubgroupSize = Info.MinSubgroupSize;
      Props13->maxSubgroupSize = Info.MaxSubgroupSize;
      Props13->maxComputeWorkgroupSubgroups = Info.MaxComputeWorkgroupSubgroups;
      Props13->requiredSubgroupSizeStages = Info.RequiredSubgroupSizeStages;
      // (roadmap E14) `inlineUniformBlock` is implemented (Descriptor.{h,cpp}'s
      // byte-blob descriptor storage); these six limits agree with the
      // dedicated `VkPhysicalDeviceInlineUniformBlockProperties` case
      // below, exactly like `subgroupSizeControl`'s four fields above. The
      // two `UpdateAfterBind` variants equal their non-`UpdateAfterBind`
      // counterparts (rather than `0`) even though
      // `descriptorBindingInlineUniformBlockUpdateAfterBind` stays
      // `VK_FALSE` below: per spec, both are required limits independent
      // of that feature bit --
      // `dEQP-VK.api.info.vulkan1p2_limits_validation.ext_inline_uniform_
      // block` enforces the same `>= 4` floor on them unconditionally
      // once this extension is advertised, `0` or not.
      Props13->maxInlineUniformBlockSize = Info.MaxInlineUniformBlockSize;
      Props13->maxPerStageDescriptorInlineUniformBlocks =
          Info.MaxPerStageDescriptorInlineUniformBlocks;
      Props13->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks =
          Info.MaxPerStageDescriptorInlineUniformBlocks;
      Props13->maxDescriptorSetInlineUniformBlocks =
          Info.MaxDescriptorSetInlineUniformBlocks;
      Props13->maxDescriptorSetUpdateAfterBindInlineUniformBlocks =
          Info.MaxDescriptorSetInlineUniformBlocks;
      Props13->maxInlineUniformTotalSize = Info.MaxInlineUniformTotalSize;
      // (roadmap E8) All 36 `integerDotProduct*Accelerated` bits: no
      // `OpSDot`/`OpUDot`/`OpSUDot`-family lowering exists yet, so every
      // one is honestly `VK_FALSE` until that row lands.
      Props13->integerDotProduct8BitUnsignedAccelerated = VK_FALSE;
      Props13->integerDotProduct8BitSignedAccelerated = VK_FALSE;
      Props13->integerDotProduct8BitMixedSignednessAccelerated = VK_FALSE;
      Props13->integerDotProduct4x8BitPackedUnsignedAccelerated = VK_FALSE;
      Props13->integerDotProduct4x8BitPackedSignedAccelerated = VK_FALSE;
      Props13->integerDotProduct4x8BitPackedMixedSignednessAccelerated =
          VK_FALSE;
      Props13->integerDotProduct16BitUnsignedAccelerated = VK_FALSE;
      Props13->integerDotProduct16BitSignedAccelerated = VK_FALSE;
      Props13->integerDotProduct16BitMixedSignednessAccelerated = VK_FALSE;
      Props13->integerDotProduct32BitUnsignedAccelerated = VK_FALSE;
      Props13->integerDotProduct32BitSignedAccelerated = VK_FALSE;
      Props13->integerDotProduct32BitMixedSignednessAccelerated = VK_FALSE;
      Props13->integerDotProduct64BitUnsignedAccelerated = VK_FALSE;
      Props13->integerDotProduct64BitSignedAccelerated = VK_FALSE;
      Props13->integerDotProduct64BitMixedSignednessAccelerated = VK_FALSE;
      Props13->integerDotProductAccumulatingSaturating8BitUnsignedAccelerated =
          VK_FALSE;
      Props13->integerDotProductAccumulatingSaturating8BitSignedAccelerated =
          VK_FALSE;
      Props13
          ->integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated =
          VK_FALSE;
      Props13
          ->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated =
          VK_FALSE;
      Props13
          ->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated =
          VK_FALSE;
      Props13
          ->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated =
          VK_FALSE;
      Props13->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated =
          VK_FALSE;
      Props13->integerDotProductAccumulatingSaturating16BitSignedAccelerated =
          VK_FALSE;
      Props13
          ->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated =
          VK_FALSE;
      Props13->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated =
          VK_FALSE;
      Props13->integerDotProductAccumulatingSaturating32BitSignedAccelerated =
          VK_FALSE;
      Props13
          ->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated =
          VK_FALSE;
      Props13->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated =
          VK_FALSE;
      Props13->integerDotProductAccumulatingSaturating64BitSignedAccelerated =
          VK_FALSE;
      Props13
          ->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated =
          VK_FALSE;
      // (roadmap E18) `VK_EXT_texel_buffer_alignment`: `vkCreateBufferView`
      // (Buffer.cpp) never enforces an offset alignment stricter than the
      // core 1.0 `minTexelBufferOffsetAlignment` limit already does, and
      // the CPU runtime's typed texel buffer load/store helpers
      // (`femeCpuResourceLoadTypedV4*`/`StoreTypedV4*`,
      // FeMeRuntimeCPU.c) read and write through `__builtin_memcpy`, which
      // imposes no stricter alignment of its own -- so both the storage
      // and uniform variants genuinely need no more than that same
      // byte alignment, and a single-texel-sized offset (e.g. 4 bytes for
      // `R32_UINT`) is always sufficient too. These four agree with the
      // dedicated `VkPhysicalDeviceTexelBufferAlignmentProperties` case
      // below, exactly like `subgroupSizeControl`'s four fields above.
      Props13->storageTexelBufferOffsetAlignmentBytes =
          Info.Properties.limits.minTexelBufferOffsetAlignment;
      Props13->storageTexelBufferOffsetSingleTexelAlignment = VK_TRUE;
      Props13->uniformTexelBufferOffsetAlignmentBytes =
          Info.Properties.limits.minTexelBufferOffsetAlignment;
      Props13->uniformTexelBufferOffsetSingleTexelAlignment = VK_TRUE;
      // (roadmap E4) `VK_KHR_maintenance4`: the largest single buffer
      // allocation this ICD can create is bounded by the same host memory
      // size `VkPhysicalDeviceMaintenance3Properties::maxMemoryAllocationSize`
      // already reports (there is no further, buffer-specific limit to
      // enforce beyond that), so this must agree with that field and with
      // the dedicated `VkPhysicalDeviceMaintenance4Properties` case below.
      Props13->maxBufferSize = Info.MaxMemoryAllocationSize;
      break;
    }
    // (roadmap E4) `VK_KHR_maintenance4`'s own properties struct, whose 1.3
    // core and `KHR` spellings share one `sType`, agreeing with the
    // aggregate `VkPhysicalDeviceVulkan13Properties::maxBufferSize` case
    // above exactly like `VkPhysicalDeviceMaintenance3Properties` (1.1)
    // already does for its own `maxMemoryAllocationSize`.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES: {
      auto *Maintenance4 =
          reinterpret_cast<VkPhysicalDeviceMaintenance4Properties *>(Base);
      Maintenance4->maxBufferSize = Info.MaxMemoryAllocationSize;
      break;
    }
    // (roadmap E2) The aggregate `VkPhysicalDeviceVulkan14Properties`
    // struct: every one of its 25 limit fields is written explicitly, for
    // the same reason the 1.3 case above documents -- `dEQP-VK.api.info.
    // vulkan1p4.property_extensions_consistency` cross-checks every one of
    // them against its own dedicated struct
    // (`VkPhysicalDeviceLineRasterizationPropertiesKHR`,
    // `VkPhysicalDeviceMaintenance5PropertiesKHR`,
    // `VkPhysicalDeviceMaintenance6PropertiesKHR`,
    // `VkPhysicalDevicePushDescriptorPropertiesKHR`,
    // `VkPhysicalDeviceVertexAttributeDivisorPropertiesKHR`,
    // `VkPhysicalDeviceHostImageCopyPropertiesEXT`,
    // `VkPhysicalDevicePipelineRobustnessPropertiesEXT`), none of which
    // this ICD implements yet -- so every field here is the conservative,
    // honest `0`/`VK_FALSE`/`nullptr` that agrees with each of those
    // still-unfilled dedicated structs, for each later row (F5, F6, F8,
    // F10, F11, F12, E6) to raise together with its own dedicated-struct
    // case.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES: {
      auto *Props14 =
          reinterpret_cast<VkPhysicalDeviceVulkan14Properties *>(Base);
      // (roadmap F5) `VK_KHR_line_rasterization` is unimplemented.
      Props14->lineSubPixelPrecisionBits = 0;
      // (roadmap F6) `vertexAttributeInstanceRateDivisor` is unimplemented.
      Props14->maxVertexAttribDivisor = 0;
      Props14->supportsNonZeroFirstInstance = VK_FALSE;
      // (roadmap F12) `pushDescriptor` is unimplemented.
      Props14->maxPushDescriptors = 0;
      // (roadmap F8) `dynamicRenderingLocalRead` is unimplemented.
      Props14->dynamicRenderingLocalReadDepthStencilAttachments = VK_FALSE;
      Props14->dynamicRenderingLocalReadMultisampledAttachments = VK_FALSE;
      // (roadmap E5's `VK_KHR_maintenance5`) None of this group's fixed-
      // function guarantees have been verified for this software
      // rasterizer yet -- E5 itself only lands a null dynamic-rendering
      // attachment view, two new formats, and `vkCmdBindIndexBuffer2`,
      // none of which this properties group's own fields describe; see
      // the dedicated `VkPhysicalDeviceMaintenance5PropertiesKHR` case
      // below, which this must agree with.
      Props14->earlyFragmentMultisampleCoverageAfterSampleCounting = VK_FALSE;
      Props14->earlyFragmentSampleMaskTestBeforeSampleCounting = VK_FALSE;
      Props14->depthStencilSwizzleOneSupport = VK_FALSE;
      Props14->polygonModePointSize = VK_FALSE;
      Props14->nonStrictSinglePixelWideLinesUseParallelogram = VK_FALSE;
      Props14->nonStrictWideLinesUseParallelogram = VK_FALSE;
      // (roadmap E6's `VK_KHR_maintenance6`) `blockTexelViewCompatibleMultiple
      // Layers` and `fragmentShadingRateClampCombinerInputs` describe
      // fixed-function guarantees this ICD hasn't verified (out of this
      // row's own scope; block-compressed multi-layer texel views and
      // fragment shading rate combiners are each their own separate,
      // unimplemented feature), so they stay the conservative `VK_FALSE`.
      // `maxCombinedImageSamplerDescriptorCount` is this row's one real
      // limit: with no multi-planar/YCbCr sampler support (`samplerYcbcr
      // Conversion` above is `VK_FALSE`), a combined image sampler
      // descriptor always consumes exactly one descriptor slot.
      Props14->blockTexelViewCompatibleMultipleLayers = VK_FALSE;
      Props14->maxCombinedImageSamplerDescriptorCount = 1;
      Props14->fragmentShadingRateClampCombinerInputs = VK_FALSE;
      // (roadmap F10) `VK_EXT_pipeline_robustness` is unimplemented: even
      // though buffer bounds checking is unconditionally on elsewhere in
      // this ICD (see the Vulkan 1.0 core feature comment above), claiming
      // `ROBUST_BUFFER_ACCESS` here without a matching
      // `VkPhysicalDevicePipelineRobustnessPropertiesEXT` case would
      // disagree with that still-unfilled dedicated struct.
      Props14->defaultRobustnessStorageBuffers =
          VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DEVICE_DEFAULT;
      Props14->defaultRobustnessUniformBuffers =
          VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DEVICE_DEFAULT;
      Props14->defaultRobustnessVertexInputs =
          VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DEVICE_DEFAULT;
      Props14->defaultRobustnessImages =
          VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DEVICE_DEFAULT;
      // (roadmap F11) `hostImageCopy` is unimplemented: no supported
      // source/destination layout list exists yet, and
      // `identicalMemoryTypeRequirements` -- though a real, honest
      // `VK_TRUE` this ICD's single memory type could otherwise support
      // today -- is this same struct's own field
      // (`VkPhysicalDeviceHostImageCopyPropertiesEXT`), so it stays
      // `VK_FALSE` in lockstep with the rest of this group until F11 adds
      // that dedicated case.
      Props14->copySrcLayoutCount = 0;
      Props14->pCopySrcLayouts = nullptr;
      Props14->copyDstLayoutCount = 0;
      Props14->pCopyDstLayouts = nullptr;
      std::memset(Props14->optimalTilingLayoutUUID, 0, VK_UUID_SIZE);
      Props14->identicalMemoryTypeRequirements = VK_FALSE;
      break;
    }
    // (roadmap E5) `VK_KHR_maintenance5`'s own properties struct, agreeing
    // with the aggregate `VkPhysicalDeviceVulkan14Properties` case above
    // exactly like `VkPhysicalDeviceMaintenance4Properties` (1.3) already
    // does for its own fields: every one of this group's fixed-function
    // rasterizer guarantees remains an honest `VK_FALSE` -- E5 itself adds
    // a null dynamic-rendering attachment view, two new formats, and
    // `vkCmdBindIndexBuffer2`, none of which this struct's own fields
    // describe.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_PROPERTIES_KHR: {
      auto *Maintenance5 =
          reinterpret_cast<VkPhysicalDeviceMaintenance5PropertiesKHR *>(Base);
      Maintenance5->earlyFragmentMultisampleCoverageAfterSampleCounting =
          VK_FALSE;
      Maintenance5->earlyFragmentSampleMaskTestBeforeSampleCounting = VK_FALSE;
      Maintenance5->depthStencilSwizzleOneSupport = VK_FALSE;
      Maintenance5->polygonModePointSize = VK_FALSE;
      Maintenance5->nonStrictSinglePixelWideLinesUseParallelogram = VK_FALSE;
      Maintenance5->nonStrictWideLinesUseParallelogram = VK_FALSE;
      break;
    }
    // (roadmap E6) `VK_KHR_maintenance6`'s own properties struct, whose
    // 1.4 core and `KHR` spellings share one `sType`, agreeing with the
    // aggregate `VkPhysicalDeviceVulkan14Properties` case above exactly
    // like `VkPhysicalDeviceMaintenance5PropertiesKHR` already does for
    // its own fields.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_PROPERTIES: {
      auto *Maintenance6 =
          reinterpret_cast<VkPhysicalDeviceMaintenance6Properties *>(Base);
      Maintenance6->blockTexelViewCompatibleMultipleLayers = VK_FALSE;
      Maintenance6->maxCombinedImageSamplerDescriptorCount = 1;
      Maintenance6->fragmentShadingRateClampCombinerInputs = VK_FALSE;
      break;
    }
    // (roadmap E7) `VK_EXT_subgroup_size_control`'s own properties struct,
    // whose 1.3 core and `EXT` spellings share one `sType`, agreeing with
    // the aggregate `VkPhysicalDeviceVulkan13Properties` case above exactly
    // like `VkPhysicalDeviceMaintenance6Properties` already does for its
    // own fields.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES: {
      auto *SubgroupSizeControl =
          reinterpret_cast<VkPhysicalDeviceSubgroupSizeControlProperties *>(
              Base);
      SubgroupSizeControl->minSubgroupSize = Info.MinSubgroupSize;
      SubgroupSizeControl->maxSubgroupSize = Info.MaxSubgroupSize;
      SubgroupSizeControl->maxComputeWorkgroupSubgroups =
          Info.MaxComputeWorkgroupSubgroups;
      SubgroupSizeControl->requiredSubgroupSizeStages =
          Info.RequiredSubgroupSizeStages;
      break;
    }
    // (roadmap E14) `VK_EXT_inline_uniform_block`'s own properties struct,
    // whose 1.3 core and `EXT` spellings share one `sType`, agreeing with
    // the aggregate `VkPhysicalDeviceVulkan13Properties` case above exactly
    // like `VkPhysicalDeviceSubgroupSizeControlProperties` already does for
    // its own fields.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES: {
      auto *InlineUniformBlock =
          reinterpret_cast<VkPhysicalDeviceInlineUniformBlockProperties *>(
              Base);
      InlineUniformBlock->maxInlineUniformBlockSize =
          Info.MaxInlineUniformBlockSize;
      InlineUniformBlock->maxPerStageDescriptorInlineUniformBlocks =
          Info.MaxPerStageDescriptorInlineUniformBlocks;
      // Equal to the non-`UpdateAfterBind` field above, not `0`: per spec
      // these two are required limits independent of
      // `descriptorBindingInlineUniformBlockUpdateAfterBind` (see the
      // aggregate `VkPhysicalDeviceVulkan13Properties` case above).
      InlineUniformBlock
          ->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks =
          Info.MaxPerStageDescriptorInlineUniformBlocks;
      InlineUniformBlock->maxDescriptorSetInlineUniformBlocks =
          Info.MaxDescriptorSetInlineUniformBlocks;
      InlineUniformBlock->maxDescriptorSetUpdateAfterBindInlineUniformBlocks =
          Info.MaxDescriptorSetInlineUniformBlocks;
      break;
    }
    // (roadmap E8) `VK_KHR_shader_integer_dot_product`'s own properties
    // struct, whose 1.3 core and `KHR` spellings share one `sType`,
    // agreeing with the aggregate `VkPhysicalDeviceVulkan13Properties`
    // case above exactly like `VkPhysicalDeviceSubgroupSizeControlProperties`
    // already does for its own fields: every one of these 36 bits stays
    // `VK_FALSE` -- a genuine `spirv`->`llvm` lowering exists (see
    // SPIRVToLLVMPatterns.cpp), but it is an ordinary per-lane multiply-add
    // sequence, not a hardware-accelerated one, on this CPU target.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES: {
      auto *ShaderIntegerDotProduct =
          reinterpret_cast<VkPhysicalDeviceShaderIntegerDotProductProperties *>(
              Base);
      ShaderIntegerDotProduct->integerDotProduct8BitUnsignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct->integerDotProduct8BitSignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct->integerDotProduct8BitMixedSignednessAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProduct4x8BitPackedUnsignedAccelerated = VK_FALSE;
      ShaderIntegerDotProduct->integerDotProduct4x8BitPackedSignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProduct4x8BitPackedMixedSignednessAccelerated = VK_FALSE;
      ShaderIntegerDotProduct->integerDotProduct16BitUnsignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct->integerDotProduct16BitSignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProduct16BitMixedSignednessAccelerated = VK_FALSE;
      ShaderIntegerDotProduct->integerDotProduct32BitUnsignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct->integerDotProduct32BitSignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProduct32BitMixedSignednessAccelerated = VK_FALSE;
      ShaderIntegerDotProduct->integerDotProduct64BitUnsignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct->integerDotProduct64BitSignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProduct64BitMixedSignednessAccelerated = VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating8BitUnsignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating8BitSignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating16BitSignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating32BitSignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating64BitSignedAccelerated =
          VK_FALSE;
      ShaderIntegerDotProduct
          ->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated =
          VK_FALSE;
      break;
    }
    // (roadmap E18) `VK_EXT_texel_buffer_alignment`'s own properties
    // struct, whose 1.3 core and `EXT` spellings share one `sType`,
    // agreeing with the aggregate `VkPhysicalDeviceVulkan13Properties`
    // case above exactly like `VkPhysicalDeviceSubgroupSizeControlProperties`
    // already does for its own fields.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES: {
      auto *TexelBufferAlignment =
          reinterpret_cast<VkPhysicalDeviceTexelBufferAlignmentProperties *>(
              Base);
      TexelBufferAlignment->storageTexelBufferOffsetAlignmentBytes =
          Info.Properties.limits.minTexelBufferOffsetAlignment;
      TexelBufferAlignment->storageTexelBufferOffsetSingleTexelAlignment =
          VK_TRUE;
      TexelBufferAlignment->uniformTexelBufferOffsetAlignmentBytes =
          Info.Properties.limits.minTexelBufferOffsetAlignment;
      TexelBufferAlignment->uniformTexelBufferOffsetSingleTexelAlignment =
          VK_TRUE;
      break;
    }
    default:
      break;
    }
  }
}
} // namespace

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2 *pProperties) {
  const PhysicalDeviceInfo &Info =
      fromHandle<PhysicalDevice>(physicalDevice)->getInfo();
  pProperties->properties = Info.Properties;
  fillProperties2Chain(Info, pProperties->pNext);
}

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures) {
  *pFeatures = fromHandle<PhysicalDevice>(physicalDevice)->getInfo().Features;
}

namespace {
/// Fills every `pNext` extension struct `vkGetPhysicalDeviceFeatures2` this
/// ICD recognizes with truthful values -- V3: `timelineSemaphore` (see
/// "Queues, Scheduling, and Synchronization"); roadmap C6:
/// `hostQueryReset`, `uniformBufferStandardLayout`,
/// `separateDepthStencilLayouts`, `shaderSubgroupExtendedTypes`, and
/// `subgroupBroadcastDynamicId` (see each case's own comment for why every
/// one is honest rather than a bare mandatory-floor claim) -- each
/// reported through either its dedicated 1.2 feature struct or the
/// aggregate `VkPhysicalDeviceVulkan12Features` struct, matching whichever
/// one an application chained. `multiview` is deliberately absent: layered
/// rendering (roadmap V7) is not implemented, so it cannot be honestly
/// advertised yet (see PhysicalDeviceInfo.h's field comment for the
/// properties this ICD reports regardless).
void fillFeatures2Chain(void *pNext) {
  for (auto *Base = static_cast<VkBaseOutStructure *>(pNext); Base;
       Base = Base->pNext) {
    switch (Base->sType) {
    // (roadmap C6) Explicitly all-false, not merely left untouched:
    // `dEQP-VK.api.info.vulkan1p2.features`/`multiview_features` fill
    // every chained struct's buffer with a guard pattern first and fail if
    // any field the offset table lists is unmodified -- the same
    // "must be written even when false" requirement `PhysicalDeviceInfo.h`
    // already documents for `Properties11`'s `pointClippingBehavior`/
    // `protectedNoFault` above. None of `multiview`'s two shader-stage
    // amplification bits apply either way: geometry/tessellation stages
    // are not supported at all (`GraphicsPipeline.cpp`).
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceMultiviewFeatures *>(Base);
      Features->multiview = VK_FALSE;
      Features->multiviewGeometryShader = VK_FALSE;
      Features->multiviewTessellationShader = VK_FALSE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures *>(Base);
      Features->timelineSemaphore = VK_TRUE;
      break;
    }
    // (roadmap C6) `vkResetQueryPool` (`QueryPool.cpp`) already implements
    // the host-side reset `VK_EXT_host_query_reset`/core 1.2 promotes, so
    // this is unconditionally true, not merely a floor.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceHostQueryResetFeatures *>(Base);
      Features->hostQueryReset = VK_TRUE;
      break;
    }
    // (roadmap C6) `uniformBufferStandardLayout`/`separateDepthStencilLayouts`:
    // both relax a *default* Vulkan restriction (std140 array/matrix
    // stride; a single combined layout for a depth/stencil image) that
    // this ICD never enforced in the first place -- `getUniformBlockElement`
    // (`SPIRVToLLVMPatterns.cpp`) reads whatever offset/stride the SPIR-V
    // decorations already carry rather than recomputing or validating
    // std140 layout, and no image-layout transition path
    // (`CommandBuffer.cpp`/`Image.cpp`) distinguishes a depth-only from a
    // stencil-only layout value in the first place. Advertising both is
    // therefore honest, not merely a relaxed floor.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES: {
      auto *Features = reinterpret_cast<
          VkPhysicalDeviceUniformBufferStandardLayoutFeatures *>(Base);
      Features->uniformBufferStandardLayout = VK_TRUE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES: {
      auto *Features = reinterpret_cast<
          VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures *>(Base);
      Features->separateDepthStencilLayouts = VK_TRUE;
      break;
    }
    // (roadmap C6) `vkCreateFramebuffer`/`vkCmdBeginRenderPass`
    // (`RenderPass.cpp`/`CommandBuffer.cpp`) implement
    // `VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT`: a framebuffer created with it
    // defers its attachment views to `VkRenderPassAttachmentBeginInfo` at
    // each render-pass instance instead of binding concrete image views at
    // creation time.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceImagelessFramebufferFeatures *>(
              Base);
      Features->imagelessFramebuffer = VK_TRUE;
      break;
    }
    // (roadmap C6) `shaderSubgroupExtendedTypes` only relaxes the *type*
    // restriction on `OpGroupNonUniform*` operations; this ICD converts
    // none of them at all yet (`SPIRVToLLVMPatterns.cpp` wires up only the
    // basic subgroup builtins -- see `SubgroupSupportedOperations`'s
    // `VK_SUBGROUP_FEATURE_BASIC_BIT`-only value), so there is no
    // extended-type operation this bit could let through incorrectly: it
    // is vacuously true, the same reasoning `subgroupBroadcastDynamicId`
    // below uses.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES: {
      auto *Features = reinterpret_cast<
          VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *>(Base);
      Features->shaderSubgroupExtendedTypes = VK_TRUE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
      // (roadmap C6) Explicitly all-false: nothing 1.1 promotes is
      // advertised (`multiview` cannot be -- see the dedicated-struct case
      // above), but every field must still be written for the same guard-
      // pattern reason that case documents.
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceVulkan11Features *>(Base);
      Features->storageBuffer16BitAccess = VK_FALSE;
      Features->uniformAndStorageBuffer16BitAccess = VK_FALSE;
      Features->storagePushConstant16 = VK_FALSE;
      Features->storageInputOutput16 = VK_FALSE;
      Features->multiview = VK_FALSE;
      Features->multiviewGeometryShader = VK_FALSE;
      Features->multiviewTessellationShader = VK_FALSE;
      Features->variablePointersStorageBuffer = VK_FALSE;
      Features->variablePointers = VK_FALSE;
      Features->protectedMemory = VK_FALSE;
      Features->samplerYcbcrConversion = VK_FALSE;
      Features->shaderDrawParameters = VK_FALSE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceVulkan12Features *>(Base);
      // (roadmap C6) Every field is written explicitly, true or false, for
      // the same guard-pattern reason `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_
      // VULKAN_1_1_FEATURES` above documents -- this struct has no
      // "leave everything else zeroed" shortcut since some of its fields
      // are true.
      Features->samplerMirrorClampToEdge = VK_FALSE;
      Features->drawIndirectCount = VK_FALSE;
      Features->storageBuffer8BitAccess = VK_FALSE;
      Features->uniformAndStorageBuffer8BitAccess = VK_FALSE;
      Features->storagePushConstant8 = VK_FALSE;
      Features->shaderBufferInt64Atomics = VK_FALSE;
      Features->shaderSharedInt64Atomics = VK_FALSE;
      Features->shaderFloat16 = VK_FALSE;
      Features->shaderInt8 = VK_FALSE;
      Features->descriptorIndexing = VK_FALSE;
      Features->shaderInputAttachmentArrayDynamicIndexing = VK_FALSE;
      Features->shaderUniformTexelBufferArrayDynamicIndexing = VK_FALSE;
      Features->shaderStorageTexelBufferArrayDynamicIndexing = VK_FALSE;
      Features->shaderUniformBufferArrayNonUniformIndexing = VK_FALSE;
      Features->shaderSampledImageArrayNonUniformIndexing = VK_FALSE;
      Features->shaderStorageBufferArrayNonUniformIndexing = VK_FALSE;
      Features->shaderStorageImageArrayNonUniformIndexing = VK_FALSE;
      Features->shaderInputAttachmentArrayNonUniformIndexing = VK_FALSE;
      Features->shaderUniformTexelBufferArrayNonUniformIndexing = VK_FALSE;
      Features->shaderStorageTexelBufferArrayNonUniformIndexing = VK_FALSE;
      Features->descriptorBindingUniformBufferUpdateAfterBind = VK_FALSE;
      Features->descriptorBindingSampledImageUpdateAfterBind = VK_FALSE;
      Features->descriptorBindingStorageImageUpdateAfterBind = VK_FALSE;
      Features->descriptorBindingStorageBufferUpdateAfterBind = VK_FALSE;
      Features->descriptorBindingUniformTexelBufferUpdateAfterBind = VK_FALSE;
      Features->descriptorBindingStorageTexelBufferUpdateAfterBind = VK_FALSE;
      Features->descriptorBindingUpdateUnusedWhilePending = VK_FALSE;
      Features->descriptorBindingPartiallyBound = VK_FALSE;
      Features->descriptorBindingVariableDescriptorCount = VK_FALSE;
      Features->runtimeDescriptorArray = VK_FALSE;
      Features->samplerFilterMinmax = VK_FALSE;
      Features->scalarBlockLayout = VK_FALSE;
      Features->imagelessFramebuffer = VK_TRUE;
      Features->uniformBufferStandardLayout = VK_TRUE;
      Features->shaderSubgroupExtendedTypes = VK_TRUE;
      Features->separateDepthStencilLayouts = VK_TRUE;
      Features->hostQueryReset = VK_TRUE;
      Features->timelineSemaphore = VK_TRUE;
      Features->bufferDeviceAddress = VK_FALSE;
      Features->bufferDeviceAddressCaptureReplay = VK_FALSE;
      Features->bufferDeviceAddressMultiDevice = VK_FALSE;
      Features->vulkanMemoryModel = VK_FALSE;
      Features->vulkanMemoryModelDeviceScope = VK_FALSE;
      Features->vulkanMemoryModelAvailabilityVisibilityChains = VK_FALSE;
      Features->shaderOutputViewportIndex = VK_FALSE;
      Features->shaderOutputLayer = VK_FALSE;
      // (roadmap C6) No `OpGroupNonUniformBroadcast` conversion exists at
      // all (see the dedicated-struct case above), so there is no
      // non-dynamic-index broadcast this bit could be lying about: every
      // broadcast operation this ICD implements (none) supports a dynamic
      // id, vacuously.
      Features->subgroupBroadcastDynamicId = VK_TRUE;
      break;
    }
    // (roadmap E1) The aggregate `VkPhysicalDeviceVulkan13Features` struct:
    // every member is written explicitly, true or false, for the same
    // guard-pattern reason `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_
    // FEATURES` above documents. `dynamicRendering` is the one bit this ICD
    // genuinely implements (RenderPass.cpp/GraphicsPipeline.cpp) -- see the
    // dedicated `VK_KHR_dynamic_rendering` struct case below, which this
    // must agree with; every other 1.3 bit remains unimplemented.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceVulkan13Features *>(Base);
      Features->robustImageAccess = VK_FALSE;
      // (roadmap E14) `Descriptor.{h,cpp}`'s byte-blob descriptor storage
      // implements `VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK`, so this bit
      // -- like `dynamicRendering` above -- must agree with the dedicated
      // `VkPhysicalDeviceInlineUniformBlockFeatures` struct case below.
      // `descriptorBindingInlineUniformBlockUpdateAfterBind` stays
      // `VK_FALSE`: it only matters once a binding can be marked
      // `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT`
      // (`VkDescriptorSetLayoutBindingFlagsCreateInfo`), and no
      // update-after-bind/descriptor-indexing mechanism exists in this ICD
      // at all yet (`descriptorIndexing` below is likewise `VK_FALSE`).
      Features->inlineUniformBlock = VK_TRUE;
      Features->descriptorBindingInlineUniformBlockUpdateAfterBind = VK_FALSE;
      // (roadmap E9) `Pipeline.cpp`/`GraphicsPipeline.cpp` honor
      // `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT`, and
      // `PipelineCache.{h,cpp}` honors `VK_PIPELINE_CACHE_CREATE_
      // EXTERNALLY_SYNCHRONIZED_BIT`, so this bit -- like `dynamicRendering`/
      // `synchronization2` -- must agree with the dedicated
      // `VkPhysicalDevicePipelineCreationCacheControlFeatures` struct case
      // below.
      Features->pipelineCreationCacheControl = VK_TRUE;
      // (roadmap E10) `PrivateData.{h,cpp}` implements
      // `vkCreatePrivateDataSlot`/`vkSetPrivateData`/`vkGetPrivateData`/
      // `vkDestroyPrivateDataSlot`, so this bit -- like
      // `pipelineCreationCacheControl` above -- must agree with the
      // dedicated `VkPhysicalDevicePrivateDataFeatures` struct case below.
      Features->privateData = VK_TRUE;
      // (roadmap E11) SPIR-V's `OpDemoteToHelperInvocation` now converts
      // (SPIRVToLLVMPatterns.cpp/CanonicalizeStage.cpp) to
      // `feme.stage.demote`, whose reference/SIMD lowering
      // (ReferenceLowering.cpp/Linearize.cpp/SIMDize.cpp) already existed,
      // so this bit -- like `pipelineCreationCacheControl`/`privateData`
      // above -- must agree with the dedicated
      // `VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures` struct
      // case below.
      Features->shaderDemoteToHelperInvocation = VK_TRUE;
      // (roadmap E12) SPIR-V's `OpTerminateInvocation` -- a true
      // terminator, unlike `OpDemoteToHelperInvocation` above -- now
      // converts (SPIRVToLLVMPatterns.cpp) to an unconditional
      // discard-and-return reusing `feme.stage.discard`'s own existing
      // reference/SIMD lowering, so this bit -- like
      // `shaderDemoteToHelperInvocation` above -- must agree with the
      // dedicated `VkPhysicalDeviceShaderTerminateInvocationFeatures`
      // struct case below.
      Features->shaderTerminateInvocation = VK_TRUE;
      // (roadmap E7) `Pipeline.cpp`'s `compileComputePipeline` honors both
      // a chained `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` and
      // `VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT`, so
      // these two bits -- like `synchronization2`/`maintenance4` below --
      // must agree with the dedicated
      // `VkPhysicalDeviceSubgroupSizeControlFeatures` struct case below.
      Features->subgroupSizeControl = VK_TRUE;
      Features->computeFullSubgroups = VK_TRUE;
      // (roadmap E3) `vkCmdPipelineBarrier2`/`vkCmdWriteTimestamp2`/
      // `vkQueueSubmit2`/`vkCmdSetEvent2`/`vkCmdResetEvent2`/
      // `vkCmdWaitEvents2` (CommandBuffer.cpp/Sync.cpp) translate
      // `VkDependencyInfo`'s 2-mask shape down to the existing 1-mask
      // model, so this bit -- like `dynamicRendering` -- must agree with
      // the dedicated `VK_KHR_synchronization2` feature struct case below.
      Features->synchronization2 = VK_TRUE;
      Features->textureCompressionASTC_HDR = VK_FALSE;
      // (roadmap E13) `feme::spirv::WorkgroupGlobalVariablePattern`
      // (SPIRVToLLVMPatterns.cpp) imports a `zero_initializer`'d
      // `Workgroup` variable's own `#llvm.zero`, and
      // `feme::cpu::EntryWrapperPass` zeros the whole groupshared buffer
      // once per group whenever one is present
      // (`GroupSharedLayout::NeedsZeroInit`, GroupShared.h/EntryWrapper.cpp),
      // so this bit -- like `shaderTerminateInvocation` above -- must
      // agree with the dedicated
      // `VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures` struct
      // case below.
      Features->shaderZeroInitializeWorkgroupMemory = VK_TRUE;
      Features->dynamicRendering = VK_TRUE;
      // (roadmap E8) `spirv.SDot`/`spirv.UDot`/`spirv.SUDot`/`*AccSat`
      // (SPIRVToLLVMPatterns.cpp) are implemented, so this bit -- like
      // `dynamicRendering`/`synchronization2` -- must agree with the
      // dedicated `VK_KHR_shader_integer_dot_product` feature struct case
      // below. None of the 36 `integerDotProduct*Accelerated` limit bits
      // (`VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES` above)
      // follow from this: this feature bit only claims the operations are
      // supported, not that any of them run faster than the equivalent
      // scalar sequence on this CPU target.
      Features->shaderIntegerDotProduct = VK_TRUE;
      // (roadmap E4) `vkGetDeviceBufferMemoryRequirements`/
      // `vkGetDeviceImageMemoryRequirements`/
      // `vkGetDeviceImageSparseMemoryRequirements` (Buffer.cpp/Image.cpp)
      // are implemented, so this bit -- like `dynamicRendering`/
      // `synchronization2` -- must agree with the dedicated
      // `VK_KHR_maintenance4` feature struct case below.
      Features->maintenance4 = VK_TRUE;
      break;
    }
    // (roadmap E1) The aggregate `VkPhysicalDeviceVulkan14Features` struct:
    // every member is written explicitly, true or false, for the same
    // guard-pattern reason `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_
    // FEATURES` above documents. (roadmap E5) `maintenance5` is the one bit
    // this ICD genuinely implements (a null dynamic-rendering attachment
    // view, `VK_FORMAT_A8_UNORM`/`A1B5G5R5_UNORM_PACK16`, and
    // `vkCmdBindIndexBuffer2`) -- see the dedicated
    // `VkPhysicalDeviceMaintenance5FeaturesKHR` struct case below, which
    // this must agree with; every other 1.4 bit remains unimplemented.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceVulkan14Features *>(Base);
      // (roadmap F1) `vkGetPhysicalDeviceQueueFamilyProperties2`'s
      // `VkQueueFamilyGlobalPriorityProperties` chain now reports the full
      // mandatory priority list for every queue family (see
      // `fillQueueFamilyGlobalPriorityProperties` below), so this bit --
      // like `maintenance5`/`maintenance6` above -- must agree with the
      // dedicated `VkPhysicalDeviceGlobalPriorityQueryFeatures` struct case
      // below.
      Features->globalPriorityQuery = VK_TRUE;
      // (roadmap F2) `spirv.GroupNonUniformRotateKHR` now converts
      // (SPIRVToLLVMPatterns.cpp's `RotateConversionPattern`), covering both
      // the plain and `cluster_size` (clustered) forms with the same
      // pattern, so these two bits -- like `globalPriorityQuery` above --
      // must agree with the dedicated
      // `VkPhysicalDeviceShaderSubgroupRotateFeatures` struct case below.
      Features->shaderSubgroupRotate = VK_TRUE;
      Features->shaderSubgroupRotateClustered = VK_TRUE;
      Features->shaderFloatControls2 = VK_FALSE;
      Features->shaderExpectAssume = VK_FALSE;
      Features->rectangularLines = VK_FALSE;
      Features->bresenhamLines = VK_FALSE;
      Features->smoothLines = VK_FALSE;
      Features->stippledRectangularLines = VK_FALSE;
      Features->stippledBresenhamLines = VK_FALSE;
      Features->stippledSmoothLines = VK_FALSE;
      Features->vertexAttributeInstanceRateDivisor = VK_FALSE;
      Features->vertexAttributeInstanceRateZeroDivisor = VK_FALSE;
      Features->indexTypeUint8 = VK_FALSE;
      Features->dynamicRenderingLocalRead = VK_FALSE;
      Features->maintenance5 = VK_TRUE;
      // (roadmap E6) `vkCmdBindDescriptorSets2`/`vkCmdPushConstants2`
      // (CommandBuffer.cpp) are implemented, so this bit -- like
      // `maintenance5` -- must agree with the dedicated
      // `VkPhysicalDeviceMaintenance6Features` struct case below.
      Features->maintenance6 = VK_TRUE;
      Features->pipelineProtectedAccess = VK_FALSE;
      Features->pipelineRobustness = VK_FALSE;
      Features->hostImageCopy = VK_FALSE;
      Features->pushDescriptor = VK_FALSE;
      break;
    }
    // (V6) `VK_KHR_dynamic_rendering`'s own feature struct, whose 1.3 core
    // and `KHR` spellings share one `sType`.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceDynamicRenderingFeatures *>(Base);
      Features->dynamicRendering = VK_TRUE;
      break;
    }
    // (roadmap E3) `VK_KHR_synchronization2`'s own feature struct, whose
    // 1.3 core and `KHR` spellings share one `sType`, exactly like
    // `dynamicRendering` above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceSynchronization2Features *>(Base);
      Features->synchronization2 = VK_TRUE;
      break;
    }
    // (roadmap E4) `VK_KHR_maintenance4`'s own feature struct, whose 1.3
    // core and `KHR` spellings share one `sType`, exactly like
    // `synchronization2` above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceMaintenance4Features *>(Base);
      Features->maintenance4 = VK_TRUE;
      break;
    }
    // (roadmap E7) `VK_EXT_subgroup_size_control`'s own feature struct,
    // whose 1.3 core and `EXT` spellings share one `sType`, exactly like
    // `maintenance4` above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceSubgroupSizeControlFeatures *>(Base);
      Features->subgroupSizeControl = VK_TRUE;
      Features->computeFullSubgroups = VK_TRUE;
      break;
    }
    // (roadmap E14) `VK_EXT_inline_uniform_block`'s own feature struct,
    // whose 1.3 core and `EXT` spellings share one `sType`, exactly like
    // `subgroupSizeControl` above.
    // `descriptorBindingInlineUniformBlockUpdateAfterBind` stays
    // `VK_FALSE` here too, for the same reason the aggregate
    // `VkPhysicalDeviceVulkan13Features` case documents.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceInlineUniformBlockFeatures *>(Base);
      Features->inlineUniformBlock = VK_TRUE;
      Features->descriptorBindingInlineUniformBlockUpdateAfterBind = VK_FALSE;
      break;
    }
    // (roadmap E5) `VK_KHR_maintenance5`'s own feature struct. Unlike
    // `dynamicRendering`/`synchronization2`/`maintenance4` above, this
    // extension's core promotion added no core-spelled alias struct of its
    // own, so only the `KHR`-suffixed `sType`/type exist.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceMaintenance5FeaturesKHR *>(Base);
      Features->maintenance5 = VK_TRUE;
      break;
    }
    // (roadmap E6) `VK_KHR_maintenance6`'s own feature struct, whose 1.4
    // core and `KHR` spellings share one `sType` (unlike `maintenance5`
    // above, this extension's core promotion did add a core-spelled
    // alias), agreeing with the aggregate `VkPhysicalDeviceVulkan14Features`
    // case above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceMaintenance6Features *>(Base);
      Features->maintenance6 = VK_TRUE;
      break;
    }
    // (roadmap F1) `VK_KHR_global_priority`'s own feature struct, whose 1.4
    // core and `KHR` spellings share one `sType`, exactly like
    // `maintenance6` above, agreeing with the aggregate
    // `VkPhysicalDeviceVulkan14Features` case above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceGlobalPriorityQueryFeatures *>(Base);
      Features->globalPriorityQuery = VK_TRUE;
      break;
    }
    // (roadmap F2) `VK_KHR_shader_subgroup_rotate`'s own feature struct,
    // whose 1.4 core and `KHR` spellings share one `sType`, exactly like
    // `globalPriorityQuery` above, agreeing with the aggregate
    // `VkPhysicalDeviceVulkan14Features` case above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceShaderSubgroupRotateFeatures *>(
              Base);
      Features->shaderSubgroupRotate = VK_TRUE;
      Features->shaderSubgroupRotateClustered = VK_TRUE;
      break;
    }
    // (roadmap E8) `VK_KHR_shader_integer_dot_product`'s own feature
    // struct, whose 1.3 core and `KHR` spellings share one `sType`, exactly
    // like `maintenance4` above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceShaderIntegerDotProductFeatures *>(
              Base);
      Features->shaderIntegerDotProduct = VK_TRUE;
      break;
    }
    // (roadmap E9) `VK_EXT_pipeline_creation_cache_control`'s own feature
    // struct, whose 1.3 core and `EXT` spellings share one `sType`, exactly
    // like `subgroupSizeControl` above.
    // `vkCreateComputePipelines`/`vkCreateGraphicsPipelines` (Pipeline.cpp/
    // GraphicsPipeline.cpp) honor `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_
    // COMPILE_REQUIRED_BIT`, and `vkCreatePipelineCache` (Pipeline.cpp)
    // honors `VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT`
    // (PipelineCache.{h,cpp}), so this bit -- like `maintenance4`/
    // `subgroupSizeControl` above -- must agree with the aggregate
    // `VkPhysicalDeviceVulkan13Features` case above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES: {
      auto *Features = reinterpret_cast<
          VkPhysicalDevicePipelineCreationCacheControlFeatures *>(Base);
      Features->pipelineCreationCacheControl = VK_TRUE;
      break;
    }
    // (roadmap E10) `VK_EXT_private_data`'s own feature struct, whose 1.3
    // core and `EXT` spellings share one `sType`, exactly like
    // `pipelineCreationCacheControl` above. `PrivateData.{h,cpp}` implements
    // all four entrypoints unconditionally, so this bit is unconditionally
    // true.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDevicePrivateDataFeatures *>(Base);
      Features->privateData = VK_TRUE;
      break;
    }
    // (roadmap E11) `VK_EXT_shader_demote_to_helper_invocation`'s own
    // feature struct, whose 1.3 core and `EXT` spellings share one
    // `sType`, exactly like `pipelineCreationCacheControl`/`privateData`
    // above. `SPIRVToLLVMPatterns.cpp`/`CanonicalizeStage.cpp` convert
    // `OpDemoteToHelperInvocation` to `feme.stage.demote` unconditionally,
    // so this bit -- like those above -- must agree with the aggregate
    // `VkPhysicalDeviceVulkan13Features` case above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES: {
      auto *Features = reinterpret_cast<
          VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *>(Base);
      Features->shaderDemoteToHelperInvocation = VK_TRUE;
      break;
    }
    // (roadmap E12) `VK_KHR_shader_terminate_invocation`'s own feature
    // struct, whose 1.3 core and `KHR` spellings share one `sType`, exactly
    // like `shaderDemoteToHelperInvocation` above.
    // `SPIRVToLLVMPatterns.cpp` converts `OpTerminateInvocation` to an
    // unconditional discard-and-return, so this bit -- like the one above
    // -- must agree with the aggregate `VkPhysicalDeviceVulkan13Features`
    // case above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceShaderTerminateInvocationFeatures *>(
              Base);
      Features->shaderTerminateInvocation = VK_TRUE;
      break;
    }
    // (roadmap E13) `VK_KHR_zero_initialize_workgroup_memory`'s own
    // feature struct, whose 1.3 core and `KHR` spellings share one
    // `sType`, exactly like `shaderTerminateInvocation` above. A
    // `zero_initializer`'d SPIR-V `Workgroup` variable's groupshared
    // buffer is zeroed once per group (GroupShared.h/EntryWrapper.cpp),
    // so this bit -- like the one above -- must agree with the aggregate
    // `VkPhysicalDeviceVulkan13Features` case above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES: {
      auto *Features = reinterpret_cast<
          VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures *>(Base);
      Features->shaderZeroInitializeWorkgroupMemory = VK_TRUE;
      break;
    }
    // (roadmap C4c) `VK_EXT_extended_dynamic_state`'s own feature struct:
    // all 12 dynamic states it adds are implemented (GraphicsPipeline.cpp/
    // CommandBuffer.cpp), so this is unconditionally true, exactly like
    // `dynamicRendering` above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *>(
              Base);
      Features->extendedDynamicState = VK_TRUE;
      break;
    }
    // (roadmap E18) `VK_EXT_texel_buffer_alignment`'s own feature struct
    // (there is no aggregate 1.3/1.4 feature-struct field to agree with --
    // only this extension's *properties* struct was promoted to core
    // 1.3, per the Vulkan specification -- unlike every other row's
    // feature bit above). `vkCreateBufferView` (Buffer.cpp) needs no
    // stricter offset alignment than the dedicated
    // `VkPhysicalDeviceTexelBufferAlignmentProperties` case
    // (`fillProperties2Chain` above) already reports, so this is
    // unconditionally true, exactly like `extendedDynamicState` above.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *>(
              Base);
      Features->texelBufferAlignment = VK_TRUE;
      break;
    }
    // (roadmap E19) `VK_EXT_4444_formats`'s own feature struct; like
    // `maintenance5` above, this extension's core promotion added no
    // core-spelled alias struct of its own, only the `EXT`-suffixed one.
    // Both formats it names (`Format.cpp`'s `mapVkFormat`) are recognized
    // `VkFormat` values, so both bits are unconditionally true.
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT: {
      auto *Features =
          reinterpret_cast<VkPhysicalDevice4444FormatsFeaturesEXT *>(Base);
      Features->formatA4R4G4B4 = VK_TRUE;
      Features->formatA4B4G4R4 = VK_TRUE;
      break;
    }
    default:
      break;
    }
  }
}

/// Fills a queue family's `VkQueueFamilyGlobalPriorityProperties` chain
/// entry (roadmap F1, `VK_KHR_global_priority`/`globalPriorityQuery`): this
/// ICD has one worker pool with no real OS-level scheduling priority (see
/// "Queue families" in FeMeVulkanDesign.md), so every priority level the
/// spec defines is reported as supported for every queue family, the same
/// "single logical queue, narrowed by capability flags only" precedent
/// roadmap C7 set for `queueFlags` itself -- there is no real per-priority
/// distinction for this executor to narrow. An application must not chain
/// a struct for a feature it didn't enable, so any unrecognized `sType` is
/// left untouched, matching `fillProperties2Chain`/`fillFeatures2Chain`
/// above.
void fillQueueFamilyGlobalPriorityProperties(void *pNext) {
  for (auto *Base = static_cast<VkBaseOutStructure *>(pNext); Base;
       Base = Base->pNext) {
    if (Base->sType !=
        VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES)
      continue;
    auto *Priority =
        reinterpret_cast<VkQueueFamilyGlobalPriorityProperties *>(Base);
    Priority->priorityCount = 4;
    Priority->priorities[0] = VK_QUEUE_GLOBAL_PRIORITY_LOW;
    Priority->priorities[1] = VK_QUEUE_GLOBAL_PRIORITY_MEDIUM;
    Priority->priorities[2] = VK_QUEUE_GLOBAL_PRIORITY_HIGH;
    Priority->priorities[3] = VK_QUEUE_GLOBAL_PRIORITY_REALTIME;
  }
}

} // namespace

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 *pFeatures) {
  pFeatures->features =
      fromHandle<PhysicalDevice>(physicalDevice)->getInfo().Features;
  fillFeatures2Chain(pFeatures->pNext);
}

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties *pMemoryProperties) {
  *pMemoryProperties =
      fromHandle<PhysicalDevice>(physicalDevice)->getInfo().MemoryProperties;
}

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2 *pMemoryProperties) {
  pMemoryProperties->memoryProperties =
      fromHandle<PhysicalDevice>(physicalDevice)->getInfo().MemoryProperties;
}

VKAPI_ATTR void VKAPI_CALL
feme::vulkan::vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties) {
  const PhysicalDeviceInfo &Info =
      fromHandle<PhysicalDevice>(physicalDevice)->getInfo();
  enumerate<VkQueueFamilyProperties>(
      PhysicalDeviceInfo::NumQueueFamilies, Info.QueueFamilies,
      pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL
feme::vulkan::vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2 *pQueueFamilyProperties) {
  const PhysicalDeviceInfo &Info =
      fromHandle<PhysicalDevice>(physicalDevice)->getInfo();
  constexpr uint32_t TrueCount = PhysicalDeviceInfo::NumQueueFamilies;
  if (!pQueueFamilyProperties) {
    *pQueueFamilyPropertyCount = TrueCount;
    return;
  }
  uint32_t ToCopy = *pQueueFamilyPropertyCount < TrueCount
                        ? *pQueueFamilyPropertyCount
                        : TrueCount;
  for (uint32_t I = 0; I < ToCopy; ++I) {
    pQueueFamilyProperties[I].queueFamilyProperties = Info.QueueFamilies[I];
    fillQueueFamilyGlobalPriorityProperties(pQueueFamilyProperties[I].pNext);
  }
  *pQueueFamilyPropertyCount = ToCopy;
}

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice, VkFormat format, VkFormatProperties *pFormatProperties) {
  // Roadmap E24: this used to unconditionally report an all-zero
  // `VkFormatProperties` for every format ("no image is supported yet"),
  // stale since V5 added real image support. `formatFeatureFlags`
  // (Format.h) now reports the real, already-implemented feature set for
  // any format `mapVkFormat` recognizes; an unrecognized format still gets
  // the honest all-zero result. `VK_IMAGE_TILING_LINEAR`/`_OPTIMAL` are not
  // distinguished anywhere in this ICD (see Image.h's file comment), so
  // both tiling fields are identical.
  std::optional<feme::cpu::ResourceFormat> Format = mapVkFormat(format);
  VkFormatFeatureFlags ImageFeatures =
      Format ? formatFeatureFlags(*Format) : VkFormatFeatureFlags(0);
  pFormatProperties->linearTilingFeatures = ImageFeatures;
  pFormatProperties->optimalTilingFeatures = ImageFeatures;
  // `isTexelBufferFormatSupported` (Format.h) gates `vkCreateBufferView`
  // (Descriptor.h's file comment): the CPU runtime's typed load *and*
  // store helpers exist for exactly that same format set, so both texel
  // buffer feature bits apply together.
  pFormatProperties->bufferFeatures =
      Format && isTexelBufferFormatSupported(*Format)
          ? (VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
             VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT)
          : VkFormatFeatureFlags(0);
}

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkGetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice physicalDevice, VkFormat format,
    VkFormatProperties2 *pFormatProperties) {
  feme::vulkan::vkGetPhysicalDeviceFormatProperties(
      physicalDevice, format, &pFormatProperties->formatProperties);
  // (Roadmap E25) `VkFormatProperties3` (core since Vulkan 1.3, which this
  // ICD's advertised `apiVersion` implies is always available via a
  // chained `pNext` struct, whether or not `VK_KHR_format_feature_flags2`
  // is separately listed as an advertised extension name) used to be left
  // entirely untouched here -- any caller that chained one (as
  // `dEQP-VK.api.info.unsupported_image_usage.*`'s own `Context::
  // getFormatProperties` helper does once it sees a >=1.3 device) read
  // back whatever it had zero-initialized the struct to, not this ICD's
  // real answer, silently discarding every bit `formatFeatureFlags`
  // reports. Filling it (with the same 32-bit `VkFormatFeatureFlags`
  // result widened to `VkFormatFeatureFlags2`, since every bit this ICD
  // sets today has an identical numeric value in both) fixes that for any
  // caller of this entry point, not just the format-broadening this row
  // otherwise does.
  for (auto *Base = static_cast<VkBaseOutStructure *>(pFormatProperties->pNext);
       Base; Base = Base->pNext) {
    if (Base->sType != VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3)
      continue;
    auto *Props3 = reinterpret_cast<VkFormatProperties3 *>(Base);
    Props3->linearTilingFeatures =
        pFormatProperties->formatProperties.linearTilingFeatures;
    Props3->optimalTilingFeatures =
        pFormatProperties->formatProperties.optimalTilingFeatures;
    Props3->bufferFeatures = pFormatProperties->formatProperties.bufferFeatures;
  }
}

VKAPI_ATTR VkResult VKAPI_CALL
feme::vulkan::vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkImageTiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkImageFormatProperties *pImageFormatProperties) {
  // Roadmap E24: this used to unconditionally return
  // `VK_ERROR_FORMAT_NOT_SUPPORTED` for every format/type/usage/flags
  // combination ("no image is supported yet"), stale since V5 added real
  // image support -- and the reason E22's own CTS run measured zero
  // headline movement despite `textureCompressionASTC_LDR` reading
  // `VK_TRUE`: `dEQP-VK.texture.*`'s own capability probe calls this
  // command before creating any image at all, of any format.
  const PhysicalDeviceInfo &Info =
      fromHandle<PhysicalDevice>(physicalDevice)->getInfo();
  std::optional<feme::cpu::ResourceFormat> Format = mapVkFormat(format);
  if (!Format)
    return VK_ERROR_FORMAT_NOT_SUPPORTED;

  // A requested `usage` bit this format has no matching feature for is
  // unsupported outright -- matching real Vulkan's own
  // `VUID-vkGetPhysicalDeviceImageFormatProperties-usage-parameter`-adjacent
  // rule that an application must not use a format/usage combination this
  // query reported unsupported.
  VkFormatFeatureFlags Features = formatFeatureFlags(*Format);
  if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) &&
      !(Features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
  if ((usage & VK_IMAGE_USAGE_STORAGE_BIT) &&
      !(Features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
  if ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
      !(Features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
  if ((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) &&
      !(Features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
  if ((usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
      !(Features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
  if ((usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
      !(Features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
  // An input attachment reuses the same read-only image-view + layout
  // record as a sampled color or depth/stencil attachment (see "V5: Images
  // and sampling"'s status note), so it needs one of those two feature
  // bits, not a dedicated one of its own.
  if ((usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) &&
      !(Features & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)))
    return VK_ERROR_FORMAT_NOT_SUPPORTED;

  // The shape (flags/type/usage) validated here is otherwise the exact
  // same check `vkCreateImage` itself applies (`isValidImageShape`,
  // Image.h) to a maximal single-mip, single-layer, single-sample image of
  // this shape -- an unsupported shape (an unadvertised `flags` bit, or a
  // `usage` this device's sample-count limits reject at 1 sample, which
  // never happens) is reported as unsupported rather than silently
  // ignored.
  VkImageCreateInfo ShapeProbe{};
  ShapeProbe.imageType = type;
  ShapeProbe.flags = flags;
  ShapeProbe.usage = usage;
  ShapeProbe.samples = VK_SAMPLE_COUNT_1_BIT;
  ShapeProbe.mipLevels = 1;
  ShapeProbe.arrayLayers = 1;
  if (!isValidImageShape(ShapeProbe, Info))
    return VK_ERROR_FORMAT_NOT_SUPPORTED;

  const VkPhysicalDeviceLimits &Limits = Info.Properties.limits;
  uint32_t MaxExtentXY;
  uint32_t MaxDepth = 1;
  switch (type) {
  case VK_IMAGE_TYPE_1D:
    MaxExtentXY = Limits.maxImageDimension1D;
    break;
  case VK_IMAGE_TYPE_2D:
    MaxExtentXY = (flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
                      ? Limits.maxImageDimensionCube
                      : Limits.maxImageDimension2D;
    break;
  case VK_IMAGE_TYPE_3D:
    MaxExtentXY = Limits.maxImageDimension3D;
    MaxDepth = Limits.maxImageDimension3D;
    break;
  default:
    llvm_unreachable("unhandled VkImageType");
  }

  VkImageCreateInfo MaxProbe = ShapeProbe;
  MaxProbe.extent = {MaxExtentXY, MaxExtentXY, MaxDepth};
  MaxProbe.mipLevels = llvm::Log2_32(std::max(MaxExtentXY, MaxDepth)) + 1;
  MaxProbe.arrayLayers =
      type == VK_IMAGE_TYPE_3D ? 1 : Limits.maxImageArrayLayers;
  VkSampleCountFlags SampleCounts = supportedSampleCounts(Info, usage);
  // A multisample image is only ever a single-mip 2D one
  // (`isValidImageShape`'s own `VUID-VkImageCreateInfo-samples-02257` check
  // above), but that is a property of *a* multisample image's own shape
  // (samples > 1 implies mipLevels == 1), not of `MaxProbe`'s unrelated
  // maximal, non-multisampled mip chain -- checking `MaxProbe.mipLevels`
  // here instead reported `VK_SAMPLE_COUNT_1_BIT` for essentially every 2D
  // format (whose maximal image always has more than one mip level),
  // permanently hiding every wider sample count `supportedSampleCounts`
  // above already computes correctly (`dEQP-VK.glsl.texture_functions.
  // query.texturesamples.*`, whose `OpImageQuerySamples` case list assumes
  // a 2D sampled image can report more than one).
  if (type != VK_IMAGE_TYPE_2D)
    SampleCounts = VK_SAMPLE_COUNT_1_BIT;

  pImageFormatProperties->maxExtent = MaxProbe.extent;
  pImageFormatProperties->maxMipLevels = MaxProbe.mipLevels;
  pImageFormatProperties->maxArrayLayers = MaxProbe.arrayLayers;
  pImageFormatProperties->sampleCounts = SampleCounts;
  pImageFormatProperties->maxResourceSize =
      computeImageCreateInfoSize(MaxProbe, *Format);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
feme::vulkan::vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice, VkFormat, VkImageType, VkSampleCountFlagBits,
    VkImageUsageFlags, VkImageTiling, uint32_t *pPropertyCount,
    VkSparseImageFormatProperties *) {
  // No sparse residency is supported (see "Initial Non-Goals"); no format
  // combination reports any sparse property.
  *pPropertyCount = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
feme::vulkan::vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice, const char *pLayerName, uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties) {
  if (pLayerName)
    return VK_ERROR_LAYER_NOT_PRESENT;
  llvm::ArrayRef<VkExtensionProperties> Extensions =
      getSupportedDeviceExtensions();
  return enumerate<VkExtensionProperties>(
      static_cast<uint32_t>(Extensions.size()), Extensions.data(),
      pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL feme::vulkan::vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice, uint32_t *pPropertyCount,
    VkLayerProperties *pProperties) {
  return enumerate<VkLayerProperties>(0, nullptr, pPropertyCount, pProperties);
}

// (roadmap E19) `VK_EXT_tooling_info`: this ICD is not itself a layer or
// debugging tool, and wraps no such tool internally (unlike e.g. a
// validation-layer-aware driver, which might report the validation layer
// here), so the truthful answer is the same "zero" `vkEnumerateDeviceLayer
// Properties` above already reports for its own, differently-shaped list.
VKAPI_ATTR VkResult VKAPI_CALL feme::vulkan::vkGetPhysicalDeviceToolProperties(
    VkPhysicalDevice, uint32_t *pToolCount,
    VkPhysicalDeviceToolProperties *pToolProperties) {
  return enumerate<VkPhysicalDeviceToolProperties>(0, nullptr, pToolCount,
                                                   pToolProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL feme::vulkan::vkCreateDevice(
    VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
  // Only an extension this driver actually implements may be enabled (see
  // `getSupportedDeviceExtensions`); anything else is refused rather than
  // silently accepted and then not honored.
  //
  // (roadmap F1) A `VkDeviceQueueGlobalPriorityCreateInfo` chained onto a
  // `pQueueCreateInfos` entry's `pNext` is deliberately never inspected
  // here: this ICD has one worker pool with no real OS-level scheduling
  // priority to raise or lower, and every priority the mandatory list
  // permits (`fillQueueFamilyGlobalPriorityProperties` in this file) is
  // reported as supported for every queue family, so there is nothing this
  // hint could honestly cause device creation to refuse. Treating it as a
  // no-op is therefore truthful, not merely convenient -- the same
  // "single logical queue, narrowed by capability flags only" precedent
  // roadmap C7 set for `queueFlags` itself.
  for (uint32_t I = 0; I != pCreateInfo->enabledExtensionCount; ++I) {
    bool Supported = false;
    for (const VkExtensionProperties &Extension :
         getSupportedDeviceExtensions())
      Supported |= std::strcmp(Extension.extensionName,
                               pCreateInfo->ppEnabledExtensionNames[I]) == 0;
    if (!Supported)
      return VK_ERROR_EXTENSION_NOT_PRESENT;
  }

  PhysicalDevice *Physical = fromHandle<PhysicalDevice>(physicalDevice);
  Allocator Alloc(pAllocator);
  Device *Obj =
      Alloc.create<Device>(VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, *Physical, Alloc);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;

  for (uint32_t I = 0; I < pCreateInfo->queueCreateInfoCount; ++I) {
    const VkDeviceQueueCreateInfo &QueueInfo =
        pCreateInfo->pQueueCreateInfos[I];
    // Only `PhysicalDeviceInfo::NumQueueFamilies` families exist; the
    // loader/validation layers are expected to reject any other index
    // before this point, but this ICD checks for itself rather than
    // trusting the caller.
    if (QueueInfo.queueFamilyIndex >= PhysicalDeviceInfo::NumQueueFamilies ||
        !Obj->createQueues(QueueInfo.queueFamilyIndex, QueueInfo.queueCount)) {
      Alloc.destroy(Obj);
      return VK_ERROR_INITIALIZATION_FAILED;
    }
  }

  *pDevice = toHandle<VkDevice>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkDestroyDevice(
    VkDevice device, const VkAllocationCallbacks *pAllocator) {
  (void)pAllocator;
  if (!device)
    return;
  Device *Obj = fromHandle<Device>(device);
  Allocator Alloc = Obj->getAllocator();
  Alloc.destroy(Obj);
}

VKAPI_ATTR void VKAPI_CALL
feme::vulkan::vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex,
                               uint32_t queueIndex, VkQueue *pQueue) {
  Queue *Q = fromHandle<Device>(device)->getQueue(queueFamilyIndex, queueIndex);
  *pQueue = toHandle<VkQueue>(Q);
}

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkGetDeviceQueue2(
    VkDevice device, const VkDeviceQueueInfo2 *pQueueInfo, VkQueue *pQueue) {
  // No protected-memory queue exists (see "Initial Non-Goals": protected
  // memory reports false), so any protected queue request finds nothing,
  // matching the spec's "return VK_NULL_HANDLE if no queue matches".
  if (pQueueInfo->flags != 0) {
    *pQueue = VK_NULL_HANDLE;
    return;
  }
  Queue *Q = fromHandle<Device>(device)->getQueue(pQueueInfo->queueFamilyIndex,
                                                  pQueueInfo->queueIndex);
  *pQueue = toHandle<VkQueue>(Q);
}

VKAPI_ATTR VkResult VKAPI_CALL feme::vulkan::vkDeviceWaitIdle(VkDevice) {
  // vkQueueSubmit executes every submission synchronously (see Sync.h's
  // file comment), so every queue is always already idle by the time this
  // is called.
  return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
feme::vulkan::vkGetDeviceProcAddr(VkDevice, const char *pName) {
  return getDeviceProcAddr(pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
feme::vulkan::vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
  return getInstanceProcAddr(instance, pName);
}
