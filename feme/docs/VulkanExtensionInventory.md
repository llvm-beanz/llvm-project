# Vulkan `KHR`/`EXT` extension inventory

Every non-disabled `VK_KHR_*`/`VK_EXT_*` extension Vulkan-Headers' `vk.xml`
declares as of this file's own generation, cross-checked against what
`libfeme_vulkan` actually implements -- generated the same way
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) is, straight from
the registry rather than re-derived by hand, by
`feme/utils/vk_gen_extension_inventory.py`.

`VK_KHR_*`/`VK_EXT_*` is the full author-prefix scope this file's own name
promises: `VK_ANDROID_*`/`VK_NV_*`/`VK_AMD_*`/`VK_QCOM_*`/other vendor
prefixes, and the `VK_KHR_*`/`VK_EXT_*` extensions `vk.xml` itself marks
`supported="disabled"` (withdrawn or never shipped) or as VulkanSC-only, are
out of scope and not listed below.

To regenerate this file after a `PhysicalDeviceInfo.cpp`/
`getSupportedDeviceExtensions` change, update
`feme/lib/Vulkan/AdvertisedExtensions.txt`/
`feme/lib/Vulkan/CorePromotedNotAdvertisedExtensions.txt` to match and
re-run:

```shell
python3 feme/utils/vk_gen_extension_inventory.py <path-to-vk.xml> \
    --advertised feme/lib/Vulkan/AdvertisedExtensions.txt \
    --core-promoted feme/lib/Vulkan/CorePromotedNotAdvertisedExtensions.txt \
    -o feme/docs/VulkanExtensionInventory.md
```

(then re-add this intro section by hand -- the script only emits the table
below).

## Findings

- **300 non-disabled `VK_KHR_*`/`VK_EXT_*` extensions exist in the current
  Vulkan registry (149 `KHR`, 151 `EXT`). 17 are advertised by name**
  (`getSupportedDeviceExtensions`, `PhysicalDeviceInfo.cpp`) -- 3 of the 17
  are also part of [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md)'s
  own "39 extensions promoted into 1.3/1.4, 3 advertised" finding
  (`VK_KHR_dynamic_rendering`, `VK_EXT_extended_dynamic_state`,
  `VK_KHR_shader_integer_dot_product`); the other 14 are advertised only
  because a `deqp-vk` case enables them by name regardless of the
  advertised `apiVersion`, per each roadmap E-row's own "measured impact"
  note: `VK_KHR_synchronization2` (E3), `VK_KHR_maintenance5` (E5),
  `VK_KHR_maintenance6` (E6), `VK_EXT_pipeline_creation_cache_control` (E9),
  `VK_EXT_private_data` (E10),
  `VK_EXT_shader_demote_to_helper_invocation` (E11),
  `VK_KHR_shader_terminate_invocation` (E12),
  `VK_KHR_zero_initialize_workgroup_memory` (E13),
  `VK_EXT_inline_uniform_block` (E14), `VK_EXT_texel_buffer_alignment`
  (E18), `VK_EXT_4444_formats`/`VK_EXT_pipeline_creation_feedback`/
  `VK_KHR_shader_non_semantic_info`/`VK_EXT_tooling_info` (E19).
- **3 more are implemented but not advertised by name**
  (`VK_KHR_copy_commands2`, roadmap D0; `VK_KHR_maintenance4`, roadmap E4;
  `VK_EXT_subgroup_size_control`, roadmap E7): each is real, `apiVersion`
  1.4 promotes each into core, and this ICD's own `vk_gen_entrypoints.py`
  resolves every one of their commands as a core `VK_VERSION_1_x` name an
  application can call without enabling the extension by name -- but unlike
  the 14 "enabled by name regardless of `apiVersion`" extensions above, no
  known `deqp-vk` case requires these three specifically by name, so
  `getSupportedDeviceExtensions` was never grown to include them (nothing
  in this ICD's own test suite -- `check-feme`'s `FeMeVulkanTests`, or the
  targeted CTS runs each of D0/E4/E7's own "measured impact" sections
  describes -- currently depends on `vkEnumerateDeviceExtensionProperties`
  reporting these three).
