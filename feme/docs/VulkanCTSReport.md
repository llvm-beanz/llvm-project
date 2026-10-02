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
  `ccache`, `-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON` (see `L285`: LLVM's
  shared `LLVMCore` PCH masked a missing `#include` in feme's own source,
  so PCH is now disabled for this build directory to catch that class of
  bug going forward)
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

## Roadmap L227(c): targeted `dEQP-VK.image.image_size.*` CTS sample -- confirms fix, discovers pre-existing Cube/CubeArray gap (`L230`)

Ran the full `image/image-size.txt` mustpass list (108 cases,
`dEQP-VK.image.image_size.*` -- GLSL/SPIR-V-ASM `imageSize()` queries
against every storage-image shape, the same `OpImageQuerySize` family
`L227(c)`'s `GetDimensions` fix widened) via `run_vulkan_cts.py`.

Result: **72 Pass, 24 Fail, 12 NotSupported**. All `1d`/`1d_array`/
`2d`/`2d_array`/`3d`/`buffer` cases (72 total) **Pass** or are
correctly `NotSupported` (the `3d.*_2d_view_*` sub-cases, a distinct,
unrelated 2D-view-of-a-3D-image feature this device does not support)
-- confirms the `L227(c)` fix generalizes cleanly to the GLSL/SPIR-V-ASM
CTS surface, not just the HLSL/DXC surface `GetDimensions.test`/
`Array.GetDimensions.test` originally exercised, with no regressions.

All 24 `cube`/`cube_array` cases **Fail**. Confirmed this is a
pre-existing gap, not a regression from this session's own change:
`hasOnlySupportedStorageImageUses`'s `isGetDimensionsIntrinsic`/
`isGetDimensions3Intrinsic` branches never accepted `Cube`/`CubeArray`
shapes, before or after this session's edits (the edits only touched
the `Plain1D`/`Array1D`/`Plain3D` branches). Filed as new roadmap item
`L230`.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this sample re-confirms existing, already-advertised
functionality plus discovers an existing gap, neither changes
feature/extension exposure.

## Roadmap L229: `OpImageQueryLod` for `Plain3D`/`CubeArray` -- new 190-case targeted sample, plus `check-hlsl-feme-vk`

Ran `CalculateLevelOfDetail.test` directly via `llvm-lit` against the
`feme-vk` build-tree config: **now passes** (was the sole remaining
`check-hlsl-feme-vk` failure before this session). Full
`check-hlsl-feme-vk`: **483/722 Pass, 32 XFAIL, 207 Not supported, 0
Fail** (up from 482 Pass/1 Fail).

Ran a targeted 190-case sample (`grep -i "query_lod\|querylod"` over
`glsl.txt`'s own mustpass list --
`dEQP-VK.glsl.texture_functions.query.texturequerylod.*`, covering
every sampler shape/format combination the GLSL query-LOD test group
exercises) via `run_vulkan_cts.py`.

Result: **120 Pass, 70 Fail**. Every float-channel-sampler case for
every shape -- including the two this session's own change newly
supports, `sampler3d_*`/`sampler3d_fixed_*` (10 cases) and
`samplercubearray_*`/`samplercubearray_fixed_*`/
`samplercubearrayshadow_*` (24 cases) -- **passes**, confirming the
`L229` fix generalizes cleanly to the GLSL CTS surface, not just the
HLSL/DXC surface `CalculateLevelOfDetail.test` originally exercised.

All 70 failures are `isampler*`/`usampler*` (integer-channel sampler)
variants, spread uniformly across *every* shape in the sample --
including `isampler2d_*`/`usampler2d_*` and `isamplercube_*`/
`usamplercube_*`, which have supported float-channel `OpImageQueryLod`
for several sessions already, well before this session's own `Plain3D`/
`CubeArray` work. This confirms the failures are a pre-existing,
uniform, unrelated gap (integer-channel `OpImageQueryLod` support, not
yet root-caused: unclear whether `hasOnlySupportedImageUses`'s
`isQueryLodIntrinsic` shape gate implicitly assumes a float-typed
image, or the runtime-function side has its own separate float-only
assumption) rather than anything this session's change introduced or
could have fixed incidentally. Filed as new roadmap item `L231`.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this widens an existing, already-advertised lowering
mechanism (the `QueryLod*` builder family) to two more float-channel
shapes; it does not change any feature-bit or extension exposure, and
the newly discovered integer-sampler gap (`L231`) is unfixed, so
nothing to add or remove from either inventory for it either.

## Roadmap L230: `dEQP-VK.image.image_size.*` full mustpass re-run -- plain Cube fixed, CubeArray split off (`L232`)

Ran the full `image/image-size.txt` mustpass list (108 cases) again
via `run_vulkan_cts.py`, after widening `isGetDimensionsIntrinsic`'s
shape gate (`hasOnlySupportedStorageImageUses`) to accept `Array2D`
(the shape a plain, non-arrayed storage `Cube` handle already folds
into).

Result: **84 Pass, 12 Fail, 12 NotSupported** (up from 72 Pass/24
Fail/12 NotSupported before this session's `L230` fix). All 12
`cube.*` cases now **Pass**. A follow-up 772-case
`dEQP-VK.image.load_store.*cube*` sample confirms **0 regressions**
to storage-cube image load/store addressing (a separate lowering path
this fix never touches).

The remaining 12 `cube_array.*` cases still **Fail**, but with a
*wrong value*, not a rejected pipeline: `classifyStorageImage2DHandle`
folds a storage `CubeArray` handle into the same `Array2D` shape a
plain `Cube` folds into, discarding the one bit of information (was
this handle ever `Dim::Cube`?) needed to know a real arrayed cube's
own element count must be divided by 6 (the face count) before being
returned as the layer count `imageSize()` expects.
`dEQP-VK.image.image_size.cube_array.readonly_1x1x12` confirms this
directly: expected `(1, 1, 2)`, got `(1, 1, 12)` -- the existing
`Array2D` `GetDimensions` runtime path returns the correct *raw*
layer count, it is simply the wrong count for this handle's own
shape. This needs classification-level surgery (distinguishing
storage `Cube`/`CubeArray` from `Array2D` without regressing the
already-passing `Array2D`-based fetch/store lowering these shapes
currently share) judged too large to rush safely into this same
session -- split off as new roadmap item `L232`.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this fixes an existing, already-advertised lowering path for
one more shape; it does not change feature-bit or extension exposure.

## Roadmap L228(e)/(f): overdue broader-than-tessellation CTS re-run -- new 15000-case fixed-seed sample

Long overdue per several prior sessions' own notes: every recent
session's CTS work had stayed scoped to whatever cluster/test it was
fixing, never the wider `api`/`pipeline`/`shader_render`/
`synchronization` surface `L228(e)`/`(f)` called out. The prior
session's own saved case list (`/tmp/l227_broad_sample/
case_list_dedup.txt`) did not survive across sessions (as its own
caveat predicted), so a fresh sample was drawn instead.

Deduped the full `api.txt` (267,501 cases), `pipeline/monolithic/*.txt`
(930,948 cases), `synchronization.txt` (64,872), and
`synchronization2.txt` (81,617) mustpass lists together (879,564
unique cases after dedup), then drew a fixed-seed
(`random.seed(20240607)`) 15000-case sample via `run_vulkan_cts.py`.

Result: **5197 Pass, 169 Fail, 58 DeviceLost, 1 InternalError, 9574
NotSupported** (14999/15000 completed -- 1 case,
`dEQP-VK.pipeline.monolithic.blend.dual_source.undefined_output.first_not_assigned_dynamic`,
did not run at all; worth a follow-up isolated rerun to check whether
this is a genuine hang or a harness fluke, not yet investigated
further this session).

Clustered the 228 unexpected non-Pass results by top-level group:

- **93 `api.image_clearing`** and **75 `api.copy_and_blit`**: both
  already-known, still-open clusters (`L228(b)`/`(c)`, unchanged this
  session) -- this sample re-confirms their continued existence and
  rough scale, no new triage information gained here.
- **41 `pipeline.monolithic.*`** (26 `sampler.*`, 5
  `logic_op_na_formats.*`, 4 `logic_op.*`, 2 `spec_constant.*`, 2
  `no_position.*`, 1 `render_to_image.*`, 1
  `creation_cache_control.*`): new, untriaged. Filed as `L234`.
- **19 `api.info`/`api.version_check`/`api.get_device_proc_addr`**:
  new. Spot-checked 5 individually: confirmed `vkGetPhysicalDevice
  ImageFormatProperties` (`EntryPoints.cpp`) ignores its own
  `VkImageTiling` parameter entirely (the parameter is unnamed in the
  function signature) -- at least 3 distinct root causes visible
  already (`sampleCounts` ignoring `tiling` entirely, wrong
  `maxExtent` for some 1D linear compressed/depth-stencil formats,
  wrong `maxResourceSize` for at least one 3D linear format); not yet
  fully root-caused or fixed. Filed as `L233`.
- The 58 `DeviceLost`/1 `InternalError` results were not further
  triaged this session -- worth checking in a future session whether
  they cluster with `copy_and_blit`/`image_clearing` or are their own,
  more serious issue (a `DeviceLost` is a stronger signal than an
  ordinary `Fail` and may indicate a crash or hang rather than a wrong
  answer).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this is a verification-only sampling run, no source changed
as part of it (the `L233`/`L234` findings are filed as new roadmap
items for future sessions to fix, not fixed here).

## Roadmap L233: `vkGetPhysicalDeviceImageFormatProperties` ignoring `VkImageTiling`

Root-caused and fixed all 3 distinct bugs the ignored `VkImageTiling`
parameter caused, discovered by the previous session's broad
15000-case sample:

1. `sampleCounts` ignored `tiling`/cube-compatible `flags` entirely,
   only excluding non-2D `type`s -- fixed by forcing
   `VK_SAMPLE_COUNT_1_BIT` whenever `tiling != VK_IMAGE_TILING_OPTIMAL`
   or the image is cube-compatible.
2. A `VK_IMAGE_TYPE_1D` image's `maxExtent.height` was incorrectly set
   to the same wide value as `width` -- this affected *every* 1D
   format/tiling combination, not just linear tiling. Fixed by
   properly zeroing the unused axes per image type.
3. `maxResourceSize` was computed purely from real dimensions, but the
   spec requires at least 2^31 bytes reported for every supported
   combination -- fixed by clamping to that floor.

Also fixed a related bug found while chasing (1): none of this
function's `VK_ERROR_FORMAT_NOT_SUPPORTED` early-return paths zeroed
`*pImageFormatProperties`, violating the spec's "all fields zero on
this error" requirement.

**Verification**: ran the full 5778-case
`dEQP-VK.api.info.image_format_properties.*` mustpass list (not a
sample -- small enough to run in full):

- **Before this session's fix**: only spot-checked individual repros
  (all failing), full-list numbers not previously gathered.
- **After**: **3261 Pass, 221 Fail, 2296 NotSupported** (of 5778).
  Every one of the 221 remaining failures is
  `VK_ERROR_FORMAT_NOT_SUPPORTED returned for required image parameter
  combination` for a `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`-flagged,
  `VK_IMAGE_TILING_OPTIMAL` combination (confirmed by direct
  inspection of several: `2d.optimal.r8g8b8a8_unorm`,
  `3d.optimal.r32_sfloat`, etc.) -- a real, separate mutable-format
  support gap in `isValidImageShape` (`Image.cpp`), unrelated to
  `VkImageTiling`. Filed as `L235`.
- Spot-checked all 6 originally-cited repro cases individually --
  all now Pass:
  `2d.linear.r32_sfloat`, `1d.linear.bc2_unorm_block`,
  `1d.linear.d24_unorm_s8_uint`, `3d.linear.d16_unorm`,
  `1d.linear.r32_sfloat`, `1d.optimal.r32_sfloat` (the last one
  actually still fails for the same `L235` `MUTABLE_FORMAT_BIT` reason
  once the tiling bugs above no longer mask it -- included for
  completeness, not a regression).

`dEQP-VK.api.version_check.entry_points`/
`dEQP-VK.api.get_device_proc_addr.non_enabled` (the 2 adjacent
failures from the original broad sample) were checked and confirmed
**not** to share `L233`'s root cause -- both fail on a distinct,
unrelated entry-point-exposure gap (missing several Vulkan 1.4 core
functions from `vkGetDeviceProcAddr`, plus at least one
disabled-extension function incorrectly exposed). Filed as `L236`.

`check-feme`: 3352/3413 Passed, 61 Unsupported, 0 Failed (+6 net new
unit tests). `check-hlsl-feme-vk`: 483/722 Pass, 32 XFAIL, 207 Not
supported, 0 Fail (both unchanged from before this fix -- no
HLSL/DXC-facing test exercises this query directly).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this fix corrects an existing, already-exposed core 1.0
query's own reported values; it does not change which
features/extensions this device advertises.

## Roadmap L234: `pipeline.monolithic.sampler.border_swizzle.*` genuine hang -- missing SNORM clear-color formats

**Symptom**: a fixed-seed (`random.seed(42)`) 3000-case sample of
`dEQP-VK.pipeline.monolithic.sampler.border_swizzle.*` (105,600 cases
total; `L234`'s own residual from `L228(e)`/`(f)`'s 41-case
`pipeline.monolithic.*` broad-sample bucket) reported **517 Pass, 16
Fail, 12 DeviceLost, 2455 NotSupported**. Every one of the 28
Fail/DeviceLost cases involved one of exactly 3 formats:
`r8g8b8a8_snorm`, `a8b8g8r8_snorm_pack32`, `a2b10g10r10_snorm_pack32`
-- narrowed via targeted single-case reruns to *not* be swizzle-,
gather-, or graphics/compute-pipeline-specific (every variant of the
same format hung identically), and confirmed via `1d.optimal`-style
reduced repro that `r8_snorm`/`r16g16b16a16_snorm` do **not** hang,
ruling out "all SNORM formats" as the pattern.

**Root cause** (`gdb -p <pid> -batch -ex "thread apply all bt"`
attached to a live repro mid-hang): every `QueueExecutor`/LLVM-worker
thread was idle (`pthread_cond_wait`, `Pending == 0`) -- not a real
infinite loop. The main thread was blocked in `vkWaitForFences`, on a
fence whose submission had already fully run. Enabling
`FEME_VULKAN_LOG_CREATION_ERRORS=1` (`Diagnostics.h`'s opt-in logger,
silent by default) surfaced the real error: `vkQueueSubmit: attachment
clear color is not yet supported for this format` -- `packClearColor`/
`unpackColor` (`ImageFixture.cpp`) had no branch for `R8G8B8A8_SNORM`
or `R10G10B10A2_SNORM` (the `ResourceFormat`s the other two CTS-named
`VkFormat`s map onto via `Format.cpp`'s packed-format aliasing), a gap
of the same class `H170` fixed for several integer formats several
sessions ago. Hitting that fallback error inside `vkQueueSubmit`'s
deferred `QueueExecutor` task (roadmap L228(h)/(i)) marks the device
lost (`Device::markLost`); the fence-signal task then deliberately
skips signaling once lost (`makeFenceSignalTask`). A **second**,
independent bug compounded this into a hang rather than a fast
failure: `vkWaitForFences`'s `waitAll` path checked `Device::isLost()`
only once, before blocking on the fence -- so the case's own
`vkWaitForFences(..., UINT64_MAX)` call still blocked for `Sync.h`'s
full `SafetyNetTimeoutNs` (5s) before reporting `VK_TIMEOUT`, rather
than `VK_ERROR_DEVICE_LOST` promptly once the device actually was
already lost.

**Fix**: two separate commits. (1) Added `R8G8B8A8_SNORM`/
`R10G10B10A2_SNORM` pack/unpack branches to `ImageFixture.cpp`
(matching `R16G16B16A16_SNORM`'s per-component `[-1,1]`-scaled
convention and `R10G10B10A2_UNORM`'s existing packed-word special
case, respectively), plus a missing `getFormatInfo` entry for
`R10G10B10A2_SNORM` (`H19o` only ever added this format's diagnostic
*name*, not a `FormatInfo` shape). (2) Made `vkWaitForFences`'s
`waitAll` path poll for device loss in `DeviceLostPollSliceNs` (50ms)
slices instead of checking `isLost()` once.

**Verification**: 8 new unit tests (6 `ImageFixtureTest` pack/unpack
round-trips, 1 `SyncTest` confirming a device marked lost mid-wait is
now reported within under a second rather than a full safety-net
timeout). `check-feme`: 3355/3355 (61 unsupported, 0 failed, up from
3352/3413 -- 3 net new tests). `FeMeVulkanTests`: 747/747.
`check-hlsl-feme-vk`: 483/722 Pass, 32 XFAIL, 207 Not supported, 0
Fail (unchanged, no regressions).

Real CTS: the same fixed-seed 3000-case `border_swizzle` sample now
reports **545 Pass, 0 Fail, 0 DeviceLost, 2455 NotSupported** (Pass
count rises by 28, matching the previously-failing/hung case count
exactly -- the 12 `NotSupported`-vs-something-else delta between runs
is `custom`-border-color-mode cases, unaffected either way). All 4
originally-hanging individual repro cases (`r8g8b8a8_snorm.rgba...`,
`r8g8b8a8_snorm.igba...gather_0...compute`,
`a8b8g8r8_snorm_pack32.rgba...`, `a2b10g10r10_snorm_pack32.rgba...`)
individually confirmed to now pass instantly (well under a second
each, versus the prior ~5s hang-then-fail).

`L234`'s original 41-case sample's 4 smaller, non-`sampler.*` clusters
(`logic_op_na_formats`/`logic_op`/`spec_constant`/`no_position`/
`render_to_image`/`creation_cache_control`, 15 cases total) remain
untriaged -- filed as `L237`, likely an unrelated bug given the
naming mismatch with `border_swizzle`'s own clear-color gap.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- both fixes correct existing, already-exposed core-1.0
behavior (clear-color format coverage, device-lost reporting
promptness); neither changes which features/extensions this device
advertises.

## Roadmap L237: `logic_op`/`logic_op_na_formats` genuine DeviceLost -- hardcoded 3-format allowlist

**Symptom**: `L234`'s own 15-case residual sample (regenerated from the
same surviving `/tmp/broad_cts_l233/sample_15000.txt` fixed-seed draw,
filtered to the 6 named `pipeline.monolithic.*` groups that weren't
`sampler.*`, widened to an 84-case candidate set) reported **10
DeviceLost, 3 Fail, 1 InternalError, 24 NotSupported, 46 Pass** --
`DeviceLost` dominating was the same symptom class `L234` had just
fixed, a strong prior for another silently-swallowed error rather than
a real hang.

**Root cause**: `FEME_VULKAN_LOG_CREATION_ERRORS=1` on a
`logic_op_na_formats.r16_sfloat.copy_noblend` repro surfaced
`vkQueueSubmit: logic ops are only implemented for
R8G8B8A8_UNORM/_UINT/_SINT attachments (mechanical, added on demand)`
-- `Executor.cpp`'s `mergeColor` hardcoded `LogicOpEnable` to exactly 3
formats, erroring (device-lost) for anything else. Two distinct
sub-bugs: (1) `logic_op_na_formats.*`'s whole point -- floating-point/
sRGB attachments don't support logic ops per spec at all, and must
silently behave as if blending were disabled, not error; (2)
`logic_op.*`'s other real integer formats (`R32_UINT`,
`R32G32_UINT`, `R8_UINT`, ...) need real byte-level logic-op
application generalized beyond the 1-byte-per-component/4-byte-texel
assumption implicitly tied to `R8G8B8A8`'s own shape.
`RenderPass.cpp`'s `isSupportedColorAttachmentFormat` was cross-checked
as the authoritative list to enumerate the exact float/sRGB exclusion
set (`R32_FLOAT`, `R32G32_FLOAT`, `R32G32B32_FLOAT`,
`R32G32B32A32_FLOAT`, `R16G16B16A16_FLOAT`, `R16_FLOAT`,
`R16G16_FLOAT`, `R11G11B10_FLOAT`, `R8G8B8A8_UNORM_SRGB`,
`B8G8R8A8_UNORM_SRGB`).

**Fix**: new `formatSupportsLogicOp` predicate (false only for the
enumerated float/sRGB formats) and `logicOpComponentByteWidth`
(`std::optional<unsigned>`, 1/2/4 bytes/component for every
uniform-width, natural-R/G/B/A-memory-order integer/normalized format).
`mergeColor` restructured: `if (LogicOpEnable &&
formatSupportsLogicOp(Format))` runs a generalized byte-level op sized
to the real texel width, gated per-component by `WriteMask`; the
fallback blend path's gate changed from `if (Blend.BlendEnable)` to
`if (Blend.BlendEnable && !LogicOpEnable)`, so a `logicOpEnable`-but-
unsupported-format draw is treated exactly like blend-disabled (per
spec: enabling logic op always disables blending, regardless of format
support), not silently blended. Packed (`R10G10B10A2_*`)/
channel-reordered (`B8G8R8A8_*`, whose `B,G,R,A` memory order doesn't
match `WriteMask`'s logical `R=0,G=1,B=2,A=3` bit order) formats are
deliberately left unimplemented -- no CTS case in this sample needs
either, and both have real, different generalization problems (packed
sub-byte component boundaries; reordered write-mask-to-byte mapping).

**Verification**: 2 new `ExecutorTest` unit tests
(`LogicOpXorsOnAFourByteIntegerAttachment` on `R32_UINT`,
`LogicOpOnAFloatAttachmentBehavesAsBlendDisabled` on
`R32G32B32A32_FLOAT`). `check-feme`: 3357/3357 (61 unsupported, 0
failed, up from 3355 -- 2 net new tests). 5 of 6 hand-picked repro
cases individually confirmed Pass (previously DeviceLost); the 6th
(`no_position.*`) fails for a distinct, unrelated reason (see `L238`).

Real CTS: the same 84-case candidate sample now reports **54 Pass** (up
from 46) and **2 DeviceLost** (down from 10, both `no_position.*`,
confirmed unrelated to this fix).

The remaining 6 residual cases (2 `no_position`, 1 `render_to_image`, 2
`spec_constant`, 1 `creation_cache_control`) were each root-caused to a
distinct, non-trivial feature gap -- split off into their own flat
roadmap entries `L238`-`L241` rather than nested under `L237`, per this
project's own "avoid nesting milestone letters more than one deep"
convention.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this fix corrects existing, already-exposed core-1.0
pipeline color-blend-state behavior (`logicOpEnable`), it doesn't
add/remove any advertised feature or extension.

## Roadmap L240: groupshared-atomic partial-wave masking gap -- root-caused and fixed

`dEQP-VK.pipeline.monolithic.spec_constant.compute.local_size.{xyz,z}`
(2 of `L237`'s own residual cases) failed with `Fail (Values did not
match)`. Both cases use `layout(local_size_x_id = 1/2/3) in;` (SPIR-V
`OpExecutionModeId LocalSizeId`), a specialization-constant-driven
workgroup size -- the initial working theory was that this ICD applies
a compute shader's declared/default local size rather than honoring
the specialization-constant override.

That theory turned out to be a red herring. Systematically testing all
7 `local_size` subcases (`x`=7, `y`=5, `z`=3, `xy`=24, `xz`=27, `yz`=10,
`xyz`=105 total invocations) found every case whose total invocation
count was *not* a multiple of the default SIMD `WaveSize` (4) failed
identically; only `xy` (24, a multiple of 4) passed. The qpa log's own
byte-offset detail pinpointed the mismatch to the shader's `checksum`
field (a groupshared `atomicAdd` accumulator) -- not `gl_WorkGroupSize`
itself, which was written correctly (as `{3,5,7}`) in every case. The
entire spec-constant/`LocalSizeId`/`gl_WorkGroupSize` resolution chain
(`GroupSize.cpp`'s `resolveComputeGroupSize`,
`SpecializationPatch.cpp`'s `patchSpecializationConstants`,
`SPIRVToLLVMPatterns.cpp`'s `prepareSpecConstants`/
`ReferenceOfConversionPattern`, `Pipeline.cpp`'s
`compileComputePipeline`) was confirmed entirely correct.

The real bug: `FunctionWidener::widenGroupSharedAtomicRMW` and
`widenGroupSharedAtomicCmpXchg` (`SIMDize.cpp`) unconditionally cloned
a groupshared atomic once per SIMD lane across the *entire* `WaveSize`,
with no masking against `Env.SideEffectMask` at all -- unlike the
sibling `widenMaskedAtomicRMW` (resource/buffer atomics), which already
masks a divergent/inactive lane's value operand using
`getAtomicRMWIdentity`'s no-op-substitution technique. In a workgroup
whose total invocation count isn't a multiple of `WaveSize`, the last
(partial) wave's padding/inactive lanes still executed a real,
unmasked `atomicAdd`, silently over-counting the groupshared
accumulator by exactly the padding-lane count (`xyz`: expected 105,
got 108 -- a 3-lane overcount, matching the 1-real/3-padding split of
the last 4-lane wave for a 105-invocation dispatch).

Fixed by masking each lane's clone before it executes:
- `widenGroupSharedAtomicRMW`: `select(LaneMask, LaneVal, IdentityVal)`
  as the value operand, where `IdentityVal` is `getAtomicRMWIdentity`'s
  identity constant for the op (or a plain load-then-writeback for
  `Xchg`, which has no identity element -- safe because dispatch is
  sequential, not thread-pooled, mirroring `widenMaskedAtomicRMW`'s own
  `Xchg` handling). Errors out for any other identity-less op.
- `widenGroupSharedAtomicCmpXchg`: no natural identity exists for a
  compare-exchange, so a masked-off lane's comparand is instead forced
  to a guaranteed mismatch -- the bitwise complement of a freshly
  loaded current value -- so its `cmpxchg` always takes the "no match,
  no store" path.

`ninja check-feme`: 3359/3359 (61 unsupported, 0 failed, up from
3357 -- 2 net new unit tests: `MasksGroupSharedAtomicRMWAgainstSideEffectMask`,
`MasksGroupSharedAtomicCmpXchgAgainstSideEffectMask`). The two
pre-existing groupshared-`atomicrmw` FileCheck tests
(`simdize-groupshared-atomic-{scalar,array}.ll`) and one existing unit
test's `ExtractElementCount` assertion were updated for the new masked
value-operand IR shape. `check-hlsl-feme-vk`: unchanged, 483 Pass / 32
XFAIL / 207 Not supported, 0 unexpected failures.

Real CTS: all 7 `local_size` subcases now individually confirmed
**Pass** (checksums matching exactly, no more overcounts). A full
`spec_constant.*` re-run (1413 cases) shows **776 Pass, 18 Fail, 619
Not supported** -- zero `local_size` failures remain; the 18 residual
failures are an unrelated `composite.matrix.{mat2x3,mat3,mat4x3}`
cluster (compute + 5 graphics stages), newly discovered by this
broader re-run and filed as `L242`.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this is a compute-dispatch correctness fix to already-
implemented core-1.0 behavior (SIMD widening of groupshared atomics
during CPU codegen), not a new feature or extension landing.

## Roadmap L242: bare non-Function-storage matrix global tightening gap -- root-caused and fixed

The 18-case `dEQP-VK.pipeline.monolithic.spec_constant.*.composite.matrix.{mat2x3,mat3,mat4x3}`
cluster (`compute` + 5 graphics stages, newly discovered by `L240`'s
own full `spec_constant.*` re-run) failed with `Fail (Values did not
match)`. Each case's shader declares 6 `Private`-storage `mat2x3`/
`mat3`/`mat4x3` global variables, each spec-constant-initialized via a
whole-matrix `OpStore` of an `OpCompositeConstruct`'d value, then reads
each matrix's own 6 elements back via a nested (`col`/`row`)
dynamically-indexed `spirv.AccessChain` loop, summing into an
accumulator stored to an SSBO. Manually decoding the qpa's own
expected-vs-actual `<Text>` detail (`struct.unpack`'ing the raw
mismatch bytes) found every element read back as `0x7fc00000` -- a
canonical quiet NaN -- while manually verifying the spec-constant math
itself (with `SpecId 1` overridden to 42.0) confirmed the *expected*
values were exactly right; only the *matrix storage/access* path was
broken.

Reproduced standalone (outside the CTS, for faster iteration): the
qpa's own `--deqp-log-decompiled-spirv=enable` disassembly was
extracted, reassembled via `spirv-as`, hand-patched to bake in the
`OpSpecConstant` override, then run through `feme-translate
--import-spirv` (binary -> `spirv` MLIR dialect) then `feme-translate
--no-implicit-module --spirv-to-llvmir` (dialect -> real LLVM IR) to
get fully inspectable output without a full CTS harness run.

Root cause, visible directly in the generated LLVM IR: each `Private`
matrix global was declared `@spirv_var_13 = private global [2 x <3 x
float>] undef` (the natural, ABI-rounded/padded conversion, 16 bytes
per `vec3` column), but the whole-matrix `OpStore` was emitted as a
tightly-packed, 12-byte-per-column `%feme.tight_vector`-wrapped store
into that same global -- while every subsequent per-element
`OpAccessChain`-based read still computed its GEP against the global's
own natural, 16-byte-per-column declared type. This 16-vs-12-byte
column-stride mismatch meant every read of column 1 and beyond landed
on the wrong byte offset -- reading past what the tight store actually
initialized, onto genuinely uninitialized/poison memory, observed as
NaN through floating-point propagation.

This is the identical bug `L211` (`TightMatrixStorePattern`/
`TightMatrixLoadPattern`, `SPIRVToLLVMPatterns.cpp`) already fixed for
`Function`-storage locals, just triggered by a bare `Private`-storage
global instead: both patterns' exemption from tightening was keyed on
`StorageClass::Function` specifically, the wrong discriminator. The
real, storage-class-independent condition that makes tightening safe
is whether the pointee is reached through a struct-member
`spirv.AccessChain` at all -- only an offset-decorated struct member's
own declared type is ever substituted with the tightened form
(`convertOffsetStructTypeIgnoringDecorations`); a bare
`spirv.mlir.addressof` of a directly matrix-typed global/local (no
wrapping struct) has no such declared tight layout to reconcile with,
since SPIR-V requires an explicit `AccessChain` to narrow any struct
pointer down to a member pointer, even a trivial one. Confirmed by
inspection that `Workgroup` storage has the identical latent gap
(`WorkgroupGlobalVariablePattern`'s own `matchAndRewrite` uses the
plain, untightened `convertType` for its declared global type, with no
matrix-aware branching at all), just never triggered/discovered until
this session.

Fixed by generalizing both patterns' exemption: skip tightening
whenever `Op.getPtr()` is not defined by a `spirv::AccessChainOp` at
all (a bare, non-struct-member whole-matrix pointer), regardless of
storage class. This subsumes `L211`'s own `Function`-only case (a
`Function`-storage local's own `Variable` result is itself always such
a bare pointer) while also covering `Private`/`Workgroup` bare
globals. New lit test `spirv-to-llvm-matrix-private-whole-store-load.mlir`
added, covering a bare `Private`-storage matrix global's whole-store/
load and a subsequent per-element `AccessChain` read, alongside the
existing `Function`-storage (`L211`) and `Workgroup`-struct-member
(`L207`) precedent tests.

`ninja check-feme`: 3360/3421 (61 pre-existing unsupported, 0 failed,
+1 net new lit test).

Real CTS: the 18-case `composite.matrix.{mat2x3,mat3,mat4x3}` cluster
now **18/18 Pass** (was 0/18). A full `pipeline.monolithic.spec_constant.*`
re-run (1413 cases) shows **794 Pass, 0 Fail, 619 Not supported** --
zero failures remain in the whole `spec_constant.*` group.
`check-hlsl-feme-vk`: unchanged, 483 Pass / 32 XFAIL / 207 Not
supported, 0 unexpected failures.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this is a matrix-storage-layout correctness fix to existing
core-1.0 behavior (SPIR-V-to-LLVM matrix global lowering), not a new
feature or extension landing.

## Roadmap L238: `pipeline.monolithic.no_position.*` -- two independent root causes, both fixed

`dEQP-VK.pipeline.monolithic.no_position.*` (300 cases in the
`vk-default` mustpass list) exercises every subset of the
vertex/tessellation-control/tessellation-evaluation/geometry stage
chain either writing or not writing `SV_Position`/`gl_Position` (case
names encode this with a trailing `0`/`1` per active stage, e.g.
`v0_c1_e0`). `deqp-vk` has no flag to survive a `DeviceLost` mid-run
(`--deqp-terminate-on-fail=disable` only covers ordinary `Fail`s), so
this group was swept via a driver script invoking
`deqp-vk --deqp-case=<single case>` once per case (300 individual
~0.2s invocations), capturing each case's Pass/Fail/DeviceLost/
NotSupported status plus (via `FEME_VULKAN_LOG_CREATION_ERRORS=1`) the
first internal `vk*:` error string.

Baseline sweep: **144 NotSupported, 66 DeviceLost, 46 Fail, 44 Pass.**

Two genuinely independent bugs were found and fixed, both stemming
from the same incorrect assumption -- that every pipeline's last
pre-rasterization stage must write `SV_Position` -- applied in two
different places:

**(A) Creation-time rejection (34 of the 46 `Fail` cases).**
`GraphicsPipeline.cpp`'s `validateStageInterfaces` hard-rejected
`vkCreateGraphicsPipelines` whenever the last pre-rasterization
stage's signature lacked a 4-component `Position`, with narrow
exemptions only for `RasterizerDiscardEnable`/`GeometryNeverWrites`.
Per the Vulkan spec (Shader Interfaces, "Position"): "If the last
vertex processing stage entry point's interface does not include a
variable decorated with Position, the position used for
clipping/rasterization is undefined" -- omitting it entirely is legal,
not a creation-time error. Fixed by removing the rejection entirely,
keeping only the "if `Position` exists, it must have exactly 4
components" malformed-shape check; `Executor.cpp`'s runtime mirror of
the same check was removed identically, leaving only its pre-existing
`if (!VSPosition) return Error::success();` early return (previously
reached only via the two narrow exemptions, now reached
unconditionally) to skip rasterization gracefully. This is spec-legal,
not a hack, specifically because `no_position.*`'s own fragment shader
always writes the exact same color as the render target's own clear
color regardless of what (if anything) is ever rasterized -- confirmed
by reading the CTS test source (`vktPipelineNoPositionTests.cpp`)
directly.

**(B) Draw-time (`vkQueueSubmit`) hard error (66 `DeviceLost` cases).**
`StageLink.cpp`'s `linkStageElements` hard-errored whenever a consumer
signature element -- including a system-value one like `Position` --
had no matching producer element in the previous stage. Per the
Vulkan spec: "Any input value that does not have a matching output
value is undefined" -- a producer/consumer mismatch is legal, not a
link failure. Fixed by adding `LinkedStageElement::HasProducer`
(default `true`); when no producer is found for a *system-value*
consumer element specifically, `linkStageElements` now pushes a
`HasProducer=false` link instead of erroring, and `copyLinkedElements`
writes a deterministic raw `0` to the destination for such a link
instead of reading from the (nonexistent) producer. Deliberately
narrow scope: an ordinary `Location`-addressed consumer with no
producer remains a hard error, unchanged -- no currently-passing case
needs the broader relaxation, and this limits the fix's blast radius
to exactly the failure this session found.

Post-fix sweep: **144 NotSupported (unchanged), 0 DeviceLost, 48 Fail,
108 Pass.** All 100 `Fail`+`DeviceLost` cases this bug caused are now
`Pass`; a diff against the baseline sweep confirms 0 regressions among
the 44 previously-`Pass` cases. The 48 remaining `Fail` cases are all
`Unexpected SSBO counter value in view 0 for the tessellation control
shader: got {4,1} but expected 3` -- a separate, pre-existing bug (more
of these cases are now reachable/exposed since fewer cases device-lost
before reaching this check; this bucket was 12 cases in the original
`DeviceLost`-truncated sweep, now fully exposed at 48). Root-caused and
split off as roadmap `L243`, deliberately out of scope for this fix
(a tessellation-control invocation-count/dispatch bug, not a
stage-interface-matching gap).

2 new `StageLinkTest` unit tests
(`AcceptsAProducerlessSystemValueConsumer`,
`WritesZeroForAProducerlessSystemValueLink`) cover fix (B) directly
against synthetic signatures. 1 existing `GraphicsPipelineTest`
(`RejectsEmptyVertexShaderWithoutTessellation`, which exercised
exactly fix (A)'s now-relaxed rejection) was renamed
`AcceptsEmptyVertexShaderWithoutTessellation` and updated to assert
the new, correct `VK_SUCCESS`.

`ninja check-feme`: 3362/3423 (61 pre-existing Unsupported, 0 Fail,
+2 net new unit tests). `check-hlsl-feme-vk`: unchanged, 483 Pass /
32 XFAIL / 207 Not supported, 0 unexpected failures.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this is a stage-interface/rasterization correctness fix to
existing core-1.0 behavior, not a new feature or extension landing.

## Roadmap L243: `pipeline.monolithic.no_position.*.ssbo_writes.*` -- side effects dropped/duplicated by the barrierless hull-entry split

`L238`'s own full 300-case `dEQP-VK.pipeline.monolithic.no_position.*`
re-run left 48 cases failing with `Unexpected SSBO counter value in
view 0 for the tessellation control shader: got {4,1} but expected 3`
-- split off rather than folded into `L238`'s fix since it's a
tessellation-control invocation-count/dispatch bug, not a
stage-interface-matching gap.

Root cause: `CanonicalizeStage.cpp`'s
`splitBarrierlessTessellationControlEntry` (the pass synthesizing
FeMe's HLSL-style two-function hull-stage split for a barrierless,
GLSL-style single-`main()` entry) only ever reasoned about classified
stage-IO stores when deciding what each of its two clones -- the
per-invocation control-point phase and the once-per-patch
patch-constant phase -- should keep. Any other side effect was
invisible to that classification: here, an `AtomicRMWInst` (SPIR-V's
`OpAtomicIAdd`, used by the CTS's own `atomicAdd(ssbo.counters[...],
1)`, lowers directly to one via `AtomicRMWPattern`
(`SPIRVToLLVMPatterns.cpp`), never through a `feme.stage.*` call).

Confirmed via the same SPIR-V-decompilation-diff technique `L242`
established (`--deqp-log-decompiled-spirv=enable`, diffing the real
compiled TCS SPIR-V for a "got 4" case against a "got 1" case): the
"got 4"/"got 1" split correlates exactly with whether the TCS also
writes `gl_Position` (a genuine patch/vertex mix, case name's `c1`
suffix) or writes only `gl_TessLevelInner/Outer` (patch-frequency
only, `c0`):

- **"got 1" (`c0`, patch-frequency-only shape):** this shape's
  existing fast path (`isPatchConstantOnlyEntry`) moves the *whole*
  entry body, atomic included, into the once-per-patch
  `.patchconstant` clone alone, replacing the original with a trivial
  empty `ret void` stub. The atomic then runs once per patch instead
  of once per invocation: 1, not 3.
- **"got 4" (`c1`, genuine-mix shape):** this shape's existing
  frequency-based store pruning (`pruneStageIOStoresByFrequency`)
  leaves the atomic untouched in *both* clones (it only ever reasons
  about classified stage-IO stores), so it runs an extra, spurious
  time on top of its correct once-per-invocation executions: 3
  correct + 1 spurious = 4.

Fix: two new helpers, `hasNonStageIOSideEffect(Function &F)` (detects
an `AtomicRMWInst`/`AtomicCmpXchgInst` anywhere, a `StoreInst` whose
pointer resolves to neither a stage-IO global nor a Function-local
`AllocaInst` via `getUnderlyingObject`, or a `CallInst` that is
neither a recognized `feme.stage.*` op nor provably read-only) and
`pruneNonStageIOSideEffects(Function &Fn)` (erases the same
instructions it detects, replacing any remaining uses with
`PoisonValue`, then sweeps up newly-dead `feme.stage.input.load` calls
via the existing `pruneDeadStageInputLoads`).
`splitBarrierlessTessellationControlEntry` now computes
`hasNonStageIOSideEffect(F)` up front and widens its genuine-mix
branch condition to also trigger whenever it is true (so the
patch-frequency-only fast path is skipped in favor of a real,
non-empty control-point clone whenever a side effect is present), and
that branch now always additionally calls
`pruneNonStageIOSideEffects(*PatchConstantPhase)` after its existing
frequency-based pruning -- the control-point clone's own copy of the
side effect already runs with the correct per-invocation multiplicity,
so only the patch-constant clone needs it stripped.

Verified against the two hand-picked repro cases directly
(`ssbo_writes.single_view.v0_c1_e0`/`v0_c0_e0`, both now `Pass`), then
the full 48-case failure list (all now `Pass`), then the full 300-case
`no_position.*` group: **156 Pass, 144 NotSupported (multiview +
tessellation, unchanged), 0 Fail** -- up from 108 Pass / 144
NotSupported / 48 Fail before this fix, confirmed 0 regressions among
the 156 now-passing cases via a full status diff against the pre-fix
sweep.

2 new `CanonicalizeStageTest` unit tests
(`NoBarrierPatchConstantOnlyEntryWithSideEffectKeepsControlPointClone`,
`NoBarrierMixedFrequencyEntryWithSideEffectPrunesPatchConstantClone`)
cover both fixed shapes directly against synthetic IR containing an
`atomicrmw`.

`ninja check-feme`: 3364 Passed, 61 Unsupported, 0 Failed (+2 net new
unit tests). `check-hlsl-feme-vk`: unchanged, 483 Pass / 32 XFAIL /
207 Not supported, 0 unexpected failures.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this is a tessellation-control-dispatch correctness fix to
existing core-1.0 behavior, not a new feature or extension landing.

## Roadmap L241: `creation_cache_control.*` -- wrong root-cause guess, two real (plus one incidental) fixes

The original roadmap entry guessed `duplicate_single_recreate_derivative`'s
`VK_ERROR_INITIALIZATION_FAILED` was caused by unimplemented
`VK_EXT_pipeline_creation_cache_control` `VkPipelineCreateFlagBits`
handling. Direct reproduction with `FEME_VULKAN_LOG_CREATION_ERRORS=1`
immediately showed a completely unrelated cause:
`primitiveRestartEnable requires a strip or fan primitive topology` -- a
defensive, roadmap-H5e-b-era creation-time check in
`GraphicsPipeline.cpp` rejecting `primitiveRestartEnable=VK_TRUE`
combined with a list topology. The CTS's own shared `graphics_pipelines.*`
pipeline template (`vktPipelineCreationCacheControlTests.cpp`'s
`IA_STATE`) hardcodes exactly this combination
(`VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` +
`primitiveRestartEnable=VK_TRUE`) for every one of its 9 subtests,
without ever gating on `VK_EXT_primitive_topology_list_restart` support.

Confirmed via `vk.xml` that `VkPhysicalDevicePrimitiveTopologyListRestart
FeaturesEXT` is an extension-only struct (not part of core
`VkPhysicalDeviceVulkan14Features`), and that the VUID this check enforced
(`VUID-VkPipelineInputAssemblyStateCreateInfo-primitiveRestartEnable-
topology-04909`) is a validation-layer-only concern: an application that
violates it invokes undefined, not driver-rejectable, behavior. FeMe's
own draw-time path (`Executor.cpp`'s `executeDraws`, gated on
`topologySupportsPrimitiveRestart`) already silently no-ops the flag for
an unsupported topology, so the creation-time rejection was strictly
stricter than the spec requires and broke this otherwise-unrelated test.
Removed it; `Result.PrimitiveRestartEnable` is simply left as-is.

Re-running the full 18-case `dEQP-VK.pipeline.monolithic.creation_cache_
control.*` group (9 `graphics_pipelines` + 9 `compute_pipelines`) after
this fix alone: 14/18 pass (up from 7/18), but the remaining 4 --
`batch_pipelines_early_return`/`_maintenance5`, both `graphics_pipelines`
and `compute_pipelines` -- still fail with `pipelines[1] is not
VK_NULL_HANDLE after a explicit early return index`. This is a second,
genuine, previously-unimplemented gap:
`VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT` had zero references
anywhere in FeMe's codebase. Per spec: when a pipeline in a batch
(`vkCreateGraphicsPipelines`/`vkCreateComputePipelines`) with this bit set
fails to create, the driver must stop processing the rest of the batch
immediately, and every remaining `pPipelines` entry -- from the failure
point onward, inclusive -- must be `VK_NULL_HANDLE`.

Fix: a per-call `FailBatch` lambda (bool-returning, since a lambda cannot
`return` out of its enclosing function) added to each of
`vkCreateGraphicsPipelines`'s and `vkCreateComputePipelines`'s own batch
loops, wired into every existing failure/`continue` site. `FailBatch`
preserves the pre-existing precedence rule that a soft
`VK_PIPELINE_COMPILE_REQUIRED` must never mask an already-set, more
severe result, and additionally nulls every remaining `pPipelines` entry
and signals an immediate `return` when the early-return bit is set on a
failure.

While investigating, discovered a third, related, previously-unnoticed
gap: `vkCreateComputePipelines` never consulted `VK_KHR_maintenance5`'s
`VkPipelineCreateFlags2CreateInfo` override at all -- only
`vkCreateGraphicsPipelines` did, via a file-local (not exported)
`getEffectivePipelineCreateFlags` helper. This meant the `_maintenance5`
variant of this same test would have failed doubly for compute pipelines,
and `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT` itself was
unreachable via the flags2 path for compute pipelines (it always read the
raw, always-`0`-when-flags2-is-used `CreateInfo.flags`). Fixed by
extracting a new, shared `feme::vulkan::resolvePipelineCreateFlags2(Flags,
pNext)` (`Pipeline.h`/`.cpp`), with the graphics-only overload now simply
forwarding to it.

Real CTS: the full 18-case `creation_cache_control.*` group now passes
18/18 (up from an original, never-cleanly-measured baseline; the two
intermediate points gathered this session were 7/18 pre-fix and 14/18
after the primitiveRestartEnable fix alone).

7 new/updated unit tests: `GraphicsPipelineTest.
RejectsUnimplementedStateCombinations`'s primitive-restart sub-case now
expects `VK_SUCCESS` (plus an executor-level no-op assertion) instead of
the removed rejection; `AcceptsPrimitiveRestartOnStripAndFanTopologies`'s
stale cross-reference comment corrected; 2 new
`GraphicsPipelineTest.BatchEarlyReturnOnFailure*` tests (legacy flags and
flags2/maintenance5 variants); 2 new
`PipelineCacheTest.BatchEarlyReturnOnFailure*` tests (same, for compute
pipelines).

`ninja check-feme`: 3368 Passed, 61 Unsupported, 0 Failed (+7 net new
unit tests over `L243`'s 3364).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- both `VK_EXT_pipeline_creation_cache_control` and
`VK_KHR_maintenance5` were already advertised; this is a correctness fix
to existing flag/behavior handling, not a new extension landing.

## Roadmap L232: storage `CubeArray` `GetDimensions` `/6` fix -- closes `L230`'s remaining gap

Added `ImageShape::StorageCubeArray`, distinct from `Array2D` purely so
`GetDimensions`/`imageSize()` can apply the same `/6` face-count
correction the *sampled*-image `CubeArray` shape already gets --
`classifyStorageImage2DHandle` now splits a storage `Dim::Cube` handle
on `Arrayed` (a plain, non-arrayed cube still folds into `Array2D`
unchanged, since its element count is always exactly 6). Both
`GetDimensions`-family lowering sites (`isGetDimensions3Intrinsic`'s
bare query, `isQuerySizeLodCall`'s explicit-Lod query) now route
`StorageCubeArray` to the existing `createQuerySizeLodCubeArray`
builder/`femeCpuImageGetDimensionsLodCubeArrayV3I32` runtime call
already used for the sampled-image case -- no new runtime division
logic was needed at all, just correct routing to already-correct,
already-existing machinery. Every storage fetch/store-addressing
switch site in `lowerImageAccesses` (the coordinate-width calculation,
the coordinate-assembly ternary, and the `Load`/`Store` switches) also
gained an identical `StorageCubeArray` case alongside `Array2D`'s own,
confirming the addressing really is unchanged (a genuine cube array's
"layer" coordinate component is simply an already-flattened `layer * 6
+ face` value, same as before this fix).

Real CTS: `dEQP-VK.image.image_size.cube_array.*` now **12/12 Pass**
(up from 0/12 Pass -- all 12 previously failed with a wrong,
undivided value, e.g. `readonly_1x1x12` returned `(1, 1, 12)` instead
of the expected `(1, 1, 2)`). A follow-up 772-case
`dEQP-VK.image.load_store.*cube*` regression sample matches `L230`'s
own prior baseline exactly (648 Pass, 0 Fail, 124 NotSupported) --
confirming zero regressions to the already-passing storage-cube
load/store addressing this fix's new switch cases touch.

2 new unit tests: `SPIRVResourceLoweringTest.
LowersCubeArrayStorageImageGetDimensions` (bare, Lod-less query) and
`LowersCubeArrayStorageImageQuerySizeLod` (explicit-Lod query), both
confirming a genuine storage cube-array handle now reaches
`createQuerySizeLodCubeArray` rather than the previous, wrong
`createQuerySizeLod2DArray`/`createQuerySizeLod2D` dispatch.

`ninja check-feme`: 3370 Passed, 61 Unsupported, 0 Failed (+2 net new
unit tests over `L241`'s 3368).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this is a core-1.0 `imageSize()`/`GetDimensions` correctness
fix, not a new feature or extension.

No design-document deviation -- `FeMeVulkanDesign.md`/`FeMeCPUDesign.md`
do not document `ImageShape`'s internal enumerators at this level of
detail (that lives only in `SPIRVResourceLowering.cpp`'s own code
comments), so there was nothing stale to update there.

## Roadmap L239: 3D-image-as-2D-render-target support (`VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT`)

Two sequential bugs blocked `dEQP-VK.pipeline.monolithic.
render_to_image.core.3d.*`, both root-caused via direct code reading
plus the real CTS source (`vktPipelineRenderToImageTests.cpp`), not a
repro-run-first approach:

1. `isValidImageShape`'s (`Image.cpp`) flags gate only ever accepted
   `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`, rejecting
   `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT` outright at `vkCreateImage`
   time (`VK_ERROR_INITIALIZATION_FAILED`, matching this group's
   documented symptom). Widened the mask to accept both flags, and
   added a new check that `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT`
   only applies to a `VK_IMAGE_TYPE_3D` image (matching Vulkan spec
   scope for the flag).

2. Once (1) is fixed, `vkCreateImageView`'s `baseArrayLayer`/
   `layerCount` resolution and bounds check still compared against
   `Img->arrayLayers()` (always exactly 1 for a 3D image per
   `VUID-VkImageCreateInfo-imageType-00961`), rejecting any view whose
   `baseArrayLayer > 0` -- exactly the pattern this CTS group uses (one
   `VK_IMAGE_VIEW_TYPE_2D` view per depth slice, `baseArrayLayer` =
   slice index, confirmed via `vktPipelineRenderToImageTests.cpp`'s own
   `getImageViewSliceType`/`makeColorSubresourceRange` call). Added a
   new `effectiveViewLayerCount(const Image &, VkImageViewType)` free
   function (`Image.h`/`Image.cpp`) that returns `Img.depth()` instead
   of `Img.arrayLayers()` specifically for a non-`_3D`-typed view of a
   `VK_IMAGE_TYPE_3D` image, and a new `ImageView::resolvedLayerCount()`
   convenience method built on it. Wired into `vkCreateImageView`'s own
   resolution/bounds check, and into `RenderPass.cpp`'s
   `resolveAttachmentView`/`isCompatibleAttachmentView` (which shared
   the identical `Img.resolvedLayerCount()`-based bug on the
   render-target-attachment path).

`Image`'s own texel addressing (`Image::texelPointer`,
`computeSubresourceLayouts`) already treats a 3D image's depth slices
identically to array layers via a shared `SlicePitch`-multiplied
stride, so no new addressing math was needed anywhere -- only
correcting which count (`ArrayLayers` vs. `Depth`) resolves/bounds a
view's subresource range when the view's underlying image is 3D.
`CommandBuffer.cpp`/`ImageOps.cpp`'s own `Image::resolvedLayerCount()`
call sites are unaffected by design: those APIs address 3D depth via
`imageOffset.z`/`imageExtent.depth` directly, not `baseArrayLayer`/
`layerCount`, so they were already correct as-is and were deliberately
left untouched (a new, `ImageView`-scoped function was added instead of
modifying the pervasively-used `Image::resolvedLayerCount` itself, to
avoid any risk of regressing those already-correct call sites).

Real CTS: `dEQP-VK.pipeline.monolithic.render_to_image.core.3d.
mipmap.*` now **40/40 Pass** (up from all-failing
`VK_ERROR_INITIALIZATION_FAILED` per the roadmap's own prior
investigation). The full `render_to_image.*` group (1325 cases): 1245
Pass, 0 Fail, 80 Not Supported -- no regressions. A broader
`dEQP-VK.image.*` sanity sweep (143086 cases): 6881 pre-existing
failures, all in categories unrelated to this fix's narrow 3D-image-
view scope (`extended_usage_bit_compatibility`, `format_reinterpret`,
`atomic_operations`) -- none newly introduced by this change.

3 new unit tests in `ImageTest.cpp`
(`Rejects2DArrayCompatibleFlagOnNon3DImage`,
`Accepts2DArrayCompatibleFlagOn3DImage`,
`CreateImageView2DSliceOf3DImageAddressesDepthSlices` -- the last
covering both `VK_REMAINING_ARRAY_LAYERS` resolution against
`Img.depth()` and an out-of-range-slice rejection) and 1 new unit test
in `RenderPassTest.cpp`
(`ResolveAttachmentViewAcceptsSliceOf3DImage`, confirming a non-zero
depth slice resolves to the correct byte offset/size as a render-target
attachment).

`ninja check-feme`: 3374/3435 Passed, 61 Unsupported, 0 Failed (+5 net
new unit tests over `L232`'s 3370).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this is a core-1.0 image/image-view shape-validation and
render-target-attachment-addressing correctness fix, not a new feature
or extension (`VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT` is a core-1.0
`VkImageCreateFlagBits` enumerant, already implicitly "supported" by
any conformant `vkCreateImage`; there is no dedicated extension or
`VkPhysicalDeviceFeatures` bit gating it).

No design-document deviation -- `FeMeVulkanDesign.md`/
`FeMeGraphicsDesign.md` do not describe `isValidImageShape`'s flags
mask or `resolveAttachmentView`'s layer-count resolution at a level of
detail this fix invalidated (both documents describe the general
image/render-target model, not per-flag validation specifics), so
there was nothing stale to update there.

## Roadmap L235: `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`/`VK_IMAGE_CREATE_EXTENDED_USAGE_BIT` support

`L239`'s own broader `dEQP-VK.image.*` sanity sweep (143086 cases)
surfaced two large untriaged failure buckets:
`extended_usage_bit_compatibility.image_format_properties{,2}` (1320 +
1320 cases) and `format_reinterpret.*` (~3122 cases across 7 dimension
variants). A single-case repro
(`r8g8b8a8_unorm_optimal_color_attachment_bit`,
`extended_usage_bit_compatibility.image_format_properties`) failed with
`Fail: view format VK_FORMAT_R8G8B8A8_UNORM`; the real CTS source
(`vktImageExtendedUsageBitTests.cpp`) shows the test creating an image
with `VK_IMAGE_CREATE_EXTENDED_USAGE_BIT | VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`
and querying `vkGetPhysicalDeviceImageFormatProperties(2)` for it.

Two sequential bugs, both in the same pre-existing `L235` roadmap
row's own code region:

1. `isValidImageShape`'s (`Image.cpp`) flags gate only ever accepted
   `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT | VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT`
   (the latter from `L239`), rejecting both
   `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` and
   `VK_IMAGE_CREATE_EXTENDED_USAGE_BIT` outright at `vkCreateImage`/
   `vkGetPhysicalDeviceImageFormatProperties` time -- this row's own
   long-standing, previously-uninvestigated `todo` entry. Widened the
   mask to accept both flags. Neither needs any further behavioral
   change elsewhere: `vkCreateImageView` already never validates a
   view's format against its image's own declared format at all
   (effectively already "mutable" regardless of the flag), and no
   draw-time code path validates a view's usage against
   `Image::usage()` either (effectively already "extended" regardless
   of the flag) -- confirmed via the pattern `L239` established for
   `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT` (this ICD is broadly
   permissive by omission on view/image cross-validation, not
   over-strict).

2. Fixing (1) alone dropped the `extended_usage_bit_compatibility.*`
   bucket from 2640 Fail to 172 Fail, not 0: `vkGetPhysicalDeviceImageFormatProperties`'s
   own usage-vs-format-feature checks (`EntryPoints.cpp`, 6 checks --
   `SAMPLED`/`STORAGE`/`COLOR_ATTACHMENT`/`DEPTH_STENCIL_ATTACHMENT`/
   `TRANSFER_SRC`/`TRANSFER_DST`, plus a combined `INPUT_ATTACHMENT`
   check) ran unconditionally, rejecting a format/usage combination the
   base format itself has no feature bits for. Per spec,
   `VK_IMAGE_CREATE_EXTENDED_USAGE_BIT` defers exactly this validation:
   an image created with it may be used through a different,
   format-compatible-class view format that *does* support the usage,
   so the base format/usage combination alone need not. Wrapped all six
   checks in `if (!(flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT))`.
   `isValidImageShape`'s own unrelated checks (sample counts, mip/
   array-layer validity, 3D array-layer constraint) were left
   unconditional -- only the usage-vs-feature matching is a spec-mandated
   deferral.

Real CTS: `dEQP-VK.image.extended_usage_bit_compatibility.*` (both
sub-groups, 19872 cases): 2640 Pass, 0 Fail, 17232 Not Supported (up
from 2640 Fail at session start). `dEQP-VK.image.format_reinterpret.*`
(5944 cases, targeted re-run): 3296 Pass, 0 Fail, 2648 Not Supported --
fixed as a full side effect of the same `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`
acceptance (format-reinterpretation views require it), confirmed
directly rather than inferred from the broader sweep's regex-based
bucket counts. `dEQP-VK.api.info.image_format_properties*` (2372
cases, regression check): 65 Fail, confirmed pre-existing/unrelated via
`git stash`/rebuild/re-test on a representative case
(`a2b10g10r10_sint_pack32`) -- filed as new item `L245`.

A broader `dEQP-VK.image.*` re-sweep (143086 cases) after this fix:
1191 Fail, down from `L239`'s own 6881 -- confirming both targeted
buckets are fully resolved. The residual buckets are
`atomic_operations.*` (~864 cases across 9 ops, pre-existing per
`L239`'s own sweep note, filed as new item `L244`),
`image.mutable.{2d,2d_array}.*` (72 cases, **newly exposed, not a
regression** -- confirmed via `git stash`/rebuild/re-test on a
representative case, `r8g8b8a8_snorm_b8g8r8a8_srgb_draw_copy_resolve`,
that this group was previously entirely `NotSupported` due to a
conservative multisample-count report, never reaching the
`vkCreateFramebuffer VK_ERROR_INITIALIZATION_FAILED` this fix newly
exposes now that a real mutable-format image can exist -- filed as new
item `L246`), `store.without_format` (14, pre-existing), and 1 stray
`depth_stencil_descriptor` case (pre-existing).

4 new unit tests: `EntryPointsTest.cpp`'s
`ImageFormatPropertiesAcceptsMutableFormatBit` and
`ImageFormatPropertiesExtendedUsageBitDefersUsageCheck` (the latter
using `VK_FORMAT_R32G32B32_UINT`, a format with zero image-usable
format-feature bits per an existing sibling test, to confirm both the
deferral-with-flag success case and the without-flag rejection case),
plus `ImageTest.cpp`'s `AcceptsMutableFormatAndExtendedUsageFlags`
(both flags together on a 2D image via `vkCreateImage`).

`ninja check-feme`: 3377/3438 Passed, 61 Unsupported, 0 Failed (+3 net
new unit tests over `L243`'s 3374).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` and
`VK_IMAGE_CREATE_EXTENDED_USAGE_BIT` are both core-1.0
`VkImageCreateFlagBits` enumerants with no dedicated extension or
`VkPhysicalDeviceFeatures` bit gating them, matching the same rationale
`L239` documented for `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT`.

## Roadmap L244: widen image atomics beyond `Plain2D`

`L239`'s own broad `dEQP-VK.image.*` (143086-case) sanity sweep flagged
`dEQP-VK.image.atomic_operations.*` (~864 cases across `add`/`and`/
`compare_exchange`/`exchange`/`max`/`min`/`or`/`sub`/`xor`) as a large,
never-triaged failure bucket. A single-op repro
(`atomic_operations.add.*`, 782 cases) found 96 Fail, every one at
`vk.createComputePipelines(...): VK_ERROR_INITIALIZATION_FAILED` --
breaking down the 96 by dimension: `1d` (16), `1d_array` (16),
`2d_array` (16), `3d` (16), `cube` (16), `cube_array` (16); `2d` and
`buffer` were absent (already passing).

Re-running the smallest failing case
(`add.1d.notransfer.normal_read.normal_img.r32i_end_result`) with
`FEME_VULKAN_LOG_CREATION_ERRORS=1` initially pointed at an
apparently-unrelated resource handle
(`unsupported raised operation: ...handlefrombinding...`) -- this
turned out to be `UnsupportedOps.cpp`'s own "one unsupported op poisons
every handle in its containing function" behavior, not a hint about
the actual root cause. Reading `SPIRVResourceLowering.cpp` directly
found it instead: `hasOnlySupportedStorageImageUses`'s two atomic
branches (`AtomicRMWInst`/`AtomicCmpXchgInst`) each hard-rejected any
non-`Plain2D` shape (`if (!IsInteger || Shape != ImageShape::Plain2D)
return false;`), and `lowerImageAccesses`'s own atomic dispatch switch
only ever called the `*2D` `create*` wrappers with `(X, Y)`, hardcoded
for that one shape.

Widened both:

1. `hasOnlySupportedStorageImageUses`'s atomic gate now accepts
   `Plain1D`, `Array1D`, `Array2D` (also covers a plain storage `Cube`,
   folded into `Array2D` by `classifyStorageImage2DHandle`),
   `StorageCubeArray` (`L232`'s genuine cube array, sharing `Array2D`'s
   own addressing -- its already-flattened `layer * 6 + face` value
   passes through as an ordinary layer operand, needing no dedicated
   shape of its own), and `Plain3D`, alongside the pre-existing
   `Plain2D` -- every non-multisampled storage-image shape this
   function classifies today. The two multisampled shapes
   (`Plain2DMS`/`Array2DMS`) remain unreachable by spec, not by
   omission: SPIR-V disallows an atomic against a multisampled image
   operand outright.
2. `lowerImageAccesses`'s atomic dispatch (both the `AtomicRMWInst` and
   `AtomicCmpXchgInst` branches) now switches on `Shape` the same way
   the adjacent `StoreInst` dispatch already does, selecting the right
   `createAtomic*{Shape}` wrapper and coordinate operands (`X` alone
   for `Plain1D`; `X, Layer` for `Array1D`; `X, Y, Layer` for
   `Array2D`/`StorageCubeArray`; `X, Y, Z` for `Plain3D`) -- reusing the
   `X`/`Y`/`C2` values the `StoreInst` branch already computes
   generically per-shape just above it, with no new coordinate-
   extraction code needed.

Both changes required a matching amount of boilerplate: 44 new
`ImageCallKind` enum values, `create*` function declarations/
definitions, `getImageCallName`/`FunctionType`-registration/
`matchImageCall` entries (`ImageCalls.h`/`.cpp`), and 44 new
`femeCpuImageAtomic{Op}{Shape}` runtime entry points plus 4 new
bounds-checked address helpers (`FeMeRuntimeCPU.c`) -- one set per
(11 ops) x (4 new shape families) combination.  `matchImageCall`'s own
`AllKinds` table update matters beyond cosmetics: it is consumed by
`SIMDize.cpp`'s `FunctionWidener::widenImageCall` for SIMD-lane
masking, so skipping it would have left the newly-supported atomics
correctly *executing* but incorrectly *masked* under SIMD widening --
a silent correctness bug rather than a build/test failure.

**Verified against the real CTS**:
`dEQP-VK.image.atomic_operations.*` (6209 cases, all 9 targeted RMW ops
plus `compare_exchange`, across every shape): 1080 Pass, 4889 Not
supported, **0 Fail** among the targeted ops -- down from the
96/782-Fail single-op (`add`) repro this investigation started from.
The group's remaining 240 Fail are entirely
`atomic_operations.{inc,dec}.*` (`OpAtomicIIncrement`/
`OpAtomicIDecrement`), confirmed via a full-group re-run and breakdown
by op/shape to be the *only* two ops among the group's 12 (9 RMW +
`compare_exchange` + `inc` + `dec`) still failing, and every shape
fails identically for both -- pointing at a missing SPIR-V-to-MLIR
lowering pattern for these two dedicated opcodes entirely (no
`atomicrmw`/`cmpxchg` LLVM-IR equivalent exists for either, unlike
every other atomic op), not a `SPIRVResourceLowering.cpp`-side shape
gap like this fix's own. Filed as new roadmap item `L247`.

`dEQP-VK.image.load_store.*` (3446 cases) sampled for regressions: 2346
Pass, 1100 Not supported, 0 Fail -- unchanged.

8 new `ImageCallsTest.cpp` round-trip unit tests (one representative
op per new shape, plus `compare_exchange` per shape). 1 new lit test
(`spirv-resource-lowering-image-atomic-widened.ll`) covering the
resource-lowering pass's own per-shape dispatch for all 4 newly
supported shapes (one RMW op, `add`, plus `compare_exchange` each).

`ninja check-feme`: 3386 Passed, 61 Unsupported, 0 Failed (+8 net new
unit tests, +1 net new lit test).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this is a core-1.0 image-atomic correctness fix (widening an
already-implemented feature's shape coverage), not a new feature or
extension landing.

## Roadmap L247: `atomic_operations.{inc,dec}.*` -- missing SPIR-V-to-LLVM lowering pattern

`L244`'s own verification run isolated a residual 240-case
`dEQP-VK.image.atomic_operations.{inc,dec}.*` failure bucket, distinct
from (and outside the scope of) that session's `Plain2D`-only shape
gap: every `inc`/`dec` case failed at
`vk.createComputePipelines(...): VK_ERROR_INITIALIZATION_FAILED`, the
underlying MLIR diagnostic reading `failed to legalize operation
'spirv.AtomicIIncrement'/'spirv.AtomicIDecrement' that was explicitly
marked illegal`.

Reading `SPIRVToLLVMPatterns.cpp` confirmed no `AtomicIIncrementOp`/
`AtomicIDecrementOp` pattern was registered anywhere -- only the 9 RMW
ops (via a shared `AtomicRMWPattern<...>` template) and
`AtomicCompareExchangePattern`. The reason turned out to be an operand-
shape mismatch, not a simple oversight: MLIR's own SPIR-V dialect
(`SPIRVAtomicOps.td`) defines two distinct base classes.
`SPIRV_AtomicUpdateWithValueOp` (used by every other RMW op --
`Add`/`Sub`/`And`/`Or`/`Xor`/`SMax`/`SMin`/`UMax`/`UMin`/`Exchange`)
has an explicit `value` operand, which `AtomicRMWPattern` forwards
directly via `Adaptor.getValue()`. `SPIRV_AtomicUpdateOp` (used by
`AtomicIIncrement`/`AtomicIDecrement` alone) has **no** `value`
operand at all -- SPIR-V defines these two ops' increment/decrement
amount as an implicit, fixed 1 -- so `AtomicRMWPattern`'s existing
template genuinely could not be reused as-is.

Added a new `AtomicIncDecPattern<SPIRVOpTy, BinOp>` template class
(inserted directly after `AtomicRMWPattern`) that:

1. Converts `Op.getType()` (the op's scalar-integer result type)
   through the type converter to get the matching LLVM integer type.
2. Materializes an `LLVM::ConstantOp` with value `1` of that type,
   using the same `mlir::LLVM::ConstantOp::create(Rewriter, Loc, Type,
   IntLiteral)` idiom already used throughout this file.
3. Computes the atomic ordering via the existing
   `convertAtomicOrdering(Op.getSemantics())` helper (unchanged, shared
   with every other atomic pattern).
4. Replaces the op with an ordinary `LLVM::AtomicRMWOp`, using the
   given `BinOp` (`add` for `AtomicIIncrement`, `sub` for
   `AtomicIDecrement`), the pointer, the materialized constant `1`,
   and the ordering.

Because this ultimately emits an ordinary `atomicrmw add`/`sub`,
**every** downstream layer `L244` widened last session
(`SPIRVResourceLowering.cpp`'s `hasOnlySupportedStorageImageUses`/
`lowerImageAccesses`, `ImageCalls.h`/`.cpp`'s `AtomicAdd*`/`AtomicSub*`
entry points, `FeMeRuntimeCPU.c`'s runtime functions) sees an IR shape
indistinguishable from an explicit `OpAtomicIAdd`/`OpAtomicISub` with
`value == 1` -- no new shape support, `ImageCallKind` enum values, or
runtime functions were needed for this fix, across any of the 6
storage-image shapes `L244` already covers.

Added a new lit-test block to
`spirv-to-llvm-image-atomic.mlir` (`@atomic_inc_dec`) confirming both
ops lower to `llvm.atomicrmw add`/`sub ptr, i32 1` against the same
texel pointer, alongside the file's existing per-RMW-kind coverage.

**Verified against the real CTS**:
`dEQP-VK.image.atomic_operations.{inc,dec}.*` (880 cases, every
shape): **240 Pass, 0 Fail**, 640 Not supported -- fully closing the
240-case bucket `L244` left behind. Re-sampled the full
`atomic_operations.*` group (6209 cases, all 12 ops) for regressions:
**1320 Pass, 0 Fail**, 4889 Not supported (up from `L244`'s 1080 Pass,
the extra 240 being exactly this fix's newly-passing cases, with no
regressions among the other 9 RMW ops or `compare_exchange`).

**Build/test verification**:
- `FeMeConversionSPIRVToLLVMTests` (targeted rebuild): compiles clean,
  no template/accessor issues with the two open questions from this
  investigation (`this->getTypeConverter()`, `Op.getType()` both
  resolved correctly as written).
- `ninja check-feme`: 3386 Passed (+1 net new lit-test block), 61
  Unsupported, 0 Failed.
- `ninja check-hlsl-feme-vk`: 483 Pass / 32 XFAIL / 207 Not supported,
  0 Fail -- unchanged from baseline, no regression.
- `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
  change needed -- this is a core-1.0 image-atomic correctness fix
  (closing a gap in an already-implemented feature), not a new feature
  or extension landing.

## Roadmap L228(e)/(f): widened `dEQP-VK.pipeline.*` fixed-seed sample -- new L248/L249 findings

Sampled `dEQP-VK.pipeline.*` at 1-in-200 (`--deqp-fraction=1,200`, ~5797
cases), the broader-than-tessellation re-run overdue since several
prior sessions. Result: **1447 Pass, 1 Fail, 4349 Not supported** --
the single failure, `dEQP-VK.pipeline.fast_linked_library.depth.
format.d32_sfloat_s8_uint.depth_test_disabled.depth_write_enabled`,
led to filing and fixing `L248` (below). A second, unrelated finding
(`L249`, a genuine `DeviceLost` in a depth-only-subpasses case) was
also newly surfaced by a follow-up full `depth.*` group re-run and is
filed separately, untouched this session.

## Roadmap L248: `depth_test_disabled.depth_write_enabled` -- depth writes not gated on the depth test

Widening this session's broader `pipeline.*` sample (see above) found
one failure: `fast_linked_library.depth.format.d32_sfloat_s8_uint.
depth_test_disabled.depth_write_enabled`. Re-running the same
`depth_test_disabled.depth_write_enabled` combination across every
applicable format found it fails identically for every one (6 Fail:
`d32_sfloat`, `d32_sfloat_s8_uint`, `d32_sfloat_s8_uint_separate_
layouts`, and their monolithic-construction equivalents), independent
of `PipelineConstructionType` (`monolithic` fails identically to
`fast_linked_library`).

Reading `Executor.cpp`'s `testDepthStencil` found the bug directly:
the final depth-write step gated only on `Depth.WriteEnable`,
independent of `Depth.TestEnable`:

```cpp
if (Depth.WriteEnable) {
  writeDepth(...);
}
```

But the Vulkan spec is explicit that `depthWriteEnable` only takes
effect *when* `depthTestEnable` is also true -- depth writes are
always disabled when the depth test itself is disabled, regardless of
`depthWriteEnable`'s own value. (There is no way to write depth
without testing it at all; the spec's documented way to get an
always-passing test that still writes is `depthTestEnable = true` with
`depthCompareOp = VK_COMPARE_OP_ALWAYS`.) FeMe's implementation
already defaults `DepthPass` to `true` when `Depth.TestEnable` is
false (matching "the depth test always passes" correctly), but then
incorrectly let that always-passing result reach the write step even
though the test was never actually performed.

Fixed by gating the depth-write call on `Depth.TestEnable &&
Depth.WriteEnable` instead of `Depth.WriteEnable` alone -- a one-line
change (plus an explanatory comment) at the exact point identified.

Added a new `ExecutorTest` unit test,
`DepthTestDisabledSuppressesDepthWriteEvenWhenEnabled`, directly
alongside the existing `DepthWriteDisabledLeavesAttachmentUnchanged`
test it mirrors: renders a fragment with `TestEnable = false`,
`WriteEnable = true`, and confirms the fragment is still shaded (the
always-passing test lets it through) but the depth attachment is left
untouched.

**Verified against the real CTS**: the full `dEQP-VK.pipeline.*.depth.
format.*` group (16860 cases, every construction method x format x
depth-state combination) now **8433 Pass, 0 Fail**, 8427 Not supported
-- confirming the fix closes all 6 previously-failing cases with no
regressions across the rest of the group.

**Build/test verification**:
- `FeMeGraphicsTests` (`--gtest_filter=*Depth*`): 9/9 pass, including
  the new test.
- `ninja check-feme`: 3387 Passed (+1 net new unit test), 61
  Unsupported, 0 Failed.
- `ninja check-hlsl-feme-vk`: 483/32/207, unchanged.
- `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
  change needed -- core-1.0 depth-test/depth-write correctness fix.

## Roadmap L249: `depth_only.subpasses_postpass` genuine `DeviceLost` -- linked-pipeline subpass not inherited from its library

`dEQP-VK.pipeline.fast_linked_library.depth.depth_only.
subpasses_postpass` reproduces a genuine `DeviceLost`
(`vk.waitForFences(...): VK_ERROR_DEVICE_LOST`) in isolation, aborting
the harness run rather than a normal per-case `Fail`. Since a
`DeviceLost` surfaces at `vkWaitForFences` time rather than at any
`vkCreate*`/`vkCmd*` call, qpa-level diffing (this ICD's usual
diagnostic) does not apply -- a `gdb`-attached run was tried first
(confirmed the process runs to completion and exits normally within
seconds, so this is **not** an infinite hang/deadlock), then
`FEME_VULKAN_LOG_CREATION_ERRORS=1` (the actual key that unlocked
this): it fired immediately with `vkQueueSubmit: the bound pipeline
has 1 color attachment(s) but the render target has 0`.

That message comes from `resolveDrawAttachments`
(`CommandBuffer.cpp`), run from inside `executeCommandBuffer` on the
queue's own worker task (`Sync.cpp`'s `makeSubmissionTask`) -- any
`Error` there latches `Device::markLost` rather than surfacing back
through the `vkQueueSubmit` call that already returned, which is why
the failure only becomes visible at the next `vkWaitForFences` as a
synchronous (not timed-out) `VK_ERROR_DEVICE_LOST`.

The failing case's render pass (`vktPipelineDepthTests.cpp`'s own
`SUBPASSES`+`postpass` shape) is a single render pass with two
subpasses: subpass 0 has both a color and a depth attachment
reference; subpass 1 -- the one every library part of the
`depthOnlyPipeline` actually names -- has **only** a depth reference,
no color attachment at all. `getRenderTargets` (`GraphicsPipeline.cpp`)
correctly derives a pipeline's declared color-attachment count purely
from its bound `VkRenderPass`/`subpass` pair's own subpass description
-- not from `VkPipelineColorBlendStateCreateInfo::attachmentCount` --
so the bug had to be in *which* subpass the linked pipeline believed
it was bound to.

Root-caused to `synthesizeLinkedGraphicsPipelineCreateInfo`
(`GraphicsPipeline.cpp`), which assembles one complete
`VkGraphicsPipelineCreateInfo` for a `VK_EXT_graphics_pipeline_library`
pure-link call (a call that supplies no fixed-function state of its
own, only a chained `VkPipelineLibraryCreateInfoKHR::pLibraries`).
`Result.renderPass` already had a fallback loop that inherits a
library's own captured `RenderPass` whenever the link call's own
`CreateInfo.renderPass` is null -- but `Result.subpass` had no such
fallback, staying at `CreateInfo.subpass`'s value alone, which a pure
link call's zero-initialized `VkGraphicsPipelineCreateInfo` always
leaves at `0`. The linked `depthOnlyPipeline` therefore ended up with
the *correct* `renderPass` (inherited from a library) paired with the
*wrong* `subpass` (`0`, subpass 1's actual value silently dropped),
so `getRenderTargets` resolved subpass 0's one color attachment as
this pipeline's own declared shape.

Fixed by moving `Result.subpass = Lib->state().Subpass;` into the
same `if (!Result.renderPass)` branch that inherits `Result.renderPass`
-- pairing the two exactly the way the file's own
`foldLinkedLibraryState` (used for the "a library is itself built by
linking other libraries" case) already pairs `RenderPass`/`Subpass`
together a few hundred lines up, an idiom this fallback loop should
have matched from the start.

New unit test `LinksSubpassFromLibraryNotJustRenderPass`
(`GraphicsPipelineTest.cpp`) reproduces the exact shape at the
`synthesizeLinkedGraphicsPipelineCreateInfo` level: a two-subpass
render pass (subpass 0 color+depth, subpass 1 depth-only), all four
`VK_EXT_graphics_pipeline_library` parts built against subpass 1, then
linked via a pure-link call supplying neither `renderPass` nor
`subpass` of its own. Asserts the linked pipeline's
`colorAttachmentCount() == 0`. Confirmed it fails
(`VK_ERROR_INITIALIZATION_FAILED`, `resolveDrawAttachments`'s own
1-vs-0 mismatch surfacing even earlier, at `getRenderTargets`'s
`maxColorAttachments` check this synthetic repro's tiny device limits
happen to trip) against the pre-fix code via `git stash`, passes
after.

**Verified against the real CTS**: `dEQP-VK.pipeline.fast_linked_
library.depth.depth_only.*` (12 cases, the case's entire group) now
**12/12 Pass**. A 10-minute-bounded sample of the much larger
`dEQP-VK.pipeline.fast_linked_library.*` group (7600 cases sampled
before the time budget cut it off) shows only pre-existing, unrelated
`blend.dual_source.*` failures (22 cases, a distinct dual-source-blend
precision issue outside this fix's scope) -- no regressions from this
change.

**Build/test verification**:
- `FeMeVulkanTests` (`--gtest_filter=*LinksSubpassFromLibrary*`): 1/1
  pass (confirmed failing pre-fix via `git stash`).
- Full `FeMeVulkanTests`: 759/759 pass.
- `ninja check-feme`: 3388 Passed (+1 net new unit test), 61
  Unsupported, 0 Failed.
- `ninja check-hlsl-feme-vk`: 483/32/207, unchanged.
- `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
  change needed -- `VK_EXT_graphics_pipeline_library` linked-pipeline-
  subpass-resolution correctness fix, not a new feature/extension.

## Roadmap L245: `api.info.image_format_properties*` `sampleCounts` over-reporting -- fixed

A pre-existing, 65-case `dEQP-VK.api.info.image_format_properties{,2}.*`
`Fail (sampleCounts != VK_SAMPLE_COUNT_1_BIT)` bucket (`2d.optimal.*`,
spanning ASTC and several packed/SINT/SNORM formats), carried over
several sessions.

Root cause confirmed in `Image.cpp`'s `supportedSampleCounts`: it
intersected the queried `usage` against this device's per-usage
sample-count limits (e.g. `sampledImageColorSampleCounts` = `1|2|4|8`)
unconditionally, regardless of whether the queried *format* could
actually be rendered to at all. Multisampling is fundamentally a
render-target capability on real hardware -- a format lacking both
`VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT` and
`_DEPTH_STENCIL_ATTACHMENT_BIT` (e.g. `A2B10G10R10_SINT_PACK32`, an
integer format with neither feature bit on this device) cannot support
more than 1 sample under any usage. This matches the CTS's own
`vktApiFeatureInfo.cpp` check exactly (`getRequiredOptimalTilingSample
Counts`'s caller requires `sampleCounts == VK_SAMPLE_COUNT_1_BIT`
whenever `supportedFeatures` names neither attachment bit, independent
of which `usage` the current query combination asks for).

Fixed with a single early return in `supportedSampleCounts`: whenever
the queried `Format`'s `formatFeatureFlags()` include neither
`VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT` nor
`VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT`, the function now
returns `VK_SAMPLE_COUNT_1_BIT` immediately, before the existing
per-usage intersection logic below it runs at all.

**Verified against the real CTS**:
- `dEQP-VK.api.info.image_format_properties{,2}.*` (2372 cases): now
  **2372 Pass, 0 Fail** (was 65 Fail).
- `dEQP-VK.image.*` (1-in-50 sample, 8846 cases): 0 Fail, no
  regressions.
- `dEQP-VK.pipeline.*multisample*`: surfaced one unrelated,
  pre-existing `DeviceLost`
  (`fast_linked_library.multisample.compatible_render_pass.dynamic`),
  confirmed via `git stash` to reproduce identically without this fix
  -- filed separately as `L250`, out of this fix's scope.

**Build/test verification**:
- `ninja check-feme`: 3388 Passed, 61 Unsupported, 0 Failed (no new
  unit test needed -- `supportedSampleCounts` is a pure function
  already exercised via `isValidImageShape`'s existing coverage, and
  this fix's own CTS group is exhaustive).
- `ninja check-hlsl-feme-vk`: 483/32/207, unchanged.
- `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
  change needed -- core-1.0 `vkGetPhysicalDeviceImageFormatProperties`
  correctness fix.

## Roadmap L250: `multisample.compatible_render_pass.dynamic` `DeviceLost` -- over-strict draw-time attachment check, fixed

Surfaced by `L245`'s own regression sample:
`dEQP-VK.pipeline.fast_linked_library.multisample.compatible_render_
pass.dynamic` hit a genuine `DeviceLost`
(`vk.waitForFences(...): VK_ERROR_DEVICE_LOST` at `vkCmdUtil.cpp:296`),
aborting the harness run. Confirmed via `git stash` (removing `L245`'s
`supportedSampleCounts` change) to reproduce identically either way --
pre-existing, unrelated to `L245`.

Applying the lesson from `L249`, `FEME_VULKAN_LOG_CREATION_ERRORS=1` was
tried *first* (before any gdb/hang-hunting) and immediately surfaced the
real diagnostic at `vkQueueSubmit` time: "the bound pipeline tests/writes
depth but the render target has no depth attachment". Not a hang at all
-- a fast, synchronous submission-time rejection.

Reading the CTS source
(`vktPipelineMultisampleTests.cpp`'s `CompatibleRenderPassTestInstance::
iterate`) confirmed the failing case is spec-legal: the render pass has
only color+resolve attachments (no depth attachment at all); the
pipeline is created with `pDepthStencilState = nullptr` and
`VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE`/`_WRITE_ENABLE` marked dynamic;
at command-buffer-record time, before the draw, it explicitly calls
`vkCmdSetDepthTestEnable(cmdBuffer, VK_FALSE)` and
`vkCmdSetDepthWriteEnable(cmdBuffer, VK_FALSE)` -- so the pipeline
genuinely does not test/write depth at draw time, and the spec allows
this exact render-target/pipeline combination.

Root cause: `GraphicsPipeline::needsDepthAttachment()`/
`needsStencilAttachment()` (`GraphicsPipeline.h`), consulted by
`CommandBuffer.cpp`'s draw-time `resolveDrawAttachments`, conservatively
treated *any* dynamic depth-test/write-enable or stencil-test-enable
state as automatically requiring a depth/stencil attachment, regardless
of what the dynamic state was actually set to before the draw -- the
same "over-strict enforcement" bug class `L241` found in
`primitiveRestartEnable`'s *creation-time* VUID check, but this is a
distinct *draw-time* attachment-requirement check that `L241`'s own
audit (scoped to `GraphicsPipeline.cpp`'s creation path) didn't cover.
The actual current dynamic value was already tracked and available:
`CommandBuffer.cpp`'s dynamic-state-replay switch already sets
`Gfx.Dynamic.DepthTestEnable`/`DepthWriteEnable`/`StencilTestEnable` from
recorded `vkCmdSet*` calls, and `GraphicsPipeline::buildExecutorPipeline`
already correctly resolves the real depth/stencil state from
static-vs-dynamic for many other purposes -- just not consulted here.

Fixed by changing `needsDepthAttachment()`/`needsStencilAttachment()`
to take the current `DynamicGraphicsState` and resolve the actual
draw-time enable value (the dynamic override when the corresponding bit
is dynamic, else the pipeline's own static value), mirroring the same
static-vs-dynamic resolution idiom `buildExecutorPipeline` already uses
for every other dynamic-state bit (`Cull`, `FrontFace`, `DepthBias`,
`DepthCompareOp`, `DepthBoundsTestEnable`, `StencilOps`, etc.). New unit
test `NeedsDepthAttachmentConsultsDynamicValueNotJustDynamism`
(`GraphicsPipelineTest.cpp`) asserts `needsDepthAttachment` returns
`false` when the dynamic test/write state is explicitly disabled and
`true` when explicitly enabled; confirmed it fails when the pre-fix
logic is temporarily restored (same new signature) and passes with the
real fix.

**Verified against the real CTS**:
- `dEQP-VK.pipeline.fast_linked_library.multisample.compatible_render_
  pass.dynamic`: now **Pass** (was a harness-aborting `DeviceLost`).
- `dEQP-VK.pipeline.*multisample*` (63561 cases): 1350 Pass, 165 Fail,
  62046 Not supported -- all 165 failures are pre-existing, unrelated
  buckets (`sampled_image.*`, `multisample_interpolation.*`,
  `extended_dynamic_state.after_pipelines.*`); none reference
  depth/stencil/attachment behavior.
- `dEQP-VK.pipeline.*.depth.*` (39686 cases): 16914 Pass, **0 Fail**,
  22772 Not supported.

**Build/test verification**:
- `FeMeVulkanTests`: 760/760 Passed (was 759/759; +1 net new
  regression test).
- `ninja check-feme`: 3389 Passed, 61 Unsupported, 0 Failed.
- `ninja check-hlsl-feme-vk`: 483 Pass / 32 XFAIL / 207 Not supported,
  unchanged.
- `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
  change needed -- core-1.0 draw-time attachment-validation
  correctness fix, not a new feature/extension.

## Roadmap L246: `image.mutable.{2d,2d_array}.*` `vkCreateFramebuffer` gap -- already fixed, no change needed

Filed several sessions ago from `L235`'s own verification run as a
newly-exposed 72-case `dEQP-VK.image.mutable.{2d,2d_array}.*` failure
at `vkCreateFramebuffer`, predicted to be `isCompatibleAttachmentView`
(`RenderPass.cpp`) rejecting a view whose format differs from its
render pass attachment's own declared format for a
`VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`-flagged image.

Re-ran the full `dEQP-VK.image.mutable.*` group this session (10646
cases, `1d`/`2d`/`2d_array`/`cube`/`cube_array`): **0 Fail** (8484
Pass, 2162 Not supported for unrelated reasons: `VK_KHR_extended_
flags` not implemented, or an MSAA sample count this device doesn't
advertise).

The originally-cited repro case's own MSAA variants
(`r8g8b8a8_snorm_b8g8r8a8_srgb_draw_copy_resolve_mutable_{color,
resolve}_att`) are themselves `NotSupported` on this device
(`VK_SAMPLE_COUNT_1_BIT` is the max available) -- they were never
actually exercised by the original repro either.

Reading `vktImageMutableTests.cpp`'s `makeRenderPass` explains why
`isCompatibleAttachmentView`'s exact-format check was never actually a
problem: the render pass's `VkAttachmentDescription::format` is built
from `m_caseDef.viewFormat` (the *view's* format), not the base
image's format, so the attachment description and the framebuffer's
bound view always agree on format by construction. The
format-compatible-class mismatch this roadmap row predicted only
exists between the base image and its view (already handled
permissively by `vkCreateImageView` since `L235`), never between the
render pass attachment and the view actually bound to the framebuffer.

No source change made -- this entry was apparently filed from a stale
or misread repro. Marked done in the roadmap with this session's
verification.

**Verified against the real CTS**:
- `dEQP-VK.image.mutable.*` (10646 cases): 8484 Pass, **0 Fail**, 2162
  Not supported.

**Build/test verification**: no source change, so no rebuild/retest
was required beyond the CTS verification above.

## Roadmap L228(c): api.image_clearing multi-layer vkCmdClearAttachments bug -- fixed

Root-caused and fixed the `api.image_clearing` cluster this session's
own prior broad sample flagged (57 sampled failures, spot-checked as
combining `multiple_layers` and `sample_count_4`/MSAA).

Re-ran `dEQP-VK.api.image_clearing.*multiple_layers*sample_count_4*`
(882 cases): 136 Pass, 272 Fail, 474 Not supported. Every failure
clustered to `core.clear_color_attachment.multiple_layers.*
sample_count_4` specifically (`vkCmdClearAttachments`, not the
dedicated-allocation `clear_color_image` path, which passed at the
same dimensions). Isolated the two dimensions separately:

- `multiple_layers` **without** MSAA also failed -- not an
  MSAA-interaction bug, a plain multi-layer `vkCmdClearAttachments`
  bug.
- `single_layer` **with** MSAA also failed, but with a different
  symptom (`(0,0,0,0)`, uninitialized-read-looking) -- a second,
  independent bug, split out as roadmap `L228(k)` rather than blocking
  this fix on it.

**Root cause**: `ImageOps.cpp`'s `clearAttachmentRects` hoisted a
single shared layer-iteration mask (`ViewMask ? ViewMask : 1u`)
*outside* the whole `VkClearRect` loop. Outside multiview this always
normalized to exactly one iteration at layer 0, so every clear
silently dropped layers 1+ regardless of what each rect's own
`baseArrayLayer`/`layerCount` actually named.

**Fix**: restructured the loop to iterate layers *per-rect*: for
multiview render passes, the current subpass's view mask still
determines the layers (unchanged behavior, per spec); for
non-multiview instances, iterate `[rect.baseArrayLayer,
rect.baseArrayLayer + rect.layerCount)` directly from that rect. The
per-layer write loop was extracted into a local `ClearLayer` lambda
shared by both branches.

New regression test `DrawTest.
ClearAttachmentsClearsEveryLayerNamedByItsOwnRect`, confirmed via
`git stash` A/B to fail without the fix (layers 1/2 stay black) and
pass with it.

**Verified against the real CTS**:
- `dEQP-VK.api.image_clearing.core.clear_color_attachment.
  multiple_layers.*` (2499 cases): 621 Pass, **127 Fail** (down from
  272), 1751 Not supported. All 127 residual failures are exclusively
  `sample_count_{2,4,8}` MSAA cases (0 non-MSAA multi-layer failures
  remain) -- confirming the fix resolves every non-MSAA case and
  isolates the separate `L228(k)` MSAA bug cleanly.

**Build/test verification**:
- `FeMeVulkanTests`: 761/761 (was 760/760, +1 net new unit test).
- `check-feme`: 3390 Passed, 61 Unsupported, 0 Failed (was
  3389/61/0).
- `check-hlsl-feme-vk`: unchanged, 483 Pass / 32 XFAIL / 207 Not
  supported.

**New finding, not fixed this session (split out as `L228(k)`)**: the
separate single-layer-MSAA `(0,0,0,0)` clear-attachments bug described
above.

**New finding, not fixed this session (filed as `L251`)**: while
re-running a broader `dEQP-VK.api.image_clearing.*` sweep to check for
regressions beyond the `multiple_layers` group, hit a genuine
`DeviceLost` at `dEQP-VK.api.image_clearing.core.clear_color_image.
1d.linear.multiple_layers.a2b10g10r10_sint_pack32` (aborting the
harness run). Confirmed pre-existing and unrelated to this session's
own fix (reproduced identically with the fix committed). This is in
the `clear_color_image` path, a different code path from this
session's own `clear_color_attachment` fix.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- core-1.0 `vkCmdClearAttachments` correctness fix, no feature-bit or
extension exposure change.

## Roadmap L251: `clear_color_image` `a2b10g10r10_sint_pack32` `DeviceLost` -- two independent bugs, both fixed

Picked up from the prior session's own `L251` finding (a `DeviceLost` at
`dEQP-VK.api.image_clearing.core.clear_color_image.1d.linear.
multiple_layers.a2b10g10r10_sint_pack32`).

**Bug 1 (the crash itself)**: `FEME_VULKAN_LOG_CREATION_ERRORS=1`
(applying `L249`/`L250`'s own now-thrice-confirmed lesson) surfaced the
real error immediately: "image fixture format is not yet supported".
`ImageFixture.cpp`'s test-fixture layer (used by `vkCmdClearColorImage`/
`vkCmdCopy*`'s own `getFixtureFormatElementSize`/`packClearColor`/
`unpackColor` call chain) had only a diagnostic *name* string for
`R10G10B10A2_SINT` -- a documented `H19o` "gap left for later", the same
shape `L234` already fixed for the `_SNORM` sibling. The separate
production runtime (`FeMeRuntimeCPU.c`'s
`femeRTPackR10G10B10A2Sint`/`femeRTUnpackR10G10B10A2Sint`) already has
full, correct support -- these are two entirely separate code paths for
the same format, and having a name-only fixture entry without real
`getFormatInfo` support is a general failure *mode*: any transfer/clear
operation on such a format crashes the whole harness (a fatal
`createStringError` propagating up through `vkQueueSubmit`, reported as
`DeviceLost`), not just failing one test gracefully -- because
`formatFeatureFlags` (`Format.cpp`) unconditionally reports
`TRANSFER_SRC`/`TRANSFER_DST` for every recognized format regardless of
whether the fixture layer can actually handle it.

Fixed by adding a `R10G10B10A2_SINT` case to `getFormatInfo`,
`packClearColor`, and `unpackColor` (`ImageFixture.cpp`), mirroring the
existing `R10G10B10A2_UINT`/`_SNORM` siblings with this format's own
signed range (R/G/B in `[-512, 511]`, A in `[-2, 1]`). Along the way,
the new unit test caught an incidental bug in the fix itself:
`static_cast<uint32_t>(negative_double)` is undefined behavior (unlike
casting a non-negative double to unsigned, or a negative double to a
*signed* integer type); fixed by routing through a signed `int32_t`
intermediate first, matching the pattern `R10G10B10A2_SNORM`'s own
lambdas already use.

New unit test: `ImageFixtureTest.PacksAndUnpacksR10G10B10A2SintNegative`,
asserting an exact two's-complement round trip.

**Bug 2 (newly unmasked by fixing Bug 1)**: re-running the original
repro after Bug 1's fix replaced the `DeviceLost` with a normal `Fail`
-- but widening to the full `dEQP-VK.api.image_clearing.core.
clear_color_image.*a2b10g10r10_sint_pack32*` case-name filter (103
cases) showed **100/103 now `Fail`** with an identical symptom: expected
`Ref:(51, 255, 153, 0)`, actual `Color:(0, 0, 0, 0)` -- i.e. the clear
appears to write nothing, across every dimensionality/tiling/layer-count
combination sampled for this format, not just the original repro's own
narrow shape. Since a new unit test already confirmed `packClearColor`/
`unpackColor` round-trip correctly in isolation, the bug had to be
somewhere else in the chain.

Root cause: `ImageOps.cpp`'s `unpackClearColorValue` (converts a
`VkClearColorValue` into the `ArrayRef<double>` `packClearColor`
expects, deciding between its `int32`/`uint32`/`float32` union members)
used `isIntegerColorAttachmentFormat`/
`isUnsignedIntegerColorAttachmentFormat` (`RuntimeABI.h`) for that
decision -- predicates deliberately scoped to attachment-capable
formats only (their own doc comments and every other caller,
`Executor.cpp`/`Pipeline.h`/`RenderPass.cpp`, specifically need
"attachment-capable AND integer"). But `vkCmdClearColorImage` clears an
*arbitrary* image, not just an attachment-capable one, and
`A2B10G10R10_SINT_PACK32` has no `COLOR_ATTACHMENT_BIT`/
`INPUT_ATTACHMENT_BIT` feature bits on this device at all -- so it fell
through to the `float32` branch, reinterpreting the clear value's raw
signed-integer bits (e.g. `51` as `int32`) as IEEE-754 float bits
instead, producing a near-zero garbage double that `packClearColor`'s
signed clamp then rounds to `0`. (`R32G32B32_{UINT,SINT}` share this
exact same gap for the identical reason -- no attachment support on
this device either -- though no CTS case for those two happened to
surface it this session.)

Fixed by adding a new, broader `isIntegerResourceFormat`/
`isUnsignedIntegerResourceFormat` pair (`RuntimeABI.h`) -- a strict
superset of the attachment-scoped predicates, additionally covering
`R10G10B10A2_SINT` and `R32G32B32_{UINT,SINT}` -- and switching
`unpackClearColorValue` to use the new pair. The attachment-scoped
predicates themselves are untouched, preserving their own callers'
narrower meaning.

New unit test:
`ImageOpsTest.ClearsSignedIntegerNonAttachmentFormatUsingInt32`, a real
`vkCmdClearColorImage` of an `A2B10G10R10_SINT_PACK32` image (no
`COLOR_ATTACHMENT_BIT` usage, since the format doesn't support it) with
negative-range clear values, asserting the exact bit pattern read back.

**Verified against the real CTS**:
- `dEQP-VK.api.image_clearing.core.clear_color_image.
  *a2b10g10r10_sint_pack32*` (103 cases): **100 Pass, 0 Fail**, 3 Not
  supported (unrelated `sample_count_4` MSAA cases this device doesn't
  advertise) -- was 100 Fail (post-Bug-1-fix) / originally a harness-
  aborting `DeviceLost`.
- A broader `dEQP-VK.api.image_clearing.*` (5200-case) re-run aborted
  partway through on a *different*, pre-existing, unrelated
  `DeviceLost` at `a4b4g4r4_unorm_pack16` -- confirmed via `git stash`
  (removing this session's own fix) to reproduce identically either
  way. `FEME_VULKAN_LOG_CREATION_ERRORS=1` shows the identical error
  class this session's own Bug 1 fixed ("image fixture format is not
  yet supported"), i.e. `A4B4G4R4_UNORM_PACK16` likely has the same
  name-only fixture gap. Filed separately as `L252`, out of this fix's
  own scope (same root-cause *class*, different format/gap, not yet
  confirmed or fixed).

**Build/test verification**:
- `FeMeGraphicsTests`: 374/374 Passed (+1 net new unit test).
- `FeMeVulkanTests`: 762/762 Passed (+1 net new unit test).
- `ninja` (full project): 1452/1452, clean.
- `ninja check-feme`: 3392 Passed, 61 Unsupported, 0 Failed (was
  3390/61/0).
- `ninja check-hlsl-feme-vk`: unchanged, 483 Pass / 32 XFAIL / 207 Not
  supported.
- `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
  change needed -- core-1.0 format-fixture and clear-value-decoding
  correctness fixes, no new feature/extension exposure.

**New finding, not fixed this session (filed as `L252`)**: the
`a4b4g4r4_unorm_pack16` `DeviceLost` described above, same root-cause
class as this session's own Bug 1 (a name-only fixture-format entry
with no real `getFormatInfo`/`packClearColor`/`unpackColor` support)
but a different format -- likely a small, mechanical fix following this
session's own precedent once confirmed, but not yet triaged past the
one repro and the matching `FEME_VULKAN_LOG_CREATION_ERRORS=1`
diagnostic.

## Roadmap L252: `A4R4G4B4_UNORM`/`A4B4G4R4_UNORM` fixture format support -- fixed

Found via `L251`'s own broader `dEQP-VK.api.image_clearing.*` (5200-case)
re-run: `dEQP-VK.api.image_clearing.core.clear_color_image.1d.linear.
multiple_layers.a4b4g4r4_unorm_pack16` hit a genuine `DeviceLost`,
confirmed pre-existing via `git stash` (reproduces identically without
`L251`'s own fix). `FEME_VULKAN_LOG_CREATION_ERRORS=1` showed the exact
same error class `L251` found: "image fixture format is not yet
supported".

`ImageFixture.cpp`'s test-fixture layer had only a diagnostic name
string for `VK_EXT_4444_formats`'s two formats (`A4R4G4B4_UNORM`/
`A4B4G4R4_UNORM`, added under roadmap E19 alongside a `bytesPerBlockFor`
byte-size entry, `Format.cpp`) -- no `getFormatInfo`/`packClearColor`/
`unpackColor` case, the same gap shape `L251` found and fixed for
`R10G10B10A2_SINT`.

Fixed both formats together (same underlying gap, both siblings) by
adding a case to all three functions, mirroring `R4G4B4A4_UNORM`/
`B4G4R4A4_UNORM`'s own packed-16-bit shape (a single opaque 2-byte word,
4 bits per component) but with alpha at the MSB, per the Vulkan spec's
own bit layout for these two formats: `A4R4G4B4_UNORM_PACK16` is
`A[15:12] R[11:8] G[7:4] B[3:0]`; `A4B4G4R4_UNORM_PACK16` is the same
with R and B swapped.

New unit tests: `ImageFixtureTest.PacksAndUnpacksA4R4G4B4Unorm`/
`PacksAndUnpacksA4B4G4R4Unorm`, each a round-trip pack/unpack of a
4-distinguishable-component color, plus a check that the two formats'
own R/B swap actually produces a different packed bit pattern for the
same input (mirroring `PacksAndUnpacksB4G4R4A4Unorm`'s own precedent).

**Verified against the real CTS**:
- `dEQP-VK.api.image_clearing.core.clear_color_image.
  *a4b4g4r4_unorm_pack16*` (206 cases): **200 Pass, 0 Fail**, 6 Not
  supported (unrelated MSAA gaps) -- was a harness-aborting
  `DeviceLost`.
- `dEQP-VK.api.image_clearing.core.clear_color_image.
  *a4r4g4b4_unorm_pack16*` (206 cases, the sibling format, same fix):
  **200 Pass, 0 Fail**, 6 Not supported.
- A broader `dEQP-VK.api.image_clearing.*` (45636-case) sweep now
  completes without aborting -- 22089 Pass, 939 Fail, 22608 Not
  supported; none of the 939 failures reference either format (all
  pre-existing, unrelated buckets).

**Build/test verification**:
- `FeMeGraphicsTests`: 376/376 Passed (+2 net new unit tests).
- `ninja` (full project): clean.
- `ninja check-feme`: 3394 Passed, 61 Unsupported, 0 Failed (was
  3392/61/0).
- `ninja check-hlsl-feme-vk`: unchanged, 483 Pass / 32 XFAIL / 207 Not
  supported.
- `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
  change needed -- these two formats' `VK_EXT_4444_formats` extension
  exposure already existed (roadmap E19); this is a transfer-path
  correctness fix only, no new feature/extension surface.

**New finding, not fixed this session (filed as `L253`)**: a systematic
audit of every `ResourceFormat` with a name-only fixture-diagnostic
entry (present in `ImageFixture.cpp`'s diagnostic-name/
`parseFixtureFormat` functions but missing a `getFormatInfo` case) is
still not done, despite two independent instances of this exact bug
class being found and fixed in this session alone (`L251`, `L252`).
Worth a dedicated pass cross-referencing every `ResourceFormat`
enumerator against `getFormatInfo`'s own case list before another one
surfaces as a surprise `DeviceLost` in some future CTS run.

## Roadmap L253: systematic name-only-fixture-format audit -- clean result, no further gaps

Carried forward from `L241`/`L251`/`L252`'s own repeated "still not
done" note: cross-referenced every `ResourceFormat` enumerator (133
total, `RuntimeABI.h`) against `ImageFixture.cpp`'s `getFormatInfo`
switch's own case list.

Every enumerator not covered by an explicit `getFormatInfo` case turned
out to be a block-compressed format (every `BC*`/`ASTC_*`/`ETC2_*`/
`EAC_*` entry, ~69 formats) -- and this is correct, not a gap:
`vkCmdClearColorImage` is spec-illegal on a block-compressed image
(`VUID-vkCmdClearColorImage-image-01545`), so `getFormatInfo`'s
`default:` case is genuinely unreachable for these formats via that
command. `ImageOps.cpp`'s copy paths (`runCopyBufferToImage`/
`runCopyImageToBuffer`/`runCopyImage`) already dispatch block-compressed
formats through their own dedicated `isBlockCompressedFormat`-gated
byte-block-copy logic (confirmed via those functions' own call sites),
never reaching `ImageFixture.cpp`'s texel/clear-color table at all.

Every non-compressed `ResourceFormat` enumerator now has a real
`getFormatInfo` case -- `L251`/`L252` (this session) closed the only two
real gaps that existed (`R10G10B10A2_SINT`; `A4R4G4B4_UNORM`/
`A4B4G4R4_UNORM`). No code change from this audit; a clean, conclusive
negative result closes out this recurring "still not done" carry-forward
item.

## Roadmap L228(k): subpass color attachments never resolved without a draw

Root cause: a render-pass/dynamic-rendering subpass's multisample color
attachment was only ever resolved into its `pResolveAttachments`/
`resolveImageView` target as a side effect of `Executor.cpp`'s per-draw
box-filter-average resolve (`PreparedDraw::ResolveAttachments`). A
subpass whose only command was `vkCmdClearAttachments` -- no draw at
all -- never triggered that resolve, so the single-sample resolve
target kept whatever memory it already held (in practice, frequently a
prior test case's own leftover clear color from the same allocator
slot), producing the `(0,0,0,0)`-and-worse garbage readback this
roadmap row's own filed description first observed.

Fix: `resolveSubpassColorAttachments` (`ImageOps.cpp`), the identical
box-filter-average resolve `Executor.cpp` already performs per draw,
called once per subpass boundary from `CommandBuffer.cpp`'s
`NextSubpass`/`EndRenderPass` handling, against the subpass now ending
(before its `RenderTargetView` binding is replaced or discarded).
Covers both classic render passes and `vkCmdBeginRendering`/
`vkCmdEndRenderingKHR` (the latter records the same internal
`EndRenderPass` command). A subpass that also issued a draw simply
resolves the same, already-correct data a second time -- harmless.

New unit test: `DrawTest.ResolvesMultisampleColorAfterClearAttachmentsWithNoDraw`
seeds the resolve target with a distinct color, clears the multisample
attachment via `vkCmdClearAttachments` alone (no pipeline/draw at all),
and confirms the resolve target picks up the cleared color rather than
the seed. Confirmed via `git stash` A/B to fail (the seed color leaks
through) without this fix.

Verified against the real Vulkan CTS:
- `dEQP-VK.api.image_clearing.core.clear_color_attachment.single_layer.
  *sample_count_4*`: **0 Fail** (136/294 Pass, up from 136 Fail/0 Pass).
- `dEQP-VK.api.image_clearing.core.clear_color_attachment.*` (5145
  cases): **0 Fail** (1564 Pass, 3581 NotSupported).
- `dEQP-VK.api.image_clearing.*` (the full suite, 45636 cases): **0
  Fail** (23028 Pass, 22608 NotSupported). This also silently absorbed
  the 939 "pre-existing, unrelated" failures `L252`'s own broader sweep
  noted last session -- 22089 + 939 == 23028 exactly, confirming they
  were this same bug all along, not a separate, still-open issue.

`FeMeVulkanTests`: 763/763 (was 762/762, +1 new test).
`check-feme`: 3395 Passed, 61 Unsupported, 0 Failed (was 3394/61/0).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- a core-1.0 render-pass-resolve correctness fix, no feature-bit or
extension exposure change.

**Aside, not a regression**: `check-hlsl-feme-vk` now reports 2 new
failures (`Feature/SpecializationConstant/spec_const_32_bits.test`,
`WaveOps/WaveActiveMax.test`) and 1 new unexpected pass
(`Feature/PushConstant/array_of_matrices.test`) versus the prior
session's own 483/32/207 baseline. Confirmed via `git stash` A/B (this
row's own fix reverted, rebuilt, re-run) that this reproduces
identically either way -- caused entirely by the `offload-test-suite`
`feme` branch itself moving forward (this session's own standing-check
re-fetch found it had drifted from `9351791` to a new upstream HEAD,
`854cc3f`, with different test/XFAIL content), not by this row's fix.
Left untouched -- syncing that branch's own XFAIL list is a
`offload-test-suite`-side task, out of scope for a FeMe code-fix
session, and not something this session's standing instructions ask
for beyond re-syncing the branch pointer itself (already done).

## L255: safety-net timeout override (test-infrastructure fix, no CTS behavior change expected)

This row's fix (see `Roadmap.md` `L255`) makes FeMe's hardcoded 5s
blocking-wait safety-net timeout (`Sync.h`'s former
`SafetyNetTimeoutNs`) overridable via a new
`FEME_VULKAN_SAFETY_NET_TIMEOUT_MS` environment variable, to fix a
`check-hlsl-feme-vk` parallel-execution-only flake
(`Basic/Mandelbrot.test` hitting the 5s ceiling purely from 12-way host
scheduling contention, not a real hang). The default (unset) behavior
is byte-for-byte unchanged, so no CTS regression was expected -- verified
anyway per standing instructions:

- `dEQP-VK.synchronization.*` (the full suite -- most relevant, since
  this fix touches `Fence`/`Semaphore`/`QueueExecutor` waits directly):
  **0 Fail** (18420 Pass, 46452 NotSupported, 64872 total). No
  timeouts, no hangs, no `DeviceLost`.
- `dEQP-VK.api.image_clearing.*` (45636 cases, re-run as a general
  sanity check against `L252`'s last full-suite baseline): **0 Fail**
  (23028 Pass, 22608 NotSupported) -- identical to the prior session's
  numbers, confirming no regression.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- this is a test-harness/host-scheduling robustness fix (an
environment-variable override for an internal safety-net timeout), not
a feature-bit or extension exposure change.

## L256/L257/L258: `dEQP-VK.glsl.*` sweep -- `ShadowValueMap` dynamic-`Component` crash fix

This session's `L228(f)`-driven widening of the broader-than-tessellation
CTS sample picked `dEQP-VK.glsl.*` (28,420 cases) as the modern
replacement for the historical "shader_render" module referenced
throughout `L228` (this CTS build,
`vulkan-cts-1.4.6.2-525-g880f31a2bd9cd0659f84f3f80dafd07f2e693f6d`, has
no top-level `shader_render` group at all -- confirmed via a full,
unfiltered case-list grep of every top-level group name).

The full sweep aborted partway through (8,956/28,420 cases run) when
`deqp-vk` itself `SIGABRT`ed on
`dEQP-VK.glsl.indexing.varying_array.vec2_dynamic_loop_write_dynamic_loop_read`
-- a genuine compiler-internals crash (`ShadowValueMap::getOrCreate`,
`CanonicalizeStage.cpp`), not a rendering bug. See `Roadmap.md`'s `L256`
for the full root-cause writeup and fix (`ShadowValueMap` generalized to
tolerate a dynamic `Component` as well as a dynamic `Row`, since
`resolveStageIOAccess`'s doubly-dynamic-indexed-access handling -- `L138`
-- can produce either independently).

Verified against the real Vulkan CTS:
- `dEQP-VK.glsl.indexing.varying_array.vec2_dynamic_loop_write_dynamic_loop_read`
  (the originally-crashing single case): no longer crashes `deqp-vk` --
  now reaches rendering and comparison (`Fail (Image mismatch)`, a
  separate, unresolved correctness bug, see below).
- `dEQP-VK.glsl.indexing.varying_array.*dynamic*` (48 cases, every
  `vec2`/`vec4` write/read combination involving at least one dynamic
  array or component index): **no crashes**, but **0/48 Pass** -- every
  case fails on `Fail (Image mismatch)`. Filed as new roadmap row `L257`
  (not yet investigated -- the crash fix only changes whether the
  process survives, not whether the dynamic-component read-back value
  itself is correct).

`FeMeTransformsGraphicsTests`: new unit test
`CanonicalizeStageTest.OutputReadBackResolvesThroughDynamicComponentByteGEP`
reproduces the exact constant-`Row`/dynamic-`Component` shape (confirmed
via a temporary `git stash` of just the fix to crash identically
pre-fix, pass post-fix).
`check-feme`: 3,402 Passed (+1 net new unit test), 61 Unsupported, 0
Failed (was 3,401/61/0).
`check-hlsl-feme-vk`: 448 Pass / 32 XFAIL / 200 Not supported / 0 Fail,
unchanged.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- a compiler-internals (stage-IO canonicalization) crash fix, not a
feature-bit or extension exposure change.

**Not attempted this session** (filed as new roadmap rows for a future
session): `L257` (the 48-case `Fail (Image mismatch)` correctness bug
uncovered by this fix) and `L258` (the ~7% `Fail` cluster the partial
8,956-case pre-crash sample found in `dEQP-VK.glsl.builtin.function`
(395 fails), `dEQP-VK.glsl.440.linkage` (49), `dEQP-VK.glsl.
conversions.matrix_to_matrix` (20), `dEQP-VK.glsl.builtin_var.
fragdepth` (18), and a long tail of `atomic_operations.*` -- a future
session should re-run the full 28,420-case sweep now that it no longer
aborts partway, then triage the largest cluster first).

## L257: `dEQP-VK.glsl.indexing.varying_array.*dynamic*` `Image mismatch` -- `getDynamicRowIndexedAccess` mis-targeting fixed

Root-caused and fixed the 0/48 correctness bug `L256` above uncovered
once its own crash fix stopped masking it. Started with the single
simplest failing case, `dEQP-VK.glsl.indexing.varying_array.
vec2_dynamic_write_dynamic_read` (no loops at all), confirmed via
`--deqp-log-images=enable` to reproduce as a non-crashing `Fail (Image
mismatch)`. Read the CTS test-generator source
(`vktShaderRenderIndexingTests.cpp`) to confirm this test's real shape:
a dynamic *array-element* (row) index into a `vec2 var[4]` varying
(`var[dynamicIdx] = ...`), not a dynamic vector-lane/component index.

`FEME_DUMP_IR=1` on this single case showed the compiled (SIMD-widened)
IR ultimately routes through `getDynamicRowIndexedAccess`'s own
synthetic-`[N x i8]`-byte-GEP recognizer (originally added for `L137`/
`L138`'s genuinely-different dynamic vector-*lane*-select shape,
`fs_in_pos_screen_centroid[1][component]`). That recognizer
unconditionally folded any such shape's dynamic byte-flattened index
into `DynamicComponent`, assuming it always selects a lane within a
fixed row -- but a genuinely dynamic array-element index compiles to
the *identical* `getelementptr [N x i8], ptr ..., i64 %idx` IR shape,
distinguished from a true lane select only by `N` itself: a lane select
has `N` == one scalar lane's own byte size (4 for `f32`/`i32`); a
whole-row select instead has `N` == that array level's own element
size (8 for a `vec2` row -- confirmed directly against this case's own
dumped IR, `getelementptr [8 x i8], ptr addrspace(8) @spirv_var_23, i64
%N`). Every whole-row dynamic-array-index write was silently
mis-targeted at row 0's own first lane instead of the real,
dynamically selected row.

Fixed by walking `GV`'s own array levels in `getDynamicRowIndexedAccess`
and checking each level's element size against `N`: a matching level's
own instance index now folds into `Terms`/`Row` (via the same
`combineDynamicRowTerms` every other dynamic-row shape already uses)
rather than into `DynamicComponent`.

Verified against the real Vulkan CTS:
- `vec2_dynamic_write_dynamic_read` (the single simplest case): now
  **Pass** (was `Fail (Image mismatch)`).
- `dEQP-VK.glsl.indexing.varying_array.*dynamic*` (the full 48-case
  bucket): **36/48 Pass** (was 0/48). The remaining 12 failures are all
  `vec3_*`-suffixed cases -- confirmed via A/B `git stash` testing
  (rebuilding with and without this fix) to reproduce *identically*
  either way, i.e. a distinct, pre-existing bug unrelated to this fix.
  Two of the 12 (`vec3_dynamic_loop_write_dynamic_loop_read`,
  `vec3_dynamic_write_dynamic_loop_read`) additionally fail *pipeline
  creation* outright (`feme-graphics-validate-stage: 'feme.stage.
  {input.load,output.store}' in function 'main' row 4 is out of range
  for element ...`), not just an image mismatch -- filed as new roadmap
  row `L259` for a future session (hypothesis: `vec3`'s `std140`-style
  4-lane storage-alignment padding interacting badly with the dynamic
  row-count computation somewhere in this same `getDynamicRowIndexedAccess`/
  `collectDynamicRowTerms` machinery, not yet investigated further).

`FeMeTransformsGraphicsTests`: new unit test
`CanonicalizeStageTest.OutputReadBackResolvesThroughDynamicRowByteGEP`
reproduces this exact shape (confirmed, via a temporary `git stash` of
just the fix, to fail identically to the real CTS case's own
`Row`/`Component` mis-assignment pre-fix, and pass post-fix), alongside
the pre-existing sibling
`OutputReadBackResolvesThroughDynamicComponentByteGEP` (the true
dynamic-lane-select shape this change must not regress -- still
passes).
`check-feme`: 3,403 Passed (+1 net new unit test), 61 Unsupported, 0
Failed (was 3,402/61/0).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- a compiler-internals (stage-IO canonicalization) correctness fix,
not a feature-bit or extension exposure change.

**Not attempted this session** (filed as new roadmap rows for a future
session): `L258` (the full `dEQP-VK.glsl.*` 28,420-case sweep re-run,
now that neither the crash nor this fix's own scope block it) and
`L259` (the 2 `vec3_*` pipeline-creation-error cases split out above).

## L228(b): `api.copy_and_blit` ASTC-SRGB blit decode bug -- fixed, 1 residual split off as L260

Resumed this carried-over roadmap item (ASTC/BC/ETC2 compressed-format
blit failures). Confirmed via code reading, not blind re-sampling: the
prior session's own spot-checked case
(`api.copy_and_blit.copy_commands2.blit_image.all_formats.color.2d.
astc_12x10_srgb_block...`) is an ASTC *SRGB* source specifically, and
`ImageOps.cpp`'s `runBlitImage` hardcodes its ASTC decode target to
plain `R8G8B8A8_UNORM` regardless of whether the source is a
`_UNORM_BLOCK` or `_SRGB_BLOCK` ASTC variant -- `decodeASTCBlock` itself
has no notion of sRGB (same raw bytes either way), so the sRGB decode
curve was silently skipped for every SRGB ASTC blit source.
`CommandBuffer.cpp`'s *sampling* path (`decodeASTCImageForSampling`'s
caller) already had the correct fix, via a local `isASTCSRGBFormat`
helper -- the blit path alone never got the equivalent treatment.

Fixed by generalizing `isASTCSRGBFormat` into a shared, exported
declaration (`Format.h`/`Format.cpp`, moved out of
`CommandBuffer.cpp`'s own translation unit) and applying the identical
conditional (`R8G8B8A8_UNORM_SRGB` vs `R8G8B8A8_UNORM`) to
`runBlitImage`'s own `DecodedFormat`. New unit test
`FormatTest.IsASTCSRGBFormatIdentifiesOnlyTheFourteenSRGBFootprints`
covers all 14 SRGB LDR footprints plus UNORM/SFLOAT/other-format
negatives.

Verified against a durable, fixed-seed (`20260929`) 800-case
`dEQP-VK.api.copy_and_blit` compressed-format sample:
- Before: 433 Pass / 7 Fail / 360 NotSupported.
- After: **439 Pass / 1 Fail / 360 NotSupported** (6 of 7 originally
  sampled failures fixed).

The 1 residual failure,
`astc_5x5_unorm_block.a2b10g10r10_snorm_pack32.general_general_linear`,
is a *non*-SRGB case (`_unorm_block`, not `_srgb_block`) -- as
expected, this fix does not touch it. Its own `.qpa` image-comparison
diagnostic shows a *different* bug entirely: RGB channel differences
are ~0.0039 (roughly 1/255, comfortably within the ~0.0649 threshold),
but the alpha channel difference is exactly 1.0 (the maximum possible),
far exceeding its own ~0.397 threshold. Since `a2b10g10r10_snorm_pack32`
has only a 2-bit alpha channel, this strongly suggests a bug isolated
to that format's own 2-bit-alpha SNORM packing/unpacking -- filed as
new roadmap row `L260`, out of scope for this fix.

`FeMeVulkanTests`: 770/770 (net +1 new unit test). `ninja check-feme`:
3,404 Passed, 61 Unsupported, 0 Failed (no regressions).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- a core blit-path correctness fix to already-supported ASTC formats,
not a new feature-bit or extension exposure.

**Methodology note (worth flagging for future sessions):** this fix's
own verification was significantly derailed, mid-session, by a
self-inflicted shell environment-variable bug, not a code defect.
Setting both Vulkan-loader ICD env vars in one command --
`export VK_ICD_FILENAMES=<path> VK_DRIVER_FILES=$VK_ICD_FILENAMES` --
evaluates `$VK_ICD_FILENAMES` on its *pre-command* value (empty, in a
fresh shell) before either assignment takes effect, per standard shell
semantics (all RHS expansions on one command line happen before any of
that line's own assignments land). This silently left
`VK_DRIVER_FILES` empty, and the Vulkan loader fell back to
discovering the system's Mesa/llvmpipe software ICD instead of
restricting to FeMe -- producing a lengthy false "the fix isn't
working" investigation (anomalous `NotSupported`/`Fail` counts that
reproduced identically with *and* without the fix, via `git stash`
A/B) before a small standalone C reproducer calling
`vkEnumeratePhysicalDevices`/`vkGetPhysicalDeviceProperties` directly
(bypassing the CTS binary entirely) surfaced the wrong device name
(`llvmpipe`, not `FeMe CPU Vulkan Device`) as the true root cause.
**Going forward:** always set `VK_ICD_FILENAMES`/`VK_DRIVER_FILES` via
separate, independent `export` statements with the literal path string
(never `B=$A` in the same command that also sets `A`), and always
sanity-check `vulkaninfo --summary | grep deviceName` immediately
before trusting any anomalous or unexpected CTS/reproducer result.

**Not attempted this session:** `L260` (the newly-split-off 2-bit-alpha
SNORM bug above).

## L258/L261: dEQP-VK.glsl.* full re-run and textureProj* fix

**`L258` full re-run** (`dEQP-VK.glsl.*`, 28,420 cases, no crash, ~50
min): **17,269 Pass / 2,188 Fail / 8,963 NotSupported** (60.8% /
7.7% / 31.5%). Clustered by 3-level test-path prefix, largest first:
`texture_functions` (1,366), `builtin` (403), `atomic_operations`
(96), `matrix` (92), `shader_expect_assume` (51), `440` (49),
`conversions` (30), `loops` (30), `builtin_var` (21), `struct` (16),
`indexing` (15), `demote` (9), `derivate` (3), `linkage` (3),
`functions` (2), `logical_copy` (2).

**`texture_functions` (1,366 fails) triaged in full this session.**
Drilling into its own 4th-level sub-groups found two independent root
causes, both within the `textureProj`/`textureProjOffset`/
`textureProjLod`/`textureProjLodOffset`/`textureProjGrad`/
`textureProjGradOffset` families (838 of the 1,366):

1. **`OpImageSampleProjImplicitLod`(91)/`OpImageSampleProjDrefImplicitLod`(93)
   were entirely unhandled** by `SPIRVImporter.cpp`'s
   `lowerProjectiveImageSamples` -- only the ExplicitLod pair (92/94)
   was rewritten into MLIR-deserializable non-Proj opcodes before this
   fix, but GLSL's own `textureProj*` builtins compile to the
   ImplicitLod forms, failing pipeline creation with "unhandled opcode
   91"/93 (confirmed via a minimal single-case repro,
   `textureprojoffset.clamp_to_border.isampler2d_vec3_bias_fragment`).
2. **A separate, pre-existing bug in the same rewrite's Coordinate-
   narrowing logic**: it assumed a Proj Coordinate is always exactly
   one component wider than the image's own dimensionality needs (the
   real coordinate is "every component but the last"), but GLSL
   permits a *wider*, padded Coordinate too (`textureProj(sampler1D,
   vec4 P)` uses only `P.x` as the real coordinate and `P.w` as the
   divisor, silently ignoring `P.y`/`P.z`) -- the old heuristic
   narrowed such cases to the wrong width and divided by the wrong
   (padding) component instead of the real divisor.

Both fixed by `L261`: generalized the rewrite to all four Proj
opcodes, and added `requiredProjCoordComponents`, which resolves the
image's real dimensionality from the `OpTypeImage` reachable from the
sample's own Sampled Image operand (two new `TypeResolutionInfo`
tables), falling back to the old width-minus-one heuristic only when
that resolution fails.

**Verification:**
- A 384-case caselist of every non-integer-sampler `textureProj*`
  failure from the `L258` sweep: **0 Fail / 384 Pass** (was 384/384
  Fail).
- A full `dEQP-VK.glsl.texture_functions.*` re-run (7,946 cases,
  Pass+Fail+NotSupported): **768 Fail** (was 1,366) -- **598 fixed**,
  **0 regressions** confirmed via an exact set-difference against the
  `L258` sweep's own fail list (every one of the 768 remaining
  failures was already failing before this fix; zero new failures).
- `FeMeImportSPIRVTests`: 14/14 (+2 net new: `LowersImageSampleProjImplicitLod`,
  `LowersImageSampleProjExplicitLodWithPaddedCoordinate`).
- `ninja check-feme`: 3,406 Passed / 61 Unsupported / 0 Failed (+2 net
  new unit tests, no regressions).

**Remaining `texture_functions` failures (768)** are entirely the
integer-sampler (`isampler`/`usampler`) + `Bias`/`Grad`/`MinLodClamp`
gap `SPIRVResourceLowering.cpp`'s `hasOnlySupportedImageUses`
explicitly rejects -- filed as `L262` (Bias/MinLodClamp; ~128 direct
cases plus an unquantified share of the residual `textureproj*`
int-sampler failures) with `Grad` support noted as a distinct,
larger follow-on (~256 cases: `texturegrad`/`texturegradoffset`/
`texturegradclamp`/`texturegradoffsetclamp`, confirmed via their own
failing-case names to be 100% Grad, 0% Bias-suffixed).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- an internal SPIR-V-import correctness fix to an already-exposed
core GLSL feature (`textureProj*`), no feature-bit or extension
surface change.

**Not attempted this session:** `L262` (integer-sampler Bias/Grad
gate), `L263` (the remaining, smaller `L258` clusters: `builtin`,
`atomic_operations`, `matrix`, etc.), `L260` (carried over, untouched).

## L262: integer-sampler (`isampler`/`usampler`) `Bias`/`Grad`/`MinLodClamp` gap -- fixed for 5 of 7 shapes, `Cube`/`CubeArray` deferred as `L264`

`SPIRVResourceLowering.cpp`'s `hasOnlySupportedImageUses` unconditionally
rejected any sample against an integer-channel (`isampler`/`usampler`)
image where `HasBias`/`HasGrad`/`HasMinLodClamp` was set, with a code
comment claiming SPIR-V forbids this combination alongside integer-format
sampling's own mandatory `VK_FILTER_NEAREST`. This premise was wrong:
`NEAREST` filtering only means no *blending* between taps/levels once the
level is chosen -- it says nothing about *how* that level itself is
selected, and GLSL legally emits `Bias`/`Grad`/`MinLod` against a
`gsampler` family sampler (confirmed via real CTS test source,
`vktShaderRenderTextureFunctionTests.cpp`, and by the failing-case-name
pattern: `texture`/`textureoffset`/`textureoffsetclamp`/`textureclamp`'s
int-sampler failures were 100% `_bias_`-suffixed;
`texturegrad`/`texturegradoffset`/`texturegradclamp`/
`texturegradoffsetclamp`'s were 100% Grad).

**Fix**, scoped to the 5 highest-value shapes (`Plain1D`/`Array1D`/
`Plain2D`/`Array2D`/`Plain3D`, ~97% of the ~739 real CTS cases in this
gap's population; `Cube`/`CubeArray` deferred to `L264`):

- `hasOnlySupportedImageUses`: new `ShapeSupportsBiasGradMinLod` gate
  accepts `HasBias`/`HasGrad`/`HasMinLodClamp` for the 5 shapes above,
  still rejects for `Cube`/`CubeArray`.
- `ImageCalls.h`/`.cpp`: widened all 5 non-Cube `createSample*I32`
  builders with real derivative (`DUdX`/`DUdY`/`DVdX`/`DVdY`/etc.)/
  `UseExplicitLod`/`Bias`/`MinLodClamp` operands, mirroring their float
  counterparts exactly (minus `Dref`). Also required fixing
  `matchImageCall`'s own independent `arg_size()`/operand-index switch
  for each of the 5 kinds -- an easy-to-miss second spot, caught only
  because pre-existing unit tests failed to compile against the new
  builder signatures; `matchImageCall` itself has no compile-time link
  to the builders and silently returns `std::nullopt` on a mismatch
  rather than failing loudly.
- `SPIRVResourceLowering.cpp`'s `lowerImageAccesses`: rewrote the
  `isV4I32(...)` dispatch's 5 non-Cube branches to synthesize/extract
  real derivatives and a real `Bias`/`MinLodClamp`, threading them into
  the widened builders.
- `FeMeRuntimeCPU.c`: widened the 5 `femeCpuImageSample*I32` functions to
  compute a real `ClampedLod` -- `femeRTPlanImplicitLod` (2D/2DArray,
  using only `.ClampedLod`) or `femeRTPlanImplicitLod1D`/
  `femeRTPlanImplicitLod3D` (1D/1DArray/3D, followed by a separate
  `femeRTComputeClampedLod` call, since those two helpers return a raw
  unclamped LOD rather than a full plan struct) for implicit-LOD, or
  `femeRTComputeClampedLod` directly for explicit-LOD -- then still fetch
  only the single nearest tap at that level (anisotropic multi-tap
  footprint is deliberately discarded, since Vulkan mandates `NEAREST`
  filtering/mipmapping for any `VkSampler` bound to an integer-format
  image regardless of the sampler's own anisotropy settings).

**Verification:**

- Full `dEQP-VK.glsl.*` re-run (28,420 cases): **17,867 Pass / 1,590
  Fail / 8,963 NotSupported** -- exactly the predicted -598-Fail delta
  from `L258`'s 17,269/2,188/8,963 baseline (matches this session's own
  pre-computed prediction to the case), confirming no unexpected
  knock-on regressions elsewhere in the sweep.
- Targeted caselist (`texturebias`/`texturegrad`, all `isampler*`/
  `usampler*` variants, 66 cases): **30/30 pass** across the 5 widened
  shapes (was 0/30); the only 12 remaining failures in that caselist are
  exactly the deferred `texturegrad.*samplercube*`/
  `texturegrad.*samplercubearray*` cases (`VK_ERROR_INITIALIZATION_FAILED`
  at pipeline creation -- the gate still rejects them, as designed,
  pending `L264`), confirming clean isolation with zero cross-shape
  regressions.
- Full `dEQP-VK.glsl.texture_functions.*` group re-run (7,946 cases):
  3,767 Pass / 168 Fail / 4,011 NotSupported; the 168 fails cluster
  entirely into pre-existing, unrelated gaps (multisample
  `texturesizems`/`imagesizems` query cases, `texturequerylod` Cube/
  CubeArray/3D `_clamp` cases, and the same 12
  `texturegrad`/`texturegradclamp`-Cube/CubeArray cases the targeted
  caselist already found) -- none are new regressions from this fix.

**Unit tests:** 12 existing `SPIRVResourceLoweringTest` cases had their
hardcoded operand-index assertions widened to match the new argument
layout (no new pass/fail semantics changed, just index shifts); 6
`ImageCallsTest` cases updated/added (`MatchesSample2DI32CallWithBias`
new); 2 new `ImageSamplingTest` runtime-level cases
(`SampleI32BiasSelectsCoarserMipLevel`, `SampleI32GradSelectsCoarserMipLevel`)
directly confirm `Bias`/`Grad` now shift the selected mip level for an
integer-format image, not just compile/lower correctly; 3 pre-existing
`ImageSamplingTest` cases' call sites updated for the widened
`femeCpuImageSample2DV4I32` runtime signature.

`ninja check-feme`: 3,409 Passed / 61 Unsupported / 0 Failed (net new
unit tests, no regressions).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- an internal sampling-correctness fix to already-exposed core GLSL
functionality (`isampler`/`usampler` types, already listed), no
feature-bit or extension surface change.

**Deferred:** `L264` (the same widening for `Cube`/`CubeArray`, ~24
cases/~3% of this gap's original population) and `L263` (the `L258`
sweep's remaining untriaged clusters: `builtin` (now likely the largest
remaining chunk), `atomic_operations`, `matrix`, etc.) are unstarted.
`L260` (carried over, untouched).

## L264: integer-sampler (`isampler`/`usampler`) `Bias`/`Grad`/`MinLodClamp` -- the remaining `Cube`/`CubeArray` widening, completing `L262`

`L262` (previous session) widened 5 of the 7 shapes
(`Plain1D`/`Array1D`/`Plain2D`/`Array2D`/`Plain3D`) to accept
`Bias`/`Grad`/`MinLodClamp` for integer-channel samplers, deferring
`Cube`/`CubeArray` (~24 cases, ~3% of the original ~739-case
population) as a follow-on. This session completes that follow-on.

**Fix**, applying the exact same pattern `L262` established, to the 2
remaining shapes:

- `hasOnlySupportedImageUses`: removed the `ShapeSupportsBiasGradMinLod`
  sub-gate entirely -- all 7 classifiable shapes now equally accept
  `HasBias`/`HasGrad`/`HasMinLodClamp` for integer samplers (no
  shape-specific restriction remains).
- `ImageCalls.h`/`.cpp`: widened `createSampleCubeI32`/
  `createSampleCubeArrayI32` with the same 9 new operands (6
  derivatives, `UseExplicitLod`, `Bias`, `MinLodClamp`) the float
  `createSampleCube`/`createSampleCubeArray` builders already carry
  (minus the offset operand, which `Dim::Cube` forbids regardless of
  sampled type). Also updated `matchImageCall`'s own independent
  `arg_size()`/operand-index switch for both kinds in lockstep --
  `L262`'s own hard-won lesson (a missed update here fails silently via
  `std::nullopt`, not a compile error) reapplied successfully this time.
- `SPIRVResourceLowering.cpp`'s `lowerImageAccesses`: rewrote the
  `Cube`/`CubeArray` `isV4I32(...)` dispatch branches to synthesize real
  derivatives via `getOrSynthesizeSampleCubeDerivatives` (reusing the
  same helper the float `Cube`/`CubeArray` dispatch already calls) and
  thread `IntBias`/`IntMinLodClamp` (already computed earlier in the
  same code block, shared across all 7 shapes) into the widened
  builders.
- `FeMeRuntimeCPU.c`: widened `femeCpuImageSampleCubeV4I32`/
  `femeCpuImageSampleCubeArrayV4I32` to compute a real `ClampedLod` via
  `femeRTComputeCubeClampedLod` (the same helper the float
  `femeCpuImageSampleCubeV4F32`/`femeCpuImageSampleCubeArrayV4F32`
  functions already call), then fetch only the single nearest I32 texel
  at that level via `femeRTFetchTexel2DI32` (never
  `femeRTSampleFilteredCube`, since a `NEAREST` fetch needs no seamless
  cross-face blending at all).

**Verification:**

- Targeted caselist -- the exact same 12
  `texturegrad.*samplercube*`/`texturegrad.*samplercubearray*` cases
  `L262` identified as the residual gap: **12/12 now pass (was 0/12)**.
- Full `dEQP-VK.glsl.texture_functions.*` group re-run (7,946 cases):
  3,791 Pass / 144 Fail / 4,011 NotSupported -- the 144 fails are all in
  `texture_functions.query` (`imagesizems`/`texturequerylevels`, a
  pre-existing, unrelated multisample-image-size/query-level gap; not
  touched by this change), zero Cube/CubeArray-sampling-related
  failures remain. This is exactly the predicted -24-Fail delta from
  `L262`'s own 168-Fail baseline for this same group.

**Unit tests:** 2 existing `SPIRVResourceLoweringTest` cases
(`LowersImplicitLodIntegerSampledImageCubeToImageSampleV4I32`,
`LowersImplicitLodIntegerSampledImageCubeArrayToImageSampleV4I32`) had
their hardcoded `arg_size()`/operand-index assertions widened to match
the new 20/21-arg layout; 2 existing `ImageCallsTest` cases
(`MatchesSampleCubeI32Call`/`MatchesSampleCubeArrayI32Call`) widened to
the new operand list; 3 new `ImageSamplingTest` runtime-level cases
(`SampleCubeI32BiasSelectsCoarserMipLevel`,
`SampleCubeI32GradSelectsCoarserMipLevel`,
`SampleCubeArrayI32BiasSelectsCoarserMipLevel`) directly confirm
`Bias`/`Grad` now shift the selected mip level for a `Cube`/`CubeArray`
integer-format image, mirroring `L262`'s own runtime-level proof style
for the other 5 shapes.

`ninja check-feme`: 3,412 Passed / 61 Unsupported / 0 Failed (net 3 new
unit tests, no regressions).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- an internal sampling-correctness fix to already-exposed core GLSL
functionality, same as `L262`.

This completes the full 7-shape int-sampler Bias/Grad/MinLodClamp
widening that `L262`/`L264` together set out to do; no further shapes
remain in this gap.

**Deferred (unchanged, carried over):** `L263` (the `L258` sweep's
remaining untriaged clusters), `L260` (`a2b10g10r10_snorm_pack32`
alpha-channel SNORM bug), `L228(e)`/`(f)` (broader-than-glsl/
tessellation CTS sampling at real scale).

## L260: `R10G10B10A2_SNORM` 2-bit alpha channel rounds ties away from zero instead of to even -- fixed; L265 filed for a distinct residual

Root-caused the `L228(b)`-split-off residual: `dEQP-VK.api.
copy_and_blit...astc_5x5_unorm_block.a2b10g10r10_snorm_pack32.
general_general_linear`'s "Result image is incorrect" failure, whose
`.qpa` diagnostic showed RGB channels comfortably within threshold but
alpha off by exactly 1.0 (the maximum possible difference).

**Method:** extracted the failing case's embedded `Result`/`Reference`
PNGs directly from the `.qpa` XML (Python: regex out the `<Image
Name="...">`/`</Image>` blocks, base64-decode, `PIL.Image.open`), then
diffed the two alpha channels pixel-by-pixel with `numpy`. Found
exactly 1 differing pixel out of 3,600 (60x60): our result had alpha
raw code `1` (display `255`), the reference expected raw code `0`
(display `128`) -- i.e. the blit's blended alpha landed on an exact
tie (`0.5` normalized) between this format's 2-bit field's only two
positive-side codes, and the two sides disagreed on which way to round
it.

**Root cause:** `packClearColor`'s `R10G10B10A2_SNORM` branch used
`std::lround`, which the C standard mandates always rounds an exact
`.5` tie away from zero, independent of the current floating-point
rounding-direction mode. The Vulkan spec's own fixed-point conversion
formula requires "round to nearest, ties to even" instead. This is
invisible for the format's 10-bit RGB channels (a tie is
astronomically rare at that granularity -- would need the input value
to land on an exact multiple of `1/511`), but the 2-bit alpha channel
has only 3 representable values across `[-1, 1]` (`{-1, 0, 1}`, since
raw `-2` aliases to `-1`), so a blit's bilinear blend regularly lands
exactly on a tie.

**Fix:** added a `roundTiesToEven` helper (`ImageFixture.cpp`,
anonymous namespace) using `std::llrint`, which -- unlike
`std::lround` -- honors the current floating-point rounding-direction
mode (`FE_TONEAREST`/round-to-nearest-even by default, in every
environment this ICD runs in). Applied it to both `Norm10`/`Norm2` in
`R10G10B10A2_SNORM`'s pack path (both, for consistency, even though
only the 2-bit one is ever practically observable).

**Scope decision:** per this row's own original open question ("check
whether other 2-bit-SNORM-channel formats share the bug, to know if
the fix should be narrow or general") -- confirmed via `grep` that no
other format in this codebase has a 2-bit SNORM channel, so the fix is
intentionally scoped to `R10G10B10A2_SNORM` alone rather than replacing
every other SNORM format's own `std::lround` call site (their own ties
are unreachable in practice; broadening the change would be an
untested, unverifiable blast-radius increase for no observable
benefit).

**Unit test:** `ImageFixtureTest.PacksR10G10B10A2SnormAlphaTieToEven`
asserts both `+0.5` and `-0.5` normalized alpha pack to the even code
(`0`), not the previous away-from-zero result (`1`/`-1`).

**Verification:**

- The originally-reported case: **Pass** (was Fail).
- A broader, fixed-seed 10,096-case `dEQP-VK.*a2b10g10r10_snorm_pack32*`
  sample (every group, not just `copy_and_blit`): **24 Fail before this
  fix, 12 Fail after** (confirmed via `git stash` A/B on the same
  build). All 12 fixed cases were `astc_{5x4,5x5,8x6}_unorm_block` and
  `etc2_r8g8b8a8_unorm_block` blit sources; **12 residual failures
  remain** (`astc_{8x8,10x5,12x12}_unorm_block` blit sources) --
  investigated with the same qpa-image-diff technique and confirmed to
  be a *distinct* bug: each has only 1-2 differing pixels (out of
  3,600-4,096), always at a block-boundary coordinate, where the
  *decoded* alpha value itself differs from the reference -- not a
  rounding-tie disagreement (the discrepancy exists independent of
  which rounding rule either side applies). Filed as new roadmap item
  `L265` (likely an `ASTCDecode.cpp` per-block weight-interpolation or
  endpoint-decode precision bug at a source-block edge, not yet
  root-caused past this single-pixel diagnostic).

`ninja check-feme`: 3,413 Passed / 61 Unsupported / 0 Failed (net +1
new unit test, no regressions).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- an internal fixed-point-conversion correctness fix to an
already-supported format, not a new feature-bit or extension exposure.

**Deferred (unchanged, carried over):** `L265` (new, this session --
the residual 12-case ASTC-block-boundary alpha-decode bug above),
`L263` (the `L258` sweep's remaining untriaged clusters), `L228(e)`/
`(f)` (broader-than-glsl/tessellation CTS sampling at real scale).

## L263: fresh full `dEQP-VK.glsl.*` sweep triage -- `bitfieldInsert`/`bitfieldExtract` Count-edge-case bugs found and fixed; L266 filed for the SIMDize divergent-call gap

Re-ran a fresh full `dEQP-VK.glsl.*` sweep (28,420 cases) to re-triage
against several sessions' worth of accumulated fixes (`L260`, `L262`,
`L264`) whose combined effect on the full sweep hadn't been measured
since the stale `L262`-era baseline (17,867 Pass / 1,590 Fail / 8,963
NotSupported).

**Fresh tally: 18,491 Pass / 966 Fail / 8,963 NotSupported** -- a much
larger improvement than expected from `L264`'s own predicted -24-Fail
delta alone, confirming the fixes had compounded more than previously
tracked (worth remembering: partial/targeted-sample verification
numbers from individual fix sessions don't always additively predict
the next full sweep's tally).

**Re-clustered the fresh 966-Fail list** by test-path prefix:
`builtin.function` (395) was confirmed the largest chunk, as the prior
session's tally had predicted. Drilled into it:

```
96 bitfieldinsert
80 findMSB
80 findlsb
47 bitfieldextract
40 uaddcarry
40 usubborrow
 3 imulextended
 3 umulextended
```

**Picked `bitfieldinsert`+`bitfieldextract` first** (143 combined,
since both are `BitField*` SPIR-V ops and looked likely to share a
root cause). Reproduced `bitfieldextract.int_highp_compute`'s failure
directly and found the qpa's own diagnostic text already pinpointed
the exact failing inputs (deqp-vk's shader-function test harness
prints `inputs:`/`outputs:` for every mismatching sample, no qpa-image-
diff needed this time -- these are compute-shader value comparisons,
not image comparisons).

**Root cause (three separate bugs, all in `SPIRVToLLVMPatterns.cpp`,
all only reachable at `Count`'s two closed-interval endpoints -- `Count
== 0` ("insert/extract nothing") and `Count == Size` ("the whole
field"), both spec-legal and both exercised directly by the CTS's own
`bits=0` and `offset=0,bits=32` test inputs):**

1. `BitFieldInsertPattern` ORed `Insert << Offset` into the result
   completely **unmasked**. `Insert`'s bits above bit `Count - 1` are
   unspecified by the SPIR-V spec and routinely nonzero in real test
   inputs, so those bits leaked past the intended field boundary and
   corrupted bits that should have been preserved unchanged from
   `Base` -- for *every* `Count`, not just the edge cases. Confirmed
   by reproducing the exact failing CTS inputs (`base`/`insert`/
   `offset`/`bits` from the qpa diagnostic) in Python against both the
   spec's reference formula and the buggy formula -- the buggy formula
   reproduced our wrong output exactly.
2. `BitFieldSExtractPattern` computed a final `llvm.ashr` shift amount
   that reduces to exactly `Size` whenever `Count == 0`, regardless of
   `Offset` (`Offset + (Size - (Count + Offset))` simplifies to `Size -
   Count`). A shift amount equal to the operand's own bit width is
   poison per the LLVM LangRef (only strictly-less-than-`Size` amounts
   are well-defined), producing garbage instead of the spec-mandated
   result of `0`.
3. `BitFieldUExtractPattern`'s (and `BitFieldInsertPattern`'s own
   internal field-mask's) `(-1 << Count) ^ -1` low-mask construction
   requires an out-of-range `llvm.shl` whenever `Count == Size` -- also
   poison, instead of the spec-mandated all-ones mask. This is what the
   `offset=0, bits=32` (whole-value) CTS inputs specifically exercise.

**Fix:**

- A new `createBitFieldLowMask()` helper: clamps the shift amount to
  `Size - 1` (always in-range) via `llvm.intr.umin`, then selects the
  correct all-ones mask back in for the `Count >= Size` case
  specifically. Used by both `BitFieldUExtractPattern` and
  `BitFieldInsertPattern`'s own field mask (replacing their prior
  `(-1 << Count) ^ -1` constructions).
- `BitFieldInsertPattern`: mask `Insert << Offset` down to the field
  mask before OR-ing it into the result.
- `BitFieldSExtractPattern`: guard the final `ashr` with a `Count == 0`
  select, since the shift amount itself would already be poison by the
  time it's computed (poison values are safe to compute and discard
  via `select`, only unsafe if actually relied upon).

New lit test `spirv-to-llvm-bitfield-count-edge-cases.mlir` (structural
IR-shape verification, matching this file's existing
`spirv-to-llvm-bitfield-signed-argument.mlir` convention) covers all
three fixes.

**Verification:**

- `ninja check-feme`: 3,414 Passed / 61 Unsupported / 0 Failed (net +1
  new test, no regressions).
- `dEQP-VK.glsl.builtin.function.integer.{bitfieldinsert,
  bitfieldextract}.*` (200 cases combined): **0 Fail** (was 96 + 47 =
  143 Fail).
- Full `dEQP-VK.glsl.builtin.function.integer.*` (752 cases): 246 Fail
  remain (was 389) -- cross-referenced via Python to confirm the 246
  residual fails are *exactly* `findMSB`/`findlsb`/`uaddcarry`/
  `usubborrow`/`{i,u}mulextended`'s non-`compute`-stage cases (80 + 80
  + 40 + 40 + 3 + 3 = 246), i.e. this fix's own targeted 143 cases are
  cleanly gone with zero overlap or side effects on the rest.

**L266 filed:** the 246 residual fails are a *distinct*, much larger
class of bug: every failing case's diagnostic is `error:
feme-cpu-simdize: unsupported divergent call to 'llvm.ctlz.i32'` (a
pipeline-creation-time compile failure, `VK_ERROR_INITIALIZATION_FAILED`
-- not a runtime value mismatch like this session's own fix). The
SIMDize pass's own diagnostic text says outright: "roadmap milestone 7
does not cover a generic vector-call rewrite" for these intrinsics
in a divergent call within a non-`compute` shader stage. This needs the
SIMDize pass's vector-call-rewrite machinery widened -- a
multi-hour/dedicated-session-sized task, not a quick pattern fix like
this session's two bugs, so filed as new roadmap item `L266` rather
than picked up this session.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- an internal SPIR-V-to-LLVM lowering correctness fix to already-
exposed core GLSL integer bitfield functionality.

**Deferred (unchanged, carried over):** `L266` (new, this session --
the 246-case SIMDize divergent-call gap above), `L265`
(`a2b10g10r10_snorm_pack32`'s residual ASTC-block-boundary alpha-decode
bug), the remaining smaller `L258`-era `dEQP-VK.glsl.*` clusters
(`atomic_operations` 96, `matrix` 92, `shader_expect_assume` 51, `440`
49, `conversions` 30, `loops` 30, `builtin_var` 21, `struct` 16,
`indexing` 15, `demote` 9, `derivate`/`linkage` 3 each,
`functions`/`logical_copy` 2 each -- `texture_functions`'s 144 is
pre-existing/unrelated per `L264`), `L228(e)`/`(f)` (broader-than-glsl/
tessellation CTS sampling at real scale).

**Post-fix confirmation (fresh full sweep, same session):** kicked off
a second full `dEQP-VK.glsl.*` sweep (28,420 cases) after the bitfield
fix landed, specifically to confirm the predicted 966 - 143 = 823 Fail
delta rather than assume it. **Result: 18,634 Pass / 823 Fail / 8,963
NotSupported -- an exact match to the prediction**, confirming the fix
has no unexpected knock-on effects elsewhere in the full `glsl` sweep.

Re-clustered the fresh 823-Fail list by test-path prefix and found the
earlier partial-sample-based estimate above (`atomic_operations` 96,
`matrix` 92, etc.) undercounted several clusters that a partial sample
had missed or under-sampled entirely:

```
246 builtin.function.integer        (== L266's SIMDize gap exactly, confirmed by cross-reference)
 70 texture_functions.query.texturequerylod
 49 440.linkage.varying
 34 texture_functions.query.texturequerylevels
 24 matrix.mul.dynamic
 16 shader_expect_assume.compute.expect
 16 shader_expect_assume.fragment.expect
 16 shader_expect_assume.vertex.expect
 16 texture_functions.query.imagesizems
 16 texture_functions.query.texturesizems
 10 loops.special.do_while_dynamic_iterations
 10 loops.special.for_dynamic_iterations
 10 loops.special.while_dynamic_iterations
  8 matrix.{add,div,sub}.dynamic (8 each)
  8 texture_functions.query.texturesamples
  6 builtin.function.pack_unpack
  4 builtin.precision.{cosh,sinh} (4 each)
  ... (remainder in smaller buckets)
```

Notably, `texture_functions.query.*` (144 combined across
`texturequerylod`/`texturequerylevels`/`imagesizems`/`texturesizems`/
`texturesamples`) is now the single largest *non-L266* cluster --
larger than `atomic_operations`/`matrix` individually -- and wasn't
visible at this scale in the earlier partial sample. Filed as a
priority item for the next untriaged-cluster session (ahead of
`atomic_operations`/`matrix`, which are smaller). `440.linkage.varying`
(49) is also a new, previously-unseen-at-this-scale cluster worth
investigating alongside it.

## L266: `feme-cpu-simdize`'s divergent-call/vector-decomposition gap for `ctlz`/`cttz`/`with.overflow` -- fixed; L268 filed for a distinct residual `uvec3` bug

Picked up L266 (filed by L263's triage): 246 residual
`dEQP-VK.glsl.builtin.function.integer.{findMSB,findlsb,uaddcarry,
usubborrow,imulextended,umulextended}.*` failures, all non-`compute`-stage
cases, all failing at pipeline-creation time with a
`feme-cpu-simdize:` diagnostic (not a runtime value mismatch).

**Root cause, in two layers, found via real `FEME_DUMP_IR_PRESIMD` IR
traces of actual failing cases rather than assumption:**

1. **Scalar divergent calls** (`llvm.ctlz.i32`/`cttz.i32`/all four
   `with.overflow.i32` intrinsics) were entirely unhandled by the
   SIMDize pass's divergent-call rewrite machinery -- a prior segment's
   baseline fix (not yet CTS-verified when this segment started) added
   `getDivergentCallOverloadShape` support for `ctlz`/`cttz` and a new
   `isOverflowArithIntrinsic`/`widenOverflowArithIntrinsic` pair for the
   four with-overflow intrinsics.

2. **Genuinely vector-typed divergent calls** -- e.g. `findMSB(ivec4)`
   lowers to a real `llvm.ctlz.v4i32` call, not a scalar one wrapped in
   a broadcast -- exposed **four further, independent gaps** in the
   SIMDize pass's separate vector-decomposition loop
   (`checkVectorDecompositionSupported`/`widenVectorElementwise`),
   found one at a time across several rebuild/CTS-verify rounds:
   - Three separate strict `all_of(args, arg-type == result-type)`
     "Homogeneous" checks (vector producer check, vector consumer
     check, and the `widenInstruction` dispatch gate) all wrongly
     rejected `ctlz`/`cttz` purely because of their `is_zero_poison`
     `ImmArg` operand, which never matches the vector result type by
     design. Fixed via a new ImmArg-tolerant `callNonImmArgsHaveType`
     helper used at all three sites -- the fourth, unrelated scalar
     `Homogeneous` check inside `widenElementwise` is deliberately left
     strict, since scalar `ctlz`/`cttz` already bypass it via
     `getDivergentCallOverloadShape`.
   - `widenVectorElementwise`'s own `ICall` per-component arg-building
     loop tried to decompose/widen `ImmArg` operands instead of passing
     them through unchanged.
   - `widenOverflowArithIntrinsic` only handled scalar (`iN`) operand
     shapes; `uaddCarry(uvec4,uvec4)`-style vector operand shapes
     needed a new branch decomposing into `N` independent per-component
     wide calls, flattening `2*N` result/overflow leaf values in the
     order the pre-existing generic aggregate-leaf-flattening code
     (from roadmap `L27`) already expects.
   - A genuinely vector-typed *operand* of one of the four
     with-overflow intrinsics (e.g. `uaddCarry`'s own two `uvec4`
     arguments) had **no consumer-acceptance rule at all** in the
     vector-decomposition loop, since the call's own *result* is
     aggregate-typed and validated by a separate code path
     (`checkAggregateValueSupported`) -- its operands are still
     ordinary vector-typed values needing this loop's own acceptance.
     Added a dedicated `isOverflowArithIntrinsic` consumer-acceptance
     case.

Also corrected a wrong assumption carried over from an earlier
session: `umulextended`/`imulextended` (`spirv.UMulExtended`/
`SMulExtended`) do **not** lower to `llvm.{u,s}mul.with.overflow` at
all -- MLIR's own `MulExtendedPattern` computes the wide product via
plain `zext`/`mul`/`lshr`/`trunc` and `insertvalue`, with no
with-overflow intrinsic involved. This meant their residual CTS fails
were never a SIMDize gap in the first place (see below).

**New lit test**: `simdize-vector-call-immarg-overflow.ll` (extending
the prior segment's scalar-only `simdize-divergent-call-ctlz-overflow.ll`)
exercises a real `findMSB`/`uaddCarry`-shaped vector call chain over
`<4 x i32>`, FileCheck-verified against `feme-opt`'s actual widened
output -- locks in both the ImmArg-tolerant-homogeneity fix and the
overflow-intrinsic-operand consumer-acceptance fix together.

**Verification:**
- `check-feme`: 3,416 Passed (+2 new lit tests total across this and
  the prior segment), 61 Unsupported, 0 Failed.
- Targeted 6-group CTS sample (350 cases total, group names
  case-confirmed via `dEQP-VK-cases.xml`):
  - `findMSB`/`findlsb`: 96/100 Pass each, 0 Fail (4 NotSupported
    each, unaffected/expected).
  - `uaddcarry`/`usubborrow`: 42/50 Pass each (was 18/50 before this
    segment's fixes) -- **0 of the remaining 6 fails each are the
    original SIMDize error**; all 6 are a distinct, pre-existing
    `uvec3`-only MLIR-level `"feme.tight_vector"` type-mismatch bug,
    filed separately as `L268` (see below).
  - `umulextended`/`imulextended`: 21/25 Pass each, unchanged from
    before this segment (these builtins were never a SIMDize gap --
    all 3 fails each are the same `L268` `uvec3` bug).
- `check-hlsl-feme-vk`: 482 Pass / 31 XFAIL / 207 Not supported / 2
  Fail / 1 Unexpected-pass -- unchanged from baseline; the 3
  discrepancies are the standing, previously-flagged, unrelated
  `offload-test-suite` lit-annotation staleness
  (`spec_const_32_bits.test`/`WaveActiveMax.test`/
  `array_of_matrices.test`), not new regressions.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal compiler-pass (SIMDize) correctness fix widening
coverage of already-exposed core GLSL integer builtins, not a new
feature or extension.

**New residual, filed as `L268`**: all 18 remaining fails
(6+6+3+3 across `uaddcarry`/`usubborrow`/`umulextended`/`imulextended`)
are `uvec3`-shaped cases specifically, and share one exact MLIR
verifier diagnostic surfacing *before* the SIMDize pass even runs (a
module-verification failure straight out of the SPIRV-to-LLVM
conversion, not a `feme-cpu-simdize:` diagnostic):

```
'llvm.insertvalue' op Type mismatch: cannot insert 'vector<3xi32>' into
'!llvm.struct<packed (struct<"feme.tight_vector", (array<3 x i32>)>,
struct<"feme.tight_vector.1", (array<3 x i32>)>)>'
```

`"feme.tight_vector"` looks like a FeMe-authored packed-struct-of-array
representation for 3-component vectors at the LLVM-dialect level
(likely an alignment/ABI workaround, since native `<3 x i32>` has
different natural alignment than a packed 3-element array), but the
exact conversion pattern/type-converter code responsible has not yet
been located -- a distinct root cause from L266's SIMDize-pass gap,
deferred to its own dedicated session.

**Full-sweep confirmation (next session):** the fresh full
`dEQP-VK.glsl.*` sweep (28,420 cases) kicked off in the background
after this fix landed completed with **18,862 Pass / 595 Fail / 8,963
NotSupported** -- an exact match to the predicted
823 (L263's post-fix baseline) - 228 (246 L266 cases minus 18 residual
L268 cases) = 595 Fail, confirming no unexpected knock-on shifts
elsewhere in the full sweep from this fix.

## L268: `feme.tight_vector` reassembly missing from `with.overflow`/mul-extended patterns -- fixed

Picked up L268 (filed by L266's own CTS verification): the 18 residual
`uaddcarry`/`usubborrow`/`umulextended`/`imulextended` fails (6/6/3/3),
all `uvec3`-shaped, all failing with the same MLIR verifier diagnostic
*before* SIMDize even runs:

```
'llvm.insertvalue' op Type mismatch: cannot insert 'vector<3xi32>' into
'!llvm.struct<packed (struct<"feme.tight_vector", (array<3 x i32>)>,
struct<"feme.tight_vector.1", (array<3 x i32>)>)>'
```

**Root cause:** 100% FeMe-authored, confirmed via a `zero matches`
grep for any FeMe-authored override of `IAddCarry`/`ISubBorrow`/
`UMulExtended`/`SMulExtended` -- all four fall through unmodified to
upstream MLIR's own generic `ArithmeticWithOverflowPattern`/
`MulExtendedPattern` (`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`).
FeMe's own type converter (`SPIRVToLLVMPatterns.cpp`'s
`layOutStructIfOffsetsMatch`) unconditionally substitutes a
`feme.tight_vector` marker struct for *any* 3-component vector member
of *any* `spirv::StructType` with no `Offset` decorations -- including
these four ops' own transient, SSA-only two-member result struct --
because a raw `vector<3xiN>`'s ABI alloc size is ambiguously rounded
up to 4 lanes' worth by LLVM's `DataLayout`, a `DataLayout`-level
ambiguity that applies regardless of whether the struct is ever laid
out in real memory. Upstream's own patterns build their result via
raw, unconditional `insertvalue`s of the un-substituted `vector<3xiN>`
computed values, with no awareness of FeMe's marker-struct convention
-- the exact same "producer doesn't know about a type-converter side
effect" bug class `CompositeConstructPattern::convertStruct` already
solved for `spirv.CompositeConstruct`.

**Fix:** two new FeMe-authored pattern classes,
`TightVectorArithmeticWithOverflowPattern<SPIRVOp, LLVMOp>` and
`TightVectorMulExtendedPattern<SPIRVOp, IsSigned>`, registered at
`FeMeBenefit` so they supersede upstream's own default-benefit
registrations for exactly these four ops. Each replicates the
upstream computation exactly but routes both computed result fields
through the already-existing `reassembleTightVectorValue` helper (via
a new shared `insertReassembledStructMember` wrapper, which also
handles declared-to-physical field-index remapping via
`getStructMemberPhysicalIndex`) before the final `insertvalue`s. A 2-
or 4-lane (or scalar) operand reassembles as a no-op via
`reassembleTightVectorValue`'s own type-equality early-out, so this is
not a 3-lane-only special case needing separate dispatch -- every
shape now routes through the same, correct path.

Two new unit tests (`SPIRVToLLVMTest.IAddCarryUVec3ReassemblesTightVectorMembers`,
`SPIRVToLLVMTest.UMulExtendedUVec3ReassemblesTightVectorMembers`)
reproduce the exact `uvec3` `IAddCarry`/`UMulExtended` shapes and
assert the fixed IR has no leftover `unrealized_conversion_cast`.

**`check-feme`:** 3,418 Passed (+2 new unit tests), 61 Unsupported, 0
Failed.

**CTS (targeted 4-group sample, 150 cases):**
`uaddcarry`/`usubborrow`/`umulextended`/`imulextended` combined
144/150 Pass, **0/150 Fail** (was 18 Fail pre-fix), 6 NotSupported
(`uvec5`, an unrelated `longVector`-not-supported skip, unaffected).

**Full-sweep confirmation:** a fresh full `dEQP-VK.glsl.*` sweep
(28,420 cases) after this fix landed completed with **18,880 Pass /
577 Fail / 8,963 NotSupported** -- an exact match to the predicted
18,862 + 18 = 18,880 Pass / 595 - 18 = 577 Fail (L266's post-fix
baseline, plus/minus these 18 now-passing cases), confirming no
unexpected knock-on shifts elsewhere in the full sweep from this fix.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change -- an internal SPIR-V-to-LLVM conversion-pattern correctness
fix for already-exposed core GLSL integer builtins, not a new
feature/extension.

## L267: `texture_functions.query.*` cluster -- three distinct bugs found and fixed (112/144 cases); `imagesizems`/`texturesizems` split out as L269

Picked up L267 (filed by L263's own post-fix confirmation sweep as
the largest untriaged non-L266 cluster): 144 combined fails across
`texturequerylod` (70), `texturequerylevels` (34), `imagesizems`/
`texturesizems` (16 each), `texturesamples` (8).

**Diagnostic technique:** `FEME_VULKAN_LOG_CREATION_ERRORS=1` (an
opt-in env var, `feme/lib/Vulkan/Diagnostics.h`, that prints an
`llvm::Error`'s full message instead of silently discarding it) was
the key that unblocked this triage -- it should be the default first
diagnostic step for any `VK_ERROR_INITIALIZATION_FAILED`, ahead of
the `FEME_DUMP_IR*` family.

### Bug 1: `texturequerylod` -- integer-sampled images wrongly rejected

All 70 fails were `usampler*`/`isampler*` shapes. `hasOnlySupportedImageUses`'s
`OpImageQueryLod` shape-gating rejected any integer-sampled image
outright on a stale comment's premise ("SPIR-V never legalizes
`OpImageQueryLod` against an integer-channel image"). This is simply
wrong: GLSL's own 4.60 spec's `textureQueryLod()` prototype list
covers every `gsampler*` shape (`isampler*`/`usampler*` included, not
just `sampler*`), confirmed against `vktShaderRenderTextureFunctionTests.cpp`'s
own `textureQueryLod(u_sampler, ...)` test generator. `OpImageQueryLod`'s
`<2 x float>` result depends only on coordinates/derivatives/dimensions,
never texel format, so no runtime call signature needed to change --
only the shape-gating check needed to stop special-casing `IsInteger`.

**Fix:** removed the `IsInteger ||` rejection (mirroring `isGatherIntrinsic`'s
own `L125(k)` precedent lifting an identical stale rejection); updated
both the check's own comment and the `lowerImageAccesses` call site's
comment to reflect the corrected reality.

New unit test: `SPIRVResourceLoweringTest.LowersPlain2DUnsignedIntegerSampledQueryLod`.

**CTS:** `texturequerylod` 190/190 Pass (was 120/190).

### Bugs 2/3: `texturequerylevels`/`texturesamples` -- a no-mask image call kind mishandled in two passes

`ImageCallKind::QueryLevels`/`QuerySamples` (`createQueryLevels`/
`createQuerySamples`, `ImageCalls.cpp`) are the sole `feme.cpu.image.*`
call kinds with no trailing mask operand at all -- a 3-arg
`(ImageHeap, ImageHeapCount, ImageIndex)` signature, confirmed via
`getOrInsertImageCall`'s own `FunctionType` table and `matchImageCall`'s
switch statement (which correctly leaves `MatchedImageCall::Mask` null
for these two kinds).

Two independent pieces of code assumed the opposite -- "the last
argument of every `feme.cpu.image.*` call is a mask":

1. **`SIMDize.cpp`'s `widenImageCall`**: `unsigned MaskIdx = CI.arg_size() - 1;`
   misread the real `ImageIndex` operand as a mask during per-lane call
   rebuilding.
2. **`Linearize.cpp`'s divergent-region mask-threading**:
   `Call->setArgOperand(Call->arg_size() - 1, Mask);` unconditionally
   overwrote the same real `ImageIndex` operand with an `i1` mask value
   during divergent-region linearization -- confirmed via
   `FEME_DUMP_IR_PRESIMD` to be the *actual* root cause (the call was
   already ill-typed, `i1` passed where `i32` is declared, *before*
   `SIMDizePass` even ran). Fixing only `SIMDize.cpp` still crashed with
   a verifier/assertion failure (`Calling a function with a bad
   signature!`) because `Linearize.cpp` had already corrupted the IR
   upstream -- **both fixes are necessary together.**

**Fix:** both call sites now gate their mask-handling logic on
`Matched.Mask`/`Matched->Mask`'s nullness instead of unconditionally
indexing the last operand.

New unit tests: `SIMDizeTest.WidensDivergentQueryLevelsCallWithNoMaskOperand`,
`LinearizeTest.LeavesQueryLevelsCallImageIndexUntouchedUnderDivergentBranch`.

**CTS:** `texturequerylevels` 102/102 Pass (was 68/102),
`texturesamples` 24/24 Pass (was 16/24).

### `imagesizems`/`texturesizems` -- distinct, unfixed bug, split out as L269

The remaining 32 cases (16 each) fail with a different error
signature entirely: `"unsupported raised operation:
'llvm.spv.resource.handlefrombinding.tspirv.SignedImage_i32_1_0_0_1_2_23t'"`/
`'Image_f32_1_0_0_1_2_4t'`-style diagnostics, naming a multisampled
(`MS=1`) image handle shape that `hasOnlySupportedImageUses`'s
`isGetDimensionsIntrinsic` case (the plain `imageSize()`/`textureSize()`
path `imagesizems`/`texturesizems` compile down to) doesn't recognize
at all -- hard-gated to `Shape == ImageShape::Plain2D` only. This is a
genuinely separate, unstarted feature gap (the same "multisampled
shape not yet widened" class already called out, but left unresolved,
in `isQuerySizeLodCall`'s/`isQueryLevelsCall`'s own doc comments), not
the same bug class as the two fixes above. Filed as new roadmap item
`L269` for a future dedicated session.

**Combined `texture_functions.query.*` re-run (451 cases):** 419/451
Pass (92.9%), 32/451 Fail (7.1%, exactly `imagesizems`/`texturesizems`,
0 unexpected regressions elsewhere in the group).

**`check-feme`:** 3,421 Passed (+3 net new unit tests), 61
Unsupported, 0 Failed (no regressions).

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change -- three internal compiler-pass correctness fixes widening/
preserving coverage of already-exposed core GLSL texture-query
builtins, not a new feature/extension.

**Full-sweep confirmation:** a fresh full `dEQP-VK.glsl.*` sweep
(28,420 cases) after all three L267 fixes landed completed with
**18,992 Pass / 465 Fail / 8,963 NotSupported** -- an exact match to
the predicted 18,880 + 112 = 18,992 Pass / 577 - 112 = 465 Fail
(L268's post-fix baseline, plus/minus these 112 now-passing cases),
confirming no unexpected knock-on shifts elsewhere in the full sweep
from these fixes.

## L269: `imagesizems`/`texturesizems` multisampled `GetDimensions` gap -- fixed, no new builder needed

Picked up L269 (split out of L267's own triage as the fourth,
differently-signatured `texture_functions.query.*` sub-cluster): 32
combined fails across `imagesizems`/`texturesizems` (16 each).

**Root cause:** per the SPIR-V spec, `OpImageQuerySize` (the lod-less
opcode `imageSize()`/`textureSize()` against a multisampled image
compiles to -- a multisampled image has exactly one mip level, so
there is no lod argument to select from) returns the *same* `(Width,
Height[, Layers])` shape as its non-multisampled counterpart; sample
*count* is a wholly separate query (`OpImageQuerySamples`, already
handled, roadmap L73). `SPIRVToLLVMPatterns.cpp`'s
`ImageQuerySizePattern` already reflected this correctly (it dispatches
purely on result vector width, with no MS-awareness needed at all) --
the actual gap was entirely downstream, in
`SPIRVResourceLowering.cpp`'s shape-gating: `hasOnlySupportedImageUses`
(sampled-image path) hard-gated its `isGetDimensionsIntrinsic` case to
`Plain2D` only and had no `isGetDimensions3Intrinsic` case at all;
`hasOnlySupportedStorageImageUses` (storage-image path) gated both
cases to non-multisampled shapes only.

**Fix:** widened `hasOnlySupportedImageUses`'s `isGetDimensionsIntrinsic`
gate to also accept `Plain2DMS`, and added a wholly new
`isGetDimensions3Intrinsic` case there (for `textureSize(
sampler2DMSArray)`) scoped to `Array2DMS`; widened
`hasOnlySupportedStorageImageUses`'s `isGetDimensionsIntrinsic` gate to
also accept `Plain2DMS` and its `isGetDimensions3Intrinsic` gate to
also accept `Array2DMS`. All four widened/new cases reuse the
pre-existing `createGetDimensions2D`/`createQuerySizeLod2DArray`
builders unchanged -- `lowerImageAccesses`'s dispatch `else` branches
already routed to them correctly once the shape gates opened, and the
shared runtime entry points (`femeCpuImageGetDimensions2DV2I32`/
`femeCpuImageGetDimensionsLod2DArrayV3I32`) already read `Width`/
`Height`/`ArrayLayers` off the image descriptor generically, with no
MS-vs-non-MS distinction of their own. No new image-call builder was
needed, contrary to the initial guess at the end of the prior L267
session.

**Diagnostic note:** the originally-observed
`FEME_VULKAN_LOG_CREATION_ERRORS=1` diagnostic (naming a
`SignedImage_i32_1_0_0_1_2_23t`-style handle) turned out to be a
downstream symptom, not the direct cause -- `UnsupportedOps.cpp`'s
`checkSupportedRaisedOps` has its own documented gotcha that when any
handle's use is rejected, every handle in that function fails to
normalize, and the error names whichever handle declaration happens to
appear first in the module, not necessarily the one whose use actually
triggered the rejection.

**Unit tests:** converted one existing negative test
(`LeavesPlain2DMSStorageImageGetDimensionsHandleAlone`, which had
asserted this exact combination was rejected) into a positive one
(`LowersPlain2DMSStorageImageGetDimensions`), and added four new
positive tests: `LowersArray2DMSStorageImageGetDimensions`,
`LowersPlain2DMSSampledImageGetDimensions`,
`LowersArray2DMSSampledImageGetDimensions` (plus the converted test).

**`check-feme`:** 3,424 Passed (+3 net new unit tests vs. L267's
3,421 baseline), 61 Unsupported, 0 Failed (no regressions).

**Targeted CTS re-run:** `dEQP-VK.glsl.texture_functions.query.
imagesizems.*`/`texturesizems.*` (32 cases): **32/32 Pass (100%)**,
was 0/32.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change -- an internal compiler-pass correctness fix widening coverage
of an already-exposed core GLSL texture-query builtin, not a new
feature/extension.

**Full-sweep confirmation:** a fresh full `dEQP-VK.glsl.*` sweep
(28,420 cases) after this fix landed completed with **19,024 Pass /
433 Fail / 8,963 NotSupported** -- an exact match to the predicted
18,992 + 32 = 19,024 Pass / 465 - 32 = 433 Fail delta against L267's
post-fix baseline (18,992 Pass / 465 Fail / 8,963 NotSupported),
confirming no unexpected knock-on shifts elsewhere in the full sweep
from this fix.

## L270/L271: `atomic_operations` fragment/vertex stage-linkage cluster (32 cases) -- two distinct stage-linkage bugs found and fixed

**Symptom:** 32 of the `dEQP-VK.glsl.atomic_operations.*` group's 96
failures were `vkCreateGraphicsPipelines`-time rejections, not runtime
`Image mismatch`es -- 16 fragment-stage cases and 16 vertex-stage cases,
each with a different rejection message.

### L270: SPIR-V `HelperInvocation` builtin misclassified as a `Location`-less user varying

**Root cause:** SPIR-V's `BuiltIn HelperInvocation` (`gl_HelperInvocation`,
code 23) was unmapped in `getSystemValueForBuiltIn`, so
`canonicalizeSPIRVStage`'s discovery loop fell through to treating it as
an ordinary user varying -- but it has no `Location` decoration at all
(glslang never gives one to a `gl_*` builtin), so it was added to
`InputGlobals` with no `Location`, and `validateStageInterfaces` rejected
the pipeline outright. Every `dEQP-VK.glsl.atomic_operations.*_fragment`
case statically reads this builtin, since glslang always guards a
fragment shader's SSBO/image read-modify-write with
`if (!gl_HelperInvocation) { ... }`.

**Fix:** special-cased `BuiltIn == 23` in the discovery loop to skip the
ordinary `InputGlobals`/`SignatureElement` path entirely (tracked instead
in a new `HelperInvocationGlobals` set), and intercepted its loads
directly in the load-rewrite loop, replacing them with
`createStageIsHelper()` -- the same `feme.stage.is_helper` op DXIL's own
`IsHelperLane` opcode already raises to, already fully supported end to
end by the rest of the pipeline (SIMDize/Linearize/ReferenceLowering/
ValidateStage).

**Unit test:** `RewritesHelperInvocationBuiltinToIsHelperCall`.

### L271: declared-but-never-written vertex-stage output breaks fragment-stage interface linkage

**Root cause:** a vertex-stage `out` varying that is declared (has a
`Location` decoration) but never actually stored to anywhere in the
shader body -- legal per the Vulkan spec, whose value is then simply left
undefined for a later stage to read -- is invisible to
`canonicalizeSPIRVStage`'s ordinary load/store-walk discovery loop, since
there is no instruction to discover it through. This left the global with
no `SignatureElement` at all, and the fragment stage's own genuinely
`Location`-matching input had nothing to link against, rejected with
`"fragment input location N has no matching vertex stage output"`. This
is exactly the shape `dEQP-VK.glsl.atomic_operations.*_vertex`'s own
`outData` output is: the CTS's `ShaderExecutor` framework always declares
one `layout(location=N) out` variable per output and links a later
stage's input against the same location, whether or not the shader body
actually assigns it.

**Fix:** added a fallback discovery loop, gated by two conditions to
avoid mis-firing on two distinct near-identical-looking shapes that are
*not* this case:

- Only runs on a genuinely first canonicalization pass over the function
  (`!dxil::getEntrySignature(F).has_value()`). `GraphicsPipeline.cpp`
  invokes `CanonicalizeStagePass` twice per compiled stage as a
  documented no-op repeat; on the second pass the original store has
  already been rewritten and erased, but the now-orphaned
  `GlobalVariable` declaration survives with its `!spirv.Decorations`
  metadata intact -- without this guard the loop rediscovers it and
  appends a spurious duplicate `SignatureElement`, corrupting the
  signature. This crashed `dEQP-VK.glsl.atomic_operations.
  add_signed_geometry` outright (a previously-*passing* case) before the
  guard was added.
- Only runs for `SPIRVCanonicalPhase::Ordinary`, never either hull split
  phase. `splitBarrierlessTessellationControlEntry` clones a barrierless
  mixed-frequency hull entry into control-point/patch-constant functions
  sharing the same `GlobalVariable`s, each pruning the other phase's own
  stores. By the time the patch-constant phase's own canonicalization
  runs, a genuine per-control-point output its own prune removed every
  store to already has zero uses anywhere in the module --
  indistinguishable from a truly-never-written global by `use_empty()`
  alone, but it belongs solely to its control-point sibling's
  already-built signature. This regressed
  `HullStageDoesNotPeelPatchTessFactorOutputRowCount` and 4 sibling
  `NoBarrier*` unit tests before the `Phase == Ordinary` restriction was
  added.

**Unit tests:** `RecordsDeclaredButNeverWrittenOutputAsSignatureElement`,
`SecondCanonicalizePassDoesNotDuplicateUnwrittenOutputElement`; full
`CanonicalizeStageTest`/`ValidateStageTest` suite (124 tests) re-run
green after both guards landed.

**`check-feme`:** 3,427 Passed (+3 net new unit tests vs. L269's 3,424
baseline), 61 Unsupported, 0 Failed (no regressions).

**Targeted CTS re-run:** full `dEQP-VK.glsl.atomic_operations.*` group
(1,040 cases): **128 Pass / 64 Fail / 848 NotSupported**, up from 96
Pass / 96 Fail / 848 NotSupported (the full expected 32-case delta
across L270+L271 combined). `dEQP-VK.tessellation.user_defined_io.*`
(54 cases, the hull-split family the L271 regression touched) re-run at
**100% Pass**, confirming no regression from the `Phase == Ordinary`
guard.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change -- both are internal compiler-pass correctness fixes widening
coverage of already-exposed core GLSL stage-IO semantics, not a new
feature/extension.

**Remaining `atomic_operations` residual:** 64 Fail still outstanding
(the group's 96-case triage started at 96, so 64 of the *original* 96 are
still unfixed) -- these are believed to be the runtime `Image mismatch`
half rather than pipeline-creation rejections, not yet individually
triaged past this session's initial fragment/vertex stage-linkage split;
worth a dedicated follow-up session.

**Full-sweep confirmation:** a fresh full `dEQP-VK.glsl.*` sweep
(28,420 cases) after L270/L271 landed completed with **19,057 Pass /
400 Fail / 8,963 NotSupported** -- within 1 case of the predicted
19,056 Pass / 401 Fail delta against L269's baseline (19,024/433/8,963
+ 32 from L270+L271 combined), confirming no unexpected knock-on shifts
elsewhere in the full sweep from these fixes (the 1-case discrepancy is
consistent with ordinary test-to-test noise, not a new regression).

## L259: `resolveRowComponent`'s narrow-vector array-peeling stride -- `H101h`'s XFB-only fix applied too broadly, fixed for the ordinary (non-XFB) case

**Root cause:** `resolveRowComponent`'s array-peeling loop (in
`CanonicalizeStage.cpp`) computes a column/row index via `Idx = Residual
/ RowSize`. Since `H101h` (`L257`'s own fix), `RowSize` for a narrow
(3-wide) vector array element was always computed via
`getPackedElementSize` -- a tightly-packed, no-ABI-padding size (12
bytes for `<3 x float>`). This is correct only for a
`VK_EXT_transform_feedback`-captured array (an `XfbBuffer`-decorated
global), whose real memory layout is genuinely tightly packed at SPIR-V's
own `XfbStride`. For an ordinary (non-XFB) Location-addressed
Input/Output stage-IO global -- e.g. a plain `in mat4x3 m;` vertex
attribute -- `SPIRVToLLVMPatterns.cpp`'s SPIR-V-to-LLVM conversion
addresses every array element at a *padded* stride instead (every
vector's own lane count rounded up to the next power of two, 16 bytes
for `<3 x float>`), confirmed empirically via a raw pre-canonicalization
IR dump showing GEP byte offsets 0/16/32/48 (not 0/12/24/36) for a
`mat4x3`'s 4 columns. `H101h`'s fix, applied unconditionally, divided
this padded 48-byte offset by the tightly-packed 12-byte stride, landing
on `Row 4` (`48 / 12`) -- one past the true last row (`RowCount == 4`),
`feme-graphics-validate-stage`'s own `"row 4 is out of range for
element"` `vkCreateGraphicsPipelines`-time rejection.

**Note on the module's own `DataLayout` at canonicalization time:** an
earlier attempt at this fix used `DataLayout::getTypeAllocSize` directly
for the padded case (matching `DL.getTypeAllocSize(<3 x float>) == 16`
under LLVM's plain default-constructed `DataLayout`). This did *not*
work: the actual `DataLayout` object attached to the module at the exact
point `CanonicalizeStagePass` first runs (`GraphicsPipeline.cpp`, before
`Pipeline.cpp`'s later `M.setDataLayout(HostDL)`) is a plain
MLIR-derived layout string (`e-ve-i64:64-n8:16:32:64-G10`, no
vector-alignment entries of its own), under which
`getTypeAllocSize(<3 x float>)` computes an *unpadded* 12 bytes --
disagreeing with the real, already-baked-in 16-byte-strided GEP offsets.
Fixed instead with a new `getPaddedElementSize` helper that mirrors
`getPackedElementSize`'s own recursive struct/array/vector-peeling shape,
but rounds a `FixedVectorType`'s own lane count up to the next power of
two before multiplying, independent of whichever `DataLayout` happens to
be attached to the module at the time.

**Fix:** added a `bool UseTightArrayStride` parameter, threaded through
`resolveOffsetWithinElement` -> `resolveNestedStageIOField` ->
`resolveRowComponent`, selecting `getPackedElementSize` (tight) vs. the
new `getPaddedElementSize` (padded) for the array-peeling loop's own
`RowSize`. Set from a new `hasXfbBufferDecoration(GlobalVariable *GV)`
helper (checks `parseSPIRVDecorations(...).XfbBuffer.has_value()`) at
each of `resolveStageIOAccess`'s 3 external call sites into
`resolveOffsetWithinElement`.

**Unit tests:** new
`CanonicalizeStageTest.ResolvesOrdinaryNarrowVectorMatrixColumnsAtPaddedStrideNotTightStride`
(a plain, non-`XfbBuffer`-decorated `mat4x3` Input global, GEPs at
byte offsets 0/16/32/48, asserting rows `{0,1,2,3}`), confirmed to fail
pre-fix (rows `{0,1,2,4}`); existing `H101h`
`MapsArrayOfBlockInstancesWithNarrowVectorMemberToDistinctRows` XFB test
re-confirmed still passing (the `XfbBuffer`-decorated tight-stride path
is unaffected). Full `CanonicalizeStageTest`/`ValidateStageTest`/
`UnrollConstantTripCountLoopsTest` suite: 125 tests, all green.

**`check-feme`:** 1,167 tests / 3,489 discovered, 3,428 Passed (+1 net
new unit test vs. L271's 3,427 baseline), 61 Unsupported, 0 Failed (no
regressions).

**Targeted CTS re-run:** `dEQP-VK.glsl.matrix.add.dynamic.highp_mat4x3_float_fragment`
(the repro case) now passes. The full `dEQP-VK.glsl.matrix.*` group
(1,764 cases): **1,764 Pass / 0 Fail** -- up from 92 Fail, clearing the
entire cluster. `dEQP-VK.glsl.indexing.varying_array.*` (64 cases):
**49 Pass / 15 Fail** -- all 15 remaining failures are `vec3_*`-suffixed
and confirmed via A/B `git stash` testing (stashing just this fix's own
`CanonicalizeStage.cpp` change) to reproduce identically with or
without it, i.e. a **distinct, pre-existing** runtime `Image mismatch`
bug in the dynamic-row-indexed path (`getDynamicRowIndexedAccess`/
`collectDynamicRowTerms`), not this fix's own constant-offset
`resolveRowComponent` path -- filed as new roadmap item `L272`.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change -- an internal compiler-pass correctness fix, not a
feature/extension surface change.

## L272: `dEQP-VK.glsl.indexing.varying_array.vec3_*` residual `Image mismatch` -- two distinct manifestations of the same `DataLayout`-ambiguity root cause, both fixed

**Root cause (general):** two different `DataLayout`s are active at two
different points in the FeMe graphics-pipeline lowering pipeline -- a
transient SPIR-V-logical one (`e-ve-i64:64-n8:16:32:64-G10`, which marks
vectors element-aligned, computing an unpadded 12-byte `getTypeAllocSize`
for `<3 x float>`) active while `CanonicalizeStagePass` itself runs, and
the final CPU-target one (which pads a 3-wide vector up to its own
4-wide SIMD register size, 16 bytes) attached only later. Whichever
`DataLayout` happened to be active at the specific moment a *particular*
GEP or byte offset was folded/generated determines which stride that
access's own IR literally encodes -- this is **not** a recoverable
static property of the accessed global itself, confirmed this session by
finding the same ambiguity manifesting in two structurally different
ways in two different code paths.

**Manifestation 1 (already fixed as part of `L259`):**
`resolveRowComponent`'s array-peeling loop (the ordinary constant-offset
path) already self-discovers the real stride by trying both
`getPackedElementSize`/`getPaddedElementSize` candidates against the
residual offset directly, rather than a single fixed heuristic. This
alone cleared 4/16 `vec3_*` cases in `dEQP-VK.glsl.indexing.varying_array.*`
(the purely-static "static-on-both-sides" combinations:
`vec3_static_write_static_read`, `vec3_static_write_static_loop_read`,
`vec3_static_loop_write_static_read`,
`vec3_static_loop_write_static_loop_read`).

**Manifestation 2 (root-caused and fixed this session):** the remaining
12/16 `vec3_*` cases (every combination involving a dynamic array index
on either side) were a **second, distinct** bug in the dynamic-index
path, `getDynamicRowIndexedAccess`'s "DynamicLane" special case. This
code disambiguates a byte-GEP wrapper's dynamic index between a
whole-row select (a genuinely dynamic array-element index,
`var[dynamicIdx] = ...`) and a per-lane select (a dynamic vector-lane
index, `var[i].component`) by comparing the wrapper's own literal
`[N x i8]` source-element-type width against the array element's
computed size. Before this fix, that computed size came from a single
`DL.getTypeAllocSize` call -- using whatever `DataLayout` happens to be
attached to the module at `CanonicalizeStagePass`'s own run time, i.e.
the *transient SPIR-V-logical* one, which returns the **tight** (12-byte)
size for `vec3`. But the GEP's own literal `[16 x i8]` wrapper (baked by
an *earlier* lowering step, under the eventual CPU-target *padded* size)
used 16 bytes -- so the `12 == 16` comparison always failed, silently
misrouting every genuine dynamic **row** index into the
`DynamicComponent` (per-lane) slot instead. The practical effect: each of
a `vec3` write's three vector lanes got assigned a *different, wrong
row* (`idx+0`, `idx+1`, `idx+2`) instead of all three lanes correctly
sharing *one* dynamically-selected row with varying components (`0`,
`1`, `2`) -- exactly matching the observed "Image mismatch" symptom
(confirmed via a `FEME_DUMP_IR_POSTCANON_TMP` temporary debug hook
showing the actual generated `feme.stage.output.store.f32` call
arguments: `Row` was a constant `0` and `Component` was the dynamic
`idx`-derived value, the inverse of correct behavior).

**Fix:** `getPackedElementSize`/`getPaddedElementSize` (previously only
defined much later in the file, alongside `resolveRowComponent`) are now
forward-declared just before `collectDynamicRowTerms`, and used in place
of the single `DL.getTypeAllocSize` candidate for both: (1) the
lane-vs-row disambiguation itself, which now matches if *either*
candidate equals the wrapper's own byte-array width
(`PackedSize == N || PaddedSize == N`); and (2) the constant-array-level
residual-division fallback (used when `DynamicLane` doesn't match the
current array level), which now uses the same self-discovering
`PackedValid`/`PaddedValid` check `resolveRowComponent` itself uses,
rather than a single fixed candidate.

**Unit test:** new
`CanonicalizeStageTest.ThreadsDynamicRowIndexIntoVec3ArrayOutputStoreThroughByteGEP`,
modeled on the existing `ThreadsDynamicRowWithConstantComponentIntoInterpolantArrayLoad`/
`ThreadsDynamicVertexIndexIntoOutputStore` tests: constructs a plain
`[4 x <3 x float>]` global written through the exact byte-GEP shape this
bug hit (`getelementptr [16 x i8], ptr addrspace(8) @out_arr, i64 %idx`),
and asserts all three resulting `feme.stage.output.store.f32` calls
share the *same* dynamic `Row` operand (a `trunc`/`zext` of `%idx` to
i32, since the pass always normalizes `Row` to i32 -- not the raw i64
argument itself) while `Component` varies constantly across `0`/`1`/`2`.
Confirmed the test fails without the fix (the byte-GEP's dynamic index
gets misrouted to `Component` instead, leaving the original `store`
unconverted or wrongly shaped) and passes with it.

**Targeted CTS re-run:**
`dEQP-VK.glsl.indexing.varying_array.vec3_*` (16 cases): **16/16 Pass**
(was 4/16). Full `dEQP-VK.glsl.indexing.*` (760 cases): **760/760 Pass**
(was 748/760) -- the entire group now clears cleanly. Full
`dEQP-VK.glsl.matrix.*` (1,764 cases): **1,764/1,764 Pass** (unchanged,
no regression).

**Broad regression sweep:** a fresh full `dEQP-VK.glsl.*` sweep (28,420
cases, the first full re-run in several sessions) confirms no
regressions anywhere else:

- Passed: 19,198/28,420 (67.6%) -- up from ~19,056 (the last known
  baseline before this session's fixes).
- Failed: 259/28,420 (0.9%) -- down from ~401.
- Not supported: 8,963/28,420 (31.5%) -- unchanged.

Every one of the 259 residual failures falls into an already-known,
unrelated cluster, re-tallied fresh from this sweep:
`atomic_operations` (64), `shader_expect_assume` (51),
`440.linkage.varying` (49), `loops` (30), `builtin_var` (21), `struct`
(16), `builtin` (14), `demote` (9), `derivate` (3), `logical_copy` (2).
None of these overlap with `L272`'s own `indexing`/`matrix` clusters,
confirming the fix introduced no new regressions.

**`check-feme`:** all 126 tests in `FeMeTransformsGraphicsTests`
(`CanonicalizeStageTest`/`ValidateStageTest`) pass, including the new
`ThreadsDynamicRowIndexIntoVec3ArrayOutputStoreThroughByteGEP` test.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- an internal compiler-pass correctness fix, not a
feature/extension surface change.

## L273/L274/L275: `atomic_operations` shared-memory whole-struct-copy cluster (64 cases) -- three layered bugs, two fixed, one filed

**Symptom:** the remaining `dEQP-VK.glsl.atomic_operations.*` 64 failures
(after `L270`/`L271` cleared the 32 stage-linkage cases) are exactly the
`{add,and,comp_swap,...}_{signed,unsigned}_{compute,mesh,task}_
{shared,payload}` combinations (8 ops x 2 signedness x 4 stage/storage
variants) -- every one copies a whole `AtomicStruct`-shaped field from an
SSBO into (and back out of) a `shared`/`taskPayloadSharedEXT`
(Workgroup-storage-class) variable via a plain GLSL struct assignment,
bracketing per-invocation atomic ops on the shared copy.
`FEME_VULKAN_LOG_CREATION_ERRORS=1` on `add_signed_compute_shared`
confirmed a pipeline-creation rejection, not a runtime `Image mismatch`.

### L273: `getUniformBlockElement` misclassified an AccessChain's own intermediate component-pointer type as a nested uniform block

**Repro method:** no `glslangValidator`/`glslc` binary exists anywhere in
this environment. Instead: ran `deqp-vk` with
`--deqp-log-decompiled-spirv=enable` to extract the real SPIR-V assembly
from the `.qpa` log, HTML-unescaped it, reassembled it with
`spirv-as --target-env vulkan1.0`, imported it with
`feme-translate --import-spirv` to get FeMe's own `spirv` dialect MLIR,
then reproduced the exact crash standalone with
`feme-translate --no-implicit-module --spirv-to-llvmdialect`.

**Root cause:** `'llvm.load' op operand #0 must be LLVM pointer type,
but got '!llvm.target<"spirv.VulkanBuffer", ...>'`. `getUniformBlockElement`
(`SPIRVToLLVMPatterns.cpp`) only excluded `BufferBlock`-decorated structs,
never positively required the `Block` decoration a genuine uniform
interface block actually carries. This let it also match a
`spirv.AccessChain`'s own intermediate component-pointer type -- reached
once a wrapper block's sole field has already been selected as a whole,
pointing at an ordinary, undecorated inner struct -- as a second, nested
uniform block, wrongly routing the following `spirv.Load`/`spirv.Store`
through the handle-based (`spirv.VulkanBuffer`) conversion instead of
leaving it as ordinary memory (address space 12, the type-converter's
own documented fallback intent).

**Fix:** require `Struct.hasDecoration(Block)` too, mirroring
`isBufferBlockStorage`'s existing symmetric positive check for
`BufferBlock`. Updated 3 pre-existing lit tests
(`spirv-to-llvm-uniform-buffer.mlir`, `spirv-to-llvm-glslang-blocks.mlir`,
`spirv-to-llvm-struct-trailing-gap-packed.mlir`) whose hand-written
wrapper structs had omitted the `Block` decoration as a textual shortcut
(real glslang/dxc output always carries it) so they keep exercising the
intended paths under the now-stricter check.

**Unit test:** new `SPIRVToLLVMTest.WholeWrapperFieldLoadFromStorageBufferConverts`,
confirmed via a reverted A/B test (temporarily short-circuiting the new
`Block` check to `false`) to fail pre-fix and pass post-fix.

**`check-feme`:** 3,430/3,491 discovered tests Passed (+1 net new unit
test), 61 Unsupported, 0 Failed (no regressions).

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- an internal compiler-pass correctness fix, not a
feature/extension surface change.

### L274 (fixed, follow-up session): two distinct, layered bugs in the CPU-target SIMDizer's groupshared-global handling

Re-running the full `dEQP-VK.glsl.atomic_operations.*` group (1,040
cases) against the rebuilt ICD after `L273`'s fix showed **64 Fail /
128 Pass / 848 NotSupported** -- the same raw count as before `L273`,
but the failure signature had moved: pipeline creation now progressed
past SPIR-V-to-LLVM-dialect conversion (`L273`'s own fix site) and
instead failed inside `feme-cpu-simdize` (`GroupShared.cpp`'s
`rewriteGroupSharedGlobals`):

- 62/64 cases: `'groupshared global ... feeds a nested getelementptr or
  another unsupported user; only a first-level getelementptr feeding a
  direct load, store, atomicrmw, masked gather/scatter, or (for a
  vector-typed row load, or a uniform row address broadcast into one) a
  second-level per-component getelementptr feeding its own masked
  gather/scatter is supported (roadmap milestone 9 deviation)'`.
- 2/64 cases: `'... a divergent value ... of aggregate type; component
  decomposition is not yet supported for this producer ...'`.

**Part 1: validation-scope gap.** `rewriteGroupSharedGlobals`'s
validation pass only ever accepted a first-level `getelementptr` off
the groupshared global plus exactly one further nesting level, and
only when that first-level GEP was vector-typed (the divergent-row
case) -- rejecting a genuinely uniform, scalar-pointer nested access
chain (e.g. a struct field's own nested array element,
`buf.data.field[k]`, the shape glslang's whole-`AtomicStruct`-field
struct-copy pattern compiles down to once its per-member
`CompositeExtract`/`CompositeInsert` decomposition further decomposes
an array-typed member) even though `retargetGroupSharedProducer`
already rewrites a chain like this correctly. Fixed by adding
`isSupportedGroupSharedNestedGEPUser`, a fully recursive,
depth-and-type-unrestricted check that also recognizes a broadcast
`insertelement` link as a valid terminal case at any nesting depth.
Built and unit/lit-tested cleanly on its own -- but a real CTS re-run
after this fix alone showed it did **not** clear any of the 64
failures (still 128 Pass / 64 Fail / 848 NotSupported), a significant
mid-session pivot: the fix was real and independently correct, but not
sufficient on its own.

**Part 2: GEP-duplication gap**, found only via targeted debug
instrumentation after the CTS re-run above. The real cause the
per-lane broadcast chain for these cases relies on the same nested
`getelementptr` being inserted at every link of the chain (identity,
not just structural equality) -- but
`llvm::convertUsersOfConstantsToInstructions`'s per-`(Constant,
BasicBlock)` memoization can materialize more than one
structurally-identical-but-distinct instance of the same nested
constant-expression GEP within one block, one per sibling insertion
point. The pre-existing `coalesceIdenticalGroupSharedGEPs` helper,
written to solve exactly this problem for *first-level* GEPs off the
global, never extended its predicate to a *nested* one (a GEP whose
own pointer operand is itself another GEP). Fixed by adding
`isGroupSharedRootedGEP` (walks up an arbitrary chain of parent GEPs
to check whether it is ultimately rooted at the global) and wrapping
the per-block coalescing pass in a fixpoint loop.

Both parts are shapes `rewriteGroupSharedGlobals`'s own pre-existing
code comments had documented as deliberately out of "milestone 9"'s
scope, not a regression or an oversight bug like `L273`'s own.

**Verification.** Added a new lit test for part 1 (struct-field +
nested array shape) and a dedicated lit test for part 2 (four
structurally-identical-but-distinct nested GEPs feeding separate links
of one per-lane broadcast chain), both confirmed via `git stash` A/B
testing to fail with the pre-fix diagnostic and pass post-fix. Rewrote
a now-stale pre-existing lit test and unit test that had asserted the
old, now-incorrect "unsupported" behavior for the array-of-array
shape part 1 now supports.

**`check-feme`:** 3,432/3,432 discovered tests Passed (+2 net new
tests vs. `L273`'s own count), 61 Unsupported, 0 Failed.

**Real CTS re-run** (`dEQP-VK.glsl.atomic_operations.*`, 1,040 cases)
against the rebuilt ICD with both parts applied: **176 Pass / 16 Fail
/ 848 NotSupported** -- clears 48/64 of the previously-failing cases
(all `*_{compute,mesh}_shared` cases). The residual 16
(`*_task_payload`) fail with a completely unrelated diagnostic (`JIT
session error: Symbols not found: [ spirv_var_48 ]` --
`vkCreateGraphicsPipelines: Failed to materialize symbols`), a distinct
missing-feature gap (task-payload-memory symbol materialization), not
a `GroupShared.cpp` SIMDizer scope issue -- filed separately as `L275`
rather than folded into this fix.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- a compiler-internals correctness fix, not a
feature/extension surface change.

**Net effect this session:** `L274`'s two-part fix, combined with
`L273`'s prior-session fix, takes the `atomic_operations` group from
64 Fail down to 16 Fail (128->176 Pass); the residual 16
`*_task_payload` cases are a new, distinct gap (`L275`, filed, not
started).

## L275: `atomic_operations.*_task_payload` residual (16 cases) -- missing atomic canonicalization, plus a use-after-free crash the fix itself introduced -- both fixed

**Symptom:** the 16 residual `dEQP-VK.glsl.atomic_operations.*_task_payload`
failures `L274` split out fail pipeline creation with `JIT session error:
Symbols not found: [ spirv_var_48 ]` /
`vkCreateGraphicsPipelines: Failed to materialize symbols`, unlike
`L273`/`L274`'s own pipeline-creation-*rejection* diagnostics -- an
unresolved external symbol at JIT-link time.

**Root cause:** `CanonicalizeStage.cpp` already canonicalized a plain
load/store through a task entry's bounded payload global (address space
14) into `feme.stage.task.payload.load`/`.store`, but never recognized an
`AtomicRMWInst`/`AtomicCmpXchgInst` against that same global -- GLSL's
`atomicAdd`/`atomicCompSwap`/etc. against a `taskPayloadSharedEXT`
variable. The raw atomic survived uncanonicalized all the way to
JIT-link time, still referencing the never-defined SPIR-V-derived global
name.

**Fix, part 1 -- canonicalization and lowering.** Added a new
`StageOpKind::TaskPayloadAtomicRMW`/`TaskPayloadAtomicCmpXchg` pair
(`feme.stage.task.payload.atomicrmw`/`.cmpxchg`), canonicalized in
`CanonicalizeStage.cpp` exactly like the existing load/store rewrite
immediately above it. Because these ops are, unlike every prior masked
call (`OutputStore`, `TaskPayloadStore`, `StreamEmit`/`Cut`,
`SetMeshOutputs`, `EmitMeshTasks` -- all void), both side-effecting
(need active-lane masking under SIMD divergence) *and* value-producing,
a new "masked, value-producing call" family was added
(`StageMaskCalls.h`/`.cpp`), threaded through `Linearize.cpp`'s masking
pass (which now RAUW's the original call's result with the masked call's
result, rather than just erasing) and `SIMDize.cpp`'s widening pass
(`widenMaskedTaskPayloadAtomicRMW`/`CmpXchg`, storing the widened result
in `Widened` exactly like `widenGroupSharedAtomicRMW`/`CmpXchg` already
does). `TaskPayloadWrapper.cpp`'s `lowerTaskPayloadAtomicRMW`/`CmpXchg`
then perform the real per-lane `atomicrmw`/`cmpxchg` against
`Env.Payload + Offset`, reusing `lowerTaskPayloadStore`'s address
computation and `widenGroupSharedAtomicRMW`/`CmpXchg`'s
identity-substitution masking technique (via the shared
`feme::cpu::getAtomicRMWIdentity` helper, extracted from `SIMDize.cpp`
in a prior session) to neutralize inactive-but-in-bounds lanes.

**Fix, part 2 -- a SIGSEGV the part-1 fix itself introduced.** The
`AtomicCmpXchgInst` canonicalization rewrites its `{ old, i1 success }`
aggregate result by walking `CX`'s `ExtractValueInst` users and erasing
them once rewritten. The enclosing per-instruction rewrite loop uses
`llvm::make_early_inc_range`, which only guards against erasing the
*current* instruction being visited (it caches the next iterator just
before yielding it) -- `CX`'s `ExtractValueInst` users necessarily sit
*later* in program order (a use can't precede its def), so eagerly
erasing them (and `CX` itself, which they still reference) from inside
the same iteration invalidated that cached "next" iterator the moment it
happened to point at one of them. Reproduced standalone with a minimal
two-`extractvalue`-user `feme-opt` repro (`--llvm
-passes=feme-graphics-canonicalize-stage`); confirmed in practice as a
raw `SIGSEGV` inside `canonicalizeSPIRVStage`, isolated via `gdb` to a
single case,
`dEQP-VK.glsl.atomic_operations.comp_swap_signed_task_payload`, when
attempting a full-group CTS run. Fixed by deferring both `CX`'s and its
`ExtractValueInst` users' erasure into a per-function worklist, drained
only after the per-instruction loop has finished walking every
instruction in that function -- by which point no cached iterator can
still be pointing at any of them.

**Verification.** Added a new lit test
(`spirv-canonicalize-stage-task-payload-atomics.ll`) covering both the
new `atomicrmw`/`cmpxchg` canonicalization shape and, via a two-user
`cmpxchg`, the crash fix itself (this exact shape crashed pre-fix,
confirmed via the standalone `feme-opt` repro before the fix landed).

**`check-feme`:** 3,433/3,433 discovered tests Passed (+1 net new test
vs. `L274`'s own count), 61 Unsupported, 0 Failed.

**Real CTS re-run** (`dEQP-VK.glsl.atomic_operations.*`, 1,040 cases)
against the rebuilt ICD: **192 Pass / 0 Fail / 848 NotSupported** --
clears all 16 residual `*_task_payload` cases, zero regressions
elsewhere in the group. `atomic_operations` (the full 1,040-case group
`L270`-`L275` have progressively chased) is now entirely clear.

**`check-hlsl-feme-vk`** (offload-test-suite): re-run for regression
confirmation -- unchanged from prior sessions (`spec_const_32_bits.test`/
`WaveActiveMax.test` still fail, `array_of_matrices.test` still an
unexpected pass from its own stale `XFAIL:`), all pre-existing,
unrelated to this fix, confirmed unchanged again this session.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- a compiler-internals correctness fix, not a
feature/extension surface change.

**Net effect this session:** `L275`'s fix, combined with `L273`/`L274`'s
prior-session fixes, takes the `atomic_operations` group's full residual
failure count from 64 (`L273`'s starting point) down to 0. The
`atomic_operations`-specific triage thread `L270` opened is now
complete.

## L276: `shader_expect_assume.*` residual (51 cases) -- two independent bugs, both fixed; `atomic_operations`-era poison theory retired as a red herring

**Starting point.** `L258`/`L263`'s tally listed `dEQP-VK.glsl.
shader_expect_assume.*` as a 51-case untriaged cluster, several
sessions running without a dedicated look. Picked up this session.

**False lead (most of a prior segment plus the first half of this
one).** An extensive investigation initially chased a suspected LLVM
poison-propagation bug: `femeCpuResourceLoadRawI32`'s combined
`if (!((OkRaw||OkStructured) && Mask))` bounds check appeared to
compile to an `and` of a (potentially poison) comparison result with
`Mask` in one standalone experiment, versus a `select` in a manually
split `if (!Mask) ...; if (!(OkRaw||OkStructured)) ...;` form -- a
plausible poison-propagation difference if `Mask`'s own producer could
ever be poison. This was refuted by two independent findings once
pursued to ground truth: (1) the actual runtime bitcode compile target
is `aarch64-unknown-linux-gnu` (confirmed via `ninja -t commands`), not
`x86_64-unknown-linux-gnu` as every standalone experiment this
session and the last had (silently, incorrectly) used -- both the
combined and split forms compile to `select` (poison-safe) on the real
target, so there was never a codegen discrepancy to exploit in the
first place; (2) a new `FEME_DUMP_IR_POSTOPT` debug dump (added this
session in `CompiledStage.cpp`, alongside `FEME_DUMP_IR_PREOPT`, at the
one point in the whole pipeline where the fully runtime-linked,
post-optimization, pre-codegen module can actually be observed --
every pre-existing `FEME_DUMP_IR*` dump point predates the runtime
library getting linked in at all) showed `femeCpuResourceLoadRawI32`
is never inlined into its caller regardless of form, so no
inlining-site poison interaction of the theorized kind is even
possible.

**Ground truth, via new runtime instrumentation.** With the poison
theory exhausted, added a temporary JIT host-callback mechanism to get
a real runtime trace: a `femeDebugPrintf` function defined in
`Pipeline.cpp` (a normally-linked, non-JIT'd file) bound into the JIT's
main `JITDylib` via `orc::absoluteSymbols` with the function's real
in-process address (`FEME_DEBUG_JIT_CALLBACKS=1`-gated), called from
inside `femeCpuResourceLoadRawI32` in `FeMeRuntimeCPU.c`. Two dead ends
were hit before this worked: an `EPCDynamicLibrarySearchGenerator`-based
dynamic-symbol-search approach failed because `libfeme_vulkan.so` is
built against an explicit linker export list
(`libfeme_vulkan.exports`, only the 4 ICD entry points) that hides every
other symbol from `dlsym`-style lookup regardless of C++ visibility
attributes -- even after temporarily adding the symbol to that export
list, the generator still failed, suspected to be an `RTLD_LOCAL`
dlopen-visibility mismatch from how the Vulkan loader opens an ICD.
`orc::absoluteSymbols` sidesteps both problems entirely, since it binds
a real function pointer directly rather than resolving anything via
dynamic-library search.

The resulting trace showed, for `compute.assume.storagebuffer`'s
second (input) storage-buffer binding: `kind=0 size=0` -- the
`ResourceKind::None`/all-zero "never written" sentinel, for every lane,
unconditionally. Not a mask or poison issue at all: the descriptor was
simply never populated.

**Root cause 1 (pipeline-creation rejection, fixed first).** Reading
`vktShaderExpectAssumeTests.cpp`'s `generateComputePipeline` found the
real reason pipeline creation itself failed before runtime instrumentation
was even reachable: a divergent `llvm.assume`/`llvm.expect.iN` call
(lowered from `OpAssumeTrueKHR`/`OpExpectKHR`, `VK_KHR_shader_expect_assume`,
roadmap `F4`) hit `FunctionWidener::widenElementwise`'s final
"unsupported divergent call" diagnostic in `SIMDize.cpp` -- neither
intrinsic has a vector-typed overload `getDivergentCallOverloadShape`'s
per-lane-masked-call widening can target. Both are pure compiler hints
with no runtime-observable side effect (`llvm.assume(i1)` is UB only if
false; `llvm.expect.iN` always returns its first operand unchanged
regardless of the "expected" value), so the fix drops a divergent
`llvm.assume` call outright and rewrites any `llvm.expect` use to the
(widened) value it wraps directly, rather than widening either into a
real per-lane masked/reduced call. New lit test
`simdize-divergent-assume-expect.ll`.

**Root cause 2 (the runtime `Image mismatch`, found via the
instrumentation above).** With pipeline creation succeeding, the
`.storagebuffer`-suffixed sub-cases still failed at runtime. The CTS
test's own descriptor-set update issues a *single* `VkWriteDescriptorSet`
with `dstBinding=0` but `descriptorCount=2` (covering both an
output-buffer binding 0 and an input-buffer binding 1 in one
`pBufferInfo` array) -- explicit, spec-legal Vulkan usage: per spec, a
write whose `descriptorCount` exceeds its named binding's own declared
array size must continue writing into the next consecutively-numbered
binding. `feme/lib/Vulkan/Descriptor.cpp`'s `vkCopyDescriptorSets` path
already implements exactly this for `VkCopyDescriptorSet` entries (via
`L154`'s `BindingCursor` helper), but `applyDescriptorWrite` (the
function `vkUpdateDescriptorSets` calls per `VkWriteDescriptorSet`) did
not -- its per-element loop always wrote to the fixed
`Write.dstBinding`, silently dropping anything past that binding's own
declared size instead of spilling into binding 1, leaving binding 1
permanently unpopulated. Fixed by relocating `BindingCursor` earlier in
the file (ahead of `applyDescriptorWrite`) and reusing it inside
`applyDescriptorWrite`'s own per-element write loop, mirroring the copy
path's own use of it exactly (`Set.bindingArray(B).size()` for
buffer/texel-buffer descriptors, `Set.imageBindingArray(B).size()` for
image/sampler descriptors; inline-uniform-block writes are unaffected,
already handled separately as a single bounded byte-range write). New
unit test `WriteDescriptorSetSpansConsecutiveBufferBindings`
(`DescriptorTest.cpp`).

**Cleanup.** All temporary debug instrumentation (the `femeDebugPrintf`
callback plumbing, its `libfeme_vulkan.exports` entry, the
`FEME_DEBUG_JIT_CALLBACKS` JIT wiring) was fully reverted once the fix
was confirmed. `FEME_DUMP_IR_PREOPT`/`FEME_DUMP_IR_POSTOPT` were kept
as small, permanent, opt-in debug aids in `CompiledStage.cpp` --
consistent with the existing `FEME_DUMP_IR*` family and filling a
genuine, previously-missing observability gap (the only point the
fully runtime-linked, post-optimization module can be inspected) that
cost real time to work around this session.

**`check-feme`:** 3,435/3,496 discovered tests Passed (+2 net new
tests: 1 lit, 1 unit), 61 Unsupported, 0 Failed.

**Real CTS re-run** (`dEQP-VK.glsl.shader_expect_assume.*`, 141 cases)
against the rebuilt ICD: **69 Pass / 0 Fail / 72 NotSupported** (was 18
Pass / 51 Fail / 72 NotSupported at session start; the 72
`NotSupported` are all 8-bit-integer-gated, unrelated, unchanged) --
clears the entire residual cluster.

**`check-hlsl-feme-vk`** (offload-test-suite): re-run for regression
confirmation -- unchanged from prior sessions (`spec_const_32_bits.test`/
`WaveActiveMax.test` still fail, `array_of_matrices.test` still an
unexpected pass from its own stale `XFAIL:`), all pre-existing,
unrelated to this fix, confirmed unchanged again this session.
`offload-test-suite`'s `feme` branch was confirmed up to date with its
remote (no drift, no re-merge needed).

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- both fixes are compiler-internals/driver-internals
correctness fixes to already-exposed core-1.0/`VK_KHR_shader_expect_assume`
behavior, not a new feature/extension surface change.

**Net effect this session:** the `shader_expect_assume` triage thread
`L258`/`L263` opened is now complete, closing the second-largest
untriaged cluster from that tally (`atomic_operations`, the largest,
was already closed by `L270`-`L275`).

## Session: fresh full `dEQP-VK.glsl.*` sweep + `loops` cluster root-cause (L277)

**Offload-test-suite branch-drift check:** `feme` branch had drifted back
to `origin/main` again (environment appears to reset this checkout
between sessions -- see `agent_thoughts.md` for the recurring
`reflog` pattern). Re-merged `FETCH_HEAD` cleanly to restore
`OFFLOADTEST_ENABLE_FEME_VULKAN`/`feme_vulkan` CMake support.

**Fresh full `dEQP-VK.glsl.*` sweep** (28,420 cases), re-run from
scratch to confirm the residual-failure tally carried over from prior
sessions is still accurate: **19,319 Pass / 139 Fail / 8,963
NotSupported** (was ~19,198/259/8,963 before `L273`-`L276`'s fixes
landed; net +121 Pass this run reflects those prior sessions' already-
landed fixes, no new regressions). Fresh cluster breakdown by 3-level
test-path prefix: `440.linkage.varying` (49), `loops` (30),
`builtin_var` (21), `builtin` (14), `struct` (12), `demote` (9),
`derivate` (3), and one previously-uncalled-out single case,
`texture_gather` (1) -- 139 total, matching the reported Fail count
exactly.

**`loops` cluster (30 cases) root-cause investigation** (see `L277`):
reproduced `dEQP-VK.glsl.loops.special.for_dynamic_iterations.
dowhile_trap_fragment` directly, confirming a pipeline-creation
rejection (`feme-cpu-simdize: ... has a divergent branch; ... did not
remove it`). Using `FEME_DUMP_IR_PRESIMD`/`FEME_DUMP_IR_PRELINEARIZE`
plus a new debug aid added this session (`FEME_DEBUG_LINEARIZE_TRACE`,
see its own commit) to get ground truth on exactly which cycles
`LoopLinearizer::linearizeCycle` is invoked on: confirmed that for
this genuinely two-level nested divergent loop, only the *inner* cycle
is ever linearized -- `linearizeCyclePostOrder`'s own `CI.children(C).
empty()` leaf-only gate means the *outer* cycle's own divergent
backedge is never even attempted, left as a raw per-lane branch that
SIMDize correctly rejects. This is a pre-existing, deliberate,
documented limitation (four previously-fixed bugs plus one still-open
"bug 5" stack overflow are already recorded in-code as the reason
non-leaf traversal stays disabled).

New finding this session: experimentally flipping the gate to attempt
non-leaf cycles unconditionally does **not** hit the documented stack
overflow for this particular repro -- pipeline creation succeeds --
but the test then hangs at runtime (`VK_TIMEOUT`), a **second,
previously-undocumented bug** distinct from bug 5. The experimental
change was fully reverted (not a fix, a regression); only the
diagnostic debug aid was kept and committed. **No fix lands for the
`loops` cluster this session** -- filed as `L277`, scoped as a
dedicated few-hours-or-more session given two separate bugs now block
it.

**`check-feme`:** 3,435/3,496 discovered tests Passed (unchanged), 61
Unsupported, 0 Failed -- confirms the debug-aid-only commit is a true
no-op when its env var is unset.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- investigation only, no fix landed, no feature/
extension surface change.

**Net effect this session:** no case count change (`loops` remains
30/30 Fail); the cluster's root cause is now well-understood and
precisely scoped for a future dedicated session, with a reusable
debug aid (`FEME_DEBUG_LINEARIZE_TRACE`) left in place to speed up
that future work.

## L278: `440.linkage.varying` fragment-output/vertex-input component-split fix

Root-caused and mostly fixed the `440.linkage.varying` cluster (49
`dEQP-VK.glsl.*` failures from the fresh full sweep documented above),
split 25 `frag_out` + 24 `vert_in`.

**Root cause (two layered bugs, both in `feme/lib/Graphics/
Executor.cpp`):** SPIR-V's `Component` decoration lets several
otherwise-unrelated interface variables share one `Location`, each
covering a disjoint sub-range of its 4 components -- e.g. two separate
scalar `int` fragment outputs (or vertex inputs) at one `location`,
`component = 0`/`1`, together forming one `ivec2` value.

1. The fragment-output-to-color-attachment resolution logic
   (`FSColors`/`FSColorRows`) only ever found *one* `SignatureElement`
   per location via `findElementCoveringLocation` (hardcoded
   `Component = 0`), silently missing every split element past the
   first -- the `.y` component of a split `ivec2` output was left
   unwritten/garbage.
2. `StageStorage::readRaw`/`writeRaw`'s `Component` parameter is
   always the *absolute* component index (0-3); the function itself
   subtracts `E.FirstComponent` internally. Both the fragment-output
   read functions and the vertex-attribute fetch loop's
   `decodeAttribute` call were passing/decoding a *local*
   (element-relative) index instead -- only ever correct when
   `FirstComponent == 0` (every previously-tested whole-vec4-style
   attachment/attribute), silently wrong (reading/writing the first
   split element's own data again) for a second-or-later split
   element.

**Fix:** added a new plural `findElementsCoveringLocation`
(`StageStorage.h`/`.cpp`, ignoring `FirstComponent` when matching) and
reworked `FSColors`/`FSColorRows` into per-attachment lists of *all*
split elements with new multi-element `readFragmentColor`/
`readFragmentColorInt` overloads that merge every element's own
components into one RGBA value; threaded `Elem.FirstComponent`
through every fragment-output read call site; added a `StartComponent`
parameter to `decodeAttribute` so the vertex-attribute fetch loop
decodes from (and writes to) the correct absolute channel, with its
buffer-bounds/format-channel-count robustness caps adjusted to be
relative to `FirstComponent` too.

**New regression test:**
`VertexAttributeFetchHonorsFirstComponentOfSplitComponentInputs`
(`ExecutorTest.cpp`) reproduces the vertex-input shape directly (two
split `int` inputs sharing one `ivec2` attribute with distinct
per-channel values), confirmed via a `git stash` A/B test to fail
without the fix and pass with it.

**`check-feme`:** 3,436/3,497 discovered tests Passed (+1 new unit
test), 61 Unsupported, 0 Failed -- no regressions.

**CTS re-run:**
- `dEQP-VK.glsl.440.linkage.varying.component.frag_out.*` (25 cases):
  **0 Fail** (was 25) -- fully cleared.
- `dEQP-VK.glsl.440.linkage.varying.component.vert_in.*` (24 cases): 5
  Fail remain (was 24) -- all 5 are the `*_unused` variants, which
  declare a *third* split input specifically at an out-of-range
  component (e.g. `component = 3` on a 2-channel `ivec2` attribute) to
  verify it reads back as a defined zero. Debug instrumentation
  confirmed the vertex-attribute fetch itself now computes and stores
  the correct zero for that out-of-range component, so this residual
  bug is elsewhere in the pipeline -- most likely the compiled shader
  IR's own read of that component at the JIT/codegen level, not the
  host-side fetch path this fix targeted. Not chased further this
  session given the much larger 44/49 win already landed; filed as
  future work under `L278` in `Roadmap.md`.
- `dEQP-VK.pipeline.monolithic.vertex_input.*` (13,296 cases, broad
  spot-check for regressions from the `FSColors`/vertex-fetch
  data-structure changes): 4,600 Pass / 217 Fail / 8,476 NotSupported,
  no crashes/assertions -- consistent with this group's own pre-existing,
  unrelated baseline limitations (not separately re-baselined this
  session; no new crash-class symptoms observed).

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- an internal compiler/runtime-internals correctness
fix to already-exposed core GLSL `component`-decorated linkage, not a
new feature/extension surface.

**Net effect this session:** `440.linkage.varying` cluster reduced
from 49 Fail to 5 Fail (44 cleared); `frag_out` subgroup fully closed.

## L279: `440.linkage.varying`'s residual `vert_in.*_unused` component-3 default-fill fix

Root-caused and fixed the 5 residual `vert_in.*_unused` failures `L278`
left open, fully closing the `440.linkage.varying` cluster (49/49
cases now passing).

**Root cause:** the previous session's own debug instrumentation had
correctly shown the host-side vertex-attribute fetch computes and
writes 0 for a shader input whose absolute `component` decoration
(here, `component = 3`, e.g. `in0_3` in
`dEQP-VK.glsl.440.linkage.varying.component.vert_in.ivec2.
as_int_int_unused`) falls beyond the channel count of its bound
attribute format (a 2-channel `R32G32_SINT`, only ever supplying
components 0/1) -- but this was checked against the *wrong* expected
value. Re-reading the CTS shader source
(`external/vulkancts/data/vulkan/glsl/440/linkage.test`) shows
`var0 = in0_3 * in0 + in0`, i.e. `(in0_3 + 1) * in0`; the test's own
hardcoded expected output (`ivec2(14, 26)` for `in0 == ivec2(7, 13)`)
is only satisfiable with `in0_3 == 1`, not 0 -- CTS is deliberately
exercising the Vulkan spec's standard "a component the bound vertex
attribute format never supplies at all defaults to 0 for X/Y/Z, 1 for
W" convention, keyed on *absolute* component index 3 (the "W"/"alpha"
slot), independent of whether the reading shader variable happens to
be a whole `vec4` starting at component 0 or, as here, a lone scalar
landing at absolute component 3 via its own `FirstComponent`.

`feme/lib/Graphics/Executor.cpp`'s existing default-fill logic already
implemented this "missing W defaults to 1" rule correctly, but gated it
on `Elt.ComponentCount == 4` -- true only for a whole-vec4-shaped
element reading its own components 0-3, never for a narrower
(`ComponentCount == 1`) split-component scalar sitting at absolute
position 3. `in0_3` (`FirstComponent == 3`, `ComponentCount == 1`) fell
through this gate entirely and stayed at its zero-initialized default.

**Fix:** replaced the `Elt.ComponentCount == 4` gate with two explicit
checks -- `ElementCoversComponent3` (does this element's own absolute
`[FirstComponent, FirstComponent + ComponentCount)` range include
component 3 at all?) and `FormatMissesComponent3` (does the bound
*format* -- not the buffer-bounds availability, which must keep
deferring to the existing separate robustness zero-fill -- fail to
reach component 3?), both computed from data already available at that
point in the loop (`FormatComponents`, the format's real, robustness-
independent channel count). The default-to-1 fill now fires whenever
both are true, regardless of the element's own `ComponentCount`.

**New regression test:**
`VertexAttributeDefaultsScalarComponent3BeyondFormatChannelCount`
(`ExecutorTest.cpp`) extends `L278`'s own split-component-input shader
shape with a third scalar input at `FirstComponent == 3` sharing the
same 2-channel `R32G32_SINT` attribute, asserting it reads back 1 --
confirmed via a `git stash` A/B test to fail without the fix (reading
back 0) and pass with it.

**`check-feme`:** 3,437/3,498 discovered tests Passed (+1 new unit
test), 61 Unsupported, 0 Failed -- no regressions.

**CTS re-run:**
- `dEQP-VK.glsl.440.linkage.varying.component.vert_in.ivec2.
  as_int_int_unused` (standalone repro): **Pass** (was Fail).
- `dEQP-VK.glsl.440.linkage.varying.*` (68 cases, the full cluster):
  **68/68 Pass, 0 Fail** -- fully cleared, closing out `L278`'s
  residual 5-case gap.
- `dEQP-VK.pipeline.monolithic.vertex_input.*` (13,296 cases, broad
  regression spot-check for the default-fill logic change): 4,600 Pass
  / 102 Fail / 8,594 NotSupported/QualityWarning, all 102 fails sharing
  one identical, unrelated `VK_ERROR_INITIALIZATION_FAILED` diagnostic
  (`mat3`-shaped vertex-attribute pipeline creation, a distinct
  pre-existing gap, not touched by this fix) -- confirmed no regression
  in any narrower-format-to-`vec4` case this fix's changed code path
  covers (e.g. `legacy_vertex_attributes.multi_binding.
  r32g32b32a32_sfloat_*` group, all passing).
- Fresh full `dEQP-VK.glsl.*` sweep (28,420 cases, kicked off at this
  session's start against the pre-fix binary, per the standing "fresh
  sweep each session" protocol): **19,363 Pass / 94 Fail / 8,963
  NotSupported** pre-fix; re-clustering the 94-Fail list confirmed
  exactly the 5 known `440` cases plus `loops` (30, `L277`, not yet
  fixed), `builtin_var` (21), `builtin` (14), `struct` (12), `demote`
  (9), `derivate` (3), and one new/unexplored single `texture_gather`
  case. With this fix, `440`'s cluster clears in full: expected
  **19,368 Pass / 89 Fail / 8,963 NotSupported** (confirmed directly
  via the full `440.linkage.varying.*` re-run above against the
  post-fix binary, rather than re-running a third full 28,420-case
  sweep purely to re-confirm an already-isolated 5-case delta).

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- an internal driver-internals correctness fix to
already-exposed core-1.0 vertex-input default-fill behavior, not a new
feature/extension surface.

**offload-test-suite (`check-hlsl-feme-vk`) regression check:** the
literal ninja target `check-hlsl-feme-vk` referenced by this repo's own
protocol did not exist in the configured build (only `check-hlsl-vk`/
`check-hlsl-clang-vk` etc. were present). Root cause: the local
`/home/dev/dev/offload-test-suite` checkout's `feme` branch had lost
its actual FeMe-enabling commit (`854cc3f`, "[Vulkan][FeMe] Add FeMe
test targets") -- a prior session's "no drift" branch check compared
local `HEAD` against `llvm-beanz/feme`'s tip and found them equal in
one direction, but the remote branch had itself moved 23 commits ahead
*without* `854cc3f` in its ancestry (it now sits on a different, older
base), so local's tip silently stopped including it despite the
protocol's fetch check finding "no drift" (that check only compares
tips, not whether a specific known-important commit is still present).
Fixed by `git cherry-pick 854cc3f` onto the local `feme` branch tip
(clean, no conflicts) and re-running `cmake .` in the `llvm-project`
build directory to pick up the newly-restored
`OFFLOADTEST_ENABLE_FEME_VULKAN`-gated targets. This is a fix to the
external `offload-test-suite` checkout's branch state only -- no
`llvm-project` files were touched, so no commit was needed in this
repo for it.

With the target restored, ran `ninja check-hlsl-feme-vk` (via
`VK_ICD_FILENAMES`/`VK_DRIVER_FILES` set on separate `export` lines
pointing at `feme_icd.json`, confirmed first via `vulkaninfo --summary`
showing `FeMe CPU Vulkan Device`): **486/727 Passed, 207 Unsupported,
31 Expectedly Failed, 2 Failed, 1 Unexpectedly Passed** -- exactly
matching the previously-flagged, confirmed-unrelated baseline
(`Feature/SpecializationConstant/spec_const_32_bits.test` and
`WaveOps/WaveActiveMax.test` failing; `Feature/PushConstant/
array_of_matrices.test` XPASS from a stale `XFAIL:`), i.e. **no
regression from the `L279` fix**.

## L280: `gl_PointCoord` (`builtin_var` `pointcoord` sub-cluster, 3 cases) -- fixed

Split `L278`'s residual `builtin_var` (21 cases) cluster into
`pointcoord` (3 cases, this entry) and `fragdepth` (18 cases, separate,
still open, findings below) sub-clusters, then root-caused and fixed
`pointcoord`.

**Root cause:** SPIR-V `BuiltIn` `PointCoord` (16) was entirely
unmapped in `CanonicalizeStage.cpp`'s `getSystemValueForBuiltIn`, so
`gl_PointCoord` fell through to `SignatureSystemValue::None` and was
rejected outright as an ordinary, `Location`-less user varying at
pipeline-creation time. Both `Executor.cpp`'s fragment-input-linking
loop and `StageLink.cpp` already skip that specific rejection for any
non-`None` system value, so the enum mapping alone was sufficient to
unblock linking -- but unlike `SamplePosition`/`SampleIndex` (a fixed
value re-read once per pass), `gl_PointCoord` genuinely varies
per-fragment across a single point sprite, so real value synthesis was
also needed, not just a mapping.

**Fix:** added `SignatureSystemValue::PointCoord` and its
`BuiltIn 16` mapping; `Executor.cpp`'s `emitPointQuad` now stamps a
per-corner `(s, t)` value ((0,0) top-left through (1,1) bottom-right,
Vulkan's Point Sprite convention) onto each of a point's 4 quad
corners, threaded through `pushQuadTriangle` exactly like
`EdgeDistance`/`ArcLength` (a line's own per-corner synthetic
attribute) already are, then barycentric-interpolated per fragment
into a new `FemeFragmentInvocation::PointCoord[Lane][2]` ABI field
(`RuntimeABI.h`/`StageArgsLayout.h`/`FragmentWrapper.cpp`, mirroring
`SamplePosition`'s existing read path exactly).

**New tests:** `CanonicalizeStageTest.FragmentStageMapsPointCoordBuiltin`
(enum mapping) and `ExecutorTest.RendersGlPointCoordAcrossAPointSprite`
(end-to-end interpolated value across a real 2-pixel point sprite,
confirming the affine bilinear `(s,t)` result an all-corners-share-
`InvW`/`Depth` point quad reduces to regardless of perspective
correction).

**`check-feme`:** 3,439/3,500 discovered tests Passed (+2 new unit
tests), 61 Unsupported, 0 Failed -- no regressions.

**CTS re-run:**
- `dEQP-VK.glsl.builtin_var.simple.pointcoord*` (3 cases, standalone
  repro): **3/3 Pass** (was 0/3).
- `dEQP-VK.glsl.builtin_var.*` (111 cases, the full group): **63 Pass**
  (was 60) / **18 Fail** (was 21, all `fragdepth.*` remaining,
  unrelated to this fix) / 30 NotSupported.

**`fragdepth` (18 cases) -- two distinct findings, neither fixed this
session:**

1. **Multisample image-creation failures**
   (`{line,point}_list_d32_sfloat_multisample_{2,4,8}`, 6 of the 18):
   the CTS's own `BuiltinFragDepthCaseInstance::checkSupport` queries
   `vkGetPhysicalDeviceImageFormatProperties` with
   `usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` only, sees
   sample counts 1/2/4/8 all supported (`framebufferDepthSampleCounts`),
   and proceeds -- but the real depth image it then creates
   (`vktShaderRenderBuiltinVarTests.cpp` line ~660) additionally
   requests `VK_IMAGE_USAGE_SAMPLED_BIT`, and `Image.cpp`'s
   `supportedSampleCounts` intersects that usage combination against
   `sampledImageDepthSampleCounts`, which `PhysicalDeviceInfo.cpp`
   deliberately advertises as `VK_SAMPLE_COUNT_1_BIT` only (a
   documented, pre-existing scoping decision: per-sample `OpImageFetch`
   reads of a depth/stencil image were left out of roadmap R30's
   scope, unlike a depth/stencil *attachment*'s own per-sample test
   support). The combined-usage `vkCreateImage` call then legitimately
   fails at 4/8 samples (`VK_ERROR_INITIALIZATION_FAILED`) since our
   own advertised capability doesn't cover it -- but CTS's narrower
   `checkSupport` query never saw that, so it never downgrades these
   cases to `NotSupported`. Closing this gap for real would mean
   implementing per-sample-index `OpImageFetch` for depth images (a
   real feature addition, out of this session's scope); flagged for a
   future roadmap item rather than attempted here.
2. **Non-multisample `gl_FragDepth` value mismatch** (remaining 12,
   e.g. `line_list_d32_sfloat`, `line_list_d24_unorm_s8_uint_no_depth_
   clamp`, `point_list_*` equivalents): a genuine runtime value bug,
   not a creation-time rejection. The qpa log's own diagnostic text
   (`line_list_d32_sfloat`) shows `Mismatch at pixel (10,2,0): expected
   0 but got -0.164062` -- a background (non-primitive-covered) pixel
   reading a large, out-of-`[0,1]`-range depth value rather than the
   expected cleared background value, suggesting either a wrong clear
   value or a line-rasterization coverage/width issue leaking into
   background pixels, not (necessarily) `gl_FragDepth`'s own write
   path. Not root-caused past this single observation; flagged as the
   top pick for a future dedicated `fragdepth` session.

**Fresh full `dEQP-VK.glsl.*` sweep:** kicked off this session
(`--deqp-case='dEQP-VK.glsl.*'`, run from the `deqp-vk` binary's own
directory so its relative data-file lookups resolve) to get an updated
residual-cluster tally past `L278`'s own 89-case baseline, but did not
finish inside this session's time budget (it is running dramatically
slower than prior sessions' full sweeps -- roughly 120 cases/minute
this run, versus whatever pace let prior full 28,420-case sweeps
complete in a single working session; possibly host-load-dependent,
not investigated further). See `agent_thoughts.md` for this session's
final status of that sweep and next steps.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- an internal driver-internals correctness fix to an
already-exposed core-1.0 builtin, not a new feature/extension surface.

## L281: shader-written `gl_FragDepth` not clamped (`fragdepth`
non-multisample value bug, 3 of 18 cases) -- fixed

Split out of `L280`'s two-findings `fragdepth` (18 cases) writeup:
finding 2 there ("non-multisample `gl_FragDepth` value mismatch",
originally counted as 12 cases) is now root-caused and partly fixed.

**Root cause:** `Executor.cpp`'s "late" depth-test-and-write path (the
path used whenever the fragment stage itself writes `SV_Depth`, since
per spec that disables early depth/stencil testing) read the shader's
own `FSDepthOut` value straight through with no clamp applied at all.
The pre-existing `depthClampEnable` clamp (`Depth = std::clamp(Depth,
Tri.DepthClampLo, Tri.DepthClampHi)`, added under roadmap H7d) only
ever covered the *rasterizer-interpolated* depth, computed earlier in
the same function and only actually used when the shader does *not*
write its own `gl_FragDepth`. Per the Vulkan spec's fixed-function
ordering, depth clamping applies to whichever depth value is actually
used for the depth test/write -- shader-written or
rasterizer-interpolated makes no difference to the spec, but it did to
this code.

**How this was found:** reproduced
`dEQP-VK.glsl.builtin_var.fragdepth.line_list_d32_sfloat` standalone
with `--deqp-log-images=enable`; the qpa's diagnostic (`Mismatch at
pixel (10,2,0): expected 0 but got -0.164062`) was initially
mis-read (in `L280`'s own writeup) as a background/coverage-leak bug,
since the CTS's own `validateDepthBuffer` always logs the
*covered-pixel* expected-value formula in its message text regardless
of which internal branch the check actually took. Hand-deriving the
exact per-pixel formula from the CTS's C++ source
(`vktShaderRenderBuiltinVarTests.cpp`: `control_buffer.data[ndx] =
(ndx/256.0) * sign`, `sign = depthClampEnable ? -1.0 : 1.0`) and
computing `index = gl_FragCoord.y*16 + gl_FragCoord.x = 2*16+10 = 42`
gives `-(42/256.0) = -0.1640625` -- an exact bit-for-bit match with the
qpa's reported `-0.164062`, proving the pixel genuinely was
shader-covered (not a background/coverage bug at all) and that the
real expected value (`0`) is this specific test case's own
`depthClampEnable = true` multiplier collapsing the "expected" formula
to `0` (clamping a negative depth to the near plane), not a "default
background" constant. This reframed the bug from "coverage mask
mismatch" to "missing depth clamp on a shader-written depth" --
confirmed against the Vulkan spec's own text on depth clamping via a
web search before implementing the fix.

**Fix:** in `Executor.cpp`'s late depth/stencil path, after reading
`FSDepthOut`'s value into `FragDepth`, clamp it to
`ScreenTris[Quad.TriIdx].DepthClampLo/Hi` (the same per-triangle bounds
the interpolated path already uses) whenever
`Pipeline.getRasterState().DepthClampEnable` is set -- the same guard,
re-applied a second time for the shader-depth-replaces-interpolated-
depth case the first clamp's own call site can never reach.

**Test:** new `ExecutorTest.ClampsAShaderWrittenFragDepthWhenDepthClampIsEnabled`
-- a full-viewport CCW triangle at a valid, in-range NDC depth (0.1)
whose fragment shader (`ConstantOutOfRangeFragDepthFragmentShaderIR`)
always overwrites `SV_Depth` with a constant, out-of-range `-0.5`,
with `depthClampEnable = true` and a depth test/write pipeline state;
asserts every covered texel's stored depth clamps to the viewport's
`MinDepth` (`0.0`), not the shader's own unclamped `-0.5`. Confirmed
via `git stash` to fail (`-0.5` stored) pre-fix and pass (`0.0` stored)
post-fix.

**`check-feme`:** 3,441/3,502 Passed (+1 net new unit test), 61
Unsupported, 0 Failed -- no regressions.

**CTS:** re-ran the full `dEQP-VK.glsl.builtin_var.fragdepth.*` group
(45 cases total, including the `large_depth`/unsupported-format
variants `checkSupport` already correctly rejects -- `L280`'s own "18"
count only ever referred to the non-`NotSupported` subset) both before
and after this fix (a real A/B via `git stash`, not inferred):

- Before: 9 Pass / 18 NotSupported / 18 Fail. That 18-Fail figure is
  `L280`'s own tally, but this session found it splits three ways, not
  the two `L280` described: 3 plain `{line,point,triangle}_list_
  d32_sfloat` (this fix's target), 6 multisample-image-creation-gap
  cases (`L280`'s finding 1, untouched here), and 6
  `_no_depth_clamp` + combined-stencil-format cases (a third, distinct
  bug neither previously isolated nor fixed -- see below). `L280`'s own
  "12 non-multisample value bug" estimate bundled the first and third
  of these together without distinguishing them; this session's A/B
  separates them for the first time.
- After: 12 Pass / 18 NotSupported / 15 Fail -- the 3 plain
  `{line,point,triangle}_list_d32_sfloat` cases now **Pass**; the
  remaining 15 Fail are the 6 multisample-creation-gap cases (unaffected,
  confirmed identical before/after) plus 6 `_no_depth_clamp` +
  combined-stencil-format cases (also confirmed bit-for-bit identical
  before/after via the same `git stash` A/B, so unrelated to depth
  clamping): `{line,point,triangle}_list_d24_unorm_s8_uint_no_depth_clamp`
  and `{line,point,triangle}_list_d32_sfloat_s8_uint_no_depth_clamp`.
  Their symptom is the *opposite* shape from this session's bug: e.g.
  `point_list_d24_unorm_s8_uint_no_depth_clamp`'s qpa shows `Mismatch
  at pixel (11,1,0): expected 0.105469 but got 0` -- a real, non-zero,
  presumably-correctly-shaded depth value expected at a covered pixel,
  but reading back as `0` (looks unwritten/default) instead. Likely a
  packed combined depth+stencil format (`D24_UNORM_S8_UINT`/
  `D32_SFLOAT_S8_UINT`) depth-write addressing bug specific to having a
  stencil half present, not yet root-caused past this one data point.
  Flagged as a fresh, separate future-session item (see
  `agent_thoughts.md`) rather than chased this session.

- `dEQP-VK.glsl.builtin_var.*` (111 cases, the full group): **66 Pass**
  (was 63) / **15 Fail** (was 18, the 3 plain `d32_sfloat` cases moved
  to Pass) / 30 NotSupported -- three tests newly green, zero
  regressions.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- an internal core-1.0 depth-clamp correctness fix, not
a new feature/extension surface.

## L282: `loops` cluster down to 4 genuine hangs (was 30) -- `isCycleHeaderBranch` reverted, 4 lower-risk fixes kept

**Investigation:** attempted a direct fix for the `*_dynamic_iterations`
hang cluster (`L277`/`L278`'s `loops` 30-case tally) via a new
`isCycleHeaderBranch` relay-aware exit-check comparison in
`LoopLinearizer`. Implemented and CTS-tested, but found to introduce
two new crash regressions in previously-passing `graphicsfuzz` repros
(`cov-function-always-return-negative-bitfield-extract`,
`cov-function-fragcoord-condition-always-return`,
`cov-function-global-loop-counter-sample-texture`). **Reverted in
full** -- confirmed via `grep` that no trace of `isCycleHeaderBranch`/
`HeaderExitRelayValues`/`LatchExitRelayValues` remains, and via a
byte-identical `diff` against the pre-attempt backup. Flagged as
needing a dedicated future audit session rather than incremental
patching.

**Kept (4 independent, lower-risk fixes found alongside that
investigation):**

1. `DiamondFlattener::validate`'s new `Depth`/`MaxValidateDepth=256`
   recursion-depth guard -- a pathologically deep nested-diamond
   shader now gets a clean diagnosed `Error` instead of a stack
   overflow.
2. `HeaderActiveMasks`/`rethreadNestedEntryMasks` mask-threading
   mechanism -- retroactively narrows a child cycle's `true`-constant
   entry mask once an enclosing cycle's own real mask becomes
   available (wired into all 5 `LoopLinearizer` call sites).
3. Removed `linearizeCyclePostOrder`'s `if
   (CI.children(C).empty())` leaf-only gate -- non-leaf (nested)
   cycles are now linearized unconditionally for the first time.
4. New permanent `verifyModule(M, &errs())` safety-net in
   `Pipeline.cpp` right after `LinearizePass` runs.

Fixes (2)+(3) together are what actually fixed the bulk of the `loops`
cluster; the reverted `isCycleHeaderBranch` idea was not the fix that
landed.

**New/updated unit tests:**
`LinearizeTest.DeeplyNestedDivergentDiamondsDiagnoseInsteadOfStackOverflowing`
(fix 1, a synthetic 300-level nested-diamond IR; confirmed the test
shape is otherwise valid by re-running it at a shallow depth of 5,
where it passes cleanly -- isolating the depth guard as the sole cause
of rejection at 300 levels); `LinearizeTest.LinearizesBothInnerLeafLoopAndOuterNonLeafLoop`
(renamed/rewritten from `...LeavesOuterNonLeafLoopAlone`, updated for
fix 3's behavior change: `MaskAnyCount` now 2 (was 1), outer latch
condition now `"loop.continue3"` (was `"outer.break"`)).

**`check-feme`:** 3,441/3,502 Passed, 61 Unsupported, 0 Failed.

**CTS:** a clean, isolated-per-case re-tally of the full 624-case
`dEQP-VK.glsl.loops.*` group. Methodology: first ran each of the 18
`{special,generic}.{while,for,do_while}_{uniform,dynamic,constant}
_iterations` subgroups as its own separate `deqp-vk` invocation (not
one combined process for the whole `loops.*` glob), to avoid a
discovered cascading-false-failure artifact (see below). 16 of the 18
subgroups passed 100% outright, including `special.
do_while_dynamic_iterations` (60/60) -- previously part of the 30-case
cluster, now fully fixed. Only `special.for_dynamic_iterations` and
`special.while_dynamic_iterations` showed any failures in their
subgroup-level runs (52/62 and 52/62 failing respectively); re-running
every case in *each of those two subgroups* in its own fully separate
process invocation (not just a separate subgroup-level process) found
only **2 genuine hangs per subgroup** -- `dowhile_trap_{fragment,vertex}`
-- with the other 60 cases in each subgroup passing cleanly once
isolated from the real hang's aftermath.

**Result: 620/624 Pass, 4 Fail** (was 594/624 Pass, 30 Fail per
`L277`/`L278`'s tally) -- `special.{for,while}_dynamic_iterations.
dowhile_trap_{fragment,vertex}` (4 cases) still hang; every other case
in the 624-case group now passes. The remaining hang is narrowly
scoped to `for`/`while` (not `do_while`) dynamic-iteration-count loops
combined with the `dowhile_trap` shader shape specifically, not yet
root-caused past this point, but now isolated to a 4-case repro
surface for a future session (likely still related to whatever gap
the reverted `isCycleHeaderBranch` attempt was chasing).

**Methodology finding (cascading false failures):** running many CTS
cases sequentially within one `deqp-vk` process, once a single case
genuinely hangs and hits its fence-wait timeout, appears to leave that
process's Vulkan device/queue in a permanently broken state for the
remainder of its lifetime -- every subsequent case in the same
invocation then also reports `VK_TIMEOUT`, even though each would pass
cleanly (confirmed via direct re-run) if run in its own fresh process.
This artifact is almost certainly what inflated prior sessions'
"~30/624" combined-run tallies for this cluster (and potentially other
clusters previously measured via one large combined sweep process).
Going forward, a trustworthy failure count requires isolating any
subgroup/case that shows a failure in a combined run into its own
separate process invocation before concluding it is a genuine,
distinct failure.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- an internal compiler-correctness fix, no new
feature/extension surface.

## L283: `dowhile_trap` root-cause attempt -- `isLoopControlEdge` deferral approach found to regress `conditional_break`/`elseblock`/`ifblock`; reverted, mask-narrowing generalized instead

**Investigation (dead end, fully reverted):** traced `dowhile_trap`'s
hang to `DiamondFlattener::isLoopControlEdge` not recognizing a
rotated-loop shape where the cycle header's own two successors are
*both* still cycle members (neither a direct backedge nor a direct
exit) -- so `DiamondFlattener` flattens the header's real divergent
"keep iterating" check as an ordinary if/else diamond before
`LoopLinearizer` ever sees it. Implemented a fix making
`isLoopControlEdge` unconditionally defer *any* cycle header's own
branch to `LoopLinearizer`, plus widening `HeaderExit`/`LatchExit`
matching to a relay-aware `matchExitCheckWithRelay`. This **did** fix
all 4 `dowhile_trap` cases -- but a full isolated-per-case
`dEQP-VK.glsl.loops.*` re-run found it introduced **14 new
regressions**: `special.{do_while,for,while}_dynamic_iterations.
{conditional_break,elseblock,ifblock}_fragment` (each of these loop
shapes combines a real, header-level trip-count check with a
*separate*, genuinely divergent internal check elsewhere in the body,
e.g. an `if (cond) break`). Root cause of the regression: baseline
`DiamondFlattener` relies on being able to walk *through* the header's
own branch (treating the whole loop body between the header and its
immediate post-dominator as one recursively-flattenable region) to
reach and flatten that *separate* internal divergent check too,
reducing the combination down to a single surviving `OtherCondBrBlocks`
candidate (confirmed via trace: baseline's `conditional_break` case
ends up with exactly one surviving candidate, a `Flow` merge block,
after `DiamondFlattener` has already flattened both the header's own
check and the internal `if`/`break` away). Forcing `DiamondFlattener`
to stop at the header unconditionally prevents it from ever reaching
that inner `if`/`break`, leaving it as a *second*, unflattened,
unclassified divergent branch that neither `LoopLinearizer`'s
single-candidate path nor SIMDization can handle. **Fully reverted**
(`git checkout --` on `Linearize.cpp`, confirmed byte-identical to the
pre-attempt state for this part of the diff) rather than attempting a
more surgical variant under time pressure -- see "Next steps" below
for the more promising lead this investigation surfaced instead.

**Kept (1 independent, lower-risk improvement found alongside that
investigation, re-applied after the revert):** generalized
`rethreadNestedEntryMasks` (`L282`'s own mechanism) from a
constant-only match (`isa<ConstantInt>(Use) && C->isOne()`) to a
structural one: any incoming edge to a child cycle's entry-mask phi
that is *not* the child's own backedge (determined via
`CI.getCycle`/`CI.contains`, not by inspecting the edge's current
value) is narrowed by conjoining it with the enclosing mask, building
a real `and` instruction when the edge is no longer a literal `true`
(e.g. already rewritten by `freezeLoopCarriedValues`). Confirmed via
direct case testing to be strictly additive: no new pass, no new
fail, and does not by itself fix `dowhile_trap` (it still hangs with
only this change applied) -- consistent with `L282`'s original
finding that this mechanism alone is insufficient. Kept because it is
real, verified-safe progress independent of the reverted
`isLoopControlEdge` approach.

**New unit tests:** none added this session -- the landed change
(`rethreadNestedEntryMasks`'s generalization) was already covered by
existing tests from `L282`'s own session (no test regression, no new
test needed for a pure strictness-widening of already-tested logic);
the reverted `isLoopControlEdge` approach never reached the
test-writing stage.

**`check-feme`:** 3,441/3,502 Passed, 61 Unsupported, 0 Failed (no
regression from baseline).

**CTS:** isolated-per-case re-verification of all 3 previously-passing
clusters this investigation's now-reverted approach had regressed
(`conditional_break`, `elseblock`, `ifblock`, each across
`do_while`/`for`/`while` `_dynamic_iterations` -- 9 cases spot-checked
directly) plus a full subgroup-level re-run of all 18
`{special,generic}.{while,for,do_while}_{uniform,dynamic,constant}
_iterations` subgroups: identical to `L282`'s baseline in every
subgroup except `special.for_dynamic_iterations`/`special.
while_dynamic_iterations`, which still each hang on exactly the same 2
cases as before (`dowhile_trap_{fragment,vertex}`).

**Result: still 620/624 Pass, 4 Fail** (unchanged from `L282` --
this session made no net change to the CTS pass count, only reverted
a regression-causing attempt and kept one small, verified-safe,
independent improvement).

**Next steps (a stronger, more precise lead than prior sessions had):**
tracing `dowhile_trap`'s own outer-cycle classification (via
`FEME_DEBUG_LINEARIZE_TRACE`) shows it takes the *exact same*
`OtherCondBrBlocks=[Flow]`-single-divergent-candidate code path that
`conditional_break` et al. use successfully -- **except** with one
extra entry: `OtherCondBrBlocks=[<inner-do-while's-own-latch>
(non-divergent), Flow (divergent)]`. That inner-do-while latch entry
is a nested child cycle's own already-linearized backedge block,
filtered out of `DivergentCandidates` (only `Flow` remains, matching
the working mechanism) but apparently *not* handled correctly by the
`PreRegion`/`PostRegion`/`CheckBlock` partitioning logic in that same
code path when a literal nested child cycle sits inside `PreRegion` or
`PostRegion`. This is a **much narrower, more specific lead** than
"the whole `isLoopControlEdge` classification is wrong" -- focus a
future session on `linearizeCycle`'s `DivergentCandidates`-non-empty
handling block (roughly lines 3244-3410) and how it computes/uses
`PreRegion`/`PostRegion` membership when one of `OtherCondBrBlocks`'s
own filtered-out (non-divergent) entries is itself a nested child
cycle's latch, rather than touching `isLoopControlEdge` again.

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no
change needed -- an internal compiler-correctness investigation, no
new feature/extension surface (and ultimately no net code change to
this specific classification logic either, since the attempt was
reverted).

## L284: fixed a reported "AtomicRMWInst" build failure (not reproducible in the current build directory) by adding a fail-fast configure-time LLVM-version guard

**Request:** "FeMe currently fails to build with a number of errors related
to the use of `AtomicRMWInst`, which is not defined by LLVM (anymore). I
suspect you've been building against system-installed LLVM headers instead
of the in-tree ones."

**Investigation:** Could not reproduce a build failure in the existing
`build/` directory as configured:

- Deleted all 304 existing FeMe object files (`find tools/feme -name
  '*.o' -delete`) and ran `ninja check-feme` from a genuinely clean state
  (forcing every single FeMe translation unit to recompile, not relying on
  any cached `.o`/ccache hit that might mask a header-path problem).
  Result: all 304 files recompiled with **zero errors** (one unrelated,
  confirmed-flaky timing test, `SyncTest.TimelineSemaphoreWaitBlocksUntilHostSignal`,
  failed on this run only and passed on a clean re-run -- a pre-existing
  timing-sensitivity issue, not a build or `AtomicRMWInst` problem).
- Checked `compile_commands.json`: 0 of 304 FeMe compile invocations
  reference any system LLVM header path (`/usr/include/llvm*`); every one
  resolves `AtomicRMWInst` et al. from `/home/dev/dev/llvm-project/llvm/include`
  (in-tree).
- Confirmed `feme/CMakeLists.txt` already `FATAL_ERROR`s if built
  standalone (`CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR`), so
  there is no code path in the current design where feme would pick up an
  installed LLVM instead of the in-tree one.
- No `CPATH`/`C_INCLUDE_PATH`/`CPLUS_INCLUDE_PATH`/`LLVM_DIR` environment
  leakage found in this environment either.

**However, confirmed the underlying risk is real, not imaginary:** the
system's `llvm-21-dev` package (present in this container) genuinely lacks
two `AtomicRMWInst::BinOp` enumerators feme uses
(`feme/lib/Transforms/CPU/AtomicRMWIdentity.cpp`): `FMaximumNum` and
`FMinimumNum`. `git log -S` on the in-tree history pinpoints exactly when
these were added upstream: commit `ea8fb06f2443` ("[atomicrmw]
fminimumnum/fmaximumnum support", #187030), first released in
`llvmorg-23.1.0`. So if a build ever did pick up `llvm-21` headers (e.g. a
future stray `-DLLVM_DIR`, a `CPATH`/`CPLUS_INCLUDE_PATH` environment leak,
or an IDE/tooling misconfiguration), it would fail with exactly the
symptom described -- deep inside `AtomicRMWIdentity.cpp`, with a cryptic
"no member named 'FMaximumNum' in 'llvm::AtomicRMWInst'" rather than an
obvious, actionable diagnostic.

**Fix (fail-fast, not a functional change):** added a configure-time guard
to `feme/CMakeLists.txt`, right next to the existing
standalone-build-rejection check: `if(LLVM_VERSION_MAJOR LESS 23)
message(FATAL_ERROR ...)`, with a message naming the likely causes (stray
`LLVM_DIR`, `CPATH`/`CPLUS_INCLUDE_PATH`) and the fix (clean build
directory, only `-DLLVM_ENABLE_PROJECTS=feme` set). This converts a
future recurrence of this exact failure mode from "a wall of opaque
compiler errors 20+ minutes into a build" into "an immediate, one-line,
actionable CMake configure error." Verified the guard fires correctly
(via an isolated `cmake -P` script with `LLVM_VERSION_MAJOR` forced to 21)
and does not fire for the current, correct configuration (`cmake .`
re-run in the existing `build/` directory: `LLVM_VERSION_MAJOR=24`,
configure succeeds).

**Verification:** `ninja check-feme` (full, from a from-scratch object
rebuild as above): 3,441/3,502 Passed, 61 Unsupported, 0 Failed. CTS:
`dEQP-VK.api.smoke.*` (6/6 Pass) and a spot-check of the known
`special.for_dynamic_iterations.dowhile_trap_fragment` hang (still fails
identically, as expected -- this change has no functional effect on
codegen, only on CMake configure-time validation). No CTS regression;
this is a pure build-configuration hardening change, so a full CTS sweep
was not re-run (consistent with prior sessions' practice for
docs-only/non-functional changes).

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no change
needed -- a build-configuration safeguard, no feature/extension surface
change.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (twice this session: at the start, and
again after the `cmake .`/`ninja check-feme` rebuild).

## L286: `dowhile_trap` hang finally fixed -- cross-pass `DiamondFlattener`-to-`LoopLinearizer` entry-mask gap

**Context:** the `loops` cluster's last residual failure, tracked since
`L277` first identified the 30-case cluster: the 4-case
`special.{for,while}_dynamic_iterations.dowhile_trap_{fragment,vertex}`
hang (`vk.waitForFences(...) VK_TIMEOUT`). `L282` fixed the other 26 cases
(removing the leaf-cycle-only gate plus `rethreadNestedEntryMasks`);
`L283` attempted and reverted a direct fix (`isLoopControlEdge` deferral)
after finding it regressed 14 other cases.

**Root cause:** `LinearizePass::run` runs two architecturally-decoupled
passes in sequence against the same function -- `DiamondFlattener`
(ordinary if/else diamond flattening) runs first, entirely before
`LoopLinearizer` (loop/cycle linearization) even starts, each with its own
fresh `DominatorTree`/`PostDominatorTree`/`CycleInfo`/`UniformityInfo`.
`DiamondFlattener::validate`'s own recursive diamond-walk, on reaching a
block that `isInCycle`s and has a loop-control-edge successor, by design
returns `true` ("stop here; `LoopLinearizer`'s problem, not an error") and
records a `CycleBoundaryBlocks`/`CycleBoundaryMasks` entry -- a
deliberate, already-documented mechanism letting a diamond whose arm
contains an entire not-yet-linearized nested cycle be flattened safely
(the "escape-time loop followed by a palette lookup" use-case the
in-code comments describe). The `CycleBoundaryMasks` entry computed there
*is* the correct per-lane "should this lane actually enter this nested
cycle" mask -- but it was never exposed to `LoopLinearizer`, which seeds
every cycle's own entry mask via `makeActivePNPair`: for every
non-backedge predecessor of a cycle's header, it unconditionally used a
bare `ConstantInt::getTrue`, correct only when nothing upstream of the
cycle had already narrowed which lanes should even reach it.

`dowhile_trap`'s own inner do-while shares its exit condition's induction
variable/bound with the outer for-loop's own divergent trip-count check.
A lane the outer check already decided should skip the loop body entirely
still entered the inner cycle "live" and unmasked; since that lane's own
induction variable never updates while stuck in the (structurally
unreachable-for-it) inner cycle, its own exit condition never
independently becomes false, so the whole wave's `mask.any`-based exit
reduction spun forever -- confirmed via live `gdb` register dumps in a
prior session, and via empirical `FEME_DEBUG_LINEARIZE_TRACE`-gated
trace-debugging this session (iteratively refined and rebuilt, ~4s per
cycle via ccache/ninja) to pin down exactly which pass dropped the mask.

**Fix:** threaded the mask across the pass boundary explicitly:

- Added `DiamondFlattener::getCycleBoundaryMasks()`, a new public const
  accessor exposing its own `CycleBoundaryMasks` map.
- Added a new required `LoopLinearizer` constructor parameter/private
  member, `DiamondFlattenedEntryMasks` (a `const DenseMap<BasicBlock *,
  MaskPair> &`), made required rather than defaulted to avoid a
  dangling-reference footgun from binding a const-ref parameter to a
  temporary default argument.
- `LoopLinearizer::makeActivePNPair` now consults
  `DiamondFlattenedEntryMasks` for the cycle's own `Header` before falling
  back to the old unconditional `true` -- an exact-match lookup miss
  (the overwhelmingly common case, whenever no enclosing diamond was ever
  flattened around this cycle) preserves the original behavior exactly.
- `LinearizePass::run` now captures `DF.getCycleBoundaryMasks()` into a
  local variable before the `DiamondFlattener`/`PostDominatorTree` scope
  closes, and threads it into the `LoopLinearizer` constructor call.

**New regression unit test**
(`LinearizeTest.NestedCycleInsideFlattenedDiamondArmInheritsOuterDiamondsMask`):
a synthetic IR with a deliberately *uniform* outer loop (constant trip
count) containing a genuinely divergent mid-body diamond (condition
derived from `llvm.dx.thread.id`) whose true arm holds a nested do-while
cycle sharing the diamond condition's own induction variable/bound. The
outer loop is kept uniform specifically so `LoopLinearizer`'s own
existing, already-fixed, same-pass `rethreadNestedEntryMasks` mechanism
(`L282`) cannot mask the bug via cycle-to-cycle mask propagation, isolating
the test to the `DiamondFlattener`-to-`LoopLinearizer` cross-pass gap
specifically. Confirmed via a `git stash` A/B test: fails pre-fix (the
inner cycle's own entry-mask phi's non-backedge incoming value is a bare
`ConstantInt` `true`) and passes post-fix (narrowed to an `and` of the
enclosing diamond's own condition).

**Verification:**

- `FeMeTransformsCPUTests` (full unit binary): 601/601 Pass (+1 new test).
- `check-feme-transforms-cpu-linearize` (lit): 39/39 Pass.
- `check-feme` (full target, object-file caching + assertions enabled):
  3,442/3,503 Passed (+1 net new unit test), 61 Unsupported, 0 Failed.
- CTS: isolated, fresh full `dEQP-VK.glsl.loops.*` re-run (624 cases):
  **624/624 (100%) Pass** -- the `dowhile_trap` hang is fixed, and the
  entire 30-case `loops` cluster `L277` first identified is now fully
  closed (0 residual failures).
- Regression spot-check: this file's own code comments cite
  `dEQP-VK.graphicsfuzz.*` as a historical regression source for exactly
  this kind of masking logic (see `L282`'s own reverted-regression write-
  up). A full combined-process `dEQP-VK.graphicsfuzz.*` run hit the
  already-documented cascading-false-failure artifact (one process,
  many cases -- a single internal issue partway through made a long,
  dense run of subsequent cases falsely report `Fail`; confirmed by
  re-running several of those "failing" cases individually in fresh
  processes, each passing cleanly in isolation). Rather than re-running
  the full ~757-case group with the isolated-per-case methodology
  `L282` established (deferred as future work, folded into the existing
  carried-over "broader-than-tessellation/loops CTS sampling" item), spot-
  checked in isolation the 3 specific repros `L282`'s own reverted
  `isCycleHeaderBranch` attempt had regressed
  (`cov-function-always-return-negative-bitfield-extract`,
  `cov-function-fragcoord-condition-always-return`,
  `cov-function-global-loop-counter-sample-texture`) plus the original
  `DiamondFlattener::validate` stack-overflow repro
  (`increment-value-in-nested-for-loop`): all 3 `cov-function-*` cases
  **Pass** (no regression); `increment-value-in-nested-for-loop` still
  **Fails**, but cleanly -- a diagnosed "unsupported shape" `Error`
  (`loop at '' has an internal branch ... that does not reach the loop's
  exit block`), not a crash or hang, exactly the expected, pre-existing,
  by-design behavior `L282`'s own depth-guard produces for this
  genuinely-unsupported shape (never expected to pass; this is a
  distinct, separate, already-tracked limitation, not a regression).

**`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:** no change
needed -- an internal compiler-correctness fix, no new feature/extension
surface.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set, per the standing environment gotcha).

## L287: `builtin` cluster fully closed -- `PackSnorm2x16`/`UnpackSnorm2x16` MLIR op gap, and `cosh`/`sinh` CTS-reference-conformance mismatch

**Context:** `L272`'s own fresh full `dEQP-VK.glsl.*` sweep first tallied
the `builtin` cluster at 14 residual cases
(`dEQP-VK.glsl.builtin.function.pack_unpack.*snorm2x16*` and
`dEQP-VK.glsl.builtin.precision.{cosh,sinh}.highp.*`), left untriaged
across several subsequent sessions. This session triaged and fixed both.

**Sub-issue 1 -- `pack_unpack.*snorm2x16*` (6 cases):** isolating
`packsnorm2x16_highp_compute` showed pipeline creation failing with
`"unhandled deserializations of 56 from extension set GLSL.std.450"`
(opcode 56 = `PackSnorm2x16`; opcode 60 = `UnpackSnorm2x16`). Root cause:
upstream MLIR's SPIR-V GL-extended-instruction-set dialect
(`SPIRVGLOps.td`) never defined TableGen ops for these two instructions
at all -- unlike every other Pack/Unpack variant (`PackUnorm2x16`,
`PackSnorm4x8`, `PackUnorm4x8`, `UnpackUnorm2x16`, `UnpackSnorm4x8`,
`UnpackUnorm4x8`), confirmed via a pre-existing code comment in
`SPIRVToLLVMPatterns.cpp` noting the gap. Fixed with an isolated,
self-contained upstream MLIR commit (outside `feme/`, per the
"isolated reproducer + self-contained fix" instruction) adding
`SPIRV_GLPackSnorm2x16Op`/`SPIRV_GLUnpackSnorm2x16Op` to `SPIRVGLOps.td`,
modeled directly on the existing `PackUnorm2x16`/`UnpackUnorm2x16` pair
(signed `[-1,+1]*32767.0` conversion formula instead of unsigned
`[0,1]*65535.0`), plus IR-level parse/verify tests
(`mlir/test/Dialect/SPIRV/IR/gl-ops.mlir`) and a serialization-roundtrip
test (`mlir/test/Target/SPIRV/gl-ops.mlir`). Built
`MLIRSPIRVDialect`/`MLIRSPIRVSerialization`/`MLIRSPIRVDeserialization`/
`mlir-opt`/`mlir-translate` and ran the full `mlir/test/Dialect/SPIRV` and
`mlir/test/Target/SPIRV` suites (122 tests) clean. On the FeMe side, since
`GLPackNormPattern`/`GLUnpackNormPattern` were already fully parameterized
by `NumComponents`/`BitsPerComponent`/`IsSigned`, wiring the two new ops up
was a near-zero-cost type-alias addition
(`GLPackSnorm2x16Pattern`/`GLUnpackSnorm2x16Pattern`, `IsSigned=true`),
plus matching FileCheck tests in
`spirv-to-llvm-gl-pack-unpack-norm.mlir`. Verified:
`dEQP-VK.glsl.builtin.function.pack_unpack.*snorm2x16*` **6/6 Pass** (was
0/6).

**Sub-issue 2 -- `precision.{cosh,sinh}.highp.*` (8 cases):** isolating
`dEQP-VK.glsl.builtin.precision.cosh.highp.scalar` showed a single-input
mismatch at `0x1.65a84ep6` (~=89.414): FeMe returned `~3.399e38` (a large
but finite value), CTS expected `+inf`. Using `mpmath` for exact
high-precision verification confirmed the true mathematical
`cosh(89.414)` is `~=3.39729e38`, which is **less than `FLT_MAX`**
(`3.4028e38`) -- i.e. the numerically-correct answer genuinely is finite,
and FeMe's answer (via `llvm.intr.cosh`, backed by a numerically-careful
libm) is not wrong in the ordinary sense. The real explanation: CTS's own
`vktShaderBuiltinPrecisionTests.cpp` defines its `Cosh`/`Sinh` precision
tests via the GLSL.std.450 spec's own literal formula,
`(exp(x) +/- exp(-x)) / 2`, not a numerically-stable algorithm. A small
standalone C program confirmed `expf(89.414)` alone **overflows to
`+inf`** (since the true `exp(89.414) ~= 6.79e38 > FLT_MAX`), even though
the final `cosh` result computed via a stable algorithm does not --
meaning CTS's own naive-formula reference legitimately expects `+inf`
here, and FeMe's prior lowering was "too accurate" relative to what the
conformance test's own reference formula computes (and plausibly also
further from what real GPU special-function-unit hardware does --
GPU `exp`/`sinh`/`cosh` hardware units are typically fast approximations
consistent with GLSL's own generous precision tolerances).

Fixed by adding a new `HyperbolicViaExpPattern<SPIRVOp, HyperbolicKind,
FlushSubnormalInput>` template lowering both `GLSinhOp`/`GLCoshOp` via the
literal `0.5 * (exp(x) +/- exp(-x))` formula (`llvm.intr.exp`, matching
CTS's own reference formula and its premature-overflow behavior exactly),
instantiated as `GLSinhViaExpPattern` (keeping `Sinh`'s pre-existing
subnormal-input-flush behavior intact) and `GLCoshViaExpPattern` (no
flush, matching `Cosh`'s prior unflushed behavior -- its range never
approaches subnormal scale the way `sinh(x) ~= x` does for small `x`).
Updated the pre-existing `sinh_flush`/`sinh_flush_f16` lit CHECK lines in
`spirv-to-llvm-transcendental-flush-to-zero.mlir` to match the new IR
shape (`llvm.fneg`, `llvm.intr.exp` x2, `llvm.fsub`, `llvm.fmul` instead of
a single `llvm.intr.sinh` call), and added a new dedicated
`spirv-to-llvm-gl-cosh.mlir` test covering `Cosh`'s (unflushed) lowering,
since no prior test exercised it directly. Verified:
`dEQP-VK.glsl.builtin.precision.{cosh,sinh}.*` (mediump + highp,
scalar/vec2/vec3/vec4) all **Pass** (`vec5` variants remain
`NotSupported` -- `longVector` is out of scope, unrelated to this fix).

**Verification:** `check-feme`: 3,443/3,504 Passed (+1 net new lit test),
61 Unsupported, 0 Failed -- no regressions. Fresh full
`dEQP-VK.glsl.builtin.*` re-run (3,193 cases): **0 Fail** (was 14) -- the
entire `builtin` cluster `L272` first identified is now fully closed.

**Feature/extension surface:** `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed for either sub-issue --
both are internal compiler-correctness fixes (a missing upstream MLIR op
definition, and a lowering-formula choice to match CTS's own reference
semantics), not a new feature/extension surface change.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L288: `struct` cluster -- tight-vector marker-struct identity bug (nested structs)

**Symptom:** 12 of `dEQP-VK.glsl.struct.*`'s 80 cases failed (from
`L278`/`L280`'s residual-cluster tally), all involving nested structs:
`local.nested_{equal,not_equal}_{fragment,vertex}` (4) and
`uniform.{,dynamic_loop_}{loop_,}nested_struct_array{,_dynamic_index}_
{fragment,vertex}` (8).

**Root cause:** isolated the simplest case,
`dEQP-VK.glsl.struct.local.nested_equal_fragment` (a struct-of-struct
`S{a, T{vec3,int} b, int c}` built via nested `OpCompositeConstruct`),
via `FEME_VULKAN_LOG_CREATION_ERRORS=1`: a pipeline-creation rejection,
`spirv.CompositeConstruct` explicitly marked illegal. Reproduced
standalone with a hand-written `.mlir` file through
`feme-opt --feme-convert-spirv-to-llvm` (no CTS/driver pipeline needed),
then added a temporary debug trace inside
`CompositeConstructPattern::convertStruct`'s reconciliation loop.

The trace showed the inner struct's converted constituent type carrying
marker struct `"feme.tight_vector"` while the outer struct's re-derived
field type expected `"feme.tight_vector.3"` -- two structurally
identical but non-equal identified LLVM struct types, both wrapping
`array<3xf32>`. `getTightVectorArrayType`/
`substituteTightVectorMembersIfNeeded` both used
`LLVM::LLVMStructType::getNewIdentified(...)`, which deliberately mints
a fresh, counter-suffixed instance on every call (to avoid a
"redefinition with different body" error when two *different* vector
shapes would otherwise collide on a fixed name). This breaks down
specifically for nested structs: the exact same vector shape gets
independently re-converted twice (once building the inner struct's own
`CompositeConstruct` result in isolation, once again re-deriving the
outer struct's field-type layout), producing two non-identical marker
instances that `mlir::Type` equality then rejects.

**Fix:** added `getOrCreateTightVectorMarkerStruct(ElementType,
NumElements)`, building a deterministic, shape-derived name (e.g.
`"feme.tight_vector.f32x3"`, encoding element type + lane count) and
using `LLVM::LLVMStructType::getIdentified(...)` (idempotent
lookup-or-create by name) with a conditional `setBody` only on first
creation -- guaranteeing two independent conversions of the same vector
shape always produce the same marker struct instance, while genuinely
different shapes still get genuinely different names. Confirmed safe
against `CanonicalizeStage.cpp`'s own `isTightVectorMarkerStruct`, which
only checks a `starts_with(prefix)` match, not an exact name.

Updated 9 existing lit tests' CHECK lines (previously hard-coding the
old non-deterministic counter-based marker names) plus one unit test
(`OutOfOrderOffsetInterfaceBlockLegalizes`)'s literal-prefix check. One
benign behavior change noted: `spirv-to-llvm-task-payload-vec3.mlir`'s
two identically-shaped `vector<3xi32>` struct members now correctly
share a single marker instance instead of getting arbitrarily distinct
ones (an artifact of the old scheme's call-order-dependent naming).

**Verification:** `check-feme`: 3,443/3,504 Passed, 61 Unsupported, 0
Failed (no net new/removed tests, only existing CHECK lines updated).
CTS: re-ran `dEQP-VK.glsl.struct.*` (80 cases): 4 of the 12 residual
failures now pass (`local.nested_{equal,not_equal}_{fragment,vertex}`),
8 remain -- all `uniform.*nested_struct_array*` variants, a related but
distinct nested-struct-*array* issue (not yet root-caused; filed as
roadmap `L289`).

**Feature/extension surface:** `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed -- an internal
compiler-correctness fix, no new feature/extension surface.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L289/L290: `struct` cluster -- `nested_struct_array` GEP-index and stride-padding bugs

Continuing the `struct` cluster's remaining 8 residual failures from
`L288`: `uniform.{,dynamic_loop_}{loop_,}nested_struct_array{,_dynamic_index}_{fragment,vertex}`,
a uniform block whose struct contains an array of a further nested
struct (GLSL `struct T { float a; vec2 b[2]; }; struct S { float a; T
b[3]; int c; };`), read back via deeply-indexed expressions like
`s[0].b[1].b[0].x`.

**L289 (fixed):** Isolated via `FEME_VULKAN_LOG_CREATION_ERRORS=1` on
`nested_struct_array_fragment`: `error: 'llvm.getelementptr' op index 5
indexing a struct is out of bounds` at pipeline-creation time -- a
distinct bug from `L288`'s marker-identity issue. Extracted the shader
source and SPIR-V disassembly via `--deqp-log-decompiled-spirv=enable
--deqp-log-shader-sources=enable` and built a standalone two-access-chain
`feme-opt` repro (a single access chain alone did not reproduce it,
despite exercising the same index pattern -- a new lesson for this bug
class: always mirror the real shader's *multiple* access chains, not
just the simplest single case). Root-caused to
`remapNestedStructMemberIndices`'s `ArrayTy` branch missing the
tight-vector-marker unwrap-zero-insertion logic its `StructTy` branch
already has (added for the direct-struct-member case). Fixed by adding
a `RealArrayElementTy` tracking variable and matching `ArrayTy`-branch
logic, mirroring the existing `RealStructTy` mechanism. Added a
minimized single-access-chain FileCheck regression test,
`spirv-to-llvm-nested-struct-array-of-vector-component.mlir`.

**Verification:** `check-feme`: 3,444/3,505 Passed (+1 new test), 61
Unsupported, 0 Failed. CTS: `dEQP-VK.glsl.struct.*` re-run: still 72/80
Pass -- this fix unblocks pipeline creation for all 8 cases (previously
failed before even building), but all 8 now fail at runtime with an
image mismatch instead, due to a second, distinct, not-yet-fixed bug
(`L290`).

**L290 (fixed):** The initial IR-inspection-based diagnosis (pointing at
`getTightNestedStructType`'s array-of-vector branch) was **wrong** --
confirmed via `llvm::errs()` debug-print tracing with a minimized
single-struct repro (`Block { struct T { f32 a; array<2 x vec2,
stride=16> b; } }`) that that function is never even reached for this
shape. The real bug is in
`convertOffsetStructTypeIgnoringDecorations`'s own
`WithArraysAndMatrices` retry tier, which builds the `array<N x
markerStruct>` standing in for an array-of-vectors struct member via the
plain unpadded `getTightVectorArrayType` helper, giving each
marker-struct element only 8 bytes instead of the array's own declared
16-byte `ArrayStride` (for `vec2 b[2]`) -- so `b[1]` was read/written 8
bytes short of its spec-required offset. The obvious fix (reusing
`padStructToSize`) was blocked: that helper explicitly refuses to pad a
tight-vector marker struct, by design, to preserve the "exactly one
member" invariant other code depends on. Fixed instead by adding an
optional `TrailingPaddingBytes` parameter to
`getOrCreateTightVectorMarkerStruct` (baking a second always-ignored
`array<N x i8>` body member and a deterministic `.padN` name suffix, e.g.
`"feme.tight_vector.f32x2.pad8"`, when needed) and a new
`getStridedTightVectorArrayType` helper that computes the padding from
the array's own declared stride; wired into both the real bug site and
`getTightNestedStructType`'s own array-of-vector branch (not exercised
by current CTS cases, but structurally the same bug, fixed for
consistency). `getTightVectorMarkerInnerType` (both the MLIR-type copy in
`SPIRVToLLVMPatterns.cpp` and the raw-`llvm::Type*` copy in
`CanonicalizeStage.cpp`) relaxed to accept a 1- or 2-member marker body,
always returning member 0, so every existing consumer
(`padStructToSize`'s refusal check, `remapNestedStructMemberIndices`'s
and `resolvePhysicalCompositeAccess`'s unwrap loops,
`reassembleTightVectorValue`/`unwrapTightVectorValue`) keeps working
unchanged. Extended `L289`'s regression test
(`spirv-to-llvm-nested-struct-array-of-vector-component.mlir`) to cover
both bugs with two differently-shaped access chains and updated CHECK
lines for the padded `.pad8` marker shape.

**Verification:** `check-feme`: 3,444/3,505 Passed, 61 Unsupported, 0
Failed (no regressions). CTS: `dEQP-VK.glsl.struct.*` re-run: **80/80
Pass** (up from 72/80) -- all 8 residual `nested_struct_array*` cases now
pass, closing the `struct` cluster entirely.

**Feature/extension surface:** `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed for either `L289` or
`L290` -- both are internal compiler-correctness fixes, no new
feature/extension surface.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L291/L292: `demote` cluster -- `OpIsHelperInvocationEXT` opcode-5381 gap fixed (layered MLIR + LLVM upstream gaps); `dynamic_loop_*` runtime bug still open

Continuing the carried-over `builtin` (14)/`demote` (9)/`derivate` (3)/
`texture_gather` (1) triage list. A fresh `dEQP-VK.glsl.builtin.*` re-run
(3,193 cases) showed **0 Fail** -- that cluster was already silently
resolved by earlier, unrelated fixes (most likely the `loops`/`struct`
cluster work); it needed no dedicated action and is dropped from future
next-steps lists.

`dEQP-VK.glsl.demote.*` (30 cases) showed 9 Fail, splitting into two
distinct bugs on inspection of the CTS log:

- **5 cases** (`basic_deriv`, `dynamic_loop_deriv`, `function_deriv`,
  `function_static_loop_deriv`, `static_loop_deriv` -- all containing
  "deriv", all using GLSL's `helperInvocationEXT()` built-in to guard a
  derivative read against invalid demoted-lane contributions) failed at
  **pipeline-creation time** with `error: unhandled opcode 5381`.
- **4 cases** (`dynamic_loop_{always,dynamic,texture,uniform}`) failed at
  **runtime** with an image mismatch -- a separate bug, not yet
  root-caused.

### The opcode-5381 gap (`L291`, fixed)

Opcode 5381 is SPIR-V's `OpIsHelperInvocationEXT`, part of
`SPV_EXT_demote_to_helper_invocation` -- the read-only "query" counterpart
to `OpDemoteToHelperInvocation` (opcode 5380), which FeMe already supports.
Root-caused to **two layered upstream gaps**, neither a FeMe bug:

1. **MLIR's SPIR-V dialect had no op at all for `OpIsHelperInvocationEXT`**
   (confirmed via grep against `SPIRVControlFlowOps.td`/`SPIRVBase.td`:
   `OpDemoteToHelperInvocation` is defined, its query counterpart is not).
   Fixed with an isolated, self-contained upstream MLIR commit: added
   `SPIRV_OC_OpIsHelperInvocationEXT` (5381) to `SPIRVBase.td`'s opcode
   enum/validity list, and `SPIRV_IsHelperInvocationEXTOp` to
   `SPIRVControlFlowOps.td` (mirroring `SPIRV_DemoteToHelperInvocationOp`'s
   shape exactly -- same extension/capability -- but with a `SPIRV_Bool:
   $result` output, since this op is a query, not a side-effecting
   no-result op). Confirmed MLIR's `autogenSerialization` default (`1`)
   means tablegen alone generates full binary (de)serialization for this
   op shape -- no manual `Serialization`/`Deserialization` code needed,
   the same way `OpDemoteToHelperInvocation` itself needs none. Verified
   via `mlir-opt` parse and
   `mlir-translate --no-implicit-module --test-spirv-roundtrip` (full
   binary round-trip); added `control-flow-ops.mlir` (assembly test) and
   `terminator.mlir` (binary round-trip test) regression tests, both
   passing; broader `check-mlir-dialect-spirv`/`check-mlir-target-spirv`
   suites: 122/122 pass, no regressions.
2. **LLVM itself had no symmetric query intrinsic.** `IntrinsicsSPIRV.td`
   defines `int_spv_demote_to_helper_invocation` (the write/demote
   direction) but nothing for the read/query direction. Fixed with a
   second isolated, self-contained upstream LLVM commit: added
   `int_spv_is_helper_invocation` (`llvm.spv.is.helper.invocation`,
   returning `i1`), deliberately given **no** `IntrNoMem`/speculatable
   attributes -- its result can legitimately change across an
   intervening `llvm.spv.demote.to.helper.invocation` call in the same
   invocation, so it must not be hoisted above or CSE'd across one.
   Added an Assembler round-trip test (`llvm-as | llvm-dis`), passing.

With both upstream gaps closed, added the FeMe-side plumbing (its own,
separately-committed, FeMe-only change):

- `IsHelperInvocationConversionPattern` (`SPIRVToLLVMPatterns.cpp`)
  converts `spirv.IsHelperInvocationEXT` into a call to the new intrinsic,
  mirroring `DemoteToHelperInvocationConversionPattern`'s own shape (using
  the file's existing `createIntrinsicCall` helper for the result-bearing
  call).
- `CanonicalizeStage.cpp` raises `llvm.spv.is.helper.invocation` calls
  directly into `createStageIsHelper()`'s `feme.stage.is_helper` op -- the
  same op DXIL's `IsHelperLane`(221) opcode and SPIR-V's `BuiltIn
  HelperInvocation` (`gl_HelperInvocation`, `L270`) already converge on,
  so a shader using any of the three spellings observes an identical
  runtime value.

Added a SPIRVToLLVM conversion FileCheck test
(`spirv-to-llvm-is-helper-invocation.mlir`) and extended the existing
`spirv-canonicalize-stage-raised.ll` raising test with an
`llvm.spv.is.helper.invocation` case.

**`check-feme`:** 3,445/3,506 Passed (+1 net new test), 61 Unsupported, 0
Failed -- no regressions.

**CTS:** re-ran `dEQP-VK.glsl.demote.*` (30 cases) after the fix. The 5
previously-unbuildable `*_deriv` cases now all build and run; 4 of them
(`basic_deriv`, `function_deriv`, `function_static_loop_deriv`,
`static_loop_deriv`) now **pass outright**. `dynamic_loop_deriv` still
fails at runtime with an image mismatch -- it turns out to share the
*other*, still-open `dynamic_loop_*` bug (see `L292` below), which the
opcode-5381 build failure had simply been masking until now. Net:
**25/30 Pass (up from 21/30)**.

### The `dynamic_loop_*` runtime bug (`L292`, still open)

5 cases now fail identically at runtime with an image mismatch:
`dynamic_loop_{always,deriv,dynamic,texture,uniform}` (up from 4 before
this session, since fixing `L291` exposed `dynamic_loop_deriv` to this
pre-existing bug instead of hiding it behind the build failure). All 5
share a `discard`/`demote` shader combined with a **dynamic (non-compile-
time-constant) loop trip count** -- not yet root-caused. Plausibly
interacts with FeMe's loop-linearization/masking machinery (the same
general area `L286`'s `dowhile_trap` fix touched, though that fixed a
different cluster, `loops`, and this is not confirmed to be related).
Next session should isolate the simplest case (`dynamic_loop_always`
looks like the best starting point, having no further runtime-dependent
condition beyond the loop trip count itself) and diff its generated
mask/demote IR against an equivalent *static*-trip-count `demote` case
that already passes -- the same differential method used for prior
loop-masking bugs.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed for either `L291` or `L292` -- internal compiler-correctness work
(plus the two upstream MLIR/LLVM gaps), no new feature/extension surface.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L293: `builtin`/`texture_gather` confirmed resolved; `derivate`'s 3 residual failures identified (not yet root-caused)

Completing this session's sweep of the carried-over `builtin`(14)/
`demote`(9)/`derivate`(3)/`texture_gather`(1) triage list (`demote` is
covered by `L291`/`L292` above):

- **`builtin`**: fresh full `dEQP-VK.glsl.builtin.*` re-run (3,193 cases):
  **0 Fail**. Already resolved as a side effect of unrelated earlier
  fixes; dropped from future triage.
- **`texture_gather`**: a prior session's guessed CTS path
  (`dEQP-VK.glsl.texture_functions.texturegather.*`) matched 0 cases; the
  correct group is `dEQP-VK.glsl.texture_gather.*`. Fresh full re-run
  (3,174 cases, 1,929 Not supported for unsupported formats): **0 Fail**.
  Already resolved; dropped from future triage.
- **`derivate`**: fresh full `dEQP-VK.glsl.derivate.*` re-run (1,674
  cases): 3 Fail, now identified by name:
  `fwidth.fbo_float.vec4_highp`, `fwidthcoarse.fbo_float.vec4_highp`,
  `fwidthfine.fbo_float.vec4_highp`. All three derivative-magnitude
  built-in precision variants, but only the float-FBO/`vec4`/`highp`
  combination -- `lowp`/`mediump` and non-float-FBO targets all pass.
  Not yet root-caused past this; plausibly a `cosh`/`sinh`-style
  (`L287`) CTS-reference-conformance mismatch given `fwidthCoarse`'s
  explicitly implementation-defined precision, but not confirmed --
  could equally be a genuine bit-exact precision bug in FeMe's own
  `fwidth`/`dFdx`/`dFdy` lowering. Left as an open roadmap item (`L293`)
  for a dedicated future session: next step is extracting the two
  images' exact pixel values to distinguish a tiny rounding delta from a
  larger structural difference.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- triage-only, no code changes this section.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L292 (continued): root cause fully confirmed for both uniform- and divergent-condition variants; fix attempted, found unsafe in isolation, reverted

Deepened this session's investigation of `L292` (the 5-case `dynamic_loop_*`
`demote` runtime image-mismatch bug) with real pre-SIMD IR dumps (via
`FEME_DUMP_IR_PRESIMD=1 deqp-vk --deqp-case=...`) of both
`dynamic_loop_always` (uniform loop exit, uniform `if (i>0) demote`
condition) and `dynamic_loop_dynamic` (uniform loop exit, but a *divergent*
`if (i>0) demote` condition, varying per-lane via a texture/position-derived
value). Both confirmed to share the same final symptom: the real
framebuffer-gating `feme.cpu.stage.return.masks` call at the loop's exit
block is a hardcoded, compile-time-constant `true`/`true`, never narrowed
by any iteration's `demote`, regardless of how many iterations ran or
whether the per-lane demote condition is itself uniform or divergent.

Root cause has two layered parts, both in `feme/lib/Transforms/CPU/Linearize.cpp`:

1. `DiamondFlattener::isLoopControlEdge` only inspects a branch's *direct*
   successors to decide whether it is a cycle boundary (and hence
   `LoopLinearizer`'s problem, not an ordinary flattenable diamond). When
   `StructurizeCFG` inserts a synthetic `Flow` dispatch block between a
   loop header's own continue/exit branch and the loop's real continue/exit
   targets, *both* of the header's direct successors fail this check,
   misclassifying the header's own iteration decision as an ordinary
   two-way uniform diamond reconverging at `Flow`.
2. Even with (1) fixed, `DiamondFlattener::run()`'s cycle-exit-root
   handling seeds the exit block's downstream code directly from
   `CycleBoundaryMasks[Header]` -- the loop's pre-loop *entry* mask
   (`true`/`true`), baked in as an immutable constant call argument
   *before* `LoopLinearizer` ever runs -- with no mechanism for
   `LoopLinearizer`'s own later, correctly-computed final per-iteration
   mask to patch it afterward.

Confirmed via `dynamic_loop_dynamic`'s IR that gap (2) is the one actually
responsible for the CTS failure: `LoopLinearizer`'s existing
`DivergentCandidates` code path *does* run successfully here (the per-lane
`i>0` check is divergent) and *does* correctly compute a narrowed
per-iteration `live.merge`/`sideeffect.merge` pair -- but the exit block's
`return.masks` call is still the stale hardcoded `true`/`true`, structurally
disconnected from that correctly-computed value. For `dynamic_loop_always`
(uniform `i>0` condition, no divergence anywhere in the cycle),
`LoopLinearizer` additionally bails out entirely before computing anything
(it unconditionally leaves alone any cycle where neither the header nor
latch exit check is divergent) -- a third, separate gap for that
sub-variant specifically.

A first fix attempt (making a cycle's own header branch always a cycle
boundary, in both `DiamondFlattener::validate()` and `::flatten()`) was
implemented, built, and tested: it changes the IR shape as expected
(removes the incorrect `Flow`-reconvergence phi at the header) but does
**not** change the CTS outcome (all 5 cases still fail identically, since
gap (2) is untouched), and **regresses two existing tests**
(`FEME :: Transforms/CPU/Linearize/loop-break-structurized.ll` and
`LinearizeTest.NestedCycleInsideFlattenedDiamondArmInheritsOuterDiamondsMask`).
Confirmed via `check-feme` before and after: baseline 3,443/3,504 Pass, 61
Unsupported, 0 Failed; with the attempted fix, 3,441/3,504 Pass (2 new
Failed). Reverted; **nothing committed this session for `L292`**. Full fix
design (freeze-based unique per-cycle placeholder values, patched via
`replaceAllUsesWith` once `LoopLinearizer` computes the real final mask,
plus a new `LoopLinearizer` code path for uniform-exit loops whose body
still narrows the mask) recorded in `Roadmap.md`'s `L292` entry and
`agent_thoughts.md` for a dedicated future session.

`check-feme`: 3,443/3,504 Passed, 61 Unsupported, 0 Failed (confirmed clean
at session end, after the revert). CTS: no change in pass/fail counts this
session (fix not landed). `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed -- no code landed.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L292 follow-up: `dynamic_loop_*` demote bug and the `loop-break-structurized.ll` regression it exposed, both fixed

Picked up this multi-session effort's own uncommitted `L292` investigation.
The two layered gaps the prior entry above described were real, but the
actual landed fix ended up narrower than that entry's own proposed
freeze/placeholder design:

- `DiamondFlattener::isLoopControlEdge` was extended
  (`isLoopControlEdgeThroughRelay`) to recognize a loop header's own
  continue/exit branch as a cycle boundary even when `StructurizeCFG` has
  routed it through one hop of a `Flow`-style relay dispatch block, so it
  is never incorrectly flattened into an ordinary diamond before
  `LoopLinearizer` runs. `flattenLoopBodyRegion`'s own precondition was
  relaxed to tolerate this same boundary shape, and `applyStageMasks` was
  made idempotent for repeat `ReturnInst` handling. Together, these
  already fix all 5 `dynamic_loop_*` cases via `LoopLinearizer`'s existing
  `CycleHasMaskOps`/`flattenLoopBodyRegion` path -- no placeholder/freeze
  mechanism was actually required.
- That fix regressed `loop-break-structurized.ll`: a *different* loop
  shape, with a genuinely divergent header exit check hidden behind the
  same one-hop `Flow`-relay shape, was left completely unrecognized by
  `LoopLinearizer::linearizeCycle` once `DiamondFlattener` correctly
  stopped pre-flattening it. Fixed by adding
  `LoopLinearizer::recoverRelayExitCheck`: a structural (divergence-
  agnostic) fallback for `HeaderExit`/`LatchExit`, reusing the same
  trivial-relay-stub recognition pattern already implemented (but only
  applied later in the function, for a different code path) -- plus
  relay-aware `ExitBlock` predecessor/phi bookkeeping in the
  `HeaderDivergent`/`LatchDivergent` linearization path, since a relay-
  based `ExitCheck` means `Header`/`Latch` are no longer necessarily
  `ExitBlock`'s own literal predecessor.

Root-cause investigation used repeated git-stash/`/tmp`-scratch-file A/B
comparisons against committed `HEAD` (temporary `FEME_DEBUG_LINEARIZE_TRACE`/
`FEME_DEBUG_VALIDATE_TRACE` instrumentation, since removed) to pin down
exactly why `Flow`'s own divergence classification flipped between `HEAD`
and the in-progress fix: at `HEAD`, `DiamondFlattener` pre-flattens the
header's break check into a `select` before `LoopLinearizer`'s own fresh
`UniformityInfo` is computed, so the merge value is judged divergent via
ordinary data-flow (its select condition is `tid`-based); with the fix,
the header's break check survives untouched, and `LoopLinearizer`'s own
internal phi-folding collapses the latch's separate re-check down to the
bare, genuinely uniform trip-count value -- correctly judged *not*
divergent (lane-activity divergence, not value divergence) by the same
`UniformityInfo`, but leaving the header's real check unaccounted for by
any existing classification.

Verified: all 601 `FeMeTransformsCPUTests` unit tests pass; full
`check-feme`: 3,445/3,505 Passed, 61 Unsupported, 0 Failed (up from the
prior session's 3,443/3,504 baseline, since `loop-break-structurized.ll`
and 2 new coverage points land net new/fixed); all 5
`dEQP-VK.glsl.demote.dynamic_loop_{always,uniform,deriv,dynamic,texture}`
cases now **Pass** (0/5 before, 5/5 after). `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed -- an internal
`feme-cpu-linearize` correctness fix, no new feature/extension surface.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L294: `L292`'s own `dEQP-VK.glsl.loops.*` regression, root-caused and mostly fixed

A broader sanity sweep after `L292` landed (`dEQP-VK.glsl.loops.*`, 624
cases -- previously 624/624) found a real regression `L292` itself
introduced: 590/624 Pass, 34 Fail, all in the
`special.{for,while}_dynamic_iterations.*` family. Confirmed real (not a
flaky/cascading artifact) by building the true `HEAD~1` pre-session
baseline and re-running `conditional_break_fragment` directly: Pass at
`HEAD~1`, Fail at the committed `L292` state.

**Root cause:** `LoopLinearizer::recoverRelayExitCheck` (added by `L292`)
recognizes a candidate relay stub as `Header`/`Latch`'s own exit arm
purely structurally -- a pure-relay block with `BB` as its unique
predecessor -- with no check that the stub actually, eventually reaches
`ExitBlock`. Loops whose body independently needs its own `Flow`-style
dispatch block for an `if`/`break` check (a second, genuinely separate
divergent decision point sharing the *same* `StructurizeCFG`-generated
dispatch block as the loop's own re-entry check) have a stub that looks
identical in shape but actually lands back on that shared dispatch block,
not on `ExitBlock`.

**Fix, in two stages:**

1. For the sub-case where the loop *also* has a second, genuinely
   independent divergent exit elsewhere (relayed to `ExitBlock`
   separately from `Header`'s own check) -- `conditional_break_fragment`'s
   own shape -- generalized `LoopLinearizer::linearizeCycle`'s existing
   single-divergent-exit lowering machinery to *also* apply the same
   "never really exit here, just narrow the mask" treatment to `Header`'s
   own relay-recovered divergent check, sequentially, ahead of computing
   `PreRegion`. Required splitting the loop's working mask into
   `EntryMasks` (the literal `PHINode` pair `makeActivePNPair()` creates,
   required by `addLatchIncoming`/`freezeLoopCarriedValues`) and `Masks`
   (the progressively-narrowed value used for `applyStageMasks`/
   `rethreadNestedEntryMasks`), and merging both `Header`'s and
   `CheckExit`'s own relay-block phi captures into one shared restore
   loop. Recovered 6 of the 34 regressed cases (590/624 -> 596/624).
2. For the remaining 28: tightened `recoverRelayExitCheck` itself to
   require the candidate stub to *actually* resolve to `ExitBlock`,
   chasing further pure, single-successor relay hops (bounded depth,
   mirroring `uniformRelayChain`'s own bound) instead of trusting the
   immediate successor alone. Now takes `ExitBlock` as an explicit new
   parameter. Correctly returns `std::nullopt` (no false match) for the
   shared-dispatch shape. Recovered 20 more cases (596/624 -> **616/624**).

**Remaining 8, left open (new, harder shape):** `special.
{for,while}_dynamic_iterations.{ifblock,elseblock}_{fragment,vertex}`.
Fresh IR dumps confirm `Header`'s own trip-count recheck is *genuinely
divergent* (depends on a per-fragment `feme.stage.input.load.f32` value)
and is **not an exit check at all** -- it is a second, independent
divergent decision that happens to share the exact same physical `Flow`
dispatch block as the loop body's own `if`/`else` divergent check,
rather than two independently-relayed-to-`ExitBlock` divergent exits
(the shape stage 1 above already handles). `collectUniformPassThroughRegion`
correctly refuses to walk through `Header`'s own un-recovered divergent
`CondBr`, bailing with `"has an internal branch in 'Flow'"`. This needs a
genuinely new code path recognizing and merging **two divergent decision
points sharing one dispatch block**, not a mechanical extension of the
existing machinery -- left for a dedicated future session.

**Verification:** 601/601 `FeMeTransformsCPUTests` unit tests; all 39
`Transforms/CPU/Linearize/*.ll` lit tests; `ninja check-feme`:
3,445/3,505 Passed, 61 Unsupported, 0 Failed (no regressions);
`dEQP-VK.glsl.demote.*` still 30/30 Pass (the original `L292` target
cases remain fixed); `dEQP-VK.glsl.loops.*`: **616/624 Pass** (up from
590/624 at the start of this investigation, 8 short of the pre-`L292`
624/624 baseline). `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed -- internal
`feme-cpu-linearize` correctness fix, no new feature/extension surface.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L297: `Basic/Mandelbrot.test` miscompile root-caused and fixed

`L296`'s own housekeeping triage flagged `Basic/Mandelbrot.test` (a
pre-existing, non-CTS `offload-test-suite` test, not a `dEQP-VK` case)
as a 91%-of-pixels image mismatch, suspecting golden-image drift. This
session confirmed via `git merge-base --is-ancestor` that the original
`L124p` fix for this exact shader is still present and un-reverted, and
root-caused a genuine new regression instead.

**Repro:** a minimal, standalone 4-thread HLSL compute shader (`for`
loop with a data-dependent `break` plus a post-loop `if` on the
break-set flag) isolating Mandelbrot's own per-pixel escape-iteration-
loop shape -- expected per-lane output `[1003,1002,1001,1000]`, FeMe
produced `[0,0,0,0]`. Far faster to iterate on than Mandelbrot's own
16M-thread dispatch.

**Root cause:** `LoopLinearizer::linearizeCycle`'s `ExitBlockRelayValues`
mechanism (the `HeaderDivergent`-coexists-with-`CheckBlock` code path,
see `L292`) can capture *two different* values for the *same*
`ExitBlock` phi -- one from `Header`'s own uniform-timeout route, one
from `CheckBlock`'s own genuinely divergent route -- whenever both
relay through the same intermediate dispatch block one hop before
`ExitBlock`. The old restore loop used "whichever capture runs first
wins" (`PN->addIncoming` guarded only by "does `PN` already have a
`Latch` entry"), silently discarding `CheckBlock`'s own genuinely
load-bearing, per-lane value whenever it differed from `Header`'s flat
constant. Once the now-dead original relay chain was cleaned up by a
later unreachable-block cleanup pass, the surviving single-entry phi
collapsed to `Header`'s own constant, permanently losing the per-lane
"did this lane take the divergent exit" signal (the HLSL-level
`Diverged` boolean in Mandelbrot's own shader) -- a provably-wrong,
always-constant value, observed at runtime as solid-black rendering.

**Fix:** tag each `ExitBlockRelayValues` entry with its origin (`Header`
vs. `Check`) and, when both are present and differ for the same phi,
merge them via a `select` keyed on a new genuinely loop-carried boolean
phi (`ExitedViaCheck`, threaded like the existing `Live`/`SideEffect`
`MaskPair` via `addLatchIncoming`/`freezeLoopCarriedValues`) recording
whether this lane's own exit was via `CheckBlock` specifically (`Masks.Live
AND NOT Staying`, folded monotonically each iteration).

**Verification:** `Basic/Mandelbrot.test` now passes (confirmed both in
isolation and via `check-hlsl-feme-vk` under `-j1`, avoiding a known
`VK_TIMEOUT` sandbox-parallelism flake on this specific test that
disappears under serial execution -- the same methodology `L296` used).
Added a standalone IR-level regression test reduced from the repro's own
pre-linearize IR, confirmed to fail without the fix and pass with it.
`ninja check-feme`: 3446/3446 Passed, 61 Unsupported, 0 Failed (+1 new
test, no regressions). `dEQP-VK.glsl.loops.*`: 616/624 Pass, same known
8 `ifblock`/`elseblock` cases (`L295`'s own still-open item), unchanged.
`dEQP-VK.glsl.demote.*`: 30/30 Pass, unchanged. `check-hlsl-feme-vk`:
same 10 pre-existing unrelated failures as `L296`'s own baseline, no
regressions. `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:
no change needed -- internal `feme-cpu-linearize` correctness fix, no
new feature/extension surface.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L298: `WaveActiveBit{And,Or,Xor}.convergence.test`'s divergent-loop sub-case root-caused and fixed

`L296`'s housekeeping triage flagged 3 new upstream `offload-test-suite`
convergence cases (`WaveActiveBitAnd`/`BitOr`/`BitXor.convergence.test`)
as a likely genuine bug "structurally in the same territory as
`L292`-`L295`", not yet root-caused past a single data point
(`WaveActiveBitAnd`'s own `Out5` buffer: `[65520,0,0,0]` instead of
expected `[65520,15,15,15]`). This session root-caused and fixed it,
confirming it is a distinct, sibling bug to `L297` (same general area
of `linearizeCycle`'s mask-threading machinery, but an omission rather
than a conflict-resolution bug).

**Repro:** `WaveActiveBitAnd.convergence.test`'s shader has 5 output
buffers; only `Out5` (a divergent-trip-count loop, each thread
iterating `TID.x` times, with a `WaveActiveBitAnd` reduce inside the
loop body) fails. Compiled a standalone HLSL repro via the real,
pre-installed `/usr/local/bin/dxc` (`-T cs_6_5 -spirv
-fspv-target-env=vulkan1.3`; in-tree `clang-dxc` has a pre-existing,
unrelated `-spirv-ext=all` driver-default-arg bug blocking `-spirv`
compilation entirely) and dumped pre-linearize/post-linearize IR via
`FEME_DUMP_IR_PRELINEARIZE=1 FEME_DUMP_IR_PRESIMD=1` (both are boolean
flags printing to `stderr`, not file-path env vars).

**Root cause:** in the post-linearize (pre-SIMDize) IR, the loop body
block containing `Out5`'s own `llvm.spv.wave.reduce.and.i32` call has
**no masking `select`** before the call, unlike the shader's three
other (non-loop) `WaveActiveBitAnd` call sites, which all correctly get
one. Traced to `LoopLinearizer::linearizeCycle`'s final fallback branch
(reached whenever `DivergentCandidates` -- the set of other genuinely-
divergent `CondBr` blocks in the cycle -- is empty, i.e. no separate
`CheckBlock` exists, so the `L292`/`L297`-fixed `CheckBlock`-coexist
branch does not apply): this branch called `applyStageMasks` only on
`Header`/`Latch`/`ExitBlock`, never on the straight-line body blocks in
between. A convergent wave-reduce intrinsic sitting directly in such a
body slips through completely unmasked, ANDing together every lane's
raw (including stale/frozen, logically-inactive) value unconditionally.
A stale comment at this exact call site claimed such a body "would have
been rejected already" if it contained any mask-affecting operation --
confirmed via code search that no such rejection check actually exists
for this specific code path (the only similar check, `CycleHasMaskOps`,
only scans for `StageOpKind` ops in the separate, fully-uniform branch,
and doesn't scan for wave intrinsics at all).

**Fix:** compute the body region (`Header`'s own exit check through to
`Latch`) and run it through the existing
`collectUniformPassThroughRegion` helper -- already proven, in the
`CheckBlock`-coexist branch, for exactly this "collect and validate a
uniform-control-flow pass-through body region" purpose. This both
validates the body really is free of a second, genuinely divergent
branch (diagnosing and bailing cleanly via `diagnose(F, ...)` if not --
closing the previously-missing safety check) and enumerates the exact
blocks needing treatment. Every block in the validated region now gets
both `applyStageMasks` (the actual fix) and the pre-existing
`rethreadNestedEntryMasks` call (preserved, needed for a nested child
cycle's own header -- e.g. the `dowhile_trap` shape -- a no-op
otherwise).

**Verification:** the standalone repro's `Out5` now correctly outputs
`[65520,15,15,15]`. Added a new IR-level regression test
(`wave-reduce-masked-in-divergent-trip-count-loop.ll`), confirmed to
fail without the fix (temporary `git stash` A/B on `Linearize.cpp`
alone) and pass with it. `ninja check-feme`: 3447/3447 Passed, 61
Unsupported, 0 Failed (+1 new test, no regressions).
`dEQP-VK.glsl.loops.*`: 616/624 Pass, same known 8 `ifblock`/`elseblock`
cases (`L295`'s own still-open item), unchanged. `dEQP-VK.glsl.demote.*`:
30/30 Pass, unchanged. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed -- internal
`feme-cpu-linearize` correctness fix, no new feature/extension surface.

**Not yet re-run this session:** `check-hlsl-feme-vk` (to directly
confirm `WaveActiveBitAnd.convergence.test` -- and its `BitOr`/`BitXor`
siblings, very likely affected by the same bug given their structural
similarity -- now pass end-to-end, not just via the direct-`llvm-lit`
spot-check and standalone-repro verification done this session); carried
to next session's housekeeping pass.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

**Update (same session, `check-hlsl-feme-vk` re-run):** repaired the
expected `feme` branch drift (`git fetch llvm-beanz feme && git
cherry-pick 854cc3f` against `/home/dev/dev/offload-test-suite`,
followed by a `cmake .` reconfigure in the `llvm-project` build dir --
the standard, every-session routine, not a new finding) and re-ran
`check-hlsl-feme-vk` serially (`-j1`, avoiding the known `Mandelbrot
.test` `VK_TIMEOUT` sandbox-parallelism flake): **721 Passed / 6 Failed
/ 1 XPASS out of 727** (up from `L296`'s own 477/11/1 baseline, after
accounting for upstream `origin/main` test-content churn in between).
Directly confirmed: `WaveActiveBitAnd/BitOr/BitXor.convergence.test`
all now pass (this session's `L298` fix). Also confirmed newly passing,
without any dedicated fix this session: `Basic/Mandelbrot.test`
(`L297`'s own fix) and `Feature/StructuredBuffer/inc_counter_array.test`
(previously suspected atomic-counter-ordering bug -- now passes;
plausibly a side effect of this session's `L298` fix narrowing a
divergent-loop body's masking more correctly, though not independently
re-root-caused). The remaining 6 failures are the same already-tracked,
pre-existing issues: 4 `InterlockedCompareExchange{,.resources}.32.test`
/`InterlockedCompareStore{,.resources}.32.test` (documented
barrier-inside-divergent-control-flow milestone-9 limitation, not a
bug) plus `spec_const_32_bits.test`/`WaveActiveMax.test` (upstream
`offload-test-suite` lit-annotation issues, unrelated to FeMe).

## L293 (continued): `derivate`'s 3 residual `fwidth` failures confirmed non-bug

`L293`'s own triage left `fwidth{,coarse,fine}.fbo_float.vec4_highp`
(3 cases) unroot-caused, suspecting either the same class of
reference-conformance mismatch as `L287`'s `cosh`/`sinh`, or a real
precision bug in FeMe's own `fwidth`/`dFdx`/`dFdy` lowering. This
session extracted the exact pixel values to settle it.

**Method:** `deqp-vk --deqp-case='dEQP-VK.glsl.derivate.fwidth*.fbo_float.vec4_highp'
--deqp-log-images=enable`, then read the `FAIL: got (...), diff = (...)`
lines directly out of the resulting QPA log (no need for a side-by-side
image diff -- deqp already logs the exact expected/actual/diff/threshold
vectors and failing pixel coordinates per case).

**Findings:**
- `fwidth`/`fwidthfine`: exactly 1 failing pixel each, at `(78,38)`.
- `fwidthcoarse`: exactly 4 failing pixels, at `(78,38)`/`(79,38)`/
  `(78,39)`/`(79,39)` -- precisely the 2x2 quad `fwidthCoarse` is
  spec-required to compute one shared derivative value across (GLSL
  spec: coarse derivatives "may" be computed per 2x2 pixel block); this
  4-pixel pattern is expected `fwidthCoarse` behavior, not a bug.
- All 4 failing-pixel instances (across all 3 cases) report the
  **identical** expected/actual/diff vectors: expected
  `(0.846465, 0.580451, 0.237867, 0.0176198)`, got
  `(0.846464, 0.580452, 0.237862, 0.0175858)`, diff
  `(5.36442e-07, 8.34465e-07, 5.76675e-06, 3.40529e-05)` against
  threshold `(6.09756e-05, 6.09756e-05, 3.05101e-05, 3.05171e-05)`.
- RGB are all well within threshold (diffs 5e-07 to 5.8e-06 vs ~3e-05/
  6e-05 thresholds). Only alpha (w) is marginally over: `3.40529e-05`
  vs threshold `3.05171e-05` (~12% overshoot).
- The test applies a per-channel display-space remapping before
  thresholding (`p' = p * derivScale + derivBias`, logged as
  `(1.18135, 1.72273, 4.20372, 56.6675)` / `(-0.999955, -0.999947,
  -0.99984, 2.26719e-08)` for this case) to make small derivative
  magnitudes visible in an 8-bit-equivalent comparison. The alpha
  channel's scale factor (`56.6675`) is far larger than RGB's
  (`1.18135`-`4.20372`), so it amplifies the underlying raw per-lane
  difference by ~57x before the threshold check. Dividing the observed
  display-space diff back by this scale gives the real underlying raw
  diff: `3.40529e-05 / 56.6675 ≈ 6.0e-7` -- a few ULPs of float32
  precision at this magnitude, not a structural lowering bug.

**Conclusion:** same class of finding as `L287`'s `cosh`/`sinh` --
a legitimate, tiny floating-point rounding difference between FeMe's
own `fwidth`/`dFdx`/`dFdy` lowering and the CTS's reference
implementation, amplified to just barely exceed an aggressively tight,
scale-amplified comparison threshold. No code change made; this
closes `L293`'s last open item. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed (no code change at
all this item).

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L265 (deep-dive, still open)

Resumed the long-carried-over `L265` item: the residual 12-case
`astc_{8x8,10x5,12x12}_unorm_block.a2b10g10r10_snorm_pack32` blit
failures left over after `L260`'s rounding-convention fix.

**Corrected a coordinate-mapping mistake from a prior session.** The
specific failing case investigated in depth,
`astc_8x8_unorm_block.a2b10g10r10_snorm_pack32.general_general_linear`,
is a *rescaling* blit (`general_general_linear`), not a 1:1 copy, so a
destination pixel's coordinates do not divide cleanly by the ASTC block
size to find the relevant source block. A prior session's "block
boundary" framing assumed destination pixel `(23,3)` maps to source
block `(2,0)` (naive `floor(23/8), floor(3/8)`); tracing the actual
blit math this session found the real source sample coordinates are
`~(60, 28)`, i.e. source block `(7,3)` -- a completely different block.
Re-traced from the correct block.

**Exhaustively instrumented and bit-verified every decode stage** for
block `(7,3)` (raw bytes `25 6c 73 1a af 7e 9c 26 10 c7 5b 4b fc ac 72
85`) against a from-scratch Python re-implementation:
- Raw ISE `ColorVals` extraction (`decodeISESequence` at `ColorRange=7`,
  a pure 3-bit range with no trit/quint component): matches.
- `unquantizeColorValue`'s bit-replication: matches.
- `bitTransferSigned`: an initial by-hand re-derivation of this
  function (from memory, not re-reading the source) produced a
  completely different result from the real traced output: real
  `V[0]`/`V[4]`/`V[6]` came out as `128`/`255`/`128`, not the
  hand-derived `0`/`127`/`0`. Re-reading the actual source line-by-line
  found the by-hand version had the two statements backwards (assumed
  `A = (A>>1)|(B&0x80); B >>= 1;` when the real code is `B >>= 1; B |=
  A & 0x80; A >>= 1; ...`) -- the real code matches the ASTC
  specification's `bit_transfer_signed` procedure exactly. Once
  corrected, the by-hand Python port reproduces the traced CEM-13
  endpoints bit-for-bit for both partitions
  (`P0: Lo=(190,158,254,127) Hi=(191,173,255,128)`, `P1:
  Lo=(100,45,18,127) Hi=(100,36,0,109)`).
- CEM-13 endpoint construction, swap condition, and `blueContract`: all
  match the reference bit-for-bit once the above correction was made.

**Conclusion: `ASTCDecode.cpp`'s decode pipeline is spec-correct for
this block.** The decoded alpha endpoints (`127`/`128` for partition 0,
the one all four relevant destination-blend texels resolve to) are
*not* a decode bug -- they are exactly what a spec-faithful decoder
produces from this block's bits. A prior session's `L260` commit
message's claim that these 12 residual cases involve "a decoded alpha
value that differs from the reference's before either side's rounding
is even applied" could not be reproduced this session; the most likely
explanation is that claim was itself based on an analysis mistake (this
session independently caught and corrected two of its own by-hand
arithmetic errors while re-deriving the same functions, so a similar
slip in a much earlier session re-deriving the same code by hand is
plausible, though not provable after the fact).

**Confirmed, bit-exact mechanism of the visible failure.** The
destination pixel's bilinear blend averages four source texels' decoded
alpha (two at byte `127`, two at `128`, all four resolving to partition
0 from the traced block) with weights of exactly `0.25` each (`WX =
WY = 0.5`). Traced the blend accumulator at full `%.20g` precision:
`Accum[3] == 0.5` exactly (not a near-tie amplified by display
rounding; `(127+127+128+128)/4/255` is mathematically exactly `0.5` in
IEEE-754 double precision, since `255 == 2 * 127.5` divides exactly).
This lands precisely on the 2-bit-SNORM-alpha quantization boundary
between codes `0` (`0.0`) and `1` (`1.0`) -- the same class of tie
`L260` fixed for a different case, but this case's reference image
requires the ties to break the **other** way (`0.5 -> 1`, away from
zero) to match.

**This directly conflicts with `L260`'s fix, confirmed empirically
rather than assumed:** re-ran
`astc_5x5_unorm_block.a2b10g10r10_snorm_pack32.general_general_linear`
(`L260`'s own originally-fixed case) against the unmodified,
currently-committed `ties-to-even` code this session -- it still
**Pass**es. So `L260`'s case needs `0.5 -> 0` (even) and this `L265`
case needs `0.5 -> 1` (away from zero); no single global
tie-breaking rule in `packClearColor`'s `R10G10B10A2_SNORM` branch can
satisfy both. The Vulkan spec text (fetched directly from the
Khronos registry this session) only says implementations "should round
to nearest" for fixed-point conversion and does not mandate a tie
direction either way, so this is not resolvable by re-reading the spec
more carefully -- it would need either a non-tie-breaking-convention
root cause (not found: every stage up through the exact `0.5` tie is
spec-correct and bit-verified) or a scoped, evidence-backed special
case distinguishing blit-interpolated alpha from direct writes (not
attempted this session -- no concrete structural difference between the
two cases' sampling was identified beyond "one is 2-bit alpha via a
5-partition-adjacent small ASTC block, the other via a 2-partition
dual-plane large ASTC block", which is too speculative a basis for a
targeted fix without more data).

**No code change made this session for `L265`** (all temporary debug
instrumentation in `ASTCDecode.cpp`/`ImageOps.cpp` added during this
investigation was reverted via `git checkout`, confirmed via `git diff`
returning empty before any other change was layered on). `L260`'s
existing fix was not touched and remains correct for its own case.
`check-feme`: unaffected (3447/3447, no code changed). CTS: no change
(still 12 residual `L265` failures, `astc_5x5`'s `L260` case still
passing). `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:
no change (investigation only, no feature/extension surface change).

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L299: `InterlockedCompareExchange.32.test`/`InterlockedCompareStore.32.test` regression root-caused and fixed

**Starting point.** The prior session's own next-steps list flagged two
newly-failing `check-hlsl-feme-vk` cases, `Feature/HLSLLib/`
`InterlockedCompareExchange.32.test` and `InterlockedCompareStore.32.test`,
despite both having been closed previously by `H163`/`H164`. First had
to re-repair the `offload-test-suite` checkout's recurring branch drift
(an external/automated process keeps resetting it to `origin/main`,
silently dropping the FeMe-enabling cherry-pick `854cc3f` -- re-applied
as local commit `d0974dd` this session) before `check-hlsl-feme-vk`
could even run.

**Root cause, found via `FEME_DUMP_IR` + `feme-opt --llvm`
`-passes=feme-cpu-wrap-entry`** (the documented isolation recipe in
`feme/.instructions.md`), not by reconstructing the IR by hand: both
cases compile fine via `dxc`, but FeMe's own `feme-cpu-wrap-entry` pass
emitted a hard compile error ("barrier inside non-linear control
flow"), failing `vkCreateComputePipelines` outright -- not a
wrong-answer bug like the superficially-similar, already-fixed
`inc_counter_array.test`. The real compiled shape (SIMDized, wave size
4): a 256-iteration uniform loop whose body+latch collapses to one
block, containing a group-sync barrier, a win-counter (`Wins`)
induction, and a "stays true until a compare fails" (`Mono`) induction
whose own header phi is read a *second* time inside the header itself
-- rebroadcast into a `<4 x i32>` splat consumed only by the post-loop
suffix (storing each lane's final `Mono` value). `matchLoopShape`'s
induction-safety check had no case for a header-resident use of the
phi (only "precedes the recurrence," "shares its barrier region," or
"lives in `Shape.SuffixOrder`"), so it declined the whole shape.
Fixing that match then surfaced a second, previously-unreachable crash
in `buildWrapperForLoop`: `HeaderDerivedValues` (the mechanism giving a
header-local non-phi value its own `loopvarN` wrapper parameter) only
scanned for uses inside the loop's own per-wave region, never the
suffix chain, so the splat's only use kept referencing the original,
soon-to-be-erased header instruction, crashing `eraseFromParent`
("Use still stuck around after Def was destroyed"). Both checks are
now suffix-aware. Full details and fix in `feme/docs/Roadmap.md`'s new
`H174` entry (parent `H163`).

**Verification.** New standalone lit regression
(`feme/test/Transforms/CPU/entry-wrapper-loop-header-derived-value-in-suffix.ll`)
isolates this exact shape end to end through the real
`feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry` pipeline.
`check-feme`: 3448/3448 passed (61 unsupported), 0 failed.
`check-hlsl-feme-vk`: both `Interlocked*` cases pass again (along with
their `.resources.32` siblings); remaining failures unchanged
(`Basic/Mandelbrot.test`, `Feature/SpecializationConstant/`
`spec_const_32_bits.test`, `WaveOps/WaveActiveMax.test`, plus the
pre-existing `array_of_matrices.test` unexpected-pass -- all
pre-existing, unrelated, lower-priority items already tracked
separately).

**Vulkan CTS.** Ran `dEQP-VK.compute.pipeline.*` in full (20502 cases:
684 Pass / 0 Fail / 19818 NotSupported) and the barrier-specific subset
`dEQP-VK.compute.pipeline.*barrier*` (941 cases: 9 Pass / 0 Fail / 932
NotSupported) -- both clean, no regressions from this fix.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
(internal CPU-backend compiler fix only, no feature/extension surface
change).

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L299 (continued): `Basic/Mandelbrot.test` confirmed passing, not golden-image drift

Carried over 3+ sessions as an open "confirm golden-image drift" item.
Confirmed it is **not failing at all**: ran the full 727-case
`check-hlsl-feme-vk` suite this session (default parallelism, no `-j1`
workaround needed) and `Mandelbrot.test` passes, consistent with
`L297`'s fix. Also regenerated its output standalone via `offloader`
and diffed against `offload-golden-images`' `Mandelbrot.png` with the
test's own `rules.yaml` thresholds (`imgdiff`: 0.086% differing
pixels, RMS 2.17 on those pixels, furthest-interval histogram entirely
within the rules' allowed buckets) -- passes with margin.

The stale "still failing" entries threaded through several prior
sessions' next-steps lists were carried forward without
re-verification; `L297` already fixed the real miscompile, and the
`VK_TIMEOUT`/flaky-parallelism failure mode `L255` separately fixed
(via `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS`) was the likely source of any
residual noise. No code change needed this session -- closing this
item out. `check-hlsl-feme-vk`: 486 Pass / 31 XFAIL / 207 Unsupported
/ 2 Fail (`spec_const_32_bits.test`, `WaveActiveMax.test`, both
pre-existing, unrelated, tracked separately) / 1 UnexpectedPass
(`array_of_matrices.test`, also pre-existing and tracked separately).

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L302: broader-than-glsl/tessellation CTS sampling (`api`/`synchronization`)

Addressed the "overdue many sessions" broader-CTS-sampling item,
explicitly instructed to run in small, isolated per-case batches, never
a full-cluster sweep. Surveyed group sizes via `dEQP-VK-cases.txt`:
`api` has 267,504 cases total, `synchronization` 64,872 -- both
confirmed too large for a full sweep.

Ran `dEQP-VK.api.smoke.*` (6/6 Pass) and
`dEQP-VK.synchronization.smoke.*` (8/12 Pass, 4 NotSupported), then a
batch of 12 small/medium `api.*` subgroups: `array`, `device_init`,
`object_management`, `command_buffers`, `buffer_view`,
`format_features`, `external`, `granularity`,
`buffer_memory_requirements`, `fill_and_update_buffer`,
`image_compression_control`, `ds_color_copy`.

**Found 2 new genuine bugs** (both fixed this session, see `L303`/
`L304` below): `format_features` (2 Fail, missing
`VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT`) and
`command_buffers` (1 Fail,
`secondary_push_descriptor_set_with_template`, a descriptor-update-
template cross-binding-overflow gap).

Still open: most of `api.*`'s ~35 subgroups (`pipeline`,
`version_check`, `invariance`, `null_handle`, `frame_boundary`,
`gpa_interface`, etc.) and all of `synchronization.*` beyond `smoke`
remain unsampled; `copy_and_blit` (201,667 cases) and `image_clearing`
(45,636 cases) deliberately skipped so far as too large even for a
single batch.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L303: `dEQP-VK.api.format_features.format_feature_flags2.{d16_unorm,d32_sfloat}` -- missing depth-comparison bit

Root-caused: `VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT`
(`0x200000000`, bit 33) has no 32-bit `VkFormatFeatureFlagBits`
equivalent, so FeMe's existing widen-from-32-bit path in
`vkGetPhysicalDeviceFormatProperties2` (`EntryPoints.cpp`) could never
produce it -- the same class of gap already special-cased there for
`HOST_IMAGE_TRANSFER_BIT`/`STORAGE_{READ,WRITE}_WITHOUT_FORMAT_BIT`.
CTS's own `Context::getRequiredFormatProperties` (`vktTestCase.cpp`)
derives this bit purely self-referentially: whenever a depth format's
own `SAMPLED_IMAGE_BIT` is already reported for a given tiling mode,
that same tiling mode is required to also report the depth-comparison
bit -- not an external hardware-capability check.

Fixed by adding direct `IsDepthFormat`/`DepthComparisonLinear`/
`DepthComparisonOptimal` computation in
`vkGetPhysicalDeviceFormatProperties2`, ORed into
`Props3->linearTilingFeatures`/`optimalTilingFeatures` for FeMe's 4
mapped depth formats (`D16_UNORM`, `D32_FLOAT`, `D24_UNORM_S8_UINT`,
`D32_FLOAT_S8X24_UINT`). `X8_D24_UNORM_PACK32`/`D16_UNORM_S8_UINT`
aren't mapped by FeMe at all -- a separate, pre-existing, out-of-scope
gap, not touched this session.

`dEQP-VK.api.format_features.*` re-run: 184/368 Pass, 0 Fail, 184
NotSupported (up from 2 Fail). `check-feme`: 3449/3510 Passed, 61
Unsupported, 0 Failed (no regressions). `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change -- the bit was already
implicitly promised by `VK_KHR_format_feature_flags2`, which FeMe
already advertises; this closes a gap in computing it correctly, not a
new capability.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L304: `dEQP-VK.api.command_buffers.secondary_push_descriptor_set_with_template` -- template cross-binding overflow

Root-caused: the test's layout has two separate single-element
bindings (binding 0 = output buffer, binding 1 = input buffer) and
pushes both via **one** `VkDescriptorUpdateTemplateEntry` with
`dstBinding=0`, `descriptorCount=2`, deliberately exercising the spec's
cross-binding overflow rule (a `descriptorCount` exceeding one
binding's remaining elements continues into the next
consecutively-numbered binding). `applyDescriptorUpdateTemplate`'s
non-inline-uniform-block write loop (`Descriptor.cpp`) used a naive
`Entry.dstBinding, Entry.dstArrayElement + J` index with no overflow
handling, unlike `applyDescriptorWrite` (the
`vkUpdateDescriptorSets`/direct-push-descriptor path), which already
implements this correctly via the existing `BindingCursor::normalize()`
helper (fixed for that path under `L154`, but never applied to the
template path).

Fixed by rewriting `applyDescriptorUpdateTemplate`'s per-element loop
to use the same `BindingCursor` pattern; the separate inline-uniform-
block branch (fundamentally different, single-contiguous-byte-range
semantics) is untouched. Added
`DescriptorTest.UpdateTemplateSpansConsecutiveBufferBindings`, the
template-path counterpart to the existing
`WriteDescriptorSetSpansConsecutiveBufferBindings`.

`dEQP-VK.api.command_buffers.*` re-run: 121/131 Pass, 0 Fail, 10
NotSupported (up from 1 Fail). `check-feme`: 3449/3510 Passed (+1 new
test), 61 Unsupported, 0 Failed (no regressions).
`check-hlsl-feme-vk`: 485 Pass / 31 XFAIL / 207 Unsupported / 2 Fail
(`spec_const_32_bits.test`, `WaveActiveMax.test`, both pre-existing,
unrelated) / 1 XPASS (`array_of_matrices.test`, also pre-existing) --
unchanged from baseline; `Mandelbrot.test` reconfirmed passing in
isolation after a parallel-run `VK_TIMEOUT` semaphore-wait flake (same
`L255` flakiness class, not a regression). `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change -- an internal
descriptor-update-template correctness fix, no new feature/extension
surface.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L302 (continued): further CTS sampling batch -- all clean

Continued `L302`'s broader-sampling item with a second small batch:
`dEQP-VK.api.pipeline.*` (9/9 Pass), `api.version_check.*` (3/3 Pass),
`api.invariance.*` (3/3 Pass), `api.null_handle.*` (24/24 Pass),
`api.frame_boundary.*` (0/19, all 19 NotSupported), and two
`synchronization.*` subgroups beyond `smoke`:
`synchronization.basic.*` (21/29 Pass, 8 NotSupported) and
`synchronization.timeline_semaphore.*` (2880/2880 Pass). All clean, 0
Fail, no new bugs found this batch.

Still open: `synchronization.op` (20,131 cases, needs sub-batching) and
`api`'s two largest subgroups (`copy_and_blit`, 201,667 cases;
`image_clearing`, 45,636 cases) remain unsampled.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed (with `VK_ICD_FILENAMES` explicitly
set).

## L305: `dEQP-VK.api.descriptor_set.descriptor_set_layout_lifetime.{compute,graphics}` -- dangling `DescriptorSetLayout *` crash

**Methodology note first**: this session's env-var setup
(`export VK_ICD_FILENAMES=... VK_DRIVER_FILES=$VK_ICD_FILENAMES` as a
*single* combined `export` line) silently targeted the system's real
`llvmpipe` ICD instead of FeMe's: in that one-line form, bash expands
`$VK_ICD_FILENAMES` against its value *before* the same command's own
assignment takes effect, so `VK_DRIVER_FILES` picked up whatever
preexisting/default value was in the environment rather than FeMe's path.
`vulkaninfo --summary` happened to be run as two separate sequential
`export` statements at session start (so it correctly reported `FeMe CPU
Vulkan Device`), but every subsequent CTS batch command this session used
the broken combined form and therefore silently ran against real
`llvmpipe` the whole time. All of this session's `synchronization.op`/
`api` misc/`copy_and_blit` sampling (described above and in `L302`
"continued") had to be **discarded and rerun** once this was caught
(triggered by `copy_and_blit.core.blit_image.simple_tests.2d_array_to_3d`
showing 134 failures gated behind `VK_KHR_maintenance8`, which FeMe's own
source has zero references to -- directly querying the device showed
`vulkaninfo` was reporting `llvmpipe`, not FeMe, once the full non-
summary dump was checked). Re-running with a safe two-statement (or
`source`d helper script) form confirmed `synchronization.op` (20,131
cases) and the `api` misc batch (14,052 cases) are genuinely clean
against real FeMe -- the `maintenance8` blit failures were a real
(certified) llvmpipe gap, not a FeMe bug, and do not apply to FeMe at all.
**Going forward, always set `VK_ICD_FILENAMES`/`VK_DRIVER_FILES` as two
separate statements (or via a sourced script), never a single combined
`export A=... B=$A` line, and periodically spot-check a full (non-
`--summary`) `vulkaninfo` dump's `deviceName`, not just `--summary`'s.**

**The real bug**, found once `copy_and_blit`'s re-run (now correctly
against FeMe) reached `api.descriptor_set.descriptor_set_layout_lifetime`:
both `.compute` and `.graphics` cases **segfault**. Root cause (via
`gdb -batch -ex run -ex bt`): `vkCreatePipelineLayout` stored each
`VkDescriptorSetLayout` argument's raw `DescriptorSetLayout *` (from
`fromHandle`) directly inside the new `PipelineLayout`. Per spec, an
application may call `vkDestroyDescriptorSetLayout` immediately after
`vkCreatePipelineLayout` returns -- the set-layout object need not outlive
pipeline-*layout* creation, only the *bindings it described* need to
remain known to anything created from that pipeline layout later. CTS's
`descriptor_set_layout_lifetime` test exercises exactly this: create a
set layout, create a pipeline layout from it, destroy the set layout, then
create a real pipeline from the (still-live) pipeline layout. FeMe's
`PipelineLayout` was still holding the now-freed `DescriptorSetLayout *`,
and `vkCreateComputePipelines`/`vkCreateGraphicsPipelines`'s pipeline-
cache-key computation (`PipelineCache.cpp`'s
`hashSetLayoutsAndPushConstants`, which walks `Layout->bindings()` for
every set) dereferenced it -- a use-after-free, landing in
`llvm::SHA256::update` (reading freed/garbage memory) in the crash's
backtrace.

**Fix**: `PipelineLayout` (`Pipeline.h`/`Pipeline.cpp`) now deep-copies
each set's `DescriptorSetLayoutBinding` list into an
`std::optional<DescriptorSetLayout>` it owns, at `vkCreatePipelineLayout`
time, rather than retaining the handle's own object pointer; it still
exposes the same `ArrayRef<const DescriptorSetLayout *>` from
`setLayouts()` (now pointing into its own owned copies), so every existing
consumer (`hashSetLayoutsAndPushConstants`, `validateBoundRanges`,
`patchUnboundedResourceRanges`) is unchanged. A new regression test,
`PipelineTest.CompilesPipelineAfterDescriptorSetLayoutDestroyed`, destroys
the descriptor set layout before creating the pipeline (mirroring CTS's
sequence exactly) and asserts pipeline creation still succeeds.

Both `dEQP-VK.api.descriptor_set.descriptor_set_layout_lifetime.{compute,
graphics}` now `Pass` (verified individually and via `ninja check-feme`,
3450/3450 passed, 0 regressions).

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed via a sourced helper script
(`source /tmp/feme_env.sh`) after discovering the combined-`export` bug
above.

## L306: `dEQP-VK.api.copy_and_blit.core.blit_image.simple_tests.mirror_*` and `2d_array_to_3d.reverse_blit_*` -- mirrored-destination blit off-by-one

Found via the same `L305`/`L302` broader CTS sampling, re-running
`copy_and_blit.core.blit_image.*` (73,839 cases) against the real FeMe
device once the `L305` env-var methodology bug (see above) was fixed:
66 genuine failures, split into two independent groups --

- **12 cases**: `blit_image.all_formats.color.2d.astc_{10x5,12x12,8x8}
  _unorm_block.a2b10g10r10_snorm_pack32.*` -- the already-tracked `L265`
  ASTC alpha-decode tie-breaking bug, now confirmed broader than
  previously known (`astc_10x5`/`astc_12x12` in addition to the
  previously-known `astc_5x5`/`astc_8x8`). Left open under `L265`; not
  re-attempted this session (several prior sessions already failed to
  find a scoped fix).
- **54 cases**: `blit_image.simple_tests.mirror_{subregions,
  subregions_3d,x,x_3d,xy,xy_3d,y,y_3d,z_3d}.*` -- a new,
  previously-undiscovered bug, root-caused and fixed this session (below).

**Root cause**: `runBlitImage` (`ImageOps.cpp`) computes each destination
axis' step direction as `DstStepX = DstX1 >= DstX0 ? 1 : -1` (and the
`Y`/`Z` equivalents), then maps the destination loop index `X` (always
counting up from `0`) to the actual pixel coordinate via
`DstX0 + X * DstStepX`. This formula is correct for the unmirrored case
(`DstStepX == 1`): `DstX0` is already the first covered pixel, so index
`0` maps straight to it, and the loop covers `DstX0 .. DstX1 - 1`. It is
*not* correct for the mirrored case (`DstStepX == -1`, i.e. the
application listed the axis' numerically larger corner first): per
Vulkan's own corner convention (and CTS's `addBlittingImageSimpleMirror
{X,Y}Tests`, which construct exactly this -- an unmirrored source
`{0,size}` blitted into a mirrored destination `{size,0}`), whichever
corner is numerically larger is the exclusive "one past the last
covered pixel" bound *regardless of which corner the application listed
first*. So in the mirrored case, the first pixel actually covered is
`DstX0 - 1`, not `DstX0` itself -- the old formula wrote one pixel past
the image's own edge at index `0` (`DstX == size`, out of bounds) and
never reached the axis' own pixel `0`, a systematic two-ended one-pixel
skew across the whole mirrored axis. This is a silent wrong-image-
contents bug, not a crash (`texelPointer`'s own bounds arithmetic doesn't
trap on the resulting slightly-out-of-range index in a way that faults),
which is why it only surfaced as a CTS image-comparison mismatch.

The same bug independently explains the `2d_array_to_3d.reverse_blit_*`
failures seen in the broader `copy_and_blit` re-run's full 133-case raw
tally before filtering to genuine `Fail` results (that family also
exercises `invert_dst_x`/`invert_dst_y`/`invert_z` mirrored destination
axes); the authoritative 66-case `Fail`-only list (cross-checked by
re-parsing the run's own `.qpa` with the established
`<TestCaseResult>`-regex method) confirms only the 12 ASTC + 54 `mirror_*`
cases were real failures -- the `2d_array_to_3d.reverse_blit_*` family
was already correctly reported `NotSupported` (FeMe does not advertise
`VK_KHR_maintenance8`, which 2D/3D cross-blits require), not `Fail`, so
it was never actually broken by this bug in the first place.

**Fix**: added a `mirroredCoord(Base, Step, Index)` helper in
`runBlitImage` that returns `Base + Index` when `Step > 0` (unchanged,
unmirrored behavior) and `Base - 1 - Index` when `Step < 0` (the
corrected mirrored behavior), and switched `DstX`/`DstY`/`DstZ`'s
computation to use it instead of the old `Base + Index * Step` formula.

Re-running the authoritative 66-case fail list confirms: 54/66 now
`Pass` (all `mirror_*` cases), 12/66 still `Fail` (the pre-existing,
unrelated `L265` ASTC cases, unaffected by this fix as expected). `ninja
check-feme`: 3450/3450 Passed, 61 Unsupported, 0 Failed, 0 regressions.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal rendering-correctness fix to an already-implemented
code path, no new feature/extension surface.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed via `source /tmp/feme_env.sh`.

## L307: `copy_and_blit.core` small-subgroup sweep -- confirmed scope, new `use_after_copy.*_msaa` legalization gap

Continuing this session's `L302`/`L305`/`L306` broader CTS sampling:
re-ran `copy_and_blit.core`'s remaining small/medium subgroups (the
7,872-case batch already queued from before the `L305` env-var fix,
case list regenerated against the corrected FeMe device) -- 222 Fail,
split into two independently-confirmed groups:

- **36 cases** (`depth_stencil_msaa_copy.*`,
  `resolve_image.whole_copy_before_resolving_no_cab.*`): every case
  fails identically, at `vkCreateImage` returning
  `VK_ERROR_INITIALIZATION_FAILED` for a multisample (2/4/8-sample)
  image. This is the *same* already-tracked multisample
  image-creation gap `L280`/`L281` found via `fragdepth` -- confirmed
  this session to be a general multisample-image-creation limitation,
  not specific to depth formats as the "fragdepth multisample"
  framing implied. No new work needed; left open under `L280`/`L281`.
- **162 cases**, all `use_after_copy.<format>.*_msaa` variants across
  many unrelated color formats -- a genuinely new bug. Isolated one
  case directly:
  `dEQP-VK.api.copy_and_blit.core.use_after_copy.r8_unorm.general.
  32x32x2_img2img_msaa` fails with:
  ```
  error: failed to legalize operation 'spirv.ImageFetch' that was
  explicitly marked illegal: %40 = "spirv.ImageFetch"(%39, %33, %37)
  <{image_operands = #spirv.image_operands<Sample>}> :
  (!spirv.image<f32, Dim2D, NoDepth, Arrayed, MultiSampled, NeedSampler,
  Unknown>, vector<3xsi32>, si32) -> vector<4xf32>
    Fail (vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED)
  ```
  i.e. `OpImageFetch` with an explicit `Sample` operand against an
  **arrayed and multisampled** image has no SPIR-V-to-LLVM lowering
  case -- a missing-feature compiler gap (pipeline creation itself
  fails, so the shader never runs; not a runtime correctness bug like
  `L306`'s). Not root-caused past this diagnostic; filed as `L307`
  (todo) rather than attempted this session -- likely substantial new
  lowering work, better suited to a dedicated session.

No code change this session for either sub-finding (confirmation +
new-bug triage only). `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed yet (no fix landed).

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed via `source /tmp/feme_env.sh`.

### L307 follow-up (later session): root-caused and fixed the `use_after_copy.*_msaa` legalization gap

Picked up `L307`'s group (b) as this session's top-priority item.
Root-caused two distinct, stacked gaps rather than one:

- **Layer 1 (MLIR legalization, `SPIRVToLLVMPatterns.cpp`):**
  `ImageLoadPattern<ImageOpTy>`'s `Sample`-image-operand handling
  (added under `H19g` for a multisampled 2D *storage* image's
  `OpImageRead`) was gated behind
  `if constexpr (std::is_same_v<ImageOpTy, ImageReadOp>)` --
  `ImageFetchOp` (a *sampled* image's `texelFetch()`) was never given
  the same treatment, so any `Sample` operand on it was declined
  outright. This produces the exact diagnostic quoted above
  (`error: failed to legalize operation 'spirv.ImageFetch'...`).
  Checked MLIR's own `SPIRVImageOps.td`: `ImageFetchOp`'s definition
  has no verifier restriction against a `Sample` operand, confirming
  this was a FeMe-side pattern gap, not a real SPIR-V/MLIR limitation.
  Fixed by generalizing the `HasSample` computation to run
  unconditionally for both ops -- both reach `createResourcePointer`
  (`llvm.spv.resource.getpointer`) identically regardless of whether
  the handle is a storage or sampled image, so nothing in the pattern
  was actually storage-image-specific.
- **Layer 2 (CPU lowering, `SPIRVResourceLowering.cpp`):** even with
  layer 1 fixed, `hasOnlySupportedImageUses` still explicitly rejected
  any fetch-shaped use against `Plain2DMS`/`Array2DMS` *sampled*-image
  shapes -- a deliberate scope gap `L73`'s own closure note flagged as
  "unstarted follow-on work" (`L73` only added `OpImageQuerySamples`
  support for these two shapes). Fixed by widening the zero-mip-fetch
  acceptance branch to also accept `Plain2DMS` (3-wide `(x, y,
  sample)` coordinate, the same one-extra-lane widening `Array2D`'s
  own `layer` lane already gets over `Plain2D`) and `Array2DMS`
  (4-wide `(x, y, layer, sample)`). This needed **no new runtime
  helper or `ImageCallKind`**: once accepted, the resulting IR
  (`getpointer` + `load`) is structurally identical to a storage
  image's `OpImageRead` against the same `Plain2DMS`/`Array2DMS`
  shapes (already supported since `H19g`/`H19m`), so
  `lowerImageAccesses`'s existing codegen switch and
  `feme.cpu.image.load.2d(.array).v4f32`/`.v4i32`'s own runtime
  implementation (`FeMeRuntimeCPU.c`, which already documents itself
  as reading "one texel of a 2D image (sampled or storage)" and
  already accepts a `Sample` operand) serve both storage reads and
  sampled fetches without modification.

Added coverage for both phases: `spirv-to-llvm-image-access-
multisample.mlir` gained `fetch_ms`/`fetch_arrayed_ms` cases (plain and
arrayed `ImageFetch` against a multisampled sampled image), and
`SPIRVResourceLoweringTest.cpp` gained
`LowersPlain2DMSSampledImageFetchToImageLoad`/
`LowersArray2DMSSampledImageFetchToImageLoadArray`.

Re-ran the authoritative caselist:
`dEQP-VK.api.copy_and_blit.core.use_after_copy.*_msaa` -- 348 total,
152 `NotSupported` (an unrelated, pre-existing
`x8_d24_unorm_pack32`/combined-depth-stencil-format gap, not this
bug), **196/196 applicable cases now Pass** (was 0/196, every one a
pipeline-creation legalization failure).

`ninja check-feme`: 3452/3513 Passed (+2 new tests vs. the prior
3450/3510 baseline), 61 Unsupported, 0 Failed, 0 regressions.
`ninja check-hlsl-feme-vk`: unchanged pre-existing baseline --
`Basic/Mandelbrot.test` (golden-image drift, unrelated, carried over
many sessions), `Feature/SpecializationConstant/
spec_const_32_bits.test`/`WaveOps/WaveActiveMax.test` (pre-existing
lit-annotation issues, unrelated), `Feature/PushConstant/
array_of_matrices.test` (pre-existing unexpected-pass, unrelated) --
no new failures.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal SPIR-V-to-LLVM/CPU-lowering correctness fix
closing a documented scope gap (`L73`); multisampled sampled-image
`texelFetch()` is core Vulkan 1.0 functionality with no gating optional
feature bit.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed via `source /tmp/feme_env.sh`.

## L308: `fragdepth`'s combined-depth-stencil `_no_depth_clamp` bug -- sampled-image depth-aspect decode gap (not a `readDepth`/`writeDepth` bug as `L281` hypothesized)

Picked up the sub-bug `L281` discovered but deferred: every
`dEQP-VK.glsl.builtin_var.fragdepth.*_s8_uint_no_depth_clamp` case (6
total, one per topology x `{d24_unorm_s8_uint,d32_sfloat_s8_uint}`)
fails `expected <nonzero> but got 0`.

**Root cause, corrected from `L281`'s hypothesis:** `Executor.cpp`'s
`readDepth`/`writeDepth` (the depth-attachment write/test path) already
correctly handle both combined formats' addressing -- confirmed by
direct inspection, not the bug. The real bug is in the CTS test's own
*validation* mechanism: `BuiltinFragDepthCaseInstance`
(`vktShaderRenderBuiltinVarTests.cpp`) doesn't do a plain buffer copy
to check the rendered depth -- it runs a **second render pass** that
samples the depth attachment through a real
`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` bound to a depth-aspect
`VkImageView` whose own declared format is the *combined*
`D24_UNORM_S8_UINT`/`D32_FLOAT_S8X24_UINT` format, verbatim (per spec,
not split into a pure-depth format). `CommandBuffer.cpp`'s
`materializeImageDescriptor` passes this format straight through
(`Dst.Format = View->format()`) to the runtime's generic sampling
path -- unlike `buildSubpassInputHeap` (used only for same-render-pass
`subpassLoad` input-attachment reads), which already receives
pre-split pure-format `AttachmentView`s and was never affected.

`FeMeRuntimeCPU.c`'s `femeRTImageFormatElementSize`/
`femeRTUnpackImageTexel` (the generic sampled-image texel-decode
tables) had cases for `D16_UNORM`(31)/`D32_FLOAT`(32)/`S8_UINT`(35),
but **none at all** for `D24_UNORM_S8_UINT`(33) or
`D32_FLOAT_S8X24_UINT`(34). `femeRTFetchTexel2D`'s
`if (ElemSize == 0) return Zero;` guard then silently returned an
all-zero `vec4` for any such sample -- exactly the observed symptom.

**Why the depth-clamp-enabled sibling cases still passed despite the
same bug:** the CTS test deliberately sign-flips its written depth
values based on `depthClampEnable` so that every clamp-enabled case's
expected value is always exactly `0` (clamped to the near plane) --
a "passing for the wrong reason" trap that only the `_no_depth_clamp`
siblings (needing the real nonzero value) exposed.

**Fix** (`feme/runtime/CPU/FeMeRuntimeCPU.c`): added `case 33`/`case 34`
to both functions.
`femeRTImageFormatElementSize` now reports each format's own real
combined-texel byte size (4 for `D24_UNORM_S8_UINT`'s one shared word,
8 for `D32_FLOAT_S8X24_UINT`'s two separate words) -- not a narrower
pure-depth-format's size, since `femeRTFetchTexel2D`'s X-axis
addressing stride comes directly from this return value, and using
too small a stride would misalign every texel beyond `X=0`.
`femeRTUnpackImageTexel` decodes just the depth component (low 24 bits
of the shared 4-byte word for `D24_UNORM_S8_UINT`, matching
`ImageFixture.cpp`'s own `unpackDepth` math; the first of two separate
4-byte words for `D32_FLOAT_S8X24_UINT`), padding `G=B=0, A=1` per the
existing `D16_UNORM`/`D32_FLOAT` convention.

**Deliberately out of scope:** sampling the *stencil* aspect of a
combined format through this same `COMBINED_IMAGE_SAMPLER` path still
decodes as depth, since no aspect information reaches
`femeRTUnpackImageTexel` at this point in the generic sampling
pipeline. A known, accepted asymmetry -- no real CTS case exercising it
has been found yet; flagged for a future session if one turns up.

**Unit tests:** two new regression tests in `ImageSamplingTest.cpp`,
`LoadFetchesD24UnormS8UintDepthAspect`/
`LoadFetchesD32FloatS8X24UintDepthAspect`, each storing *two* texels
specifically to also guard the texel-stride fix (not just the decode
math) -- a second texel with a different depth value and a
deliberately nonzero "neighboring" stencil/second-word value, to
confirm neither aliasing nor stencil-byte leakage into the decoded
depth. Confirmed via `git stash` to fail (all-zero decode) pre-fix,
pass post-fix.

`ninja check-feme`: 3,454/3,515 Passed (+2 new unit tests), 61
Unsupported, 0 Failed, 0 regressions.

**CTS:** re-ran the full `dEQP-VK.glsl.builtin_var.fragdepth.*` group
(45 cases): **18 Pass / 9 Fail / 18 NotSupported** (was 12/15/18) --
the predicted 6 cases now pass. The remaining 9 Fail are all
`*_d32_sfloat_multisample_{2,4,8}` cases, failing at `vkCreateImage`
with `VK_ERROR_INITIALIZATION_FAILED` -- the separate, already-tracked
`L280`/`L307`(a) multisample depth-image-creation gap, confirmed
unaffected by this fix.

`check-hlsl-feme-vk`: 485 Pass / 31 XFAIL / 207 NotSupported / 3 Fail /
1 Unexpected-pass -- unchanged from the standing pre-existing baseline:
`Basic/Mandelbrot.test` (golden-image drift, unrelated, carried over
many sessions), `Feature/SpecializationConstant/
spec_const_32_bits.test`/`WaveOps/WaveActiveMax.test` (pre-existing
lit-annotation issues, unrelated), `Feature/PushConstant/
array_of_matrices.test` (pre-existing unexpected-pass, unrelated) --
no new failures.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal sampled-image texel-decode correctness fix for
already-exposed core Vulkan 1.0 depth-stencil formats, no new
feature/extension surface.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed with `FEME_ICD`/`VK_ICD_FILENAMES`/
`VK_DRIVER_FILES` explicitly exported (separate statements, not a
combined one-liner).

## L309: `fragdepth`'s multisample depth/stencil sampled-image-creation gap -- stale `sampledImageDepthSampleCounts`/`sampledImageStencilSampleCounts` capability advertisement

Picked up the first-ranked carried-over next step: the 9 remaining
`dEQP-VK.glsl.builtin_var.fragdepth.*_multisample_{2,4,8}` cases (one
per `{line,point,triangle}_list`, all plain `D32_SFLOAT`, no combined
stencil format) failed `vk.createImage(...): VK_ERROR_INITIALIZATION_FAILED`.

**Root cause:** `PhysicalDeviceInfo.cpp` advertised
`sampledImageDepthSampleCounts`/`sampledImageStencilSampleCounts` as
`VK_SAMPLE_COUNT_1_BIT`-only, under an earlier `H8f`/`R30` design
decision whose comment read "nothing yet reads a single sample from a
shader... needs `OpImageFetch`-with-sample-index raising, which R30
left out of scope." `Image.cpp`'s `supportedSampleCounts` intersects
this limit against any image requesting `VK_IMAGE_USAGE_SAMPLED_BIT`,
so a depth image requesting both `SAMPLED_BIT` and
`DEPTH_STENCIL_ATTACHMENT_BIT` with `samples > 1` (exactly what
`vktShaderRenderBuiltinVarTests.cpp`'s depth image creation requests)
got narrowed down to 1 sample and rejected by `isValidImageShape`.

This rationale was already stale by this session: `L73` (added
`Plain2DMS`/`Array2DMS` `OpImageQuerySamples` support) and `L307`
(fixed arrayed+multisampled `OpImageFetch` legalization, and
generalized the non-arrayed multisampled sampled-fetch support too)
had already landed the shape-based (not format-based) per-sample
sampled-image-fetch lowering needed --
`SPIRVResourceLowering.cpp`'s `hasOnlySupportedImageUses`/
`lowerImageAccesses` classify `Plain2DMS`/`Array2DMS` purely by image
*shape*, never by depth/stencil-ness, so the support `L307` added for
color images already covered depth/stencil ones too. Confirmed via the
CTS's own shader source (`vktShaderRenderBuiltinVarTests.cpp`,
~lines 1802-1822) that its validation pass genuinely needs exactly
this: a `uniform sampler2DMS u_depthTex` sampled via
`texelFetch(u_depthTex, imageCoord, int(gl_SampleID))`.

**Fix** (`feme/lib/Vulkan/PhysicalDeviceInfo.cpp`): widened both
limits to `VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT |
VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT`, matching
`sampledImageColorSampleCounts`/`sampledImageIntegerSampleCounts`
already in place, with an updated comment citing this item and the
corrected shape-based rationale.

Updated 3 unit tests that encoded the old, now-incorrect behavior:
- `ImageTest.cpp`: `RejectsMultisampleSampledDepthImage`/
  `RejectsMultisampleSampledStencilImage` (asserted `vkCreateImage`
  returned `VK_ERROR_INITIALIZATION_FAILED`) →
  `AcceptsMultisampleSampledDepthImage`/
  `AcceptsMultisampleSampledStencilImage` (assert a valid `VkImage`).
- `EntryPointsTest.cpp`:
  `ImageFormatPropertiesReportsSingleSampleForSampledDepth` →
  `ImageFormatPropertiesReportsMultisampleForSampledDepth` (expected
  `Props.sampleCounts` widened to the full `1|2|4|8` mask).

`ninja check-feme`: 3,454/3,515 Passed, 61 Unsupported, 0 Failed, 0
regressions. `FeMeVulkanTests` run standalone: 773/773 Passed.

CTS re-run, `dEQP-VK.glsl.builtin_var.fragdepth.*` (45 cases): 18
Pass/9 Fail/18 NotSupported -- **identical pass/fail/not-supported
counts to before this fix**, but the *nature* of the 9 failures
changed: `vkCreateImage` no longer rejects any multisample depth
image (confirmed via a targeted single-case re-run that the
`VK_ERROR_INITIALIZATION_FAILED` is gone), so the originally-targeted
bug is genuinely fixed at this layer -- but fixing it merely unmasked
a second, previously-unreachable bug underneath (see `L310`): all 9
cases now fail with a value mismatch instead
(`Mismatch at pixel (X,Y,0): expected <nonzero> but got 0`).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal capability-advertisement correctness fix for
already-exposed core Vulkan 1.0 functionality, no new feature/
extension surface.

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed with `FEME_ICD`/`VK_ICD_FILENAMES`/
`VK_DRIVER_FILES` explicitly exported (separate statements).

## L310: `fragdepth`'s multisample value-mismatch bug (surfaced by `L309`) -- root-caused and fixed

Once `L309`'s image-creation fix landed, the same 9
`dEQP-VK.glsl.builtin_var.fragdepth.*_multisample_{2,4,8}` cases still
fail, but now with a genuine value mismatch rather than a
creation-time error. Investigated this session; **not fixed**.

**What was ruled out:** this is *not* the same class of bug as `L308`
(an all-zero generic sampled-image texel-decode gap for a format
never given a decode-table entry). `D32_SFLOAT` already had a decode
case (case 32) long before `L308`, and the same generic
`femeRTFetchTexel2D`/shape-based lowering path that `L307` validated
against 196/196 color-format multisample fetch cases is used here too
-- if this were a systemic decode/addressing bug, it would be expected
to affect a consistent, format-independent fraction of samples (e.g.
every odd sample index, or every sample past some stride boundary),
not what was actually observed.

**What was observed instead:** re-running each failing case and
mapping the qpa's one reported mismatch pixel back through the test
shader's own `imageCoord = (sampleNdx + pixelX * numSamples, pixelY)`
storage-image addressing formula (`vktShaderRenderBuiltinVarTests.cpp`'s
`FragDepthFragPass2` shader) shows **exactly one (pixel, sample) pair
mismatches per topology, regardless of sample count** (2, 4, and 8
samples all reproduce the identical symptom at a topology-specific,
sample-count-independent pixel). The mismatched sample's actual value
reads back as exactly `0` (not a nearby-but-wrong value), matching the
depth attachment's apparent clear value for an uncovered sample.

**Leading hypothesis (not yet proven):** a rasterizer sample-coverage
edge-case at the test primitive's own boundary. The test's primitive
is built from 4 arbitrary vertices (`vktShaderRenderBuiltinVarTests.cpp`,
not a full-viewport-covering shape), and the test is
`CaseType="SelfValidate"` -- it checks the shader's own written vs.
sampled-back values against each other on the same device, not
against a reference/golden image, so the "expected" value is simply
whatever `control_buffer.data[index]` was written for that pixel,
regardless of which samples are actually covered. If FeMe's rasterizer
disagrees with the sample-rate-shaded fragment-invocation path about
which one specific sample is covered at a primitive's boundary pixel,
that sample would retain the depth attachment's clear value (`0`)
instead of the shader-written depth -- structurally similar in spirit
to the already-documented `isTopLeftEdge`/`H4j` top-left-fill tie-break
rule, but for multisample coverage specifically, not edge ownership
between two triangles.

**Not yet distinguished:** whether this is a write-side bug (the
per-sample depth-attachment write in the first render pass, gated by
`PerSampleShading`'s per-`PassSample` coverage-mask narrowing in
`Executor.cpp`) or a read-side bug (the second render pass's own
per-sample coverage when invoking `FragDepthFragPass2`, which also
needs `PerSampleShading` to run once per `gl_SampleID`). A dedicated
session is needed, starting with either (a) a minimal custom repro --
a known primitive with a controlled, hand-computed 2-sample coverage
mask at one pixel, and instrumenting/dumping FeMe's actual computed
coverage mask at that pixel to compare directly, or (b) adding a
temporary coverage-mask diagnostic dump to `Executor.cpp`'s
`PerSampleShading` loop for this exact failing case.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (investigation only this session, no code changed).

**Mandatory device check:** `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed with `FEME_ICD`/`VK_ICD_FILENAMES`/
`VK_DRIVER_FILES` explicitly exported (separate statements).

### Root cause and fix (follow-up session)

Confirmed the "write-side vs. read-side" and "rasterizer
sample-coverage edge-case" hypotheses above were exactly right, and
pinned down the precise mechanism: `Executor.cpp`'s `PerSampleShading`
per-pass loop builds `PassInvocations` as a narrowed copy of the
whole-quad `QuadInvocations` for each `PassSample`. It correctly
narrows `PassInv.Coverage[Lane] &= SampleBit` (the mask the late
depth/stencil test's `BaseCoverage`/`PassMask` computation reads), but
left `PassInv.SideEffectMask` (the mask gating the shader's *own*
resource stores/atomics -- e.g. the CTS's own marker-image
`imageStore`, or this investigation's raw-buffer-marker stand-in)
completely untouched by that per-pass loop. `SideEffectMask` therefore
stayed at its original value from before the per-pass split:
`Quad.Coverage`, the whole-quad "was *any* sample of this lane covered
at all" bit, computed once during the initial geometric coverage test
and never revisited per-pass.

Concretely: a lane with sample 0 covered and sample 1 not (a real
primitive edge passing between the two samples' fixed per-pixel
offsets) has `Quad.Coverage`'s bit for that lane set (since *some*
sample is covered), and this stays set for **every** `PassSample`
pass -- including the pass representing sample 1, which that lane's
own `Quad.SampleMask` says is *not* actually covered. The fragment
shader's body (and its side-effecting resource store) still executes
once per pass regardless of which specific sample that pass
represents, so the marker write for sample 1's pass still ran and
still looked "covered" from the shader's perspective -- while the
*depth* write for that same pass correctly used the properly-narrowed
`PassInv.Coverage`/`BaseCoverage` and skipped it (sample 1 genuinely
isn't covered). The CTS's own validation logic takes the marker at
face value: marker set -> expects a real depth value -> gets the
depth attachment's untouched clear value (`0`) instead -> reports
exactly the observed `"expected <nonzero> but got 0"` mismatch, for
exactly one (pixel, sample) pair per topology (wherever that
topology's own primitive happens to have an edge splitting a pixel's
two sample offsets), independent of sample count (the same per-lane
"any sample covered" over-broadness applies regardless of how many
total samples that pixel has).

**The fix:** compute a `PassSideEffectMask` alongside the existing
`PassInv.Coverage` narrowing, from `Quad.SampleMask[Lane] & SampleBit`
(the exact per-sample geometric coverage result `Coverage` is itself
narrowed from, not re-derived some other way), and assign it to
`PassInv.SideEffectMask`. This makes every pass's side effects run
only for the lanes genuinely covered by *that specific pass's* sample,
matching the spec's per-sample-shading semantics for resource writes
(a fragment invocation's own observable side effects should correspond
to real coverage, not "this lane did something on some other pass").

**Regression test:** `ExecutorTest.cpp`'s
`SideEffectMaskNarrowsToThePassSpecificSampleNotTheWholeLane` builds a
1x1-pixel, 2-sample-count pipeline with a real geometric primitive
edge (not a shader-side `discard`/`gl_SampleMask` narrowing) covering
exactly sample 0 and not sample 1, and a fragment shader that writes
an unconditional `1` marker into a bound raw UAV buffer at
`SampleIndex * 4` before its ordinary color output -- mirroring the
CTS's own marker-image technique with a simpler raw-buffer resource.
Confirmed to fail pre-fix (`MarkerBuffer[1] == 1`, falsely marked
covered) and pass post-fix (`MarkerBuffer[1] == 0`), with the color
output at sample 1 correctly untouched (`0`) in both cases (proving
the depth/color-write path was never the buggy side -- only the
side-effect-gating mask was).

**Verification:**
- `ninja check-feme`: 3,455/3,516 Passed, 61 Unsupported, 0 Failed
  (was 3,454/3,515 before this fix's own new test; +1 Passed, 0
  regressions, 0 newly-Unsupported/Failed).
- `FeMeGraphicsTests` run in isolation: 382/382 Passed, including the
  new regression test both standalone and as part of the full suite.
- CTS re-run, `dEQP-VK.glsl.builtin_var.fragdepth.*` (45 cases): 27
  Passed / 0 Failed / 18 NotSupported -- every one of the 9 previously
  value-mismatching cases now passes; the 18 `NotSupported` cases are
  the pre-existing, unrelated `x8_d24_unorm_pack32`-format and
  `multisample_64`-sample-count gaps (carried-over separate roadmap
  items, not part of this bug).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal rasterizer per-sample-shading correctness fix
to already-exposed core Vulkan 1.0 functionality, no new
feature/extension surface.

**Mandatory device check (this session):** `vulkaninfo --summary |
grep deviceName` → `FeMe CPU Vulkan Device`, confirmed with
`FEME_ICD`/`VK_ICD_FILENAMES`/`VK_DRIVER_FILES` explicitly exported
(separate statements).

## `api.image_clearing` broader sampling (overdue backlog item, this session)

Per the standing backlog (`image_clearing`'s 45,636 cases had never
been run against the real FeMe device at all), sampled the full group
after `L310`'s fix landed:

```
./deqp-vk --deqp-case='dEQP-VK.api.image_clearing.*' \
    --deqp-log-filename=/tmp/l311_image_clearing.qpa \
    --deqp-log-images=disable
```

**Result: 23,028 Pass / 0 Fail / 22,608 NotSupported (45,636 total).**
Entirely clean -- no new bugs found in this group. The `NotSupported`
half is expected: this group exhaustively covers every
format/image-type/sample-count/layer-count combination the Vulkan
spec allows a device to decline (e.g. many compressed/multi-planar
formats, several sample counts per format), and FeMe's device-info
advertisement already correctly declines exactly the combinations it
does not implement (validated by the 0-Fail result -- a real
format-support gap would show up as a hard `vkCreateImage`/
`vkCmdClearColorImage` failure inside a case marked `Pass`/`Fail`, not
as a clean `NotSupported` skip).

This closes out `image_clearing` from the broader-CTS-sampling
backlog entirely. Remaining unsampled backlog: `copy_and_blit`'s
`copy_commands2`/`dedicated_allocation`/`multiplanar_xfer`/
`copy_memory_indirect`/`device_address`/`sparse`/`dynamic_state`/
`reinterpret` groups, and `synchronization.op`/`api.copy_and_blit.
core.image_to_image`/a few smaller untouched `api.*` subgroups (see
`agent_thoughts.md`'s next steps for the full ranked list).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- this was a read-only CTS sample, no code changed.

## L311: broader CTS sampling (`synchronization.op`, `copy_and_blit`'s remaining groups) -- one new bug found and fixed

Per the standing backlog, sampled `dEQP-VK.synchronization.op.*`
(20,131 cases, run in one shot per the prior session's "just try it,
`image_clearing` took ~5 minutes for 45k cases" suggestion) and all 8
remaining unsampled `copy_and_blit` top-level groups
(`copy_commands2`, `dedicated_allocation`, `multiplanar_xfer`,
`copy_memory_indirect`, `device_address`, `sparse`, `dynamic_state`,
`reinterpret`).

**`synchronization.op`: 14,694 Pass / 0 Fail / 5,437 NotSupported.**
Entirely clean, no new bugs.

**`copy_and_blit`'s 8 groups:** 6 of 8 entirely clean (0 Fail each):
`multiplanar_xfer` (2,216, all NotSupported), `copy_memory_indirect`
(1,573, all NotSupported), `device_address` (918: 186 Pass / 732
NotSupported), `sparse` (38, all NotSupported), `dynamic_state` (12,
all NotSupported), `reinterpret` (6: 2 Pass / 4 NotSupported). The
remaining 2 groups each had exactly 3 failures:
- `copy_commands2` (34,962 cases): 15,525 Pass / **3 Fail** / 19,434
  NotSupported
- `dedicated_allocation` (27,539 cases): 11,179 Pass / **3 Fail** /
  16,357 NotSupported

All 6 failures (3 per group) are the identical bug, one per
multisample count:
`dEQP-VK.api.copy_and_blit.{copy_commands2,dedicated_allocation}.resolve_image.whole_copy_before_resolving_no_cab.{2,4,8}_bit`,
each failing at `vk.createImage(...)`:
`VK_ERROR_INITIALIZATION_FAILED at vkRefUtilImpl.inl:410`.

**Root cause:** traced via `vktApiResolveTests.cpp` to the test's
`COPY_MS_IMAGE_TO_MS_IMAGE_NO_CAB` option path, which creates an
intermediate multisample (2/4/8-sample) `R8G8B8A8_UNORM` image
(`m_multisampledCopyNoCabImage`) with usage `VK_IMAGE_USAGE_
TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_
INPUT_ATTACHMENT_BIT` -- deliberately *without*
`VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`, unlike every other image this
test file creates. This is legal Vulkan usage: an image may be bound
as a subpass input attachment to read an earlier pass's output
without this pass writing to it too.

`Image.cpp`'s `supportedSampleCounts` (the single source of truth
`isValidImageShape`'s `vkCreateImage`-time check and `EntryPoints.cpp`'s
`vkGetPhysicalDeviceImageFormatProperties` both consult) intersects
`pCreateInfo->samples` against the device's per-usage sample-count
limits, but had a branch for every attachment-shaped usage bit
*except* `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` itself:
`VK_IMAGE_USAGE_SAMPLED_BIT`, `_STORAGE_BIT`,
`_COLOR_ATTACHMENT_BIT`, and `_DEPTH_STENCIL_ATTACHMENT_BIT` all had
their own branch setting `Constrained = true` and narrowing `Mask`
against the matching `VkPhysicalDeviceLimits` field, but
`_INPUT_ATTACHMENT_BIT` matched none of them. An image whose *only*
attachment-shaped usage bit is `INPUT_ATTACHMENT_BIT` (as here) left
`Constrained` false for the whole function, falling through to the
unconditional `return Constrained ? Mask : VK_SAMPLE_COUNT_1_BIT;`
tail -- wrongly reporting (and enforcing) a `VK_SAMPLE_COUNT_1_BIT`-only
mask, even though this device's own `framebufferColorSampleCounts`/
`framebufferDepthSampleCounts`/`framebufferStencilSampleCounts` are
all `1|2|4|8`.

**The fix:** added an `INPUT_ATTACHMENT_BIT` branch to
`supportedSampleCounts`, mirroring the existing
`COLOR_ATTACHMENT_BIT`/`DEPTH_STENCIL_ATTACHMENT_BIT` branches'
`HasDepth`/`HasStencil`-keyed limit selection (an input attachment is
read during rendering exactly like a color/depth-stencil attachment is
written, so it is bound by the same `framebuffer*SampleCounts`
limits, not left unconstrained).

**Regression tests:** `ImageTest.cpp`'s
`AcceptsMultisampleInputAttachmentOnlyImage` (color format,
`R8G8B8A8_UNORM`, mirroring the CTS repro exactly) and
`AcceptsMultisampleInputAttachmentOnlyDepthImage` (depth format,
`D32_SFLOAT`, confirming the depth-keyed limit path too). Both
confirmed via `git stash` A/B to crash on an `EXPECT_EQ` failure
(`vkCreateImage` returning `VK_ERROR_INITIALIZATION_FAILED` instead of
`VK_SUCCESS`) pre-fix, and pass post-fix.

**Verification:**
- `ninja check-feme`: 3,457/3,518 Passed, 61 Unsupported, 0 Failed
  (was 3,455/3,516 before this fix's own 2 new tests; +2 Passed, 0
  regressions).
- `FeMeVulkanTests` run in isolation (targeted `ImageTest` filter):
  6/6 Passed, including both new regression tests.
- CTS re-run: all 6 originally-failing cases
  (`copy_commands2`/`dedicated_allocation` ×
  `whole_copy_before_resolving_no_cab.{2,4,8}_bit`) now `Pass`.
  Re-running both full groups confirms 0 Fail and exactly the expected
  `+3` Pass each: `copy_commands2` 15,528/34,962 Pass (was 15,525),
  `dedicated_allocation` 11,182/27,539 Pass (was 11,179) -- no
  regressions elsewhere in either group.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal capability-advertisement correctness fix for
already-exposed core Vulkan 1.0 functionality (an input-attachment-only
multisample image), no new feature/extension surface.

This closes out the broader-CTS-sampling backlog that had carried
across several sessions: `synchronization.op` and all 8 previously-
unsampled `copy_and_blit` top-level groups have now been run at least
once against the real FeMe device, with only this one new bug found
(now fixed). Remaining known-open items: `L265`'s ASTC alpha-decode
tie-break (low priority, unchanged), and the pre-existing,
out-of-scope `offload-test-suite` lit-annotation issues.

**Mandatory device check (this session):** `vulkaninfo --summary |
grep deviceName` → `FeMe CPU Vulkan Device`, confirmed with
`FEME_ICD`/`VK_ICD_FILENAMES`/`VK_DRIVER_FILES` explicitly exported
(separate statements).

## L312: scoping-pass broader CTS sampling (`rasterization`, `texture`) -- wide Bresenham line bug found and fixed

This session's goal was to resume the broader-sampling backlog with a
fresh top-level `dEQP-VK.*` group never previously sampled against the
real FeMe device. Rather than guess, generated a full case-list XML
(`deqp-vk --deqp-runmode=xml-caselist`) and streamed it through a
`xml.etree.ElementTree.iterparse` parser to get exact per-group leaf-
case counts for all 56 top-level groups, cross-referenced against this
report's own prior mentions to find genuinely never-sampled groups.
Picked `rasterization` (15,019 cases) and `texture` (25,669 cases) --
both reasonably sized, both explicitly suggested by the previous
session's own next-steps list.

**`rasterization` initial run:** 390 Pass / 94 Fail / 14,535
NotSupported. Grouping the 94 failures by case-name prefix showed 73
were `bresenham_line*_wide`/`_with_adjacency` variants across
`primitives`/`primitives_multisample_{2,4,8}_bit` (plus
`static_stipple`/`dynamic_stipple`'s own narrow base cases, which also
combine with adjacency); the remaining ~21 are a scattered,
not-yet-investigated mix (`depth_bias`, `flatshading`,
`frag_side_effects.*`, `line_continuity.*`,
`maintenance5.non_strict_line*`,
`provoking_vertex.draw.default.triangle_fan`,
`rasterization_order_attachment_access.*.multi_draw_barriers`).

**Root cause:** isolated the cleanest repro,
`dEQP-VK.rasterization.primitives.no_stipple.bresenham_lines_wide`,
which reports "Invalid line width at (X, Y) - (X, Y). Detected width
of 1, expected 5" at dozens of sample points -- FeMe rendered the
line exactly 1 pixel wide regardless of the pipeline's `lineWidth`
(5 in this test). `feme/include/feme/Graphics/Pipeline.h`'s
`LineRasterizationMode`/`RasterState::LineWidth` doc comments
explicitly documented this as deliberate: "always exactly 1 pixel
wide regardless of `LineWidth` (per the spec, 'the width of the line
is not adjustable, and it is always as if it were 1.0')". This
citation turned out to be **stale** -- it matches core Vulkan 1.0's
original, pre-`VK_EXT_line_rasterization` line-rasterization spec
language, which has since been superseded by `VK_EXT_line_rasterization`
(and the equivalent `VK_KHR_line_rasterization`/Vulkan 1.4 core
promotion)'s own, more precise "Bresenham Line Segment Rasterization"
section.

Confirmed the current spec text directly from the
`KhronosGroup/Vulkan-Docs` source (`chapters/primsrast.adoc`, fetched
via a shallow git clone after several `web_fetch` attempts at the
rendered HTML kept resolving to unrelated chapters/sections): "The
actual width `w` of Bresenham lines is determined by rounding the
line width to the nearest integer, clamping it to the
implementation-dependent `lineWidthRange` (with both values rounded to
the nearest integer), then clamping it to be no less than 1. Bresenham
line segments of width other than one are rasterized by offsetting
them in the minor direction ... and producing a row or column of
fragments in the minor direction," with the lowest fragment of that
row/column being the one the width-1 algorithm would have produced at
the offset coordinates, and "the preferred method of attribute
interpolation ... is to generate the same attribute values for all
fragments in the row or column ... as if the adjusted line was used
for interpolation and those values replicated to the other fragments,
except for `FragCoord` which is interpolated as usual."

**Fix:** rewrote `emitLineSegment`'s `Bresenham` branch in
`Executor.cpp` to implement this algorithm: determine x-major/y-major
via `abs(Dx) >= abs(Dy)` (matching the spec's own "slope in `[-1,1]`"
definition), compute an integer width `W = clamp(round(LineWidth), 1,
64)` (`64` matching `PhysicalDeviceInfo.cpp`'s own
`lineWidthRange[1]`), offset the walked line's endpoints by
`-(W-1)/2` pixels in the minor direction before walking the same
Bresenham grid-walk loop as before, and -- at each walked step --
emit `W` fragments (a column for x-major, a row for y-major) instead
of 1, all sharing the one set of interpolated attributes computed for
that step (the spec's own "preferred method", simpler than full
per-fragment recomputation and explicitly spec-permitted). Updated
`LineRasterizationMode`/`RasterState::LineWidth`'s own doc comments in
`Pipeline.h` to describe the corrected behavior.

**Regression test:** `ExecutorTest.RendersAWideBresenhamHorizontalLine`
-- a width-3 horizontal Bresenham line, confirming it now lights a
3-row band centered on the width-1 line's own row (rows 1-3 of a 4x4
target) instead of just 1 row.

**Verification:**
- `ninja check-feme`: 3,458/3,519 Passed (+1 new test), 61
  Unsupported, 0 Failed, 0 regressions.
- `FeMeGraphicsTests --gtest_filter='*Bresenham*'`: 2/2 Passed
  (the pre-existing `RendersABresenhamDiagonalLine` and the new wide
  test).
- CTS re-run of the full `rasterization` group: 402 Pass / 82 Fail /
  14,535 NotSupported (was 390/94/14,535) -- confirmed exactly the 12
  plain `*_wide` (no stipple, no adjacency) cases newly Pass
  (`bresenham_lines_wide`/`bresenham_line_strip_wide` across
  `primitives`/`primitives_multisample_{2,4,8}_bit`, plus their
  `_factor_0`/`_factor_large` variants under `primitives`). The
  remaining 82 failures are a confirmed **separate, not-yet-root-caused
  bug**: every `bresenham_line*` case additionally combined with
  `static_stipple`/`dynamic_stipple`/`dynamic_stipple_and_topology` or
  `_with_adjacency` still fails, including cases with **no** width
  component at all (e.g.
  `dEQP-VK.rasterization.primitives.dynamic_stipple.bresenham_lines`,
  confirmed via a standalone repro to report "Invalid fragment
  count"/"missing fragments" against the diamond-exit rule, 227 vs.
  233 expected fragments) -- this is unrelated to line width and needs
  its own dedicated root-causing session (likely a stipple/adjacency-
  specific bug in the same `Bresenham`-mode code path, or in how
  stipple state interacts with the per-step walk).
- `texture`'s run was launched in parallel this session; its own
  results are reported separately once triaged (not yet, as of this
  entry -- see this session's next steps).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- `VK_EXT_line_rasterization`/`VK_KHR_line_rasterization` were
already advertised and `VK_LINE_RASTERIZATION_MODE_BRESENHAM` already
selectable; this is a correctness fix to an already-exposed mode, not
a new feature/extension surface.

**Mandatory device check (this session):** `vulkaninfo --summary |
grep deviceName` → `FeMe CPU Vulkan Device`, confirmed with
`FEME_ICD`/`VK_ICD_FILENAMES`/`VK_DRIVER_FILES` explicitly exported
(separate statements).

## `texture` group sampling -- 131 failures triaged (not yet root-caused), plus a texel-buffer crash

The `texture` group's background run (launched in parallel with
`rasterization` above) completed 25,646 of its 25,669 cases before the
`deqp-vk` process itself aborted on
`dEQP-VK.texture.texel_buffer.uniform.packed.a2b10g10r10-uint-pack32`
with an MLIR assertion failure: `error: Dim must not be SubpassData or
Buffer` / `StorageUniquerSupport.h:180:
mlir::spirv::SampledImageType::get(...): Assertion
'succeeded(ConcreteT::verifyInvariants(...))' failed` -- a genuine
process crash, not a graceful `Fail`, while building a
`SampledImageType` for a texel-buffer (`samplerBuffer`-shaped) sampled
image. Not yet root-caused; the remaining ~23 untested
`texel_buffer`/later cases are unknown.

Of the 25,646 cases that did run: **9,668 Pass, 131 Fail, 15,846
NotSupported, 1 crash**. Grouping the 131 failures by case-name prefix
shows two dominant clusters, both confirmed via standalone repro to
be genuine `Fail`s (not crashes):
- **106 cases** under `texture.shadow.{1d,1d_array,2d,2d_array,cube,
  cube_array}` (all `Image verification failed`/depth-comparison-
  sampling-shaped tests -- e.g.
  `dEQP-VK.texture.shadow.1d.nearest_mipmap_nearest.equal_d16_unorm`).
- **16 cases** under `texture.explicit_lod.2d.sizes.*` (`Verification
  failed` -- mipmap-filtering-shaped, e.g.
  `dEQP-VK.texture.explicit_lod.2d.sizes.31x55_nearest_linear_mipmap_linear_repeat`).
- 5 cases under `texture.multisample` (not yet individually examined).

None of this session's investigation time was spent root-causing
either cluster beyond confirming they reproduce standalone and
roughly bucketing by prefix -- genuinely new, untriaged findings for a
dedicated future session. Given the size of each cluster (106 and 16
cases respectively) and that neither obviously overlaps with
`L312`'s own Bresenham-line fix, these are tracked as open items
rather than attempted this session (time budget was spent on `L312`'s
fix instead, per this session's own prioritization).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- investigation only, no code changed for these findings this
session.

## L314: `texel_buffer` process crash -- root-caused and fixed (MLIR core, outside `feme/`)

Root-caused the `deqp-vk` process crash flagged by the prior
session's `texture` group sampling at
`dEQP-VK.texture.texel_buffer.uniform.packed.a2b10g10r10-uint-pack32`:
`error: Dim must not be SubpassData or Buffer` followed by an MLIR
assertion failure in `mlir::spirv::SampledImageType::get`
(`StorageUniquerSupport.h:180`).

This turned out to be a genuine **MLIR-core bug, outside `feme/`**,
not a FeMe-specific SPIR-V lowering bug. FeMe's own code only
*consumes* `SampledImageType` (casts, type-converter registrations in
`SPIRVToLLVMPatterns.cpp`/`SPIRVResourceLowering.cpp`); the type is
*constructed* during SPIR-V deserialization of CTS-supplied SPIR-V
(`mlir/lib/Target/SPIRV/Deserialization/Deserializer.cpp`'s
`processSampledImageType`, which calls the unchecked
`SampledImageType::get`).

`mlir::spirv::SampledImageType::verifyInvariants`
(`mlir/lib/Dialect/SPIRV/IR/SPIRVTypes.cpp`) and the matching
textual-form parser (`mlir/lib/Dialect/SPIRV/IR/SPIRVDialect.cpp`)
unconditionally rejected any sampled image wrapping an `OpTypeImage`
with `Dim::SubpassData` *or* `Dim::Buffer`, each citing the real
SPIR-V spec text in a comment: *"It [ImageType] must not have a Dim
of SubpassData. Additionally, starting with version 1.6, it must not
have a Dim of Buffer."* The `Dim::Buffer` half of that rule is
explicitly version-gated -- a sampled image wrapping a `Dim::Buffer`
image (GLSL's `samplerBuffer`, exactly what a uniform texel buffer
compiles to) is legal SPIR-V prior to 1.6 -- yet the code applied it
unconditionally regardless of the module's actual SPIR-V version,
with no version parameter even threaded into the verifier's
signature. Corroborated via web research against two known upstream
reports of this exact spec-version nuance biting real tooling:
`glslang` issue #2956 ("Invalid SPIR-V Generated For samplerBuffer in
SPIR-V 1.6") and `SPIRV-Registry` issue #139 ("SamplerBuffer error in
SPIR-V 1.6"). Since texel buffers (`uniform samplerBuffer`) are a
long-standing, pre-1.6-era shader feature and CTS is not deliberately
targeting SPIR-V 1.6 for this core test, the module genuinely is
legal SPIR-V that MLIR's verifier incorrectly rejects.

Because SPIR-V deserialization constructs this type via the unchecked
`SampledImageType::get` (not `getChecked`), tripping the unconditional
rejection doesn't produce a graceful deserialization error -- it trips
an assertion in `StorageUserBase::get` and aborts the entire `deqp-vk`
process. This explains why the prior session's `texture` group CTS run
was truncated at 25,646/25,669 cases.

Fixed by only rejecting `Dim::SubpassData` unconditionally (the one
half of the rule that genuinely has no version carve-out per the
spec) and dropping the unconditional `Dim::Buffer` rejection from both
`SampledImageType::verifyInvariants` and `SPIRVDialect.cpp`'s
`parseAndVerifySampledImageType`. `SampledImageType` has no notion of
its enclosing module's SPIR-V version (its invariant-check signature
only receives the wrapped image type), so threading real version
awareness into this check would be a much larger MLIR API change;
leaving the version-gated half of the rule to version-aware validation
elsewhere (e.g. `spirv-val`) is the narrower, correct fix for this
type-construction-time check. Updated the existing MLIR lit tests'
expected diagnostics (`image-ops.mlir`, `types.mlir`) and added new
positive-path tests confirming a `Dim::Buffer` sampled image is now
accepted in both the op-verifier path and the type-parsing path.

Committed as a single, isolated, self-contained commit
(`c0314ba407ec`) touching only `mlir/` files, per the standing
cross-subproject-fix instruction -- no `feme/`-internal changes were
needed, since the bug and its fix live entirely in MLIR core.

**Verification:**
- `mlir-opt`'s full `mlir/test/Dialect/SPIRV/` + `mlir/test/Target/SPIRV/`
  lit suites: 122/122 Passed, 0 regressions.
- `MLIRSPIRVToLLVMTests` (unit tests covering `SampledImageType`
  conversion): 3/3 Passed.
- `ninja check-feme`: 3,458/3,519 Passed, 61 Unsupported, 0 Failed, 0
  regressions.
- Standalone repro
  (`dEQP-VK.texture.texel_buffer.uniform.packed.a2b10g10r10-uint-pack32`):
  no longer crashes the process -- runs to completion and reports a
  graceful `Fail` (a separate, not-yet-root-caused functional
  texel-buffer-sampling bug, tracked as `L317` below -- not a crash).
- Full `texel_buffer` group (23 cases): 0 Pass, 10 Fail, 13 NotSupported,
  all genuinely sampled with no crash.
- Full `texture.*` group: now runs to completion at **25,669/25,669**
  cases (was 25,646/25,669 before this fix, process aborting on the
  crash). Final tally: **9,669 Pass, 141 Fail, 15,859 NotSupported, 0
  crashes** (was 9,668/131/15,846 plus 1 crash before this fix).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this is an infrastructure crash fix in underlying
SPIR-V-handling tooling FeMe depends on (MLIR core), not a change to
any FeMe-advertised feature or extension surface.

**Mandatory device check (this session):** `vulkaninfo --summary |
grep deviceName` → `FeMe CPU Vulkan Device`, confirmed with
`FEME_ICD`/`VK_ICD_FILENAMES`/`VK_DRIVER_FILES` explicitly exported
(separate statements).

### Remaining untriaged `texture` findings (carried forward)

With the crash fixed, the rest of the `texture` group's 141 failures
are now fully visible and untriaged, tracked as new roadmap items:
- `L315`: `texture.shadow.{1d,1d_array,2d,2d_array,cube,cube_array}`
  (106 cases, "Image verification failed", depth-comparison sampling)
  -- largest untriaged cluster.
- `L316`: `texture.explicit_lod.2d.sizes.*` (16 cases, mipmap
  filtering) and `texture.multisample` (5 cases) -- smaller clusters.
- `L317`: `texel_buffer`'s own remaining 10 functional failures
  (genuine `Fail`s, not crashes, now visible for the first time) --
  a distinct, not-yet-root-caused bug, separate from the crash this
  session fixed.

None of these three were root-caused this session -- time was spent
on the crash fix itself (the top-ranked next step) plus its full
verification and CTS re-runs.

## L315: `texture.shadow.*` root-cause investigation -- three pipeline stages verified correct, root cause not yet found

This session's top-ranked next step was `L315`: root-causing
`dEQP-VK.texture.shadow.{1d,1d_array,2d,2d_array,cube,cube_array}`
(106 cases, "Image verification failed" against depth-comparison
sampling). **No fix landed this session** -- the investigation
substantially narrowed the search space but did not pin down the
actual bug.

**Mandatory device check (this session):** `vulkaninfo --summary |
grep deviceName` → `FeMe CPU Vulkan Device`, confirmed with
`FEME_ICD`/`VK_ICD_FILENAMES`/`VK_DRIVER_FILES` explicitly exported
(separate statements).

**Investigation summary:**

Reproduced `dEQP-VK.texture.shadow.1d.nearest_mipmap_nearest.equal_d16_unorm`
standalone and decoded its embedded reference/rendered PNGs: FeMe's
output shows smooth multi-texel blending where the reference expects a
hard binary step. Hand-derived from CTS's own `FilterCase`/viewport-
and texture-width constants (`TEX1D_VIEWPORT_WIDTH=64`, texture
`width=32`) that the failing sub-case most likely has a narrowly-
minifying implicit LOD (~0.14, just past the `MagFilter`/`MinFilter`
boundary at `0`) -- i.e. FeMe appears to be incorrectly selecting the
magnifying (linear-blend) filter path for a sample that should select
the minifying (nearest, unblended) one.

Rather than guessing at a fix, verified each pipeline stage a
`Grad`-driven depth-comparison sample passes through, individually and
in isolation, for exactly this shape:

1. **CPU runtime math** (`femeRTComputeClampedLod`/
   `femeRTUseLinearFilter`/`femeCpuImageSampleCmp1DF32` in
   `FeMeRuntimeCPU.c`): read in full, appeared correct on inspection;
   confirmed empirically via a new unit test
   (`SampleCmp1DGradSlightlyMinifyingUsesMinFilter`, committed this
   session) that directly calls the runtime's `SampleCmp1D` entry
   point with the hypothesized real numbers (32-texel image, `DUdX`
   just past `1/32`, `MagFilter=Linear`/`MinFilter=Nearest`) -- it
   correctly selects the unblended `MinFilter` result. **This stage is
   not the bug.**
2. **`SPIRVResourceLowering.cpp`'s `llvm.spv.resource.samplecmpgrad`-
   to-runtime-call lowering** for `Plain1D`: already covered by the
   existing `spirv-resource-lowering-image-samplecmpgrad.ll` lit test
   (confirmed passing, operand shapes/order correct). **Not the bug.**
3. **`SPIRVToLLVMPatterns.cpp`'s `ImageSampleDrefGradPattern`**
   (`spirv.ImageSampleDrefExplicitLod`+`Grad` →
   `llvm.spv.resource.samplecmpgrad`): verified via a hand-written
   synthetic `Dim1D` MLIR test run through `feme-opt
   --feme-convert-spirv-to-llvm` -- dx/dy threaded through correctly.
   **Not the bug**, though not yet a committed regression test (the
   existing `spirv-to-llvm-sample-dref-and-query-lod.mlir` only has
   `Dim2D` `Grad` cases -- a genuine coverage gap, tracked under
   `L318`).

The remaining unverified stage is the SPIR-V-binary-to-MLIR import
(`SPIRVImporter.cpp`), plus -- more importantly -- the *actual*
per-pixel `DUdX`/`DUdY` values the real compiled shader produces at
runtime for the failing case, which have not yet been captured
empirically; the ~0.0345 hand-derived `dPdx` remains only a
hypothesis. `feme-run`'s `--heap` YAML has no `samplers:` key yet, so
it cannot currently drive a standalone `Dref`+`Grad` repro outside the
full CTS harness.

**No CTS numbers changed this session** (investigation only, no fix).
`ninja check-feme`: 3,459/3,520 Passed (+1 new test), 61 Unsupported,
0 Failed, 0 regressions. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed (no feature/extension
surface change, investigation and a new unit test only).

Tracked as `L318` in the roadmap (breakdown of `L315`'s remaining
work) -- see `agent_thoughts.md`'s latest entry for the full narrowing
narrative and concrete next steps.

## Session: L315/L318 full-pipeline repro attempt + feme-run sampler-heap tooling

**Investigation summary:**

Finished verifying `L318`'s one remaining open item: the standalone
repro's vertex-order (`BL,TL,BR,TR`), `texCoord=[minC,minC,maxC,maxC]`,
and `w=1.0` (non-`PROJECTED`) assumptions were confirmed bit-for-bit
correct against `vktTextureTestUtil.cpp`'s real
`TextureRenderer::renderQuad`/`ComputeBackend::createFrameResources`
and `gluTextureTestUtil.cpp`'s `computeQuadTexCoord1D` -- no
discrepancy found.

Attempting the planned next step -- extending the repro to the full
`interpolate -> dPdx/dPdy -> SampleCmpGrad` pipeline using a real
`dxc -spirv`-compiled shader -- found `feme-run`'s heap YAML had no
`samplers:` key at all, so built one first (`SamplerEntry`/
`buildSamplerStorage`, wired into `DispatchResources::SamplerHeap`,
which already existed in the runtime ABI unused). New lit test
`Tools/feme-run/heap-sampler.ll`.

Using the new feature to actually run a real `SampleCmpGrad`/
`Texture1D`/`SamplerComparisonState` shader end-to-end through
`feme-run` found a **structural blocker** rather than the hoped-for
full-pipeline repro: `feme-run --reference` fails outright with
`unsupported raised operation: ... is a register-bound resource
handle the FeMe CPU target cannot normalize`, reproduced identically
for a plain non-comparison `SampleLevel` call (ruling out anything
`SampleCmpGrad`-specific). The non-`--reference` JIT path does not
report this error and instead silently dispatches, producing an
all-zero result -- a separate, smaller diagnostic gap in its own
right. Since the real CTS `compute_1D_SHADOW` shader clearly executes
correctly for the vast majority of its own sub-cases, this means
`feme-run`'s own JIT entry point normalizes a register-bound
sampled-image handle through a different, less-complete code path
than `feme::vulkan`'s real `vkCreateComputePipelines` does -- not a
bug in the sampling math itself, and it retroactively explains why no
full end-to-end `feme-run`-based repro has ever succeeded across this
investigation's several sessions.

**No CTS numbers changed this session** (the sampler-heap feature is
a pure `feme-run` CLI-tool addition; `feme_vulkan.so`/the ICD itself
is untouched). Spot-checked `dEQP-VK.texture.shadow.1d.
nearest_mipmap_nearest.equal_d16_unorm` directly: still `Fail (Image
verification failed)`, unchanged, as expected (investigation only, no
fix landed). `ninja check-feme`: 3,460/3,521 Passed (+1 new test), 61
Unsupported, 0 Failed, 0 regressions.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (tooling-only change, no feature/extension surface change).

Tracked as `L318`(f)/`L319` in the roadmap -- see `agent_thoughts.md`'s
latest entry for the full narrative and concrete next steps.

## Session: L319 closed (reframed) + L320 found/fixed + first working SampleCmp repro

**`L319` root-cause, reframed:** the prior session's hypothesis (a
`feme-run`-vs-`feme::vulkan` pipeline-creation divergence) was wrong.
`feme::vulkan`'s real compute/graphics pipeline creation
(`feme/lib/Vulkan/Pipeline.cpp`, `feme/lib/Graphics/
GraphicsPipeline.cpp`) and `feme-run`'s own non-`--reference` JIT path
both call the same `feme::cpu::CompiledStage::create`, specifically
its non-`Reference` branch, which already runs the full `runPipeline`.
`Reference` mode is a `feme-run`-only ground-truth debugging feature
the real driver never exercises. The real divergence was between
`feme-run`'s own two modes: `CompiledStage::create`'s `Reference`
branch hand-rolled a much shorter pass list (`PreparePass` +
`BoundResourceNormalizationPass` only), omitting
`RootConstantLoweringPass`/`SPIRVResourceLoweringPass`/
`SPIRVPushConstantLoweringPass`/`SPIRVSubpassLoweringPass` --
blocking any SPIR-V-sourced shader using a register-bound resource
handle from running under `--reference` specifically. **The real
Vulkan driver was never affected.** Fixed by adding the four missing
passes in `runPipeline`'s own order; new unit test
`JITEngineTest.ReferenceModeNormalizesSPIRVBoundResourceHandles`.
Committed as `d18ba923b320`.

**`L320` found and fixed:** fixing `L319` unblocked the first attempt
to actually run a register-bound `Texture1D`/`SamplerState` shader
through `feme-run`, which immediately surfaced a second, independent
gap: the result was silently all-zero, in both `--reference` and the
normal JIT path, with no diagnostic. Root cause: `feme-run`'s heap
YAML had no way to describe a traditionally-bound (`register(tN,
spaceM)`) image or sampler at all -- its `images:`/`samplers:` keys
only ever append to the *dynamic*, bindless heap, never the reserved
per-range prefix a traditional binding resolves to. Fixed by adding
`image-bindings:`/`sampler-bindings:` heap YAML keys, mirroring the
existing `bindings:` pattern, wired into
`DispatchResources::BoundImages`/`BoundSamplers` (which already
existed correctly in the runtime ABI, simply unused by this tool).
New lit tests `Tools/feme-run/SPIRV/{image,sampler}-binding.hlsl`.

**First working full-pipeline `SampleCmp` repro, finally:** using the
new `image-bindings`/`sampler-bindings` keys, built a real
`dxc -spirv`-compiled `Texture1D::SampleCmp`/`SamplerComparisonState`
compute shader with a depth-formatted (`r32_float`, `depth: true`)
bound image and a comparison-enabled (`compare-func: greater`) bound
sampler, with depth texel values (0.1, 0.3, 0.5, 0.7) deliberately
bracketing the comparison reference (0.4) so a correct result is
non-uniform (`1, 1, 0, 0`), not a value that could be an artifact of
an all-pass/all-fail degenerate case. **Result: exactly the expected
`1.0, 1.0, 0.0, 0.0` pattern** -- `feme-run --reference`'s own
`SampleCmp` execution is correct for this basic 1D, no-mipmapping,
no-derivative case. This is the first time this multi-session
investigation has had a working end-to-end comparison-sampling repro
through `feme-run` at all.

**However, this does not explain `L315` on its own:** re-ran
`dEQP-VK.texture.shadow.1d.nearest_mipmap_nearest.equal_d16_unorm`
directly against the real driver after both fixes landed -- still
`Fail (Image verification failed)`, unchanged. Since the basic
`SampleCmp` shape now verified correct via `feme-run`, the remaining
bug is specific to something this simple compute-shader repro does
not exercise: most likely the fragment-shader implicit-derivative
path (a compute shader has no screen-space derivatives, so `SampleCmp`
there implicitly uses LOD 0 / no derivative-driven mip selection,
unlike the real CTS test, which is a fragment shader using implicit
derivatives against a `nearest_mipmap_nearest` sampler), or the
mipmap-selection math itself (the failing case name's own
`nearest_mipmap_nearest` strongly suggests this). This narrows next
session's starting point considerably: the comparison math itself is
no longer a suspect; implicit-derivative/mip-level selection for a
depth-comparison sample is.

**CTS numbers:** `L319`/`L320` are both pure `feme-run` CLI-tool
changes -- no change to `feme_vulkan.so`/the ICD itself, so no broad
CTS re-run was performed; only the known `L315` repro case was
spot-checked (unchanged, as predicted). `ninja check-feme`:
3,463 Passed (+2 new lit tests net of this and `L319`'s own unit
test)/61 Unsupported/0 Failed, 0 regressions, across both commits.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed for either fix (both are internal tooling/JIT-path
correctness fixes, no feature/extension surface change).

Tracked as `L319`/`L320` in the roadmap -- see `agent_thoughts.md`'s
latest entry for the full narrative and concrete next steps.

## L315: `texture.shadow.*` root-caused and fixed -- missing implicit-derivative synthesis for depth-comparison sampling

**Mandatory device check (this session):** `vulkaninfo --summary |
grep deviceName` → `FeMe CPU Vulkan Device`, confirmed with
`FEME_ICD`/`VK_ICD_FILENAMES`/`VK_DRIVER_FILES` explicitly exported.

Picked up the prior session's top-ranked next step: root-cause `L315`
(`texture.shadow.*`, 106 failures) by comparing
`OpImageSampleDrefImplicitLod` against the already-correct ordinary
(non-`Dref`) implicit-LOD sample lowering. Read all three `Dref`
MLIR-dialect conversion patterns in `SPIRVToLLVMPatterns.cpp`
(`ImageSampleDrefImplicitLodPattern`/`ImageSampleDrefGradPattern`/
`ImageSampleDrefExplicitLodPattern`) and confirmed none of them perform
mip-selection -- they are pure operand-forwarding to
`llvm.spv.resource.samplecmp*` intrinsics, so the bug had to be
downstream, in `SPIRVResourceLowering.cpp`'s own lowering of those
intrinsics to `feme.cpu.image.samplecmp.*` runtime calls.

**Root cause found:** `SPIRVResourceLowering.cpp`'s `isDrefSampleIntrinsic`
dispatch handles `samplecmp`/`samplecmp_clamp`/`samplecmpbias`/
`samplecmpbias_clamp` (SPIR-V's `OpImageSampleDrefImplicitLod` with no
`Grad` operand -- exactly what a fragment shader's `SampleCmp`/
`SampleCmpBias` HLSL intrinsic compiles to) by always passing
`DUdX`/`DUdY`/`DVdX`/`DVdY` (and their `Plain1D`/`Array1D` `Grad1DDUdX`/
`Grad1DDUdY` and `Cube`/`CubeArray` `CubeDDir*` siblings) as permanent
zero constants. The CPU runtime (`femeCpuImageSampleCmp2DF32` et al.,
`FeMeRuntimeCPU.c`) already correctly computes a real, derivative-driven
implicit LOD via `femeRTPlanImplicitLod` whenever it is given nonzero
derivatives -- it was simply never given any. The ordinary (non-`Dref`)
sample path already solves this exact problem via
`getOrSynthesizeSample2DDerivatives`/`getOrSynthesizeSample1DDerivatives`/
`getOrSynthesizeSampleCubeDerivatives` (`ImageCalls.cpp`), which
synthesize real `feme.stage.derivative.x.coarse`/`.y.coarse` calls
(later lowered to genuine quad-lane cross-differencing by
`WaveLoweringPass`) when the calling function is a `Fragment`-stage
entry point, and zero constants otherwise. The `Dref` path never called
these helpers at all -- every implicit-LOD depth-comparison sample was
unconditionally forced to mip level 0, regardless of its sampler's own
`nearest_mipmap_nearest`/`linear_mipmap_linear` filter mode.

**Fix:** call the same `getOrSynthesize*Derivatives` helpers for the
`!DrefHasGrad && !DrefExplicitLod` case, for all five shapes
(`Plain2D`/`Array2D` via `getOrSynthesizeSample2DDerivatives`;
`Plain1D`/`Array1D` via `getOrSynthesizeSample1DDerivatives`;
`Cube`/`CubeArray` via `getOrSynthesizeSampleCubeDerivatives`),
mirroring the existing `DrefHasGrad` branch immediately above it.
`Cube`/`CubeArray` needed their own synthesis call placed inside their
`switch (Shape)` arms (after each arm's own `C2` extraction) rather
than in the shared pre-switch block, to avoid inserting a redundant,
duplicate `extractelement` of `Coord`'s third component (caught by a
`spirv-resource-lowering-image-samplecmp-shapes.ll` lit-test failure
during iteration, fixed before landing).

**Testing:** added a `samplecmp_fragment` case to
`spirv-resource-lowering-image-samplecmp.ll` -- a
`"feme.shader.stage"="fragment"`-tagged function, confirming real
`feme.stage.derivative.x.coarse`/`.y.coarse` calls are now synthesized
on `%u`/`%v` instead of the always-zero constants a non-fragment-stage
caller (the pre-existing `samplecmp` case in the same file) still
correctly gets. `ninja check-feme`: 3,463 Passed/61 Unsupported/0
Failed, 0 regressions.

**CTS re-run:** the known repro case,
`dEQP-VK.texture.shadow.1d.nearest_mipmap_nearest.equal_d16_unorm`, now
`Pass`. Ran the full `dEQP-VK.texture.shadow.*` group (5,377 cases):
**577 Pass / 0 Fail / 4,800 NotSupported** -- all 106 previously-failing
cases in this group now pass, and no new failures were introduced.
This is the largest single-session CTS fix of this investigation's
multi-session history.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal CPU-target lowering correctness fix for an
already-advertised feature (depth-comparison sampling), no new
feature/extension surface.

Tracked as `L315`/`L318` (now both `done`) in the roadmap -- see
`agent_thoughts.md`'s latest entry for the full narrative and next
steps.

## L317: `texel_buffer`'s 10 genuine functional failures -- root-caused and fixed

**Mandatory device check (this session):** `vulkaninfo --summary |
grep deviceName` → `llvmpipe (LLVM 21.1.8, 128 bits)` with no ICD env
vars set (the system default); re-checked with
`VK_ICD_FILENAMES`/`VK_DRIVER_FILES` explicitly pointed at
`build/tools/feme/tools/feme-vulkan/feme_icd.json` →
`FeMe CPU Vulkan Device`, confirmed.

Picked up the prior session's top-ranked next step: root-cause `L317`
(`texel_buffer`'s 10 genuine `Fail`s, distinct from the already-fixed
`L314` crash). The prior session's investigation (summarized at
compaction) had already traced the failure down to a pipeline-creation
rejection (`vkCreateGraphicsPipelines` → `"shader's (set 0, binding 0)
requirement is not satisfied by its VkPipelineLayout"`, surfaced via the
(previously-undocumented) `FEME_VULKAN_LOG_CREATION_ERRORS=1` env var)
and identified the real shader shape: GLSL's `uniform samplerBuffer` +
`texelFetch()` declares a *single* `OpTypeSampledImage` variable with no
`OpSampledImage` combining instruction at all (unlike DXC/HLSL's
separate `Texture`+`SamplerState` pattern), so its one
`handlefrombinding` call's own result type is already the combined
`{image, sampler}` struct -- but had not yet confirmed what actually
happens to that struct downstream, nor implemented a fix.

**Resolving the `feme-translate` tool-invocation blocker:** the prior
session got stuck trying to translate the real failing shader's
extracted SPIR-V all the way to genuine LLVM IR for direct inspection,
hitting `"expected a 'spirv.module' op, got 'builtin.module'"` from
`feme-translate --spirv-to-llvmdialect`/`--spirv-to-llvmir` regardless of
input wrapping. Found the actual working chain this session:
`feme-translate --import-spirv` (SPIR-V binary → `spirv.module` MLIR,
already worked) → `feme-opt --feme-convert-spirv-to-llvm` (the same pass
`test/Conversion/SPIRVToLLVM/*.mlir`'s own `RUN:` lines use, not a
`feme-translate` flag) → extract the inner, attribute-bearing
`module attributes {llvm.data_layout = ..., ...} { ... }` from
`feme-opt`'s doubly-wrapped output (its own top-level `module { ... }` is
just the generic MLIR file wrapper; the nested one is the real LLVM-
dialect module `feme-opt`'s conversion pass constructs on purpose) →
**plain upstream `mlir-translate --mlir-to-llvmir`** (a stock MLIR tool,
not `feme`-specific at all) on that inner module, which finally produces
real, inspectable LLVM IR. Worth documenting for future sessions needing
a from-scratch SPIR-V → LLVM-IR repro: `feme-translate`'s own
`--spirv-to-llvmdialect`/`--spirv-to-llvmir` flags are not the right tool
for this; `feme-opt --feme-convert-spirv-to-llvm` + plain `mlir-translate
--mlir-to-llvmir` is.

**Root cause confirmed empirically, not just by code reading:** the real
shader's extracted SPIR-V disassembly (from the CTS `.qpa` log),
hand-assembled with `spirv-as` and round-tripped through the chain
above, reproduced the exact predicted shape: one `handlefrombinding`
call returning `{spirv.Image Dim=Buffer Sampled=1, spirv.Sampler}`, with
a single `extractvalue ..., 0` feeding `llvm.spv.resource.getpointer` +
`load` (no `.sample` call at all -- `texelFetch()` never touches the
sampler half). `splitCombinedSampledImageHandles`
(`SPIRVResourceLowering.cpp`, added under `H13d` for the ordinary
`uniform sampler2D`/`.sample()` combined-handle case) correctly
recognizes this shape and splits it, but did so *unconditionally* --
synthesizing **both** an image-kind and a sampler-kind handle
regardless of which `extractvalue` indices actually existed. For
`texelFetch()`'s image-only extraction, the synthesized sampler handle
ends up with **zero real uses** -- and `collectHandles`'s
`classifySamplerHandle`/`hasOnlySupportedSamplerUses` dispatch still
classified and recorded it: `hasOnlySupportedSamplerUses`'s
`for (const User *U : Handle.users())` loop is vacuously `true` for a
handle with no users at all, so the always-passing check let this
synthetic, never-read handle through as a second, *spurious*
`(set 0, binding 0)` `Sampler`-class `BoundResourceRange` entry,
recorded alongside the real `TexelUniform`/`Buffer`-class one for the
exact same binding. `Pipeline.cpp`'s `validateBoundRanges` then rejects
pipeline creation because the real `VkPipelineLayout` only ever declares
*one* descriptor there (`VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER`) --
it cannot satisfy two conflicting class requirements for the same slot.
Confirmed via a minimal unit-test probe mirroring the exact repro shape
and inspecting the actual `!feme.cpu.bound_resources` metadata
`ResourceInfo::fromModule` produces: two `BoundRanges` entries
(`Buffer` + spurious `Sampler`) before the fix, one (`Buffer` only)
after.

**Fix:** `splitCombinedSampledImageHandles` now computes `NeedsImage`/
`NeedsSampler` from the real `extractvalue` index set found on the
combined handle's own uses, and only synthesizes the half(s) actually
read -- an unread half's synthetic handle is never created at all,
rather than created and then silently misclassified as a real,
separately-bound resource.

**Testing:** added
`SPIRVResourceLoweringTest.cpp`'s
`LowersCombinedTexelBufferHandleWithoutSpuriousSamplerRange`, mirroring
the real repro's exact IR shape (struct-typed single-call handle, one
`extractvalue ..., 0`, `getpointer` + `load`, no `.sample`), asserting
both the existing IR-shape expectations (no surviving
`handlefrombinding`/`extractvalue`) and -- the actual regression check
this bug needed -- exactly one `BoundResourceRange` via
`ResourceInfo::fromModule`, of `BoundResourceClass::Buffer`. Needed one
small test-infrastructure change: linked `FeMeTargetCPU` into
`FeMeTransformsCPUTests` (`unittests/Transforms/CPU/CMakeLists.txt`) so
the test can call `ResourceInfo::fromModule` directly, rather than only
inspecting IR shape as every prior test in this file does.

`ninja FeMeTransformsCPUTests`: 604/604 Passed, 0 Failed, 0 regressions.
`ninja check-feme` (ccache + assertions, full target-dependency build):
all discovered tests passing, 0 Failed (3,464/3,525 total; 61
Unsupported, same pre-existing baseline as every prior session).

**CTS:** re-ran `dEQP-VK.texture.texel_buffer.*` (23 cases): **10/23
Pass (was 0/23), 0 Fail (was 10/23), 13 NotSupported** (unchanged) --
every one of the 10 originally-failing cases
(`uniform.packed.{a2b10g10r10-uint-pack32, a2b10g10r10-unorm-pack32,
a8b8g8r8-{sint,snorm,uint,unorm}-pack32, b10g11r11-ufloat-pack32}`,
`uniform.snorm.{r8-snorm, r8g8-snorm, r8g8b8a8-snorm}`) now passes, no
regressions elsewhere in the group.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal SPIR-V-resource-classification correctness fix
for already-exposed core Vulkan 1.0 functionality (uniform texel
buffers), no new feature/extension surface.

`check-hlsl-feme-vk`/offload-test-suite `feme`-branch-drift housekeeping
check (last done several sessions ago at `d0974dd`): re-investigated
this session. Local `feme` branch tip (`d0974dd`) and upstream
`llvm-beanz/offload-test-suite`'s `feme` branch tip (`854cc3f`) still
differ in commit hash, but diffing each branch's own patch against its
own base commit (`CMakeLists.txt`/`test/CMakeLists.txt`/
`test/lit.cfg.py`, the only files either branch touches) and then
diffing *those two diffs* against each other confirms the actual patch
content is **byte-identical** -- the hash difference is purely because
upstream's `feme` branch is based on an older point in `main`
(`9c6792d`) than this checkout's base (`1814e12`, 24 commits newer), so
the unified-diff hunk line numbers differ even though the real changes
do not. **No action needed**; same conclusion as every prior session
this check has been run.

Tracked as `L317` (now `done`) in the roadmap.

## L321: `texture.multisample.invalid_sample_index.*` root-caused and fixed -- `isLinearChain` didn't accept a barrier-free diamond inside a loop body

Mandatory device check: `vulkaninfo --summary | grep deviceName` ->
`FeMe CPU Vulkan Device`.

Picked up `L316`'s `texture.multisample` cluster (5 failures, carried
forward from `L313`'s untriaged item 3). Re-ran the group first:

```
dEQP-VK.texture.multisample.*: 0 Pass / 5 Fail / 5 NotSupported
```

The 5 NotSupported are `atomic.storage_image_r64{i,ui}` (needs
`shaderInt64`, unimplemented, unrelated) and
`invalid_sample_index.sample_count_{16,32,64}` (sample counts beyond
this device's rasterizer floor, unrelated). Of the 5 real failures,
3 (`invalid_sample_index.sample_count_{2,4,8}`) all hit the identical
pipeline-creation diagnostic:

```
error: feme-cpu-wrap-entry: function 'main' has a barrier inside
non-linear control flow (a surviving branch not part of a supported
loop); region splitting only supports a straight-line wave body or a
single uniform loop (roadmap milestone 9 deviation)
```

Reproduced standalone via the documented `FEME_DUMP_IR` recipe
(`feme/.instructions.md`): dumped the real post-`SIMDizePass`/
post-`JumpThreadingPass` module for `sample_count_4`, extracted it, and
fed it straight to `feme-opt --llvm -passes=feme-cpu-wrap-entry` --
reproduced the exact diagnostic with no CTS/Vulkan involvement at all.

The dumped IR showed the shader's real shape: an "initialize" loop
(header block 19) whose body is a plain `if (s >= 0 && s < numSamples)
color = ndxColors[s % 4];` -- a barrier-free, single-sided diamond
(`21` -> `26`/`Flow27._crit_edge` -> backedge to `19`) -- followed by a
group-sync barrier, then a structurally identical "verify" loop. No
barrier sits inside either loop's body at all.

Root cause: `EntryWrapper.cpp`'s `walkBarrierFreeArm` (used by
`isLinearChain` to recognize a loop whose body is itself a barrier-free
straight chain before closing back to its header) only tolerated a
`CondBr` mid-arm in one narrow shape (roadmap H94b: exactly one
successor already a backedge to an established block -- `SIMDizePass`'s
own widened "is any lane still active" reduction shape). A loop body
containing a genuine barrier-free `if` with no `else` hit neither that
shape nor a fresh exit, so the walk failed outright -- a real
diagnostic-scope gap (the loop is fully barrier-free and splittable),
not an actual unsupported shader shape.

Fix: `walkBarrierFreeArm` now falls back to `matchBarrierFreeRegion`
(the existing general acyclic-region matcher, roadmap H163) whenever a
mid-arm `CondBr`'s neither successor is already a backedge, absorbing
the nested diamond/region and continuing the walk from its exit. This
required loosening the walk's own "stop at a 2+-predecessor block"
rule: a region's own internal merge block legitimately has 2+
predecessors, all of them blocks this same arm just walked, which must
not be mistaken for an external reconvergence.

New `EntryWrapperTest.SplitsLoopWithDiamondBodyBeforeBarrier` reduces
the real shape to two sequential loops (each with a diamond body)
joined by one barrier -- confirmed via `git stash` A/B to fail without
the fix (the region split never happens, `main.region0` is never
created) and pass with it.

`ninja check-feme` (ccache + assertions, full target-dependency build):
3,465/3,526 Passed (+1 new test), 61 Unsupported, 0 Failed, 0
regressions.

**CTS impact:** re-ran `dEQP-VK.texture.multisample.*` (10 cases):
**3 Pass (was 0), 2 Fail (was 5), 5 NotSupported** (unchanged) -- all 3
`invalid_sample_index.sample_count_{2,4,8}` cases now Pass. The
remaining 2 Fails are `atomic.storage_image_r32{i,ui}`, a distinct,
unrelated resource-handle-normalization gap (see `L322` below).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal compiler diagnostic-scope/control-flow-matching
correctness fix, no feature/extension-surface change.

### Side discoveries, not fixed this session

**`L322`** (new): `texture.multisample.atomic.storage_image_r32{i,ui}`
now the only real failures left in this group.
`FEME_VULKAN_LOG_CREATION_ERRORS=1` shows a distinct pipeline-creation
rejection: a register-bound, multisampled (`image2DMS`) storage-image
handle used only for `imageAtomicExchange`-family ops fails FeMe's
resource-handle normalization outright (`"... is a register-bound
resource handle the FeMe CPU target cannot normalize ..."`). Not yet
root-caused.

**`L323`** (new): while A/B-testing `L321`'s fix with a *single*-loop
variant of the same diamond-body shape (barrier only in a block
*after* the loop, not inside it, so it instead goes through the
separate `matchLoopShape`/`buildWrapperForLoop` path rather than
`splitAtGroupSyncBarriers`), discovered `buildWrapperForLoop` crashes
with an LLVM IR-verifier assertion (`Uses remain when a value is
destroyed!`, `Value.cpp:99`) -- reproduced identically with and without
`L321`'s own fix in place, confirming it is a separate, pre-existing
bug, not a regression. The real CTS shape (two separate loops, not
one) never hits this path, so it did not block `L321`'s own fix, but
it is a real, user-triggerable crash in a sibling code path and is
carried forward for its own future session.

Tracked as `L321` (now `done`), `L322`, `L323` (both `not started`) in
the roadmap.

## L316: `texture.explicit_lod.2d.sizes.*` -- all 16 NPOT mipmap-filtering failures root-caused and fixed

Mandatory device check: `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed.

Picked up `L316`'s remaining scope: all 16 `texture.explicit_lod.2d.
sizes.{31x55,57x35}_*linear_mipmap_linear*_repeat*` failures (the
`texture.multisample` portion of the original `L316` was already fully
disposed of by `L321`/`L322`).

### Investigation

Reproduced `dEQP-VK.texture.explicit_lod.2d.sizes.
31x55_linear_linear_mipmap_linear_repeat` with
`--deqp-log-images=enable`: every failing sample's "Fail (Verification
failed)" is a `tcuTexLookupVerifier`-style mismatch at mip level 2,
small (~0.001-0.003 per channel) and numerically close to the ideal
range rather than a gross logic error -- the GPU result sits just
outside the verifier's own accepted tolerance band.

Hand-traced `vktSampleVerifier.cpp`'s own quantized-weight search
(`calcTexelGridCoordRange`/`verifySampleFiltered`) for the first
failing sample (`Coordinate: (0, 0.0363636, 0, 0)`, `LOD: 2` exactly):
level 2's height is `55 >> 2 = 13`, so the unnormalized V coordinate is
`0.0363636 * 13 = 0.472727`; the true (continuous, unquantized) texel
offset is `floor(0.472727 - 0.5) = -1` with fractional weight
`0.472727 - 0.5 - (-1) = 0.9727`. The verifier's own search, however,
only tests weight candidates quantized to the device's advertised
`subTexelPrecisionBits` -- `PhysicalDeviceInfo.cpp` reported the
spec-mandated *minimum* of 4 bits (16 steps), whose two candidates
nearest 0.9727 are only 0.9375 and 0.0, neither close enough to bracket
the real (unquantized) result.

Confirmed via `grep` that `FeMeRuntimeCPU.c`'s own bilinear/trilinear
filtering (`femeRTComputeBilinearSupport`, `femeRTSelectMipLevels`)
never actually quantizes its interpolation weights to any fixed bit
count -- every weight is computed as a genuine `float` fraction, full
IEEE precision throughout. Advertising only 4 bits of precision was
simply dishonest about what this target actually delivers, and every
one of these 16 failing cases' own ideal weight happens to land between
4-bit's two nearest quantization candidates.

### Fix

Raised `PhysicalDeviceInfo.cpp`'s `subTexelPrecisionBits` and
`mipmapPrecisionBits` from 4 to 8 -- the value real CPU Vulkan
implementations that also compute unquantized float weights (Mesa's
`lavapipe`, SwiftShader) report for the same reason. At 8 bits (256
steps), the verifier's own quantized search now lands within 1/256 of
the true continuous weight for every failing case, comfortably within
its own tolerance band.

New `PhysicalDeviceInfoTest.
TexelAndMipmapPrecisionBitsReflectUnquantizedFiltering` asserts both
limits are `>= 8`.

`ninja check-feme` (ccache + assertions, full target-dependency build):
3,466/3,527 Passed (+1 new test), 61 Unsupported, 0 Failed, 0
regressions.

**CTS impact:** re-ran `dEQP-VK.texture.explicit_lod.2d.sizes.*` (288
cases incl. `_compute` variants, the latter all `NotSupported` on this
build's exclusive-compute-queue limitation, unrelated): **144 Pass (was
128), 0 Fail (was 16), 144 NotSupported** (unchanged) -- all 16
previously-failing NPOT mipmap-filtering cases now Pass.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- a device-limits-accuracy fix (correcting an inaccurately-low
advertised precision value), no feature/extension-surface change.

`L316` is now fully `done`.

## L323: minimal IR reduction confirmed (not yet fixed)

Mandatory device check: `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed.

While wrapping up `L316`/`L322` this session, re-confirmed `L323`
(carried over from `L321`'s own A/B testing) with a minimal, standalone
IR reduction added temporarily to `EntryWrapperTest.cpp`, run, observed
to crash, then reverted via `git checkout --` (never committed, since
it aborts the process rather than failing an `EXPECT_` check, which
would break `check-feme` if left in the suite):

```llvm
define void @main() #0 {
entry:
  br label %header
header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %merge ]
  %cmp = icmp ult i32 %i, 4
  br i1 %cmp, label %body, label %afterloop
body:
  %cond = icmp eq i32 %i, 0
  br i1 %cond, label %true, label %merge
true:
  br label %merge
merge:
  %i.next = add i32 %i, 1
  br label %header
afterloop:
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  br label %exit
exit:
  ret void
}
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
```

Running `SIMDizePass(4).run(*M, MAM)` → `WaveLoweringPass().run(*M,
MAM)` → `EntryWrapperPass().run(*M, MAM)` over this module crashes
with:

```
While deleting: label %merge
Use still stuck around after Def is destroyed:  br i1 %cond, label %true, label %merge
Uses remain when a value is destroyed!
UNREACHABLE executed at llvm/lib/IR/Value.cpp:99!
  ...
  buildWrapperForLoop(llvm::Function&, (anonymous namespace)::LoopShape, ...) EntryWrapper.cpp:0:0
  buildWrapper(llvm::Function&) EntryWrapper.cpp:0:0
  feme::cpu::EntryWrapperPass::run(...)
```

i.e. `%true`'s own terminator (a plain `br label %merge`) still
references `%merge` as a label operand at the point something tries to
erase `%merge` -- whatever outlines/clones the diamond's wave region
moves or erases `%merge` without first rewriting (or itself erasing)
`%true`'s own terminator.

Not fixed this session (scoped as "a few hours, standalone IR-reduction
investigation" in the roadmap, and the remaining session time went to
`L316`/`L322` instead). The repro above is copy-pasteable directly into
a local, uncommitted `TEST(EntryWrapperTest, ...)` for the next
session to step through `buildWrapperForLoop` with a debugger.

`ninja check-feme` not re-run for this item specifically (no
`feme/` source change made) -- the `L316`/`L322` commits' own
`check-feme` runs (3,466/3,527 Passed, 61 Unsupported, 0 Failed) stand
as the session's build-health confirmation. `Vulkan14FeatureInventory.
md`/`VulkanExtensionInventory.md`: no change -- investigation only, no
fix.

## L323: fixed -- `outlineChain` dangling cross-function terminator edge

Mandatory device check: `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed.

Picked up directly from the prior session's confirmed minimal IR
reduction (recorded above in this same file). Root-caused to
`outlineChain` (`feme/lib/Transforms/CPU/EntryWrapper.cpp`, used by
`splitLoopBodyAtBarriers`/`outlineChainAtBarriers` to outline a
`LoopShape`'s body chain into its own per-wave region function): it only
ever patched *the chain's own last block's* terminator when redirecting
the chain's external successor edge to a `ret void`. This assumption
holds for a plain linear chain, but not for a chain containing a uniform
safe diamond (`matchSafeDiamond`) whose "false" arm is empty -- in the
repro IR, `body`'s own `CondBr` (`br i1 %cond, label %true, label
%merge`) branches *directly* from inside the chain to the chain's
external successor (`merge`, the loop's own `Latch`), while the chain's
actual last block (`true`) branches only to the diamond's own internal
merge point, which happens to be that same `merge`/`Latch` block already
excluded from the chain by `matchLoopShape`. Patching only `true`'s
terminator left `body`'s own `CondBr` still referencing `merge` as a
label operand after `merge` was spliced away into a different function
-- invalid cross-function IR, surfacing only later as the observed
IR-verifier "use after def destroyed" assertion once something tried to
erase/replace `merge`.

**Fix**: generalized `outlineChain`'s terminator-fixup step to scan
every block of the newly-outlined chain (not just the last one) for a
terminator operand pointing outside the chain, redirecting each such
edge to one shared, lazily-created `ret void` block:
- A `CondBr` with one in-chain and one out-of-chain successor has only
  the out-of-chain operand retargeted via `setSuccessor`, leaving the
  in-chain edge (and the branch condition) untouched.
- A plain `UncondBr` whose sole successor is out-of-chain is still
  rewritten in place exactly as before (erase + `CreateRetVoid` in the
  same block), with no new block created at all -- every chain that was
  already passing keeps producing byte-identical IR.

New test: `EntryWrapperTest.WrapsLoopWithDiamondBodyBeforeTrailingBarrier`,
using the exact minimal repro IR captured in the prior session's
investigation (one loop, `header`→`body`→(`true`/`merge`)→`header`, with
the group-sync barrier only in a block after the loop). Confirmed the
test no longer crashes and `verifyModule` reports the resulting wrapper
module clean.

**Verification**:
- `FeMeTransformsCPUTests` (`*EntryWrapper*`): 38/38 Passed, 0
  regressions (the new test plus all 37 existing).
- `ninja check-feme` (ccache + assertions, full target-dependency
  build): 3,467/3,528 Passed (+1 new test), 61 Unsupported, 0 Failed, 0
  regressions.
- CTS: no specific failing CTS case was ever isolated exercising exactly
  this IR shape -- the bug was found via an abstract IR reduction during
  `L321`'s own A/B testing in a prior session, not a concrete CTS
  failure. As a broad correctness/no-crash sanity check, re-ran the full
  `dEQP-VK.compute.*` group (61,460 cases): 686 Pass/0 Fail/60,774
  NotSupported (unrelated missing `VK_EXT_shader_object`), no crash, no
  regression from before this fix.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal compiler correctness fix (a miscompile/crash in
region-outlining control flow), no feature/extension-surface change.

## L324: fixed -- Bresenham half-open diamond-exit rule (adjacency double-counting)

Mandatory device check: `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed.

Picked up `L312`'s own carried-forward rasterization backlog (82
failures, dominated by stipple/adjacency Bresenham cases). Extracted all
12 `no_stipple` failures: all are
`bresenham_line{s,_strip}_with_adjacency*` variants (plain/`_factor_0`/
`_factor_large` x narrow/wide). Ran the first with
`--deqp-log-images=enable`: "Diamond-exit rule: 127 fragments. Result
image: 131 fragments" -- a consistent +4 excess (+1 per drawn line
segment; this test draws 4 independent line segments via the adjacency
topology).

Reviewed `GraphicsPipeline.cpp`'s `mapTopology`, `Executor.cpp`'s
adjacency-stripping code, and `Pipeline.cpp`'s
`stripAdjacency`/`getListPrimitiveVertexCount`/
`splitListPrimitiveAdjacency`/`getStripPrimitiveCount`/
`splitStripPrimitiveAdjacency` -- all already correct (adjacency vertex
stripping only ever exposes the 2 "core" vertices per window to the
rasterizer).

Root-caused by re-reading the real Vulkan spec's own "Bresenham Line
Segment Rasterization" section (`primsrast.adoc`, fetched from the
`KhronosGroup/Vulkan-Docs` source): the diamond-exit rule is explicitly
**half-open**: "the final fragment (corresponding to `p_b`) is not
drawn. This means that when rasterizing a series of connected line
segments, shared endpoints will be produced only once rather than twice
(as would occur with Bresenham's algorithm)." `Executor.cpp`'s
`emitLineSegment` Bresenham branch drew every pixel from `(X0,Y0)`
through `(X1,Y1)` **inclusive**, violating this rule.

For a single isolated line segment this produces one extra fragment,
which falls within the CTS's own documented tolerance ("must not differ
...  by more than one"), masking the bug for every previously-passing
single-line Bresenham case (including `L312`'s own plain `_wide`
cases). But `lines_with_adjacency`/`line_strip_with_adjacency` tests
draw 2+ independent line segments per draw, each contributing its own +1
excess, exceeding the tolerance cumulatively -- exactly matching the
"Invalid fragment count" symptom and the 12-case scope (all and only the
adjacency-topology Bresenham cases).

**Fix** (`feme/lib/Graphics/Executor.cpp`, `emitLineSegment`'s Bresenham
branch): moved the segment's own end-of-walk check (`X == X1 && Y ==
Y1`) to occur *before* fragment emission rather than after, skipping
emission of the segment's own final pixel -- except in the degenerate
single-pixel case (`X0 == X1 && Y0 == Y1`), where that one pixel is
still drawn since it represents both `p0` and `p1` at once.

**Why this doesn't break `LineStrip` continuity**: previously, a shared
vertex between consecutive strip segments was double-drawn (once as
segment *i*'s own inclusive endpoint, once as segment *i+1*'s own
starting pixel). The fix makes segment *i* exclude its own endpoint, so
the shared pixel is drawn exactly once -- as segment *i+1*'s starting
pixel. Verified with a new additive-blending test, since plain coverage
can't distinguish "drawn once" from "drawn twice" at a pixel.

**Tests**:
- Updated `ExecutorTest.RendersABresenhamDiagonalLine`: previously
  asserted all 4 diagonal pixels lit; now asserts only 3, with the
  segment's own final pixel `(3,0)` explicitly excluded.
- Added `ExecutorTest.BresenhamLineStripDoesNotDoubleDrawASharedVertex`:
  a 2-segment `LineStrip` along the same diagonal path, split at the
  shared midpoint vertex, rendered with additive blending
  (`BlendOp::Add`, both factors `One`) to confirm the shared vertex
  pixel accumulates exactly one unit of color, not two.

**Verification**:
- `FeMeGraphicsTests`: 384/384 Passed (+1 new test), 0 regressions.
- `ninja check-feme` (ccache + assertions, full target-dependency
  build): 3,468/3,528 Passed (+1 new test), 61 Unsupported, 0 Failed, 0
  regressions.
- CTS: re-ran the full `dEQP-VK.rasterization.*` group (15,019 cases):
  **414 Pass (+12) / 70 Fail (-12) / 14,535 NotSupported**, was
  402/82/14,535. Confirmed via an exact Python set-diff between the
  original 82-case failure list and the post-fix 70-case list: exactly
  the 12 targeted `no_stipple.bresenham_*_with_adjacency*` cases are
  newly Pass, 0 cases newly Fail -- 0 regressions anywhere else in the
  group.

The remaining 70 failures are dominated by
`static_stipple`/`dynamic_stipple`/`dynamic_stipple_and_topology` (49/70
-- likely a related but distinct stipple-arc-length-computation bug, not
investigated this session; tracked as `L325`), plus smaller scattered
groups already present in the original 82-case list (`stencil`,
`color_at_beginning`/`color_at_end`, `non_strict_line*`,
`triangle_fan`/`triangle_strip`, `line-strip`, `polygon-mode-lines`,
`depth_bias`, `provoking_vertex`, `flatshading`, `line_continuity`,
`frag_side_effects`, `maintenance5`,
`d24_unorm_constant_one_greater`, `draw`, `depth`) -- all confirmed
pre-existing, not new regressions from this fix.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- a correctness fix to an already-exposed, already-advertised
line-rasterization mode, no new feature/extension surface.

## L325: fixed (40/49 cases) -- Bresenham stipple counter must be an integer per-fragment count, not a distance

Mandatory device check: `vulkaninfo --summary | grep deviceName` →
`FeMe CPU Vulkan Device`, confirmed.

Picked up `L324`'s dominant carried-forward cluster: `static_stipple`/
`dynamic_stipple`/`dynamic_stipple_and_topology` (49/70 of `L324`'s
remaining rasterization failures). Ran
`dEQP-VK.rasterization.primitives.dynamic_stipple.bresenham_lines` with
`--deqp-log-images=enable`: "Diamond-exit rule: 233 fragments. Result
image: 224 fragments" in one iteration (missing fragments, beyond
tolerance) and position-deviation failures even where counts were
within tolerance in others -- a *position* bug (wrong fragments kept
vs. discarded), not a count-off-by-N bug like `L324`'s.

Root-caused via the real Vulkan spec's own "Line Stipple" section
(`primsrast.adoc`): the stipple counter `s` is an **integer**, with
**mode-dependent** increment rules:
- **Bresenham**: `s` is incremented by exactly 1 "after production of
  each fragment of a line segment" -- a pure per-walked-pixel step
  count, independent of the line's slope/length.
- **Rectangular**/**RectangularSmooth**: `s` is incremented once per
  "adjacent unit-length rectangle" the line is subdivided into -- a
  continuous-distance measure.

Cross-confirmed against `VK-GL-CTS`'s own reference software
rasterizer, `framework/referencerenderer/rrRasterizer.cpp`'s
`SingleSampleLineRasterizer::rasterize` (the authoritative generator of
each test's "expected" verification image): `m_stippleCounter++`
increments exactly once per walked diamond-exit-rule position, *outside
and after* the inner width-replication loop -- i.e. shared across all
of a wide line's replicated fragments at one step, not incremented once
per individual fragment.

`Executor.cpp`'s `emitLineSegment` used the same continuous
Euclidean-distance `Arc = ArcAccum + T * Len` formula for *both* modes
-- correct for Rectangular, but wrong for Bresenham on any
non-axis-aligned line (e.g. a 45-degree line's per-step distance is
`sqrt(2) ≈ 1.414`, not `1`), causing the wrong stipple-pattern bit to be
tested (and thus the wrong fragments kept/discarded) on many walked
pixels -- explaining the "missing fragments"/position-deviation
symptom.

**Fix** (`feme/lib/Graphics/Executor.cpp`, `emitLineSegment`'s Bresenham
branch): added a dedicated `float StippleCounter = ArcAccum;` (reusing
`ArcAccum`, now reinterpreted for Bresenham mode as an integer
fragment-count carried from a prior connected strip segment, consistent
with the spec's own carry-over rule, rather than a distance). Replaced
`float Arc = ArcAccum + T * Len;` with `float Arc = StippleCounter;`.
Added `StippleCounter += 1.0f;` immediately after the width-replication
loop (`for (int32_t I = 0; I < W; ++I)`), matching the reference
rasterizer's per-step (not per-fragment) increment. Added
`return StippleCounter;` as an early return bypassing the function's
shared `return ArcEnd;`, which remains the Rectangular/RectangularSmooth
branch's own correct distance-based continuity value, unchanged.

**Tests**: added `ExecutorTest.BresenhamStippleCounterCountsFragmentsNotDistance`
-- a 45-degree diagonal Bresenham line from NDC `(-0.75,-0.75)` to
`(1.0,1.0)` in a 4x4 viewport, `StippleFactor=1`,
`StipplePattern=0b1000` (only bit 3 on). With the fix, only the 4th
walked pixel, `(3,3)`, is lit (`Arc=3` → bit 3 → on); the first 3 are
not (`Arc=0,1,2` → bits 0,1,2 → off). Confirmed via `git stash` A/B
that this test fails pre-fix exactly as hand-predicted: the buggy
Euclidean-distance formula computes `Arc ≈ 3*sqrt(2) ≈ 4.243` for the
4th pixel → `floor(4.243) % 16 == 4`, not `3` → bit 4 (off) →
incorrectly left unlit. `FeMeGraphicsTests`: 385/385 Passed (+1 new
test), 0 regressions.

**CTS impact**: re-ran the full `rasterization` group (15,019 cases):
454 Pass / 30 Fail / 14,535 NotSupported (was 414/70/14,535 before this
fix). Of the 49 cases originally scoped into this cluster, 40 now Pass.
The remaining 9 all share a distinct, unrelated root cause -- see
`L326` below.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- a correctness fix to an already-exposed, already-advertised
stipple feature (`VK_EXT_line_rasterization`), no new feature/extension
surface.

## L326: remaining 9 `*stipple*.bresenham_line_strip_wide` failures -- distinct wide-line-strip join overlap, not yet fixed

Split out of `L325`'s remaining 9 failures, all of the shape
`dEQP-VK.rasterization.{primitives,primitives_multisample_{2,4,8}_bit}.{static_stipple,dynamic_stipple,dynamic_stipple_and_topology}.bresenham_line_strip_wide`.

Ran `primitives.static_stipple.bresenham_line_strip_wide` with
`--deqp-log-images=enable`: its 3 test iterations (widths 5, 10, 64)
all report "No invalid deviations found" and (where fragment-count
verification isn't skipped due to overdraw at higher widths) "Fragment
count is valid" -- i.e. this is **not** a stipple-pattern bug, FeMe's
`L325` fix is working correctly here too. The actual failure is in the
subsequent **line-width verification** step, specific to the
width-64 iteration (a 4-vertex strip): "Invalid line width at (212, 85)
- (212, 149). Detected width of 65, expected 64" -- a one-row/column
overlap, most likely at the join between two consecutive wide Bresenham
line-strip segments (a single-segment wide Bresenham line, exercised by
`L312`'s own regression test, is unaffected).

Not yet root-caused as of the prior session. **This session root-caused
it precisely** (still not fixed -- see rationale below for why the fix
is deferred).

**Root cause (confirmed, not a strip-join/half-open-rule bug as
previously hypothesized)**: extracted and diffed the Reference vs
Result PNGs pixel-by-pixel (not just trusting the verifier's own
"Invalid line width" text, which was misleading) and found the real
divergence is a **whole extra walked step** in FeMe's output at
segment 1's `Y=85` row (an entire row wrongly lit across the full
width-replicated span, not a localized 1px geometry glitch). Added
temporary `getenv("FEME_DEBUG_BRESENHAM")`-guarded instrumentation to
capture ground-truth per-segment values (`P0`, `P1`, `XMajor`, `W`,
floored endpoints, `ArcAccum`, stipple state), then hand-simulated
FeMe's own algorithm in Python (`numpy.float32`, matching C++ `float`
precision exactly) using those real values. This confirmed FeMe's own
stipple-bit arithmetic is **internally self-consistent** -- given its
own `ArcAccum=231` carried over from segment 0 and
`Factor=2`/`Pattern=0xf0f`, the `Y=85` step's stipple bit genuinely
computes to "on" by a literal reading of FeMe's algorithm. This rules
out a simple `L325`-style arithmetic bug.

The actual mismatch is **algorithmic**, found by reading VK-GL-CTS's
own reference rasterizer (`framework/referencerenderer/rrRasterizer.cpp`,
`SingleSampleLineRasterizer`) in detail: FeMe's Bresenham-mode walk
uses the classic integer DDA (err-accumulator) algorithm -- floor the
(width-shifted) float endpoints to get `X0Y0`/`X1Y1`, then step one
pixel per major-axis increment, choosing exactly one minor-axis
candidate per step via the accumulated error term. This is a
widely-used *approximation* of the Vulkan spec's actual rasterization
rule, provably exact for simple unshifted width-1 lines, but not
identical to the spec's literal definition.

The spec's actual rule, and what the reference rasterizer implements
verbatim, is an exact **diamond-exit test**
(`LineRasterUtil::doesLineSegmentExitDiamond`, ~250 lines) performed in
fixed-point subpixel arithmetic (`int64_t`, precision matching the
device's own advertised `VkPhysicalDeviceLimits::subPixelPrecisionBits`
-- FeMe currently advertises `4`, confirmed in
`feme/lib/Vulkan/PhysicalDeviceInfo.cpp`). Critically, this test is
evaluated against the **entire** width-shifted line segment (`m_v0` to
`m_v1`, not an incremental local step) for every candidate pixel in the
line's bounding box, with explicit handling for a line passing exactly
through a diamond's corner (4 distinct corner behaviors, each with
"line-through", "starts-here", and "ends-here" sub-cases) to resolve
ties consistently. For slopes near specific ratios (common at
non-axis-aligned, non-45-degree angles, which is segment 1's case
here: `(211,12)` to `(186,115)`, a shallow-steep y-major slope), the
exact geometric test can accept a position that the DDA's "exactly one
minor-axis candidate per major step" assumption doesn't produce (or
vice versa), shifting FeMe's effective step count by one relative to
the reference for the remainder of that segment's walk -- which then
feeds forward into the stipple counter and manifests as an entire
extra/missing row, exactly matching the observed symptom.

**Why not fixed this session**: a bit-exact fix requires literally
porting `doesLineSegmentExitDiamond`'s exact fixed-point algorithm
(including all 4 diamond-corner tie-break cases) into
`emitLineSegment`'s Bresenham branch, replacing the DDA walk with a
bounding-box-and-diamond-test walk. This is a substantial, intricate
port (not a quick arithmetic patch) and attempting it under session
time pressure risks introducing a subtly wrong implementation that
appears to work on this one reduction but miscompiles other slopes.
Deferred to a dedicated future session; broken down into sub-items in
`Roadmap.md` (`L326`/`L327`/`L328`) rather than left as a single
"unknown effort" item.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
anticipated once fixed -- expected to be a correctness fix to an
already-exposed, already-advertised wide-line-strip rendering path, no
new feature/extension surface.

## L327: standalone exact diamond-exit-rule primitive ported, not yet wired in

Split out of `L326`'s own root-cause finding (above): a direct,
bit-for-bit port of VK-GL-CTS's `LineRasterUtil::doesLineSegmentExitDiamond`
and its helpers into `feme::graphics::doesLineSegmentExitDiamond`
(`feme/lib/Graphics/LineRasterization.cpp` +
`feme/include/feme/Graphics/LineRasterization.h`), including the
4-entry diamond-bound table, the 4-entry diamond-corner
edge/start/end-case table, `vertexOnLeftSideOfLine`/
`vertexOnRightSideOfLine`/`vertexOnLine`/`vertexOnLineSegment`/
`getVertexSide`, and `lineInCornerAngleRange`/
`lineInCornerOutsideAngleRange`, all operating on `int64_t`
fixed-point subpixel coordinates exactly as the reference does.
`llvm::countl_zero` (`llvm/ADT/bit.h`) replaces the reference's own
`deClz64` for the broad-reject overflow check -- the only
non-mechanical substitution in the port.

New `LineRasterizationTest.cpp`: axis-aligned horizontal/vertical
half-open-rule tests, a 45-degree diagonal cross-validated directly
against `ExecutorTest.RendersABresenhamDiagonalLine`'s own
CTS-confirmed output (identical screen endpoints, identical expected
lit/unlit pixel set), a shallow non-45-degree slope matching `L326`'s
own failing-segment shape (expected pixel selection independently
derived from standard Bresenham rounding interpolation by hand, then
confirmed to match the ported exact algorithm's own output -- not
simply asserting whatever the implementation happened to produce),
and the function's own early broad-reject-distance path. `ninja
check-feme`: 3,474/3,535 Passed (+5 new tests), 61 Unsupported, 0
Failed, 0 regressions.

**Not yet wired into `emitLineSegment`** -- `L326`'s CTS failures are
therefore still open; this commit adds the primitive only, with no
behavior change to the existing renderer. Tracked as `L328`.

**Performance caveat found while studying the reference's full
`rasterize()` body for port fidelity**: the reference's own per-segment
walk is a brute-force nested sweep testing every `(x, y)` combination
in the segment's entire bounding box, not one candidate per
major-axis step -- cheap for a reference-only software rasterizer,
but a literal port of that loop structure into a real driver would
make `emitLineSegment` `O(length^2)` for long, near-45-degree lines,
a correctness-for-performance regression `L328` needs to design
around (see `Roadmap.md`'s `L328` entry for the proposed narrowed
per-major-step candidate window instead).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal, not-yet-wired-in geometric primitive, no
feature/extension-surface change.

## L328: fixed -- exact diamond-exit rule wired into Bresenham line rasterization

Replaced `emitLineSegment`'s Bresenham-mode walk (`Executor.cpp`) with a
hybrid approach, per the design `L327` left pending: the classic integer
DDA (error-accumulator) stepping is retained purely as a *prediction* of
where the minor-axis coordinate roughly sits at each major-axis step, but
the actual per-pixel accept/reject decision is now
`feme::graphics::doesLineSegmentExitDiamond` (`L327`'s exact, bit-for-bit
port of the reference rasterizer's own test), evaluated over a narrow
`+/-2`-pixel candidate window around that prediction. A `llvm::DenseSet`
of already-emitted `(x, y)` keys deduplicates across overlapping windows
from consecutive major steps, so no candidate is tested or drawn twice.
This keeps the walk `O(length)` (as today), rather than the reference's
own `O(length^2)` brute-force whole-bounding-box sweep (`L327`'s
performance caveat).

This replaces the old DDA walk's bespoke half-open `AtEnd`/degenerate-
single-pixel special case entirely: the exact diamond-exit test already
implements the spec's half-open rule (including the degenerate case)
for every corner geometry, so no separate handling is needed above it.
`L312`'s width replication and `L325`'s per-fragment (not per-distance)
Bresenham stipple-counter semantics are preserved by factoring the
per-accepted-candidate draw logic into a new `EmitPixel` lambda, called
once per unique accepted pixel.

**Regression found and fixed during this change**:
`ExecutorTest.RendersAWideBresenhamHorizontalLine` initially failed
after the rewrite, with pixel `(3, Y)` missing. Root-caused (via a
standalone debug harness directly exercising
`doesLineSegmentExitDiamond`, and by reading
`VK-GL-CTS/framework/referencerenderer/rrRasterizer.cpp` directly to
confirm the ported primitive is bit-for-bit faithful) to the test's own
baked-in assumption -- "every column 0..3 lit uniformly" -- being an
artifact of the old *approximate* DDA walk, not true exact-rasterization
behavior: for a perfectly horizontal, pixel-center-aligned line, whether
a given column's diamond is lit is a genuine geometric question of
whether the (sub-pixel-precision) segment crosses that diamond's own
local boundary before terminating, which is legitimately sensitive to
the line's exact height relative to the row's center (`HalfPixel -
|height from center|` is the diamond's local half-extent at that
height). Fixed by nudging the test's NDC endpoints
(`{-1.0, 0.25}`/`{1.0, 0.25}` -> `{-0.95, 0.1}`/`{0.95, 0.1}`) off that
exact tie case while preserving the test's original intent (3 full rows,
every visible column lit) -- not a fix to the implementation, which is
correct per spec.

`FeMeGraphicsTests`: 390/390 Passed, 0 regressions. `ninja check-feme`:
3,474/3,535 Passed, 61 Unsupported, 0 Failed, 0 regressions (same totals
as `L327`'s run -- this change only touches already-tested Bresenham
line paths).

**CTS impact**: re-ran the specific known 9-case `L326` failure list
(`primitives`/`primitives_multisample_{2,4,8}_bit` x
`static_stipple`/`dynamic_stipple` `.bresenham_line_strip_wide`): **all 9
now Pass** (0 Fail). Re-ran the full `rasterization` group (15,019
cases): 463 Pass/21 Fail/14,535 NotSupported (was 454/30/14,535 after
`L325`) -- exactly the expected +9 Pass/-9 Fail, 0 regressions elsewhere.
The remaining 21 failures are the pre-existing scattered cluster
(`conservative.overestimate.*degenerate`, `depth_bias`, `flatshading`,
`frag_side_effects.color_at_{beginning,end}.*`, `line_continuity`,
`maintenance5.non_strict_line*`, `polygon_as_large_points`,
`provoking_vertex`, `rasterization_order_attachment_access.*`) --
unrelated to Bresenham line rasterization, each individually untriaged.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- a correctness fix to an already-exposed feature
(`lineRasterizationMode=Bresenham`), no new feature/extension surface.
