//===- EntryPoints.cpp - Vulkan command implementations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"
#include "ProcAddr.h"

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
  // V3 bumped this from 1.1 to 1.2 for `vkWaitSemaphores`/
  // `vkSignalSemaphore`/`vkGetSemaphoreCounterValue`'s core (non-`KHR`)
  // names -- see vk_gen_entrypoints.py's `CORE_FEATURES` comment. This ICD
  // still implements only a growing subset of either version's full
  // mandatory surface, exactly as it already did advertising 1.1.
  *pApiVersion = VK_API_VERSION_1_2;
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
          reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreProperties *>(
              Base);
      TimelineSemaphore->maxTimelineSemaphoreValueDifference =
          Info.MaxTimelineSemaphoreValueDifference;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
      auto *IdProps = reinterpret_cast<VkPhysicalDeviceIDProperties *>(Base);
      std::memcpy(IdProps->deviceUUID, Info.DeviceUUID, VK_UUID_SIZE);
      std::memcpy(IdProps->driverUUID, Info.Properties.pipelineCacheUUID,
                  VK_UUID_SIZE);
      IdProps->deviceLUIDValid = VK_FALSE;
      IdProps->deviceNodeMask = 1;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES: {
      auto *DriverProps =
          reinterpret_cast<VkPhysicalDeviceDriverProperties *>(Base);
      fillDriverProperties(Info, *DriverProps);
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
      // (roadmap C6) The promoted twin of
      // `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES`
      // above; both must agree; see PhysicalDeviceInfo.h's field comment
      // for why this is `UINT64_MAX` rather than the spec's `2^31-1` floor.
      Props12->maxTimelineSemaphoreValueDifference =
          Info.MaxTimelineSemaphoreValueDifference;
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
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceUniformBufferStandardLayoutFeatures
                               *>(Base);
      Features->uniformBufferStandardLayout = VK_TRUE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures
                               *>(Base);
      Features->separateDepthStencilLayouts = VK_TRUE;
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
      auto *Features =
          reinterpret_cast<
              VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *>(Base);
      Features->shaderSubgroupExtendedTypes = VK_TRUE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
      auto *Features =
          reinterpret_cast<VkPhysicalDeviceVulkan12Features *>(Base);
      Features->timelineSemaphore = VK_TRUE;
      Features->hostQueryReset = VK_TRUE;
      Features->uniformBufferStandardLayout = VK_TRUE;
      Features->separateDepthStencilLayouts = VK_TRUE;
      Features->shaderSubgroupExtendedTypes = VK_TRUE;
      // (roadmap C6) No `OpGroupNonUniformBroadcast` conversion exists at
      // all (see the dedicated-struct case above), so there is no
      // non-dynamic-index broadcast this bit could be lying about: every
      // broadcast operation this ICD implements (none) supports a dynamic
      // id, vacuously.
      Features->subgroupBroadcastDynamicId = VK_TRUE;
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
    default:
      break;
    }
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
  for (uint32_t I = 0; I < ToCopy; ++I)
    pQueueFamilyProperties[I].queueFamilyProperties = Info.QueueFamilies[I];
  *pQueueFamilyPropertyCount = ToCopy;
}

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice, VkFormat, VkFormatProperties *pFormatProperties) {
  // No buffer or image format is supported yet (V0 does no shader
  // execution and V1-V4 add buffers before V5 adds images); every feature
  // flag is honestly zero.
  *pFormatProperties = VkFormatProperties{};
}

VKAPI_ATTR void VKAPI_CALL feme::vulkan::vkGetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice, VkFormat, VkFormatProperties2 *pFormatProperties) {
  pFormatProperties->formatProperties = VkFormatProperties{};
}

VKAPI_ATTR VkResult VKAPI_CALL
feme::vulkan::vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice, VkFormat, VkImageType, VkImageTiling, VkImageUsageFlags,
    VkImageCreateFlags, VkImageFormatProperties *) {
  // No image is supported yet (see "Initial Non-Goals": images are out of
  // scope before V5); every combination is honestly unsupported. This
  // command is still implemented -- rather than omitted -- because it is a
  // required loader entrypoint regardless of feature support (see
  // "Initial Non-Goals": "An advertised core version may report a feature
  // as unsupported; it may not omit a command").
  return VK_ERROR_FORMAT_NOT_SUPPORTED;
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

VKAPI_ATTR VkResult VKAPI_CALL feme::vulkan::vkCreateDevice(
    VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
  // Only an extension this driver actually implements may be enabled (see
  // `getSupportedDeviceExtensions`); anything else is refused rather than
  // silently accepted and then not honored.
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
