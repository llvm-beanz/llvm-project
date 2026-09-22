# Vulkan core mandatory-feature/limit/extension inventory

This inventory is generated from the Vulkan registry used by the tested
VK-GL-CTS checkout and cross-checked against the feature and extension
manifests maintained beside the FeMe Vulkan ICD. It covers every core feature
from Vulkan 1.0 through 1.4, every 1.3/1.4 limit, and every extension promoted
into those core versions.

Current registry: VK-GL-CTS `880f31a2bd9c`
(`external/vulkan-docs/src/xml/vk.xml`, `VK_HEADER_VERSION` 358).

| Version | Features advertised | Limits enumerated | Promoted extensions implemented |
|---|---:|---:|---:|
| 1.0 | 33 of 55 | n/a | n/a |
| 1.1 | 2 of 12 | n/a | 7 of 23 |
| 1.2 | 20 of 47 | n/a | 8 of 24 |
| 1.3 | 12 of 15 | 45 | 19 of 23 |
| 1.4 | 20 of 21 | 25 | 15 of 16 |
| **Total** | **87 of 150** | **70** | **49 of 86** |

Regenerate after changing feature or promoted-extension support:

```shell
python3 feme/utils/vk_gen_feature_inventory.py \
  /home/dev/dev/VK-GL-CTS/external/vulkan-docs/src/xml/vk.xml \
  --version VK_VERSION_1_0:feature,extension \
  --version VK_VERSION_1_1:feature,extension \
  --version VK_VERSION_1_2:feature,extension \
  --version VK_VERSION_1_3 --version VK_VERSION_1_4 \
  --advertised-features feme/lib/Vulkan/AdvertisedPromotedFeatures.txt \
  --advertised-extensions feme/lib/Vulkan/AdvertisedPromotedExtensions.txt \
  -o <generated-table>
```

## Inventory

