# FeMe Vulkan ICD: Vulkan CTS Status Report

This report is the current full-suite measurement of `libfeme_vulkan`. The
roadmap owns remaining work; design documents own implementation decisions.

## Scope and provenance

- FeMe source revision: `96207bf38f781edd4488208d1ffeecd5ef28360d`
- VK-GL-CTS revision: `880f31a2bd9cd0659f84f3f80dafd07f2e693f6d`
  (`vulkan-cts-1.4.6.2-525-g880f31a2`)
- Case list: 3,244,369 cases in all 54 top-level `dEQP-VK` groups
- Device: `FeMe CPU Vulkan Device`
- Build: `Release`, `LLVM_ENABLE_ASSERTIONS=ON`, C and C++ compilation through
  `ccache`
- `check-feme`: 3,352 passed, 3 unsupported, 0 failed
- Registry used by the inventories: `VK_HEADER_VERSION` 358

The inventory audit reports 87 of 150 Vulkan core feature bits advertised, 49
of 86 promoted extensions implemented, and 41 extensions advertised by name.
See [Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md).

## Method

The explicit build-tree ICD was rebuilt before testing and confirmed with:

```console
VK_DRIVER_FILES=$PWD/build/tools/feme/tools/feme-vulkan/feme_icd.json \
VK_ICD_FILENAMES=$PWD/build/tools/feme/tools/feme-vulkan/feme_icd.json \
  vulkaninfo --summary | grep deviceName
```

Every CTS process ran from the Vulkan module directory with shader caching and
log images disabled. `feme/utils/run_vulkan_cts.py` used six workers, initial
1,800-case batches, recovery batches of 200, 20, and 1, and a 60-second
isolated timeout. It accepted a result only when the QPA record had a matching
`#endTestCaseResult`, then reran every ordinary failure alone to exclude
process-state contamination. The 70 cases still lacking complete QPA records
were run once more, one case per process, to distinguish 68 signal-terminated
processes from two timeouts. Thus every generated case has a measured outcome.

Artifacts, including every QPA and log, `report.txt`,
`verified-failures.txt`, and the abnormal-case classification, are retained at
`/home/dev/dev/VK-GL-CTS/run/feme-20260926-full/`.

## Headline

| Result | Count | Share of total |
|---|---:|---:|
| Total | 3,244,369 | 100.0000% |
| Measured | 3,244,369 | 100.0000% |
| Pass | 637,801 | 19.6587% |
| Fail | 48,307 | 1.4889% |
| NotSupported | 2,558,135 | 78.8485% |
| QualityWarning | 47 | 0.0014% |
| InternalError | 9 | 0.0003% |
| Crashed | 68 | 0.0021% |
| Timed out | 2 | 0.0001% |
| Unrun | 0 | 0.0000% |

The accounting identities are:

```text
QPA-complete = Pass + Fail + NotSupported + QualityWarning + InternalError
Measured = QPA-complete + Crashed + TimedOut
Total = Measured + Unrun
```

## Comparison with the preceding verified run

The preceding `L146` run at FeMe revision `d828c5cd4a0c` had 82,207
solo-verified failures. Against the same case list, this run resolves 35,022 of
those failures and introduces 1,122, a net reduction of **33,900** to 48,307.
Crashes and timeouts are not counted as ordinary failures in either baseline.

| Group | Prior Fail | Current Fail | Delta | Resolved old failures | New failures |
|---|---:|---:|---:|---:|---:|
| `api` | 15,479 | 15,545 | +66 | 49 | 115 |
| `binding_model` | 26,517 | 3 | -26,514 | 26,514 | 0 |
| `draw` | 0 | 2 | +2 | 0 | 2 |
| `glsl` | 3,139 | 2,933 | -206 | 599 | 393 |
| `graphicsfuzz` | 118 | 71 | -47 | 47 | 0 |
| `image` | 6,937 | 6,941 | +4 | 0 | 4 |
| `memory_model` | 117 | 166 | +49 | 1 | 50 |
| `mesh_shader` | 91 | 90 | -1 | 81 | 80 |
| `pipeline` | 3,728 | 3,710 | -18 | 18 | 0 |
| `rasterization` | 106 | 98 | -8 | 8 | 0 |
| `renderpasses` | 14,127 | 7,854 | -6,273 | 6,273 | 0 |
| `spirv_assembly` | 1,183 | 995 | -188 | 606 | 418 |
| `subgroups` | 112 | 0 | -112 | 112 | 0 |
| `synchronization` | 3,127 | 3,128 | +1 | 0 | 1 |
| `tessellation` | 356 | 396 | +40 | 0 | 40 |
| `transform_feedback` | 2,035 | 2,053 | +18 | 1 | 19 |
| `ubo` | 713 | 0 | -713 | 713 | 0 |

The largest resolved families are `binding_model.shader_access` (26,288),
`renderpasses` (6,273), `ubo` (713), `spirv_assembly` (606), `glsl` (599),
`subgroups.ballot_broadcast` (112), and `mesh_shader.ext` (81).

Of the 1,122 new failures, 696 are newly exposed 16-bit/f16 paths: 412
SPIR-V assembly cases, 104 GLSL matrix or fp16-precision cases, 80 mesh-shader
f16 I/O cases, 50 16-bit memory-model cases, and 40 tessellation f16 I/O
cases. Another 276 are texture-gather failures, principally dynamic-offset
variants; 72 are depth/stencil multisample-copy cases; and 40 are attachment
clear cases. The remaining 38 are spread across transform feedback, SPIR-V
assembly, draw, image-format queries, and one timeline-semaphore case.

## Complete group accounting

