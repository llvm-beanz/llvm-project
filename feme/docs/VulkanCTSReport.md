# FeMe Vulkan ICD: Vulkan CTS Status Report

This report is the current full-suite measurement of `libfeme_vulkan`. The
roadmap owns remaining work; design documents own implementation decisions.
Targeted milestone notes from earlier sessions have been removed now that a
new complete baseline supersedes them.

## Scope and provenance

- FeMe source revision: `1170553cf03edc5986b7a3104f7effee4faae6b7`
- VK-GL-CTS revision: `880f31a2bd9cd0659f84f3f80dafd07f2e693f6d`
  (`vulkan-cts-1.4.6.2-525-g880f31a2`)
- Case list: 3,244,369 cases in all 54 top-level `dEQP-VK` groups
- Device: `FeMe CPU Vulkan Device`
- Build: `Release`, `LLVM_ENABLE_ASSERTIONS=ON`, C and C++ compilation through
  `ccache`, `-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON`
- `check-feme`: 3,587 passed, 3 unsupported, 0 failed
- Registry used by the inventories: `VK_HEADER_VERSION` 358

The inventory audit reports 87 of 150 Vulkan core feature bits advertised, 49
of 86 promoted extensions implemented, and 41 extensions advertised by name.
See [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md).

## Method

The build-tree ICD was rebuilt before testing and selected explicitly through
both `VK_DRIVER_FILES` and `VK_ICD_FILENAMES`. `vulkaninfo --summary` reported
`FeMe CPU Vulkan Device`.

`feme/utils/run_vulkan_cts.py` ran from the Vulkan module directory with six
workers, 1,800-case initial batches, recovery batches of 200, 20, and 1, and a
60-second timeout for isolated cases. The initial sweep completed 1,802
batches normally; one batch stopped making progress for roughly three hours
and its single `deqp-vk` process was terminated so QPA reconciliation could
continue. Recovery used 97, 153, and 1,264 batches at the three granularities.

Every one of the 47,192 initially reported ordinary failures was run again in
its own process. Eighteen mesh-shader cases changed from `Fail` to `Pass` and
are treated as contention-only flakes, not failures. Fifty-three cases still
lacked a complete QPA record after bounded recovery. A final isolated run with
exit-status capture classified three as ordinary `Fail` and 50 as
signal-terminated processes (47 `SIGSEGV`, three `SIGABRT`). Every generated
case therefore has a measured outcome.

Artifacts, including all QPA files and process logs, `report.txt`,
`verified-failures.txt`, and the abnormal-case classification, are retained at
`/home/dev/dev/VK-GL-CTS/run/feme-20261005-full-1170553-complete/`.

## Headline

| Result | Count | Share of total |
|---|---:|---:|
| Total | 3,244,369 | 100.0000% |
| Measured | 3,244,369 | 100.0000% |
| Pass | 650,027 | 20.0355% |
| Fail | 47,174 | 1.4540% |
| NotSupported | 2,546,724 | 78.4967% |
| QualityWarning | 46 | 0.0014% |
| InternalError | 0 | 0.0000% |
| DeviceLost | 348 | 0.0107% |
| Crashed | 50 | 0.0015% |
| TimedOut | 0 | 0.0000% |
| Unrun | 0 | 0.0000% |

The accounting identity is:

```text
Total = Pass + Fail + NotSupported + QualityWarning + InternalError
      + DeviceLost + Crashed + TimedOut + Unrun
```

`DeviceLost` is a complete QPA result and is kept separate from a process
crash. It is nevertheless a conformance failure and a stability priority.

## Comparison with the preceding verified run

The ordinary failure count fell by **1,133**, from 48,307 to 47,174. Passes
rose by 12,226 and `NotSupported` fell by 11,411, showing that substantial
capability coverage became executable. The distribution changed sharply:
many former `api`, `image`, `glsl`, and `renderpasses` failures are resolved,
while newly reachable image and sampler coverage now dominates `pipeline`.

| Result | 2026-09-26 | 2026-10-05 | Delta |
|---|---:|---:|---:|
| Pass | 637,801 | 650,027 | +12,226 |
| Fail | 48,307 | 47,174 | -1,133 |
| NotSupported | 2,558,135 | 2,546,724 | -11,411 |
| QualityWarning | 47 | 46 | -1 |
| InternalError | 9 | 0 | -9 |
| DeviceLost | 0 | 348 | +348 |
| Crashed | 68 | 50 | -18 |
| TimedOut | 2 | 0 | -2 |

The largest ordinary-failure changes by group are:

| Group | Prior Fail | Current Fail | Delta |
|---|---:|---:|---:|
| `pipeline` | 3,710 | 39,644 | +35,934 |
| `api` | 15,545 | 34 | -15,511 |
| `image` | 6,941 | 14 | -6,927 |
| `renderpasses` | 7,854 | 4,845 | -3,009 |
| `glsl` | 2,933 | 15 | -2,918 |
| `spirv_assembly` | 995 | 458 | -537 |
| `texture` | 453 | 2 | -451 |
| `tessellation` | 396 | 30 | -366 |
| `transform_feedback` | 2,053 | 1,918 | -135 |
| `memory_model` | 166 | 52 | -114 |

## Current triage

