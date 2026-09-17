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

Each extension is in exactly one of four states:

- **Advertised** — `getSupportedDeviceExtensions` (`PhysicalDeviceInfo.cpp`)
  lists it by name.
- **Implemented (core, not advertised by name)** — its functionality exists
  through the promoted core `VK_VERSION_1_x` entry points this ICD's
  advertised `apiVersion` (1.4) already provides, but the name itself is
  never listed. A real, Vulkan-legal state: an application using the core
  name gets the real implementation regardless.
- **Planned (in scope, not implemented)** — nothing of it exists yet, but it
  is inside the declared conformance scope (full Vulkan 1.4 including
  graphics and ray tracing, see
  [FeMeVulkanDesign.md](FeMeVulkanDesign.md)'s "Conformance Target") and has
  an assigned [Roadmap.md](Roadmap.md) row, cited in its note.
- **Not implemented** — none of the above, and, per the membership rule in
  `feme/lib/Vulkan/PlannedExtensions.txt`, outside the declared scope rather
  than merely unfinished.

The third state is new, and is the whole point of regenerating this file:
under the previous compute-only scope, "not implemented" covered both a
tracked gap and a deliberate non-goal, which made the table unusable for
planning exactly where planning mattered most.

To regenerate this file after a `PhysicalDeviceInfo.cpp`/
`getSupportedDeviceExtensions` change, update
`feme/lib/Vulkan/AdvertisedExtensions.txt`/
`CorePromotedNotAdvertisedExtensions.txt`/`PlannedExtensions.txt` to match
and re-run:

```shell
python3 feme/utils/vk_gen_extension_inventory.py <path-to-vk.xml> \
    --advertised feme/lib/Vulkan/AdvertisedExtensions.txt \
    --core-promoted feme/lib/Vulkan/CorePromotedNotAdvertisedExtensions.txt \
    --planned feme/lib/Vulkan/PlannedExtensions.txt \
    -o feme/docs/VulkanExtensionInventory.md
```

(then re-add this intro section by hand -- the script only emits the table
below). Use the `vk.xml` from the VK-GL-CTS checkout under test
(`external/vulkan-docs/src/xml/vk.xml`, `VK_HEADER_VERSION` 358), not the
system package: the system one is older and declares 26 fewer extensions.

## Findings

**301 non-disabled `VK_KHR_*`/`VK_EXT_*` extensions exist in the current
Vulkan registry (149 `KHR`, 152 `EXT`) -- one more than the previous
edition's 300, since this regeneration's own `vk.xml` gained
`VK_EXT_cooperative_matrix_maintenance1` since this file was last
generated; unrelated to this edition's own H12 changes below, and itself
still out of scope (cooperative matrix/vector, per Part 4):**

| Status | Count |
|---|---:|
| Advertised | 36 |
| Implemented (core, not advertised by name) | 18 |
| Planned (in scope, not implemented) | 51 |
| Not implemented (out of scope) | 196 |

- **The 35 advertised** are the ones a `deqp-vk` case enables by name
  regardless of the advertised `apiVersion`, plus the three that predate
  that discipline: `VK_KHR_dynamic_rendering`,
  `VK_EXT_extended_dynamic_state`, `VK_KHR_shader_integer_dot_product`,
  `VK_KHR_synchronization2` (E3), `VK_KHR_maintenance5` (E5),
  `VK_KHR_maintenance6` (E6), `VK_EXT_pipeline_creation_cache_control`
  (E9), `VK_EXT_private_data` (E10),
  `VK_EXT_shader_demote_to_helper_invocation` (E11),
  `VK_KHR_shader_terminate_invocation` (E12),
  `VK_KHR_zero_initialize_workgroup_memory` (E13),
  `VK_EXT_inline_uniform_block` (E14), `VK_EXT_texel_buffer_alignment`
  (E18), `VK_EXT_4444_formats`/`VK_EXT_pipeline_creation_feedback`/
  `VK_KHR_shader_non_semantic_info`/`VK_EXT_tooling_info` (E19),
  `VK_KHR_global_priority` (F1), `VK_KHR_shader_subgroup_rotate` (F2),
  `VK_KHR_shader_expect_assume` (F4), `VK_KHR_line_rasterization` (F5),
  `VK_KHR_vertex_attribute_divisor` (F6), `VK_KHR_index_type_uint8` (F7),
  `VK_KHR_dynamic_rendering_local_read` (F8),
  `VK_EXT_pipeline_protected_access` (F9), `VK_EXT_pipeline_robustness`
  (F10), `VK_EXT_host_image_copy` (F11), `VK_KHR_push_descriptor` (F12),
  and `VK_KHR_load_store_op_none` (F13), `VK_KHR_map_memory2` (F14),
  `VK_KHR_multiview` (H2), and `VK_EXT_mesh_shader` (H6f). `VK_EXT_mesh_shader`
  moved from "Planned" to "Advertised" in an earlier edition now that
  `vkCreateGraphicsPipelines` accepts a mesh pipeline (GraphicsPipeline.cpp),
  `vkCmdDrawMeshTasksEXT`/`vkCmdDrawMeshTasksIndirectEXT`/
  `vkCmdDrawMeshTasksIndirectCountEXT` (CommandBuffer.cpp) route through the
  same prepared-draw code `vkCmdDraw*` already uses, and `taskShader`/
  `meshShader` plus every `VkPhysicalDeviceMeshShaderPropertiesEXT` limit are
  advertised at this implementation's own honest, bounded ceilings
  (EntryPoints.cpp, PhysicalDeviceInfo.cpp) -- see Roadmap.md's H6f entry.
  This edition's own additions, `VK_KHR_xcb_surface` (advertised at the
  instance level, conditional on `libxcb` being found at configure time --
  `FEME_HAVE_XCB`), move from "Not implemented (out of scope)" to
  "Advertised" now that `XcbSurface.cpp` implements it -- see Roadmap.md's
  H10a entry. A prior edition's own additions, `VK_KHR_surface`/
  `VK_EXT_headless_surface`
  (advertised at the instance level, the first two extensions in this file
  distinguished from the device-level list) and `VK_KHR_swapchain` (H10,
  partially closed), moved from "Planned" to "Advertised" once
  `Surface.cpp`/`Swapchain.cpp` implemented the headless surface object model
  and the full swapchain state machine -- see Roadmap.md's H10 entry.
