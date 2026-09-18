# FeMe Vulkan ICD: Vulkan CTS Status Report

This report is a current measurement of `libfeme_vulkan`, not a history of
individual fixes. The roadmap owns the remaining work and the design documents
own implementation decisions.

## Scope and provenance

- FeMe source revision under test: `ac4590b42fb8`
- Documentation/inventory revision: `e7c884c84462`
- VK-GL-CTS revision: `880f31a2bd9cd0659f84f3f80dafd07f2e693f6d`
  (`vulkan-cts-1.4.6.2-525-g880f31a2`)
- CTS source tree: no tracked modifications; `build/` and `run/` are local
  generated directories
- Device: `FeMe CPU Vulkan Device`
- Build: `Release`, `LLVM_ENABLE_ASSERTIONS=ON`,
  `CMAKE_CXX_COMPILER_LAUNCHER=ccache`
- `check-feme`: 3,157 passed, 3 unsupported, 0 failed
- Registry used by the inventories: `VK_HEADER_VERSION` 358

The inventory audit performed with this run reports 87 of 150 Vulkan
1.1-1.4 feature bits advertised, 49 of 86 promoted extensions implemented,
and 41 extensions advertised by name. See
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md).

## Method

The CTS generated 3,244,369 cases in 54 top-level `dEQP-VK` groups. Every
group was traversed against the explicit build-tree ICD:

```console
VK_DRIVER_FILES=$PWD/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  vulkaninfo --summary | grep deviceName
```

Every CTS invocation used `--deqp-shadercache=disable`; reusing CTS's on-disk
shader cache across resumed processes can create false crash cascades. Cases
were split into 1,800-case processes and six groups ran concurrently.
Interrupted batches resumed after the last started case, with up to nine
attempts. A final six-worker recovery pass retried every remaining case with a
20-minute process timeout.

Roadmap D4 now makes this recovery accounting reproducible:
`feme/utils/vk_cts_reconcile.py` reads all QPA files for a run and accepts a
complete result only when its `#endTestCaseResult` marker is present. Given the
generated case list, it reports unrun cases in recovery order, rejects
contradictory retries, and compares the current per-case result map with a
baseline map. It also accepts an expected-failure case list, so G2's future
full-run job can distinguish ordinary known failures from regressions without
masking crashes, timeouts, or missing results.

`deqp-vk` returns nonzero when an ordinary test fails, so completion was
derived from per-case log records rather than the process exit code. A case is:

- **Crashed** when its process ended after printing the case name but before
  recording a result.
- **Timed out** when that incomplete process reached the 20-minute ceiling.
  This also includes 567 known `_requiredsubgroupsize128` subgroup compilation
  long poles classified explicitly under roadmap L92.
- **Unrun** only when no result was produced before the bounded recovery budget
  was exhausted.

## G2(a) reconciled failure baseline

The retained QPAs were reconciled independently for all 54 groups with
`vk_cts_reconcile.py --write-failures`. The checked-in result is
[`test/Vulkan/Inputs/vk-cts-expected-failures.txt`](../test/Vulkan/Inputs/vk-cts-expected-failures.txt):
160,248 completed `Fail` cases in generated case-list order. Its comments
record the FeMe source revision (`aa5742ca7ed1`), CTS revision, case-list
size, and payload SHA-256
(`29fcc2ce64beaf59b02cbaafe424ecc8232645d7cd9175b1368a5d58cdf19127`).

The QPA-complete map contains 3,237,254 results and 7,115 cases without a
completed QPA record. The latter comprises the reported crashes, timeouts,
and recovery tail, plus 645 records counted in the earlier log-derived
headline but lacking a matching completed QPA record. They are intentionally
absent from the baseline: G2 must never convert a process termination or an
incomplete result into an expected ordinary failure.

## Headline

| Result | Count | Share of total |
|---|---:|---:|
| Total | 3,244,369 | 100.0000% |
| Measured | 3,238,716 | 99.8258% |
| Pass | 507,559 | 15.6443% |
| Fail | 160,461 | 4.9458% |
| NotSupported | 2,569,827 | 79.2088% |
| QualityWarning | 43 | 0.0013% |
| InternalError | 9 | 0.0003% |
| Crashed | 243 | 0.0075% |
| Timed out | 574 | 0.0177% |
| Unrun | 5,653 | 0.1742% |

All totals satisfy:

```text
Measured = Pass + Fail + NotSupported + QualityWarning
         + InternalError + Crashed + TimedOut
Total = Measured + Unrun
```

All 54 groups were entered and 53 have no unrun cases. Forty-seven groups
completed without a crash, timeout, or unrun case.

## Complete group accounting

| Group | Total | Measured | Pass | Fail | NotSupported | QW | IE | Crash | Timeout | Unrun |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `api` | 267,504 | 267,504 | 96,343 | 18,502 | 152,656 | 0 | 0 | 3 | 0 | 0 |
| `binding_model` | 150,289 | 150,289 | 72,988 | 14,684 | 62,617 | 0 | 0 | 0 | 0 | 0 |
| `clipping` | 308 | 308 | 298 | 0 | 10 | 0 | 0 | 0 | 0 | 0 |
| `compute` | 61,460 | 61,460 | 656 | 29 | 60,775 | 0 | 0 | 0 | 0 | 0 |
| `conditional_rendering` | 1,030 | 1,030 | 0 | 0 | 1,030 | 0 | 0 | 0 | 0 | 0 |
| `cooperative_vector` | 53,562 | 53,562 | 0 | 0 | 53,562 | 0 | 0 | 0 | 0 | 0 |
| `data_graph` | 12,632 | 12,632 | 0 | 0 | 12,632 | 0 | 0 | 0 | 0 | 0 |
| `depth` | 8 | 8 | 0 | 0 | 8 | 0 | 0 | 0 | 0 | 0 |
| `descriptor_indexing` | 115 | 115 | 0 | 0 | 115 | 0 | 0 | 0 | 0 | 0 |
| `device_group` | 21 | 21 | 2 | 7 | 12 | 0 | 0 | 0 | 0 | 0 |
| `dgc` | 4,734 | 4,734 | 0 | 0 | 4,734 | 0 | 0 | 0 | 0 | 0 |
| `draw` | 29,451 | 29,451 | 2,879 | 379 | 26,193 | 0 | 0 | 0 | 0 | 0 |
| `drm_format_modifiers` | 1,572 | 1,572 | 0 | 0 | 1,572 | 0 | 0 | 0 | 0 | 0 |
| `dynamic_state` | 671 | 671 | 181 | 70 | 420 | 0 | 0 | 0 | 0 | 0 |
| `fragment_operations` | 151 | 151 | 101 | 20 | 30 | 0 | 0 | 0 | 0 | 0 |
| `fragment_shader_interlock` | 576 | 576 | 0 | 0 | 576 | 0 | 0 | 0 | 0 | 0 |
| `fragment_shading_barycentric` | 20,991 | 20,991 | 0 | 0 | 20,991 | 0 | 0 | 0 | 0 | 0 |
| `fragment_shading_rate` | 110,603 | 110,603 | 0 | 0 | 110,603 | 0 | 0 | 0 | 0 | 0 |
| `geometry` | 200 | 200 | 148 | 41 | 11 | 0 | 0 | 0 | 0 | 0 |
| `glsl` | 28,420 | 28,420 | 12,602 | 5,336 | 10,482 | 0 | 0 | 0 | 0 | 0 |
| `graphicsfuzz` | 757 | 757 | 515 | 210 | 8 | 0 | 0 | 20 | 4 | 0 |
| `image` | 143,086 | 143,086 | 14,005 | 6,940 | 122,141 | 0 | 0 | 0 | 0 | 0 |
| `image_processing` | 1,211 | 1,211 | 0 | 0 | 1,211 | 0 | 0 | 0 | 0 | 0 |
| `imageless_framebuffer` | 12 | 12 | 4 | 0 | 8 | 0 | 0 | 0 | 0 | 0 |
| `info` | 23 | 23 | 18 | 2 | 3 | 0 | 0 | 0 | 0 | 0 |
| `memory` | 6,364 | 6,364 | 4,921 | 65 | 1,378 | 0 | 0 | 0 | 0 | 0 |
| `memory_model` | 18,530 | 18,530 | 15 | 128 | 18,387 | 0 | 0 | 0 | 0 | 0 |
| `mesh_shader` | 28,044 | 28,044 | 451 | 6 | 27,587 | 0 | 0 | 0 | 0 | 0 |
| `multiview` | 838 | 838 | 571 | 72 | 195 | 0 | 0 | 0 | 0 | 0 |
| `pipeline` | 1,172,229 | 1,166,576 | 202,970 | 82,906 | 880,467 | 42 | 9 | 179 | 3 | 5,653 |
| `postmortem` | 24 | 24 | 0 | 0 | 24 | 0 | 0 | 0 | 0 | 0 |
| `protected_memory` | 6,000 | 6,000 | 0 | 0 | 6,000 | 0 | 0 | 0 | 0 | 0 |
| `query_pool` | 19,276 | 19,276 | 16,254 | 333 | 2,689 | 0 | 0 | 0 | 0 | 0 |
| `rasterization` | 15,019 | 15,019 | 373 | 111 | 14,535 | 0 | 0 | 0 | 0 | 0 |
| `ray_query` | 49,311 | 49,311 | 0 | 0 | 49,311 | 0 | 0 | 0 | 0 | 0 |
| `ray_tracing_pipeline` | 22,659 | 22,659 | 0 | 0 | 22,659 | 0 | 0 | 0 | 0 | 0 |
| `reconvergence` | 6,253 | 6,253 | 0 | 0 | 6,253 | 0 | 0 | 0 | 0 | 0 |
| `renderpasses` | 81,188 | 81,188 | 18,989 | 15,830 | 46,369 | 0 | 0 | 0 | 0 | 0 |
| `robustness` | 98,776 | 98,776 | 661 | 32 | 98,083 | 0 | 0 | 0 | 0 | 0 |
| `shader_object` | 243,853 | 243,853 | 0 | 6 | 243,847 | 0 | 0 | 0 | 0 | 0 |
| `sparse_resources` | 19,402 | 19,402 | 0 | 0 | 19,402 | 0 | 0 | 0 | 0 | 0 |
| `spirv_assembly` | 68,734 | 68,734 | 5,849 | 1,344 | 61,540 | 0 | 0 | 1 | 0 | 0 |
| `ssbo` | 12,225 | 12,225 | 2,236 | 1,006 | 8,983 | 0 | 0 | 0 | 0 | 0 |
| `subgroups` | 48,705 | 48,705 | 546 | 96 | 47,490 | 0 | 0 | 6 | 567 | 0 |
| `synchronization` | 64,872 | 64,872 | 15,089 | 3,330 | 46,452 | 1 | 0 | 0 | 0 | 0 |
| `synchronization2` | 81,617 | 81,617 | 20,940 | 3,691 | 56,986 | 0 | 0 | 0 | 0 | 0 |
| `tensor` | 2,811 | 2,811 | 0 | 0 | 2,811 | 0 | 0 | 0 | 0 | 0 |
| `tessellation` | 1,114 | 1,114 | 241 | 355 | 518 | 0 | 0 | 0 | 0 | 0 |
| `texture` | 25,669 | 25,669 | 6,631 | 2,593 | 16,435 | 0 | 0 | 10 | 0 | 0 |
| `transform_feedback` | 133,719 | 133,719 | 4,376 | 2,336 | 126,983 | 0 | 0 | 24 | 0 | 0 |
| `ubo` | 13,240 | 13,240 | 5,686 | 1 | 7,553 | 0 | 0 | 0 | 0 | 0 |
| `video` | 9,471 | 9,471 | 0 | 0 | 9,471 | 0 | 0 | 0 | 0 | 0 |
| `wsi` | 36,880 | 36,880 | 4 | 0 | 36,876 | 0 | 0 | 0 | 0 | 0 |
| `ycbcr` | 68,159 | 68,159 | 16 | 0 | 68,143 | 0 | 0 | 0 | 0 | 0 |