- **The remaining 280 are not implemented at all.** Most are entirely
  outside this ICD's declared scope (a compute-only, headless, single-GPU
  software ICD -- see
  [FeMeVulkanDesign.md](FeMeVulkanDesign.md)'s "Initial Non-Goals"): every
  surface/swapchain/display extension (`VK_KHR_surface`,
  `VK_KHR_swapchain`, `VK_EXT_full_screen_exclusive`, ...; this ICD has no
  presentation engine and, per `EntryPoints.cpp`'s
  `vkEnumerateInstanceExtensionProperties`, does not even distinguish an
  instance-level extension list from the device-level one above), every
  ray-tracing extension (`VK_KHR_ray_tracing_pipeline`,
  `VK_KHR_acceleration_structure`, ...; `dEQP-VK.ray_tracing_pipeline`/
  `ray_query` both report zero failures in
  [VulkanCTSReport.md](VulkanCTSReport.md)'s current headline precisely
  because nothing in that space is advertised), every
  video-decode/encode extension (`VK_KHR_video_*`), every multi-GPU/
  external-memory/external-synchronization extension
  (`VK_KHR_external_memory_*`, `VK_KHR_device_group*`, ...; this ICD is
  single-device and in-process), and graphics-pipeline extensions this
  ICD's own compute-first roadmap has not reached yet (mesh shaders,
  transform feedback, conservative rasterization, blend/line-rasterization
  extensions, ...). A handful are within scope but simply not yet
  implemented (e.g. `VK_EXT_descriptor_buffer`, `VK_EXT_shader_object`,
  `VK_EXT_robustness2`) -- unlike the mandatory 1.3/1.4 floor
  [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) tracks,
  closing any of these 280 is optional roadmap work, not gap-closing
  against a required floor, so none is currently an assigned roadmap row;
  see [Roadmap.md](Roadmap.md) for what *is* assigned.
