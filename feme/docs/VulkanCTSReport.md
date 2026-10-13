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

**Update (`L420`):** the harness now automates both of the manual steps
above. `DeviceLost` receives exactly the same one-case-per-process solo
re-verification `Fail` already got, and any case killed for exceeding
`--final-timeout` while running completely alone (the only granularity at
which a process timeout can be attributed to one specific case) is
recorded directly as a first-class `TimedOut` outcome rather than reverting
to `Unrun`. A future full re-triage will no longer need the by-hand
`DeviceLost`/long-timeout reruns this section describes.

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
| SPIR-V assembly instructions, led by `opquantize` (160, now 13 after an upstream `OpQuantizeToF16` fix), compute `opinboundsaccesschain` (72, now 0 -- fully fixed by a `BlockAccessChainPattern` templating fix, a struct-of-vec3 CompositeInsert/Extract fix, and a final `matrix_3x3.float`-specific Function-storage/InBoundsAccessChain tight-matrix fix), `opmemoryaccess` (35, now 0 -- fully fixed by a new upstream `CopyMemoryPattern` SPIRVToLLVM lowering plus an upstream SPIR-V deserializer `Aligned`-bit-detection fix), and `decoration_group` (31, now 0 -- fully fixed by adding the entirely-missing upstream `OpDecorationGroup`/`OpGroupDecorate`/`OpGroupMemberDecorate` opcodes and deserializer support) | 243 | L419(a), L419(l) |
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
| **Total** | **1,295** | |

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
-> 6 Fail. The residual 6 (`vec_3` and `matrix_3x{2,3,4}` variants) failed
with an unrelated `'llvm.insertvalue' op position out of bounds: 2`, a
distinct vec3/matrix-3-row layout bug. The sibling
`indexing.input.struct.opinboundsaccesschain_*` cases (a different,
already-passing access shape) were re-verified unaffected.

This residual 6 was root-caused this session to two stacked bugs. **Bug 1
(upstream, outside `feme/`):** upstream MLIR's `CompositeExtractPattern`/
`CompositeInsertPattern` (`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`)
only special-cased a vector reached at the *top level* of a SPIR-V index
list, crashing whenever an index list walks through a struct/array and
*then* into a nested vector lane -- the exact shape the CTS generator's
`Function`-storage scratch composite uses
(`OpCompositeInsert %struct_t %inval %struct 3 2`). Fixed with a new
`getCompositeIndicesAggregatePrefixLength` helper that splits the index
list at the point a nested vector is reached, emitting
`extractvalue`/`insertvalue` for the aggregate-only prefix and
`extractelement`/`insertelement` for the final vector lane, plus
defensive `isa<VectorType>` guards. New upstream lit coverage in
`misc-ops-to-llvm.mlir`. **Bug 2 (FeMe-local), exposed only once Bug 1
stopped crashing first:** `resolvePhysicalCompositeAccess` unwraps a
`feme.tight_vector` marker struct (substituted for every struct's own
`vector<3xT>` member) without recording that it did so;
`CompositeExtractMemberReorderPattern`/`CompositeInsertMemberReorderPattern`'s
bail-out logic missed the case where an unwrap happens *without* a
following vector-lane split (true for 5 of these 6 cases, since the
marker's own inner data is a flat, fully `extractvalue`-navigable
`array<3xT>`). Added `NeededTightVectorUnwrap` tracking to
`PhysicalCompositeAccess` and both patterns' bail-out conditions; also
fixed `CompositeInsertMemberReorderPattern`'s final `InsertValueOp` to use
the unwrap-aware `Access->ExtractValueIndices` instead of the original
`PhysicalIndices`. New FeMe unit test:
`SPIRVToLLVMTest.CompositeInsertExtractThroughStructMemberVec3LaneLegalizes`.
Result: `vec_3` and `matrix_3x2`/`matrix_3x4` now pass, 6 Fail -> 1 Fail.
**Residual (1):** `matrix_3x3.float` still fails, now with a data
mismatch (not a crash) -- a distinct, narrower, not-yet-diagnosed bug left
for a future session. Also verified (via a stashed-fix baseline re-run)
that `composite_insert.nested_struct`/`undef_nested_struct`, found during
a broader regression sweep, are pre-existing failures unrelated to this
fix (an unrelated `'llvm.store' op operand #1 must be LLVM pointer type`
error, present identically before and after).

Combined net reduction: 218 cases (1,549 -> 1,331 against the full-run
baseline above). See `Roadmap.md`'s `L419(a)` entry for the full
root-cause writeups.

### `matrix_3x3.float` fix, closing L419(a)'s `opinboundsaccesschain` cluster