1. **Linearizer dominance regression — `L377`/`L377(a)` fully resolved this
   session.** `feme-cpu-linearize`'s `LoopLinearizer::linearizeCycle` now
   actually linearizes the `CheckBlock`-not-sole-entry shape instead of
   merely diagnosing it: the offending second divergent branch (`ChainGate`)
   is detected, its bypass arm is proven to be pure, side-effect-free
   control-flow glue, and the two arms are merged via a `select`-based
   condition with `ChainGate`'s own branch replaced by a single
   unconditional branch into `CheckBlock` (eliminating the divergent branch
   entirely, not just repairing dominance -- `feme-cpu-simdize` requires
   zero divergent branches to remain, which the first dominance-only attempt
   did not satisfy). **Re-verified via a complete rerun of all three
   affected families** (`pipeline.monolithic.image.*`: 122,912 cases, 46,112
   Pass / **0 Fail** / 76,800 NotSupported; `pipeline.monolithic.image_view.*`:
   27,708 cases, 15,936 Pass / **0 Fail** / 11,772 NotSupported;
   `pipeline.monolithic.sampler.view_type.*`: 74,616 cases, 15,078 Pass /
   **0 Fail** / 59,538 NotSupported -- all 0 crashes, 0 DeviceLost). All
   31,024 previously-failing `image*`/`image_view*` cases and all 7,453
   previously-failing `sampler.view_type.*` cases (same root cause, see
   below) now pass, confirmed not from the Pass/Fail totals alone but also
   against the real end-to-end reproducer
   (`pipeline.monolithic.image.dedicated_allocation.sampling_type.combined.view_type.1d.format.r16_sfloat.count_1.size.127x1_compute`),
   which produces correct pixels. `ninja check-feme`: 3,589/3,589 Passed, 0
   Failed, 3 Unsupported, 0 regressions. No `Vulkan14FeatureInventory.md`/
   `VulkanExtensionInventory.md` change needed -- internal
   compiler-correctness fix only.
2. **Sampler coverage — `L378` bulk folded into `L377(a)`, now fully
   resolved including `L378(a)`.** Of the 7,454 `sampler` failures, 7,453
   (`sampler.view_type.*`) shared `L377`'s exact root cause and were fixed
   by the `L377(a)` linearizer change (see above). The one remaining case,
   `sampler.max_sampler_lod_bias.shader_bias`, is now also fixed
   (`L378(a)`): a `--deqp-log-images=enable` QPA diff against the test's 13
   known mip clear colors showed FeMe sampling mip level 12 (max) instead
   of the expected level 2, root-caused to `Executor.cpp`'s per-tile
   fragment-quad construction extrapolating a literal, unclamped
   `gl_FragCoord` for synthetic "helper" lanes that fall entirely outside
   the attachment's own extent (unavoidable on this test's 1x1
   framebuffer), manufacturing a large spurious screen-space derivative
   that drove the implicit-LOD base far above 0 before the 2.0 bias was
   even added. Fixed by clamping each lane's position to
   `[0, ExtentWidth-1]x[0, ExtentHeight-1]` at the two `gl_FragCoord`
   call sites (ordinary and per-sample-shading paths) -- a no-op for any
   genuinely covered lane. A direct rerun of `sampler.max_sampler_lod_bias.*`
   (10 cases) now shows 8 Pass / **0 Fail** / 2 NotSupported (the
   NotSupported pair is the pre-existing, unrelated `VK_EXT_image_view_min_lod`
   gap). `ninja check-feme`: 3,590/3,593 Passed (+1 new regression test),
   3 Unsupported, 0 Failed, 0 regressions. No `Vulkan14FeatureInventory.md`/
   `VulkanExtensionInventory.md` change needed -- internal rasterizer
   correctness fix only.
3. **Render-pass correctness and device loss.** `renderpasses` has 4,845
   ordinary failures and 328 `DeviceLost` results. The largest ordinary
   families are dynamic rendering (2,990) and suballocation tests (1,842).
   **Update (roadmap `L379`/`L379(b)`, this session):** the full 328-case
   `renderpasses` `DeviceLost` share is now fixed. Two independent bugs
   were found and fixed: (1) `CommandBuffer.cpp`'s `resolveDrawAttachments`
   wrongly required every bound color attachment to either all resolve or
   none, when the Vulkan spec allows resolving independently per
   attachment; (2) `vkCmdSetRenderingAttachmentLocations` bounded location
   values against the bound attachment count instead of the device's
   `maxColorAttachments` limit, and `Executor.cpp`'s `locationForAttachment`
   searched its lookup array backwards (only coincidentally correct for
   self-inverse remaps). A complete, from-scratch re-run of
   `dEQP-VK.renderpasses.dynamic_rendering.*` (28,898 cases) now shows
   **zero** `DeviceLost`: 9,917 Pass / 2,506 ordinary Fail / 16,475
   NotSupported. The 2,506 ordinary Fails are a separate, not-yet-triaged
   gap (largest families: `primary_cmd_buff.suballocation` 993,
   `primary_cmd_buff.random` 756, `graphics_pipeline_library.suballocation`
   144, `complete_secondary_cmd_buff.suballocation` 144, all returning
   `VK_ERROR_INITIALIZATION_FAILED` at pipeline construction) -- left open
   for a future session, see `Roadmap.md`. The 46 `pipeline` `SIGSEGV`s
   below remain completely untouched and are the next stability-triage
   priority.
4. **Transform feedback and SPIR-V long tails.** `transform_feedback` has
   1,918 failures, led by primitives-generated query get/copy cases (1,308).
   `spirv_assembly` has 458 failures and two isolated `SIGABRT` crashes.
5. **Process stability.** The final isolated classification records 50
   crashes: 46 `pipeline` `SIGSEGV`s, one `api` `SIGSEGV`, one
   `graphicsfuzz` `SIGABRT`, and two `spirv_assembly` `SIGABRT`s. There are
   no remaining timeouts. Separately, completed QPA records contain 348
   `DeviceLost` results: 328 `renderpasses`, 12 `pipeline`, and 8
   `robustness`. **Update (this session):** `L379`/`L379(b)` fixed the full
   328-case `renderpasses` share (see item 3 above) -- 20 `DeviceLost`
   results remain (12 `pipeline`, 8 `robustness`), neither re-triaged this
   session. The 46 `pipeline` `SIGSEGV`s are also still unstarted: no case
   names are recorded anywhere, so the next stability session should
   narrow by `pipeline` sub-family first, the same approach that worked
   for `renderpasses` this session and last.

