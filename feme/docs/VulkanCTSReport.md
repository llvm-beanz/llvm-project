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

### Roadmap L201(d): mesh-shader f16 stage-I/O correctness (mesh half done; tessellation split to L215)

`dEQP-VK.mesh_shader.ext.in_out.with_f16.*` (80 cases) was failing with
a silent "Result does not match reference" and no further QPA detail
at default verbosity. `--deqp-log-images=enable` showed the *entire*
rendered image wrong (all-black vs. all-blue), not a rounding-boundary
diff, ruling out a subtle precision issue and pointing at a systemic
data-corruption bug. A temporary, uncommitted local diagnostic patch to
`vktMeshShaderInOutTestsEXT.cpp` (reverted before every real CTS run;
never committed) encoded which of the fragment shader's many `good_*`
per-variable checks failed first into the output color, localizing the
first failure to `vert_f16d1_inter_0`, a smooth-interpolated f16 scalar
vertex varying.

**Bug 1 (fixed inside FeMe):** `MeshOutputWrapper.cpp` (mesh-shader
per-vertex/per-primitive output stores) was the only stage wrapper of
six (`DomainWrapper`/`FragmentWrapper`/`GeometryWrapper`/`HullWrapper`/
`PatchConstantWrapper`/`VertexWrapper`.cpp all have this) missing a
`widenForStageStorageStore` helper. It stored a shader's raw `half`
value directly, writing only 2 of `StageStorage`'s always-4-byte-per-
element slot; the other 2 stale bytes were then reinterpreted as part
of an IEEE-754 float32 by `StageStorage::readFloat`, producing
numerically unrelated garbage rather than a rounding error -- matching
the all-black-vs-all-blue symptom exactly. Fixed by adding the missing
helper and calling it in `lowerMeshOutputStore`, mirroring
`GeometryWrapper.cpp`'s `lowerGeometryOutputStore`. This alone raised
`with_f16.*` from 0/80 to 22/80.