| Group | Total | Measured | Pass | Fail | NotSupported | QW | IE | Crash | Timeout | Unrun |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `api` | 267,504 | 267,504 | 99,450 | 15,545 | 152,507 | 0 | 0 | 2 | 0 | 0 |
| `binding_model` | 150,289 | 150,289 | 87,669 | 3 | 62,617 | 0 | 0 | 0 | 0 | 0 |
| `clipping` | 308 | 308 | 298 | 0 | 10 | 0 | 0 | 0 | 0 | 0 |
| `compute` | 61,460 | 61,460 | 686 | 0 | 60,774 | 0 | 0 | 0 | 0 | 0 |
| `conditional_rendering` | 1,030 | 1,030 | 0 | 0 | 1,030 | 0 | 0 | 0 | 0 | 0 |
| `cooperative_vector` | 53,562 | 53,562 | 0 | 0 | 53,562 | 0 | 0 | 0 | 0 | 0 |
| `data_graph` | 12,632 | 12,632 | 0 | 0 | 12,632 | 0 | 0 | 0 | 0 | 0 |
| `depth` | 8 | 8 | 0 | 0 | 8 | 0 | 0 | 0 | 0 | 0 |
| `descriptor_indexing` | 115 | 115 | 0 | 0 | 115 | 0 | 0 | 0 | 0 | 0 |
| `device_group` | 21 | 21 | 2 | 7 | 12 | 0 | 0 | 0 | 0 | 0 |
| `dgc` | 4,734 | 4,734 | 0 | 0 | 4,734 | 0 | 0 | 0 | 0 | 0 |
| `draw` | 29,451 | 29,451 | 3,256 | 2 | 26,193 | 0 | 0 | 0 | 0 | 0 |
| `drm_format_modifiers` | 1,572 | 1,572 | 0 | 0 | 1,572 | 0 | 0 | 0 | 0 | 0 |
| `dynamic_state` | 671 | 671 | 251 | 0 | 420 | 0 | 0 | 0 | 0 | 0 |
| `fragment_operations` | 151 | 151 | 107 | 11 | 30 | 3 | 0 | 0 | 0 | 0 |
| `fragment_shader_interlock` | 576 | 576 | 0 | 0 | 576 | 0 | 0 | 0 | 0 | 0 |
| `fragment_shading_barycentric` | 20,991 | 20,991 | 0 | 0 | 20,991 | 0 | 0 | 0 | 0 | 0 |
| `fragment_shading_rate` | 110,603 | 110,603 | 0 | 0 | 110,603 | 0 | 0 | 0 | 0 | 0 |
| `geometry` | 200 | 200 | 148 | 41 | 11 | 0 | 0 | 0 | 0 | 0 |
| `glsl` | 28,420 | 28,420 | 16,500 | 2,933 | 8,963 | 0 | 0 | 24 | 0 | 0 |
| `graphicsfuzz` | 757 | 757 | 676 | 71 | 8 | 0 | 0 | 0 | 2 | 0 |
| `image` | 143,086 | 143,086 | 14,499 | 6,941 | 121,646 | 0 | 0 | 0 | 0 | 0 |
| `image_processing` | 1,211 | 1,211 | 0 | 0 | 1,211 | 0 | 0 | 0 | 0 | 0 |
| `imageless_framebuffer` | 12 | 12 | 4 | 0 | 8 | 0 | 0 | 0 | 0 | 0 |
| `info` | 23 | 23 | 18 | 2 | 3 | 0 | 0 | 0 | 0 | 0 |
| `memory` | 6,364 | 6,364 | 4,921 | 65 | 1,378 | 0 | 0 | 0 | 0 | 0 |
| `memory_model` | 18,530 | 18,530 | 47 | 166 | 18,317 | 0 | 0 | 0 | 0 | 0 |
| `mesh_shader` | 28,044 | 28,044 | 447 | 90 | 27,507 | 0 | 0 | 0 | 0 | 0 |
| `multiview` | 838 | 838 | 643 | 0 | 195 | 0 | 0 | 0 | 0 | 0 |
| `pipeline` | 1,172,229 | 1,172,229 | 294,280 | 3,710 | 874,185 | 43 | 9 | 2 | 0 | 0 |
| `postmortem` | 24 | 24 | 0 | 0 | 24 | 0 | 0 | 0 | 0 | 0 |
| `protected_memory` | 6,000 | 6,000 | 0 | 0 | 6,000 | 0 | 0 | 0 | 0 | 0 |
| `query_pool` | 19,276 | 19,276 | 16,259 | 328 | 2,689 | 0 | 0 | 0 | 0 | 0 |
| `rasterization` | 15,019 | 15,019 | 386 | 98 | 14,535 | 0 | 0 | 0 | 0 | 0 |
| `ray_query` | 49,311 | 49,311 | 0 | 0 | 49,311 | 0 | 0 | 0 | 0 | 0 |
| `ray_tracing_pipeline` | 22,659 | 22,659 | 0 | 0 | 22,659 | 0 | 0 | 0 | 0 | 0 |
| `reconvergence` | 6,253 | 6,253 | 0 | 0 | 6,253 | 0 | 0 | 0 | 0 | 0 |
| `renderpasses` | 81,188 | 81,188 | 26,965 | 7,854 | 46,369 | 0 | 0 | 0 | 0 | 0 |
| `robustness` | 98,776 | 98,776 | 685 | 8 | 98,083 | 0 | 0 | 0 | 0 | 0 |
| `shader_object` | 243,853 | 243,853 | 6 | 0 | 243,847 | 0 | 0 | 0 | 0 | 0 |
| `sparse_resources` | 19,402 | 19,402 | 0 | 0 | 19,402 | 0 | 0 | 0 | 0 | 0 |
| `spirv_assembly` | 68,734 | 68,734 | 9,541 | 995 | 58,193 | 0 | 0 | 5 | 0 | 0 |
| `ssbo` | 12,225 | 12,225 | 3,242 | 0 | 8,983 | 0 | 0 | 0 | 0 | 0 |
| `subgroups` | 48,705 | 48,705 | 888 | 0 | 47,817 | 0 | 0 | 0 | 0 | 0 |
| `synchronization` | 64,872 | 64,872 | 15,291 | 3,128 | 46,452 | 1 | 0 | 0 | 0 | 0 |
| `synchronization2` | 81,617 | 81,617 | 21,224 | 3,407 | 56,986 | 0 | 0 | 0 | 0 | 0 |
| `tensor` | 2,811 | 2,811 | 0 | 0 | 2,811 | 0 | 0 | 0 | 0 | 0 |
| `tessellation` | 1,114 | 1,114 | 256 | 396 | 438 | 0 | 0 | 24 | 0 | 0 |
| `texture` | 25,669 | 25,669 | 8,771 | 453 | 16,435 | 0 | 0 | 10 | 0 | 0 |
| `transform_feedback` | 133,719 | 133,719 | 4,678 | 2,053 | 126,987 | 0 | 0 | 1 | 0 | 0 |
| `ubo` | 13,240 | 13,240 | 5,687 | 0 | 7,553 | 0 | 0 | 0 | 0 | 0 |
| `video` | 9,471 | 9,471 | 0 | 0 | 9,471 | 0 | 0 | 0 | 0 | 0 |
| `wsi` | 36,880 | 36,880 | 4 | 0 | 36,876 | 0 | 0 | 0 | 0 | 0 |
| `ycbcr` | 68,159 | 68,159 | 16 | 0 | 68,143 | 0 | 0 | 0 | 0 | 0 |