## Remaining abnormal outcomes

Seven groups contain process terminations: `api` (3), `graphicsfuzz` (20),
`pipeline` (179), `spirv_assembly` (1), `subgroups` (6), `texture` (10), and
`transform_feedback` (24). Of the 243 crashes, 233 ended without a diagnostic;
the other 10 reported `Dim must not be SubpassData or Buffer`.

The seven non-subgroup timeouts are:

- `dEQP-VK.graphicsfuzz.arr-value-set-to-arr-value-squared`
- `dEQP-VK.graphicsfuzz.spv-stable-quicksort-dontinline`
- `dEQP-VK.graphicsfuzz.spv-stable-quicksort-mat-func-param`
- `dEQP-VK.graphicsfuzz.stable-quicksort-conditional-bitwise-or-clamp`
- `dEQP-VK.pipeline.fast_linked_library.blend.dual_source.multi_attachments.r16g16b16a16_unorm`
- `dEQP-VK.pipeline.monolithic.blend.dual_source.multi_attachments.r8g8b8a8_srgb`
- `dEQP-VK.pipeline.pipeline_library.blend.dual_source.multi_attachments.r16g16b16a16_unorm`

The remaining 5,653 unrun pipeline cases are concentrated in
graphics-pipeline-library and extended-dynamic-state families. The largest
prefixes are `pipeline_library.extended_dynamic_state.mesh_shader` (1,013),
`fast_linked_library.extended_dynamic_state.mesh_shader` (918), and
`pipeline_library.graphics_library.independent_sets_random` (720).

Roadmap L93 now owns the seven measured compile-time long poles. Roadmap L94
owns the seven groups with process terminations and the pipeline recovery
tail. Its older nine-group inventory was re-triaged: `geometry`, `glsl`,
`image`, `rasterization`, `synchronization`, `synchronization2`, and
`tessellation` now finish without process termination. The 567 wide-subgroup
timeouts remain under L92.

The first deterministic
`pipeline_library.extended_dynamic_state.mesh_shader` recovery case,
`dEQP-VK.pipeline.pipeline_library.extended_dynamic_state.mesh_shader.after_pipelines.depth_bias_disable`,
formerly reproduced an immediate `deqp-vk` segmentation fault after starting.
L94(a) added the missing Vulkan 1.3 promoted `vkCmdSetDepthBiasEnable` device
dispatch, recording, and dynamic-raster-state resolution. The same case now
passes. With `--deqp-log-images=enable`, the intervening result showed a
passing color attachment but all 4,096 depth pixels at the statically biased
0.75 value instead of the dynamically disabled-bias 0.5 value. The monolithic
construction variant passed, while both graphics-pipeline-library variants
failed identically. L94(b) fixed this by deep-copying
`VkGraphicsPipelineCreateInfo::pDynamicState` into each library and
reconstructing its de-duplicated union when linking, so the linked pipeline
preserves `VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE`. This implementation changes no
advertised feature or extension, so both Vulkan inventories remain current.

The next executable case in the same deterministic recovery order,
`dEQP-VK.pipeline.pipeline_library.extended_dynamic_state.mesh_shader.two_draws_dynamic.disable_raster`,
initially failed pipeline creation with `VK_ERROR_INITIALIZATION_FAILED`.
L94(c) added the Vulkan 1.3 promoted
`VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE` and
`vkCmdSetRasterizerDiscardEnable` path. The existing static rasterizer-discard
implementation now defers its pipeline value when dynamic, and applies the
recorded per-draw value instead. Both the `disable_raster` reproduction and
its `enable_raster` counterpart pass with the explicit build-tree ICD and
`--deqp-shadercache=disable`. This implementation changes no advertised
feature or extension, so both Vulkan inventories remain current.

L94(d) added the remaining promoted Vulkan 1.3
`VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE` and
`vkCmdSetPrimitiveRestartEnable` path. The deterministic
`dEQP-VK.pipeline.pipeline_library.extended_dynamic_state.two_draws_static.prim_restart_enable`
case previously failed pipeline creation with `VK_ERROR_INITIALIZATION_FAILED`;
it now passes with the explicit build-tree ICD and `--deqp-shadercache=disable`.
The static input-assembly value remains used only when the state is not
dynamic, while the recorded per-draw value is resolved otherwise. This changes
no advertised feature or extension, so both Vulkan inventories remain current.

After the capability-gated `two_draws_static.vertex_input_*` and viewport
cases, the first abnormal result in deterministic order was
`dEQP-VK.pipeline.pipeline_library.framebuffer_attachment.no_attachments`.
The four point primitives were each expanded into two synthetic screen
triangles, and the fallback fragment `PrimitiveID` counter advanced for each
triangle instead of each logical point. The shader consequently observed IDs
`0,2,4,6`; its `imageStore` index modulo four wrote only texels 0 and 2. L94(e)
now resolves the ID once before clipping or point/line expansion, resets the
fallback sequence for every direct draw instance, and carries the result
through every generated triangle. The pipeline-library, fast-linked-library,
and monolithic single-sample cases all pass with the rebuilt explicit ICD and
`--deqp-shadercache=disable`. This implementation changes no advertised feature
or extension, so both Vulkan inventories remain current.

# L94(f): attachment-free multisample pipeline and sample-ID invocation

## Outcome

**`dEQP-VK.pipeline.pipeline_library.framebuffer_attachment.no_attachments_ms`
now passes.**

The four-sample pipeline initially failed creation with
`VK_ERROR_INITIALIZATION_FAILED`. After fixing creation, it rendered only the
`gl_SampleID == 0` storage-image row. Both failures are fixed by
`501d6b1e936c`.

## Investigation and change

1. A render-pass subpass with no color or depth/stencil attachments has no
   attachment sample count. `getRenderTargets()` left its normalized count at
   one, which rejected the valid four-sample pipeline. It now derives that
   count from `VkPipelineMultisampleStateCreateInfo::rasterizationSamples`,
   matching the existing dynamic-rendering path.
2. The CTS fragment shader writes a storage image indexed by
   `gl_PrimitiveID` and `gl_SampleID`. Its multisample state leaves
   `sampleShadingEnable` false. The executor had therefore run one invocation
   with sample ID zero, while a shader reading `gl_SampleID` requires
   sample-frequency invocations. Reflected `SampleIndex` now shares the
   existing per-sample path with explicit sample shading.
3. `GraphicsPipelineTest.AcceptsMultisampledNoAttachmentRenderPass` and
   `ExecutorTest.SampleIndexForcesFragmentInvocationPerSample` cover the two
   reduced causes.

## Validation

- Focused graphics-pipeline and executor tests pass.
- `ninja -C build2 check-feme`: 3,159 passed; 3 unsupported.
- The rebuilt assertions-enabled, ccache-backed explicit FeMe ICD passes the
  exact CTS case with `--deqp-shadercache=disable`.

