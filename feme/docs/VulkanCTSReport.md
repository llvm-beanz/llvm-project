# Vulkan CTS status

This report records the complete Vulkan CTS re-triage performed on
2026-10-12. It supersedes the 2026-10-05 full-run baseline and all partial
counts previously recorded in this file.

## Tested configuration

- FeMe revision: `d3577f8378eb`
- VK-GL-CTS upstream revision: `c28b36293093`
  (`vulkan-cts-1.4.7.0-40-gc28b3629`)
- VK-GL-CTS tested revision: `6b0464e83`, the upstream revision plus an
  isolated host-mirror fix for std430 `vec3` arrays in
  `vktMeshShaderInOutTestsEXT.cpp`
- Vulkan registry: `VK_HEADER_VERSION` 365
- Device: `FeMe CPU Vulkan Device`
- Build: Release, LLVM assertions enabled, precompiled headers disabled,
  and C/C++ compilation through `ccache`
- Full-run artifacts:
  `/home/dev/dev/VK-GL-CTS/run/feme-20261012-full-d3577f-c28b362/`

The updated CTS contains 3,253,584 cases, 9,215 more than the preceding
3,244,369-case baseline.

## Method

The full list was run through `feme/utils/run_vulkan_cts.py` in 1,800-case
processes with six workers. Recovery used 200-, 20-, and one-case processes
with a 60-second isolated limit. Shader caching and terminate-on-failure were
disabled.

The harness re-ran every ordinary `Fail` in isolation. The initial parallel
run also produced 466 `DeviceLost` outcomes; because the harness does not
isolate that status automatically, all 466 were rerun as one-case processes
with one worker. Of those, 442 passed and 24 reproduced `DeviceLost`. The 20
unfinished large copy/blit cases were rerun individually with a 600-second
limit: four passed and 16 again exceeded the limit.

The full run exposed a previously diagnosed VK-GL-CTS host-layout defect in
58 `mesh_shader.ext.in_out.with_f16` cases: C++ arrays of `tcu::Vec3` used a
12-byte stride while the matching std430 shader arrays used 16 bytes. The
isolated CTS commit `6b0464e83` restores 16-byte host elements. All 58 affected
cases pass after rebuilding CTS, and the final accounting below incorporates
those outcomes.

## Complete result

| Outcome | Cases | Percent |
|---|---:|---:|
| Pass | 704,296 | 21.647% |
| Fail | 1,549 | 0.048% |
| NotSupported | 2,547,677 | 78.304% |
| QualityWarning | 46 | 0.001% |
| DeviceLost | 0 | 0% |
| TimedOut | 16 | <0.001% |
| InternalError | 0 | 0% |
| Crashed | 0 | 0% |
| Unrun | 0 | 0% |
| **Total** | **3,253,584** | **100%** |