- **The 17 core-but-unadvertised** were 3 until roadmap E-series' own
  audit. A full audit of every core-promoted extension against this ICD's
  own sources (rather than only the ones a roadmap row happened to name)
  found 14 more that are genuinely implemented through their promoted core
  entry points and had simply never been recorded: `VK_KHR_bind_memory2`,
  `VK_KHR_descriptor_update_template`, `VK_KHR_maintenance1`,
  `VK_KHR_maintenance3`, `VK_KHR_relaxed_block_layout`,
  `VK_KHR_storage_buffer_storage_class`, `VK_EXT_host_query_reset`,
  `VK_KHR_create_renderpass2`, `VK_KHR_driver_properties`,
  `VK_KHR_imageless_framebuffer`,
  `VK_KHR_shader_subgroup_extended_types`, `VK_KHR_timeline_semaphore`,
  `VK_KHR_uniform_buffer_standard_layout` and
  `VK_KHR_format_feature_flags2`. Most are roadmap C6/V3-era work whose
  extension-name consequences were never written down. None is newly
  implemented by this edition. Roadmap H3 adds an 18th:
  `VK_EXT_shader_viewport_index_layer` (`shaderOutputViewportIndex`/
  `shaderOutputLayer`, core-promoted into Vulkan 1.2), moving from
  "Planned" now that both bits are genuinely backed by a real
  `gl_ViewportIndex`/`gl_Layer` vertex-stage output.
