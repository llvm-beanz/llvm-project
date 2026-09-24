# Vulkan `KHR`/`EXT` extension inventory

This inventory lists every non-disabled `VK_KHR_*` and `VK_EXT_*` extension
in the Vulkan registry used by the tested VK-GL-CTS checkout. Status is
derived from the FeMe Vulkan ICD manifests:

- **Advertised**: exposed by the instance or device extension enumerator.
- **Implemented (core, not advertised by name)**: available through promoted
  core entry points without exposing the extension name.
- **Planned (in scope, not implemented)**: part of FeMe's declared scope and
  assigned to a roadmap milestone.
- **Not implemented**: outside the currently declared implementation plan.

Current registry: VK-GL-CTS `880f31a2bd9c`
(`external/vulkan-docs/src/xml/vk.xml`, `VK_HEADER_VERSION` 358).

| Status | Count |
|---|---:|
| Advertised | 41 |
| Implemented (core, not advertised by name) | 18 |
| Planned (in scope, not implemented) | 49 |
| Not implemented | 194 |
| **Total** | **302** |

Regenerate after changing extension support:

```shell
python3 feme/utils/vk_gen_extension_inventory.py \
  /home/dev/dev/VK-GL-CTS/external/vulkan-docs/src/xml/vk.xml \
  --advertised feme/lib/Vulkan/AdvertisedExtensions.txt \
  --core-promoted feme/lib/Vulkan/CorePromotedNotAdvertisedExtensions.txt \
  --planned feme/lib/Vulkan/PlannedExtensions.txt \
  -o <generated-table>
```

## Inventory

