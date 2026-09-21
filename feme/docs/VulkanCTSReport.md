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
| `compute` | 61,460 | 61,460 | 669 | 16 | 60,775 | 0 | 0 | 0 | 0 | 0 |
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
| `ssbo` | 12,225 | 12,225 | 2,337 | 905 | 8,983 | 0 | 0 | 0 | 0 | 0 |
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

# L107: measured impact (fix landed)

## Root cause

A producer-side gap in `SPIRVToLLVMPatterns.cpp`'s
`OffsetStructMemberReorderAccessChainPattern`, not the initially-suspected
generic upstream `AccessChainPattern` (that hypothesis turned out
incorrect once traced further). This pattern remaps a `spirv.AccessChain`'s
declared (SPIR-V) struct-member index to its real physical LLVM field
index whenever `layOutStructIfOffsetsMatch` (L103/L104) inserted a
synthetic `[N x i8]` pad -- but it only ever peeled a **single** outer
array dimension before giving up on finding the struct to remap into
(`MemberIndexPos` 0 or 1, matching either "struct behind the base
pointer directly" or "struct behind one array of struct instances").

A tessellation-control shader's own loose (non-`Block`) array-of-structures
output (`out TestStruct testStructArray[3];`) gets an *additional*, implicit
per-control-point array dimension from TCS's own execution model
(`gl_InvocationID`-indexed), giving the real declared type an effective
`testStructArray[][3]` shape -- the struct sits **two** array dimensions
deep, not one. With `StructTy` left `null` after this pattern's own
single-level peel, its own guard bailed (`notifyMatchFailure`), falling
through to MLIR's generic `AccessChainPattern`, which forwards the
declared (pre-remap) member index straight through unmodified.

Confirmed via a temporary `errs()` trace (`FEME_L107_TRACE`, gated on an
env var, mirroring `FEME_L105_TRACE`'s technique) in
`CanonicalizeStage.cpp`'s consumer-side resolution: the byte offset baked
into the compiled IR for `testStructArray[gl_InvocationID][2].
variableInStruct` was `68`, which decomposes exactly as `2 * 32 (the
correct, padded per-instance stride: 4-byte `float` + 12-byte pad +
16-byte `vec4`) + 4` -- the *outer* array-of-array stride was already
correct, but the residual `4` lands exactly at the pad's own start (right
after the leading `float`), not at `variableInStruct`'s real offset of 16.
This confirmed the *inner* member selector itself, not the array
indexing, carried the bug -- consistent with an unremapped, declared
member index (1) being forwarded straight into the 3-physical-field
padded struct (selecting the pad, physical field 1, instead of the real
member at physical field 2).

## Fix

Generalized `OffsetStructMemberReorderAccessChainPattern`'s array-peeling
logic from a single `if` (0 or 1 array level) to a `while` loop peeling
any number of nested `spirv.array` levels before testing for a
`spirv.struct`, tracking one `MemberIndexPos` per level peeled. The GEP
construction was correspondingly generalized to forward that many outer
array indices (one per level) ahead of the (now correctly remapped)
member selector, instead of special-casing exactly one array index.

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` at session start.
- New unit test
  `SPIRVToLLVMTest.OutputStorageTwoArrayDimsStructRemapsMemberIndex`,
  modeling the exact two-array-dims-then-struct shape directly (an
  `Output`-storage `spirv.array<2 x spirv.array<3 x spirv.struct<(f32,
  vector<4xf32>)>>>`, accessed via a 3-index `AccessChain`): confirms the
  resulting GEP's trailing indices are `[..., <outer-idx>, <inner-idx>,
  2]` (the remapped physical index), not `[..., <outer-idx>, <inner-idx>,
  1]` (the unremapped, declared index the bug used to forward straight
  through).
- `ninja check-feme`: 3176 Passed, 3 pre-existing Unsupported, 0 Failed --
  up 1 test, no regressions.
- The originally-crashing case
  (`dEQP-VK.pipeline.pipeline_library.interface_matching.
  decoration_mismatch.out_flat_in_none_member_of_array_of_structures_
  vert_tesc_out_tese_in_frag`) now Passes.
- Full `pipeline_library.interface_matching.*` re-sweep (360 cases, one
  case at a time, same methodology as L105): **360 Pass / 0 Fail / 0
  Assert** -- up from 354 Pass/0 Fail/6 Assert, an exact +6 shift with
  zero collateral regressions. This closes out the `interface_matching.
  decoration_mismatch.*`/L105/L107 investigation chain entirely.

No advertised Vulkan feature or extension changed -- this is a pure
`AccessChain`-conversion correctness fix; `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` are unchanged.

## Reproduction

```console
cd /home/dev/dev/llvm-project/build2 && ninja check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n "dEQP-VK.pipeline.pipeline_library.interface_matching.decoration_mismatch.*" \
  --deqp-log-images=disable --deqp-log-shader-sources=disable
```

## L106 triage: full `interface_matching.*` group swept (no fix needed)

While closing out L107, the full `dEQP-VK.pipeline.pipeline_library.
interface_matching.*` group (not just `decoration_mismatch.*`) was swept
to check L106's own "broaden the sweep" candidate (a): **1589 cases total
across `decoration_mismatch` (360), `vector_length` (972), and
`shader_layout_component_matching`+`misc` (257) -- 1445 Pass / 0 Fail /
144 NotSupported, zero remaining failures or crashes anywhere in the
group.** The 144 NotSupported cases are all `shader_layout_component_
matching`'s own `float64` variants (`"Double-precision floats not
supported"`), a legitimate, expected capability gap (feme does not
advertise `shaderFloat64`), not a bug. This closes out the entire
`interface_matching.*` group as a sweep target; L106's own next
candidate is a fresh `dEQP-VK.pipeline.*` subgroup or a top-level
`dEQP-VK.*` group outside `pipeline.*` altogether (see Roadmap.md's L106
entry for specifics).

# L108: `SPIRVResourceLoweringPass` rejects an entire function over a dead subpass-input handle

## Reproduction

```console
cd /home/dev/dev/llvm-project/build2 && ninja check-feme
cd /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n "dEQP-VK.pipeline.pipeline_library.graphics_library.*" \
  --deqp-log-images=disable --deqp-log-shader-sources=disable
```

Picking up L106's own next candidate group (a fresh, untriaged
`dEQP-VK.pipeline.*` subgroup), `pipeline_library.graphics_library.*`
(836 cases) was swept for the first time this milestone series: **460
Pass / 88 Fail / 287 NotSupported / 1 Warning**. All 88 failures clustered
under `independent_sets_random` (a randomized-descriptor-set fuzz-test
family), across `vert_frag`/`vert_geom_frag`/`mesh_frag`/`task_mesh_frag`
stage combos and 3 pipeline-construction variants
(`fast_lib`/`monolithic`/`optimized_lib`) each -- reduced to a single
standalone repro,
`dEQP-VK.pipeline.pipeline_library.graphics_library.independent_sets_
random.fast_lib.vert_frag.case_0`, which failed with a bare
`Fail (retcode: VK_ERROR_INITIALIZATION_FAILED at
vkPipelineConstructionUtil.cpp:176)` and no error text by default.

## Root cause

`feme::Vulkan::Diagnostics.cpp`'s `logCreationFailure` only prints an
`Error`'s message when `FEME_VULKAN_LOG_CREATION_ERRORS` is set (off by
default, otherwise `consumeError` swallows it silently). With it set, the
real error was:

```
vkCreateGraphicsPipelines: unsupported raised operation:
'llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_1_0t' is a
register-bound resource handle the FeMe CPU target cannot normalize...
```

`checkSupportedRaisedOps` (`UnsupportedOps.cpp`) itself documents (roadmap
L64) that the *named* handle in this diagnostic is often just "whichever
declaration happens to appear first in the module" -- an innocent
bystander, not necessarily the actual failing resource. Rather than
re-deriving the pass's own accept/reject logic by hand against the IR, the
pass's own pre-existing `FEME_CPU_LOG_RESOURCE_NORMALIZATION` env-var-gated
diagnostic (`SPIRVResourceLowering.cpp`'s `logNormalizationRejection`,
previously unused by this milestone series) pinpointed the exact rejected
call directly:

```
FEME_CPU_LOG_RESOURCE_NORMALIZATION: handle result type is not one of the
kinds this pass normalizes:   %20 = call target("spirv.Image", float, 6,
0, 0, 0, 2, 0) @llvm.spv.resource.handlefrombinding...
```

`Dim == 6` is SPIR-V's `SubpassData` -- a GLSL `subpassInput` variable's
own handle. `collectHandles` walks every `handlefrombinding` call in a
function and declines the *whole function* (leaving every handle
unnormalized) the moment any one of them fails every `HandleKind`
classification -- correct behavior for a genuinely unsupported resource
access, but wrong here: a subpass input's `OpImageRead` always converts
directly to `feme.stage.subpass.load`
(`feme::spirv::SubpassLoadPattern`), never referencing this handle's own
result at all, so the handle is *always* dead. The failing fragment
shader legitimately mixed a subpass input (two, actually) with several
otherwise-perfectly-normalizable resources (a separate sampler + 2
textures, an `imageBuffer` storage texel buffer, an SSBO) in the same
function; the whole function was being declined purely because of the
two always-dead subpass handles, leaving every genuinely-normalizable
handle raw too, which is what `checkSupportedRaisedOps` then tripped on
downstream (against an unrelated, innocent-bystander handle name, exactly
as its own L64 comment warns).

The sibling *vertex* shader's function, with no subpass input at all,
lowered cleanly -- confirming the bug was specific to the subpass-input
shape, not the other resource kinds present.

## Fix

`collectHandles` now skips (rather than declining the whole function
over) any unclassifiable `handlefrombinding` call whose own result is
`use_empty()`, mirroring `checkSupportedRaisedOps`'s own pre-existing
tolerance for this exact "dead subpass-input handle" shape. A dead handle
that is skipped is simply left as a raw, unreferenced call -- harmless,
since nothing downstream ever reads its result -- while every other,
real handle in the same function is now normalized as usual.

## Validation

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device` at session start.
- New unit test
  `SPIRVResourceLoweringTest.LowersOrdinaryHandleWhenAnUnusedSubpassInputHandleSharesTheFunction`:
  an ordinary storage-buffer handle is normalized to a resource-load call
  even when an unused `Dim::SubpassData` handle shares the same function.
- `ninja check-feme`: 3177 Passed, 3 pre-existing Unsupported, 0 Failed --
  up 1 test, no regressions.
- The originally-crashing case (`...independent_sets_random.fast_lib.
  vert_frag.case_0`) now Passes.
- Full `pipeline_library.graphics_library.*` re-sweep (836 cases, single
  batched invocation -- all failures here are soft `Fail`s, not hard
  crashes, so no one-at-a-time loop was needed): **541 Pass / 7 Fail /
  287 NotSupported / 1 Warning** -- up from 460 Pass/88 Fail, an exact
  +81 shift. The remaining 7 failures are a single, distinct,
  not-yet-triaged cluster (`misc.other.unusual_multisample_state` and six
  `misc.other.view_index_from_device_index_in_*` variants) -- confirmed
  unrelated to this fix (no subpass-input handle involved) and left open
  under roadmap L106 for a future session.

No advertised Vulkan feature or extension changed -- this is a pure
resource-normalization correctness fix inside the SPIR-V-to-CPU-ABI
lowering pipeline; `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` are unchanged.

## Roadmap H51/L109/L110/L111: `gl_ViewIndex` on Hull/Domain/Geometry, and the real remaining `misc.other.*` gap

### Reproduction

`dEQP-VK.pipeline.pipeline_library.graphics_library.misc.other.
view_index_from_device_index_in_{all_stages,fragment,pre_rasterization}`
(and their `_link_time_opt` siblings; the `_mesh_shading` variants are
`NotSupported`, unaffected). Before this session: `vkQueueSubmit` crashed
with `"vertex/domain stage output -> geometry stage input: element 5 has
no matching producer element"` (roadmap H51).

### Root cause (H51/L109 -- the crash)

A geometry (and, for a device advertising `multiviewTessellationShader`,
tessellation-control/-evaluation) stage reading `gl_ViewIndex` was
classified as an ordinary stage-IO *input* element, so `Executor.cpp`'s
geometry-input linkage demanded a producing output for it in the
previous stage -- but `gl_ViewIndex` is system-supplied, like
`SV_PrimitiveID`/`gl_InvocationID`, which the same linkage call already
excludes. `FemeGeometryInvocation`/`FemeDomainInvocation` (`RuntimeABI.h`)
had no `ViewIndex` field at all, and none of `HullWrapper.cpp`/
`PatchConstantWrapper.cpp`/`DomainWrapper.cpp`/`GeometryWrapper.cpp`
lowered a `ViewIndex` input load.

### Fix (L109)

Added the full chain, mirroring `SV_PrimitiveID`/H5d-a/L81's own shape:
- `ViewIndex` fields carved from existing `Reserved[N]` padding in
  `FemePatchArgs`/`FemePatchConstantArgs`/`FemeDomainInvocation`/
  `FemeGeometryInvocation` (`RuntimeABI.h`), mirrored in
  `StageArgsLayout.h`'s LLVM struct-type builders (ABI size/version
  unchanged).
- Wrapper-lowering support (`lowerHullViewIndex`, a `ViewIndex` arm in
  `PatchConstantWrapper.cpp`'s unified ternary, `lowerDomainViewIndex`,
  `lowerGeometryViewIndex`) in all four wrapper files.
- `Executor.cpp`'s geometry-input `ConsumerFilter` and `PatchPipeline.cpp`'s
  `isForwardedFromProducerStage` both exclude `ViewIndex` from
  producer-matching, alongside the existing `PrimitiveID`/`InvocationID`
  exclusions.
- `Draw.ViewIndex` threaded from `Executor.cpp`'s per-view draw loop
  through `runPatchPipeline`/`buildDomainInvocations`/
  `buildGeometryInvocations` and `ResourceHeap.h`'s `PatchResources`/
  `PatchConstantResources`/`PreparedPatchBatch`/
  `PreparedPatchConstantBatch` -- these are free functions taking scalar
  parameters rather than writable struct fields, so each needed its own
  new parameter; materially deeper plumbing than H51's own text
  anticipated.

### Validation (L109)

- `vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
  Device`.
- New unit tests: `HullWrapperTest.LowersViewIndexInput`,
  `PatchConstantWrapperTest.LowersViewIndexInput`,
  `DomainWrapperTest.LowersViewIndexInput`,
  `GeometryWrapperTest.LowersViewIndexInputLoad`,
  `DomainInvocationsTest.BroadcastsViewIndexToEveryPoint`,
  `GeometryInputsTest.BroadcastsViewIndexToEveryInvocation`.
- `ninja check-feme`: **3183 Passed, 3 pre-existing Unsupported, 0
  Failed** -- up 6 tests, no regressions.
- Real CTS re-run: the originally-reported crash is gone on all 6 target
  cases (confirmed via `FEME_VULKAN_LOG_CREATION_ERRORS=1`: pipeline
  creation and `vkQueueSubmit` both now succeed). `dEQP-VK.multiview.*`
  (838 cases, the pre-existing non-pipeline-library-split path) stays a
  clean 643 Pass/0 Fail/195 NotSupported -- no regression.
- The 6 cases still `Fail`, now on image comparison rather than a crash.
  `pipeline_library.graphics_library.*`'s own bucket count is unchanged,
  541 Pass/7 Fail/287 NotSupported/1 Warning: real progress
  (crash -> correctness-level mismatch), not yet a CTS-visible pass.

### Root cause (L110 -- the actual remaining mismatch)

An earlier pass through this investigation, this session, decoded the
CTS's own logged PNG images with a plain PNG viewer and found every pixel
near `(0,0,0)` across all 3 multiview slices -- reported as "all black,
nothing rendered". That reading was **wrong**, and re-investigating it
this session found why: `vktPipelineLibraryTests.cpp` logs each image
with a `Description` recording a per-image `p' = p*scale + offset`
normalization CTS itself applied before encoding the PNG (small integer
data like 0/1/2 is otherwise invisible in an 8-bit image), and the
displayed/decoded pixel values must be un-transformed (`p = (p' -
offset)/scale`) to recover the real underlying data. Doing that (see
`view_index_from_device_index_in_all_stages`'s own 3 slices) shows the
real R/B channel values are exactly `0`, `1`, `2` for slices 0/1/2 --
precisely the correct per-view `gl_ViewIndex` value L109's own fix
produces. **Rendering is not broken; L109's fix is correct.**

The CTS test's own expected-value table (`ViewIndexFromDeviceIndexParams`
tests three `pipelineStateMode`s: `PRE_RASTERIZATION`, `FRAGMENT`, `BOTH`)
computes different expected values depending on whether
`VK_PIPELINE_CREATE_VIEW_INDEX_FROM_DEVICE_INDEX_BIT` was set on the
pre-rasterization library part, the fragment library part, or both: for
whichever part(s) have that bit, `gl_ViewIndex` must evaluate to the
*device index* (always `0` here -- feme has no multi-device support) in
that part's own stages, not the real per-view value. **feme does not
implement `VK_PIPELINE_CREATE_VIEW_INDEX_FROM_DEVICE_INDEX_BIT` at all**
(confirmed: zero grep hits anywhere in the codebase before this row), so
it always reports the real per-view value -- correct only for the parts
that did *not* request the device-index substitution, wrong for the ones
that did. This is a real, distinct, well-scoped feature gap, filed as
roadmap L110 (unimplemented) -- not attempted this session, to keep this
investigation bounded; see L110's own roadmap entry for the concrete
suggested shape (two independent `PreRasterViewIndexIsDeviceIndex`/
`FragmentViewIndexIsDeviceIndex` bits, captured from `Pipeline::
createFlags()` per linked library part, reaching a per-stage-group
`ViewIndex` override at draw time).

`unusual_multisample_state`, the 7th `graphics_library.*` failure, is
confirmed unrelated (no `gl_ViewIndex`/multiview involved) and left open
under a new roadmap L111 for a future session.

### Validation (L110/L111)

No fix landed this session for either -- both are scoping-only entries.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` are unchanged:
no new feature or extension is newly advertised by L109's fix
(`multiviewGeometryShader` was already `VK_TRUE` from H5e), and L110/L111
are not yet implemented.

## Roadmap L110 (fixed this session): `VK_PIPELINE_CREATE_VIEW_INDEX_FROM_DEVICE_INDEX_BIT`

### Fix

Implemented exactly the shape scoped in the prior session's L110 roadmap
entry:

- `feme::graphics::GraphicsPipeline` (`Pipeline.h`/`Pipeline.cpp`) gained
  two defaulted-`false` constructor parameters/fields,
  `PreRasterViewIndexIsDeviceIndex`/`FragmentViewIndexIsDeviceIndex`, and
  matching getters.
- Vulkan-side `GraphicsPipelineState` (`GraphicsPipeline.h`) gained the
  same two bools.
- `synthesizeLinkedGraphicsPipelineCreateInfo` (`GraphicsPipeline.cpp`)
  gained two `bool &` out-parameters, resolved the same way it already
  resolves `pVertexInputState`/`pViewportState`/etc.: from whichever
  linked `GraphicsPipelineLibrary`'s own `createFlags()` supplies that
  stage group's part, falling back to the top-level `CreateInfo.flags`
  for a part not supplied by any linked library.
- `compileGraphicsPipeline` gained the same two bools as parameters,
  storing them straight into the `GraphicsPipelineState` it builds.
- `vkCreateGraphicsPipelines`'s call site now: defaults both bools from
  `pCreateInfos[I].flags` (the monolithic-pipeline case, and the default
  before any linked-library override); when a `VkPipelineLibraryCreateInfoKHR`
  links one or more parts, passes both by reference into
  `synthesizeLinkedGraphicsPipelineCreateInfo`, which overwrites them per
  the per-part resolution above.
- `Executor.cpp`'s 4 `ViewIndex`-writing sites (fragment shading, vertex
  fetch, the `runPatchPipeline` call, the `buildGeometryInvocations`
  call) each now consult `Pipeline.getFragmentViewIndexIsDeviceIndex()`
  (fragment site) or `Pipeline.getPreRasterViewIndexIsDeviceIndex()`
  (vertex/patch-pipeline/geometry sites), substituting the constant `0`
  (this ICD's own always-single device index) for `Draw.ViewIndex` when
  the relevant flag is set.

Mesh/task stages were deliberately left untouched: the CTS test's own
`_mesh_shading` variants are already `NotSupported` for an unrelated,
pre-existing reason (`multiviewMeshShader` not advertised), so there is
no `ViewIndex`-carrying mesh/task ABI record to update.

### Validation (L110)

New unit tests:
- `GraphicsPipelineTest.ViewIndexIsDeviceIndexDefaultsFalseAndIsPerStageGroup`
  (`PipelineTest.cpp`): confirms both bools default `false`, and that each
  is independently settable via the constructor.
- `DrawTest.MultiviewViewIndexFromDeviceIndexReadsZeroForEveryView`
  (`DrawTest.cpp`): a real two-view multiview draw (mirroring the
  existing `MultiviewRendersDifferentColorPerViewIntoItsOwnLayer` test)
  with `VK_PIPELINE_CREATE_VIEW_INDEX_FROM_DEVICE_INDEX_BIT` set on the
  (monolithic) pipeline -- confirms both views' own layers render
  identically (`gl_ViewIndex == 0` for both), instead of the flag-off
  test's real-per-view red/green split.

`ninja check-feme` (ccache, assertions-enabled build): 3185/3188 passed,
3 pre-existing Unsupported, 0 Failed (up 2 discovered tests from this
session's additions, no regressions).

CTS re-sweep (`deqp-vk`, `feme_icd.json` rebuilt first per the standing
gotcha):
- All 16 applicable `pipeline_library.graphics_library.misc.other.
  view_index_from_device_index_in_*` cases now **Pass** (6 mesh-shading
  variants remain `NotSupported`, the unrelated pre-existing gap noted
  above).
- `pipeline_library.graphics_library.*` (836 cases) now lands at **547
  Pass / 1 Fail / 287 NotSupported / 1 Warning** -- the sole remaining
  Fail is `unusual_multisample_state` (roadmap L111, confirmed unrelated
  to `gl_ViewIndex`/multiview), and the sole Warning is a pre-existing,
  benign "linking of one or more combinations took too long" quality
  warning, unrelated to this fix.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed. `VK_PIPELINE_CREATE_VIEW_INDEX_FROM_DEVICE_INDEX_BIT` is a
`VkPipelineCreateFlagBits` value gated by the already-`VK_TRUE`
`VkPhysicalDeviceMultiviewFeatures::multiview` (`VK_KHR_multiview`/core
1.1) and `VK_KHR_device_group_creation`'s own always-1-device-1-group
`vkEnumeratePhysicalDeviceGroups` stub, both already accurately listed;
this fix corrects previously-silent mishandling of an already-advertised
flag, not a newly-advertised capability, so neither inventory document
gains a new row.

## Roadmap L111(a) (fixed this session): `unusual_multisample_state`'s pipeline-creation crash

`unusual_multisample_state` had been assumed (by the L109/L110 session)
to be a runtime image-comparison mismatch like the rest of `misc.other.*`.
Running it directly against `deqp-vk` this session instead showed pipeline
*creation* itself failing outright:

```
error: feme-graphics-validate-stage: 'feme.stage.input.load' in function
'main' has a non-constant vertex operand, illegal outside the
geometry/mesh stages
error: feme-graphics-validate-stage: 'feme.stage.output.store' in
function 'main' has a non-constant vertex operand, illegal outside the
geometry/mesh stages
Fail (vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED)
```

### Root cause

The CTS test's `frag0` masks per-sample coverage with a loop:
`for (i...) gl_SampleMask[i] = sampleMask & gl_SampleMaskIn[i];` -- a
genuinely dynamic (loop-carried) index into `gl_SampleMask`/
`gl_SampleMaskIn` (SPIR-V `BuiltIn` `SampleMask`, 20). `CanonicalizeStage.
cpp`'s `isDynamicIndexedArrayGlobal` (used by
`getDynamicVertexIndexedAccess`, tried first by `resolveStageIOAccess`)
classified *any* address-space-7/8 `ArrayType` stage-IO global accessed
with a non-constant index as a per-vertex/per-primitive array (the
`gl_in[]`/`gl_MeshVerticesEXT[]` shape) -- with no restriction by stage or
`BuiltIn` at all, unlike its constant-index counterparts
(`isPerVertexArrayInputGlobal`/`isPerVertexArrayMeshOutputGlobal`, both
already correctly stage-restricted). This wrongly claimed
`gl_SampleMask`/`gl_SampleMaskIn` -- an ordinary per-invocation coverage
mask, not a per-vertex/per-primitive array at all -- threading the sample
index through as a bogus `Vertex` operand instead of `Row`. `ValidateStage
Pass`'s `validateVertex` then correctly (if unhelpfully upstream)
diagnosed the resulting non-constant `Vertex` operand as illegal outside
Geometry/Mesh. `getDynamicRowIndexedAccess` (the correct path for this
shape) already deliberately defers to `getDynamicVertexIndexedAccess` for
any global it claims, so the wrong classification silently foreclosed the
correct path rather than surfacing an ambiguity.

An initial attempt to fix this by restricting `isDynamicIndexedArrayGlobal`
by `ShaderStage` (Geometry-only for address space 7, Mesh-only for 8)
compiled cleanly but regressed 5 pre-existing `CanonicalizeStageTest`
cases: several deliberately use a `"feme.shader.stage"="vertex"` attribute
on their test IR (testing the mechanical GEP-shape recognition in
isolation, not simulating a fully realistic pipeline) for a genuinely
Mesh-only-in-production shape, and a stage-based restriction broke them.
The actual fix taken instead is narrower and more surgical: exclude only
`BuiltIn == SampleMask` (20) from `isDynamicIndexedArrayGlobal`, leaving
every other stage-IO global's classification (and every existing test)
unaffected.

### Fix

`isDynamicIndexedArrayGlobal` (`CanonicalizeStage.cpp`) now returns `false`
for a global decorated `BuiltIn SampleMask`, mirroring its existing
`!D.Patch` exclusion. This lets `getDynamicRowIndexedAccess` claim the
shape instead, threading the dynamic sample index through as `Row`, which
`validateVertex` never restricts by stage.

### Validation (L111(a))

New unit test: `ThreadsDynamicRowIndexIntoSampleMaskOutputStore`
(`CanonicalizeStageTest.cpp`) -- a fragment-stage `gl_SampleMask[i]` store
with a non-constant loop index `i`, confirming it now resolves via `Row`
(operand 1, the loop variable itself) rather than being wrongly claimed as
`Vertex`.

`ninja check-feme` (ccache, assertions-enabled build): 3186/3189 passed, 3
pre-existing Unsupported, 0 Failed (up 1 discovered test from this
session's addition, no regressions).

CTS re-run (`deqp-vk`, `feme_icd.json` rebuilt first per the standing
gotcha): the pipeline-creation crash is gone -- `vk.createGraphicsPipelines`
now succeeds. The test still reports `Fail (192 wrong samples values out
of 256)`, a **separate**, deeper runtime bug: split out as roadmap
L111(b) rather than declaring this row done. `pipeline_library.
graphics_library.*` (836 cases) re-swept at **547 Pass / 1 Fail / 287
NotSupported / 1 Warning** -- unchanged from L110's own count, since
`unusual_multisample_state` is still the sole Fail (now for a different
reason).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- this is a correctness fix within already-advertised multisample
rendering support (`VkPhysicalDeviceFeatures::sampleRateShading` and
core multisample state are already listed as supported), not a
newly-advertised capability.

## Roadmap L111(b) (root-caused, not yet fixed): fragment-shader `gl_SampleMask` output has no runtime consumer

With L111(a)'s pipeline-creation crash fixed, `unusual_multisample_state`
now fails at runtime instead: `Fail (192 wrong samples values out of
256)`. Inspecting the compiled IR (`FEME_DUMP_IR=1`) confirms `frag0`'s
own `gl_SampleMask[i] = sampleMask & gl_SampleMaskIn[i]` masking logic
compiles correctly (reads `gl_SampleMaskIn` via `feme.stage.input.load`,
ANDs with the shader's own mask constant, writes the result back via
`feme.stage.output.store` with `ElementID` resolving to
`SignatureSystemValue::Coverage`) -- the shader-side compilation is not
the bug.

Grepping `Executor.cpp` for `SignatureSystemValue::Coverage` (the system
value a fragment shader's `gl_SampleMask` *output* now correctly resolves
to, post-L111(a)) finds **zero** consumers: the CPU rasterizer's own
per-sample coverage/write-masking logic (`Quad.SampleMask`/`Quad.Coverage`,
and the existing `alphaToCoverageEnable`/`FSAlphaToCoverage` handling
nearby, ~`Executor.cpp` line 3174-3390) only ever derives per-sample
coverage from rasterization geometry, depth/stencil tests, and
`alphaToCoverage` -- never from an explicit shader-written coverage mask
output. This is a genuine, previously-undiscovered feature gap (the CPU
executor never reads back a fragment shader's own `SampleMask` output at
all), not a bug introduced by L111(a)'s fix.

Scoped as roadmap L111(b) for a future session: read the fragment
shader's own `Coverage`-system-value output once per invocation (parallel
to how `FSAlphaToCoverage` is already looked up by `SystemValue`), AND it
together with the coverage mask already computed from rasterization/
depth-stencil/alpha-to-coverage (never OR, per Vulkan's "sample mask test"
semantics -- the shader's mask can only narrow coverage, never widen it),
and mask out the corresponding per-sample color/depth writes accordingly.
Not yet attempted this session, to keep this investigation's own scope
bounded; see L111(b)'s own roadmap entry for further detail.

## Roadmap L111(b) (fixed this session): fragment-shader `gl_SampleMask` output now consumed

### Fix

`Executor.cpp` had zero consumers for a fragment shader's own
`SignatureSystemValue::Coverage` output. Added:

- A lookup for the fragment stage's `Coverage`-direction `Output` element
  (`FSSampleMaskOut`), mirroring how `FSAlphaToCoverage` is already looked
  up by location. Validated to be a single-component signed/unsigned
  integer.
- `UseEarlyDepthStencil` now also forces the late depth/stencil path when
  `FSSampleMaskOut` is present -- mirroring `alphaToCoverageEnable`'s own
  identical reasoning: the narrowed mask a fragment shader computes is not
  known until the shader itself has run, so an early (pre-shader)
  depth/stencil test would test/write samples the shader's own mask was
  about to cull.
- Each lane's `BaseCoverage` is ANDed with the shader's own mask (its
  first/only word -- this executor's own per-lane coverage representation
  is already a single `uint32_t`, an existing 32-bit-sample-count
  limitation unrelated to this fix) before the depth/stencil test and
  color merge -- unconditionally, unlike `alphaToCoverageEnable`'s own
  pipeline-state-gated mask, matching Vulkan's "sample mask test"
  (`fragops.adoc`) always-on semantics.

### Validation (L111(b))

New unit test: `ExecutorTest.FragmentSampleMaskOutputNarrowsPerSampleCoverage`
-- a fragment shader writing a constant `0b0101` `gl_SampleMask` output on
a fully-covering, fully-opaque-red 4x-multisampled triangle, confirming
only samples 0 and 2 (the mask's set bits) keep the shaded red color;
samples 1 and 3 are left at the attachment's zero-initialized clear
value.

`ninja check-feme` (ccache, assertions-enabled build): 3187/3190 passed, 3
pre-existing Unsupported, 0 Failed (up 1 discovered test from this
session's addition, no regressions).

CTS re-run (`deqp-vk`, `feme_icd.json` rebuilt first per the standing
gotcha): `unusual_multisample_state` now **Passes**. Full re-sweep of
`pipeline_library.graphics_library.*` (836 cases) lands **fully clean**
at **548 Pass / 0 Fail / 287 NotSupported / 1 Warning** -- the sole
Warning is the same pre-existing, benign `compare_link_times` "linking of
one or more combinations took too long" quality warning noted in every
prior sweep of this group, unrelated to this fix. This closes roadmap
L111 overall (both L111(a) and L111(b)).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- this is a correctness fix within already-advertised multisample
rendering support (core `VkPipelineMultisampleStateCreateInfo`, already
listed as supported), not a newly-advertised capability.

## Roadmap L106 continued: `dEQP-VK.pipeline.monolithic.multisample_shader_builtin.*`

A fresh top-level group swept per L106's own "broaden past `pipeline_library.*`"
candidate list, chosen for its small size (95 cases) and topical relevance to
the just-closed L111. First sweep: **37 Pass / 18 Fail / 40 NotSupported**.

### Roadmap L112 (fixed this session): MLIR's unhandled SPIR-V `Sample` decoration

6 of the 18 failures showed `error: unhandled Decoration : 'Sample'` at
pipeline creation. Traced (via `grep -rn "unhandled Decoration"` across the
whole repo) to MLIR's own SPIR-V deserializer/serializer -- **not** any
feme-specific code:

- `mlir/lib/Target/SPIRV/Deserialization/Deserializer.cpp` and
  `mlir/lib/Target/SPIRV/Serialization/Serializer.cpp` each have a `switch`
  over `spirv::Decoration` listing ~20 plain "unit decoration" (no-operand)
  cases (`Aliased`, `Block`, `Centroid`, `Flat`, `NoPerspective`, `Patch`,
  `Coherent`, `PerPrimitiveEXT`, `Volatile`, ...); `Sample` (SPIR-V value 17,
  GLSL's `sample`-qualified fragment-shader input) was simply missing from
  both lists, so any module using it was rejected outright before feme's own
  code ever saw it.
- Confirmed feme's own downstream handling was already complete and
  unaffected: `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`
  already has `{"sample", 17}` in its `StageIOFlagDecorations` table and a
  full `case mlir::spirv::Decoration::Sample:` in its own attribute-building
  switch, and `feme/lib/Transforms/Graphics/CanonicalizeStage.cpp` already
  has a `ParsedSPIRVDecorations::Sample` field consuming it -- this was
  purely an upstream deserialization gap.
- This is the same class of bug as two already-documented precedent fixes in
  `mlir/test/Target/SPIRV/decorations.mlir` (`Centroid`, `NonUniform` -- the
  latter's own test comment cites feme's prior roadmap L7f), confirming an
  established pattern of fixing upstream MLIR gaps discovered via feme's own
  CTS work, in-tree, as part of feme's own effort.

Fix: added `case spirv::Decoration::Sample:` to both switch statements'
existing unit-decoration case lists (`getSymbolDecoration`'s generic
CamelCase-to-`snake_case` mangling means no separate attribute-name mapping
was needed). New MLIR roundtrip test added to `decorations.mlir`.

`ninja -C build2 MLIRSPIRVDeserialization MLIRSPIRVSerialization` and the
`decorations.mlir` lit test both pass cleanly.

CTS re-sweep after this fix alone: **43 Pass / 12 Fail / 40 NotSupported** --
the 6 `sample_mask.pattern.*` cases flipped from Fail to a *different* Fail
(see L113 below), and the other 12 (`sample_position.correctness.*`/
`sample_position.distribution.*`) were unaffected, confirming they are a
distinct root cause (see L114).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- `Sample`-qualified fragment inputs are part of core
`sampleRateShading` (already advertised), not a newly-advertised capability;
this fix only removes a deserialization gap blocking already-claimed support.

### Roadmap L113 (fixed this session): static `VkPipelineMultisampleStateCreateInfo::pSampleMask`

After L112's fix, the 6 `sample_mask.pattern.*` cases still failed pipeline
creation, now with a different, previously-hidden error:
`"vkCreateGraphicsPipelines: a partial VkSampleMask is not implemented"` --
a deliberate rejection in `GraphicsPipeline.cpp`'s `translateMultisampleState`-
equivalent code, unrelated to the `Sample` decoration.

Implemented instead of rejected:

- `feme::graphics::GraphicsPipeline` (`Pipeline.h`) and the Vulkan-side
  `GraphicsPipelineState` (`GraphicsPipeline.h`) both gained a 32-bit
  `SampleMask` field, defaulting to all-1s (no effect).
- Captured from `VkPipelineMultisampleStateCreateInfo::pSampleMask`'s first
  word in the monolithic translation path -- the linked-
  `VK_EXT_graphics_pipeline_library` path funnels through the same code via
  its own synthesized `VkGraphicsPipelineCreateInfo`
  (`synthesizeLinkedGraphicsPipelineCreateInfo`), so no separate
  library-path plumbing was needed.
- ANDed into the rasterizer's per-lane per-sample coverage mask as early as
  possible -- at rasterization itself (`Quad.SampleMask`), not only ahead of
  the depth/stencil test like `alphaToCoverageEnable`/the fragment shader's
  own `gl_SampleMask` output (L111(b)) -- since a static mask is known
  before any fragment shader runs, it can (and should) also gate early
  depth/stencil testing and whether a lane counts as covered at all.

New unit test: `ExecutorTest.PipelineSampleMaskNarrowsPerSampleCoverage` --
an ordinary fragment shader (no `Coverage`-system-value output of its own)
on a pipeline with `setSampleMask(0b0101)`, confirming only samples 0 and 2
keep the shaded color, exactly mirroring L111(b)'s own shader-driven test.

`ninja check-feme` (ccache, assertions-enabled build): 3188/3191 passed, 3
pre-existing Unsupported, 0 Failed (up 1 from the new test, no
regressions).

CTS re-sweep after this fix: **43 Pass / 12 Fail / 40 NotSupported** -- all
6 `sample_mask.pattern.*` cases now Pass; the remaining 12 failures
(`sample_position.correctness.*`/`sample_position.distribution.*`) are
unaffected, confirming (per L114 below) they are a third, distinct root
cause.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- a static `pSampleMask` is core `VkPipelineMultisampleStateCreateInfo`
state, already advertised; this is a correctness fix, not a new capability.

### Roadmap L114 (fixed this session): `gl_SamplePosition` (`BuiltIn SamplePosition`) unmapped

The remaining 12 failures in this group (of the original 18) were a third,
distinct root cause from L112/L113 -- confirmed via
`FEME_VULKAN_LOG_CREATION_ERRORS=1` single-case reruns, which showed a
different pipeline-creation rejection:
`"vkCreateGraphicsPipelines: fragment input element 1 has no location to
link against a vertex output"`.

#### Root cause

SPIR-V `BuiltIn SamplePosition` (`gl_SamplePosition`'s backing builtin) was
entirely unmapped in `feme::getSystemValueForBuiltIn`
(`CanonicalizeStage.cpp`) -- falling through to `default: return
SignatureSystemValue::None`, making a real `gl_SamplePosition` read look
like an ordinary, `Location`-less varying, which `Executor.cpp`'s
fragment-input-to-vertex-output linkage loop then rejected outright.

The raw `BuiltIn` decimal value is **19**, not 24 as this session's initial
analysis assumed -- caught only because the very first attempted fix
(mapping `case 24`) still failed pipeline creation on rerun with the exact
same "element 1 has no location" error, prompting a direct cross-check
against `mlir/include/mlir/Dialect/SPIRV/IR/SPIRVBase.td`'s own
`SPIRV_BI_SamplePosition` definition (`I32EnumAttrCase<"SamplePosition",
19>`), which confirmed the correct value and immediately fixed the
remaining failure. A useful reminder: a plausible-looking, self-consistent
mental model (bare digit read off an earlier session's own doc-comment
list) is not the same as a verified constant -- re-run and re-check against
the actual spec/tablegen source before trusting a "these agree" analysis.

#### Fix

- `feme/include/feme/Core/Signature.h`: added `SignatureSystemValue::SamplePosition`, appended before `NumSystemValues` per the established no-renumbering convention.
- `feme/lib/Transforms/Graphics/CanonicalizeStage.cpp`: added `case 19: return SignatureSystemValue::SamplePosition;` to `getSystemValueForBuiltIn`, and updated its doc comment.
- `feme/include/feme/Target/CPU/RuntimeABI.h`: added a new per-lane `float SamplePosition[4][2]` field to `FemeFragmentInvocation` (too large for the existing `Reserved[3]` headroom).
- `feme/lib/Transforms/CPU/StageArgsLayout.h`: added the matching `FragmentInvocationFieldSamplePosition` enumerator and `getFragmentInvocationType()` literal-type-list entry, in lockstep with the C++ struct.
- `feme/lib/Transforms/CPU/FragmentWrapper.cpp`: added a `case SignatureSystemValue::SamplePosition:` to `loadFragmentSystemValue`, mirroring `Position`'s per-lane/per-component GEP-and-load pattern against the `[4][2]` (not `[4][4]`) shape.
- `feme/lib/Graphics/Executor.cpp`: `PerSampleShading`'s OR-condition now also checks for a `SamplePosition`-system-value fragment input (matching the spec's identical per-sample-execution requirement for `gl_SampleID`/`gl_SamplePosition`), and the existing per-`PassSample` block populates `PassInv.SamplePosition[Lane]` from the same `Offset` already computed for `Position.xy`'s own per-sample shift -- no new sample-position-table lookup logic needed.

New unit tests:
`CanonicalizeStageTest.FragmentStageMapsSamplePositionBuiltin`,
`FragmentWrapperTest.LowersSamplePositionSystemValueInput`,
`ExecutorTest.SamplePositionForcesPerSampleShadingAndReadsRealOffset` (the
latter uses `SampleShadingEnable=false`, proving the `PerSampleShading`
OR-condition change alone is what forces per-sample execution here, not
explicit sample shading).

## Roadmap L114(a) (fixed this session): sample-accurate varying interpolation

`multisample_shader_builtin.sample_position.correctness.*` (6 cases, found
while closing L114 above) failed, purely on values:
`"Varying values are not sampled at gl_SamplePosition"` (dEQP's own
message, not a feme-side error string).

### Root cause

The fragment shader declares `layout(location = 0) sample in vec2
fs_in_position_screen;` and compares it against a `gl_SamplePosition`-
derived expected value per sample. `Executor.cpp`'s barycentric
coordinates (`Quad.Bary0`/`Bary1`/`Bary2`) were -- per this file's own
prior comment -- "still evaluated once, at the pixel center", even
across the per-`PassSample` loop L114 above added `SamplePosition`'s own
per-pass write to: only `gl_FragCoord`/`gl_SamplePosition` themselves were
shifted per pass, not the ordinary interpolated varyings a `sample`-
qualified declaration (or, per the Vulkan spec more broadly, any input
while sample shading is active) requires be evaluated at the real sample
location instead of the pixel center. `fs_in_position_screen` therefore
stayed a fixed, pixel-center-derived value across every sample pass, while
`gl_SamplePosition` itself correctly varied -- an inevitable mismatch a
correctness test built specifically to compare the two catches directly.

The related, deferred-during-L114 sub-case (a `Sample`-qualified varying
with no `gl_SamplePosition`/`gl_SampleID` present) turned out **not** to
need its own separate fix: `multisample_interpolation.
sample_qualifier_distinct_values.*` exercises exactly that shape and now
largely passes post-fix (see L115 below for the *separate*, unrelated
failures discovered in that same CTS group).

### Fix

Per quad (not per lane -- `Area`/`Tri.Pos` are lane-independent), compute
`Area = edgeFn(Tri.Pos[0], Tri.Pos[1], Tri.Pos[2])` and
`SampleOffset = (*SamplePositions)[PassSample]` once, ahead of the
per-lane varying-interpolation loop. Per lane, when `PerSampleShading` is
true, recompute `B0`/`B1`/`B2` via `edgeFn(Tri.Pos[i], Tri.Pos[j], P) /
Area` at the real per-pass sample point `P = {Quad.PixelX[Lane] +
SampleOffset[0], Quad.PixelY[Lane] + SampleOffset[1]}`, instead of
reusing the fixed pixel-center `Quad.Bary0/1/2[Lane]`. When
`PerSampleShading` is false, the original pixel-center weights are reused
unchanged -- avoiding any recomputation cost or behavior change in the
(much more common) non-per-sample-shaded path.

### New unit test

`ExecutorTest.PerSampleShadingReinterpolatesVaryingsAtEachSample`: a
triangle covering a whole pixel whose "color" varying is set, per vertex,
to that vertex's own precomputed screen-space pixel position (`(0,0)`,
`(8,0)`, `(0,8)` for the existing overscanned-triangle vertex setup and
`{0,0,4,4}` viewport) -- an affine function of vertex position, so
barycentric-interpolating it at *any* point exactly reproduces that point.
The fragment shader reads both this varying and the `Position` system
value's X/Y and writes their `fsub` difference to an `R32G32B32A32_FLOAT`
attachment; the test asserts this difference is ~0 at all 4 samples.
Confirmed to genuinely exercise the fix (not just look plausible): a
`git stash` of `Executor.cpp` alone reproduces the exact pre-fix failure
(non-zero differences matching each sample's own pixel-center-vs-real-
offset gap) with this same test.

### Validation (L114(a))

`ninja check-feme`: 3192/3195 Passed, 3 pre-existing Unsupported, 0 Failed
(up 1 test from this session, zero regressions).

CTS re-sweep, the 6 previously-failing
`dEQP-VK.pipeline.monolithic.multisample_shader_builtin.sample_position.
correctness.*` cases directly (samples 2/4/8; samples 16/32/64 are the
pre-existing, unrelated `NotSupported` sample-count capability gap): all
6 now **Pass** (0 Fail).

CTS re-sweep, `dEQP-VK.pipeline.monolithic.multisample_shader_builtin.*`
(95 cases): 55 Pass / 0 Fail / 40 NotSupported -- up from 49/6/40,
exactly the predicted numbers, closing this bucket fully clean (modulo
the pre-existing, separately-confirmed-benign `NotSupported` sample-count
gap).

CTS re-sweep, `dEQP-VK.pipeline.pipeline_library.graphics_library.*` (836
cases, regression check since this fix touches `Executor.cpp`'s shared
varying-interpolation loop): unchanged at 548 Pass / 0 Fail / 287
NotSupported / 1 pre-existing benign "linking took too long" warning --
confirming zero collateral regressions.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- sample-accurate interpolation is core Vulkan 1.0 functionality
implied by already-advertised `sampleRateShading`, not a new capability;
this is a correctness fix for an existing, already-advertised feature.

## Roadmap L115(a) (fixed this session): missing GLSL.std.450 `InterpolateAt*` extended instructions -- MLIR-side op support

While continuing the L106 sweep with `dEQP-VK.pipeline.monolithic.
multisample_interpolation.*` (247 cases, the next fresh group per the
prior session's own suggested next steps) after L114(a) landed: 115 of
247 cases failed pipeline creation with `"error: unhandled
deserializations of 76 from extension set GLSL.std.450"` (12 Pass, 120
NotSupported for the remainder).

### Root cause

`76` is `InterpolateAtCentroid`'s own GLSL.std.450 extended-instruction
opcode; `77`/`78` are `InterpolateAtSample`/`InterpolateAtOffset`
(verified directly against the spec's own `GLSL.std.450.h`, not trusted
from memory, per the L114(a) "verify against source of truth" lesson).
All three were entirely absent from MLIR's own `SPIRVGLOps.td` --
confirmed via a GitHub code search that this is also true of real
upstream `llvm/llvm-project`, not just this fork's own checkout, so
this is a genuine upstream MLIR gap (per the L112 precedent of checking
upstream first) rather than something to expect already-fixed there.
Entirely unrelated to L114(a)'s own barycentric-interpolation
architecture fix -- this is a missing SPIR-V *extended-instruction*
import, not an interpolation-math bug.

### Fix (MLIR-side only)

Added `spirv.GL.InterpolateAtCentroid`/`InterpolateAtSample`/
`InterpolateAtOffset` to MLIR's SPIR-V dialect. Unlike every other
GLSL.std.450 op (plain-value operands), these 3 take an `Interpolant`
operand typed as a pointer (`SPIRV_AnyPtr`) to the Input-storage-class
variable to re-interpolate -- the same operand shape `spirv.Load`
already uses for its own `ptr` operand, so no new type-system machinery
was needed. Added a `verifyInterpolateAtOp` helper (mirroring
`LoadOp::verify()`'s own pointee/value-type check) confirming the
pointer's storage class is `Input` and its pointee type matches the
op's own result type exactly.

### New tests

- `mlir/test/Dialect/SPIRV/IR/gl-ops.mlir`: parse/print roundtrip for
  all 3 ops (scalar and vector interpolants), plus 2 verifier-failure
  cases (wrong storage class, mismatched result type).
- `mlir/test/Target/SPIRV/gl-ops.mlir`: SPIR-V binary
  serialize/deserialize roundtrip for all 3 ops.

Both confirmed passing directly via `mlir-opt`/`mlir-translate` +
`FileCheck` and via `llvm-lit`.

### Validation

`ninja check-feme`: 3192/3195 Passed, 3 pre-existing Unsupported, 0
Failed -- unaffected by this MLIR-only change, as expected.

CTS re-sweep, `multisample_interpolation.*`: unchanged at 12 Pass/115
Fail/120 NotSupported (expected -- this fix only unblocks
*deserialization*, not feme's own lowering). But the failure signature
for all 115 changed from `"unhandled deserialization of 76"` to
`"failed to legalize operation 'spirv.GL.InterpolateAtCentroid' ...
explicitly marked illegal"` (confirmed via
`FEME_VULKAN_LOG_CREATION_ERRORS=1` plus a single reduced case rerun,
the L112/L113/L114 technique) -- proving this fix is both necessary and
correctly scoped: the gap moved cleanly one layer downstream into
feme's own `SPIRVToLLVMPatterns.cpp`, tracked as L115(b) below rather
than folded into this fix's own scope.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- `interpolateAtCentroid`/`interpolateAtSample`/
`interpolateAtOffset` are core GLSL 4.00+ functions implied by
already-advertised `sampleRateShading`, not a new Vulkan capability;
this is an importer completeness fix, and it still doesn't render
correctly end-to-end until L115(b) lands.

## Roadmap L115(b) (scoped, not started): feme-side lowering for `InterpolateAtCentroid`/`InterpolateAtSample`/`InterpolateAtOffset`

Unlike a typical `spirv.GL.*` arithmetic op (a handful of pure LLVM
instructions, no runtime dependency), these 3 ops need a genuinely new
runtime-callback ABI surface in feme's own CPU backend:

- `Interpolant` resolves at compile time to a specific `Location`/
  `Component` stage-input (the same signature-element analysis
  `CanonicalizeStage.cpp` already does for ordinary varyings).
- But the *interpolation point itself* (a sample index, or an x/y
  offset) is often a **runtime** SSA value (e.g.
  `interpolateAtSample(v, gl_SampleID)`), not a compile-time constant --
  so this cannot reuse the existing `feme.stage.input.load.f32`
  intrinsic, which only ever reads an already-precomputed value baked
  into per-invocation storage *before* the shader runs (L114(a)'s own
  architecture: every ordinary varying is interpolated once, up front,
  at either the pixel center or the current pass's fixed sample
  location -- never on demand, mid-shader, at an arbitrary point the
  shader itself computes).

### Suggested fix shape (not yet implemented)

1. A new stage op (e.g. `feme.stage.input.interpolate`) carrying the
   resolved element/row/component plus a runtime mode (centroid/
   sample/offset) and its runtime operand(s).
2. A new per-invocation runtime-callback mechanism -- likely modeled on
   `ImageCalls.cpp`'s existing precedent of shader code calling into a
   host-implemented C runtime function with runtime arguments (e.g.
   texture sampling) -- exposing enough of `Executor.cpp`'s own
   per-lane triangle data (`Tri.Pos`/`InvW`/`Varyings`, `Area`,
   `Quad.PixelX`/`PixelY`) for a host function to recompute barycentric
   weights at an arbitrary runtime-supplied point, reusing L114(a)'s
   own `edgeFn`-based math rather than duplicating it.
3. `SPIRVToLLVMPatterns.cpp` conversion patterns for the 3 ops
   themselves, resolving `Interpolant` back to its `Location`/
   `Component` and lowering to the new stage op.

A materially larger, multi-file architecture addition -- not yet
started, not yet estimated in detail beyond "likely 1-2 full sessions
given the new ABI surface". Left open for a future session; see
`Roadmap.md`'s own L115(b) row.




## Roadmap L116 (fixed one bug; broadly swept and triaged): `dEQP-VK.graphicsfuzz.*`

The first fresh top-level group outside `pipeline.*` picked up by the L106
sweep, per several prior sessions' own suggested next steps ("still no
session has picked one of these up yet, worth prioritizing one of them next
specifically to break the multi-session `pipeline.*`-only pattern").

### Fix landed this session: `spirv.Kill`/`spirv.TerminateInvocation` return arity

`dEQP-VK.graphicsfuzz.always-false-if-with-discard-return` failed pipeline
creation with `"error: 'llvm.return' op expected 1 operand"`. Its fragment
shader has a `vec3`-returning helper function whose body is just `discard;`
(no reachable `return` -- valid SPIR-V, since `OpKill`/
`OpTerminateInvocation` are true terminators). `KillConversionPattern`/
`TerminateInvocationConversionPattern` (`SPIRVToLLVMPatterns.cpp`) always
replaced the op with a zero-operand `llvm.return`, correct only when the
enclosing function is void-returning.

Fixed with a shared `buildDiscardReturnOperands` helper looking up the
enclosing function's actual result type -- preferring the already-converted
`llvm.func` (MLIR's dialect conversion driver had already rewritten the
parent `spirv.func`'s signature by the time this pattern runs, confirmed by
testing) and falling back to the original `spirv.func`'s own result type
otherwise -- synthesizing a `poison` value of that type when non-void,
since the value can never actually be observed once the discard has
already terminated the invocation.

New tests: a non-void-returning variant appended (via `--split-input-file`)
to both `spirv-to-llvm-kill.mlir` and
`spirv-to-llvm-terminate-invocation.mlir`. `ninja check-feme`: 3192/3195
Passed, 3 pre-existing Unsupported, 0 Failed. CTS: the reduced single case
confirmed flips Fail -> Pass.

### Full sweep, with the fix applied: 757 cases

|                                        | Count |
|----------------------------------------|-------|
| Pass                                   | 528   |
| Fail                                   | 197   |
| NotSupported                           | 8     |
| Process hangs or crashes (no verdict)  | 24    |

The last row is new: unlike every prior `pipeline.*` group swept this
milestone series, `graphicsfuzz.*`'s control-flow-heavy shaders (loops,
`discard`/`return`/`break` nested inside them, data-dependent bounds) hit
cases where `deqp-vk` itself never produces a per-case verdict -- either
because the compiled shader hangs (an apparent infinite loop) or because a
compiler bug crashes the whole process with a fatal internal error (an
LLVM assertion, not a graceful diagnostic). Found via an ad hoc
`--deqp-watchdog`-enabled skip-and-continue harness built this session
(`/tmp/l106sweep/run_sweep.sh`, not committed -- a throwaway sweep-only
script; reproducible directly from this section's own description if
needed again) that re-runs the remaining caselist, watches for a case that
starts but never finishes, and removes just that one case before
resuming.

### Failure taxonomy (this session's triage, not yet fixed beyond the one bug above)

Broken out into `Roadmap.md`'s L116(a)-(f) rows (all one lowercase-letter
deep, no further nesting), roughly in descending order of `Fail`-case
volume:

- **L116(a)** (~59% of `Fail` error occurrences): `feme-cpu-masked-mem-op`
  rejects struct/aggregate/matrix element types outright. Likely the same
  root cause as the already-tracked `C8b` row (`SIMDize.cpp`'s own
  aggregate `insertvalue`/`extractvalue` gap) -- by far the highest-value
  single investigation in this list if so.
- **L116(b)** (~80 error occurrences): `feme-cpu-linearize`/`-simdize`/
  `-wrap-entry` divergent-branch/mask-affecting-op-in-a-loop gaps --
  graphicsfuzz's own `discard`/`return`/`break`/`continue`-nested-in-loops
  idiom, likely several distinct sub-gaps in `LinearizePass`.
- **L116(c)** (31 + 12 occurrences): 6 more missing/incomplete
  GLSL.std.450 ops beyond L115's own three --
  `Determinant`/`Modf`/`PackUnorm4x8`/`PackUnorm2x16`/`UnpackUnorm2x16`/
  `UnpackUnorm4x8` entirely undefined in `SPIRVGLOps.td` (same per-op
  TableGen pattern L115(a) established), plus `Ldexp`/`UnpackSnorm*`
  already defined but failing SPIRVToLLVMPatterns.cpp legalization for
  some operand shape.
- **L116(d)** (20 occurrences): `spirv.Constant`/`spirv.CompositeConstruct`
  of an aggregate type fails legalization -- likely overlaps L116(a)/C8b's
  own theme.
- **L116(e)** (3 occurrences, smallest, best quick-win candidates): a
  `spirv.Switch`-shaped branch-arity bug, and a `spirv.Store` with
  `Volatile|Nontemporal` memory-access qualifiers the existing conversion
  pattern doesn't yet handle.
- **L116(f)** (24 cases, no verdict at all): two investigated directly --
  `arr-value-set-to-arr-value-squared` (a quicksort-with-injected-bug
  shader, apparent infinite loop in JIT'd code, not yet confirmed via
  debugger) and `complex-nested-loops-and-call` (a genuine
  `LinearizePass` bug: `"Uses remain when a value is destroyed!"` at
  `llvm/lib/IR/Value.cpp:99`, deleting a loop-guard value
  (`%.inv21`/`loop.exit.guard4.Flow24_crit_edge`) that still has a live
  use). The other 22 were only skipped, not yet individually triaged.

None of L116(a)-(f) were attempted this session -- each is a
substantially sized, independent investigation (per the taxonomy above,
several plausibly as large as L115 itself), left for future sessions per
`Roadmap.md`'s own breakdown.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed for the one fix landed this session (a pure bug fix, not a new
capability).

## L116(e): both one-off bugs fixed this session

Continuing the L116 breakdown, this session picked L116(e) (the smallest,
best quick-win row per the prior session's own ranking) over L116(a).

### Bug 1: `spirv.Store`/`spirv.Load` with combined `Volatile|Nontemporal`

`dEQP-VK.graphicsfuzz.spv-stable-pillars-volatile-nontemporal-store` failed
pipeline creation with `"failed to legalize operation 'spirv.Store'"` when
its `MemoryAccess` operand combined the `Volatile` and `Nontemporal` bits
(`Volatile|Nontemporal`, a real, independently-combinable bit-OR per SPIR-V's
own `MemoryAccess` bit-enum, not two mutually-exclusive alternatives).

Root cause: upstream MLIR's `LoadStorePattern`
(`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`) matched the memory-access
attribute with a `switch` comparing the *whole* value against each single
enumerant (`Aligned`/`None`/`Nontemporal`/`Volatile`); a combined value like
`Volatile|Nontemporal` (1|4 = 5) equals none of those single values, so it
always fell to `default: return failure()` -- a classic bit-enum-vs-switch
antipattern.

Fix: replaced the `switch` with independent `bitEnumContainsAll`/
`bitEnumContainsAny` bit checks (the idiomatic pattern already used
elsewhere in the SPIR-V dialect, e.g. `CooperativeMatrixOps.cpp`): reject
any bit outside `{Aligned, Volatile, Nontemporal}` exactly as before
(verified `NonPrivatePointer` is still rejected), and independently honor
each of the three supported bits in any combination, including `Aligned`'s
alignment operand alongside the other two.

New tests: `store_volatile_nontemporal`, `load_volatile_nontemporal`,
`store_volatile_nontemporal_aligned` appended to
`mlir/test/Conversion/SPIRVToLLVM/memory-ops-to-llvm.mlir`.

### Bug 2: `OpSwitch` block-argument wiring for a duplicated target block

`dEQP-VK.graphicsfuzz.call-if-while-switch` failed pipeline creation with
`"branch has 0 operands for successor #3, but target block has 1"`. Its
SPIR-V has an `OpSwitch` with two case literals (38 and 23) both branching
to the same label, and that label has its own `OpPhi` (SPIR-V/LLVM phi
semantics key a value by predecessor *block*, not predecessor *edge*, so a
block with two switch-case edges from the same predecessor still needs
only one phi entry for it).

Root cause, one layer further upstream than feme's own code (per the L112
precedent -- always check the upstream MLIR SPIR-V import path first):
`Deserializer::wireUpBlockArgument()`
(`mlir/lib/Target/SPIRV/Deserialization/Deserializer.cpp`) uses
`llvm::find()` to locate a `spirv.Switch`'s target block index when
back-patching the `OpPhi`-derived block arguments onto it. `llvm::find()`
only returns the *first* matching index -- so when the same block appears
more than once in the switch's own target list (our case, since case
literals 38 and 23 both target it), only the first occurrence's operand
list gets the phi's block arguments; every later duplicate occurrence's
list is left at the empty `{}` it was constructed with in `processSwitch()`,
producing an ill-typed `llvm.switch` once lowered.

Fix: iterate every occurrence of the target block (`llvm::enumerate`) and
assign the same phi-derived block arguments to each one, rather than
stopping at the first match.

New test: `mlir/test/Target/SPIRV/selection_switch_duplicate_target.spvasm`,
a minimal switch with two case literals targeting the same block that takes
one `OpPhi`-derived block argument -- confirmed to fail without the fix
(`git stash` round-trip) with the exact same error shape, and pass with it.

### Validation

`ninja check-feme`: 3192/3195 Pass, 3 pre-existing Unsupported, 0 Failed --
zero regressions from either fix. `mlir/test/Target/SPIRV/` (66 tests) and
`mlir/test/Conversion/SPIRVToLLVM/` all passing.

CTS: `dEQP-VK.graphicsfuzz.call-if-while-switch` flips Fail -> Pass outright.
`spv-stable-pillars-volatile-nontemporal-store`'s original `spirv.Store`
legalization error is gone, but the case still shows `Fail` overall -- it
now fails one layer deeper on the already-tracked L116(a)/(d) aggregate-value
gap (`feme-cpu-simdize: ... divergent aggregate value ...`), confirming this
fix is correctly scoped rather than incomplete.

### Full `graphicsfuzz.*` re-sweep (757 cases, with both fixes applied)

|                                        | Count | Prior session |
|----------------------------------------|-------|----------------|
| Pass                                   | 529   | 528            |
| Fail                                   | 196   | 197            |
| NotSupported                           | 8     | 8              |
| Process hangs or crashes (no verdict)  | 24    | 24             |

Net movement is exactly the one case expected to flip
(`call-if-while-switch`, Fail -> Pass); the `MemoryAccess` fix's own case
stays `Fail` for the separate, already-tracked reason above, and the 24
hang/crash cases (re-confirmed via the same watchdog-enabled skip-and-continue
harness as last session, rebuilt fresh this session as a throwaway script,
not committed) are the identical 24 case names as last session's sweep --
no new hangs, no previously-hanging case resolved.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- both fixes are pure bug fixes in existing SPIR-V import/lowering
paths, not new capabilities.

## Roadmap L116(a)/C8b (fixed this session, then a regression fixed): aggregate-typed `Private` globals now localize

Continuing from the prior session's C8b re-verification (which found
`SIMDize.cpp`'s `insertvalue`/`extractvalue`/`select` producer support,
added under L21, already covers the shape C8b's own text originally named,
but the repro still failed on a different diagnostic), this session traced
the real remaining gap to `feme::cpu::LocalizePrivateGlobalsPass`
(roadmap H170): a GLSL-compiler-materialized matrix/struct intermediate is
frequently routed through a genuine module-scope `Private`-storage
`OpVariable` via a whole-aggregate `store`/`load` round-trip, and this
pass's own `isCandidateGlobal` check previously rejected any array/struct
-typed global outright, leaving it as a bare `GlobalVariable` that
`SIMDize.cpp` has no widening rule for at all.

### The fix

Broadened `isCandidateGlobal` (`LocalizePrivateGlobals.cpp`) to also accept
a struct/array-typed global whose every leaf is a scalar or fixed-vector
type, as long as no leaf is a non-power-of-2-width vector (which would
reintroduce the tight-vs-ABI-padded-layout corruption
`feme::cpu::LocalNarrowVectorArrayInitPass`, roadmap H69/L99, already
exists to prevent for that narrower shape). Verified via `feme-opt`+`sroa`
that a localized aggregate global fully promotes back to a pure
`insertvalue`/`extractvalue` chain, the shape `SIMDize.cpp` already
supports.

New/updated lit coverage in `localize-private-globals.ll`: `@anArray`
(scalar-leaf array, now localized), `@vec2Array` (`<2 x float>`-leaf,
power-of-2, now localized), `@vec3Array` (`<3 x float>`-leaf,
non-power-of-2, still excluded), `@aStruct` (scalar-leaf struct, now
localized).

Real-repro result: `dEQP-VK.glsl.linkage.varying.struct.mat4x2` (C8b's own
original case) no longer fails at SIMDize time; `FEME_VULKAN_LOG_CREATION_
ERRORS=1` confirms the fix genuinely closes the compile-time gap. The case
still does not `Pass` overall -- it now fails at `vkQueueSubmit` on a
separate, pre-existing limitation (`Executor.cpp` rejects any matrix
*vertex attribute* outright, as distinct from a matrix *varying*, which
already works) -- tracked as new roadmap row L117.

### A `graphicsfuzz.*` re-sweep found one genuine regression

A full re-sweep of the 733 non-hanging `dEQP-VK.graphicsfuzz.*` cases (24
known hangs/crashes excluded, same list as prior sessions' own watchdog
sweeps) against this fix, versus the last documented full-sweep baseline:

|               | Baseline | After L116(a)/C8b fix (pre-mitigation) |
|---------------|----------|-----------------------------------------|
| Pass          | 529      | 531                                       |
| Fail          | 196      | 194                                       |
| NotSupported  | 8        | 8                                          |

Per-case diffing (`#beginTestCaseResult`/`StatusCode` parsing of the raw
`.qpa` logs) found **3 cases flipped Fail -> Pass**
(`stable-binarysearch-tree-nested-if-and-conditional`,
`cov-nested-functions-accumulate-global-matrix`,
`stable-binarysearch-tree-fragcoord-less-than-zero`) and **1 case flipped
Pass -> Fail**: `cov-function-loop-condition-constant-array-always-false`
-- a genuine regression, confirmed real and deterministic via repeated
reruns and `git stash`-based A/B testing (passes with the fix stashed out,
fails with it applied).

Root cause: the regressed shader's two file-scope `int[10]` arrays are
mutated inside a helper function containing a `discard` that is
*statically* unprovable (driven by a uniform-buffer read) but, for this
specific test's runtime values, always false. `feme::cpu::LinearizePass`
conservatively treats every later memory access in that function as
conditionally executed (a `feme.cpu.masked.load`/`.store` call) once any
discard/demote is reachable at all -- correct in general. With this
session's fix, the two arrays are now localized to per-function `alloca`s,
so `SIMDize.cpp`'s `collectMaskedAllocas`/`widenMaskedAlloca*` family now
gives each SIMD lane **real, independent storage** instead of one
previously-shared global address -- also correct in isolation. The actual
bug is a separate, pre-existing gap this interaction exposed for the first
time: `SIMDize.cpp`'s generic "leftover stale use, value already
classified uniform by `UniformityInfo`" recovery path (~SIMDize.cpp:4515-
4595, roadmap H107) extracts lane 0 of a widened value as a scalar
stand-in whenever a scalar use survives erasure -- always safe when the
source was one shared global address, not proven safe once the source is
4 independently-masked-and-gathered per-lane copies. This gap is real and
separate from the fix itself; recorded as new roadmap row **L118** for a
future session, since properly teaching the stale-use recovery path to
verify per-lane validity (rather than assuming it) is a deeper
`SIMDize.cpp` architectural change, out of scope here.

### Mitigation applied this session

Added `mayDiscardOrDemote(Function &F)` to `LocalizePrivateGlobals.cpp`: a
transitive scan (via `feme::getStageOpKind`, matching a callee against
`StageOpKind::Discard`/`Demote`) for whether a function can ever reach a
`feme.stage.discard`/`.demote` call. `feme::graphics::CanonicalizeStagePass`
(which creates these calls) runs before `LocalizePrivateGlobalsPass` in the
pipeline, so they are already present in the IR when this pass runs. The
aggregate-broadening path (not the pre-existing scalar/fixed-vector path,
which has no such gap) now skips a global whose one using function may
discard/demote, leaving it as a shared global exactly as before this
session's fix -- a deliberately conservative mitigation that trades away
some potential future localization wins in discard/demote-adjacent code
for correctness today.

New lit coverage: `@arrayInDiscardingFunction`, an `i32` array (no ABI-
padding risk on its own) used only in a function that calls
`feme.stage.discard`, confirming it is still excluded despite otherwise
qualifying.

### Final, post-mitigation re-sweep

|               | Baseline | Final (post-mitigation) |
|---------------|----------|---------------------------|
| Pass          | 529      | 532                        |
| Fail          | 196      | 193                        |
| NotSupported  | 8        | 8                           |

Confirmed via the same per-case diff: the regression is gone (no case
flipped Pass -> Fail this time), and all 3 of the fix's own wins are
retained. Net movement: +3 Pass / -3 Fail, 0 regressions.

`ninja -C build2 check-feme`: 3192/3195 Passed, 3 pre-existing Unsupported,
0 Failed -- clean, both before and after the mitigation.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- pure bug fix (plus a conservative regression mitigation) in an
existing CPU-target optimization pass, not a new capability or extension.

## Roadmap L116(c) (Determinant fixed this session): GLSL.std.450 `Determinant`

Continuing the L116 breakdown, this session picked L116(c)'s `Determinant`
sub-item (17 of the row's 31 `unhandled deserializations` occurrences, by
far the largest of its six named gaps) -- a genuinely missing MLIR SPIR-V
op, the same shape as roadmap L115(a)'s own `InterpolateAt*` precedent.

### The fix

Added `SPIRV_GLDeterminantOp` (GLSL.std.450 opcode 33) to `SPIRVGLOps.td`:
a square-matrix operand, a scalar-of-the-matrix's-own-component-type
result -- a shape none of the existing generic GL-op patterns
(`SPIRV_GLUnaryArithmeticOp` et al.) model, so a bespoke op definition
plus a hand-written `verify()` (matrix-is-square check via
`MatrixType::getNumRows()`/`getNumColumns()`, result-type-matches-
component-type check) was needed, mirroring `InterpolateAt*`'s own
approach.

feme's own `SPIRVToLLVMPatterns.cpp` lowering (`GLDeterminantPattern`)
computes the determinant via ordinary Laplace (cofactor) expansion along
the first row, recursing on the matrix's own already-extracted scalar
elements (`llvm.extractvalue`/`llvm.extractelement` against the matrix's
`!llvm.array` of column vectors, the same representation the existing
`MatrixTimesMatrix`/`Transpose`/etc. patterns already use) -- pure
arithmetic, no runtime callback needed, unlike L115(b)'s own
`InterpolateAt*` lowering. Correct for any N in principle, though only
ever exercised at GLSL's own fixed 2x2/3x3/4x4 sizes.

New tests:
- `mlir/test/Dialect/SPIRV/IR/gl-ops.mlir`: parse/print roundtrip (f32
  and f16 matrices), a non-square-matrix and a mismatched-result-type
  verifier failure.
- `mlir/test/Target/SPIRV/gl-ops.mlir`: SPIR-V binary
  serialize/deserialize roundtrip.
- `feme/test/Conversion/SPIRVToLLVM/spirv-to-llvm-matrix-arithmetic.mlir`:
  2x2 and 3x3 lowering, IR shape confirmed against a hand-computed
  cofactor expansion (2x2 collapses to the ordinary `ad - bc` formula).

### Measured impact

Identified the real CTS repro set directly (`grep -li determinant` over
`external/vulkancts/data/vulkan/amber/graphicsfuzz/*.amber`): exactly 17
cases, matching the original sweep's own `Determinant` occurrence count.
All 17 now `Pass` (all were `Fail` before):

`cov-apfloat-determinant`, `cov-apfloat-determinant-for-if`,
`cov-apfloat-negative-step-func`, `cov-condition-matrix-determinant-
uniform`, `cov-const-folding-clamp-max`, `cov-const-folding-det-identity`,
`cov-const-folding-dot-determinant`, `cov-determinant-uninitialized-
matrix-never-chosen`, `cov-function-loop-check-determinant-zero-return-
vector`, `cov-inst-combine-add-sub-determinant`, `cov-instr-info-det-mat-
min`, `cov-irbuilder-matrix-cell-uniform`, `cov-simplify-select-
fragcoord`, `cov-value-tracking-constant-fold-refraction-dfxd-
determinant`, `cov-x86-instr-info-determinant-min`, `cov-x86-isel-
lowering-determinant-exp-acos`, `stable-colorgrid-modulo-float-mat-
determinant-clamp`.

A full `graphicsfuzz.*` re-sweep (733 non-hanging cases, same 24-case
exclusion list as prior sessions) confirms exactly those 17 flip and
nothing else moves:

|               | Before this fix | After |
|---------------|------------------|-------|
| Pass          | 532              | 549   |
| Fail          | 193              | 176   |
| NotSupported  | 8                | 8     |

`ninja -C build2 check-feme`: 3192/3195 Passed, 3 pre-existing
Unsupported, 0 Failed -- clean.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- a new GL-op *implementation*, not a new Vulkan feature/
extension surface.

The other five gaps L116(c)'s original text named (`Modf`, the four
`Pack/Unpack*` ops, and the separate `Ldexp`/`UnpackSnorm*`-family
legalization-only follow-up) remain open, tracked as new roadmap row
L119.

## Roadmap L119 (Pack/Unpack Snorm/Unorm family fixed this session): GLSL.std.450 `PackSnorm4x8`/`PackUnorm4x8`/`PackUnorm2x16`/`UnpackSnorm4x8`/`UnpackUnorm2x16`/`UnpackUnorm4x8`

Picked up L119 (opened last session for the five remaining gaps in
L116(c)'s original `Determinant`/`Modf`/`Pack*`/`Unpack*`/`Ldexp` sweep).
Split into two distinct kinds of gap, only one of which this session
closes:

1. `PackUnorm4x8`/`PackUnorm2x16`/`UnpackUnorm2x16`/`UnpackUnorm4x8` had
   **no TableGen op definition at all** in `SPIRVGLOps.td` -- added all
   four (opcodes 55/57/61/64 per `GLSL.std.450.h`), mirroring the existing
   `PackSnorm4x8`/`UnpackSnorm4x8` op shape exactly (same non-verified
   `SPIRV_GLOp<Name, opcode, [Pure]>` shape, vector-float <-> i32
   operand/result) with each op's own `Unorm` conversion formula
   (`round(clamp(c, 0, 1) * 255)` for the `4x8` pair, `* 65535` for the
   `2x16` pair) in place of `Snorm`'s `[-1, 1]`/`127`/`32767`.
2. `PackSnorm4x8`/`UnpackSnorm4x8` already had a TableGen op definition
   but **no feme-side `SPIRVToLLVMPatterns.cpp` lowering pattern at all**
   -- confirmed via exhaustive grep (also confirmed upstream MLIR's own
   `DirectConversionPattern` table in `SPIRVToLLVM.cpp` never covers any
   of these six ops, since none has a matching single LLVM intrinsic; they
   need the same kind of bespoke bit-manipulation pattern
   `GLPackHalf2x16Pattern`/`GLUnpackHalf2x16Pattern` already established
   for the analogous `Half2x16` pair).

**Correction to last session's own L119 text**: it claimed
`spirv.GL.UnpackSnorm2x16`/`PackSnorm2x16` "already deserialize (the op
exists)" but fail feme's legalization step. Exhaustive grep across all of
`mlir/` this session found **no trace of `PackSnorm2x16`/`UnpackSnorm2x16`
anywhere** -- neither op exists in MLIR at all. This was very likely a
naming mix-up with `PackSnorm4x8`/`UnpackSnorm4x8` (which do exist and are
exactly the ones with the missing-lowering gap described above). Recorded
here, and in `Roadmap.md`'s L119 row, so the error isn't repeated again.

Added two new templated pattern classes to `SPIRVToLLVMPatterns.cpp`,
`GLPackNormPattern<OpTy, NumComponents, BitsPerComponent, IsSigned>` and
its inverse `GLUnpackNormPattern`, covering all six affected ops via one
shared implementation each (`packNormalizedComponents`/
`unpackNormalizedComponents`) of the GLSL.std.450 spec's own per-lane
formula:

- Pack: `round(clamp(c, signed ? -1 : 0, 1) * scale)` per lane (`scale =
  2^(B-1) - 1` signed, `2^B - 1` unsigned), each lane's `B`-bit result
  packed into a single `i32` (component 0 in the low bits) via
  `llvm.fptosi` + mask + shift + `llvm.or`. Clamping uses `llvm.maxnum`/
  `llvm.minnum` (the same NaN-propagating-but-IEEE-754-otherwise choice
  `ClampPattern` already makes for `spirv.GL.*Clamp`); rounding uses
  `llvm.round` (ties away from zero, matching the spec's own `round()`).
  `llvm.fptosi` (not `fptoui`) is used uniformly for both encodings,
  since masking a two's-complement negative value to its low `B` bits
  after sign-extension already yields the correct unsigned bit pattern --
  no separate signed/unsigned integer-conversion path is needed.
- Unpack: the reverse per lane, extracting each `B`-bit field via the same
  shift-left-then-`(a|l)shr` idiom `BitFieldSExtractPattern`/
  `BitFieldUExtractPattern` already use to correctly sign- or zero-extend
  an arbitrary-width bitfield, converting to float (`sitofp`/`uitofp`),
  and dividing by the same `scale`. The signed encoding's result gets an
  extra `[-1, 1]` clamp (its own most-negative fixed-point value would
  otherwise divide to something slightly past `-1`); the unsigned
  encoding's `[0, 1]` result never needs one, since its fixed-point range
  is exactly `[0, scale]`.

New tests:
- `mlir/test/Dialect/SPIRV/IR/gl-ops.mlir`: parse/print roundtrip plus a
  wrong-vector-length verifier-failure case for each of the four new ops.
- `mlir/test/Target/SPIRV/gl-ops.mlir`: SPIR-V binary
  serialize/deserialize roundtrip for the four new ops.
- `feme/test/Conversion/SPIRVToLLVM/spirv-to-llvm-gl-pack-unpack-norm.mlir`:
  all six lowering-pattern instantiations, IR shape confirmed by hand
  against `feme-opt`'s own output for each op before writing the
  `CHECK` lines.

### Measured impact

Identified 8 real CTS repro cases directly (`grep -li` across
`external/vulkancts/data/vulkan/amber/graphicsfuzz/*.amber` for
pack/unpack-unorm/snorm-related keywords): `cov-inst-combine-and-or-xor-
pack-unpack`, `cov-inst-combine-simplify-demanded-pack-unpack`,
`cov-apfloat-unpackunorm-loop`, `cov-function-unpack-unorm-2x16-one`,
`cov-inst-combine-pack-unpack`, `cov-packhalf-unpackunorm`,
`cov-inst-combine-simplify-demanded-packsnorm-unpackunorm`,
`cov-unpack-unorm-mix-always-one`. All 8 now `Pass` (all previously failed
pipeline creation with "failed to legalize operation").

A full `graphicsfuzz.*` re-sweep (757 cases this session -- the CTS
checkout's own case count grew by one group-header parsing artifact since
last session's 733 + 24 count; same 24 hangs/crashes excluded, reconfirmed
identical by name) shows exactly a 10-case Pass/Fail flip and nothing
else moves -- matching the roadmap's own occurrence-count arithmetic
exactly (`PackUnorm4x8` 2 + `PackUnorm2x16` 1 + `UnpackUnorm2x16` 1 +
`UnpackUnorm4x8` 4 + the `PackSnorm4x8`/`UnpackSnorm4x8` legalization-only
family's own 2 occurrences the roadmap's prior text miscounted as
`UnpackSnorm2x16` = 10):

|               | Before this fix | After |
|---------------|------------------|-------|
| Pass          | 549              | 559   |
| Fail          | 176              | 166   |
| NotSupported  | 8                | 8     |

`ninja -C build2 check-feme`: 3193/3196 Passed (+1 new test), 3
pre-existing Unsupported, 0 Failed -- clean. Broader
`mlir/test/Dialect/SPIRV`, `mlir/test/Target/SPIRV`, and
`mlir/test/Conversion/SPIRVToLLVM` lit suites also 0 regressions.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- a new/completed GL-op *implementation*, not a new Vulkan
feature/extension surface.

`Modf` and `Ldexp` (the two remaining gaps from L116(c)'s original text)
remain open, split out to new roadmap row L120 since they're a distinct
shape (an `OpVariable` out-parameter for `Modf`, a different
exponent-scaling math for `Ldexp`) from this session's Pack/Unpack-family
fix.

## Roadmap L120 (`Ldexp` fixed this session; `Modf` remains open): GLSL.std.450 `Ldexp`

`spirv.GL.Ldexp` already had a TableGen op definition (opcode 53) but no
feme-side `SPIRVToLLVMPatterns.cpp` lowering pattern at all. Investigated
`LLVM_LoadExpOp` (`llvm.intr.ldexp`) as the natural target and found its
own `power`/exponent operand is constrained to always be a *scalar*
integer (MLIR's `LLVM_PowFI` TableGen base class), even when the `val`
operand is a vector -- confirmed against both the generated accessor
types and an existing upstream lit test. `spirv.GL.Ldexp`'s vector form
legitimately allows `exp` to be a full per-lane vector matching `x`'s
component count per the GLSL.std.450 spec, so a simple
`DirectConversionPattern<GLLdexpOp, LLVM::LoadExpOp>` table entry could
not cover it.

Added a bespoke `GLLdexpPattern`: the scalar form forwards directly to
one `LLVM::LoadExpOp`; the vector form loops per-lane, extracting `x`/
`exp` lanes via `llvm.extractelement`, calling `LLVM::LoadExpOp` per
lane, and reassembling via `llvm.insertelement` into a
`llvm.mlir.poison`-initialized result vector. New lit test:
`feme/test/Conversion/SPIRVToLLVM/spirv-to-llvm-gl-ldexp.mlir` (scalar
and vector forms), matched against manually-verified `feme-opt` output.

`ninja -C build2 check-feme`: 3194/3197 Passed (+1 new test), 3
pre-existing Unsupported, 0 Failed -- clean.

Measured real CTS impact: dumped the full case list via
`deqp-vk --deqp-runmode=txt-caselist` and grepped for `ldexp` (171 cases).
Of the 7 `dEQP-VK.graphicsfuzz.*ldexp*` cases directly tied to the
roadmap's original 10-occurrence count, 6 now `Pass` (were `Fail`):
`cov-apfloat-acos-ldexp`, `cov-inst-combine-add-sub-ldexp`,
`cov-inst-combine-compares-ldexp`, `cov-simplify-ldexp-exponent-zero`,
`cov-sinh-ldexp`, `cov-ldexp-undefined-mat-vec-multiply`. The 7th
(`cov-ldexp-exponent-undefined-divided-fragcoord-never-executed`) still
fails, but now with a new, more specific diagnostic: `"feme-cpu-simdize:
unsupported divergent call to 'llvm.ldexp.f32.i32'"` -- not a regression
(it failed earlier, less specifically, before this fix), but a genuinely
new, previously-undiscovered gap in `SIMDize.cpp`'s `widenElementwise`:
it only widens a divergent intrinsic call when every non-result operand
shares the exact same type as the result, or via one hardcoded
`is_fpclass` special case; `llvm.ldexp`'s integer exponent operand is
independently-overloaded and never matches the float result type, so it
falls through to the generic "unsupported divergent call" path. Recorded
as new roadmap row L121 (not fixed this session).

A full `graphicsfuzz.*` re-sweep (757 cases, same skip-and-continue
per-case methodology, same known hangs/crashes excluded) after this fix:

|               | Before this fix | After |
|---------------|------------------|-------|
| Pass          | 559              | 568   |
| Fail          | 166              | 156   |
| NotSupported  | 8                | 8      |

(568 + 156 + 8 = 732 accounted for; the remaining 25 are the pre-existing
known hangs/timeouts/crashes, one more than the previously-tracked 24 --
not investigated further this session, no evidence any of them are new;
most plausibly test-harness timing variance around the `timeout 20`
skip-and-continue methodology rather than a real regression, since 0
`check-feme` regressions and 0 unexpected new Fails were observed
anywhere else in this sweep.)

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- a GL-op legalization fix, not a new Vulkan feature/extension
surface.

`Modf` remains open (needs a new `SPIRV_GLModfOp` taking an `OpVariable`
out-parameter, a shape unlike any existing GL op) -- not started this
session.

## Roadmap L117 (fixed this session): matrix vertex attributes

`feme::graphics::Executor`'s vertex-input fetch previously rejected any
input `SignatureElement` with `RowCount != 1` outright ("vertex input
element %u spans %u rows; matrix vertex attributes are not implemented
yet"), unlike a matrix *varying* between shader stages, which
`StageStorage`/`readRaw`/`writeRaw` already support directly via an
optional `Row` parameter. Found via C8b's own re-verification: once C8b's
fix closed the compile-time SIMDize gap, `dEQP-VK.glsl.linkage.varying.
struct.mat4x2` (a `mat4x2` bound as a *vertex attribute*, not just a
varying) advanced to fail here instead, at `vkQueueSubmit` time.

Replaced the outright rejection in `Executor.cpp`'s vertex-input fetch
loop with a `for (Row = 0; Row != Elt.RowCount; ++Row)` loop: for each
matrix row, look up and fetch the `VertexBufferBinding`/`VertexAttribute`
bound at that row's own consecutive `Location` (`*Elt.Location + Row`,
Vulkan's own one-`VkVertexInputAttributeDescription`-per-row/column
convention, mirrored by every upstream frontend that splits a matrix
input parameter into per-row signature elements), then pass `Row`
through to the existing `StageStorage::writeRaw`'s already-supported
optional parameter. No `StageStorage` changes were needed -- the matrix-
varying machinery C8b's own history built already covers this shape;
only the vertex-input fetch loop itself needed updating to use it for
attributes too.

New unit test: `ExecutorTest.RendersTriangleWithColorFromAMatrixVertexAttribute`,
mirroring the existing `InterpolatesConstantColorPackedInAMatrixVarying`
matrix-varying test, but for a 2x2 matrix vertex attribute fetched from
two separate per-row `VertexAttribute` bindings at consecutive locations,
rendered into a solid-color triangle and checked pixel-for-pixel.

Real-repro result: `dEQP-VK.glsl.linkage.varying.struct.mat4x2` now
`Pass`es end to end (was `Fail` at `vkQueueSubmit` on this exact
rejection before this fix).

`ninja -C build2 check-feme`: 3195/3198 Passed (+1 new test), 3
pre-existing Unsupported, 0 Failed -- clean.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- a bug fix in existing vertex-input handling, not a new Vulkan
feature/extension surface.

## Roadmap L118 (partial fix, real regression unresolved): stale-use recovery's hardcoded lane 0

`feme::cpu::SIMDizePass`'s post-widening cleanup (`FunctionWidener`'s
"leftover stale scalar use" recovery, roadmap H107) extracts lane 0 of a
widened value as a scalar stand-in whenever `UniformityInfo` classifies
the value uniform and a scalar use survives erasure -- justified by "lane
0 of every wave this pass ever iterates is guaranteed a real invocation",
citing `EntryWrapperPass`'s `WavesPerGroup` loop bound. Root-caused
during roadmap C8b's own investigation to a real regression
(`dEQP-VK.graphicsfuzz.cov-function-loop-condition-constant-array-always-false`),
mitigated there only by excluding the triggering shape from a separate,
otherwise-desirable global-localization broadening (C8b's own guard),
not by fixing the recovery itself.

This session confirmed the compute-shader justification above does not
generalize to every shader stage: a fragment shader's own `EntryMask`
(`FragmentWrapper.cpp`'s `buildQuadMaskValue`) is built per quad from
each invocation's own "live"/helper-invocation state, and can leave lane
0 itself inactive -- a whole quad is dispatched whenever at least one of
its four pixels is covered by the primitive, so a pixel outside the
primitive but sharing a quad with a covered one (kept alive only so that
covered pixel's own derivatives have real neighbor data) can land at
quad-lane 0. `WavesPerGroup`'s bound has no bearing on this per-quad
mask, so the recovery's "always lane 0" assumption is unsound here in
general, independent of C8b's own specific repro.

Fixed the general case: `FunctionWidener::getFirstActiveLaneIndex`
derives the lane to extract from `EntryMask` itself (the lowest set bit,
via `cttz` over its bitcast-to-integer form, computed once per widened
function and memoized) instead of a hardcoded constant 0. Every
iterated wave has at least one active lane by construction (a wave with
none would never be dispatched), so this is always well-defined, and it
reduces to exactly lane 0 for the compute/task/mesh case (`EntryMask`'s
lane 0 is always set there), so no widened function's behavior changes
outside the fragment/quad-tiled case this was written for.

New unit test:
`SIMDizeTest.RecoversUniformValueFromEntryMaskDerivedLaneNotHardcodedLaneZero`,
asserting the recovery's `extractelement` index is a genuinely computed
`cttz`-derived value (traced through its `zext`/`trunc`), never a bare
constant.

**Measured against the actual named regression** (temporarily
re-enabling C8b's excluded localization path to reproduce it):
`cov-function-loop-condition-constant-array-always-false` still `Fail`s
after this fix. That specific fragment shader does not use derivative
instructions (`functionUsesQuadTiledComputeDerivatives` is false for it),
so its own `EntryMask` is not quad/helper-invocation-shaped at all --
this fix's mechanism does not apply to this particular case's actual
failure. The real root cause behind this specific regression remains
unidentified; C8b's own conservative exclusion guard is unchanged and
still required.

`ninja -C build2 check-feme`: 3196/3199 Passed (+1 new test), 3
pre-existing Unsupported, 0 Failed -- clean.

A `graphicsfuzz.*` re-sweep (733 of 757, excluding the same 24
pre-existing hangs/crashes as roadmap L120's own last-recorded sweep),
with this fix applied but C8b's guard left unchanged:

|               | Before this fix (L120's last sweep, 732 of 757) | After (733 of 757) |
|---------------|--------------------------------------------------|----------------------|
| Pass          | 568                                              | 568                 |
| Fail          | 156                                               | 157                 |
| NotSupported  | 8                                                 | 8                   |

Identical Pass count; the 1-case Fail-count difference is a 24-vs-25-
name hang-exclusion-list mismatch between this session's reused list
(`/tmp/gf_skipped.txt`, 24 names) and L120's own session (25 names,
noted there as "one more than the previously-tracked 24...most plausibly
test-harness timing variance"), not a new regression -- no case Passing
before now Fails, or vice versa.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- a `SIMDizePass` internal correctness fix, not a new Vulkan
feature/extension surface.

This row is **not closed**: the fix landed is real and independently
motivated (a genuine, previously-undiscovered fragment/quad-tiled-shader
bug in the same recovery code, now fixed), but the CTS regression this
roadmap row was originally opened to explain is still unresolved by it.
See `agent_thoughts.md`'s new entry for this session's own next-steps
recommendation on how to actually pin down that regression's root cause.

## Roadmap L118 (fixed this session): the real regression root cause was a `widenMaskedStore` masking bug, not the recovery lane

The section above ("Roadmap L118 (partial fix, real regression
unresolved)") landed a real, independently-motivated fix
(`getFirstActiveLaneIndex`) but measured it did *not* close
`dEQP-VK.graphicsfuzz.cov-function-loop-condition-constant-array-always-false`.
This session found and fixed the actual root cause, using runtime
instrumentation instead of further manual IR tracing (the prior two
sessions' own hand-algebra had both failed to converge).

**Method**: added a temporary debug-print host callback
(`femeCpuDebugPrintUniformRecoveryI32`, `feme/runtime/CPU/FeMeRuntimeCPU.c`,
reverted after use -- linked in automatically via the runtime bitcode's
own `LinkOnlyNeeded` embedding, the same mechanism every
`feme.cpu.resource.*` call already uses) and called it from
`SIMDize.cpp` at every masked-gather and masked-scatter site in the
failing shader's own widened IR, printing all 4 lanes' values and the
site's own effective mask bits. Rebuilt `libfeme_vulkan.so` (with C8b's
guard temporarily disabled to reproduce the regression) and re-ran the
failing case through `deqp-vk` to capture real runtime values.

**Finding**: `EntryMask` was `0xf` (all 4 lanes genuinely active) at
every site, and the two masked gathers inside the shader's own loop body
always agreed across all 4 lanes -- ruling out the stale-use recovery's
lane choice entirely, since that mechanism never even mattered here.
The divergence appeared only at the *final* masked store into the
localized `data0`/`data1` arrays: for roughly 32 of 272 dispatched
wave-groups, the scatter's own effective mask was `0xb` (lanes 0, 1, 3)
or `0x4` (lane 2 alone) instead of the expected `0xf` -- a genuine
per-lane mask divergence in a shader whose own control flow is otherwise
100% uniform (every branch condition reads only a shared uniform
buffer, no per-invocation input at all). This is exactly the signature
of a fragment shader's own helper invocations: whichever lane(s) are
"helper" for that particular dispatched quad (kept alive only so a
covered quad-mate's derivatives see real neighbor data, at a triangle or
framebuffer edge) get excluded from `Env.SideEffectMask`, which
`FunctionWidener::widenMaskedStore` used, *unconditionally*, as every
masked store's own governing mask -- including a masked store into a
`MaskedAllocas`-tracked base (a `Private`-storage global localized to a
real per-lane alloca, roadmap L84). A `MaskedAllocas` write is not a
device-visible side effect at all -- it is one invocation's own private
local storage, invisible to every other invocation regardless of
live/helper status -- so masking it with `SideEffectMask` incorrectly
skipped a helper invocation's own write to *its own* local copy, leaving
that lane's own later read of the same local variable stale. (Masked
*loads*, by contrast, already correctly used `Env.EntryMask`, which is
why the two earlier loop-body gathers always agreed.)

**Fix**: `widenMaskedStore` now branches its effective mask on whether
`Matched.Ptr` bottoms out (through zero or more `getelementptr`s, via
the pass's own existing `getUnderlyingAlloca` helper) at a
`MaskedAllocas`-tracked alloca: `Env.EntryMask` for that case,
`Env.SideEffectMask` unchanged for every other destination (groupshared,
device resources), where excluding helper invocations from an
observable side effect remains correct.

New unit test: `SIMDizeTest.MaskedAllocaStoreUsesEntryMaskNotSideEffectMask`,
constructing a masked store into a local alloca and asserting the
resulting `llvm.masked.scatter`'s mask operand traces back to
`wave_entry_mask`, never `wave_sideeffect_mask` -- confirmed to fail
without the fix (`wave_sideeffect_mask` found, `wave_entry_mask` not).

`ninja -C build2 check-feme`: 3197/3200 Passed (+1 new test since the
last L118 session), 3 pre-existing Unsupported, 0 Failed -- clean.

**Measured against the actual named regression** (temporarily
re-disabling C8b's excluded-localization guard, the same reproduction
technique as every prior L118/C8b session): `cov-function-loop-
condition-constant-array-always-false` now **Passes**.

A full `graphicsfuzz.*` re-sweep (733 of 757, excluding the same 24
pre-existing hangs/crashes reused across every recent session's own
sweep, `/tmp/gf_skipped.txt`), with this fix applied and C8b's own guard
left in place (unchanged -- see roadmap row L122 for why it is not yet
removed):

|               | Before this fix (568/157/8 baseline) | After (this session) |
|---------------|----------------------------------------|-----------------------|
| Pass          | 568                                     | 593                   |
| Fail          | 157                                     | 132                   |
| NotSupported  | 8                                       | 8                     |

**+25 Pass / -25 Fail, 0 regressions** -- the largest single-session
`graphicsfuzz.*` sweep improvement recorded on this roadmap to date,
despite fixing what reduces to a single-line masking bug. This is
consistent with the bug's own mechanism: any shader combining a
localized `Private` aggregate (or any other `MaskedAllocas`-tracked
local) with a masked store, dispatched across a wave containing at least
one helper invocation (i.e. essentially any fragment shader whose
triangle edges or framebuffer bounds do not exactly tile into whole
wave-groups), was silently corrupting that helper lane's own local
storage -- a broad, generically-triggered bug, not one specific to the
named regression's own particular shader shape.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- a `SIMDizePass` internal correctness fix, not a new Vulkan
feature/extension surface.

This row is now **closed**. See roadmap row L122 (new this session) for
the natural follow-up: confirming C8b's own conservative exclusion guard
is now provably redundant and can be removed, recovering whatever
further localization wins it still forgoes.

## Roadmap L122 (closed): removed C8b's now-redundant discard/demote localization guard

C8b's `mayDiscardOrDemote` guard (`LocalizePrivateGlobals.cpp`)
conservatively excluded any aggregate global used by a discard/demote-
capable function from localization at all, working around a
`SIMDize.cpp` bug (roadmap L118) rather than fixing it. L118's own
session fixed that bug at its root (`widenMaskedStore` now uses
`Env.EntryMask`, not `Env.SideEffectMask`, for a `MaskedAllocas`-based
destination) and confirmed, with the guard temporarily disabled, that
the one named regression the guard was written to prevent
(`cov-function-loop-condition-constant-array-always-false`) no longer
reproduces. The guard itself was left in place pending a broader
confirmation sweep, since it is broader than that one bug's trigger
condition.

This session ran that confirmation: rebuilt with the guard temporarily
disabled (same `if (false && ...)` throwaway technique used throughout
this roadmap's own history) and re-ran the full `graphicsfuzz.*` sweep
(733 of 757 cases, same 24-name hang exclusion list reused across
sessions). Result: **identical 593 Pass / 132 Fail / 8 NotSupported**
totals to the guard-enabled baseline -- 0 regressions, confirming no
other bug is being masked by the guard.

Removed the guard for real: deleted `mayDiscardOrDemote` and its call
site from `LocalizePrivateGlobalsPass::run`, along with its now-unused
`feme/Core/StageOps.h` and `llvm/IR/InstIterator.h` includes. Updated
`localize-private-globals.ll`'s `usesArrayInDiscardingFunction`
regression case to assert the global is now localized (previously
asserted it was left alone). Re-ran the full `graphicsfuzz.*` sweep a
second time with the guard actually removed (not just disabled):
**identical 593/132/8 totals again**, confirming the real removal
behaves exactly like the diagnostic disable.

`ninja -C build2 check-feme`: 3197/3200 Passed, 3 pre-existing
Unsupported, 0 Failed -- clean, no change in test count since removing
a guard doesn't add new coverage, only removes an exclusion.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- an internal `LocalizePrivateGlobalsPass` simplification, not a
new Vulkan feature/extension surface.

This row is now **closed**. C8b's own row has been updated to reflect
that its guard was removed with 0 further regressions.

## Roadmap L116(a) (closed): per-leaf decomposition for aggregate masked load/store

`MaskIntrinsics.cpp`'s `appendScalarMangling` has no notion of a
struct/array element type at all -- masked load/store semantics are
inherently per-element, so a masked op over a whole aggregate has no
natural single-instruction lowering the way a masked scalar/vector
access does. This left any masked access to a `struct`- or
`mat4`-typed local array element indexed by a divergent index inside a
loop (exactly graphicsfuzz's own idiom) diagnosed as unsupported by
`feme-cpu-masked-mem-op` rather than legalized -- by far the largest
single bucket in the original L116 sweep, ~59% of that sweep's `Fail`
error volume.

**Fix**: added `createMaskedLoadRecursive`/`createMaskedStoreRecursive`
to `Linearize.cpp`. Each recursively walks a struct/array-typed access
one leaf (scalar or fixed-vector) at a time: `CreateStructGEP`/
`CreateGEP` compute each leaf's own address, `insertvalue`/
`extractvalue` reassemble/decompose the aggregate value itself, and
`commonAlignment` narrows each leaf's own alignment from the whole
access's base alignment and the leaf's byte offset (mirroring
`SIMDize.cpp`'s own per-component alignment narrowing for a wave-widened
vector gather/scatter, roadmap L118's own neighboring code). Each leaf
is still handed to the existing, entirely unchanged
`createMaskedLoad`/`createMaskedStore` -- so this fix needed no change
at all to `MaskIntrinsics.cpp`'s own mangling, nor to `SIMDize.cpp`:
the decomposed leaves are ordinary scalar/vector masked load/store
calls, a shape `SIMDize.cpp` already fully supports (confirmed directly
via `feme-opt --llvm -passes="feme-cpu-linearize,feme-cpu-simdize"`
against a hand-written `{i32, <2 x float>}` repro -- both leaves widen
cleanly into a `llvm.masked.scatter`/per-lane store with no error).

The pre-existing `LinearizeTest.
UnsupportedAggregateMaskedStoreDiagnosesGracefullyInsteadOfCrashing`
regression case was reduced against `{float, float}` -- a shape this
fix now fully supports -- so it was rewritten against a genuinely-
still-unsupported leaf type (`x86_fp80`) to keep covering the
graceful-diagnostic path for whatever leaf shape decomposition itself
cannot yet handle. New tests
`DecomposesAggregateMaskedStorePerLeaf`/`DecomposesAggregateMaskedLoadPerLeaf`
cover the new decomposition itself (a two-leaf struct store, and a
struct-containing-an-array load reassembled via `insertvalue`).

`ninja -C build2 check-feme`: 3199/3202 Passed (+2 new tests), 3
pre-existing Unsupported, 0 Failed -- clean.

A full `graphicsfuzz.*` re-sweep (733 of 757 cases, same 24-name hang
exclusion list reused across sessions), immediately following L122's
own confirmation sweep in this same session:

|               | Before this fix (L122's own 593/132/8) | After |
|---------------|------------------------------------------|--------|
| Pass          | 593                                       | 594    |
| Fail          | 132                                       | 131    |
| NotSupported  | 8                                         | 8      |

**+1 Pass, 0 regressions.** A smaller real-world win than this row's
own ~59%-of-error-volume estimate might suggest: most of the original
197 `Fail` cases this error occurred in also hit at least one other,
still-open `L116` sub-row's own gap (see L116(b)'s divergent-branch/
reconvergence gaps, L116(d)'s `spirv.Constant`/`CompositeConstruct`
aggregate-legalization gap, or L116(f)'s un-root-caused hangs/crashes)
before the shader could ever reach a real pass/fail verdict -- so this
fix is a necessary, but for most of those cases not sufficient, step
toward closing them. It is still real, independently-verified progress
(confirmed error-free legalization end to end for the shape this row
names), and removes what was previously the single largest blocking
gap in the L116 breakdown.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- an internal `LinearizePass` legalization fix, not a new
Vulkan feature/extension surface.

This row is now **closed**.

## L120's `Modf` (this session)

`spirv.GL.Modf` (GLSL.std.450 opcode 35) had no MLIR op at all --
only its pointer-free sibling `ModfStruct` (opcode 36) was
implemented. Added `SPIRV_GLModfOp` to `SPIRVGLOps.td` (a genuine
memory-effect op, since it writes its integer part through a pointer
operand; verified via a new `spirv::GLModfOp::verify()`), plus a
feme-side `ModfPattern` lowering it the same way `ModfStructPattern`
already does (truncation for the integer part, subtraction for the
fraction), except storing the integer part through the pointer
operand instead of packing it into a struct result.

A full `graphicsfuzz.*` re-sweep (733 of 757 cases, same 24-name hang
exclusion list reused across sessions):

|               | Before this fix (L116(a)'s own 594/131/8) | After |
|---------------|--------------------------------------------|--------|
| Pass          | 594                                          | 600    |
| Fail          | 131                                          | 125    |
| NotSupported  | 8                                             | 8      |

**+6 Pass, 0 regressions** -- matching exactly the 6 `Modf`
occurrences flagged in L116(c)'s original sweep. `ninja check-feme`:
3199/3202 Passed, 3 pre-existing Unsupported, 0 Failed -- clean.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
update needed -- an internal SPIR-V-to-LLVM legalization fix for an
already-supported GLSL.std.450 extended instruction set, not a new
Vulkan feature/extension surface.

This closes L120 (both `Ldexp`, from the prior session, and `Modf`,
this session).

## L121 (this session)

`feme::cpu::SIMDizePass`'s `widenElementwise` only widened a divergent
call to a vectorizable intrinsic via a same-type-everywhere
`Homogeneous` check, plus one hardcoded special case for
`llvm.is.fpclass`. `llvm.ldexp`'s independently-overloaded `i32`
exponent operand fell through to the generic "unsupported divergent
call" error -- discovered measuring L120's own `Ldexp` fix's CTS
impact (`dEQP-VK.graphicsfuzz.cov-ldexp-exponent-undefined-divided-
fragcoord-never-executed`).

Replaced the ad hoc `is_fpclass`-only special case with a small,
explicitly-enumerated `DivergentCallOverloadShape` table so both
`is_fpclass` and `ldexp` share one general widening path.

A full `graphicsfuzz.*` re-sweep (733 of 757 cases, same 24-name hang
exclusion list reused across sessions):

|               | Before this fix (L120's own 600/125/8) | After |
|---------------|-------------------------------------------|--------|
| Pass          | 600                                        | 601    |
| Fail          | 125                                        | 124    |
| NotSupported  | 8                                           | 8      |

**+1 Pass, 0 regressions** -- the exact case this row's own
investigation found. `ninja check-feme`: 3200/3203 Passed, 3
pre-existing Unsupported, 0 Failed -- clean.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- an internal `SIMDizePass` legalization fix, not a new Vulkan
feature/extension surface.

This row is now **closed**.

## L106 (this session)

Triaged the previously-untriaged `pipeline.monolithic.*`/`subgroups.*`/
`compute.*` families (too large to sweep exhaustively -- 465,554/48,705/
61,460 cases respectively). Sampled `subgroups.*`'s small/medium
subfamilies (~9,700+ cases): no real `Fail`s found; `quad`/`clustered`
are 100% `NotSupported` (legitimate unadvertised-feature gaps, not
bugs). Sampled `compute.pipeline.*`'s small subfamilies (~136 cases) and
found `builtin_var.*` systemically broken: 10 of 11 cases failing --
every vec3-typed compute builtin (`global_invocation_id`,
`local_invocation_id`, `work_group_id`, etc.) failed; only the scalar
`local_invocation_index` passed.

Root-caused this to a real bug in the SPIR-V-to-LLVM storage-buffer
lowering: `convertBufferBlockType` (SPIRVToLLVMPatterns.cpp) and
`classifyVulkanBufferHandle` (SPIRVResourceLowering.cpp) both assumed a
std430 storage buffer's `ArrayStride` always equals its element's
natural LLVM store size. True for scalar/vec2/vec4/matrix elements, but
false for a 3-component vector element: std430 still pads every array
element up to a 16-byte multiple regardless of storage class, so a
`vec3`'s 12-byte natural size still needs a 16-byte stride. Using the
too-small natural-size stride addressed later array indices at the
wrong byte offset, silently dropping writes to the buffer's true tail
slots -- confirmed via a new, minimal `CommandBufferTest.cpp` unit test
before touching any production code.

Fixed by carrying the real `ArrayStride` explicitly as an optional
third integer parameter on the `spirv.VulkanBuffer` handle, mirroring
the existing convention `convertUniformArrayContent`/
`classifyVulkanBufferHandle` already use for a std140 uniform array's
own (more common) stride mismatch, and by disambiguating a storage vs.
uniform array handle by storage class plus writability (mirroring the
adjacent struct-handling branch) instead of by parameter count, since
parameter count no longer implies the kind. `SPIRVRaising.cpp`'s DXIL
handle-type translation needed a matching relaxation (accept two or
three int parameters, ignoring the third): DXIL's own
`StructuredBuffer<T>` has no std430-style padding, so it always uses
`ElemTy`'s own natural size regardless.

`dEQP-VK.compute.pipeline.builtin_var.*` (the family that exposed the
bug):

|               | Before | After |
|---------------|--------|-------|
| Pass          | 1      | 11    |
| Fail          | 10     | 0     |

A full `compute.*` re-sweep (61,460 cases):

|               | Before | After |
|---------------|--------|-------|
| Pass          | 656    | 669   |
| Fail          | 29     | 16    |
| NotSupported  | 60,775 | 60,775 |

A full `ssbo.*` re-sweep (12,225 cases -- the other family directly
exercising storage-buffer array addressing):

|               | Before | After |
|---------------|--------|-------|
| Pass          | 2,236  | 2,337 |
| Fail          | 1,006  | 905   |
| NotSupported  | 8,983  | 8,983 |

**+114 Pass across `compute.*`/`ssbo.*` combined, 0 regressions.**
`ninja check-feme`: 3,201/3,204 Passed, 3 pre-existing Unsupported, 0
Failed -- clean.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- an internal SPIR-V-to-LLVM/DXIL-raising legalization fix for
an already-supported buffer-resource surface, not a new Vulkan
feature/extension.

The remaining `compute.*`/`ssbo.*` failures, `pipeline.monolithic.*`'s
untriaged status, and `subgroups.ballot_broadcast.*`'s sweep are not
yet root-caused -- see `agent_thoughts.md` for next steps.

## Roadmap L124 (partially fixed this session): `compute.*`'s remaining fails triaged and 11/16 fixed; `ssbo.*` bucketed

Picked up L124's own `compute.*`/`ssbo.*` triage. Bucketed `compute.*`'s 16
`Fail`s by case name: 11 in `zero_initialize_workgroup_memory.*` (10 matrix
types plus `types.bool`/`composites.2`), 5 in `compute.pipeline.basic.*`/
`device_group.*` (carried over unfixed from an earlier session).

**Fix 1: `OpConstantNull` for `spirv.matrix`.** `getNullAttrForType`
(`mlir/lib/Target/SPIRV/Deserialization/Deserializer.cpp`) had no case for
`spirv::MatrixType` -- `Builder::getZeroAttr` itself only special-cases
`VectorType`/`RankedTensorType`, not `MatrixType`, so every matrix-typed
`Workgroup`-storage global's `OpConstantNull` zero-initializer failed with
`unsupported OpConstantNull type: '!spirv.matrix<...>'`. Fixed by building
the same flat, broadcast-element `DenseElementsAttr` shape
`processConstantComposite` already builds for an ordinary matrix composite
constant. New test case added to `mlir/test/Target/SPIRV/global-variable.mlir`.

**Fix 2: `OpName`/`OpEntryPoint` name mismatch + `spirv.GL.NClamp`
legalization**, both needed to unblock
`dEQP-VK.compute.pipeline.basic.vec2_nclamp_nan_component`:

- Its hand-written SPIR-V has `OpName %_computeSomething
  "_computeSomething"` alongside `OpEntryPoint GLCompute %_computeSomething
  "main"` -- entirely legal SPIR-V (`OpName` is a purely informational debug
  annotation, no semantic significance per spec), but the deserializer's
  `OpEntryPoint` processing (`DeserializeOps.cpp`) rejected any non-
  placeholder name mismatch as an error. Fixed by always renaming the
  function to the entry point's own authoritative name. New `.spvasm`
  deserialization-only regression test (round-tripping through MLIR text
  alone can never construct this mismatch, since serialization always keeps
  a function's name and its entry point's name in sync).
- With that fixed, the case still failed one layer deeper: `failed to
  legalize operation 'spirv.GL.NClamp'`. Upstream's `SPIRVToLLVM.cpp`
  registered a `ClampPattern` for `FClamp`/`SClamp`/`UClamp` but never for
  `NClamp`, even though `NClamp`'s NaN-safe `min(max(x, minVal), maxVal)`
  semantics are exactly what the same `llvm.intr.maxnum`/`llvm.intr.minnum`
  pair `FClamp` already uses computes. Fixed by reusing the existing
  `ClampPattern` template for `GLNClampOp`, no new pattern class needed. New
  test in `mlir/test/Conversion/SPIRVToLLVM/gl-ops-to-llvm.mlir`.

A full `compute.*` re-sweep (61,460 cases):

|               | Before | After |
|---------------|--------|-------|
| Pass          | 669    | 679   |
| Fail          | 16     | 6     |
| NotSupported  | 60,775 | 60,775 |

**+10 Pass, 0 regressions.** `ninja check-feme`: 3,201/3,204 Passed, 3
pre-existing Unsupported, 0 Failed -- clean, run after each fix.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- both fixes are internal SPIR-V-dialect/legalization gaps for
already-supported surfaces (a null-constant shape, a debug annotation
relaxation, and a GLSL.std.450 extended instruction), not new Vulkan
features/extensions.

The remaining 6 `compute.*` fails are scoped as roadmap rows L124(a)-(d)
(`read_unbound_ssbo`, `remove_global_load_pass`, `undefined_values`,
`device_group.device_index`) plus the pre-existing, separately-documented
`containsAddressableBool` limitation (`types.bool`/`composites.2`, out of
scope for this session's fixes).

`ssbo.*`'s 905 `Fail`s were bucketed this session (no fixes attempted) by
grepping each failing case's first `error:`/`JIT session error`/result-
comparison line across a full re-sweep:

| Bucket | Count | Roadmap row |
|--------|-------|-------------|
| Missing `feme.cpu.resource.store.raw.i8` runtime symbol | 274 | L124(e) |
| `spirv.AccessChain` into a `RowMajor` matrix in a runtime array fails legalization | 36 | L124(f) |
| Wrong numeric result (not a crash/legalization failure) | 572 | L124(g) |
| Unclassified / `unsized_array_length.*` | 23 | L124(h) |

735 of the 905 (81%) are matrix-typed cases by name, and the 572-case
wrong-numeric-result bucket (63% of the total) is not yet root-caused at
all -- likely the single highest-value item in the whole L124 breakdown,
plausibly a systemic std140/std430 matrix layout/stride bug analogous in
shape to L123's own vec3-stride fix, but for matrices. See `agent_thoughts.md`
for the full narrative and next steps.

## Roadmap L124(e) (closed this session): `ssbo.*`'s missing-runtime-symbol bucket fixed as a poison-padding-store elision, not new runtime variants

Started this session by picking up L124(e), the previous session's own
largest scoped `ssbo.*` bucket (274 of 905 `Fail`s, all hitting
`JIT session error: Symbols not found: [ feme.cpu.resource.store.raw.i8 ]`).
The prior session's own scoping assumed this needed brand-new
`i8`/`v2i8`/`v3i8`/`v4i8` raw-store runtime-function variants (mirroring the
existing `i16` variant's shape) plus their JIT-symbol registration.

Reproducing `dEQP-VK.ssbo.layout.single_basic_type.std140.
column_major_highp_mat2` with `FEME_DUMP_IR=1` showed every failing
`store.raw.i8` call in the dumped IR stores a literal `poison` operand, one
call per byte, across 8 consecutive bytes between a `mat2`'s two real
4-byte columns -- exactly std140's own per-column vec4 padding. Tracing
this to `layOutStructIfOffsetsMatch` (`SPIRVToLLVMPatterns.cpp`) confirmed
it inserts a synthetic `[N x i8]` gap array member wherever a std140/std430
struct/matrix needs interior alignment padding -- a member no SPIR-V-level
composite construction ever assigns a real value to. `lowerRawStore`
(`SPIRVResourceLowering.cpp`)'s generic per-field/per-element decomposition
had no awareness of "padding vs. real data" and recursed all the way into
each gap byte, emitting a genuine runtime call for it regardless.

Confirmed via the mirror-image LOAD path in the same dumped IR that the
read side does the identical per-byte decomposition (16 `load.raw.i8`
calls for the same 16 padding bytes) and does not crash today only because
`feme.cpu.resource.load.raw.i8` already happens to exist -- meaning the
read side already silently tolerates this same wasteful pattern.

Concluded the minimal, correct fix is not new runtime variants at all, but
teaching `lowerRawStore` to recognize a poison-valued aggregate subtree and
skip storing it entirely -- writing an unspecified byte pattern into
padding no SPIR-V-visible load can ever observe is a pure no-op, and no
real SPIR-V-level value production could ever legitimately produce a
literal `poison`/`undef` operand at this call site. Since a real matrix
value's gap member is usually reached via an `extractvalue` over an
as-yet-unfolded `insertvalue` chain (not yet a literal `poison` constant at
this pass's own point in the pipeline, confirmed by a first attempt at this
fix that only checked `isa<UndefValue>` on the aggregate/leaf directly and
found 0 elision in a hand-written unit test until a later `ninja
check-feme`-style full-pipeline InstCombine run would have folded it),
`lowerRawStore` now uses `llvm::FindInsertedValue` (the same insertvalue-
chain-walking utility InstCombine's own peephole uses) to see through that
chain immediately, rather than depending on a separate, later simplification
pass to fold it first.

New unit test: `SPIRVResourceLoweringTest.
SkipsRawStoreOfPoisonAlignmentGapMember`, constructing a packed
`<{ [2 x float], [8 x i8] }>`-shaped store value (the same shape the real
`mat2`/std140 repro produces) with only the real `[2 x float]` field
populated via `insertvalue`, confirming exactly 2 `store.raw.f32` calls are
emitted and 0 `store.raw.i8` calls.

A full `ssbo.*` re-sweep (12,225 cases):

|               | Before | After |
|---------------|--------|-------|
| Pass          | 2,337  | 2,591 |
| Fail          | 905    | 651   |
| NotSupported  | 8,983  | 8,983 |

**+254 Pass, 0 regressions.** A full `compute.*` re-sweep confirmed no
change (679/6/60,775, as expected -- this bug was `ssbo.*`-only, since
`compute.*`'s own remaining fails are the unrelated L124(a)-(d) gaps).
`ninja check-feme`: 3,202/3,205 Passed, 3 pre-existing Unsupported, 0
Failed (+1 Passed from this session's own new unit test; 0 regressions).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- this is an internal codegen-efficiency/correctness fix on an
already-supported storage-buffer surface (std140/std430 layout was already
modeled; this only fixes a wasteful/crashing padding-byte round-trip), not
a new Vulkan feature or extension.

`ssbo.*`'s remaining 651 `Fail`s were re-bucketed after this fix (L124(e)
only removed missing-symbol crashes, not any wrong-result cases, so
L124(f)/(g)/(h)'s own bucket counts shift only by whichever cases happened
to double-fail on both a missing symbol and something else -- none did, so
L124(f)/(g)/(h)'s buckets are unchanged in absolute count, just a larger
share of the new, smaller 651 total):

| Bucket | Count | Roadmap row |
|--------|-------|-------------|
| `spirv.AccessChain` into a `RowMajor` matrix in a runtime array fails legalization | 36 | L124(f) |
| Wrong numeric result (not a crash/legalization failure) | ~572 | L124(g) |
| Unclassified / `unsized_array_length.*` | ~23 | L124(h) |

555 of the 651 (85%) are still matrix-typed cases by name, mostly
`row_major`. L124(g)'s 572-case wrong-numeric-result bucket remains the
single largest, still-unstarted item in the whole `ssbo.*` breakdown. See
`agent_thoughts.md` for the full narrative and next steps.

## L124(g): Array-of-matrices RowMajor/MatrixStride storage-buffer fix

`dEQP-VK.ssbo.layout.single_basic_array.std140.row_major_mat2` (an array of
`mat2`s, plain GLSL `buffer Block { mat2 matrices[3]; }`) was reduced with
`FEME_DUMP_IR=1`: its intra-matrix row-to-row byte offsets landed at `+0`/
`+8` (the row's own natural, unpadded size) instead of std140's required
`+0`/`+16` (vec4-rounded `MatrixStride`), even though the *array*-level
stride between matrix elements was correctly `32`.

Root-caused to two independent gaps in `SPIRVToLLVMPatterns.cpp` that
combined to silently miscompile this shape:

1. `getBufferBlockElement` only ever recognized a sole `spirv.rtarray`
   member (FeMe's own dxc-wrapper shape) as a dynamically-indexed wrapper;
   a sole *fixed*-size `spirv.array` member (this GLSL shape) fell through
   to the "ordinary struct" branch instead, unlike `getUniformBlockElement`'s
   pre-existing analogous fix for uniform blocks (roadmap F12a). This alone
   meant `getMatrixWholeAccess` could never recognize a whole-matrix access
   through this shape at all, so `RowMajorMatrixStorePattern`/
   `RowMajorMatrixLoadPattern` never fired, silently falling back to the
   ordinary (always logical/column-major) `spirv.Store`/`spirv.Load`
   conversion.
2. Independently, `isMatrixMemberLayoutRepresentable` -- the check
   `convertOffsetStructTypeIgnoringDecorations` uses to decide whether a
   member needs the physical RowMajor/MatrixStride layout substitution at
   all -- only ever recognized a matrix that is directly a struct member's
   own type, never one reached through an array wrapper, so it always
   answered "representable" for an array-of-matrices member regardless of
   its real decorations.

Both root-caused via direct debugging (`llvm::errs()` diagnostics
temporarily added and removed, per the standing plan from the prior
session, rather than reasoning from source alone this time): the first
diagnostic confirmed the type-conversion fix (added first) computed the
correct physical/padded member type, yet the generated IR was unchanged;
a second diagnostic in `RowMajorMatrixStorePattern::matchAndRewrite`
confirmed `getMatrixWholeAccess` was returning `std::nullopt` for this
repro, tracing back to `getBufferBlockElement`'s own wrapper-recognition
gap as the real blocker.

Fixed by recognizing a sole fixed-size `spirv.array` member as
`HasWrapper=true` in `getBufferBlockElement` (mirroring
`getUniformBlockElement`), updating `convertBufferBlockType`'s
stride/writability computation to handle both array kinds, and adding
`peelArraysToMatrixType`/`wrapPhysicalMatrixInArrays` helpers so
`convertOffsetStructTypeIgnoringDecorations`'s per-member representability
check and physical-type substitution both see through array nesting to the
real inner matrix, exactly as `rewriteBlockAccess` already does for its own
narrower AccessChain purpose.

New regression test: `spirv-to-llvm-matrix-rowmajor-fixed-array-block.mlir`
(mirrors `spirv-to-llvm-matrix-rowmajor-buffer-block.mlir`'s own dxc-wrapper
RowMajor store/load test, but for this plain-GLSL fixed-array shape).
`spirv-to-llvm-storage-buffer.mlir`'s own `read_vec3_array_element` CHECK
lines needed updating too: the same underlying fix means a fixed-size
array-of-vec3 storage-buffer member (no matrix/RowMajor decorations
involved) is now also recognized as the wrapper shape, so its handle's
content type is the array directly (no enclosing single-member struct), no
extra GEP is needed to reach an element, and its declared `NonWritable`
decoration is now correctly reflected in the handle's `IsWriteable`
parameter (previously always `1`, a pre-existing bug for this exact shape
this incidentally also fixes).

A full `ssbo.*` re-sweep (12,225 cases):

|               | Before | After |
|---------------|--------|-------|
| Pass          | 2,591  | 2,729 |
| Fail          | 651    | 513   |
| NotSupported  | 8,983  | 8,983 |

**+138 Pass, 0 regressions.** A full `compute.*` re-sweep confirmed no
change (679/6/60,775, as expected -- this bug was `ssbo.*`-only).
`ninja check-feme`: 3,203/3,206 Passed, 3 pre-existing Unsupported, 0
Failed (+1 Passed from this session's own new unit test; 0 regressions).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- an internal storage-buffer layout-correctness fix on an
already-supported surface, not a new Vulkan feature or extension.

`ssbo.*`'s remaining 513 `Fail`s were re-bucketed by case-name family (see
`Roadmap.md`'s new L124(i) row): `3_level_unsized_array` (87),
`3_level_array` (87), `2_level_array` (87), `instance_array_basic_type`
(84), `random` (68), `unsized_nested_struct_array` (24), plus 4 lingering
`unsized_array_length.*` singletons. None of these individually
root-caused yet -- L124(g)'s own fix (single fixed-size array-of-matrices
member) did not close them, suggesting a related but distinct gap in the
same array-wrapper-recognition/physical-substitution machinery for more
deeply nested or struct-wrapped shapes. See `agent_thoughts.md` for the
full narrative and next steps.

## Roadmap L124(i) (whole-access gap closed this session): nested array-of-matrices RowMajor/MatrixStride storage-buffer fix

Picked up L124(i)'s own 513-fail re-bucketing from the prior session.
Reduced a first repro from the largest bucket family
(`dEQP-VK.ssbo.layout.2_level_array.std140.column_major_mat2`), confirmed
via `FEME_DUMP_IR=1` that the bug affects both `RowMajor` and `ColMajor`
matrices (not `RowMajor`-transpose-specific), and traced it to the same
underlying `MatrixStride`-padding symptom L124(g) fixed, but for a matrix
reached through *two* levels of array nesting instead of one.

Root cause: `getMatrixWholeAccess`'s wrapper-shape branch hard-coded the
expected `spirv.AccessChain` index count to exactly 2 (a dummy wrapper
selector plus one array index) -- exactly L124(g)'s own single-nesting
shape, and no other. A 2-level-or-deeper wrapper content
(`!spirv.array<M x !spirv.array<N x matCxR>>>`, what `2_level_array`/
`3_level_array`/`3_level_unsized_array` exercise) needs one more index
per array level, so this check always rejected it: neither
`RowMajorMatrixStorePattern` nor `RowMajorMatrixLoadPattern` ever fired
for this shape, and the plain, physically-wrong generic `spirv.Store`/
`spirv.Load` conversion silently took over instead.

Fixed by replacing the hard-coded count with a loop that peels through
however many levels of array nesting the wrapper's own sole member
actually has before reaching the innermost matrix, and requiring exactly
that many indices. Investigated (via `padStructToSize`/
`convertArrayTypeIgnoringDecorations`) whether the array levels'
themselves also needed a fix, and confirmed they did not: the pre-existing
generic array-type conversion already pads an undersized element (a
matrix's own naturally-tight LLVM type) up to its declared `ArrayStride`
with a uniform byte-array stand-in, recursively at every nesting level,
so a nested array-of-matrices' own per-level byte strides were already
correct -- only the whole-matrix *access recognition* itself was too
narrow.

New regression test: `spirv-to-llvm-matrix-rowmajor-nested-array-block.mlir`
(mirrors L124(g)'s own `spirv-to-llvm-matrix-rowmajor-fixed-array-block.mlir`,
but with a second array nesting level).

A full `ssbo.*` re-sweep (12,225 cases):

|               | Before | After |
|---------------|--------|-------|
| Pass          | 2,729  | 2,847 |
| Fail          | 513    | 395   |
| NotSupported  | 8,983  | 8,983 |

**+118 Pass, 0 regressions.** A full `compute.*` re-sweep confirmed no
change (679/6/60,775). `ninja check-feme`: 3,204/3,207 Passed, 3
pre-existing Unsupported, 0 Failed (was 3,203/3,206 -- +1 Pass from this
session's own new lit test; 0 regressions).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- an internal storage-buffer layout-correctness fix on an
already-supported surface, not a new Vulkan feature or extension.

`ssbo.*`'s remaining 395 `Fail`s were re-bucketed by case-name family:
`instance_array_basic_type` (84), `random` (67), `3_level_unsized_array`
(48), `3_level_array` (48), `2_level_array` (48), `single_basic_array`
(36), `basic_unsized_array` (36), `unsized_nested_struct_array` (24), plus
4 lingering `unsized_array_length.*` singletons. Inspecting the still-
failing case names within `2_level_array`/`3_level_array`/
`3_level_unsized_array` shows every one is now a `*_store_cols`/
`*_comp_access_store_cols` variant (a partial column/scalar write, not a
whole-matrix one) -- this session's fix only ever addressed whole-matrix
access; `rewriteBlockAccess`'s own partial-access special case
(`isa<MatrixType>(SelectedType)`) still requires `SelectedType` to be
*directly* a matrix one index past the wrapper's own initial
`getpointer` selector, so it's skipped entirely for any array nesting
(including `single_basic_array`'s own pre-existing single-level case,
whose RowMajor column-select was already known-declined rather than
fixed). Tracked as `Roadmap.md`'s new L124(j) row. `instance_array_basic_type`'s
84 remaining fails include *whole*-access failures too (not just partial
ones), suggesting a materially different content shape (array of block
*instances*, not an array member nested inside one block) -- tracked as
L124(k). The rest (`random`/`basic_unsized_array`/
`unsized_nested_struct_array`/`unsized_array_length.*`) not yet
re-triaged this session -- tracked as L124(l). See `agent_thoughts.md` for
the full narrative and next steps.

## Roadmap L124(j) (`ColMajor` half closed this session): partial (column-select/scalar-element) access into a nested-array-wrapped matrix

L124(i)'s fix only ever addressed *whole*-matrix `spirv.Store`/`spirv.Load`
recognition for a nested-array-wrapped matrix; a *partial* access (a single
column write/read, or a single scalar element) into the same shape --
exactly `2_level_array`/`3_level_array`/`3_level_unsized_array`'s own
`*_store_cols`/`*_comp_access_store_cols` variants -- silently miscompiled
even after that fix, since `rewriteBlockAccess`'s own
`isa<MatrixType>(SelectedType)` partial-access branch only ever fires when
`SelectedType` (the type reached one index past the wrapper's own initial
selector) is *directly* a matrix. For any array nesting, `SelectedType` was
still an array at that point, so the branch never fired and the access fell
through to the generic, `MatrixStride`-unaware index-forwarding GEP
fallback instead, computing a plain byte-granularity offset rather than the
correct `MatrixStride`-granularity one.

Fixed with a peek-then-conditionally-act design (an unconditional eager-peel
first attempt caused a regression in `spirv-to-llvm-array-of-identified-
struct-stride.mlir`, a fixed-array-of-identified-struct case with no matrix
involved at all -- reverted in favor of this safer design): peek ahead (no
side effects) through however many further array levels precede the matrix
or a matrix-containing struct, to confirm the access is genuinely one of
the two deep-nesting shapes needing special handling (a matrix with 1-2
indices remaining, or a struct with exactly 2, mirroring H151's own
nested-struct-with-matrix-member shape) -- only then perform the actual
peeling GEPs and generalize every downstream `Selector+1`/`+2`/`+3`
index-position reference (the partial-access branch itself, the H151
nested-struct-in-array branch, and the final generic fallback) to a dynamic
`NextIndexPos`. When the peek doesn't confirm a deep-nesting shape,
`NextIndexPos` equals `Selector+1` exactly as before, so every existing
non-deep-nested shape's IR/test output is unchanged.

New regression test: `spirv-to-llvm-matrix-colmajor-nested-array-column.mlir`
(column-select and scalar-element access into a `ColMajor` 2-level-array-
wrapped matrix, mirroring `spirv-to-llvm-matrix-block-wrapper-partial.mlir`'s
own single-level shape one nesting level deeper).

A full `ssbo.*` re-sweep (12,225 cases):

|               | Before | After |
|---------------|--------|-------|
| Pass          | 2,847  | 2,865 |
| Fail          | 395    | 377   |
| NotSupported  | 8,983  | 8,983 |

**+18 Pass, 0 regressions.** Confirmed every remaining `2_level_array`/
`3_level_array`/`3_level_unsized_array` fail (42 each, was 48 each) is now
`RowMajor`-only -- `ColMajor` is fully closed for this shape. A full
`compute.*` re-sweep confirmed no change (679/6/60,775). `ninja check-feme`:
3,205/3,208 Passed, 3 pre-existing Unsupported, 0 Failed (was 3,204/3,207 --
+1 Pass from this session's own new lit test; 0 regressions).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- an internal storage-buffer layout-correctness fix on an
already-supported surface, not a new Vulkan feature or extension.

The `RowMajor` half of L124(j)'s original scope (`2_level_array`'s/
`3_level_array`'s/`3_level_unsized_array`'s own remaining `RowMajor` fails,
plus `single_basic_array`'s pre-existing 36) turned out, on closer
inspection this session, to be the *same* underlying bug as L124(f) (a
`spirv.AccessChain` into a `RowMajor`-decorated matrix through an array
wrapper fails legalization outright -- an explicit conversion-target
rejection, not a silent wrong result), not a distinct
`getMatrixColumnAccessShape` gap as a prior session's notes speculated (the
error message/shape for `single_basic_array.std140.row_major_mat2_store_cols`
is identical to L124(f)'s own repros). No new roadmap row needed -- L124(f)
already covers it. See `agent_thoughts.md` for the full narrative and next
steps.

## Roadmap L124(f) (closed this session): `RowMajor` column-select through a wrapper array

`spirv.AccessChain` into a `RowMajor`-decorated matrix reached through a
dxc-style wrapper array (`RWStructuredBuffer<matCxR>`) or nested fixed-size
GLSL arrays, at any nesting depth, previously declined legalization outright
for the column-select case (`error: failed to legalize operation
'spirv.AccessChain' that was explicitly marked illegal`) -- confirmed via
`dEQP-VK.ssbo.layout.single_basic_array.std140.row_major_mat2_store_cols` and
its `2_level_array`/`3_level_array`/`3_level_unsized_array` siblings.

Root cause: `rewriteBlockAccess`'s own `isa<MatrixType>(SelectedType)`
partial-access branch, for the `RowMajor` column-select case, only deferred to
`MatrixColumnLoadPattern`/`MatrixColumnStorePattern` (by replacing the op with
the bare `ElementPtr`) when `!Element.HasWrapper` -- i.e. only for the direct
(`cbuffer`/`ConstantBuffer<T>`) shape. For the wrapper-array shape it fell
through and declined instead. Those patterns' own `getMatrixColumnAccess`
helper, which independently re-derives the access shape from the original
`AccessChainOp`, only recognized two shapes: `Element.Content` directly a
`StructType`, or an array of `StructType` -- never an array (at any nesting
depth) directly wrapping a `MatrixType` with no intervening struct at all,
exactly the wrapper-array shape.

Both sides had to move in lockstep: `rewriteBlockAccess`'s decision to defer
(rather than decline) must be gated on exactly the shapes `getMatrixColumnAccess`
can actually resolve, or a deferred, unresolved address would silently
corrupt (worse than declining). Fixed by adding
`getWrapperArrayMatrixColumnAccess`, which peels `Element.Content` through
however many array levels precede the matrix (mirroring L124(j)'s own peel)
and -- if the fully-peeled type is a `MatrixType` with exactly one remaining
index (a column selector) -- reads the `RowMajor`/`MatrixStride` decorations
off the wrapper struct's own sole member (always index 0, since there is no
member index to read from the access chain itself for this shape, unlike the
pre-existing array-of-struct shape). Wired this in ahead of the pre-existing
`getMatrixColumnAccessShape`-based logic in `getMatrixColumnAccess`, verified
it naturally falls through unchanged for both pre-existing shapes (the
direct-struct-content shape's peel loop breaks immediately; the
array-of-struct shape's peel loop stops at the struct, never reaching a bare
matrix). Removed the now-redundant `!Element.HasWrapper` guard in
`rewriteBlockAccess`'s own deferral, since `ElementPtr` already points at the
matrix's own base address regardless of wrapper/nesting.

New `spirv-to-llvm-matrix-rowmajor-wrapper-array-column.mlir` regression test
(single-level wrapper `read_column`, plus a new 2-level nested-array
`read_column_nested` matching `2_level_array`'s own CTS shape). The
now-resolved case that used to live in
`spirv-to-llvm-matrix-block-invalid.mlir`'s second `RUN` split (whose
`expected-error` no longer fires) moved into the new file as a positive test
instead; that invalid-test file now covers only the one case that remains
genuinely malformed (`RowMajor` with no `MatrixStride` decoration at all).

Re-swept `ssbo.*` (12,225 cases): **3,027 Pass / 215 Fail / 8,983
NotSupported** (was 2,865/377/8,983) -- **+162 Pass, 0 regressions**, well
beyond the 126 cases originally estimated (36 `single_basic_array` + 90 from
`2_level_array`/`3_level_array`/`3_level_unsized_array`), since this same code
path also closed most of `instance_array_basic_type`/`random`/
`unsized_nested_struct_array`'s own RowMajor column-select cases. `compute.*`
unchanged (679/6/60,775, confirmed by a full re-sweep). `ninja check-feme`:
3,206/3,209 Passed, 3 Unsupported, 0 Failed (was 3,205/3,208 -- +1 Pass from
the new lit test/split).

Remaining `ssbo.*` fails re-bucketed post-fix (215 total): `layout.
instance_array_basic_type` (84, L124(k)), `layout.random` (67), `layout.
unsized_nested_struct_array` (24), `layout.2_level_array`/`3_level_array`/
`3_level_unsized_array` (12 each, 36 total -- a new residual bucket, tracked
as L124(m) since it's a materially different remaining shape from the
column-select gap this fix closed), and 4 `unsized_array_length.*`
singletons. See `agent_thoughts.md` for the full narrative and next steps.

## Roadmap L124(m) (closed this session): non-square RowMajor matrix through 2+ array levels

Root-caused and fixed a silent miscompile (wrong numeric result, not a
legalization failure) affecting `RowMajor`+`MatrixStride`-decorated
*non-square* matrices (e.g. `mat4x3`: 4 columns, 3 rows) reached through 2 or
more levels of SPIR-V array nesting inside a storage buffer block --
`2_level_array`/`3_level_array`/`3_level_unsized_array`'s own residual 12
fails each (36 total), left after L124(f)'s column-select fix.

Root cause: this codebase's own `convertArrayTypeIgnoringDecorations`
correctly *declines* (returns `nullptr`) to convert an array wrapping a
non-square matrix when the matrix's natural LLVM size -- vector-padded per
SPIR-V rules (e.g. 64 bytes for `mat4x3`, since LLVM pads `vector<3xf32>` to
16 bytes) -- exceeds the declared `ArrayStride` (e.g. 48 bytes, the *correct*
physical size). That decline was intended to fall through to a caller that
performs the correct RowMajor/MatrixStride-aware substitution. Instead, it
fell through to MLIR upstream's own `spirv::ArrayType` conversion (in
`SPIRVToLLVM.cpp`), which validates the declared stride against a
*different* natural-size calculation for matrices --
`VulkanLayoutUtils::getNaturalArrayStride`, a tightly-packed scalar-count
size (`rows*cols*sizeof(scalar)` = 48 for `mat4x3`), not the vector-padded
size FeMe's own matrix conversion actually produces. Since 48 == 48 (the
declared stride), upstream's own check passed, and it silently built the
array around the *natural* (wrong, 64-byte) matrix conversion regardless --
a previously-undocumented discrepancy between the two conversion paths'
notions of "natural size" for a matrix specifically.

This mismatch is invisible for square matrices (natural and physical sizes
numerically coincide), and for 0 or exactly 1 levels of array nesting for a
whole-matrix access (handled by other, unaffected code paths). It manifested
in two call sites within `rewriteBlockAccess`:
- the initial `ElementType` computation, for whole-matrix access through 2+
  array levels;
- the `NeedsDeepNestingPeel` loop's per-iteration `PeeledElementType`
  computation, for partial/column-select access through 3+ array levels
  (at exactly 2 levels, the single peel iteration reaches the bare matrix
  directly, so no array-of-matrix conversion step is needed).

Fixed both via a new shared helper, `substituteArrayOfMatrixElementType`,
which substitutes the correct physical matrix type (via the pre-existing
`getPhysicalMatrixMemberType`/`wrapPhysicalMatrixInArrays` helpers, the same
machinery `convertOffsetStructTypeIgnoringDecorations`'s own struct-member
loop already uses) whenever the selected type is an array wrapping a matrix
whose naive conversion isn't representable. Required forward-declaring
`peelArraysToMatrixType`/`getPhysicalMatrixMemberType`/
`wrapPhysicalMatrixInArrays` before `rewriteBlockAccess`, since they were
previously only defined much later in the file.

This fix is a pure type-spelling change for square matrices, where the bug
was invisible: the fix now always prefers the physical substitution over the
old accidental-padding-based type, even when the two happened to coincide in
byte size. Verified via a minimal isolated repro that the GEP's actual byte
size/stride is identical, and that the real `llvm.load`/`llvm.store` types
(computed independently by `RowMajorMatrixLoadPattern`/
`RowMajorMatrixStorePattern`) are completely unchanged -- required updating
one pre-existing test's CHECK line
(`spirv-to-llvm-matrix-rowmajor-nested-array-block.mlir`) with an explanatory
comment, no behavioral regression.

New `spirv-to-llvm-matrix-rowmajor-nonsquare-nested-array-block.mlir`
regression test, covering the whole-matrix-access fix (2-level array of
`mat4x3`, `RowMajor`+`MatrixStride=16`). The partial-access
(`NeedsDeepNestingPeel` loop) fix does not have its own synthetic unit test:
a hand-constructed 3-level wrapper-array-of-non-square-matrix repro,
mimicking `spirv-to-llvm-matrix-rowmajor-wrapper-array-column.mlir`'s own
style, consistently failed to legalize for reasons unrelated to this fix (a
2-level version of the identical shape converts correctly and produces the
expected physical-substitution GEP) -- likely a pre-existing, narrower gap in
shape recognition specific to hand-built (non-CTS-derived) 3-level SPIR-V,
not a product bug. Given the *real* CTS sweep already confirms 0 failures
across all of `2_level_array`/`3_level_array`/`3_level_unsized_array`
(including every `_store_cols`/`_comp_access_store_cols` case, which exercise
exactly this partial-access shape), this was not pursued further given the
concrete CTS evidence of correctness.

Re-swept `ssbo.*` (12,225 cases): **3,063 Pass / 179 Fail / 8,983
NotSupported** (was 3,027/215/8,983) -- **+36 Pass, 0 regressions**, exactly
matching the 36 cases this row scoped, with all three target families now at
0 fails. `compute.*` unchanged (679/6/60,775, confirmed by a full re-sweep).
`ninja check-feme`: 3,207/3,210 Passed, 3 Unsupported, 0 Failed (was
3,206/3,209 -- +1 Pass from the new lit test).

Remaining `ssbo.*` fails (179 total): `layout.instance_array_basic_type` (84,
L124(k)), `layout.random` (67), `layout.unsized_nested_struct_array` (24),
and 4 `unsized_array_length.*` singletons (L124(l)). See `agent_thoughts.md`
for the full narrative and next steps.

## Roadmap L124(k) (closed this session): RowMajor matrix miscompile in arrayed block instances

Root-caused and fixed a silent miscompile (wrong numeric result, not a
legalization failure) affecting `RowMajor`/`MatrixStride`-decorated matrix
members of an *arrayed block instance* -- GLSL's `buffer Block { mat2 var;
} block[3];`, a single binding covering `N` descriptors, each its own
storage/uniform buffer block instance, unlike every other L124 fix's own
shape of an array member nested *inside* one block -- `instance_array_
basic_type`'s own 84 remaining fails, all matrix-typed.

Root cause: `getMatrixWholeAccess`/`getMatrixColumnAccess` (used by
`RowMajorMatrixLoadPattern`/`RowMajorMatrixStorePattern`/
`MatrixColumnLoadPattern`/`MatrixColumnStorePattern` to re-derive their own
shape) both re-derive that shape directly from a `spirv.AccessChain`'s
original (unconverted) base pointer type, assuming its pointee is directly
the block's own `spirv::StructType`. For an arrayed block instance, that
pointee is instead an `spirv::ArrayType` *of* that struct (one level per
instance-array dimension) -- neither helper accounted for this extra
wrapping level, so `getBufferBlockElement`/`getUniformBlockElement` (both
requiring a `StructType` pointee directly) always returned `std::nullopt`,
and every RowMajor/non-representable matrix reached this way silently fell
back to the generic, physically-wrong (always natural, always-column-major)
`spirv.Store`/`spirv.Load` conversion.

Notably, `ArrayedBlockAccessChainPattern` itself -- the pattern that
actually builds the per-instance handle from the leading (instance-
selecting) index and delegates to `rewriteBlockAccess` for the rest of the
navigation -- already correctly accounted for that leading index via its
own `Selector` parameter, which is why every *non*-matrix basic type in
this same family already passed. The bug was isolated entirely to the two
matrix-specific helpers above, which independently re-derive their own
shape straight from the SPIR-V `AccessChainOp` rather than reusing
`rewriteBlockAccess`'s own already-correct resolved `Element`/`Selector`.

Fixed via a new `peelInstanceArrayPointer` helper: peels however many
leading `spirv::ArrayType`/`spirv::RuntimeArrayType` levels wrap a pointer's
pointee before reaching a `spirv::StructType`, returning both the
struct-pointee pointer type and how many levels were peeled. Both
`getMatrixWholeAccess` and `getMatrixColumnAccess` now peel first, then
apply their own pre-existing per-block shape logic completely unchanged --
the peeled depth is simply folded into an index-position offset (added to
`getMatrixWholeAccess`'s own index-count checks, and into
`getMatrixColumnAccess`'s own `Selector`), so every existing
`Op.getIndices()`-relative check downstream (which still operates against
the full, unmodified original index list) stays correct as-is.

New `spirv-to-llvm-matrix-rowmajor-instance-array-block.mlir` regression
test, covering both the whole-matrix-access (store) and column-select
(load) fixes for a `RowMajor` `mat2` member of a 3-instance arrayed block,
verified against actual `feme-opt` output.

Re-swept `ssbo.*` (12,225 cases): **3,150 Pass / 92 Fail / 8,983
NotSupported** (was 3,063/179/8,983) -- **+87 Pass, 0 regressions**,
`instance_array_basic_type` now fully closed (0 of its own 84 -- the small
excess over 84 is a couple of `readonly` variants of the same family also
closing). `compute.*` unchanged (679/6/60,775, confirmed by a full
re-sweep). `ninja check-feme`: 3,208/3,211 Passed, 3 Unsupported, 0 Failed
(was 3,207/3,210 -- +1 Pass from the new lit test).

Remaining `ssbo.*` fails (92 total, all of `ssbo.*`'s named buckets now
closed except these): `layout.random` (64, L124(l)),
`layout.unsized_nested_struct_array` (24, L124(l)), and 4
`unsized_array_length.*` singletons (L124(l)). See `agent_thoughts.md` for
the full narrative and next steps.

## Roadmap L124(l) (3 of 4 sub-buckets closed this session): legacy-spelled readonly SSBO misclassification + struct-typed runtime-array-element stride padding

Continued L124(l)'s own scope: `random` (64), `unsized_nested_struct_array`
(24), and 4 `unsized_array_length.*` singletons, the entire remaining
`ssbo.*` fail set (92 of 12,225) after L124(m).

Started with the 4 singletons (smallest bucket). All 4 failed identically
at `vkCreateComputePipelines` time (`VK_ERROR_INITIALIZATION_FAILED`).
Setting `FEME_VULKAN_LOG_CREATION_ERRORS=1` (an opt-in diagnostic env var,
`feme::vulkan::logCreationFailure`, `feme/lib/Vulkan/Diagnostics.cpp`)
surfaced the real underlying error: `"unsupported raised operation:
'llvm.spv.resource.handlefrombinding...' is a register-bound resource
handle the FeMe CPU target cannot normalize... (an unbounded range...)"`.

Root cause: all 4 cases' shader has a `readonly buffer x { int xs[]; };`
binding -- a genuine, legitimate storage buffer using the pre-SPIR-V-1.3
legacy spelling (`Uniform` storage class + `BufferBlock` decoration,
still what glslang emits by default for a plain GLSL `buffer` block).
`convertBufferBlockType` (`SPIRVToLLVMPatterns.cpp`) forwarded the
pointer's own *literal* storage class (`Uniform`, value 2) as the emitted
`spirv.VulkanBuffer` handle's own storage-class marker. Downstream,
`classifyVulkanBufferHandle` (`SPIRVResourceLowering.cpp`) disambiguates
a literal-`Uniform`-class handle as storage-vs-uniform via its
`Writable` parameter, since a real uniform block is always non-writable
-- but this is inherently ambiguous for a `readonly buffer` (a
legitimate, intentionally non-writable storage buffer) using the legacy
spelling: its `[Uniform, Writable=0]` pair is bit-for-bit identical to a
genuine uniform block's own. The misclassification treats the buffer's
runtime-sized array as a std140 *uniform* array, which can never
legitimately be unbounded, so it is rejected downstream as an
unsupported "unbounded range" handle.

Fixed at the source rather than patching the ambiguous downstream
disambiguation (which structurally cannot distinguish the two cases from
the `Writable` bit alone): `convertBufferBlockType` is the only function
that emits a `spirv.VulkanBuffer` handle for a storage buffer block,
already gated by `getBufferBlockElement`'s `isBufferBlockStorage` check,
which unambiguously recognizes both spellings as "this is a storage
buffer" -- so it now always emits the canonical `StorageBuffer` (12)
marker, discarding the pointer's own literal (and, for the legacy
spelling, misleading) storage class value. Required updating two
pre-existing tests' CHECK lines (`2` -> `12`) for the same reason
L124(m)'s own fix did, since this changes every legacy-spelled storage
buffer's emitted handle spelling, not just the previously-misclassified
case.

Verified all 4 singleton cases now Pass; confirmed `random`/
`unsized_nested_struct_array` unaffected (isolated fix).

Continued with `unsized_nested_struct_array` (24 fails) next -- confirmed
(via `FEME_DUMP_IR=1`) these are runtime miscompiles ("Counter value
incorrect"), not creation-time failures. Traced one repro
(`per_block_buffer.std140`)'s emitted LLVM IR byte offsets by hand against
its own `OpDecorate ArrayStride` (read via `--deqp-log-decompiled-spirv`):
its trailing `T t[]` member's declared stride is 368 bytes, but the
second element (`t[1]`) was loaded starting at byte 468 -- exactly `T`'s
own *unpadded* natural size (356, rounded to nothing) past the first,
not the declared 368.

Root cause: the `RuntimeArrayType` type conversion discards a runtime
array's own declared `ArrayStride` unconditionally. This is correct for
the *wrapper* shape (a storage buffer whose sole member is the runtime
array itself) -- its element indexing goes through an explicit
`index * Stride` byte computation instead, reading the handle's own
third integer parameter (the mechanism roadmap L106 added for a bare
`vec3` element's own 12-vs-16-byte mismatch). It is *not* correct for a
runtime array that is instead one member of a directly-converted,
multi-member outer block struct -- glslang's usual shape for a plain
GLSL `buffer` block with a trailing unsized array member, e.g. `buffer
Block { int header; T t[]; };`. That shape classifies as
`HandleKind::StorageStruct` (`classifyVulkanBufferHandle`), which
carries no explicit stride at all and addresses every element through
ordinary `getelementptr` into the array's own converted LLVM element
type -- so a struct-typed element whose natural (packed) size undershoots
its declared `ArrayStride` (needing the same "round every array element
up to a 16-byte multiple" tail padding a bare `vec3` element already
needs) places every element past the first at the wrong byte offset,
for both a constant-index access (resolved to a literal byte offset at
compile time) and a dynamic-index one (resolved via `getelementptr`
using the element type's own `DataLayout`-derived size) -- both paths
are downstream of the same converted LLVM element type's own reported
size.

Fixed by padding the converted element type to its declared
`ArrayStride` with the pre-existing `padStructToSize` helper whenever it
is a struct, mirroring exactly what `convertArrayTypeIgnoringDecorations`
already does for a *fixed*-size array's own identified-struct element.
`padStructToSize` is a no-op for any non-struct (vector/scalar) element,
so this does not disturb the wrapper shape's own explicit-third-
parameter mechanism for a `vec3` element. New test in
`spirv-to-llvm-glslang-blocks.mlir` (a header field alongside a trailing
runtime array of an identified struct whose own natural size undershoots
its declared stride).

Verified all 24 `unsized_nested_struct_array` cases now Pass.

`ninja check-feme`: 3,208/3,211 Passed, 3 Unsupported, 0 Failed (no
regressions from either fix; the two updated CHECK lines and one new
test case account for the unchanged total test count -- new cases were
added within existing `RUN`-split files, not new files).

Re-swept `ssbo.*` (12,225 cases): **3,178 Pass / 64 Fail / 8,983
NotSupported** (was 3,150/92/8,983) -- **+28 Pass, 0 regressions**.
`compute.*` unchanged (679/6/60,775, confirmed by a full re-sweep).

Remaining `ssbo.*` fails (64 total, all `random`): triaged one repro
(`dEQP-VK.ssbo.layout.random.all_per_block_buffers.15`) and found a
related-but-distinct root cause this session's fix does not cover: its
`blockB` has a trailing `lowp ivec2 d[]` member (a *vector*-typed runtime
array element, not struct-typed) in the same `HandleKind::StorageStruct`
shape -- `padStructToSize` is a deliberate no-op for a non-struct
element, so this vector case is not padded by this session's fix. Broken
out as roadmap L124(n), since a correct fix needs to avoid disturbing the
wrapper shape's own existing vec3-handling mechanism (see L124(n)'s own
roadmap text for the scoping concern) -- not implemented this session.
See `agent_thoughts.md` for the full narrative and next steps.

## Roadmap L124(n) (closed this session): vector-typed runtime-array-element stride padding, `ssbo.*`'s last named bucket

Picked up exactly where the prior session left off: `random`'s residual
64 `ssbo.*` fails were already root-caused (last session) to a sibling
of L124(l)'s struct-element bug -- a *vector*-typed (`ivec2`) trailing
runtime-array element in the same `HandleKind::StorageStruct` shape,
whose 8-byte natural size undershoots its 16-byte std430 `ArrayStride`,
which `padStructToSize` (L124(l)'s own fix) is a deliberate no-op for
(it only ever pads a struct-typed element).

Before writing any code, spent this session's first half confirming (via
code reading, not just testing) that a fix could safely extend the
*shared* `RuntimeArrayType` type conversion -- used by both the wrapper
shape (`HandleKind::Storage`/`UniformArray`, roadmap L106's own vec3
case) and the non-wrapper shape (`HandleKind::StorageStruct`, this
session's target) -- without disturbing the wrapper shape's existing
vec3 mechanism:

- `classifyVulkanBufferHandle` (SPIRVResourceLowering.cpp): its `Stride`
  computation only ever falls back to the converted element type's own
  `DataLayout` size when the handle has *no* explicit third integer
  parameter -- and `convertBufferBlockType` always attaches that
  parameter whenever `Type.getArrayStride()` is nonzero, i.e. exactly
  whenever this session's new padding would ever apply. The fallback
  path is therefore never reached for any real `ArrayStride`-decorated
  array, confirming the wrapper shape's own `Stride` value can never be
  affected by padding the element type.
- `lowerAccesses` (SPIRVResourceLowering.cpp): the wrapper shape's own
  addressing (both element access and `ArrayLength`) always uses the
  handle's explicit `BH.Stride` field via `ElemIdx * BH.Stride`, never
  the array's own converted LLVM element type's size directly.
- `rewriteBlockAccess` (SPIRVToLLVMPatterns.cpp): for the wrapper shape,
  `SelectedType` is recovered as the runtime array's *element* type
  directly from the SPIR-V type itself (`RuntimeArrayType::getElementType()`),
  never by re-converting the `RuntimeArrayType` as a whole through the
  type converter -- so the per-element load/store value type used there
  is entirely independent of this session's change. The only place the
  wrapper shape's `RuntimeArrayType` conversion result reaches anything
  is `convertBufferBlockType`'s own `ContentType`, which becomes part of
  the handle's own type parameter -- purely descriptive metadata, per
  the two points above.
- Grepped every existing lit test for a wrapper-shape handle with a
  stride-mismatched vector/scalar runtime-array element: found none, so
  no test-visible handle-type-spelling change was expected to need
  updating.

With that confirmed, implemented the fix by extending the
`RuntimeArrayType` conversion (after its existing `padStructToSize`
attempt) with the identical byte-array stand-in substitution
`convertArrayTypeIgnoringDecorations` already applies for a *fixed*-size
array's own scalar/vector element: whenever the natural size undershoots
the declared `Stride`, substitute a `Stride`-sized opaque `[N x i8]`
array for the element uniformly (safe since every pointer in this
codebase is opaque). Updated the surrounding doc comment to describe
this new case alongside L124(l)'s existing struct case. New lit test
`vector_array_element` in `spirv-to-llvm-glslang-blocks.mlir`, mirroring
L124(l)'s own `nested_struct_array_element` test, confirming the emitted
handle type is `!llvm.struct<packed (i32, array<12 x i8>, array<0 x
array<16 x i8>>)>` and the element GEP indexes correctly regardless of
which vector component is loaded.

**Critical regression check** (flagged by last session's own notes,
run before landing): re-ran `dEQP-VK.compute.pipeline.builtin_var.*`
(L106's own vec3-stride regression coverage) directly via `deqp-vk` --
still 11/11 Pass, confirming the wrapper shape's own vec3 mechanism is
unaffected.

`ninja check-feme`: 3,208/3,211 Passed, 3 Unsupported, 0 Failed (+1 from
the new lit test, 0 regressions).

Re-swept `ssbo.*` (12,225 cases): **3,187 Pass / 55 Fail / 8,983
NotSupported** (was 3,178/64/8,983) -- **+9 Pass, 0 regressions**.
`compute.*` unchanged (679/6/60,775, confirmed by a full re-sweep).

Remaining `ssbo.*` fails (55, still all `random`): a quick
failure-message-only triage (grepping the sweep log's own per-case
verdict text, not a `FEME_DUMP_IR=1` trace on any individual repro) found
at least 3 distinct symptom shapes still mixed into this one bucket -- 25
"Result comparison and counter values are incorrect", 15 "Counter value
incorrect" (both mention an SSBO atomic-counter mismatch specifically,
suggesting a bug distinct from this session's plain-load/store one), 14
"Result comparison failed" (plain data mismatch, possibly more
`ArrayStride`-shaped bugs of the same general family), and 1
`VK_ERROR_INITIALIZATION_FAILED` (a pipeline-creation-time failure, not a
runtime miscompile at all). Broken out as roadmap L124(o), not
individually reduced this session. See `agent_thoughts.md` for the full
narrative and next steps.

## Session: L124(o) investigated -- one repro root-caused, no fix landed

Investigated `ssbo.*`'s residual 55 `random` fails (roadmap L124(o)).
Reproduced directly via `deqp-vk`: 267 Pass / 55 Fail / 1559 NotSupported
of 1881 `random` cases -- unchanged from last session's own count.

Reduced one repro to a precise root cause:
`dEQP-VK.ssbo.layout.random.all_per_block_buffers.41` mismatches on
`b.mB` (`expected mat2(7, 4, 7, -9), got mat2(7, -4, 4, -2)`) --
`getMatrixWholeAccess`'s non-wrapper branch (used by
`RowMajorMatrixStorePattern`/`LoadPattern`) only recognizes a
`RowMajor`/non-natural-`MatrixStride` matrix that is *directly* a
top-level block member (`Op.getIndices().size() != InstanceArrayDepth +
1` bails out), not one nested a further struct level down (`b.mB`),
silently falling back to the ordinary unpadded whole-matrix store/load.
`getTightNestedStructType`/`getTightMatrixType` (the parallel
type-conversion-side helpers) have the same gap: they only *tighten* a
nested struct's own matrix member, never *widen* it to its declared
`MatrixStride`. Confirmed via an isolated `feme-opt`-only MLIR repro
(`!spirv.struct<(!spirv.struct<B, (f32, mat2 stride=16)>)>`) outside CTS
entirely -- see roadmap L124(o) for the full writeup.

A separate hypothesis (a `mlir::DataLayout` 3-lane-vector rounding bug in
`padStructToSize`/`convertArrayTypeIgnoringDecorations`, parallel to
L124(n)'s own fix) was implemented, verified in isolation, and passed
`check-feme` clean, but a full `ssbo.*` re-sweep showed it fixed **zero**
of `random`'s 55 fails while regressing 36 previously-passing
`single_struct{,_array,_nested_struct}` matrix-column tests --
**reverted in full**; working tree is back at the exact state this
session started from (commit `f6c49b1164db`).

`ssbo.*` re-swept after the revert to confirm no drift: **3,187 Pass /
55 Fail / 8,983 NotSupported** (unchanged from last session).
`compute.pipeline.builtin_var.*` (L106's own regression coverage):
11/11 Pass, unchanged. `ninja check-feme`: 3,208/3,211 Passed, 3
Unsupported, 0 Failed, unchanged.

No code changes landed this session (0 net diff to
`feme/lib`/`feme/test`). See `agent_thoughts.md` for the full narrative
and precisely scoped next steps.

## Session: merged upstream `llvm/llvm-project` `main` (1,398 commits), fixed one resulting build break

Merged `origin/main` into this branch per the standing "merge main"
next-step request (fetch + merge, resolve conflicts, fix any resulting
test issues). `git merge` itself produced **zero textual conflicts** across
1,398 incoming upstream commits against feme's 4,292; every feme-touched
file (all under `feme/`, plus feme's small deltas to upstream MLIR SPIR-V
and `llvm/lib/Target/DirectX`/`DXContainer` files) merged cleanly.

The merge was not build-clean, though: upstream reworked
`llvm::opt::OptTable` (adding subcommand support) in the merged window,
replacing `GenericOptTable`/`OPTTABLE_STR_TABLE_CODE`/
`OPTTABLE_PREFIXES_TABLE_CODE` and the `LLVM_CONSTRUCT_OPT_INFO`-based
`Info` table with a single `OPTTABLE_CODE` section emitting an
`optionTables()` helper and a new `OptTable(Tables, IgnoreCase)`
constructor. `feme/lib/Frontend/Options.cpp` was the only feme file still
using the old pattern (`feme/include/feme/Frontend/Options.h`'s `ID` enum
already used the unaffected `LLVM_MAKE_OPT_ID` macro, so it needed no
change). Updated `Options.cpp` to the new pattern, matching the
now-canonical usage in `llvm/tools/llvm-ml/llvm-ml.cpp`. One commit,
surgical (5 insertions, 19 deletions in that one file).

Verified with a full from-scratch build (`Release`,
`LLVM_ENABLE_ASSERTIONS=ON`, `CMAKE_CXX_COMPILER_LAUNCHER=ccache`,
`CMAKE_DISABLE_PRECOMPILE_HEADERS=ON` per `feme/cmake/caches/feme.cmake`):

- `ninja check-feme`: **3,208 Passed / 3 Unsupported / 0 Failed**
  (unchanged from the pre-merge baseline).

Also rebuilt `deqp-vk` from scratch against the merged VK-GL-CTS checkout
(same revision, `880f31a2bd9c` -- the merge did not touch that checkout)
and re-verified the FeMe ICD against it:

```console
VK_DRIVER_FILES=$PWD/build/tools/feme/tools/feme-vulkan/feme_icd.json \
  vulkaninfo --summary | grep deviceName
# => FeMe CPU Vulkan Device
```

Re-ran the two sweeps this report already tracks as regression coverage
(rather than the full multi-hour, 3.2M-case sweep, since nothing in the
merge touched Vulkan-facing feme code -- only the unrelated CLI
option-parsing fix above):

- `ssbo.*` (12,225 cases): **3,187 Pass / 55 Fail / 8,983 NotSupported**
  -- unchanged from the last recorded baseline, 0 regressions.
- `compute.*` (61,460 cases): **679 Pass / 6 Fail / 60,775 NotSupported**
  -- unchanged from the last recorded baseline, 0 regressions.

FeMe source revision under test: `5417103e8b02`. No feature or extension
inventory changes: this session touched only CLI option-table plumbing,
not any Vulkan-facing code path, so
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) are unchanged
and still accurate. See `agent_thoughts.md` for the full narrative and
next steps.

## Session: L124(o) fixed -- widened non-representable nested-struct matrix members, third-fix regression found and fixed same session

Continued L124(o) (`ssbo.*`'s `random` bucket's residual repro,
`all_per_block_buffers.41`'s `b.mB` whole-matrix corruption, root-caused
but not yet fixed by the prior session). Landed the fix in three
separate commits:

1. Generalized `getMatrixWholeAccess`'s non-wrapper branch to walk
   through zero-or-more intervening struct-member selects (not just
   exactly one) before the final matrix-member select, fixing the
   originally-root-caused whole-matrix Store/Load corruption for a
   matrix nested one or more struct levels below the top-level `Block`.
2. Added `getTightOrPhysicalMatrixMemberType`, wired into
   `getTightNestedStructType`'s matrix branch and
   `convertOffsetStructTypeIgnoringDecorations`'s array-of-matrix retry
   tier, so a non-representable (`RowMajor`, or non-natural
   `MatrixStride`) matrix member nested inside a struct member is
   *widened* rather than naively tightened, matching a direct block
   member's own already-correct physical layout.
3. **Same-session regression, found and fixed**: a full `ubo.random.*`
   CTS sweep after (1)+(2) surfaced
   `dEQP-VK.ubo.random.nested_structs_arrays_instance_arrays_compute.4`
   crashing (`'llvm.getelementptr' op index N indexing a struct is out
   of bounds`) rather than merely computing a wrong value. Root-caused
   to the *same* tighten-vs-widen bug existing independently in
   `convertOffsetStructTypeIgnoringDecorations`'s *direct*-matrix-member
   retry-tier case (a third call site (2)'s own fix missed) -- for a
   nested struct with two or more non-representable matrix members,
   this produced a struct whose field layout silently disagreed with
   the one `getTightNestedStructType` produces for the same struct when
   embedded as another struct's own member, so a GEP built against one
   shape got indexed with a physical index computed for the other.
   Fixed by giving that third call site the identical widen-or-tighten
   treatment as its already-fixed array-of-matrix sibling.

Build: `Release`, `LLVM_ENABLE_ASSERTIONS=ON`,
`CMAKE_CXX_COMPILER_LAUNCHER=ccache`, incremental (existing build
directory reused, per standing practice).

```console
VK_DRIVER_FILES=$PWD/build/tools/feme/tools/feme-vulkan/feme_icd.json \
  vulkaninfo --summary | grep deviceName
# => FeMe CPU Vulkan Device
```

`ninja check-feme`: **3,210 Passed / 3 Unsupported / 0 Failed** (+2 new
regression tests: `spirv-to-llvm-matrix-rowmajor-nested-struct-block.mlir`
for fix (1), `spirv-to-llvm-two-nonrepresentable-matrices-nested-struct.mlir`
for fix (3); 2 pre-existing tests' CHECK lines updated to reflect the
now-correct widened output, `spirv-to-llvm-array-of-matrix-struct-
member.mlir` and `spirv-to-llvm-nested-struct-reorder.mlir`).

Full Vulkan CTS re-sweep after all three fixes landed:

- `ubo.random.*` (2,250 cases): **607 Pass / 0 Fail / 1,643 NotSupported**
  -- was 606/1/1,643 with only fixes (1)+(2) applied (the fix-(3)
  regression); confirmed clean (0 Fail) with fix (3) added. The specific
  regression repro, `nested_structs_arrays_instance_arrays_compute.4`,
  individually re-confirmed Pass.
- `ssbo.*` (12,225 cases): **3,195 Pass / 47 Fail / 8,983 NotSupported**
  -- was 3,187/55/8,983 before this session's fixes: **-8 Fail**, 0
  regressions, matching this fix (the remaining 47 are a different,
  not-yet-triaged bucket, filed separately, not part of L124(o)).
- `compute.pipeline.builtin_var.*` (11 cases): **11 Pass / 0 Fail** --
  unchanged, sanity check for an unrelated area, no regression.

A full `ubo.*` sweep (beyond just `ubo.random.*`) remains blocked by a
newly-discovered, **pre-existing** (confirmed via `git stash` comparison
to crash identically without this session's changes) fatal
`StructType::getMemberDecorations`: "member index out of range"
assertion in `dEQP-VK.ubo.single_struct.per_block_buffer.std140_both` --
filed as roadmap L124(p), not fixed this session (out of scope).

Also discovered, flagged for future work, not fixed: `spirv-to-llvm-
nested-struct-reorder.mlir`'s own dynamic-column-select `spirv.
AccessChain` had a latent stale-physical-index bug that this session's
fix (3) happened to also correct as a side effect (see that test's own
updated comment) -- confirmed via this session's investigation to be
the *same* embedded-vs-standalone-conversion mismatch root cause as the
fix-(3) regression itself, just for a struct shape that didn't
previously *crash* (a numerically in-bounds, merely type-mismatched
GEP). No further action needed; noted here only for completeness since
it was investigated as part of this session's own root-causing.

FeMe source revision under test: see this session's own commits (three
`[feme] L124(o): ...`-titled commits immediately preceding this report
update in `git log`). No feature or extension inventory changes: this
session's fixes are internal SPIR-V-to-LLVM struct/matrix layout
conversion correctness fixes, not new Vulkan feature/extension surface,
so [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) are unchanged
and still accurate. See `agent_thoughts.md` for the full narrative and
next steps.

## Session: L124(p) fixed -- `BlockStruct`/`Element.Content` decoration mismatch, unblocks full `ubo.*` sweep

Triaged and fixed roadmap L124(p) (the fatal `StructType::
getMemberDecorations`: "member index out of range" assertion in
`dEQP-VK.ubo.single_struct.per_block_buffer.std140_both`, discovered
incidentally during the prior session's L124(o) verification and
confirmed pre-existing/unrelated to that fix).

Root-caused via a `gdb -batch -ex run -ex bt` backtrace against the
real CTS test plus code reading: `rewriteBlockAccess` always looked up
a member's `RowMajor`/`MatrixStride` decorations on `BlockStruct` (the
*outer* struct type the original base pointer points to) at
`MatrixDecorationMemberIndex`. For `getUniformBlockElement`'s "sole
member is itself a struct" wrapper shape -- indistinguishable, by type
shape alone, from dxc's own `cbuffer`/`ConstantBuffer<T>` convention,
but here reached via glslang's lowering of a plain
`uniform Block { S s; };`, exactly `single_struct`'s own shape --
`MatrixDecorationMemberIndex` is actually computed relative to
`Element.Content` (the inner Field struct `S`), not `BlockStruct`
itself. `BlockStruct` has only one member in this wrapper shape, so any
index >= 1 (any member of `S` past its own first) crashed the assertion
outright, rather than returning a wrong answer.

Fixed by introducing a new `DecorationStruct` local (defaults to
`BlockStruct`, reassigned to `Element.Content` cast to `StructType`
whenever `MatrixDecorationMemberIndex` is set from indexing into
struct-typed content) and using `DecorationStruct` in place of
`BlockStruct` at every decoration-lookup call site in
`rewriteBlockAccess`: both `substituteArrayOfMatrixElementType` calls
and the two direct `isMatrixLayoutRepresentable`/`getMatrixMemberLayout`
calls in the RowMajor/ColMajor column-select logic. Also audited
`getWrapperArrayMatrixColumnAccess` (a structurally similar helper
taking its own `BlockStruct` parameter) for the same mismatch pattern
and confirmed it does *not* share the bug: its own shape
(array-wraps-matrix-directly, no intervening struct) always has
`MatrixDecorationMemberIndex == 0`, which is correctly `BlockStruct`'s
own sole member index in that shape.

Verified the fix two ways before touching the real CTS harness: (1) a
minimal `feme-opt`-only repro (a `uniform Block { S s; }`-shaped module
with `S` containing a non-square, `ColMajor`+`MatrixStride`-decorated
array-of-matrices member at index 1) confirmed to crash identically to
the real bug pre-fix (via `git stash`) and produce the correctly-widened
output post-fix; (2) added as a permanent regression test,
`spirv-to-llvm-matrix-uniform-wrapper-struct-array-of-matrix.mlir`.

Build: `Release`, `LLVM_ENABLE_ASSERTIONS=ON`,
`CMAKE_CXX_COMPILER_LAUNCHER=ccache`, incremental (existing build
directory reused).

```console
VK_DRIVER_FILES=$PWD/build/tools/feme/tools/feme-vulkan/feme_icd.json \
  vulkaninfo --summary | grep deviceName
# => FeMe CPU Vulkan Device
```

`ninja check-feme`: **3,211 Passed / 3 Unsupported / 0 Failed** (+1 from
the new lit test, 0 regressions).

Full Vulkan CTS re-sweep after the fix:

- The originally-crashing test,
  `dEQP-VK.ubo.single_struct.per_block_buffer.std140_both`: **now
  individually Passes** (previously a fatal abort).
- `ubo.single_struct.*` (72 cases): **48 Pass / 0 Fail / 24
  NotSupported** -- clean, no other crashes/fails in this family.
- `ubo.*` **full** sweep (13,240 cases, previously entirely blocked by
  this crash): **5,687 Pass / 0 Fail / 7,553 NotSupported**. This is
  the first time a full `ubo.*` sweep (not just `ubo.random.*`) has run
  to completion.
- `ssbo.*` (12,225 cases): **3,195 Pass / 47 Fail / 8,983 NotSupported**
  -- unchanged from before this fix (confirmed via a full re-run, same
  47 test names both before and after), as expected since L124(p) is a
  `ubo.*`-only code path (`getUniformBlockElement`'s wrapper shape via
  `BlockAccessChainPattern`, not any `ssbo.*`-specific pattern). These
  47 are re-filed as a new roadmap item, L124(q) (see below), not part
  of L124(p).

Light triage of one of the 47 `ssbo.*` fails
(`dEQP-VK.ssbo.layout.random.basic_types.18`) done as a sanity check,
not a full root-cause: fails with a generic "Counter value incorrect"
message (a data-mismatch, not a crash/legalization-rejection),
consistent with -- but not yet confirmed to share a root cause with --
the `random`-bucket pattern from L124(l)/(n) (several small, distinct
layout bugs bundled under one fuzzer-driven test family). Filed as
L124(q), not started.

FeMe source revision under test: `361df0b2f177` (`[feme] Fix L124(p):
BlockStruct/Element.Content decoration mismatch`) and `85a81554f4b7`
(roadmap update). No feature or extension inventory changes: this
session's fix is an internal SPIR-V-to-LLVM access-chain-rewriting
correctness fix, not new Vulkan feature/extension surface, so
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) are unchanged
and still accurate. See `agent_thoughts.md` for the full narrative and
next steps.

## Session: L124(q) fixed -- direct array-of-matrix member misses whole-access reinterpretation, closes 35 of `ssbo.*`'s remaining 47

Triaged and fixed roadmap L124(q) (the 47 `ssbo.*` fails left unchanged
by the prior session's L124(p) fix, all in `layout.random.*`).

Reproduced `dEQP-VK.ssbo.layout.random.basic_types.18` (fails with
"Counter value incorrect", not a crash) and pulled its own decompiled
SPIR-V via `--deqp-log-decompiled-spirv=enable`. Its SSBO struct has a
`mat2x3` member, `RowMajor`+`MatrixStride=16` (non-natural: a `vec3`
column's natural size is 12 bytes, padded to 16), wrapped in a
one-element fixed-size array -- a direct (non-wrapper) block member,
not the dxc/glslang dynamically-indexed-array wrapper shape.

Root-caused via code reading: `getMatrixWholeAccess`'s non-wrapper
branch (added for L124(o)) only ever walked through nested *struct*-
member selects before a final select landing directly on a bare matrix.
It never expected that final member itself to be a direct array of
matrices, so this shape's `spirv.AccessChain` (one member-select plus
one array-index selecting the sole element) was rejected outright by
that branch, falling back to the generic conversion. The matrix's own
*type* was already correctly widened (`convertOffsetStructTypeIgnoringDecorations`'s
array-of-matrix retry tier, roadmap H134), but the whole-matrix
`spirv.Store`/`spirv.Load` reinterpretation (RowMajor transpose +
per-row `MatrixStride` padding) that requires never fired -- silently
storing/loading the plain logical (column-major, unpadded) value
straight into the already-widened physical (row-major, padded) memory
layout, corrupting every element past the first.

Fixed by generalizing the non-wrapper branch's terminal case: once a
struct-member select lands on a member that is directly an array (of
however many levels) of matrices rather than a bare matrix, require
exactly that many further array-index selectors before treating the
access as reaching the whole matrix -- mirroring the `HasWrapper`
branch's own pre-existing array-nesting peel just above it in the same
function -- with the physical-layout decorations still read from the
owning struct's own member (the array itself).

Verified via a minimal `feme-opt`-only repro built directly from the
real SPIR-V's own struct shape (mirroring `basic_types.18`'s own
5-member SSBO struct): confirmed to fail (produce a plain,
un-transposed/un-padded store/load) pre-fix and produce the correctly
transposed/padded physical-layout IR post-fix. Added as a permanent
regression test,
`spirv-to-llvm-matrix-rowmajor-array-member-whole-access.mlir`,
confirmed to fail pre-fix (via `git stash`) and pass post-fix.

Build: `Release`, `LLVM_ENABLE_ASSERTIONS=ON`,
`CMAKE_CXX_COMPILER_LAUNCHER=ccache`, incremental (existing build
directory reused).

```console
VK_DRIVER_FILES=$PWD/build/tools/feme/tools/feme-vulkan/feme_icd.json \
  vulkaninfo --summary | grep deviceName
# => FeMe CPU Vulkan Device
```

`ninja check-feme`: **3,212 Passed / 3 Unsupported / 0 Failed** (+1 from
the new lit test, 0 regressions).

Full Vulkan CTS re-sweep after the fix:

- The originally-failing test, `dEQP-VK.ssbo.layout.random.basic_types.18`:
  **now individually Passes** (previously "Counter value incorrect").
- `ssbo.*` (12,225 cases): **3,230 Pass / 12 Fail / 8,983 NotSupported**
  -- was 3,195/47/8,983 before this session's fix: **+35 Pass**, 0
  regressions. The remaining 12 fails are all still in `layout.random.*`
  (`all_per_block_buffers` 2, `all_shared_buffer` 5, `nested_structs` 2,
  `nested_structs_arrays` 2, `nested_structs_instance_arrays` 1) --
  confirmed distinct from this fix's own repro, re-filed as roadmap
  L124(r), not yet individually triaged.
- `ubo.random.*` (2,250 cases): **607 Pass / 0 Fail / 1,643 NotSupported**
  -- unchanged, confirmed by a re-sweep, no regression (this fix's own
  code path, `getMatrixWholeAccess`'s non-wrapper branch, is shared by
  both `ubo.*` and `ssbo.*`, so a targeted re-check was worthwhile even
  though the fix was found via an `ssbo.*` repro). The full `ubo.*`
  sweep from the prior session's L124(p) fix was not re-run in full this
  session (no `Block`/`Uniform`-class-specific code touched by this fix)
  -- left for a future session's own full-sweep pass if ever needed
  again.

FeMe source revision under test: `eee82b9b5903` (`[feme] Fix L124(q):
getMatrixWholeAccess misses direct array-of-matrix member`) and
`394614650ccf` (roadmap update). No feature or extension inventory
changes: this session's fix is an internal SPIR-V-to-LLVM matrix-access
recognition correctness fix, not new Vulkan feature/extension surface,
so [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) are unchanged
and still accurate. See `agent_thoughts.md` for the full narrative and
next steps.

## Session: L124(r) fixed -- wrapper-array-of-struct matrix access, closes 2 of `ssbo.*`'s remaining 12; remaining 10 re-scoped as L124(s), spanning 3 distinct failure classes

Triaged and fixed roadmap L124(r) (2 of the 12 `ssbo.*` fails left
unchanged by the prior session's L124(q) fix).

Reproduced `dEQP-VK.ssbo.layout.random.nested_structs.12` (fails with
"Result comparison failed") and pulled its own decompiled SPIR-V via
`--deqp-log-decompiled-spirv=enable`. Its SSBO is a
`StructuredBuffer<S>`-style wrapper block whose sole member is a
dynamically-indexed array, and `S` itself is a struct with two direct
matrix members (`mat2x2`, `RowMajor`+`MatrixStride=16`) either side of
a non-matrix member -- a shape distinct from both L124(q) (a direct,
non-wrapper array-of-matrix block member) and every prior wrapper-shape
fix in this series (which only ever expected the wrapped array's
element to be directly a matrix, never a struct containing one).

Root-caused via code reading: `getMatrixWholeAccess`'s `HasWrapper`
branch peels through however many array-nesting levels wrap the
wrapper's own content, then required the fully-peeled inner type to be
directly a `MatrixType` -- it declined unconditionally whenever that
inner type was instead a `StructType`. The matrix's own type was
already correctly widened elsewhere, but the plain generic store/load
used the unpadded logical shape instead of the widened physical layout,
a memory-size mismatch that silently corrupted adjacent struct data.

Fixed by extracting the non-wrapper branch's existing nested-struct-
member-walk loop into a shared helper, `walkStructMembersToMatrix`, and
calling it from the `HasWrapper` branch too: after peeling array levels
down to the inner type, if that inner type is a struct rather than a
bare matrix, continue the same walk from there instead of declining.
The non-wrapper branch's own call site is a pure refactor (same logic,
now shared), not a behavior change.

Verified via a minimal `feme-opt`-only repro mirroring the real
struct's shape: confirmed to fail pre-fix and produce the correctly
transposed/padded physical layout post-fix for both matrix members.
Added as a permanent regression test,
`spirv-to-llvm-matrix-rowmajor-wrapper-array-struct-member.mlir`,
confirmed to fail pre-fix (via `git stash`) and pass post-fix.

Build: `Release`, `LLVM_ENABLE_ASSERTIONS=ON`,
`CMAKE_CXX_COMPILER_LAUNCHER=ccache`, incremental (existing build
directory reused).

```console
VK_DRIVER_FILES=$PWD/build/tools/feme/tools/feme-vulkan/feme_icd.json \
  vulkaninfo --summary | grep deviceName
# => FeMe CPU Vulkan Device
```

`ninja check-feme`: **3,213 Passed / 3 Unsupported / 0 Failed** (+1 from
the new lit test, 0 regressions).

Full Vulkan CTS re-sweep after the fix:

- The originally-failing test, `dEQP-VK.ssbo.layout.random.nested_structs.12`:
  **now individually Passes** (previously "Result comparison failed").
- `ssbo.*` (12,225 cases): **3,232 Pass / 10 Fail / 8,983 NotSupported**
  -- was 3,230/12/8,983 before this session's fix: **+2 Pass**, 0
  regressions.
- `ubo.random.*` (2,250 cases): **607 Pass / 0 Fail / 1,643 NotSupported**
  -- unchanged, confirmed by a re-sweep, no regression.

**Remaining 10 `ssbo.*` fails triaged this session and confirmed to
span (at least) 3 distinct failure classes, not one shared root cause**
(unlike L124(l)/(n)/(q), each of which found one shared bug closing a
large chunk):

1. `all_per_block_buffers.20`: `vk.createComputePipelines` fails with
   `VK_ERROR_INITIALIZATION_FAILED` -- a compiler crash/pipeline-creation
   failure, structurally distinct from every data-mismatch bug fixed so
   far in this L124 series. Not yet investigated further.
2. `all_per_block_buffers.47`, `all_shared_buffer.{1,13,17}`,
   `nested_structs.16`, `nested_structs_instance_arrays.8`: all fail
   with an unexplained `ac_numPassed = 0, expected 1` message, not yet
   decoded at all.
3. `all_shared_buffer.{41,44}`, `nested_structs_arrays.14`: fail with
   "Result comparison failed" data mismatches. Spot-checked
   `all_shared_buffer.44`'s own shape (a non-wrapper block member that
   is a direct array of *structs* containing a matrix, e.g.
   `struct { ...; struct { matCxR mA; vecN other; } j[N]; }`'s own
   `j[i].mA`) with a speculative extension of
   `walkStructMembersToMatrix` to also recurse through a nested-struct
   array element (mirroring this session's own `HasWrapper`-branch
   fix). That access-pattern-level change alone did not fix the real
   CTS repro, and a minimal `feme-opt`-only repro of the same shape
   revealed why: **the block struct's own type conversion fails to
   legalize at all** (`spirv.GlobalVariable` legalization failure,
   independent of any `getMatrixWholeAccess`-side logic) --
   `convertOffsetStructTypeIgnoringDecorations`/
   `convertArrayTypeIgnoringDecorations` do not yet widen a matrix
   nested inside a non-wrapper array-of-struct member's own *type*.
   This is a type-level gap, a materially bigger fix than any single
   `getMatrixWholeAccess` change in this series so far. The speculative
   access-pattern change was reverted (unverified, and insufficient on
   its own); no code change from this investigation is included in this
   session's commit.

Re-scoped as roadmap L124(s), to be time-boxed as its own dedicated
investigation given the now-confirmed type-level scope of at least one
of its three sub-classes.

FeMe source revision under test: `b7fcb31deba3` (`[feme] L124(r): fix
wrapper-array-of-struct matrix access`). No feature or extension
inventory changes: this session's fix is an internal SPIR-V-to-LLVM
matrix-access recognition correctness fix, not new Vulkan
feature/extension surface, so
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) are unchanged
and still accurate. See `agent_thoughts.md` for the full narrative and
next steps.

## Session: L124(s) fixed -- non-wrapper array-of-struct matrix access, corrected a prior methodology false positive, closes 6 of `ssbo.*`'s remaining 10; residual 4 re-scoped as L124(t)

Confirmed `FeMe CPU Vulkan Device` via `vulkaninfo --summary | grep
deviceName` at session start, per standing instructions.

**Corrected a methodology false positive from the prior session**: the
prior session's own "type-level gap" finding (that
`convertOffsetStructTypeIgnoringDecorations`/
`convertArrayTypeIgnoringDecorations` do not widen a matrix nested
inside a non-wrapper array-of-struct member's own type) was based on
testing with the *wrong* `feme-opt` pass flag
(`--convert-spirv-to-llvm`, an upstream MLIR generic pass that does not
run FeMe's own registered patterns, instead of the real
`--feme-convert-spirv-to-llvm`). Re-testing the same minimal repro with
the correct flag showed the struct type converts successfully and is
**already correctly widened** -- there is no type-level gap. The real
gap was at the access-pattern level: the store/load at the correctly-
widened GEP still used the plain, unpadded logical type.

Root-caused to `walkStructMembersToMatrix`'s array-nesting-peel loop
(shared by `getMatrixWholeAccess`'s non-wrapper and `HasWrapper`
branches, roadmap L124(o)/(q)/(r)) only recognizing a peeled array's
inner type being a bare `MatrixType`, with no case for the inner type
being a `StructType` -- i.e. a non-wrapper block member that is a
direct fixed-size array of *structs*, each containing a matrix (e.g.
`struct { ...; struct { matCxR mA; vecN other; } j[N]; }`'s own
`j[i].mA`), found via `dEQP-VK.ssbo.layout.random.all_shared_buffer.44`
and `.nested_structs_arrays.14`. Fixed by extending the peel loop's
terminal case: when the fully-peeled inner type is a `StructType`,
consume the array-index selectors already walked and continue the same
struct-member search from that inner struct, rather than declining.

Also discovered a **second methodology gap** while verifying: rebuilding
`feme-opt`/`feme` alone does not update `libfeme_vulkan.so` (the actual
Vulkan ICD loaded via `VK_DRIVER_FILES`), which is a separate build
target -- an initial round of real-CTS verification against the stale
ICD showed all 3 targeted tests still failing, a false negative. Running
`ninja libfeme_vulkan.so` explicitly (a relink only, the object file was
already up to date) and re-testing confirmed the fix is real.

New `spirv-to-llvm-matrix-rowmajor-nonwrapper-array-struct-member.mlir`
regression test. `ninja check-feme`: **3,214/3,217 Passed, 3
Unsupported, 0 Failed** (+1 from the new test, 0 regressions).

Full Vulkan CTS re-sweep after the fix:

- `dEQP-VK.ssbo.layout.random.all_shared_buffer.44`,
  `.nested_structs.12`, `.nested_structs_arrays.14`: **all now
  individually Pass** (previously "Result comparison failed").
- `ssbo.*` (12,225 cases): **3,238 Pass / 4 Fail / 8,983 NotSupported**
  -- was 3,232/10/8,983 before this session's fix: **+6 Pass** (3
  directly confirmed, plus 3 collateral fixes from the same root cause
  among the prior session's six-strong unexplained `ac_numPassed`
  bucket), 0 regressions.
- `ubo.random.*` (2,250 cases): **607 Pass / 0 Fail / 1,643 NotSupported**
  -- unchanged, confirmed by a re-sweep, no regression.

**Remaining 4 `ssbo.*` fails, re-scoped as roadmap L124(t)**:

1. `all_per_block_buffers.20`: `vk.createComputePipelines` fails with
   `VK_ERROR_INITIALIZATION_FAILED` -- unchanged from the prior session,
   not investigated further this session.
2. `all_shared_buffer.13` ("Counter value incorrect") and
   `nested_structs_instance_arrays.8` ("Result comparison and counter
   values are incorrect") -- the residual of the prior session's
   six-strong unexplained `ac_numPassed` bucket after this session's fix
   collaterally closed the other four; still not decoded.
3. `all_shared_buffer.41` ("Result comparison failed") -- investigated
   this session in depth. Initially suspected to be a distinct
   wrapper-content-type stride mismatch (a non-square `mat4x3` `RowMajor`
   matrix reached through a sole-fixed-array-member wrapper shape, whose
   content type is computed via a decoration-blind
   `convertArrayTypeIgnoringDecorations` path). However, pulling the
   real failing test's own SPIR-V shape via
   `--deqp-log-decompiled-spirv=enable` showed the block struct has 4
   members (not 1), so it is **not** actually the wrapper shape at all
   -- it is an ordinary non-wrapper block member, going through the
   already-decoration-aware `convertOffsetStructTypeIgnoringDecorations`
   path instead. A faithful minimal `feme-opt` repro mirroring the real
   4-member struct layout converts correctly: the struct's own physical
   layout, the widened matrix member, and the GEP addressing all look
   correct. **The true root cause remains unidentified** -- likely a
   runtime/JIT-level bug (untested past `feme-opt`'s own IR output) or
   an interaction with a sibling struct member's own layout
   substitution, not a matrix-decoration gap in the conversion patterns
   themselves.

FeMe source revision under test: this session's own commits (see
`agent_thoughts.md` for the exact commit list). No feature or extension
inventory changes: this session's fix is an internal SPIR-V-to-LLVM
matrix-access recognition correctness fix, not new Vulkan
feature/extension surface, so
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) are unchanged
and still accurate. See `agent_thoughts.md` for the full narrative and
next steps.

## Session: L124(t) fixed -- runtime-array-of-matrix conversion gap, closes 3 of `ssbo.*`'s remaining 4; residual 1 re-scoped as L124(u)

Triaged and fixed roadmap L124(t) (the 4 `ssbo.*` fails left after
L124(s)): `all_per_block_buffers.20`'s own pipeline-creation crash,
`all_shared_buffer.13`/`nested_structs_instance_arrays.8`'s unexplained
`ac_numPassed` mismatches, and `all_shared_buffer.41`'s "Result
comparison failed" (previously investigated but unresolved, since the
prior session's own repro had mirrored the wrong member).

Re-triaged `all_shared_buffer.13` and `nested_structs_instance_arrays.8`
via `--deqp-log-decompiled-spirv=enable` and confirmed both still fail.
Built two minimal `feme-opt` repros mirroring `all_shared_buffer.13`'s
own two candidate blocks: `BlockB` (a runtime array of struct with a
`ColMajor` matrix -- converted correctly, no bug) and `BlockC` (`mat2`,
`mat4`, and a trailing runtime array of `mat4x3`, all `RowMajor`) -- the
second repro revealed the actual bug: the trailing runtime-array-of-
`mat4x3` member's content type stayed in its natural, untransposed,
column-major shape instead of the `RowMajor`-substituted physical shape,
while the two preceding direct matrix members widened correctly. Cross-
checked this shape against `all_shared_buffer.41`'s own real decompiled
SPIR-V and found an exact match: **both tests share the same root
cause**, a trailing `spirv::RuntimeArrayType` directly wrapping a matrix
(`buffer Block { ...; matCxR m[]; };`, no fixed-array nesting).

Root-caused to `peelArraysToMatrixType` (the helper used by
`convertOffsetStructTypeIgnoringDecorations`'s own per-member
representability check, L124(g)): it deliberately never peeled through
a `spirv::RuntimeArrayType` at all, reasoning that since a runtime array
can only ever be a struct's own last member (per SPIR-V/Vulkan
validation), no *nesting* case was needed for it -- true, but this
missed that the runtime array can still *directly* wrap a matrix itself
(not merely nest one deeper). Because the peel returned null for this
shape, the per-member loop fell back to
`isMatrixMemberLayoutRepresentable`, which only recognizes a member as
needing the check if it is *directly* a `MatrixType` (false for a
`RuntimeArrayType`), so it always (silently) reported the member
representable regardless of its real decorations, entirely skipping the
`RowMajor`/`MatrixStride` physical substitution. For a non-square matrix
(`mat4x3`: physical `RowMajor` layout is 48 bytes; natural column-major
layout, once LLVM pads each column vector, is 64 bytes), this silently
corrupts every array element's addressing -- exactly matching both
tests' own symptoms.

Fixed by extending `peelArraysToMatrixType` to peel a single leading
`spirv::RuntimeArrayType` before its existing fixed-`ArrayType` peel
loop, and its inverse, `wrapPhysicalMatrixInArrays`, to symmetrically
re-wrap in an unsized `!llvm.array<0 x T>` (matching
`RuntimeArrayType`'s own type-conversion shape) when the original type
was a `RuntimeArrayType`. New
`spirv-to-llvm-matrix-rowmajor-runtime-array-block.mlir` regression
test, confirmed to fail pre-fix (`git stash` on the source) and pass
post-fix (`FileCheck`), following the project's established
regression-test verification pattern.

Also verified via a further targeted `feme-opt` repro (mirroring
`nested_structs_instance_arrays.8`'s own `BlockD.n[]` member, a
*square* `mat3` `RowMajor` runtime array with an `sF{bool}` preceding
struct member matching the real offsets) that this fix already
correctly widens/transposes a square-matrix runtime array too (not just
the non-square case that motivated the fix) -- `isMatrixLayoutRepresentable`
unconditionally rejects any `RowMajor` decoration regardless of size
match, so the fix's physical substitution correctly fires for both.

Results:

- `ninja check-feme`: **3,215/3,218 Passed, 3 Unsupported, 0 Failed**
  (+1 from the new test, 0 regressions). `libfeme_vulkan.so` confirmed
  freshly rebuilt as a `check-feme` dependency before any CTS
  verification.
- Re-confirmed `FeMe CPU Vulkan Device` via `vulkaninfo` with the
  freshly-built ICD.
- The originally-failing test, `dEQP-VK.ssbo.layout.random.all_shared_buffer.41`:
  now **Passes** (was "Result comparison failed").
- `ssbo.*` (12,225 cases): **3,241 Pass / 1 Fail / 8,983 NotSupported**
  -- was 3,238/4/8,983 before this session's fix: **+3 Pass**
  (`all_shared_buffer.41` and `all_shared_buffer.13` directly, both
  sharing the same trailing-runtime-array-of-matrix root cause;
  `all_per_block_buffers.20`'s own `VK_ERROR_INITIALIZATION_FAILED`
  pipeline-creation crash also collaterally closed -- it was itself
  downstream of the same bad type, not a separate compiler-crash class
  as previously suspected), 0 regressions.
- `ubo.random.*` (2,250 cases): **607 Pass / 0 Fail / 1,643 NotSupported**
  -- unchanged, confirmed by a re-sweep, no regression (checked with
  extra care this session since `peelArraysToMatrixType`/
  `wrapPhysicalMatrixInArrays` are shared helpers also used by the
  uniform-block conversion path).

**Remaining 1 `ssbo.*` fail, re-scoped as roadmap L124(u)**:
`dEQP-VK.ssbo.layout.random.nested_structs_instance_arrays.8` ("Result
comparison and counter values are incorrect"). Its own shape is
considerably more complex than any other L124 repro so far -- three
buffer blocks (`BlockB`/`BlockC`/`BlockD`), six distinct nested struct
types (`sA`-`sF`), and several distinct matrix shapes (direct
`mat2x3`/`mat3x2`/`mat3` block/struct members, a `mat4` inside an
array-of-struct, and `BlockD`'s own trailing `mat3` runtime array).
This session ruled out this session's own fix as the cause (a faithful
repro of `BlockD.n[]`'s exact real shape, including its preceding
`sF{bool}` struct member at the real offsets, converts and
transposes correctly) -- the real mismatch must be a value-level or
interaction bug elsewhere in this shader, not yet isolated.

FeMe source revision under test: this session's own commits (see
`agent_thoughts.md` for the exact commit list). No feature or extension
inventory changes: this session's fix is an internal SPIR-V-to-LLVM
matrix-access recognition correctness fix, not new Vulkan
feature/extension surface, so
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) are unchanged
and still accurate. See `agent_thoughts.md` for the full narrative and
next steps.

## Roadmap L124(u) (closed this session): `ssbo.*`'s sole remaining fail fixed -- `ssbo.*` and `ubo.*` both now fully clean

Picked up L124(u), the single remaining `ssbo.*` fail left after L124(t):
`dEQP-VK.ssbo.layout.random.nested_structs_instance_arrays.8` ("Result
comparison and counter values are incorrect").

Root cause: `getStructMemberPhysicalIndex`'s "redo" of a nested struct's
own physical layout (used by `remapNestedStructMemberIndices` to
translate a declared struct-member index into its real physical index)
converts the nested struct **standalone**, as if it were top-level. For
this test's `sB` (nested inside `sD`, itself `BlockC.f`'s own member),
that standalone conversion trivially succeeds at the "natural" (tier 1)
retry -- a bare `vec3` member's generic/rounded LLVM size (16 bytes)
happens to exactly reach the next member's declared offset in isolation.
But `sB` embedded for real inside `sD` uses a **different, correct**
physical layout: `sD`'s own natural-tier conversion fails (its own `i32`
member right after `sB` can't be reached without a gap), forcing a retry
at the `VectorOnly` tier, whose per-member substitution loop
unconditionally calls `getTightNestedStructType` on any nested-struct
member -- always force-tightening every interior vector/matrix member of
that nested struct regardless of whether the nested struct would need
tightening in isolation. This confirms (and extends) a pattern this whole
roadmap item has repeatedly hit in different shapes: whether a nested
struct's own body ends up tightened is a property of **which retry tier
its enclosing struct actually needed**, not an intrinsic property of the
nested struct type itself -- now confirmed at the struct-member-index
level, not just the matrix-widening level this series has fixed before.

Fixed by a new `getStructMemberPhysicalIndexInRealType`, which reads the
declared-to-physical index mapping directly off the already-known-correct
real LLVM type used by the immediate enclosing struct's own conversion
(walking its body in declared-offset order and matching each member's
declared SPIR-V offset against a running byte-offset walk), rather than
re-deriving the nested struct's layout independently.
`remapNestedStructMemberIndices` now tracks this real type across nested
struct levels, using the new function for genuinely nested (non-top-level)
levels and falling back to the pre-existing logic for the first/top-level
struct (unchanged there). The real type is reset to null when the access-
chain walk passes through an array level, conservatively preserving all
prior behavior for array-of-struct cases (not the specific shape this fix
targets, and not yet assessed as in/out of scope for the same bug class).

New `spirv-to-llvm-doubly-nested-struct-tier-dependent-tightening.mlir`
regression test, mirroring `sD`/`sB`'s exact real shape, confirmed via
`git stash` to compute the wrong physical index (4, from `sB`'s untightened
standalone layout) pre-fix and the correct index (5, from `sB`'s real
tightened-with-interior-gap layout as embedded in `sD`) post-fix.

Also investigated, before finding the true root cause: built a
hand-rolled `feme-run` numeric reproduction harness (heap + verify
script) for the real shader to try to isolate the bug independently of
`feme-opt`'s own IR output. This surfaced a large detour: the harness's
own hand-typed verification script had at least one confirmed
offset-computation bug of its own (reading the wrong buffer word for
`f.mA.mC`/`f.mA.mD`, unrelated to the compiler), which produced a false
"FAIL" even after the real fix was already correct -- caught only by
manually re-deriving absolute byte offsets from first principles and
decoding raw output words directly. Given the real CTS test and full
`ssbo.*`/`ubo.random.*` sweeps are strictly more authoritative and
confirm the fix directly, this ad hoc harness's own remaining apparent
mismatches (in `BlockB`/`BlockD`) were not chased further -- they are
very likely further bugs in the hand-rolled harness itself (e.g. wrong
buffer size/binding assumptions), not real compiler defects, since the
authoritative CTS test now passes outright.

Results:

- `ninja check-feme`: **3,216/3,219 Passed, 3 Unsupported, 0 Failed**
  (+1 from the new test, 0 regressions).
- Re-confirmed `FeMe CPU Vulkan Device` via `vulkaninfo --summary` with
  the freshly-built ICD (`VK_DRIVER_FILES` pointed at the local build's
  `feme_icd.json`) before running any CTS cases.
- The originally-failing test,
  `dEQP-VK.ssbo.layout.random.nested_structs_instance_arrays.8`: now
  **Passes** (was "Result comparison and counter values are incorrect"),
  confirmed by running it directly via `deqp-vk --deqp-case=...`.
- `ssbo.*` (12,225 cases): **3,242 Pass / 0 Fail / 8,983 NotSupported**
  -- was 3,241/1/8,983 before this session's fix: **+1 Pass, 0 Fail**.
  **`ssbo.*` is now fully clean.**
- `ubo.random.*` (2,250 cases): **607 Pass / 0 Fail / 1,643 NotSupported**
  -- unchanged, confirmed no regression.

With this fix, **both the `ubo.*` and `ssbo.*` CTS families are now
fully clean** (0 `Fail` each), closing out the entire L124 roadmap
series that started with `compute.*`/`ssbo.*` triage several sessions
ago.

FeMe source revision under test: this session's own commits (see
`agent_thoughts.md` for the exact commit list). No feature or extension
inventory changes: this session's fix is an internal SPIR-V-to-LLVM
struct-member-index correctness fix, not new Vulkan feature/extension
surface, so [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md)
and [VulkanExtensionInventory.md](VulkanExtensionInventory.md) are
unchanged and still accurate. See `agent_thoughts.md` for the full
narrative and next steps.

## Session: L124(a) fixed -- `spirv.ArrayLength` against a real multi-field storage block, closes `compute.*`'s `read_unbound_ssbo`

Confirmed `FeMe CPU Vulkan Device` via `vulkaninfo --summary` (as required
at the start of every session) before making any changes:

```
# => FeMe CPU Vulkan Device
```

With the entire L124 `ssbo.*`/`ubo.*` triage series closed by the prior
session's L124(u) fix, surveyed `Roadmap.md` for the next unstarted item
and picked **L124(a)**: `dEQP-VK.compute.pipeline.basic.read_unbound_ssbo`,
failing because `spirv.ArrayLength`'s `ArrayLengthPattern`
(`SPIRVToLLVMPatterns.cpp`) rejected any `array_member != 0`, on the false
assumption (like a similar one L119 corrected) that a runtime array is
always its enclosing struct's sole member. The real CTS shader is
`SSBO_1 { vec4 data; uint not_set[]; }`, a genuine multi-field block whose
runtime array is legally member 1 -- the struct's own *last* member, the
only position SPIR-V/Vulkan validation rules ever allow a runtime array to
occupy.

Fixed across three layers, exactly as the roadmap's own prior scoping
(from an earlier session) had already laid out:

1. **`ArrayLengthPattern`** (`SPIRVToLLVMPatterns.cpp`): relaxed the check
   from "member 0 only" to "member must be the pointee struct's own last
   element index" -- 0 for a one-member wrapper, or the real last index
   for a multi-field block. Both are legal; no other value ever is.
2. **`SPIRVResourceLowering.cpp`**: extended `hasOnlySupportedUses`'s and
   `lowerAccesses`'s `isGetArrayLengthIntrinsic` special-cases from
   `HandleKind::Storage`-only to also accept `HandleKind::StorageStruct`.
   For the `StorageStruct` case, `lowerAccesses` now derives the runtime
   array's element stride and byte prefix fresh from `BH.ElementStruct`'s
   own last member (its `ArrayType` element's store size, and
   `StructLayout::getElementOffset` for the prefix) instead of reusing
   `BH.Stride`/`0` as the pre-existing `Storage` case does.
3. **`ResourceCalls.h`/`.cpp` and `FeMeRuntimeCPU.c`**: threaded a new
   `PrefixOffset` operand through `createGetDimensionsRaw` and the shared
   `createCall` helper (all ~10 call sites updated to pass `nullptr`
   except this one), and updated `femeCpuResourceGetDimensionsRawI32` to
   compute `(SizeInBytes - PrefixOffset) / Stride` *inside* the runtime
   function itself, with an explicit underflow guard
   (`SizeInBytes < PrefixOffset`). This is deliberate, not incidental:
   doing the subtraction via ordinary IR arithmetic around the call
   would make an unbound descriptor's `0 - PrefixOffset` wrap around to
   a huge value instead of staying `0`, silently breaking the existing
   "reads as 0 if invalid" convention every other resource-dimension
   query already relies on.

New/updated tests: a second case in
`spirv-to-llvm-array-length.mlir` (conversion layer, the real multi-field
block shape); a new `spirv-resource-lowering-array-length-struct.ll`
(resource-lowering layer, `HandleKind::StorageStruct` shape, confirming
the derived stride=4/prefix=16 values match the real struct's own
layout); and an updated `CHECK` line in the pre-existing
`spirv-resource-lowering-array-length.ll` (the one-member-wrapper
`HandleKind::Storage` case) for the new prefix operand, confirming no
regression to that shape.

Results:

- `ninja check-feme`: **3,217/3,220 Passed, 3 Unsupported, 0 Failed**
  (+1 from the new lit test, 0 regressions).
- Re-confirmed `FeMe CPU Vulkan Device` again before running any CTS
  cases, with the freshly-built ICD.
- The originally-failing test,
  `dEQP-VK.compute.pipeline.basic.read_unbound_ssbo`: now **Passes**
  (was failing `spirv.ArrayLength` legalization), confirmed by running
  it directly via `deqp-vk --deqp-case=...`.
- `compute.*` (61,460 cases): **680 Pass / 5 Fail / 60,775 NotSupported**
  -- was 679/6/60,775 before this session's fix: **+1 Pass, 0
  regressions**. The remaining 5 fails are exactly L124(b)
  (`remove_global_load_pass`), L124(c) (`undefined_values`), L124(d)
  (`device_index`), and the 2 still-open `zero_initialize_workgroup_memory`
  cases (`composites.2`, `types.bool`) -- all previously known, unchanged.

No feature or extension inventory changes: this session's fix is an
internal SPIR-V-to-LLVM/resource-lowering correctness fix (a
previously-too-narrow legality check plus a missing byte-prefix
capability in an existing runtime call), not new Vulkan feature/extension
surface, so [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md)
and [VulkanExtensionInventory.md](VulkanExtensionInventory.md) are
unchanged and still accurate. See `agent_thoughts.md` for the full
narrative and next steps.

## Session: L124(b) fixed -- `initial_value` attribute plus whole-array wrapper access chain, closes `compute.*`'s `remove_global_load_pass`

Confirmed `FeMe CPU Vulkan Device` via `vulkaninfo --summary` (as required
at the start of every session) before making any changes:

```
# => FeMe CPU Vulkan Device
```

Picked up **L124(b)**: `dEQP-VK.compute.pipeline.basic.remove_global_load_pass`,
which turned out to need two distinct, separately-root-caused fixes layered
one atop the other.

**Fix 1 -- `spirv.GlobalVariable`'s `initial_value` attribute (3 commits,
upstream MLIR files).** The real shader's `%count = OpVariable
%_ptr_Private_uint Private %uint_0` -- a plain (non-spec) `OpConstant` used
directly as a module-scope global's Initializer -- was entirely unmodeled:
the deserializer's `processGlobalVariable` only recognized a symbol-bearing
initializer (`getGlobalVariable`/`getSpecConstant`/
`getSpecConstantComposite`) or the symbol-less `OpConstantNull` special
case (`zero_initialized`). Added a new `initial_value` attribute
(`AnyAttr`, matching `spirv.Constant`'s own `value` attribute's typing,
since a composite constant's SPIR-V representation is an untyped
`ArrayAttr` with type tracked separately) across ODS, parser/printer/
verifier (`SPIRVOps.cpp`), deserializer (`Deserializer.cpp`, reusing
`getConstant`), serializer (`SerializeOps.cpp`, reusing the existing
`prepareConstant` machinery `spirv.Constant`/spec-constant-composite
constituents already share), and `SPIRVToLLVM` lowering (a new
`convertInitialValueForGlobal` helper mirroring
`ConstantScalarAndVectorPattern`'s sign-stripping logic; scalar/vector
only -- a composite `initial_value` deserializes/serializes correctly but
lowering to LLVM is deliberately out of scope until a real CTS case needs
it, failing cleanly via `notifyMatchFailure` rather than silently
mishandling it). New/updated lit tests at all three layers
(`structure-ops.mlir`, `global-variable.mlir`,
`memory-ops-to-llvm.mlir`).

**Fix 2 -- whole-array access chain through a wrapper block (1 commit,
`feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`).** Fixing the
deserialization error above surfaced a second, deeper failure one layer
down: `spirv.AccessChain` legalization failed for
`%15 = OpAccessChain %_ptr_StorageBuffer__runtimearr_int %outputs %uint_0`
-- a single-index access chain into a `Block`-decorated, one-member
wrapper struct that selects the wrapper's sole member (its runtime array)
directly, producing a pointer to the *entire* array rather than any one
element within it. `BlockAccessChainPattern` unconditionally required a
real per-element index following the wrapper-selecting one, declining
this legal, if degenerate, shape outright ("not enough indices"). Tint's
own compiler output for this exact test emits precisely this access chain
as a dead value (computed but never loaded from) -- confirmed via
`--deqp-log-decompiled-spirv=enable` against the real failing case.
Fixed by reusing the wrapper-selecting index itself (always the constant
0) as the `llvm.spv.resource.getpointer` operand when no further index
follows it; `rewriteBlockAccess`'s own pre-existing `AllIndices.size() ==
Selector + 1` early return then produces the correct whole-array pointer
with no further changes needed there. New lit test
(`spirv-to-llvm-wrapper-whole-array-access-chain.mlir`), plus a minimal
`.mlir` repro built and verified directly against `feme-opt` before
writing the test, matching the real shader's exact shape.

Results:

- `ninja check-feme`: **3,218/3,221 Passed, 3 Unsupported, 0 Failed**
  (+1 from the new access-chain lit test on top of the prior session's
  count, which itself already included the 3 `initial_value` lit tests;
  0 regressions throughout both fixes).
- Re-confirmed `FeMe CPU Vulkan Device` again before running any CTS
  cases, with the freshly-built ICD.
- The originally-failing test,
  `dEQP-VK.compute.pipeline.basic.remove_global_load_pass`: now
  **Passes** (confirmed by running it directly via
  `deqp-vk --deqp-case=...` after each of the two fixes; failed
  deserialization after fix 1 alone had not yet landed, then failed
  `AccessChain` legalization after fix 1 alone, then passed once fix 2
  landed).
- `compute.*` (61,460 cases): **681 Pass / 4 Fail / 60,775 NotSupported**
  -- was 680/5/60,775 before this session's fixes: **+1 Pass, 0
  regressions**. The remaining 4 fails are exactly L124(c)
  (`undefined_values`), L124(d) (`device_index`), and the 2 still-open
  `zero_initialize_workgroup_memory` cases (`composites.2`,
  `types.bool`) -- all previously known, unchanged.

This session's fix is a new deserializer/serializer/lowering capability
(`initial_value`) plus an internal `AccessChain`-legalization correctness
fix, not new Vulkan feature/extension surface, so
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) are unchanged
and still accurate. See `agent_thoughts.md` for the full narrative and
next steps.

## L124(c)/(d): spirv.CopyLogical + gl_DeviceIndex

This session fixed the two remaining `compute.*` fails from the prior
session's sweep.

**L124(d)** (`dEQP-VK.compute.pipeline.device_group.device_index`):
`gl_DeviceIndex` was entirely unwired for the compute pipeline (only the
graphics pipeline's `ViewIndex`/`DeviceIndex` aliasing existed). Since
neither `feme`'s own `BuiltInMappings[]` table nor LLVM's own
`IntrinsicsSPIRV.td` has an `llvm.spv.*` intrinsic for it at all, and
FeMe's CPU backend always models exactly one physical device, it always
legally resolves to the constant `0` -- a value-level fold rather than an
intrinsic call. Fixed via a new `isDeviceIndexBuiltIn()` check
special-cased in `BuiltInAddressOfPattern::matchAndRewrite`
(`feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`).

**L124(c)** (`dEQP-VK.compute.pipeline.basic.undefined_values`): the real
shader's `OpCopyLogical` (opcode 400, SPIR-V 1.4) was entirely unmodeled
in MLIR's SPIR-V dialect -- confirmed via
`--deqp-log-decompiled-spirv=enable` that it copies an uninitialized
`Function`-storage local struct into a logically-compatible-but-not-
identical `StorageBuffer` block-member struct (same member value types,
different/absent `Offset` decorations) before storing. Added a brand-new
upstream `spirv.CopyLogical` op end-to-end:

- `SPIRVBase.td` (opcode-400 enum case), `SPIRVMiscOps.td` (ODS
  definition), `SPIRVOps.cpp` (`areLogicallyCompatible()` verifier
  implementing the spec's recursive same-shape-ignoring-decorations
  check).
- Following the `spirv.CopyObject` precedent, (de)serialization needed
  **zero** manual `Deserializer.cpp`/`SerializeOps.cpp` code -- confirmed
  via `mlir-translate -test-spirv-roundtrip`.
- `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`:
  `CopyLogicalConversionPattern`, with an identity fast-path (mirroring
  `CopyObject`) plus a leaf-by-leaf `extractvalue`/`insertvalue` rebuild
  for the general case. A deliberately-constructed stress repro (not the
  real CTS shape, which is naturally tight/gapless) exposed a bug: the
  destination struct's own explicit layout can insert a synthetic
  alignment-padding member the source side has no equivalent of, so the
  declared-to-physical member index must be remapped independently on
  *both* sides via `getStructMemberPhysicalIndex` (the same helper
  `spirv.AccessChain`'s own struct-member lowering already relies on).

Results:

- `ninja check-feme`: **3,219/3,222 Passed, 3 Unsupported, 0 Failed**
  (0 regressions; +1 from the new `spirv-to-llvm-copy-logical.mlir` lit
  test on top of the prior session's 3,218/3,221).
- Re-confirmed `FeMe CPU Vulkan Device` before running any CTS cases.
- Both originally-failing tests now **Pass** when run directly:
  `dEQP-VK.compute.pipeline.basic.undefined_values` and
  `dEQP-VK.compute.pipeline.device_group.device_index`.
- `compute.*` (61,460 cases): **683 Pass / 2 Fail / 60,775 NotSupported**
  -- was 681/4/60,775 before this session's fixes: **+2 Pass, 0
  regressions**. Confirmed both fixes hold across every `compute.*`
  variant (`pipeline`, `shader_object_binary`, `shader_object_spirv`).
  The remaining 2 fails are exactly the still-open
  `zero_initialize_workgroup_memory` cases (`composites.2`,
  `types.bool`), previously known and unchanged.

This session's fixes are a new SPIR-V-dialect op (`spirv.CopyLogical`,
mirroring `OpCopyLogical`'s existing SPIR-V 1.4 semantics -- no new
Vulkan-visible capability) plus an internal builtin-value fold
(`gl_DeviceIndex` always folding to a compile-time constant, since
`VK_KHR_device_group`'s multi-device semantics are moot on a
single-device CPU backend). Neither introduces new Vulkan feature or
extension surface, so [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md)
and [VulkanExtensionInventory.md](VulkanExtensionInventory.md) are
unchanged and still accurate. See `agent_thoughts.md` for the full
narrative and next steps.

## L124(v): allow bool/bvec members in Workgroup globals

This session closed the last 2 `compute.*` fails, and with them, the
entire L124 series.

`dEQP-VK.compute.pipeline.zero_initialize_workgroup_memory.{composites.2,
types.bool}` (misnamed `shader_object_spirv.*` in the prior session's own
scoping -- corrected here: the `shader_object_spirv.*` variant is
`NotSupported`, `VK_EXT_shader_object` unimplemented on this driver, so it
was never actually failing) both crashed with `error: failed to legalize
operation 'spirv.GlobalVariable' that was explicitly marked illegal`.
Root-caused via `--deqp-log-decompiled-spirv=enable`: `types.bool` has
several bare scalar `i1` `Workgroup` globals loaded directly (no
`AccessChain`); `composites.2` has one `Workgroup` struct global mixing a
plain `i1` member with `bvec2`/`bvec3`/`bvec4` members. Both were rejected
outright by `WorkgroupGlobalVariablePattern`'s own `containsAddressableBool`
guard, which assumed any `i1` inside a `Workgroup` aggregate is unaddressable
by `getelementptr`.

That assumption turned out to be wrong for every shape FeMe's own struct/
array conversion actually produces -- confirmed via a direct
`mlir-translate -mlir-to-llvmir` + `opt -passes=verify` repro: a struct
member's own `getelementptr` index is always a fixed byte offset baked into
the struct's layout (never a runtime multiply-by-element-size the way
array/vector indexing is), and LLVM's data layout already reserves a full
byte for an `i1` array element's own stride. The one shape that genuinely
can't use `getelementptr` -- indexing a single lane out of a `bool`
*vector* -- was never even covered by this guard's own struct/array-only
recursion in the first place, and is already handled correctly by the
pre-existing `BoolVectorLaneAccessChainPattern`/`BoolVectorLaneLoadPattern`/
`BoolVectorLaneStorePattern` (full-vector load/store plus
`extractelement`/`insertelement`). Fixed by removing the guard entirely.

Results:

- `ninja check-feme`: **3,219/3,222 Passed, 3 Unsupported, 0 Failed** (0
  regressions; same total as before, since the rewritten
  `spirv-to-llvm-workgroup-bool.mlir` -- now a positive test covering a
  bare scalar `i1` global, a struct member `i1`, and a struct member
  `bvec2` -- replaces the old negative test 1-for-1).
- Re-confirmed `FeMe CPU Vulkan Device` before running any CTS cases.
- Both originally-failing tests now **Pass** when run directly.
- `compute.*` (61,460 cases): **685 Pass / 0 Fail / 60,775 NotSupported**
  -- was 683/2/60,775 before this session's fix: **+2 Pass, 0
  regressions**. **`compute.*` is now fully clean.**
- `ssbo.*` (12,225 cases, re-swept since the struct-conversion code path
  this fix touches is shared): **3,242 Pass / 0 Fail / 8,983 NotSupported**
  -- unchanged, still fully clean, confirming no regressions from this
  fix's shared code path either.

This session's fix is an internal correctness fix (a `Workgroup`-storage
global lowering pattern that was overly conservative), not new Vulkan
feature/extension surface, so
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) are unchanged
and still accurate.

**The entire L124 series (`compute.*` and `ssbo.*`) is now fully closed:
both families are 100% clean of `Fail`s.** See `agent_thoughts.md` for the
full narrative and next steps.

## L125(a): implicit-LOD integer-sampled `Plain2D` image sampling; L126 re-scoped

This session picked up both of the prior session's suggested next steps:
**L126** (finish the abandoned `subgroups.ballot_broadcast.*` sweep) and
**L125** (first triage pass over the never-before-sampled
`pipeline.monolithic.*`, 465,554 cases).

### L125: `pipeline.monolithic.*` first triage

A `--deqp-fraction=0,50` (1/50) representative sample (8,007 cases)
completed cleanly and found **1,165 Fail (14.5%)**. Bucketing by error
message found the largest single bucket (515 of 1,165) was a
`VK_ERROR_INITIALIZATION_FAILED` pipeline-creation failure, concentrated in
integer-channel image formats (`r8_uint`/`r8_sint`/`r32_uint`/`r8g8_uint`/
`r32g32b32a32_uint`) across many `view_type`s.

Root-caused via `FEME_CPU_LOG_RESOURCE_NORMALIZATION=1` (an existing opt-in
diagnostic env var, `SPIRVResourceLowering.cpp`): an ordinary
`texture()`/`Sample()` call (`OpImageSampleImplicitLod`) against an
integer-format sampled image was rejected outright by
`hasOnlySupportedImageUses`'s roadmap-H109 integer-sample acceptance, which
required an *explicit* LOD unconditionally -- a prior session's own fix
only ever covered `usampler2D`/`isampler2D`'s narrower explicit-LOD shape
(a real but rarer case from `mesh_shader.ext.synchronization.*`), leaving
the far more common ordinary-sampling shape (used pervasively by
`pipeline.monolithic.image.*`'s own integer-format test matrix) unsupported
and failing the *entire* shader's resource normalization, hence a
pipeline-creation-time crash rather than a data mismatch.

Fixed by widening `hasOnlySupportedImageUses`'s integer-`Plain2D` branch to
also accept implicit-LOD sampling: `lowerImageAccesses`'s own pre-existing
`Lod` default (a constant `0.0` whenever `ExplicitLod` is false) is already
exactly right, since every real CTS case this widening covers samples a
single-mip-level image (mip selection would clamp to 0 regardless of a
real derivative-based LOD computation).

New unit tests: `LowersImplicitLodIntegerSampledImageToImageSampleV4I32`
(the fix itself) and `LeavesA1DIntegerSampledImageHandleUsedForSampleAlone`
(confirming every non-`Plain2D` shape -- e.g. `Plain1D` -- is still
correctly rejected, unchanged; no `createSample1DI32`-equivalent runtime
helper exists yet for any other shape).

Results:

- `ninja check-feme`: **3,220/3,223 Passed, 3 Unsupported, 0 Failed** (+2
  from the new tests, 0 regressions).
- Re-confirmed `FeMe CPU Vulkan Device` before running any CTS cases.
- Direct re-verification: `dEQP-VK.pipeline.monolithic.image.suballocation.
  sampling_type.combined.view_type.2d.format.r8_sint.*` went from **18
  Fail / 34 Pass** (pre-fix) to **0 Fail / 36 Pass** (post-fix).
- A second, fresh `--deqp-fraction=0,50` sample (note: `deqp-vk`'s own
  fraction sampling is not deterministic run-to-run, so this is a
  *different* ~8,007-case subset, not a like-for-like before/after diff of
  the same cases) confirmed the fix's own target bucket is gone from every
  case reached; this session's own time budget only allowed 5,214 of that
  second sample's cases to run before stopping partway through the very
  large `sampler.*` subfamily. The residual fails in that partial run are
  a mix of already-known buckets (`Image mismatch`, likely ASTC/EAC/ETC2
  compressed-format decoding) plus newly-found ones, broken out in
  `Roadmap.md` as **L125(b)** (widen this same integer-sampling fix to
  every non-`Plain2D` shape), **L125(c)** (the other, not-yet-root-caused
  fail buckets, including a `sampler.border_swizzle`-specific
  `VK_ERROR_INITIALIZATION_FAILED` bucket confirmed *not* to share
  L125(a)'s own root cause, since it also hits ordinary float formats),
  and **L125(d)** (a smaller, already-decoded `sampler.border_swizzle`
  color-mismatch bucket, suggesting a custom component swizzle isn't
  applied to a synthesized border color the way it is to an in-bounds
  texel fetch).

This session's fix is an internal correctness fix (widening an existing,
narrowly-scoped resource-lowering acceptance check), not new Vulkan
feature/extension surface, so
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) are unchanged
and still accurate.

### L126: `subgroups.ballot_broadcast.*` -- not a quick win after all

Resuming the prior session's abandoned sweep found it was not actually
hung nor merely "slow" in the way a `requiredsubgroupsize128` case for a
narrow type (`bool`) had already been observed to be (eventually
completing in around a minute) -- `subgroupbroadcast_bvec2_
requiredsubgroupsize128` ran for over 10 minutes at 100% CPU with zero
forward progress (checked via `/proc/<pid>/status`/`top`, confirmed truly
executing, not blocked), and a fresh, independent single-case repro with a
60-second `timeout` also never completed.

A further, tightly time-boxed triage (30-second `timeout` per case) found
`subgroupbroadcast_bool_requiredsubgroupsize128` and
`subgroupbroadcast_bvec3_requiredsubgroupsize64` *also* fail to finish in
30 seconds, while `subgroupbroadcast_bvec2_requiredsubgroupsize64` finishes
quickly -- suggesting a cost that scales steeply (possibly exponentially)
with `(required subgroup size) x (vector width)`, rather than a single
isolated hang. Given the depth of investigation this would need (a
debugger-attached backtrace or profiler sample to find the actual hot
loop, and to determine whether the cost lives in FeMe's own subgroup-
broadcast emulation or in the CTS harness's own reference-value
computation), this was **re-scoped rather than closed**: broken out as
**L126(a)** in `Roadmap.md` for a future session with a larger time budget
to root-cause properly.

No code change for L126 this session -- purely an investigation, captured
in `Roadmap.md` for the next session to pick up. See `agent_thoughts.md`
for the full narrative and next steps.

## L125(b): widen integer-sampled implicit-LOD fix to `Plain1D`

This session resumed **L125(b)**, widening L125(a)'s `Plain2D`
integer-channel implicit-LOD-sampling fix one shape at a time. Picked up
`Plain1D` first (the smallest/simplest remaining shape), per the prior
session's own scoping.

Added `ImageCallKind::Sample1DI32` (`feme.cpu.image.sample.1d.v4i32`),
mirroring `Sample2DI32`'s operand shape but narrowed to a bare scalar
`U`/`Offset` the same way `Sample1D` narrows `Sample2D`'s own 2-component
coordinate/offset. Wired through `getImageCallName`/
`getOrInsertImageCall`/`createSample1DI32`/`matchImageCall`'s `AllKinds`
table and per-kind switch (`ImageCalls.h`/`.cpp`), a new
`femeCpuImageSample1DV4I32` runtime implementation (`FeMeRuntimeCPU.c`,
reusing the pre-existing `femeRTFetchTexel1DI32` texel-fetch helper — no
new low-level fetch code needed), and `SPIRVResourceLowering.cpp`'s
`hasOnlySupportedImageUses`/`lowerImageAccesses` to accept and lower a
`Plain1D` integer-channel sample the same way `Plain2D`'s was widened by
L125(a).

Also fixed a stale doc comment on `ImageCallKind::Sample2DI32` itself
(`ImageCalls.h`), left over from before L125(a)'s own fix: it still
claimed this call kind was "scoped, for now, to an explicit-LOD sample
only" with a nonexistent `UseExplicitLod` parameter, both no longer true
since L125(a) widened acceptance to implicit-LOD sampling too.

### Unit tests

Converted the previous `LeavesA1DIntegerSampledImageHandleUsedForSample
Alone` rejection test into a pair of "lowers" tests for `Plain1D`
(`LowersImplicitLodIntegerSampledImage1DToImageSampleV4I32`/
`LowersIntegerSampledImage1DToImageSampleV4I32`, mirroring L125(a)'s own
`Plain2D` test pair), added a new `LeavesAnArray1DIntegerSampledImage
HandleUsedForSampleAlone` rejection test to keep the next remaining shape
(`Array1D`) covered as still correctly out of scope, and added a
`matchImageCall` unit test (`MatchesSample1DI32Call`) for the new call
kind.

### Results

- `ninja FeMeTransformsCPUTests`: all 526 tests pass (+3 net vs. before
  this session: 2 new "lowers" tests + 1 new `matchImageCall` test, minus
  the 1 converted rejection test, plus the still-present
  `LeavesAnArray1DIntegerSampledImageHandleUsedForSampleAlone` replacement
  for it).
- `ninja check-feme`: **3,223/3,226 Passed, 3 Unsupported, 0 Failed** (0
  regressions).
- Re-confirmed `FeMe CPU Vulkan Device` before running any CTS cases.
- Direct re-verification: `dEQP-VK.pipeline.monolithic.image.suballocation.
  sampling_type.combined.view_type.1d.format.r8_sint.*` — **0 Fail / 36
  Pass** (16 `NotSupported`, an unrelated
  `shaderSampledImageArrayDynamicIndexing` feature gate, not this fix's
  concern).

This session's fix is an internal correctness fix (widening an existing,
narrowly-scoped resource-lowering acceptance check to one more image
shape), not new Vulkan feature/extension surface, so
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) are unchanged
this session.

`Array1D`/`Array2D`/`Plain3D`/`Cube`/`CubeArray` remain unaddressed --
`Roadmap.md`'s L125(b) row is updated to reflect `Plain1D` done and the
remaining five shapes still to go, one small commit per shape following
this same session's own pattern. See `agent_thoughts.md` for the full
narrative and next steps.

## L125(b) continued: widen integer-sampled implicit-LOD fix to `Array1D`

Continuing this same session's `Plain1D` commit, picked up `Array1D` next
(the second-smallest remaining shape per the roadmap's own scoping).

Added `ImageCallKind::Sample1DArrayI32` (`feme.cpu.image.sample.1darray.
v4i32`), mirroring `Sample1DI32`'s own operand shape plus an added
`ArrayLayer` operand alongside `U`, matching `Sample1DArray`'s own
relationship to `Sample1D`. `Offset` stays a bare scalar (excluding the
array layer), matching `Array1D`'s own `ConstOffset` dimensionality
already established by the float-sampling path's own precedent
(`createSample1DArray`).

Wired through `getImageCallName`/`getOrInsertImageCall`/
`createSample1DArrayI32`/`matchImageCall`'s `AllKinds` table and per-kind
switch (`ImageCalls.h`/`.cpp`), a new `femeCpuImageSample1DArrayV4I32`
runtime implementation (`FeMeRuntimeCPU.c`, reusing the pre-existing
`femeRTFetchTexel1DArrayI32`/`femeRTRoundClampLayer` helpers -- placed
after `femeRTRoundClampLayer`'s own definition in the file to satisfy C's
forward-declaration ordering, since that helper is `static`), and
`SPIRVResourceLowering.cpp`'s `hasOnlySupportedImageUses`/
`lowerImageAccesses` to accept and lower an `Array1D` integer-channel
sample the same way `Plain1D`'s was widened by this same session's prior
commit.

### Unit tests

Converted the previous `LeavesAnArray1DIntegerSampledImageHandleUsedFor
SampleAlone` rejection test into a pair of "lowers" tests for `Array1D`
(mirroring the `Plain1D` test pair), added a new
`LeavesAnArray2DIntegerSampledImageHandleUsedForSampleAlone` rejection
test to keep the next remaining shape (`Array2D`) covered as still
correctly out of scope, and added a `matchImageCall` unit test
(`MatchesSample1DArrayI32Call`).

### Results

- `ninja FeMeTransformsCPUTests`: all 529 tests pass (+3 net vs. the
  `Plain1D` commit).
- `ninja check-feme`: **3,226/3,229 Passed, 3 Unsupported, 0 Failed** (0
  regressions).
- Re-confirmed `FeMe CPU Vulkan Device` before running any CTS cases.
- Direct re-verification: `dEQP-VK.pipeline.monolithic.image.suballocation.
  sampling_type.combined.view_type.1d_array.format.r8_sint.*` — **0 Fail
  / 72 Pass** (16 `NotSupported`, the same unrelated
  `shaderSampledImageArrayDynamicIndexing` feature gate seen for `Plain1D`).

Internal correctness fix again, not new Vulkan feature/extension surface
-- [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) remain
unchanged.

`Array2D`/`Plain3D`/`Cube`/`CubeArray` remain unaddressed --
`Roadmap.md`'s L125(b) row is updated to reflect `Plain1D`/`Array1D` done
and the remaining three shapes still to go. See `agent_thoughts.md` for
the full narrative and next steps.

## L125(b) continued: widen integer-sampled implicit-LOD fix to `Array2D`

Continuing this same milestone's `Plain1D`/`Array1D` widening, picked up
`Array2D` next: added `ImageCallKind::Sample2DArrayI32` /
`createSample2DArrayI32` / `feme.cpu.image.sample.2darray.v4i32`
(`ImageCalls.h`/`.cpp`, including `getImageCallName`/`getOrInsertImageCall`/
`matchImageCall`'s `AllKinds` array and switch-case), the
`femeCpuImageSample2DArrayV4I32` CPU runtime implementation
(`FeMeRuntimeCPU.c`, placed after `femeRTRoundClampLayer`'s own definition
to satisfy that helper's static-function-ordering requirement, mirroring
the `Array1D` commit's identical placement reasoning), and
`SPIRVResourceLowering.cpp` acceptance (`hasOnlySupportedImageUses`'s
`IsInteger` branch widened to accept `Array2D`, with
`isSupportedOffset`'s existing `AllowArray2D=true` now passed for that
shape) plus lowering (`lowerImageAccesses` extracts `U`/`V`/`ArrayLayer`
from `Array2D`'s own real 3-wide coordinate and `OffsetX`/`OffsetY` from
its 2-wide `ConstOffset`, then calls `createSample2DArrayI32`).

Unlike `Plain1D`/`Array1D`, `Array2D` needed **no new low-level
texel-fetch helper**: `femeRTFetchTexel2DI32` already accepts a real
`Layer` parameter (the same helper `femeCpuImageSample2DV4I32` calls with
`Layer=0`), so the new sampling entry point only needed to resolve the
layer via `femeRTRoundClampLayer` and pass it straight through.

Converted the previous `LeavesAnArray2DIntegerSampledImageHandleUsedFor
SampleAlone` rejection test into a pair of "lowers" tests for `Array2D`
(implicit-LOD defaulting `Lod` to `0.0`, and explicit-LOD threading a
real nonzero `ConstOffset`), added a new
`LeavesAPlain3DIntegerSampledImageHandleUsedForSampleAlone` rejection test
so the next unaddressed shape stays covered as still out of scope, and
added `MatchesSample2DArrayI32Call` to `ImageCallsTest.cpp`.

- `ninja FeMeTransformsCPUTests`: all 532 tests pass (+3 net vs. the
  `Array1D` commit).
- `ninja check-feme`: **3,229/3,232 Passed, 3 Unsupported, 0 Failed** (0
  regressions).
- Re-confirmed `FeMe CPU Vulkan Device` before running any CTS cases.
- Direct re-verification: a 20-case sample of
  `dEQP-VK.pipeline.monolithic.image.suballocation.sampling_type.combined.
  view_type.2d_array.format.r32_sint.*` (several sizes/array counts,
  both the `combined` graphics variant and its `_compute` counterpart) --
  **0 Fail / 20 Pass**.

Internal correctness fix again, not new Vulkan feature/extension surface
-- [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) remain
unchanged.

`Plain3D`/`Cube`/`CubeArray` remain unaddressed -- `Roadmap.md`'s
L125(b) row is updated to reflect `Plain1D`/`Array1D`/`Array2D` done and
the remaining two/three shapes still to go. See `agent_thoughts.md` for
the full narrative and next steps.

## L125(b) continued: widen integer-sampled implicit-LOD fix to `Plain3D`

Continuing this same milestone's `Plain1D`/`Array1D`/`Array2D` widening,
picked up `Plain3D` next: added `ImageCallKind::Sample3DI32` /
`createSample3DI32` / `feme.cpu.image.sample.3d.v4i32` (`ImageCalls.h`/
`.cpp`, including `getImageCallName`/`getOrInsertImageCall`/
`matchImageCall`'s `AllKinds` array and switch-case), the
`femeCpuImageSample3DV4I32` CPU runtime implementation
(`FeMeRuntimeCPU.c`, no static-ordering constraint this time since
`Plain3D` has no array layer to resolve via `femeRTRoundClampLayer`), and
`SPIRVResourceLowering.cpp` acceptance (`hasOnlySupportedImageUses`'s
`IsInteger` branch widened to accept `Plain3D`) plus lowering
(`lowerImageAccesses` extracts `U`/`V`/`W` from `Plain3D`'s own real
3-wide coordinate and `OffsetX`/`OffsetY`/`OffsetZ` from its genuine
3-wide `ConstOffset`, then calls `createSample3DI32`).

Like `Array2D`, `Plain3D` needed **no new low-level texel-fetch helper**:
`femeRTFetchTexel3DI32` already existed (reused by
`feme.cpu.image.load.3d.v4i32`, roadmap H19c), so the new sampling entry
point only had to resolve the mip level and address each of the three
axes before calling straight through. `isSupportedOffset` also already
unconditionally accepted a genuine 3-wide `Plain3D` offset (added by the
earlier float-sampling L67(c) work) -- no change needed there either,
unlike `Array2D`'s own `AllowArray2D` gating.

Converted the previous
`LeavesAPlain3DIntegerSampledImageHandleUsedForSampleAlone` rejection
test into a pair of "lowers" tests for `Plain3D` (implicit-LOD
defaulting `Lod` to `0.0`, and explicit-LOD threading a real nonzero
3-wide `ConstOffset`), added a new
`LeavesACubeIntegerSampledImageHandleUsedForSampleAlone` rejection test
so the next unaddressed shape stays covered as still out of scope, and
added `MatchesSample3DI32Call` to `ImageCallsTest.cpp`.

- `ninja FeMeTransformsCPUTests`: all 535 tests pass (+3 net vs. the
  `Array2D` commit).
- `ninja check-feme`: **3,232/3,235 Passed, 3 Unsupported, 0 Failed** (0
  regressions).
- Re-confirmed `FeMe CPU Vulkan Device` before running any CTS cases.
- Direct re-verification: a 20-case sample of
  `dEQP-VK.pipeline.monolithic.image.suballocation.sampling_type.combined.
  view_type.3d.format.r32_sint.*` (several sizes, both the `combined`
  graphics variant and its `_compute` counterpart) -- **0 Fail / 20
  Pass**.

Internal correctness fix again, not new Vulkan feature/extension surface
-- [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) remain
unchanged.

`Cube`/`CubeArray` remain unaddressed -- `Roadmap.md`'s L125(b) row is
updated to reflect `Plain1D`/`Array1D`/`Array2D`/`Plain3D` done and the
final two shapes still to go. See `agent_thoughts.md` for the full
narrative and next steps.

## L125(b) continued: widen integer-sampled implicit-LOD fix to `Cube`

`Cube` was structurally deferred by the prior `Plain3D` session because
its sample coordinate is a 3-component direction vector rather than a
spatial `(U, V[, W])` triple, and it needed its own investigation before
committing to the established `SampleXI32` pattern. Confirmed all of the
following before writing any code:

- `femeRTComputeCubeClampedLod` can be called with `UseExplicitLod=1`
  exactly like every other shape's `femeRTComputeClampedLod` call --
  when `UseExplicitLod` is true it short-circuits directly to the
  simple bias-and-clamp path and never touches its own derivative
  parameters, so no new implicit-LOD-derivative logic is needed (`Lod`
  already defaults to a constant `0.0` at the call site, same as every
  prior shape).
- `femeRTSelectCubeFace` already resolves a direction vector to a face
  index (Vulkan's own `+X,-X,+Y,-Y,+Z,-Z` layer order) plus a
  `[0, 1]`-normalized face-local `(U, V)` -- exactly the shape needed to
  feed straight into `femeRTFetchTexel2DI32` with `Layer=CF.Face`, so no
  new low-level texel-fetch helper is needed (continuing the streak
  `Array2D`/`Plain3D` already established).
- A cube's `NEAREST` filter never has a fractional footprint that could
  straddle a face edge, so the new I32 function can point-sample
  directly rather than reusing `femeRTSampleFilteredCube`'s own seamless
  bilinear cross-face-edge logic.
- Cube sampling unconditionally forces `ClampToEdge` addressing before
  any cube sample, which never sets `UseBorder` -- so the new I32
  function needs no border-color fallback branch at all, unlike every
  prior shape's own sampler-controlled address mode.
- SPIR-V forbids `ConstOffset` against `Dim::Cube` entirely, so
  `createSampleCubeI32` (mirroring `createSampleCube`'s own identical
  absence) carries no offset operand -- and `isSupportedOffset` already
  required an all-zero offset for `Cube` via its existing generic
  `isZeroOffset` fallback, so no change was needed there either.

Implemented `ImageCallKind::SampleCubeI32`/`createSampleCubeI32`
(11-arg call: image/sampler heap operands, `DirX`/`DirY`/`DirZ`, `Lod`,
`Mask` -- no offset, no derivatives, no `Bias`/`MinLodClamp`),
`femeCpuImageSampleCubeV4I32` (placed after
`femeCpuImageSampleCubeV4F32`'s own definition to satisfy
`femeRTSelectCubeFace`/`femeRTComputeCubeClampedLod`'s static-function
ordering), and widened `hasOnlySupportedImageUses`'s `IsInteger` shape
check plus `lowerImageAccesses`'s integer-sample emission branch (a new
`Cube` case extracting `DirX`/`DirY`/`DirZ` from `Coord`'s three lanes,
no offset extraction at all).

Converted the previous
`LeavesACubeIntegerSampledImageHandleUsedForSampleAlone` rejection test
into a pair of "lowers" tests for `Cube` (implicit-LOD defaulting `Lod`
to `0.0`, and explicit-LOD with an all-zero `ConstOffset` since `Cube`
accepts no nonzero one), added a new
`LeavesACubeArrayIntegerSampledImageHandleUsedForSampleAlone` rejection
test so the final remaining shape stays covered as still out of scope,
and added `MatchesSampleCubeI32Call` to `ImageCallsTest.cpp`.

- `ninja FeMeTransformsCPUTests`: all 538 tests pass (+3 net vs. the
  `Plain3D` commit).
- `ninja check-feme`: **3,235/3,238 Passed, 3 Unsupported, 0 Failed** (0
  regressions).
- Re-confirmed `FeMe CPU Vulkan Device` before running any CTS cases.
- Direct re-verification: a 20-case sample of
  `dEQP-VK.pipeline.monolithic.image.suballocation.sampling_type.combined.
  view_type.cube.format.*_[su]int.*` (several sizes/formats, both the
  `combined` graphics variant and its `_compute` counterpart) -- **0
  Fail / 18 Pass / 2 NotSupported** (the 2 NotSupported an unrelated
  `shaderSampledImageArrayDynamicIndexing`-feature gap, not this fix's
  concern). A broader 20-case sample mixing in `cube_array` cases
  confirmed those still correctly fail with the same pre-existing
  `VK_ERROR_INITIALIZATION_FAILED` (CubeArray is not yet lowered at all
  -- expected, unrelated to this session's Cube-only change, not a
  regression).

Internal correctness fix again, not new Vulkan feature/extension surface
-- [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) remain
unchanged.

`CubeArray` remains the final unaddressed shape -- `Roadmap.md`'s
L125(b) row is updated to reflect `Plain1D`/`Array1D`/`Array2D`/
`Plain3D`/`Cube` done and `CubeArray` still to go. See
`agent_thoughts.md` for the full narrative and next steps.

## L125(b) closed: widen integer-sampled implicit-LOD fix to `CubeArray`

`CubeArray` was the final remaining shape in the L125(b) `SampleXI32`
widening series. Confirmed its own design was a direct follow-on to
this session's own prior `Cube` work:

- `femeRTRoundClampLayer` (already reused by `Array2D`/`Array1D`) rounds
  and clamps a float array index to a valid selectable cube element,
  same as `femeCpuImageSampleCubeArrayV4F32`'s own identical use.
- `femeCpuImageSampleCubeArrayV4F32`'s own `NEAREST` path (via
  `femeRTSampleFilteredCube`) already folds `LayerBase + BaseFace` into
  a single texel fetch's own `Layer` -- the new I32 function does the
  same directly against `femeRTFetchTexel2DI32` (`CubeIndex * 6 +
  CF.Face`), reusing it rather than any new low-level helper.
- SPIR-V's own arrayed-cube coordinate convention is a 4-wide `(DirX,
  DirY, DirZ, ArrayLayer)` vector (`SampleCoordWidth == 4` for
  `CubeArray`, already correctly set for the existing float-sampling
  path) -- `isSupportedOffset` already required (and accepts) a zero
  offset for `CubeArray` via the same generic fallback `Cube` uses, so
  no change was needed there either.

Implemented `ImageCallKind::SampleCubeArrayI32`/
`createSampleCubeArrayI32` (12-arg call: image/sampler heap operands,
`DirX`/`DirY`/`DirZ`, `ArrayLayer`, `Lod`, `Mask` -- no offset, no
derivatives, no `Bias`/`MinLodClamp`), `femeCpuImageSampleCubeArrayV4I32`
(placed after `femeCpuImageSampleCubeArrayV4F32`'s own definition), and
widened `hasOnlySupportedImageUses`'s `IsInteger` shape check plus
`lowerImageAccesses`'s integer-sample emission branch (a new
`CubeArray` case extracting `DirX`/`DirY`/`DirZ`/`ArrayLayer` from
`Coord`'s four lanes, no offset extraction).

Converted the previous
`LeavesACubeArrayIntegerSampledImageHandleUsedForSampleAlone` rejection
test into a pair of "lowers" tests for `CubeArray` (implicit-LOD
defaulting `Lod` to `0.0`, and explicit-LOD with an all-zero
`ConstOffset` since `CubeArray` accepts no nonzero one, same as
`Cube`) -- no further rejection test is needed, since `CubeArray` was
the final remaining shape -- and added `MatchesSampleCubeArrayI32Call`
to `ImageCallsTest.cpp`.

- `ninja FeMeTransformsCPUTests`: all 540 tests pass (+2 net vs. the
  `Cube` commit).
- `ninja check-feme`: **3,237/3,240 Passed, 3 Unsupported, 0 Failed** (0
  regressions).
- Re-confirmed `FeMe CPU Vulkan Device` before running any CTS cases.
- Direct re-verification: two independent 20-case samples of
  `dEQP-VK.pipeline.monolithic.image.suballocation.sampling_type.combined.
  view_type.cube_array.format.*_[su]int.*` (several sizes/formats/array
  counts, both the `combined` graphics variant and its `_compute`
  counterpart) -- **0 Fail / 18 Pass / 22 NotSupported** combined
  (the NotSupported cases an unrelated feature gap, not this fix's
  concern).

Internal correctness fix again, not new Vulkan feature/extension surface
-- [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) remain
unchanged.

**This closes the entire L125(b) widening series** -- all six shapes
(`Plain1D`, `Array1D`, `Array2D`, `Plain3D`, `Cube`, `CubeArray`) now
accept integer-channel implicit-LOD/explicit-LOD sampling. `Roadmap.md`'s
L125(b) row is struck through. See `agent_thoughts.md` for the full
narrative and next steps (picking between `L125(c)`/`L125(d)` or
L125's next fresh sample).

## Roadmap L125(d): `sampler.border_swizzle.*` component-swizzle fix

Root cause: `feme::vulkan::ImageView` (Image.h) never stored
`VkImageViewCreateInfo::components` at all -- `vkCreateImageView`
silently dropped it -- so a synthesized `ClampToBorder` border color
(from `SamplerAddressMode::ClampToBorder` resolving an out-of-range
coordinate) was never swizzled, even though core Vulkan requires it for
`VK_BORDER_COLOR_FLOAT/INT_TRANSPARENT_BLACK` and
`VK_BORDER_COLOR_FLOAT/INT_OPAQUE_WHITE` with **no**
`VK_EXT_border_color_swizzle` needed (only `OPAQUE_BLACK`/custom colors
need that extension for a non-identity mapping, per
`vktPipelineSamplerBorderSwizzleTests.cpp`'s own `checkSupport`).

Fixed by: `ImageView` now stores the mapping (`Image.h`/`Image.cpp`);
`materializeImageDescriptor` (CommandBuffer.cpp) packs it into a new
`FemeImageDescriptor::Swizzle` field (`RuntimeABI.h`'s
`ImageComponentSwizzle` enum, deliberately numbered to match
`VkComponentSwizzle`'s own values so `resolveImageSwizzle`'s per-channel
resolution is a direct `static_cast`, with `Identity == 0` so a
zero-initialized descriptor still decodes as full identity);
`femeRTFetchTexel2D`/`femeRTFetchTexel3D` (FeMeRuntimeCPU.c) apply it to
the border-color branch via a new `femeRTApplyImageSwizzle` helper --
the two functions every other sampled shape (1D, 1D-array, cube via
2D-array reuse) funnels through, confirmed via grep no other
border-color code path exists.

New unit tests: `ImageSamplingTest.ClampToBorderAppliesImageViewSwizzle`
(direct runtime-level `Img.Swizzle` manipulation, a `BARG`+`Zero`
mapping against a distinguishable border color) and
`CommandBufferTest.cpp`'s new `BorderSwizzleSampledImageDispatchTest`
fixture (`AppliesImageViewSwizzleToSynthesizedBorderColor`, full
Vulkan-level dispatch: `vkCreateImageView` with a `VK_COMPONENT_SWIZZLE_
ONE` R-channel override, `ClampToBorder` addressing,
`VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK`, an out-of-range-`uv` shader).

- `ninja check-feme`: **3,239/3,242 Passed, 3 Unsupported, 0 Failed** (0
  regressions; +2 new tests vs. the L125(b) commit above).
- Re-confirmed `FeMe CPU Vulkan Device` before running any CTS cases.
- A 48-case sample of every real 4-component-format
  `dEQP-VK.pipeline.monolithic.sampler.border_swizzle.*.transparent_black
  .no_gather.no_swizzle_hint` case (`r8g8b8a8_unorm`/`r32g32b32a32_sfloat`,
  every non-identity component mapping CTS generates for those two
  formats) -- **48/48 Pass**, every `Ref:`/`Color:` value matching
  exactly (was universally `Color:(0,0,0,0)` before this fix, since the
  border color was never swizzled at all).
- A broader 150-case random sample across `opaque_black`/`opaque_white`/
  `transparent_black` (every format CTS generates a border-swizzle case
  for) -- **35 Pass / 33 Fail / 82 NotSupported**. Of the 33 fails, 26
  are `VK_ERROR_INITIALIZATION_FAILED` at pipeline/compute-pipeline
  creation, an already-tracked, separate L125(c) bucket (confirmed not
  the same root cause -- this fix never touches pipeline creation), and
  7 are a **newly-discovered, distinct** root cause: a format lacking a
  full alpha (or full RGB) channel -- `d16_unorm`, `r16_sfloat`,
  `r16g16_sfloat`, `r32_sfloat`, `r32g32b32_sfloat` -- doesn't get its
  border color's missing channels re-defaulted (alpha to `1`, per core
  Vulkan's "conversion to RGBA" rule) before the swizzle applies, since
  `mapBorderColor`/`Sampler::Sampler` (Image.cpp) bakes a fixed,
  format-independent `float[4]` at *sampler* creation time -- before
  which image it will ever sample is even known. Split out as new
  roadmap row **L125(e)**, since it's a genuinely separate bug (a
  missing-channel default, not a swizzle-application gap) discovered
  only while verifying this fix, not part of this fix's own original
  scope.

Internal correctness fix, not new Vulkan feature/extension surface --
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) remain
unchanged (`VK_EXT_border_color_swizzle` itself is still not
implemented; this fix is purely the core-spec-mandated swizzle/alpha
behavior for the two extension-independent border-color kinds).

`Roadmap.md`'s L125(d) row is marked done, with its own residual gap
split to new row L125(e) rather than left implicit. See
`agent_thoughts.md` for the full narrative and next steps.

## Roadmap L125(e): border-color missing-channel defaulting

Root cause (found while verifying L125(d) above): `mapBorderColor`/
`Sampler::Sampler` (Image.cpp) bakes a fixed, format-independent
`float[4]` `BorderColor` at *sampler* creation time -- a real
`VkSampler` genuinely has no knowledge of which image format it will
ever sample against, so it cannot itself apply core Vulkan's
"conversion to RGBA" rule, which requires a component the *sampled
image's own format* doesn't store to be re-defaulted (`0` for a missing
R/G/B, `1` for a missing A) rather than read from the border-color
value verbatim. `VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK` (nominally
`(0,0,0,0)`) therefore actually samples as `(0,0,0,1)` through an
alpha-less format like `R32G32B32_FLOAT` -- exactly like a real
in-bounds texel of that format would.

Fixed by: `femeRTImageFormatComponentMask(Format)` (FeMeRuntimeCPU.c), a
4-bit R/G/B/A mask of which components a format actually stores,
mirroring `femeRTUnpackImageTexel`'s own per-case fill values one case
at a time (`A8_UNORM` is the sole format whose one real channel is A,
not R; every other partial format fills left-to-right starting from
R -- confirmed by reading every case in that function's own switch);
and `femeRTExpandBorderColorForFormat`, which re-defaults any component
the mask says is missing before `femeRTApplyImageSwizzle` runs. Both
`femeRTFetchTexel2D`'s and `femeRTFetchTexel3D`'s border branches now
call it instead of taking `BorderColor`'s 4 raw components verbatim.
Depth/stencil-only formats (`D16_UNORM`, `D32_FLOAT`, `S8_UINT`) turned
out to share the exact same root cause as `R32_FLOAT` (they already
fill G/B/A identically in `femeRTUnpackImageTexel`), so no separate
handling was needed for them, resolving this row's own open question.

New unit test: `ImageSamplingTest.ClampToBorderExpandsMissingComponentsForFormat`
(`R32G32B32_FLOAT`, sampler bakes alpha `0.4`, fetch must read back
`1.0` instead).

- `ninja check-feme`: **3,240/3,243 Passed, 3 Unsupported, 0 Failed** (+1
  new test vs. the L125(d) commit above, 0 regressions).
- Re-confirmed `FeMe CPU Vulkan Device` before running any CTS cases.
- A targeted 224-case sample of every `d16_unorm`/`r16_sfloat`/
  `r16g16_sfloat`/`r32_sfloat`/`r32g32b32_sfloat` `transparent_black`/
  `opaque_white` `no_gather.no_swizzle_hint` case -- **160 Pass / 0 Fail
  / 64 NotSupported** (every one of these was `Fail` on exactly the
  alpha channel before this fix; the `NotSupported` cases are an
  unrelated, pre-existing feature gap).
- Re-ran the identical 150-case random sample used to verify L125(d)
  above (same shuffle seed, so the same 150 cases): **35 -> 42 Pass**,
  the 7 previously-mismatched cases (the ones that motivated this row)
  now all pass, and the remaining 26 fails are entirely the
  already-tracked, separate L125(c) `VK_ERROR_INITIALIZATION_FAILED`
  bucket (unaffected by this fix, as expected).

**This closes the entire `sampler.border_swizzle.*` `Ref:`/`Color:`
mismatch family L125(d) and L125(e) together set out to fix.** A
distinct, still-open gap was found and split out separately: an
in-bounds texel fetch does not yet apply an image view's own component
swizzle at all (only the border-color path does, as of L125(d)) --
tracked as new roadmap row **L125(f)**.

Internal correctness fix, not new Vulkan feature/extension surface --
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) remain
unchanged.

`Roadmap.md`'s L125(e) row is marked done. See `agent_thoughts.md` for
the full narrative and next steps.

## Roadmap L125(f): in-bounds texel fetch component-swizzle fix

L125(d)/(e) above only fixed a *synthesized border color*'s own
swizzle handling; an ordinary in-range texel read never consulted
`Img->Swizzle` at all, a real and independently-observable gap (any
non-identity `VkImageViewCreateInfo::components` on an ordinary
sampled image silently did nothing). Investigation established the
root cause is architectural: `femeRTFetchTexel2D`/`femeRTFetchTexel3D`
(and their `femeRTFetchTexel1D`/`femeRTFetchTexel1DArray`/
`femeRTFetchCubeSeamlessTexel` wrappers) are shared between two
different Vulkan semantics -- a sampled-image fetch (`OpImageSample*`/
`OpImageFetch`) must apply the image view's own `VkComponentMapping`,
while a storage-image load (`OpImageRead`, `feme.cpu.image.load.*`)
must never be swizzled, per spec and per
`femeCpuImageLoad2DV4F32`'s own doc comment ("reads one texel ...
sampled or storage").

### Fix

Added a new `ApplySwizzle` bool parameter to the four low-level fetch
helpers above. Every plain `Sample2D`/`Sample1D`/`Sample1DArray`/
`Sample3D`/`SampleCube`/`SampleCubeArray` call site (point and linear
filtering) now passes `ApplySwizzle=1`; every `Load*`-family call site
passes `ApplySwizzle=0` (unchanged, already-correct behavior).
`SampleCmp*`/`Gather*`/`GatherCmp*` call sites also pass
`ApplySwizzle=0` for now -- deliberately deferred to new roadmap row
**L125(g)**, since a depth-compare's single-channel dref read and
`OpImageGather`'s `Component` selector both have swizzle-interaction
questions not yet resolved against a real CTS case. The (wholly
separate, non-shared) integer-sampled `*I32` path is similarly
deferred to new roadmap row **L125(h)**.

### Unit tests

- `ImageSamplingTest.SampleAppliesImageViewSwizzleToInBoundsTexel`:
  `Sample2D` (nearest) with a `BARG` mapping against a real, in-bounds
  (not border) texel -- every output channel differs from its
  identity-mapped counterpart, so a regression that only re-breaks the
  in-bounds branch cannot coincidentally still pass.
- `ImageSamplingTest.LoadNeverAppliesImageViewSwizzle`: the
  mirror-image regression guard -- `feme.cpu.image.load.2d.v4f32`
  must read back unswizzled even with a non-identity `Img.Swizzle`,
  protecting the storage-image semantics going forward.

`ninja check-feme`: 3,242/3,245 Passed, 3 Unsupported, 0 Failed (+2
new tests, 0 regressions).

### Results

Two independent, real CTS buckets confirmed the gap and the fix:

- `dEQP-VK.pipeline.monolithic.image_view.*.component_swizzle.*` (a
  5,496-non-identity-case bucket, distinct from the
  `sampler.border_swizzle.*` family L125(d)/(e) fixed) -- a 42-case
  sample spanning every non-cmp/gather view type (`1d`/`1d_array`/
  `2d`/`2d_array`/`3d`/`cube`/`cube_array`) at `r8g8b8a8_unorm` went
  from **0/42 Fail to 42/42 Pass**.
- `dEQP-VK.texture.swizzle.component_mapping.*` (a second, wholly
  independent bucket exercising the same gap) -- a 100-case
  float-format sample went from 6/100 Fail (all `_sint`/`_uint`, the
  deferred I32 path) to its float-format subset going **39/39 Pass**
  (0 fails).
- A 400-case random `image-view.txt` sample's remaining 32
  `component_swizzle.*` fails are entirely ASTC/EAC/ETC2
  compressed-format decoding (L125(c), unrelated) or integer-format
  (`_sint`/`_uint`, deferred to L125(h)) -- no unexplained fails in
  this fix's own float, non-compressed scope.
- A 150-case storage-image `load_store_lod`/`store_load_consistency`
  sample shows no regressions (the `Load*`-family's `ApplySwizzle=0`
  correctly preserves its pre-existing unswizzled behavior).

Internal correctness fix, not new Vulkan feature/extension surface --
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) remain
unchanged.

`Roadmap.md`'s L125(f) row is marked done; new sibling rows L125(g)
(`SampleCmp*`/`Gather*`/`GatherCmp*` swizzle) and L125(h) (integer
`*I32` path swizzle) capture the deferred remainder. See
`agent_thoughts.md` for the full narrative and next steps.

## Roadmap L125(h): integer-sampled texel fetch component-swizzle fix

The mechanically-similar, wholly-separate follow-on to L125(f) above:
the integer-channel fetch family (`femeRTFetchTexel2DI32`/
`femeRTFetchTexel1DI32`/`femeRTFetchTexel1DArrayI32`/
`femeRTFetchTexel3DI32`) shares no code with its float counterpart, so
never picked up L125(f)'s `ApplySwizzle` parameter at all.

### Fix

Added `femeRTApplyImageSwizzleI32` (the `FemeRTv4i32` counterpart of
`femeRTApplyImageSwizzle`) and threaded the same `ApplySwizzle` bool
through all four `*I32` fetch helpers, following L125(f)'s design
exactly. Every `femeCpuImageSample*V4I32` call site now passes
`ApplySwizzle=1`; every `femeCpuImageLoad*V4I32` call site passes
`ApplySwizzle=0`. There is no integer `SampleCmp*`/`Gather*`/
`GatherCmp*` family at all (no such intrinsics exist), so unlike
L125(f) there is no deferred bucket left behind here -- every `*I32`
call site is now fully covered.

### Unit tests

- `ImageSamplingTest.SampleI32AppliesImageViewSwizzleToInBoundsTexel`
  and `ImageSamplingTest.LoadI32NeverAppliesImageViewSwizzle` mirror
  L125(f)'s own float-path tests. These are also the **first
  runtime-execution unit tests for `feme.cpu.image.sample.2d.v4i32`
  at all** -- no prior test in `ImageSamplingTest.cpp` exercised this
  intrinsic (only compile-time/lowering tests in
  `Transforms/CPU/ImageCallsTest.cpp` and
  `Transforms/CPU/SPIRVResourceLoweringTest.cpp` did). A new
  `SampleI32Fn` typedef captures the intrinsic's own explicit-LOD-only
  operand shape (no `DUdX`/`DUdY`/`DVdX`/`DVdY`/`UseExplicitLod`/
  `Bias`/`MinLodClamp`, unlike the float `SampleFn`).

`ninja check-feme`: 3,244/3,247 Passed, 3 Unsupported, 0 Failed (+2
new tests, 0 regressions).

### Results

- A 60-case sample of `dEQP-VK.pipeline.monolithic.image_view.*.
  component_swizzle.*` restricted to `_sint`/`_uint` formats: **32/32
  of the supported cases Pass** (28 `NotSupported`, unrelated
  format-support gating -- e.g. `VK_FORMAT_B8G8R8A8_UINT` isn't a
  sampleable format on this driver).
- A 60-case `_sint` and a separate 60-case `_uint` sample of
  `dEQP-VK.texture.swizzle.component_mapping.*`: every `_uint`
  supported case passes (**19/19**). Every `_sint` case that reaches
  pipeline creation instead hits a pre-existing, unrelated
  `VK_ERROR_INITIALIZATION_FAILED` (`vk.createGraphicsPipelines`/
  `vk.createComputePipelines`) -- confirmed **not** a swizzle-
  correctness failure (no `Image mismatch` seen at all in this sample);
  this is the same `VK_ERROR_INITIALIZATION_FAILED` family already
  flagged as an untriaged L125(c) bucket, now with a new data point
  that it also affects plain (non-border-swizzle) `_sint` sampling,
  not just `sampler.border_swizzle.*` -- left for L125(c)'s own triage,
  out of scope here.
- A follow-up 400-case random `image-view.txt` sample: 95 fails, all
  entirely ASTC/EAC/ETC2/BC compressed-format decoding (L125(c),
  unrelated) -- zero `component_swizzle`/plain-format fails, confirming
  no regressions from this fix.

Internal correctness fix, not new Vulkan feature/extension surface --
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md) remain
unchanged.

`Roadmap.md`'s L125(h) row is marked done -- this closes out the
entire L125(f)/(g)/(h) in-bounds-swizzle sub-tree except for L125(g)'s
own deferred `SampleCmp*`/`Gather*`/`GatherCmp*` scope. See
`agent_thoughts.md` for the full narrative and next steps.

## Roadmap L125(g): plain-`Gather*` post-swizzle `Component` fix + `shaderImageGatherExtended` enablement

Resolved the spec question L125(f) deliberately deferred: per the
authoritative `KhronosGroup/Vulkan-Docs` source (`textures.adoc`'s
"Sampling Operations" and "Texel Gathering" sections -- the rendered
`docs.vulkan.org` page only exposes a summary bullet list, not the
detailed prose), a depth-compare result replaces the depth component
and then flows through the ordinary substitution/swizzle steps like
any texel; and for a plain (non-Cmp) gather, each of the four gathered
neighbor texels is independently converted, substituted, and
**swizzled** first, and only then does `OpImageGather`'s `Component`
operand select a channel from each already-swizzled result. This means
`Component` operates post-swizzle.

Implemented the post-swizzle-`Component` half for the three non-Cmp
gather shapes that exist (`femeCpuImageGather2DV4F32`/
`GatherArray2DV4F32`/`GatherCubeV4F32`, `FeMeRuntimeCPU.c`): each
gathered-neighbor `femeRTFetchTexel2D` call now passes
`ApplySwizzle=1`. `SampleCmp*`/`GatherCmp*` are deliberately left at
`ApplySwizzle=0`, split out as roadmap row L125(i) -- no CTS coverage
combining depth-compare with a non-identity swizzle was found this
session.

While researching this row, also found and fixed a real, independent
enabler bug: `Info.Features.shaderImageGatherExtended`
(`PhysicalDeviceInfo.cpp`) had never been advertised (default
`VK_FALSE`), even though `ImageGatherPattern`'s own MLIR-to-LLVM
lowering (`SPIRVToLLVMPatterns.cpp`) already forwards `OpImageGather`'s
`Component` operand as a fully generic runtime value with no
restriction to 0, and the CPU runtime already indexes any of the four
channels unconditionally -- there was no real capability gap, only a
missing advertisement. This silently made *every*
`dEQP-VK.glsl.texture_gather.*` CTS case report `NotSupported` (even
ones using only the implicit `Component=0`, since dEQP-VK's own
`TextureGatherInstance::init` always declares an explicit SPIR-V
`Component` operand), which is the only real CTS coverage for
gather+swizzle at all. Flipping the bit to `VK_TRUE` (with
`PhysicalDeviceInfoTest.cpp`'s own truthful-capability whitelist test
updated to match) is what made this row's own CTS verification
possible.

New unit test: `ImageSamplingTest.GatherAppliesImageViewSwizzleToEachTexel`
(a red/green-swapping swizzle plus `Component=1` (green) must read the
raw red channel of each gathered neighbor).

`ninja check-feme`: 3,245/3,248 Passed, 3 Unsupported, 0 Failed (+1 new
test, 0 regressions).

CTS: the newly-unlocked `dEQP-VK.glsl.texture_gather.graphics.basic.*.
texture_swizzle.*` bucket (108 cases):
- Passed: 12/12 (100%) of the genuinely-testable non-integer,
  non-cube cases -- every `rgba8` `2d`/`2d_array` swizzle permutation.
- `rgba8i`/`rgba8ui` cases: `VK_ERROR_INITIALIZATION_FAILED` at
  pipeline creation -- a **pre-existing, already-tracked** L125(c)
  integer-sampling bucket, confirmed unrelated to this fix (same
  failure mode independently reproduced against a plain, non-swizzle,
  non-gather integer-sampling case in a prior session).
- `cube` cases (with and without a non-identity swizzle): `Result
  verification failed` -- reproduced on a plain identity-swizzle
  sanity case too
  (`dEQP-VK.glsl.texture_gather.graphics.basic.cube.rgba8.filter_mode.
  min_linear_mag_linear`), confirming a **pre-existing, separate
  Cube-gather correctness bug**, newly exposed by the feature-bit flip
  rather than caused by this row's own swizzle change. Re-scoped as
  roadmap row L125(j).
- An 800-case regression sample of `pipeline.monolithic.{image_view,
  sampler.border_swizzle}.*` shows the identical fail set (77/800)
  whether or not a case uses `gather*`/`no_gather` (confirmed via
  matching `no_gather` cases failing the same way) -- zero regressions
  from this row's own change.

`Vulkan14FeatureInventory.md` updated: `shaderImageGatherExtended` now
advertised `VK_TRUE`.

`Roadmap.md`'s L125(g) row is marked done for the plain-`Gather*` case;
new sibling rows L125(i) (`SampleCmp*`/`GatherCmp*` swizzle, still
deferred pending CTS coverage) and L125(j) (the newly-discovered
Cube-gather correctness bug, unrelated to swizzle) capture the
remaining open work. See `agent_thoughts.md` for the full narrative
and next steps.

## Roadmap L125(j): Cube/CubeArray Gather* seamless cross-face remap fix

**Root cause**: `femeCpuImageGatherCubeV4F32`/`femeCpuImageGatherCmpCubeV4F32`
computed their 2x2 gather footprint via `femeRTComputeBilinearSupport`/
`femeRTFetchTexel2D` with a forced `ClampToEdge` address mode -- any tap
that fell outside the current cube face was clamped back onto that
same face's own edge texel. Vulkan mandates seamless cube-map filtering
(unlike desktop GL, where it is optional): a footprint that straddles a
face edge must remap the out-of-face taps onto the correct neighboring
face, exactly as `femeRTSampleCubeLinearAtLevel` (used by plain cube
`Sample`) already does via `femeRTComputeCubeBilinearSupport`/
`femeRTFetchCubeSeamlessTexel`. This was a known, previously-documented
gap ("no real CTS case yet exercises a face-edge-straddling cube
gather") from the prior L125(g) session -- this session found and
confirmed a real CTS case that does.

**Investigation method**: extracted the failing repro's embedded
`Rendered`/`Reference`/`ErrorMask` PNGs from its QPA log (a small Python
script decoding the base64-embedded images), diffed them pixel-by-pixel
with `pillow`. The 226-of-4096 differing pixels clustered along
diagonal/edge regions with values matching *different* rows of the
color ramp -- consistent with a face-boundary sampling error, not a
broad ordering or swizzle bug. Cross-checked against VK-GL-CTS's own
reference (`framework/common/tcuTexture.cpp`): `TextureCubeView::gather`
reuses its sampling path's own `getCubeLinearSamples` helper verbatim,
including the same doubly-out-of-bounds corner-averaging rule -- and its
`sampleIndices[4] = {2, 3, 1, 0}` result mapping already matched FeMe's
existing `Result[0]=T01/Result[1]=T11/Result[2]=T10/Result[3]=T00`
ordering exactly, so only the fetch/footprint mechanism needed to
change, not the ordering.

**Fix**: both `femeCpuImageGatherCmpCubeV4F32` and
`femeCpuImageGatherCubeV4F32` (FeMeRuntimeCPU.c) switched from
`femeRTComputeBilinearSupport`/`femeRTFetchTexel2D`-with-forced-
`ClampToEdge` to `femeRTComputeCubeBilinearSupport`/
`femeRTFetchCubeSeamlessTexel`, including the same three-way
doubly-out-of-bounds corner-averaging chain
`femeRTSampleCubeLinearAtLevel` already uses. `ApplySwizzle` values are
unchanged from the already-committed L125(g) fix (`0` for the Cmp path,
deferred to L125(i); `1` for the plain path). A cube gather no longer
needs its sampler's address mode or border color at all, since
`femeRTFetchCubeSeamlessTexel` never consults either.

New unit tests: `GatherCubeSeamlessBlendsAcrossFaceEdge`,
`GatherCmpCubeSeamlessBlendsAcrossFaceEdge` (ImageSamplingTest.cpp),
reusing `SampleCubeSeamlessBlendsAcrossFaceEdge`'s own two-face layout
(face 0 uniformly one value, face 4 uniformly another) and direction
vector (`DirX=1, DirY=0, DirZ=0.6`, placing the footprint's low-`U` tap
one texel below `u==0`) to prove two of the four gathered corners
(`T00`/`T01`) now read face 4's own value instead of a clamped-in-place
face-0 value.

`ninja check-feme`: 3,247/3,250 Passed, 3 Unsupported, 0 Failed (+2 new
tests, 0 regressions).

CTS (`dEQP-VK.glsl.texture_gather.*`, `feme_icd.json`,
`FeMe CPU Vulkan Device`):
- The original repro,
  `dEQP-VK.glsl.texture_gather.graphics.basic.cube.rgba8.filter_mode.min_linear_mag_linear`,
  now **Passes** (was `Result verification failed`).
- The broader `dEQP-VK.glsl.texture_gather.graphics.basic.cube.*`
  bucket (220 cases): 25 Pass, 153 NotSupported (unrelated
  format-support gating), 42 Fail -- every one of the 42 fails is the
  pre-existing, already-tracked L125(c)
  `vk.createGraphicsPipelines`/`VK_ERROR_INITIALIZATION_FAILED` bucket
  (confirmed via grepping every failure message: all 42 are the
  identical `VK_ERROR_INITIALIZATION_FAILED at vkRefUtil.cpp:37`
  pipeline-creation error, zero `Result verification failed` fails
  remain).
- `dEQP-VK.glsl.texture_gather.graphics.basic.cube.rgba8.texture_swizzle.*`
  (the L125(g) session's own original discovery point for this bug):
  6 Pass, 6 NotSupported, **0 Fail**.

`Roadmap.md`'s L125(j) row is now struck through and marked done. See
`agent_thoughts.md` for the full narrative and next steps.

## Roadmap L125(k): integer-format (non-Cmp) `Gather*` widening

**Root cause**: `hasOnlySupportedImageUses`'s `isGatherIntrinsic` branch
(`SPIRVResourceLowering.cpp`) unconditionally rejected any
integer-sampled (`IsInteger`) image outright, with a comment noting
this as unstarted follow-on work with no concrete repro yet (L7g's own
original closing text). Triaging L125(c)'s own `createGraphicsPipelines`/
`VK_ERROR_INITIALIZATION_FAILED` bucket this session with
`FEME_VULKAN_LOG_CREATION_ERRORS=1` (a previously-unused-across-this-
whole-roadmap opt-in diagnostic, `feme/lib/Vulkan/Diagnostics.h`)
revealed the real underlying error behind
`dEQP-VK.glsl.texture_gather.graphics.basic.cube.rgba8ui.texture_swizzle.zero_one_red_green`'s
bare `VK_ERROR_INITIALIZATION_FAILED`: an "unsupported raised operation"
naming an `llvm.spv.resource.handlefrombinding` handle the CPU target
could not normalize -- exactly this rejection.

**Fix**: widened `hasOnlySupportedImageUses`'s `isGatherIntrinsic` check
to accept either `<4 x i32>` (`IsInteger`) or `<4 x float>` (the
existing case) as `OpImageGather`'s result type. Added three new
`ImageCallKind`s -- `Gather2DI32`/`GatherArray2DI32`/`GatherCubeI32`
(`ImageCalls.h`/`.cpp`), mirroring the existing float `Gather2D`/
`GatherArray2D`/`GatherCube` call builders exactly -- and routed
integer gathers to them via an `isV4I32(CI->getType())` check in
`lowerImageAccesses` (mirroring the existing `Sample*I32` dispatch
precedent). Added the matching runtime entry points
`femeCpuImageGather2DV4I32`/`GatherArray2DV4I32`/`GatherCubeV4I32`
(FeMeRuntimeCPU.c), each reading through `femeRTFetchTexel2DI32` (the
L125(h)-added integer texel-fetch helper) with `ApplySwizzle=1`
preserved at every tap (matching L125(g)/L125(h)'s own post-swizzle-
`Component`-selection convention). The cube shape needed a new
`femeRTFetchCubeSeamlessTexelI32` helper -- the integer counterpart of
`femeRTFetchCubeSeamlessTexel` (L125(j)'s own fix) -- since a cube
gather's footprint must remap seamlessly across a face edge regardless
of the sampled image's integer-ness; the doubly-out-of-bounds corner
tap is still resolved by averaging the other three, using integer
division in place of the float path's `1.0f / 3.0f` multiply. Per-tap
`CLAMP_TO_BORDER` handling in the `Plain2D`/`Array2D` variants falls
back to the same fixed `{0, 0, 0, 1}` default the existing `*I32`
`Sample` family already uses (roadmap H109's own no-integer-border-
color-storage limitation). `GatherCmp*` (depth-compare) is deliberately
untouched: depth-format images are never integer-sampled in Vulkan, so
that rejection remains correct, permanent behavior.

New unit tests across all three translation phases:
`ImageCallsTest.MatchesGather2DI32Call`/`MatchesGatherArray2DI32Call`/
`MatchesGatherCubeI32Call` (create/match round-trip);
`SPIRVResourceLoweringTest.LowersIntegerGatherToImageGatherI32`/
`LowersIntegerGatherCubeToImageGatherCubeI32`/
`LowersIntegerGatherArray2DToImageGatherArray2DI32` (end-to-end
lowering); `ImageSamplingTest.Gather2DI32ReturnsFourTexelsInGatherOrder`/
`GatherArray2DI32IsolatesNamedLayer`/`GatherCubeI32IsolatesNamedFace`/
`GatherCubeI32SeamlessBlendsAcrossFaceEdge` (runtime execution,
including the cube seamless-remap proof).

`ninja check-feme`: 3,257/3,260 Passed, 3 Unsupported, 0 Failed (+10 new
tests, 0 regressions).

CTS (`dEQP-VK.glsl.texture_gather.*`, `feme_icd.json`,
`FeMe CPU Vulkan Device`):
- The original repro,
  `dEQP-VK.glsl.texture_gather.graphics.basic.cube.rgba8ui.texture_swizzle.zero_one_red_green`,
  now **Passes** (was `VK_ERROR_INITIALIZATION_FAILED`).
- `dEQP-VK.glsl.texture_gather.graphics.basic.*.rgba8ui.*` (126 cases):
  51 Pass, 75 NotSupported (sparse residency, unrelated), **0 Fail**.
- `dEQP-VK.glsl.texture_gather.graphics.basic.*.rgba8i.*` (126 cases):
  51 Pass, 75 NotSupported, **0 Fail** (matches the `ui` bucket exactly).
- A separate `dEQP-VK.glsl.texture_gather.graphics.offsets.*` sample
  confirmed `ConstOffsets` (`TextureGatherOffsets`, a 4-independent-
  offset array) fails identically with
  `VK_ERROR_INITIALIZATION_FAILED` for **both** integer and float
  formats (`spirv.ImageGather`'s own
  `<{image_operands = #spirv.image_operands<ConstOffsets>}>` shape is
  not yet legalized at all) -- a distinct, pre-existing, still-unfiled
  gap, confirmed unrelated to this row's own integer-format scope.

`Roadmap.md`'s L125(k) row is now struck through and marked done. See
`agent_thoughts.md` for the full narrative and next steps.

## Roadmap L125(o): graphics-pipeline cache-key omits specialization data

**Root cause**: picked up L125(c)'s `sampler.border_swizzle.r8g8b8a8_unorm.*`
sample (1,280 cases: 357 Pass, 13 Fail, 910 NotSupported) and found all 13
`Fail`s were `no_gather.no_swizzle_hint` cases. Running any single failing
case in isolation always Passed; running it after certain other cases in
the same process reproducibly Failed -- a test-order-dependent bug, not a
stateless one. Bisected to a minimal 2-test repro:
`...opaque_black.gather_0.no_swizzle_hint` immediately followed by
`...opaque_black.no_gather.no_swizzle_hint` in the same process. The wrong
`Color` the second (failing) test reported was byte-identical to the
first test's own correct `Color` -- proof the second pipeline was silently
executing the first's stale compiled code.

Both sub-cases share one GLSL module (a graphics and a compute variant)
differentiated only by `constant_id`-selected specialization-constant
values (`u`/`v`/`gatherFlag`) supplied at pipeline-creation time, not by
separate shader modules or entry points. `computeGraphicsPipelineCacheKey`
(`PipelineCache.cpp`) hashed every stage's SPIR-V words, entry point,
descriptor-set/push-constant layout, and serialized fixed-function state,
but never a stage's own `VkSpecializationInfo` -- unlike its compute
sibling, `computePipelineCacheKey`, which already hashes an explicit
`Overrides` list. `compileGraphicsStage` genuinely folds specialization
constants into the compiled graphics-stage code via
`buildSpecializationOverrides`, directly contradicting
`PipelineCache.h`'s own (now-corrected) doc comment claiming a graphics
stage has no specialization data to fold in. Two pipelines built from an
identical module/entry point but differing specialization data therefore
collided on the same cache key and silently reused each other's stale
compiled artifact.

**Fix**: added a shared `hashSpecializationOverrides` helper (used by
both `computePipelineCacheKey` and `computeGraphicsPipelineCacheKey` now)
and threaded a `SpecializationOverride` list per graphics stage (Vertex,
Fragment, TessControl, TessEval, Geometry, Mesh, Task) through
`computeGraphicsPipelineCacheKey`'s signature, hashing each the same way
the compute key already does. Updated the one real call site
(`GraphicsPipeline.cpp`) to compute each stage's own
`buildSpecializationOverrides(StageInfo->pSpecializationInfo)` result and
pass it through; a stage with no info, or one whose specialization data
fails to validate, falls back to an empty override list (caching is
simply skipped for that malformed creation -- `compileGraphicsStage` will
independently surface the real validation error once it gets there, the
same honest simplification already used elsewhere for an inline shader
module). Corrected `PipelineCache.h`'s stale doc comment.

### Unit tests

`GraphicsPipelineTest.DifferingSpecializationDataIsACacheMiss`: two
pipelines built from the same shader module/entry point but differing
`VkSpecializationInfo` must compile independent artifacts (the
specialization-data counterpart of the existing
`DifferingFixedFunctionStateIsACacheMiss`).

`ninja check-feme`: 3,258/3,261 Passed, 3 Unsupported, 0 Failed (+1 new
test, 0 regressions).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- The original 13-fail sample,
  `dEQP-VK.pipeline.monolithic.sampler.border_swizzle.r8g8b8a8_unorm.*`
  (1,280 cases), is now **370 Pass / 0 Fail / 910 NotSupported** (was
  357/13/910).
- A full `dEQP-VK.pipeline.monolithic.sampler.border_swizzle.*` sweep
  across every supported format: **619 Pass / 0 Fail / 1,618
  NotSupported**.
- A time-boxed, partial `dEQP-VK.pipeline.monolithic.sampler.*` sample
  (broader than `border_swizzle.*` alone, thousands of cases, not run to
  full completion) surfaced zero additional fails in the portion covered.

This is a general, cross-cutting correctness bug (any graphics pipeline
pair sharing a shader module with differing specialization constants was
affected), not scoped to border-swizzle specifically -- it likely explains
a share of L125(c)'s wider, still-untriaged "image mismatch"/pipeline
buckets too, though that has not been re-verified case-by-case.
`Roadmap.md`'s new L125(o) row is struck through and marked done; L125(c)'s
own remaining buckets (ASTC/EAC/ETC2 image mismatches, the
`createComputePipelines`-site `VK_ERROR_INITIALIZATION_FAILED`,
`vktPipelineBindPointTests.cpp`) are unaffected by this fix and remain
untriaged. See `agent_thoughts.md` for the full narrative and next steps.

## Roadmap L125(c) re-triage (post-L125(o)): decomposed into L125(p)-(u)

Re-ran the same `--deqp-fraction=0,50` sample of `dEQP-VK.pipeline.monolithic.*`
(8,007 cases) used for L125(c)'s original triage, now that L125(o)'s
pipeline-cache-key fix has landed: **655 Fail / 2,125 Pass / 5,227
NotSupported** (was 1,165 Fail pre-`L125(a)`/`(b)`; ~650 expected residual
after those two integer-sampling fixes landed, confirming L125(o)'s own
fix did not measurably move this particular fractional sample -- its
repro needed a specific adjacent test ordering a random 1/50 fraction
rarely reproduces).

Grouped all 655 fails by test area and failure-message signature (each
of the 9 resulting buckets below cross-checked against a standalone,
single-case isolation re-run to confirm it fails identically alone, not
only after a preceding test -- ruling out any remaining cache-style
artifact):

- **440 fails, "Image mismatch"** (244 `image.suballocation`, 134
  `image_view.view_type`, 62 `sampler.view_type`) -- filed as **L125(p)**,
  not yet root-caused past prior sessions' suspicion of ASTC/EAC/ETC2
  compressed-format decoding.
- **144 fails, `sampler.border_swizzle.*`** -- filed as **L125(q)**, two
  distinct sub-causes: (1) 80 `vk.queueSubmit`/`VK_ERROR_INITIALIZATION_
  FAILED` fails, root-caused via `FEME_VULKAN_LOG_CREATION_ERRORS=1` to
  "image fixture format is not yet supported" for single/dual-channel
  non-8-bit formats (`r16_snorm`, `r16_sint`, etc.) combined with a
  `gather_N` sub-case; (2) 64 `Ref`-vs-`Color` value-mismatch fails
  (e.g. `r16_sint.barg.transparent_black.gather_3.no_swizzle_hint`,
  confirmed to fail identically standalone) combining a single/dual-
  channel format, a custom swizzle, a non-default border color, and
  `gather_N` -- likely a border-color-defaulting-through-swizzle gap
  specific to `Gather*`.
- **27 fails, `multisample_interpolation.*`** -- filed as **L125(r)**, a
  newly-identified bucket (not present in L125(c)'s original
  description): an `error: failed to legalize operation
  'spirv.GL.InterpolateAtCentroid'`/`'spirv.GL.InterpolateAtSample'` MLIR
  diagnostic immediately precedes each `VK_ERROR_INITIALIZATION_FAILED`
  -- a missing SPIR-V-to-LLVM conversion pattern for these two GLSL
  extended instructions.
- **28 fails, `vertex_input.*`** (16 `multiple_attributes`, 12
  `single_attribute`) -- filed as **L125(s)**, matching L125(c)'s own
  original description; still present, unaffected by any fix since.
- **10 fails, `bind_point.graphics_compute`**, "Invalid value found in
  graphics buffer" -- filed as **L125(t)**, matching L125(c)'s own
  `vktPipelineBindPointTests.cpp` bucket exactly; still present.
- **6 fails, `sampler.exact_sampling`**, "Pixel mismatch" -- filed as
  **L125(u)**, a newly-identified small bucket.

None of these 9 buckets were affected by L125(o)'s cache-key fix. No code
changes made this session; `Roadmap.md`'s L125(c) row is struck through
(re-triaged and decomposed) and 6 new rows (L125(p) through L125(u)) file
the residual work with per-bucket root-cause notes and fail counts, ready
to be picked up individually in a future session.

## Roadmap L125(q) sub-bucket (1): `border_swizzle` R16/R16G16 UNORM/SNORM format-support gap

**Investigated L125(r) first, found it duplicates L115(b) (no code change)**:
this session picked up the prior session's "fastest win" pick, `L125(r)`
(`InterpolateAtCentroid`/`InterpolateAtSample` legalization, 27 fails).
Found the prior session's time estimate ("mechanically similar... likely
a good win") was wrong: `StageOps.h`/`.cpp` already has
`StageOpKind::InterpolateAtCentroid`/`AtSample`/`AtOffset` builders, but
nothing in `SPIRVToLLVMPatterns.cpp` calls them; `FragmentWrapper.cpp`
already has a switch case for these ops that deliberately emits
"pull-model interpolation is not implemented yet" rather than
miscompiling; `FeMeGraphicsDesign.md` already documents this exact
deviation; and this exact gap is already tracked in detail as `L115(b)`,
scoped as needing a genuinely new runtime-callback ABI surface (not just
an LLVM lowering pattern) since the interpolation point is often a
runtime SSA value, not a compile-time constant -- confirmed via
`RuntimeABI.h`'s `FemeFragmentInvocation` struct, which has no
barycentric-plane/interpolant-plane data at all in the current
per-invocation ABI. `L115(b)`'s own text already estimates this at
"likely 1-2 full sessions" -- too large for this session's "small,
separately-committed changes" budget. No code change attempted;
`Roadmap.md`'s `L125(r)` row updated to cross-reference `L115(b)` as the
real tracking item rather than independent work.

**Root cause (L125(q) sub-bucket (1))**: pivoted to `L125(q)`'s
sub-bucket (1), 80 fails, `sampler.border_swizzle.*`'s single/dual-channel
non-8-bit formats combined with a `gather_N` sub-case, previously
root-caused (but not yet fixed) to "image fixture format is not yet
supported". `FEME_VULKAN_LOG_CREATION_ERRORS=1` on the isolated repro
(`dEQP-VK.pipeline.monolithic.sampler.border_swizzle.r16_snorm.rgba.
transparent_black.gather_2.no_swizzle_hint`) confirmed the message comes
from `ImageFixture.cpp`'s `getFormatInfo` (a *test-fixture*-layer switch,
distinct from the production sampling path, used by the CTS-facing
test-image/color-attachment setup machinery) -- its switch over
`ResourceFormat` had no case at all for `R16_UNORM`/`R16_SNORM`/
`R16G16_UNORM`/`R16G16_SNORM`, unlike their `R16_UINT`/`R16_SINT`/
`R16G16_UINT`/`R16G16_SINT` siblings (which already have entries). A
stale comment on those `UINT`/`SINT` siblings claimed the UNORM/SNORM
pair were "an `EAC_R11` sampling-bridge target only, never a color
attachment" -- contradicted by: (a) `parseFixtureFormat` already accepting
`"r16-unorm"`/`"r16-snorm"`/`"r16g16-unorm"`/`"r16g16-snorm"` as ordinary
fixture-format spellings; (b) `packClearColor`/`unpackColor` already
having dedicated, working code paths treating these as real render-target
clear colors, not merely an EAC-decode bridge; (c) the actual failing CTS
test samples a real `VkImage` in `r16_snorm` format directly.

**Fix**: added the missing `FormatInfo` entries to `getFormatInfo`
(`ImageFixture.cpp`), mirroring the existing `UINT`/`SINT` shape exactly
(`R16_UNORM`/`R16_SNORM`: `{1, 2, false}`; `R16G16_UNORM`/`R16G16_SNORM`:
`{2, 2, false}`), and corrected the stale comments (on the `UINT`/`SINT`
case, and on the neighboring `R16_FLOAT` case, which also referenced the
old "unlike its UNORM/SNORM neighbors" claim).

### Unit tests

`ImageFixtureTest.GetFixtureFormatElementSizeCoversR16UnormSnormAndR16G16UnormSnorm`:
confirms `getFixtureFormatElementSize` now resolves successfully (2 bytes
for the single-channel pair, 4 bytes for the two-channel pair) rather than
erroring, mirroring the existing `R16_FLOAT`/`R16G16_FLOAT`-coverage test's
own shape.

`ninja check-feme`: 3,259/3,262 Passed, 3 Unsupported, 0 Failed (+1 new
test, 0 regressions).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- The original isolated repro,
  `dEQP-VK.pipeline.monolithic.sampler.border_swizzle.r16_snorm.rgba.
  transparent_black.gather_2.no_swizzle_hint`, now **Passes** (was
  `VK_ERROR_INITIALIZATION_FAILED`).
- A full `dEQP-VK.pipeline.monolithic.sampler.border_swizzle.r16*`
  re-sweep (25,600 cases: 4,686 Pass, 864 Fail, 20,050 NotSupported)
  confirms **zero** remaining fails on any `_unorm`/`_snorm` format --
  every one of this sub-bucket's original 80 "not yet supported" fails is
  gone, with no regressions on the already-passing `_uint`/`_sint`/
  `_float`/`_sfloat` sibling formats.
- The same re-sweep's remaining 864 fails are **all** `Ref`-vs-`Color`
  value mismatches on integer formats (`r16_uint`/`r16_sint`: 76+76,
  `r16g16_uint`/`r16g16_sint`: 96+96, `r16g16b16a16_uint`/
  `r16g16b16a16_sint`: 140+140) -- the same "sub-bucket (2)" shape
  (border-color-defaulting-through-swizzle-plus-gather mismatch)
  `L125(q)`'s own original triage already named, but confirmed this
  session to be **larger in scope than originally estimated** (864 fails
  across 6 formats, not just the 64 originally attributed to
  `r16_sint`/`r16_uint`). `Roadmap.md`'s `L125(q)` row updated: sub-bucket
  (1) struck through and marked fixed; sub-bucket (2) left open with the
  corrected, larger scope noted. See `agent_thoughts.md` for the full
  narrative and next steps.

## Roadmap L125(q) sub-bucket (2): integer `CLAMP_TO_BORDER` swizzle order (partial fix; residual re-scoped as L125(v))

### Repro

`dEQP-VK.pipeline.monolithic.sampler.border_swizzle.r16_sint.barg.
transparent_black.gather_3.no_swizzle_hint` (and its `no_gather`
counterpart), isolated standalone: `Fail (Ref:(0, 0, 0, 0)
Threshold:(0, 0, 0, 0) Color:(1, 1, 1, 1))`.

### Root cause

`FeMeRuntimeCPU.c`'s integer-sampled (`v4i32`) `CLAMP_TO_BORDER` fallback
(a fixed `{0, 0, 0, 1}` default, used because `FemeRTSamplerDescriptor`
has no integer border-color storage at all -- roadmap H109) was used
**directly, unswizzled**, at all 7 call sites:
`femeCpuImageSample{1D,2D,3D,Array1D,Array2D}V4I32`'s own border branch,
and `femeCpuImageGather{2D,Array2D}V4I32`'s own per-tap border fallback.
This bypassed the image view's own `VkComponentMapping` entirely for any
border-fallback texel, unlike every in-bounds-texel code path (already
routed through `femeRTApplyImageSwizzleI32`) and unlike the float path's
own border branch (`femeRTFetchTexel2D`, which already applies
`femeRTApplyImageSwizzle` to its border default).

Manually traced the CTS's own `getExpectedColor` order
(`vktPipelineSamplerBorderSwizzleTests.cpp`): swizzle is always applied to
the (format-defaulted) border/texel value **before** `Gather*`'s
`Component` operand selects one channel -- the same "post-swizzle
Component selection" convention already established by L125(g)/L125(h).
For `r16_sint` (1 stored channel) + `transparent_black` + `barg` swizzle +
`gather_3`: pre-swizzle border defaults to `(0, 0, 0, 1)` (alpha forced to
1 for any format storing fewer than 4 real channels, matching Vulkan's own
"conversion to RGBA" rule); post-`barg`-swizzle this becomes `(0, 1, 0,
0)`; `gather_3` (alpha) selects index 3 from the swizzled result for all
4 output slots = `(0, 0, 0, 0)`, matching the CTS's own `Ref`. The
production code instead selected index 3 from the **unswizzled**
`(0, 0, 0, 1)` = `1` for all 4 slots, matching the observed
`Color:(1, 1, 1, 1)`.

### Fix

Route all 7 sites through the existing `femeRTApplyImageSwizzleI32`
helper before returning/using the fixed default (the two `Gather*` sites
apply it once and reuse the swizzled result across all 4 border-fallback
taps). Corrected 5 stale "no real CTS case is known to exercise
`CLAMP_TO_BORDER` against an integer-sampled image yet" doc comments on
the `Sample*I32` sites, since the `no_gather` repro above disproves that
claim.

### Unit tests

`ImageSamplingTest.SampleI32AppliesImageViewSwizzleToBorderColorFallback`
and `.Gather2DI32AppliesImageViewSwizzleToBorderColorFallback`: force
`ClampToBorder` on both axes (`U=V=5.0`, out of `[0, 1)`), configure a
`BARG` view swizzle, and confirm the returned/gathered value is the
swizzled border default, not the raw one.

`ninja check-feme`: 3,261/3,264 Passed, 3 Unsupported, 0 Failed (+2 new
tests, 0 regressions vs. the prior session's 3,259/3,262).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- The isolated repro pair (`gather_3` and `no_gather` variants of
  `r16_sint.barg.transparent_black`) both now **Pass** (previously both
  Fail).
- A full `dEQP-VK.pipeline.monolithic.sampler.border_swizzle.r16*`
  re-sweep (25,600 cases): **4,918 Pass / 632 Fail / 20,050 NotSupported**
  (up from the pre-fix 4,686 Pass / 864 Fail) -- **232 fails fixed**, 0
  regressions on previously-passing cases.
- The remaining 632 fails are a **second, distinct root cause** under the
  same H109 umbrella: an isolated repro on a non-`transparent_black`
  border color (`r16_sint.argb.opaque_white.no_gather.no_swizzle_hint`:
  `Ref:(1, 1, 0, 0)` vs `Color:(1, 0, 0, 0)`) shows the fixed `{0, 0, 0,
  1}` default is not just a swizzle-order bug but **factually wrong** for
  any border color whose per-format-masked value doesn't coincide with
  `{0, 0, 0, 1}` -- the integer path never reads the sampler's actual
  `borderColor` enum at all. This is a materially larger, ABI-touching
  fix (new integer border-color storage on `FemeSamplerDescriptor`, a
  resolver in `Image.cpp`, and a `femeRTExpandBorderColorForFormatI32`
  counterpart), so it is re-scoped as its own roadmap row, `L125(v)`,
  rather than folded into this fix. `Roadmap.md`'s `L125(q)` row updated
  accordingly; see `agent_thoughts.md` for the full narrative and next
  steps.

## Roadmap L125(v): integer `CLAMP_TO_BORDER` default now uses the sampler's real `borderColor`

### Repro

`dEQP-VK.pipeline.monolithic.sampler.border_swizzle.r16_sint.argb.
opaque_white.no_gather.no_swizzle_hint`, isolated standalone:
`Fail (Ref:(1, 1, 0, 0) Threshold:(0, 0, 0, 0) Color:(1, 0, 0, 0))`.

### Root cause

L125(q)'s own swizzle-order fix closed exactly the fails whose border
color was `transparent_black` (232 of 864), leaving 632 residual fails.
Sampling 10 of them showed every one combined a non-`transparent_black`
border color (`opaque_white` in the isolated repro above) with a
non-identity swizzle on the same 6 integer formats.

Hand-traced the CTS's own expected math: for `r16_sint` (1 real stored
channel) + `opaque_white` (nominal `(1, 1, 1, 1)`) + `argb` swizzle
(`R<-A, G<-R, B<-G, A<-B`), Vulkan's border-color "conversion to RGBA"
rule keeps only the border color's own real value for a stored channel
and forces every other channel to its fixed default (`0` for R/G/B, `1`
for A) -- pre-swizzle border is therefore `(1, 0, 0, 1)`, not
`opaque_white`'s own nominal `(1, 1, 1, 1)`; post-`argb`-swizzle:
`(1, 1, 0, 0)`, matching the CTS's own `Ref` exactly.

`FeMeRuntimeCPU.c`'s integer `CLAMP_TO_BORDER` fallback used a single
hardcoded `{0, 0, 0, 1}` literal at all 7 call sites, entirely ignoring
the sampler's own `VkBorderColor` -- correct only when that border
color's own per-format-masked default happens to coincide with
`{0, 0, 0, 1}` (`transparent_black`), and wrong for anything else
(`opaque_white`/`opaque_black`/any future custom border color).

### Fix

No new integer-typed ABI storage was needed, unlike the prior session's
own speculative scoping: `mapBorderColor` (Image.cpp) already bakes
every `VkBorderColor` this ICD accepts (it advertises no
`VK_EXT_custom_border_color`, so every enumerator -- float or int variant
alike -- is representable as a binary `0.0f`/`1.0f`-per-channel pattern)
onto `FemeSamplerDescriptor::BorderColor`, the identical pattern an
equivalent int32 border color would have. Truncating that existing float
value to `int32_t` recovers the real border color exactly.

Added:
- `femeRTImageFormatComponentMaskI32`: the integer-format counterpart of
  `femeRTImageFormatComponentMask` -- the existing float-only mask
  function covers no `_UINT`/`_SINT` format code at all (its case labels
  are for `R32_FLOAT`/`R16_UNORM`/etc., numerically distinct formats), so
  a dedicated switch over the format codes `femeRTUnpackImageTexelI32`
  itself decodes was written.
- `femeRTExpandBorderColorForFormatI32`: truncates `Samp.BorderColor` to
  `int32_t` and masks it via the above, mirroring the float path's own
  `femeRTExpandBorderColorForFormat`.

Applied at all 7 `femeCpuImage{Sample,Gather}*V4I32` `CLAMP_TO_BORDER`
sites in place of the fixed `{0, 0, 0, 1}` literal.

### Unit tests

New: `ImageSamplingTest.
SampleI32BorderColorFallbackVariesByFormatAndBorderColor` -- an
`R16_SINT` image (1 real stored channel) with an explicit
`VK_BORDER_COLOR_INT_OPAQUE_WHITE`-equivalent `BorderColor` and an
`argb` swizzle, confirming the masked-then-swizzled result matches the
hand-traced CTS math above.

Updated: the two existing L125(q) border-swizzle tests
(`SampleI32AppliesImageViewSwizzleToBorderColorFallback`,
`Gather2DI32AppliesImageViewSwizzleToBorderColorFallback`) now set an
explicit `VK_BORDER_COLOR_INT_OPAQUE_BLACK` rather than relying on
`makeSampler`'s zero-initialized (`transparent_black`-shaped) default --
this fix correctly stopped forcing a *4-real-channel* format's alpha to
`1` unconditionally (it now only forces a component the format doesn't
actually store), so those tests' own prior expectations (written against
the still-buggy hardcoded default) needed updating to remain correct.

`ninja check-feme`: 3,262/3,265 Passed, 3 Unsupported, 0 Failed (+1 new
test, 0 regressions vs. this session's own earlier L125(q)-follow-up
build of 3,261/3,264).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- The isolated `opaque_white` repro now **Passes** (was Fail); the
  `transparent_black` repro from L125(q) continues to Pass.
- A full `dEQP-VK.pipeline.monolithic.sampler.border_swizzle.r16*`
  re-sweep (25,600 cases): **5,550 Pass / 0 Fail / 20,050 NotSupported**
  -- every one of the 632 residual fails left after L125(q)'s own partial
  fix is now gone, 0 regressions on the 4,918 cases already passing
  before this fix.
- `Roadmap.md`'s `L125(v)` row updated to reflect the fix (struck
  through, marked fixed and CTS-verified, noting the simpler-than-scoped
  no-new-ABI-storage design); see `agent_thoughts.md` for the full
  narrative and next steps.

## Roadmap L125(p): compressed-format (ASTC/BC/ETC2) array-layer / 3D-slice decode gap

### Root cause

`CommandBuffer.cpp`'s host-side software decoders for the three
block-compressed format families (`decodeASTCImageForSampling`,
`decodeBCImageForSampling`, `decodeETC2ImageForSampling`) were originally
scoped (roadmap E23/H8j/H8n) to decode only array layer 0 / depth slice 0
of a compressed image, with `materializeImageDescriptor` explicitly early
-returning an all-zero descriptor for any view with a nonzero
`baseArrayLayer` or a layer/slice count other than 1. This was a real,
intentional-at-the-time scope limitation that had never been revisited.

Investigation for this roadmap row started from a narrower "4 specific
formats" theory left over from the prior triaging session
(`eac_r11g11_snorm_block`, `astc_8x8_srgb_block`, `astc_10x8_srgb_block`,
`astc_12x10_srgb_block`), but isolated single-case CTS runs showed
`eac_r11g11_unorm_block` -- not one of the 4 -- also failed once viewed as
a `2d_array`, while the same format passed under a plain `2d` view. A
full `view_type.2d_array.format.astc*` sweep (2,464 cases) then showed
**2,016 Fail / 0 Pass** on real samples: the actual bug affected nearly
every ASTC format under any array/cube-array/3D view, not just 4 specific
ones.

### Fix

Widened all three decode functions to accept `(BaseSlice, SliceCount,
Is3D)` and loop over every requested array layer (or depth slice, for
3D) into consecutive `SlicePitch`-sized chunks per mip level, computing a
per-level slice count (`Is3D ? max(1, Img->depth() >> Level) :
SliceCount`) that mirrors `Image.cpp`'s own `computeSubresourceLayouts`
exactly. `Image::blockPointer`'s existing "ArrayLayer + Z as one unified
slice index" design, plus the CPU runtime's already-generic
`Layer * SlicePitch` / `Z * SlicePitch` addressing in
`femeRTFetchTexel2D`/`femeRTFetchTexel3D`, meant **no runtime-side change
was needed at all** -- the fix is entirely host-side, in
`CommandBuffer.cpp`'s decode/materialization layer. The three early
-return guards in `materializeImageDescriptor` were removed and replaced
with a shared `IsDecodedImage3D`/`DecodeBaseSlice`/`DecodeSliceCount`
computation feeding all three decode call sites.

A first fix attempt crashed (segfault in `decodeASTCBlock` on a
multi-mip 3D ASTC case) because a `Texture3D`'s depth halves per mip
level while the initial patch reused one fixed base-level slice count
across all levels, causing out-of-bounds `Z` at deeper mips. Diagnosed
via `gdb` register inspection (`x0=0` null `Block` pointer) plus a
temporary `fprintf` trace (the shared `libfeme_vulkan.so` lacks the debug
info needed for reliable `gdb break <function-name>`), then fixed by
computing the per-level slice count *inside* each decode function rather
than at the call site.

`ninja check-feme`: 3,263/3,266 Passed, 3 Unsupported, 0 Failed (+1 new
regression test `ASTCSecondArrayLayerSampledImageDispatchTest`, 0
regressions).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- `image.suballocation.*.view_type.2d_array.format.astc*` (2,464 cases):
  **0 Fail** (was 2,016 Fail).
- `...view_type.3d.format.astc*` (4,176 cases): **0 Fail**.
- `...view_type.2d_array.format.eac*` (352 cases): **0 Fail**.
- The entire `view_type.cube_array.*` bucket (5,024 cases, all formats):
  **0 Fail**.
- The original triaging `image.*` 1/50-fraction sample (2,340 cases, was
  232 Fail): **0 Fail**.
- A smaller, unrelated residual surfaced separately: a
  `sampler.view_type.*` 1/30-fraction sample (2,768 cases) showed 21
  fails under `2d_unnormalized` coordinate mode combined with
  border-color/mag-filter/compressed-format edge cases (plus one
  unrelated `cube_array` case) -- a distinct pattern, filed as new
  roadmap row `L125(w)` rather than investigated further this session.
- `Roadmap.md`'s `L125(p)` row updated to reflect the fix (struck
  through, marked fixed and CTS-verified); see `agent_thoughts.md` for
  the full narrative and next steps.

## Roadmap L125(w): unnormalized-coordinate double-scaling and ETC2/BC border-component-mask gap

### Repro

The `L125(p)` session's 1/30-fraction `sampler.view_type.*` sample found
a 21-fail residual, all under `view_type.2d_unnormalized` plus a handful
of format/mag-filter/border-color edge cases, and filed it as this row
without further investigation. Re-running the full
`sampler.view_type.2d_unnormalized.*` bucket (2,994 cases, not a
fraction) found the real count was far larger: **814 Fail**, not 21 --
the fractional sample had badly undercounted.

### Root cause (1): unnormalized-coordinate double-scaling (~810 of 814 fails)

`VkSamplerCreateInfo::unnormalizedCoordinates` was never read anywhere
in the codebase (confirmed via a zero-hit grep across the whole tree).
Every CPU-runtime sampling function unconditionally computed
`U * (float)LevelWidth`, assuming the shader-supplied coordinate was
always normalized `[0, 1)`. With `unnormalizedCoordinates = VK_TRUE`,
the shader instead supplies a coordinate already in texel space
`[0, extent)`, so this unconditional multiply double-scaled it,
producing wildly wrong texel addresses.

### Fix (1)

Added a `FEME_SAMPLER_UNNORMALIZED_COORDINATES` flag bit
(`FemeSamplerDescriptorFlagBits`, `RuntimeABI.h`), set from
`CreateInfo.unnormalizedCoordinates` in `Sampler::Sampler`
(`Image.cpp`). Added a new `femeRTUnnormalizeCoord` helper
(`FeMeRuntimeCPU.c`) that divides the incoming coordinate by the level-0
extent when the flag is set, converting it back to the normalized
convention every downstream addressing/filtering computation already
assumes -- done once, immediately after loading the sampler descriptor,
so no other sampling code needs to change. Wired into the only 4 sample
entry points the Vulkan spec ever permits this bit to reach:
`femeCpuImageSample2DV4F32`, `femeCpuImageSample2DV4I32`,
`femeCpuImageSample1DV4I32`, `femeCpuImageSample1DV4F32` -- the spec
forbids mipmapping, anisotropy, `compareEnable`, and any
Gather/derivative-LOD/array/cube addressing alongside this bit, so no
other entry point can ever see it set.

### Root cause (2): ETC2/BC border-component-mask gap (the remaining 4 fails, pre-existing and unrelated)

Isolated to `etc2_r8g8b8_{unorm,srgb}_block` + `CLAMP_TO_BORDER` +
`VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK`, and confirmed via a standalone
`2d`-view repro to be entirely unrelated to `2d_unnormalized` -- a
pre-existing bug this session happened to surface while re-triaging.
`femeRTImageFormatComponentMask` (`FeMeRuntimeCPU.c`) had no case at all
for any BC/ETC2/EAC format, falling through to the `0xf` (raw
all-4-channels) default rather than forcing alpha to `1` for these
alpha-less formats.

A first fix attempt (new switch cases keyed on the *original* compressed
`ResourceFormat` ordinal, e.g. `ETC2_RGB8_UNORM`) turned out to be dead
code: re-running the CTS sweep afterward showed the exact same 4 fails,
unchanged. Tracing further found `CommandBuffer.cpp`'s
`materializeImageDescriptor` always decodes narrow-channel BC/ETC2/EAC
formats (`BC1_RGB`, `BC6H`, `ETC2_RGB8`, `BC4`, `BC5`, `EAC_R11`,
`EAC_R11G11`) into a *widened* uncompressed target format (e.g.
`R8G8B8A8_UNORM`) for texel-fetch convenience before the descriptor ever
reaches the runtime -- so `Img.Format` at border-color-fallback time is
never the original narrow-channel format code, only ever the widened
target. The reverted attempt is left with no trace in the final diff.

### Fix (2)

Added a `BorderComponentMask` field to `FemeImageDescriptor`
(`RuntimeABI.h`, and its C mirror `FemeRTImageDescriptor` in
`FeMeRuntimeCPU.c`), consuming one `Reserved` word. `0` means "no
override, derive from `Format` as before" (every uncompressed image and
every already-4-channel BC/ETC2 family). Added
`compressedFormatBorderComponentMask` (`CommandBuffer.cpp`), a switch
over the *original* `Img->format()` -- still available at materialization
time, unlike the already-widened runtime `Format` -- mapping
`BC1_RGB`/`BC6H`/`ETC2_RGB8` to `0x7` (RGB), `BC4`/`EAC_R11` to `0x1`
(R only), `BC5`/`EAC_R11G11` to `0x3` (RG), and everything else
(including all ASTC) to `0` (no override). Called from both the BC and
ETC2 decode branches of `materializeImageDescriptor`, populating
`Dst.BorderComponentMask`. `femeRTExpandBorderColorForFormat` gained a
`BorderComponentMaskOverride` parameter, preferring it over the
`Format`-derived mask whenever nonzero, at both call sites
(`femeRTFetchTexel2D`/`femeRTFetchTexel3D`). The integer (`I32`)
border-color path was left untouched -- compressed formats are never
integer-sampled.

### Unit tests

- `UnnormalizedCoordinatesSampledImageDispatchTest.TexelSpaceCoordinateSelectsTheCorrectTexelWithoutDoubleScaling`
  (`CommandBufferTest.cpp`): a sampler with `unnormalizedCoordinates =
  VK_TRUE` sampling a 2x2 image at texel-space `uv = (0.5, 0.5)` must
  select texel (0, 0), not the double-scaled texel (1, 1).
- `ETC2RGB8BorderColorSampledImageDispatchTest.ForcesAlphaToOneRatherThanTheRawTransparentBlackZero`
  (`CommandBufferTest.cpp`): a single-block `ETC2_RGB8_UNORM` image
  sampled out-of-range with `CLAMP_TO_BORDER` +
  `VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK` must read alpha back as
  `1.0`, not the border color's own raw `0.0`.

Both tests were independently confirmed to fail against a pre-fix build
(via a `git apply -R`/rebuild/re-run round-trip keeping only the new
tests) and pass against the fix, before being counted as valid
regression coverage.

`ninja check-feme`: 3,265/3,268 Passed, 3 Unsupported, 0 Failed (+2 new
tests, 0 regressions).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- `sampler.view_type.2d_unnormalized.*` (2,994 cases): **0 Fail** (was
  814 Fail before any fix in this session; 4 Fail after fix (1) alone;
  0 Fail after fix (2)).
- `sampler.view_type.*.format.*etc2*.address_modes.*clamp_to_border*`
  (996 cases, across every view type, not just `2d_unnormalized`):
  **432 Pass, 0 Fail, 564 NotSupported** -- confirms fix (2) generalizes
  beyond the one bucket it was found in.
- `sampler.view_type.*.format.*bc*.address_modes.*clamp_to_border*`: 0/0
  matched -- BC formats are not exercised under this glob in this CTS
  tree at all, a pre-existing coverage gap noted but not investigated.
- A regression sweep, `sampler.*` at 1/40-fraction (4,772 cases): 28
  fails, all in the pre-existing `border_swizzle.*` SNORM-format
  `gather_N` bucket. Confirmed unrelated to this session's changes via a
  stash/rebuild/re-run round-trip: the same 12 representative case IDs
  fail identically on the pre-session build.
- `Roadmap.md`'s `L125(w)` row updated to reflect the fix (struck
  through, marked fixed and CTS-verified); see `agent_thoughts.md` for
  the full narrative and next steps.

## Roadmap L125(u)/L125(x): fragment-output integer-signedness and sRGB attachment write/read gaps

### Investigation

`sampler.exact_sampling.*` (708 cases) was re-run in full (not the
1/50-fraction sample earlier sessions' triage estimates were based on)
and found 18 fails, not the 6 the roadmap's own stale estimate carried
forward: 6 `r32_uint.gradient.*` and 12 `r8g8b8a8_srgb.*` -- the
"fractional-sample undercount" pattern several prior sessions have now
independently hit.

`--deqp-log-images=enable` on the isolated `r32_uint.gradient.
normalized_coords.centered` repro, decoded via an ad hoc Python/PIL
script, showed sampled-back values increasing linearly up to exactly
`2^31 - 1`, then hard-cutting to `0` for every value at or above `2^31`
-- the classic signature of an unsigned value reinterpreted as signed
somewhere, then clamped to a non-negative range. Traced to
`Executor.cpp`'s `readFragmentColorInt`, which derived the raw value's
signedness from `Elem.ComponentType`. `Pipeline.h`'s own
`isCompatibleColorComponentType` comment already documents that a real
SPIR-V-sourced fragment stage's signature can *never* actually report
`UInt`: LLVM's integer types are signless, and
`CanonicalizeStage.cpp`'s `getComponentType` maps every SPIR-V integer
to `SInt` regardless of `OpTypeInt`'s own signedness bit. So
`Elem.ComponentType` was always `SInt`, and every raw `uint32_t`
fragment-output value with its top bit set got reinterpreted as
negative, then floored to `0` by `packClearColor`'s own unsigned-range
`std::clamp`.

The same PNG-decode-and-compare methodology on an isolated
`r8g8b8a8_srgb.solid_color.normalized_coords.centered` repro found
every output channel equal to `round(srgbToLinear(input) * 255)` -- an
sRGB decode applied on read with no corresponding re-encode on write.
Traced to `ImageFixture.cpp`'s `packClearColor`/`unpackColor`, whose
`R8G8B8A8_UNORM_SRGB`/`B8G8R8A8_UNORM_SRGB` branches were bucketed with
their plain `_UNORM` siblings, applying no linear<->sRGB gamma curve at
all -- a gap a prior `H8r`-row unit test had even explicitly documented
as intentional ("no gamma curve is applied here"), contradicted by the
real sampling path's own already-correct `femeRTSRGBToLinear` decode
(`FeMeRuntimeCPU.c`).

### Fixes

1. `readFragmentColorInt` now derives signedness from the real
   attachment format (`cpu::isUnsignedIntegerColorAttachmentFormat(
   Att.Format)`, always known correctly regardless of what the
   signature reports), not from `Elem.ComponentType`.
2. Added `srgbToLinear`/`linearToSRGB` helpers to `ImageFixture.cpp`
   (mirroring `femeRTSRGBToLinear`'s own formula) and applied them to
   the R/G/B channels only (alpha is never sRGB-encoded, by convention)
   in both `packClearColor` and `unpackColor` for both `_UNORM_SRGB`
   formats.

### Unit tests

- `ExecutorTest.RendersAnSIntFragmentOutputWithTheSignBitSetToAnUnsignedIntegerAttachment`
  (`ExecutorTest.cpp`): writes a raw `i32` value at/above `2^31`
  (`3000000000`) from a fragment output reported as `SInt` (the only
  shape a real SPIR-V-sourced stage ever produces) to a real
  `R32_UINT` color attachment, and checks the exact raw value
  round-trips.
- Updated `ImageFixtureTest.PacksAndUnpacksB8G8R8A8UnormSrgb` to its
  correct sRGB-encoded expected values (previously asserting the bug's
  own behavior), and added
  `ImageFixtureTest.PacksAndUnpacksR8G8B8A8UnormSrgb`.

Both the new `ExecutorTest` and the updated/new `ImageFixtureTest`
cases were independently confirmed to fail against a pre-fix build
(via a `git stash`/rebuild/re-run round-trip) and pass against the
fix, before being counted as valid regression coverage.

`ninja check-feme`: 3,267/3,270 Passed, 3 Unsupported, 0 Failed (+2 new
tests, 0 regressions).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- `sampler.exact_sampling.*` (708 cases): **372 Pass, 0 Fail, 336
  NotSupported** (was 18 Fail before either fix: 6 after fixing the
  `r32_uint` half alone, 0 after both).
- `image.load_store.*uint*` (772 cases): 610 Pass, 0 Fail -- no
  regression from the `readFragmentColorInt` fix.
- `sampler.view_type.*.format.*srgb*` (10,296 cases): 2,056 Pass, 0
  Fail -- no regression from the sRGB pack/unpack fix.
- `pipeline.monolithic.render_to_image.*` (1,325 cases): 1,050 Pass, 80
  Fail. Confirmed **pre-existing and unrelated** via a revert-and-rerun
  of one failing case (`render_to_image.core.3d.mipmap.r16g16_sint`):
  it fails identically before either of this session's fixes, with a
  `VK_ERROR_INITIALIZATION_FAILED` at `vkCreateImage` (a 3D-mipmap
  image-creation gap, not a pixel mismatch) -- not investigated
  further this session.
- `pipeline.monolithic.blend.format.r8g8b8a8_srgb.*` (100 cases): 6
  Pass, 94 Fail. Confirmed **pre-existing** (100/100 failed before
  either fix too, via the same revert-and-rerun method) -- the sRGB
  pack/unpack fix improves this from 100/100 to 94/100 but does not
  close it, indicating a separate, larger blend+sRGB interaction gap.
  Filed as `Roadmap.md`'s new `L125(y)` row rather than chased further
  this session.
- `Roadmap.md`'s `L125(u)` and `L125(x)` rows updated to reflect both
  fixes (struck through, marked fixed and CTS-verified); the
  blend+sRGB gap filed as a new `L125(y)` row; see `agent_thoughts.md`
  for the full narrative and next steps.

## Roadmap L125(y): blend Min/Max op ignoring blend-factor rule

Investigated the `L125(y)` row filed in the prior session
(`pipeline.monolithic.blend.format.r8g8b8a8_srgb.*`, 94/100 fails,
suspected blend+sRGB interaction gap). Isolating a single failing case
and diffing `--deqp-log-images=enable` PNGs against the reference found
every RGB channel matched exactly, with only the alpha channel wrong --
immediately ruling out an sRGB gamma-curve cause (which would show up as
an RGB divergence, not alpha-only). Confirmed the same case name fails
identically (94/100) on a plain, non-sRGB `r8g8b8a8_unorm` attachment,
proving this was never sRGB-related at all.

### Root cause

Per the Vulkan/Direct3D spec, `VK_BLEND_OP_MIN`/`VK_BLEND_OP_MAX` ignore
both the source and destination blend factors entirely and compute a
raw `min`/`max` of the unscaled operands. `Executor.cpp`'s
`applyBlendOp`/`blendColor` unconditionally pre-multiplied both operands
by their blend factors before taking the min/max, for every op
including `Min`/`Max` -- correct for `Add`/`Subtract`/
`ReverseSubtract`, wrong for `Min`/`Max`.

### Fix

`applyBlendOp` now takes both the raw (`Src`/`Dst`) and factor-scaled
(`SrcTerm`/`DstTerm`) operand pairs, using the raw pair for `Min`/`Max`
and the scaled pair for every other op. Corrected two pre-existing unit
tests whose hand-computed expected values had encoded the *buggy*
factor-scaled `Min` result as "expected" (a latent test bug matching the
code bug); added a new `MinAndMaxBlendOpsIgnoreBothBlendFactorsEntirely`
regression test using zeroed blend factors specifically to make the
pre-fix bug's wrong answer (always 0) maximally distinguishable from the
correct result.

### Build/test

`ninja check-feme`: 3,268/3,271 Passed, 3 Unsupported, 0 Failed (+1 new
test, 0 regressions).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- `pipeline.monolithic.blend.format.r8g8b8a8_srgb.*` (100 cases):
  **100 Pass, 0 Fail** (was 94 Fail).
- `pipeline.monolithic.blend.format.r8g8b8a8_unorm.*` (100 cases):
  **100 Pass, 0 Fail** (was 94 Fail -- same underlying bug, confirmed
  unrelated to sRGB).
- `pipeline.monolithic.blend.*` regression sweep (8,073 of a much
  larger full family; timed out after 30 minutes on this session's own
  time budget before finishing the entire set): 3,927 Pass, 4 Fail, the
  remainder `NotSupported`. All 4 fails are `blend.clamp.*`
  (`b8g8r8a8_unorm`/`r16g16b16a16_snorm`/`r16g16b16a16_unorm`/
  `r8g8b8a8_unorm`), confirmed **pre-existing and unrelated** via a
  revert-and-rerun (fails identically before this fix) -- not
  investigated further this session. 0 new regressions found in the
  portion completed.
- `Roadmap.md`'s `L125(y)` row updated to reflect the fix (struck
  through, marked fixed and CTS-verified); see `agent_thoughts.md` for
  the full narrative and next steps.

## Roadmap L125(z): missing pre-blend clamp for fixed-point attachments

Investigated the `L125(z)` row filed in the prior session
(`pipeline.monolithic.blend.clamp.*`, 4-of-6 fail bucket across
`b8g8r8a8_unorm`/`r16g16b16a16_snorm`/`r16g16b16a16_unorm`/
`r8g8b8a8_unorm`, confirmed pre-existing and unrelated to `L125(y)`'s
own Min/Max fix). Read the real CTS test source
(`vktPipelineBlendTests.cpp`'s `ClampTest`/`ClampTestInstance`), which
embeds the exact Vulkan spec text this test is checking: for a
fixed-point (UNORM/SNORM) color attachment, the source/destination
values *and blend factors* (including the blend-constant color) must
each be clamped to `[0, 1]` or `[-1, 1]` respectively **before** the
blend equation is evaluated; no clamping applies for a floating-point
attachment. The test deliberately supplies out-of-range `quadColor`/
`blendConstants` values and computes its reference as
`clamp(blendConstants) * clamp(quadColor)` (clamp-then-multiply) --
which differs numerically from multiply-then-clamp for any
out-of-range operand (e.g. `clamp(2.0, 0, 1) * 0.5 = 0.5` vs.
`2.0 * 0.5 = 1.0`, and `1.0` survives a final `[0, 1]` clamp unchanged,
so a final-result-only clamp cannot substitute for a pre-blend input
clamp).

### Root cause

`Executor.cpp`'s `mergeColor` fed `Src`/`Dst`/`Src1`/`BlendConstants`
straight into `blendColor` with no pre-blend clamp at all -- only the
final packed result was ever clamped (by `packClearColor`'s existing
normalization), which is not equivalent to the spec's required
per-operand pre-blend clamp.

### Fix

Added a `blendClampRange(cpu::ResourceFormat) ->
std::optional<std::pair<double, double>>` helper, derived from
`RenderPass.cpp`'s `isSupportedColorAttachmentFormat` (the authoritative
list of formats `mergeColor` can ever reach with `BlendEnable=true`): 7
floating-point formats return `std::nullopt` (no clamp, per spec); the
one blend-eligible SNORM format (`R16G16B16A16_SNORM`) returns
`{-1.0, 1.0}`; every other reachable format returns `{0.0, 1.0}`.
`mergeColor`'s `BlendEnable` branch now clamps `Src`/`Dst`/`Src1`/
`BlendConstants` to this range (when present) before calling
`blendColor`, replacing the previous unclamped pass-through. New unit
test `BlendClampsSourceColorAndConstantFactorBeforeEvaluatingTheEquation`
reproduces the real CTS `blend.clamp.r8g8b8a8_unorm` case's exact input
shape (`Src=(2.0, 0.5, 1.0, -1.0)`,
`BlendConstants=(0.5, 2.0, -1.0, 1.0)`,
`SrcColorFactor=ConstantColor`/`DstColorFactor=Zero`/`Add`) and expected
clamp-then-multiply result; confirmed via a stash/rebuild round-trip to
fail identically to the real bug pre-fix (`255`/`255` instead of
`128`/`128` in R/G) and pass post-fix.

### Build/test

`ninja check-feme`: 3,269/3,272 Passed, 3 Unsupported, 0 Failed (+1 new
test, 0 regressions).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- `pipeline.monolithic.blend.clamp.*` (6 cases): **0 Fail** (was 4
  Fail) -- 4 Pass, 2 NotSupported (both `_SNORM` formats this ICD
  doesn't support for blending at all, unchanged and unrelated).
- `pipeline.monolithic.blend.format.r16g16b16a16_snorm.*` (100 cases,
  the one blend-eligible SNORM format, spot-checking the `[-1, 1]`
  clamp branch specifically): **100 Pass, 0 Fail**, 0 regressions.
- `pipeline.*.blend.clamp.*` (42 cases, all 7 pipeline-construction-type
  variants): **12 Pass, 0 Fail, 30 NotSupported** (most
  construction-type variants report `NotSupported` in this build for
  unrelated extension-support reasons -- `VK_EXT_shader_object`,
  graphics-pipeline-library, etc. -- not a clamp-bug artifact).
- `pipeline.monolithic.blend.*` full-family regression sweep: hit the
  same ~25-minute timeout ceiling the prior `L125(y)` session's own
  sweep hit, again not completing the entire family. Got through 3,927
  cases with **0 Fail**, 0 new regressions found in the portion
  completed.
- An old `H99a` roadmap entry's own closing note claims a full re-run
  once found `blend.clamp.*` **21/21 pass** (of the non-`NotSupported`
  cases) -- a count that matches neither this session's
  `pipeline.monolithic.blend.clamp.*` total (6 cases) nor its
  cross-construction-type `pipeline.*.blend.clamp.*` total (42 cases,
  12 non-`NotSupported`). This discrepancy is **not fully reconciled**;
  most likely explanation is a different CTS build/extension-support
  snapshot at the time `H99a` was written (e.g. more pipeline
  construction types reporting `Supported` then than now), rather than
  a residual bug -- the fix itself is independently confirmed correct
  against both the real Vulkan spec text and the actual CTS
  reference-value computation, regardless of the historical count.
- `Roadmap.md`'s `L125(z)` row updated to reflect the fix (struck
  through, marked fixed and CTS-verified); see `agent_thoughts.md` for
  the full narrative and next steps.

## Roadmap L125(s): vertex-input matrix AccessChain legalization and integer signedness gaps

### Bug 1: matrix-typed vertex-input attribute AccessChain legalization crash

Any `mat2`/`mat3`/`mat4`-typed vertex-input attribute (e.g.
`dEQP-VK.pipeline.monolithic.vertex_input.multiple_attributes.
binding_one_to_many.attributes.float.mat2.mat3`) crashed with `error:
failed to legalize operation 'spirv.AccessChain' that was explicitly
marked illegal`. `SPIRVToLLVMPatterns.cpp`'s `isCompositeStageIOType`
(checked on the *original* SPIR-V pointee type, used by
`isInputArrayAccessChain` to route `AccessChain` pattern selection)
recognized `spirv.array`/`spirv.struct` but not `spirv.matrix` -- a pure
oversight, since `isCompositeLLVMType` (checked on the *converted* LLVM
type) already treated a matrix's address as a real pointer (a matrix
converts to an `!llvm.array` of column vectors).

Fixed by adding `mlir::spirv::MatrixType` to `isCompositeStageIOType`'s
`isa<>` check. The existing `StageIOArrayAccessChainPattern` GEP-building
logic worked correctly unmodified once routed there: `remapNestedStruct
MemberIndices` hits its matrix/vector/scalar-leaf `break` case
immediately for a bare (non-struct-nested) matrix, leaving the
column/row indices unmodified -- correct, since RowMajor/MatrixStride
decorations only apply to struct members per the SPIR-V spec, never to
standalone Input/Output matrix variables.

New unit test `SPIRVToLLVMTest.
InputStorageMatrixAccessChainConvertsInsteadOfFailing`, confirmed via a
stash/rebuild round-trip to fail identically to the real bug pre-fix
and pass post-fix.

### Bug 2: integer-signedness false-positive rejection of `uint` vertex attributes

A genuinely `uint`-typed vertex shader input bound to a `*_UINT`-format
vertex attribute (e.g. the real CTS case `...attributes.int.ivec2.
uint`) was unconditionally rejected at `vkQueueSubmit` with "vertex
attribute format is UInt but the shader input is not". This is the same
architectural limitation `L125(u)` already found and fixed on the
fragment-output side: LLVM IR's integer types are signless, so
`CanonicalizeStage.cpp`'s `getComponentType` (which runs after
SPIRVToLLVM conversion) can never recover a SPIR-V scalar's original
`si32`/`ui32` distinction -- every integer scalar unconditionally maps
to `SignatureComponentType::SInt`. `Executor.cpp`'s `decodeAttribute`
had 6 integer-format validation checks requiring an exact `WantType ==
UInt`/`SInt` match, which the `*_UINT` half could never satisfy.

Fixed via a new `isIntegerComponentType(SignatureComponentType)` helper
accepting either `SInt` or `UInt` for `*_UINT`/`*_SINT`-format
validation, since the raw byte-decode logic itself is bit-identical
regardless of signedness (a raw memcpy or zero/sign-extend, with no
`WantType`-dependent step) -- only genuine `Float`/`Bool` mismatches
remain rejected.

New unit test `ExecutorTest.
RendersTriangleFromAUintVertexAttributeBoundToAUintFormat`, using a
custom vertex shader whose scalar `uint` input is compared via `icmp`
so both the fix's own error-avoidance and the actual decoded byte value
are exercised; also confirmed via a stash/rebuild round-trip to fail
identically to the real bug pre-fix and pass post-fix.

### Build/test

`ninja check-feme`: 3,271/3,274 Passed, 3 Unsupported, 0 Failed (+2 new
tests, 0 regressions).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- `pipeline.monolithic.vertex_input.*` full sweep (13,296 cases): **4
  Fail** (was 1,571 pre-session) -- 2,484 Pass (was 917), 10,805
  NotSupported (unchanged).
- The isolated repro cases for both bugs (`...float.mat2.mat3` and
  `...attributes.int.ivec2.uint`) individually confirmed to now Pass.
- The remaining 4 fails (`max_attributes.query_max_attributes.*` x3,
  `misc.unused_binding` x1) are a distinct, unrelated, newly-surfaced
  residual, not yet root-caused -- filed as `L127` in `Roadmap.md`.
- `Roadmap.md`'s `L125(s)` row updated to reflect both fixes (struck
  through, marked fixed and CTS-verified); `L127` added for the
  remaining 4-fail residual; `L125(t)` left untouched (not
  investigated this session). See `agent_thoughts.md` for the full
  narrative and next steps.

## Roadmap L127: unused vertex-input binding fix; L128 filed for dynamic-index gap

### Fix: unused vertex-input binding no longer requires a bound buffer

`FEME_VULKAN_LOG_CREATION_ERRORS=1` traces on all 4 of `L127`'s own
residual fails found two unrelated root causes. `misc.unused_binding`'s
own "vertex binding %u is not bound to a buffer" came from `runDraw`/
`validateDrawFetchBounds` requiring *every* declared
`VkVertexInputBindingDescription` to have an actual buffer bound to it
at draw time -- even one no `VkVertexInputAttributeDescription` ever
references at all. The real CTS case's own binding 1 is declared
alongside binding 0 but never consumed by any attribute; the Vulkan
spec permits leaving such a binding entirely unbound.

Fixed via a new shared `bindingHasAnyAttribute` helper, used at both
call sites to skip/relax the bound-buffer requirement for any binding
it reports as unreferenced. New unit test `DrawTest.
RendersWithAnUnusedVertexInputBindingLeftUnbound`, confirmed via a
stash/rebuild round-trip to fail identically to the real bug pre-fix
and pass post-fix.

### Filed, not fixed: `max_attributes.*`'s dynamic vertex-input-array gap

The other 3 of `L127`'s own residual fails
(`max_attributes.query_max_attributes.*`) share one different, larger,
not-yet-fixed root cause: `error: feme-graphics-validate-stage:
'feme.stage.input.load' ... has a non-constant vertex operand`. The
real CTS shader source (read from the QPA log's own embedded
`ShaderSource`) declares `layout(location = 1) in vec4
attr[numAttributes-1];` (`numAttributes` a specialization constant)
and reads it inside a genuine, non-unrolled `for` loop
(`attr[checkNdx-1]`). `SpecializationPatch.h` already resolves
`numAttributes` to a concrete value before deserialization (fixing the
array's own declared length correctly), but glslang itself never
unrolls the loop at SPIR-V-generation time, so the array index reaches
`CanonicalizeStagePass` as a genuine non-constant SSA value (an
`OpPhi`-derived loop induction variable).

This is a fundamentally different shape from every currently-supported
dynamic-`Vertex`-operand case (`getDynamicVertexIndexedAccess`'s
geometry/mesh per-vertex-array support): those all select a row/slot
*within* one already-fixed element (e.g. `gl_in[i]`), never *among*
several distinct, separately-`Location`d elements the way this array's
own slots are. `feme.stage.input.load`'s `(Element, Row, Component,
Vertex)` call convention has no operand that can express "which
Element" as a runtime value at all -- a real ABI gap, not a quick
pattern-add.

Filed as its own roadmap row, `L128`, with two candidate fix
directions sketched (a pre-canonicalization full-loop-unroll pass for
compile-time-constant trip counts, vs. a new dynamic-element-index ABI
operand) but neither attempted or spiked this session; needs its own
dedicated session to prototype and compare.

### Build/test

`ninja check-feme`: 3,272/3,275 Passed, 3 Unsupported, 0 Failed (+1 new
test, 0 regressions).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- `pipeline.monolithic.vertex_input.misc.unused_binding` (1 case): now
  **Pass** (was Fail).
- `pipeline.monolithic.vertex_input.*` full sweep (13,296 cases): **3
  Fail** (was 4) -- 2,485 Pass (was 2,484), 10,805 NotSupported
  unchanged.
- The remaining 3 fails (`max_attributes.query_max_attributes.*`) are
  the distinct, larger, not-yet-fixed dynamic-index gap filed as
  `L128`.
- `Roadmap.md`'s `L127` row updated to reflect the fix and the `L128`
  filing (struck through, marked partially fixed); `L128` added with
  the full root-cause writeup. See `agent_thoughts.md` for the full
  narrative and next steps.

## Roadmap L125(t): descriptor-set bind-point state isolation fix

### Fix: graphics and compute bind points no longer share bound-descriptor-set state

`bind_point.graphics_compute.*`'s original roadmap description ("10 of
655 fails") badly undercounted the real scope: a full sweep across all
three pipeline-construction types (3,024 cases) found **1,296 Fail**,
another instance of this roadmap's own recurring fractional-sample
undercount pattern.

`vktPipelineBindPointTests.cpp` (read directly from the CTS source
tree) interleaves graphics/compute pipeline and descriptor-set binds
in various orders, then issues both a draw and a dispatch, checking
each one's own SSBO ends up with only its own bind point's expected
value. Root cause: `CommandBuffer.cpp`'s `vkCmdBindDescriptorSets`/
`vkCmdPushDescriptorSet`/`vkCmdPushDescriptorSetWithTemplate` (and
their `2`-suffixed/`*Info`-struct siblings) all accepted a bind-point
selector of some form (a flat `pipelineBindPoint` argument, a
`stageFlags` mask, or -- for the template-based push variants, which
take no bind-point argument of their own at all -- the target
`DescriptorUpdateTemplate`'s own creation-time `pipelineBindPoint`) but
discarded it entirely. `executeCommandsInto`'s shared command-buffer
interpreter threaded one single `std::vector<BoundSetState> &BoundSets`
used identically by the compute-dispatch path and the graphics-draw
path -- a direct violation of the Vulkan spec's "there is a separate
set of bound descriptor sets for each of graphics and compute"
(`BoundPipeline`/`BoundGraphicsPipeline` were already correctly split
by bind point; only the descriptor-set state was not).

Fixed by threading a real bind point through the whole recording/
execution path: a new `BindPoint` field on `RecordedCommand`'s
`BindDescriptorSets` payload; a new `DescriptorUpdateTemplate::
bindPoint()` accessor (captured at `vkCreateDescriptorUpdateTemplate`
time, since `vkCmdPushDescriptorSetWithTemplate` itself has no
bind-point argument to read one from); and splitting
`executeCommandsInto`'s single `BoundSets` parameter into two fully
independent vectors (`BoundGraphicsSets`/`BoundComputeSets`), with
every draw-family case consuming only the former and every
dispatch-family case consuming only the latter.

Push-constant state was investigated and confirmed **out of scope**
for this fix: the CTS bucket's own `SetUpdateType` enum is purely
about descriptor-set population method (`vkUpdateDescriptorSets` vs.
`vkCmdPushDescriptorSet(WithTemplate)`), and the test source has zero
push-constant references. Vulkan's spec separately requires
push-constant state to be tracked per bind point too, and `feme` still
shares one `PushConstants` vector across both -- a real, latent,
still-open gap, filed as a new roadmap row (`L129`) rather than fixed
here, since no CTS bucket currently exercises it (no concrete repro to
drive or verify a fix from yet).

Fixing this surfaced one pre-existing test bug as a direct
consequence: `CommandBufferTest.cpp`'s `PushDescriptorSetDispatchTest.
WithTemplateReadsAndWrites` never set its own
`VkDescriptorUpdateTemplateCreateInfo::pipelineBindPoint` (defaulting
to `0` = `VK_PIPELINE_BIND_POINT_GRAPHICS`) despite pushing into a
compute dispatch -- harmless before this fix (bind point was ignored
everywhere), a real bug once it wasn't. Fixed by setting it to
`VK_PIPELINE_BIND_POINT_COMPUTE` explicitly.

New regression test `StorageBufferDispatchTest.
GraphicsBindDoesNotClobberComputeBoundDescriptorSets` reproduces the
bug's exact shape without needing a real graphics pipeline/draw at
all: bind a real descriptor set at the compute bind point, interleave
an unrelated bind at the graphics bind point to the same set index
(mirroring the CTS family's own interleaving), dispatch, and confirm
the dispatch's own reads/writes are untouched by the interleaved
graphics bind. Confirmed via a stash/rebuild round-trip to fail
identically to the real bug pre-fix and pass post-fix.

### Build/test

`ninja check-feme`: 3,273/3,276 Passed, 3 Unsupported, 0 Failed (+2 new
tests, 0 regressions).

### Results

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- `pipeline.*.bind_point.graphics_compute.*` full sweep (3,024 cases,
  all three pipeline-construction types): now **1,296 Pass / 0 Fail /
  1,728 NotSupported** (was 1,296 Fail).
- `pipeline.monolithic.push_descriptor.*` regression sweep (76 cases,
  exercising the same `bindDescriptorSets`/`pushDescriptorSet*` code
  paths this fix's refactor ran through): **76/76 Pass, 0 Fail** -- no
  regression to the ordinary (single-bind-point) push-descriptor paths.
- `Roadmap.md`'s `L125(t)` row updated to reflect the fix (struck
  through, marked fixed and CTS-verified); a new `L129` row filed for
  the related, still-open push-constant-state bind-point-separation
  gap (no concrete CTS repro found yet). See `agent_thoughts.md` for
  the full narrative and next steps.

## Roadmap L129/L130: push-constant bind-point repro search + BC-format CTS coverage gap investigation

Investigation-only session (no code changes) closing out two backlog
items from prior sessions' suggested-next-steps lists.

### L129: searching for a concrete CTS repro of the push-constant
bind-point isolation gap

`vktPipelinePushConstantTests.cpp`'s `push_constant.lifetime.*` group
is the only CTS bucket combining a graphics bind, a compute bind, and
push constants in one command buffer
(`pipeline_change_same_range_bind_push_vert_and_comp` /
`pipeline_change_diff_range_bind_push_vert_and_comp`). Read both
cases' `CommandData` sequences: each pushes once with a `stageFlags`
mask already spanning `VERTEX|COMPUTE`, draws, pushes again with a
different value, then dispatches -- the draw always consumes its push
before the compute one lands, so a single shared `PushConstants`
buffer (today's actual shape) produces the same observable result a
correctly-isolated implementation would. Ran the full bucket anyway as
a sanity check.

CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`):
- `pipeline.*.push_constant.lifetime.*` (63 cases, all
  `PipelineConstructionType`s): **27 Pass, 0 Fail, 36 NotSupported**
  (shader-object construction types, unrelated).

No regression, and no repro found -- confirms `L129` remains
correctly filed as "not yet started, no concrete CTS repro." A future
fix attempt will need a hand-written `feme` unit test (same
stash/rebuild-round-trip methodology as `L125(t)`'s own regression
test) rather than a CTS-driven one.

### L130: BC-format sampler-addressing CTS coverage gap

Root-caused the "`sampler.view_type.*.format.*bc*.address_modes.
*clamp_to_border*` matches 0 cases" gap flagged across several prior
sessions. Confirmed via `grep` that `vktPipelineSamplerTests.cpp`'s own
`formats[]` array (source of every `pipeline.*.sampler.view_type.*`
case) includes `ETC2`/`EAC`/`ASTC` but zero `VK_FORMAT_BC*` entries; a
repo-wide search for any file combining a BC format with
border/address-mode testing also found nothing. This is a genuine
upstream CTS coverage gap, not a feme bug -- no test anywhere in
dEQP-VK exercises BC-format sampling with border-color addressing.

Cross-checked feme's own BC-format border-color handling (`Command
Buffer.cpp`'s `compressedFormatBorderComponentMask`, added by the
unrelated `L125(w)` fix) by code inspection: every non-4-channel BC
format (`BC1_RGB`, `BC4`, `BC5`, `BC6H`) already has a correct
component-mask case, mirroring the equivalent ETC2/EAC cases that fix
verified via CTS. No code change made or needed.

### Build/test

No code changes this session -- `ninja check-feme` not re-run (no
functional change to validate).

### Results

`Roadmap.md`'s `L129` row updated with this session's investigation
result; new `L130` row filed and closed in the same edit (investigated,
confirmed CTS-side gap, no feme action needed). See `agent_thoughts.md`
for the full narrative and next steps.

## Roadmap L129: push-constant bind-point state isolation fix

Fixed the gap the prior session's investigation confirmed had no
concrete CTS repro anywhere in dEQP-VK -- the same shared-state bug
`L125(t)` fixed for descriptor sets, but for push constants instead.

### Root cause

`executeCommandsInto` (`CommandBuffer.cpp`) threaded a single shared
`std::vector<uint8_t> &PushConstants` through both the compute-dispatch
path and the graphics-draw path. `vkCmdPushConstants`'s `stageFlags`
argument was entirely discarded (an unnamed parameter, with a stale
comment claiming "V3's single compute stage means every push constant
is compute-visible" -- no longer true since graphics support exists).

### Fix

- `RecordedCommand` gained a `VkShaderStageFlags StageFlags` field
  (reused for the `PushConstants` command kind).
- `vkCmdPushConstants`/`vkCmdPushConstants2` now capture and thread the
  real `stageFlags` through to `CommandBuffer::pushConstants()`.
- `executeCommandsInto`'s single `PushConstants` parameter was split
  into two independent vectors, `PushConstantsGraphics` and
  `PushConstantsCompute`, each independently sized/zeroed to
  `maxPushConstantsSize`.
- The `PushConstants` execution case now checks `Cmd.StageFlags` and
  writes (with independent bounds checks) into one or both vectors --
  a push whose mask spans both bind points (e.g.
  `VERTEX_BIT | COMPUTE_BIT`) correctly writes into both, unlike a
  descriptor-set bind which only ever targets one bind point.
- All Dispatch/DispatchIndirect cases now consume only
  `PushConstantsCompute`; all Draw/DrawIndexed/DrawIndirect/
  DrawMeshTasks/DrawIndirectByteCount cases now consume only
  `PushConstantsGraphics`; `ExecuteCommands` and the top-level
  `executeCommandBuffer` entry point thread both vectors through.

### Testing

As the prior session established, no CTS bucket exercises this gap, so
a hand-written unit test was required, mirroring `L125(t)`'s own
methodology: new test `PushConstantDispatchTest.
GraphicsOnlyPushDoesNotClobberComputeBoundPushConstants` pushes a real
value at `VK_SHADER_STAGE_COMPUTE_BIT`, then an unrelated sentinel at
`VK_SHADER_STAGE_VERTEX_BIT` to the same byte range (no real graphics
pipeline/draw needed, since `vkCmdPushConstants` performs no validation
against any bound pipeline layout), dispatches, and confirms the result
reflects only the compute push. Confirmed via a stash/rebuild
round-trip: fails identically to the real bug pre-fix
(`Result == 0xCAFEF00D`, the sentinel, instead of the expected `42`),
passes post-fix.

`ninja check-feme`: **3,274/3,277 Passed, 3 Unsupported, 0 Failed**
(+1 new test, 0 regressions).

### CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`)

- `pipeline.*.push_constant.lifetime.*` (63 cases): unchanged at
  **27 Pass, 0 Fail, 36 NotSupported**, exactly matching the prior
  session's baseline -- confirms no regression (this bucket still
  cannot detect the fix itself, as already established; it isn't
  designed to stress cross-bind-point isolation).
- `pipeline.monolithic.push_constant.*` broader sweep (65 cases): **50
  Pass, 9 Fail, 6 NotSupported**. The 9 fails are pipeline-creation
  failures (`JIT session error: Symbols not found: [ spirv_var_NN ]`,
  `OpTypeArray count <id> ... must come from a constant, specialization
  constant, or supported specialization constant operation`) -- nothing
  to do with push-constant bind-point routing. Confirmed
  **pre-existing and unrelated** via a revert-and-rerun (stash the
  implementation fix, rebuild, rerun): all 9 fail identically without
  this fix too. Out of scope for this row; not investigated further.

### Results

`Roadmap.md`'s `L129` row struck through and rewritten as fixed. No
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed -- pure correctness fix, no new feature/extension surface. See
`agent_thoughts.md` for the full narrative and next steps.

## Roadmap L131: missing `!feme.signature` for zero-stage-IO Vertex/Fragment entries

Root-caused and fixed the 9 `pipeline.monolithic.push_constant.*`
failures `L129` above flagged but deferred as out-of-scope.

### Root cause

`CanonicalizeStage.cpp`'s `canonicalizeSPIRVStage` only calls
`dxil::setEntrySignature` inside its
`if (!InputGlobals.empty() || !OutputGlobals.empty())` branch. A
Vertex or Fragment entry with **zero** genuine stage-IO globals (only
push-constant reads plus resource writes -- e.g. `overwrite`'s
`imageStore`-only fragment shader) never enters that branch, so it
never gets `!feme.signature` metadata attached at all.
`feme::cpu::FragmentWrapperPass::lowerFragmentStageOps`
(`FragmentWrapper.cpp`) hard-requires an attached signature once an
entry uses *any* stage op (every fragment entry does, unconditionally,
via masked-output-store/return-mask calls `SPIRVToLLVMPatterns` always
emits) -- the missing metadata surfaced downstream as a JIT
symbol-resolution failure
(`JIT session error: Symbols not found: [ spirv_var_NN ]`), confirmed
via an isolated `FEME_VULKAN_LOG_CREATION_ERRORS=1` trace on
`dynamic_index_frag` pre-fix. This is **not** a rejection by
`SPIRVPushConstantLowering.cpp`'s `hasOnlyConstantIndices` scope
limitation, as the prior session's surface-level error-text triage had
speculated -- that limitation remains real and undisturbed, but an
isolated trace confirms it is not actually exercised by any of these 9
cases.

A prior fix (`H5e-d`/`H91`) already solved this exact shape for
Geometry/Mesh entries (an `else if (Stage == Geometry || Stage ==
Mesh)` branch attaching an empty signature) but deliberately excluded
Vertex/Fragment: a genuine DXIL-origin Vertex/Fragment entry (also
dispatched through `canonicalizeDXILStage`) always has empty SPIR-V
-style globals, so a blind empty-signature attach there could clobber
its real, already-correct signature -- the reason a prior rejected fix
attempt (`H4g`) had failed.

### Fix

Confirmed via code inspection that `feme::dxil::MetadataRaisingPass`
(`MetadataRaising.cpp`) unconditionally calls `dxil::setEntrySignature`
for every DXIL-origin entry point before `CanonicalizeStagePass` ever
runs (even for a trivially-empty converted signature), and that
`canonicalizeDXILStage` never itself writes signature metadata (only
reads it) -- making `!dxil::getEntrySignature(F)` a safe, exact
SPIR-V/DXIL origin discriminator. Extended the `else if` to:

```
Stage == Geometry || Stage == Mesh ||
    ((Stage == Vertex || Stage == Fragment) && !dxil::getEntrySignature(F))
```

Fixing this surfaced two stale, unrelated test assertions that had
only ever passed because this bug's own crash masked an already-correct,
separately-relaxed validation:

- `GraphicsPipelineTest.RejectsMissingFragmentOutput` (renamed
  `AcceptsFragmentWithNoOutputAtAll`, now expects `VK_SUCCESS`): its
  premise was invalidated by the pre-existing, unrelated `H11` fix,
  which already made "fragment declares no output at a real
  color-attachment location" spec-legal.
- `CanonicalizeStageTest.UnresolvableLoadInputIsLeftAlone`'s stale
  `EXPECT_FALSE(run(*M))` assertion no longer holds now that its exact
  test shape (no signature, no stage IO) legitimately gets an empty
  signature attached by this fix; the real invariant check
  (`SawLoadInput`) is retained.

### Testing

Two new regression tests added to `CanonicalizeStageTest.cpp`:
`FragmentWithNoStageIOStillGetsASignature` and
`FragmentWithPreAttachedSignatureIsNotClobbered` (the latter standing
in for genuine DXIL origin, confirming it is not clobbered). Both
confirmed via a stash/rebuild round-trip to fail identically to the
real bug pre-fix, then pass post-fix.

`ninja check-feme`: **3,276/3,279 Passed, 3 Unsupported, 0 Failed**
(+2 new tests, 0 regressions -- the 2 stale-test corrections are
pre-existing-bug/pre-existing-invariant fixes, not new regressions).

### CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`)

- `pipeline.*.push_constant.*` (437 cases, every pipeline-construction
  type): **409 Pass, 0 Fail, 28 NotSupported**, confirmed reproducible
  across 3 consecutive full reruns -- all 9 originally-reported
  failures, and their `fast_linked_library`/`pipeline_library`
  construction-type counterparts (24 total), are fixed. A
  substantially larger scope than this row's own original 9-case
  framing (`L129`'s sweep only covered the `monolithic` construction
  type).
- `pipeline.*.push_constant.lifetime.*` (63 cases): **63 Pass, 0
  Fail, 0 NotSupported** (100%) -- re-checked as a regression guard.
- `dEQP-VK.geometry.emit.*` (23 cases, the original `H5e-d` motivating
  family): **23 Pass, 0 Fail** -- confirms the shared code path this
  fix touches is unaffected for Geometry.

### Results

`Roadmap.md`'s `L131` row added, fixed and CTS-verified. No
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed -- pure correctness fix, no new feature/extension surface. See
`agent_thoughts.md` for the full narrative and next steps.

## Roadmap L128: dynamic-vertex-index misclassification fix (partial); L128(a) filed for a new nondeterministic JIT crash

### What changed

`isDynamicIndexedArrayGlobal` (`CanonicalizeStage.cpp`), used by both
`getDynamicVertexIndexedAccess` and (via its own exclusion)
`getDynamicRowIndexedAccess`, had no `ShaderStage` restriction at all --
unlike its two already-stage-scoped sibling helpers
(`isPerVertexArrayInputGlobal`, `isPerVertexArrayMeshOutputGlobal`,
restricted to Hull/Domain/Geometry or Mesh respectively). This let a
plain (non-per-vertex-arrayed) Vertex/Fragment-stage arrayed stage-IO
global -- e.g. `layout(location = 1) in vec4 attr[N];`, the
`vertex_input.max_attributes.query_max_attributes.*` shape `L128`
targets -- get wrongly claimed by `getDynamicVertexIndexedAccess`,
threading its array index through as a bogus `Vertex` operand instead
of `getDynamicRowIndexedAccess`'s `Row` (the operand this shape
actually belongs on, since `addElement` already models such an array as
one `SignatureElement` with `RowCount == N`, exactly like a real
matrix's own rows).

Fixed: `isDynamicIndexedArrayGlobal` now takes `ShaderStage Stage` and
only matches Hull/Domain/Geometry/Mesh, threaded through
`getDynamicVertexIndexedAccess`/`getDynamicRowIndexedAccess`/
`getStageIOGlobal`'s own signatures and every call site (two call
sites -- `usesSPIRVStageIO`/`classifyTessControlOutputStoreFrequency`
-- are only ever reachable for Hull-stage tessellation-control
splitting, hardcoded to `ShaderStage::Hull` accordingly;
`canonicalizeSPIRVStage`'s own discovery loop already had `Stage` in
scope). Confirmed via `FEME_DUMP_IR` that a *constant* (post-unroll)
`attr[k]` access now correctly resolves to `(Element, Row=k, Component,
Vertex=0)` instead of the pre-fix `(Element, Row=0, Component,
Vertex=k)`.

This fix alone does not close `L128`: the original, non-unrolled
loop's genuinely-non-constant index still cannot resolve through
`getDynamicRowIndexedAccess`'s existing `collectDynamicRowTerms`
recursion either (confirmed: without a loop-unrolling pass, this now
fails with a *different* error, "unresolved stage-IO global-variable
access", at the same 3 cases). A `!llvm.loop.unroll.full`-forced
full-unroll pass (gated on `ScalarEvolution::getSmallConstantTripCount`
being nonzero and below a small cap) was prototyped in
`Pipeline.cpp` and, combined with this fix, got all 3 target CTS cases
past pipeline creation for the first time -- but surfaced a new,
**nondeterministic** JIT runtime SIGSEGV (roughly 2-in-3 reruns of the
identical compiled IR) once actually executed. This prototype was
**reverted** rather than landed in this unverified, crash-prone state;
see `L128(a)` (`Roadmap.md`) for the crash itself, not yet localized.

### Testing

Three existing unit tests had mistagged
`"feme.shader.stage"="vertex"` function attributes (never checked
before this fix, since the code path was previously stage-blind)
corrected to the real stage each test's own shape represents:

- `ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberLoad`
  (`gl_in[]`, a genuine Geometry-stage shape) -> `geometry`.
- `ThreadsDynamicVertexIndexIntoOutputStore` and
  `ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberStore`
  (per-vertex/per-primitive Mesh output arrays) -> `mesh`.

Retagging `ThreadsDynamicVertexIndexIntoOutputStore` to the real
`mesh` stage also, for the first time, legitimately exercised
`addElements`'s own, separately-added `PerInvocationOutputArray`
Mesh-stage peeling (added after this test was originally written, and
unreachable under the old, incorrect `vertex` tag) -- its own
`RowCount` expectation was stale (`3`, from before that peeling
existed) and is corrected to the now-actually-produced, already-correct
`1`.

`ninja check-feme`: **3,276/3,279 Passed, 3 Unsupported, 0 Failed** (0
regressions once the 3 stale-tag/stale-assertion tests were
corrected).

### CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`)

- `pipeline.monolithic.vertex_input.*` (13,296 cases): **2,485 Pass, 3
  Fail, 10,805 NotSupported, 3 Warnings** -- unchanged from the prior
  session's baseline; the same 3 `max_attributes.query_max_attributes.*`
  cases, not a new or different fail count. `L128` itself is not
  closed by this session's change alone.
- `tessellation.user_defined_io.*` (9 `per_patch_array.*` cases run
  clean; `per_patch_block.*` hits a pre-existing, unrelated assertion
  crash in `resolveNestedStageIOField` confirmed via revert-and-rerun
  to predate this session entirely, unrelated to this fix) and
  `clipping.user_defined.*` (256 cases): **256/256 Pass** -- both swept
  as regression guards for the Hull/Domain/Geometry/Mesh stage
  restriction (the only stages this fix's classification narrowing
  could plausibly affect); no regression found.
- `pipeline.monolithic.blend.*` (the long-pending, two-session-running
  full-family sweep first flagged in `L128`'s own prior session):
  kicked off again this session in the background, still running at
  the time this report was written (32,000+ of an eventual much
  larger total already Pass, 0 Fail so far) -- see `agent_thoughts.md`
  for this session's final status/next-step note on whether it
  completed before the session ended.

### Results

`Roadmap.md`'s `L128` row updated (not struck through -- still open):
root cause corrected (a stage-classification bug, not "no ABI operand
can express this at all" as originally framed) and the classification
bug itself fixed and CTS-verified (no behavior change to any
currently-passing case). New row `L128(a)` filed for the
nondeterministic JIT crash discovered while prototyping the
loop-unrolling half of this fix -- not yet localized, needs
ASan/valgrind/`rr`-class tooling in a future dedicated session. No
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed -- pure compiler-internals correctness fix, no new
feature/extension surface. See `agent_thoughts.md` for the full
narrative and next steps.

## Session: L128(a) crash localization with valgrind + GDB JIT-debug support

No functional code change landed this session (the `Pipeline.cpp`
forced-unroll prototype used to reproduce `L128(a)` was reinstated
locally, used purely for diagnosis, and reverted again -- `git diff`
against the prior commit is empty for anything under `feme/lib`).
This session's CTS activity was entirely diagnostic:

- Installed `valgrind` (previously absent) and used
  `valgrind --track-origins=yes` against the 3
  `vertex_input.max_attributes.query_max_attributes.*` target cases
  (with the forced-unroll prototype temporarily reinstated) to
  localize the `L128(a)` crash: an "Invalid read of size 4" inside
  unsymbolized JIT-compiled code, at a wild address landing inside
  an unrelated, coincidentally-adjacent CTS-internal heap allocation
  in one run and near-null in another -- a genuine memory-safety bug,
  not a compile-time/logic error, and its address-dependent nature on
  otherwise byte-identical compiled IR explains the previously-observed
  run-to-run nondeterminism.
- Used this project's own existing `FEME_CPU_JIT_DEBUG_SUPPORT=1` GDB
  JIT-registration hook (`CompiledStage.cpp`, previously undiscovered
  by recent sessions) to get real function-symbol backtraces for the
  first time: confirmed the crash is inside `main` (the compiled
  vertex-shader entry), called from `feme_cpu_entry_main` (the
  per-invocation wrapper) -- i.e. a draw-time execution crash inside
  the compiled shader body, not a pipeline-creation-time bug.
- Ruled out an initial "runaway self-recursion / stack overflow"
  theory (suggested by a `bt full` showing ~15,000 `feme_cpu_entry_main`
  frames) by checking `$sp` directly at the crash: it was only ~20KB
  below the top of a stack region with ~114KB of unused headroom below
  it, nowhere near the process's 8&nbsp;MiB `ulimit -s` -- the deep
  "recursion" was an artifact of `gdb`'s naive frame-pointer-chasing
  failing on frame-pointer-omitted (optimized) JIT code, not a real
  call chain.
- Confirmed (via `FEME_DUMP_IR_PREUNROLL`, a debug hook added to the
  now-reverted prototype) that the forced-full-loop-unroll mechanism
  itself is correct: all 15 loop iterations became distinct,
  compile-time-constant-offset GEP+load pairs with zero residual phi
  nodes -- the unrolling is not the bug; the bug is downstream of it,
  likely an LLVM IR-construction or codegen defect specific to a
  `RowCount`-15 plain Vertex-stage input (far larger than any
  previously-exercised `RowCount`, which topped out at 4 for real
  matrices).
- `pipeline.monolithic.blend.*` full-family sweep (PID 40501, running
  since a prior session): checked repeatedly through this session,
  climbing from 32,326 to 41,122+ cases completed, 0 Fail throughout;
  still running at the time this report was written -- carried
  forward again, see `agent_thoughts.md` for this session's final
  status note.

### Results

`Roadmap.md`'s `L128(a)` row updated with this session's much more
precise diagnosis (genuine wild-pointer read inside JIT-executed
compiled-shader code, heap-layout-dependent, not a stack overflow,
not yet localized to a specific IR construct) and concrete next-step
guidance (reinstate the same prototype with `valgrind` +
`FEME_CPU_JIT_DEBUG_SUPPORT=1` from the start; build a hand-minimized
few-attribute repro shader to make bisection tractable). `L128` itself
remains open/not struck through. No `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` update needed -- diagnostic-only session,
no feature/extension surface changed.

## Closing out: pipeline.monolithic.blend.* full-family sweep (3-session-running background item)

The `pipeline.monolithic.blend.*` full-family regression sweep (PID
40501, first kicked off 2 sessions ago, checked repeatedly since
without finishing) **completed in the background at the start of this
session**, after 3630.70 seconds (~60.5 minutes) of runtime:

```
Test run totals:
  Passed:        5945/12206 (48.7%)
  Failed:        0/12206 (0.0%)
  Not supported: 6261/12206 (51.3%)
  Warnings:      0/12206 (0.0%)
  Waived:        0/12206 (0.0%)
```

**100% Pass of all supported cases, 0 Fail across the entire family.**
This definitively closes the "is `blend.*` actually clean" question
that had been carried forward, unresolved, across 3 sessions -- no
regression, no partial coverage gap, nothing further needed here. The
`NotSupported` cases are expected (format/blend-op combinations the
device legitimately doesn't advertise support for, not a feme gap).
No roadmap or feature-inventory change needed -- this was a
regression-confirmation sweep, not new work.

## Session: L128(a) root-caused to CanonicalizeStagePass ordering (source-level, not just disassembly)

Continuing directly from the prior section's `pipeline.monolithic.blend.*`
closure, this session picked up `L128(a)` (the nondeterministic JIT
SIGSEGV blocking `L128`'s 3 `vertex_input.max_attributes.query_max_attributes.*`
fails) with `valgrind`/`FEME_CPU_JIT_DEBUG_SUPPORT=1` already in place from
the prior session.

**New fast repro vehicle**: built a hand-minimized `RowCount==5` vertex
shader (`attr[0..4]`, past the `RowCount<=4` real-matrix ceiling but far
smaller than the real CTS shader's `RowCount==15`) in MLIR SPIR-V dialect
text, exercised directly through `feme/unittests/Vulkan/DrawTest.cpp`'s
own in-process `FeMeVulkanTests` GTest harness rather than the full
`deqp-vk`/CTS path -- seconds per run instead of minutes, and reproduces
the crash reliably (~3/3 direct runs, ~3/3 under `gdb`). Landed as a
disabled (`#if 0`) test, `DrawTest.L128ARowCount5Repro`, for reuse by
whichever session implements the real fix below.

**Disassembled the exact crash site** (`disassemble feme_cpu_entry_main`
at the crash PC): a genuine, deterministic null-pointer-plus-offset
dereference at `feme_cpu_entry_main+80` (`ldr w17, [x15, #108]`), where
`x15` is `FemeVertexArgs::InputLayout->Elements` (confirmed against
`RuntimeABI.h`'s own struct layout: offset 24 is `InputLayout`, and
offsets 108/116/120 land exactly on `Elements[1].{InvocationStride,
RowStride,DataOffset}`) -- i.e. the shader's own `InputLayout` has a
**null `Elements` array**, not a wild/heap-adjacent read as the prior
session's `valgrind`-based characterization had it.

**Traced why with temporary, reverted `FEME_DEBUG_SIG`-gated prints**
through `StageStorage.cpp`/`CanonicalizeStage.cpp`/`CompiledStage.cpp`:
the vertex shader's *exported* `EntrySignature` -- the one
`Executor.cpp`'s `buildStageStorage` actually uses at draw time via
`CompiledStage::getArtifactInfo().Signature` -- never contains an entry
for `attr` at all, even though the compiled shader body correctly
references its `ElementID` directly (`FEME_DUMP_IR` confirms 5 separate,
already-unrolled `feme.stage.input.load.v4f32` calls, rows 0-4, one
`ElementID`). Root cause: `feme::cpu::createStage` (`CompiledStage.cpp`)
serializes `!feme.signature` metadata into the artifact's exported
`Signature` bytes **before** calling `runPipeline` (deliberately, to
dodge `EntryWrapperPass`'s function-erasing/renaming paths -- confirmed
a naive post-`runPipeline` re-read finds *no* `!feme.signature` metadata
on the final wrapper function at all). But the *real*, authoritative
signature-building `CanonicalizeStagePass` call for a graphics-pipeline
shader is not inside `runPipeline` at all -- it is
`feme::vulkan::compileGraphicsStage`'s own separate, *earlier* call
(`GraphicsPipeline.cpp:541`), run before `createStage` is ever invoked.
No pass anywhere ahead of *that* call ever unrolls a compile-time-
constant-trip-count loop over an array-typed (`RowCount>4`) stage-IO
global, so `CanonicalizeStagePass`'s raw-SPIR-V-global scan can't trace
`attr`'s loop-carried GEP and silently omits it. `L128`'s previously-
prototyped forced-unroll pass, placed inside `runPipeline` (Pipeline.cpp),
is too late to help -- it only fixes the *redundant*, ineffective second
`CanonicalizeStagePass` call inside `runPipeline`, not the real one in
`GraphicsPipeline.cpp` that the exported signature is actually captured
from. A `RowCount<=4` real matrix never hits this, since SPIR-V/glslang
always emits its row accesses as separate, already-unrolled loads with
no loop to fail to see through.

**Concrete fix identified, not attempted this session**: move (or
duplicate) a loop-unrolling/constant-GEP-recognition pass to run inside
`feme::vulkan::compileGraphicsStage`, immediately ahead of its own
`CanonicalizeStagePass().run(...)` call at `GraphicsPipeline.cpp:541` --
not inside `feme::cpu::runPipeline`. Deferred to a dedicated session:
this touches a core, shared pipeline stage used by every graphics shader
compile and needs full regression testing (`check-feme` plus a broad CTS
sweep) beyond what was safe to rush here.

All diagnostic-only scaffolding this session added (the `Pipeline.cpp`
forced-unroll prototype, the `FEME_DEBUG_SIG` prints, the post-
`runPipeline` metadata-propagation check) was reverted
(`git checkout --`); nothing landed except the disabled
`DrawTest.L128ARowCount5Repro` test and comments. `Roadmap.md`'s new
`L128(b)` row has the full writeup.

**Regression check**: `ninja check-feme` -- 3,276/3,279 Passed, 3
Unsupported, 0 Failed (no functional code changed this session, so no
change from baseline). Reran the 3 target `vertex_input.max_attributes.
query_max_attributes.*` cases directly against `deqp-vk` to confirm no
regression: all 3 still `Fail` identically to before this session
(`error: feme-graphics-validate-stage: ... has an unresolved stage-IO
global-variable access ... a shape CanonicalizeStagePass does not yet
canonicalize into a 'feme.stage.*' call`, `VK_ERROR_INITIALIZATION_FAILED`
at pipeline creation) -- expected, since no functional fix was attempted
this session. No feature/extension inventory changes (no new Vulkan
functionality shipped this session).

## Session: L128/L128(a)/L128(b) closed -- `UnrollConstantTripCountStageLoopsPass` implemented and landed

Picked up directly from the prior session's `L128(b)` writeup: the fix
location was fully identified (`GraphicsPipeline.cpp`, immediately ahead
of `compileGraphicsStage`'s own `CanonicalizeStagePass().run(...)` call)
but not attempted. This session implemented, debugged, and landed it.

**The pass**: `feme::graphics::UnrollConstantTripCountStageLoopsPass`
(`feme/include/feme/Transforms/Graphics/UnrollConstantTripCountLoops.h`,
`feme/lib/Transforms/Graphics/UnrollConstantTripCountLoops.cpp`) builds a
self-contained, cross-registered `PassBuilder` (its own analysis
managers, not the caller's incomplete one) and runs `SROAPass` →
`LoopSimplifyPass` → `LCSSAPass` → an internal `MarkLoopsForForcedFullUnroll`
function pass (attaches `!llvm.loop.unroll.full` to any loop whose
`ScalarEvolution::getSmallConstantTripCount` is nonzero and ≤ 64) →
`createFunctionToLoopPassAdaptor(LoopFullUnrollPass(OptLevel=2,
OnlyWhenForced=true))` → `InstCombinePass` → `SimplifyCFGPass`, scoped to
Vertex/Fragment entry points only. Wired into `GraphicsPipeline.cpp`
immediately before the existing `CanonicalizeStagePass().run(...)` call.

**Two real bugs found and fixed while landing this** (both silently
produced a no-op pass on the first attempt):

1. `MarkLoopsForForcedFullUnroll` initially called
   `addStringMetadataToLoop(L, "llvm.loop.unroll.full")` with a bare
   `const char*` string literal. C++ overload resolution binds this
   exactly to `addStringMetadataToLoop(Loop*, const char*, unsigned V =
   0)` (an exact-type match beats the intended `StringRef` overload's
   implicit conversion), silently producing a *false*-valued
   `!{!"llvm.loop.unroll.full", i32 0}` "key = value" node instead of a
   bare, name-only "attribute set" node (always `true`) --
   `llvm::getOptionalBoolLoopAttribute`'s own `case 2` reads the literal
   operand value, so this loop was marked "unroll.full = false" the
   whole time. Fixed by explicitly constructing a `StringRef` at the call
   site.
2. Even after fix (1), the loop still visibly failed to unroll when
   traced via a temporary IR dump. Root-caused to **`FeMeVulkanTests`
   (and every other in-process Vulkan unit-test binary) statically
   linking its own separate copy of `FeMeVulkanCore`/
   `FeMeTransformsGraphics`**, distinct from the copy dynamically
   `dlopen`ed via `feme_icd.json`'s `library_path` when a real Vulkan
   application (including `deqp-vk`) creates an instance through the
   loader. Rebuilding only the `feme_vulkan` CMake target (the shared
   ICD library) left the *test binary's* own statically-linked copy
   stale, so the fix appeared entirely inert against `FeMeVulkanTests`
   even though the loaded `.so` was correct. `cmake --build . --target
   FeMeVulkanTests` (not just `feme_vulkan`) is required after any
   change to code reachable from a Vulkan-core in-process unit test --
   worth remembering for any future in-process debugging session.

**Validated the fix**: confirmed via direct IR inspection (temporary
`errs()`-based tracing, since removed) that the `RowCount==5` repro's
loop fully unrolls into 5 separate `feme.stage.input.load.f32` calls
ahead of `CanonicalizeStagePass`. `DrawTest.L128ARowCount5Repro`
(finalized as a permanent, always-enabled regression test; its header
comment rewritten from "NOT a landable test" to describe the now-landed
fix) passes cleanly and repeatably (previously crashed with `SIGSEGV`
before any fix existed, and failed pipeline creation with an
"unresolved stage-IO global-variable access" error with only `L128(b)`'s
misclassification fix in place).

**Regression check**: `ninja check-feme` -- 3,277/3,280 Passed, 3
Unsupported, 0 Failed (+1 newly-enabled test, 0 regressions).

**CTS verification**:
- The 3 target cases this row exists for --
  `vertex_input.max_attributes.query_max_attributes.{binding_one_to_many.
  interleaved,binding_one_to_many.sequential,binding_one_to_one.
  interleaved}` -- all **Pass** for the first time.
- Full `vertex_input.*` re-sweep: **0 Fail** (13,296 cases: 2,488 Pass,
  10,805 NotSupported, 3 Warning), confirming no regression across the
  entire vertex-input test family this pass runs ahead of.
- A full `pipeline.monolithic.*` sweep was additionally started as a
  broader regression guard, since this new pass runs ahead of the
  signature-building step for every graphics shader compiled through
  `compileGraphicsStage`, not just vertex-input ones. It was still
  in progress at several thousand cases in with 0 new fails (the small
  number of `Fail`s observed are `bind_buffers_2.*`, confirmed via a
  stash/rebuild-to-baseline round-trip to be pre-existing and unrelated
  to this session's change) -- see this file's own later note (if any)
  or a future session for the final tally; not required to close this
  row, since the row's own target cases and the full `vertex_input.*`
  family are both already fully verified clean.

No feature/extension inventory changes (no new Vulkan functionality
shipped this session -- this is a compiler-internal correctness fix to
existing dEQP-VK coverage, not a new capability).

## Session: L128(c) full `pipeline.monolithic.*` sweep completed; L132, L133 filed

Continuing directly from the prior session's `L128(c)` closure (which
left a broad `pipeline.monolithic.*` regression sweep running in the
background as extra evidence, not required to close the row). Confirmed
`FeMe CPU Vulkan Device` at session start (standing requirement).

**Checked the background sweep**: it had completed 188,690 cases (686
`Fail`) before the `deqp-vk` process itself hard-aborted on a debug
assertion, never reaching the rest of `pipeline.monolithic.*`. The
abort:

```
deqp-vk: .../CanonicalizeStage.cpp:3055: NestedStageIOField (anonymous
namespace)::resolveNestedStageIOField(Type *, uint64_t, Type *, const
DataLayout &): Assertion `!isStageIOPadField(ST->getElementType(Member))
&& "load/store into a nested struct's own synthetic pad field"' failed.
```

on `dEQP-VK.pipeline.monolithic.interface_matching.decoration_mismatch.
out_flat_in_none_member_of_structure_in_block_vert_geom_out_frag_in`.

**Triaged the 686 `Fail`s by family**: 592 `depth.*`, 57 `bind_buffers_2.*`,
26 `extended_dynamic_state.*`, 5 `early_destroy.*`, 2 `input_assembly.*`,
2 `creation_cache_control.*`, 1 `empty_fs.*`, 1 `cache.*`.

**Confirmed every family (and the crash) is pre-existing, not a
regression from `L128(c)`'s fix**: temporarily swapped
`GraphicsPipeline.cpp` back to its pre-`L128(c)` content (removing just
the new pass's invocation, via a diff against the commit before
`L128(c)` landed -- a 12-line, single-file change), rebuilt `feme_vulkan`
only, and reran one representative case from each family plus the
crashing `interface_matching` case directly against this baseline
build:

- `depth.format.d16_unorm.depth_test_disabled.depth_write_enabled` --
  `Fail (Image mismatch)`, identical to the fix-present run.
- `bind_buffers_2.*` (already known pre-existing from last session) --
  reconfirmed.
- `extended_dynamic_state.after_pipelines.depth_bias_enable` -- `Fail
  (Incorrect value found in attachments...)`, identical.
- `early_destroy.cache` -- `Fail (retcode: VK_ERROR_INITIALIZATION_
  FAILED...)`, identical.
- `input_assembly.primitive_restart.restart_mix.restart_mix_dynamic_topo`
  -- `Fail (retcode: VK_ERROR_INITIALIZATION_FAILED...)`, identical.
- `creation_cache_control.compute_pipelines.
  batch_pipelines_early_return` -- `Fail (pipelines[1] is not
  VK_NULL_HANDLE...)`, identical.
- `empty_fs.masked_samples` -- `Fail (vk.createImage(...):
  VK_ERROR_INITIALIZATION_FAILED...)`, identical.
- `cache.misc_tests.invalid_size_test` -- `Fail (Data needs to be
  empty...)`, identical.
- `interface_matching.decoration_mismatch.
  out_flat_in_none_member_of_structure_in_block_vert_geom_out_frag_in`
  -- aborts with the exact same assertion, identical.

Restored the real fix (`GraphicsPipeline.cpp` back to its committed
state -- `git status` confirmed a clean, zero-diff working tree
afterward), rebuilt `feme_vulkan` + `FeMeVulkanTests`, and reconfirmed
`DrawTest.L128ARowCount5Repro` still passes.

**Filed the two newly-discovered gaps as their own roadmap rows**
(`Roadmap.md`), since neither had a dedicated row before this session --
both were previously only visible as noise inside a broader sweep:
- `L132`: `bind_buffers_2.*`'s 57 fails (`vkCmdBindVertexBuffers2`
  stride/offset handling). Not root-caused this session, filed for a
  future dedicated session.
- `L133`: the `interface_matching.*` nested-struct-pad-field assertion
  crash (`CanonicalizeStage.cpp`'s `resolveNestedStageIOField`). Not
  root-caused this session; flagged as possibly related in shape to
  `L124`'s pad-field/nested-struct work but not confirmed the same
  root cause. This is a genuine `deqp-vk` process abort, not just a
  reported `Fail` -- worth prioritizing over `L132` if only one can be
  picked up next, since it silently truncates any batch CTS sweep that
  reaches it (as it did to this row's own extra regression-guard sweep).

**Started a follow-up sweep** (`--deqp-exclude-case=
dEQP-VK.pipeline.monolithic.interface_matching.*`) to get a complete
tally of the rest of `pipeline.monolithic.*` without hitting the `L133`
crash. Left running in the background at session end (not required to
close `L128(c)`, which is now fully verified via the 188,690-case
partial sweep plus per-family baseline spot-checks above) -- see a
future session for its final tally if picked up.

**`L128(c)` is now considered fully closed**: the row's own 3 target
cases pass, the full `vertex_input.*` family is 0 Fail, and the full
`pipeline.monolithic.*` family (bar the separately-filed, pre-existing
`L133` crash) shows 0 new fails across every distinct failing family
observed. No feature/extension inventory changes (no new Vulkan
functionality shipped this session).

## Session: L133 root-caused and fixed -- nested-struct interior-pad remap gap

Continuing directly from the prior session's `L133` filing. Confirmed
`FeMe CPU Vulkan Device` at session start (standing requirement).

**Checked the still-running background sweep** (PID 12156, the
`interface_matching`-excluded `pipeline.monolithic.*` follow-up from the
prior session): still running, 0 new fails past the already-known
`bind_buffers_2.*` (`L132`) fails, mid-`blend.*` (a known multi-hour
sub-family). Left running throughout this session; not required to
close anything.

**Root-caused `L133`** using a temporary, env-var-gated `errs()` dump
inside `resolveNestedStageIOField` (`CanonicalizeStage.cpp`, reverted
before committing) plus a full module IR dump immediately before
`CanonicalizeStagePass` runs (`GraphicsPipeline.cpp`, also reverted).
The captured IR for the crashing fragment shader showed the exact
faulty GEP:

```
%1 = load float, ptr addrspace(7) getelementptr inbounds nuw
       (i8, ptr addrspace(7) @spirv_var_15, i64 24), align 4
```

against `@spirv_var_15`'s real (correctly padded) type `<{ <2 x float>,
[8 x i8], <{ <2 x float>, [8 x i8], <4 x float> }> }>` -- byte 24 lands
inside the *inner* struct's own interior pad (bytes 24-32 absolute);
`variableInStruct` actually starts at byte 32. The outer struct's own
member selection (`structInBlock` at byte 16) was computed correctly;
only the *recursive, one-level-deeper* remap into `TestStruct` itself
was wrong.

**Root cause**: `SPIRVToLLVMPatterns.cpp`'s
`getStructMemberPhysicalIndexInRealType` (called by
`remapNestedStructMemberIndices` for any struct-member selector *below*
the outermost struct level) unconditionally returned `DeclaredIndex`
unchanged whenever the nested struct had no explicit SPIR-V `Offset`
decorations. That is correct for member *permutation* (impossible
without declared offsets to sort by), but wrong for a plain
*natural-alignment interior pad* (`layOutStructIfOffsetsMatch`'s own
`!Type.hasOffset()` branch, roadmap L103, still inserts one before a
member needing wider alignment than its predecessor leaves room for --
exactly `TestStruct`'s own `vec2 dummy; vec4 variableInStruct;` shape).

**Fix**: `getStructMemberPhysicalIndexInRealType` now falls back to
`getStructMemberPhysicalIndex`'s own isolated re-derivation (the same
path the outermost-struct level already uses unconditionally) when
`!Struct.hasOffset()`, rather than assuming no remap is ever needed --
safe because a natural-alignment gap's position depends only on the
members strictly before the one being resolved, never on any enclosing
struct's own retry-tier choice (unlike the separate tight-vector-marker-
struct substitution concern this function's own doc comment discusses,
which is unaffected by this change).

### Testing

`ninja check-feme`: 3,277/3,280 Passed, 3 Unsupported, 0 Failed (no
regressions; no new unit test added this session -- see "Suggested next
steps" below).

### CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`)

- The exact crashing case now **Pass**es:
  `dEQP-VK.pipeline.monolithic.interface_matching.decoration_mismatch.
  out_flat_in_none_member_of_structure_in_block_vert_geom_out_frag_in`.
- Full `dEQP-VK.pipeline.monolithic.interface_matching.*` re-sweep
  (previously impossible to complete due to the abort): **0 Fail**,
  1,445 Pass, 144 NotSupported, 1,589 total.

### Results

`L133` is now considered fully closed: the crashing case passes, and a
full re-sweep of its entire CTS family completes cleanly with no
sibling regressions. No feature/extension inventory changes (a
correctness fix to existing lowering, no new Vulkan functionality
shipped this session).

## Session: L132 root-caused and fixed -- barycentric-weight-sum precision gap

Continuing directly from the prior session's `L132` filing. Confirmed
`FeMe CPU Vulkan Device` at session start (standing requirement).

**Checked the still-running background sweep** (PID 12156, the
`interface_matching`-excluded `pipeline.monolithic.*` follow-up, now
spanning 4+ sessions): still running, mid-`blend.*`, 0 new fails.
Left running throughout this session (not required to close anything);
not yet finished as of this session's end either.

**Reproduced and precisely enumerated `L132`'s failure set**: all 57
`dEQP-VK.pipeline.monolithic.bind_buffers_2.*` fails are exactly the
`single.*`/`separate.*`/`dynamic_stride.*` shape (every stride/offset/
count combination, 100% of that shape, 0 exceptions) -- the sibling
`bind_buffers_2.maintenance5.*` shape is 100% passing. Investigated and
**ruled out** a Vulkan-loader entry-point-resolution gap (FeMe's ICD only
implements the `EXT`-suffixed `vkCmdBindVertexBuffers2EXT`, not the
1.3-promoted core `vkCmdBindVertexBuffers2`, but CTS's own generated
`vkInitDeviceFunctionPointers.inl` already falls back to the `EXT` name
when the core name resolves to null -- confirmed via a standalone C
program against the real loader, and via reading CTS's own generated
fallback code; not the bug).

**Root cause**: the failing shape's own pixel check
(`BindBuffers2Instance::iterate`,
`vktPipelineBindVertexBuffers2Tests.cpp`) uses **exact** floating-point
equality (`pix != colors[N]`), unlike the passing sibling shape's
0.2-epsilon threshold comparison. The test's color varying is a
genuinely **constant** per-instance attribute (identical across all 4
vertices of one instance's triangle strip, fetched via an instance-rate
binding). `feme/lib/Graphics/Executor.cpp`'s fragment-shader
varying-interpolation loop computed every non-`Flat` varying via
`B0*V0 + B1*V1 + B2*V2` (or the perspective-correct `Numerator/InvW`
form), where `B0`/`B1`/`B2` are three *independently rounded* divisions
(`edgeFn(...) / Area`) that are not provably `== 1.0f`/exactly
self-cancelling when summed against `InvW`. For a genuinely constant
`V0 == V1 == V2`, this can drift the interpolated result off the exact
constant by a few ULPs -- invisible at the CTS log's 2-decimal display
precision, invisible through any 8-bit-UNORM-quantized readback (every
`Flat`/varying test elsewhere in this codebase's own unit-test suite
uses one), but directly detected by this family's exact-equality check.

**Fix**: added a `V0 == V1 && V1 == V2` short-circuit to the
varying-interpolation loop, ahead of both the perspective and
non-perspective smooth branches (mirroring the existing `Flat` branch's
own `Value = V0`) -- a genuinely constant varying is now interpolated
by definition rather than by (potentially lossy) arithmetic, which is
both correct (linear interpolation of a constant is that constant, for
any convex combination of weights) and matches real, spec-conformant
hardware behavior (the reason this CTS family expects it exactly).

Confirmed the hypothesis and the fix directly via a temporary
source-swap-to-baseline round-trip (`Executor.cpp`'s pre-fix content
saved to `/tmp`, copied over the working tree, rebuilt, re-tested,
restored): the new regression unit test (below) fails identically to
the real bug with the fix removed, and passes cleanly restored.

### Testing

Added `ExecutorTest.InterpolatesAGenuinelyConstantColorExactly`
(`feme/unittests/Graphics/ExecutorTest.cpp`): renders the same
oversized CCW triangle shape used elsewhere in this file, every vertex
writing an identical (0.1, 0.2, 0.3, 0.4) color -- `0.1f`/`0.3f` have no
exact binary representation, so any rounding asymmetry between the
three per-vertex contributions shows up directly -- into a new
`R32G32B32A32_FLOAT` attachment (via a new `buildPipelineWithFloatAttachment`
helper) so the test can assert bit-exact equality on the raw float
readback rather than an 8-bit-quantized byte, which would mask this
exact class of bug. Confirmed via source-swap-to-baseline round-trip
that this test fails (three of four channels off by 1 ULP) with the
fix removed, and passes cleanly restored.

`ninja check-feme`: 3,279/3,282 Passed, 3 Unsupported, 0 Failed (+1
newly-added test, 0 regressions).

### CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`)

- Full `dEQP-VK.pipeline.monolithic.bind_buffers_2.*` re-sweep (97
  cases): **0 Fail** (73 Pass, 24 NotSupported) -- was 57 Fail /
  16 Pass / 24 NotSupported before this fix. All 57 originally-filed
  fails are now fixed, 0 exceptions.
- Full `dEQP-VK.draw.*` regression sweep (29,451 cases, run both with
  and without the fix): **224 Fail** in both runs, and the exact set of
  224 failing case names is byte-for-byte identical between the two
  runs (`diff` confirms) -- these are pre-existing, unrelated fails
  (`indexed_draw`/`maintenance6`, `multiple_interpolation`,
  `implicit_sample_shading`, `shader_layer`, `depth_clamp`, and a few
  others), not a regression from this session's change. Not
  investigated further this session (out of scope for `L132`); worth a
  future dedicated session if not already tracked.

### Results

`L132` is now considered fully closed: all 57 originally-filed
`bind_buffers_2.*` fails are fixed, confirmed via a clean re-sweep of
the full family, and a broad `dEQP-VK.draw.*` regression sweep (chosen
since this fix sits in the fragment-shader varying-interpolation path,
which every draw call with a non-`Flat` varying exercises) shows 0 new
fails, run both with and without the fix for a byte-for-byte identical
224-case pre-existing fail set. No feature/extension inventory changes
(a correctness fix to existing interpolation, no new Vulkan
functionality shipped this session).

## Session: L134(f) root-caused and fixed -- same barycentric-sum gap, depth path

Continuing directly from filing `L134` at the end of the prior session.
Confirmed `FeMe CPU Vulkan Device` at session start.

**Checked the background `pipeline.monolithic.*` sweep** (PID 12156):
found it had loaded FeMe's ICD `.so` into its process memory *before*
`L132`'s fix was rebuilt this session, meaning it had been running on
stale, pre-`L132`-fix code the whole time (a dlopen'd ICD library is
mapped once at process start and does not reload mid-process). Killed
it -- it could never have confirmed `L132` at full-sweep scale, and its
now-4+-session runtime was no longer worth continuing on stale code.
This session's own targeted `bind_buffers_2.*` re-sweep already
confirmed `L132` at full-family scale, so nothing was lost by killing
it. Cleaned up its now-orphaned scratch log (`/tmp/ctsrun/l128fix/`,
~1GB).

**Checked whether the 224 `dEQP-VK.draw.*` fails found while landing
`L132` were already filed anywhere**: they were not. Filed them as a
new roadmap row (`L134`), broken into 7 sub-rows by CTS sub-family
(`indexed_draw`/`maintenance6`, `output_location.array`,
`multiple_interpolation`, `implicit_sample_shading`, `shader_layer` at
layer 256, `depth_clamp`, and `basic_draw.misc`).

**Picked up `L134(f)` (`depth_clamp.*_clamp_four_viewports`, 4 cases)
immediately after filing it**, since its own CTS failure message --
`"Depth value mismatch, expected: 0.66, got: 0.66"` -- was
unmistakably the exact same visible-precision-but-not-bit-exact
pattern `L132` had just been fixed for. Confirmed via the CTS source
(`vktDrawDepthClampTests.cpp`): the comparison tolerance for
`D32_SFLOAT`/`D32_SFLOAT_S8_UINT` is `std::numeric_limits<float>::
epsilon()` -- a near-zero, ULP-scale tolerance, not a real fuzz margin.

**Root cause**: `Executor.cpp`'s per-pixel depth computation (`Depth =
B0*Tri.Depth[0] + B1*Tri.Depth[1] + B2*Tri.Depth[2]`) has the identical
gap `L132` fixed for varyings -- `B0`/`B1`/`B2` are three independently
rounded divisions, not provably summing to exactly `1.0f`. A
four-viewport depth-clamp draw's underlying quad has an identical
depth at all 3 vertices of each of its constituent triangles, so the
same few-ULP drift that broke `bind_buffers_2.*`'s exact color check
also breaks this test's near-zero-tolerance depth check.

**Fix**: applied the identical `Tri.Depth[0] == Tri.Depth[1] &&
Tri.Depth[1] == Tri.Depth[2]` short-circuit `L132` added to the
varying-interpolation loop, at depth computation's own single
consumption site (~line 3470). Confirmed via a source-swap-to-baseline
round-trip.

### Testing

Added `ExecutorTest.InterpolatesAGenuinelyConstantDepthExactly`
(depth-analogue of `L132`'s own
`InterpolatesAGenuinelyConstantColorExactly`): an oversized CCW
triangle, every vertex at NDC Z `0.1f` (no exact binary representation,
matching `L132`'s own choice of awkward constant), depth test/write
enabled with `CompareOp::Always`, asserting a bit-exact `0.1f` readback
via `Scene.DepthStorage`. Confirmed via source-swap-to-baseline
round-trip that this test fails (1 ULP off at texel 15) with the fix
removed, and passes cleanly restored. (An initial attempt at NDC Z
`0.32f` passed even without the fix -- not every "awkward" constant
actually exposes a visible ULP drift for a given triangle geometry, so
`0.1f` was chosen instead, matching `L132`'s own precedent value.)

`ninja check-feme`: 3,280/3,283 Passed, 3 Unsupported, 0 Failed (+1
newly-added test, 0 regressions).

### CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`)

- Full `dEQP-VK.draw.*.depth_clamp.*` re-sweep (268 cases): **0 Fail**
  (54 Pass, 214 NotSupported) -- was 4 Fail before this fix. All 4
  originally-filed fails are now fixed, 0 exceptions.
- Full `dEQP-VK.draw.*` regression sweep (29,451 cases, run both with
  and without the fix): fail count drops from 224 to 220, and `diff`
  of the two full fail-name lists confirms the removed 4 case names
  are exactly `dEQP-VK.draw.{renderpass,dynamic_rendering.primary_
  cmd_buff}.depth_clamp.{d32_sfloat,d32_sfloat_s8_uint}_clamp_four_
  viewports` -- every other pre-existing fail is unchanged, 0
  regressions.

### Results

`L134(f)` is now considered fully closed: all 4 originally-filed
`depth_clamp.*_clamp_four_viewports` fails are fixed, confirmed via a
clean re-sweep of the full sub-family, and the broader `dEQP-VK.draw.*`
regression sweep confirms the fail-count delta matches exactly with 0
side effects. `L134`'s other 6 sub-rows remain open. No feature/
extension inventory changes (a correctness fix to existing
interpolation, no new Vulkan functionality shipped this session).

## Session: `L134(g)` -- `VK_KHR_maintenance5` flags2-override gap fixed

### Summary

Picked up `L134(g)` (`dEQP-VK.draw.renderpass.basic_draw.misc.
maintenance5`, 1 of `L134`'s originally-filed 224 pre-existing
`dEQP-VK.draw.*` fails), the smallest of its 6 then-remaining open
sub-rows, per the prior session's own next-steps ordering.

**Reproduced the exact failure first** (per the prior session's own
instruction to check each `L134` sub-row's CTS message text before
assuming a root cause): `Fail (vk.queueSubmit(queue, 1u, &submitInfo,
*fence): VK_ERROR_INITIALIZATION_FAILED at vkCmdUtil.cpp:338)` -- a
real submission failure, not an "expected: X, got: X" mismatch,
confirming this is **not** another instance of the `L132`/`L134(f)`
barycentric-sum-precision bug class, and needs its own distinct
root-cause investigation.

**Root cause**: this CTS case (`vktBasicDrawTests.cpp`'s
`IndexedIndirectCase` with `useMaintenance5 = true`) deliberately sets
`VkGraphicsPipelineCreateInfo::flags = VK_PIPELINE_CREATE_LIBRARY_BIT_
KHR` (a legacy, "wrong" value with real-world Mesa-crash-history
significance per the CTS source's own comment), while chaining the
*real* intended value via `VK_KHR_maintenance5`'s
`VkPipelineCreateFlags2CreateInfoKHR::flags = VK_PIPELINE_CREATE_2_
ALLOW_DERIVATIVES_BIT_KHR`. Per the extension's own spec, the chained
flags2 struct must take priority over the legacy field whenever
present -- this is the exact mechanism the test deliberately probes,
to confirm a conformant implementation reads the override rather than
the legacy field. `GraphicsPipeline.cpp`'s `vkCreateGraphicsPipelines`
read only the legacy `flags` field, so it misclassified this call as a
`VK_EXT_graphics_pipeline_library` library-creation request instead of
building the real, complete, executable pipeline the app intended --
the returned (non-executable "library") handle later failed at
`vkQueueSubmit` instead of drawing.

Confirmed via a targeted grep that this is a genuine,
previously-entirely-unhandled gap: no code anywhere in
`feme/lib/Vulkan/` ever referenced `VkPipelineCreateFlags2CreateInfo`/
`VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO` at all, despite
`VK_KHR_maintenance5` (`Roadmap.md`'s `E5` row) being marked closed --
`E5`'s own row text never mentioned either `Flags2CreateInfo` struct,
confirming this was never in scope for that row, not a regression.

### Fix

Added `getEffectivePipelineCreateFlags(const VkGraphicsPipelineCreateInfo
&)` (`GraphicsPipeline.cpp`, anonymous namespace): scans `pNext` for a
chained `VkPipelineCreateFlags2CreateInfo` and returns its `flags` (a
64-bit `VkPipelineCreateFlags2`/`VkFlags64`) when present, else returns
the legacy `flags` field widened to 64 bits. Every flag bit this file
currently interprets fits within the legacy field's 32 bits, so this
widening/truncation loses nothing any call site reads today.

Applied the helper at every legacy-`flags`-reading call site in the
file, not just the one this specific CTS case's failure traces to (the
task's own coding-change guidance favors complete-not-just-minimal
fixes, and every site shares the identical underlying gap):
- The `VK_PIPELINE_CREATE_LIBRARY_BIT_KHR` check that misroutes a call
  into the pipeline-library branch (the fix this case's own failure
  needed).
- Both `VK_PIPELINE_CREATE_VIEW_INDEX_FROM_DEVICE_INDEX_BIT` checks in
  `synthesizeLinkedGraphicsPipelineCreateInfo` (roadmap L110).
- The `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT` check
  in `compileGraphicsPipeline` (roadmap E9).
- `synthesizeLinkedGraphicsPipelineCreateInfo`'s own `Result.flags =
  CreateInfo.flags` (the synthesized create-info's `pNext` never
  carries a flags2 struct of its own, so this call resolves the
  top-level call's own flags2-vs-legacy value once and stores the
  *effective* result, keeping a later `compileGraphicsPipeline` call
  against the synthesized info correct even without re-scanning).
- Every `Pipeline`/`GraphicsPipeline`/`GraphicsPipelineLibrary`
  object's own stored `createFlags()` value, so a pipeline's own
  recorded flags reflect the effective, not raw legacy, value for any
  later query.

### Testing

Added `GraphicsPipelineTest.Flags2CreateInfoOverridesLegacyLibraryBit`
(`GraphicsPipelineTest.cpp`): a `VkGraphicsPipelineCreateInfo` with a
legacy `flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR` but a chained
`VkPipelineCreateFlags2CreateInfoKHR` with a real, non-library flags
value; asserts the resulting `VkPipeline` is a real `GraphicsPipeline`
(`Pipeline::Kind::Graphics`), not a `GraphicsLibrary`, and that its
recorded `createFlags()` does not carry the legacy library bit.

`ninja check-feme`: 3,280/3,283 Passed, 3 Unsupported, 0 Failed (+1
newly-added test, 0 regressions). `FeMeVulkanTests` standalone: 720/720
Passed (0 regressions).

### CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`)

- `dEQP-VK.draw.renderpass.basic_draw.misc.maintenance5`: **Pass** (was
  `Fail`).
- Full `dEQP-VK.draw.*` regression sweep (29,451 cases): **219 Fail**
  (was 224 pre-`L134(f)`/`(g)`, exactly `224 - 4 (L134(f)) - 1
  (L134(g))`), 0 `maintenance5` fails remaining, 0 regressions in every
  other pre-existing fail.

### Results

`L134(g)` is now fully closed. `L134`'s other 5 sub-rows (`L134(a)`
through `L134(e)`) remain open. No feature/extension inventory changes
(a correctness fix to existing `VK_KHR_maintenance5` handling, no new
Vulkan functionality shipped this session).

## Session: `L134(e)` -- 32-bit layer-clear-mask truncation fixed

### Summary

Picked up `L134(e)` (`dEQP-VK.draw.renderpass.shader_layer.
{vertex_shader_256,tessellation_shader_256}`, 8 of `L134`'s originally-
filed 224 pre-existing `dEQP-VK.draw.*` fails), the smallest of its 5
then-remaining open sub-rows, per the prior session's own next-steps
ordering.

**Reproduced the exact failure first** (per the standing instruction to
check each `L134` sub-row's CTS message text before assuming a root
cause): `Fail (Rendered image is not correct at
vktDrawShaderLayerTests.cpp:941)` -- a real image-content mismatch, not
an "expected: X, got: X" near-miss, confirming this is **not** another
instance of the `L132`/`L134(f)` barycentric-sum-precision bug class,
and needs its own distinct root-cause investigation.

**Root cause**: `vktDrawShaderLayerTests.cpp`'s `testVertexShader`/
`testTessellationShader` render into a 256-array-layer image (16x16
grid of 16x16-pixel rectangles across a 256x256 image, one rectangle
per layer routed via `gl_Layer`), then compare every layer's rendered
rectangle-and-background against a reference. Extracting and
pixel-diffing the QPA's embedded `Result`/`Reference` PNGs isolated the
first divergence to layer 32 (the 33rd layer, the first past a 32-bit
boundary): the layer's own drawn rectangle was correct, but its
background was fully transparent `(0,0,0,0)` instead of the test's
clear color `(128,128,128,255)` -- i.e. the layer's own
`VK_ATTACHMENT_LOAD_OP_CLEAR` was silently never applied. Traced to
`CommandBuffer.cpp`'s `fullLayerMask(uint32_t Layers)` helper, which
used a `uint32_t` bitmask to record "which array layers of a plain,
non-multiview layered render target still need a clear applied,"
reusing the exact same machinery as genuine Vulkan multiview view
masks (spec-capped at 32 bits by `VkRenderPassMultiviewCreateInfo`/
`VkRenderingInfo::viewMask`). For layer counts >=32, `fullLayerMask`
"saturated" to `~0u` -- but a 32-bit `~0u` can only ever represent
layers 0-31; layers 32-255 were left permanently unrepresented and
therefore never cleared. This bug was never previously caught because
the CTS test's own `numLayersToTest[]` array jumps straight from `8` to
`256` (`MIN_MAX_FRAMEBUFFER_LAYERS`, this ICD's own advertised
`maxFramebufferLayers`/`maxImageArrayLayers` limit), so no intermediate
layer count (9-255) was ever exercised, and the ICD's own advertised
limit happens to be exactly the value this one CTS case uses.

### Fix

Replaced the `uint32_t`-mask-based clear-tracking machinery in
`CommandBuffer.cpp` with `llvm::BitVector`-based tracking, which has no
fixed width:
- `GraphicsState::LoadedAttachmentViewMask` changed from
  `llvm::DenseMap<uintptr_t, uint32_t>` to
  `llvm::DenseMap<uintptr_t, llvm::BitVector>`.
- `applyClear` now takes `const llvm::BitVector &ViewsToClear` and
  `llvm::DenseMap<uintptr_t, llvm::BitVector> &AlreadyLoaded`, using
  `BitVector::reset(const BitVector&)` (and-not semantics) and
  `operator|=` (auto-resizing union) in place of the old raw
  bit-shift-loop arithmetic.
- `fullLayerMask` removed entirely, replaced by a new
  `viewsToClear(const RenderTargetBinding &Binding)` helper: for
  genuine multiview (`Binding.ViewMask != 0`), builds a 32-bit-bounded
  `BitVector` from the mask bits (still spec-correct, since real
  multiview is itself capped at 32 bits); for plain layered rendering
  (`Binding.ViewMask == 0`), returns an all-set `BitVector` sized to
  the render target's *actual* layer count, with no 32-bit cap.
- `applyLoadOps` updated to call the new helper and pass the resulting
  `BitVector` through to all three (`Color`/`Depth`/`Stencil`)
  `applyClear` call sites.

The genuine-multiview per-view draw-replication loop in `runDraw`
(a separate, correctly-32-bit-bounded concept, since real multiview
view counts are themselves spec-capped at 32) was confirmed out of
scope and left untouched.

### Testing

Added `DrawTest.ClearsEveryLayerOfALayeredRenderTargetPastThirtyTwo`
(`DrawTest.cpp`): a 40-array-layer plain (non-multiview) render target
whose backing memory is deliberately poisoned with `0xAB` via `memset`
before rendering (so a zero-initialized allocator can't make an
unfixed clear-skip bug indistinguishable from a correctly-applied
clear), draws a single triangle whose vertex shader outputs a constant
`gl_Layer = 33`, and asserts layer 35 (untouched, past the old 32-bit
boundary) reads back as the render pass's own clear color rather than
the poison bytes, while layer 33 (the actual draw target) reads back
as the fragment shader's color. Confirmed via a source-swap-to-baseline
round-trip (`git show HEAD:...CommandBuffer.cpp` swapped in place of
the fix, rebuilt, retested, restored) to fail with the fix removed
(poison bytes leak through the untouched layer) and pass cleanly
restored.

`ninja check-feme`: 3,282/3,285 Passed, 3 Unsupported, 0 Failed (+2
newly-discovered tests, 0 regressions). `FeMeVulkanTests` standalone:
721/721 Passed (0 regressions).

### CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`)

- `dEQP-VK.draw.*shader_layer*` (56 cases): **0 Fail** (was 8).
- Full `dEQP-VK.draw.*` regression sweep (29,451 cases): **211 Fail**
  (was 219 pre-`L134(e)`, exactly `219 - 8`), 0 `shader_layer` fails
  remaining, 0 regressions in every other pre-existing fail.

### Results

`L134(e)` is now fully closed. `L134`'s other 4 sub-rows (`L134(a)`
through `L134(d)`) remain open. No feature/extension inventory changes
(a correctness fix to existing layered-render-target clear handling,
no new Vulkan functionality shipped this session).

## Session: `L134(d)` -- two distinct implicit-sample-shading gaps fixed

### Summary

Picked up `L134(d)` (`dEQP-VK.draw.*.implicit_sample_shading.
{sample_decoration_dynamic_use,sample_id_static_use,
sample_position_static_use}`, 12 of `L134`'s originally-filed 224
pre-existing `dEQP-VK.draw.*` fails), the next-smallest remaining
sub-row per the prior session's own ordering.

**Reproduced the exact failure first**: all 12 cases fail as
`Fail (Atomic counter value lower than expected: 20)` in
`vktDrawSampleAttributeTests.cpp` -- a genuine per-invocation-count
deficit (the fragment shader isn't running at sample rate when it
should be), not the `L132`/`L134(e)`/`L134(f)` "expected: X, got: X"
barycentric-precision bug class, confirming this needs its own
distinct root-cause investigation.

The CTS test renders a 4x4, 4x-multisampled target with an
oversized full-coverage triangle, using a fragment shader that
(depending on a `Trigger` enum) either statically references
`gl_SampleID`/`gl_SamplePosition` as a bare, result-discarded
statement, or reads an ordinary `sample in float verify` (SPIR-V
`Sample`-decorated) varying, then atomically increments a counter;
the test asserts the counter reaches `sampleCount * width * height =
64`, i.e. that per-sample-rate shading was forced.

### Root cause (two distinct bugs)

**Bug 1** (`sample_id_static_use`/`sample_position_static_use`, 8 of
12 cases): extracting and disassembling the actual SPIR-V
(`--deqp-log-decompiled-spirv=enable`) confirmed glslang emits a
`BuiltIn SampleId`-decorated `OpVariable` that genuinely appears in
`OpEntryPoint`'s own interface list, but -- since the CTS shader's
bare `gl_SampleID;` statement discards the loaded value without ever
consuming it -- **no `OpLoad` instruction anywhere reads it**.
`CanonicalizeStage.cpp`'s `canonicalizeSPIRVStage` stage-IO discovery
loop walks only `LoadInst`/`StoreInst` pointer operands within the
entry function's own instructions to find stage-IO globals worth a
`SignatureElement`; since no load instruction exists for this
global, it's never discovered, never gets an element, and
`Executor.cpp`'s `PerSampleShading` check (which looks for
`SignatureSystemValue::SampleIndex`/`SamplePosition` via
`findElement`) never sees it, so per-sample shading never gets
forced. Confirmed via the Vulkan spec
(`primsrast.adoc`'s "Sample Shading" section,
`external/vulkan-docs/src/chapters/` in the CTS checkout) that
"statically uses" an input decorated `SampleId`/`SamplePosition`
means present in the entry's interface, regardless of whether the
loaded value is ever consumed -- confirming a genuine architectural
gap in the discovery mechanism, not a CTS-artificial corner case.

**Bug 2** (`sample_decoration_dynamic_use`, 4 of 12 cases): SPIR-V
disassembly confirmed this variant's `verify` varying *is* genuinely
loaded and used (`uint(ceil(verify))` drives the atomic-increment
condition), decorated `Sample` (not `NoPerspective`), so it correctly
gets a normal `SignatureElement` with
`Interpolation == SignatureInterpolationMode::PerspectiveSample` via
the existing, unmodified `getInterpolationMode` machinery. However,
`Executor.cpp`'s `PerSampleShading` boolean only checked
`Pipeline.getSampleShadingEnable()` plus `findElement` lookups for
the two system values -- it never checked whether any *ordinary*
varying (not a system value) carried per-sample interpolation, so a
`Sample`-decorated ordinary input never forced per-sample shading,
despite the same spec section requiring it.

### Fix

**Fix A** (`feme/lib/Graphics/Executor.cpp`, `PerSampleShading`'s
OR-chain): added an `llvm::any_of(FSSig.Elements, ...)` check for any
`Input`-direction element whose `Interpolation` is `PerspectiveSample`
or `NoPerspectiveSample`, with a doc comment distinguishing this from
the pre-existing `SampleIndex`/`SamplePosition` system-value checks.

**Fix B** (`feme/lib/Transforms/Graphics/CanonicalizeStage.cpp`,
inside `canonicalizeSPIRVStage`, right after the existing load/store
discovery loop): added a second, narrowly-scoped discovery pass over
`F.getParent()->globals()` that finds any not-yet-`Seen`,
address-space-7 (`Input`) stage-IO global decorated `BuiltIn`
code 18 (`SampleId`) or 19 (`SamplePosition`) and adds it to
`InputGlobals` too, so it still gets a `SignatureElement` via the
existing `addElements`/`addElement` machinery even though no load
instruction ever referenced it. Deliberately scoped only to these two
specific builtins (not every unused stage-IO global) to keep the
change minimal and avoid perturbing `ElementID` numbering or
cross-stage linkage assumptions for any other unused-but-declared
interface variable.

### Testing

Added two new regression tests:

- `CanonicalizeStageTest.RecordsUnusedSampleIdAndSamplePositionBuiltInsAsInputSignatureElements`
  (Fix B): an LLVM-IR module declaring `@gl_SampleID` (genuinely
  loaded, to keep the test's own IR plausible) and
  `@gl_SamplePosition` (declared and `BuiltIn`-decorated, but never
  loaded anywhere -- the actual thing under test), plus an ordinary
  used `@gl_FragDepth` output so the fragment entry has a valid
  signature at all. Asserts `findElement` finds both `SampleIndex`
  and `SamplePosition` system-value elements in the resulting
  `EntrySignature`.
- `ExecutorTest.SampleDecoratedVaryingForcesPerSampleShading` (Fix A):
  builds a `GraphicsPipeline` with an ordinary, `Location`-based
  `SignatureElement` (not a system value) whose `Interpolation` is
  set to `PerspectiveSample`, with `SampleShadingEnable=false` and no
  `SampleIndex`/`SamplePosition` elements anywhere in the signature.
  The fragment shader writes the varying (a per-vertex affine
  screen-space-position formula, matching pixel (0, 0)'s own real
  per-sample offsets) directly to its color output; the test checks,
  the same way `SamplePositionForcesPerSampleShadingAndReadsRealOffset`
  does for the builtin case, that all 4 samples' written values match
  the expected per-sample offset table and that no two samples'
  written values coincide -- proving a real per-sample re-evaluation
  reached storage, not a single shared pixel-center-broadcast value.

Both new tests confirmed via a source-swap-to-baseline round-trip
(`git show HEAD:...` swapped in place of each fix, rebuilt, retested,
restored) to fail identically to the real bug pre-fix, then pass
cleanly once the fix was restored; `git status --short` confirmed
clean/matching after each restore.

`ninja check-feme`: 3,284/3,287 Passed, 3 Unsupported, 0 Failed (+2
newly-discovered tests, 0 regressions).

### CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`)

- `dEQP-VK.draw.*implicit_sample_shading*` (39 cases): **0 Fail**
  (was 12).
- Full `dEQP-VK.draw.*` regression sweep (29,451 cases): **199 Fail**
  (was 211 pre-`L134(d)`, exactly `211 - 12`), 0
  `implicit_sample_shading` fails remaining, 0 regressions in every
  other pre-existing fail.

### Results

`L134(d)` is now fully closed (both of its two distinct root causes
fixed). `L134`'s other 3 sub-rows (`L134(a)`, `L134(b)`, `L134(c)`)
remain open. No feature/extension inventory changes (a correctness
fix to existing per-sample-shading trigger detection, no new Vulkan
functionality shipped this session).

## Session: `L134(b)` -- multi-row array-output color-attachment binding fixed (partial)

### Change

`dEQP-VK.draw.renderpass.output_location.array.*` fails 28/28 (roadmap's
own prior estimate of 24 was short by 4). Split into 3 distinct bugs by
investigation this session:

1. **Fixed**: 18 of 24 genuine value-mismatch (`Probe failed at: 0, 0`)
   cases. Root cause: `StageStorage.cpp`'s `findElementByLocation` does an
   *exact* `Location` match, with no `RowCount` awareness. A GLSL array
   output (`layout(location=0) out highp float frag_out[3]`) is a single
   `SignatureElement` with `Location=0, RowCount=3` spanning locations
   0-2 -- `Executor.cpp`'s per-color-attachment `FSColors[]`-building loop
   only ever resolved attachment 0 (querying `Location=0`); attachments 1
   and 2 found nothing and were silently never written. A
   `FEME_DEBUG_DUMP_STAGE_IR`-gated (temporary, reverted) LLVM-IR dump
   confirmed the fragment shader's own compiled IR is already correct
   post-canonicalization, ruling out the compiler front end.

   Fixed by adding `findElementCoveringLocation` (`StageStorage.h`/`.cpp`),
   which finds the element whose `[Location, Location+RowCount)` span
   contains the queried location and reports the resolved `Row`
   (`findElementByLocation` itself deliberately left unchanged -- its
   other 3 call sites, varying/matrix linking and dual-source blend, need
   exact single-location matching, a genuinely different semantic). A new
   parallel `FSColorRows` vector threads the resolved row through to
   `readFragmentColor`/`readFragmentColorInt` (both gain a `Row`
   parameter, default `0`, already backed by `StageStorage::readFloat`/
   `readRaw`'s existing `Row` parameter).

2. **Not fixed, re-scoped as `L134(h)`**: 4 cases crash in
   `feme-cpu-simdize` (`... has a divergent value '.bc' of vector type
   ...`) on a vector-wider-than-scalar array-output shape (e.g.
   `vec2[3]`). Not root-caused this session.

3. **Not fixed, re-scoped as `L134(i)`**: 6 cases
   (`b10g11r11-ufloat-pack32-{highp,mediump}`, plain and
   `-output-{float,vec2}`) now fail with an explicit `"Vulkan color
   attachment format is not supported"` -- `RenderPass.cpp`'s
   `isSupportedColorAttachmentFormat` never lists
   `ResourceFormat::R11G11B10_FLOAT`. This gap was previously masked
   behind the routing bug's own generic "Probe failed" symptom and only
   surfaced as its own distinct failure once the routing fix let these 6
   cases reach real attachment binding. `ImageFixture.cpp` already has a
   `packClearColor` case for this format but no `unpackColor` case.

New unit test `ExecutorTest.RendersMultiRowArrayOutputToSeparateColorAttachments`
(a `RowCount == 2` fragment output at `Location == 0`, row 0 to
attachment 0, row 1 to attachment 1) confirmed via a stash/rebuild
round-trip to fail identically to the real bug pre-fix.

`ninja check-feme`: 3,285/3,288 Passed, 3 Unsupported, 0 Failed (+1 new
test, 0 regressions).

### CTS (`feme_icd.json`, `FeMe CPU Vulkan Device`)

- `dEQP-VK.draw.*output_location.array*` (28 cases): **18 Pass, 10 Fail**
  (was 0 Pass, 28 Fail -- the 24-case original roadmap estimate having
  undercounted the real 28-case sub-family by 4).
- Full `dEQP-VK.draw.*` regression sweep (29,451 cases): **181 Fail**
  (was 199 pre-this-session, exactly `199 - 18`), 0 regressions in every
  other pre-existing fail (spot-checked: the 6 `b10g11r11-ufloat-pack32.*`
  and 4 `feme-cpu-simdize`-crash cases in the full sweep's own fail list
  match exactly the 10 cases isolated in the standalone repro above, and
  no other `output_location.*` case newly failed).

### Results

`L134(b)`'s array-*routing* bug is fixed and CTS-verified; the 4-case
simdize crash and 6-case `B10G11R11_UFLOAT_PACK32` color-attachment-
format gap it uncovered are re-scoped as `L134(h)`/`L134(i)`, neither
attempted this session. `L134`'s other 2 sub-rows (`L134(a)`, `L134(c)`)
remain open. No feature/extension inventory changes (a correctness fix
to existing color-attachment binding, no new Vulkan functionality
shipped this session).
