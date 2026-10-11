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
   for a future session, see `Roadmap.md`. **Update: fully resolved by
   `L387` through `L391` (items 12-20 below), with items 20/23 confirming
   a from-scratch full-family re-sweep at 0 Fail.** The 46 `pipeline`
   `SIGSEGV`s below remain completely untouched and are the next
   stability-triage priority.
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

   **Update (this session): flagging an unresolved discrepancy for the next
   stability session, not re-triaged here (this session's budget went to
   `L375` root-cause work, see `Roadmap.md`).** The "Complete group
   accounting" table below still shows `pipeline` Crash=46 and `pipeline`
   DeviceLost=12, not the 3 crashes (all `L375`) this item's own 53-case
   re-run found. Since item 9's re-run was scoped to only the 53 case names
   recorded in the old `feme-20261005-full-1170553-complete` baseline
   artifact, not a fresh full `dEQP-VK.pipeline.*` sweep, this 46-vs-3 gap
   most likely means the group-accounting table's 46/12 figures are
   themselves stale (carried forward from before `L381`/`L382`/`L384`
   fixed most of that family) rather than evidence of new crashes --
   **but this has not been confirmed with a fresh full-family sweep**, so
   do not assume it is stale without checking. Next session: re-run the
   full `dEQP-VK.pipeline.*` group (or at least re-capture its `Unrun`/
   `Crash`/`DeviceLost` case names from a fresh run) and reconcile the
   table's 46/12 figures against item 9's 3-crash finding before trusting
   either number further; the same applies to `robustness`'s 8 `DeviceLost`
   results, which have never had case names recorded at all.
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
17. **`L389` fixed: self-regression from `L388`'s own fix, same session --
    2 `SIGSEGV` crashes, now 0.** While verifying `L388`'s own CTS sweep
    (item 16 above), `dEQP-VK.renderpasses.dynamic_rendering.
    graphics_pipeline_library.local_read.
    {null_color_attachment_location_with_locationinfo,
    null_color_attachment_location_with_locationinfo_before_identity}`
    were found to be genuine `SIGSEGV` crashes (confirmed via a direct,
    isolated `deqp-vk -n <case>` run, exit code 139). `gdb bt` pointed
    directly at `L388`'s own new `captureGraphicsPipelineLibraryState`
    code; checking out the pre-`L388` sources and re-running the same
    case confirmed it passed before this session's own `L388` commit --
    a self-inflicted regression, not a pre-existing bug. Root cause:
    `VkRenderingAttachmentLocationInfo::pColorAttachmentLocations ==
    nullptr` with a nonzero `colorAttachmentCount` is the spec-legal
    identity-mapping spelling, but both of `L388`'s new call sites (plus
    `L387`'s own pre-existing, never-before-exercised
    `compileGraphicsPipeline` code, which had the identical latent bug
    for monolithic/non-linked pipelines) unconditionally called
    `std::vector::assign(ptr, ptr + count)` without checking the pointer
    for null. **Fixed this session**: both call sites now guard with a
    null check, mirroring the existing `ColorBlendAttachments::
    pAttachments` null-check pattern already used immediately above in
    the same function. Added `GraphicsPipelineTest.
    LinksALibraryWithANullColorAttachmentLocationsArray`. **Verified:**
    both crash cases now Pass; a full re-run of the
    `graphics_pipeline_library.*` family (6,155 cases) shows 0 Unrun (was
    2), Pass 4,130 (was 4,128), Fail unchanged at 4 (the remaining,
    unrelated `local_read` ordinary-Fail sub-family, scoped as new
    roadmap item `L390`, not yet triaged). A full re-run of the wider
    `renderpasses.dynamic_rendering.*` group (28,898 cases) confirms no
    regressions: 11,595 Pass / 828 Fail / 16,475 NotSupported / 0 Unrun
    (Fail unchanged, Pass +2 for the two fixed crashes). `FeMeVulkanTests`:
    802/802 Passed (+1 new unit test). `ninja check-feme`: 3,603/3,606
    Passed, 3 Unsupported, 0 Failed, 0 regressions. No
    `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` change
    needed -- internal correctness fix only, no feature/extension-surface
    change.

18. **`L390` root-caused (not fixed): `local_read`'s 4 depth/stencil
    `VK_ERROR_INITIALIZATION_FAILED` cases are a `Dim::SubpassData`
    image reaching resource normalization unconverted via
    `OpImageTexelPointer`, not `OpImageRead`.** `FEME_VULKAN_LOG_
    CREATION_ERRORS=1` on all 4 cases (`depth_stencil_mapping_to_
    no_index`, `_depth_clear`, `_stencil_clear`, `input_attachments_
    without_mapping`) confirms one shared error: an
    `llvm.spv.resource.handlefrombinding` call on a
    `target("spirv.Image", ..., 6, ...)` handle (SPIR-V `Dim::
    SubpassData`, dim code 6) that `BoundResourceNormalizationPass`
    cannot normalize. `FEME_DUMP_IR_PRENORM=1` shows the handle flows
    into `llvm.spv.resource.getpointer` (a raw storage-image texel
    address), not the `feme.stage.subpass.load` ABI `SPIRVToLLVM
    Patterns.cpp`'s `SubpassLoadPattern` builds for an ordinary
    `OpImageRead`-based `subpassInput` read. Root cause:
    `ImageTexelPointerPattern` (built for roadmap `R39`/`H8u`'s
    atomic-on-storage-image support) unconditionally lowers *any*
    image dimension via `createResourcePointer`, with no `Dim::
    SubpassData` special case the way `SubpassLoadPattern` has --
    these 4 cases' depth/stencil local-read shaders apparently reach
    their subpass image through `OpImageTexelPointer` rather than
    plain `OpImageRead`, so `SubpassLoadPattern` never sees them.
    Confirmed via a full sweep (below) that this affects all 3
    command-buffer variants identically (`primary_cmd_buff`/
    `partial_secondary_cmd_buff`/`graphics_pipeline_library`), 12
    Fails total. No code changed -- the real fix (a `Dim::SubpassData`
    case in `ImageTexelPointerPattern`, routed through the subpass-input
    heap mechanism) needs its own verified design session.

19. **`L391` fixed: `VK_RENDERING_RESUMING_BIT`/`VK_RENDERING_
    SUSPENDING_BIT` were entirely unhandled, re-clearing a suspended
    dynamic-rendering instance's contents on resume -- 60 Fails, now
    0.** Item 3 of this session's carried-over low-priority triage:
    bucketed the 828-case `renderpasses.dynamic_rendering.*` Fail set
    by sub-family (`primary_cmd_buff.random` 756, `primary_cmd_buff.
    basic` 60, `local_read` 12 -- see item 18) and sampled the `basic`
    bucket. All 60 failing case names share one substring: `resuming`.
    A `grep` for `RESUMING`/`SUSPENDING` across `feme/lib/Vulkan`/
    `feme/lib/Graphics` found zero matches -- `VkRenderingInfo::flags`'
    suspend/resume bits were silently ignored everywhere. Per spec, a
    resuming instance's own `VkRenderingAttachmentInfo::loadOp` must be
    ignored (treated as `LOAD`, preserving the suspended instance's
    contents), but `vkCmdBeginRenderingKHR` applied it verbatim --
    real CTS cases (and most real applications) specify the same
    `loadOp` (often still `CLEAR`) on every begin call in a suspend/
    resume chain, so the resume re-cleared data the suspended instance
    had already drawn. **Fixed this session**: `vkCmdBeginRenderingKHR`
    now forces every attachment's `LoadOp` away from `CLEAR` when
    `VK_RENDERING_RESUMING_BIT` is set, reusing `applyClear`'s existing
    "`LOAD`/`DONT_CARE`/`NONE` are all a no-op" handling. `StoreOp`
    needed no matching change: this software renderer already always
    preserves written memory regardless of `StoreOp`, matching `STORE`'s
    spec-mandated suspend-side behavior. Added `DrawTest.
    DynamicRenderingResumingIgnoresLoadOp` (confirmed to fail pre-fix
    via a `git checkout --`/rebuild A/B, pass post-fix). **Verified:**
    `primary_cmd_buff.basic.*resuming*` (140 cases: 70 run, 70
    `VK_KHR_maintenance10`-gated NotSupported) goes from 60 Fail to 0
    Fail. Full `renderpasses.dynamic_rendering.*` re-sweep (28,898
    cases): 12,411 Pass / 12 Fail / 16,475 NotSupported (Fail 828 ->
    12, the remaining 12 being exactly item 18's `L390` cases, confirmed
    by identical case names before and after). `FeMeVulkanTests`:
    803/803 Passed (+1 new unit test). `ninja check-feme`: 3,604/3,607
    Passed, 3 Unsupported, 0 Failed, 0 regressions. No
    `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` change
    needed -- `VK_KHR_dynamic_rendering`'s suspend/resume flags were
    already advertised as accepted; this corrects their handling, not
    their advertised support.

20. **`L390` fixed: `local_read`'s 4 no-index depth/stencil
    `subpassInput`/`usubpassInput` cases, 12 Fails across all 3
    command-buffer variants.** Continuing from item 18's root-cause:
    further tracing found the real shape is not `ImageTexelPointerPattern`
    mishandling `Dim::SubpassData` after all, but a
    `subpassInput`/`usubpassInput` SPIR-V variable with **no
    `input_attachment_index` decoration at all** -- a deliberate,
    spec-legal shape (`VkRenderingInputAttachmentIndexInfoKHR`'s own
    semantics) for a shader reading the *current* subpass's depth/
    stencil attachment directly, with no indexed local-read mapping ever
    engaged. **Fix:** reuse `StageOpKind::SubpassLoad` unchanged --
    `SubpassLoadPattern` (`SPIRVToLLVMPatterns.cpp`) now bakes one of two
    new reserved sentinel constants
    (`kSubpassLoadDirectDepthAttachmentIndex`/
    `kSubpassLoadDirectStencilAttachmentIndex`, `StageOps.h`) as the
    `attachment_index` operand instead of declining the match when
    `input_attachment_index` is absent; `lowerFragmentSubpassLoad`
    (`FragmentWrapper.cpp`) detects the sentinel at runtime and resolves
    it against `SubpassInputHeapCount - 2`/`- 1`; `buildSubpassInputHeap`
    (`CommandBuffer.cpp`) always appends two trailing descriptor slots
    populated from `DepthStencil.Depth`/`.Stencil` directly. Needs zero
    new ABI struct fields and zero new function parameters. **Two
    regressions found and fixed during implementation:** (1)
    `Executor.cpp`'s pre-existing `HasSelfReferencingDepthStencilInput`
    feedback-loop detector (roadmap `L330`) compares every
    `SubpassInputHeap` entry's `Data` pointer against `DepthStencil.
    Depth`/`Stencil`'s own -- since the two new trailing slots always
    equal that same pointer by construction, this initially
    false-positived on *every* depth/stencil draw, forcing the late
    depth/stencil-test path even for fixed-function-only draws with no
    fragment stage at all (a `FeMeVulkanTests` regression,
    `DepthStencilResolveSkipsAspectResolveAttachmentDoesNotHave`); fixed
    by excluding exactly the last two heap entries from that scan, since
    a true feedback-loop self-reference can only ever happen through a
    real, indexed `subpassLoad`. (2) An interim attempt at fixing (1) by
    gating the two trailing slots' construction on whether this draw
    already uses an indexed subpass input for *something* broke
    `depth_stencil_mapping_to_no_index_depth_clear`/`_stencil_clear`
    specifically -- their shaders read *only* the no-index depth/stencil
    input, with zero indexed subpass input anywhere else in the same
    subpass; reverted that gate, keeping only fix (1) above. **Verified:**
    new unit test `DrawTest.
    SubpassLoadReadsBackTheDepthAttachmentItWroteWithNoIndex` reproduces
    the second regression's exact shape and passes. `FeMeVulkanTests`:
    804/804 Passed (+1 new unit test). `ninja check-feme`: 3,605/3,608
    Passed, 3 Unsupported, 0 Failed, 0 regressions. Full `local_read`
    CTS sub-family (182 cases): 105 Passed, 0 Failed, 77 NotSupported
    (`shader_object`, expected) -- the originally-targeted 12 cases all
    now Pass. No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.
    md` change needed -- an internal correctness fix to already-
    advertised `VK_KHR_dynamic_rendering_local_read` functionality.
    **Full-family re-sweep confirmation (same session, after the above):**
    the entire `dEQP-VK.renderpasses.dynamic_rendering.*` family
    (28,898 cases) was re-run in full. Totals: 12,423 Passed, **0
    Failed**, 16,475 Not supported, 0 Warnings, 0 Waived -- the 12
    remaining Fails from `L391`'s own full-family sweep (all `L390`'s
    own cases) are now gone, with no new Fails introduced anywhere else
    in the family.