No advertised Vulkan feature or extension changed, so
`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` remain
current.

# L94(g): null descriptor set layout crash in the pipeline cache key

## Outcome

**All 44 `dEQP-VK.pipeline.pipeline_library.graphics_library.misc.
always_null_set_layout.*` cases now pass.**

Continuing the deterministic pipeline-case order after L94(f), a fresh
1,800-case batch starting at
`pipeline_library.framebuffer_attachment.resolve_input_same_attachment`
(itself already passing) segfaulted partway through on the first
`always_null_set_layout` case.

## Investigation and change

1. `VK_EXT_graphics_pipeline_library`'s independent-sets feature lets an
   unused descriptor set's layout be `VK_NULL_HANDLE` in
   `VkPipelineLayoutCreateInfo::pSetLayouts`. `fromHandle` maps that to a
   null `DescriptorSetLayout *`, which `vkCreatePipelineLayout` stores
   as-is in `PipelineLayout`'s set-layout list.
2. `hashSetLayoutsAndPushConstants`, used by
   `computeGraphicsPipelineCacheKey` for every graphics-pipeline creation,
   dereferenced each set layout unconditionally to walk its bindings,
   crashing on the null entry. It now skips a null layout: an unused set
   contributes no bindings to the pipeline's identity.
3. `patchUnboundedResourceRanges` and `validateBoundRanges` index a
   `PipelineLayout`'s set layouts the same way and had the same latent
   null-dereference for a shader that (invalidly) binds through an unused
   set. Both are now guarded, the latter by reporting the same descriptor-
   mismatch error it already reports for an out-of-range set index.
4. Added `PipelineCacheTest.ComputePipelineCacheKeyToleratesNullSetLayout`.

## Validation

- The rebuilt assertions-enabled, ccache-backed explicit FeMe ICD (confirmed
  via `vulkaninfo --summary | grep deviceName` reporting `FeMe CPU Vulkan
  Device`) passes all 44 exact CTS cases with `--deqp-shadercache=disable`.
- Focused `PipelineCacheTest` unit test passes; the full `FeMeVulkanTests`
  suite (707 tests) passes.
- `ninja -C build2 check-feme`: 3,160 passed; 3 unsupported.
- Re-running the same 1,800-case batch that previously segfaulted now
  completes cleanly (668 passed, 181 failed, 950 not supported, 1 warning);
  the next reproducible signature, `error: unhandled Decoration : 'Component`
  on 181 `interface_matching.shader_layout_component_matching.*` cases, is
  recorded as roadmap L94(h) rather than fixed here, since it is a missing
  SPIR-V decoration, not a single reproducible pipeline bug.

No advertised Vulkan feature or extension changed, so
`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` remain
current.

# L94(h): SPIR-V `Component` decoration for interface matching

## Outcome

**All 112 supported cases of `dEQP-VK.pipeline.pipeline_library.
interface_matching.shader_layout_component_matching.*` now pass** (144 of
the 256 total cases in this family are `NotSupported`: double-precision
floats are not implemented, an unrelated, pre-existing gap). Starting from
the reduction target
`vert_tesc_tese_frag.loose_var.float32.multiple_locations.
scalar_scalar_scalar_scalar`, this previously failed pipeline creation
outright with `error: unhandled Decoration : 'Component`.

## Investigation and change

SPIR-V's `Component` decoration lets several otherwise-unrelated interface
variables share one `Location`, each occupying its own disjoint sub-range
of that location's four components. Three independent gaps stacked to
produce the original failure and its two successive downstream symptoms:

1. `mlir/lib/Target/SPIRV/{De,}serialization` never recognized the
   `Component` decoration as one of its plain-integer-literal decoration
   groups at all, so any module using it failed import outright (fixed
   separately, `dc03c83e08a3`, with an MLIR round-trip test).
2. feme's cross-stage interface matching (`StageLink.cpp`'s
   `findProducer`, `GraphicsPipeline.cpp`'s pipeline-creation-time
   vertex-output/fragment-input check, and `Executor.cpp`'s runtime
   varying linking) matched only by `Location`/`Index`, ignoring
   `Component` entirely -- it could find/link the wrong element whenever
   more than one shared a `Location`, and `Executor.cpp`'s own
   `StageStorage::readRaw`/`writeRaw` calls always addressed a varying's
   components starting at 0 rather than at its own `FirstComponent`.
3. `CanonicalizeStage.cpp` already parsed the `Component` decoration into
   `SignatureElement::FirstComponent` correctly, but a whole-variable
   stage-IO access (one that never peels into a sub-range of its own)
   still seeded the emitted `feme.stage.input.load`/`.output.store`'s
   `Component` operand from a bare constant 0, silently addressing
   component 0 of storage regardless of the element's own declared
   `Component` -- exposed only once (2) above let pipeline creation
   succeed far enough to reach `vkQueueSubmit`
   (`feme-graphics-validate-stage: component N is out of range`).
4. Fixing (3) exposed a fourth, pre-existing bug specific to this test
   family's own "multiple_locations" shape (a genuine 3-consecutive-
   `Location` array, `float loose[3];`, not a per-vertex duplication): a
   Hull/Domain/Geometry stage's own per-vertex-arrayed `Input` global had
   its outer per-vertex/control-point array dimension folded directly
   into `SignatureElement::RowCount` (with `RowCountIsVertexArray` the
   only marker distinguishing it from a real matrix's row count), and
   `StageLink.cpp`'s `effectiveRowCount` unconditionally folded that flag
   back down to `1` before comparing/copying -- correct only by
   coincidence for an ordinary (unarrayed) per-vertex varying. A
   genuinely arrayed one got its real 3-row extent folded together with
   the per-vertex dimension into one bogus combined `RowCount` (96 for
   this Hull-stage case: 32 control points times the real 3 rows),
   disagreeing with its producer's own genuine, unfolded `RowCount` (3)
   at `vkQueueSubmit` time ("disagree on component/row count or type").
   `addElements` now peels the outer per-vertex array dimension off
   before computing `RowCount` at all, mirroring the existing
   `PerInvocationOutputArray` (Hull/Mesh `Output`) peeling and consistent
   with how the per-vertex dimension is already addressed separately
   everywhere else (the `Vertex` operand, `StageStorage`'s own
   `Invocation` parameter).

Each of (2)-(4) landed as its own commit with focused unit tests, verified
to fail without the corresponding fix.

## Validation

- The rebuilt assertions-enabled, ccache-backed explicit FeMe ICD (confirmed
  via `vulkaninfo --summary | grep deviceName` reporting `FeMe CPU Vulkan
  Device`) passes the exact reduction target case, then the full
  `shader_layout_component_matching.*` family (256 cases: 112 passed, 0
  failed, 144 not supported) with `--deqp-shadercache=disable`.
- `ninja -C build2 check-feme`: 3,163 passed; 3 unsupported; 0 failed.
- A broader sweep of the full `pipeline_library.interface_matching.*` group
  (688 of an unknown larger total, before an unrelated heap-corruption
  crash in a `vector_length.*member_of_array_of_structures_in_block` case)
  showed no new failures attributable to this change; its remaining
  `vector_length.*` failures (`VK_ERROR_INITIALIZATION_FAILED` at
  `vkPipelineConstructionUtil.cpp:176`/`vkCmdUtil.cpp:338`) are a
  pre-existing, separate gap (vector-length truncation matching, not
  `Component`), out of this milestone's scope.

No advertised Vulkan feature or extension changed, so
`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` remain
current.

## Dominant ordinary failures

The largest result signatures are:

| Count | Signature |
|---:|---|
| 38,552 | Image mismatch |
| 22,313 | Queue submission returned `VK_ERROR_INITIALIZATION_FAILED` |
| 15,372 | Image verification failed |
| 14,178 | Pipeline construction returned `VK_ERROR_INITIALIZATION_FAILED` |
| 12,969 | Compute-pipeline creation returned `VK_ERROR_INITIALIZATION_FAILED` |
| 7,751 | `llvm.insertelement` operand/element type verification failure |
| 3,463 | Image creation returned `VK_ERROR_INITIALIZATION_FAILED` |
| 2,898 | Empty sample mask produced incorrect pixels |
| 2,622 | Empty sample mask produced incorrect pixel values |
| 2,141 | Graphics-pipeline creation returned `VK_ERROR_INITIALIZATION_FAILED` |

These are aggregate signatures, not assumed root causes. They should be split
by reduced case before assigning implementation work, following roadmap G3.

## Reproduction

Build and unit/lit validation:

```console
ninja -C build2 check-feme
```

Generate the complete case list from the CTS Vulkan module directory:

```console
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_DRIVER_FILES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-runmode=txt-caselist --deqp-shadercache=disable
```

For reliable full runs, split each top-level group into at most 1,800 cases,
run with `--deqp-shadercache=disable`, preserve each process log/QPA file, and
resume from the first case after the last started case. Treat a nonzero exit as
a test result until the log proves that the process terminated before writing
that result.

# L94(i): vector-length narrowing rejection and array-of-struct block crash

## Outcome

**Two independent gaps found while sweeping the broader
`pipeline_library.interface_matching.*` group after L94(h), both fixed.**
Reduction targets: `vector_length.
out_ivec3_in_ivec2_loose_variable_vert_out_frag_in` (part 1) and
`vector_length.
out_ivec3_in_ivec2_member_of_array_of_structures_in_block_vert_out_frag_in`
(part 2).

## Investigation and change

### Part 1: consumer-narrower-than-producer vector-length rejection

The `vector_length.*` family intentionally mismatches a producer's and a
consumer's vector length (e.g. a vertex-stage `ivec3` output feeding a
fragment-stage `ivec2` input). `VK_KHR_maintenance4` (already implemented
in feme as core, roadmap E4) explicitly permits this: a consumer may
declare *fewer* vector components than its producer and read only the
leading ones; only a consumer requesting *more* components than its
producer supplies is an error. `FEME_VULKAN_LOG_CREATION_ERRORS=1` turned
the bare `VK_ERROR_INITIALIZATION_FAILED` into the real diagnostic,
`"vertex output and fragment input at location 0 disagree on component
count/type"`.

