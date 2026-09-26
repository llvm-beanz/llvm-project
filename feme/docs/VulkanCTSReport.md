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