- **The 51 planned** decompose into four groups, and the membership rule
  is enforced by `PlannedExtensions.txt`'s own header rather than by
  judgement per row: every core-promoted (1.1-1.4) extension this ICD does
  not implement (the mandatory floor a 1.4 claim inherits, including the
  eleven [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md)
  records as only *partially* implemented -- partial is a gap, not a
  status); the ray-tracing set and its dependencies
  (`VK_KHR_acceleration_structure`, `VK_KHR_ray_query`,
  `VK_KHR_ray_tracing_pipeline`, `VK_KHR_deferred_host_operations`,
  `VK_KHR_pipeline_library`, `VK_KHR_buffer_device_address`,
  `VK_KHR_ray_tracing_maintenance1`,
  `VK_KHR_ray_tracing_position_fetch`, roadmap &sect;1.9.8); until this
  edition, the graphics/WSI set (`VK_KHR_surface`, `VK_KHR_swapchain`,
  `VK_EXT_headless_surface`, `VK_KHR_get_surface_capabilities2`, roadmap
  &sect;1.9.7); and, new in this edition, the large optional extension
  groups roadmap H12 (&sect;1.9.7) decided belong in scope for this effort
  -- `VK_EXT_transform_feedback` (H21), `VK_EXT_shader_object` (H22),
  `VK_KHR_fragment_shading_rate` (H23), `VK_EXT_fragment_shader_interlock`
  (H24), `VK_KHR_fragment_shader_barycentric` (H25), and
  `VK_EXT_conditional_rendering` (H26). `VK_EXT_mesh_shader` leaves this
  group in an earlier edition, moving to "Advertised" above; roadmap H10
  (partially closed) now moves `VK_KHR_surface`/`VK_EXT_headless_surface`/
  `VK_KHR_swapchain` the same way, leaving only
  `VK_KHR_get_surface_capabilities2` (still unimplemented) in this group.