| Extension | Status | Note |
|---|---|---|
| `VK_EXT_4444_formats` | Advertised |  |
| `VK_EXT_acquire_drm_display` | Not implemented |  |
| `VK_EXT_acquire_xlib_display` | Not implemented |  |
| `VK_EXT_astc_decode_mode` | Not implemented |  |
| `VK_EXT_attachment_feedback_loop_dynamic_state` | Not implemented |  |
| `VK_EXT_attachment_feedback_loop_layout` | Not implemented |  |
| `VK_EXT_blend_operation_advanced` | Not implemented |  |
| `VK_EXT_border_color_swizzle` | Not implemented |  |
| `VK_EXT_buffer_device_address` | Not implemented |  |
| `VK_EXT_calibrated_timestamps` | Not implemented |  |
| `VK_EXT_color_write_enable` | Not implemented |  |
| `VK_EXT_conditional_rendering` | Planned (in scope, not implemented) | roadmap H26: split out of roadmap H12's decision to bring this group into scope |
| `VK_EXT_conservative_rasterization` | Not implemented |  |
| `VK_EXT_cooperative_matrix_maintenance1` | Not implemented |  |
| `VK_EXT_custom_border_color` | Not implemented |  |
| `VK_EXT_custom_resolve` | Not implemented |  |
| `VK_EXT_debug_marker` | Not implemented |  |
| `VK_EXT_debug_report` | Not implemented |  |
| `VK_EXT_debug_utils` | Not implemented |  |
| `VK_EXT_depth_bias_control` | Not implemented |  |
| `VK_EXT_depth_clamp_control` | Not implemented |  |
| `VK_EXT_depth_clamp_zero_one` | Not implemented |  |
| `VK_EXT_depth_clip_control` | Not implemented |  |
| `VK_EXT_depth_clip_enable` | Not implemented |  |
| `VK_EXT_depth_range_unrestricted` | Not implemented |  |
| `VK_EXT_descriptor_buffer` | Not implemented |  |
| `VK_EXT_descriptor_heap` | Not implemented |  |
| `VK_EXT_descriptor_indexing` | Planned (in scope, not implemented) | roadmap J2: needed in large part by the ray-tracing CTS corpus, independent of ray tracing itself |
| `VK_EXT_device_address_binding_report` | Not implemented |  |
| `VK_EXT_device_fault` | Not implemented |  |
| `VK_EXT_device_generated_commands` | Not implemented |  |
| `VK_EXT_device_memory_report` | Not implemented |  |
| `VK_EXT_direct_mode_display` | Not implemented |  |
| `VK_EXT_directfb_surface` | Not implemented |  |
| `VK_EXT_discard_rectangles` | Not implemented |  |
| `VK_EXT_display_control` | Not implemented |  |
| `VK_EXT_display_surface_counter` | Not implemented |  |
| `VK_EXT_dynamic_rendering_unused_attachments` | Not implemented |  |
| `VK_EXT_extended_dynamic_state` | Advertised |  |
| `VK_EXT_extended_dynamic_state2` | Planned (in scope, not implemented) | roadmap 1.9.4 (E-series): the 1.3 floor's remaining dynamic states |
| `VK_EXT_extended_dynamic_state3` | Not implemented |  |
| `VK_EXT_external_memory_acquire_unmodified` | Not implemented |  |
| `VK_EXT_external_memory_dma_buf` | Not implemented |  |
| `VK_EXT_external_memory_host` | Not implemented |  |
| `VK_EXT_external_memory_metal` | Not implemented |  |
| `VK_EXT_filter_cubic` | Not implemented |  |
| `VK_EXT_fragment_density_map` | Not implemented |  |
| `VK_EXT_fragment_density_map2` | Not implemented |  |
| `VK_EXT_fragment_density_map_offset` | Not implemented |  |
| `VK_EXT_fragment_shader_interlock` | Planned (in scope, not implemented) | roadmap H24: split out of roadmap H12's decision to bring this group into scope |
| `VK_EXT_frame_boundary` | Not implemented |  |
| `VK_EXT_full_screen_exclusive` | Not implemented |  |
| `VK_EXT_global_priority` | Not implemented |  |
| `VK_EXT_global_priority_query` | Not implemented |  |
| `VK_EXT_graphics_pipeline_library` | Advertised |  |
| `VK_EXT_hdr_metadata` | Not implemented |  |
| `VK_EXT_headless_surface` | Advertised | roadmap H10 (V8): the first surface this ICD implements, per FeMeVulkanDesign.md's WSI decision (Surface.cpp) |
| `VK_EXT_host_image_copy` | Advertised |  |
| `VK_EXT_host_query_reset` | Implemented (core, not advertised by name) | roadmap C6: vkResetQueryPool implemented and hostQueryReset reported true (QueryPool.cpp, EntryPoints.cpp) |
| `VK_EXT_image_2d_view_of_3d` | Not implemented |  |
| `VK_EXT_image_compression_control` | Not implemented |  |
| `VK_EXT_image_compression_control_swapchain` | Not implemented |  |
| `VK_EXT_image_drm_format_modifier` | Not implemented |  |
| `VK_EXT_image_robustness` | Planned (in scope, not implemented) | roadmap E16 |
| `VK_EXT_image_sliced_view_of_3d` | Not implemented |  |
| `VK_EXT_image_tiling_control` | Not implemented |  |
| `VK_EXT_image_view_min_lod` | Not implemented |  |
| `VK_EXT_index_type_uint8` | Not implemented |  |
| `VK_EXT_inline_uniform_block` | Advertised | roadmap L180: dispatch-consumption verified end to end (CTS `descriptorset_random`/`inline_uniform_blocks` both fully passing) |
| `VK_EXT_layer_settings` | Not implemented |  |
| `VK_EXT_legacy_dithering` | Not implemented |  |
| `VK_EXT_legacy_vertex_attributes` | Not implemented |  |
| `VK_EXT_line_rasterization` | Not implemented |  |
| `VK_EXT_load_store_op_none` | Not implemented |  |
| `VK_EXT_map_memory_placed` | Not implemented |  |
| `VK_EXT_memory_budget` | Not implemented |  |
| `VK_EXT_memory_decompression` | Not implemented |  |
| `VK_EXT_memory_priority` | Not implemented |  |
| `VK_EXT_mesh_shader` | Advertised |  |
| `VK_EXT_metal_objects` | Not implemented |  |
| `VK_EXT_metal_surface` | Not implemented |  |
| `VK_EXT_multi_draw` | Not implemented |  |
| `VK_EXT_multisampled_render_to_single_sampled` | Not implemented |  |
| `VK_EXT_multisampled_render_to_swapchain` | Not implemented |  |
| `VK_EXT_mutable_descriptor_type` | Not implemented |  |
| `VK_EXT_nested_command_buffer` | Not implemented |  |
| `VK_EXT_non_seamless_cube_map` | Not implemented |  |
| `VK_EXT_opacity_micromap` | Not implemented |  |
| `VK_EXT_pageable_device_local_memory` | Not implemented |  |
| `VK_EXT_pci_bus_info` | Not implemented |  |
| `VK_EXT_physical_device_drm` | Not implemented |  |
| `VK_EXT_pipeline_creation_cache_control` | Advertised |  |
| `VK_EXT_pipeline_creation_feedback` | Advertised |  |
| `VK_EXT_pipeline_library_group_handles` | Not implemented |  |
| `VK_EXT_pipeline_properties` | Not implemented |  |
| `VK_EXT_pipeline_protected_access` | Advertised |  |
| `VK_EXT_pipeline_robustness` | Advertised |  |
| `VK_EXT_post_depth_coverage` | Not implemented |  |
| `VK_EXT_present_mode_fifo_latest_ready` | Not implemented |  |
| `VK_EXT_present_timing` | Not implemented |  |
| `VK_EXT_primitive_restart_index` | Not implemented |  |
| `VK_EXT_primitive_topology_list_restart` | Not implemented |  |
| `VK_EXT_primitives_generated_query` | Advertised |  |
| `VK_EXT_private_data` | Advertised |  |
| `VK_EXT_provoking_vertex` | Not implemented |  |
| `VK_EXT_queue_family_foreign` | Not implemented |  |
| `VK_EXT_rasterization_order_attachment_access` | Not implemented |  |
| `VK_EXT_ray_tracing_invocation_reorder` | Not implemented |  |
| `VK_EXT_rgba10x6_formats` | Not implemented |  |
| `VK_EXT_robustness2` | Not implemented |  |
| `VK_EXT_sample_locations` | Not implemented |  |
| `VK_EXT_sampler_filter_minmax` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_EXT_scalar_block_layout` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_EXT_separate_stencil_usage` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_EXT_shader_64bit_indexing` | Not implemented |  |
| `VK_EXT_shader_atomic_float` | Not implemented |  |
| `VK_EXT_shader_atomic_float2` | Not implemented |  |
| `VK_EXT_shader_demote_to_helper_invocation` | Advertised |  |
| `VK_EXT_shader_float8` | Not implemented |  |
| `VK_EXT_shader_image_atomic_int64` | Not implemented |  |
| `VK_EXT_shader_long_vector` | Not implemented |  |
| `VK_EXT_shader_module_identifier` | Not implemented |  |
| `VK_EXT_shader_object` | Planned (in scope, not implemented) | roadmap H22: split out of roadmap H12's decision to bring this group into scope |
| `VK_EXT_shader_ocp_microscaling_types` | Not implemented |  |
| `VK_EXT_shader_replicated_composites` | Not implemented |  |
| `VK_EXT_shader_split_barrier` | Not implemented |  |
| `VK_EXT_shader_stencil_export` | Not implemented |  |
| `VK_EXT_shader_subgroup_ballot` | Not implemented |  |
| `VK_EXT_shader_subgroup_partitioned` | Not implemented |  |
| `VK_EXT_shader_subgroup_vote` | Not implemented |  |
| `VK_EXT_shader_tile_image` | Not implemented |  |
| `VK_EXT_shader_uniform_buffer_unsized_array` | Not implemented |  |
| `VK_EXT_shader_viewport_index_layer` | Implemented (core, not advertised by name) | roadmap H3: shaderOutputViewportIndex/shaderOutputLayer both reported true, and genuinely backed now -- gl_ViewportIndex and gl_Layer are real vertex-stage outputs (Executor.cpp's VSViewportOut/VSLayerOut), with no geometry stage in sight for either bit's "no geometry shader required" promise to be moot against |
| `VK_EXT_subgroup_size_control` | Implemented (core, not advertised by name) | roadmap E7: VkPipelineShaderStageRequiredSubgroupSizeCreateInfo/VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT and the four related limit fields (minSubgroupSize/maxSubgroupSize/maxComputeWorkgroupSubgroups/requiredSubgroupSizeStages) implemented via the promoted VK_VERSION_1_3 aggregate struct; extension string itself never added to getSupportedDeviceExtensions (unlike VK_KHR_synchronization2/maintenance5/maintenance6/shader_integer_dot_product, no known CTS case was found requiring it by name regardless of apiVersion) |
| `VK_EXT_subpass_merge_feedback` | Not implemented |  |
| `VK_EXT_surface_maintenance1` | Not implemented |  |
| `VK_EXT_swapchain_colorspace` | Not implemented |  |
| `VK_EXT_swapchain_maintenance1` | Not implemented |  |
| `VK_EXT_texel_buffer_alignment` | Advertised |  |
| `VK_EXT_texture_compression_astc_3d` | Not implemented |  |
| `VK_EXT_texture_compression_astc_hdr` | Planned (in scope, not implemented) | roadmap E21: HDR block formats and decodeASTCBlockHDR exist, but no copy/blit/sampling path consumes one, so the feature bit stays false (E22's own closing note) |
| `VK_EXT_tooling_info` | Advertised |  |
| `VK_EXT_transform_feedback` | Advertised |  |
| `VK_EXT_validation_cache` | Not implemented |  |
| `VK_EXT_validation_features` | Not implemented |  |
| `VK_EXT_validation_flags` | Not implemented |  |
| `VK_EXT_vertex_attribute_divisor` | Not implemented |  |
| `VK_EXT_vertex_attribute_robustness` | Not implemented |  |
| `VK_EXT_vertex_input_dynamic_state` | Not implemented |  |
| `VK_EXT_ycbcr_2plane_444_formats` | Planned (in scope, not implemented) | roadmap E19 declined it while samplerYcbcrConversion is unimplemented; still part of the 1.3 mandatory floor, so it returns as roadmap 1.9.10 K-series work alongside VK_KHR_sampler_ycbcr_conversion |
| `VK_EXT_ycbcr_image_arrays` | Not implemented |  |
| `VK_EXT_zero_initialize_device_memory` | Not implemented |  |
| `VK_KHR_16bit_storage` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_8bit_storage` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_acceleration_structure` | Planned (in scope, not implemented) | roadmap J4 |
| `VK_KHR_android_surface` | Not implemented |  |
| `VK_KHR_bind_memory2` | Implemented (core, not advertised by name) | vkBindBufferMemory2/vkBindImageMemory2 implemented (Buffer.cpp, Image.cpp) |
| `VK_KHR_buffer_device_address` | Planned (in scope, not implemented) | roadmap J1 |
| `VK_KHR_calibrated_timestamps` | Not implemented |  |
| `VK_KHR_compute_shader_derivatives` | Advertised |  |
| `VK_KHR_cooperative_matrix` | Not implemented |  |
| `VK_KHR_copy_commands2` | Implemented (core, not advertised by name) | roadmap D0: vkCmdCopyBuffer2/vkCmdCopyImage2/vkCmdBlitImage2/vkCmdCopyBufferToImage2/vkCmdCopyImageToBuffer2/vkCmdResolveImage2 all implemented as core VK_VERSION_1_3 names; extension string never needed by any known CTS case |
| `VK_KHR_copy_memory_indirect` | Not implemented |  |
| `VK_KHR_create_renderpass2` | Implemented (core, not advertised by name) | vkCreateRenderPass2 and vkCmdBeginRenderPass2/vkCmdNextSubpass2/vkCmdEndRenderPass2 implemented (RenderPass.cpp, CommandBuffer.cpp) |
| `VK_KHR_dedicated_allocation` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_deferred_host_operations` | Planned (in scope, not implemented) | roadmap J3 |
| `VK_KHR_depth_clamp_zero_one` | Not implemented |  |
| `VK_KHR_depth_stencil_resolve` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_descriptor_update_template` | Implemented (core, not advertised by name) | descriptor-set update templates implemented; the push-descriptor template type is explicitly rejected as the separate, unimplemented VK_KHR_push_descriptor (Descriptor.cpp's vkCreateDescriptorUpdateTemplate/vkUpdateDescriptorSetWithTemplate) |
| `VK_KHR_device_address_commands` | Not implemented |  |
| `VK_KHR_device_fault` | Not implemented |  |
| `VK_KHR_device_group` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_device_group_creation` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_display` | Not implemented |  |
| `VK_KHR_display_swapchain` | Not implemented |  |
| `VK_KHR_draw_indirect_count` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_driver_properties` | Implemented (core, not advertised by name) | roadmap C5: VkPhysicalDeviceDriverProperties filled, with a truthful zero VkConformanceVersion and no impersonated VkDriverId (EntryPoints.cpp's fillDriverProperties) |
| `VK_KHR_dynamic_rendering` | Advertised |  |
| `VK_KHR_dynamic_rendering_local_read` | Advertised |  |
| `VK_KHR_extended_flags` | Not implemented |  |
| `VK_KHR_external_fence` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_external_fence_capabilities` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_external_fence_fd` | Not implemented |  |
| `VK_KHR_external_fence_win32` | Not implemented |  |
| `VK_KHR_external_memory` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_external_memory_capabilities` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_external_memory_fd` | Not implemented |  |
| `VK_KHR_external_memory_win32` | Not implemented |  |
| `VK_KHR_external_semaphore` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_external_semaphore_capabilities` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_external_semaphore_fd` | Not implemented |  |
| `VK_KHR_external_semaphore_win32` | Not implemented |  |
| `VK_KHR_format_feature_flags2` | Implemented (core, not advertised by name) | roadmap E24/E25: vkGetPhysicalDeviceFormatProperties2 fills a chained VkFormatProperties3 (EntryPoints.cpp) |
| `VK_KHR_fragment_shader_barycentric` | Planned (in scope, not implemented) | roadmap H25: split out of roadmap H12's decision to bring this group into scope |
| `VK_KHR_fragment_shading_rate` | Planned (in scope, not implemented) | roadmap H23: split out of roadmap H12's decision to bring this group into scope |
| `VK_KHR_get_display_properties2` | Not implemented |  |
| `VK_KHR_get_memory_requirements2` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_get_physical_device_properties2` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_get_surface_capabilities2` | Planned (in scope, not implemented) | roadmap H10 (V8) |
| `VK_KHR_global_priority` | Advertised |  |
| `VK_KHR_image_format_list` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_imageless_framebuffer` | Implemented (core, not advertised by name) | roadmap C6: VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT and VkRenderPassAttachmentBeginInfo both honored (RenderPass.cpp's vkCreateFramebuffer, CommandBuffer.cpp's vkCmdBeginRenderPass) |
| `VK_KHR_incremental_present` | Not implemented |  |
| `VK_KHR_index_type_uint8` | Advertised |  |
| `VK_KHR_internally_synchronized_queues` | Not implemented |  |
| `VK_KHR_line_rasterization` | Advertised |  |
| `VK_KHR_load_store_op_none` | Advertised |  |
| `VK_KHR_maintenance1` | Implemented (core, not advertised by name) | vkTrimCommandPool implemented (CommandBuffer.cpp) |
| `VK_KHR_maintenance10` | Not implemented |  |
| `VK_KHR_maintenance11` | Not implemented |  |
| `VK_KHR_maintenance2` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_maintenance3` | Implemented (core, not advertised by name) | vkGetDescriptorSetLayoutSupport plus VkPhysicalDeviceMaintenance3Properties (Descriptor.cpp, EntryPoints.cpp's fillProperties2Chain) |
| `VK_KHR_maintenance4` | Implemented (core, not advertised by name) | roadmap E4: vkGetDeviceBufferMemoryRequirements/vkGetDeviceImageMemoryRequirements/vkGetDeviceImageSparseMemoryRequirements, maxBufferSize, all implemented as core VK_VERSION_1_3 names; extension string never needed by any known CTS case |
| `VK_KHR_maintenance5` | Advertised |  |
| `VK_KHR_maintenance6` | Advertised |  |
| `VK_KHR_maintenance7` | Not implemented |  |
| `VK_KHR_maintenance8` | Not implemented |  |
| `VK_KHR_maintenance9` | Not implemented |  |
| `VK_KHR_map_memory2` | Advertised |  |
| `VK_KHR_multiview` | Advertised |  |
| `VK_KHR_opacity_micromap` | Not implemented |  |
| `VK_KHR_performance_query` | Not implemented |  |
| `VK_KHR_pipeline_binary` | Not implemented |  |
| `VK_KHR_pipeline_executable_properties` | Not implemented |  |
| `VK_KHR_pipeline_library` | Advertised |  |
| `VK_KHR_pipeline_library_group_handles` | Not implemented |  |
| `VK_KHR_portability_enumeration` | Not implemented |  |
| `VK_KHR_portability_subset` | Not implemented |  |
| `VK_KHR_present_id` | Not implemented |  |
| `VK_KHR_present_id2` | Not implemented |  |
| `VK_KHR_present_mode_fifo_latest_ready` | Not implemented |  |
| `VK_KHR_present_wait` | Not implemented |  |
| `VK_KHR_present_wait2` | Not implemented |  |
| `VK_KHR_push_descriptor` | Advertised |  |
| `VK_KHR_ray_query` | Planned (in scope, not implemented) | roadmap J5 |
| `VK_KHR_ray_tracing_maintenance1` | Planned (in scope, not implemented) | roadmap J7 |
| `VK_KHR_ray_tracing_pipeline` | Planned (in scope, not implemented) | roadmap J6 |
| `VK_KHR_ray_tracing_position_fetch` | Planned (in scope, not implemented) | roadmap J8 |
| `VK_KHR_relaxed_block_layout` | Implemented (core, not advertised by name) | SPIR-V block access uses each member's declared offset/stride rather than imposing or validating a std140 re-layout (SPIRVToLLVMPatterns.cpp's getUniformBlockElement) |
| `VK_KHR_robustness2` | Not implemented |  |
| `VK_KHR_sampler_mirror_clamp_to_edge` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_sampler_ycbcr_conversion` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_separate_depth_stencil_layouts` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_shader_abort` | Not implemented |  |
| `VK_KHR_shader_atomic_int64` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_shader_bfloat16` | Not implemented |  |
| `VK_KHR_shader_clock` | Not implemented |  |
| `VK_KHR_shader_constant_data` | Not implemented |  |
| `VK_KHR_shader_draw_parameters` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_shader_expect_assume` | Advertised |  |
| `VK_KHR_shader_float16_int8` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_shader_float_controls` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_shader_float_controls2` | Planned (in scope, not implemented) | roadmap F3 |
| `VK_KHR_shader_fma` | Not implemented |  |
| `VK_KHR_shader_integer_dot_product` | Advertised |  |
| `VK_KHR_shader_maximal_reconvergence` | Not implemented |  |
| `VK_KHR_shader_non_semantic_info` | Advertised |  |
| `VK_KHR_shader_quad_control` | Not implemented |  |
| `VK_KHR_shader_relaxed_extended_instruction` | Not implemented |  |
| `VK_KHR_shader_subgroup_extended_types` | Implemented (core, not advertised by name) | roadmap C6: reported true, and vacuously so -- no OpGroupNonUniform* operation is converted at all yet (EntryPoints.cpp's fillFeatures2Chain) |
| `VK_KHR_shader_subgroup_rotate` | Advertised |  |
| `VK_KHR_shader_subgroup_uniform_control_flow` | Not implemented |  |
| `VK_KHR_shader_terminate_invocation` | Advertised |  |
| `VK_KHR_shader_untyped_pointers` | Not implemented |  |
| `VK_KHR_shared_presentable_image` | Not implemented |  |
| `VK_KHR_spirv_1_4` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_storage_buffer_storage_class` | Implemented (core, not advertised by name) | the StorageBuffer storage class is accepted directly (SPIRVToLLVMPatterns.cpp's isBufferBlockStorage) |
| `VK_KHR_surface` | Advertised | roadmap H10 (V8): headless surface first (Surface.cpp) |
| `VK_KHR_surface_maintenance1` | Not implemented |  |
| `VK_KHR_surface_protected_capabilities` | Not implemented |  |
| `VK_KHR_swapchain` | Advertised | roadmap H10 (V8): full create/destroy/get-images/acquire/present state machine (Swapchain.cpp) |
| `VK_KHR_swapchain_maintenance1` | Not implemented |  |
| `VK_KHR_swapchain_mutable_format` | Not implemented |  |
| `VK_KHR_synchronization2` | Advertised |  |
| `VK_KHR_timeline_semaphore` | Implemented (core, not advertised by name) | V3: vkGetSemaphoreCounterValue/vkWaitSemaphores/vkSignalSemaphore plus the feature and limit cases (Sync.cpp, EntryPoints.cpp) |
| `VK_KHR_unified_image_layouts` | Not implemented |  |
| `VK_KHR_uniform_buffer_standard_layout` | Implemented (core, not advertised by name) | roadmap C6: reported true, and honest -- no std140 layout restriction was ever enforced to relax (EntryPoints.cpp, SPIRVToLLVMPatterns.cpp) |
| `VK_KHR_variable_pointers` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.1: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_vertex_attribute_divisor` | Advertised |  |
| `VK_KHR_video_decode_av1` | Not implemented |  |
| `VK_KHR_video_decode_h264` | Not implemented |  |
| `VK_KHR_video_decode_h265` | Not implemented |  |
| `VK_KHR_video_decode_queue` | Not implemented |  |
| `VK_KHR_video_decode_vp9` | Not implemented |  |
| `VK_KHR_video_encode_av1` | Not implemented |  |
| `VK_KHR_video_encode_feedback2` | Not implemented |  |
| `VK_KHR_video_encode_h264` | Not implemented |  |
| `VK_KHR_video_encode_h265` | Not implemented |  |
| `VK_KHR_video_encode_intra_refresh` | Not implemented |  |
| `VK_KHR_video_encode_quantization_map` | Not implemented |  |
| `VK_KHR_video_encode_queue` | Not implemented |  |
| `VK_KHR_video_maintenance1` | Not implemented |  |
| `VK_KHR_video_maintenance2` | Not implemented |  |
| `VK_KHR_video_queue` | Not implemented |  |
| `VK_KHR_vulkan_memory_model` | Planned (in scope, not implemented) | core-promoted into Vulkan 1.2: part of the mandatory floor a 1.4 claim inherits (roadmap 1.9.10, K-series) |
| `VK_KHR_wayland_surface` | Not implemented |  |
| `VK_KHR_win32_keyed_mutex` | Not implemented |  |
| `VK_KHR_win32_surface` | Not implemented |  |
| `VK_KHR_workgroup_memory_explicit_layout` | Not implemented |  |
| `VK_KHR_xcb_surface` | Advertised | roadmap H10a (V8): the second surface this ICD implements, backed by libxcb against a real Xvfb-hosted X window (XcbSurface.cpp); only advertised when libxcb was found at configure time |
| `VK_KHR_xlib_surface` | Not implemented |  |
| `VK_KHR_zero_initialize_workgroup_memory` | Advertised |  |