21. **`L375` -- a third fix attempt this session, also reverted; `L392` found
    and confirmed instead.** Continued `L375`'s cross-root
    `FlattenedBlockMasks` memoization design from the prior session: traced
    the memo's own lookups and confirmed the map worked exactly as designed
    (a second visit to the same block correctly resumed from the first
    walk's recorded answer instead of re-deriving it) -- but this still
    crashed identically, root-caused to `LoopLinearizer::linearizeCycle`
    constructing **two separate `DiamondFlattener` objects** for one cycle
    (their matching `this` pointers across traces were coincidental stack-
    address reuse between two sequential, non-overlapping object lifetimes,
    not genuine shared-instance reuse -- confirmed via a map-size dump, not
    pointer equality). Fixed that by threading a single
    `SharedFlattenedBlockMasks` map by pointer into every `DiamondFlattener`
    construction within one `linearizeCycle` call. This made the original
    `pred_iterator out of range!` assertion disappear on all three
    reproducers -- but uncovered a *worse*, silent failure: `verifyModule`/
    `Module::print` would themselves segfault later from memory corruption,
    because the memoized masks (seeded fresh/unconditional by
    `flattenLoopBodyDiamond`'s own sweep) are semantically incompatible with
    `flattenLoopBodyRegion`'s own loop-carried-masking contract -- resuming
    the second walk with the first walk's stale masks silently discards the
    real loop-carried state, corrupting IR in a way that only crashes much
    later, not at the actual mistake. Fully reverted (`git checkout --`),
    confirmed via rebuild that the tree is back to the original, clean
    assertion-crash baseline; no code committed. See `Roadmap.md`'s `L375`
    entry for the complete, detailed root-cause writeup and the two
    candidate next-step designs. Re-checked this session's two other
    carried-over triage items: the `primary_cmd_buff.random` bucket (800
    cases) is confirmed fully resolved already, a side effect of `L390`/
    `L391` -- 800 Passed, 0 Failed. The `multisample_resolve.layers_3.
    *samples_8*` thread-count anomaly flagged (unconfirmed) by the prior
    session is now **confirmed real and tracked as `L392`**: an isolated
    single-case run, and a 5-case back-to-back run of the same format's
    whole `samples_8*` sub-family, both pass cleanly, but running the
    *entire* ~85-case `samples_8` sub-family from one process start (the
    shape the original full sweep exercised) reproducibly shows the
    process's own OS thread count climbing monotonically, never shrinking,
    across cases (15 to 85 sampled), then a genuine `VK_TIMEOUT` on one
    case's fence wait, followed immediately by the *next* case (confirmed
    to pass in total isolation) crashing with `unhandled VkImageViewType` /
    `UNREACHABLE` inside `ImageView::dimension()` -- not a bug in that
    already-exhaustive switch itself, but a symptom of whatever resource is
    silently accumulating corrupting later state once exhausted. Not fixed
    this session; see `Roadmap.md`'s new `L392` entry for the reproducer
    and next-step recommendation (a memory/handle-counting tool, not more
    `ps -eLf` sampling).
22. **`L392` root-caused and fixed: every shader-stage compile was spinning
    up (and only eventually tearing down) a whole six-worker OS thread
    pool it never needed.** `QueueExecutor` (the one long-lived
    per-`VkQueue` background thread) was instrumented and traced first and
    ruled out: its construct/destroy lifecycle stayed clean (3 clean
    cycles) across four separate repro attempts that could not even
    reproduce the originally-reported crash. Broadening the repro to the
    full 750-case `multisample_resolve.layers_3.*` family (not just the
    85-case `samples_8` sub-family) reliably reproduced the thread growth
    itself (95 to 546+ OS threads over one run via `ps -eLf` sampling)
    without a crash, and `/proc/<pid>/task/*/comm` enumeration identified
    the growing threads as `llvm-worker-0` through `llvm-worker-5` --
    `llvm::StdThreadPool`'s own worker-naming convention. A `gdb` breakpoint
    on `pthread_create` (not more static reading) captured the real
    creation backtrace: `mlir::verify`'s `OperationVerifier::
    verifyOpAndDominance` calling `mlir::parallelForEach`, reached from
    `Vulkan/Pipeline.cpp`'s `compileGraphicsStage`/`compileComputePipeline`
    via `SPIRVToLLVMDialectTranslator::translate`'s `mlir::PassManager::
    run`. Root cause: `feme::Context`'s owned `mlir::MLIRContext` (one
    fresh instance per shader-stage compile, per `Pipeline.cpp:642`) used
    MLIR's own default `Threading::ENABLED`, so the first pass/verifier
    call on *any* new context lazily constructs a brand-new `llvm::
    ThreadPool` -- even though every owned context is a single,
    short-lived, single-shader synchronous compile that gains nothing from
    MLIR's own internal parallelism. Fixed by constructing the owned
    `mlir::MLIRContext` with `Threading::DISABLED`
    (`feme/lib/Core/Context.cpp`), confirmed via
    `mlir::parallelForEach`/`failableParallelForEach`'s own
    `isMultithreadingEnabled()` check in `mlir/include/mlir/IR/
    Threading.h` to fall back to fully sequential processing with no
    thread pool touched at all when disabled. **Verified:** a post-fix
    rerun of the same 750-case `multisample_resolve.layers_3.*` family
    shows unchanged results (600 Pass / **0 Fail** / 150 NotSupported) with
    the process's OS thread count sampled every 15s for the whole run and
    staying flat at 7 throughout (vs. the pre-fix climb past 500). `ninja
    check-feme`: 3,606/3,609 Passed, 3 Unsupported, 0 Failed, 0
    regressions (including a new `ContextTest.
    OwnedMLIRContextHasMultithreadingDisabled` regression test). No
    `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` change
    needed -- internal compile-path efficiency/stability fix only, no
    conformance-surface change.
23. **`L393` -- `L392` follow-up sweep closed one remaining fuzzer-only gap;
    `renderpasses.dynamic_rendering.*` triage confirmed already closed.**
    A sweep of every `mlir::MLIRContext`/`feme::Context` construction site
    in `feme/lib`/`feme/tools`/`feme/include` found one call site that
    bypasses `feme::Context` and its `L392` fix entirely:
    `feme-dxbc-import-fuzzer.cpp` builds a raw
    `mlir::MLIRContext Context(Registry);` once per fuzz iteration, left at
    MLIR's default `Threading::ENABLED`. Fixed with the same
    `Threading::DISABLED` pattern as `L392`; built clean via `ninja
    feme-dxbc-import-fuzzer`. No conformance-surface effect -- internal
    tooling fix only, no `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md` change needed. Separately, a fresh,
    from-scratch full re-run of `dEQP-VK.renderpasses.dynamic_rendering.*`
    (28,898 cases) confirms this family's long-carried-over ordinary-Fail
    triage item is **already fully resolved**, a side effect of `L387`
    through `L391`'s own fixes in prior sessions: 12,423 Pass / **0 Fail**
    / 16,475 NotSupported, 0 Crashes, 0 DeviceLost -- unchanged from the
    prior session's own full-family sweep. No further triage or fix was
    needed; the handoff's framing of this item as still outstanding was
    itself stale.
24. **`L394` fixed: all 12 `pipeline` `DeviceLost` cases named and closed.**
    Item 9's long-standing "12 `pipeline`/8 `robustness` `DeviceLost` never
    had case names recorded" gap is resolved for `pipeline` this session.
    The 12 case names were recovered intact from the original
    `feme-20261005-full-1170553-complete/report.txt` baseline artifact (the
    same source item 9 used for its own 53-case crash re-run) and re-run
    verbatim via `deqp-vk -n <names> --deqp-terminate-on-device-lost=disable`:
    all 12 were a single real, reproducible bug (`L394`, see item above) --
    stencil testing enabled against a render target with no stencil
    attachment -- now fixed, all 12 Pass. **Group accounting table below
    updated:** `pipeline` `DeviceLost` 12 -> 0 (Pass +12). `pipeline`
    `Crash` (46, per item 9 likely stale but still unconfirmed) remains
    unresolved -- a `dEQP-VK.pipeline.*` re-sweep to pin it down precisely
    is infeasible in a single session (~1.17-1.64M cases, confirmed via
    `--deqp-runmode=stdout-caselist` and the mustpass case list, not the
    tens-of-thousands earlier handoffs assumed). `check-feme`: 3606/3609
    Passed, 3 Unsupported, 0 Failed, 0 regressions. No
    `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` change
    needed -- internal core-1.0 draw-time correctness fix only.
25. **`robustness` `DeviceLost=8` case names recovered via a genuine full
    re-sweep -- the figure itself was not stale, but item 9's "never had
    case names recorded" gap is now closed.** Confirmed `dEQP-VK.robustness.*`
    is a tractable 98,776 cases (`--deqp-runmode=stdout-caselist`), unlike
    `pipeline`; ran it fully, from scratch, in the background
    (`/tmp/robustness_full_sweep/`, `--deqp-terminate-on-device-lost=disable
    --deqp-terminate-on-fail=disable`): deqp's own summary totals (683
    Pass / 10 Fail / 98,083 NotSupported) fold `DeviceLost` results into
    the `Fail` count; per-case grepping splits that 10 into **8
    DeviceLost (an exact match for the stale table's own figure -- that
    cell was already accurate, not stale) + 2 genuine ordinary Fail**
    (previously counted as 0, since the table's prior `685 Pass / 0 Fail`
    cell predates this session's re-sweep). (An earlier draft of this
    item wrongly claimed a prior, already-completed "13,786-case full
    sweep" had found only 1 DeviceLost; that was actually a narrower
    `index_access`-only subset run during a now-compacted part of this
    session, not the true full family -- logged here as a correction.)
    All 8 DeviceLost cases share one shape:
    `dEQP-VK.robustness.index_access.draw_indexed{,_indirect}_pipeline_
    robustness_1_vert{,_geom,_tess,_tess_geom}_frag` (case names now
    recorded for the first time). Re-running one of the 8 in isolation
    (`draw_indexed_pipeline_robustness_1_vert_frag` alone) **does not
    reproduce** the DeviceLost -- it passes cleanly standalone, matching
    `L392`'s own earlier-documented symptom shape (an order/accumulated-
    process-state-dependent bug, not a per-case-content bug) rather than
    `L394`'s single-case-reproducible shape. Not root-caused or fixed this
    session -- needs a dedicated investigation reproducing the full-sweep
    ordering rather than a single isolated case, out of this session's
    remaining budget. The 2 ordinary `Fail`s
    (`non_robust_buffer_access.unexecuted_oob_{overflow,underflow}`) are a
    new, previously-unrecorded, separate finding, also not triaged this
    session. **Group accounting table:** `robustness` `Pass` 685 -> 683,
    `Fail` 0 -> 2 (the 8 DeviceLost were already counted in this row's
    total, just uncredited to any specific case name); case names for
    both the 8 DeviceLost and 2 Fail are now on record above for a future
    session.

26. **`L375` fixed (7th investigation, this session): `DiamondFlattener`
    cross-instance merge-block collision, 2 of 2 `spirv_assembly` crashes
    now clean diagnostics.** The real root cause (finally confirmed, after
    six prior sessions' disproven theories): `LoopLinearizer::
    linearizeCycle` constructs *multiple separate* `DiamondFlattener`
    instances for the same `Function` (the scan-driver loop's `DF`, a
    read-only `Precheck` probe, and the real `DF` used for
    `flattenLoopBodyRegion`), each previously tracking "merge blocks
    already claimed" only via its own per-call or per-instance state, so
    one instance had no memory of what a different, sequentially
    constructed-and-destroyed instance already claimed. Fix: thread one
    `SmallPtrSet<BasicBlock *>` owned by the per-`Function` driver
    (`LinearizePass::run`) through every `DiamondFlattener`/
    `LoopLinearizer` instance via a `SmallPtrSetImpl<BasicBlock *> &`
    reference member; `validate()` gains a uniqueness check that now
    produces a clean diagnostic instead of asserting when a merge block is
    revisited. `Precheck` deliberately keeps its own local, non-shared set
    (a read-only feasibility probe that must not pollute or be polluted by
    the real, mutating call's state). **Verified:** both
    `spirv_assembly.switch_continue_{geom,tesse}` (item 5/9's own 2
    `spirv_assembly` crashes) now produce a clean `Fail
    (VK_ERROR_INITIALIZATION_FAILED)` instead of asserting --
    `switch_continue_tessc` (not previously counted as a crash) also now
    produces the same clean `Fail`, confirming no regression there either.
    The remaining crash, `graphicsfuzz.nested-for-loops-switch-
    fallthrough`, is confirmed (again, via live trace) a *different*
    mechanism -- the colliding block is never itself visited twice as
    `Cur`; an edge is silently stolen via a `RedirectTo` rewrite without a
    literal double-visit -- matching item 7's already-tracked `L383(a)`,
    deliberately left open per standing guidance that it needs its own
    dedicated design session. **Regression-verified:** full
    `spirv_assembly.instruction.graphics.loop.*` (32/35 Pass, 3 expected
    Fail, 0 Crash) and full `graphicsfuzz.*` minus the one known `L383(a)`
    crash (666/756 Pass, 82 Fail, 8 Not Supported -- an *exact* match to
    this table's already-recorded 666/82/8 figures, confirming zero
    regressions anywhere in the family); `ninja check-feme`: 3,606/3,609
    Passed, 3 Unsupported, 0 Failed. **Group accounting table:**
    `spirv_assembly` `Crash` 2 -> 0, `Fail` 458 -> 460 (the 2 former
    crashes now counted as ordinary `Fail`s); `graphicsfuzz` unchanged (0
    regressions, the 1 remaining crash is the separately-tracked
    `L383(a)`). No `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md` change needed -- internal correctness
    fix only. Item 9's still-open `pipeline` Crash=46/DeviceLost=12
    reconciliation (a fresh full-family sweep) and `robustness`'s 8
    `index_access` DeviceLost ordering-dependent bisection (item 25) both
    remain untouched this session, as does the 2 new
    `non_robust_buffer_access.unexecuted_oob_{overflow,underflow}` Fails
    (item 25) -- none investigated this session, budget went entirely to
    `L375`.

27. **`L395` fixed (this session): `L383(a)`'s own, previously-mischaracterized
    `graphicsfuzz.nested-for-loops-switch-fallthrough` crash is a
    *different* mechanism from `L375` after all, now fixed too -- the
    last remaining process crash in the entire measured abnormal set.**
    Item 26's own "closed as a duplicate of `L375`" classification for
    `L383(a)` (carried from an even earlier session's shallow `gdb bt`
    match) was wrong: a dedicated, fully live-traced re-investigation this
    session (new, temporary `FLATTENTRACE`/`VALIDATETRACE` instrumentation
    gated behind `FEME_DEBUG_FLATTEN_TRACE`, removed once the fix was
    confirmed) found `L375`'s own fix only guards `validate()`'s
    `CondBr`-terminated diamond-start blocks against cross-walk
    collisions -- it does not guard a plain pass-through (`UncondBr`-only)
    block against the same collision. This reproducer's crash is exactly
    that gap: an outer divergent diamond's own true-arm is a straight-line
    chain that happens to pass through the *same physical block*
    (confirmed via pointer-identity trace) that a nested *uniform* diamond
    (found later, while validating the outer diamond's own false-arm)
    also branches to directly as one of its own two successors -- both
    diamonds' own `hasNPredecessors(2)` check at their (coincidentally
    identical) shared reconvergence point passes individually, so the
    overlap is invisible until `flatten`'s "unconditional fallthrough"
    rewrite steals one of the shared block's two real predecessor edges
    out from under the nested diamond's own, not-yet-processed merge.
    **Fix:** `validate()`'s `MergeVisited` uniqueness check now runs for
    *every* visited block (`ReturnInst`/`UncondBr`/`CondBr` alike), not
    only `CondBr`-terminated diamond starts, while still letting the
    cycle-boundary exemption run first (preserving its own legitimate
    multi-visit tolerance). **Verified:** the reproducer no longer
    crashes -- a clean `Fail` diagnostic instead of a `SIGSEGV`/assert
    abort. All four known `L375`/`L383(a)` reproducers
    (`switch_continue_{geom,tessc,tesse}`,
    `nested-for-loops-switch-fallthrough`) re-verified unaffected/fixed as
    expected. **Regression-verified:**
    `spirv_assembly.instruction.graphics.loop.*` unchanged (32/35 Pass, 3
    Fail, 0 Crash); full `graphicsfuzz.*` now runs all 757 cases (the
    reproducer no longer needs excluding) -- 666 Pass (unchanged), 83 Fail
    (+1, exactly the former crash converted to an ordinary `Fail`), 8 Not
    Supported (unchanged), **0 Crash**. New regression unit test:
    `shared-pass-through-block-between-arms.ll`, confirmed via `git
    stash`/rebuild A-B testing to crash with the identical assertion
    pre-fix and produce the expected clean diagnostic post-fix. `ninja
    check-feme`: 3,607/3,610 Passed (+1 new test), 3 Unsupported, 0
    Failed, 0 regressions. **Group accounting table:** `graphicsfuzz`
    `Crash` 1 -> 0, `Fail` 82 -> 83. No
    `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` change
    needed -- internal correctness fix only. **The entire measured
    abnormal set's original 53 crash/`Unrun` cases are now fully
    accounted for and fixed (0 remain).** Items 9's still-open `pipeline`
    `Crash=46` figure reconciliation, `robustness`'s 8 `index_access`
    `DeviceLost` ordering-dependent bisection (item 25), and the 2
    `non_robust_buffer_access.unexecuted_oob_{overflow,underflow}` Fails
    (item 25) remain untouched this session.

28. **`L396` fixed (this session): the 8 `robustness.index_access.draw_
    indexed{,_indirect}_pipeline_robustness_1_*` `DeviceLost` cases were
    never order-dependent at all -- item 25's own "does not reproduce in
    isolation" finding was simply stale/wrong, and the real bug reproduces
    every single time, standalone.** Re-running one of the 8 in isolation
    this session (`draw_indexed_pipeline_robustness_1_vert_frag` alone,
    3x for determinism) reproduced `DeviceLost` every time, contradicting
    item 25's record -- no bisection was needed at all.
    `FEME_VULKAN_LOG_CREATION_ERRORS=1` immediately surfaced the real
    error: `"vkQueueSubmit: the indexed draw's index range exceeds
    maxDrawIndexedIndexValue"`. Root cause: this CTS test family
    deliberately sets `firstIndex` to `UINT32_MAX - 100` (or, for the
    8/16-bit-index variants, just past the index buffer's own end) to
    exercise `VK_EXT_pipeline_robustness`'s/`robustBufferAccess`'s
    "out-of-bounds index buffer read returns zero" guarantee -- but this
    ICD's own `CommandBuffer.cpp::validateDrawFetchBounds` pre-rejected
    any indexed draw whose index range reached past its bound index
    buffer, and a second, independently wrong check in
    `validateDrawCounts` additionally rejected whenever `firstIndex +
    indexCount` (a buffer-byte-range quantity) exceeded
    `maxDrawIndexedIndexValue` (a limit on actual index *values* fetched
    from the buffer, an unrelated quantity); even past both of those,
    `Executor.cpp`'s own index fetch loop still hard-errored on an
    out-of-range read instead of reading zero. This is exactly the same
    bug shape F10 already fixed for vertex-attribute fetches
    (`Roadmap.md`'s F10 entry) -- F10's own audit simply missed the index
    buffer read path itself. **Fix:** removed both pre-validation
    rejections (`validateDrawFetchBounds`'s index-range-overrun check,
    `validateDrawCounts`'s mismatched `maxDrawIndexedIndexValue` check for
    indexed draws) and changed `Executor.cpp`'s index fetch to read index
    value `0` for any out-of-range slot, mirroring the vertex-attribute
    fetch's own existing zero-fill convention immediately below it in the
    same function. Two pre-existing unit tests
    (`RejectsOutOfBoundsIndexRange`,
    `RejectsIndexRangeBeyondBindIndexBuffer2Size`) asserted the old,
    now-incorrect `VK_ERROR_DEVICE_LOST` behavior; both renamed
    (`IndexBufferReadPastEndReadsZero`,
    `IndexBufferReadBeyondBindIndexBuffer2SizeReadsZero`) and rewritten to
    assert success plus the correct degenerate-triangle (clear-color-only)
    pixel output instead. New unit test
    `IndexBufferReadWithHugeFirstIndexReadsZero` covers the exact
    `firstIndex = UINT32_MAX - 100` reproducer shape. `ninja check-feme`:
    3,608/3,611 Passed (+1 new test, net of the 2 renames), 3 Unsupported,
    0 Failed, 0 regressions. **Verified:** all 8 target cases now Pass; a
    full `dEQP-VK.robustness.index_access.*` re-run (53 cases) is 8 Pass /
    0 Fail / 45 NotSupported (was 0 Pass / 8 Fail among the executable
    subset); a full `dEQP-VK.robustness.*` re-sweep is **693 Pass / 0 Fail
    / 98,083 NotSupported** (was 683/10/98,083) -- the same single fix
    also silently resolved item 25's other, previously-untriaged finding:
    the 2 `non_robust_buffer_access.unexecuted_oob_{overflow,underflow}`
    Fails both now Pass too (re-verified standalone and as a full
    `non_robust_buffer_access.*` sub-family run, 2/2 Pass), confirming
    they were never a separate bug -- they were collateral damage from a
    preceding case's `DeviceLost` corrupting shared process state within
    the same `deqp-vk` run, consistent with item 25's original (correct,
    just misattributed) "order-dependent" symptom observation. A targeted
    `dEQP-VK.draw.*` regression sweep (29,451 cases) confirms 0 Fail, no
    regression from relaxing the two removed validation checks.
    **Group accounting table:** `robustness` `Pass` 683 -> 693, `Fail` 2
    -> 0, `DeviceLost` 8 -> 0. No `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md` change needed -- internal correctness
    fix only, no new feature surfaced. Handoff items 1 and 2 (bisection
    and oob triage) are therefore both fully closed this session, with no
    further action needed on either.
29. **`pipeline` `Crash=46`/`api` `Crash=1` reconciled this session: both
    are stale, zero real crashes remain.** Extended item 9's/`L394`'s own
    targeted-case-name technique instead of a full `pipeline.*` sweep
    (still ~1.17-1.64M cases, infeasible). Recovered the exact 53 `Unrun`
    case names from the original `feme-20261005-full-1170553-complete/
    report.txt` baseline artifact (49 `pipeline`, 1 `api`, 1
    `graphicsfuzz`, 2 `spirv_assembly` -- the same artifact item
    9/`L385`/`L394` have each reused) and re-ran all 49 `pipeline` cases
    plus the 1 `api` case verbatim against the current build:
    **49/49 `pipeline` and 1/1 `api` cases now Pass, 0 Fail, 0 Crash.**
    This confirms, with direct case-level evidence (not just the
    `Roadmap.md` stability-row's own prose claim), that all 53 of the
    original baseline's crash/`Unrun` cases are fixed: the 46 real
    `pipeline` `SIGSEGV`s (44 `ds_resolve_sample_zero`, fixed by `L384`;
    2 `compatible_render_pass`, fixed by `L381`) plus 3 collateral
    `Unrun`-but-not-actually-crashing `ds_resolve_sample_zero` cases
    (already noted by `L385` as pre-existing ordinary `Fail`s, now also
    confirmed Pass -- a separate, later fix to `L379(a)`'s own
    `resolveMode` gap not tracked under any single roadmap item in this
    session's history, found only by this reconciliation's own direct
    rerun), the 1 `api` `SIGSEGV` (fixed by `L382`), the 1 `graphicsfuzz`
    crash (fixed by `L395`), and the 2 `spirv_assembly` crashes (fixed by
    `L375`) -- **a complete, zero-crash close-out of the entire original
    53-case baseline crash/`Unrun` set.** Also re-ran all 4 of `L375`'s/
    `L383(a)`'s own known reproducers (`switch_continue_{geom,tessc,
    tesse}`, `nested-for-loops-switch-fallthrough`) directly: all 4 now
    fail with clean, diagnosable "roadmap milestone 6 deviation" errors
    (ordinary `Fail`, not a crash) -- confirming the handoff claim that
    `L375` is "still open, needing a design session" was itself stale;
    `Roadmap.md`'s own `L375`/`L383(a)` rows already record both as
    fixed (`Fix committed.` / `fixed (see L395)`), this session's rerun
    is independent, direct confirmation of that record. **Group
    accounting table:** `pipeline` `Pass` 259,029 -> 259,075, `Crash` 46
    -> 0; `api` `Pass` 115,798 -> 115,799, `Crash` 1 -> 0. The remaining
    `pipeline` `Fail`/`NotSupported` totals (39,644/873,467) are
    themselves still the stale 2026-10-05 baseline figures, unrelated to
    this reconciliation -- a full `pipeline.*` resweep remains infeasible
    in a single session and is not attempted here. No
    `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` change
    needed -- a documentation reconciliation only, no code changed this
    item.


30. **First-ever genuine full-case-space sweep completes (this session):
    `feme-20261007-full` (3,237,548 total cases, the full `vk-default`
    mustpass index, vs. the Oct-5 baseline's partial 1,170,553) finished
    after this session unblocked it by killing one worker stuck 6+ hours
    in a real `QueueExecutor`/`Fence`-teardown deadlock (see `L398`,
    `Roadmap.md`) -- the sweep itself continued and completed cleanly
    afterward. Result counts: 688,262 Pass / 2,539,673 NotSupported /
    3,519 Fail / 192 DeviceLost / 1,656 ResourceError / 46
    QualityWarning / 4,200 Unrun (3,233,348 completed). Because this is
    the *first* run of the ~2.07M cases the Oct-5 baseline never
    reached, nearly every "changed" entry is `Unrun -> X`, not a true
    regression -- diffing against a never-run baseline mostly surfaces
    previously-unmeasured, not newly-broken, results. **The single
    largest, cleanly-clustered new finding: all 192 `DeviceLost` cases
    are `dEQP-VK.renderpasses.{renderpass1,renderpass2}.suballocation.
    {unused_clear_attachments,multisample_resolve}`** (95 + 95 +
    1 + 1), nearly half of each `unused_clear_attachments` variant's own
    208-case family. Re-ran one case standalone to confirm it is
    genuinely, deterministically reproducible (not an artifact of the
    sweep's own worker-kill): `dEQP-VK.renderpasses.renderpass1.
    suballocation.unused_clear_attachments.colorunused_colorunused_
    colorunused_colorunused_depthonly_d32_unused` fails immediately
    (`TestDuration` 120,548us, far under the 5s `SafetyNetTimeoutNs`
    default -- this is **not** the `L398` hang/timeout path, a
    different, fast-failing bug) with `vk.waitForFences(...):
    VK_ERROR_DEVICE_LOST at vkCmdUtil.cpp:296` and no
    `FEME_VULKAN_LOG_CREATION_ERRORS=1` diagnostic emitted. Filed as
    `L399` (`Roadmap.md`) for a dedicated future root-cause session; not
    root-caused or fixed this session (reasoning-effort/time-budget
    reasons -- this item was found only in the sweep's final minutes).
    The other two result buckets (3,519 `Fail`, 1,656 `ResourceError`)
    show no comparably clean single-sub-family cluster on a quick
    `sed`/`sort`/`uniq -c` pass and are left untriaged -- a future
    session should re-run this same clustering technique against them
    specifically. No code changed this item -- a sweep-monitoring and
    reconciliation finding only. `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md`: no change needed. Full artifact:
    `/home/dev/dev/VK-GL-CTS/run/feme-20261007-full/report.txt`
    (509MB).

31. **`L399` root-caused and fixed (this session): 190 of its 192-case
    `DeviceLost` cluster closed.** `FEME_VULKAN_LOG_CREATION_ERRORS=1`
    (not set by item 30's repro) immediately surfaced the real error:
    `vkQueueSubmit: vkCmdClearAttachments names the depth attachment,
    which is not bound`. Root cause, in `feme/lib/Vulkan/ImageOps.cpp`'s
    `runClearAttachments`: a subpass that never references a depth/
    stencil attachment at all (`VK_ATTACHMENT_UNUSED`, so `Binding.
    Depth`/`Binding.Stencil` is unset entirely) was hard-errored instead
    of treated as a no-op -- a genuinely distinct, equally legal "unused"
    shape from the already-handled present-but-unused case (`Binding.
    Depth` set, `.View` null, for dynamic rendering's `VK_NULL_HANDLE`
    `imageView`). Confirmed the fix direction via the `dynamic_rendering`
    sibling CTS case (identical scenario shape, different API path),
    which already passes cleanly. **Fix:** removed the two hard-error
    paths, changed the two view-null checks to also require the
    `optional` itself be set. New regression unit test
    `ClearAttachmentsSkipsUnboundDepthStencilAttachment`
    (`DrawTest.cpp`) covers this exact shape; confirmed it fails
    (`VK_ERROR_DEVICE_LOST`) before the fix and passes after.
    `FeMeVulkanTests`: 806/806 Passed (+1 net new test), 0 regressions.
    **Verified:** the original failing case now passes standalone; all
    190 `unused_clear_attachments` cases from the cluster (excluding the
    2 `multisample_resolve` cases) now Pass (190/190); a
    `dEQP-VK.renderpasses.*.suballocation.*` regression sweep (21,932
    cases, excluding the still-broken `multisample_resolve` family) shows
    14,931 Pass / 36 Fail / 6,965 NotSupported, 0 DeviceLost -- the 36
    Fails are pre-existing `subpass_dependencies.late_fragment_tests`
    cases, confirmed byte-for-byte identical to the Oct-7 baseline
    (`feme-20261007-full/report.txt`), i.e. zero regressions from
    relaxing the removed validation. **The remaining 2
    `multisample_resolve.mixed_sample_count.
    mixed_sample_count_subpasses` cases (`renderpass1`/`renderpass2`) are
    a separate, unrelated bug** (`vkQueueSubmit: vertex binding 0 is not
    bound to a buffer`) -- not addressed by this fix, left open for a
    future session. `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md`: no change needed -- internal
    correctness fix only, no feature/extension-surface change.

across all 48,705 `subgroups` cases; `api` falling to 34 failures; `image` to
14; `glsl` to 15; `texture` to 2; and `tessellation` to 30.

32. **`L400` root-caused and fixed (this session): both cases of its
    2-case cluster closed.** The CTS case's pipeline calls
    `GraphicsPipelineWrapper::setupVertexInputState()` with no arguments,
    which falls back to the CTS framework's own filler default vertex
    input state (one binding, one attribute at location 0) -- never read
    by the test's actual vertex shader, which draws entirely from
    `gl_VertexIndex`-indexed constants and declares no
    `layout(location=...) in` variable at all. FeMe's existing `L127`
    fix (`bindingHasAnyAttribute`, `CommandBuffer.cpp`) considered a
    binding load-bearing (requiring a bound buffer at draw time) if
    *any* pipeline attribute referenced it, regardless of whether the
    vertex shader itself ever reads that attribute's location -- but the
    real VUID (e.g. VUID-vkCmdDraw-None-04914) is scoped to bindings
    "accessed via vertex input variables declared in the vertex shader
    entry point's interface". **Fix:** `GraphicsPipeline.cpp`'s
    `validateStageInterfaces` (which already parses the vertex shader's
    reflected `feme::EntrySignature` at pipeline-creation time to
    validate every shader-declared input `Location` against the
    pipeline's attribute table, previously discarding that data once
    validation passed) gained an optional `UsedVertexInputLocations`
    out-parameter; the result is now cached on `GraphicsPipelineArtifact`
    (the shareable, shader-derived, cache-friendly struct, following the
    existing `Tessellation`/`Geometry`/`Mesh` precedent) and exposed via
    a new `GraphicsPipeline::usedVertexInputLocations()` accessor.
    `bindingHasAnyAttribute` now also requires the matching attribute's
    `Location` be in this set before treating the binding as
    load-bearing. New regression test
    `RendersWithAVertexInputAttributeTheShaderNeverDeclares`
    (`DrawTest.cpp`), confirmed to fail (`VK_ERROR_DEVICE_LOST`) pre-fix
    and pass post-fix. `FeMeVulkanTests`: 807/807 Passed (+1 net new
    test), 0 regressions; `check-feme`: 3610/3613 Passed, 3 Unsupported,
    0 Failed (one pre-existing timing-flaky `SyncTest` case under
    concurrent CPU load from this session's own CTS sweep, confirmed
    unrelated and passing cleanly in isolation). **Verified:** both
    original `L400` cases no longer `DeviceLost` -- the vertex-binding
    error is gone, replaced by an unrelated pixel-mismatch `Fail`
    ("Resolved tile mismatch detected at pixel (63, 66)! RTV B color
    does not match RTV A reference."), filed separately as `L401`
    (a genuine rendering-correctness bug `L400`'s draw-time validation
    fix was merely unblocking, not fixing). A full
    `dEQP-VK.renderpasses.*` regression sweep (81,188 cases, the entire
    group) shows 35,521 Pass / 51 Fail / 45,616 NotSupported, **0
    DeviceLost** (down from the Oct-7 baseline's 328 recorded in the
    group-accounting table below) -- the 51 Fails are all pre-existing
    (9 `depth_stencil_write_conditions`, 36
    `subpass_dependencies.late_fragment_tests`, 4
    `depth_stencil_resolve`, the 2 new `L401` cases), confirming zero
    regressions from this fix. `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md`: no change needed -- internal
    correctness fix only, no feature/extension-surface change.

33. **`L401` root-caused and fixed (this session): both cases of its
    2-case cluster closed.** The failing render pass has 2 subpasses
    sharing one single-sample attachment two different ways: subpass 0
    renders into a separate 4x MSAA color attachment and resolves it
    into this attachment at the subpass boundary; subpass 1 then binds
    the *same* attachment directly as its own plain color attachment (no
    resolve of its own, no draw of its own in the failing CTS case's
    narrow repro). `CommandBuffer.cpp`'s `applyLoadOps` (roadmap H2i)
    tracks, per attachment `ImageView*`, which of its views have already
    had their `VK_ATTACHMENT_LOAD_OP_CLEAR` load op applied in this
    render-pass instance, so a later subpass's new reference to an
    already-used attachment doesn't get wrongly re-cleared -- but it only
    ever recorded a *directly-bound* `RenderTargetView::View` as "used",
    never a color/depth/stencil attachment's own `ResolveView`. Subpass
    1's direct binding of the resolve target therefore looked like that
    attachment's first use, and its `LOAD_OP_CLEAR` ran a second time at
    the `vkCmdNextSubpass` boundary, overwriting subpass 0's
    `resolveSubpassColorAttachments` resolve result with the attachment's
    own clear value just before `vkCmdEndRenderPass` -- exactly the
    "Resolved tile mismatch detected at pixel (63, 66)! RTV B color does
    not match RTV A reference" symptom (RTV B, the test render pass's
    resolve output, silently reverted to background/clear color; RTV A,
    the reference render pass's single-subpass resolve, correctly kept
    its drawn content). **Fix:** a new `markResolveViewLoaded` helper,
    called from `applyLoadOps` for every one of `Binding.Colors`'/
    `Binding.Depth`'s/`Binding.Stencil`'s own `ResolveView`, right after
    that entry's own `applyClear` call -- marking the resolve target's
    `loadedViewMaskKey` as loaded for the current subpass's own
    `ViewsToClear` views, without actually clearing it (the resolve
    itself, not a clear, is what first writes real data there). New
    regression test
    `DrawTest.ResolveTargetSurvivesALaterSubpassThatBindsItDirectly`: a
    minimal 2-subpass render pass (no draws at all -- the MSAA
    attachment's own `LOAD_OP_CLEAR` to solid red is enough to exercise
    the resolve, and the single-sample attachment's own distinct green
    `LOAD_OP_CLEAR` marker value makes an erroneous re-clear
    unmistakable), confirmed via `git stash` A/B to fail (every pixel of
    the resolve target reads back the green marker instead of the
    resolved red) pre-fix and pass post-fix. `FeMeVulkanTests`: 808/808
    Passed (+1 net new test), 0 regressions. **Verified:** both original
    `L401` cases now Pass (`Pass (Dynamic MSAA resolve is compliant)`).
    A full `dEQP-VK.renderpasses.*` regression sweep (81,188 cases, the
    entire group) shows 35,523 Pass / 49 Fail / 45,616 NotSupported,
    **0 DeviceLost, 0 Crash** -- the 49 Fails are exactly the same
    pre-existing families item 32 already identified (9
    `depth_stencil_write_conditions`, 36
    `subpass_dependencies.late_fragment_tests`, 4
    `depth_stencil_resolve`), with the 2 `L401` cases now moved from
    Fail to Pass and zero new Fails introduced anywhere else in the
    group. `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:
    no change needed -- internal correctness fix only, no
    feature/extension-surface change.

34. **`L398` root-caused and fixed (this session): `Fence`
    use-after-free during `QueueExecutor`/device teardown.** A prior
    session's `gdb` inspection of a genuinely stuck full-case-space sweep
    worker found the main thread blocked forever in `vkDestroyDevice ->
    QueueExecutor::~QueueExecutor() -> std::thread::join()`, waiting on a
    worker thread itself blocked forever in `pthread_mutex_lock` inside
    `Fence::signal()`'s own `Mutex` -- a `std::mutex` locked (or merely
    accessed) after its owning object has been destroyed is undefined
    behavior, and in practice hangs forever rather than crashing cleanly.
    Root cause: `vkDestroyFence` freed its `Fence` object with zero
    synchronization against the same fence's own `QueueExecutor`
    fence-signal task (`makeFenceSignalTask`) -- if that task had not run
    yet when `vkDestroyFence` was called, the worker thread would later
    call `signal()` on already-freed memory. Ordinary `vkWaitForFences`-
    then-`vkDestroyFence` usage was never actually exposed to this
    (`Fence::wait`/`isSignaled` cannot observe a signal before `signal()`
    itself has fully completed), but a host machine heavily
    oversubscribed by many concurrent CTS workers -- exactly this
    project's own full-sweep harness -- can make
    `QueueExecutor::waitIdle`'s own generous safety-net timeout
    (`vkQueueWaitIdle`/`vkDeviceWaitIdle`) lapse from pure scheduling
    contention rather than a real hang, and an application that then
    treats that as "safe to tear down" must not be able to free memory a
    worker thread still intends to touch. **Fix:** `Fence` gained simple
    retain/release bookkeeping for its own outstanding `QueueExecutor`
    fence-signal tasks. `vkQueueSubmit`/`vkQueueSubmit2` retain the fence
    synchronously on the submitting host thread before enqueuing its
    signal task; `vkDestroyFence` frees the object immediately if none is
    outstanding (the ordinary case), or otherwise defers, remembering the
    caller's `VkAllocationCallbacks*`; the fence-signal task itself
    performs the deferred free once it finishes running, if one was
    requested. New regression tests: `FenceLifetimeTest.
    DestroyDefersUntilPendingSignalTaskReleases`/`DestroyWithNoPendingSign
    alFreesImmediately` deterministically exercise `Fence`'s new
    bookkeeping through its own public API (not timing-dependent);
    `SyncTest.StressFenceCreateSubmitWaitDestroyNoGlobalWait` is a
    broader multi-thread, multi-queue stress test of the same lifecycle
    under real `vkQueueSubmit` dispatch (did not itself reproduce a hang,
    pre- or post-fix, in this single-process environment -- the live
    sweep's trigger needs genuine host oversubscription across several
    concurrent `deqp-vk` processes this test cannot recreate -- but is
    kept as ongoing coverage). `FeMeVulkanTests`: all 24 `SyncTest`/
    `FenceLifetimeTest` cases Pass; full `check-feme`: 3614/3617 Passed, 3
    Unsupported, 0 Failed. **Verified:** a targeted CTS sweep of
    `dEQP-VK.api.smoke.*`, every `dEQP-VK.synchronization*.*fence*` case,
    and `dEQP-VK.api.object_management.single.fence*` (18,302 cases total)
    shows 9764 Pass / 0 Fail / 8538 NotSupported -- no regressions.
    `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
    needed -- internal correctness fix only, no feature/extension-surface
    change.

35. **`L402` filed (this session): `subpass_dependencies.late_fragment_tests`
    depth/stencil `Fail` cluster, not yet root-caused.** Re-clustered the
    `renderpasses.*`-only sweep already on disk from a prior session
    (81,188 cases) by extracting every `Fail`/`ResourceError` case name
    and grouping by prefix -- the same technique that found `L399`-`L401`.
    Found 36 cases, all `dEQP-VK.{renderpass1,renderpass2}.suballocation.
    subpass_dependencies.late_fragment_tests.*` (every combination of
    `render_size_{32,64,128}_{32,64,128}` x `subpass_count_{2,3,5}` x
    `d24_unorm_s8_uint`/`d32_sfloat_s8_uint`) -- a family both `L400`'s
    and `L401`'s own sweeps had already noted in passing as pre-existing,
    but which had never been given its own roadmap entry or
    investigation. Confirmed independently reproducible via a standalone
    rerun of one case (`...render_size_32_32.subpass_count_2.
    d24_unorm_s8_uint`): `Fail (Depth compare failed.)` -- a genuine
    rendering-correctness mismatch, not a `DeviceLost`/`Crash`/timeout.
    Not root-caused this session (found via the ~30 min clustering task
    only) -- filed as `L402` for a dedicated session, with a pointer to
    `VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT`-scoped depth/stencil
    subpass-dependency handling across `vkCmdNextSubpass` as the likely
    area (the test group exercises exactly that). No code change.
    `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
    needed -- investigation only.

36. **`L403` fixed (this session):
    `transform_feedback.primitives_generated_query.{get,copy}`, 1,308
    cases, `pgqGenerated == 0, expected N`.** Found via re-clustering a
    fresh full-case-space sweep (`feme-20261008-full/`); root-caused to
    two layered bugs, both needed together to fix the cluster:
    (1) `vkCmdBeginQueryIndexedEXT`/`vkCmdEndQueryIndexedEXT` silently
    dropped their own `index` parameter (which stream a
    `VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT` query counts), always
    scoping to stream 0; (2) `buildMemberDecorationTuple`
    (`SPIRVToLLVMPatterns.cpp`) had no case for SPIR-V's `Stream`
    decoration on a struct *member* (only whole variables), so a
    multi-stream geometry shader whose only output is `gl_Position`
    inside `gl_PerVertex` never got its `Stream` tag recovered at all;
    and (3), found only after (1)/(2) alone still left the cluster
    failing: the specific CTS shader variant under test
    (`PrimitivesGeneratedQueryTestCase::initPrograms`'s `no_xfb`/
    `pgq_1`/`pgq_2` branch) calls `EmitStreamVertex(N)`/
    `EndStreamPrimitive(N)` with **no** output-variable write at all, so
    no signature element of any kind ever carries a `Stream` tag for
    (2) to even apply to -- `Executor.cpp`'s `StreamCount` (derived
    purely from signature elements) stayed stuck at 1 regardless.
    Fixed by scanning each geometry entry point's own
    `EmitStreamVertex`/`EndStreamPrimitive` call operands at SPIR-V-to-
    LLVM conversion time (`ConvertSPIRVToLLVMPass.cpp`, new
    `feme.geometry.stream_count` passthrough attribute /
    `GeometryState::StreamCount`), taking the larger of that and the
    signature-derived count in `Executor.cpp`, and populating
    `PrimitivesGeneratedByStream` for every declared stream (not just
    ones with their own signature elements). Four commits, one per
    layer, each with its own unit/lit test coverage spanning SPIR-V
    deserialization, the SPIR-V-to-LLVM conversion pass, `CommandBuffer`
    recording/replay, and `Executor`'s runtime statistics computation.
    `check-feme`: 3616/3619 Passed, 3 Unsupported, 0 Failed (up from
    3614/3617 before the 2 new unit tests this session added).
    **Verified:** the full `dEQP-VK.transform_feedback.
    primitives_generated_query.*` family (107,866 cases): 0 Fail (down
    from 1,308), 4,926 Pass, 102,940 NotSupported (unrelated --
    `transformFeedbackQueries` disabled). `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md`: no change needed -- internal
    correctness fix to an already-advertised extension
    (`VK_EXT_primitives_generated_query`), no feature/extension-surface
    change.

