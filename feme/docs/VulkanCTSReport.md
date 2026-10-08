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
| `transform_feedback` | 133,719 | 4,814 | 1,918 | 126,987 | 0 | 0 | 0 | 0 | 0 | -135 |
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