| Category | Version | Name | Advertised | Note |
|---|---|---|---|---|
| feature | VK_VERSION_1_0 | `robustBufferAccess` | yes |  |
| feature | VK_VERSION_1_0 | `fullDrawIndexUint32` | no |  |
| feature | VK_VERSION_1_0 | `imageCubeArray` | yes |  |
| feature | VK_VERSION_1_0 | `independentBlend` | yes |  |
| feature | VK_VERSION_1_0 | `geometryShader` | yes |  |
| feature | VK_VERSION_1_0 | `tessellationShader` | yes |  |
| feature | VK_VERSION_1_0 | `sampleRateShading` | yes |  |
| feature | VK_VERSION_1_0 | `dualSrcBlend` | yes |  |
| feature | VK_VERSION_1_0 | `logicOp` | yes |  |
| feature | VK_VERSION_1_0 | `multiDrawIndirect` | yes |  |
| feature | VK_VERSION_1_0 | `drawIndirectFirstInstance` | yes |  |
| feature | VK_VERSION_1_0 | `depthClamp` | yes |  |
| feature | VK_VERSION_1_0 | `depthBiasClamp` | yes |  |
| feature | VK_VERSION_1_0 | `fillModeNonSolid` | yes |  |
| feature | VK_VERSION_1_0 | `depthBounds` | yes |  |
| feature | VK_VERSION_1_0 | `wideLines` | yes |  |
| feature | VK_VERSION_1_0 | `largePoints` | yes |  |
| feature | VK_VERSION_1_0 | `alphaToOne` | yes |  |
| feature | VK_VERSION_1_0 | `multiViewport` | yes |  |
| feature | VK_VERSION_1_0 | `samplerAnisotropy` | yes |  |
| feature | VK_VERSION_1_0 | `textureCompressionETC2` | yes |  |
| feature | VK_VERSION_1_0 | `textureCompressionASTC_LDR` | yes |  |
| feature | VK_VERSION_1_0 | `textureCompressionBC` | yes |  |
| feature | VK_VERSION_1_0 | `occlusionQueryPrecise` | yes |  |
| feature | VK_VERSION_1_0 | `pipelineStatisticsQuery` | yes |  |
| feature | VK_VERSION_1_0 | `vertexPipelineStoresAndAtomics` | yes |  |
| feature | VK_VERSION_1_0 | `fragmentStoresAndAtomics` | yes |  |
| feature | VK_VERSION_1_0 | `shaderTessellationAndGeometryPointSize` | no |  |
| feature | VK_VERSION_1_0 | `shaderImageGatherExtended` | yes | roadmap L125(g): ImageGatherPattern already forwards the Component operand and every femeCpuImageGather*V4F32 runtime entry point handles it; the fresh full CTS run confirms the feature is advertised, with residual texture-gather correctness tracked by L144 |
| feature | VK_VERSION_1_0 | `shaderStorageImageExtendedFormats` | yes |  |
| feature | VK_VERSION_1_0 | `shaderStorageImageMultisample` | yes |  |
| feature | VK_VERSION_1_0 | `shaderStorageImageReadWithoutFormat` | yes |  |
| feature | VK_VERSION_1_0 | `shaderStorageImageWriteWithoutFormat` | yes |  |
| feature | VK_VERSION_1_0 | `shaderUniformBufferArrayDynamicIndexing` | no |  |
| feature | VK_VERSION_1_0 | `shaderSampledImageArrayDynamicIndexing` | no |  |
| feature | VK_VERSION_1_0 | `shaderStorageBufferArrayDynamicIndexing` | no |  |
| feature | VK_VERSION_1_0 | `shaderStorageImageArrayDynamicIndexing` | no |  |
| feature | VK_VERSION_1_0 | `shaderClipDistance` | yes |  |
| feature | VK_VERSION_1_0 | `shaderCullDistance` | yes |  |
| feature | VK_VERSION_1_0 | `shaderFloat64` | no |  |
| feature | VK_VERSION_1_0 | `shaderInt64` | no |  |
| feature | VK_VERSION_1_0 | `shaderInt16` | no |  |
| feature | VK_VERSION_1_0 | `shaderResourceResidency` | no |  |
| feature | VK_VERSION_1_0 | `shaderResourceMinLod` | yes |  |
| feature | VK_VERSION_1_0 | `sparseBinding` | no |  |
| feature | VK_VERSION_1_0 | `sparseResidencyBuffer` | no |  |
| feature | VK_VERSION_1_0 | `sparseResidencyImage2D` | no |  |
| feature | VK_VERSION_1_0 | `sparseResidencyImage3D` | no |  |
| feature | VK_VERSION_1_0 | `sparseResidency2Samples` | no |  |
| feature | VK_VERSION_1_0 | `sparseResidency4Samples` | no |  |
| feature | VK_VERSION_1_0 | `sparseResidency8Samples` | no |  |
| feature | VK_VERSION_1_0 | `sparseResidency16Samples` | no |  |
| feature | VK_VERSION_1_0 | `sparseResidencyAliased` | no |  |
| feature | VK_VERSION_1_0 | `variableMultisampleRate` | no |  |
| feature | VK_VERSION_1_0 | `inheritedQueries` | no |  |
| feature | VK_VERSION_1_1 | `storageBuffer16BitAccess` | no |  |
| feature | VK_VERSION_1_1 | `uniformAndStorageBuffer16BitAccess` | no |  |
| feature | VK_VERSION_1_1 | `storagePushConstant16` | no |  |
| feature | VK_VERSION_1_1 | `storageInputOutput16` | no |  |
| feature | VK_VERSION_1_1 | `multiview` | yes |  |
| feature | VK_VERSION_1_1 | `multiviewGeometryShader` | yes |  |
| feature | VK_VERSION_1_1 | `multiviewTessellationShader` | no |  |
| feature | VK_VERSION_1_1 | `variablePointersStorageBuffer` | no |  |
| feature | VK_VERSION_1_1 | `variablePointers` | no |  |
| feature | VK_VERSION_1_1 | `protectedMemory` | no |  |
| feature | VK_VERSION_1_1 | `samplerYcbcrConversion` | no |  |
| feature | VK_VERSION_1_1 | `shaderDrawParameters` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_16bit_storage` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_bind_memory2` | yes | core, not advertised by name: vkBindBufferMemory2/vkBindImageMemory2 (Buffer.cpp, Image.cpp) |
| extension | VK_VERSION_1_1 | `VK_KHR_dedicated_allocation` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_descriptor_update_template` | yes | core, not advertised by name: descriptor-set update templates (Descriptor.cpp) |
| extension | VK_VERSION_1_1 | `VK_KHR_device_group` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_device_group_creation` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_external_fence` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_external_fence_capabilities` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_external_memory` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_external_memory_capabilities` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_external_semaphore` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_external_semaphore_capabilities` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_get_memory_requirements2` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_get_physical_device_properties2` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_maintenance1` | yes | core, not advertised by name: vkTrimCommandPool (CommandBuffer.cpp) |
| extension | VK_VERSION_1_1 | `VK_KHR_maintenance2` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_maintenance3` | yes | core, not advertised by name: vkGetDescriptorSetLayoutSupport + VkPhysicalDeviceMaintenance3Properties (Descriptor.cpp, EntryPoints.cpp) |
| extension | VK_VERSION_1_1 | `VK_KHR_multiview` | yes | roadmap H2, advertised by name: vkCreateFramebuffer/vkCreateRenderPass/vkCreateRenderPass2 accept layers > 1/a nonzero viewMask (RenderPass.cpp), CommandBuffer.cpp's runDraw runs one draw per set view bit -- dEQP-VK.multiview's own cases enable this extension by name regardless of apiVersion |
| extension | VK_VERSION_1_1 | `VK_KHR_relaxed_block_layout` | yes | core, not advertised by name: declared offsets/strides are used as-is, no std140 re-layout is imposed (SPIRVToLLVMPatterns.cpp) |
| extension | VK_VERSION_1_1 | `VK_KHR_sampler_ycbcr_conversion` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_shader_draw_parameters` | no |  |
| extension | VK_VERSION_1_1 | `VK_KHR_storage_buffer_storage_class` | yes | core, not advertised by name: the StorageBuffer storage class is accepted directly (SPIRVToLLVMPatterns.cpp) |
| extension | VK_VERSION_1_1 | `VK_KHR_variable_pointers` | no |  |
| feature | VK_VERSION_1_2 | `samplerMirrorClampToEdge` | no |  |
| feature | VK_VERSION_1_2 | `drawIndirectCount` | no |  |
| feature | VK_VERSION_1_2 | `storageBuffer8BitAccess` | no |  |
| feature | VK_VERSION_1_2 | `uniformAndStorageBuffer8BitAccess` | no |  |
| feature | VK_VERSION_1_2 | `storagePushConstant8` | no |  |
| feature | VK_VERSION_1_2 | `shaderBufferInt64Atomics` | no |  |
| feature | VK_VERSION_1_2 | `shaderSharedInt64Atomics` | no |  |
| feature | VK_VERSION_1_2 | `shaderFloat16` | no |  |
| feature | VK_VERSION_1_2 | `shaderInt8` | no |  |
| feature | VK_VERSION_1_2 | `descriptorIndexing` | no |  |
| feature | VK_VERSION_1_2 | `shaderInputAttachmentArrayDynamicIndexing` | no |  |
| feature | VK_VERSION_1_2 | `shaderUniformTexelBufferArrayDynamicIndexing` | yes |  |
| feature | VK_VERSION_1_2 | `shaderStorageTexelBufferArrayDynamicIndexing` | yes |  |
| feature | VK_VERSION_1_2 | `shaderUniformBufferArrayNonUniformIndexing` | no |  |
| feature | VK_VERSION_1_2 | `shaderSampledImageArrayNonUniformIndexing` | no |  |
| feature | VK_VERSION_1_2 | `shaderStorageBufferArrayNonUniformIndexing` | no |  |
| feature | VK_VERSION_1_2 | `shaderStorageImageArrayNonUniformIndexing` | no |  |
| feature | VK_VERSION_1_2 | `shaderInputAttachmentArrayNonUniformIndexing` | no |  |
| feature | VK_VERSION_1_2 | `shaderUniformTexelBufferArrayNonUniformIndexing` | no |  |
| feature | VK_VERSION_1_2 | `shaderStorageTexelBufferArrayNonUniformIndexing` | no |  |
| feature | VK_VERSION_1_2 | `descriptorBindingUniformBufferUpdateAfterBind` | yes |  |
| feature | VK_VERSION_1_2 | `descriptorBindingSampledImageUpdateAfterBind` | yes |  |
| feature | VK_VERSION_1_2 | `descriptorBindingStorageImageUpdateAfterBind` | no |  |
| feature | VK_VERSION_1_2 | `descriptorBindingStorageBufferUpdateAfterBind` | yes |  |
| feature | VK_VERSION_1_2 | `descriptorBindingUniformTexelBufferUpdateAfterBind` | yes |  |
| feature | VK_VERSION_1_2 | `descriptorBindingStorageTexelBufferUpdateAfterBind` | yes |  |
| feature | VK_VERSION_1_2 | `descriptorBindingUpdateUnusedWhilePending` | yes |  |
| feature | VK_VERSION_1_2 | `descriptorBindingPartiallyBound` | yes |  |
| feature | VK_VERSION_1_2 | `descriptorBindingVariableDescriptorCount` | yes |  |
| feature | VK_VERSION_1_2 | `runtimeDescriptorArray` | yes |  |
| feature | VK_VERSION_1_2 | `samplerFilterMinmax` | no |  |
| feature | VK_VERSION_1_2 | `scalarBlockLayout` | no |  |
| feature | VK_VERSION_1_2 | `imagelessFramebuffer` | yes |  |
| feature | VK_VERSION_1_2 | `uniformBufferStandardLayout` | yes |  |
| feature | VK_VERSION_1_2 | `shaderSubgroupExtendedTypes` | yes |  |
| feature | VK_VERSION_1_2 | `separateDepthStencilLayouts` | yes |  |
| feature | VK_VERSION_1_2 | `hostQueryReset` | yes |  |
| feature | VK_VERSION_1_2 | `timelineSemaphore` | yes |  |
| feature | VK_VERSION_1_2 | `bufferDeviceAddress` | no |  |
| feature | VK_VERSION_1_2 | `bufferDeviceAddressCaptureReplay` | no |  |
| feature | VK_VERSION_1_2 | `bufferDeviceAddressMultiDevice` | no |  |
| feature | VK_VERSION_1_2 | `vulkanMemoryModel` | no |  |
| feature | VK_VERSION_1_2 | `vulkanMemoryModelDeviceScope` | no |  |
| feature | VK_VERSION_1_2 | `vulkanMemoryModelAvailabilityVisibilityChains` | no |  |
| feature | VK_VERSION_1_2 | `shaderOutputViewportIndex` | yes |  |
| feature | VK_VERSION_1_2 | `shaderOutputLayer` | yes |  |
| feature | VK_VERSION_1_2 | `subgroupBroadcastDynamicId` | yes |  |
| extension | VK_VERSION_1_2 | `VK_EXT_descriptor_indexing` | no |  |
| extension | VK_VERSION_1_2 | `VK_EXT_host_query_reset` | yes | core, not advertised by name: roadmap C6, vkResetQueryPool (QueryPool.cpp) |
| extension | VK_VERSION_1_2 | `VK_EXT_sampler_filter_minmax` | no |  |
| extension | VK_VERSION_1_2 | `VK_EXT_scalar_block_layout` | no |  |
| extension | VK_VERSION_1_2 | `VK_EXT_separate_stencil_usage` | no |  |
| extension | VK_VERSION_1_2 | `VK_EXT_shader_viewport_index_layer` | yes | core, not advertised by name: roadmap H3, shaderOutputViewportIndex/shaderOutputLayer both VK_TRUE (Executor.cpp's VSViewportOut/VSLayerOut) |
| extension | VK_VERSION_1_2 | `VK_KHR_8bit_storage` | no |  |
| extension | VK_VERSION_1_2 | `VK_KHR_buffer_device_address` | no |  |
| extension | VK_VERSION_1_2 | `VK_KHR_create_renderpass2` | yes | core, not advertised by name: vkCreateRenderPass2 and its command family (RenderPass.cpp, CommandBuffer.cpp) |
| extension | VK_VERSION_1_2 | `VK_KHR_depth_stencil_resolve` | no |  |
| extension | VK_VERSION_1_2 | `VK_KHR_draw_indirect_count` | no |  |
| extension | VK_VERSION_1_2 | `VK_KHR_driver_properties` | yes | core, not advertised by name: roadmap C5, VkPhysicalDeviceDriverProperties with a truthful zero VkConformanceVersion (EntryPoints.cpp) |
| extension | VK_VERSION_1_2 | `VK_KHR_image_format_list` | no |  |
| extension | VK_VERSION_1_2 | `VK_KHR_imageless_framebuffer` | yes | core, not advertised by name: roadmap C6, VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT + VkRenderPassAttachmentBeginInfo (RenderPass.cpp, CommandBuffer.cpp) |
| extension | VK_VERSION_1_2 | `VK_KHR_sampler_mirror_clamp_to_edge` | no |  |
| extension | VK_VERSION_1_2 | `VK_KHR_separate_depth_stencil_layouts` | no |  |
| extension | VK_VERSION_1_2 | `VK_KHR_shader_atomic_int64` | no |  |
| extension | VK_VERSION_1_2 | `VK_KHR_shader_float16_int8` | no |  |
| extension | VK_VERSION_1_2 | `VK_KHR_shader_float_controls` | no |  |
| extension | VK_VERSION_1_2 | `VK_KHR_shader_subgroup_extended_types` | yes | core, not advertised by name: roadmap C6; roadmap F2 closed "no OpGroupNonUniform* conversion exists" for spirv.GroupNonUniformRotateKHR specifically, but Rotate exercises no 8/16-bit/bool-typed operand, so this bit's own "extended types" claim is still unvalidated by any real conversion (EntryPoints.cpp) |
| extension | VK_VERSION_1_2 | `VK_KHR_spirv_1_4` | no |  |
| extension | VK_VERSION_1_2 | `VK_KHR_timeline_semaphore` | yes | core, not advertised by name: V3's timeline semaphores (Sync.cpp) |
| extension | VK_VERSION_1_2 | `VK_KHR_uniform_buffer_standard_layout` | yes | core, not advertised by name: roadmap C6, no std140 restriction was ever enforced to relax (EntryPoints.cpp, SPIRVToLLVMPatterns.cpp) |
| extension | VK_VERSION_1_2 | `VK_KHR_vulkan_memory_model` | no |  |
| feature | VK_VERSION_1_3 | `robustImageAccess` | no |  |
| feature | VK_VERSION_1_3 | `inlineUniformBlock` | yes |  |
| feature | VK_VERSION_1_3 | `descriptorBindingInlineUniformBlockUpdateAfterBind` | no |  |
| feature | VK_VERSION_1_3 | `pipelineCreationCacheControl` | yes |  |
| feature | VK_VERSION_1_3 | `privateData` | yes |  |
| feature | VK_VERSION_1_3 | `shaderDemoteToHelperInvocation` | yes |  |
| feature | VK_VERSION_1_3 | `shaderTerminateInvocation` | yes |  |
| feature | VK_VERSION_1_3 | `subgroupSizeControl` | yes |  |
| feature | VK_VERSION_1_3 | `computeFullSubgroups` | yes |  |
| feature | VK_VERSION_1_3 | `synchronization2` | yes |  |
| feature | VK_VERSION_1_3 | `textureCompressionASTC_HDR` | no |  |
| feature | VK_VERSION_1_3 | `shaderZeroInitializeWorkgroupMemory` | yes |  |
| feature | VK_VERSION_1_3 | `dynamicRendering` | yes |  |
| feature | VK_VERSION_1_3 | `shaderIntegerDotProduct` | yes |  |
| feature | VK_VERSION_1_3 | `maintenance4` | yes |  |
| limit | VK_VERSION_1_3 | `minSubgroupSize` | n/a |  |
| limit | VK_VERSION_1_3 | `maxSubgroupSize` | n/a |  |
| limit | VK_VERSION_1_3 | `maxComputeWorkgroupSubgroups` | n/a |  |
| limit | VK_VERSION_1_3 | `requiredSubgroupSizeStages` | n/a |  |
| limit | VK_VERSION_1_3 | `maxInlineUniformBlockSize` | n/a |  |
| limit | VK_VERSION_1_3 | `maxPerStageDescriptorInlineUniformBlocks` | n/a |  |
| limit | VK_VERSION_1_3 | `maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks` | n/a |  |
| limit | VK_VERSION_1_3 | `maxDescriptorSetInlineUniformBlocks` | n/a |  |
| limit | VK_VERSION_1_3 | `maxDescriptorSetUpdateAfterBindInlineUniformBlocks` | n/a |  |
| limit | VK_VERSION_1_3 | `maxInlineUniformTotalSize` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct8BitUnsignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct8BitSignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct8BitMixedSignednessAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct4x8BitPackedUnsignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct4x8BitPackedSignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct4x8BitPackedMixedSignednessAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct16BitUnsignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct16BitSignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct16BitMixedSignednessAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct32BitUnsignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct32BitSignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct32BitMixedSignednessAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct64BitUnsignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct64BitSignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProduct64BitMixedSignednessAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating8BitUnsignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating8BitSignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating16BitUnsignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating16BitSignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating32BitUnsignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating32BitSignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating64BitUnsignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating64BitSignedAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated` | n/a |  |
| limit | VK_VERSION_1_3 | `storageTexelBufferOffsetAlignmentBytes` | n/a |  |
| limit | VK_VERSION_1_3 | `storageTexelBufferOffsetSingleTexelAlignment` | n/a |  |
| limit | VK_VERSION_1_3 | `uniformTexelBufferOffsetAlignmentBytes` | n/a |  |
| limit | VK_VERSION_1_3 | `uniformTexelBufferOffsetSingleTexelAlignment` | n/a |  |
| limit | VK_VERSION_1_3 | `maxBufferSize` | n/a |  |
| extension | VK_VERSION_1_3 | `VK_EXT_4444_formats` | yes | roadmap E19: VK_FORMAT_A4R4G4B4_UNORM_PACK16/A4B4G4R4_UNORM_PACK16 recognized VkFormat values (Format.cpp) |
| extension | VK_VERSION_1_3 | `VK_EXT_extended_dynamic_state` | yes | roadmap C4c: all 12 dynamic states implemented |
| extension | VK_VERSION_1_3 | `VK_EXT_extended_dynamic_state2` | no |  |
| extension | VK_VERSION_1_3 | `VK_EXT_image_robustness` | no |  |
| extension | VK_VERSION_1_3 | `VK_EXT_inline_uniform_block` | yes | roadmap E14: VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK over Descriptor.{h,cpp}'s per-binding storage |
| extension | VK_VERSION_1_3 | `VK_EXT_pipeline_creation_cache_control` | yes | roadmap E9: VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT/VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT implemented (Pipeline.cpp/GraphicsPipeline.cpp/PipelineCache.{h,cpp}) |
| extension | VK_VERSION_1_3 | `VK_EXT_pipeline_creation_feedback` | yes | roadmap E19: VkPipelineCreationFeedbackCreateInfo filled for vkCreateGraphicsPipelines/vkCreateComputePipelines (Pipeline.cpp's fillPipelineCreationFeedback) |
| extension | VK_VERSION_1_3 | `VK_EXT_private_data` | yes | roadmap E10: VkPrivateDataSlot and its four commands (PrivateData.cpp) |
| extension | VK_VERSION_1_3 | `VK_EXT_shader_demote_to_helper_invocation` | yes | roadmap E11: OpDemoteToHelperInvocation conversion pattern (SPIRVToLLVMPatterns.cpp) |
| extension | VK_VERSION_1_3 | `VK_EXT_subgroup_size_control` | yes | core, not advertised by name: roadmap E7, VkPipelineShaderStageRequiredSubgroupSizeCreateInfo and its four limit fields |
| extension | VK_VERSION_1_3 | `VK_EXT_texel_buffer_alignment` | yes | roadmap E18: VkPhysicalDeviceTexelBufferAlignmentProperties and the four promoted limit fields |
| extension | VK_VERSION_1_3 | `VK_EXT_texture_compression_astc_hdr` | no |  |
| extension | VK_VERSION_1_3 | `VK_EXT_tooling_info` | yes | roadmap E19: vkGetPhysicalDeviceToolProperties implemented, truthfully reporting zero tools |
| extension | VK_VERSION_1_3 | `VK_EXT_ycbcr_2plane_444_formats` | no |  |
| extension | VK_VERSION_1_3 | `VK_KHR_copy_commands2` | yes | core, not advertised by name: roadmap D0, all six vkCmd*2 copy commands (CommandBuffer.cpp) |
| extension | VK_VERSION_1_3 | `VK_KHR_dynamic_rendering` | yes | every command and the dynamicRendering feature bit are implemented, including (roadmap E1) the aggregate Vulkan13Features struct |
| extension | VK_VERSION_1_3 | `VK_KHR_format_feature_flags2` | yes | core, not advertised by name: roadmap E24/E25, chained VkFormatProperties3 (EntryPoints.cpp) |
| extension | VK_VERSION_1_3 | `VK_KHR_maintenance4` | yes | core, not advertised by name: roadmap E4, vkGetDevice{Buffer,Image}MemoryRequirements + maxBufferSize |
| extension | VK_VERSION_1_3 | `VK_KHR_shader_integer_dot_product` | yes | roadmap E8: OpSDot/OpUDot/OpSUDot-family spirv->llvm conversion patterns implemented; the 36 integerDotProduct*Accelerated limit bits remain honestly VK_FALSE (not hardware-accelerated on this CPU target) |
| extension | VK_VERSION_1_3 | `VK_KHR_shader_non_semantic_info` | yes | roadmap E19: NonSemantic.* OpExtInst instructions stripped before MLIR deserialization (SPIRVImporter.cpp's stripNonSemanticExtInst) |
| extension | VK_VERSION_1_3 | `VK_KHR_shader_terminate_invocation` | yes | roadmap E12: OpTerminateInvocation conversion pattern (SPIRVToLLVMPatterns.cpp) |
| extension | VK_VERSION_1_3 | `VK_KHR_synchronization2` | yes | roadmap E3: vkCmdPipelineBarrier2/vkQueueSubmit2 and their four peers translate down to Sync.cpp's existing 1-mask model |
| extension | VK_VERSION_1_3 | `VK_KHR_zero_initialize_workgroup_memory` | yes | roadmap E13: Workgroup-storage-class globals zero-initialized once per dispatch |
| feature | VK_VERSION_1_4 | `globalPriorityQuery` | yes | roadmap F1: reported through both the aggregate VkPhysicalDeviceVulkan14Features struct and the dedicated VkPhysicalDeviceGlobalPriorityQueryFeatures struct |
| feature | VK_VERSION_1_4 | `shaderSubgroupRotate` | yes |  |
| feature | VK_VERSION_1_4 | `shaderSubgroupRotateClustered` | yes |  |
| feature | VK_VERSION_1_4 | `shaderFloatControls2` | no |  |
| feature | VK_VERSION_1_4 | `shaderExpectAssume` | yes |  |
| feature | VK_VERSION_1_4 | `rectangularLines` | yes |  |
| feature | VK_VERSION_1_4 | `bresenhamLines` | yes |  |
| feature | VK_VERSION_1_4 | `smoothLines` | yes |  |
| feature | VK_VERSION_1_4 | `stippledRectangularLines` | yes |  |
| feature | VK_VERSION_1_4 | `stippledBresenhamLines` | yes |  |
| feature | VK_VERSION_1_4 | `stippledSmoothLines` | yes |  |
| feature | VK_VERSION_1_4 | `vertexAttributeInstanceRateDivisor` | yes |  |
| feature | VK_VERSION_1_4 | `vertexAttributeInstanceRateZeroDivisor` | yes |  |
| feature | VK_VERSION_1_4 | `indexTypeUint8` | yes |  |
| feature | VK_VERSION_1_4 | `dynamicRenderingLocalRead` | yes |  |
| feature | VK_VERSION_1_4 | `maintenance5` | yes |  |
| feature | VK_VERSION_1_4 | `maintenance6` | yes |  |
| feature | VK_VERSION_1_4 | `pipelineProtectedAccess` | yes |  |
| feature | VK_VERSION_1_4 | `pipelineRobustness` | yes | roadmap F10: reported through both the aggregate VkPhysicalDeviceVulkan14Features struct and the dedicated VkPhysicalDevicePipelineRobustnessFeatures struct |
| feature | VK_VERSION_1_4 | `hostImageCopy` | yes | roadmap F11: reported through both the aggregate VkPhysicalDeviceVulkan14Features struct and the dedicated VkPhysicalDeviceHostImageCopyFeatures struct |
| feature | VK_VERSION_1_4 | `pushDescriptor` | yes |  |
| limit | VK_VERSION_1_4 | `lineSubPixelPrecisionBits` | n/a |  |
| limit | VK_VERSION_1_4 | `maxVertexAttribDivisor` | n/a |  |
| limit | VK_VERSION_1_4 | `supportsNonZeroFirstInstance` | n/a |  |
| limit | VK_VERSION_1_4 | `maxPushDescriptors` | n/a |  |
| limit | VK_VERSION_1_4 | `dynamicRenderingLocalReadDepthStencilAttachments` | n/a |  |
| limit | VK_VERSION_1_4 | `dynamicRenderingLocalReadMultisampledAttachments` | n/a |  |
| limit | VK_VERSION_1_4 | `earlyFragmentMultisampleCoverageAfterSampleCounting` | n/a |  |
| limit | VK_VERSION_1_4 | `earlyFragmentSampleMaskTestBeforeSampleCounting` | n/a |  |
| limit | VK_VERSION_1_4 | `depthStencilSwizzleOneSupport` | n/a |  |
| limit | VK_VERSION_1_4 | `polygonModePointSize` | n/a |  |
| limit | VK_VERSION_1_4 | `nonStrictSinglePixelWideLinesUseParallelogram` | n/a |  |
| limit | VK_VERSION_1_4 | `nonStrictWideLinesUseParallelogram` | n/a |  |
| limit | VK_VERSION_1_4 | `blockTexelViewCompatibleMultipleLayers` | n/a |  |
| limit | VK_VERSION_1_4 | `maxCombinedImageSamplerDescriptorCount` | n/a |  |
| limit | VK_VERSION_1_4 | `fragmentShadingRateClampCombinerInputs` | n/a |  |
| limit | VK_VERSION_1_4 | `defaultRobustnessStorageBuffers` | n/a |  |
| limit | VK_VERSION_1_4 | `defaultRobustnessUniformBuffers` | n/a |  |
| limit | VK_VERSION_1_4 | `defaultRobustnessVertexInputs` | n/a |  |
| limit | VK_VERSION_1_4 | `defaultRobustnessImages` | n/a |  |
| limit | VK_VERSION_1_4 | `copySrcLayoutCount` | n/a |  |
| limit | VK_VERSION_1_4 | `pCopySrcLayouts` | n/a |  |
| limit | VK_VERSION_1_4 | `copyDstLayoutCount` | n/a |  |
| limit | VK_VERSION_1_4 | `pCopyDstLayouts` | n/a |  |
| limit | VK_VERSION_1_4 | `optimalTilingLayoutUUID` | n/a |  |
| limit | VK_VERSION_1_4 | `identicalMemoryTypeRequirements` | n/a |  |
| extension | VK_VERSION_1_4 | `VK_EXT_host_image_copy` | yes | roadmap F11: vkCopyMemoryToImage/vkCopyImageToMemory/vkCopyImageToImage/vkTransitionImageLayout (HostImageCopy.cpp) copy/transition images with no VkCommandBuffer |
| extension | VK_VERSION_1_4 | `VK_EXT_pipeline_protected_access` | yes | roadmap F9 (closed): see the pipelineProtectedAccess feature row above |
| extension | VK_VERSION_1_4 | `VK_EXT_pipeline_robustness` | yes | roadmap F10: VkPipelineRobustnessCreateInfo accepted and validated at both compute and graphics pipeline creation (Pipeline.cpp's resolvePipelineRobustness) |
| extension | VK_VERSION_1_4 | `VK_KHR_dynamic_rendering_local_read` | yes | roadmap F8/F8a/F8b/F8c (closed): see the dynamicRenderingLocalRead feature row above |
| extension | VK_VERSION_1_4 | `VK_KHR_global_priority` | yes | roadmap F1: VkDeviceQueueGlobalPriorityCreateInfo is a no-op at vkCreateDevice; VkQueueFamilyGlobalPriorityProperties reports the full mandatory priority list (LOW/MEDIUM/HIGH/REALTIME) for every queue family |
| extension | VK_VERSION_1_4 | `VK_KHR_index_type_uint8` | yes | roadmap F7: see the indexTypeUint8 feature row above |
| extension | VK_VERSION_1_4 | `VK_KHR_line_rasterization` | yes | roadmap F5: vkCmdSetLineStippleKHR (CommandBuffer.cpp), VkPipelineRasterizationLineStateCreateInfoKHR translation (GraphicsPipeline.cpp) |
| extension | VK_VERSION_1_4 | `VK_KHR_load_store_op_none` | yes | roadmap F13: applyClear's existing LoadOp != VK_ATTACHMENT_LOAD_OP_CLEAR check (CommandBuffer.cpp) already treats VK_ATTACHMENT_LOAD_OP_NONE as "do nothing"; StoreOp is never read to act on at all, so STORE/DONT_CARE/NONE are indistinguishable in this real-memory-backed renderer |
| extension | VK_VERSION_1_4 | `VK_KHR_maintenance5` | yes | roadmap E5: null VkRenderingAttachmentInfo image views, VK_REMAINING_ARRAY_LAYERS in copy/blit/resolve regions (E27), and the rest of the group |
| extension | VK_VERSION_1_4 | `VK_KHR_maintenance6` | yes | roadmap E6: vkCmdBindDescriptorSets2/vkCmdPushConstants2/vkCmdPushDescriptorSet2 (Descriptor.cpp, CommandBuffer.cpp) |
| extension | VK_VERSION_1_4 | `VK_KHR_map_memory2` | yes | roadmap F14: vkMapMemory2/vkUnmapMemory2 (Memory.cpp) wrap the existing vkMapMemory/vkUnmapMemory; restored here (this row's own bookkeeping was missing it, the same drift found and fixed for F5/F7/F8/F9/F12 in roadmap F13) |
| extension | VK_VERSION_1_4 | `VK_KHR_push_descriptor` | yes | roadmap F12: see the pushDescriptor feature row above |
| extension | VK_VERSION_1_4 | `VK_KHR_shader_expect_assume` | yes | roadmap F4: spirv.KHR.AssumeTrue/spirv.KHR.Expect conversion patterns (SPIRVToLLVMPatterns.cpp's AssumeTrueConversionPattern/ExpectConversionPattern) |
| extension | VK_VERSION_1_4 | `VK_KHR_shader_float_controls2` | no |  |
| extension | VK_VERSION_1_4 | `VK_KHR_shader_subgroup_rotate` | yes | roadmap F2: spirv.GroupNonUniformRotateKHR conversion pattern (SPIRVToLLVMPatterns.cpp's RotateConversionPattern) |
| extension | VK_VERSION_1_4 | `VK_KHR_vertex_attribute_divisor` | yes | roadmap F6: VkPipelineVertexInputDivisorStateCreateInfo's per-binding divisor, including the 0 ("every instance reads firstInstance") case (GraphicsPipeline.cpp's translateVertexInput, Executor.cpp's fetch-index formula) |