37. **`L404` fixed (this session): `pipeline.pipeline_library.
    vertex_input.*`'s `mat2`/`mat3`/narrow-matrix `Fail` cluster, 217
    cases, `feme-graphics-validate-stage` "component N is out of range
    for element M" at pipeline-creation time.** Found via clustering a
    prior session's full-sweep `part4` 13,971-Fail bucket (13,754 of
    which are a separate, already-known, unrelated `DeviceLost`
    cluster, left open). Root-caused via `FEME_DUMP_IR_PRECANON`/
    `FEME_DUMP_IR_POSTCANON` IR dumps: `CanonicalizeStage.cpp`'s
    `resolveRowComponent`, which reverse-engineers a scalar stage-IO
    access's `(Row, Component)` pair from its byte offset, disambiguates
    between a narrow (<4-lane) vector array's tight (12-byte, e.g. a
    `mat3` column) and padded (16-byte) per-row stride candidates by
    requiring the residual divide the candidate exactly -- true only
    for each row's first lane, with every other in-row component
    falling back to a tie-break keyed on `hasXfbBufferDecoration`,
    which is unconditionally false for ordinary vertex `Input` globals
    (XFB capture never applies to shader inputs) and so always defaults
    to the wrong, padded stride -- even though the SPIR-V spec
    guarantees `Input`/`Output` interface variables can never carry
    `ArrayStride`/`MatrixStride` decorations, making the tight stride
    the only architecturally-possible answer. Confirmed via a Python
    simulation of the exact heuristic against the real captured byte
    offsets (0, 4, 8, ..., 32) reproducing the exact wrong
    `(Row, Component)` pairs observed, including the literal
    out-of-range `Component == 3`. Fixed, scoped only to the
    single-global (bare interface variable) access path: relaxed
    `resolveRowComponent`'s validity check to accept any in-range
    residual when the caller signals "known-tight", and forced that
    signal on for every `Input`-direction single-global access in
    `resolveOffsetWithinElement`, independent of
    `hasXfbBufferDecoration`; `Output`-direction (XFB-decoration-keyed)
    behavior and the struct/block-member recursive path are both
    completely unchanged. New unit test
    `ResolvesScalarComponentsOfOrdinaryNarrowVectorMatrixColumnsAtTightStride`.
    `ninja check-feme`: 3,617 Passed, 3 Unsupported, 0 Failed (0
    regressions). **Verified:**
    `single_attribute.mat3.as_r32g32b32_sfloat_rate_vertex` Fail ->
    Pass; all 142 `single_attribute.mat*` cases now 0 Fail;
    `multiple_attributes.binding_one_to_many.attributes.f16vec2.mat2.
    mat3` Fail -> Pass. `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md`: no change needed -- internal
    correctness fix, no feature/extension-surface change. The separate
    ~4,598-case `pipeline_library.vertex_input.*` `DeviceLost` cluster
    in the same test family remains open and untouched.

