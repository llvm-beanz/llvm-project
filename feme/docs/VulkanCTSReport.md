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