Three independent call sites each did an exact `ComponentCount` equality
check rather than this asymmetric one: `StageLink.cpp`'s
`linkStageElements` (general cross-stage linker), `GraphicsPipeline.cpp`'s
`validateStageInterfaces` (pipeline-creation-time vertex/fragment check),
and `Executor.cpp`'s own runtime fragment-varying-linking loop. All three
were relaxed from `!=` to `>` (consumer wider than producer remains an
error; consumer narrower is now accepted). The actual component copy/read
loops already used the *consumer's* (smaller) `ComponentCount` as their
loop bound, so no other change was needed to read/write only the leading
components correctly.

### Part 2: `member_of_array_of_structures_in_block` heap corruption

A single-member `Block`-decorated interface variable (e.g. `struct
TestStruct { vec4 a; ivecN b; }; layout(location=0) out block { TestStruct
s[3]; } blk;`) whose one real member is itself an array of a genuine
multi-member nested struct crashed `deqp-vk` with a glibc `corrupted size
vs. prev_size while consolidating` heap-corruption abort.
`TakeBlockPath` (the existing multi-member-block decomposition) only
triggers when the *top-level* block declares more than one member, so
this single-member case fell to the plain `addElement` path instead,
where `getStageIORowShape` ran out of single-member wrappers and array
levels to peel once it reached `TestStruct` itself (2 real members) and
silently treated the whole member as one opaque scalar `ComponentCount=1`
leaf, undersizing the `SignatureElement` relative to what the compiled
stores actually wrote.

Fixed on both sides in `CanonicalizeStage.cpp`:

- Construction (`addElements`' plain path): after peeling the outer
  single-member-block wrapper via `peelSingleMemberStruct`, recognize
  when the recovered content is a genuine multi-member nested struct
  (array-wrapped or not) via `isGenuineMultiMemberNestedStruct`, and
  route it through the already-existing `addStageIOStructMembers`
  instead of a single `addElement` call.
- Access resolution (`resolveOffsetWithinElement`): added a dispatch
  branch recognizing this same peeled shape and resolving through the
  existing `resolveNestedStageIOField` helper directly (no block-instance
  folding to undo first, unlike `AllowBlockArrayInstanceFold`'s
  `Patch`-array-of-block-instances case).

The construction-side fix deliberately excludes `RowCountIsVertexArray`/
`XfbBufferArrayStride` cases -- combining this array-of-struct shape with
a per-vertex/per-control-point-arrayed stage-IO dimension is a narrower,
separate gap, split out as roadmap L94(j).

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` against the rebuilt, assertions-enabled, ccache-backed ICD.
- `StageLinkTest.AcceptsAConsumerNarrowerThanItsProducer`,
  `GraphicsPipelineTest.AcceptsFragmentInputNarrowerThanVertexOutput`, and
  `CanonicalizeStageTest.
  RewritesSingleMemberBlockArrayOfGenuineMultiMemberNestedStruct` each
  confirmed to fail without their corresponding fix (stash/rebuild/test/
  unstash cycle).
- `ninja -C build2 check-feme`: 3,166 passed; 3 unsupported; 0 failed.
- `vector_length.*loose_variable*` (162 cases): 162 pass, 0 fail (part 1
  fully fixed for this shape).
- `vector_length.*member_of_array_of_structures_in_block*` (162 cases):
  72 pass, 90 fail, **0 crash** (part 2 fixed the crash; a rendering-
  correctness gap remains for tesc/geom-per-vertex-array combinations,
  tracked as L94(j)).
- Full `vector_length.*` (972 cases): 720 pass, 252 fail, 0 not-supported,
  **0 crash** (was crashing part-way through a larger sweep before this
  session; the prior session's sweep of the broader
  `interface_matching.*` group stopped after 688 cases on this exact
  crash).
- The 252 `vector_length.*` failures break down as 90
  `member_of_array_of_structures_in_block` cases (tesc/geom per-vertex-
  arrayed-input combinations) and 18 sibling non-`Block`
  `member_of_array_of_structures` cases (`vert_tesc_out_tese_in_frag`),
  confirmed via an `out_vec4_in_vec4` case (a same-length pairing, no
  vector-length mismatch at all) to be unrelated to part 1's scope -- a
  pre-existing struct/array-modeling gap this family happens to expose,
  filed as L94(j).

No advertised Vulkan feature or extension changed, so
`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` remain
current.

## Reproduction

```console
ninja -C build2 check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-case="dEQP-VK.pipeline.pipeline_library.interface_matching.vector_length.*" \
  --deqp-log-images=disable --deqp-shadercache=disable
```

# L94(j): array-of-struct combined with a per-vertex/per-invocation stage-IO dimension

## Outcome

**Two independent gaps, split out of L94(i)'s own closing session, both
fixed.** Reduction targets: `vector_length.
out_vec4_in_vec4_member_of_array_of_structures_vert_tesc_out_tese_in_frag`
(gap 1) and `vector_length.
out_vec4_in_vec4_member_of_array_of_structures_vert_tesc_tese_out_frag_in`
(gap 2, discovered while sweeping the broader family after fixing gap 1).

## Investigation and change

`StageIOGlobalVariablePattern` (the MLIR importer) only peels *one* outer
`ArrayType` level before checking whether the remaining type is a
`StructType`, to decide whether to attach `feme.spirv.MemberDecorations`
metadata to a stage-IO global. This means two structurally similar CTS
shapes -- differing only in whether there's an extra per-vertex array
level wrapping the genuine array-of-struct -- take entirely different
code paths through `CanonicalizeStage.cpp`'s `addElements`, each with its
own independent correctness gap.

### Gap 1: doubly-arrayed shape on the plain (non-block) path

A Hull-stage `Output`/Domain-stage `Input` per-control-point array
wrapping an inner genuine array-of-struct (e.g. tesc's `layout(location=0)
out struct {float dummy; vec4 v;} testStructArray[][3];`, LLVM type `[1 x
[3 x {float, vec4}]]`) has *two* nested array levels, so the importer's
one-level peel never exposes a bare `StructType`, and `MemberMD` is never
attached. `addElements`' plain path already decomposed a single-member
wrapper's genuine multi-member nested struct content via
`addStageIOStructMembers` (L94(i)'s own fix), but deliberately excluded
the `RowCountIsVertexArray` case (the tesc/tese per-control-point-arrayed
consuming side) rather than risk an under-tested interaction --
`isPerVertexArrayInputGlobal` applies to this Domain-stage-input global,
so it fell to a single opaque `addElement` call, undersizing/mistyping
the element (a rendering mismatch, not a crash or pipeline-creation
error).

Fixed by removing the `!RowCountIsVertexArray` exclusion and adding a new
`AddDecomposedElement` wrapper lambda that forwards this element's own
`RowCountIsVertexArray` (and always-0 `XfbBufferArrayStride`, mutually
exclusive by construction) through to every leaf `addElement` call
`addStageIOStructMembers` makes -- its generic 4-argument `AddElement`
callback signature has no room for either flag, previously silently
defaulting both to `false`/`0`.

### Gap 2: singly-arrayed shape on `TakeBlockPath`

An ordinary, non-`Block`, non-`Patch`, non-`XfbBuffer` array-of-struct
declared directly as a whole stage-IO variable's own type (e.g. a
fragment shader's plain `layout(location=0) in flat struct {float dummy;
vec4 v;} testStructArray[3];`, a *single* array level) *does* get
`MemberMD` attached (the importer's one-level peel exposes the
`StructType` directly), routing it through `TakeBlockPath` instead of the
plain path. `TakeBlockPath`'s `BlockArrayCount` mechanism (added for
roadmap H117/H118) only folded the outer array dimension into each leaf
member's own widened `RowCount` when the array was `patch`-qualified;
every other shape (including this one) left `BlockArrayCount` at 0,
undersizing every leaf's own `RowCount` to 1 regardless of the real array
extent -- surfaced as `feme-graphics-validate-stage`'s own "row N is out
of range for element M" pipeline-creation-time validation failure once a
store/load indexed a non-zero array element.

Distinguishing this ordinary shape from a genuine per-vertex/per-
invocation dynamically-indexed array can't rely on `Stage`/`AddrSpace`
alone: every stage-IO global lives in address space 7 (`Input`) or 8
(`Output`) regardless of stage (`isSPIRVStageIOGlobal`'s own contract),
and several existing unit tests deliberately tag a synthetic per-vertex-
shaped global with an unrelated `Stage` purely to exercise the resolution
mechanism in isolation. The fix combines the existing `Stage`-based
per-vertex/per-invocation recognition
(`isPerVertexArrayInputGlobal`/`isPerVertexArrayMeshOutputGlobal`, plus
the Hull-per-invocation-`Output` condition `PerInvocationOutputArray`
already uses) with a scan of the global's own actual accesses for a
non-constant outer array index (catching the synthetic-stage unit tests
that the `Stage`-based checks alone don't cover), folding `BlockArrayCount`
whenever neither signals a genuine per-vertex/per-invocation shape and the
array isn't XFB-captured (a genuine, non-`Patch` XFB "array of block
instances" has its own separate, pre-existing, imperfect handling that a
first attempt at this fix wrongly conflated with `BlockArrayCount`,
regressing a `transform_feedback` CTS case discovered via the same
stash/rebuild/retest cycle used throughout this investigation -- reverted
before landing).

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` against the rebuilt, assertions-enabled, ccache-backed ICD, both
  before starting and again after the final rebuild.
- `ninja -C build2 check-feme`: 3,166 passed; 3 unsupported; 0 failed (all
  97 `FeMeTransformsGraphicsTests` cases pass, including the two existing
  tests -- `ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberLoad`,
  `ThreadsInvocationIndexIntoMultiMemberHullPerInvocationOutputBlock` --
  and the two more -- `FoldsConstantVertexIndexIntoInterfaceBlockArray
  MemberVertexOperand`,
  `FoldsConstantVertexIndexIntoSingleMemberInterfaceBlockOutputStore` --
  that an initial, less-precise discriminator for gap 2 regressed before
  landing the combined `Stage`-plus-usage-scan check above).
- Both reduction targets pass individually after their respective fix.
- `vector_length.*member_of_array_of_structures*` (324 cases): 324 pass,
  0 fail (up from 180/324 at this milestone's start, itself up from the
  L94(i)-session baseline).
- Full `vector_length.*` (972 cases): **972 pass, 0 fail** (up from
  720/972 at the L94(i) session's close).
- Full `pipeline_library.interface_matching.*` group (1589 cases): 1121
  pass, 468 not-supported, **0 fail, 0 crash**.

No advertised Vulkan feature or extension changed, so
`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` remain
current.

## Reproduction

```console
ninja -C build2 check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-case="dEQP-VK.pipeline.pipeline_library.interface_matching.*" \
  --deqp-log-images=disable --deqp-shadercache=disable
```

# L97: `graphicsPipelineLibraryIndependentInterpolationDecoration` triage and fix

## Outcome

**Fixed.** Split out of L94(j)'s own closing session's next-step ask to
triage the `pipeline_library.interface_matching.*` sweep's 468
`NotSupported` cases for cheap wins versus genuine feature gaps.

## Investigation and change

Categorized the 468 `NotSupported` cases (via the saved sweep log from
the prior session): 324 blocked on
`graphicsPipelineLibraryIndependentInterpolationDecoration` (the
`decoration_mismatch.*` family, testing `Flat`/`NoPerspective`
interpolation-decoration mismatches between producer/consumer stages),
112 on 16-bit floats, 32 on double-precision floats.

The CTS `DECORATION_MISMATCH` test type
(`vktPipelineInterfaceMatchingTests.cpp`) is a rendering-correctness
test, not a validation-error test: it renders values chosen to produce
the same result regardless of interpolation mode, so the only thing
being checked is whether the implementation still computes the right
answer despite the mismatch. The CTS gates this family behind
`graphicsPipelineLibraryIndependentInterpolationDecoration` only for
non-monolithic pipeline-construction types, because an implementation
*could* legitimately fail to propagate per-stage interpolation
decorations correctly when pipeline-library stages are compiled
independently.

`EntryPoints.cpp` reported this property `VK_FALSE` with no
implementation work behind that value. But this ICD's own graphics-
pipeline-library "link" is always a full `compileGraphicsPipeline`
recompile of the merged pipeline state
(`synthesizeLinkedGraphicsPipelineCreateInfo`, roadmap H29c), never a
genuinely independent per-stage compilation -- every linked stage's own
SPIR-V interpolation decorations are always visible together at the
point `CanonicalizeStage.cpp` computes each `SignatureElement`'s
`Interpolation` mode. There is no code path in this ICD that could
behave differently for a pipeline-library-constructed pipeline than for
the equivalent monolithic one w.r.t. interpolation decoration --
confirmed directly by running the same
`decoration_mismatch.out_flat_in_none_loose_variable_vert_out_frag_in`
case under both `pipeline_library` and `monolithic` construction types
and observing identical passing behavior even before this change (the
monolithic variant is never gated on this property at all).

Fixed by flipping `graphicsPipelineLibraryIndependentInterpolationDecoration`
to `VK_TRUE` in `EntryPoints.cpp`. The sibling
`graphicsPipelineLibraryFastLinking` property remains `VK_FALSE`
(unchanged): that property promises the link step is cheaper than a full
recompile, which is false for this ICD, so it correctly stays `VK_FALSE`.

The remaining 144 `NotSupported` cases (112 16-bit float, 32
double-precision float) were confirmed to be genuine, unimplemented
feature gaps: `EntryPoints.cpp` reports `shaderFloat16` and
`storageInputOutput16` both `VK_FALSE`, and doesn't mention
`shaderFloat64` at all (defaults `VK_FALSE`). Filed as roadmap L98
rather than attempted this session -- a materially larger scope than a
property-flip fix.

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` against the rebuilt, assertions-enabled, ccache-backed ICD.
- `FeMeVulkanTests --gtest_filter="*GraphicsPipelineLibrary*"`: 3/3 pass
  (including the updated
  `PhysicalDeviceProperties2Test.GraphicsPipelineLibraryIsImplementedAndAdvertised`).
- `ninja -C build2 check-feme`: 3,168 passed; 3 unsupported; 0 failed
  (same totals as the L94(j) session's close -- no regressions).
- `decoration_mismatch.*` pipeline-library family (360 cases): **360
  pass, 0 not-supported, 0 fail** (was 0 pass / 360 not-supported before
  this fix).
- Full `pipeline_library.interface_matching.*` group (1589 cases):
  **1445 pass** (up from 1121), **144 not-supported** (the remaining
  16-bit/64-bit float gaps, tracked as L98), **0 fail, 0 crash**.

No advertised extension changed (`VK_EXT_graphics_pipeline_library` was
already listed as advertised); this is a within-extension property
correction, so `Vulkan14FeatureInventory.md` and
`VulkanExtensionInventory.md` remain current.

## Reproduction

```console
ninja -C build2 check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-case="dEQP-VK.pipeline.pipeline_library.interface_matching.decoration_mismatch.*" \
  --deqp-log-images=disable --deqp-shadercache=disable
```

# L99: `matNx3` composite-construct/indexing rendering bug (spec_constant sweep)

## Outcome

**Reduced and scoped, not fixed.** Broadened the CTS sweep beyond
`pipeline_library.interface_matching.*` to a fresh top-level group,
`pipeline_library.spec_constant.*` (per this milestone series' own
"broaden the sweep" next-step). Reduction target:
`dEQP-VK.pipeline.pipeline_library.spec_constant.graphics.fragment.
composite.matrix.mat2x3`.

## Investigation

Full `pipeline_library.spec_constant.*` sweep (1170 cases): 455 pass,
200 fail, 515 not-supported. Categorized the 200 failures:

- 115 "Values did not match" -- `composite.matrix.*`/`composite.struct.*`
  rendering-correctness failures.
- 45 `spirv.VectorExtractDynamic` "failed to legalize" pipeline-creation
  failures (a dynamically-indexed-vector-with-spec-constant-index gap,
  not yet reduced).
- 10 `OpTypeArray count ... must come from a constant` pipeline-creation
  failures (an array-size-from-spec-constant-expression gap, not yet
  reduced).
- ~30 "GEP into vector with non-byte-addressable element type"
  pipeline-creation failures (not yet reduced).

Reduced the largest bucket to `composite.matrix.mat2x3`, confirmed
reproducible standalone (`Fail (Values did not match)`, 1/1 in
isolation). Swept every `composite.matrix.*` case across all 5 graphics
stages (vertex, fragment, geometry, tess_control, tess_eval) and found
an exact, consistent pattern:

| Matrix | Column shape | Result |
|---|---|---|
| `mat2`, `mat2x4`, `mat3x2`, `mat3x4`, `mat4`, `mat4x2` | 2 or 4 rows | Pass |
| `mat2x3`, `mat3`, `mat4x3` | **3 rows (`vec3` column)** | **Fail** |

This holds identically in every one of the 5 stages checked (15/18
matching cases seen so far all follow this rule). The failure is
independent of column count and shader stage -- the only common factor
is a 3-component (`vec3`) column vector.

`SPIRVToLLVMPatterns.cpp`'s `convertMatrix`/`CompositeConstructOp`
handling, and the file's existing "tight vector array" substitution
machinery (`getTightVectorArrayType`, roadmap H101j -- built specifically
to reconcile a `vec3`'s native LLVM vector representation with its
tightly-packed in-memory layout elsewhere in this file), are the most
likely defect location. Confirming this needs comparing the actual
SPIR-V/LLVM IR for a passing `mat2` case against the failing `mat2x3`
case's own construction and `m[i][j]` indexing sequence side by side --
not attempted this session, given the size of that subsystem and the
time already spent on this milestone series' other work.

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` before and during this investigation.
- No code changed this session for L99 -- reduction/scoping only, so no
  `check-feme`/CTS re-run was needed.

No advertised Vulkan feature or extension changed.

## Reproduction

```console
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-case="dEQP-VK.pipeline.pipeline_library.spec_constant.graphics.fragment.composite.matrix.mat2x3" \
  --deqp-log-images=disable --deqp-shadercache=disable
```

# L99: measured impact (fix landed)

## Outcome

**Fixed.** Root-caused and fixed this session's continuation: the bug was
not in `SPIRVToLLVMPatterns.cpp`'s matrix conversion at all (the
suspected location from the prior session's own reduction), but in
`feme::cpu::LocalNarrowVectorArrayInitPass` (roadmap H69) applying its
tight-offset store-splitting fixup to a global it was never designed for.

## Investigation

Built a standalone repro (`mat2x3.frag`, mirroring the CTS's own
generated GLSL) and traced `feme-translate`'s SPIR-V-to-LLVM-IR
translation in isolation -- self-consistent, no bug visible there.
Captured the *real*, fully-CPU-pipeline-processed IR for the actual CTS
case via `FEME_DUMP_IR=1` and found the divergence: the matrix global's
per-column init stores land at a *tight* 12-byte offset for column 1,
while the `m[i][j]` read path's `getelementptr` computes a *natural-ABI*
16-byte stride (LLVM's `DataLayout` rounds a 3-lane vector's 12-byte
store size up to a 4-lane vector's 16-byte alloc size -- confirmed via
an isolated `opt -passes=instsimplify` GEP-folding test against the
real CPU target datalayout). Bisected the CPU `Normalize` pass pipeline
one pass at a time (`feme-opt --llvm -passes=<pass>`) and found
`LocalNarrowVectorArrayInitPass` (H69) is the pass performing this
split -- but H69's own scoping (any `Private`/`Function`-storage
address-space-0 global whose array element is a narrow vector) is too
broad: it also matches a `spirv.MatrixType`-turned array-of-columns
global, which is read back through MLIR upstream's own generic,
natural-ABI-strided `AccessChainOp` conversion, not the tight-offset
`i8`-GEP convention H69's own mesh-scratch-array scenario
(`uint3 idx[2]` feeding `gl_PrimitiveTriangleIndicesEXT`) actually uses.

Fixed by adding `hasTightGEPUser` to `LocalNarrowVectorArrayInitPass`:
it now only rewrites a global's init store if some other real user of
that same global is itself already a tight (`i8`-element) `getelementptr`
-- true for H69's own case, false for a matrix global -- so the pass no
longer "fixes" a global whose reads were never tight to begin with.

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` before and during this session (note: `VK_ICD_FILENAMES` must
  be exported explicitly each session -- it is not set in any shell
  profile).
- `ninja check-feme`: 3169 Passed, 3 pre-existing Unsupported, 0 Failed
  (no regressions; 4 new `LocalNarrowVectorArrayInitTest` cases,
  including the new `IgnoresGlobalWithOnlyNaturalGEPReaders` regression
  test for this exact bug).
- Reduced case (`composite.matrix.mat2x3`): now **1/1 Pass** (was Fail).
- Full `composite.matrix.*` re-sweep (90 cases across 5 stages): **45/45
  Pass** of the supported cases (45 not-supported, unrelated), up from
  27/45 pre-fix -- exactly the 18 `matNx3` cases across the 5 stages,
  zero collateral regressions.
- Full `pipeline_library.spec_constant.*` re-sweep (1170 cases): **470
  Pass / 185 Fail / 515 NotSupported**, up from the pre-fix 455/200/515
  -- an exact +15/-15 shift (this session's own standalone-vs-mustpass
  case counts differ slightly from the 18-case CTS-stage sweep above
  since not every stage/matrix combination is present in the default
  build's mustpass list). The remaining 185 failures are the three
  other, already-tracked, unrelated buckets (`VectorExtractDynamic`,
  `OpTypeArray` count, "GEP into vector") now tracked as roadmap L100.

No advertised Vulkan feature or extension changed -- this is a pure
rendering-correctness fix.

## Reproduction

```console
cd /home/dev/dev/llvm-project/build2 && ninja check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n "dEQP-VK.pipeline.pipeline_library.spec_constant.graphics.*.composite.matrix.*" \
  --deqp-log-images=disable --deqp-shadercache=disable
```


# L100: measured impact

## Outcome

**Fixed.** The `OpTypeArray count ... must come from a constant` bucket
(10 cases, one of the three unrelated `spec_constant.*` buckets left
over from L99) is closed.

## Investigation

Reduced to
`dEQP-VK.pipeline.pipeline_library.spec_constant.graphics.vertex.
expression.array_size_spec_const_expression`. Built a standalone repro
GLSL shader (`const int size = sc0 + 3; int a0[size];`) matching the
CTS's own generated shape, compiled it with `glslangValidator`, and
disassembled with `spirv-dis`: the array length operand is an
`OpSpecConstantOp %int IAdd %sc0 %int_3` (and, for the two-spec-constant
sibling case, `%sc0 %sc1`). Upstream MLIR's SPIR-V deserializer
(`Deserializer.cpp`'s `resolveConstantArrayLength`, a feme-added
function) only folded `OpCompositeExtract` and `OpIMul` enclosed
opcodes for a spec-constant-derived array length, declining every other
opcode including `OpIAdd`/`OpISub` -- exactly this shape.

Fixed by adding `OpIAdd`/`OpISub` cases to the switch, recursively
folding each operand exactly like the existing `OpIMul` case already
does.

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` before and during this session (remember to export
  `VK_ICD_FILENAMES` first in a fresh shell).
- New MLIR deserialization test `array-spec-constant-add-length.spvasm`
  (mirrors `array-spec-constant-composite-length.spvasm`'s convention);
  updated `array-spec-constant-length-invalid.spvasm`'s comment to
  reflect the now-3-opcode supported list. All 4
  `array-spec-constant-*.spvasm` tests pass.
- `ninja check-feme`: 3169 Passed, 3 pre-existing Unsupported, 0 Failed
  (no regressions).
- All 10 previously-failing cases (`array_size_expression`/
  `array_size_spec_const_expression` across all 5 graphics stages) now
  **Pass**.
- Full `pipeline_library.spec_constant.*` re-sweep (1170 cases): **480
  Pass / 175 Fail / 515 NotSupported**, up from the pre-fix 470/185/515
  -- an exact +10/-10 shift, matching this row's own 10-case scope
  precisely, zero collateral regressions. The remaining 175 failures are
  the two other, already-tracked buckets (`VectorExtractDynamic`,
  now roadmap L101; "GEP into vector", now roadmap L102).

No advertised Vulkan feature or extension changed -- this is a pure
SPIR-V-deserialization correctness fix.

## Reproduction

```console
cd /home/dev/dev/llvm-project/build2 && ninja check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n "dEQP-VK.pipeline.pipeline_library.spec_constant.graphics.*.expression.array_size_*expression" \
  --deqp-log-images=disable --deqp-shadercache=disable
```

# L101: measured impact

## Outcome

**Fixed.** The `spirv.VectorExtractDynamic` "failed to legalize" bucket
(45 cases, one of the three unrelated `spec_constant.*` buckets left
over from L99/L100) is closed.

## Investigation

Reduced to
`dEQP-VK.pipeline.pipeline_library.spec_constant.graphics.fragment.
composite.vector.ivec2` (and the sibling `bvec2` case): GLSL indexing a
spec-constant-mixed local vector with a runtime, non-constant index
(`v[i]` where `i` is a loop variable, as opposed to a literal index)
compiles to `spirv.VectorExtractDynamic`, which errored with `failed to
legalize operation 'spirv.VectorExtractDynamic' that was explicitly
marked illegal`. Confirmed neither upstream MLIR's own
`SPIRVToLLVM.cpp` nor feme's own `SPIRVToLLVMPatterns.cpp` had any
conversion pattern for this op at all -- a plain missing-pattern gap,
not a narrower scoping bug like L99/L100.

Fixed by adding `VectorExtractDynamicPattern`: the op's `vector`/`index`
operands and result type (always the vector's own element type) match
`llvm.extractelement`'s own shape exactly, so the conversion is a
direct 1:1 rewrite.

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` before and during this session.
- New unit test `SPIRVToLLVMTest.VectorExtractDynamicConvertsInsteadOfFailing`
  (all 25 tests in this suite pass).
- `ninja check-feme`: 3170 Passed, 3 pre-existing Unsupported, 0 Failed
  (up 1 test, no regressions).
- Full `composite.vector.*` re-sweep (75 cases across 5 stages): **60/60
  Pass** of the supported cases (15 not-supported, unrelated), up from
  45/60 pre-fix -- exactly the `bvec*`/`ivec*`/`uvec*` dynamic-index
  cases across the 5 stages, zero collateral regressions.
- Full `pipeline_library.spec_constant.*` re-sweep (1170 cases): **525
  Pass / 130 Fail / 515 NotSupported**, up from the pre-fix 480/175/515
  -- an exact +45/-45 shift, matching this row's own 45-case scope
  precisely. The remaining 130 failures split into roadmap L102 ("GEP
  into vector", ~30 cases, not yet touched) and a newly-identified,
  previously-undercounted roadmap L103 (`composite.struct.*` "Values
  did not match", 100 cases -- see L103's own roadmap entry for why
  this was missed in L99's own closing count).

No advertised Vulkan feature or extension changed -- this is a pure
missing-conversion-pattern fix.

## Reproduction

```console
cd /home/dev/dev/llvm-project/build2 && ninja check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n "dEQP-VK.pipeline.pipeline_library.spec_constant.graphics.*.composite.vector.*" \
  --deqp-log-images=disable --deqp-shadercache=disable
```

# L102: measured impact

## Outcome

**Fixed.** The "GEP into vector with non-byte-addressable element type"
pipeline-creation-time bucket (~30 cases, the last of the three
unrelated `spec_constant.*` failure buckets tracked since L99's own
sweep) is closed.

## Investigation

Reduced to
`dEQP-VK.pipeline.pipeline_library.spec_constant.graphics.fragment.
composite.array.bvec2`: the failure is an LLVM IR *verifier* rejection
(`llvm/lib/IR/Verifier.cpp:4594`), surfacing only after MLIR-to-LLVM-IR
translation, not an MLIR-level dialect-conversion failure -- the
conversion itself "succeeds" but produces invalid IR.

Root cause: `spirv.AccessChain`'s generic upstream conversion
(`AccessChainPattern` in `mlir/lib/Conversion/SPIRVToLLVM/
SPIRVToLLVM.cpp`) builds a single `llvm.getelementptr` using *all* of
the AccessChain's indices, including a final index that selects a lane
inside a vector. This is valid when the vector's element type is
byte-sized (`i32`/`f32`, etc.), since GEP computes byte offsets, but
LLVM's IR verifier rejects it for an `i1` (bool) vector element, which
has no byte size -- hence only `bvec2/3/4` (never scalar `bool` or a
non-bool vector) hit this bucket.

Per SPIR-V's own rule that a pointer to a vector component is a valid
input only to `OpLoad`/`OpStore` (never stored/passed elsewhere),
fixed by fusing the AccessChain into its consuming Load/Store, the
same shape as the existing `MatrixColumnLoadPattern`/
`MatrixColumnStorePattern` precedent:

- `BoolVectorLaneAccessChainPattern` converts the AccessChain on its
  own into a harmless/unused GEP addressing the vector itself (every
  index but the last), so it still has *some* legal conversion instead
  of falling through to the illegal generic pattern.
- `BoolVectorLaneLoadPattern`/`BoolVectorLaneStorePattern` match the
  consuming `spirv.Load`/`spirv.Store`, rebuild the same
  vector-addressing GEP directly from the AccessChain's own (remapped)
  base pointer, and perform the final lane selection with
  `llvm.extractelement`/`llvm.insertelement`.

Three bugs were found and fixed along the way while building this:
a `sed` command intended narrowly ended up globally corrupting ~9
unrelated pre-existing function signatures (reverted by hand); an
`IntegerAttr::getInt()` assertion crash inside the new struct-index
walk helper (its signless-only precondition doesn't hold for every
SPIR-V constant, fixed by using `.getValue().getSExtValue()` instead);
and a `dialect conversion attempted to replace a root operation that
has no parent block` crash from explicitly `eraseOp`-ing the
AccessChain inside the Load/Store patterns (fixed by never erasing it,
matching the `MatrixColumnLoadPattern` precedent, and adding the
companion `BoolVectorLaneAccessChainPattern` so the now-otherwise-dead
AccessChain still converts legally on its own).

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` before and during this session.
- New unit test `SPIRVToLLVMTest.BoolVectorLaneLoadStoreConvertsInsteadOfFailing`.
- `ninja check-feme`: 3171 Passed, 3 pre-existing Unsupported, 0 Failed
  (up 1 test, no regressions).
- Full `composite.array.*` re-sweep (385 cases across 5 stages):
  **255/385 Pass, 0 Fail, 130 NotSupported**, up from 225/30/130
  pre-fix -- exactly the 30 `bvec2/3/4` and `array_bvec2/3/4` cases
  across the 5 stages, zero collateral regressions.
- Full `pipeline_library.spec_constant.*` re-sweep (1170 cases): **555
  Pass / 100 Fail / 515 NotSupported**, up from the pre-fix
  525/130/515 -- an exact +30/-30 shift, matching this row's own scope
  precisely. The remaining 100 failures are entirely roadmap L103's
  own, still-open `composite.struct.*` bucket.

No advertised Vulkan feature or extension changed -- this is a pure
SPIR-V-to-LLVM conversion correctness fix for an existing (bool
vector) type shape.

## Reproduction

```console
cd /home/dev/dev/llvm-project/build2 && ninja check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n "dEQP-VK.pipeline.pipeline_library.spec_constant.graphics.*.composite.array.*" \
  --deqp-log-images=disable --deqp-shadercache=disable
```

# L103: measured impact (fix landed)

## Root cause

A struct-typed `spirv.AccessChain` into a **non-`Offset`-decorated**
struct (a plain `Private`/`Function`/`Input`-storage struct, not a
`Block`-decorated interface block) was previously converted as an
ordinary, non-packed LLVM struct type. Any padding a member's own
natural ABI alignment required (e.g. a `<2 x i32>` member immediately
after a 1-byte `bool`/`i1` member) was left for LLVM to compute
implicitly, using whatever `DataLayout` happened to be attached to the
surrounding `llvm::Module` at the moment a GEP into it was constant-
folded to a raw byte offset. For `feme::cpu`, that moment is
`feme::SPIRVToLLVMTranslator`'s own translation -- well before the
later switch to the real host `DataLayout` (`Pipeline.cpp`) -- and the
SPIR-V execution model's own triple-derived `DataLayout` disagrees with
the real host's on a `<2 x i32>`'s natural alignment (4 vs 8 bytes),
silently baking in the wrong byte offset for every member after such a
gap, permanently (switching to the real host `DataLayout` afterward
cannot retroactively re-fold an already-materialized constant GEP).

## Fix

Three parts, since there is no single choke point for "does this
struct's declared member index need remapping to a different physical
index":

1. `layOutStructIfOffsetsMatch`'s non-offset branch now always builds
   an explicitly `packed` struct, with every natural-alignment gap
   materialized as its own synthetic `[N x i8]` member (computed via
   `mlir::DataLayout`'s own default rules, which agree with the real
   host's for these shapes) -- fixing its byte layout independent of
   whichever `DataLayout` a later GEP fold happens to see.
2. `OffsetStructMemberReorderAccessChainPattern`'s gate,
   `remapNestedStructMemberIndices`'s own inner per-level gate, and
   `CompositeConstructPattern::convertStruct`'s member-index ternary
   were all broadened from `StructTy.hasOffset()` to unconditional --
   a declared member index may now need remapping to its physical
   index even for a struct with no `Offset` decorations at all.
3. `StageIOArrayAccessChainPattern` (despite its name, this handles
   any `Input`-storage composite -- struct or array -- `AccessChain`,
   not just arrays) previously forwarded `Adaptor.getIndices()`
   verbatim, with zero remapping awareness -- the largest find this
   session, since it silently selected the wrong (or a gap) field for
   any `Input`-storage struct with a natural-alignment gap. Fixed by
   routing its indices through the same `remapNestedStructMemberIndices`
   helper the other patterns already use.

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` before and during this session.
- New unit tests `SPIRVToLLVMTest.NonOffsetStructWithAlignmentGapBuilds
  ExplicitPadding` and `SPIRVToLLVMTest.InputStorageStructAccessChain
  RemapsPhysicalIndex`.
- `ninja check-feme`: 3173 Passed, 3 pre-existing Unsupported, 0 Failed
  (up 2 tests, no regressions).
- Reduced case `dEQP-VK.pipeline.pipeline_library.spec_constant.
  graphics.fragment.composite.struct.ivec2`: now Passes.
- Full `composite.struct.*` re-sweep (200 cases across 5 stages):
  **165/200 Pass, 35 Fail, 0 NotSupported** (this group has no
  NotSupported cases), up from the pre-fix 100/100/0. The remaining 35
  failures are a distinct, separately root-caused bucket -- every
  3-lane-vector-column shape (`vec3`/`ivec3`/`uvec3`, every `matNx3`,
  and `array`) across all 5 stages -- tracked separately as roadmap
  L104.
- Full `pipeline_library.spec_constant.*` re-sweep (1170 cases): **620
  Pass / 35 Fail / 515 NotSupported**, up from the pre-fix 555/100/515
  -- an exact +65/-65 shift, matching this row's own scope precisely
  (the 65 now-passing cases are every `composite.struct.*` shape except
  the 35 3-lane-vector ones), zero collateral regressions.

No advertised Vulkan feature or extension changed -- this is a pure
SPIR-V-to-LLVM conversion correctness fix for existing (struct member
layout) shapes.

## Reproduction

```console
cd /home/dev/dev/llvm-project/build2 && ninja check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n "dEQP-VK.pipeline.pipeline_library.spec_constant.graphics.*.composite.struct.*" \
  --deqp-log-images=disable --deqp-log-shader-sources=disable
```

# L104: measured impact (fix landed)

## Root cause

A **different** `DataLayout`-timing bug than L103's, though the same
root-cause family: this one is about a `vec3`'s own *alloc size* (its
own contribution to the *next* member's offset), not a preceding
member's alignment gap.

`layOutStructIfOffsetsMatch`'s gap computation used a bare, unscoped
`mlir::DataLayout`, whose generic default rule for a builtin
`VectorType` (`getDefaultTypeSizeInBits`) always rounds a vector's lane
count up to the next power of two (3 lanes -> 4, 16 bytes) when
computing size. But the *actual* SPIR-V-logical `DataLayout` string
(`e-ve-i64:64-n8:16:32:64-G10`, identical across every shader stage's
own execution model -- confirmed via direct inspection that this is
one `DataLayout`, not a distinct compute-vs-graphics pair as first
suspected) sets `vectorsAreElementAligned`, giving a `<3 x float>` a
real, tight 12-byte alloc size instead, with no power-of-two rounding
at all -- confirmed via both a hand-built standalone repro and a real
`FEME_DUMP_IR=1` capture on `composite.struct.vec3` itself.

The result: the member `e` right after a `vec3` member `d` got folded
to `offset(d) + 12` (matching the real fold-time `DataLayout`'s tight
size), but the struct type our own code built assumed `d` occupied 16
bytes -- so `e`'s real address, per the struct's own declared LLVM
type, was actually `offset(d) + 16`, 4 bytes further out. The baked GEP
silently read from `d`'s own trailing padding bytes instead of `e`'s
real value.

## Fix

Rather than trying to keep two `DataLayout`-derived size/alignment
computations in permanent lockstep (fragile, and the exact failure mode
L103 and L104 both are), the fix eliminates the ambiguity at its
source: any non-power-of-two-lane vector member (only ever `vec3` for
SPIR-V, since vectors are 2/3/4 lanes) is substituted for
`getTightVectorArrayType`'s own tight, alignment-free array form
(`!llvm.struct<"feme.tight_vector", (array<N x Scalar>)>`), reusing a
marker-struct substitution mechanism that previously existed only as an
offset-decorated-struct fallback retry. An LLVM array's own alloc size
has no target-specific rounding at all, so every later reader agrees on
it regardless of which `DataLayout` happens to be attached to the
module at that point.

Applied unconditionally in `layOutStructIfOffsetsMatch`'s non-offset
branch (not as a fallback retry, unlike the offset-decorated branch's
usage, since that branch has no declared `Offset` to validate a retry
against). Two consumer-side gaps needed closing to make this land
cleanly:

1. Two `AccessChain` component-index remap gates (in
   `remapNestedStructMemberIndices` and a sibling AccessChain-lowering
   function), previously scoped to `StructTy.hasOffset()`, were
   broadened to unconditional -- a non-offset struct's own `vec3`
   member can now also need the same marker-stepping GEP index
   inserted.
2. A `matNx3`/array-of-`vec3` struct member converts its own
   constituent's *natural* type (`!llvm.array<N x vector<...>>`, with
   *raw*, non-marker-wrapped elements) with no awareness of this
   struct-context substitution happening one level down inside its own
   column/element array. `CompositeConstructPattern::convertStruct`'s
   reassembly logic (previously a single-level "is the whole field a
   marker" unwrap) was generalized to a new recursive helper,
   `reassembleTightVectorValue`, walking both the constituent's actual
   type and the field's real (post-substitution) type in lockstep
   through any depth of `LLVM::LLVMArrayType` nesting.

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` throughout this session.
- Renamed/updated unit test
  `SPIRVToLLVMTest.InputStorageStructVec3MemberStaysAtDeclaredIndex`
  (was `...RemapsPhysicalIndex` -- the corrected layout needs no remap
  for that particular shape) and new test
  `SPIRVToLLVMTest.NonOffsetStructTightlyPacksVec3MemberSize`, modeling
  the exact CTS bug shape.
- `ninja check-feme`: 3174 Passed, 3 pre-existing Unsupported, 0 Failed
  -- no regressions.
- Reduced case `dEQP-VK.pipeline.pipeline_library.spec_constant.
  graphics.fragment.composite.struct.vec3`: now Passes.
- Full `composite.struct.*` re-sweep (200 cases): **135 Pass / 0 Fail /
  65 NotSupported**, up from L103's own closing baseline of 115/20/65
  -- a clean +20/-20 shift, fully closing this bucket (0 remaining
  failures).
- Full `pipeline_library.spec_constant.*` re-sweep (1170 cases): **655
  Pass / 0 Fail / 515 NotSupported**, up from L103's own closing
  baseline of 620/35/515 -- matching the prior session's own predicted
  655/0/515 exactly, zero collateral regressions. This closes the
  entire `spec_constant.*` group's failure count to 0 across the whole
  L99-L104 fix chain.
- A broader `pipeline_library.interface_matching.*` spot-check (for
  collateral effects beyond `spec_constant.*`) surfaced one pre-existing,
  unrelated crash (`ArrayRef::slice` assertion) and one related
  non-fatal "component out of range" error, both confirmed via baseline
  comparison to pre-date this session's fix -- tracked as roadmap L105,
  not caused by or fixed as part of L104.

No advertised Vulkan feature or extension changed -- this is a pure
SPIR-V-to-LLVM conversion correctness fix for existing (struct member
layout) shapes; `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`
are unchanged.

## Reproduction

```console
cd /home/dev/dev/llvm-project/build2 && ninja check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n "dEQP-VK.pipeline.pipeline_library.spec_constant.*" \
  --deqp-log-images=disable --deqp-log-shader-sources=disable
```

# L105: measured impact (fix landed for CanonicalizeStage.cpp's own consumer-side bug; producer-side gap split off as L107)

## Root cause

Not Component-decoration-related (the initial hypothesis, a red herring
from the first-reported case happening to also carry a `Component`
decoration) -- a general bug in `CanonicalizeStage.cpp`'s own
consumer-side stage-IO struct-member mapping, in the same root-cause
family as L103/L104.

`layOutStructIfOffsetsMatch`'s non-offset branch (added by L103, tightened
by L104) can insert a synthetic `[N x i8]` alignment-gap pad field at
**any** physical position in a struct -- not just leading -- whenever two
adjacent real members have a natural-alignment gap between them (e.g.
`float dummy; vec4 v;`, needing 12 bytes of padding so `v` lands on its
own 16-byte natural alignment). `CanonicalizeStage.cpp`'s own consumer
side had five separate places that only ever recognized a **leading**
pad (`HasLeadingPad`/`ST->getNumElements() == MemberDecorations.size() +
1`, an artifact of how much narrower the pad shape was when this logic
was first written), silently mis-mapping every declared member after an
interior gap to the wrong physical LLVM struct field:

1. `resolveOffsetWithinElement`'s `IDStart` computation (block member
   path).
2. `addElements`' `TakeBlockPath` physical-index mapping (construction
   side).
3. `addStageIOStructMembers` (loose, non-`Block` struct/array-of-struct
   path).
4. `getStageIOFlattenedRowCount`/`getStageIOLeafElementCount` (nested
   multi-member struct row/leaf counting).
5. `resolveNestedStageIOField`'s own `IDStart` computation (nested
   struct member path).