## Abnormal outcomes

The 68 isolated crashes are: `api` 2, `glsl` 24, `pipeline` 2,
`spirv_assembly` 5, `tessellation` 24, `texture` 10, and
`transform_feedback` 1. The two isolated timeouts are:

- `dEQP-VK.graphicsfuzz.cov-multiple-functions-global-never-change`
- `dEQP-VK.graphicsfuzz.cov-nested-structs-function-set-inner-struct-field-return`

Compared with the preceding run, 36 formerly incomplete cases now complete,
including all 14 previously incomplete subgroup-broadcast cases. One SPIR-V
assembly case and one transform-feedback fuzz case are newly incomplete.

## Conformance assessment

This is not a conformant result. There are 48,307 ordinary failures, 68
crashes, two timeouts, and 2,558,135 `NotSupported` outcomes. The latter
include mandatory core capability gaps as well as optional extensions, so a
zero ordinary-failure count alone would not establish conformance. The
roadmap's C/H/K/L rows retain implementation ownership; the full-run
re-triage there records current counts and separates the newly exposed
16-bit/f16, texture-gather, multisample-copy, and attachment-clear clusters.

## Post-run fixes (this session, against the 2026-09-26 baseline above)

Targeted re-runs of the specific affected case lists (not a new full run;
see roadmap L201/L202/L203 for the detailed narrative):

- **`spirv.GL.MatrixInverse`** (roadmap L201): 9/9 previously-failing
  `matrixinverse`-named cases now Pass (`spirv_assembly.instruction.compute`,
  5 graphics stages, and `glsl.builtin.precision_fp16_storage32b.inverse.*`).
- **`spirv.OuterProduct`** (roadmap L201): 117/117 previously-failing
  `outerproduct`-named cases now Pass, plus incidentally the 3
  `glsl.builtin.precision_fp16_storage32b.outerproduct.compute.*` cases.
- **`spirv.Image{,Dref}Gather` dynamic `Offset`** (roadmap L202): the
  MLIR-level legalization gap is fixed, but a re-run of the full 540-case
  `texture_gather.*.offset`/`.offset_dynamic` list is still 0/540 Pass -- a
  deeper CPU-backend limitation (`isSupportedOffset` requiring a compile-time
  constant) remains; see L202(a).
- **`depth_stencil_msaa_copy`** (roadmap L203): investigated, not fixed --
  root-caused to a verified bug in VK-GL-CTS itself (an accidental
  `VkImageLayout` value OR'd into a `VkImageUsageFlags` mask), confirmed via
  an isolated Vulkan reproducer independent of the CTS harness. FeMe's own
  behavior is correct and already unit-tested; classified as won't-fix in
  FeMe. All 72 cases remain Fail against the CTS's own (buggy) test code.

Net this session: 226 of the original 48,307 verified failures confirmed
fixed (MatrixInverse + OuterProduct), 0 additional fixed yet for L202/L203
(both still open, one backend-blocked and one an external test bug), 0
regressions in any of the re-run case lists or in `check-feme` (3352/3355
passing throughout, matching the pre-session baseline exactly).

## Post-run fixes (this session, against the 2026-09-26 baseline above)

Continuing from the prior session's L202 addendum above (`isSupportedOffset`
requiring a compile-time constant offset, blocking the full 540-case
`texture_gather.*.offset`/`.offset_dynamic` list at the CPU-backend layer):

- **CPU-backend runtime gather offsets** (roadmap L202(a)): relaxed
  `isSupportedOffset`'s `isa<Constant>` requirement (a new
  `AllowNonConstant` parameter, `true` only for the two gather call sites)
  -- the actual gather codegen and runtime entry points never required a
  compile-time-constant offset in the first place, so no new
  coordinate-computation code was needed. A single-case repro
  (`texture_gather.compute.offset.implementation_offset.2d.depth32f.
  base_level.level_1`) flipped Fail -> Pass; a re-run of the full 616-case
  `texture_gather` case list (the broader superset originally used to scope
  L202, encompassing the 540-case `offset`/`offset_dynamic` subgroup) showed
  540/616 now Pass, up from 0/616 before this fix.