6. **Pipeline/api `SIGSEGV` triage begun — `L381`/`L382` fixed 3 of the 46
   `pipeline`-group/one `api`-group crashes this session.** Two independent
   `pipeline` `SIGSEGV`s (`fast_linked_library`/`pipeline_library.misc.
   compatible_render_pass`, `L381`) and the one `api` `SIGSEGV`
   (`api.pipeline.pipeline_invalid_pointers_unused_structs.graphics`,
   `L382`) were reproduced and fixed; see `Roadmap.md` for full root-cause
   detail. The `compatible_render_pass` fix required gating two further,
   independently-discovered latent bugs in the same eager-caching code path
   (an unconditional-capture bug and a dynamic-rendering pNext-scoping
   bug), both found via a new regression unit test
   (`GraphicsPipelineTest.LinksAfterOriginalRenderPassIsDestroyed`) and the
   full suite rerun it triggered, not by code review alone. Fixing
   `pipeline_invalid_pointers_unused_structs` required gating four
   pointer-dereferences on a new `isRasterizationStaticallyDiscarded`
   helper (`GraphicsPipelineTest.
   IgnoresUnusedStructsWhenRasterizationStaticallyDisabled`). **Verified:**
   both `compatible_render_pass` cases, both `bad_rendering_create_info`/
   `null_rendering_create_info_ptr`, and `pipeline_invalid_pointers_
   unused_structs.{graphics,compute}` all Pass via direct reruns; a
   70/88-case `pipeline_library.graphics_library.misc.*` sweep and a
   2,984-case `pipeline.*.misc.*` sweep both complete with zero
   crashes/regressions; `ninja check-feme`: 3,595/3,598 Passed (+3 new unit
   tests), 3 Unsupported, 0 Failed. **44 `pipeline` `SIGSEGV`s remain
   open** (still not narrowed to case names -- a fresh, full `pipeline.*`
   crash sweep naming every surviving case is the natural next step for a
   future session). No `Vulkan14FeatureInventory.md`/
   `VulkanExtensionInventory.md` change needed for either fix -- internal
   correctness fixes only.