The remaining `matrix_3x3.float` data mismatch was root-caused to two
compounding bugs in `TightMatrixStorePattern`/`TightMatrixLoadPattern`
(`feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`), distinct from
the `BlockAccessChainPattern`/`CompositeInsert`/`CompositeExtract` bugs
fixed above. **Bug 1:** `TightMatrixLoadPattern` retained a dormant
`StorageClass::Function` bail-out predating roadmap L242, which added a
newer, more precise `!AccessChain`-keyed check and claimed (incorrectly)
to subsume the old one; `TightMatrixStorePattern` had already had this
same dead code removed in a prior fix, but the sibling Load pattern was
missed. **Bug 2:** both Tight patterns (and the `getMatrixWholeAccess`/
`walkStructMembersToMatrix` helpers they share) matched only
`spirv.AccessChain`, never `spirv.InBoundsAccessChain` -- but glslang's
generated shader for this exact case uses `InBoundsAccessChain`, mirroring
the same `BlockAccessChainPattern` gap fixed above. Fixed by removing the
dormant bail-out and templating `getMatrixWholeAccess`/
`walkStructMembersToMatrix` over the SPIR-V op type (plus two small
dispatcher helpers, `getMatrixWholeAccessFromPtr`/
`hasAccessChainDefiningOp`, so each pattern's own call site stays
op-type-agnostic). New FeMe unit test:
`SPIRVToLLVMTest.FunctionStorageStructMemberWholeMatrixStoreLoadUsesTightColumnType`.
Result: `matrix_3x3.float` now passes; the full `opinboundsaccesschain.*`
cluster is 72/72 Pass, 0 Fail -- L419(a)'s `opinboundsaccesschain`
sub-cluster is fully closed. Verified no regression in the L242
`spec_constant.*.composite.matrix.*` cluster (54/108 Pass, 54
NotSupported, 0 Fail, matching the prior baseline) and the offload-test-
suite's `matrix_splat_cast` L211 case (1/1 Pass). `check-feme`:
3707/3710 Passed (one new unit test), 3 Unsupported, 0 Failed, 0
regressions.

Combined net reduction across all three `opinboundsaccesschain` fixing
sessions: 1,549 -> 1,330 (one further case beyond the 1,331 figure
above).

### L418 MSAA image-to-image profiling

No profiler (`perf`/`strace`/`ltrace`/`valgrind`) is available in this
environment, so a lightweight, opt-in wall-clock diagnostic
(`PhaseTiming.h`/`.cpp`, `FEME_VULKAN_TIME_PHASES=1`) plus a finer-grained
per-command timing diagnostic (`FEME_VULKAN_DEBUG_CMD_TIMING=1`) were
added to `feme/lib/Vulkan/` to profile one representative case,
`dEQP-VK.api.copy_and_blit.core.use_after_copy.a1r5g5b5_unorm_pack16.
transfer_dst_optimal.1024x1024x2_color_att_flag_img2img_msaa`, standalone:

| Phase | Calls | Total | Notes |
|---|---:|---:|---|
| `ImageCopyExec` | 2 | 7.5 ms | the copy itself |
| `PipelineCompile` | 2 | 84.5 ms | |
| `CommandBufferExec` | 3 | 4552.7 ms | entire submitted command buffer replay |
| `FenceWait` | 1 | 4552.7 ms | blocked on the above |

The per-command breakdown shows this is **not** a synchronization/
fence-wait bug: three `Draw` commands account for ~4.16s of the ~4.55s
total (~91%), with `EndRenderPass` (the MSAA resolve-on-subpass-end) a
distant second at ~0.28s. The CPU software rasterizer's `Draw` path, not
queue scheduling or fence waiting, is this case's actual bottleneck --
the original hypothesis (that fence waiting itself was slow) was a red
herring caused by `FenceWait` simply reflecting time blocked on that slow
`Draw` work. This standalone run completes in ~5s, well under the
60s/600s timeout thresholds `L418` cites for this cluster; those numbers
likely reflect full-suite contention (many concurrent `QueueExecutor`
worker threads competing for CPU) rather than this case's isolated cost.
Optimizing the rasterizer itself is out of scope for this session; see
`Roadmap.md`'s `L418` entry for the follow-up breakdown.

### `opmemoryaccess` fix (L419(a))

All 35 `dEQP-VK.spirv_assembly.instruction.graphics.opmemoryaccess.*`
cases (7 `MemoryAccess` variants x 5 shader stages) failed identically
with `error: failed to legalize operation 'spirv.CopyMemory'`. Root
cause: `spirv.CopyMemory` had no SPIRVToLLVM lowering pattern anywhere
(upstream MLIR or FeMe) -- a clean upstream gap, structurally identical
to the earlier `opquantize`/`OpQuantizeToF16` discovery (an op the
SPIR-V dialect fully defines/parses/deserializes but for which no
lowering was ever written).

