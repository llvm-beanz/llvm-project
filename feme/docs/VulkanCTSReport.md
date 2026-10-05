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

1. **Linearizer dominance regression — at least 31,024 failures.**
   `pipeline.monolithic.image` contributes 23,056 failures and
   `pipeline.monolithic.image_view` contributes 7,968. Representative cases
   from both families fail pipeline creation because `feme-cpu-linearize`
   emits IR in which loop activity and exit-mask values do not dominate their
   uses. This is one shared compiler-root-cause family and the highest-value
   next fix.
2. **Sampler coverage — 7,454 failures.** These span every image view type and
   include address modes, filtering, border colors, reduction behavior, and
   one `maxSamplerLodBias` case. At least some execute and produce incorrect
   colors rather than failing compilation, so this remains a family to split
   by diagnostic after the linearizer regression is removed.
3. **Render-pass correctness and device loss.** `renderpasses` has 4,845
   ordinary failures and 328 `DeviceLost` results. The largest ordinary
   families are dynamic rendering (2,990) and suballocation tests (1,842).
4. **Transform feedback and SPIR-V long tails.** `transform_feedback` has
   1,918 failures, led by primitives-generated query get/copy cases (1,308).
   `spirv_assembly` has 458 failures and two isolated `SIGABRT` crashes.
5. **Process stability.** The final isolated classification records 50
   crashes: 46 `pipeline` `SIGSEGV`s, one `api` `SIGSEGV`, one
   `graphicsfuzz` `SIGABRT`, and two `spirv_assembly` `SIGABRT`s. There are
   no remaining timeouts. Separately, completed QPA records contain 348
   `DeviceLost` results: 328 `renderpasses`, 12 `pipeline`, and 8
   `robustness`.

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