7. **`graphicsfuzz` crash reproduced, deliberately not fixed —
   `L383(a)`.** The one `graphicsfuzz`-group crash (item 5's accounting)
   was reproduced this session: `dEQP-VK.graphicsfuzz.
   nested-for-loops-switch-fallthrough` is an `assert()` failure
   (`llvm/include/llvm/IR/CFG.h:63`, "pred_iterator out of range!") during
   SPIR-V-to-LLVM translation, not a `SIGSEGV`. Judged likely the same
   class of bug as `L375`'s own already-scoped `switch_continue_
   {geom,tessc,tesse}` `DiamondFlattener` assertion (identical symptom),
   but not confirmed via a matching `gdb bt` this session. Per explicit
   user instruction that `L375` needs its own dedicated design session,
   deliberately left open and untouched.
8. **A fourth, independent crash family found and fixed while testing the
   above — `L384`.** While adding regression coverage for `L381`,
   `DescriptorSet::Layout`'s own dangling-pointer bug was found and fixed:
   `DescriptorSet` retained a raw `const DescriptorSetLayout *` aliasing
   the original, externally-owned `VkDescriptorSetLayout`, which the
   Vulkan spec permits an application to destroy immediately after
   allocating descriptor sets from it -- doing so left `getLayout()` and
   `write()`'s own `Layout.find(Binding)` lookup dereferencing a dangling
   pointer. This is the genuine root cause of `dEQP-VK.pipeline.*.
   multisample.misc.{basic,clear_attachments}.*.ds_resolve_sample_zero.*`'s
   own `SIGSEGV` -- a 46-case crash family this session discovered by
   exploring beyond the two cases already tracked under item 6 above.
   **Its case count (46) coincidentally matches item 5's own "46 `pipeline`
   `SIGSEGV`" figure, but no case-name-level comparison was done this
   session to confirm or refute any overlap between the two** -- a future
   session should run a fresh, full `pipeline.*` crash sweep naming every
   surviving case to settle this. Fixing this by-value conversion then
   caused a further, separate regression in `CommandBuffer::
   getOrCreatePushDescriptorSet`'s own pointer-identity comparison
   (`L383`, also fixed this session, caught via the full unit test suite
   before committing). **Verified:** new regression test `DescriptorTest.
   UsableAfterDescriptorSetLayoutDestroyed` passes; a full, fresh
   `ds_resolve_sample_zero.*` rerun (768 cases) completes with zero
   crashes (120 ordinary Fail -- a separate, pre-existing correctness gap,
   out of scope here -- 648 NotSupported); full `FeMeVulkanTests` suite:
   797/797 Passed. No `Vulkan14FeatureInventory.md`/
   `VulkanExtensionInventory.md` change needed -- internal correctness fix
   only.
9. **Pipeline SIGSEGV triage fully closed — `L385`.** The original 53-case
   crash/`Unrun` case list from the baseline full-CTS run was never
   actually missing: it sits intact in
   `feme-20261005-full-1170553-complete/report.txt`'s own `Unrun case
   names:` section and its paired `abnormal/*.log`/`.qpa` artifacts, just
   never previously extracted despite repeated notes that "no case names
   are recorded anywhere." Re-ran all 53 case names verbatim against the
   current build via `deqp-vk -n <comma-joined-list>` (`-n`/`--deqp-case`
   accepts comma-separated exact names). **Result:** 3 Pass (the `api` +
   2 `compatible_render_pass` cases, confirming `L381`/`L382`), 47 ordinary
   `Fail (Incorrect multisampled rendering for color attachment N)` with
   **zero crashes** (confirming `L384`'s fix) -- and these 47 are not a
   newly-introduced or unexplained gap: the failing attachment index is
   consistently an integer-format attachment, confirming they are
   `L379(a)`'s own already-scoped, not-yet-fixed per-attachment color
   `resolveMode` plumbing gap, simply unmasked now that the crash hiding
   it is gone. The remaining 3 cases
   (`graphicsfuzz.nested-for-loops-switch-fallthrough` +
   `spirv_assembly.switch_continue_{geom,tesse}`) still crash, all
   confirmed the same `L375` bug via a matching `gdb bt` (`L383(a)`).
   **Zero `pipeline`-group `SIGSEGV`s remain unexplained** -- item 6's
   "44 `pipeline` `SIGSEGV`s remain open" and item 8's "coincidentally
   matches... no case-name-level comparison done" are both superseded:
   the `ds_resolve_sample_zero` family is confirmed the exact same bucket
   item 5/6 tracked (47 cases, not 46 -- 44 were the true SIGSEGVs, the
   other 3 were already-classified ordinary Fails in the original run),
   and it is now fully fixed from a crash perspective. The only process
   crashes left anywhere in the original 53-case baseline are the 3 that
   fold into `L375`. No FeMe source code change -- a documentation/triage-
   closure finding only, since every underlying crash was already fixed
   by `L381`/`L382`/`L384`.
10. **`L379(a)`'s per-attachment color `resolveMode` gap: fixed in two
    parts, 48 of 120 `ds_resolve_sample_zero*` cases now Pass.** Traced why
    the previously-committed dynamic-rendering `ColorResolveModes`
    plumbing fix (a prior session) did not flip any of these 120 cases:
    the specific failing cases (`fast_linked_library`/`monolithic`
    construction types) use a *classic* `VkRenderPass`, not dynamic
    rendering -- `RenderPassWrapper::m_isDynamicRendering` is keyed on
    `isConstructionTypeShaderObject`, not the CTS test's own misleadingly-
    named `dynamicRendering` parameter. A classic render pass has no
    `resolveMode` field anywhere to plumb, yet the spec still mandates
    integer color formats always resolve via sample 0 (fixed-function,
    not a free choice); fixed by forcing
    `View.ColorResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT` in
    `buildRenderTargetBinding`'s color-attachment loop (`CommandBuffer.cpp`)
    whenever `feme::cpu::isIntegerColorAttachmentFormat` is true. This
    alone did not change the CTS result: debug tracing confirmed it
    computed and threaded the correct mode all the way to `Executor.cpp`'s
    per-draw resolve, yet the final image still showed the box-filter
    average. **Second, independent bug:** `resolveSubpassColorAttachments`
    (`ImageOps.cpp`), a *second* resolve pass that runs once more at
    `vkCmdEndRenderPass`/`vkCmdNextSubpass` (to resolve any samples a
    subpass's own draws didn't already cover), unconditionally
    box-filter-averaged every color attachment, ignoring
    `RenderTargetView::ColorResolveMode` entirely -- silently re-clobbering
    the correct per-draw `SAMPLE_ZERO_BIT` result with an averaged one,
    since this pass always runs last. Fixed by branching this pass on
    `ColorResolveMode` exactly like the per-draw resolve already does.
    Added `DrawTest.cpp`'s
    `ClassicRenderPassResolvesIntegerColorToSampleZeroNotAverage` (a real
    Vulkan-API-level regression test, since `buildRenderTargetBinding` has
    internal linkage; two pipelines with disjoint static `pSampleMask`s
    create genuine per-sample divergence without `gl_SampleID`-branching
    shaders). **Verified:** of the 120 originally-targeted
    `ds_resolve_sample_zero*` cases, **48 now Pass** (was 0) -- every
    `fast_linked_library`/`monolithic` case under the `misc.basic`/
    `misc.clear_attachments` subgroups. The remaining 72
    (`dynamic_rendering`/`input_attachments` subgroups, each paired with a
    `D16_UNORM` depth format) fail with `VK_ERROR_INITIALIZATION_FAILED`
    at pipeline-creation time, not a resolve-pixel mismatch at all -- a
    distinct, unrelated bug, scoped separately as roadmap `L379(c)`, not
    investigated this session. `FeMeVulkanTests`: 798/798 Passed (+1 new
    unit test). `ninja check-feme`: 3,598/3,601 Passed, 3 Unsupported, 0
    Failed, 0 regressions. No `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md` change needed -- internal correctness
    fix only.

11. **`L379(c)` fixed: the `dynamic_rendering` half of the remaining 72
    `ds_resolve_sample_zero*` failures, 48 more cases now Pass.**
    `FEME_VULKAN_LOG_CREATION_ERRORS=1` on the single representative
    `dynamic_rendering.basic` case surfaced the real swallowed error:
    `color attachment 3 names a format this driver cannot render into`.
    Root cause: `GraphicsPipeline.cpp`'s dynamic-rendering color-attachment
    loop (building `PipelineRenderTargets` from a chained
    `VkPipelineRenderingCreateInfo`) treated any `VK_FORMAT_UNDEFINED`
    slot in `pColorAttachmentFormats` as an unrecognized/unsupported
    format and rejected the whole pipeline -- but CTS always declares a
    fixed 4-element `pColorAttachmentFormats` array, padding unused
    trailing slots with `VK_FORMAT_UNDEFINED` as a legal "unused slot"
    marker (confirmed via CTS source,
    `vktPipelineMultisampledRenderToSingleSampledTests.cpp`), exactly
    mirroring `VK_ATTACHMENT_UNUSED` for a classic `VkRenderPass` (which
    the classic-render-pass loop a few dozen lines earlier in the same
    function already handles correctly). Fixed by special-casing
    `VK_FORMAT_UNDEFINED` the same way: push
    `feme::cpu::ResourceFormat::Unknown` and `continue`, instead of
    erroring. Added `GraphicsPipelineTest.
    AcceptsAnUndefinedTrailingDynamicRenderingColorFormat`, modeled on the
    existing `AcceptsDynamicRenderingFormats` test, with a matching
    4-attachment blend state (the real CTS cases declare one color blend
    attachment state per declared format slot, including unused ones).
    **Build gotcha hit and resolved:** FeMe's Vulkan-layer code links into
    two separate targets, `FeMeVulkanTests` (unit tests) and
    `libfeme_vulkan.so` (the actual ICD `deqp-vk` loads); rebuilding only
    `FeMeVulkanTests` after a source change does not relink the `.so`,
    causing a confusing false-negative CTS re-run earlier in this
    investigation -- resolved by explicitly rebuilding both together.
    **Verified:** of the 72 cases `L379(c)` originally scoped, **48 now
    Pass** (every `dynamic_rendering.basic`/`dynamic_rendering.
    clear_attachments` case) -- re-running the full `ds_resolve_sample_
    zero*` sweep (2,304 cases total, counting all pipeline-construction-
    type variants) now shows 96 Pass (was 48), 24 Fail (was 72), 2,184 Not
    Supported (`VK_EXT_shader_object` unavailable, unrelated/expected).
    The remaining 24 are *not* this bug: they are all `input_attachments.
    render_to` cases, which fail with a different, unrelated error (`JIT
    session error: Symbols not found: [ spirv_var_N ]`, an unresolved
    stage-IO global reference specific to `subpassLoad`-based input
    attachments) -- scoped separately as roadmap `L386`, not investigated
    beyond the single reproducer's own JIT error this session.
    `FeMeVulkanTests`: 799/799 Passed (+1 new unit test, fixture-corrected
    after an initial self-inflicted blend-attachment-count mismatch in the
    test itself, not a product bug). No `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md` change needed -- internal correctness
    fix only, no feature/extension-surface change.

12. **`primary_cmd_buff.suballocation.*` low-priority triage (roadmap
    `L387`): single root cause confirmed, not fixed.** Per the handoff's
    own suggestion, ran the full sub-family (5,935 cases -- the handoff's
    "993 cases" estimate was stale) through one sweep rather than
    assuming a large triage effort was needed: 3,894 Pass / 152 Fail /
    1,889 NotSupported. All 152 failures share one error signature,
    confirmed on four independently-sampled cases:
    `vkCreateGraphicsPipelines: fragment stage has no integer output of
    1-4 components at location N (SV_TargetN)`. Every failing case uses a
    multi-subpass render pass (subpass 0 writes an integer-format color
    attachment, subpass 1 reads it back as an input attachment and writes
    a second `R8G8B8A8_UNORM` attachment) wrapped as dynamic rendering --
    a different shape from the single-attachment case `H29l` already
    fixed this same check for. Hypothesis (not yet confirmed): the
    per-subpass `ColorAttachments` list `validateStageInterfaces`
    consults may be resolving the wrong attachment's format for a
    multi-subpass-derived dynamic-rendering sequence. Not fixed this
    session -- out of scope for the ~30-minute budget this triage item
    was given.
13. **`L387` root-caused and fixed: `primary_cmd_buff.suballocation.*`'s
    remaining 152 failures, now 0.** CTS source reading
    (`vktRenderPassTests.cpp`'s `findColorAttachments`/
    `fillRenderingAttachmentLocationsInfo`) found the real root cause: a
    multi-subpass classic `VkRenderPass` translated to dynamic rendering
    gives every subpass's own pipeline the *same combined*, cross-subpass
    `pColorAttachmentFormats` list (not a per-subpass subset), then
    chains a `VkRenderingAttachmentLocationInfo` onto that pipeline's own
    create info remapping each attachment index to the fragment-shader
    output location that subpass's own shader actually writes for it
    (`VK_ATTACHMENT_UNUSED` meaning "no output expected here at all").
    `GraphicsPipeline.cpp` had no code anywhere that looked for this
    struct at pipeline-creation time -- only the already-handled dynamic
    `vkCmdSetRenderingAttachmentLocations` command form (`L379(b)`,
    `CommandBuffer.cpp`) -- so `validateStageInterfaces` silently assumed
    attachment index `I` always equals fragment-output location `I`,
    misattributing one subpass's real (float) output to the *other*
    subpass's integer attachment and rejecting pipeline creation with
    "fragment stage has no integer output ... at location N". Fixed by
    adding `findRenderingAttachmentLocationInfo` (mirroring the existing
    `findRenderingCreateInfo` pNext-search pattern) and threading its
    resolved remap through `compileAndValidateStages`/
    `validateStageInterfaces`'s per-attachment fragment-output check,
    falling back to identity when no such struct is chained (preserving
    every other, non-remapped pipeline's existing behavior). Added
    `GraphicsPipelineTest.
    AcceptsRemappedColorAttachmentLocationsFromPipelineCreateInfo`,
    confirmed to fail with the exact real error before this fix.
    **Verified:** the full `primary_cmd_buff.suballocation.*` sub-family
    (5,935 cases) now shows **4,046 Pass / 0 Fail / 1,889 NotSupported**
    (was 3,894/152/1,889) -- every one of the 152 originally-scoped
    failures fixed. A full, from-scratch re-run of the wider
    `renderpasses.dynamic_rendering.*` group (28,898 cases) shows 11,317
    Pass / 1,106 Fail / 16,475 NotSupported, with no regressions
    (unrelated families, e.g. `L386`'s own `input_attachments.render_to`
    `spirv_var_N` JIT-symbol failures, still open and untouched by this
    fix). `FeMeVulkanTests`: 800/800 Passed (+1 new unit test). `ninja
    check-feme`: 3,600/3,603 Passed, 3 Unsupported, 0 Failed, 0
    regressions. No `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md` change needed -- internal correctness
    fix only, no feature/extension-surface change.
14. **`L386` root-caused (previous guess was wrong), not fixed -- needs a
    dedicated design session.** `FEME_DUMP_IR_PRECANON`/
    `FEME_DUMP_IR_POSTCANON` on the single reproducer showed the
    unresolved `spirv_var_25` is a **push-constant block** (address space
    13), not a stage-IO variable as a prior session guessed -- still
    present, unrewritten, in the postcanon dump, meaning
    `SPIRVPushConstantLoweringPass` never touched this function at all.
    Root cause: the `ds_resolve_sample_zero` fragment shader's branchy
    resolve logic computes two different compile-time byte offsets (112
    and 116) into the push-constant block along two branches, each via
    its own `getelementptr`, then merges the two pointers with a `phi`
    before a single shared `load`. `SPIRVPushConstantLowering.cpp`'s
    `visitPushConstantUsers` walk only recognizes a GEP's user as a `load`
    or a further `getelementptr` -- a `phi` user is unsupported by its own
    documented scope, so the pass rejects the *whole* function's
    push-constant access, leaving the global forever unresolved at
    JIT-link time. This is a materially deeper gap than a quick fix: the
    rewrite is keyed per-terminal-`Load` with exactly one compile-time
    `BaseOffset`, which cannot express "whichever offset the live `phi`
    incoming edge used" -- a correct general fix needs to sink the
    existing per-load bounds-check/load logic to each `phi` edge's own
    GEP and build a parallel value-level `phi`, generalizing cleanly to
    nested `phi`s too. Scoped in `Roadmap.md` as needing its own dedicated
    design session, the same category of work as `L375` -- not attempted
    this session. No FeMe source code change this item -- investigation/
    triage only.
15. **`L386` fixed: push-constant `phi`-of-GEPs now supported.** Extended
    `visitPushConstantUsers` to recognize a `phi` merging push-constant
    pointers with different compile-time byte offsets, and
    `lowerSPIRVPushConstantAccess` to sink a bounds-checked load to each
    of the `phi`'s own incoming edges, merging the resulting *values*
    with a new `phi` that replaces the original pointer `phi` (the
    original shared `load` becomes dead and is removed). **Mid-session
    self-correction, worth recording as a methodology note:** the first
    version of this fix cached a `BasicBlock *` per edge at match time;
    this went stale whenever an unrelated, earlier-processed plain
    push-constant load happened to share (and split) the same original
    predecessor block as a later `phi` edge, producing genuinely invalid
    IR (a `phi` listing a `BasicBlock *` no longer among its real
    predecessors). This was not caught by the new unit test (too simple a
    CFG to trigger the overlap) -- it surfaced only as an assertion crash
    inside `(anonymous namespace)::DiamondFlattener::flatten` against the
    real CTS reproducer, initially (and wrongly) suspected to be `L375`
    purely because it crashed in the same function with superficially
    similar symptoms, even though the assertion text was already
    different. Running the LLVM verifier (`opt -passes=verify`) directly
    on the IR dumped immediately before `LinearizePass`
    (`FEME_DUMP_IR_PRELINEARIZE`) immediately found the real,
    independent bug: `PHI node entries do not match predecessors!` on
    this fix's own new `phi`. **Lesson for future sessions:** when a new
    crash resembles a documented/deferred bug, verify independently via
    direct tooling (the verifier, a matching `gdb bt`) before concluding
    it is the same bug -- pattern-matching against written descriptions
    alone is not sufficient. Fixed by storing the incoming-value index
    instead of a `BasicBlock *`, and re-reading
    `Phi->getIncomingBlock(Edge.Index)` fresh immediately before each
    edge's own rewrite, relying on the (confirmed) LLVM invariant that
    `BasicBlock::splitBasicBlock` always keeps every `phi`'s
    incoming-block entries pointed at their real current predecessors.
    New lit test (`spirv-push-constant-lowering-phi.ll`) covers an
    incomplete edge, a `phi` feeding a further GEP, the basic
    differently-offset-GEPs case, and a dedicated regression test for the
    stale-predecessor bug (a `phi` edge sharing its original predecessor
    block with an unrelated plain load). **Verified:** the real
    reproducer
    (`dEQP-VK.pipeline.fast_linked_library.multisample.misc.input_attachments.render_to.r8g8b8a8_unorm_r16g16b16a16_sfloat_r16g16b16a16_sint_d16_unorm.2x.ds_resolve_sample_zero.sub_framebuffer`)
    now passes end-to-end (was a crash); a full rerun of the
    `ds_resolve_sample_zero*` family (2,304 cases) shows 120 Pass, **0
    Fail**, 2,184 NotSupported (unrelated `VK_EXT_shader_object` gap) --
    every one of the 24 cases this item originally scoped is fixed.
    `ninja check-feme`: 3,601/3,604 Passed (+1 new lit test), 3
    Unsupported, 0 Failed, 0 regressions. No
    `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` change
    needed -- internal compiler-pass correctness fix only, no
    feature/extension-surface change.

16. **`L388` fixed: library-linked `attachment_allocation.roll.*` -- 278
    failures, now 0.** The `renderpasses.dynamic_rendering.*` triage
    sweep's second-largest remaining family, same error class as `L387`
    (`vkCreateGraphicsPipelines: fragment stage has no floating-point/
    integer output of 1-4 components at location N (SV_TargetN)`), but
    for `graphics_pipeline_library`-linked pipelines rather than
    monolithic/`primary_cmd_buff` ones. Root cause: `L387`'s own fix reads
    `findRenderingAttachmentLocationInfo(CreateInfo.pNext)` directly off
    its `CreateInfo` argument, but CTS's own `GraphicsPipelineWrapper`
    chains `VkRenderingAttachmentLocationInfo` onto the fragment-output-
    interface *library part's* own create info (via
    `setupFragmentOutputState`), never onto the top-level link call's --
    exactly `L381`'s own `PrecomputedRenderTargets` bug shape, but for the
    attachment-location remap instead of the render-pass handle. Fixed by
    extending `GraphicsPipelineLibraryState` with a new
    `ColorAttachmentLocations` field (captured in
    `captureGraphicsPipelineLibraryState`, folded in
    `foldLinkedLibraryState`, both gated on
    `FRAGMENT_OUTPUT_INTERFACE_BIT` mirroring `RenderTargets`'s own
    gating), threaded through `synthesizeLinkedGraphicsPipelineCreateInfo`
    as a new `PrecomputedColorAttachmentLocations` out-parameter, and
    consumed by `compileGraphicsPipeline` in place of (when supplied)
    re-searching the synthesized create info's own `pNext`. Added
    `GraphicsPipelineTest.
    LinksRemappedColorAttachmentLocationsFromLibraryPart`, confirmed to
    fail with `VK_ERROR_INITIALIZATION_FAILED` before this fix. **Verified:**
    the full `renderpasses.dynamic_rendering.graphics_pipeline_library.*`
    family (6,155 cases) shows all 200 `attachment_allocation.roll.*`
    cases in this sweep now Pass (0 Fail); the only 4 remaining failures
    are a pre-existing, unrelated `local_read` sub-family issue (2 of
    which are in fact a genuine `SIGSEGV`, not an ordinary Fail -- scoped
    as new roadmap item `L389`, not yet triaged or fixed, out of this
    fix's scope). A
    full re-run of the wider `renderpasses.dynamic_rendering.*` group
    (28,898 cases) confirms no regressions: 11,593 Pass / 828 Fail /
    16,475 NotSupported (was 11,317/1,106/16,475 -- Fail down by exactly
    278, NotSupported unchanged). `FeMeVulkanTests`: 801/801 Passed (+1
    new unit test). `ninja check-feme`: 3,602/3,605 Passed, 3 Unsupported,
    0 Failed, 0 regressions. No `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md` change needed -- internal correctness
    fix only, no feature/extension-surface change.

Important confirmed improvements include zero failures or abnormal outcomes
across all 48,705 `subgroups` cases; `api` falling to 34 failures; `image` to
14; `glsl` to 15; `texture` to 2; and `tessellation` to 30.

## Complete group accounting

| Group | Total | Pass | Fail | NotSupported | QW | IE | DeviceLost | Crash | Unrun | Fail delta |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `api` | 267,504 | 115,798 | 34 | 151,671 | 0 | 0 | 0 | 1 | 0 | -15,511 |
| `binding_model` | 150,289 | 87,669 | 3 | 62,617 | 0 | 0 | 0 | 0 | 0 | +0 |
| `clipping` | 308 | 298 | 0 | 10 | 0 | 0 | 0 | 0 | 0 | +0 |
| `compute` | 61,460 | 686 | 0 | 60,774 | 0 | 0 | 0 | 0 | 0 | +0 |
| `conditional_rendering` | 1,030 | 0 | 0 | 1,030 | 0 | 0 | 0 | 0 | 0 | +0 |
| `cooperative_vector` | 53,562 | 0 | 0 | 53,562 | 0 | 0 | 0 | 0 | 0 | +0 |
| `data_graph` | 12,632 | 0 | 0 | 12,632 | 0 | 0 | 0 | 0 | 0 | +0 |
| `depth` | 8 | 0 | 0 | 8 | 0 | 0 | 0 | 0 | 0 | +0 |
| `descriptor_indexing` | 115 | 0 | 0 | 115 | 0 | 0 | 0 | 0 | 0 | +0 |
| `device_group` | 21 | 9 | 0 | 12 | 0 | 0 | 0 | 0 | 0 | -7 |
| `dgc` | 4,734 | 0 | 0 | 4,734 | 0 | 0 | 0 | 0 | 0 | +0 |
| `draw` | 29,451 | 3,258 | 0 | 26,193 | 0 | 0 | 0 | 0 | 0 | -2 |
| `drm_format_modifiers` | 1,572 | 0 | 0 | 1,572 | 0 | 0 | 0 | 0 | 0 | +0 |
| `dynamic_state` | 671 | 251 | 0 | 420 | 0 | 0 | 0 | 0 | 0 | +0 |
| `fragment_operations` | 151 | 118 | 0 | 30 | 3 | 0 | 0 | 0 | 0 | -11 |
| `fragment_shader_interlock` | 576 | 0 | 0 | 576 | 0 | 0 | 0 | 0 | 0 | +0 |
| `fragment_shading_barycentric` | 20,991 | 0 | 0 | 20,991 | 0 | 0 | 0 | 0 | 0 | +0 |
| `fragment_shading_rate` | 110,603 | 0 | 0 | 110,603 | 0 | 0 | 0 | 0 | 0 | +0 |
| `geometry` | 200 | 190 | 1 | 9 | 0 | 0 | 0 | 0 | 0 | -40 |
| `glsl` | 28,420 | 19,442 | 15 | 8,963 | 0 | 0 | 0 | 0 | 0 | -2,918 |
| `graphicsfuzz` | 757 | 666 | 82 | 8 | 0 | 0 | 0 | 1 | 0 | +11 |
| `image` | 143,086 | 29,901 | 14 | 113,171 | 0 | 0 | 0 | 0 | 0 | -6,927 |
| `image_processing` | 1,211 | 0 | 0 | 1,211 | 0 | 0 | 0 | 0 | 0 | +0 |
| `imageless_framebuffer` | 12 | 4 | 0 | 8 | 0 | 0 | 0 | 0 | 0 | +0 |
| `info` | 23 | 19 | 1 | 3 | 0 | 0 | 0 | 0 | 0 | -1 |
| `memory` | 6,364 | 4,986 | 0 | 1,378 | 0 | 0 | 0 | 0 | 0 | -65 |
| `memory_model` | 18,530 | 161 | 52 | 18,317 | 0 | 0 | 0 | 0 | 0 | -114 |
| `mesh_shader` | 28,044 | 468 | 69 | 27,507 | 0 | 0 | 0 | 0 | 0 | -21 |
| `multiview` | 838 | 643 | 0 | 195 | 0 | 0 | 0 | 0 | 0 | +0 |
| `pipeline` | 1,172,229 | 259,017 | 39,644 | 873,467 | 43 | 0 | 12 | 46 | 0 | +35,934 |
| `postmortem` | 24 | 0 | 0 | 24 | 0 | 0 | 0 | 0 | 0 | +0 |
| `protected_memory` | 6,000 | 0 | 0 | 6,000 | 0 | 0 | 0 | 0 | 0 | +0 |
| `query_pool` | 19,276 | 16,587 | 0 | 2,689 | 0 | 0 | 0 | 0 | 0 | -328 |
| `rasterization` | 15,019 | 482 | 2 | 14,535 | 0 | 0 | 0 | 0 | 0 | -96 |
| `ray_query` | 49,311 | 0 | 0 | 49,311 | 0 | 0 | 0 | 0 | 0 | +0 |
| `ray_tracing_pipeline` | 22,659 | 0 | 0 | 22,659 | 0 | 0 | 0 | 0 | 0 | +0 |
| `reconvergence` | 6,253 | 0 | 0 | 6,253 | 0 | 0 | 0 | 0 | 0 | +0 |
| `renderpasses` | 81,188 | 30,399 | 4,845 | 45,616 | 0 | 0 | 328 | 0 | 0 | -3,009 |
| `robustness` | 98,776 | 685 | 0 | 98,083 | 0 | 0 | 8 | 0 | 0 | -8 |
| `shader_object` | 243,853 | 6 | 0 | 243,847 | 0 | 0 | 0 | 0 | 0 | +0 |
| `sparse_resources` | 19,402 | 0 | 0 | 19,402 | 0 | 0 | 0 | 0 | 0 | +0 |
| `spirv_assembly` | 68,734 | 10,081 | 458 | 58,193 | 0 | 0 | 0 | 2 | 0 | -537 |
| `ssbo` | 12,225 | 3,242 | 0 | 8,983 | 0 | 0 | 0 | 0 | 0 | +0 |
| `subgroups` | 48,705 | 888 | 0 | 47,817 | 0 | 0 | 0 | 0 | 0 | +0 |
| `synchronization` | 64,872 | 18,420 | 0 | 46,452 | 0 | 0 | 0 | 0 | 0 | -3,128 |
| `synchronization2` | 81,617 | 24,631 | 0 | 56,986 | 0 | 0 | 0 | 0 | 0 | -3,407 |
| `tensor` | 2,811 | 0 | 0 | 2,811 | 0 | 0 | 0 | 0 | 0 | +0 |
| `tessellation` | 1,114 | 646 | 30 | 438 | 0 | 0 | 0 | 0 | 0 | -366 |
| `texture` | 25,669 | 9,808 | 2 | 15,859 | 0 | 0 | 0 | 0 | 0 | -451 |
| `transform_feedback` | 133,719 | 4,814 | 1,918 | 126,987 | 0 | 0 | 0 | 0 | 0 | -135 |
| `ubo` | 13,240 | 5,687 | 0 | 7,553 | 0 | 0 | 0 | 0 | 0 | +0 |
| `video` | 9,471 | 0 | 0 | 9,471 | 0 | 0 | 0 | 0 | 0 | +0 |
| `wsi` | 36,880 | 51 | 4 | 36,825 | 0 | 0 | 0 | 0 | 0 | +4 |
| `ycbcr` | 68,159 | 16 | 0 | 68,143 | 0 | 0 | 0 | 0 | 0 | +0 |

## Conformance assessment

This is not a conformant result. There are 47,174 ordinary failures, 348
`DeviceLost` results, 50 process crashes, and 2,546,724 `NotSupported`
outcomes. The latter include mandatory core capability gaps as well as
optional extensions, so eliminating ordinary failures alone would not
establish conformance.

The roadmap's C/H/J/K/L rows retain implementation ownership. Its 2026-10-05
full-run re-triage prioritizes the shared `feme-cpu-linearize` dominance bug,
the sampler long tail, render-pass device loss, and the remaining stability
set while preserving implementation rows whose acceptance criteria are not
decidable from CTS results alone.