Every generated case has a classified outcome. The 16 `TimedOut` cases are
independently reproducible one case at a time; none are parallel-run
artifacts or missing measurements. The 24 `DeviceLost` cases this run
originally recorded are fixed as of L417 (see "Persistent abnormal
outcomes" below) and now reclassify as Pass.

Compared with the 2026-10-05 baseline, Pass increases by 54,245, ordinary
Fail decreases by 45,625, persistent DeviceLost decreases by 324, and
process crashes decrease from 50 to zero. This comparison is directional,
not a strict same-list delta, because the updated CTS adds 9,215 cases and
the old run did not have a separate timeout classification.

## Per-group accounting

The following table includes every group with a non-Pass/NotSupported
outcome. All omitted groups have only Pass and NotSupported results.

| Group | Total | Pass | Fail | NotSupported | QualityWarning | DeviceLost | TimedOut |
|---|---:|---:|---:|---:|---:|---:|---:|
| api | 267,512 | 116,698 | 21 | 150,781 | 0 | 0 | 16 |
| draw | 29,475 | 3,264 | 2 | 26,209 | 0 | 0 | 0 |
| fragment_operations | 151 | 118 | 0 | 30 | 3 | 0 | 0 |
| geometry | 200 | 190 | 1 | 9 | 0 | 0 | 0 |
| glsl | 28,421 | 19,440 | 18 | 8,963 | 0 | 0 | 0 |
| graphicsfuzz | 757 | 674 | 83 | 0 | 0 | 0 | 0 |
| image | 143,086 | 29,903 | 14 | 113,169 | 0 | 0 | 0 |
| info | 23 | 19 | 1 | 3 | 0 | 0 | 0 |
| memory_model | 18,530 | 169 | 52 | 18,309 | 0 | 0 | 0 |
| mesh_shader | 28,061 | 530 | 11 | 27,520 | 0 | 0 | 0 |
| pipeline | 1,172,932 | 303,820 | 203 | 868,866 | 43 | 0 | 0 |
| rasterization | 15,019 | 484 | 2 | 14,533 | 0 | 0 | 0 |
| renderpasses | 81,687 | 36,136 | 13 | 45,538 | 0 | 0 | 0 |
| spirv_assembly | 70,337 | 10,988 | 528 | 58,821 | 0 | 0 | 0 |
| tessellation | 1,114 | 646 | 30 | 438 | 0 | 0 | 0 |
| transform_feedback | 133,719 | 6,162 | 570 | 126,987 | 0 | 0 | 0 |

Notable clean groups include `binding_model` (87,703 Pass), `compute` (701),
`subgroups` (1,223), `synchronization` (18,420), `synchronization2` (24,631),
`texture` (9,810), `ubo` (5,687), and `query_pool` (16,587), each with zero
Fail, DeviceLost, TimedOut, or process-abnormal outcomes.

The conformance-target groups that remain honestly unsupported also have a
complete baseline:

| Group | Total | Pass | NotSupported |
|---|---:|---:|---:|
| ray_query | 49,359 | 0 | 49,359 |
| ray_tracing_pipeline | 22,707 | 0 | 22,707 |

These measurements satisfy roadmap J9's baseline requirement without
changing the open J1-J8 implementation prerequisites.

## Current failure ownership

The 1,549 isolated ordinary failures are completely partitioned below.
Roadmap IDs point to the bounded follow-up work.

| Cluster | Cases | Roadmap ownership |
|---|---:|---|
| Transform feedback: `simple*` (188 each for monolithic, fast GPL, and optimized GPL), fuzz, and primitive restart | 570 | H34, H36, H37, H173(c), L419(b) |
| SPIR-V assembly instructions, led by `opquantize` (160, now 13 after this session's upstream `OpQuantizeToF16` fix), compute `opinboundsaccesschain` (72, now 6 after this session's `BlockAccessChainPattern` templating fix), `opmemoryaccess` (35), and `decoration_group` (30) | 315 | L419(a) |
| Pipeline multisample, extended dynamic state, graphics-library, and interpolation cases | 203 | L419(c) |
| GraphicsFuzz correctness cases; no crashes or timeouts remain | 83 | L116 |
| Coherent Vulkan memory-model `fence_fence` message-passing and write-after-read cases | 52 | K9, L419(d) |
| Tessellation invariance/common-edge cases | 30 | L419(g) |
| API all-format color blits (20) and conformance-version reporting (1) | 21 | L419(e), L419(j) |
| GLSL loops, demote, derivatives, and loop-in-switch cases | 18 | L419(h) |
| Storage images without a format using `A2B10G10R10_SINT_PACK32` | 14 | L419(i) |
| Render-pass depth/stencil write conditions (9) and resolve (4) | 13 | L419(k) |
| Mesh-shader properties and miscellaneous correctness after removing the 58 CTS-side failures | 11 | L419(f) |
| Draw output-location (2), line continuity (2), geometry primitive ID (1), and mandatory-feature reporting (1) | 6 | L419(j) |
| **Total** | **1,336** | |

## Persistent abnormal outcomes

### Dynamic-rendering device loss (fixed by L417)

The run above originally recorded 24 persistent `DeviceLost` cases, exactly:

- 12
  `renderpasses.dynamic_rendering.graphics_pipeline_library.suballocation.ignore_ds_aspect`
  cases;
- 12
  `renderpasses.dynamic_rendering.primary_cmd_buff.suballocation.ignore_ds_aspect`
  cases.

Each half contained six `D24_UNORM_S8_UINT` and six `D32_SFLOAT_S8_UINT`
stencil-attachment/depth-aspect combinations. Root-caused and **fixed**:
both `CommandBuffer.cpp`'s draw-time `resolveDrawAttachments` and
`Executor.cpp`'s `executeDraws` hard-errored when a pipeline had depth
test/write enabled but the draw's render target bound no depth
attachment, rather than treating it as a spec-legal no-op (the same bug
class L394 already fixed for the analogous stencil case). A reduced
primary/GPL pair
(`...ignore_ds_aspect.d24_unorm_s8_uint.stencil_attachment_depth_aspect.
clear.clear_draw`) confirmed the exact failure via
`FEME_VULKAN_LOG_CREATION_ERRORS=1`: "the bound pipeline tests/writes
depth but the render target has no depth attachment" at `vkQueueSubmit`.
All 144 `dEQP-VK.renderpasses.*.suballocation.ignore_ds_aspect.*` cases
(covering all four render-pass construction styles, not just the two that
reproduced `DeviceLost`) now Pass; a 39,686-case
`dEQP-VK.pipeline.*.depth.*` regression sweep shows zero new Fail or
DeviceLost. The other 442 initial `DeviceLost` outcomes from the original
full run had already passed in single-case, single-worker reruns and were
classified as Pass before this fix.

### Large copy/blit timeouts

The 16 persistent timeouts are
`api.copy_and_blit.core.use_after_copy` image-to-image MSAA cases using
1024x1024x2 resources. Each exceeded both the 60-second recovery limit and a
separate 600-second isolated limit. They are performance failures with a
bounded result, not unrun cases, and are tracked by L418.

### SPIR-V assembly `opquantize`/`opinboundsaccesschain` partial fix (L419(a))

`OpQuantizeToF16` (SPIR-V opcode 116) was never defined anywhere in upstream
MLIR's SPIR-V dialect -- not a FeMe gap. Fixed upstream (outside `feme/`,
isolated/self-tested commits): added `SPIRV_QuantizeToF16Op` plus its
missing `SPIRV_OC_OpQuantizeToF16` opcode enum case (a genuine clean gap
between `OpFConvert`(115) and `OpConvertPtrToU`(117)), and a lowering
pattern (fptrunc-to-f16-then-fpext-back) in `SPIRVToLLVM.cpp`. The full
`opquantize*`/`opspecconstantop_opquantize` family (compute and every
graphics stage, 175 cases total) went from 160 Fail to 13 Fail. The
residual 13 are `flush_to_zero` (2, needs `DenormFlushToZero` execution-mode
support for f16) and 11 graphics `too_small`/`spec_const_too_small_*`
cases (a distinct, unrelated validation-size check), neither pursued
further this session.