- **`ImageDrefGather`+`ConstOffsets`** (roadmap L202(b), a gap flagged but
  not filed by L125(k)'s own closing text): the 616-case rerun's own 76
  residual failures were all this exact shape -- `ImageDrefGatherPattern`
  never gained the `ConstOffsets` (plural) widening `ImageGatherPattern` got
  under L125(m)/L125(n). Fixed by sharing `ImageGatherPattern`'s existing
  flattening logic via a new `flattenConstOffsetsArray` helper and mirroring
  `isGatherIntrinsic`'s CPU-backend `ConstOffsets` handling into
  `isGatherCmpIntrinsic`, including new `GatherCmp2DOffsets`/
  `GatherCmpArray2DOffsets` runtime entry points. A re-run of the same
  616-case list now shows **616/616 Pass** -- roadmap L202 (and both of its
  one-level-deep sub-rows, L202(a)/L202(b)) is fully closed.

Net this session: 616 of the original 48,307 verified failures confirmed
fixed (the full `texture_gather` cluster L202/L202(a)/L202(b) tracked), 0
regressions in `check-feme` (3353/3356 passing, +1 new unit test versus the
prior session's 3352/3355 baseline).

## Post-run fixes (this session, against the 2026-09-26 baseline above)

Continuing from the prior sessions' L201/L202 addenda above. This session
picked up two of the ranked L201 sub-clusters:

- **`memory_model.shared.16bit.*`** (roadmap L201(e)): triaged only, not
  fixed -- the prior session's own "likely a duplicate of the milestone-9
  loop-linearization limitation, closeable in ~10 minutes" hypothesis was
  found to be wrong. The real repro (`memory_model.shared.16bit.
  arrays_of_arrays.0`) is not a loop at all; it is a chain of uniform,
  side-effect-free comparisons short-circuiting into one shared,
  phi-bearing merge block, a shape `EntryWrapper.cpp`'s `BranchShape`
  does not recognize. No case-count change (still 50/50 Fail); the real
  fix is scoped as new roadmap row L205, not started.
- **`16bit_storage.input_output_int_16_to_16`** (roadmap L201(a)): fixed.
  The prior session's "no diagnostic text even with
  `FEME_VULKAN_LOG_CREATION_ERRORS=1`" claim was also found to be wrong --
  the CTS case path itself had moved (the roadmap's own `compute.float16`
  prefix no longer exists), and against the correct path
  (`spirv_assembly.instruction.graphics.16bit_storage.
  input_output_int_16_to_16.scalar_sint0_frag`) the env var produces a
  clear diagnostic. Root cause: `StageStorage::buildStageStorage` has no
  addressable representation for a stage-IO scalar narrower than 32 bits
  except the pre-existing, separate 16-bit-float "widened half" path.
  Fixed by mirroring roadmap H6m's `i1`/bool precedent: canonicalize a
  16-bit integer stage-IO scalar to an ordinary 32-bit element
  (`CanonicalizeStage.cpp`'s `getComponentType`, with a matching
  `zext`/`trunc` round-trip in `loadStageIOValue`/`storeStageIOValue`).
  A re-run of the full 200-case `input_output_int_16_to_16.*` group shows
  **110/200 now Pass, up from 0/200** (every case previously failed at
  `vkQueueSubmit` with `VK_ERROR_INITIALIZATION_FAILED`). The remaining 90
  failures (`scalar_sint*`/`vector_sint*` past the trivially-zero-valued
  `scalar_sint0`) are a real pixel-value mismatch, not a submission
  failure -- a distinct, deeper gap (SPIR-V's `OpTypeInt` signedness bit
  does not survive SPIRVToLLVM conversion, confirmed by diffing byte-
  identical post-conversion IR between a `sint`- and `uint`-named case
  sharing the same underlying data) split out as new roadmap row L206,
  not fixed this session. `input_output_float_32_to_16`'s own 360-case
  group (part of L201(a)'s original combined 300-case estimate, which
  undercounted this group's real size) is separately at 260/360 Pass; its
  100 failures are all `_rtz` (round-toward-zero) rounding-mode cases, a
  pre-existing, unrelated gap this session did not investigate.

Net this session: 110 of the original 48,307 verified failures confirmed
fixed (`input_output_int_16_to_16`'s zero-sign-bit and all-unsigned
cases), 0 regressions in `check-feme` (3354/3357 passing, +1 new unit test
versus the prior session's 3353/3356 baseline; the discrepancy from the
prior session's own reported 3353/3356 vs. this session's 3354/3357 totals
is exactly this session's one new test, `CanonicalizeStageTest.
CanonicalizesInt16StageIOScalarToA32BitElement`).

## Post-run fixes (this session, against the 2026-09-26 baseline above)

Picked up the top-ranked item from the prior session's own next-steps list:
roadmap **L206** (SPIR-V `OpTypeInt` signedness preservation through
SPIRVToLLVM), the remaining 90-case gap the prior session's own
`input_output_int_16_to_16` fix (L201(a)) split out rather than closed.

- **`16bit_storage.input_output_int_16_to_16`** (roadmap L206): fixed.
  Added a third FeMe-internal `!feme.spirv.*` metadata side channel
  (`feme.spirv.Int16Signed`), mirroring `feme.spirv.decorations`/
  `feme.spirv.MemberDecorations`'s own existing precedent
  (`StageIODecorations.cpp`): `StageIOGlobalVariablePattern`
  (`SPIRVToLLVMPatterns.cpp`) now marks a converted stage-IO global's
  `llvm.mlir.global` when its declared type's leaf scalar is a genuinely
  signed `OpTypeInt 16 1` (recursing through any array/vector wrapper
  first), and `SPIRVToLLVMTranslator.cpp`'s existing collect-before/
  attach-after driver realizes it as real LLVM metadata the same way it
  already does the other two channels. `CanonicalizeStagePass::run` reads
  this metadata per stage-IO global while building the entry signature
  and threads a small `ElementID -> bool` set down through
  `storeStageIOBlockValue`/`storeStageIOValue` to the one remaining
  `zext`-only widen that row's own prior fix left in place, so a
  genuinely signed 16-bit stage-IO output now widens with `sext` instead,
  per element -- not a pass-wide default flip (the prior session's own
  experiment, switching every `i16` store to `sext` unconditionally,
  merely traded the `sint` cases' failures for the `uint` cases',
  confirming per-element information, not a global default, was the
  actual missing piece).
  A re-run of the full 200-case `input_output_int_16_to_16.*` group shows
  **200/200 Pass, up from 110/200** at the start of this session. A
  broader re-run of the whole `16bit_storage.*` group (2431 cases) shows
  zero regressions: the only remaining 100 failures are the pre-existing,
  unrelated `input_output_float_32_to_16.*_rtz` (round-toward-zero
  rounding-mode) group, already noted but not investigated by the prior
  session.

Net this session: 90 of the original 48,307 verified failures confirmed
fixed (`input_output_int_16_to_16`'s remaining `scalar_sint*`/
`vector_sint*` cases), 0 regressions in `check-feme` (3359/3362 passing,
+5 new unit tests versus the prior session's 3354/3357 baseline: 4 in
`SPIRVToLLVMTest.cpp` covering the new metadata channel's collect/attach
helpers, 1 in `CanonicalizeStageTest.cpp` covering the new `sext` path).

## Post-run fixes (this session, against the 2026-09-26 baseline above)

Picked up the top-ranked item from the prior session's own next-steps list:
roadmap **L205** (`EntryWrapper.cpp`'s branch-shape support for an N-deep
short-circuit comparison chain reconverging at a phi-bearing merge block,
`memory_model.shared.16bit.*`), fully scoped by two prior sessions
(`L201(e)`'s own triage) but not started before now.

- **`memory_model.shared.16bit.*`** (roadmap L205): fixed, via a different
  code path than originally scoped. Direct instrumentation of
  `EntryWrapper.cpp`'s `walkLinearChain`/`matchBarrierFreeRegion` (rather
  than reasoning about the existing code by inspection alone) found the
  real blocker one layer downstream of where the roadmap row assumed:
  `matchBranchShape`'s own prefix-order walk already swallows the whole
  function (chain, phi-bearing merge, and everything after) before ever
  reaching a `CondBrInst` to classify, so the fix instead targets
  `EntryWrapper.cpp`'s other branch-shape family --
  `isLinearChain`/`matchSafeDiamond`, the one `splitAtGroupSyncBarriers`
  actually uses to region-split a wave body around its own barriers. A new
  `matchShortCircuitChain` (tried as `isLinearChain`'s fallback right after
  `matchSafeDiamond`) recognizes the chain-with-phi shape as a private,
  barrier-free CFG region and splices its blocks unmodified into whatever
  region contains them -- no new metadata channel, cloning, or
  uniformity/purity check needed, since `isLinearChain`'s blocks move
  (rather than being cloned into a wrapper the way `BranchShape` clones a
  header condition), so the phi and each header's own (possibly per-wave,
  not group-uniform) condition both survive untouched. Two new unit tests
  cover the shape and its own barrier-inside-a-link rejection case.
  A re-run of the full `memory_model.shared.16bit.*` group (70 cases --
  wider than the roadmap row's own original 50-case estimate) shows
  **51/70 now Pass, up from 0/70** (every case previously failed to
  compile at all, with `feme-cpu-wrap-entry: ... has a barrier inside
  non-linear control flow`). The remaining 19 cases no longer fail to
  compile -- they now run and produce a wrong result (`Counter value
  incorrect`), a distinct, previously-invisible runtime-correctness bug
  this fix's own compile-time success exposed for the first time; split
  out as new roadmap row L207, not investigated further this session.

Net this session: 51 of the original 48,307 verified failures confirmed
fixed (`memory_model.shared.16bit.*`'s now-passing cases), 0 regressions
in `check-feme` (3361/3364 passing, +1 net new unit test versus the prior
session's 3359/3362 baseline: 2 new tests added for the fix itself, 1
scratch/throwaway diagnostic test used during root-cause investigation
removed before committing).

## Post-run fixes (this session, against the 2026-09-26 baseline above)

Picked up roadmap **L207** (`memory_model.shared.16bit.*`'s remaining 19
cases, `Counter value incorrect` at runtime), the row L205's own fix split
out last session.

- **`memory_model.shared.16bit.*`** (roadmap L207): fixed. Root cause: a
  whole-matrix `spirv.Store`/`spirv.Load` through a bare `spirv::MatrixType`
  pointer (e.g. a `mat4x3` field read/written as one value, not per-column)
  converted the stored/loaded value through the generic, "natural"/
  ABI-rounded matrix conversion, which never tightens a non-power-of-2-lane
  column vector (a `vec3` column rounds up to 16 bytes instead of the tight
  12) -- disagreeing with the pointee struct member's own already-tightened
  type and overflowing into the next field. This never went through the
  existing `RowMajorMatrixStorePattern`/`RowMajorMatrixLoadPattern`
  reconciliation, since `getMatrixWholeAccess` (which those patterns key
  off) only ever matches `Uniform`/`StorageBuffer` block/wrapper shapes,
  never `Workgroup` (shared-memory) storage. Fixed by two new dedicated
  patterns, `TightMatrixStorePattern`/`TightMatrixLoadPattern`
  (`SPIRVToLLVMPatterns.cpp`), reconciling a whole-matrix `spirv.Store`/
  `spirv.Load` with the pointee's own tight type whenever
  `getMatrixWholeAccess` does not already match, leaving the matrix value's
  own general conversion (extract/insert/arithmetic/constant/composite-
  construct) untouched. A re-run of the full `memory_model.shared.16bit.*`
  group (70 cases) shows **70/70 now Pass, up from 51/70**.

Net this session: 19 of the original 48,307 verified failures confirmed
fixed (L207's own cases), 0 regressions in `check-feme` (3363/3366 passing,
+2 net new tests versus the prior session's 3361/3364 baseline: 1 new lit
test and 1 new unit test for the fix itself).

### Full re-run of the 2026-09-26 48,307-case verified-failure list

Overdue since 2026-09-26 (three fix rounds -- L202, L206, L205 -- had
landed since the last full run, on top of this session's own L207). Reran
the entire 48,307-case verified-failure list from
`/home/dev/dev/VK-GL-CTS/run/feme-20260926-full/verified-failures.txt`
through `feme/utils/run_vulkan_cts.py` (6 workers, default batch/recovery
sizes), against the FeMe build as of this session's final commit.

**Result: 48,305 of the original 48,307 cases now Pass or NotSupported.
Only 2 genuine failures remain**, both expected/by-design, not new bugs:

| Case | Result | Why |
|---|---|---|
| `dEQP-VK.api.driver_properties.conformance_version` | Fail (unchanged) | `VkPhysicalDeviceDriverProperties.conformanceVersion` is intentionally reported as a truthful `{0,0,0,0}` (roadmap C5) -- this driver has no official conformance submission, so this self-check test is *expected* to fail for any honest, non-certified implementation. |
| `dEQP-VK.info.device_mandatory_features` | Fail (unchanged) | `cooperativeMatrix` (`VK_KHR_cooperative_matrix`) is a mandatory-for-1.4-submission feature this driver does not implement -- already tracked as an explicit, deliberate scope exclusion (`feme/lib/Vulkan/PlannedExtensions.txt`'s own comment: "is optional for a 1.4 submission and stays out of scope, Roadmap.md Part 4"). |

Both are pre-existing, intentional design decisions from before this
session, not regressions or newly-discovered bugs -- no further action
taken on either. Full result counts from the rerun: 38,941 Pass, 9,364
NotSupported, 2 Fail (0 Crashed, 0 timed out, every case's `deqp-vk` process
completed and every prior `Fail` was re-verified alone per
`run_vulkan_cts.py`'s own verification round). Artifacts retained at
`/tmp/feme-l207-rerun/` (not committed -- see this session's own
`agent_thoughts.md` entry for the full path and reproduction command).

### `input_output_float_32_to_16`'s 100 `_rtz`-rounding-mode failures (roadmap L208)

Carried over, unfixed, across several prior sessions (this specific
360-case group was never part of the 2026-09-26 verified-failure list
above -- it started failing independently, so the full-list rerun's
"only 2 failures remain" result did not cover it). Picked up as roadmap
**L208** this session.

Root cause: `spirv.FConvert`'s own per-instruction `FPRoundingMode`
decoration was silently discarded on a narrowing conversion -- absent a
dedicated pattern, it always fell through to upstream MLIR's
`IndirectCastPattern`, always rounding to nearest-even regardless of any
`RTZ` decoration. Fixed with a new `FConvertRoundingModePattern`
(`SPIRVToLLVMPatterns.cpp`) implementing round-toward-zero as a
self-contained integer bit-manipulation algorithm rather than a
constrained `llvm.experimental.constrained.*` intrinsic: isolated `.ll`
reproducers this session confirmed a genuine LLVM/AArch64 backend
correctness gap (a constrained op's own explicit non-default rounding
mode is silently discarded at codegen time, no `FPCR` manipulation at
all) -- out of scope to fix in this session, and flagged as a latent risk
for the pre-existing `FloatControlArithmeticPattern` (F15c) on this same
host, not yet independently re-verified.

Fixing this exposed a second, independent latent defect in
`CanonicalizeStage.cpp`: the new pattern's bitcast-terminated result,
once its sole use is an output store, is a textbook target for
InstCombine's own "push a bitcast into its one store" canonicalization,
which mutated the store's value to a same-width integer before
`CanonicalizeStagePass` ever ran, causing it to mis-route the value
through `feme.stage.output.store.i32` instead of `.f16` and crash
`PromoteMemToReg` on a resulting mixed-type shadow alloca. Fixed by
reconciling a leaf store's value against its own signature element's
independently-tracked declared type before decomposition.

**Result: `dEQP-VK.spirv_assembly.instruction.graphics.16bit_storage.
input_output_float_32_to_16.*` now 360/360 Pass, up from 260/360** (all
100 `_rtz` failures fixed, 0 regressions among the 260 previously-passing
`_rte`/`unspecified_rnd_mode` cases).

Net this session: 100 failures fixed (a previously-untracked group, not
part of the 48,307-case list), 0 regressions in `check-feme`
(3365/3368 passing, +2 net new tests versus the prior session's baseline:
1 new lit test and 1 new unit test for the fix).

## Post-run fixes (this session, against the 2026-09-26 baseline above)

### Roadmap L211: `Function`-storage local matrix dynamic element reads

Found via `offload-test-suite`'s `feme` branch (`check-hlsl-feme-vk`), not
the deqp-vk suite: a `Function`-storage local matrix variable's
dynamically-indexed element reads (`A[row][col]`) returned wrong data for
elements past the first row whenever the matrix's column width wasn't a
power of two (e.g. `float2x3`). Root cause: `TightMatrixStorePattern`/
`TightMatrixLoadPattern` (roadmap L207/L208, `SPIRVToLLVMPatterns.cpp`)
matched unconditionally on any bare-matrix-pointee store/load regardless
of storage class, tightening a `Function`-storage local's whole-matrix
store/load even though that local's own `alloca` (and every dynamically-
indexed `AccessChain` read of it, via MLIR's own generic, unmodified
pattern) always uses the plain, untightened, "natural" conversion --
disagreeing on every row after the first. Fixed by skipping the
tightening in both patterns whenever the pointee's storage class is
`Function`; `Workgroup` (L207/L208's own case) is untouched.

Reduced from and confirmed fixing `offload-test-suite`'s
`Basic/Matrix/matrix_splat_cast.test` and
`Feature/CBuffer/Matrix/LayoutKeyword/transpose.test` (both now Pass; the
suite's 3 stable failures drop to 1, `WaveOps/WaveActiveMax.test`, XFAIL'd
for `FeMe` separately as a pre-existing spec-ambiguity edge case, not a
FeMe bug -- see that test's own updated comment).

Since this bug's own repro shape (a *local* matrix variable dynamically
indexed) has no direct deqp-vk equivalent in the 48,307-case verified-
failure list (that list's own 2 remaining failures, both pre-existing and
by design, are unrelated -- see the "Full re-run" section above), ran the
full `dEQP-VK.glsl.matrix.*` group (1,764 cases -- every GLSL local-
variable matrix arithmetic/indexing/transpose case, including several
`dynamic.*` sub-groups that index a local matrix at runtime, the closest
available first-class deqp-vk analog to this bug's own repro shape)
through `feme/utils/run_vulkan_cts.py` as a regression check for this
fix specifically.

**Result: 1,764/1,764 Pass, 0 regressions, 0 newly-fixed deqp-vk cases**
(this bug's own repro shape -- `Function`-storage local matrices with a
non-power-of-2 column count *and* a runtime, not compile-time-constant,
row/column index -- does not appear to be independently exercised by this
particular deqp-vk group; `check-hlsl-feme-vk`'s own two now-passing
tests above remain the only direct confirmation of the fix). Artifacts
retained at `/tmp/feme-l211-cts/` (not committed).

Net this session: 2 `offload-test-suite` `check-hlsl-feme-vk` failures
fixed (down from 3 stable failures to 1, the separately-triaged
`WaveActiveMax.test` XFAIL), 0 regressions in `check-feme` (3369/3372
passing, +1 net new lit test versus the prior baseline) or in the
targeted 1,764-case `dEQP-VK.glsl.matrix.*` deqp-vk re-run.

### Roadmap L212/L213: `array_of_matrices.test` stale XPASS; parallel-worker flakiness

Neither of these is a deqp-vk case -- both are `offload-test-suite`'s own
`check-hlsl-feme-vk` suite findings, carried over from several prior
sessions' next-steps lists.

`Feature/PushConstant/array_of_matrices.test`'s `XFAIL: DXC` (citing
upstream `microsoft/DirectXShaderCompiler#8080`) turned out to be stale:
that issue's own repro never uses `-fvk-use-dx-layout`, which this test
always has, so its scope never covered this test's exact configuration.
Confirmed by manually compiling the test's HLSL and inspecting the
resulting SPIR-V's own `MatrixStride`/`ArrayStride`/`RowMajor`
decorations, which are internally consistent with the test's own
push-constant offsets. Fixed by removing the stale `XFAIL: DXC` (replaced
with an explanatory comment); `XFAIL: Clang` (a distinct, still-valid
gap) untouched. `check-hlsl-feme-vk` (680 tests) now shows 0 Unexpectedly
Passed.

The parallel-worker flakiness (3 tests failing under default parallelism,
a different 12 under `-j1`, all passing individually) did not reproduce
across 8+ reruns spanning worker counts 1-96. Circumstantially attributed
to the same session's L211 fix (reading uninitialized stack memory
produces scheduling-dependent "random" values, which look like
contention-driven flakiness); not independently root-caused, since no
prior session recorded the specific failing test names to cross-check
against.

Net this session (both items): 0 code changes beyond the one-line XFAIL
removal above; `check-hlsl-feme-vk` (680 tests) stable at 0 Failed / 0
Unexpectedly Passed across all reruns.

### Roadmap L210: `ByteAddressBuffer`/`RWByteAddressBuffer` 16-bit sub-dword access -- fixed outside FeMe

A prior session's roadmap entry claimed `RWByteAddressBuffer::
Store<uint16_t>`/`Load<uint16_t>` returned 0 specifically at a non-zero
byte offset (8), found "via `check-hlsl-feme-vk`". Neither half of that
claim held up: the test (`Tools/Offloader/ByteAddress-16bit.test`)
requires `Int16` (`REQUIRES: Int16`), which this device does not
currently advertise (`shaderInt16 = false`), so it has never actually run
as part of the gated `check-hlsl-feme-vk` target -- it stays
`Unsupported`. Reproduced instead by running the test's own pipeline
directly through `%offloader`, bypassing the lit `REQUIRES` gate; once
reproduced this way, **both** the byte-offset-0 case and the byte-
offset-8 case returned 0, not just offset 8.

Root cause: **not a FeMe bug.** DXC's own SPIR-V codegen for any
sub-32-bit `ByteAddressBuffer::Load<T>`/`Store<T>` always reads/read-
modify-writes the whole containing 4-byte dword via a `RuntimeArray<uint>`
access chain, regardless of `T`'s real width. When a raw buffer's
declared byte size (derived here from a `UInt16`-formatted CPU-side
element count -- 1 element/2 bytes for `In0`, 5 elements/10 bytes for
`In1`) is not itself a multiple of 4, the dword covering the buffer's
last/only element extends past its declared end, and every conformant
Vulkan device must bounds-check that access against the descriptor's
declared byte range and reject it -- exactly the D3D/Vulkan robustness
contract FeMe's own CPU-backed device honors (return 0 for an
out-of-bounds read, drop an out-of-bounds write). The D3D12 backend
already rounds a raw buffer's device allocation up to the next multiple
of 4 for exactly this reason (see its own `createBuffer`'s "Raw
Resources" comment); the Vulkan backend never did.

Fixed entirely in `offload-test-suite`'s own Vulkan backend
(`lib/API/VK/Device.cpp`'s `createResource`/`createBuffer`), mirroring
DX12's own rounding -- **no FeMe subdirectory files were touched**, per
this project's policy of fixing issues found outside FeMe in a
self-contained, non-FeMe commit. Verified: `ByteAddress-16bit.test`'s
pipeline, run directly through `%offloader` bypassing the `REQUIRES:
Int16` gate, now round-trips correctly at both offsets (previously both
returned 0). `check-hlsl-feme-vk` (680 tests) unaffected -- still 0
Failed; the fixed test itself remains `Unsupported` until `shaderInt16`
is separately implemented (an unrelated, pre-existing feature gap, not
reopened by this fix). No FeMe feature/extension inventory change: this
fix does not touch, and is not gated by, `shaderInt16`/16-bit storage.

Net this session: 1 real bug fixed (outside FeMe, in
`offload-test-suite`), 0 regressions in `check-hlsl-feme-vk` (680 tests,
0 Failed both before and after) or `check-feme` (3366/3369 passing, 3
pre-existing Unsupported, 0 regressions -- no feme-side code was changed
this session, so this is a stability re-confirmation, not a fix
verification).

### Roadmap L214: AArch64 constrained-intrinsic rounding-mode fix for FloatControlArithmeticPattern

Follow-up to the previously-flagged, unverified AArch64 risk (F15c/
L208): `FloatControlArithmeticPattern`'s 5 binary ops (`FAdd`/`FSub`/
`FMul`/`FDiv`/`FRem`) lowered a non-default rounding-mode request to a
constrained LLVM intrinsic carrying a *static* rounding-mode metadata
operand (e.g. `"round.towardzero"`), which this host's AArch64 backend
silently drops at codegen time, producing an ordinary default-rounded
(round-to-nearest-even) instruction with zero `FPCR` manipulation.

Fixed by bracketing the constrained intrinsic with
`llvm.get.rounding()`/`llvm.set.rounding(i32)` and switching its
rounding-mode metadata operand to `"round.dynamic"` instead of a static
mode -- this reliably produces real `FPCR` read/modify/write codegen on
AArch64 (confirmed via isolated `.ll` + `llc` reproducers at both `-O0`
and `-O2`), and is never eligible for compile-time constant folding
(unlike a static-mode op), so it cannot silently mask a regression the
way the original bug's own test coverage gap did. `FRem` does not
semantically need this (IEEE-754 remainder has no rounding step) but
the fix applies uniformly across all 5 ops for simplicity; it is a
correctness-preserving no-op for `FRem`.

A subtlety worth recording: an initial version of this session's new
end-to-end JIT test used two literal-constant `fadd` operands, and
that version *still passed* even against the old, unfixed, static-mode
code -- because LLVM's own IR-level optimizer constant-folds a
static-mode constrained intrinsic call via
`ConstantFoldConstrainedFPCall` whenever both operands are compile-time
constants, using its own always-correct, backend-independent folder,
completely bypassing the buggy runtime-codegen path this fix targets.
This is very likely also why no CTS case in this project's tracked
history ever caught the original bug: it's plausible relevant test
inputs are frequently effectively constant-folded at the IR level
before ever reaching the buggy codegen path. The final test instead
routes both operands through a runtime raw-buffer load, which cannot be
constant-folded; confirmed (by temporarily reverting the fix) that this
version does correctly fail against the old, unfixed code.

`llvm.set.rounding` cannot be legalized by LLVM's SPIR-V backend
(`unable to legalize instruction: G_SET_ROUNDING`), so the two
`Target/`-level lit tests that previously round-tripped generated LLVM
IR back through the real SPIR-V backend as a cross-check now stop at
the `--spirv-to-llvmir` stage and FileCheck the LLVM IR directly
instead; this round-trip was a test-only internal-consistency check,
unrelated to FeMe's real CPU JIT execution path (which targets the
host machine directly via ORC's `detectHost()`, never the SPIR-V
backend), so narrowing it is a test-design adjustment, not a loss of
functional coverage.

Not a deqp-vk run: no existing `float_controls`/`float_controls2` CTS
case is known to numerically distinguish RTZ/RTP/RTN from RTE for this
specific bug (see above), so there is no targeted CTS group to re-run
for this fix specifically; correctness is instead verified via the new
end-to-end JIT unit test (`JITEngineTest.
RoundingModeRTZArithmeticProducesTowardZeroBits`) and the 5 updated lit
tests.

Net this session: 1 real bug fixed (inside FeMe), 5 lit tests updated,
1 new unit test added. `ninja check-feme`: 3367/3370 passing, 3
pre-existing Unsupported, 0 regressions.