- **This file's "Advertised"/"Implemented (core, not advertised by name)"
  distinction only ever narrows an extension's own real status, never
  widens it**: every extension row below reflects exactly what
  `getSupportedDeviceExtensions`/a core `VK_VERSION_1_x` entry point
  provides today, cross-checked directly against `PhysicalDeviceInfo.cpp`
  (not inferred from `Vulkan14FeatureInventory.md`'s own, narrower,
  1.3/1.4-promoted-only scope) -- a "Not implemented" row here is not a
  claim that *nothing* related exists (e.g. `VK_EXT_debug_utils` is
  unimplemented, but `vkEnumerateInstanceLayerProperties`/an unsupported
  `pNext` chain link are still handled gracefully elsewhere), only that
  this specific named extension is not usable.

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
| `VK_EXT_conditional_rendering` | Not implemented |  |
| `VK_EXT_conservative_rasterization` | Not implemented |  |
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
| `VK_EXT_descriptor_indexing` | Not implemented |  |
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
| `VK_EXT_extended_dynamic_state2` | Not implemented |  |
| `VK_EXT_extended_dynamic_state3` | Not implemented |  |
| `VK_EXT_external_memory_acquire_unmodified` | Not implemented |  |
| `VK_EXT_external_memory_dma_buf` | Not implemented |  |
| `VK_EXT_external_memory_host` | Not implemented |  |
| `VK_EXT_external_memory_metal` | Not implemented |  |
| `VK_EXT_filter_cubic` | Not implemented |  |
| `VK_EXT_fragment_density_map` | Not implemented |  |
| `VK_EXT_fragment_density_map2` | Not implemented |  |
| `VK_EXT_fragment_density_map_offset` | Not implemented |  |
| `VK_EXT_fragment_shader_interlock` | Not implemented |  |
| `VK_EXT_frame_boundary` | Not implemented |  |
| `VK_EXT_full_screen_exclusive` | Not implemented |  |
| `VK_EXT_global_priority` | Not implemented |  |
| `VK_EXT_global_priority_query` | Not implemented |  |
| `VK_EXT_graphics_pipeline_library` | Not implemented |  |
| `VK_EXT_hdr_metadata` | Not implemented |  |
| `VK_EXT_headless_surface` | Not implemented |  |
| `VK_EXT_host_image_copy` | Not implemented |  |
| `VK_EXT_host_query_reset` | Not implemented |  |
| `VK_EXT_image_2d_view_of_3d` | Not implemented |  |
| `VK_EXT_image_compression_control` | Not implemented |  |
| `VK_EXT_image_compression_control_swapchain` | Not implemented |  |
| `VK_EXT_image_drm_format_modifier` | Not implemented |  |
| `VK_EXT_image_robustness` | Not implemented |  |
| `VK_EXT_image_sliced_view_of_3d` | Not implemented |  |
| `VK_EXT_image_tiling_control` | Not implemented |  |
| `VK_EXT_image_view_min_lod` | Not implemented |  |
| `VK_EXT_index_type_uint8` | Not implemented |  |
| `VK_EXT_inline_uniform_block` | Advertised |  |
| `VK_EXT_layer_settings` | Not implemented |  |
| `VK_EXT_legacy_dithering` | Not implemented |  |
| `VK_EXT_legacy_vertex_attributes` | Not implemented |  |
| `VK_EXT_line_rasterization` | Not implemented |  |
| `VK_EXT_load_store_op_none` | Not implemented |  |
| `VK_EXT_map_memory_placed` | Not implemented |  |
| `VK_EXT_memory_budget` | Not implemented |  |
| `VK_EXT_memory_decompression` | Not implemented |  |
| `VK_EXT_memory_priority` | Not implemented |  |
| `VK_EXT_mesh_shader` | Not implemented |  |
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
| `VK_EXT_pipeline_protected_access` | Not implemented |  |
| `VK_EXT_pipeline_robustness` | Not implemented |  |
| `VK_EXT_post_depth_coverage` | Not implemented |  |
| `VK_EXT_present_mode_fifo_latest_ready` | Not implemented |  |
| `VK_EXT_present_timing` | Not implemented |  |
| `VK_EXT_primitive_restart_index` | Not implemented |  |
| `VK_EXT_primitive_topology_list_restart` | Not implemented |  |
| `VK_EXT_primitives_generated_query` | Not implemented |  |
| `VK_EXT_private_data` | Advertised |  |
| `VK_EXT_provoking_vertex` | Not implemented |  |
| `VK_EXT_queue_family_foreign` | Not implemented |  |
| `VK_EXT_rasterization_order_attachment_access` | Not implemented |  |
| `VK_EXT_ray_tracing_invocation_reorder` | Not implemented |  |
| `VK_EXT_rgba10x6_formats` | Not implemented |  |
| `VK_EXT_robustness2` | Not implemented |  |
| `VK_EXT_sample_locations` | Not implemented |  |
| `VK_EXT_sampler_filter_minmax` | Not implemented |  |
| `VK_EXT_scalar_block_layout` | Not implemented |  |
| `VK_EXT_separate_stencil_usage` | Not implemented |  |
| `VK_EXT_shader_64bit_indexing` | Not implemented |  |
| `VK_EXT_shader_atomic_float` | Not implemented |  |
| `VK_EXT_shader_atomic_float2` | Not implemented |  |
| `VK_EXT_shader_demote_to_helper_invocation` | Advertised |  |
| `VK_EXT_shader_float8` | Not implemented |  |
| `VK_EXT_shader_image_atomic_int64` | Not implemented |  |
| `VK_EXT_shader_long_vector` | Not implemented |  |
| `VK_EXT_shader_module_identifier` | Not implemented |  |
| `VK_EXT_shader_object` | Not implemented |  |
| `VK_EXT_shader_ocp_microscaling_types` | Not implemented |  |
| `VK_EXT_shader_replicated_composites` | Not implemented |  |
| `VK_EXT_shader_split_barrier` | Not implemented |  |
| `VK_EXT_shader_stencil_export` | Not implemented |  |
| `VK_EXT_shader_subgroup_ballot` | Not implemented |  |
| `VK_EXT_shader_subgroup_partitioned` | Not implemented |  |
| `VK_EXT_shader_subgroup_vote` | Not implemented |  |
| `VK_EXT_shader_tile_image` | Not implemented |  |
| `VK_EXT_shader_uniform_buffer_unsized_array` | Not implemented |  |
| `VK_EXT_shader_viewport_index_layer` | Not implemented |  |
| `VK_EXT_subgroup_size_control` | Implemented (core, not advertised by name) | roadmap E7: VkPipelineShaderStageRequiredSubgroupSizeCreateInfo/VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT and the four related limit fields (minSubgroupSize/maxSubgroupSize/maxComputeWorkgroupSubgroups/requiredSubgroupSizeStages) implemented via the promoted VK_VERSION_1_3 aggregate struct; extension string itself never added to getSupportedDeviceExtensions (unlike VK_KHR_synchronization2/maintenance5/maintenance6/shader_integer_dot_product, no known CTS case was found requiring it by name regardless of apiVersion) |
| `VK_EXT_subpass_merge_feedback` | Not implemented |  |
| `VK_EXT_surface_maintenance1` | Not implemented |  |
| `VK_EXT_swapchain_colorspace` | Not implemented |  |
| `VK_EXT_swapchain_maintenance1` | Not implemented |  |
| `VK_EXT_texel_buffer_alignment` | Advertised |  |
| `VK_EXT_texture_compression_astc_3d` | Not implemented |  |
| `VK_EXT_texture_compression_astc_hdr` | Not implemented |  |
| `VK_EXT_tooling_info` | Advertised |  |
| `VK_EXT_transform_feedback` | Not implemented |  |
| `VK_EXT_validation_cache` | Not implemented |  |
| `VK_EXT_validation_features` | Not implemented |  |
| `VK_EXT_validation_flags` | Not implemented |  |
| `VK_EXT_vertex_attribute_divisor` | Not implemented |  |
| `VK_EXT_vertex_attribute_robustness` | Not implemented |  |
| `VK_EXT_vertex_input_dynamic_state` | Not implemented |  |
| `VK_EXT_ycbcr_2plane_444_formats` | Not implemented |  |
| `VK_EXT_ycbcr_image_arrays` | Not implemented |  |
| `VK_EXT_zero_initialize_device_memory` | Not implemented |  |
| `VK_KHR_16bit_storage` | Not implemented |  |
| `VK_KHR_8bit_storage` | Not implemented |  |
| `VK_KHR_acceleration_structure` | Not implemented |  |
| `VK_KHR_android_surface` | Not implemented |  |
| `VK_KHR_bind_memory2` | Not implemented |  |
| `VK_KHR_buffer_device_address` | Not implemented |  |
| `VK_KHR_calibrated_timestamps` | Not implemented |  |
| `VK_KHR_compute_shader_derivatives` | Not implemented |  |
| `VK_KHR_cooperative_matrix` | Not implemented |  |
| `VK_KHR_copy_commands2` | Implemented (core, not advertised by name) | roadmap D0: vkCmdCopyBuffer2/vkCmdCopyImage2/vkCmdBlitImage2/vkCmdCopyBufferToImage2/vkCmdCopyImageToBuffer2/vkCmdResolveImage2 all implemented as core VK_VERSION_1_3 names; extension string never needed by any known CTS case |
| `VK_KHR_copy_memory_indirect` | Not implemented |  |
| `VK_KHR_create_renderpass2` | Not implemented |  |
| `VK_KHR_dedicated_allocation` | Not implemented |  |
| `VK_KHR_deferred_host_operations` | Not implemented |  |
| `VK_KHR_depth_clamp_zero_one` | Not implemented |  |
| `VK_KHR_depth_stencil_resolve` | Not implemented |  |
| `VK_KHR_descriptor_update_template` | Not implemented |  |
| `VK_KHR_device_address_commands` | Not implemented |  |
| `VK_KHR_device_fault` | Not implemented |  |
| `VK_KHR_device_group` | Not implemented |  |
| `VK_KHR_device_group_creation` | Not implemented |  |
| `VK_KHR_display` | Not implemented |  |
| `VK_KHR_display_swapchain` | Not implemented |  |
| `VK_KHR_draw_indirect_count` | Not implemented |  |
| `VK_KHR_driver_properties` | Not implemented |  |
| `VK_KHR_dynamic_rendering` | Advertised |  |
| `VK_KHR_dynamic_rendering_local_read` | Not implemented |  |
| `VK_KHR_extended_flags` | Not implemented |  |
| `VK_KHR_external_fence` | Not implemented |  |
| `VK_KHR_external_fence_capabilities` | Not implemented |  |
| `VK_KHR_external_fence_fd` | Not implemented |  |
| `VK_KHR_external_fence_win32` | Not implemented |  |
| `VK_KHR_external_memory` | Not implemented |  |
| `VK_KHR_external_memory_capabilities` | Not implemented |  |
| `VK_KHR_external_memory_fd` | Not implemented |  |
| `VK_KHR_external_memory_win32` | Not implemented |  |
| `VK_KHR_external_semaphore` | Not implemented |  |
| `VK_KHR_external_semaphore_capabilities` | Not implemented |  |
| `VK_KHR_external_semaphore_fd` | Not implemented |  |
| `VK_KHR_external_semaphore_win32` | Not implemented |  |
| `VK_KHR_format_feature_flags2` | Not implemented |  |
| `VK_KHR_fragment_shader_barycentric` | Not implemented |  |
| `VK_KHR_fragment_shading_rate` | Not implemented |  |
| `VK_KHR_get_display_properties2` | Not implemented |  |
| `VK_KHR_get_memory_requirements2` | Not implemented |  |
| `VK_KHR_get_physical_device_properties2` | Not implemented |  |
| `VK_KHR_get_surface_capabilities2` | Not implemented |  |
| `VK_KHR_global_priority` | Not implemented |  |
| `VK_KHR_image_format_list` | Not implemented |  |
| `VK_KHR_imageless_framebuffer` | Not implemented |  |
| `VK_KHR_incremental_present` | Not implemented |  |
| `VK_KHR_index_type_uint8` | Not implemented |  |
| `VK_KHR_internally_synchronized_queues` | Not implemented |  |
| `VK_KHR_line_rasterization` | Not implemented |  |
| `VK_KHR_load_store_op_none` | Not implemented |  |
| `VK_KHR_maintenance1` | Not implemented |  |
| `VK_KHR_maintenance10` | Not implemented |  |
| `VK_KHR_maintenance11` | Not implemented |  |
| `VK_KHR_maintenance2` | Not implemented |  |
| `VK_KHR_maintenance3` | Not implemented |  |
| `VK_KHR_maintenance4` | Implemented (core, not advertised by name) | roadmap E4: vkGetDeviceBufferMemoryRequirements/vkGetDeviceImageMemoryRequirements/vkGetDeviceImageSparseMemoryRequirements, maxBufferSize, all implemented as core VK_VERSION_1_3 names; extension string never needed by any known CTS case |
| `VK_KHR_maintenance5` | Advertised |  |
| `VK_KHR_maintenance6` | Advertised |  |
| `VK_KHR_maintenance7` | Not implemented |  |
| `VK_KHR_maintenance8` | Not implemented |  |
| `VK_KHR_maintenance9` | Not implemented |  |
| `VK_KHR_map_memory2` | Not implemented |  |
| `VK_KHR_multiview` | Not implemented |  |
| `VK_KHR_opacity_micromap` | Not implemented |  |
| `VK_KHR_performance_query` | Not implemented |  |
| `VK_KHR_pipeline_binary` | Not implemented |  |
| `VK_KHR_pipeline_executable_properties` | Not implemented |  |
| `VK_KHR_pipeline_library` | Not implemented |  |
| `VK_KHR_portability_enumeration` | Not implemented |  |
| `VK_KHR_portability_subset` | Not implemented |  |
| `VK_KHR_present_id` | Not implemented |  |
| `VK_KHR_present_id2` | Not implemented |  |
| `VK_KHR_present_mode_fifo_latest_ready` | Not implemented |  |
| `VK_KHR_present_wait` | Not implemented |  |
| `VK_KHR_present_wait2` | Not implemented |  |
| `VK_KHR_push_descriptor` | Not implemented |  |
| `VK_KHR_ray_query` | Not implemented |  |
| `VK_KHR_ray_tracing_maintenance1` | Not implemented |  |
| `VK_KHR_ray_tracing_pipeline` | Not implemented |  |
| `VK_KHR_ray_tracing_position_fetch` | Not implemented |  |
| `VK_KHR_relaxed_block_layout` | Not implemented |  |
| `VK_KHR_robustness2` | Not implemented |  |
| `VK_KHR_sampler_mirror_clamp_to_edge` | Not implemented |  |
| `VK_KHR_sampler_ycbcr_conversion` | Not implemented |  |
| `VK_KHR_separate_depth_stencil_layouts` | Not implemented |  |
| `VK_KHR_shader_abort` | Not implemented |  |
| `VK_KHR_shader_atomic_int64` | Not implemented |  |
| `VK_KHR_shader_bfloat16` | Not implemented |  |
| `VK_KHR_shader_clock` | Not implemented |  |
| `VK_KHR_shader_constant_data` | Not implemented |  |
| `VK_KHR_shader_draw_parameters` | Not implemented |  |
| `VK_KHR_shader_expect_assume` | Not implemented |  |
| `VK_KHR_shader_float16_int8` | Not implemented |  |
| `VK_KHR_shader_float_controls` | Not implemented |  |
| `VK_KHR_shader_float_controls2` | Not implemented |  |
| `VK_KHR_shader_fma` | Not implemented |  |
| `VK_KHR_shader_integer_dot_product` | Advertised |  |
| `VK_KHR_shader_maximal_reconvergence` | Not implemented |  |
| `VK_KHR_shader_non_semantic_info` | Advertised |  |
| `VK_KHR_shader_quad_control` | Not implemented |  |
| `VK_KHR_shader_relaxed_extended_instruction` | Not implemented |  |
| `VK_KHR_shader_subgroup_extended_types` | Not implemented |  |
| `VK_KHR_shader_subgroup_rotate` | Not implemented |  |
| `VK_KHR_shader_subgroup_uniform_control_flow` | Not implemented |  |
| `VK_KHR_shader_terminate_invocation` | Advertised |  |
| `VK_KHR_shader_untyped_pointers` | Not implemented |  |
| `VK_KHR_shared_presentable_image` | Not implemented |  |
| `VK_KHR_spirv_1_4` | Not implemented |  |
| `VK_KHR_storage_buffer_storage_class` | Not implemented |  |
| `VK_KHR_surface` | Not implemented |  |
| `VK_KHR_surface_maintenance1` | Not implemented |  |
| `VK_KHR_surface_protected_capabilities` | Not implemented |  |
| `VK_KHR_swapchain` | Not implemented |  |
| `VK_KHR_swapchain_maintenance1` | Not implemented |  |
| `VK_KHR_swapchain_mutable_format` | Not implemented |  |
| `VK_KHR_synchronization2` | Advertised |  |
| `VK_KHR_timeline_semaphore` | Not implemented |  |
| `VK_KHR_unified_image_layouts` | Not implemented |  |
| `VK_KHR_uniform_buffer_standard_layout` | Not implemented |  |
| `VK_KHR_variable_pointers` | Not implemented |  |
| `VK_KHR_vertex_attribute_divisor` | Not implemented |  |
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
| `VK_KHR_vulkan_memory_model` | Not implemented |  |
| `VK_KHR_wayland_surface` | Not implemented |  |
| `VK_KHR_win32_keyed_mutex` | Not implemented |  |
| `VK_KHR_win32_surface` | Not implemented |  |
| `VK_KHR_workgroup_memory_explicit_layout` | Not implemented |  |
| `VK_KHR_xcb_surface` | Not implemented |  |
| `VK_KHR_xlib_surface` | Not implemented |  |
| `VK_KHR_zero_initialize_workgroup_memory` | Advertised |  |