Separately, FeMe's own `BlockAccessChainPattern` (and the
`rewriteBlockAccess`/`remapNestedStructMemberIndices` helpers it shares
with `ArrayedBlockAccessChainPattern`) was templated only on
`spirv.AccessChain`, never `spirv.InBoundsAccessChain`, even though
upstream MLIR's own generic pattern handles both and glslang only ever
emits `OpInBoundsAccessChain` (never plain `OpAccessChain`) when indexing
into SSBO array elements. All 72
`dEQP-VK.spirv_assembly.instruction.compute.opinboundsaccesschain.*`
cases share the exact same single-member `BufferBlock`-wrapped-`rtarray`
shape this pattern handles. Fixed (FeMe-local) by templating
`BlockAccessChainPattern`/`rewriteBlockAccess`/
`remapNestedStructMemberIndices` over the SPIR-V op type and registering
both `AccessChainOp` and `InBoundsAccessChainOp` instantiations: 72 Fail
-> 6 Fail. The residual 6 (`vec_3` and `matrix_3x{2,3,4}` variants) fail
with an unrelated `'llvm.insertvalue' op position out of bounds: 2`, a
distinct vec3/matrix-3-row layout bug. The sibling
`indexing.input.struct.opinboundsaccesschain_*` cases (a different,
already-passing access shape) were re-verified unaffected.

Combined net reduction: 213 cases (1,549 -> 1,336 against the full-run
baseline above). See `Roadmap.md`'s `L419(a)` entry for the full
root-cause writeups.

## Validation outside CTS

- `check-feme`: 3,705 passed, 3 unsupported, 0 failed.
- `check-hlsl-feme-vk`: 489 passed, 27 expected failures, 273 unsupported,
  2 failed (known: `Mandelbrot.test` full-suite flake, `WaveActiveMax.test`
  H169 triaged-closed issue), 1 unexpected pass (`array_of_matrices.test`,
  known flaky, XFAIL retained).

The offload suite required two isolated external corrections: boolean
specialization constants now use the four-byte `VkBool32` representation,
and the ARM64 host-wave-size artifact plus the stale DXC-only XFAIL now have
driver-specific expectations.