Debugged via a temporary `errs()` trace gated on an env var
(`FEME_L105_TRACE`), printing every constructed `SignatureElement`'s
`ElementID`/`FirstComponent`/`ComponentCount`/`RowCount` -- this revealed
a 2-real-member block producing 4 malformed `SignatureElement`s directly,
far more effective than guessing from the crash backtrace alone.

## Fix

Added a shared `isStageIOPadField(Type *FieldTy)` helper: a synthetic pad
is always shaped as `[N x i8]` (an `ArrayType` of `i8`), an unambiguous
signal since a real stage-IO member's own GLSL/HLSL-visible type
(scalar/vector/matrix/struct thereof) is never itself a raw byte array --
the same technique the original leading-only checks already used, just
generalized to check every physical field rather than only field 0. Used
it to generalize all five locations above from leading-only to
any-position pad skipping.

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` at session start.
- New unit test
  `CanonicalizeStageTest.MapsMultiMemberInterfaceBlockWithInteriorPadToDistinctMembers`,
  modeled on the existing leading-pad sibling test.
- `ninja check-feme`: 3175 Passed, 3 pre-existing Unsupported, 0 Failed --
  no regressions.
- Both originally-reported crash cases (`out_component0_in_none_member_of_
  block_vert_out_frag_in`, `out_none_in_flat_member_of_block_vert_out_frag_in`)
  now Pass.
- A manual 72-case `member_of_block` decoration_mismatch sweep (run one
  case at a time, since an `abort()` kills the whole `deqp-vk` process):
  **0 Pass / 40 Fail / 32 Assertion pre-fix**, confirming the bug's real
  scope was the entire subgroup, not the 2-3 cases the roadmap entry
  originally described.
- Full `pipeline_library.interface_matching.decoration_mismatch.*`
  re-sweep (360 cases, same one-at-a-time methodology): **354 Pass / 0
  Fail / 6 Assert**, up from widespread failure across the
  `member_of_block` subgroup pre-fix. The remaining 6 crashes are a
  single, narrow, distinct shape (a loose, non-`Block`
  array-of-structures `Output` variable at exactly the
  tessellation-control stage boundary, e.g. `out_flat_in_none_member_of_
  array_of_structures_vert_tesc_out_tese_in_frag`) -- traced via
  `gdb`/temporary tracing to a **separate, producer-side** gap in
  `SPIRVToLLVMPatterns.cpp`'s own GEP-index remapping (the compiled IR
  itself bakes in a byte offset that assumes an un-padded layout,
  disagreeing with the real padded struct `CanonicalizeStage.cpp`
  correctly resolves against), not this consumer-side bug family --
  split off and tracked separately as roadmap L107.

No advertised Vulkan feature or extension changed -- this is a pure
stage-IO struct-layout consumer-side correctness fix; `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` are unchanged.

## Reproduction

```console
cd /home/dev/dev/llvm-project/build2 && ninja check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n "dEQP-VK.pipeline.pipeline_library.interface_matching.decoration_mismatch.*" \
  --deqp-log-images=disable --deqp-log-shader-sources=disable
```