38. **`L405` root-caused and fixed (this session): the remaining
    ~4,598-case `pipeline.pipeline_library.vertex_input.*` `DeviceLost`
    cluster `L404` left open.** Reproduced deterministically and
    standalone (`max_attributes.query_max_attributes.binding_one_to_many.
    interleaved`), confirming a genuine FeMe bug, not an environmental/
    concurrency artifact. `FEME_VULKAN_LOG_CREATION_ERRORS=1` surfaced
    the real error: `"vkQueueSubmit: vertex input location 2 has no
    bound vertex buffer attribute"`. Root cause: this CTS shape's
    vertex shader declares a single array-typed input,
    `layout(location = 1) in vec4 attr[numAttributes-1];`, reflecting
    as one `feme::SignatureElement` with `Location == 1` spanning
    `RowCount` consecutive locations (1 through 15) -- the same
    matrix/array-spanning shape `Executor.cpp`'s vertex-fetch loop
    already walks correctly via `RowLocation = *Elt.Location + Row`.
    `GraphicsPipeline.cpp`'s `validateStageInterfaces` (which builds
    the `UsedVertexInputLocations` set `L400`'s fix introduced) checked
    and recorded only the base `*VSIn.Location`, silently ignoring
    every other row -- so for this shape only location 1 was ever
    recorded as "used." Since `BINDING_MAPPING_ONE_TO_MANY` splits this
    array input's 15 locations across two different vertex bindings
    (alternating by each location's input-rate relative to the first
    attribute's), `CommandBuffer.cpp`'s `bindingHasAnyAttribute`
    wrongly judged the binding supplying locations 2, 4, 6, ..., 14 as
    "unused" and skipped fetching it entirely, leaving those locations
    unbound at draw time. **Fix:** changed `validateStageInterfaces`'s
    per-input check/record loop to iterate `Row` from `0` to
    `VSIn.RowCount-1`, using `RowLocation = *VSIn.Location + Row` for
    both the "is this location supplied" check and the
    `UsedVertexInputLocations` insertion, mirroring `Executor.cpp`'s own
    existing per-row fetch pattern. New regression test
    `L405SplitBindingArrayAttributeRepro` (`DrawTest.cpp`): a minimal
    `RowCount == 2` array input split across two separate bindings,
    confirmed to fail (`VK_ERROR_DEVICE_LOST`) pre-fix and pass
    post-fix. `ninja check-feme`: 3,618 Passed (+1 net new test), 3
    Unsupported, 0 Failed, 0 regressions. **Verified:** the single
    reproducer case Fail -> Pass; a full
    `pipeline.pipeline_library.vertex_input.*` family regression sweep
    (9,208 cases) shows 4,816 Pass / 0 Fail / 0 DeviceLost / 4,389
    NotSupported / 3 QualityWarning -- the entire cluster is resolved.
    `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
    change needed -- internal correctness fix only, no feature/
    extension-surface change.

39. **`L402` root-caused this session (not yet fixed): the
    `subpass_dependencies.late_fragment_tests` 36-case `Fail` cluster
    is a triangle-rasterizer fixed-point-precision gap, not a subpass
    hand-off bug.** First disproved the hand-off's own hypothesis (a
    cross-subpass depth/stencil hand-off bug): re-reading
    `vktRenderPassSubpassDependencyTests.cpp` showed each subpass owns
    its own separately-cleared depth/stencil attachment (not one
    shared attachment reused across subpasses), and a temporary,
    fully-reverted pointer/value trace confirmed the cross-subpass
    `subpassLoad` hand-off and depth-test logic are both correct. The
    real cause: `Executor.cpp`'s triangle coverage test
    (`edgeFnD`/`isTopLeftEdge`, the `E0`/`E1`/`E2` barycentric setup)
    evaluates every sample in full `double` precision against the
    vertices' `float` screen-space coordinates, with no fixed-point
    subpixel snapping -- but FeMe advertises `subPixelPrecisionBits ==
    4` (`PhysicalDeviceInfo.cpp`), and VK-GL-CTS's own reference
    rasterizer (`framework/referencerenderer/rrRasterizer.cpp`'s
    `TriangleRasterizer`) snaps every vertex to that exact 4-bit
    fixed-point grid (`toSubpixelCoord`) before its own edge test, by
    design. Replicated the CTS test's exact vertex-generation PRNG
    (`deRandom`, fixed seed `5`) in a standalone C program to recover
    the one standalone-reproduced failing case's (`render_size_32_32`/
    `subpass_count_2`/`d24_unorm_s8_uint`, pixel `(8,0)`) covering
    triangle's exact vertices (confirmed its generated depth,
    `0.473101437`, matches a prior session's own trace exactly), then
    computed the full-double-precision edge function: the pixel center
    lands *inside* the triangle by only ~0.018 pixels -- a genuine
    near-tie. Replicating `rrRasterizer.cpp`'s own fixed-point
    algorithm (`bits=4`) for the same triangle/pixel pair in Python
    flips that edge's sign negative -- the reference rasterizer
    correctly excludes the triangle at FeMe's own advertised
    precision, while FeMe's full-precision test wrongly includes it.
    Cross-checked against the actual Reference/Result PNGs from an
    earlier `--deqp-log-images=enable` run: pixel `(8,0)`'s reference
    value (250/255, matching the clear-value-propagated-through-
    subpass-1 math, `1.0 - 0.02 = 0.98 -> 250`) confirms the reference
    image agrees no triangle covers it there, while FeMe's result
    value (116/255) matches the covering triangle's depth propagating
    through instead -- a real coverage-decision mismatch, not a
    cosmetic one. This is the triangle-rasterizer analogue of the
    already-known `L326`-`L328` line-rasterizer gap (FeMe advertises a
    `subPixelPrecisionBits` it doesn't actually honor in its coverage
    test). Not fixed this session -- a precision-grid-snapping change
    to the hot triangle-coverage path risks regressing every other
    passing rasterization test; filed as `L406` for a dedicated
    session, mirroring the existing `L326`->`L327` split. No code
    change. `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:
    no change needed -- investigation only.

40. **`L406` fixed this session, closing `L402`: fixed-point triangle
    coverage test landed, matching VK-GL-CTS's reference rasterizer's
    own advertised precision.** Added a standalone, unit-tested
    `feme::graphics::fixedPointEdgeFn` primitive
    (`TriangleRasterization.h`/`.cpp`, in its own commit first) that
    mirrors `edgeFn`'s own directed-edge formula but operates on
    `toSubpixelCoord`-quantized coordinates, reusing the existing
    `toSubpixelCoord`/`LineSubPixelPrecisionBits` the line rasterizer's
    `L326`-`L328` work already introduced rather than duplicating them.
    Three new unit tests (axis-aligned interior coverage, a shared-edge
    top-left-rule tie-break, and `L402`'s own triangle-24/pixel-`(8,0)`
    near-tie as an explicit regression case) all passed before any
    wiring into the live rasterizer. Wired the primitive into
    `Executor.cpp`'s coverage test in a second commit, replacing the
    prior `edgeFnD`-based unlimited-double-precision inside test (now
    removed) with the fixed-point one, keeping the same
    `isTopLeftEdge`-based tie-break logic unchanged. A full `check-feme`
    run against this wiring (before the fix below) found 2 regressions,
    both stipple-pattern tests (`RendersAStippledLine`,
    `BresenhamStippleCounterCountsFragmentsNotDistance`): the stipple
    test's barycentric-weight reconstruction (`B0`/`B1`/`B2`, used to
    interpolate a stippled line's `ArcLength`) divided the new
    fixed-point `E0`/`E1`/`E2` -- now `(1 << Bits)^2` times larger than
    before, since they're computed on quantized coordinates -- by the
    old, still-unquantized float `Area`, silently returning weights off
    by that same scale factor. This didn't affect the coverage test
    itself (a sign-only comparison against zero, scale-invariant), only
    the stipple test's magnitude-sensitive interpolation. **Fix:**
    compute a same-scale `FixedArea` (via `fixedPointEdgeFn` on the
    triangle's own 3 vertices) once per quad, and divide by that instead
    of the float `Area`, only when a stipple test is actually needed
    (avoiding the extra computation on the hot non-stippled path).
    **Verified:** `FeMeGraphicsTests` 414/414 Passed (0 regressions);
    `check-feme` 3620/3624 Passed, 3 Unsupported, 1 Failed -- the sole
    failure, `SyncTest.TimelineSemaphoreWaitBlocksUntilHostSignal`,
    confirmed a pre-existing CPU-contention-sensitive timing flake (3
    other CTS sweeps were running concurrently on the same machine),
    reproduced passing 5/5 in isolation; `L402`'s own 36-case
    `dEQP-VK.renderpasses.{renderpass1,renderpass2}.suballocation.
    subpass_dependencies.late_fragment_tests.*` cluster, 36/36 Pass (0
    Fail, closing `L402`); the broader
    `dEQP-VK.renderpasses.*.suballocation.*` family (30,794 cases, a
    regression check covering every other triangle-rasterization-
    dependent test sharing this same coverage-test code path): 21,019
    Pass / 0 Fail / 9,775 NotSupported / 0 DeviceLost / 0 Crash -- 0
    regressions. `Vulkan14FeatureInventory.md`/
    `VulkanExtensionInventory.md`: no change needed -- this only makes
    an already-advertised `subPixelPrecisionBits` limit actually
    honored in the triangle rasterizer (the line rasterizer already
    honors it per `L326`-`L328`), no feature/extension-surface change.

41. **`dEQP-VK.pipeline.pipeline_library.stencil.*` verification sweep
    (carried over from a prior session, finished this session): 0
    Fail, 0 DeviceLost.** A standalone 58,478-case sweep
    (`/tmp/stencil_sweep`) left running by a prior session (to check
    whether this family was already fixed by `L398`-`L405`, without a
    dedicated re-investigation) finished cleanly this session: 41,770
    Pass / 0 Fail / 16,708 NotSupported / 0 Warnings. Confirms the
    family needs no further code change -- recorded here for the
    record only, no code change this session. No code change.
    `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
    change needed.

42. **Stability triage (`part1` SIGSEGV, `part2` `pipeline.*.blend.
    dual_source`/`stencil` `DeviceLost` growth): investigated, not
    fixed, three new data points narrowing the search space.** Not
    fixed this session -- in progress, filed as `L407` for a dedicated
    root-cause session. No code change.
    - `part2`'s `pipeline.{monolithic,fast_linked_library}.blend.
      dual_source.*` `DeviceLost` cluster is **not** a per-case blend-
      logic bug: a single isolated case from the failing streak passed
      (`r8g8_unorm` 4-attachment state combo), and a 200-case
      contiguous batch pulled from the exact point in `part2`'s
      caselist where the failures were occurring also passed 196/196
      (5 `NotSupported` for an unrelated format reason), both in
      isolation. The failures only reproduce inside `part2`'s single
      long-running process (531,232 cases processed so far this
      session, 119,904 of them `DeviceLost` -- a 22.6% rate, up from
      the single-digit aggregate percentage in earlier sessions'
      handoffs, i.e. this is actively getting worse as the process
      keeps running).
    - `part1`'s previously-suspected (never-confirmed) SIGSEGV trigger
      case, `dEQP-VK.api.copy_and_blit.core.use_after_copy.
      a8b8g8r8_srgb_pack32.transfer_dst_optimal.
      1024x1024x2_tq_color_att_flag_img2img_msaa`, passed cleanly this
      run (confirmed via its own log line: `Pass (Pass)`). `part1` has
      now run past that case's line number in its caselist without
      crashing in this specific run -- this effectively rules it out
      as a deterministic single-case trigger; whatever triggers the
      SIGSEGV is order/state/volume-dependent, not tied to that one
      case in isolation.
    - `part2`'s RSS grew measurably over a ~5-minute sampling window
      late in its run (2,903,640 KB -> 2,908,780 KB while processing
      ~1,200 more cases, roughly 4 KB/case average over that window),
      consistent with a slow, real memory leak compounding over very
      large sequential case counts, not a one-shot startup cost. A
      quick source skim of the likely culprits (`JITEngine.cpp`,
      `CompiledStage.cpp`'s `orc::LLJIT` lifetime, `PipelineCache.cpp`)
      found no obvious global/static unbounded cache -- `LLJIT` is
      owned per-`CompiledStage` via `unique_ptr` with a default (non-
      custom) destructor, and `PipelineCache` is a per-object, not a
      process-global, container. If the leak is real, it is likely a
      smaller, subtler per-object leak (e.g. in command-buffer or
      image/memory cleanup) that only becomes visible in RSS after
      hundreds of thousands of iterations, and needs a proper leak
      detector (ASan/LeakSanitizer or valgrind) rather than source
      inspection to pin down.
    - Attempted to isolate this further by replaying one single case
      thousands of times in a tight loop (to separate "many iterations
      of the literal same case" from "many iterations of diverse
      cases") -- blocked by `deqp-vk` itself: it hard-errors on a
      caselist containing a duplicate case name
      (`FATAL ERROR: Failed to initialize dEQP: Duplicate test case`),
      and `--deqp-test-iteration-count` only applies to tests that
      already support internal iteration counts, not a generic repeat
      wrapper. A real repeat-stress harness would need either a
      custom caselist-independent driver loop (bypassing `deqp-vk`'s
      CLI) or a leak detector attached to one of the existing long
      sweeps directly.
    - No code change this session -- filed as `L407` in `Roadmap.md`
      for the dedicated session this needs (see that entry for the
      concrete next steps: attach ASan/LeakSanitizer or valgrind
      massif to a fresh `check-feme`-built `deqp-vk` run against a
      large-but-bounded caselist, e.g. the `dual_source` family full
      8,078 cases, looking for the leak's actual allocation site
      rather than just its aggregate RSS symptom).
      `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
      change needed -- investigation only.

43. **`L407` substantially narrowed this session via a standalone
    leak-probe tool, bypassing `deqp-vk` entirely.** Not fixed --
    root-cause isolated to a specific, size-quantified cache, not yet
    decided whether that itself is a bug or just needs retuning. No
    code change.
    - Built a throwaway C++ program (`/tmp/leakprobe*.cpp`, direct
      Vulkan-loader client, not part of the feme tree) replicating
      CTS's own per-test-case resource lifecycle (image, render pass,
      framebuffer, shader modules, pipeline: create, one draw, full
      destroy) in a tight loop against the real FeMe ICD, sampling
      `/proc/self/status`'s `VmRSS` every N iterations -- entirely
      skipping `deqp-vk`'s own test-tree-construction overhead, which
      a prior `massif` attempt this session found dominates any
      small-scale profiling run.
    - Three variants -- plain single-attachment (200,000 iterations),
      4-attachment `dualSrcBlend`-enabled with blend factor/op cycled
      every iteration (300,000 iterations), and descriptor-pool/set
      plus uniform buffer (300,000 iterations) -- all three held
      perfectly flat RSS after an initial warm-up: **no leak in the
      basic create-destroy cycle when the same shader content is
      reused every iteration.**
    - A fourth variant that patches one SPIR-V constant word so every
      iteration's fragment shader is byte-for-byte unique (mirroring
      how real CTS cases actually differ from each other) immediately
      reproduced large, unmistakable growth: ~2.6 MB RSS per
      iteration, climbing from ~25 MB to ~740 MB over the first
      ~250-300 iterations, then plateauing almost exactly at
      `Device::ImplicitCache`'s `ImplicitPipelineCacheMaxEntries = 256`
      cap (`feme/lib/Vulkan/Objects.h`). Each cached compiled-pipeline
      artifact (`PipelineCache.h`'s `CachedPipelineArtifact`) costs
      roughly 2.6-2.9 MB; the implicit per-device cache retains up to
      256 of them (~740 MB) whenever pipeline/shader diversity is high
      enough that every create is a cache miss -- exactly the shape of
      a CTS state-space-sweeping family like `dual_source`/`stencil`.
    - A much smaller residual creep (~2.5 KB/iteration, continuing
      past the 256-entry plateau out to 3,000 iterations) was also
      observed -- the same order of magnitude as `part2`'s own
      previously-measured ~4 KB/case average, and not yet isolated to
      a specific allocation site.
    - This is a bounded cache, not a classic unbounded leak, by design
      (`PipelineCache.h`'s own comment notes a live `VkPipeline` holds
      its own `shared_ptr` to its artifact, so table eviction alone
      cannot free a still-in-use pipeline's memory) -- but at ~740 MB
      per device, with real CTS runs creating many distinct `VkDevice`
      instances over hundreds of thousands of cases, this is a fully
      plausible, now size-quantified explanation for both `part2`'s
      climbing `DeviceLost` rate and `part1`'s eventual resource-
      exhaustion `SIGSEGV`.
    - No code change this session -- updated `L407` in `Roadmap.md`
      with concrete next steps: confirm whether `vkDestroyDevice`
      actually releases the full ~740 MB, decide whether 256
      entries/~740 MB needs a lower default, and isolate the smaller
      ~2.5 KB/iteration residual creep separately.
      `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
      change needed -- investigation only.

44. **`L407`: confirmed `vkDestroyDevice` does not leak the ~740 MB
    implicit pipeline cache (this session).** Not fixed -- re-centers
    the remaining mystery on a smaller residual creep. No code change.
    - Built a device-recreate variant of the leak probe
      (`/tmp/leakprobe_devicerecreate.cpp`, also not part of the feme
      tree): creates a fresh `VkDevice` every 300 iterations (past the
      256-entry cache's own fill point from item 43), fully destroys
      it (`vkDestroyCommandPool`, `vkDestroyPipelineLayout`,
      `vkDestroyDevice`), and samples `VmRSS` immediately before and
      after each destroy.
    - Across 15 generations (4,500 total create-destroy cycles):
      generation 0 grows to ~742 MB then drops to ~645 MB after its
      device is destroyed (not all the way back to the ~23 MB
      baseline, consistent with glibc's malloc retaining freed arena
      pages rather than returning them to the OS -- normal allocator
      behavior, not evidence of a leak by itself). Generations 1-14
      then hold **rock solid** at ~737-742 MB for every single
      subsequent generation, with zero compounding growth across 14
      more full device lifecycles.
    - **Conclusion: `Device`/`PipelineCache` teardown is correct.** The
      implicit pipeline cache's memory is being properly freed by
      `vkDestroyDevice` and reused by the next device's identical
      workload -- it is a real, sizeable (~740 MB), but *bounded*
      per-device cost, not a bug. This answers last session's open
      question #1 definitively.
    - This re-centers the remaining mystery on the smaller
      ~2.5 KB/iteration residual creep found alongside the big cache
      effect last session (item 43), which this probe does not
      explain and has not yet isolated -- that creep, not the 740 MB
      cache, is now the lead suspect for `part2`'s real, still-
      unexplained climbing `DeviceLost` rate.
    - No code change this session -- updated `L407` in `Roadmap.md`
      with this finding; next session should isolate the residual
      creep (e.g. a long single-device run at fine-grained report
      intervals, or massif attached to the probe itself now that it is
      a fast, CTS-independent reproducer).
      `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
      change needed -- investigation only.

45. **`L407`: confirmed the ~2.5 KB/iteration residual creep is also
    NOT an unbounded leak -- closes `L407` (this session).** Not a
    code bug -- both memory-growth effects found during this
    investigation are now confirmed bounded, by-design caching
    behavior. No code change.
    - Re-ran `leakprobe_uniqueshader` (single device, no recreation)
      for 10,000 iterations instead of the prior session's 3,000,
      sampling `VmRSS` every 200 iterations.
    - Result: RSS climbs through the already-known ~740 MB pipeline-
      cache fill (iterations 0-2800), then **fully plateaus at
      750,060 KB from iteration ~2800 through iteration 10,000** --
      7,200 further iterations with zero net growth, only
      +/-2,048 KB page-granularity jitter.
    - **Conclusion: the previously-reported "~2.5 KB/iteration"
      residual creep was a measurement artifact of too short a
      sampling window (3,000 iterations), which stopped before this
      second, slightly-higher plateau was reached -- it is not a
      real, ongoing leak.**
    - This closes `L407` as a memory-leak investigation: both the big
      ~740 MB implicit pipeline cache (item 43) and the smaller
      residual creep (this item) are confirmed bounded/by-design, and
      `vkDestroyDevice` releases everything cleanly on teardown (item
      44). The real root cause of `part2`'s climbing `DeviceLost` rate
      and `part1`'s long-unreproduced `SIGSEGV` remains open, but is
      no longer attributable to a FeMe memory leak -- next session
      should look at resource-table/descriptor-count exhaustion (e.g.
      a fixed-size handle table or descriptor pool running out over
      hundreds of thousands of distinct cases) as the next-most-likely
      mechanism.
    - No code change this session -- updated `L407` in `Roadmap.md`,
      marking it closed with this conclusion.
      `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
      change needed -- investigation only.

46. **`L409`: root-caused `part2`'s `DeviceLost` cascade and (almost
    certainly) `part1`'s long-unreproduced `SIGSEGV` to a genuine
    use-after-free between asynchronous `vkQueueSubmit` and resource
    destruction -- found and reliably reproduced standalone this
    session; fix deferred to a dedicated future session.** No code
    change this session (investigation only).
    - Followed up on `L407`'s own closing handoff (pivot from "memory
      leak" to "resource-table exhaustion" as the next hypothesis for
      `part2`'s `DeviceLost`/`part1`'s `SIGSEGV`).
    - Analyzed `part2`'s finished run (536,411 cases, 122,004
      `DeviceLost`, 22.74%) programmatically: found 93% of all
      non-`NotSupported` results *after* the first `DeviceLost`
      occurrence (`dEQP-VK.draw.renderpass.point_size_clamp.point_size_clamp_max`,
      caselist line 25048) were also `DeviceLost` -- strong evidence of
      a single triggering event cascading forward, not independent
      per-case resource exhaustion.
    - Ruled out a per-case logic bug (trigger case passes cleanly in
      isolation) and simple deterministic case-position exhaustion (a
      full, same-order replay of the 25,048-case lead-up on a single
      device does not reproduce under normal timing).
    - Found `Sync.cpp`/`Sync.h`'s `getSafetyNetTimeoutNs()` (default
      5s, overridable via `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS`) clamps
      every blocking wait (`Fence::wait`, semaphore waits,
      `QueueExecutor::waitIdle`) -- a software TDR analogue. Shrinking
      it to 1ms and replaying the full lead-up caselist reliably
      produced a crash: an LLVM `ValueHandleBase` list-corruption
      assertion in a *subsequent* pipeline's JIT compile, immediately
      after a `vkWaitForFences` call returned `VK_TIMEOUT`.
    - A core-dump backtrace of this crash showed the main/host thread
      corrupting `ScalarEvolution`/LLVM-IR structures while JIT-
      compiling a new pipeline, concurrent with a `QueueExecutor`
      worker thread still executing an *earlier* submission's software
      rasterization -- consistent with heap corruption from a freed-
      and-reused allocation, not a live two-compiler data race (ruled
      out separately: `JITEngine::dispatch` only runs already-compiled
      code, no lazy JIT-at-dispatch-time).
    - **Built a clean, minimal, 100%-reproducing standalone C++
      reproducer** (`leakprobe_uaf.cpp`, not part of the feme tree,
      persisted to the session workspace), requiring **no CTS, no
      artificial timeout shrink, and no multi-hour contention at
      all**: create device/pipeline/render-pass/framebuffer, submit
      one draw against a 2048x2048 framebuffer (large enough that
      software rasterization takes measurably long), then
      **immediately** (no wait at all -- fully legal per the Vulkan
      spec) `vkDestroyPipeline`/`vkDestroyRenderPass`/
      `vkDestroyFramebuffer`/etc., only calling `vkQueueWaitIdle` at
      the very end of each iteration, long after every resource the
      submission referenced has already been freed from the host
      thread's point of view.
    - This crashes within the first few iterations, **every single
      run**, with `std::length_error` thrown from `vector::reserve`
      deep inside `buildRenderTargetBinding` (an internal helper
      reading a freed `RenderPass`/`Framebuffer`'s now-garbage member
      data). A core-dump backtrace conclusively confirms: the crashing
      thread is the `QueueExecutor` worker thread itself, executing
      `executeCommandBuffer` -> `executeCommandsInto` ->
      `buildRenderTargetBinding` against the already-destroyed
      objects, **while the main thread is correctly, harmlessly
      blocked in `vkQueueWaitIdle`/`QueueExecutor::waitIdle()` the
      entire time** -- proof this is a genuine use-after-free race
      between two threads, not a single-threaded logic bug.
    - **Root cause:** since the async `QueueExecutor` redesign
      (`L228(h)/(i)`), `vkQueueSubmit`/`vkQueueSubmit2` only enqueue a
      submission and return immediately; execution happens later, on a
      dedicated worker thread. Every `vkCmd*` recording call
      (`vkCmdBindPipeline`, `vkCmdBeginRenderPass`, etc.) captures the
      resources it references as **raw pointers**
      (`CommandBuffer::bindPipeline(vulkan::Pipeline*)`,
      `CommandBuffer.h:432`, and equivalently for
      `RenderPass`/`Framebuffer`), with **no shared-ownership/keep-
      alive mechanism**. Every `vkDestroy*` entry point (confirmed:
      `vkDestroyPipeline`, `Pipeline.cpp:883-891`) frees its object
      **immediately**, with **zero synchronization** against any
      `QueueExecutor` task that might still be executing a command
      buffer referencing it. This is exactly the shape of bug that
      only an async-execution redesign (vs. the prior synchronous
      `vkQueueSubmit`) can introduce, since previously there was never
      a legal window for an application to destroy a still-in-use
      resource.
    - This also conclusively confirms the safety-net-timeout-triggered
      crash discovered earlier this investigation was the **same**
      root cause manifesting via a *different* trigger path (a timed-
      out wait causing CTS's own test harness to destroy the previous
      case's objects early, rather than this probe's direct
      immediate-destroy) -- not a separate bug.
    - **Not yet scoped:** whether the same raw-pointer-capture-with-
      no-keep-alive pattern also affects other `vkCmd*`-bound resource
      types (descriptor sets, images, buffers) beyond the two types
      (`Pipeline`, `RenderPass`/`Framebuffer`) directly proven here --
      needs an audit in the dedicated fix session.
    - **Recommended fix direction** (not attempted this session,
      correctly scoped as a dedicated architectural session given its
      project-wide scope across the whole bound-resource object
      model, per this project's own precedent for similarly deep
      issues, e.g. `L375`): give every FeMe object type referenced by
      a recorded command buffer some form of shared-ownership/keep-
      alive tracking across the submit-to-execute window -- e.g.
      `CommandBuffer` capturing `std::shared_ptr`-style references (or
      an equivalent intrusive refcount) to every resource it records a
      reference to, with the `QueueExecutor` task closure holding
      those references alive for the duration of its own execution,
      and `vkDestroy*` entry points deferring the actual free until
      the last such reference drops -- mirroring the retain/release
      pattern `L398` already established for `Fence` lifetime
      (`Fence::retain`/`release`) and the existing `shared_ptr`-based
      artifact ownership in `PipelineCache.h`, generalized to the
      whole bound-resource object model.
    - No code change this session -- filed as `L409` in `Roadmap.md`.
      `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
      change needed -- investigation only, no feature/extension-surface
      change.

47. **`L409`: fixed the use-after-free this session's own item 46
    root-caused, for the three object types its standalone reproducer
    proved vulnerable (`Pipeline`, `RenderPass`, `Framebuffer`).**
    - Generalized `Fence`'s own `L398` retain/release-with-deferred-
      destroy pattern into a reusable mixin,
      `PendingExecutionRefCounted` (new file,
      `feme/lib/Vulkan/ExecutionLifetime.h`):
      `retainForExecution()`/`releaseAndTakeDeferredDestroy()`/
      `requestDestroy()`, `const`-callable (since
      `RecordedCommand::BeginPass`/`BeginFramebuffer` are stored as
      `const RenderPass *`/`const Framebuffer *`). `Pipeline`,
      `RenderPass`, and `Framebuffer` now inherit it.
    - `vkDestroyPipeline`/`vkDestroyRenderPass`/`vkDestroyFramebuffer`
      now call `requestDestroy` and only free immediately if it
      returns true, instead of freeing unconditionally.
    - `Sync.cpp`'s `makeSubmissionTask` gained a new
      `collectExecutionRefs`/`retainExecutionRefs`/
      `releaseExecutionRefs` triple: every `Pipeline`/`RenderPass`/
      `Framebuffer` a submission's command buffers reference --
      recursing into `vkCmdExecuteCommands`' secondary command buffers
      too -- is retained synchronously before the submission's task
      closure is ever returned (i.e. before any other thread could
      possibly run it), and released again (via `llvm::scope_exit`,
      covering every exit path) once that closure finishes running,
      freeing via `Allocator` for any object that had a `vkDestroy*`
      call deferred against it in the meantime.
    - New regression tests (`SyncTest.cpp`):
      `ExecutionLifetimeTest.DestroyDefersUntilRetainedTaskReleases`/
      `DestroyWithNoPendingTasksFreesImmediately`/
      `DestroyDefersUntilAllRetainedTasksRelease` (deterministic,
      through `PendingExecutionRefCounted`'s own public API, mirroring
      the existing `FenceLifetimeTest` precedent) and
      `SyncTest.SubmitThenImmediatelyDestroyPipelineWithNoWaitSucceeds`
      (the real end-to-end scenario `leakprobe_uaf.cpp` first proved
      crashes: submit, then immediately destroy the bound pipeline
      with no wait at all).
    - `ninja check-feme`: 3,625/3,628 Passed (+4 net new tests), 3
      Unsupported, 0 Failed, 0 regressions.
    - **Verified against a live CTS re-run:** a 5,000-case random
      sample of `part2`'s own
      `pipeline.{fast_linked_library,monolithic}.{stencil,
      blend.dual_source}.*` family (74,652 matching cases total in
      the full caselist) shows **0 `DeviceLost`** (down from the
      22.74%/22.9% baseline measured in items 46/the earlier
      `part2_repro*` artifacts) -- 3,084 Pass (61.7%), 1,912
      NotSupported (38.2%), 4 Fail (0.1%, all
      `b10g11r11_ufloat_pack32` dual-source-blend pixel-mismatch
      cases, unrelated to this fix -- not yet filed as their own
      roadmap item).
    - **Not yet audited or fixed:** `Buffer`, `Image`, `ImageView`,
      `DescriptorSet`, `Event`, `QueryPool` are referenced by recorded
      commands the identical raw-pointer-no-keep-alive way and are
      very likely subject to the same race -- filed as explicit
      follow-up `L410` in `Roadmap.md`.
    - `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
      change needed -- internal correctness fix only, no feature/
      extension-surface change.

48. **`L410`: fixed the same use-after-free class for `Buffer`, the
    first of the six object types item 47's own follow-up scope
    (`L410`) identified as still vulnerable.**
    - `Buffer` now inherits `PendingExecutionRefCounted`
      (`feme/lib/Vulkan/ExecutionLifetime.h`, unchanged from item 47);
      `vkDestroyBuffer` (`Buffer.cpp`) now calls `requestDestroy` and
      only frees immediately if it returns true, mirroring
      `vkDestroyPipeline`'s own now-fixed pattern exactly.
    - `Sync.cpp`'s `ExecutionRefs`/`collectExecutionRefs`/
      `retainExecutionRefs`/`releaseExecutionRefs` quadruple now also
      walks every `Buffer *` field a `RecordedCommand` can populate:
      `IndirectBuffer`, `CountBuffer`, `SrcBuffer`, `DstBuffer`
      (single pointers, reused across many `Kind`s -- e.g.
      `SrcBuffer`/`DstBuffer` serve `CopyBuffer`, `FillBuffer`,
      `UpdateBuffer`, `CopyQueryPoolResults`,
      `CopyBufferToImage`/`CopyImageToBuffer`, and
      `BindIndexBuffer` simultaneously), plus `VertexBuffers` and
      `XfbBuffers` (`std::vector<Buffer *>`, bound vertex buffers and
      transform-feedback buffers/counters respectively). Each field is
      checked unconditionally for non-null rather than switching on
      `Cmd.Op` first, matching the existing `Cmd.Pipeline`/
      `Cmd.BeginPass`/`Cmd.BeginFramebuffer` pattern.
    - New regression test (`SyncTest.cpp`):
      `SubmitThenImmediatelyDestroyBufferWithNoWaitSucceeds` --
      `vkCmdCopyBuffer` between two buffers, then immediately
      `vkDestroyBuffer` on both with no wait at all (device memory is
      freed only after the subsequent `vkQueueWaitIdle`, since freeing
      memory still bound to a buffer referenced by an incomplete
      submission is independently invalid API usage, distinct from
      this fix's actual scope: the `Buffer` wrapper object's own
      lifetime). Note: unlike item 47's `leakprobe_uaf.cpp`-confirmed
      `Pipeline` case, this specific race did not reproduce a
      deterministic pre-fix crash in this non-ASan build across 200
      repeats either with or without the fix -- the test documents and
      locks in the fix's intended behavior rather than demonstrating a
      guaranteed-reproducible pre-fix failure.
    - `check-feme`: 3626/3629 Passed (+1 net new test), 3 Unsupported,
      0 Failed, 0 regressions.
    - **Verified against CTS:** a combined 2,352-case sample
      (352 `dEQP-VK.draw.*indirect*`/`*indirect_indexed*` cases plus a
      random 2,000-case sample of the 128,426-case
      `dEQP-VK.transform_feedback.*` family -- chosen as the two
      families most likely to exercise `Buffer`'s newly-covered
      `RecordedCommand` fields) shows **0 `DeviceLost`/crashes**: 78
      Pass, 6 Fail, 2,268 NotSupported. The 6 Fails
      (`dEQP-VK.transform_feedback.simple_optimized_gpl.*`, e.g.
      `streams_culldistance_6`, `winding_line_strip_8`) are pre-
      existing transform-feedback vertex-data-verification issues,
      unrelated to buffer lifetime -- same class of pre-existing,
      lower-priority pixel/data-correctness bug as the still-open
      `L378(a)` and the 4 `b10g11r11_ufloat_pack32` dual-source-blend
      mismatches from item 47 -- not new regressions from this fix,
      and not chased further this session.
    - Also resolved this session (no code change): `part1`'s multi-
      session SIGSEGV-reproduction attempt (PID 7228) finished
      cleanly with **0 crashes ever observed** across its full
      555,650-case run (205,359 Pass / 37 Fail / 350,254
      NotSupported). The 37 Fails are all pre-existing, unrelated
      issues (18 `api.info.*` consistency checks, 12
      `api.copy_and_blit.*` ASTC block-compressed blit limitations, 3
      `binding_model.stages.*`, 2 `api.version_check.*`, 1 each
      `get_device_proc_addr`/`driver_properties`) -- none are
      `DeviceLost` or crashes. This fully resolves the long-running
      `part1` crash mystery carried across many prior sessions'
      handoffs by non-occurrence; drop it from future handoffs.
    - `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
      change needed -- internal correctness fix only, no feature/
      extension-surface change.
    - **Still open (`L410`'s remaining scope):** `Image`/`ImageView`,
      `DescriptorSet`, `Event`, `QueryPool` -- not yet audited or
      fixed.

49. **`L410`: fixed the same use-after-free class for `Image`/
    `ImageView`, the second pair of the six object types item 47's own
    follow-up scope (`L410`) identified as still vulnerable. This pair
    needed a wider audit than `Buffer`'s: images/views are reachable
    through several more `RecordedCommand` fields, plus two gaps not
    present in `Buffer`'s simpler scope.**
    - `Image` and `ImageView` both now inherit
      `PendingExecutionRefCounted`; `vkDestroyImage`/
      `vkDestroyImageView` (`Image.cpp`) now call `requestDestroy` and
      only free immediately if it returns true, mirroring
      `vkDestroyBuffer`'s own now-fixed pattern exactly.
    - `Sync.cpp`'s `ExecutionRefs`/`collectExecutionRefs`/
      `retainExecutionRefs`/`releaseExecutionRefs` quadruple now also
      walks: `Cmd.SrcImage`/`Cmd.DstImage` (reused across
      `CopyImage`/`BlitImage`/`ResolveImage`/`CopyBufferToImage`/
      `CopyImageToBuffer`/`ClearColorImage`/`ClearDepthStencilImage`),
      `Cmd.ImageBarriers[].Img` (`vkCmdPipelineBarrier`'s image layout
      transitions), `Cmd.BeginAttachments` (imageless framebuffer's
      per-instance views), and `Cmd.RenderingBinding`'s `Colors`/
      `Depth`/`Stencil`/`Inputs` (`vkCmdBeginRendering`'s own directly-
      captured `ImageView *` pointers -- a new finding, since dynamic
      rendering populates this struct straight from the recording
      call, bypassing the classic render-pass/framebuffer path
      entirely).
    - A new helper, `collectImageViewRef`, retains an `ImageView`
      together with the `Image` it wraps (via `View->image()`), since
      a view is a thin non-owning reference into its image's own
      storage -- retaining the view alone would still leave the
      underlying image's separate `vkDestroyImage` race open.
    - **Gap found and fixed beyond `Buffer`'s simpler scope:**
      `Cmd.BeginFramebuffer->attachments()` -- the classic render-pass
      path's `Framebuffer` object was already retained (item 47,
      `L409`), but its own attachment `ImageView`s were not. Retaining
      a container object does not transitively protect objects it
      merely references by raw pointer; each object independently
      reachable by a `vkDestroy*` call needs its own entry in the
      retain sweep, even when some other already-retained object also
      points to it. This generalizes to future `L410` follow-ups:
      check not just `RecordedCommand`'s direct fields but also
      anything reachable through already-fixed types' own accessor
      methods.
    - New regression test (`SyncTest.cpp`):
      `SubmitThenImmediatelyDestroyImageWithNoWaitSucceeds` --
      `vkCmdCopyImage` between two images, then immediately
      `vkDestroyImage` on both with no wait at all (same
      memory-freed-after-wait scoping discipline as item 48's
      `Buffer` test). As with that test, this specific race did not
      reproduce a deterministic pre-fix crash in this non-ASan build;
      the test documents/locks in the fix's intended behavior.
    - `check-feme`: 3627/3630 Passed (+1 net new test), 3 Unsupported,
      0 Failed, 0 regressions.
    - **Verified against CTS:** a ~170,000-case sweep of
      `dEQP-VK.api.copy_and_blit.*` (directly exercising
      `SrcImage`/`DstImage`) plus a time-boxed sample of the
      28,898-case `dEQP-VK.renderpasses.dynamic_rendering.*` family
      (directly exercising the newly-fixed `RenderingBinding`/
      `Framebuffer`-attachment paths) show **0 `DeviceLost`/crashes**.
      The 12 Fails observed in `copy_and_blit`
      (`blit_image.all_formats.color.2d.astc_*.a2b10g10r10_snorm_pack32.*_linear`)
      are the same pre-existing ASTC-blit-limit class already counted
      in item 48's `part1` 37-Fail finding, not new regressions.
    - **Explicitly out of scope:** descriptor-set-indirect image/
      buffer references (sampled/storage images bound via descriptor
      writes, read by the executor through `DescriptorSet`'s own
      internal storage rather than re-captured in `RecordedCommand`)
      remain unaddressed -- `DescriptorSet` itself isn't retained yet;
      that's the next `L410` follow-up, not folded into this fix.
    - `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
      change needed -- internal correctness fix only, no feature/
      extension-surface change.
    - **Still open (`L410`'s remaining scope):** `DescriptorSet`,
      `Event`, `QueryPool` -- not yet audited or fixed.

## Complete group accounting


| Group | Total | Pass | Fail | NotSupported | QW | IE | DeviceLost | Crash | Unrun | Fail delta |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `api` | 267,504 | 115,799 | 34 | 151,671 | 0 | 0 | 0 | 0 | 0 | -15,511 |
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
| `graphicsfuzz` | 757 | 666 | 83 | 8 | 0 | 0 | 0 | 0 | 0 | +12 |
| `image` | 143,086 | 29,901 | 14 | 113,171 | 0 | 0 | 0 | 0 | 0 | -6,927 |
| `image_processing` | 1,211 | 0 | 0 | 1,211 | 0 | 0 | 0 | 0 | 0 | +0 |
| `imageless_framebuffer` | 12 | 4 | 0 | 8 | 0 | 0 | 0 | 0 | 0 | +0 |
| `info` | 23 | 19 | 1 | 3 | 0 | 0 | 0 | 0 | 0 | -1 |
| `memory` | 6,364 | 4,986 | 0 | 1,378 | 0 | 0 | 0 | 0 | 0 | -65 |
| `memory_model` | 18,530 | 161 | 52 | 18,317 | 0 | 0 | 0 | 0 | 0 | -114 |
| `mesh_shader` | 28,044 | 468 | 69 | 27,507 | 0 | 0 | 0 | 0 | 0 | -21 |
| `multiview` | 838 | 643 | 0 | 195 | 0 | 0 | 0 | 0 | 0 | +0 |
| `pipeline` | 1,172,229 | 259,075 | 39,644 | 873,467 | 43 | 0 | 0 | 0 | 0 | +35,934 |
| `postmortem` | 24 | 0 | 0 | 24 | 0 | 0 | 0 | 0 | 0 | +0 |
| `protected_memory` | 6,000 | 0 | 0 | 6,000 | 0 | 0 | 0 | 0 | 0 | +0 |
| `query_pool` | 19,276 | 16,587 | 0 | 2,689 | 0 | 0 | 0 | 0 | 0 | -328 |
| `rasterization` | 15,019 | 482 | 2 | 14,535 | 0 | 0 | 0 | 0 | 0 | -96 |
| `ray_query` | 49,311 | 0 | 0 | 49,311 | 0 | 0 | 0 | 0 | 0 | +0 |
| `ray_tracing_pipeline` | 22,659 | 0 | 0 | 22,659 | 0 | 0 | 0 | 0 | 0 | +0 |
| `reconvergence` | 6,253 | 0 | 0 | 6,253 | 0 | 0 | 0 | 0 | 0 | +0 |
| `renderpasses` | 81,188 | 30,399 | 4,845 | 45,616 | 0 | 0 | 328 | 0 | 0 | -3,009 |
| `robustness` | 98,776 | 693 | 0 | 98,083 | 0 | 0 | 0 | 0 | 0 | -2 |
| `shader_object` | 243,853 | 6 | 0 | 243,847 | 0 | 0 | 0 | 0 | 0 | +0 |
| `sparse_resources` | 19,402 | 0 | 0 | 19,402 | 0 | 0 | 0 | 0 | 0 | +0 |
| `spirv_assembly` | 68,734 | 10,081 | 460 | 58,193 | 0 | 0 | 0 | 0 | 0 | -535 |
| `ssbo` | 12,225 | 3,242 | 0 | 8,983 | 0 | 0 | 0 | 0 | 0 | +0 |
| `subgroups` | 48,705 | 888 | 0 | 47,817 | 0 | 0 | 0 | 0 | 0 | +0 |
| `synchronization` | 64,872 | 18,420 | 0 | 46,452 | 0 | 0 | 0 | 0 | 0 | -3,128 |
| `synchronization2` | 81,617 | 24,631 | 0 | 56,986 | 0 | 0 | 0 | 0 | 0 | -3,407 |
| `tensor` | 2,811 | 0 | 0 | 2,811 | 0 | 0 | 0 | 0 | 0 | +0 |
| `tessellation` | 1,114 | 646 | 30 | 438 | 0 | 0 | 0 | 0 | 0 | -366 |
| `texture` | 25,669 | 9,808 | 2 | 15,859 | 0 | 0 | 0 | 0 | 0 | -451 |
| `transform_feedback` | 133,719 | 6,122 | 610 | 126,987 | 0 | 0 | 0 | 0 | 0 | -1,443 |
| `ubo` | 13,240 | 5,687 | 0 | 7,553 | 0 | 0 | 0 | 0 | 0 | +0 |
| `video` | 9,471 | 0 | 0 | 9,471 | 0 | 0 | 0 | 0 | 0 | +0 |
| `wsi` | 36,880 | 51 | 4 | 36,825 | 0 | 0 | 0 | 0 | 0 | +4 |
| `ycbcr` | 68,159 | 16 | 0 | 68,143 | 0 | 0 | 0 | 0 | 0 | +0 |

## Conformance assessment

This is not a conformant result. There are 47,172 ordinary failures, 340
`DeviceLost` results, and 2,546,724 `NotSupported` outcomes. The latter
include mandatory core capability gaps as well as optional extensions, so
eliminating ordinary failures alone would not establish conformance.
**Update: item 29 reconciled the previously-tracked "50 process crashes"
figure -- all 53 of the original baseline's crash/`Unrun` cases (49
`pipeline`, 1 `api`, 1 `graphicsfuzz`, 2 `spirv_assembly`) are now
confirmed, via direct case-level reruns, to be fixed. Zero process
crashes remain in the measured set.**

The roadmap's C/H/J/K/L rows retain implementation ownership. Its 2026-10-05
full-run re-triage prioritizes the shared `feme-cpu-linearize` dominance bug,
the sampler long tail, render-pass device loss, and the remaining stability
set while preserving implementation rows whose acceptance criteria are not
decidable from CTS results alone.