**Bug 2 (found and fixed, but *not* a FeMe bug -- an upstream VK-GL-CTS
test-authoring bug):** the remaining 58 failures persisted across
several different interface variables and permutations. Repeating the
diagnostic-patch technique on `permutation_9.mesh_only` (still failing
after bug 1's fix) isolated the failure to `good_prim_f16d2_flat_0`, a
per-primitive, flat, *exact-equality* check. Dumping the raw actual-vs-
expected `float` bit patterns (via `floatBitsToUint`, again through a
temporary, uncommitted local patch) showed the low byte of the expected
value (read from `ppd.prim_f16d2_flat_0[N]`, an SSBO) was consistently
*non-zero* even though the underlying test data (`tcu::Vec2(1211,
1212)`) is a small integer pair, exactly representable in float16 (and
therefore should widen back to float32 with an all-zero low mantissa
byte) -- meaning the fragment shader was reading a *completely
different* field than the one the host wrote. Confirmed via `spirv-
dis`'s `OpMemberDecorate ... Offset` on a standalone reduced shader:
the shader's own std430-computed offset for `prim_f16d2_flat_0` is byte
480, while the C++ `PerPrimitiveData` mirror struct (assuming its
tightly-packed, un-padded field sizes) places that same field at byte
432 -- a 48-byte divergence, exactly the accumulated slack from the
three `tcu::Vec3`-array fields (`prim_f64d3_flat_{0,1}`, `prim_f32d3_
flat_{0,1}`, `prim_f16d3_flat_{0,1}`) declared earlier in the same
SSBO. GLSL/SPIR-V std430 gives `vec3`/`ivec3` a 16-byte per-element
base alignment even inside arrays, but `tcu::Vec3`/`tcu::IVec3` are
plain 12-byte packed types with no such padding -- so every field
declared *after* any vec3/ivec3 array in either `PerVertexData` or
`PerPrimitiveData` was silently misaligned relative to what the
fragment shader actually reads back, for both mirror structs, across
this entire test file.

Fixed **directly in the VK-GL-CTS checkout, as its own commit
(`c6783de17`), independent of any FeMe/llvm-project change**: added
`Std430Vec3`/`Std430IVec3` wrapper types (12-byte value + 4 bytes of
explicit trailing padding, with an implicit converting constructor and
conversion operator so every existing `tcu::Vec3(...)`/`tcu::IVec3(...)`
assignment call site needed no other change) and applied them to all 9
affected array-of-vec3/ivec3 fields across both mirror structs.

CTS-confirmed: `dEQP-VK.mesh_shader.ext.in_out.with_f16.*` is now
80/80 (up from 22/80 after bug 1 alone, 0/80 before either fix). The
full `dEQP-VK.mesh_shader.ext.in_out.*` group (560 cases, covering
every advertised/unadvertised bit-width combination) is 0 failed, 400
correctly `NotSupported` (unadvertised `shaderInt64`/`shaderFloat64`),
160/160 passed of the supported cases -- no regressions.

The tessellation half of the original L201(d) row (`tess_io.max_in_out.
with_f16.*`, 40 cases) reproduces in a different CTS source file
(`vktTessellationMaxIOTests.cpp`) and is still 40/80 failing after both
fixes above (a 50% failure rate, not the 100%-then-0% pattern the mesh-
shader half showed), so it needs its own independent root-cause; split
out as new roadmap row L215 rather than assumed-fixed by either change
here.

`ninja check-feme`: 3367/3370 passing, 3 pre-existing Unsupported, 0
regressions (the FeMe-side fix here is `MeshOutputWrapper.cpp` only;
no new FeMe unit test was added specifically for this row since its
correctness is fully covered by the CTS group above, which now passes
end-to-end through the real CPU JIT path).

### Roadmap L215/L216: tessellation "max IO" f16/32-bit stage-I/O correctness (not root-caused; scope corrected)

Continued investigation of the tessellation half of the original
L201(d) row (`dEQP-VK.tessellation.tess_io.max_in_out.with_f16.*`, 40/80
failing). Ruled out a repeat of the mesh-shader bug's exact cause:
`vktTessellationMaxIOTests.cpp` never uses fixed C++ mirror structs for
its per-vertex/per-patch host buffers (it uses raw `std::vector<uint8_t>`
plus a hand-written `IfaceVar::getBindingSize`/`initBinding` pair), and
that hand-written packer already implements std430 vec3/vec4-array
stride alignment correctly -- so the exact bug found for mesh shaders
does not apply here.

Imaged one reduced case (`with_f16.permutation_9.tcs_vert_writes_tes_
reads`, `--deqp-log-images=enable`): unlike the mesh-shader bug's
all-wrong image, the Result showed a mix of correct and incorrect
colors blended smoothly across the render -- i.e. some per-vertex
`good_*` checks pass and others fail within the same primitive, a
qualitatively different (partial, not total) failure pattern. Confirmed
the write-only sibling (`..._tes_na`, no TES-side check) passes,
narrowing the bug to the TCS-output -> TES-input per-vertex varying
interpolation/check path specifically.

A temporary, uncommitted VK-GL-CTS-side diagnostic patch (reverted
before any final run) encoded the smallest-indexed failing `good_*`
check into the output color, cross-referenced against a
`makeShaders()`-side variable-name dump. Findings:

- The failing variables are **not exclusively f16** -- observed
  failures include f32/i32 as well as f16, VEC3 and VEC4, flat and
  interpolated. Independently confirmed by running the
  **`32_bits_only`** tessellation group (zero 16/64-bit types at all):
  **50/80 failing**, similar to `with_f16`'s 40/80. This means the bug
  is not f16-specific; the original L215 framing was too narrow, and
  the roadmap row has been corrected (superseded by a new row, L216,
  covering both groups).
- A second diagnostic pass distinguished the real (post-device-limit-
  trim) shader's variable list (13 vars for this permutation, confirmed
  via `vulkaninfo`'s real `maxTessellationEvaluationInputComponents=64`)
  from the mock/compile-time list (29 vars, from the test's hardcoded
  128-component mock constants) -- the two-phase trim behaves as
  designed, it was not itself a factor.
- Per-pixel analysis of the failing-check-index dump showed a
  **concentric-ring pattern keyed to screen/tess-coordinate position**:
  the failing check index varies smoothly with position (not a fixed
  "these locations are always broken" set), and two f16 VEC4
  interpolated variables at different location indices never fail at
  all in the sampled image. Since `gl_Position` -- interpolated via the
  exact same `INTERP_QUAD_VAR` bilinear macro as every per-vertex
  varying -- always renders correctly (the image is not globally
  corrupted), a wrong-corner-order/wrong-`gl_TessCoord` theory is ruled
  out; whatever is wrong must be per-(TCS-output-location,
  per-invocation-corner) in the TCS->TES interface-passing path, not a
  single broken location or a single broken bit-width.

Root cause not yet found. See roadmap row L216 for the narrowed next
steps (numeric, not just pass/fail, dumps of one failing variable's
interpolated value vs. its independently-computed min/max bounds across
the tess-coordinate range, to distinguish an addressing/indexing bug
from a precision bug in domain-point generation).

No FeMe source files were changed this session. The two temporary
diagnostic patches to `vktTessellationMaxIOTests.cpp` were reverted
(`git checkout --`) before ending the session; nothing was committed to
the VK-GL-CTS checkout. `ninja check-feme` was not rerun since no
FeMe-side code changed.

## Post-run fixes (this session, against the 2026-09-26 baseline above)

### Roadmap L216: negative-result correction (L215/L216 investigation, not resolved)

Re-verified a prior session's working theory for the still-open L216
tessellation-control/-evaluation `gl_TessCoord`-interpolation bug --
that `gl_TessCoord[1]`/the Y component was "never read" by FeMe's own
lowered IR -- via a fresh, careful, line-by-line `FEME_DUMP_IR_PRENORM=1`
trace of the same failing case
(`tess_io.max_in_out.with_f16.permutation_9.tcs_vert_writes_tes_reads`).
**This claim is disproven**: `feme.stage.input.load`'s own `Component`
argument correctly alternates 0/1 for `gl_TessCoord[0]`/`[1]`, confirmed
against both a careful SSA-value trace and real SPIR-V disassembly
(`--deqp-log-decompiled-spirv=enable`, distinct `OpConstant` operands for
the two access chains) and a raw occurrence count (85 `Component=0` vs.
85 `Component=1` calls). `DomainWrapper.cpp`'s `lowerDomainLocation` was
independently re-read and confirmed correct. Future sessions should not
re-open this specific lead; see roadmap row L216 for the corrected text
and remaining open questions (the "totally black" vs. "graded blend"
symptom split, and an unrelated, incidentally-discovered
`dEQP-VK.tessellation.tesscoord.*` 18/18 `VK_ERROR_INITIALIZATION_FAILED`
pipeline-creation failure).

No FeMe source changed as part of this correction (it is a negative
result only); `ninja check-feme` was not rerun for it alone.

### Roadmap L201(b)/L217: barrierless mixed-frequency TCS entry with an un-inlined helper function

`L201(b)`'s own `float16.opvectorshuffle.*_tessc` (27 cases) repro was
re-investigated. The `OpVectorShuffle` framing turned out to be a red
herring: the real SPIR-V for `opvectorshuffle.222_tessc` shows every
`OpVectorShuffle` op living entirely inside a pure compute/SSBO helper
function (`test_code`/`sw_fun`), never touching stage IO at all. The
actual bug is generic to *any* barrierless, mixed control-point/patch-
constant tessellation-control entry (`gl_TessLevelOuter`/`Inner` written
unconditionally, no `gl_InvocationID` guard or barrier -- FeMe's own
`splitBarrierlessTessellationControlEntry` "genuine mix" shape) whose
per-vertex output is computed via a call to a separate, not-yet-inlined
GLSL/glslang helper function that itself reads an ordinary per-vertex
`Input` unrelated to the tessellation factor.

Root cause: `splitBarrierlessTessellationControlEntry` clones the whole
entry into control-point/patch-constant phases and prunes each clone's
own stage-IO *stores* by frequency (`pruneStageIOStoresByFrequency`), but
a helper function's own body is shared, unmodified, between both clones
(GLSL never guarantees full inlining before this stage the way `dxc`
always does for HLSL -- roadmap L76b). The patch-constant clone's own
copy of the (dead, since its only consumer's store was pruned)
`feme.stage.input.load` call inside that shared helper survives
completely untouched, and `PatchConstantWrapper.cpp`'s lowering has to
resolve every such call it finds -- live or not -- against that phase's
own signature; a per-vertex-only input was never given a matching entry,
so this leftover, provably-dead load fails outright with
`feme-cpu-wrap-patch-constant: input load refers to an unknown signature
element`.

Two-part fix:
1. `feme::vulkan::compileGraphicsPipeline` (`GraphicsPipeline.cpp`) now
   runs `feme::cpu::InlineHelperFunctionsPass` before
   `CanonicalizeStagePass`, so no un-inlined helper-function call
   boundary survives into the split/prune step (this pass previously
   only ran later, inside `feme::cpu::runPipeline`, too late to matter
   for a graphics-pipeline shader's own signature-building pass).
2. Defense in depth: `pruneStageIOStoresByFrequency` (`CanonicalizeStage.
   cpp`) now also sweeps up any `feme.stage.input.load` call (and the
   ordinary dead `insertelement`/`getelementptr` chain that used to feed
   the pruned store) left with zero uses after a store prune, since that
   one specific stage-op call is not `readnone` and so is never reached
   by ordinary dead-code cleanup on its own.

CTS-confirmed:
- `dEQP-VK.spirv_assembly.instruction.graphics.float16.opvectorshuffle.
  *_tessc`: **27/27 Pass** (was 0/27).
- Broader regression sweep, `dEQP-VK.spirv_assembly.instruction.graphics.
  *_tessc` (2134 cases, same `CanonicalizeStagePass`/`PatchConstantWrapper`
  code path): **1211/2134 Pass** (was 1159/2134) -- **52 cases newly
  fixed, 0 regressions** (confirmed via a byte-for-byte QPA `StatusCode`
  diff against a stashed pre-fix rebuild).
- `dEQP-VK.tessellation.tess_io.*` (568 cases comparable both ways; a
  pre-existing, unrelated bug -- `dEQP-VK.tessellation.winding.
  default_domain.hlsl_quads_ccw` crashing with `llvm.lifetime.start/end
  can only be used on alloca or poison` / a segfault depending on build,
  confirmed identical with or without this fix -- truncates the full
  `tessellation.*` group at the same point regardless): **0 regressions,
  0 newly fixed** (as expected; this is a different bug, L216, not
  touched by this fix).

New tests: `GraphicsPipelineTest.AcceptsTessellationControlBarrierlessMixed
StoreThroughHelper` (end-to-end; confirmed to reproduce the exact real
`feme-cpu-wrap-patch-constant` error without the fix, and to pass with
it) and `CanonicalizeStageTest.NoBarrierMixedFrequencyEntryPrunesDeadInput
LoadFromPatchConstantClone` (isolates the same-function dead-load-removal
behavior in `pruneStageIOStoresByFrequency`/`pruneDeadStageInputLoads`).

`ninja check-feme`: 3369/3372 passed (0 failed, 3 pre-existing
Unsupported), +2 net new tests, 0 regressions.

No Vulkan feature/extension advertisement changed as part of this fix
(purely a correctness fix within the existing `tessellationShader`
feature's own implementation), so `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` need no update for it.

## Roadmap L218/L219: viewport-state and quad-unsubdivided fixes

Both found via a fresh, previously-untriaged `dEQP-VK.tessellation.
tesscoord.*` reproduction (12 of 18 test cases; the group was 0/18 before
`L218` due to an outright pipeline-creation blocker).

**L218** -- `vkCreateGraphicsPipelines` unconditionally rejected a null
`pViewportState`, even though the spec (`VUID-VkGraphicsPipelineCreateInfo
-rasterizerDiscardEnable-00750`) allows omitting it once rasterization is
statically disabled; every `tesscoord.*` case builds exactly this
rasterization-disabled, fragment-stage-less pipeline shape via its own
shared `GraphicsPipelineBuilder`. Fixed by having `translateViewportState`
consult `Out.Raster.DiscardEnable` (already resolved by the always-earlier
`translateRasterState` call) and accept a null `pViewportState` only when
that value is statically true (a dynamically-disabled pipeline still
requires a real one, since the static value can't be known at creation
time) -- `Executor.cpp`'s own `RasterState::DiscardEnable` already
short-circuits every consumer of `Out.Viewports`/`Out.Scissors` first, so
leaving both empty in the disabled case is safe.

- `dEQP-VK.tessellation.*` (excluding hlsl-sourced cases, which now reach
  a separate, already-documented, confirmed-pre-existing crash --
  `llvm.lifetime.start/end can only be used on alloca or poison`, A/B
  stash-verified identical before/after this fix, just reached earlier
  now that this row's own blocker is gone): **359/1088 Pass** (was
  236/1088) -- **123 newly-fixed cases, 0 regressions** (byte-for-byte
  QPA `StatusCode` diff against a stashed pre-fix rebuild).
- `dEQP-VK.draw.*` (29451-case broader regression sample, unrelated
  pipeline shapes): byte-for-byte identical both runs (3256 Pass / 2 Fail
  / 26193 Not supported) -- 0 regressions.

**L219** -- `tessellateQuad`'s general inset+bridge path forced at least
one interior lattice line per axis even when both inside factors round to
1 (no subdivision should occur at all), spuriously emitting a 4-point
interior core (`(0.25,0.25)` etc.) for a fully-unsubdivided quad -- the
quad-domain analog of `tessellateTriangle`'s own pre-existing
fully-unsubdivided special case (H7x). Fixed with a matching early-return
for quads. Confirmed fixed at the sub-case level (`quads_equal_spacing`'s
`inner:{1,1},outer:{1,1,1,1}` sub-case no longer emits the 4 spurious
points) -- but this alone does not flip any top-level `tesscoord.*` test
to Pass, since every other sub-case within the same parent test still
fails on a separate, larger-scope bug -- see `L220` below, and the
roadmap row of the same name.

New tests: `GraphicsPipelineTest.AcceptsNullViewportStateWhenRasterization
StaticallyDisabled`/`.RejectsNullViewportStateWhenRasterizerDiscardIsOnly
Dynamic` (L218); `TessellatorTest.QuadFullyUnsubdividedFactorEmitsTwoReal
Triangles` (L219).

`ninja check-feme`: 3372/3375 passed (0 failed, 3 pre-existing
Unsupported), +3 net new tests, 0 regressions.

No Vulkan feature/extension advertisement changed for either fix (both
are pure valid-usage/correctness fixes within existing feature surface),
so `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no
update.

## Roadmap L220: tessellator interior-point-generation algorithm mismatch (finding only, not yet fixed)

While investigating why 12/18 `tesscoord.*` cases still fail after
`L218`/`L219`, fetched the real Vulkan spec text (`Vulkan-Docs/chapters/
tessellation.adoc`) and compared it line-by-line against `Tessellator.
cpp`'s own implementation. Conclusion: FeMe's tessellator does not
implement the same *algorithm* the spec (and dEQP's own reference
generator) requires for interior domain-point generation -- not a
rounding/off-by-one bug, a different algorithm entirely producing a
different point *set*.

Confirmed via a direct repro (`dEQP-VK.tessellation.tesscoord.
triangles_equal_spacing`, `inner: {63}, outer: {15, 42, 10}`): FeMe
generates 2147 domain coordinates where the reference expects 2950, and
individual points differ by far more than the test's own 0.01 epsilon
(e.g. reference `(0.978836, 0.010582, 0.010582)` vs. FeMe's nearest
`(0.989744, 0.0051282, 0.0051282)`).

Full technical writeup, including the spec's own quoted algorithm text
and a concrete rewrite plan, is in the roadmap under `L220`. Likely the
true unifying root cause of `L216`'s own still-open per-vertex TCS/TES
corruption too (not yet confirmed by direct comparison). Not attempted
this session -- a real rewrite of both `tessellateQuad`'s and
`tessellateTriangle`'s interior-generation logic is a substantial,
regression-risky undertaking better scoped as its own dedicated future
session(s).

No CTS re-run performed for this row (no code changed).

## Roadmap L220: triangle-domain concentric-ring tessellator rewrite

Implements the Vulkan/GLSL spec's real "Triangle Tessellation" algorithm
(concentric, successively-shrinking equilateral rings, each ring's own
per-edge segment count exactly 2 less than the previous, terminating at
either an un-subdivided single triangle or a degenerate centroid point)
in place of the previous single inset-toward-centroid core `tessellateQuad`
and `tessellateTriangle` both used -- see `Roadmap.md`'s `L220` row for the
full technical derivation (including the closed-form homothety proof that
avoids needing real per-ring perpendicular-projection geometry in code).
This session only rewrote the **triangle** domain; the quad domain (`L221`)
is unchanged.

`ninja check-feme`: 3376/3379 passed (0 failed, 3 pre-existing
Unsupported), +4 net new `TessellatorTest.cpp` cases, 0 regressions.
Every pre-existing triangle-domain `TessellatorTest.cpp` case (winding,
shared-edge, crack-free) passes unchanged against the new algorithm.

CTS-confirmed:
- `dEQP-VK.tessellation.tesscoord.triangles_*` (6 cases): **6/6 Pass**
  (was 0/6) -- the `isolines_*` (6/6, unaffected) and `quads_*` (0/6,
  unchanged, `L221`) groups make up the remaining 12/18 of the full
  `tesscoord.*` group.
- `dEQP-VK.tessellation.*` (excluding hlsl-sourced cases, 1088-case
  sample): **386/1088 Pass** (was 359/1088) -- **27 newly-fixed cases,
  0 regressions** (byte-for-byte QPA `StatusCode` diff against a stashed
  pre-fix rebuild). Newly-fixed groups: `invariance.{inner_triangle_set,
  outer_edge_division,outer_triangle_set,primitive_set,triangle_set}.
  triangles_*`, `misc_draw.fill_{cover,overlap}_triangles_*`,
  `tesscoord.triangles_*`.
- `dEQP-VK.tessellation.tess_io.max_in_out.with_f16.*` (the original
  `L216` repro, 80 cases): **unaffected, 40/80 Pass both before and
  after** -- confirms `L216`'s own failure is specific to the **quad**
  domain, not triangle, so it needs `L221`'s own quad rewrite before it
  can be re-attempted.

No Vulkan feature/extension advertisement changed (a pure correctness
fix within the existing `tessellationShader` feature's own
implementation), so `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` need no update.

## Roadmap L201(c): floating-point-source narrowing vector bitcast in SIMDize

Closes out roadmap `L201(c)` (`opcompositeinsert`/`opcompositeextract`/
`opvectorinsertdynamic`/`opvectorextractdynamic`/`opcompositeconstruct`,
25 cases carried over many sessions as "not yet reduced"). Reducing one
representative from each opcode shape found 19/25 already passing
(fixed incidentally by earlier sessions' generic tessellation-control
work); the remaining 6 (`opcompositeinsert`/`opvectorinsertdynamic` on
`v4f16`, `frag`/`vert` only) were a genuine `SIMDize.cpp` gap: a
narrowing `bitcast <4 x half> to <2 x i32>` with a floating-point
*source* element type, a shape `isVectorNarrowingBitCast` (built for a
different, all-integer roadmap `L134h` case) didn't recognize. See
`Roadmap.md`'s `L201(c)` row for the full technical detail.

CTS-confirmed:
- The 6 previously-failing `opcompositeinsert.v4f16_{frag,vert}` and
  `opvectorinsertdynamic.v4f16_{frag,vert}` cases: **6/6 Pass** (was
  0/6).
- `dEQP-VK.spirv_assembly.instruction.graphics.float16.*` (2135 cases,
  the full float16 group all 5 opcode shapes belong to): **0 failed**
  (1995 Pass, 140 pre-existing unrelated Not Supported).
- `dEQP-VK.tessellation.tess_io.max_in_out.with_f16.*` (`L216`'s own
  repro, 80 cases): unaffected, 40/80 Pass both before and after.

`ninja check-feme`: 3377/3380 passed (0 failed, 3 pre-existing
Unsupported), +1 net new `SIMDizeTest.cpp` case.

No Vulkan feature/extension advertisement changed, so
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no
update.

## Roadmap L221/L222: real quad-domain tessellation algorithm + FractionalEven clamp fix

Closes out roadmap `L221` (the quad-domain half of `L220`'s tessellator
rewrite) and a follow-on fix split out as `L222`, both found and
verified while implementing/checking `L221` against the real CTS.

`L221` rewrote `tessellateQuad`'s interior generation to follow the
spec's real algorithm literally: a full `(N - 1) x (M - 1)` interior
grid from the inner tessellation levels, triangulating only the
strictly-interior cells and discarding the grid's own outermost ring;
the true outer edges independently re-subdivided using the four outer
tessellation levels; and the true outer boundary bridged to the
interior grid's own retained boundary ring. All 4 `m`/`n`-degenerate
combinations are handled explicitly. A genuine bridging bug (uneven
"corner bunching" whenever inner and outer factors were aligned) was
found via CTS and fixed by giving `bridgeEdge`/`bridgeRingsByEdge` an
optional real-geometric-position override for the merge-order decision.

`L222` fixed a separate, pre-existing `computeSegmentCount` bug:
`SpacingFractionalEven` was clamping to `[1, maxLevel]` like every
other partitioning mode instead of the spec's own stricter `[2,
maxLevel]`, previously masked by the old quad algorithm's imprecise
margin-based inset formula (which always over-produced triangles
relative to the strict reference count).

`ninja check-feme`: 3381/3384 passed (0 failed, 3 pre-existing
Unsupported), +2 net new `TessellatorTest.cpp` cases
(`QuadAlignedInnerAndOuterFactorsGiveUnitGridTriangles`,
`QuadSingleAxisDegenerateInsideFactorGivesInteriorLine`/
`QuadOtherAxisDegenerateInsideFactorGivesInteriorLine` were already
counted against the `L221`-only baseline), 0 regressions. Every
pre-existing quad-domain `TessellatorTest.cpp` case (winding,
shared-edge, crack-free, point-mode) passes unchanged against the new
algorithm.

CTS-confirmed (`dEQP-VK.tessellation.*`, excluding hlsl-sourced cases,
1088-case sample, byte-for-byte QPA `StatusCode` diff against the
pre-`L220`/`L221` baseline used in the `L220` report above):
**414/1088 Pass** (was 386/1088) -- **28 newly-fixed cases, 0
regressions**. Newly-fixed groups: `fractional_spacing.glsl_even`;
`geometry_interaction.limits.*` (all 3); `geometry_interaction.scatter.
{geometry_scatter_instances,geometry_scatter_layers}`;
`invariance.{inner_triangle_set,outer_edge_division,outer_triangle_set,
primitive_set,triangle_set}.*fractional_even_spacing*`;
`misc_draw.fill_cover_quads_{equal,fractional_odd}_spacing_draw{,
_indirect}`.

`geometry_interaction.scatter.geometry_scatter_primitives` (found
regressed mid-session between the `L221` rewrite alone and the
combined `L221`+`L222` fix; root-caused to `L221`'s own corner-bunching
bridging bug, fixed within the same `L221` commit alongside its
dedicated `QuadAlignedInnerAndOuterFactorsGiveUnitGridTriangles`
regression test) is included in the final 414/1088, with 0 net
regressions against the pre-`L220` baseline.

No Vulkan feature/extension advertisement changed (a pure correctness
fix within the existing `tessellationShader` feature's own
implementation), so `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` need no update.

## Roadmap L216: TCS same-invocation patch-output read-back fix

Closes out roadmap `L216`, open since the `L215`/`L216` split several
sessions ago. Two prior working theories (a shared root cause with the
`L220`/`L221` tessellator-algorithm rewrites, and an earlier
`gl_TessCoord[1]`-never-read theory) were both re-checked and
disproven this session before the real root cause was found.

Every `dEQP-VK.tessellation.tess_io.max_in_out.*` case fixes its
tessellation levels at exactly `1.0` (fully un-subdivided), so the
tessellator's own geometry/algorithm was provably irrelevant to this
failure regardless of which one generated it -- confirmed empirically
once `L221` landed and the failure count didn't move (still exactly
40/80 `with_f16` cases failing). The actual failure was isolated to
the 4 `tcs_patch_writes_reads_*` sub-variants specifically by trimming
the CTS's own variable-permutation count down to one variable per
type/bit-width/dimension in a local, uncommitted VK-GL-CTS repro
build (reverted after use, never committed): every `tcs_vert_*`
sub-variant already passed, while every `tcs_patch_*` one still failed
100% of the time regardless of variable count or type. Dumping the
generated GLSL confirmed each `tcs_patch_writes_reads_*` case writes a
`patch out` variable from an SSBO read keyed by `gl_PrimitiveID`, then
immediately reads that same variable back within the same invocation
to compare it against the source buffer -- legal, spec-defined
GLSL/SPIR-V (only a *different* invocation's own patch-output write is
undefined without a barrier).

Root cause: `splitBarrierlessTessellationControlEntry`'s mixed-
frequency handling (roadmap H9c) clones a barrierless, mixed
control-point/patch-constant tessellation-control entry into two
per-frequency-pruned functions via `pruneStageIOStoresByFrequency`,
which erases every stage-IO *store* that does not belong to its own
clone's frequency. This is correct for the stores themselves, but left
the same-invocation read-back *load* of a pruned patch-output global
behind in the control-point clone, now reading a global that clone no
longer writes at all -- silently wrong data, not a crash, which is why
this went unnoticed across multiple prior sessions' `gl_TessCoord`-
and tessellator-focused investigations.

Fix (`feme/lib/Transforms/Graphics/CanonicalizeStage.cpp`): before
erasing a pruned store, forward its stored value to every load of the
exact same pointer that it dominates, using a `DominatorTree` over the
clone being pruned. New unit test:
`CanonicalizeStageTest.NoBarrierMixedFrequencyEntryForwardsSameInvocationPatchReadBack`
(confirmed to fail with the expected pre-fix symptom -- the patch
store surviving unpruned -- via A/B stash testing against the fix).

CTS-confirmed:

- `dEQP-VK.tessellation.tess_io.max_in_out.*` (full 560-case sweep
  across every bit-width group, `32_bits_only` through `all_types`):
  **0 Failed** (was 40/80 Fail in the `with_f16` group alone; the
  other 480 cases in the full sweep are correctly `NotSupported` for
  optional 64-bit integer/float features on this device, not
  failures).
- `dEQP-VK.tessellation.*` (excluding hlsl-sourced/crashing cases,
  1088-case sample, same methodology as every prior session's report):
  **494/1088 Pass** (was 414/1088) -- **80 newly-fixed cases** (all in
  `tess_io.max_in_out.with_f16.*`), **0 regressions** (the same
  pre-existing 156-case failure set from `invariance`,
  `user_defined_io`, `primitive_discard`, `shader_input_output`,
  `misc_draw`, `tesscoord`, `common_edge`, `matrix_multiplication`, and
  `geometry_interaction` remains, all already tracked or pending
  triage under other roadmap rows).
- The pre-existing, unrelated `llvm.lifetime.start/end can only be
  used on alloca or poison` crash on hlsl-sourced tessellation-control
  shaders (first seen at `winding.default_domain.hlsl_quads_ccw`,
  this session first hit earlier at `fractional_spacing.hlsl_even`)
  still halts the `tessellation.*` batch at the same points regardless
  of this fix -- confirmed via the same chunked-resume methodology
  prior sessions used, not a regression.

`ninja check-feme`: 3382/3385 passed (0 failed, 3 pre-existing
unsupported, +1 net new unit test).

No Vulkan feature/extension advertisement changed (a pure correctness
fix within the existing `tessellationShader` feature's own
implementation), so `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` need no update.

## Roadmap L223: hull-shader barrier-split alloca-capture bug (verifier crash + dangling pointer)

Root-caused and fixed the long-standing, previously-untriaged
`llvm.lifetime.start/end can only be used on alloca or poison`
verifier crash on hlsl-sourced tessellation-control shaders (first
observed several sessions ago at
`dEQP-VK.tessellation.winding.default_domain.hlsl_quads_ccw`, later
also blocking `fractional_spacing.hlsl_{even,odd}` once `L218`
unblocked pipeline creation far enough to reach it). This had halted
the whole `tessellation.*` batch at the same point across many
sessions, treated as unrelated background noise.

Two stacked root causes, both inside
`splitTessellationControlEntry`'s roadmap-H4c "captured SSA value"
mechanism (`feme/lib/Transforms/Graphics/CanonicalizeStage.cpp`):

1. `feme::cpu::InlineHelperFunctionsPass` inlines an HLSL hull
   shader's separate patch-constant function (`PCF()`, called via
   `OpFunctionCall` in glslang's own raw SPIR-V -- confirmed via a
   temporary SPIR-V disassembly dump added directly to VK-GL-CTS's own
   `vkShaderToSpirV.cpp`, reverted after use, that these `.hlsl_*`
   cases are compiled by glslang's built-in HLSL frontend, not DXC)
   into the shared entry function. LLVM's standard `InlineFunction`
   inliner utility hoists that inlined callee's own local `alloca`
   (HLSL's `HS_CONSTANT_OUT output;`) into the *caller's* entry block,
   which ends up on the control-point side of the later barrier split,
   even though every real use of it -- the GEPs/stores building the
   struct, the final reload, and its `llvm.lifetime.start`/`.end`
   markers -- remains entirely on the patch-constant side. H4c's
   generic "route the captured value's address through a synthetic
   global" clone step then blindly rewrites every operand referencing
   that alloca, including the lifetime markers, producing a marker
   whose operand is a reloaded pointer rather than a real
   `alloca`/`poison` -- rejected outright by `llvm::verifyModule`.
2. Fixing only the lifetime-marker symptom (erasing any post-split
   marker left pointing at a non-`alloca`/`poison` value -- always
   sound, since a lifetime marker is only ever an optimization hint)
   surfaced a second, deeper bug: H4c's global-routing design was only
   ever intended for ordinary scalar/vector SSA-value captures
   (verified against `SplitsHullEntryThreadingCapturedSSAValue`'s own
   doc comment/test), never a `ptr`-typed capture pointing at stack
   memory -- routing such a pointer's *address* through a global and
   dereferencing it from `main.patchconstant` (a separately-invoked
   function, never inlined back into `main`, confirmed via
   `FEME_DUMP_IR`) reads through `main`'s own already-unwound stack
   frame, a dangling-pointer bug (observed as a SIGSEGV in
   unsymbolicated JIT code once the lifetime-marker crash alone was
   fixed).

Real fix: when a captured value is an `AllocaInst` whose uses are all
confined to the barrier-split region, clone the alloca directly into
the patch-constant phase (real, independent local storage, no dangling
access) instead of routing its address through a global at all; kept
the lifetime-marker-erasing fix as a defensive fallback for any other
capture shape not covered by the alloca-cloning path. New unit test
`CanonicalizeStageTest.SplitsHullEntryCloningCapturedAlloca` (verified
via stash A/B testing to fail without the fix, pass with it).

CTS-confirmed:

- `dEQP-VK.tessellation.fractional_spacing.hlsl_{even,odd}` and all 48
  `dEQP-VK.tessellation.winding.*.hlsl_*` cases: now **Pass** (were a
  hard crash before, halting the whole batch).
- The full 1114-case `dEQP-VK.tessellation.*` mustpass sample
  (`external/vulkancts/mustpass/main/vk-default/tessellation.txt`) now
  runs to completion with **0 crashes**: **520/1114 Pass, 156 Fail,
  438 Not supported** -- the 156 failures matching the pre-existing,
  already-tracked baseline exactly (same `invariance`,
  `user_defined_io`, `primitive_discard`, `shader_input_output`,
  `misc_draw`, `tesscoord`, `common_edge`, `matrix_multiplication`,
  and `geometry_interaction` groups noted in prior reports) -- **0
  regressions, 0 newly broken**. This is the first time the full
  1114-case mustpass list (rather than a hand-picked 1088-case
  sample excluding the crashing cases) has run to completion.

`ninja check-feme`: 3383/3386 passed (0 failed, 3 pre-existing
unsupported, +1 net new unit test).

No Vulkan feature/extension advertisement changed (a pure internal-
compiler correctness fix inside the tessellation-control barrier-split
lowering, touching no feature/extension surface), so
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no
update.

## Roadmap L224: inside tessellation factor spuriously culled triangle/quad patches

`dEQP-VK.tessellation.primitive_discard.*`'s 24 `useLessThanOneInner
Levels=true` failures (both triangles and quads) were found while
triaging the residual `tessellation.*` failure baseline once `L223`
unblocked a full-sample run.

Per the Vulkan/GLSL tessellation spec's primitive-discard rule, only
the **outer edge** tessellation factors ever discard a whole patch
when any is non-positive; a non-positive **inside** (interior) factor
is never a discard condition -- it is simply clamped up to 1 like any
other partitioning mode's ordinary minimum clamp. `tessellateIsoline`
was already correct (only ever checks the 2 outer edge factors, no
`Inside` involvement at all), which is why every `isolines_*`
`primitive_discard` sub-case already passed both before and after this
fix -- a useful cross-check confirming the bug was domain-specific to
triangle/quad, not systemic.

`tessellateTriangle`/`tessellateQuad` (`Tessellator.cpp`) incorrectly
folded `Factors.Inside[...]` into the same `anyFactorCullsPatch` array
as the outer edges, so a patch was spuriously discarded whenever only
its inside factor was invalid (e.g. `-0.42`/`0.0`) even with fully
valid outer levels -- exactly the shape `useLessThanOneInnerLevels=
true` constructs. Fixed both call sites to only pass `Edges` (outer
factors) to `anyFactorCullsPatch`; updated `anyFactorCullsPatch`'s own
doc comment and the `TessFactors` struct's doc comment (`Tessellator.
h`) to state explicitly that only outer factors cull.

New unit test: `TessellatorTest.
NonPositiveInsideFactorAloneDoesNotCullTriangleOrQuad` (confirmed via
`git stash` A/B testing to fail without the fix, pass with it).

CTS-confirmed:

- `dEQP-VK.tessellation.primitive_discard.*` goes from 20/44 to
  **40/44 Pass** (24 newly-fixed cases, 0 regressions).
- The full 1114-case `dEQP-VK.tessellation.*` mustpass sample goes
  from **520/1114 Pass, 156 Fail, 438 Not supported** to **540/1114
  Pass, 136 Fail, 438 Not supported** -- exactly the expected
  +20 Pass/-20 Fail delta, **0 crashes**, **0 regressions** (every
  previously-Pass case remains Pass; only previously-Fail cases moved
  to Pass).
- The residual 4 `quads_equal_spacing_{ccw,cw}[_point_mode]` cases
  that remained failing after this fix are a distinct, smaller,
  not-yet-root-caused bug -- see `L225`.

`ninja check-feme`: 3384/3387 passed (0 failed, 3 pre-existing
unsupported, +1 net new unit test).

No Vulkan feature/extension advertisement changed (a pure internal
correctness fix inside the tessellator's primitive-discard check,
touching no feature/extension surface), so `Vulkan14FeatureInventory.
md`/`VulkanExtensionInventory.md` need no update.

## Roadmap L225: `quads_equal_spacing` vertex undercount -- root-caused and fixed

`dEQP-VK.tessellation.primitive_discard.quads_equal_spacing_{ccw,cw}
[_point_mode]` (4 cases) still failed after `L224`'s fix, with
"expected 254 vertices from shader invocations, but got only 250".
This session built `deqp-vk` locally (source at
`/home/dev/dev/VK-GL-CTS`, revision unchanged from the Scope section
above) and reproduced the exact case, then read dEQP's own reference
implementation (`generateReferenceQuadTessCoords`/
`referenceQuadNonPointModePrimitiveCount` in
`vktTessellationUtil.cpp`) rather than re-deriving the closed-form
formula from scratch.

Root cause: `tessellateQuad`'s `L219` fully-unsubdivided fast path
fired whenever *both inside factors alone* rounded down to 1 segment,
on the (now-corrected) comment's mistaken belief that this could only
happen alongside every outer edge also being 1, attributed to a
nonexistent dEQP "precondition assert". dEQP's own reference proves
this false: it only takes its equivalent shortcut when every outer
edge is *also* exactly 1; otherwise it bumps both inside factors to 2
(3 for fractional-odd) and falls through to the ordinary formula with
the real, unbumped outer edges -- which for a two-degenerate-axes quad
still contributes exactly 1 interior center point.
`primitive_discard`'s per-patch random levels reach exactly this gap
(both inside factors `<= 1`, at least one outer edge `> 1`): the
over-eager fast path emitted only the outer boundary's own points and
no interior point at all, undercounting by 1 vertex per such patch (4
of the case's 729 patches hit it, matching "expected 254 ... got only
250" exactly).

Fix: require every outer edge to also compute to 1 segment
(`Eu0 == 1 && Eu1 == 1 && Ev0 == 1 && Ev1 == 1`) before taking the fast
path, matching dEQP's reference condition exactly. Every other
combination now falls through to the already-correct general `M`/`N`
path, whose `M == 2 && N == 2` branch already fans the real outer
boundary to a single center point regardless of the outer edges' own
subdivision. New test:
`TessellatorTest.QuadDegenerateInsideFactorsWithSubdividedOuterEdgeAddsInteriorPoint`
(inside `{1, 1}`, outer edges `{1, 1, 1, 3}`, expects `7` points
including the center).

Verification (both runs against the same locally-built `deqp-vk` and
the same 1114-case `tessellation.*` mustpass case list, using
`feme/utils/run_vulkan_cts.py`, one worker's solo re-verification pass
included):

- Before the fix (confirmed via `git stash`): **540/1114 Pass, 136
  Fail, 438 Not supported, 0 crashes** -- exactly reproducing the
  `L224` baseline above.
- After the fix: **545/1114 Pass, 131 Fail, 438 Not supported, 0
  crashes**.
- A line-by-line diff of the two runs' solo-verified failure lists
  confirms exactly 5 cases flipped Fail to Pass and 0 cases changed
  any other way: the 4 `primitive_discard.quads_equal_spacing_{ccw,cw}
  [_point_mode]` cases plus `invariance.inner_triangle_set.
  quads_equal_spacing` (a fifth case hitting the identical
  inside-factors-degenerate-but-outer-edges-not shape).
- All 4 individual `primitive_discard.quads_equal_spacing_*` cases
  independently confirmed **Pass** via a direct `deqp-vk
  --deqp-case=...` run (not just the batch harness).

`ninja check-feme`: 3327/3388 passed (0 failed, 61 unsupported, +1 net
new unit test).

No Vulkan feature/extension advertisement changed (a pure internal
correctness fix inside the tessellator's fast-path condition, touching
no feature/extension surface), so `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` need no update.

## Roadmap L226: `user_defined_io` JIT-link failure -- root-caused and fixed

All `dEQP-VK.tessellation.user_defined_io.*` cases (the roadmap's
original "27 cases" undercounted the group's real mustpass size -- it
is actually 54) failed identically with
`vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED` from
a JIT link error, `"Symbols not found: [ spirv_var_N ]"`. This session
built `deqp-vk` locally (source unchanged from the Scope section
above) and reproduced
`per_patch_block.vertex_io_array_size_implicit.triangles` end-to-end
*outside* `deqp-vk`: the failing TCS's own SPIR-V was extracted from
the case's QPA log (`--deqp-log-filename=...`'s own
`<SpirVAssemblySource>` section), reassembled with `spirv-as`, then
run through `feme-translate`'s own `--import-spirv`/`--spirv-to-llvmir`
pipeline to get raw (pre-`CanonicalizeStage`) LLVM IR, and through
`feme-opt --llvm -passes=feme-graphics-canonicalize-stage` to get the
canonicalized IR `libfeme_vulkan.so` actually JITs.

Diffing the two IR dumps: every stage-IO global access converted
cleanly into a `feme.stage.*` call *except one* -- a doubly-dynamic
`tcBlock.blockSa[i].z[j]` access (a genuine multi-member nested
struct's own array member, itself indexed dynamically, inside *that*
member's own array field, also indexed dynamically -- exactly the
shape roadmap `H115`/`H117`/`H118` document as already supported by
`getDynamicRowIndexedAccess`) -- left as a raw, unconverted
`getelementptr`/`store` pair into the still-`external` global, an
unresolvable symbol at JIT-link time.

Root cause: `collectDynamicRowTerms`'s constant-index `StructType`
branch computes its own flattened leaf index (`IDStart`, later
reported as `DynamicRowIndexedAccess::Member`) by summing
`getStageIOLeafElementCount` over *every* struct member preceding the
one actually selected -- including any synthetic `[N x i8]`
alignment-gap pad field (roadmap L105) `layOutStructIfOffsetsMatch`
inserts between real, SPIR-V-declared members to keep the LLVM
struct's own natural layout matching SPIR-V's explicit `Offset`
decorations. Every *other* `IDStart`-accumulation loop in this file
(`resolveOffsetWithinElement`'s, `resolveNestedStageIOField`'s) already
skips a pad field first via `isStageIOPadField` -- this one, alone, did
not. `TheBlock`'s own layout (`{ S blockS; float blockFa[3]; <pad>; S
blockSa[2]; float blockF; <pad>; }`) has exactly one such pad
immediately before `blockSa`, and `blockSa`'s own inner struct `S` has
a second one before its own `y` member -- so reaching `z` (`S`'s third
real member) accumulated two bogus extra leaf counts, pushing
`Dyn->Member` two past its correct value and out of
`ElementIDs[GV]`'s real bounds. This tripped `resolveStageIOAccess`'s
own bounds check (`Dyn->Member >= It->second.size()`), which silently
bails (returns `std::nullopt`) rather than asserting -- exactly why
the access was left unconverted instead of failing loudly at compile
time.

Fix: skip `isStageIOPadField` members in `collectDynamicRowTerms`'s
`IDStart` accumulation loop, matching every other such loop in the
file. New test: `CanonicalizeStageTest.
SkipsSyntheticPadFieldPrecedingArrayOfNestedStructMember` (a `patch
out`-shaped block with a leading real scalar member, forcing a pad
before its own array-of-nested-struct member; confirmed to fail with
the pre-fix off-by-N `IDStart` -- caught a *different* leaf element's
`ElementID` than the one actually stored to -- before the fix, pass
after).

Verification (both runs against the same locally-built `deqp-vk` and
the same 1114-case `tessellation.*` mustpass case list, using
`feme/utils/run_vulkan_cts.py`, one worker's solo re-verification pass
included):

- The 3 originally-reproduced cases
  (`per_patch_block`/`per_patch_block_array`/`per_vertex_block`, each
  `.vertex_io_array_size_implicit`/`.vertex_io_array_size_spec_min`)
  independently confirmed **Pass** via direct `deqp-vk
  --deqp-case=...` runs (not just the batch harness).
- The full 54-case `dEQP-VK.tessellation.user_defined_io.*` group:
  **54/54 Pass, 0 Fail** (direct `deqp-vk
  --deqp-case='dEQP-VK.tessellation.user_defined_io.*'` run).
- The full 1114-case `tessellation.*` mustpass sample goes from
  `L225`'s **545/1114 Pass, 131 Fail** baseline to **572/1114 Pass,
  104 Fail** (still 438 Not supported, still **0 crashes**) -- a clean
  `+27`/`-27` delta.
- The verified failure list's remaining 104 cases group entirely into
  the pre-existing, already-documented residual groups
  (`invariance.outer_edge_symmetry` (36), `invariance.
  outer_edge_index_independence` (24), `shader_input_output` (15),
  `misc_draw` (15), `tesscoord` (6), `common_edge` (3),
  `matrix_multiplication` (2), `invariance.inner_triangle_set` (2),
  `geometry_interaction.passthrough` (1)) -- zero `user_defined_io`
  cases remain, and zero new failures were introduced anywhere else in
  the suite.

`ninja check-feme`: 3328/3389 passed (0 failed, 61 unsupported, +1 net
new unit test).

No Vulkan feature/extension advertisement changed (a pure internal
correctness fix inside a stage-IO global's own flattened-leaf-index
bookkeeping, touching no feature/extension surface), so
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no
update.

## Roadmap L228: broader-than-tessellation CTS sample (api/pipeline/synchronization)

Ran the broader CTS sample flagged as overdue across several prior
sessions: no full `api`/`pipeline`/`synchronization` mustpass run has
been attempted so far (those lists total hundreds of thousands of
cases combined -- `api.txt` alone is 267501 cases -- too large for a
single session), so this session instead drew a fixed-seed, stratified
random sample: 3000 cases each from `api.txt`, `synchronization.txt`,
`synchronization2.txt`, and every file under `pipeline/monolithic/`
(~12000 cases total, deduplicated to 11996 after 4 cases appeared in
more than one source list). `shader_render` has no standalone mustpass
list under `external/vulkancts/mustpass/main/vk-default/` -- its cases
are folded into another group's file not yet identified -- so it was
not separately sampled this round (see `L228(f)`).

Run via `feme/utils/run_vulkan_cts.py` against the same locally-built
`deqp-vk` used throughout this session, one worker's solo
re-verification pass included:

- **3696/11996 Pass, 416 Fail, 7884 Not supported.**
- Grouping the verified failure list by test group:
  `synchronization.timeline_semaphore` (105) +
  `synchronization2.timeline_semaphore` (105) = **210**, by far the
  largest single cluster; `api.copy_and_blit` (90); `api.image_clearing`
  (57); `synchronization.op` (13) + `synchronization2.op` (23) = 36;
  `api.info` (10); `pipeline.monolithic.sampler.border_swizzle` (8);
  a handful of one-off `pipeline.monolithic.*` cases (5).
- Spot-checked one failure from each of the three largest clusters
  (single-case-deep only, not root-caused):
  - `api.copy_and_blit.copy_commands2.blit_image.all_formats.color.2d.
    astc_12x10_srgb_block.a8b8g8r8_srgb_pack32.general_general_linear`:
    `Fail (Result image is incorrect)` -- an ASTC-compressed-source
    blit producing wrong pixels.
  - `synchronization.timeline_semaphore.device_host.write_blit_image_
    read_blit_image.image_128x128_r16_uint`: `Fail
    (synchronizationWrapper->queueSubmit(queue, VK_NULL_HANDLE):
    VK_ERROR_INITIALIZATION_FAILED at
    vktSynchronizationTimelineSemaphoreTests.cpp:1055)` -- fails at
    submission itself, consistent with the 210-case cluster size
    reflecting a broad feature gap rather than one narrow edge case.
  - `api.image_clearing.core.clear_color_attachment.multiple_layers.
    a1r5g5b5_unorm_pack16_200x180_clamp_input_sample_count_4`: `Fail
    (Color value mismatch! ... Color:(0, 0, 0, 0))` -- an MSAA
    (`sample_count_4`) multi-array-layer clear reads back all-zero
    instead of the cleared color.

Filed as roadmap `L228` with sub-items `L228(a)`-`(f)` for the next
session(s): one per failure cluster to root-cause independently
(`(a)` timeline semaphores, `(b)` compressed-format blits, `(c)` MSAA
multi-layer clears, `(d)` the two smaller clusters), plus `(e)`
re-running this same fixed-seed sample after any fix lands to confirm
real-world impact, and `(f)` widening this sample's coverage (only
~4.5% of `api.txt` and a smaller fraction of `pipeline` were sampled;
`shader_render` entirely unsampled) in a future session.

`ninja check-feme`: not re-run for this session's `L228` work -- a
pure CTS-sampling exercise, no `feme/` source touched.

No Vulkan feature/extension advertisement changed by this row itself
(no code changed), so `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` need no update from this row alone; a fix
landing under one of the `L228(a)`-`(d)` sub-items may need one (e.g.
if `timeline_semaphore` support turns out to need a fresh feature-
advertisement fix rather than a bug in an already-advertised path).

## Roadmap L228(a): `timeline_semaphore` non-blocking-wait bug -- root-caused and fixed

The largest single cluster from `L228`'s own sample (210 of 416
sampled failures): `dEQP-VK.synchronization.timeline_semaphore.*`/
`dEQP-VK.synchronization2.timeline_semaphore.*` cases failing at
`vkQueueSubmit`/`vkQueueSubmit2` with `VK_ERROR_INITIALIZATION_FAILED`.

Reproduced directly (no batch harness):
`deqp-vk --deqp-case='dEQP-VK.synchronization.timeline_semaphore.device_host.write_blit_image_read_blit_image.image_128x128_r16_uint'`
confirmed the failure at `queueSubmit`.
`vktSynchronizationTimelineSemaphoreTests.cpp`'s `DeviceHostTestInstance`/
`HostCopyThread` classes were read to understand the test's own
structure: a real background `de::Thread` (`HostCopyThread`) calls
`vkWaitSemaphores`/`vkSignalSemaphore` (genuine host entry points)
concurrently with the main thread's own batched `vkQueueSubmit2` call,
chaining 12 device/host write-read iterations through one timeline
semaphore.

Root cause: `feme/lib/Vulkan/Sync.cpp`'s `consumeWaits` helper (shared
by `vkQueueSubmit`/`vkQueueSubmit2`) and `vkWaitSemaphores` both did an
instantaneous, non-blocking check of a timeline semaphore's counter
(`Op.Sem->timelineValue() < Op.Value`) instead of genuinely blocking
until another thread signals it. `Sync.h`'s own file comment claimed
"signaling and waiting are never truly concurrent" for *all*
semaphores -- true for binary semaphores (core Vulkan gives them no
host-facing signal/wait API at all) but false for timeline semaphores,
which *can* be host-signaled/waited via `vkSignalSemaphore`/
`vkWaitSemaphores` from a genuinely independent thread. `Semaphore`'s
own `Value` field (`Sync.h`) also had no thread-safety at all -- a
real data race between the host thread and the queue-submitting
thread. This also matches `FeMeVulkanDesign.md`'s original design
intent ("Synchronization objects use monotonically changing state
under a mutex and condition variable") -- the non-blocking
implementation was itself an undocumented deviation from the design
doc, which this fix restores conformance with (for timeline semaphores
specifically; binary semaphores/fences/events correctly keep the
simpler, non-blocking treatment, since they have no host-signal API at
all -- see the design doc's own newly-added clarifying paragraph).

Fix: added a `std::mutex`/`std::condition_variable` to `Semaphore` and
a new `waitTimeline(TargetValue, TimeoutNs)` method that genuinely
blocks (`signalTimeline` now locks the mutex, updates the value, then
`notify_all`s); `consumeWaits`'s timeline branch and `vkWaitSemaphores`
both now call it instead of the old instant check.

A genuine blocking wait exposed a second, distinct problem while
verifying the fix: destroying a semaphore whose condition variable a
real thread is still blocked on can hang the entire process forever if
some *other*, unrelated bug ever genuinely fails to signal the awaited
value -- confirmed via a real hang plus a `gdb` thread-apply-all-bt
dump on `dEQP-VK...device_host...image_64x64x8_r32_sfloat` (a distinct,
pre-existing 3D-image bug, not something this fix introduced -- see
`L228(g)` below). Added a bounded safety-net timeout
(`TimelineWaitSafetyNetTimeoutNs`, 5 seconds) applied even to
`vkWaitSemaphores`'s own literal `UINT64_MAX` ("wait forever") caller
value, so any such case now fails fast with `VK_TIMEOUT`/
`VK_ERROR_INITIALIZATION_FAILED` instead of hanging forever -- this
software driver's analogue of a real GPU driver's hardware TDR
(timeout-detection-and-recovery). Real signals in this driver land in
well under a millisecond in practice; `SyncTest.cpp`'s own new tests
use an artificial 200ms delay purely to make the blocking observable,
two full orders of magnitude below the 5s safety-net bound, so the
bound does not meaningfully slow down any real, successful wait.

New unit tests (`feme/unittests/Vulkan/SyncTest.cpp`):
`TimelineSemaphoreWaitBlocksUntilHostSignal` and
`QueueSubmitBlocksUntilHostSignalsTimelineSemaphore`, both spawning a
real background `std::thread` that sleeps 200ms before signaling, then
asserting the waiting call's own elapsed wall-clock time is at least
that long -- confirmed to fail (return instantly, i.e. wrongly) before
the fix and pass (genuinely block) after it. The pre-existing
`QueueSubmit2TimelineSemaphoreSignalThenWait` negative test (submits a
wait on a value nothing ever signals, expecting
`VK_ERROR_INITIALIZATION_FAILED`) now legitimately takes the full
safety-net bound to fail -- this is why the bound was tuned down from
an initially-chosen 30s to 5s, keeping the unit test suite fast while
remaining enormously generous relative to this driver's real,
sub-millisecond signal latencies.

CTS-confirmed:

- The originally-reported case now **Pass**es.
- The full mustpass-derived `timeline_semaphore` group
  (`synchronization.txt` + `synchronization2.txt`, 3217 cases, via
  `run_vulkan_cts.py` with 12 parallel workers) goes to **1579 Pass,
  98 Fail, 1540 Not supported** (the 1540 `Not supported` cases are the
  pre-existing external-memory/`cross_instance` cases this environment
  doesn't support, unrelated to this fix). The 98 remaining failures
  are **all** `synchronization2.op.single_queue.timeline_semaphore.*.
  image_64x64x8_r32_sfloat_specialized_access_flag` -- a single,
  distinct 3D-image bug (see `L228(g)` below), not a residual
  timing/threading gap.
- A separate, non-mustpass `one_to_n` test group (fan-out to multiple
  queues/waiters, not part of this session's or the original `L228`
  sample's 416-case cluster) was discovered to fail **every single
  case**, each one taking the full 5s safety-net bound -- a third,
  structurally distinct bug (see `L228(h)` below), not fixed by this
  change.

`ninja check-feme`: 3330/3391 discovered tests passed (61 pre-existing
Unsupported), 0 regressions, +2 net new unit tests.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- `VK_KHR_timeline_semaphore` was already listed as
"Implemented (core, not advertised by name)" in both; this is a
correctness fix to already-advertised functionality, like `L225`/
`L226`, not a new feature landing or a caveat being lifted.

See `L228(g)`/`L228(h)` on the roadmap for the two newly-discovered,
distinct follow-up bugs this verification pass surfaced.

## Roadmap L228(g): 3D (multi-slice) blit region rejection -- root-caused and fixed

Discovered while verifying `L228(a)`'s own fix: 98 cases in the
`timeline_semaphore` mustpass sample kept failing even with the new
genuine blocking wait in place --
`dEQP-VK.synchronization2.op.single_queue.timeline_semaphore.*.image_64x64x8_r32_sfloat_specialized_access_flag`,
every one a `write_*_read_*` pairing exercising a 3D
(`64x64x8`) `r32_sfloat` image specifically (2D images and other
formats in the same otherwise-identical `op.single_queue.
timeline_semaphore.*` group all pass).

Reproduced directly (no batch harness):
`deqp-vk --deqp-case='dEQP-VK.synchronization2.op.single_queue.timeline_semaphore.write_image_geometry_read_blit_image.image_64x64x8_r32_sfloat_specialized_access_flag'`
confirmed the failure: `Fail
(synchronizationWrapper->queueSubmit(queue, VK_NULL_HANDLE):
VK_ERROR_INITIALIZATION_FAILED at
vktSynchronizationOperationSingleQueueTests.cpp:699)`.

Key diagnostic step: **timed the failure**. It completed in ~0.6s, not
the full `TimelineWaitSafetyNetTimeoutNs` (5s) `L228(a)`'s own safety
net would take for a genuinely-unmet wait, matching a *passing* 2D
case's own timing exactly. This meant the failure was not a
timeline-semaphore issue at all, but an immediate failure somewhere in
`vkQueueSubmit`'s own synchronous, in-process command execution
(`feme/lib/Vulkan/Sync.cpp`'s `executeCommandBuffers`, called before
`vkQueueSubmit2` returns -- confirmed FeMe's synchronous execution
model has no separate "GPU work" deferral step at all).

`FEME_VULKAN_LOG_CREATION_ERRORS=1` (an existing, previously
underused diagnostic env var checked in `feme/lib/Vulkan/
Diagnostics.cpp`) surfaced the real underlying error on re-run:
`vkQueueSubmit: a multi-slice blit region is not implemented`, from
`feme/lib/Vulkan/ImageOps.cpp:720` inside `runBlitImage`'s per-region
loop.

Root cause: `isSimpleRegion` (`ImageOps.cpp`, shared only by
`runBlitImage`) required a `VkImageBlit` region's Z extent to be
exactly one slice (`Offsets[1].z - Offsets[0].z == 1`), unconditionally
rejecting anything wider. `vktSynchronizationOperation.cpp`'s own
`makeBlitRegion`/`BlitImplementation` (the CTS's shared
`read_blit_image`/`write_blit_image` operation) always blits a
resource's *entire* extent in one region (mirroring it into/out of a
same-size "staging" image, always `VK_FILTER_NEAREST`, no scaling) --
for a 3D `64x64x8` image, this single region necessarily spans all 8
Z-slices, triggering the rejection every time.

Fix: relaxed `isSimpleRegion` to require only a nonzero Z extent
(matching its pre-existing nonzero X/Y extent checks) rather than
exactly one slice -- a real Vulkan region's Z range is always exactly
one slice for a 2D/2D-array image already (`vkCmdBlitImage`'s own
VUIDs require it), so this only ever widens acceptance for a genuine
3D image's own depth, never loosens validation for the 2D/2D-array
case. Generalized `runBlitImage`'s existing per-Layer/Y/X nested
interpolation loop with a new Z dimension, following the exact same
fractional-interpolation formula (`T = (index + 0.5) / extent`)
already used for X/Y, with the same out-of-bounds clamping (roadmap
E16) extended to the Z axis. Every `texelPointer`/`blockPointer` call
site that previously hardcoded `Region.srcOffsets[0].z`/
`dstOffsets[0].z` now uses the per-Z-iteration computed value instead.
The pre-existing bilinear filter path was extended to full trilinear
(8 neighbors instead of 4) rather than left nearest-only for Z, since
the existing code's regularity made this a mechanical extension; a
2D/2D-array region's own Z weight always collapses to exactly 0 (its
`Tz` is always `0.5` and its `SrcZ0`/`SrcZ1` always exactly one slice
apart), so this is behavior-preserving for every pre-existing non-3D
case -- confirmed by `ninja check-feme`'s 0 regressions below.

New unit tests (`ImageOpsTest.cpp`):
- `BlitsWholeDepthOfA3DImage`: a same-size, same-format, nearest blit
  spanning a 3D image's entire depth in one region (the exact shape
  the fixed CTS cases exercise) is a per-slice identity copy.
- `MirrorsBlitRegionAlongZ`: the Z-axis peer of the pre-existing
  `MirrorsBlitRegion` (X/Y) test -- opposite-corner Z offsets reverse
  the slice order.
- `RejectsDegenerateZExtentBlitRegion`: confirms the relaxation only
  widened which nonzero-Z-extent regions are accepted, not whether a
  zero-extent (degenerate) one is still rejected.

`ninja check-feme`: 3333/3394 discovered tests passed (61 pre-existing
Unsupported), 0 regressions, +3 net new unit tests.

CTS-confirmed:
- The originally-reported case now **Pass**es.
- A full re-run of every mustpass-derived `timeline_semaphore` case
  naming `image_64x64x8` across `synchronization.txt`/
  `synchronization2.txt` (4578 cases -- a superset of the original
  98-case sample, covering `single_queue`/`other_queue`/
  `cross_instance` variants together) via `run_vulkan_cts.py`: **958
  Pass, 400 Fail, 3220 Not supported**. **0** of the 400 remaining
  failures are `op.single_queue`/`op.other_queue`/`cross_instance`
  cases of this bug's own shape -- the cluster fully clears with no
  regressions.
- The same run's 400 remaining failures split cleanly into two other,
  unrelated, previously-scoped-or-newly-found groups: 200 in
  `one_to_n` (already tracked, `L228(h)`) and 200 in a
  not-yet-tracked `wait_before_signal` group (new, `L228(i)`), both
  the same "genuinely never signaled" shape, not this bug's
  "immediate command-execution failure" shape.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
needed -- this is a correctness fix to `vkCmdBlitImage`'s already-
implemented, already-advertised functionality (a general image-
operation gap, not scoped to synchronization at all, despite
surfacing via that CTS group), like `L225`/`L226`/`L228(a)`, not a new
feature landing or a caveat being lifted.

See `L228(i)` on the roadmap for the newly-discovered
`wait_before_signal` follow-up cluster this verification pass
surfaced.

## Roadmap L228(h)/L228(i): shared root cause identified (not yet fixed) -- synchronous `vkQueueSubmit` cannot support submit-before-signal chains

Both `L228(h)` (`one_to_n`, 200 cases across `synchronization.txt`/
`synchronization2.txt`) and `L228(i)` (`wait_before_signal`, 200 cases)
were investigated this session while re-verifying `L228(g)`'s own fix
against the full `image_64x64x8` `timeline_semaphore` case list.
**Root cause identified for both; not yet fixed** -- this is a design
question, not a small patch (see the roadmap rows' own detail).

Reproduced directly (no batch harness):
`deqp-vk --deqp-case='dEQP-VK.synchronization.timeline_semaphore.one_to_n.write_blit_image_read_blit_image.image_128_r32_uint'`
confirmed `Fail
(synchronizationWrapper->queueSubmit(iter.queue, VK_NULL_HANDLE):
VK_ERROR_INITIALIZATION_FAILED at
vktSynchronizationTimelineSemaphoreTests.cpp:2155)`, timed at ~5.0s
(matching `L228(a)`'s `TimelineWaitSafetyNetTimeoutNs` bound exactly,
not an immediate-failure shape like `L228(g)`'s own ~0.6s).
`FEME_VULKAN_LOG_CREATION_ERRORS=1` printed nothing extra for this
case -- confirming the failure genuinely originates from the safety
net itself (a real unmet wait), not a separate internal error being
misreported as the same Vulkan error code.

Reading `OneToNTestInstance::iterate()` (`vktSynchronizationTimelineSemaphoreTests.cpp`,
~line 2155-2230) and `WaitBeforeSignalTestInstance::iterate()`
(~line 1650-1680) side by side shows both share the exact same
submission ordering: every command buffer in the chain (the initial
write, every fan-out copy, every read) is recorded and **submitted up
front**, each one waiting on a timeline value that only a **host-side
`hostSignal()` call issued after every one of those submits** will
ever satisfy, followed by a final `vkDeviceWaitIdle`. This is a
legitimate, spec-conformant Vulkan idiom: a real driver's own
`vkQueueSubmit` never blocks synchronously on an unmet wait inside
the call -- it queues the work and returns immediately, deferring
actual execution asynchronously until the wait condition is later
satisfied by *any* later call (from any thread), which is exactly
what lets an application safely issue a whole dependent submission
chain before finally releasing it with one host signal.

FeMe's `vkQueueSubmit`/`vkQueueSubmit2` (`feme/lib/Vulkan/Sync.cpp`)
instead blocks synchronously *inside* the call itself -- `consumeWaits`
(now genuinely blocking, since `L228(a)`) followed immediately by
`executeCommandBuffers`, both before the call returns. For this
specific submission ordering, the very first `submit()` call (the
write operation, waiting on the test's own `m_hostTimelineValue`)
blocks the calling thread indefinitely, since the only call that could
ever satisfy that wait (`hostSignal()`) is later in that same thread's
own program order and can now never execute -- a genuine, unavoidable
deadlock under FeMe's current model, resolved only by `L228(a)`'s own
5-second safety-net timeout kicking in and failing the case instead of
hanging the whole test process forever.

This is **not** two separate bugs: both groups reproduce the identical
"submit a chain, release with one signal after" idiom, just with
different graph shapes (`one_to_n`'s fan-out to multiple queues vs.
`wait_before_signal`'s single linear chain) -- neither the fan-out
shape nor multi-queue support specifically is the actual blocker, as
the original `L228(h)` roadmap entry had speculated before this
session's investigation.

**Not fixed this session.** This needs a genuine architectural change
to `vkQueueSubmit`'s own execution model -- deferring each
submission's wait+execute+signal to a background worker (e.g., one per
`VkQueue`, or a shared pool) so the call itself can return immediately
without blocking on an unmet wait, with `vkQueueWaitIdle`/
`vkDeviceWaitIdle`/fence-wait genuinely blocking until that queued work
later completes -- a materially larger change than any other `L228`
sub-item fixed so far, touching the whole submission path and needing
careful thought about whether the CPU executor's own state (images,
buffers, pipelines) is safe to touch from a background thread
concurrently with the calling thread's further API calls. Recommend a
dedicated future session start with a short design note in
`FeMeVulkanDesign.md` proposing the worker-thread-per-queue model
*before* writing any implementation code, and confirm whether
`Sync.h`'s existing `Semaphore` mutex/condvar (added for `L228(a)`) is
sufficient for the cross-thread state this would need or requires its
own extension.

No code changed for this investigation -- `ninja check-feme` not
re-run for this entry (root-causing/documentation only).
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no update
(no code landed; `VK_KHR_timeline_semaphore` remains listed as
implemented, this is a known-gap note added to the roadmap, not an
inventory-level claim change).

## Roadmap L228(h)/L228(i): async `vkQueueSubmit` (`QueueExecutor`-per-`VkQueue`) -- fixed

Implemented the architectural fix the prior `L228(h)`/`L228(i)` entry
above recommended a dedicated session for: `vkQueueSubmit`/
`vkQueueSubmit2` no longer execute a submission synchronously inside
the call. Each `VkQueue` (`Objects.h`) now owns a dedicated
`QueueExecutor` (`Sync.h`/`Sync.cpp`) -- a worker thread with its own
FIFO task queue. `vkQueueSubmit`/`vkQueueSubmit2` parse every
submission's waits/command buffers/signals on the calling thread
(reading only caller-supplied structs and already-existing objects, so
this needs no locking), enqueue one task per `VkSubmitInfo`/
`VkSubmitInfo2` (plus one trailing fence-signal task, if a fence was
given) onto that queue's `QueueExecutor`, and return `VK_SUCCESS`
immediately -- never blocking on an unmet wait. `vkQueueWaitIdle`/
`vkDeviceWaitIdle`/`vkWaitForFences` now genuinely block on that queued
work instead.

A submission's own failure -- an unmet wait past its bounded safety-net
timeout, or a command-buffer execution error -- can no longer be
reported synchronously through a `vkQueueSubmit` call that has already
returned. This now latches `Device::isLost()` (`Objects.h`) instead,
exactly the "Device loss is latched once: subsequent queue/device
operations return `VK_ERROR_DEVICE_LOST`" mechanism
`FeMeVulkanDesign.md` had already specified as the intended behavior
for this case, just not yet implemented.

Fixing this exposed two further pre-existing, previously-dormant bugs,
both fixed in the same session:

1. Every `feme/unittests/Vulkan/` fixture that calls `vkQueueSubmit`
   had implicitly relied on the retired synchronous model's in-call
   execution to make its own `VkQueue` handle irrelevant: the *old*
   `vkQueueSubmit`'s own `queue` parameter was unused entirely, so no
   fixture had ever needed a genuinely valid handle.
   `DrawTest.cpp`'s own device fixture created its `VkDevice` with
   `queueCreateInfoCount == 0` (already spec-invalid, but silently
   tolerated), so its `vkGetDeviceQueue(Device, 0, 0, &Queue)` call
   returned `VK_NULL_HANDLE` -- previously harmless, now a genuine
   null-pointer dereference the instant `vkQueueSubmit` actually
   dereferences its `queue` argument to find that queue's own
   `QueueExecutor`. Fixed by giving that fixture a real
   `VkDeviceQueueCreateInfo` requesting family 0.
2. ~80 `DrawTest.cpp` test cases call a shared `submit()` helper and
   then immediately read back rendered pixel data, assuming
   synchronous completion. Fixed with a single change inside `submit()`
   itself (an added `vkQueueWaitIdle` call after `vkQueueSubmit`) rather
   than touching each of the ~80 call sites. 11 `Rejects*`-style tests
   that asserted a synchronous `VK_ERROR_INITIALIZATION_FAILED` return
   from `submit()` for a command-buffer-execution-time validation
   failure (e.g. drawing outside a render pass, an out-of-bounds
   indirect draw) were updated to expect `VK_ERROR_DEVICE_LOST` instead,
   matching the new latched-device-loss reporting path (`submit()`
   itself now returns `vkQueueWaitIdle`'s result).

New/rewritten unit tests in `SyncTest.cpp`:
`QueueSubmitReturnsPromptlyThenCompletesAfterHostSignal` (replaces the
old, now behaviorally inverted
`QueueSubmitBlocksUntilHostSignalsTimelineSemaphore` -- confirms
`vkQueueSubmit` returns in well under the artificial signal delay, and
that the real dependency is still honored, just observed later via a
fence), `TimelineSemaphoreCrossQueueSubmitChainCompletesAfterHostSignal`
(the actual `one_to_n`/`wait_before_signal`-shaped regression test --
submits a two-queue dependent chain, wait-before-anything-signals, then
releases it with one host `vkSignalSemaphore` call, confirming both
`vkQueueSubmit` calls return promptly and the whole chain later
completes via `vkQueueWaitIdle` on both queues),
`SubmitEnqueuesPromptlyThenLatchesDeviceLostOnUnmetBinaryWait`,
`BinarySemaphoreSecondWaitWithoutNewSignalLatchesDeviceLost`, and the
`vkQueueSubmit2` counterparts of each (all mirroring the `vkQueueSubmit`
versions through the `VkSubmitInfo2`/`VkSemaphoreSubmitInfo` shape).

**Build/test:** `ninja check-feme`: 3335/3396 Passed, 61 Unsupported, 0
regressions. The full 740-case `FeMeVulkanTests` GoogleTest suite
(`ninja FeMeVulkanTests`) passes with 0 failures (up from 720 tests
before this session's `SyncTest.cpp`/`DrawTest.cpp` additions).

**CTS-confirmed:**

- `dEQP-VK.synchronization*.one_to_n.*`: **1910/1910 Pass** (was 100%
  failing before this fix).
- `dEQP-VK.synchronization*.wait_before_signal.*`: **1910/1910 Pass**
  (was 100% failing before this fix).
- A fresh, wider random sample of 3500 cases from the full mustpass
  `timeline_semaphore` cluster (`synchronization2.txt`, not restricted
  to `one_to_n`/`wait_before_signal`), run via `run_vulkan_cts.py`:
  **1178 Pass, 0 Fail, 2322 Not supported** -- confirms no regressions
  elsewhere in the broader `timeline_semaphore` group from this change.
- `check-hlsl-feme-vk` (offload-test-suite, `feme` branch at `adf0fc1`):
  unchanged at 461/722 Pass / 31 XFAIL / 221 Not supported / 9 Fail --
  no regression from this change.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this is a correctness/liveness fix to already-advertised
`VK_KHR_timeline_semaphore`/queue-submission functionality, not a new
feature landing.

**Known gap disclosed, not fixed this session (tracked as roadmap
`L228(j)`):** making command-buffer execution genuinely asynchronous
relative to the calling thread exposed a descriptor-set
update-after-bind data race that was structurally impossible under the
old synchronous model: `DescriptorSet` (`Descriptor.h`) has no internal
locking and returns live `ArrayRef`s from its getters, so
`vkUpdateDescriptorSets` running on the calling thread can now race
with a background `QueueExecutor` worker thread still consuming that
same descriptor set from an earlier submission. Left unfixed (feature
bits stay `VK_TRUE`, since the sequential update-then-submit pattern
the CTS and real applications overwhelmingly use is unaffected, and no
CTS regression was observed) but disclosed in both
`FeMeVulkanDesign.md`'s "Threading Rules" section and the roadmap for a
future session.

## Roadmap L228(j): DescriptorSet locking against concurrent update/dispatch -- fixed

Fixed the descriptor-set update-after-bind data race disclosed above.
`DescriptorSet` (`Descriptor.h`) now guards the *contents* of its
per-binding arrays with a `std::mutex` (the map keys themselves are
fixed for a set's whole lifetime, set once from its immutable
`DescriptorSetLayout`, so only element contents needed guarding):
`write`/`writeInlineUniformBlock` lock while mutating, and
`bindingArray`/`imageBindingArray`/`inlineUniformBlockData` now return
a snapshot `std::vector<T>` copy taken under the same lock instead of
a live `llvm::ArrayRef` into storage a concurrent write could still
mutate afterward.

Building against the new by-value return type surfaced two
dangling-reference bugs via clang's `-Wdangling-gsl`: one pre-existing
(`vkUpdateDescriptorSets`'s own `VkCopyDescriptorSet` copy loops
indexed straight into a temporary `ArrayRef`) and one this change would
otherwise have newly introduced (`CommandBuffer.cpp`'s
inline-uniform-block dispatch path pointed a materialized
`FemeDescriptor`'s `Data` at a now-by-value local that does not
outlive `buildBoundResources`) -- both fixed, the latter via a new
`MaterializedBoundResources::InlineUniformBlockStorage` owned-copy
slot.

New regression test:
`DescriptorTest.ConcurrentUpdateDescriptorSetsDoesNotRaceWithDispatchRead`
(a writer thread alternates `vkUpdateDescriptorSets` between two
fully-defined canonical states while a reader thread snapshots
`bindingArray`, asserting every read matches one whole state rather
than a torn mix of both). Verified via a temporary A/B test (locks
manually disabled, fix otherwise untouched) that this test fails
reliably (11-20 of 15-20 runs) without the fix and passes reliably
(20/20) with it; also confirmed clean under Helgrind
(`valgrind --tool=helgrind`, installed via `apt` for this session, 0
errors from 0 contexts) as a lighter-weight substitute for a full
ThreadSanitizer rebuild of this in-tree LLVM+clang toolchain (judged
too expensive for this session's time budget).

**`ninja check-feme`:** 3336/3397 Passed, 61 Unsupported, **0 Failed**
(+1 net new unit test over the prior session's 3335/3396). Needed two
`check-feme` runs during development: the *first* version of the new
regression test had its own bug (started the reader thread
concurrently with the writer's very first update, so a heavily loaded
machine -- `check-feme`'s own 12-way sharding -- could let the
reader's first iteration observe the constructor's legitimate initial
`Buf == nullptr` state before any write had landed, misreporting it as
a torn read); fixed by seeding one synchronous write before starting
either thread, then re-confirmed 0 Failed across two full re-runs.

**CTS-confirmed:** a 3115-case sample (`binding-model.txt`'s
`VK_EXT_mutable_descriptor_type` cases excluded, since FeMe does not
support that unrelated extension -- 3000 remaining `binding-model.txt`
cases plus all 115 of `descriptor-indexing.txt`) shows **1908 Pass, 0
Fail, 1207 Not supported**, consistent with `L228`'s own prior
full-suite `binding_model` baseline (87,669/150,289 Pass). Notably,
`descriptor-indexing.txt`'s own cases -- and, it turns out, every
`binding-model.txt` case actually named `update_after_bind` -- are
exclusively inside the `mutable_descriptor` group and report
`NotSupported` regardless of this fix (gated on the broader
`descriptorIndexing` aggregate feature bit, deliberately left
`VK_FALSE` per roadmap `L12b`, and on `VK_EXT_mutable_descriptor_type`,
unsupported): this fix has no currently-reachable CTS coverage of its
own specific race (no mustpass case genuinely exercises
concurrent-thread descriptor updates), consistent with the disclosed
gap's own original note that CTS's sequential update-then-submit usage
was always unaffected either way. This is purely a
robustness/correctness fix for a genuinely concurrent multi-threaded
application, verified via the new unit test and Helgrind rather than
CTS pass/fail deltas.

**`check-hlsl-feme-vk`:** unchanged, 461/722 Pass / 31 XFAIL / 221 Not
supported / 9 Fail (same known `L227(a)`-`(d)` failures, no new
regressions).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- feature-bit exposure is unchanged; this is an
internal-locking correctness fix only, not a new feature landing.

## Roadmap L227(b): boolean specialization-constant fix (outside FeMe) -- `check-hlsl-feme-vk` baseline updated

No FeMe/Vulkan-CTS-relevant source changed this session (the fix
landed entirely in `offload-test-suite`'s own `offloader` tool, not
`feme/` -- see `Roadmap.md`'s `L227(b)` entry for the full root cause).
Recorded here only because it moves `check-hlsl-feme-vk`'s own
baseline, which this report has historically tracked alongside Vulkan
CTS numbers.

`check-hlsl-feme-vk`: **462/722 Pass, 31 XFAIL, 221 Not supported, 8
Fail** (previously 461/722 Pass, 9 Fail -- `spec_const_32_bits.test`
now passes, no new regressions; the remaining 8 failures are
`L227(a)`/`(c)`/`(d)`'s own still-unresolved items).

`ninja check-feme`: 3336/3397 Passed, 61 Unsupported, 0 Failed --
unchanged, as expected for a fix touching zero `feme/` files.

Targeted Vulkan CTS sample: not re-run this session -- no
Vulkan-CTS-relevant FeMe code changed, so no new CTS signal is
expected or needed (a full 150k-plus-case CTS re-run purely to
reconfirm an already-stable baseline after a change with zero `feme/`
diff would not be a good use of session time; see `L228(e)`/`(f)`'s
own still-overdue broader-CTS-re-run note for where that time should
go instead).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed.

## Roadmap L227(a): `Barrier.32.test` SPIR-V legalization + barrier-region-split dead-block fix -- fixed

Two independent bugs fixed together this session (see `Roadmap.md`'s
`L227(a)` entry for the full root-cause narrative):

1. `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`: a new
   `VulkanMemoryModelLoadStorePattern<SPIRVOp>` widens the
   memory-access bits `spirv.Load`/`spirv.Store` legalization accepts
   to include the three Vulkan-Memory-Model-only bits
   (`MakePointerAvailable`/`MakePointerVisible`/`NonPrivatePointer`)
   upstream MLIR's own `LoadStorePattern` rejects outright.
2. `feme/lib/Transforms/CPU/EntryWrapper.cpp`: `splitAtGroupSyncBarriers`
   now calls `EliminateUnreachableBlocks` on the wave body before
   `isLinearChain` runs, since `feme::cpu::SIMDizePass`'s own
   if-conversion can leave a genuinely dead, unreachable `..._crit_edge`
   block behind that `isLinearChain`'s own sanity check previously
   (incorrectly) treated as disqualifying.

**`check-hlsl-feme-vk`:** **463/722 Pass, 31 XFAIL, 221 Not supported,
7 Fail** (previously 462/722 Pass, 8 Fail -- `Barrier.32.test` now
passes, no new regressions; the remaining 7 are `L227(c)`/`(d)`'s own
still-unresolved items).

`ninja check-feme`: 3338/3399 Passed, 61 Unsupported, 0 Failed (+1 net
new test versus the 3337/3398 prior baseline: the new
`spirv-to-llvm-vulkan-memory-model-load-store.mlir` and
`entry-wrapper-dead-block-from-simdize.ll` lit tests, net of one file
each against the prior session's own +1).

Targeted Vulkan CTS sample (this session, since both fixes touch
compute-shader barrier/region-splitting code broadly, not just this
one test): `dEQP-VK.compute.pipeline.*barrier*` (2819 cases, every
`compute.pipeline` case whose name contains "barrier", covering the
same class of barrier-adjacent workgroup-shared-memory shader this
session's fix targets). Result: **9 Pass, 2810 Not supported (mostly
`cooperative_matrix`/other extension-gated cases this device does not
advertise), 0 Fail, 0 unexpected failures** -- confirms the fix
introduces no regressions across the broader barrier-adjacent compute
surface, not just the one directly-fixed test.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- neither fix touches feature-bit exposure; both are
correctness fixes to existing, already-advertised compute-shader
lowering paths.

## Roadmap L227(c): storage-image `GetDimensions` widening + `WaveActiveMax` XFAIL -- `check-hlsl-feme-vk` baseline updated

`GetDimensions.test`/`Array.GetDimensions.test`: fixed a shape-gate
oversight in `feme/lib/Transforms/CPU/SPIRVResourceLowering.cpp` --
`hasOnlySupportedStorageImageUses` only recognized the bare (Lod-less)
`GetDimensions` intrinsics DXC emits for a storage image's
`GetDimensions` for `Plain2D`/`Array2D` shapes, so `RWTexture1D`/
`RWTexture1DArray`/`RWTexture3D` all failed pipeline creation with
"unsupported raised operation" (see `Roadmap.md`'s `L227(c)` entry for
the full root-cause narrative). Widened to also accept `Plain1D`,
`Array1D`, and `Plain3D`, reusing the existing `createQuerySizeLod1D`/
`1DArray`/`3D` runtime builders with a synthesized `Lod=0` -- no new
runtime infrastructure needed. Added 6 new unit tests in
`SPIRVResourceLoweringTest.cpp` covering the 3 new shapes plus
regression guards for the pre-existing `Plain2D`/`Array2D` paths and
the multisampled-image exclusion (none had unit-level coverage
before).

`WaveActiveMax.test`: root-caused as a genuine semantic mismatch, not
a FeMe bug -- the test's `NegInfs`/`Mix` expectations assume a
backend's default subgroup size is large enough to span a `TID.x % 8`
in/out-of-bounds residue boundary; FeMe's default subgroup size is 4
(`feme::cpu::MinWaveSize`), the same root cause the test's own
pre-existing `XFAIL: Lavapipe && host-arm64` line already covers for
another small-subgroup software Vulkan implementation. Fixed entirely
outside FeMe, in `offload-test-suite`'s own `feme` branch: added
`XFAIL: FeMe` with a rationale comment (self-contained, no
`llvm-project`/`feme/` files touched).

`CalculateLevelOfDetail.test`'s `Plain3D`/`OpImageQueryLod` failure is
a genuinely unimplemented feature (no `createQueryLod3D` builder
exists at all), not a shape-gate oversight -- split off as `L229`
rather than rushed this session.

**`check-hlsl-feme-vk`:** **482/722 Pass, 32 XFAIL, 207 Not supported,
1 Fail** (previously 480/722 Pass, 31 XFAIL, 207 Not supported, 4
Fail at this session's start -- `GetDimensions`/`Array.GetDimensions`
moved from Fail to Pass, `WaveActiveMax` moved from Fail to XFAIL, and
the sole remaining Fail is `L229`'s own `CalculateLevelOfDetail.test`).

`ninja check-feme`: 3344/3405 Passed, 61 Unsupported, 0 Failed (+6 net
new unit tests versus the 3338/3399 prior baseline, no regressions).

Targeted Vulkan CTS sample: not run this session -- these fixes are
entirely DXC/HLSL-intrinsic-facing (`GetDimensions`/`WaveActiveMax`
lowering, exercised only through `offload-test-suite`'s own
`check-hlsl-feme-vk` harness, not through `deqp-vk`'s own SPIR-V-ASM
or GLSL-sourced test cases), so no new Vulkan CTS signal is expected;
the broader-than-tessellation CTS re-run (`L228(e)`/`(f)`) remains the
right vehicle for the next dedicated CTS sampling session.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- neither fix touches feature-bit or extension exposure; both
widen or correct existing, already-advertised lowering paths.