- **The 196 out of scope** are what remains after that rule: every
  vendor-neutral extension for a capability class this ICD does not intend
  to provide -- video decode/encode, device groups, external
  memory/synchronization/fences beyond the core-promoted entry points,
  YCbCr sampling beyond its core-promoted floor, cooperative matrix/vector,
  display/DRM/platform-specific surfaces beyond the one planned platform
  surface, and the long tail of optional performance/debug extensions.
  Sparse residency, protected memory, transform feedback, and
  `VK_EXT_shader_object` left this group in this edition, per roadmap
  H12's own decision that only Vulkan video stays out of scope among the
  large groups it named (sparse residency/protected memory promote core
  1.0/1.1 *feature bits*, [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md),
  not a dedicated extension name, so neither has its own row here to
  move). Every remaining extension in this group is optional for a Vulkan
  1.4 submission; that, not "we have not got to it", is why it stays out
  of scope. If a *mandatory* CTS case is ever traced to one of them, it
  moves to "Planned" and gains a roadmap row rather than excusing the
  failure (Roadmap.md &sect;1.9.7's H12 owns that decision).
- **This file's own states only ever narrow an extension's real status,
  never widen it**: every row is cross-checked directly against
  `PhysicalDeviceInfo.cpp` and the implementing source file, not inferred
  from [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md)'s own,
  narrower, promoted-only scope -- and a "Not implemented" row is not a
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
| `VK_EXT_graphics_pipeline_library` | Advertised (`graphicsPipelineLibrary` = `VK_TRUE`; `graphicsPipelineLibraryFastLinking`/`IndependentInterpolationDecoration` both `VK_FALSE`, honestly -- this ICD's "link" is a full `compileGraphicsPipeline` recompile, not a genuinely cheaper path) | roadmap H29: discovered as a blocker while scoping H21a/H21f (`transform_feedback`'s own `*_gpl` sub-groups); real footprint measured at ~127,842 CTS cases (`dEQP-VK.pipeline.pipeline_library.*` alone is 120,483), far larger than transform-feedback's own 15,782-case slice. H29a recognized the feature/properties structs ahead of any object model existing; H29b added the partial-pipeline object model (`GraphicsPipelineLibrary`); H29c closed the loop: `synthesizeLinkedGraphicsPipelineCreateInfo` (`GraphicsPipeline.cpp`) merges linked libraries' captured state (or the call's own directly-supplied state, for a partially-monolithic mix) and reuses `compileGraphicsPipeline` unmodified, now advertised for real; H29d implemented inline shader-module creation (`resolveShaderStageModule`, `Pipeline.h`/`.cpp`), the real gap H29c's own crash-fix deferred. Real `deqp-vk` re-run across the two rows: `graphics_library.*` (836 cases) 12/139/685 -> 81/467/288 -> 83/465/288 (Passed/Failed/NotSupported); `cache.*` (773 cases) -> 297/475/1, unchanged by H29d as expected. H29e closed the hull-stage (and, sharing its root cause, roadmap H21k's domain-stage) `unsupported input system value` gap; `cache.*` totals stay numerically at 297/475/1 since the same cases now hit a distinct, still-open cross-control-point-read limitation tracked as H29g. Remaining `Failed` cases beyond H29e/H29g were characterized by H29f into 9 further scoped rows (H29h-H29n covering `graphics_library.*`'s own distinct gaps -- dominated by an MLIR `spirv.Image` legalization failure, H29h, 387 cases -- and H29o covering `cache.*`'s own new dominant cause, a non-library-specific geometry-stage input-primitive check bug, 217 cases). H29h closed: added the missing `spirv.Image` (extracting a plain image handle out of a combined `!spirv.sampled_image`) conversion pattern (`ImagePattern`, `SPIRVToLLVMPatterns.cpp`), the mirror of the existing `SampledImagePattern`. A real `independent_sets_random.*` re-run (720 cases) confirms the targeted diagnostic is fully gone (387 -> 0) but the sub-group's own pass rate barely moved (36/720 passed) since a distinct, deeper FeMe-CPU-target resource-binding-normalization gap was waiting underneath and now dominates its 414 remaining failures (378 `"... is a register-bound resource handle ..."` plus 21 an `llvm.getelementptr` type mismatch); filed as new follow-on row H29p. H29i closed: `captureGraphicsPipelineLibraryState`'s own `captureLibraryStage` now deep-copies a stage's chained `VkShaderModuleCreateInfo` too (`GraphicsPipelineLibraryStage::InlineModuleWords`), not just its `module` handle, so H29d's own inline-shader-module path survives a library merge; `addLinkedStage` reconstructs the equivalent chained struct when synthesizing a linked pipeline's stages. A real full `graphics_library.*` re-run (836 cases, cumulative with H29h) confirms the targeted diagnostic is fully gone (23 -> 0) and moves the group to 99/449/288 (Passed/Failed/NotSupported); every remaining failure matches an already-filed row, no new blocker. H29j closed: `translateDepthStencilState` no longer rejects a pipeline for statically (or dynamically) enabling depth/stencil test/write against a render target with no matching attachment -- per the Vulkan spec this is a legal no-op, not a creation-time error, confirmed by the *monolithic* sibling of the targeted CTS case hitting the exact same diagnostic (proving this was never graphics-pipeline-library-specific despite the row's own original diagnosis). Real re-run: `misc.bind_null_descriptor_set.*` 0/7 -> 7/7, `misc.other.null_descriptor_set_in_monolithic_pipeline` 0/1 -> 1/1, full `graphics_library.*` 99/449/288 -> 110/438/288; `cache.*` unaffected as expected. Two independently-real gaps found while ruling out the merge hypothesis (`synthesizeLinkedGraphicsPipelineCreateInfo` never forwarding `Result.pNext`/`Result.pDynamicState`) filed as new row H29q. H29k (6 real `mesh_frag.case_1*` rendering-mismatch failures in `independent_sets_random.*`) partially fixed: `Executor.cpp`'s `executeDraws` no longer collapses its rasterization extent to `0x0` for a draw with no attachments of any kind (a legal shape this sub-group's own fragment-side-effect-only cases use), falling back to `Draw.Scissors`'s own union instead -- but this alone does not flip any of the 6 targeted cases, since a second, distinct gap (a mesh shader's `gl_PrimitiveTriangleIndicesEXT` output has no canonicalized lowering at all and never reaches `FemeMeshArgs::PrimitiveIndices`, leaving every primitive after the first degenerate) still blocks every one of them; filed as new row H29r. H29r closed, and H29k with it: the row's own reduction corrected its premise -- roadmap H6l had already canonicalized `gl_PrimitiveTriangleIndicesEXT[k] = uvec3(...)` into ordinary `feme.stage.output.store` calls, so no new op was needed; the real gap was that the resulting signature element carried no system value and so was routed into per-primitive *attribute* storage instead of `FemeMeshArgs::PrimitiveIndices`. Added `SignatureSystemValue::PrimitiveIndices` (SPIR-V BuiltIns 5294/5295/5296, forced `PerPrimitive` since glslang does not decorate these `PerPrimitiveEXT`) and a `lowerMeshPrimitiveIndicesStore` in `MeshOutputWrapper.cpp` writing the flat primitive-major array through a new `mesh_primitive_indices` wave-body parameter. Real re-run: all 6 targeted `mesh_frag.case_1*` cases now `Pass`, moving `independent_sets_random.*` (720 cases) 36/414/270 -> 42/408/270; a `mesh_shader.ext.smoke.*` control run (measured with and without the fix) is unchanged at 20/29/18, that group's own failures all being upstream of this gap. H29l closed, and its own reduction corrected the row's premise twice over: the rejected fragment stage is not one "with no color output at all" (it is `layout(location=0) out uvec4 color;` against a matching `VK_FORMAT_R8G8B8A8_UINT` attachment), and nothing about the failure is graphics-pipeline-library-specific -- `validateStageInterfaces` hard-coded `Float` as the only legal fragment-output component type, stale relative to roadmap H8p which had already taught the draw-time twin to derive it from the attachment format, and even deriving it was insufficient because LLVM's signless integers mean a SPIR-V-sourced signature reports `SInt` for *every* integer; fixed by a shared `isCompatibleColorComponentType` (`Graphics/Pipeline.h`) applied at both the creation-time and draw-time sites. Real re-run: the diagnostic goes 6 -> 0 and all 6 `misc.other.view_index_from_device_index*` cases clear pipeline creation for the first time, but `graphics_library.*` totals stay at 116/432/288 because they now fail one phase later at `vkQueueSubmit` on an unrelated, larger gap (`gl_ViewIndex` is unimplemented for the tessellation and geometry stages entirely), filed as new row H29s. H29m closed: `captureGraphicsPipelineLibraryState` never captured a chained `VkPipelineRenderingCreateInfo` at all, so every graphics-pipeline-library build whose render target was declared through *dynamic rendering* rather than a `VkRenderPass` lost its attachment formats outright and was rejected at link time for having no render target; it is now captured (with `pColorAttachmentFormats` deep-copied) only for the fragment-output-interface part -- the one part that owns the render target's formats, a gating that is load-bearing rather than merely tidy, since `bad_rendering_create_info` chains a deliberately-junk `0xdeadbeef` pointer onto the other parts specifically to catch a driver that reads them -- and re-chained onto the synthesized create info's `pNext` at link time. This also corrects the row's own premise: the `*_rendering_create_info*` cases are positive tests, not "deliberately-negative shapes", and nothing was being validated too early. Real re-run: diagnostic 5 -> 0, all 5 affected cases now `Pass` (3 `misc.other.*_rendering_create_info*` plus 2 `misc.view_mask.*` the row had not attributed), `graphics_library.*` 116/432/288 -> 121/427/288 with a pass-set diff confirming zero regressions. H29n closed, implementing **nested pipeline libraries** -- previously absent outright: the extension lets a library itself be built by linking other libraries to arbitrary depth (a library create call may chain its own `VkPipelineLibraryCreateInfoKHR`), but `captureGraphicsPipelineLibraryState` ignored that chain entirely, so an intermediate tree node captured none of its children's state and a node owning no part of its own captured nothing at all. Each linked library's already-flattened state is now folded in at capture time with the flags unioned, which handles a tree of any depth inductively rather than by traversal. This also corrects the row's premise -- there is no "presence check ordering" issue; the `fast.N_NN_NN` case names encode a library *tree*, and every failing configuration was of depth 3 or more while every depth-2 one already passed. Real re-run: diagnostic 4 -> 0, `graphics_library.{fast,optimize}.*` now 15/15 and 14/14, full `graphics_library.*` 121/427/288 -> 131/417/288 with zero regressions; the row's own 4-case footprint was under-counted, 6 further cases having failed silently as rendering mismatches with the same root cause |
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
| `VK_EXT_mesh_shader` | Advertised | roadmap H29r: a mesh entry's `gl_PrimitiveTriangleIndicesEXT`/`gl_PrimitiveLineIndicesEXT`/`gl_PrimitivePointIndicesEXT` output is now classified as `SignatureSystemValue::PrimitiveIndices` and lowered into `FemeMeshArgs::PrimitiveIndices`; before this every emitted primitive degenerated to `(vertex 0, vertex 0, vertex 0)` and never rasterized. Roadmap H31: fixed two bugs blocking `dEQP-VK.mesh_shader.ext.api.draw.*`'s direct (`no_task_shader`) multi-draw shape -- a `Linearize.cpp` constant-mask-tracking loss across chained uniform diamonds (unrelated to mesh specifically, but this CTS shader's own control flow was the first to exercise it two diamonds deep), and an `Executor.cpp` gap where a mesh entry's `PerPrimitive`-frequency output (`perprimitiveEXT out vec4 primitiveColor[]`) was never merged into the storage the fragment stage reads varyings back from, always reading zero. `api.draw.*` moves 14/102/424 -> 58/58/424 (Passed/Failed/NotSupported), with every remaining `Fail` confirmed to be the unrelated `with_task_shader` pipeline-compile bug; see `VulkanCTSReport.md`'s "Roadmap H31: measured impact" Roadmap H106: `gl_CullPrimitiveEXT` (`BuiltIn CullPrimitiveEXT`) is now classified as `SignatureSystemValue::CullPrimitive` and consulted by `Executor.cpp`'s rasterizer to skip a culled primitive outright; before this it mapped to `None` and was silently ignored, so every primitive rasterized regardless of the shader's own culling intent. `dEQP-VK.mesh_shader.ext.builtin.cull_primitives` now passes outright. Roadmap H111(b): `SIMDize.cpp`'s `FunctionWidener::widenMaskedAllocaStore` now replicates an unconditional (uniform) store into a per-lane-private `MaskedAllocas` local across every lane's own copy, instead of leaving only lane 0's copy initialized; this was the root cause of `dEQP-VK.mesh_shader.ext.smoke.*.fullscreen_gradient`'s all-black render (a mesh entry's function-local `const vec4 positions[4]`/`colors[4]` arrays, read back per-vertex), which now passes outright for all 3 variants. A full `mesh_shader.ext.*` re-run confirms 439 Pass/0 Fail/26,482 NotSupported -- every currently-`Supported` case now passes. |
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
| `VK_EXT_pipeline_creation_cache_control` | Advertised | roadmap H29o: a pipeline satisfied from a `VkPipelineCache` now keeps its reflected tessellation/geometry/mesh stage state, which previously only a real compile ever produced |
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
| `VK_EXT_primitives_generated_query` | Advertised | roadmap H21d: real primitive-count query semantics reusing the existing pipeline-statistics `QueryPool` infrastructure; `primitivesGeneratedQuery` feature `VK_TRUE`, `primitivesGeneratedQueryWithRasterizerDiscard` stays `VK_FALSE` (blocked on roadmap H21g), `WithNonZeroStreams` stays `VK_FALSE` (blocked on roadmap H173, multi-stream end-to-end validation; H21e/H39 -- the ABI/wrapper support and MLIR dialect-op blocker -- are both closed) |
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
| `VK_EXT_transform_feedback` | Advertised (single-stream capture, including from a geometry stage; `primitives_generated_query` implemented) | roadmap H21c: real vertex-shader-output capture to bound XFB buffers; H21e: geometry-stage output capture, `RasterizationStream` selection; `transformFeedback`/`transformFeedbackDraw`/`primitivesGeneratedQuery` features `VK_TRUE`; `geometryStreams`/`transformFeedbackRasterizationStreamSelect`/`primitivesGeneratedQueryWithNonZeroStreams` stay `VK_FALSE` pending H173(b) (independent per-stream XFB capture in `Executor.cpp` plus a real CTS `dEQP-VK.transform_feedback.*` multi-stream sweep; H173(a)'s own real-SPIR-V-import `Stream`-decoration reflection plumbing and end-to-end rasterization-selection test are now done) |
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
| `VK_KHR_compute_shader_derivatives` | Implemented (both `DerivativeGroupLinearKHR` and `DerivativeGroupQuadsKHR`) | roadmap L69/L69(a): advertised, with `computeDerivativeGroupLinear=VK_TRUE`/`computeDerivativeGroupQuads=VK_TRUE` feature bits and both execution modes recognized at pipeline-creation time; both modes compile and validate end to end -- `DerivativeGroupLinearKHR` because feme's flat/linear compute-stage lane assignment already matches its spec-defined grouping, `DerivativeGroupQuadsKHR` because that same physical lane assignment is now reinterpreted into real 2x2 spatial tiles (`WaveLowering.cpp`'s `decomposeQuadTiledComponent`) before being handed to the same fragment-quad derivative-shuffle math. Real CTS payoff was initially masked by two unrelated, pre-existing gaps -- roadmap L70 (all compute-stage image sampling failed resource-handle normalization regardless of derivative use, hit by every explicit-`Grad` case) and roadmap L7 (an `unknown extension: SPV_KHR_compute_shader_derivatives` SPIR-V-import gap in MLIR's own SPIR-V dialect `Extension` enum, hit by every implicit-LOD case that needs the extension declared at all, `Grad` or `Quads`) -- both since fixed (L70 by roadmap L70/L71/L72/L72(c); the import gap by roadmap L60's own closing session, adding the missing `Extension` enum case upstream in `mlir/include/mlir/Dialect/SPIRV/IR/SPIRVBase.td`). A real CTS re-run of `dEQP-VK.glsl.texture_functions.texturegrad{,offset}.*_compute` (242 cases combined) now shows genuine Pass counts for the first time (89 Pass combined), with every remaining `Fail` confirmed to be the already-tracked, by-design `isampler*`/`usampler*` filtered-integer-sampling exclusion (roadmap L60(b)), not a new gap |
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
| `VK_KHR_pipeline_library` | Advertised | roadmap H29c: advertised alongside `VK_EXT_graphics_pipeline_library` (a hard dependency per `vk.xml`); defines only `VkPipelineLibraryCreateInfoKHR` (no new commands), now recognized for real on `vkCreateGraphicsPipelines` |
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
| `VK_KHR_shader_subgroup_extended_types` | Implemented (core, not advertised by name) | roadmap C6: reported true; UPDATE (roadmap L85): no longer vacuous for `Vote`/`Ballot`-family `OpGroupNonUniform*` operations (both now legalized/lowered), though 8/16-bit and boolean-typed group operands this feature bit specifically covers still aren't exercised by anything converted so far |
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