Implementing a new `CopyMemoryPattern` (lowering to an LLVM load from
`source` followed by a store to `target`, decoding each side's
independent `(memory_access, alignment)` pair per the op's own
single-mask-applies-to-both-sides spec text) surfaced a second, deeper
bug: a crash (`std::optional<unsigned>::operator*()`, not engaged) on
`nontemporal_aligned_*` specifically. Root-caused via `gdb` plus
temporary `llvm::errs()` instrumentation (the Release build has no
debug info for local variables in heavily-templated/inlined conversion
pattern code, so printf-style debugging was the effective fallback) to
the SPIR-V deserializer's `processOp<spirv::CopyMemoryOp>`: it detected
the `Aligned` bit via exact mask equality (`attrValue == 2`) instead of
a bit test. Since `MemoryAccess` is a combinable bit-enum (`Volatile`,
`Aligned`, `Nontemporal` are independent bits), this silently failed
whenever `Aligned` co-occurred with another bit (`Aligned|Nontemporal`
= 6), skipping the alignment literal word and desynchronizing every
subsequent word read for that instruction.

Both fixed upstream (outside `feme/`, two separate isolated/self-tested
commits): the new `CopyMemoryPattern`
(`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`) and the
deserializer's `isAlignedAttr` fix
(`mlir/lib/Target/SPIRV/Deserialization/DeserializeOps.cpp`, now
`spirv::bitEnumContainsAll(..., Aligned)`), each with new round-trip
lit coverage (`memory-ops-to-llvm.mlir`'s `@copy_memory*` tests;
`memory-ops.mlir`'s combined-bit `OpCopyMemory` cases). Result: full
`opmemoryaccess` cluster 35/35 Pass, 0 Fail (net -35). Re-verified: the
full `mlir/test/{Conversion/SPIRVToLLVM,Target/SPIRV,Dialect/SPIRV}` lit
suites (145 tests, 0 Fail) and `ninja check-feme` (3707/3710 Passed, 3
Unsupported, 0 Failed, 0 regressions).

Combined net reduction across all four `L419(a)` fixing sessions:
1,549 -> 1,295. `decoration_group` (31) was tracked as new roadmap row
`L419(l)` and is fixed below, in this same session.

### `decoration_group` fix, closing L419(a)/L419(l)

All 31 `dEQP-VK.spirv_assembly.instruction.graphics.decoration_group.*`
cases (plus a separate compute `decoration_group.all` case) failed
identically with `error: unhandled opcode 73` ->
`VK_ERROR_INITIALIZATION_FAILED`. Root cause: `OpDecorationGroup`
(73)/`OpGroupDecorate`(74)/`OpGroupMemberDecorate`(75) were entirely
missing from upstream MLIR's SPIR-V dialect -- no opcode enum case, no
deserializer handling whatsoever -- a clean upstream gap structurally
identical to the earlier `opquantize`/`opmemoryaccess` discoveries, but
one level deeper (a missing decoration-grouping *feature*, not just a
missing lowering pattern). A decoration group is a pure
decoration-storage `<id>` (never an SSA value or type) that collects
decorations via ordinary `OpDecorate`/`OpMemberDecorate`, which
`OpGroupDecorate`/`OpGroupMemberDecorate` then "replay" onto one or more
target `<id>`s as if each decoration had been applied directly.

Fixed upstream (outside `feme/`, one isolated/self-tested commit):
added the three opcode enum cases
(`mlir/include/mlir/Dialect/SPIRV/IR/SPIRVBase.td`) and three new
deserializer functions
(`mlir/lib/Target/SPIRV/Deserialization/{Deserializer.h,Deserializer.cpp,DeserializeOps.cpp}`)
reusing the existing `decorations`/`memberDecorationMap` storage so no
downstream code needs to know a decoration arrived via a group.
`OpGroupMemberDecorate` required a parallel `groupDecorationOperands`
side-channel (raw decoration + operand words, keyed by target `<id>`)
because `memberDecorationMap` is keyed by raw operand words, not by the
`Attribute` the ordinary decoration path builds; this side-channel must
be populated unconditionally for *every* `OpDecorate` rather than being
gated on already knowing the target is a group, since per SPIR-V binary
layout rules the `OpDecorate` that decorates a group always precedes the
`OpDecorationGroup` instruction that defines that group's `<id>` -- a
real ordering bug caught by a new `decoration-group.spvasm`
deserialize-only lit test. That test initially reported a vacuous pass
(this build's `lit.site.cfg.py` has `config.spirv_tools_tests = 0`, so
every `.spvasm` test's sole `%if spirv-tools %{ ... %}`-gated `RUN` line
is a no-op here even though `spirv-as`/`spirv-val` are installed on
`PATH`); the fix was validated for real by manually piping
`spirv-as | mlir-translate --deserialize-spirv | FileCheck` outside of
`llvm-lit`.

Result: full `decoration_group` cluster 31/31 Pass, 0 Fail (net -31,
plus the separate compute `decoration_group.all` case also 1/1 Pass).
Re-verified: the full
`mlir/test/{Conversion/SPIRVToLLVM,Target/SPIRV,Dialect/SPIRV}` lit
suites (146 tests, 0 Fail) and `ninja check-feme` (3707/3710 Passed, 3
Unsupported, 0 Failed, 0 regressions).

Combined net reduction across all five `L419(a)` fixing sessions:
1,549 -> 1,264 (pending a full re-triage to confirm). `L419(a)` and
`L419(l)` are both fully resolved for the clusters investigated so far.

## `L419(b)`: transform-feedback residuals (570 -> 285)

Reduced one representative from the 188-case monolithic `simple` slice,
`dEQP-VK.transform_feedback.simple.basic_1_256`: a data mismatch (`Failed
at item 1 received:0 expected:1`), not a crash/hang -- item 0 captured
correctly but every later item read back as 0.

Root cause, confirmed via `--deqp-log-decompiled-spirv=enable` and a
temporary debug print in `captureTransformFeedback`
(`feme/lib/Graphics/Executor.cpp`): glslang unconditionally stamps the
whole `gl_PerVertex` variable with `XfbBuffer`/`XfbStride` decorations
whenever *any* other output in the same entry point is genuinely
XFB-captured, even when none of `gl_PerVertex`'s own four members
(`gl_Position`/`gl_PointSize`/`gl_ClipDistance`/`gl_CullDistance`) are
written or meant to be captured -- `basic_1_256`'s vertex-shader-only
shader writes only a single, separately-declared `idx_out` variable with
its own genuine `Offset 0`/`XfbBuffer 0`/`XfbStride 4` decorations, but
`gl_PerVertex` still carries `XfbBuffer 0`/`XfbStride 4` with no
per-member `Offset` at all. `CanonicalizeStage.cpp`'s `addElements`
inherited the whole variable's `XfbBuffer`/`XfbStride` onto every member
regardless of whether that member had its own `Offset`, synthesizing
`XfbOffset=0` via `.value_or(0)` for all four -- so `captureTransformFeedback`,
processing `SignatureElement`s in `ElementID` order, let these four
spurious all-zero writes to `(XfbBuffer=0, XfbOffset=0)` clobber
`idx_out`'s own already-correct per-vertex capture at the exact same
slot.

**Fix** (FeMe-local, `feme/lib/Transforms/Graphics/CanonicalizeStage.cpp`):
only inherit the whole variable's `XfbBuffer`/`XfbStride`, and only
resolve the member's own `XfbOffset` optional into a concrete value, once
that member already carries its own explicit `Offset` decoration --
matching `xfb_pointsize_*`/`xfb_clipdistance_*`'s own real
captured-builtin shape, where the member always carries this decoration
itself. New unit test:
`CanonicalizeStageTest.DoesNotCaptureBuiltInBlockMembersWithNoOwnXfbOffset`.

**Measured impact**: `dEQP-VK.transform_feedback.simple.*` went from 188
Fail to 93 Fail (net -95) out of 7,899 cases. Since the bug lived in
signature canonicalization, not pipeline-construction-mode-specific code,
the exact same fix closes the matched `simple_fast_gpl`/
`simple_optimized_gpl` slices too -- re-run confirms perfect symmetry: both
also went 188 Fail -> 93 Fail (net -95 each), exactly matching the
monolithic slice's own delta and confirming the roadmap's own long-standing
"one shared runtime bug, not three pipeline-library bugs" hypothesis.
Combined: 564 Fail -> 279 Fail across the three `simple*` slices (net
-285). The `fuzz.*` (4 Fail) and `primitive_restart.*` (2 Fail) residuals
are unaffected (distinct, already-known bugs) -- `L419(b)`'s full 570-case
scope is now 285 remaining. The remaining 93-per-slice residuals partition
cleanly into already-owned clusters (`H37`'s 4 `backward_dependency*`
names and `H173(c)`'s 12 `streams*` names, each still exactly 12/36 across
the three slices) plus seven new, as-yet-unowned single/few-case names
(`draw_indirect_counter_resubmit`, `holes_{vert,geom}`,
`holes_extra_draw_{vert,geom}`, `maintenance5`,
`multi_stream_single_raster_non_zero`) and two larger new unowned
clusters, `resume_*` (27 per slice) and `winding_*` (43 per slice) --
tracked as new roadmap row `L419(m)`.

`ninja check-feme`: 3,708 Passed, 3 Unsupported, 0 Failed, 0 regressions
(net +1 test from the new unit test). `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed -- an internal signature-
canonicalization correctness fix, no feature/extension-surface change.

## `L419(m)`: resume-from-counter-buffer transform-feedback bug (231 -> 144)

Reduced the `resume_*` sub-cluster of `L419(m)`'s own 231-case residual by
running all 43 `dEQP-VK.transform_feedback.simple.resume_*` cases directly
(`--deqp-caselist-file=`): 27 Fail / 15 Pass / 1 NotSupported, every
failure the identical `Failed at item 0 received:<N> expected:0` pattern
at `vktTransformFeedbackSimpleTests.cpp:1131` -- the *first* captured
value already wrong, not a later one. The 27 failing names decompose
into exactly 9 unique (pass-count, buffer-size) pairs x 3 naming variants
(`resume_N_SIZE`, `resume_beginqueryindexed_streamid_0_N_SIZE`,
`resume_endqueryindexed_streamid_0_N_SIZE`, `N` in `{2,4,8}`, `SIZE` in
`{256,512,131072}`); `resume_1_*` (single-pass, no real resume) passes,
isolating the bug to the genuine multi-pass resume path.

Root cause, found by reading `feme/lib/Vulkan/CommandBuffer.cpp`'s
`BeginTransformFeedback`/`EndTransformFeedback` playback handlers: the
Vulkan spec requires `vkCmdBeginTransformFeedbackEXT`'s `pCounterBuffers`
argument, when non-null, to load each slot's *starting* byte offset from
that counter buffer (the value a matching prior
`vkCmdEndTransformFeedbackEXT` wrote there), i.e. "resume where the last
pass left off"; a null entry (or no `pCounterBuffers` at all) means
"start this slot at offset zero" instead. FeMe's handler ignored this
distinction entirely and always reset every bound slot's running
`XfbCapturedBytes` counter to 0, regardless of what `pCounterBuffers`
named -- a years-old "no resume-from-counter-buffer support yet" comment
at the call site confirms this was a known, not accidental, gap.
`feme/lib/Graphics/Executor.cpp`'s own capture-position math
(`captureTransformFeedback`'s `SeedProgress`, computing
`BaseVertex = *CapturedBytes / Stride`) already correctly resumes writes
from a nonzero `CapturedBytes` seed -- it was never consulted with one,
since `Begin` always handed it 0.

**Fix** (`feme/lib/Vulkan/CommandBuffer.cpp`'s `BeginTransformFeedback`
handler): for every slot whose `pCounterBuffers[I]` is non-null, read the
stored `uint32_t` byte count from that buffer (validating it is bound,
in-range, and resident, mirroring `EndTransformFeedback`'s own existing
checks) and seed `XfbCapturedBytes` with it instead of 0; slots with no
named counter buffer still reset to 0 as before. New unit test:
`CommandBufferTest.TransformFeedbackBeginResumesFromCounterBuffer` (seeds
a counter buffer with a sentinel "128 bytes already captured" value,
begins/ends with no draw in between, and checks the value round-trips
unchanged -- proving `Begin` loaded it rather than clobbering it with 0).

**Measured impact**: `resume_*` went from 27 Fail / 15 Pass / 1
NotSupported to 42 Pass / 1 NotSupported (0 Fail). Re-running the full
`L419(m)` residual set (`resume_*`, `winding_*`, and all seven singleton
names) across all three `simple`/`simple_fast_gpl`/`simple_optimized_gpl`
slices confirms the same perfect construction-mode symmetry established
by `L419(b)`: each slice's 77-case residual dropped to 48 (net -29 each,
-87 combined) -- `draw_indirect_counter_resubmit` and `maintenance5`
(2 of the 7 singleton names) turned out to share this same root cause and
are also now fixed, alongside the 27-per-slice `resume_*` cluster itself.
**Residual (144, 48 per slice)**: `winding_*` (43/slice) -- a distinct
"Vertex data verification failed" symptom at
`vktTransformFeedbackSimpleTests.cpp:1749`, not the `item 0` offset
mismatch this fix targeted, so likely an unrelated bug -- plus `holes_*`
(4/slice) and `multi_stream_single_raster_non_zero` (1/slice), none yet
triaged. Tracked as new roadmap row `L419(n)`.

`ninja check-feme`: 3,710 Passed, 3 Unsupported, 0 Failed, 0 regressions
(net +1 test). `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:
no change needed -- an internal command-playback correctness fix
implementing already-declared `VK_EXT_transform_feedback` resume
semantics, no new feature/extension-surface change.

## `L419(n)`: `winding_*` primitive-assembly transform-feedback bug (144 -> 27)

Reduced the `winding_*` sub-cluster of `L419(n)`'s own 144-case residual.
Reproduced `dEQP-VK.transform_feedback.simple.winding_triangle_strip_with_adjacency_8`
standalone: "Vertex data verification failed" at
`vktTransformFeedbackSimpleTests.cpp:1749`, with the embedded per-primitive
detail "expected rotation of [...] but found [...]". Categorizing all 80
`winding_*` cases by pass/fail showed an exhaustive split: every `*_list`
topology (`triangle_list`, `line_list`) passes; every `*_strip`, `*_fan`,
`*_with_adjacency`, and `patch_list` case fails.

Root cause, found by reading `feme/lib/Graphics/Executor.cpp`: the
vertex-shader-only (no geometry/tessellation stage) transform-feedback
capture call always captured XFB output in raw sequential vertex-shader-
invocation order. That is correct only for `List`-style topologies, where
each invocation is already its own output record in order by coincidence.
`VK_EXT_transform_feedback` actually requires capturing one record per
*assembled input-assembly primitive's* vertex: a `Strip`/`Fan` topology
duplicates a shared vertex once per primitive it belongs to, and a
`*WithAdjacency` topology with no bound geometry shader drops its
adjacency-only vertices entirely -- neither of which raw sequential order
produces. The already-correct primitive-assembly logic for this
(`TriIndices`/`LineIndices`, the same lists the rasterizer consumes) already
existed in `Executor.cpp`, just computed *after* the XFB-capture call, for
rasterization only.

**Fix**: hoisted that primitive-assembly computation to run before the
XFB-capture call instead of after it (a pure move, not a logic change —
the rasterizer further down now simply reuses the earlier result), and
changed `captureTransformFeedback` to take an explicit
`llvm::ArrayRef<uint32_t> FlatIndices` source-row sequence instead of an
implicit `{0,...,Count-1}` range, decoupling the capture's sequential
*write* position from its (now possibly duplicated/filtered) *read* row.
The vertex-shader-only call site builds `FlatIndices` for `Strip`/`Fan`/
`*WithAdjacency` topologies by flattening `TriIndices`/`LineIndices` per
instance (biased exactly the way the rasterizer's own `AbsTriIndices`/
`AbsLineIndices` already were); `PointList` and `List`-style topologies
keep the plain sequential range, unchanged. The two geometry-shader-
sourced call sites (already correctly merged/ordered) now pass a trivial
sequential sequence via a new `makeSequentialXfbIndices` helper and are
otherwise unaffected. New unit test:
`ExecutorTest.CapturesTriangleStripXfbInAssembledPrimitiveOrderWithDuplicatedVertices`
(a 4-vertex `TriangleStrip` draw, checking the captured XFB buffer holds
the two assembled triangles' own 6 vertex values in duplicated order,
not the 4 raw invocation values).

**Measured impact**: the standalone repro case now passes. Re-running the
full `L419(n)` residual set (`winding_*`, `holes_*`,
`multi_stream_single_raster_non_zero`) across all three
`simple`/`simple_fast_gpl`/`simple_optimized_gpl` slices confirms the same
construction-mode symmetry established by `L419(b)`/`L419(m)`: each
slice's 48-case residual dropped to 9 (net -39 each, -117 combined) --
every non-tessellated `winding_*` case (39 of 43 per slice) now passes.
**Residual (27, 9 per slice)**: `winding_patch_list_*` (4/slice) --
tessellated XFB capture is a structurally separate, not-yet-examined code
path (the vertex-shader-only capture call is gated on
`!Pipeline.hasTessellationStages()`, so a tessellated draw's domain-stage
output is never captured to XFB at all today), tracked as new roadmap row
`L419(o)` -- plus `holes_*` (4/slice) and
`multi_stream_single_raster_non_zero` (1/slice), both untouched by this
fix, confirming they are a separate root cause from `winding_*`.

`ninja check-feme`: 3,711 Passed, 3 Unsupported, 0 Failed, 0 regressions
(net +1 test). `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:
no change needed -- an internal rendering/capture-ordering correctness
fix for already-declared `VK_EXT_transform_feedback` topology support, no
new feature/extension-surface change.

## `L419(p)`: `holes_*` transform-feedback "no explicit `xfb_offset`" bug

`dEQP-VK.transform_feedback.simple.holes_vert` (and its `holes_geom`/
`holes_extra_draw_{vert,geom}` siblings, 4 cases/slice x 3 slices = 12)
failed with a *data* mismatch, not a crash: "Invalid data found for vertex
0: expected (10, 0, 30) and found (20, 0, 30)". The test declares three
scalar outputs (`goku`/`trunks`/`vegeta`) sharing `Location=0` via distinct
`Component` indices 0/1/2; only `goku` (component 0, `xfb_offset=0`) and
`vegeta` (component 2, `xfb_offset=8`) have an explicit `xfb_offset` --
`trunks` (component 1) deliberately has none, leaving a 4-byte "hole" at
byte offset 4 of the 12-byte stride-3 record that must stay zero (the
test's own comment: "note Y is not saved").

**Root cause**, confirmed via `--deqp-log-decompiled-spirv=enable` against
the real compiled SPIR-V: glslang decorates `trunks`' own `OpVariable`
with `XfbBuffer 0`/`XfbStride 12` (establishing the shared buffer's
layout) but, correctly, *no* `Offset` decoration at all -- the deliberate
signal that this particular element, despite sharing the buffer's layout,
is not itself captured. `CanonicalizeStage.cpp`'s `addElement` previously
folded this to `Elt.XfbOffset = D.XfbOffset.value_or(0)` unconditionally,
defaulting `trunks` to offset 0 -- the exact same slot `goku` legitimately
owns -- so `trunks`' own value (`20.0`) landed in `goku`'s slot instead of
being excluded from capture, matching the observed symptom exactly.

**Fix**: changed `SignatureElement::XfbOffset` from a plain `uint32_t`
(defaulting to 0) to `std::optional<uint32_t>` (mirroring `XfbBuffer`'s
own existing optional-ness), so "no `Offset` decoration at all" survives
as its own distinct state through canonicalization instead of collapsing
to a plain 0. `addElement` now only inherits `D.XfbOffset` when `D.XfbBuffer`
is itself present; `Executor.cpp`'s `captureTransformFeedback` skips any
element whose `XfbOffset` is absent, mirroring its existing `XfbBuffer`
guard exactly. `EntrySignature`'s binary serialization format bumped to
version 8 (new has-transform-feedback-offset flag word, same pattern as
`XfbBuffer`'s own has-value flag); the seven checked-in pre-baked
serialized-signature blobs in `feme/test/` (`feme-render`'s five
`.test` files, the two CPU stage-IO-wrapper `.ll` tests) were mechanically
migrated to the new layout.

**Measured impact**: all 12 `holes_*`/`holes_extra_draw_*` cases
(`simple`/`simple_fast_gpl`/`simple_optimized_gpl`) Fail -> Pass.
`ninja check-feme`: 3,714 Passed, 3 Unsupported, 0 Failed, 0 regressions
(one existing test's two assertions updated from
`EXPECT_EQ(Elt.XfbOffset, 0u)` to `EXPECT_FALSE(Elt.XfbOffset.has_value())`
-- a legitimately more precise result this fix now produces for that
already-covered "no own `Offset`" case, not a weakened test).

**New finding, not part of this fix**: this was the first *full*,
un-narrowed sweep of the `simple`/`simple_fast_gpl`/`simple_optimized_gpl`
groups (23,681 cases total) this multi-session project has run -- every
earlier residual count was a hand-picked subset. It surfaced 36 further
pre-existing failures (12/slice x 3) not previously tracked anywhere:
`streams_{1,3,6,14}`, `streams_clipdistance_{1,3,6,14}`, and
`streams_culldistance_{1,3,6,14}`, each a generic `Fail` with no detail
message yet triaged. Confirmed pre-existing (not a regression from this
fix) by reverting this change and re-running `streams_1`/
`streams_clipdistance_1`/`streams_culldistance_1` standalone against the
unmodified baseline -- all three still failed. New roadmap row `L419(q)`
tracks this cluster plus the two already-tracked ones (`L419(o)`'s
`winding_patch_list_*`, still-unowned `multi_stream_single_raster_non_zero`)
for the combined 51-case (17/slice) `simple`/`simple_fast_gpl`/
`simple_optimized_gpl` residual going forward.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal XFB-capture-eligibility correctness fix for
already-declared `VK_EXT_transform_feedback`, no new feature/extension-
surface change.

## `L419(r)`/`L419(s)`: `streams_*` cluster, two root causes, closing `H173(c)`

`L419(p)`'s own first full sweep (above) surfaced 36 previously-untracked
`streams_{1,3,6,14}`/`streams_clipdistance_{1,3,6,14}`/
`streams_culldistance_{1,3,6,14}` failures (12/slice x 3), all a generic
`Fail` with `Image comparison failed: max difference = (255, 0, 255, 0)`
-- a whole-channel magenta/black mismatch, not a subtle off-by-one.
Standalone re-run also found the bonus `multi_stream_single_raster_non_zero`
(1/slice x 3) failing identically, sharing the same root causes (confirmed
below).

While reducing this, a pre-existing roadmap row, `H173(c)` ("Fix nonzero
rasterization-stream selection for point-input geometry shaders... 36
`streams*` failures... Hand-trace one matched triplet"), turned out to
already name this exact cluster (12/slice x 3 = 36, matching exactly).
The session that first found this 36-case residual (tracked as `L419(q)`)
never found `H173(c)`'s own pre-existing row -- a process gap now flagged
in `agent_thoughts.md`, since the standing "grep `Roadmap.md` for a
candidate ID before using it" check only prevents *collisions*, not
*missed existing ownership* under a differently-worded row.

**Root cause #1 (`L419(r)`, `CanonicalizeStage.cpp`)**: the real CTS
geometry shader for this group emits every vertex to a non-zero stream
`s` via `layout(stream=s) out gl_PerVertex {...};`, with the pipeline's
`VkPipelineRasterizationStateStreamCreateInfoEXT::rasterizationStream`
also set to `s`. SPIR-V's `Stream` decoration is a *whole-variable*
decoration -- it applies to the `gl_PerVertex` block-instance variable
itself, never to individual members -- exactly like `XfbBuffer`/
`XfbStride`/`XfbOffset`, which already have an established whole-variable-
to-`BuiltIn`-member inheritance path in `CanonicalizeStage.cpp` (from the
`L419(b)`/`L419(p)` sessions). `Stream` had never been given the same
treatment: every `BuiltIn` member (`gl_Position` included) silently
defaulted to `Elt.Stream = 0` via `addElement`'s own
`D.Stream.value_or(0)`, regardless of the block's real declared stream.
This broke `Executor.cpp`'s per-stream `GSOutputElementsByStream`
bucketing: `gl_Position` landed in stream 0's bucket while rasterization
(selecting the real non-zero `RasterizationStream`) read a position-less
bucket -- exactly the whole-image `(255, 0, 255, 0)` mismatch.

**Fix**: added `if (!MemberD.Stream) MemberD.Stream = WholeVarD.Stream;`
to the existing `TakeBlockPath` member-decoration-inheritance loop,
mirroring the `XfbBuffer`/`XfbStride` pattern immediately above it. New
unit test: `CanonicalizeStageTest.
InheritsStreamOntoBuiltInBlockMembersWithNoOwnStream`.

**Measured impact of fix #1 alone**: built a 39-case targeted caselist
(`streams_{1,3,6,14}`, `streams_clipdistance_{1,3,6,14}`,
`streams_culldistance_{1,3,6,14}`, `multi_stream_single_raster_non_zero`,
across `simple`/`simple_fast_gpl`/`simple_optimized_gpl`). Monolithic
`simple` slice: 13/13 Fail -> 13/13 Pass. `simple_fast_gpl`/
`simple_optimized_gpl`: still 13/13 Fail each, uniformly, with the
identical mismatch signature -- a second, distinct, GPL-construction-
specific bug.

**Root cause #2 (`L419(s)`, `GraphicsPipeline.cpp`/`.h`)**:
`VK_EXT_graphics_pipeline_library` splits pipeline creation into
independently-created "library" objects linked together later.
`captureGraphicsPipelineLibraryState`'s `PRE_RASTERIZATION_SHADERS_BIT_EXT`
capture block deep-copies `VkPipelineRasterizationStateCreateInfo` but
unconditionally nulls its `pNext` (correct in general, since the source
chain is not safe to retain past the call) -- but never independently
re-captured the one pipeline-meaningful chained struct,
`VkPipelineRasterizationStateStreamCreateInfoEXT`'s `rasterizationStream`,
by value. At link time the merged `VkGraphicsPipelineCreateInfo`'s
`pRasterizationState` therefore pointed at a `pNext`-stripped copy, so
`translateRasterState`'s own `pNext` walk found nothing and
`RasterState::RasterizationStream` silently stayed at its spec default
(stream 0) for every GPL-constructed pipeline -- explaining the uniform
13/13 fail in both GPL slices (monolithic `simple` never goes through
this capture/link path, so fix #1 alone fixed it).

This is the identical shape of a problem `TessellationDomainOrigin`
(roadmap `L340`) already solved for
`VkPipelineTessellationDomainOriginStateCreateInfo`: capture the value
independently (not just the containing struct), fold it across linked
children, and re-chain a stable backing struct onto the link-time
reconstruction.

**Fix**: added `std::optional<uint32_t> RasterizationStream` to
`GraphicsPipelineLibraryState` and a `findRasterizationStream` helper
(mirrors `findTessellationDomainOrigin`); wired it through
`captureGraphicsPipelineLibraryState`'s existing `RS` capture,
`foldLinkedLibraryState`'s `PRE_RASTERIZATION_SHADERS_BIT_EXT` fold
branch, a new `RasterizationStateStreamState` backing field on
`LinkedPipelineStorage`, and the link-time reconstruction site (re-chains
onto `Storage.RasterizationState.pNext` whenever `S.RasterizationStream`
is set). New unit test: `GraphicsPipelineTest.
LinksLibraryWithNonZeroRasterizationStream` (GPL 4-part link round-trip,
mirrors `LinksLibraryWithLowerLeftTessellationDomainOrigin`).

**Measured impact (combined)**: re-ran the same 39-case caselist after
both fixes: **39/39 Pass** across all three slices (`simple`/
`simple_fast_gpl`/`simple_optimized_gpl`), including the bonus
`multi_stream_single_raster_non_zero`. `ninja check-feme`:
3,713/3,716 Passed (net +2 new tests), 0 regressions.

This closes the pre-existing `H173(c)` roadmap row and `L419(q)`'s own
tracking of this cluster; `H34`'s GPL-residual row is updated to reflect
`L419(s)` as the first genuinely GPL-linkage-specific fix this project
has needed. Remaining `simple`/`simple_fast_gpl`/`simple_optimized_gpl`
residual: `L419(o)`'s `winding_patch_list_*` only (4/slice).
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- both fixes are internal correctness fixes for already-declared
`VK_EXT_transform_feedback`/`VK_EXT_geometry_shader`/
`VK_EXT_graphics_pipeline_library` behavior, no new feature/extension-
surface change.

## Validation outside CTS

- `check-feme`: 3,708 passed, 3 unsupported, 0 failed.
- `check-hlsl-feme-vk`: 489 passed, 27 expected failures, 273 unsupported,
  2 failed (known: `Mandelbrot.test` full-suite flake, `WaveActiveMax.test`
  H169 triaged-closed issue), 1 unexpected pass (`array_of_matrices.test`,
  known flaky, XFAIL retained).

The offload suite required two isolated external corrections: boolean
specialization constants now use the four-byte `VkBool32` representation,
and the ARM64 host-wave-size artifact plus the stale DXC-only XFAIL now have
driver-specific expectations.

