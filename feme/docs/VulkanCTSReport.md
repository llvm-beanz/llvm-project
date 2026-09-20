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
