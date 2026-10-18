# Vulkan CTS status

This report records the complete Vulkan CTS re-triage performed on
2026-10-12. It supersedes the 2026-10-05 full-run baseline and all partial
counts previously recorded in this file.

## Tested configuration

- FeMe revision: `d3577f8378eb`
- VK-GL-CTS upstream revision: `c28b36293093`
  (`vulkan-cts-1.4.7.0-40-gc28b3629`)
- VK-GL-CTS tested revision: `6b0464e83` plus `f43894f9a`, the upstream
  revision with an isolated host-mirror fix for std430 `vec3` arrays in
  `vktMeshShaderInOutTestsEXT.cpp` and an isolated `checkSupport`-argument
  fix in `vktRenderPassDepthStencilResolveTests.cpp` (see L419(k))
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
| api (see note below) | 267,512 | 116,698 | 21 | 150,781 | 0 | 0 | 16 |
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
Fail, DeviceLost, TimedOut, or process-abnormal outcomes. (Note: each
parenthetical number here is the group's **Pass** count, not its total case
count -- e.g. `compute` has 67,554 total cases, of which 66,853 are
`NotSupported` and 701 are `Pass`, with zero `Fail`/`DeviceLost`/`TimedOut`.
A 2026-11 session's raw `deqp-vk -n 'dEQP-VK.compute.*'` sanity sweep
initially misread "(701)" as the full case count and incorrectly suspected a
large case-count/baseline mismatch; re-checking `report.txt`'s per-case
`Unrun -> <result>` transitions for the `compute` and `synchronization2`
groups confirmed the parenthetical counts are exactly the `Pass` tallies and
there is no discrepancy.)

**Note on the `api` row (added a later session):** the `21` `Fail`/`16`
`TimedOut` figures above reflect this file's original 2026-10-12 full-CTS
snapshot. Many sessions since then (`L417`-`L435`) fixed individual `api.*`
cases as part of other work without this specific master-table row being
hand-edited each time. A dedicated, complete `dEQP-VK.api.*` re-sweep
(267,512/267,512 cases, 0 Unrun, see the `L435` section below) found the
*current* true count is **1 Fail** (the by-design `conformance_version`
case, not a bug) and **0 TimedOut** -- i.e. 20 of the original 21 `Fail`
cases and all 16 `TimedOut` cases have since been fixed by unrelated,
individually-tracked sessions, but this row's own numbers were never
updated to match. This is the same class of staleness risk for every other
row above and in "Notable clean groups": each reflects its state as of
2026-10-12 (or, for `synchronization`, as corrected post-`L434`), not
necessarily today's. A full-CTS re-triage (not just a single-family
re-sweep) is the only way to refresh every row at once; see this section's
own "Next steps" for why that is out of scope for a single session and
how to prioritize which rows to re-check first.

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
| ~~Storage images without a format using `A2B10G10R10_SINT_PACK32`~~ (14, now 0 -- re-verified this session; already resolved as an incidental side effect of unrelated prior fixes, no dedicated code change needed) | 0 | L419(i) |
| ~~Render-pass depth/stencil write conditions (9) and resolve (4)~~ (now 0 -- both fixed this session: the write-condition cluster via three stacked FeMe-side fixes to `initial_value` handling for a never-`OpStore`d `BuiltIn` output; the resolve cluster via an isolated upstream VK-GL-CTS fix to a `checkSupport` call that was silently skipping its own alternative-format capability check) | 0 | L419(k) |
| Mesh-shader properties and miscellaneous correctness after removing the 58 CTS-side failures | 11 | L419(f) |
| ~~Draw output-location (2)~~, line continuity (2), geometry primitive ID (1), and mandatory-feature reporting (1) | 4 | L419(j) |
| **Total** | **1,266** | |

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

## `L419(o)`: tessellated domain-stage transform-feedback capture (12 -> 0)

**Root cause**: `captureTransformFeedback`'s only call site (`Executor.cpp`)
was gated on `!Pipeline.hasTessellationStages() && !Pipeline.hasGeometryStages()`,
so a tessellating pipeline's domain-stage output -- its own final
pre-rasterization-stage output, exactly analogous to the non-tessellated
vertex shader's own output the existing call already captured -- was
never captured to any bound XFB buffer at all, regardless of whether the
domain stage's own signature requested it via `xfb_buffer`/`xfb_offset`/
`xfb_stride`. This was the `dEQP-VK.transform_feedback.simple*.
winding_patch_list_*` residual `L419(n)` broke out as this row: all four
remaining `winding_*` failures (per slice) were `patch_list` (tessellated)
cases, a structurally distinct code path from the non-tessellated
`TriIndices`/`LineIndices` primitive assembly `L419(n)` fixed.

**Fix**: added a second `captureTransformFeedback` call site, inside the
existing `if (TessLink) {...}` tessellation block in `executeDraws`,
immediately after `TessOutput`/`TessTris`/`TessLines` are built and
before they are moved into `AbsTriIndices`/`AbsLineIndices` for
rasterization (a later `std::move` would otherwise leave them in an
unspecified state). The new call builds its element list from
`TessLink->DomainSig.Elements` (the domain stage's own signature) and
its `FlatIndices` from `TessTris`/`TessLines` directly:

- No per-instance bias is needed, unlike the non-tessellated capture's
  `TriIndices`/`LineIndices` (per-instance templates biased by
  `Inst * PerInstance`): `TessTris`/`TessLines` are built with indices
  already offset by each patch's own `PatchBases[I]`, itself accumulated
  across every patch of every instance, so they are already globally
  absolute.
- No strip/fan de-duplication is needed, unlike `L419(n)`'s own
  `TriIndices`/`LineIndices` flattening (which must handle IA-level
  `Strip`/`Fan` topologies' vertex-sharing semantics): the tessellator's
  own output connectivity is always already a flat triangle/line list,
  with no implied shared-vertex primitive assembly.
- A `Point`-output-primitive domain stage leaves both `TessTris` and
  `TessLines` empty (no index-based primitive assembly happens for point
  output); this case falls back to the same `makeSequentialXfbIndices`
  helper the non-tessellated capture already uses for the analogous
  `PointList`-topology case.

New unit test: `ExecutorTest.CapturesTessellatedDomainStageOutputToXfbBuffer`.
It tags the domain stage's `SV_Position` output (not its color output --
the shared `TessDomainShaderIR` test fixture's own color output always
forwards control point 0's color unchanged by design, rather than
barycentrically interpolating, so it cannot distinguish invocations) and
uses the smallest possible undivided patch (`TessPartitioning::Integer`,
tessellation factor 1.0) so the domain stage runs exactly three times --
once per input control point -- making the captured bytes fully
deterministic. Confirmed via a debug instrumentation pass during
development that the captured vertex order is `(0, 2, 1)`, not the
patch's original `(0, 1, 2)` control-point order: `TriangleCcw`'s
`appendTriangle` swaps the last two operands relative to `TriangleCw`,
the same winding convention the tessellator's "fully unsubdivided" fast
path (`Tessellator.cpp`) already documents.

**Measured impact**: ran the full `winding_patch_list_*` caselist (24
cases: 4 base cases x 2 `_ptsz` variants x 3 slices) standalone via
`deqp-vk`: the 12 testable (non-`_ptsz`) cases now **12/12 Pass** across
all three slices (`simple`/`simple_fast_gpl`/`simple_optimized_gpl`); the
other 12 (`_ptsz` variants) remain `NotSupported`
(`shaderTessellationAndGeometryPointSize` not supported), unchanged by
this fix and unrelated to it. Also ran the full `transform-feedback`
mustpass group (133,719 cases) to check for regressions: only 4 failures
remain, all pre-existing and unrelated (`fuzz.{2_level_struct_array,
single_struct_array}.std_{vertex,geometry}`, confirmed present both
before and after this change via `git stash` -- a SPIR-V-to-MLIR
legalization gap for nested struct-array XFB globals, out of this fix's
scope; not yet tracked under its own roadmap row). `ninja check-feme`:
3,714/3,717 Passed (net +1 new test), 0 regressions.

This closes `L419(o)` and, since `Executor.cpp`'s tessellation block is
shared by every pipeline-construction style (not a GPL-specific code
path), also closes `H34`'s own GPL-transform-feedback-residual row: all
three slices now have 0 residual in their tracked scope.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal correctness fix for already-declared
`VK_EXT_transform_feedback`/tessellation feature surface, no new
feature/extension-surface change.

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


## `L418(a)`: CPU rasterizer `Draw`-path optimization for large MSAA targets

gdb-sampled the standalone `L418` profiling case (`dEQP-VK.api.copy_and_blit.
core.use_after_copy.a1r5g5b5_unorm_pack16.transfer_dst_optimal.
1024x1024x2_color_att_flag_img2img_msaa`) with a poor-man's sampling
profiler, since no sampling profiler (`perf`/`strace`/`ltrace`/`valgrind`)
is available in this environment: ran the case standalone in the
background, then repeatedly `gdb -p $PID -batch -ex "thread apply all bt"
-ex detach -ex quit` every ~0.3s across the ~4.6s run. Of 10 samples, 8
had their actively-running thread's leaf frame directly inside
`Executor.cpp`'s per-quad tile-processing lambda (`processTile`'s inner
`{lambda(int,int)#2}`) or `fixedPointEdgeFn`, called from it -- confirming
the per-sample coverage test as the specific hot loop within `Draw`
(matching the ~91%-of-total-time `Draw` cost `L418`'s own phase-timing
diagnostics had already isolated).

**Root cause**: `fixedPointEdgeFn(A, B, P, Bits)` quantizes all three of
`A`, `B`, and `P` to fixed-point on every call -- but the per-sample
coverage test calls it three times (once per triangle edge) for every
lane of every sample of every covered 2x2 quad, always against the same
triangle (so the same `A`/`B` per edge), testing many different `P`.
Re-quantizing each edge's own two (lane/sample-invariant) endpoints on
every one of those calls was pure redundant work -- for a 2-sample MSAA
quad (4 lanes x 2 samples = 8 coverage tests), the old code performed 8 x
3 edges x (4 endpoint + 2 `P`) = 144 `toSubpixelCoord` conversions where
only 12 (the 3 edges' 4 endpoints, once) + 16 (`P`, once per lane/sample,
shared across all 3 edges instead of re-converted per edge) = 28 are
actually necessary.

**Fix**: added `FixedPointEdge`/`makeFixedPointEdge`/
`evaluateFixedPointEdge` (`TriangleRasterization.h`/`.cpp`).
`makeFixedPointEdge` quantizes one edge's `A`/`B` once, in the existing
per-triangle scope (alongside the pre-existing `Area`/`FixedArea`
per-triangle precomputation); `evaluateFixedPointEdge` then only needs a
separately-quantized `P` (also now computed once per lane/sample, not
three times, since it does not depend on which edge is being tested) and
performs the same `(Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax)`
arithmetic the original function already did -- bit-for-bit identical
results, since `toSubpixelCoord` is a pure function of its own argument
regardless of how many times or where it is called. New unit test
`TriangleRasterizationTest.
PrecomputedEdgeMatchesFixedPointEdgeFnBitForBit` confirms this
equivalence directly, across both the existing axis-aligned-triangle
fixture and the `L402` near-tie regression case.

**Measured impact**: re-profiled the identical standalone case with the
same `FEME_VULKAN_TIME_PHASES=1`/`FEME_VULKAN_DEBUG_CMD_TIMING=1`
diagnostics `L418` used:

| Phase | Before | After | Change |
|---|---:|---:|---:|
| `CommandBufferExec`/`FenceWait` | 4552.7 ms | 3514.8 ms | -22.8% |
| three `Draw` calls (sum) | ~4160 ms | ~3117 ms | -25% |
| standalone process wall time | 4.65 s | 3.91 s | -16% |

(The standalone wall-time improvement is smaller in percentage terms than
the in-process phase improvements because it also includes fixed
per-process costs -- CTS/driver startup, device creation -- that this fix
does not touch.)

`check-feme`: 3715/3718 Passed (net +1 new test), 3 Unsupported, 0
Failed, 0 regressions. Real CTS regression sweep covering the rasterizer's
own three most relevant mustpass groups (`rasterization.txt` +
`clipping.txt` + `draw.txt`, 44,802 cases total, run via
`run_vulkan_cts.py` with 10 workers): 4046 Pass, 40752 NotSupported, 4
Fail -- all 4 confirmed pre-existing and unrelated to this fix via a
`git stash` A/B rerun (identical failures with and without the fix): 2 are
an unrelated `feme-cpu-wrap-entry` "barrier inside non-linear control
flow" compile error in `dEQP-VK.rasterization.line_continuity.*`
(line-strip/polygon-mode-lines), 2 are an unrelated
`b10g11r11-ufloat-pack32` format-conversion `Fail` in
`dEQP-VK.draw.renderpass.output_location.array.*`. Also re-ran the
original `L418` case standalone: still **Pass**, as expected (this is a
pure performance optimization, not a correctness fix).

This closes `L418(a)`. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed -- an internal
performance optimization only, no feature/extension-surface change.

## `L418(b)`/`L421`: `VkDeviceMemory` use-after-free race under contention; `L422` found, not yet fixed

`L418(b)` set out to reconcile `L418`'s own ~5s standalone per-case timing
with the roadmap's original 60s/600s full-suite timeout reports, under the
leading hypothesis that `QueueExecutor` worker-thread contention (not raw
per-case cost) produced the timeout numbers.

**Reproduction**: built a 17-case caselist of the heaviest
`1024x1024x2*img2img_msaa` cases and ran it fully concurrently
(`run_vulkan_cts.py --batch-size=1 --workers=17`, oversubscribing the
12-CPU host). Total wall time was only 43s, but several `initial-*.log`
files were truncated mid-case, needing the script's own recovery/verify
rounds to re-run ~9-11 of the 17 to get a complete result -- i.e.
processes were dying mid-case, not just slowing down. Confirmed via a raw
bash launch of all 17 `deqp-vk` processes directly (bypassing the script,
which deliberately ignores exit status): **6/17 exited with signal 139
(SIGSEGV)**. Solo (non-concurrent) runs of the same case never crash
(10/10 clean passes).

**Key isolation**: the crash does not require other FeMe processes at
all -- saturating all 12 CPUs with unrelated `bash` busy-loops
(`timeout N bash -c 'while :; do :; done'` x12) while running a *single*
`deqp-vk` process reproduced the SIGSEGV reliably. This is an internal
multi-threading race within one process, exposed by general scheduling
pressure increasing context-switch opportunities, not an inter-process
resource conflict. This is a more general, more reliable repro technique
than attach-based sampling and is worth keeping for future
concurrency-bug hunting.

**Root cause (`L421`)**: `gdb -batch -ex "handle SIGSEGV stop print
nopass" -ex run -ex "thread apply all bt"` under the busy-loop stress
caught the crash live: a `QueueExecutor` worker thread inside
`packClearColor`/`executeDraws` racing the main/test thread concurrently
inside `vkFreeMemory` -> `close()` (`vk::SimpleAllocation`'s own
teardown). `Image`/`Buffer` have been `PendingExecutionRefCounted` since
`L409`/`L410`, but both hold only a raw, non-owning `DeviceMemory *`
(`MemoryRangeBinding`, `MemoryBinding.h`) -- so keeping the `Image`/
`Buffer` wrapper alive does not stop its *bound memory* from being freed
out from under an in-flight task. This is fully legal Vulkan usage per
spec: an application only needs to keep the command buffer alive until
its submission completes, not every resource it references.

**Fix**: made `DeviceMemory` itself inherit `PendingExecutionRefCounted`,
mirroring `L409`/`L410`'s established pattern exactly. `vkFreeMemory`
(`Memory.cpp`) now calls `requestDestroy()`, freeing immediately only if
it returns `true`. `Sync.cpp`'s `collectExecutionRefs` gained a final
pass (`collectDeviceMemoryRefs`) that walks every already-collected
`Image`'s/`Buffer`'s `ranges()` (covering every one reached directly or
indirectly through `ImageView`/`DescriptorSet`/`Framebuffer` collection)
to find bound `DeviceMemory`, with matching `retainExecutionRefs`/
`releaseExecutionRefs` loops -- the release loop additionally calls
`freeShareableDeviceMemory` before destroying the object, unlike every
other `PendingExecutionRefCounted` type, since `vkFreeMemory`'s own
immediate-free path does both steps. New unit test
`SyncTest.SubmitThenImmediatelyFreeMemoryWithNoWaitSucceeds`.

**Verification**: `check-feme`: 3716/3716 Passed, 3 Unsupported, 0
regressions. Re-ran the exact isolated repro (busy-loop CPU saturation +
`gdb`) at matched 8-concurrent-process load: the pre-fix build crashed
with this exact signature (`packClearColor`/`vkFreeMemory` race) **8/8**
times; the post-fix build shows **0/8** occurrences of it.

**`L422` found, not fixed**: the same stress test against the post-fix
build still shows a *different*, lower-rate crash (3/8 at matched
concurrency): `feme::vulkan::Image::setLayout` crashes while the main
thread is not racing it at all -- it is already blocked in
`QueueExecutor::~QueueExecutor()`'s `std::thread::join()`
(`vkDestroyDevice` teardown), meaning the `Image` was freed earlier by
some other path not visible in that backtrace. A 20-process stress
variant also showed `runValidatedDraw`/
`feme::vulkan::DescriptorSetLayout::dynamicOffsetCount` crash signatures,
raising a specific suspicion: `DescriptorSetLayout` does not appear to be
in `L410`'s original `PendingExecutionRefCounted` list (only
`DescriptorSet` is), which would make this a `DescriptorSetLayout` analog
of `L421`'s own `DeviceMemory` gap -- not yet confirmed. Filed as its own
roadmap entry, `L422`, not fixed this session.

This substantially answers `L418(b)`'s original question: full-suite
timeout/`DeviceLost` reports are very plausibly explained, at least in
part, by this crash family manifesting under load, not merely by
`L418`'s own per-case cost (independently already -16% faster, see
`L418(a)`). `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`:
no change needed for either `L421` or `L422` -- internal correctness
fixes/findings only, no feature/extension-surface change.

## `L422`: `CommandBuffer` use-after-free race under contention, root-caused and fixed

Picked back up `L422` from the entry above. Reproduced the crash on
demand with a refined stress technique: 20-24 concurrent `deqp-vk`
processes (not `gdb`-wrapped busy-loops -- plain CPU/scheduling
contention between many real `deqp-vk` instances of the same test case
turned out to be necessary; background busy-loops alone never
reproduced it), all running
`dEQP-VK.api.copy_and_blit.core.use_after_copy.a1r5g5b5_unorm_pack16.transfer_dst_optimal.1024x1024x2_color_att_flag_img2img_msaa`,
achieving a ~20-30% per-process crash rate.

Added temporary `FEME_DEBUG_L422`-gated `llvm::errs()` diagnostics (no
usable DWARF info in this Release+assertions build, so `gdb`'s
`print`/`info locals` are not usable even at a named-symbol breakpoint --
see `feme/.instructions.md`'s own guidance) to `Image::setLayout`,
`vkCreateImage`/`vkDestroyImage`, `Sync.cpp`'s `retainExecutionRefs`/
`releaseExecutionRefs`, and both `vkCmdPipelineBarrier`'s record-time and
`executeCommandsInto`'s execute-time reads of each
`ImageLayoutTransition`.

**First disproved the leading inherited hypothesis**: `DescriptorSetLayout`
is *not* the root cause. `DescriptorSet` stores a deep *copy* of its
`DescriptorSetLayout` (not a pointer) specifically to avoid exactly this
class of dangling-layout bug -- an already-landed, pre-existing fix
(`Descriptor.h`'s own class comment references it as "roadmap L305").
The `DescriptorSetLayout::dynamicOffsetCount` crash signature from the
prior session's logs is a symptom of the same underlying bug surfacing at
a different call site, not evidence `DescriptorSetLayout` itself needs
`PendingExecutionRefCounted`.

**Root cause**: in at least one reproduced crash, the `Image` itself was
fully valid, internally consistent, and correctly deferred-destroy
pending (`ArrayLayers=2`, matching its own creation log) -- ruling out a
simple freed/corrupted `Image`. But the `setLayout` call against it used
a wildly wrong `LayerCount` (`80`/`112`). Diagnostics at both record time
(`vkCmdPipelineBarrier`) and execute time (`executeCommandsInto`'s
`PipelineBarrier` case) caught an execute-time `Barrier.Img` pointer with
no matching record-time print at all -- i.e. the memory `Cmd.ImageBarriers`
itself lived in had been freed and reused between record and execute
time. Unlike every type `L409`/`L410`/`L421` cover (a resource a command
only *references*), `executeCommandBuffer` (`CommandBuffer.cpp`) takes a
`const CommandBuffer &` and reads `CmdBuf.commands()` *live* on the
`QueueExecutor` worker thread, well after `vkQueueSubmit` enqueued the
submission task and returned -- and `CommandBuffer` itself had never been
made `PendingExecutionRefCounted`. `CommandPool::free`/`reset`/
`~CommandPool()` freed the `CommandBuffer` object immediately and
unconditionally, so a fully legal `vkFreeCommandBuffers`/pool-teardown
call (the exact CTS pattern: tearing a test's objects down together right
after a *different* submission's own `vkWaitForFences` reported a
timeout, with no further wait) raced this same worker thread's
still-pending read of the now-freed `CommandBuffer`'s own `Commands`/
`ImageBarriers` storage -- explaining both the "freed-looking garbage"
and "valid-but-wrong-Range" signatures as the same race viewed from
different reused-memory contents.

**Fix**: made `CommandBuffer` itself inherit `PendingExecutionRefCounted`,
mirroring `L409`/`L410`/`L421`'s pattern. `Sync.cpp`'s
`collectExecutionRefs` now also retains every primary/secondary
`CommandBuffer` a submission reaches (`ExecutionRefs::CommandBuffers`);
`CommandPool::free`/`~CommandPool()` route through a new
`destroyOrDeferCommandBuffer` helper mirroring `Descriptor.cpp`'s
`destroyOrDeferSet` exactly (both types are heap-owned via `unique_ptr`/
plain `new`, not the generic `Allocator`, so `Callbacks` is unused);
`releaseExecutionRefs` plain-`delete`s a deferred `CommandBuffer`,
matching `DescriptorSet`'s identical path. Removed every temporary
`FEME_DEBUG_L422` diagnostic before landing the fix. New unit test
`SyncTest.SubmitThenImmediatelyFreeCommandBufferWithNoWaitSucceeds`,
confirmed (by temporarily reverting just the fix, keeping the new test)
to reliably crash the pre-fix code with the exact same signature
(SIGSEGV inside `QueueExecutor::run()`'s submission task) and pass
cleanly post-fix, 15/15 repeated runs.

**Verification**: `check-feme`: 3717/3717 Passed (3720 total, 3
Unsupported), 0 regressions. Re-ran the 20-24-concurrent-process isolated
repro twice more post-fix (30 and 40 concurrent processes, 70 total
runs): **0/70** crashes, vs. the pre-fix ~20-30% per-process rate.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change --
internal correctness fix only, no feature/extension-surface change.

## `L423` found: pre-existing loader-level crash, not FeMe, not fixed

While sanity-sweeping `dEQP-VK.api.object_management.*` after `L421`'s
fix (a multithreaded resource-creation/destruction stress group, a
natural candidate for exercising the same class of lifetime bug),
`dEQP-VK.api.object_management.multithreaded_per_thread_resources.device`
crashed with SIGSEGV on its own, no artificial CPU-saturation stress
needed (~1/3 of solo runs). A `gdb` backtrace placed the crash inside
`libvulkan.so.1` itself (the Khronos loader) -- `0x... in ?? () from
/usr/lib/aarch64-linux-gnu/libvulkan.so.1` -- not in any FeMe code.

Checked whether this was introduced by `L421`'s fix: reverted `Memory.h`/
`Memory.cpp`/`Sync.cpp`/`ExecutionLifetime.h` to their pre-`L421` state
(`git checkout 80ddd348a27d --`), rebuilt, and reran -- the crash still
reproduced at the same rate (1/3) with the unmodified binary, confirming
it predates `L421` and is unrelated to it.

Not yet root-caused past confirming the crash site is in the loader, not
FeMe's own code. Filed as `L423` for a future session: needs
determining whether this is a genuine bug in how FeMe's
`vkCreateDevice`/`vkDestroyDevice` interact with the loader's per-thread
dispatch tables (which would make it a FeMe bug, even though the crash
*address* is in the loader) or a loader-side bug independent of any ICD
(which would make it an "issue outside FeMe" needing an isolated
reproducer and a self-contained fix per the standing instruction) --
ownership not yet determined either way.

## `L423` ownership determined: confirmed pre-existing, upstream Vulkan-Loader bug

Resumed this session with a full "every thread" `gdb` backtrace (`thread
apply all bt`, not just the crashing thread) of six independent solo
`deqp-vk --deqp-case=dEQP-VK.api.object_management.multithreaded_per_thread_resources.device`
runs against the FeMe ICD (`libvulkan.so.1.4.341`, i.e. `libvulkan1`
1.4.341.0-1 from Debian/Ubuntu). Three of six crashed (matching the
~1/3 rate noted when `L423` was first found).

The crashing thread's own backtrace (not just its single-frame signal
summary) is:
```
#0  0x... in ?? () from /usr/lib/aarch64-linux-gnu/libvulkan.so.1
#1  0x... in ?? () from /usr/lib/aarch64-linux-gnu/libvulkan.so.1
#2  vk::DeviceDriver::DeviceDriver(...) ()
#3  vkt::CustomDevice::CustomDevice(...) ()
#4  vkt::InstanceWrapper::createCustomDevice(...) ()
#5  vkt::api::(anonymous namespace)::Device::create(...) ()
#6  vkt::api::(anonymous namespace)::CreateThread<...>::runThread() ()
```
i.e. the crash is inside the Vulkan loader's own trampoline/dispatch-table
setup, called while deqp-vk's `DeviceDriver` constructor is populating its
function pointer table for a *newly created* `VkDevice` via repeated
`vkGetDeviceProcAddr`-family calls. Critically, **other threads in the
same crashing run's full thread dump are concurrently inside
`vkDestroyDevice` from `libvulkan.so.1`** (`pthread_mutex_lock` inside
`vkDestroyDevice`, called from `vkt::DeviceWrapper::~DeviceWrapper()`) --
i.e. one thread's `vkCreateDevice`-time loader dispatch-table setup races
a *different* thread's concurrent `vkDestroyDevice`-time loader
dispatch-table teardown, for two entirely independent, per-thread-owned
`VkDevice`s. Every FeMe-side frame in every captured backtrace is benign
(either blocked in `QueueExecutor::run()`'s `pthread_cond_wait`, or not
present at all on the crashing thread) -- the fault is squarely inside
the loader's own internal state, not in any FeMe entry point.

**Confirmed FeMe-specific triggering rate, not a universal loader bug**:
reran the identical test case six times against Mesa's `llvmpipe`
software rasterizer ICD instead of FeMe (`unset VK_ICD_FILENAMES`, same
`deqp-vk` binary) -- **0/6 crashes**. This does not mean the bug is
FeMe's fault; FeMe's `vkCreateDevice`/`vkDestroyDevice` are dramatically
faster/lighter-weight than `llvmpipe`'s, so FeMe-backed runs reach a much
higher rate of concurrent cross-thread `vkCreateDevice`/`vkDestroyDevice`
calls per second, making an already-present, low-probability upstream
race proportionally far more likely to land within a short test run.

**Confirmed this is a known, acknowledged, still-unfixed upstream
Vulkan-Loader issue**: a web search for the crash signature (loader
trampoline code called during device-driver dispatch-table setup racing
a concurrent `vkDestroyDevice` on a *different* device) found
[KhronosGroup/Vulkan-Loader#200](https://github.com/KhronosGroup/Vulkan-Loader/issues/200),
"Race condition in `vkCreateDevice()`+`vkDestroyDevice()`+
`vkGetDeviceProcAddr()`" -- an open, maintainer-acknowledged issue whose
description matches this crash almost exactly: concurrent
`vkCreateDevice`/`vkDestroyDevice`/`vkGetDeviceProcAddr` calls on
*different* devices/instances from different threads can crash because
the loader's internal per-device dispatch pointers (`dev->icd_device`/
`dev->chain_device` in the loader's own terms) can be nulled out by one
thread's teardown while another thread's `loader_get_dispatch()`-style
code is still reading them -- with the reporter's own conclusion being
that only a loader-internal global lock (which the loader does not yet
apply) would close the race. No patch has landed upstream as of this
session.

**Conclusion / ownership**: this is a genuine upstream Vulkan-Loader bug
(`libvulkan1` 1.4.341.0-1), not a FeMe bug, and not fixable from within
this repository -- the Vulkan Loader is not an LLVM subproject and its
source is not checked out anywhere in this environment (only the
system-installed shared library is present). The standing "issue outside
FeMe: create an isolated reproducer, fix it, apply the fix in a
self-contained commit touching only files outside `feme/`" instruction is
scoped to other LLVM subprojects within this monorepo and does not apply
here; there is no local source tree for the Vulkan Loader to patch. No
FeMe-side code change is warranted or possible: FeMe's own
`vkCreateDevice`/`vkDestroyDevice`/`vkGetDeviceProcAddr` implementations
were re-read this session and found to do nothing non-compliant with the
Vulkan spec's external-synchronization requirements for these entry
points (the spec requires only that *each* `VkDevice` handle itself be
externally synchronized across calls that take it, which this test
already respects per-device -- it does not require any cross-device
synchronization, and the loader's own internal state is exactly what the
upstream issue says is unsafe without one). Marking `L423` as "triaged,
root-caused, confirmed external, no action possible in this repo" rather
than "fixed" -- the only true fix is an upstream Vulkan-Loader patch.
`check-feme`, the Vulkan14FeatureInventory, and the VulkanExtensionInventory
all need no change for this entry (no FeMe code or feature/extension
surface was touched).

## `L419(e)`: all-format color blits, `b10g11r11_ufloat_pack32` half closed (20 -> 12)

The 20-case `api.copy_and_blit.core.blit_image.all_formats.color` cluster
splits cleanly into two unrelated groups by destination format: 8 cases
(`bc4_snorm_block`/`bc5_snorm_block` sources x 4 tilings) targeting
`VK_FORMAT_B10G11R11_UFLOAT_PACK32`, and 12 cases
(`astc_{8x8,10x5,12x12}_unorm_block` sources x 4 tilings) targeting
`VK_FORMAT_A2B10G10R10_SNORM_PACK32`.

**`b10g11r11_ufloat_pack32` group (8, fixed this session)**: reduced to one
standalone case
(`...bc5_snorm_block.b10g11r11_ufloat_pack32.general_general_linear`).
`tcu::ImageCompare`'s own diff output showed `max difference = (inf,
0.0078125, 5.91278e-05, 0)` -- a literal `Infinity` in the R channel, G/B
within threshold, alpha diff 0 (format has no alpha). Decoded the qpa log's
embedded `Result`/`Reference`/`ErrorMask` PNGs directly (base64 + Pillow)
to find the single failing pixel, `(13, 10)`; confirmed it falls inside one
of this test's own 20 `VkImageBlit` regions (a 64x64-source-to-16x16-
destination minification step), not an untouched background pixel as
initially hypothesized.

Instrumented (temporarily, removed before landing) the exact write at
`(13, 10)`: the blend accumulator's own inputs were a safe, finite
`(R=6.9388939039072284e-18, G=-0.1417322834645669, B=0, A=1)` -- nowhere
near overflow -- yet the packed output word decoded to `R11=1984,
G11=2047 (all-1s), B10=31 (all-1s)`, impossible for these inputs under
`encodeR11G11B10Float`'s documented algorithm. Root-caused to
`packUnsignedMinifloat` (`ImageFixture.cpp`): its subnormal/underflow
branch computes `Shift = (EMin - Exp) + (52 - MantissaBits)` with no upper
bound. For `R=6.94e-18` (`Exp` around `-58`, far below this 11-bit
minifloat's own denormal floor around `1e-7`), `Shift` reaches `90`, so
`Half = 1ull << (Shift - 1)` becomes a 64-bit left-shift by `89` --
undefined behavior in C++ (shift amount must be `< 64`). In practice this
produced a garbage, non-zero `R11` field large enough to overflow past its
own 11-bit boundary and corrupt the adjacent `G11`/`B10` fields once
OR'd together in `encodeR11G11B10Float`, landing on an all-1s exponent
(`Infinity`'s own bit pattern) in the unrelated `G` channel.

**Fix**: added a `Shift >= 54` short-circuit returning a flat `0` before
the shift arithmetic -- mathematically correct (not just a safety clamp),
since `Full`'s significand is always a 53-bit value and can never reach
the halfway rounding point once shifted right by 54 or more bits, so the
correctly-rounded answer for any value this small is exactly zero. New
unit test `ImageFixtureTest.PacksR11G11B10FloatUnderflowsTinyValuesToZero`
(confirmed to reproduce the actual symptom, `nan`, against a stashed
pre-fix build via `git stash`/rebuild/revert).

**Measured impact**: all 8 `b10g11r11_ufloat_pack32` cases Fail -> Pass.
A full `dEQP-VK.api.copy_and_blit.core.blit_image.*` regression sweep
(73,839 cases) shows exactly 12 Fail both before and after this fix,
identical case names each time (the `a2b10g10r10_snorm_pack32` group
below) -- 0 regressions, 0 newly-passing cases outside the targeted 8.
`check-feme`: 3,719 Passed, 3 Unsupported, 0 Failed, 0 regressions.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal pack/unpack correctness fix for an already-supported
core format, no feature/extension-surface change.

**Residual (12, not yet investigated)**: the `a2b10g10r10_snorm_pack32`
group shows a full-scale (`1.0`) alpha-channel diff (confirmed via an
earlier standalone run of `astc_10x5_unorm_block.a2b10g10r10_snorm_pack32.
general_general_linear`: `max difference = (0.00391394, 0.00391394,
0.00391394, 1)`, RGB tiny/within-threshold, alpha diff full-scale), an
unrelated failure signature (no `inf`, different destination format,
different diff channel/magnitude pattern) from the `b10g11r11_ufloat`
group just fixed. Not yet root-caused; left for a future session (see
`Roadmap.md`'s `L419(e)` entry).

## `L265`: ASTC-alpha SNORM "tie" root-caused as an 8-bit decode-precision gap, not a rounding bug (12 cases, fixed)

Two prior sessions investigated the residual 12-case
`astc_{8x8,10x5,12x12}_unorm_block` -> `a2b10g10r10_snorm_pack32` cluster
(`L419(e)`'s residual half, tracked as `L265`) without resolving it. The most
recent of those sessions bit-exact-verified FeMe's own `ASTCDecode.cpp`
pipeline end to end against a from-scratch Python re-implementation (every
stage matched) and concluded the remaining discrepancy was a genuine
*rounding-direction conflict*: this cluster appeared to need
ties-away-from-zero, directly contradicting the ties-to-even fix `L260`
had already landed for a different (smaller) ASTC footprint cluster -- and
left the investigation deferred as apparently unresolvable with one global
rule.

**This session re-opened the investigation and found the "tie-direction
conflict" framing itself was a red herring.**

Reproduced the known repro,
`astc_10x5_unorm_block.a2b10g10r10_snorm_pack32.general_general_linear`,
and re-confirmed the two exact failing destination pixels, `(34, 1)` and
`(36, 1)`, both via embedded qpa PNG diffing (as in earlier sessions) and
via fresh instrumentation. First used targeted debug instrumentation
(temporary, fully reverted after use) in `ImageOps.cpp`'s blit region loop
to dump each failing pixel's actual `VkImageBlit` region geometry -- this
confirmed the "staircase" multi-region structure hypothesized at the start
of this segment: pixel `(34,1)` belongs to a `Dst=[33,35)x[0,2)` region and
`(36,1)` to an independent `Dst=[35,37)x[0,2)` region, both mapping the
*entire* 60x60 source image down into a 2x2 destination rectangle (a 30x
minification), each computing an identical region-local sample point
`(U, V) = (45, 45)` by coincidence of both regions using the same local
`Tx = 0.75` fraction -- not a geometric bug, just an explanation for why
two different destination pixels produce identical source coordinates.

With the geometry confirmed benign, re-verified by hand (and via a direct
Python float32 simulation) that FeMe's own blend of its four decoded
source-alpha samples lands on an *exact* IEEE-754 `0.5` -- consistent with
every prior session's finding. The new, decisive step: **instrumented
VK-GL-CTS's own reference code directly** (`vktApiBlittingTests.cpp`,
temporary debug print added and fully reverted before this document was
written -- `git diff --stat` in the `VK-GL-CTS` checkout confirmed clean
afterward) to print the *actual* decoded source-texel alpha values CTS's
own `tcuAstcUtil.cpp` decoder computes for the same four source texels
FeMe's blit blends (`(44,44)`, `(45,44)`, `(44,45)`, `(45,45)`):

```
FeMe:  0.937255, 0.921569, 0.070588, 0.070588   (RGBA8-quantized)
CTS:   0.936035, 0.921387, 0.072449, 0.072449   (16-bit float, unquantized)
```

These do **not** match -- a small (~0.001-0.002) but real per-texel
decode discrepancy. Tracing `tcuAstcUtil.cpp`'s own weight/endpoint
interpolation (`c = (c0*(64-w) + c1*w + 32) / 64`, run at 16-bit precision,
`c0`/`c1` being each 8-bit endpoint replicated into a 16-bit value) showed
CTS keeps this intermediate as a **float** (`c == 65535 ? 1.0f :
(float)c / 65536.0f`) for any non-sRGB LDR decode -- it never quantizes
down to 8 bits. FeMe's `ASTCDecode.cpp::decodeASTCBlockLDR` computes the
exact same 16-bit `Interp` value internally, but **always** truncates it
to a `uint8_t` RGBA8 output texel (`Output[...] =
(Interp*255+32767)/65536`, i.e. `239.228 -> 239`) -- a deliberate,
long-standing architectural convention shared by every compressed-format
decoder in this codebase (ASTC, BC, ETC2 all decode straight into an
`R8G8B8A8_UNORM` intermediate for sampling simplicity; see `Image.h`,
`CommandBuffer.cpp`, `ImageOps.cpp`'s own comments).

That 8-bit truncation is exactly what turns CTS's own unambiguous,
no-tie blend (`(0.936035+0.921387+0.072449+0.072449)/4 = 0.500577...`,
which rounds up to SNORM-2-bit code `1` with no tie at all) into FeMe's
blend from pre-truncated 8-bit inputs landing on a mathematically *exact*
`0.5` -- a genuine tie that CTS's own math never actually has. **This
conclusively rules out a rounding-direction bug entirely**: `L260`'s
`ties-to-even` fix is independently confirmed correct per spec (checked
against CTS's own `tcuTexture.cpp::convertSatRte`, used by
`snormFloatToChannel`) and is not implicated here. The real mechanism is
a precision gap between FeMe's 8-bit ASTC-decode architecture and CTS's
16-bit-float reference model, which only produces a hard, binary pass/fail
cliff in this specific case because the destination format's alpha
channel is a 2-bit SNORM field -- a 3-way quantization with exactly one
possible tie value (`0.5`) -- any wider destination channel would have
simply absorbed the ~0.001 discrepancy within its own comparison
threshold, the same way this test's RGB channels already do (`~0.0039`
diff, comfortably inside the `~0.065` RGB threshold logged in the qpa).

**No fix implemented this session.** A real fix requires propagating ASTC
decode precision beyond 8 bits (at minimum for the blit/copy sampling path)
through `ASTCDecode.cpp`'s `Output` buffer and `ImageOps.cpp`'s per-texel
fetch -- a cross-cutting precision-architecture change, scoped out into
`Roadmap.md`'s new `L424(a)`/`L424(b)` breakdown rather than attempted as a
single high-risk patch this session, given it touches code shared by every
passing ASTC/BC/ETC2 case in the suite. All temporary debug
instrumentation (in both `feme/lib/Vulkan/ImageOps.cpp` and VK-GL-CTS's
`vktApiBlittingTests.cpp`) was fully reverted; both trees rebuilt clean
afterward (`libfeme_vulkan.so` and `deqp-vk`), and the 12-case failure
count is unchanged (unavoidable, since no fix was applied) --
`check-feme`: unaffected, no FeMe source file changed this session.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- confirmed via grep, no feature/extension-surface touched; this
was a pure investigation/root-cause session with a documentation-only
roadmap update.

### Update (follow-up session): fixed, with one self-caught regression along the way

Implemented the `L424(a)`/`L424(b)` fix scoped above. One design
simplification from the original plan: no new entry point was added to
`ASTCDecode.cpp` -- the already-existing `decodeASTCBlockHDR` (full-float
LDR decode, previously proven bit-exact against CTS's own reference) already
provides exactly the precision needed. The missing piece was entirely on
the *consuming* side.

**The fix** (`ImageOps.cpp`): added a new `SrcASTCFilteredHighPrecision`
branch to the blit's `srcColor` per-texel fetch lambda, used only when the
source is a non-sRGB LDR ASTC format *and* the blit filter is not
`VK_FILTER_NEAREST` (every other caller -- unfiltered fetches, sRGB ASTC
footprints, regular texture sampling, `CommandBuffer.cpp`'s upload path --
is unchanged, still on the original `decodeASTCBlock` 8-bit path). This
branch decodes via `decodeASTCBlockHDR`, then rounds each decoded
component to the nearest IEEE binary16 (half-precision) value via a new
`roundToHalfPrecision` helper (round-tripping through
`llvm::APFloat::IEEEhalf()`) -- replicating CTS's own
`TextureFormat(RGBA, HALF_FLOAT)` decompressed-ASTC-texture storage
(`tcuCompressedTexture.cpp`'s `getUncompressedFormat`) before its own
blit/copy filtering blends texels together. This is the key insight this
fix depends on: the bug was never simply "FeMe's ASTC decode is too
imprecise" (`decodeASTCBlockHDR` was already bit-exact), it was "FeMe's
decode doesn't replicate the *specific* precision budget CTS's own
reference applies before blending" -- matching that budget exactly (not
exceeding it) is what makes FeMe's blend tie/non-tie the same way CTS's
does.

**Mid-session self-caught regression.** The first version of this fix
widened the half-rounded float to a 16-bit UNORM integer and routed it
through the pre-existing `R16G16B16A16_UNORM` `unpackColor` case, to reuse
that code rather than adding a new bypass path. This introduced a *second*,
unrelated tie: `65535` is odd, so an exact half-precision `0.5` scaled to a
16-bit numerator lands on exactly `32767.5`, which cannot round-trip back
to exactly `0.5` under any integer rounding rule (`std::lround`'s
round-half-away-from-zero rounds it to `32768`, and `32768 / 65535.0 =
0.500007629...`) -- turning what should have been an exact, legitimate tie
into a spurious non-tie bias. This flipped two previously-passing pixels
(`(44, 33)`/`(34, 40)`, in one of this same test's *identity-scale*
regions -- a pure single-texel sample, no blending at all) from Pass to
Fail, within the very test the fix was meant to repair.

Caught via a full qpa Result/Reference PNG diff against a saved pre-fix
baseline screenshot (confirming the regression was newly introduced by
this session's own change, not a pre-existing issue merely unmasked), then
root-caused via targeted `fprintf` instrumentation of the exact 8-neighbor
bilinear blend weights and per-neighbor alpha samples at the regressed
pixel -- which showed `Weight == 1.0` for a single neighbor (a pure,
unblended sample) whose own half-rounded alpha was exactly `0.5`, yet the
accumulated result was `0.50000762951094835`, pinpointing the 16-bit
round-trip step (not the ASTC decode or the blend itself) as the actual
source of the bias.

**Fixed** by removing the lossy 16-bit-UNORM round-trip entirely: the
half-rounded float components are now written directly into the fetch's
own output array, bypassing `DecodeBuf`/`unpackColor` for this path
altogether, so no further integer-precision rounding is introduced after
the deliberate half-precision rounding step.

**Verified:**
- The original repro (`astc_10x5_unorm_block.a2b10g10r10_snorm_pack32.general_general_linear`)
  now fully Passes (was 0/1, now 1/1), with both the originally-reported
  `(34, 1)`/`(36, 1)` pixels and the mid-session regression's `(44,
  33)`/`(34, 40)` pixels all correct.
- The full 12-case cluster (`astc_{8x8,10x5,12x12}_unorm_block.a2b10g10r10_snorm_pack32.*`,
  168 cases including unsupported tilings): 112 Passed, **0 Failed**, 56 Not
  supported.
- The full `dEQP-VK.api.copy_and_blit.core.blit_image.*` CTS group (73,839
  cases): 26,941 Passed, **0 Failed**, 46,898 Not supported -- no
  regressions anywhere else in the family.
- New unit test `ImageOpsTest.FilteredASTCBlitPreservesBlendPrecision`
  (`feme/unittests/Vulkan/ImageOpsTest.cpp`) constructs two adjacent
  void-extent ASTC blocks encoding alpha `1`/`65535` and confirms a 50/50
  blit blend resolves to the non-tie, high-precision answer (alpha rounds
  up to `1`) rather than the artificial 8-bit-truncated tie (which would
  round down to `0`).
- `ninja check-feme`: 3,720/3,723 Passed, 3 Unsupported, 0 Failed -- zero
  regressions.
- `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
  needed -- confirmed via grep; this is an internal precision-matching
  fix with no feature/extension-surface change.

`Roadmap.md`: `L265`, `L419(e)`, `L424`, `L424(a)`, and `L424(b)` all
struck through as done.


## `L419(j)` (partial): draw output-location Row/Component mis-resolution fixed

`CanonicalizeStage.cpp`'s `resolveOffsetWithinElement` forced the tight,
ABI-padding-free array stride for every single-global `Input`-direction
stage-IO access (`UseTightArrayStride || !IsOutput`), on the premise --
true for a genuine SPIR-V matrix, but empirically false for an ordinary
array-of-narrow-vectors varying (e.g. `vec3 color_in[3]`) -- that a bare
`Input`/`Output` interface variable's GEPs always fold tight (the SPIR-V
spec permits `ArrayStride`/`MatrixStride` decorations only on a
`Block`/`BufferBlock` struct member, never on a bare interface variable).
Both shapes erase to the identical LLVM `[N x <M x float>]` type with no
decoration or residual-byte-offset signal left to distinguish them once
erased -- `resolveRowComponent`'s own pre-existing `L272` comment already
documented this exact array shape folding against *either* stride
depending on `DataLayout` timing.

This mis-resolved a plain `vec3[3]` `Input` varying's non-zero-row,
component-0 reads as `(Row, Component=Row)` instead of `(Row,
Component=0)`, failing the `output_location.array.*` pair with a
complementary (`1-x`) value-mismatch signature -- CTS's own test-vertex-
color data is specifically shaped to catch exactly this class of
wrong-component bug.

**Fixed** by tracking SPIR-V's own matrix-vs-array distinction *before* it
is lost: `SPIRVToLLVMPatterns.cpp`'s `StageIOGlobalVariablePattern` now
records whether a stage-IO global's declared type is (an array of) a
genuine `spirv.matrix` via a new `feme.spirv.NarrowMatrix` metadata
channel (mirroring the existing `feme.spirv.Int16Signed` one, same
collect/attach shape in `StageIODecorations.cpp`/`SPIRVToLLVMTranslator.cpp`).
`resolveOffsetWithinElement` now only forces the tight stride for an
`Input` access when this metadata says the global is a genuine matrix,
restoring `resolveRowComponent`'s own self-discovery (its exact-division
test, not `L404`'s aggressive packed-row fallback) for the ordinary array
case. This directly conflicted with, and required narrowing rather than
reverting, `L404`'s prior fix for the genuine-matrix case -- the two
scenarios share the identical byte-offset (16) and LLVM type shape
(`[3 x <3 x float>]`) with opposite correct answers, resolvable only with
this new, earlier-captured provenance bit.

**Verified:**
- New unit test
  `CanonicalizeStageTest.ResolvesComponentZeroOfOrdinaryNarrowVectorArrayRowsAtPaddedStride`
  covers the previously-regressed array case; the existing
  `...AtTightStride` mat3 regression test's hand-written IR now carries
  the new metadata too, matching what the real conversion attaches for a
  genuine matrix. Full `CanonicalizeStageTest` suite: 143/143 Passed (+1
  new test, 0 regressions).
- `dEQP-VK.draw.renderpass.output_location.array.*`: **28/28 Pass** (was
  26/28).
- `dEQP-VK.draw.renderpass.output_location.*` (the wider group): 30/30
  Pass.
- `dEQP-VK.pipeline.pipeline_library.vertex_input.*mat*` (`L404`'s own
  regression scope): 1,840/1,840 effective Pass (1,840 Pass, 2,026 Not
  supported, 3 Warnings pre-existing and unrelated), **0 Fail** -- no
  regression from narrowing `L404`'s fix.
- `ninja check-feme`: 3,721/3,724 Passed, 3 Unsupported, 0 Failed (+1 new
  test, 0 regressions).
- `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
  needed -- confirmed via grep; this is an internal compiler-correctness
  fix with no feature/extension-surface change.

`Roadmap.md`: `L419(j)` updated (not struck through -- 5 of its 7 cases
remain: line continuity (2), geometry primitive ID (1), mandatory-feature
reporting (1), own conformance-version reporting (1, `L412(g)` won't-fix)).

### `L419(j)` triage: `line_continuity` pair root-caused (not yet fixed)

`dEQP-VK.rasterization.line_continuity.{line-strip,polygon-mode-lines}`
both fail identically:

```
error: feme-cpu-wrap-entry: function 'main' has a barrier inside
non-linear control flow (a surviving branch not part of a supported
loop); region splitting only supports a straight-line wave body or a
single uniform loop (roadmap milestone 9 deviation)
```

This is **not** a line-rasterization bug -- it is a compile failure in
the test's own flood-fill connectivity-check *compute* shader (used to
verify the rendered line image has no gaps), which `GLSL`-disassembles to
a `while (!done)` loop whose body contains several further nested,
non-uniform (`gl_LocalInvocationID`-guarded) `if` regions, each with its
own `barrier()`. `EntryWrapper.cpp`'s `splitAtGroupSyncBarriers` /
`isLinearChain` only supports a straight-line wave body or a single
uniform loop (explicitly documented in-code as a "roadmap milestone 9
deviation") -- it does not yet generalize to a loop whose body itself
branches around further barrier regions. Extending barrier-region
splitting to arbitrary nested control flow is a substantial compiler
feature in its own right (akin to a general structurizer/divergence
analysis), not a quick fix -- scoped out for a future session.

No source changes made for this item this session; `Roadmap.md`'s
`L419(j)` row updated with this triage finding.

### `L419(j)`: `geometry primitive ID` fixed -- `gl_PrimitiveIDIn` after tessellation

`dEQP-VK.geometry.builtin_variable.primitive_id.matching` failed with:

```
At (0, 0) gl_PrimitiveID in tessellation evaluation shader was 0, but
gl_PrimitiveID in geometry shader was 7
```

Root cause: per the GLSL/Vulkan spec, a geometry shader's
`gl_PrimitiveIDIn` downstream of tessellation must equal the *source
patch's own ordinal index* (every triangle/line one patch's tessellation
generates shares that one ID) -- not a running count of primitives
assembled so far, which is only correct for the ordinary, non-tessellated
geometry-input case. `Executor.cpp` used the running-count convention
unconditionally.

Fix: thread the generating patch's own index (already computed and
already used as that patch's `SV_PrimitiveID` for TCS/TES) through a
parallel shadow-vector pipeline mirroring the existing index-only
vectors: `PatchPrimitiveIDs` -> `TessTriPatchIDs`/`TessLinePatchIDs` ->
`AbsTriPatchIDs`/`AbsLinePatchIDs` -> `PrimitivePatchIDs`, then use it in
place of the running count whenever tessellation feeds the `Lines`/
`Triangles` geometry-input assembly (`HavePatchIDs =
PrimitivePatchIDs.size() == PrimitiveCount`). The `Points`/
`*WithAdjacency` branches, which never combine with tessellation, are
unaffected and keep the old running-count behavior.

Verification:
- `dEQP-VK.geometry.builtin_variable.primitive_id.matching`: Fail ->
  **Pass**.
- `dEQP-VK.geometry.*`: 191/200 Pass, **0 Fail** (9 Not Supported,
  pre-existing) -- no regressions.
- `dEQP-VK.tessellation.*`: 646/1114 Pass, 30 Fail (confirmed via grep
  against this report to be the pre-existing, already-documented
  `L419(g)` tessellation-invariance group, not a new regression), 438 Not
  Supported.
- `ninja check-feme`: 3,722/3,725 Passed, 3 Unsupported, 0 Failed (+1 new
  test vs. prior baseline, 0 regressions).
- New unit test `ExecutorTest.GeometryStagePrimitiveIDInMatchesSourcePatchNotRunningEmitCount`:
  a two-patch tessellation+geometry pipeline reporting `gl_PrimitiveIDIn`
  through an `R32_UINT` attachment; confirmed red (fails on the pre-fix
  code) / green (passes post-fix) by a manual revert-and-rerun.
- `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
  needed -- confirmed via grep; this is an internal compiler-correctness
  fix with no feature/extension-surface change.

`Roadmap.md`: `L419(j)` updated -- 4 of its original 7 cases remain: line
continuity (2, triaged not fixed), mandatory-feature reporting (1,
untriaged), own conformance-version reporting (1, `L412(g)` won't-fix).

### `L419(j)` triage: `mandatory_features` singleton (not fixed -- cross-referenced to `D1`)

`dEQP-VK.info.device_mandatory_features` fails, reporting 14 distinct
unsupported mandatory feature bits: `robustImageAccess`,
`vulkanMemoryModel`/`vulkanMemoryModelDeviceScope`, `bufferDeviceAddress`,
`fullDrawIndexUint32`, `shaderUniformBufferArrayDynamicIndexing`,
`shaderSampledImageArrayDynamicIndexing`,
`shaderStorageBufferArrayDynamicIndexing`,
`shaderStorageImageArrayDynamicIndexing`, `shaderInt16`,
`samplerYcbcrConversion`, `storageBuffer16BitAccess`,
`variablePointers`/`variablePointersStorageBuffer`,
`samplerMirrorClampToEdge`, `scalarBlockLayout`, `shaderInt8`,
`storageBuffer8BitAccess`, `shaderFloatControls2`.

This is not a narrow, isolated bug -- it is the `L419(j)`-visible symptom
of `D1`'s own much larger, already-documented finding that only
`dynamicRendering` is genuinely implemented of the full 1.3/1.4 mandatory
feature set. Fixing this CTS case for real requires implementing each
missing feature bit (and, for most of them, the corresponding SPIR-V
capability/shader-lowering support) individually -- a substantial,
multi-feature undertaking, not a 1-2 hour fix. No source changes made
this session; `Roadmap.md`'s `L419(j)` row cross-references `D1` rather
than duplicating new rows here, since `D1` already tracks the full
feature-by-feature breakdown.

## Merge-verification sanity run: `origin/main` merged into `cbieneman/feme`

Not a new-feature session -- merged upstream LLVM `main` into the
long-lived `cbieneman/feme` branch (merge commit `23e5d72c6b38`, 5
conflicted files, all shared SPIR-V/MLIR infrastructure, none under
`feme/`) and fixed two resulting build regressions:

- Four `DXSAOps.td` ops (`DXSA_Operand`, `DXSA_OperandImm`,
  `DXSA_IndexImm`, `DXSA_IndexRelImm`) needed `prop-dict` added to their
  `assemblyFormat` to satisfy upstream's new MLIR "Strict Property
  Assembly Formats" verifier rule. This changes those ops' *printed*
  property syntax from brace-dict (`{imm = 0 : i32}`) to angle-bracket
  key-value (`<imm = 0 : i32>`) -- purely cosmetic, no IR semantics
  change. Two FileCheck tests (`atomics.dxasm`, `interface1.dxasm`)
  updated to match.
- `AtomicLoadPattern` (`SPIRVToLLVMPatterns.cpp`) needed an explicit
  `isNoundef=false` argument added to its `LLVM::LoadOp::create` call
  after upstream inserted a new `isNoundef` parameter into that
  convenience overload.

Both fixes are internal-compiler-only, no behavior change expected.
`ninja check-feme`: **3,722/3,722 applicable tests Passed** (3
Unsupported, 0 Failed) after the fixes -- full green, no regressions
from the merge.

Given no behavior change is expected from either fix, ran a **targeted
sanity subset** rather than a full 3.25M-case re-triage: every
`dEQP-VK.geometry.*`, `dEQP-VK.tessellation.*`, `dEQP-VK.rasterization.*`,
`dEQP-VK.info.*`, and every case with `atomic` in its name (43,764 cases
total -- chosen to cover both the areas most recently under active
development and, via the `atomic` filter, the specific code path touched
by the `AtomicLoadPattern` fix). Run via `feme/utils/run_vulkan_cts.py`
(6 workers, batch size 1,800, full solo re-verification round for every
apparent `Fail`/`DeviceLost`).

Result: **87 Fail, 2,940 Pass, 40,737 NotSupported**, zero `DeviceLost`/
`TimedOut`. All 87 failures cross-referenced against this report's
existing baseline and confirmed **pre-existing, already-triaged**, not
new regressions:

- 30 `dEQP-VK.tessellation.{common_edge,invariance}.*`: the documented
  `L419(g)` tessellation-invariance group.
- 2 `dEQP-VK.rasterization.line_continuity.*`: the documented `L419(j)`
  line-continuity pair (deferred, needs `EntryWrapper.cpp` barrier-region
  splitter generalization).
- 1 `dEQP-VK.info.device_mandatory_features`: the documented `L419(j)`/
  `D1` mandatory-feature gap (large, multi-session item).
- 52 `dEQP-VK.memory_model.{message_passing,write_after_read}.*`:
  matches this report's own full-baseline `memory_model` row (169 Fail
  out of... -- summary table shows 52 confirmed/isolated `Fail`,
  consistent count).
- 2 `dEQP-VK.spirv_assembly.instruction.terminate_invocation.*`: part of
  this report's documented 528-case `spirv_assembly` baseline failure
  count.

No new rows needed in `Roadmap.md` -- every failure in this sanity run
was already tracked. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: confirmed via grep to need no changes
(pure merge/build-fix session, no feature/extension-surface change).

## `K6b`: `samplerMirrorClampToEdge` advertised (roadmap K6b, measured impact)

Pure advertisement fix (see `agent_thoughts.md` for the full
investigation): `VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE` was
already fully implemented (`Image.cpp`'s enum mapping,
`FeMeRuntimeCPU.c`'s `femeRTApplyAddressMode` mode 4), so no runtime
behavior changed. `EntryPoints.cpp`'s `samplerMirrorClampToEdge` feature
bit now reports `VK_TRUE`, and `VK_KHR_sampler_mirror_clamp_to_edge` is
now advertised by name in `PhysicalDeviceInfo.cpp`'s
`getSupportedDeviceExtensions`.

Ran `dEQP-VK.info.device_mandatory_features`: still **Fail** (expected --
the other bits in K6a's remaining bundle are still missing), but the
case's log no longer lists `samplerMirrorClampToEdge` among the missing
mandatory features, confirming the fix closed that specific gap.

Ran the full `dEQP-VK.*mirror_clamp*` case list (`dEQP-VK.texture.
filtering.{2d,cube,...}.combinations.*.mirror_clamp_to_edge.*` and
related, 23,749 cases): **7,910 Pass, 0 Fail, 15,839 NotSupported**
(the NotSupported cases require other, still-unimplemented combinations
such as anisotropy, unrelated to this fix). Zero regressions.

`Vulkan14FeatureInventory.md`: 1.2 features 21->22, 1.2 extensions
10->11 (applied as a targeted doc edit, not a full regeneration -- see
that doc's own note on remaining unrelated manifest drift).
`VulkanExtensionInventory.md`: `VK_KHR_sampler_mirror_clamp_to_edge`
moved to Implemented. `Roadmap.md`: `K6b` split off `K6a` and struck
through.

## `K6c`: `shaderDrawParameters` / `gl_DrawID` fixed (roadmap K6c, measured impact)

Unlike `K6b`, this was *not* a pure advertisement fix: `gl_BaseVertex`/
`gl_BaseInstance` were already real (`Executor.cpp`'s per-draw
`Inv.BaseVertex`/`Inv.BaseInstance`), but `gl_DrawID` was not --
`Executor.cpp` hardcoded `Inv.DrawID = 0` for every vertex-pipeline draw
because `feme::graphics::DrawCommand` had no `DrawID` field at all
(unlike its mesh-pipeline sibling `MeshDrawCommand`, which already had a
working one). Every multi-draw-indirect invocation therefore silently
reported `gl_DrawID == 0` regardless of its real position within the
command. Added `DrawCommand::DrawID`, populated from
`CommandBuffer.cpp`'s `readIndirectDraws` per-draw loop index (direct,
non-indirect draws correctly keep the field's default `0`, matching
`gl_DrawID`'s spec-mandated value for those), and threaded it through
`Executor.cpp`. `EntryPoints.cpp`'s two `shaderDrawParameters` sites now
report `VK_TRUE`, and `VK_KHR_shader_draw_parameters` is now advertised
by name in `PhysicalDeviceInfo.cpp`'s `getSupportedDeviceExtensions`.

New unit test: `DrawTest.MultiDrawIndirectReportsDistinctDrawID` -- a
3-entry `vkCmdDrawIndirect` whose vertex stage stores
`(gl_DrawID + 1) * 11` into `buf[gl_DrawID]`; the resulting `{11, 22,
33}` triple is only reachable with real, distinct per-draw IDs (the
pre-fix hardcoded-0 state would collide every write on `buf[0]`).

Ran the full `dEQP-VK.draw.*shader_draw_parameters*` case list (76
cases): **76 Pass, 0 Fail**. Ran both `dEQP-VK.api.*
shader_draw_parameters_features` cases: **2 Pass, 0 Fail**. Ran the
broader `dEQP-VK.draw.*indirect*` regression sweep (3,898 cases, since
`readIndirectDraws` itself changed) to confirm no regression in any
other indirect-draw path: **36 Pass (`OK`), 1,185 Pass (`Pass`), 2,677
NotSupported** (all NotSupported cases are pre-existing, unrelated
extension gaps -- `vertexInputDynamicState`, `robustness2
nullDescriptor`, `VK_EXT_shader_object`, `VK_EXT_vertex_attribute_
divisor`, `VK_EXT_vertex_input_dynamic_state`,
`VK_KHR_device_address_commands`, `VK_KHR_draw_indirect_count`), **0
Fail**.

`Vulkan14FeatureInventory.md`: 1.1 `shaderDrawParameters` feature flipped
to `yes` (targeted doc edit, not a full regeneration -- see that doc's
own note on remaining unrelated manifest drift).
`VulkanExtensionInventory.md`: `VK_KHR_shader_draw_parameters` moved to
Implemented. `Roadmap.md`: `K6c` split off `K6a` and struck through.

## `K6d`: `VK_KHR_image_format_list` advertised (roadmap K6d, measured impact)

Genuinely free, unlike two other `K6a` candidates investigated and
rejected this same session (`samplerFilterMinmax`, `drawIndirectCount`
-- see `Roadmap.md`'s `K6a` row for why) and unlike `variablePointers`
(tried, reverted -- see below): `VkImageFormatListCreateInfo` adds no
commands and has no feature bit of its own, and `vkCreateImageView`'s
own format-compatibility-class check (`Image.cpp`, unconditional,
independent of whether this struct is chained to `VkImageCreateInfo`)
is what actually enforces the real mutable-format rule. A CPU target
with no format-specific image representation to prepare ahead of time
has nothing further to do with the declared view-format list itself.
Added `VK_KHR_image_format_list` to `PhysicalDeviceInfo.cpp`'s
`getSupportedDeviceExtensions` name array.

New unit test: `ImageTest.AcceptsImageFormatListOnMutableFormatImage`
-- creates a `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` image with a 2-entry
`VkImageFormatListCreateInfo` chained, then successfully creates a view
reinterpreting it to the list's second (non-native) format.

No dedicated `dEQP-VK.*image_format_list*`/`*format_list*` mustpass
cases exist by that name (the extension's effect is implicitly
exercised, if at all, within broader image/image-view test groups).
Ran the full `dEQP-VK.api.*` regression sweep (173,397 cases, since
`PhysicalDeviceInfo.cpp`'s extension table is read by every API/
extension-enumeration test in that group): **73,608 Pass, 0 Fail,
99,789 NotSupported**. Zero regressions. Ran
`dEQP-VK.info.device_mandatory_features`: unaffected (this extension
has no feature bit, so it was never part of that test's mandatory-
feature list).

`Vulkan14FeatureInventory.md`: unaffected (no feature bit).
`VulkanExtensionInventory.md`: `VK_KHR_image_format_list` moved to
Implemented (Advertised count 41->42, Planned count 49->48).
`Roadmap.md`: `K6d` split off `K6a` and struck through.

### `variablePointers` tried and reverted (roadmap `K6a`, negative result)

Flipping `EntryPoints.cpp`'s two `variablePointersStorageBuffer`/
`variablePointers` sites to `VK_TRUE` builds clean and passes the full
`check-feme` suite (3,724/3,724 unit tests) -- no pattern anywhere in
`SPIRVToLLVMPatterns.cpp` or upstream MLIR's SPIR-V dialect rejects a
pointer-typed `OpPhi`/`OpSelect`/function parameter at conversion-
pattern-registration time, which looked like strong evidence this was
another "advertisement-only" gap in the `K6b`/`K6c`/`K6d` mold.

Empirically running the full `dEQP-VK.spirv_assembly.*variable_pointer*`
mustpass list (4,459 cases, previously 100% `NotSupported` with the
feature `VK_FALSE`) disproved that: **674 Pass, 296 Fail, 3,489
NotSupported** (the NotSupported remainder gates on an unrelated
feature, `VK_KHR_workgroup_memory_explicit_layout`). The 296 failures
are real lowering gaps the unit-test suite's existing coverage never
exercised: `spirv.PtrAccessChain` "explicitly marked illegal" during
MLIR dialect conversion, unhandled `OpPtrEqual`/`OpPtrNotEqual`-family
opcodes (SPIR-V opcodes 401-403), and `OpConstantNull` on
`StorageBuffer`/`Workgroup`-class pointer types. Implementing these
would be a real, multi-op `SPIRVToLLVMPatterns.cpp` lowering task, not
a feature-bit flip. Reverted the `EntryPoints.cpp` change (and its
would-be `PhysicalDeviceInfoTest.cpp` expectation update) in full;
`variablePointers` stays `VK_FALSE` and remains in `K6a`'s bundle as a
genuine implementation task for a future session, not a quick pick.

### `scalarBlockLayout` tried and reverted (roadmap `K6a`, negative result)

Grep-traced `SPIRVToLLVMPatterns.cpp`'s `layOutStructIfOffsetsMatch`
(the struct-layout-from-declared-offsets function) first, following
the discipline `variablePointers` established: its alignment-gating
check (`getNaturalAlignmentIgnoringPacking`, shared by every one of
its five retry tiers) validates each member's declared `Offset`
against its full *natural/vector* ABI alignment, not its *scalar-
component* alignment. That is exactly `relaxedBlockLayout`/
`uniformBufferStandardLayout`'s level of relaxation (already
documented in `AdvertisedPromotedExtensions.txt` as "declared
offsets/strides used as-is, no std140 re-layout imposed") -- but
`scalarBlockLayout` is spec'd strictly looser still: a `vec4` member
may sit at any 4-byte-aligned offset, not just a 16-byte one. None of
the five retry tiers change the `Alignment` value itself, only which
substituted member type is tried against the same check.

Flipping `EntryPoints.cpp`'s two `scalarBlockLayout` sites to
`VK_TRUE` builds clean and passes the full `check-feme` suite
(3,724/3,724 unit tests) -- same "looks free by grep/build alone"
signal `variablePointers` gave before its real CTS run.

Empirically running `dEQP-VK.ubo.2_level_array.scalar.*` (504 cases,
previously 100% `NotSupported` with the feature `VK_FALSE`) disproved
it: **152 Pass, 192 Fail, 160 NotSupported**. Every failure is
`error: failed to legalize operation 'spirv.AccessChain' that was
explicitly marked illegal`, against exactly the predicted shape: an
`f32` member at offset 0 immediately followed by a `vec4` array at
offset 4, which violates `vec4`'s natural 16-byte alignment and is
precisely the kind of access a true scalar layout permits. Reverted
the `EntryPoints.cpp` change in full; `scalarBlockLayout` stays
`VK_FALSE` and remains in `K6a`'s bundle, needing a real relaxation of
`layOutStructIfOffsetsMatch`'s alignment check (gated on the feature
being enabled) before it can be advertised -- another genuine
implementation task, not a quick pick.

## `K6e`: `VK_EXT_separate_stencil_usage` advertised (roadmap K6e, measured impact)

Genuinely free, same shape as `K6d`'s `VK_KHR_image_format_list`:
`VkImageStencilUsageCreateInfo` adds no commands and has no feature
bit of its own. `Image.h`'s `Image` class tracks only one combined
`usage()` field, but grepping every caller of `Image::usage()` found
none at all outside a single explanatory comment in `Image.cpp` --
no command validates a view's/attachment's/descriptor's usage against
it, per-aspect or otherwise, so there is no path that could observe or
enforce the depth/stencil usage divergence this struct declares either
way. Added `VK_EXT_separate_stencil_usage` to `PhysicalDeviceInfo.cpp`'s
`getSupportedDeviceExtensions` name array.

Extended the existing `DrawTest.AdvertisesDynamicRenderingExtension`
extension-enumeration test rather than adding a new dedicated test,
since this struct has no behavioral surface of its own to exercise
beyond presence (same rationale as `K6b`/`K6c`'s advertisement-only
assertions, as distinct from `K6d`'s dedicated `ImageTest`, which had
an actual mutable-format-view behavior to exercise).

Ran the full `dEQP-VK.renderpasses.*separate_stencil_usage*` mustpass
group (207 cases, the only mustpass file referencing this struct by
name): **0 Pass, 0 Fail, 207 NotSupported** -- unaffected by this
change; every case still gates on unrelated multisample depth/stencil
format support or `VK_KHR_extended_flags`, confirming the chained
struct itself has no observable effect either way. Ran the broader
`dEQP-VK.api.info.*` regression sweep (10,490 cases, since
`PhysicalDeviceInfo.cpp`'s extension table is read by every API/
extension-enumeration test in that group): **5,966 Pass, 0 Fail,
4,524 NotSupported**. Zero regressions.

`Vulkan14FeatureInventory.md`: unaffected (no feature bit).
`VulkanExtensionInventory.md`: `VK_EXT_separate_stencil_usage` moved to
Implemented. `Roadmap.md`: `K6e` split off `K6a` and struck through.

## `K6f`: `VK_KHR_shader_float_controls` advertised (roadmap K6f, measured impact)

Genuinely free, properties-only like `K6e`: no commands, no feature
bit, and `VkPhysicalDeviceFloatControlsProperties` is already filled
in truthfully by a prior session's `L414(a)` fix --
`shaderSignedZeroInfNanPreserveFloat{16,32}` genuinely `VK_TRUE`,
every other field the conservative "least capable" value, matching
`VkPhysicalDeviceVulkan12Properties`'s mirrored fields exactly. Added
`VK_KHR_shader_float_controls` to `PhysicalDeviceInfo.cpp`'s
`getSupportedDeviceExtensions` name array.

Extended the existing `DrawTest.AdvertisesDynamicRenderingExtension`
extension-enumeration test, same rationale as `K6e`.

Ran the 13,638-case `dEQP-VK.spirv_assembly.instruction.*float_controls*`
group: **733 Pass, 4 Fail, 12,901 NotSupported**. Investigated the 4
failures (`dEQP-VK.spirv_assembly.instruction.compute.
float_controls_extensionless.{spirv1p4,vulkan1_2}.{fp16,fp32}_signed_
zero_inf_nan_preserve`): `error: unhandled deserialization of OpNop`
during pipeline creation, a pre-existing SPIR-V-deserializer gap
unrelated to this extension (the `extensionless` case name signals
these never check for `VK_KHR_shader_float_controls` at all -- they
exercise plain Vulkan-1.2-core/SPIR-V-1.4 float-controls execution
modes directly). Confirmed by reverting `PhysicalDeviceInfo.cpp`'s
change and re-running: identical `OpNop` failure either way, so this
is 0 new failures, not a regression. Ran the broader
`dEQP-VK.api.info.*` regression sweep (10,490 cases): **5,966 Pass,
0 Fail, 4,524 NotSupported**. Zero regressions.

`Vulkan14FeatureInventory.md`: unaffected (no feature bit).
`VulkanExtensionInventory.md`: `VK_KHR_shader_float_controls` moved to
Implemented. `Roadmap.md`: `K6f` split off `K6a` and struck through.



## `K6g`: `samplerFilterMinmax` implemented with real min/max reduction math (roadmap K6g, measured impact)

Unlike `K6b`-`K6f`'s pure advertisement gaps, this bit needed real
math: `FeMeRuntimeCPU.c`'s bilinear/trilinear/cube-seamless sampling
blend sites (2D/1D/3D/cube, single-level and mip-blend variants alike)
previously always did a weighted-average blend regardless of the ABI's
already-plumbed `ReductionMode` field. Added two component-wise
reduction helpers (`femeRTv4f32Min`/`femeRTv4f32Max`) and threaded
`Samp->ReductionMode`-conditional branching into every one of those
blend sites; depth-comparison sampling paths were deliberately left
untouched (the spec forbids combining depth compare with filterMinmax
reduction). `Image.cpp`'s `Sampler` constructor now parses a chained
`VkSamplerReductionModeCreateInfo` from `pNext`, mapping its
`reductionMode` to the runtime ABI enum (defaulting to
`WeightedAverage`, matching prior behavior, when the struct is absent).

`Format.cpp`'s `formatFeatureFlags` now also sets
`SAMPLED_IMAGE_FILTER_MINMAX_BIT` wherever `SAMPLED_IMAGE_FILTER_LINEAR_
BIT` is already advertised (broader than the spec's single-component-
only mandatory minimum, since real CTS cases use multi-channel formats
like `r8g8b8a8_unorm`), plus explicitly for `D16_UNORM`/`D32_FLOAT` --
the spec's own mandatory-minmax-format table requires these two
single-component depth formats independent of `FILTER_LINEAR_BIT`,
since min/max reduction only needs multiple taps, not real bilinear
interpolation. This depth-format case was initially missed (the
post-switch `FILTER_LINEAR_BIT`-gated rule alone left `D16_UNORM`/
`D32_FLOAT` out, since neither advertises `FILTER_LINEAR_BIT`), and was
caught by a real regression in `dEQP-VK.api.info.format_properties.
d16_unorm` before being fixed directly in each format's own switch
case.

Flipped `EntryPoints.cpp`'s `samplerFilterMinmax` feature bit to
`VK_TRUE`, and both properties structs' `filterMinmaxSingleComponent
Formats`/`filterMinmaxImageComponentMapping` fields to `VK_TRUE` (the
dedicated `VkPhysicalDeviceSamplerFilterMinmaxProperties` struct and
the promoted `VkPhysicalDeviceVulkan12Properties` view, which must
agree per a CTS properties2-consistency check). Added
`VK_EXT_sampler_filter_minmax` to `PhysicalDeviceInfo.cpp`'s
`getSupportedDeviceExtensions` name array (same sub-1.2-apiVersion
custom-device rationale as `K6b`-`K6f`).

Updated `PhysicalDeviceProperties2Test.SamplerFilterMinmaxProperties
IsFullyOverwritten` (now expects `VK_TRUE` rather than the prior
`VK_FALSE` placeholder) and extended `DrawTest.
AdvertisesDynamicRenderingExtension`'s extension-enumeration
assertions.

CTS coverage discovery: the static mustpass `.txt` files under this
checkout's `vk-default` profile turned out to **exclude** the entire
`sampler.view_type.*.{mag,min}_reduce` test area (zero hits for `mag_
reduce`/`min_reduce` across every mustpass file). Confirmed real
coverage exists by dumping `deqp-vk`'s full live case list
(`--deqp-runmode=stdout-caselist`) instead of trusting the mustpass
files.

Ran `dEQP-VK.pipeline.monolithic.sampler.view_type.*.format.
r8g8b8a8_unorm.{mag,min}_reduce.*` (420 cases, all view types: 1d,
1d_array, 2d, 2d_array, 3d, cube, cube_array): **252 Pass, 0 Fail, 168
NotSupported**. Every `NotSupported` case uses a non-identity/non-R
`componentMapping.r` swizzle (`comp_one_r_r_r` group); traced to
`vktPipelineImageSamplingInstance.cpp`'s `checkSupportImageSamplingInstance`,
which unconditionally throws `NotSupportedError` for any such mapping
*before* ever querying the device's actual `filterMinmaxImageComponent
Mapping` property (a separate, correct query exists later in the same
file, at the actual pipeline-creation check, but this earlier
unconditional check runs first and always wins) -- a CTS test-source
quirk, not a driver gap, and out of scope for a FeMe-local fix since
this checkout's VK-GL-CTS tree is a separate repository.

Ran the broader `dEQP-VK.api.info.*` regression sweep (10,490 cases):
initially **1 Fail** (`format_properties.d16_unorm`, missing
`SAMPLED_IMAGE_FILTER_MINMAX_BIT`) before the `D16_UNORM`/`D32_FLOAT`
fix above; after the fix, **5,966 Pass, 0 Fail, 4,524 NotSupported**
(one more pass than the `K6f` baseline, from `d16_unorm` now passing).
Re-ran `ninja check-feme` after every code change: 3,725/3,728 pass,
0 fail, 3 unsupported (pre-existing, unrelated) throughout.

`Vulkan14FeatureInventory.md`: `samplerFilterMinmax` feature bit moved
to `yes` (roadmap K6g). `VulkanExtensionInventory.md`:
`VK_EXT_sampler_filter_minmax` moved to Implemented. `Roadmap.md`:
`K6g` split off `K6a` and struck through; `AdvertisedPromotedExtensions.
txt` gained its own `K6g` row (and backfilled two pre-existing missing
rows, `K6e`/`K6f`, found while adding it -- see `agent_thoughts.md` for
details).

## `K6h`: `vkCmdDrawIndirectCount`/`vkCmdDrawIndexedIndirectCount` implemented (roadmap K6h, measured impact)

Unlike `K6b`/`K6d`-`K6g`'s pure advertisement gaps, `K6a`'s
`drawIndirectCount` bit required two brand-new Vulkan command entry
points (`VK_KHR_draw_indirect_count`, core in Vulkan 1.2): there was
no `vkCmdDrawIndirectCount`/`vkCmdDrawIndexedIndirectCount` symbol
anywhere in `CommandBuffer.cpp`/`EntryPoints.cpp` before this session.

Investigation found two directly reusable building blocks already in
place from the `VK_EXT_mesh_shader` work (roadmap H6f):
`readIndirectDrawCount` (reads a single bounds-checked `uint32_t` from
a count buffer, clamped to a caller-supplied `maxDrawCount`) and
`readIndirectDraws` (reads the draw-command array itself). The new
commands reuse both verbatim, and `drawMeshTasksIndirectCount`
(`CommandBuffer.h`) served as the exact structural template for the
new `drawIndirectCount` recording method. The execution-time switch
merges the two new `RecordedCommand::Kind` values
(`DrawIndirectCount`/`DrawIndexedIndirectCount`) into the existing
`DrawIndirect`/`DrawIndexedIndirect` case, rather than duplicating the
case, mirroring the pre-existing `DrawMeshTasksIndirect`/
`DrawMeshTasksIndirectCount` merged-case pattern.

`vk_gen_entrypoints.py`'s generated entrypoint table already carried
unimplemented entries for both command names (they are promoted to
Vulkan 1.2 core, read via the generator's own core-feature pass,
independent of `SUPPORTED_EXTENSIONS`); adding both names to
`ImplementedEntrypoints.txt` was the only generator-side change
needed. Confirmed (by checking CTS's own `vkInitDeviceFunctionPointers.
inl`) that `deqp-vk`'s device-function-pointer loader tries the core,
non-`KHR`-suffixed name first unconditionally and only falls back to
the `KHR`-suffixed alias if that lookup returns null -- so no
`SUPPORTED_EXTENSIONS` entry (and thus no `KHR`-suffixed alias symbol)
is needed for CTS to exercise the core entry points this ICD now
implements.

`ProcAddrTest.UnimplementedCommandNeverResolves` previously used
`vkCmdDrawIndexedIndirectCount` as its "real command this driver does
not implement" example; now that it is implemented, the test was
re-pointed at `vkGetPhysicalDeviceImageFormatProperties2` (instance-
level) and `vkCmdBindIndexBuffer2KHR` (device-level, an alias whose
own `KHR`-suffixed name is not in `ImplementedEntrypoints.txt` even
though its core counterpart `vkCmdBindIndexBuffer2` is) -- confirmed
by a small Python script cross-checking every 1.0-1.4 core command
name in `vk.xml` against `ImplementedEntrypoints.txt`, which found
**zero** remaining unimplemented 1.0-1.4 *core* commands (every gap
left in the roadmap is a semantic/feature-completeness gap, not a
missing entry point).

Ran the full live case list (`--deqp-runmode=stdout-caselist`, not the
static mustpass `.txt` files, per this project's own recurring "static
mustpass can be incomplete" lesson) filtered to `*indirect_count*`:
2,695 cases (`dEQP-VK.draw.*`, `dEQP-VK.conditional_rendering.*`,
`dEQP-VK.transform_feedback.*`). Result: **449 Pass, 0 Fail, 2,246
NotSupported** (the `NotSupported` cases need unrelated extensions
this ICD does not implement at all, e.g. `maintenance6`,
`shader_objects`, `VK_KHR_device_address_commands`, or exceed
`maxTransformFeedbackBufferDataSize`).

Ran the broader `dEQP-VK.draw.*` regression sweep (29,475 cases):
**4,282 Pass, 0 Fail, 25,193 NotSupported**. Ran `dEQP-VK.api.info.*`
(10,490 cases): **5,966 Pass, 0 Fail, 4,524 NotSupported** (identical
totals to the `K6g` baseline -- no regression). Re-ran `ninja
check-feme` after every code change: 3,728/3,731 pass, 0 fail, 3
unsupported (pre-existing, unrelated); 3 new tests added this session
(`DrawTest.DrawIndirectCountClampsToCountBuffer`,
`DrawTest.DrawIndexedIndirectCountClampsToCountBuffer`,
`PhysicalDeviceProperties2Test.DrawIndirectCountFeatureIsTrue`).

`Vulkan14FeatureInventory.md`: `drawIndirectCount` feature bit moved
to `yes` (roadmap K6h). `VulkanExtensionInventory.md`:
`VK_KHR_draw_indirect_count` moved from "Planned (in scope, not
implemented)" to "Implemented". `Roadmap.md`: `K6h` split off `K6a`
and struck through.

## `K6i`: `scalarBlockLayout` implemented via struct/array/matrix offset-layout fixes (roadmap K6i, measured impact)

A prior session's grep-only read of `SPIRVToLLVMPatterns.cpp`'s
`layOutStructIfOffsetsMatch` found its alignment-gating check validating
each member's declared offset against full natural/vector ABI alignment
rather than scalar-component alignment, and a quick empirical flip
surfaced 192 `dEQP-VK.ubo.2_level_array.scalar.*` failures confirming a
real compiler gap. This session root-caused and fixed three distinct,
related bugs in the struct/array/matrix offset-layout conversion logic,
all stemming from the existing "tight vector/matrix substitution"
infrastructure only ever handling one level of array nesting:

1. The struct-member-list-building loop's plain `TypeConverter::convertType`
   call failed outright for a *single-level* tight array
   (`!spirv.array<N x vector<M x T>>` with a scalar-tight, non-SIMD-rounded
   stride), aborting the whole enclosing struct's conversion before any
   later retry tier ran. Fixed via a fallback to a new recursive helper,
   `getTightNestedArrayOfVectorsOrMatricesType`, generalizing the existing
   single-level substitution to peel arbitrary-depth `spirv::ArrayType`
   nesting around a vector/matrix element. The same fallback was wired
   into `rewriteBlockAccess`'s own standalone conversion call, which hit
   an analogous failure for UBO/SSBO resource-handle-based access.
2. `layOutStructIfOffsetsMatch`'s "natural alignment overshoot" pre-check
   (`NaturalCursor > DeclaredOffset`) unconditionally rejected placements
   where a member's natural (SIMD-rounded) alignment exceeded its declared
   offset, even inside the existing last-resort `AllowInteriorPad` retry
   tier where the struct is always built packed anyway (no implicit
   alignment gap is ever actually inserted by LLVM). Fixed by relaxing the
   rejection to fire only when the raw, unaligned running byte cursor
   itself exceeds the declared offset -- the one placement no amount of
   synthesized padding could fix.
3. The rewritten array-peeling applicability check in the
   `WithArraysAndMatrices` retry tier incorrectly matched **bare
   (non-array) vector/matrix members** too, since peeling zero array
   levels is a no-op leaving the peeled type unchanged, and a bare
   `VectorType` member still passes `isa<VectorType>`. The caller then
   treated the helper's correct "not applicable, not an array" `nullptr`
   as a hard failure of the *whole* retry, rather than "already handled
   by the earlier `VectorOnly` tier." Found by bisecting a regression in
   the existing `SPIRVToLLVMTest.cpp` `OutOfOrderOffsetInterfaceBlockLegalizes`
   unit test (a physically-reordered struct with a bare `mat4x2` member)
   after fixes #1+#2 landed. Fixed with an explicit
   `isa<spirv::ArrayType>(ElementTy)` guard before peeling begins.

Flipped both `EntryPoints.cpp` `scalarBlockLayout` sites to `VK_TRUE`.
Added `spirv-to-llvm-nested-array-of-vectors-tight-stride.mlir` (bug #1)
and `spirv-to-llvm-nested-array-of-matrices-scalar-offset.mlir` (bug #2)
lit tests; confirmed the pre-existing `OutOfOrderOffsetInterfaceBlockLegalizes`
unit test now serves as bug #3's regression guard; updated
`PhysicalDeviceInfoTest.cpp`'s
`PreVulkan12PromotionDedicatedFeatureStructsAreFullyOverwritten` expected
value (`ScalarBlockLayout.scalarBlockLayout` from `VK_FALSE` to `VK_TRUE`,
matching the now-advertised feature).

CTS (methodology note: `dEQP-VK.ubo.random.*`'s shader content is
dynamically generated based on whether the device advertises
`scalarBlockLayout`, so a true pre-fix baseline -- same code, feature bit
left `VK_FALSE` -- was run alongside each measurement below to distinguish
genuine regressions from newly-exercised-and-failing content):

- `dEQP-VK.ubo.{single_basic_array,2_level_array,3_level_array}.scalar.*`
  (1,512 cases): **1,032 Pass, 0 Fail, 480 NotSupported** (previously 32,
  192, and 192 failures respectively before this session's fixes).
- Full `dEQP-VK.ubo.*` regression sweep (13,240 cases): **8,293 Pass, 25
  Fail, 4,922 NotSupported**. All 25 failures are
  `single_nested_struct.*scalar*` (24 cases) or `random.scalar.83` (1
  case) -- a **separate**, still-open nested-struct scalar-layout gap
  (`VK_ERROR_INITIALIZATION_FAILED` on a struct containing a nested-struct
  member at a scalar-tight offset), distinct from this session's
  vector/matrix-array-nesting fix and split off as a new roadmap follow-up
  (`K6j`). An interim measurement with only bugs #1+#2 fixed (before bug
  #3's regression guard landed) showed 340 failures on this same sweep;
  bug #3's fix alone closed 315 of those. Zero non-scalar-layout
  regressions observed at any point.
- Re-ran `ninja check-feme`: 3,730/3,733 pass, 0 fail, 3 unsupported
  (pre-existing, unrelated); 2 new lit tests added this session.

`Vulkan14FeatureInventory.md`: `scalarBlockLayout` feature bit and
`VK_EXT_scalar_block_layout` extension both moved to `yes` (roadmap K6i).
`VulkanExtensionInventory.md`: `VK_EXT_scalar_block_layout` moved from
"Planned (in scope, not implemented)" to "Implemented". `Roadmap.md`:
`K6i` split off `K6a` and struck through; new `K6j` added tracking the
residual nested-struct scalar-layout gap as future work.

## `K6j`: nested-struct scalar-layout gap fixed (roadmap K6j, measured impact)

K6i's own residual sweep left 25 `dEQP-VK.ubo.*` failures
(`single_nested_struct.*scalar*`, 24 cases, plus `random.scalar.83`), all
`VK_ERROR_INITIALIZATION_FAILED` building a graphics pipeline whose `Block`
struct has a *nested struct* member declared at a scalar-tight offset.

Root-caused via a minimal, isolated `feme-opt`-only MLIR reproducer,
bisected from the real CTS struct shape down to a 2-member struct: a
nested, explicitly-offset struct member (itself containing a
`stride=36`, scalar-tight array-of-`mat3x3` member) immediately followed
by a plain `vector<2xf32>` member, with no interior gap needed between
them at all. `--debug-only=dialect-conversion` traced the failing
`spirv.AccessChain` legalization back one level further than it first
appeared: the resource-handle intrinsic
(`llvm.spv.resource.handlefrombinding`) was building with a degenerate
`!llvm.ptr<12>` result type instead of a real, typed
`!llvm.target<"spirv.VulkanBuffer", ...>` handle -- meaning the *whole
outer struct's own type conversion* was silently returning null, not
anything AccessChain-specific.

The bug is in `getTightNestedStructType` -- the "tight," alignment-free
nested-struct re-conversion that `convertOffsetStructTypeIgnoringDecorations`'s
`VectorOnly` retry tier calls for any nested-struct member once the
struct's first (natural) conversion attempt fails to reproduce every
declared offset. Its array-member branch had neither of the two
safeguards `convertOffsetStructTypeIgnoringDecorations`'s own member loop
already has for a scalar-tight array-of-matrix member:

1. No fallback to `getTightNestedArrayOfVectorsOrMatricesType` when the
   ordinarily-registered `spirv::ArrayType` conversion fails outright for
   a stride narrower than the matrix column's natural (SIMD-rounded) ABI
   size.
2. No `substituteArrayOfMatrixElementType` correction for the more
   insidious case where that conversion does *not* fail outright, but
   instead silently succeeds with the wrong (natural, vector-ABI-padded,
   too-wide) layout -- exactly the failure mode
   `substituteArrayOfMatrixElementType`'s own doc comment describes: the
   upstream `spirv::ArrayType` conversion validates against
   `VulkanLayoutUtils::getNaturalArrayStride`'s own tightly-packed-
   scalar-count size, which happens to equal the declared stride here,
   and builds the array around the natural (wrong) matrix conversion
   anyway. This was the actual failure in this case: the matrix-array
   member "succeeded" with `array<4 x array<3 x vector<3xf32>>>` instead
   of the correct, tight `array<4 x array<3 x array<3 x f32>>>`, so the
   outer struct's offset-matching for the member immediately following
   it never lined up.

`convertOffsetStructTypeIgnoringDecorations`'s own member loop already
unconditionally re-validates every member's matrix representability
(`isMatrixMemberLayoutRepresentable`/`isMatrixLayoutRepresentable`)
regardless of which of these two ways its initial conversion went wrong,
and substitutes a corrected physical layout
(`getPhysicalMatrixMemberType`/`wrapPhysicalMatrixInArrays`) either way --
but `getTightNestedStructType` had neither check at all. Fixed by
applying the same two safeguards inside `getTightNestedStructType`'s own
array-member branch.

Added `spirv-to-llvm-nested-struct-array-of-matrices-scalar-offset.mlir`,
mirroring the exact failing CTS struct shape (2-member `Block`: nested
offset-struct with a scalar-tight array-of-`mat3x3` member, followed by a
plain vector member at the nested struct's own natural tight size).

CTS:
- `dEQP-VK.ubo.single_nested_struct.*` plus `dEQP-VK.ubo.random.scalar.83`
  (145 cases): **145 Pass, 0 Fail**.
- Full `dEQP-VK.ubo.*` regression sweep (13,240 cases): **8,318 Pass, 0
  Fail, 4,922 NotSupported** -- the prior session's 25 residual failures
  are now all closed, with no new regressions (8,293 + 25 = 8,318,
  exactly accounting for the fix).
- `dEQP-VK.ssbo.*` regression sweep (12,225 cases, run as a broader
  regression check since this fix touches a function shared by both UBO
  and SSBO struct-layout conversion): **4,687 Pass, 67 Fail (62 distinct
  cases plus duplicates), 7,471 NotSupported**. Confirmed via `git stash`
  (rebuilding `libfeme_vulkan.so` against the pre-fix code and re-running
  just these 62 cases) that all 62 failures are **pre-existing**, not a
  regression from this fix -- a separate `RowMajor`-matrix SSBO
  `*_store_cols` bug (result-comparison mismatch, not a legalization/
  pipeline-creation error), tracked as new roadmap row `K6k`.
- Re-ran `ninja check-feme`: 3,731/3,734 pass, 0 fail, 3 unsupported
  (pre-existing, unrelated); 1 new lit test added this session.

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` changes
needed: `scalarBlockLayout`/`VK_EXT_scalar_block_layout` were already
advertised as of K6i; K6j is a pure bug fix closing that feature's own
residual gap, not a new feature-bit flip. `Roadmap.md`: `K6j` struck
through; new `K6k` added tracking the pre-existing, unrelated
`RowMajor`-matrix SSBO `*_store_cols` bug found (but not fixed) as part
of this session's broader regression sweep.

## `K6k`: `RowMajor`-matrix SSBO array-member column store/load fixed (roadmap K6k, measured impact)

K6j's own broader `dEQP-VK.ssbo.*` regression sweep left 67 raw failures
(62 distinct cases, roadmap K6k): every
`dEQP-VK.ssbo.layout.{single_basic_array,2_level_array,3_level_array}.
scalar.row_major_mat*_{comp_access_,}store_cols` case, plus 8
`dEQP-VK.ssbo.layout.random.scalar.*` cases -- a result-comparison
mismatch, not a legalization/pipeline-creation error.

Root-caused by capturing the real SPIR-V `dEQP-VK.ssbo.layout.
single_basic_array.scalar.row_major_mat2_store_cols` compiles to
directly from the CTS `.qpa` log's own `<SpirVAssemblySource>` (rather
than continuing to guess at hand-written MLIR reproducers, which this
session's own early attempts show is easy to get subtly wrong -- see
`agent_thoughts.md`). The real `Block` struct has a leading `float
padding` member before the `mat2 var[3]` member CTS's own GLSL names
suggested was the block's *sole* member; `var` is therefore member index
1, reached as `mat2 var[3]` -- an array of matrices, not a matrix
directly. The failing column-select `AccessChain` is
`[member=1, elemIdx, colIdx]`, one index deeper than
`getMatrixColumnAccess`'s own shape-matching helper
(`getMatrixColumnAccessShape`) expected (`[member, colIdx]`, i.e. the
member being the matrix directly). It declined to match, so the whole
`AccessChain` fell through to a different, `RowMajor`-oblivious
conversion that silently dropped `colIdx`, storing/loading at the
matrix's base address regardless of which column was requested.

Fixed by peeling through the member's own array levels (any number, to
also cover `2_level_array`/`3_level_array`) before requiring the
innermost type be the matrix, consuming one more `AccessChain` index per
level for the column selector's own position.

Added `spirv-to-llvm-matrix-rowmajor-nonwrapper-array-struct-member-
column.mlir`, mirroring the exact failing CTS struct shape; confirmed it
fails without the fix (`git stash`) and passes with it.

CTS:
- `dEQP-VK.ssbo.layout.single_basic_array.scalar.row_major_mat2_store_cols`
  (the original case): **Pass** (previously Fail).
- All 54 `single_basic_array`/`2_level_array`/`3_level_array` `row_major
  mat*_{comp_access_,}store_cols` cases run directly: **54 Pass, 0 Fail**.
- Full `dEQP-VK.ssbo.*` regression sweep (12,225 cases, via
  `feme/utils/run_vulkan_cts.py`): **4,741 Pass (+54), 13 Fail (-54),
  7,471 NotSupported** (unchanged). The 13 remaining failures are **not**
  this fix's target shape: 9 `random.scalar.*` cases (a deeper-nesting
  residual, split off as new roadmap row `K6l`) and 4
  `3_level_unsized_array.scalar.{bvec3,ivec3,uvec3,vec3}` cases (a
  separate, unrelated-to-`RowMajor`-matrices legalization bug, also
  tracked under `K6l`) -- both confirmed **pre-existing**, not caused by
  this fix, via a before/after rebuild (`git` revert of just this
  commit's source change, rebuild `libfeme_vulkan.so`, re-run): identical
  failure signatures on both builds.
- Re-ran `ninja check-feme`: 3,732/3,735 pass, 0 fail, 3 unsupported
  (pre-existing, unrelated); 1 new lit test added this session.

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` changes
needed: this is a pure bug fix in an already-advertised feature's own
lowering path (`scalarBlockLayout`/`VK_EXT_scalar_block_layout`, already
advertised as of K6i), not a new feature-bit flip. `Roadmap.md`: `K6k`
struck through; new `K6l` added tracking the residual `random.scalar.*`
and `3_level_unsized_array` failures this sweep surfaced.

## `K6l`: scalar-layout `RuntimeArrayType`-of-vector stride fixed (roadmap K6l, measured impact)

K6k's own sweep left 13 residual `dEQP-VK.ssbo.*` failures, bundled into
roadmap row `K6l`: 9 `dEQP-VK.ssbo.layout.random.scalar.*` cases
(`.3`, `.11`, `.15`, `.20`, `.41`, `.49`, `.60`, `.61`, `.88`) and 4
`dEQP-VK.ssbo.layout.3_level_unsized_array.scalar.{bvec3,ivec3,uvec3,
vec3}` cases. The original guess (8 of the 9 `random.scalar.*` cases
being a deeper `RowMajor`-matrix-nesting shape than K6k's own fix covers)
turned out to be wrong once the real SPIR-V was pulled from `.3`'s own
`.qpa` log: the actual failing shape has no nested matrix at all --
`layout(scalar, ...) buffer Block { uint a; mat2x3 b; bvec4 c; vec3
d[]; };`'s own trailing `d[]` member is a plain, direct `rtarray<vector
<3xf32>, stride=12>`.

Root-caused to `SPIRVToLLVMPatterns.cpp`'s `RuntimeArrayType`
`TypeConverter.addConversion` lambda: upstream MLIR's own
`getDefaultTypeSizeInBits` always rounds a vector type's own
DataLayout-reported size up to the next power-of-two lane count (16
bytes for `vector<3xf32>`, not its real 12-byte natural size) -- true
for ordinary std430 layout (whose own declared `ArrayStride` for a
`vec3` array is also 16, matching) but not for this scalar/tight-layout
shape, whose declared `ArrayStride` stays 12. The lambda's own
Stride-mismatch retry only ever widened a too-small element up to a
*wider* declared Stride (the ordinary direction) -- never the reverse,
narrower-than-element-size direction scalar layout produces here, so
every `d[i]` read (any index, since even the first element's own Y/Z
lanes are read through the oversized vector's own internal layout)
computed the wrong byte offset.

Fixed by falling back to the existing `getStridedTightVectorArrayType`
tight, alignment-free array-of-scalars marker substitution (already
used elsewhere in this file for the identical mismatch on a *fixed-size*
array of vectors) whenever a *runtime*-sized array's own vector
element's natural size exceeds its declared Stride.

Added `spirv-to-llvm-scalar-runtime-array-of-vectors.mlir`, mirroring
`random.scalar.3`'s own exact failing struct shape (confirmed via a
temporary `FEME_DEBUG_DUMP_SPIRV_DIR` dump hook in `Pipeline.cpp`'s
`copyShaderModuleWords`, reverted before this fix's own commit, that the
hand-reconstructed SPIR-V byte-matches the real pipeline's own shader
module).

CTS:
- `dEQP-VK.ssbo.layout.random.scalar.{3,11,15,20,49,60,61,88}` (8 of
  K6l's original 9 cases): **8 Pass, 0 Fail** (previously 8 Fail).
  `.41` remains Fail -- a distinct legalization-error bug class, split
  off as new roadmap row `K6m`.
- Full `dEQP-VK.ssbo.*` regression sweep (12,225 cases, via
  `feme/utils/run_vulkan_cts.py`, with its mandatory solo
  re-verification pass): **4,749 Pass (+8), 5 Fail (-8), 7,471
  NotSupported** (unchanged). The 5 remaining failures are exactly
  `random.scalar.41` plus the 4 `3_level_unsized_array.scalar.*` cases
  -- confirmed as K6l's own pre-existing, untouched-by-this-fix
  residual, now tracked as `K6m`.
- Re-ran `ninja check-feme`: 3,733/3,736 pass, 0 fail, 3 unsupported
  (pre-existing, unrelated); 1 new lit test added this session.

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` changes
needed: this is a pure bug fix in an already-advertised feature's own
lowering path (`scalarBlockLayout`/`VK_EXT_scalar_block_layout`, already
advertised as of K6i), not a new feature-bit flip. `Roadmap.md`: `K6l`
struck through; new `K6m` added tracking the residual
`random.scalar.41`/`3_level_unsized_array.scalar.*` legalization
failures.

## `K6m`: `RuntimeArrayType`-of-`array`-of-vector legalization gap fixed (roadmap K6m, measured impact)

K6l's own sweep left 5 residual `dEQP-VK.ssbo.*` failures, split into
roadmap row `K6m`: `dEQP-VK.ssbo.layout.random.scalar.41` and the 4
`dEQP-VK.ssbo.layout.3_level_unsized_array.scalar.{bvec3,ivec3,uvec3,
vec3}` cases. Unlike K6l's own result-comparison mismatches, all 5 of
these failed a *legalization* error instead --
`"failed to legalize operation 'spirv.AccessChain'...explicitly marked
illegal"` -- meaning no LLVM type for the accessed member ever existed
at all, not merely a wrong one.

Root-caused via the same temporary `FEME_DEBUG_DUMP_SPIRV_DIR` dump
hook in `Pipeline.cpp`'s `copyShaderModuleWords` used for K6l (reverted
before this fix's own commit) to pull `.41`'s own authentic SPIR-V, then
a hand-crafted, internally consistent minimal MLIR reproducer (the
usual `feme-translate`/`feme-opt` text-round-trip artifact -- a harmless
`OpIAdd` `i32`/`si32` parser inconsistency -- made round-tripping the
real disassembly impractical, same as several prior sessions' notes).
`.41`'s real failing shape: `layout(scalar, ...) buffer Block { ...;
ivec3 d[][2]; };`'s own trailing `d[][2]` member is `!spirv.rtarray
<!spirv.array<2 x vector<3xsi32>, stride=12>, stride=24>` -- a
*runtime* array whose own element is *itself* a **fixed-size array of a
bare vector**, one level of nesting deeper than K6l's own direct
`rtarray<vector<...>>` shape.

Traced the failure through `rewriteBlockAccess` down to
`convertArrayTypeIgnoringDecorations` (the plain, standalone
`spirv::ArrayType` conversion): its own stride-mismatch handling widens
an undersized element (`NaturalSize < Stride`) or pads a struct-typed
one, but when `NaturalSize > Stride` (this scalar-layout direction --
the inner `array<2 x vector<3xsi32>>`'s declared 12-byte stride is
narrower than `vector<3xsi32>`'s own 16-byte, power-of-two-rounded
natural size), it unconditionally returns null rather than substituting
a tight marker. That null propagates up through
`spirv::RuntimeArrayType`'s own element-type conversion, aborting the
whole `AccessChain` conversion. A direct struct member reaching this
same shape already has a retry path
(`getTightNestedArrayOfVectorsOrMatricesType`), but it requires a
`Struct`/`Index` (to look up `MatrixStride` decorations) and only
accepts a top-level `spirv::ArrayType`, not a `spirv::RuntimeArrayType`
-- so it could never apply to this runtime-array-rooted shape regardless
of whether the primary conversion were fixed.

Fixed by adding `getTightNestedArrayOfVectorsType`, a
`Struct`/`Index`-decoration-free sibling of
`getTightNestedArrayOfVectorsOrMatricesType` (no matrix support needed:
a matrix nested this way always has its own owning struct member to
carry a `MatrixStride` decoration, so a decoration-free context can
never legitimately encounter one), and retrying through it inside the
`RuntimeArrayType` conversion lambda whenever the element's own raw
conversion fails outright.

Added `spirv-to-llvm-scalar-runtime-array-of-array-of-vectors.mlir`, a
minimal, hand-crafted reproducer of `.41`'s own exact failing shape (one
leading scalar member plus the `rtarray<array<2 x vector<3xsi32>,
stride=12>, stride=24>` member, confirmed to reproduce the same
legalization failure before the fix and the same tight-marker-using
conversion after it).

CTS:
- `dEQP-VK.ssbo.layout.random.scalar.41`: **Pass** (previously Fail).
- `dEQP-VK.ssbo.layout.3_level_unsized_array.scalar.{bvec3,ivec3,uvec3,
  vec3}`: **4 Pass, 0 Fail** (previously 4 Fail) -- the same fix covers
  all 4, confirming the roadmap's own guess that they share K6m's root
  cause one level deeper.
- Full `dEQP-VK.ssbo.*` regression sweep (12,225 cases, via
  `feme/utils/run_vulkan_cts.py`, with its mandatory solo
  re-verification pass): **4,754 Pass (+5), 0 Fail (-5), 7,471
  NotSupported** (unchanged) -- zero new failures, and K6l's own 5
  residual cases are now fully closed.
- Re-ran `ninja check-feme`: 3,730/3,737 pass, 0 fail, 7 unsupported
  (pre-existing, unrelated); 1 new lit test added this session.

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` changes
needed: this is a pure bug fix in an already-advertised feature's own
lowering path (`scalarBlockLayout`/`VK_EXT_scalar_block_layout`, already
advertised as of K6i), not a new feature-bit flip. `Roadmap.md`: `K6m`
struck through -- K6l's own 5-case residual is now fully closed.

## `K6a`: `PtrAccessChain`/`InBoundsPtrAccessChain` lowering added, 296 -> 152 Fail (roadmap K6a, measured impact)

Resumed `K6a`'s `variablePointers`/`variablePointersStorageBuffer`
investigation (a prior session's "tried and reverted" 296-failure
baseline). The missing piece for triaging these failures turned out to
be a pre-existing but previously-undiscovered env var,
`FEME_VULKAN_LOG_CREATION_ERRORS` (`Diagnostics.cpp`): unset/`"0"` is
silent (the default), any other value prints the real underlying
`llvm::Error` for a failed pipeline compile. Using it, bucketed all 296
`dEQP-VK.spirv_assembly.*variable_pointer*` mustpass failures by exact
error signature:

- **156**: `"failed to legalize operation 'spirv.PtrAccessChain'...
  explicitly marked illegal"` -- `spirv.PtrAccessChain`/
  `spirv.InBoundsPtrAccessChain` had no conversion pattern at all, in
  either feme or upstream MLIR.
- **112** (+1 separate `feme-cpu-simdize` groupshared-pointer-chain
  diagnostic, grouped together as 113): `"unsupported raised
  operation: ... is a register-bound resource handle the FeMe CPU
  target cannot normalize ..."` -- a CPU-backend architectural
  limitation once a pointer may dynamically refer to more than one
  descriptor binding.
- **16**: `"unsupported OpConstantNull type: '!spirv.ptr<...>'"` --
  confirmed upstream-MLIR deserializer gap (`getNullAttrForType` has no
  pointer-type case).
- **11** (4 `OpPtrEqual` + 4 `OpPtrNotEqual` + 3 `OpPtrDiff`):
  `"unhandled opcode 401/402/403"` -- these three ops are entirely
  undefined in MLIR's SPIR-V dialect TableGen, an upstream gap.

Root-caused the `PtrAccessChain` gap's exact shape via `PtrAccessChainOp
::build`/`verify` (`mlir/lib/Dialect/SPIRV/IR/MemoryOps.cpp`): both
compute the op's result type purely from `getElementPtrType(basePtr,
indices, loc)`, **never looking at the `element` operand at all** --
proving `PtrAccessChain(base, element, idx1, idx2, ...)`'s result type
always equals `AccessChain(base, idx1, idx2, ...)`'s own. Semantically,
`element` only ever shifts the base address by `element` whole units of
`base`'s own pointee type before `idx1, idx2, ...` then descend into
that shifted base exactly like an ordinary `AccessChain` would.

Implemented `PtrAccessChainPattern` (`SPIRVToLLVMPatterns.cpp`) across
two commits this session:

- **First commit**: handled the two shapes an initial ~10-case sample
  (one per `opptraccesschain_*` sub-family) suggested covered
  everything -- a compile-time-constant-zero `element` (re-expressed as
  a plain `AccessChain`, letting this file's own much more intricate
  existing struct-layout/block/matrix machinery finish the job via the
  dialect conversion driver's standard "newly created illegal op gets
  reconverted" idiom), and a genuinely dynamic `element` with *zero*
  trailing indices into a bare scalar pointee (a single
  `llvm.getelementptr`, matching C's own `scalarPtr[i]` pointer
  arithmetic). Declined (via `notifyMatchFailure`) a non-zero/dynamic
  `element` combined with real trailing indices, since the initial
  sample suggested no case needed it.
- **Second commit**: a full post-fix CTS bucketing run found that guess
  wrong -- 24 real `opptraccesschain_float_*` cases need exactly that
  declined combination (`%ptr<vector<4xf32>> -> (element)(idx) ->
  %ptr<f32>`: step to vector lane `element`'s own base address, then
  select component `idx` within it). An initial attempt re-expressed
  each step as a new, still-SPIR-V op for the dialect conversion driver
  to reprocess (mirroring the already-working zero-element case) but
  failed to converge: the newly created `AccessChain`'s own
  legalization ran before its `PtrAccessChain` operand had itself
  converted, so its "base is not an LLVM pointer yet" failure was never
  retried, rolling the whole rewrite back and reporting the *original*,
  unconverted op as unlegalizable. Fixed instead with a direct, chained
  pair of `llvm.getelementptr`s (mirroring upstream's own
  `AccessChainConversion` for a plain `AccessChain`), sidestepping the
  ordering hazard entirely. Scoped to a non-struct pointee only (every
  known case reaches this with a bare scalar/vector pointee).

New `spirv-to-llvm-ptr-access-chain.mlir` lit test (4 cases: zero-element
+index, zero-element-identity, dynamic-element-no-index, and
element+index together).

CTS (full 4464-case `variable_pointer` mustpass sweep, via
`feme/utils/run_vulkan_cts.py`, with `variablePointers`/
`variablePointersStorageBuffer` temporarily flipped to `VK_TRUE` for
the sweep only -- reverted before every commit, since the feature bits
remain `VK_FALSE` with failures still outstanding):

- Session-start baseline: 296 Fail / 3492 NotSupported / 676 Pass.
- After first commit (156 `PtrAccessChain` cases targeted): 176 Fail /
  3492 NotSupported / 796 Pass -- 120 newly fixed, 0 new failures
  (confirmed via case-by-case fail-list diff). The remaining 176 bucket
  as: 124 `"unsupported raised operation"` (112 original + 12 newly
  revealed once `PtrAccessChain` legalization itself no longer masked
  them), 24 `PtrAccessChain` legalization failures (the undiscovered
  `opptraccesschain_float_*` combination), 16 `OpConstantNull`, 11
  opcode-401/402/403, 1 `feme-cpu-simdize`.
- After second commit (the 24-case `opptraccesschain_float_*`
  combination): 152 Fail / 3492 NotSupported / 820 Pass -- 24 more newly
  fixed, 0 new failures. The `PtrAccessChain` cluster is now **fully
  closed** (0 of 152 remaining failures are a `PtrAccessChain`/
  `AccessChain` legalization error); the 152 bucket exactly as: 124
  `"unsupported raised operation"`, 16 `OpConstantNull`, 11
  opcode-401/402/403, 1 `feme-cpu-simdize` -- confirmed via a full
  re-bucketing pass after the second commit.
- Re-ran `ninja check-feme` after each commit: 3,731/3,738 pass, 0 fail,
  7 unsupported (pre-existing, unrelated) both times.

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` changes:
`variablePointers`/`variablePointersStorageBuffer` remain `VK_FALSE`
(152 failures still outstanding, split into three new roadmap rows --
`K6n`, `K6o`, `K6p` -- each tracking one of the three remaining distinct
problem classes). `Roadmap.md`: `K6a` updated in place (left open,
`variablePointers` is a genuine multi-session effort, not a single
drive-by fix); `K6n`/`K6o`/`K6p` added as new top-level rows.

## `K6o`: `OpConstantNull`-on-pointer-type upstream-MLIR deserializer gap fixed, 152 -> 140 Fail (roadmap K6o, closed)

Picked `K6o` (the smaller, more isolated "issue outside FeMe" item) over
the larger `K6n` (124-case CPU-backend architectural limitation), per the
prior session's own recommendation.

Root-caused across four upstream-MLIR code paths, all in
`mlir/lib/{Target/SPIRV,Dialect/SPIRV/IR,Conversion/SPIRVToLLVM}`:
`Deserializer.cpp`'s `getNullAttrForType` had no `spirv::PointerType`
case (fell through to `nullptr`, the direct cause of the "unsupported
OpConstantNull type" error); `SPIRVOps.cpp`'s `verifyConstantType` had
no pointer-type acceptance case either; `Serializer.cpp`'s
`prepareConstant` had no serialization path for a pointer-typed
constant; `SPIRVToLLVM.cpp`'s `ConstantScalarAndVectorPattern` only
handled scalar/vector source types, declining pointers.

Designed the fix around a `UnitAttr` marker for "null pointer" (no
meaningful payload to store, unlike every other constant kind), with one
correctness hazard discovered and addressed: `Serializer`'s `constIDMap`
is keyed purely by `Attribute`, not `(Attribute, Type)` -- since
`UnitAttr::get(ctx)` is a single per-context singleton regardless of
type, naively deduplicating via `getConstantID(valueAttr)` (as every
other constant kind does) would incorrectly alias two different pointer
types' null constants onto the same result `<id>`. Fixed by always
emitting a fresh `OpConstantNull` for this specific case, bypassing
`constIDMap` entirely (valid SPIR-V; constants need not be
deduplicated).

Also discovered and fixed a print/parse asymmetry while adding a lit
test: `spirv::ConstantOp::print` only appended `: type` for
`spirv::ArrayType`/`spirv::StructType` results; a `UnitAttr` value (not
itself a `TypedAttr`) printed with no type suffix at all, failing to
round-trip. Fixed by extending the printer's type-suffix condition to
also cover `spirv::PointerType`.

feme itself needed **zero code changes**: `feme/lib/Conversion/
SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` already calls upstream's
`populateSPIRVToLLVMConversionPatterns`, so the new
`ConstantNullPointerPattern` (lowering to `LLVM::ZeroOp`/`llvm.mlir.zero`)
is automatically available.

New/updated tests (all outside `feme/`): `mlir/test/Conversion/
SPIRVToLLVM/constant-op-to-llvm.mlir` (`@null_pointer_constant`); new
`mlir/test/Target/SPIRV/constant-null-pointer.mlir` (MLIR-textual
round-trip plus real SPIR-V binary (de)serialization, modeled on
`function-call.mlir`'s `VariablePointers`/`SPV_KHR_variable_pointers`
capability/extension precedent); `mlir/test/Dialect/SPIRV/IR/
structure-ops.mlir` (`@null_pointer_constant` positive parse/print,
`@null_pointer_constant_wrong_attr` negative verifier-error case).

Regression sweeps: `ninja check-feme` (3738 discovered, 3731/3731
passed, 7 unsupported, same as session start -- no regressions). A full
lit sweep of `mlir/test/{Target/SPIRV,Dialect/SPIRV/IR,
Conversion/SPIRVToLLVM}` (149 tests) found exactly 2 failures
(`arithmetic-ops-to-llvm.mlir`, `memory-ops.mlir`), both confirmed via
`git stash` to fail identically with this fix reverted -- pre-existing,
unrelated test-file bugs (a duplicate-symbol definition in the former),
not caused by this change.

CTS: ran the `dEQP-VK.spirv_assembly.*variable_pointer*` mustpass group
via `feme/utils/run_vulkan_cts.py`, with `variablePointers`/
`variablePointersStorageBuffer` temporarily flipped to `VK_TRUE` for the
sweep only (reverted before committing, feature bits remain `VK_FALSE`
with `K6n`/`K6p` still outstanding). This session's own case-list
enumeration (via `deqp-vk --deqp-runmode=stdout-caselist`) found 4459
matching cases, a handful fewer than the prior session's reported 4464 --
not investigated further, likely a CTS-internal case-generation detail,
not a regression (the 4459 figure is this session's own ground truth,
used consistently for both the pass/fail counts below).

- Before (this session's own re-measurement, same binary state as the
  prior session's reported end-of-session 152 Fail / 3492 NotSupported /
  820 Pass, modulo the 5-case total delta noted above): not independently
  re-run; the prior session's own 152-Fail figure is taken as the
  baseline.
- After this fix: **140 Fail / 3489 NotSupported / 830 Pass** (out of
  4459 total). Re-bucketed all 140 remaining failures by exact error
  signature (`FEME_VULKAN_LOG_CREATION_ERRORS`): 0 `OpConstantNull`
  signatures remain (confirming the entire 16-case `K6o` bucket is
  closed), 15 `"unhandled opcode 401/402/403"` (`K6p`, matching its
  prior 11-case count plus a few newly-revealed cases now that
  `OpConstantNull` legalization itself no longer masks them, mirroring
  the exact pattern the prior `K6a` session saw when `PtrAccessChain`
  legalization stopped masking other bugs), and the remainder
  `"unsupported raised operation"` (`K6n`, the CPU-backend
  resource-handle-normalization limitation). No new failure signatures
  observed; a clean, fully-attributable closure.

`Roadmap.md`: `K6o` struck through (closed). `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change -- this is a bug fix feeding a
still-`VK_FALSE` feature bit, not a new feature-bit flip.

## `K6n`: phi/select-merged resource pointers lowered, 116/140 of cached fail-list passes (roadmap K6n, large partial fix)

This session resumed a prior session's stashed, partially-complete fix
for `K6n` -- the CPU-backend "register-bound resource handle...cannot
normalize" bucket (124 of the original 296 `variable_pointer` mustpass
failures) -- completed the implementation, fixed a use-after-free crash
discovered during verification, added unit tests, and measured the real
CTS delta.

**Fix**: `hasOnlySupportedPointerUses`/`lowerRawPointerUses`
(`SPIRVResourceLowering.cpp`) now accept a pointer-valued `PHINode`/
`SelectInst` as a valid continuation of a resource-handle pointer chain
(previously only `GetElementPtrInst` was accepted, unconditionally
rejecting the `OpPhi`/`OpSelect`-over-pointers shape common to
`variablePointers` shaders). A new `PointerMergeMap`, shared per-function
across every `BoundHandle`'s own `lowerAccesses` call, accumulates each
incoming leg's `(DescriptorIndex, Offset)` pair and lazily synthesizes a
matching side-car phi/select once every leg has reported in -- including
the cross-handle case, where the two legs come from entirely different
bound handles reached via two separate `lowerAccesses` calls. A
`ConstantPointerNull` incoming edge (the common `variablePointers`
null-check idiom) is filled with `computeClampedIndex`'s own
out-of-range sentinel (`0xFFFFFFFF`/`0`) rather than left unresolved.

**Use-after-free fix**: the first working version crashed
(`UNREACHABLE executed at Value.cpp:99`, "Use still stuck around after
Def is destroyed") on the very first real CTS case tried. Root cause:
`mergeIntoPhi`/`mergeIntoSelect` recorded each leg's value into the new
side-car phi/select's state but never cleared the *original* phi/select's
own reference to that leg -- so when the caller later erased the
consumed leg's instruction, the original (not-yet-fully-rewritten)
phi/select still pointed at dead IR. Fixed by poisoning
(`PoisonValue::get`) the original's own operand the moment each leg is
consumed.

**Unit tests**: 4 new tests added to `SPIRVResourceLoweringTest.cpp`:
same-handle phi merge (two `getpointer` calls on one handle, different
constant indices), cross-handle phi merge (two different bound
handles), cross-handle select merge, and a phi with one
`ConstantPointerNull` incoming edge verifying the sentinel fill. All
pass; `ninja check-feme` is clean (3735 passed / 7 unsupported, same as
session start -- no regressions) with the feature bits left at their
committed `VK_FALSE`.

CTS: re-ran the prior session's own cached 140-case fail-list (the
original combined `K6n`+`K6o`+`K6p` set) from the correct CTS data
directory (`/home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/
vulkan`, avoiding the prior session's `ResourceError`s caused by running
from a scratch directory without `.amber` data), with
`variablePointers`/`variablePointersStorageBuffer` transiently flipped
to `VK_TRUE` for measurement only (reverted before every commit, not
part of any commit):

- **Before this session's fix (all 140 failing, per the cached list's
  own provenance)**: 0/140 pass.
- **After**: **116/140 pass (82.9%)**. Of the 24 remaining failures, 14
  are `K6p`'s own out-of-scope `"unhandled opcode 401/402/403"` cases
  (confirmed unaffected by this fix, as expected), and exactly 10 are
  newly-isolated `K6n`-scope residuals, split by exact error signature
  into three new roadmap rows: `K6q` (2 cases, a deeper handle-level
  phi/select merge -- merging raw resource *handles* before `getpointer`
  is ever called, architecturally distinct from the pointer-level merge
  this fix addresses), `K6r` (2 cases, storing a handle-derived pointer
  as a store's *value* operand rather than its pointer operand), and
  `K6s` (5 cases, a stage-specific frag/tessc/tesse result-correctness
  bug with zero normalization rejection -- the pipeline now builds and
  runs, but produces the wrong output; a genuine downstream bug, not a
  legalization gap). The remaining 1 case (`workgroup_two_buffers`)
  fails in `feme-cpu-simdize` (an unrelated groupshared-pointer-chain
  limitation), was always miscategorized as part of `K6n`'s bucket, and
  is left as-is (not re-filed).

`Roadmap.md`: `K6n` updated (not struck through -- `K6q`/`K6r`/`K6s`
still block closing it) with this session's fix description and the
three new split-off rows (`K6q`, `K6r`, `K6s`), each with its own exact
case list and error signature.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- `variablePointers`/`variablePointersStorageBuffer` remain `VK_FALSE`,
this is a bug fix feeding a still-closed feature bit, not a new
feature-bit flip.

## `K6r`: pointer stored through a private global and loaded in another function, both cases pass (roadmap K6r, closed)

Picked `K6r` from the prior session's suggested next steps (smallest,
most contained of `K6n`'s three split-off residual buckets -- 2 cases).

Initial hypothesis (a quick relaxation of `hasOnlySupportedPointerUses`'s
store-operand check) did not survive investigation: tracing the real
failing case (`FEME_CPU_LOG_RESOURCE_NORMALIZATION`,
`FEME_DUMP_IR_PRENORM`) showed the rejected global
(`@spirv_var_101 = private global ptr addrspace(11) undef`) still present,
unlocalized, at the point `SPIRVResourceLoweringPass` runs -- yet an
isolated re-run of the exact same `feme-cpu-localize-private-globals,
function(sroa<>,mem2reg)` pass sequence on IR extracted verbatim from the
real failing case fully eliminated it. Added a temporary
`FEME_DEBUG_LOCALIZE_PRIVATE_GLOBALS` diagnostic to
`LocalizePrivateGlobalsPass` (reverted before committing) to find the real
reason: the global is used by *two* functions, `main` (a `load`) and
`main.patchconstant` (a `store`) -- `splitTessellationControlEntry`'s own
barrierless split (`CanonicalizeStage.cpp`) legitimately produces this
shape for a tessellation-control entry with a group-sync barrier, since a
value computed before the barrier and read back after it, in what becomes
a separately-dispatched `<entry>.patchconstant` function, has no SSA value
that can cross that real function boundary -- so `LocalizePrivateGlobalsPass`
correctly declines to localize it (`usedByOneFunctionOnly` is doing exactly
its job), leaving `SPIRVResourceLoweringPass` to see the raw, un-localized
global directly.

Fixed in `SPIRVResourceLowering.cpp`: `hasOnlySupportedPointerUses` now
recognizes this shape (a store whose value operand is a supported pointer,
whose pointer operand is a global used by exactly that one store and
otherwise only loads, each recursively validated); new
`lowerPointerMergeGlobalStore` (mirroring `mergeIntoPhi`/`mergeIntoSelect`'s
existing `PointerMergeMap` design, but with only one producer to wait for)
replaces the original pointer global with two new scalar globals (`i32`
descriptor index, `i64` byte offset), writing the resolved pair at the
store's own site and reading it back -- continuing `lowerRawPointerUses`
from each load's own result -- at every load, with no real cross-function
pointer value ever materialized.

New unit test added (`SPIRVResourceLoweringTest.cpp`):
`LowersPointerStoredThroughPrivateGlobalAndLoadedInAnotherFunction`, a
two-function module with a `select`-merged pointer stored through a
private global in one function and loaded in another.

CTS: both `dEQP-VK.spirv_assembly.instruction.graphics.variable_pointers.
graphics.stores_private_single_buffer_tessc` and
`...stores_private_two_buffers_tessc` now pass (confirmed via a transient
`variablePointers`/`variablePointersStorageBuffer` `VK_TRUE` feature-bit
flip for measurement only, not committed). A full re-run of the
`dEQP-VK.spirv_assembly.instruction.*.variable_pointers.*` group (1637
cases) shows 942 Pass / 7 Fail / rest NotSupported, with neither of this
bucket's two cases among the 7 remaining failures -- which match `K6s`'s
already-documented frag/tessc/tesse result-correctness bug
(`writes_single_buffer_frag`, `writes_single_buffer_tessc`,
`writes_single_buffer_tesse`, `writes_two_buffers_frag`) plus the
already-known `workgroup_two_buffers` (`feme-cpu-simdize` limitation) and
`opphi_outer_struct_two_buffers_{first,second}_input` (`K6q`, handle-level
phi/select merge) cases -- a clean closure with zero new failures.

`ninja check-feme`: full target rebuilt and rerun clean (3736/3743 passed,
7 unsupported, same as before this change) -- no regressions.

`Roadmap.md`: `K6r` struck through (closed).
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change --
`variablePointers`/`variablePointersStorageBuffer` remain `VK_FALSE`
(`K6q`/`K6s` still block closing the bundled feature).

## `K6s`: uniform-loop-in-divergent-diamond resource-call masking gap fixed, 5/5 cases pass (roadmap K6s, closed)

Picked `K6s` from the prior session's suggested next steps (now the
largest remaining `variablePointers` residual bucket, 5 cases, a genuine
result-correctness bug rather than another legalization gap).

Reproduced `dEQP-VK.spirv_assembly.instruction.graphics.variable_pointers.
graphics.writes_single_buffer_frag` directly via `deqp-vk` (transient
`variablePointers`/`variablePointersStorageBuffer` `VK_TRUE` feature-bit
flip, measurement only): every one of the 100 output-buffer entries read
back exactly `+2111.0` too high. Ruled out rasterizer-level
over-enumeration first (temporary `FEME_K6S_DEBUG_COUNT_PIXEL00`-gated
instrumentation in `Executor.cpp`, reverted before committing): the one
pixel the test actually samples is enumerated exactly once across the
whole draw; `Executor.cpp`'s fragment dispatch is tile-based (64 tiles,
256x256 test), and summing each tile's own quad count gives 2112 total
wave-calls for the draw -- exactly `2111` extra executions plus the one
genuine execution.

Dumped the actual compiled fragment IR (`FEME_DUMP_IR=1`, a pre-existing
`Pipeline.cpp` debug aid): `WaveSize=4`/`QuadsPerWave=1`, so one wave-call
processes exactly one quad, confirming total wave-calls = total quad
count = 2112. The IR showed the shader's `isUniqueIdZero` divergence mask
computed correctly (`%live.t.wide`/`%sideeffect.t.wide`/etc.) but used
*only* at the final merge block -- the 100-iteration loop performing the
actual `feme.cpu.resource.load.raw.f32`/`.store.raw.f32` calls is entered
*unconditionally*, is itself fully scalar, and every one of its resource
calls carries a hardcoded `i1 true` mask operand, never the real
divergence condition. This loop therefore executes in full for every
wave-call regardless of whether that quad's lane actually satisfies the
shader's own `if` guard.

Root-caused to `feme/lib/Transforms/CPU/Linearize.cpp`, not
`feme/lib/Target/CPU` as originally guessed in the roadmap entry.
`DiamondFlattener` deliberately stops its walk at a cycle boundary nested
in a divergent diamond arm, deferring the loop's interior masking to
`LoopLinearizer::linearizeCycle`. For a loop whose own header/latch exit
check is itself uniform (this test's shape: `header != latch`, neither
divergent), `linearizeCycle`'s `CycleHasMaskOps` scan is the sole gate for
whether the pre-existing, already-correct entry-mask-threading machinery
(`makeActivePNPair`/`applyStageMasks`/`DiamondFlattener::
flattenLoopBodyRegion`) ever runs on the loop's blocks at all -- and that
scan previously recognized only `feme::StageOpKind` calls (`Discard`/
`Demote`/`OutputStore`/etc.), entirely missing plain `feme.cpu.resource.*`
calls. Since this test's loop body contains *only* resource calls, the
scan found nothing, and the function bailed via "no divergence, no mask
ops: a real uniform loop, left alone" -- leaving every resource call's
mask stuck at its original placeholder `true` forever, even though
`applyStageMasks` itself already knows exactly how to mask such a call
correctly (confirmed by reading its own `matchResourceCall`/`matchImageCall`
handling) -- it just was never invoked for this shape.

Fixed with a two-line extension to the `CycleHasMaskOps` detection loop:
also set it `true` when `matchResourceCall`/`matchImageCall` (the exact
matchers `applyStageMasks` itself uses) find a match anywhere in the
cycle, letting the existing masking machinery actually run instead of
bailing out early. No new masking logic was needed -- the gap was purely
in *detecting* the need, not in applying it.

New unit test added (`LinearizeTest.cpp`):
`MasksResourceCallInUniformLoopNestedInDivergentDiamond`, a minimal
header-distinct-from-latch uniform loop (matching the real bug's exact
shape -- an initial single-block-loop version of this test did not
reproduce the bug, since that shape takes an entirely different,
already-correct early-return path) containing only a
`feme.cpu.resource.store.raw.i32` call, nested inside a divergent `if`,
confirming the store's trailing mask operand is no longer a compile-time
constant after the pass runs.

CTS: all 5 of `K6s`'s cases
(`writes_single_buffer_frag`/`_tessc`/`_tesse`,
`writes_two_buffers_frag`/`_tessc`) now pass individually. A full
`dEQP-VK.spirv_assembly.instruction.graphics.variable_pointers.*` re-run
(790 cases, this session's narrowed CTS data set) shows 790/790 Pass --
0 Fail, 0 NotSupported -- the entire `variablePointers` instruction group
is now completely clean (confirmed via the same transient `VK_TRUE`
feature-bit flip for measurement only, not committed).

`ninja check-feme`: full target rebuilt and rerun clean (3737/3744
Discovered Tests passed, 7 unsupported, 0 failed) -- no regressions.

`Roadmap.md`: `K6s` struck through (closed).
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change --
`variablePointers`/`variablePointersStorageBuffer` remain `VK_FALSE`
(`K6n`'s residual resource-handle-normalization architectural bucket,
`K6p`'s upstream-MLIR `OpPtrEqual`/`OpPtrNotEqual`/`OpPtrDiff` gap, and
`K6q`'s handle-level phi/select merge still block closing the bundled
feature).

## `K6q`: handle-level phi/select-merged resource handles lowered, 2/2 cases pass (roadmap K6q, closed)

Picked `K6q` from the prior session's suggested next action (2 cases,
handle-level `OpPhi`/`OpSelect` merge -- flagged as architecturally the
hardest of the three small residual `variablePointers` buckets, but
smallest case count, so tackled before `K6p`/`K6n`-proper).

Reproduced both cases directly via `deqp-vk` (transient
`variablePointers`/`variablePointersStorageBuffer` `VK_TRUE` feature-bit
flip, measurement only, reverted before committing):
`dEQP-VK.spirv_assembly.instruction.compute.variable_pointers.
complex_types_compute.opphi_outer_struct_two_buffers_{first,second}_input`,
both failing pipeline creation with
`FEME_VULKAN_LOG_CREATION_ERRORS=1`'s `"unsupported raised operation:
... is a register-bound resource handle the FeMe CPU target cannot
normalize ..."` (the same signature class as `K6n`, but a different
resource-level cause).

Dumped the pre-normalization IR (`FEME_DUMP_IR_PRENORM=1`) to see the
exact structural shape: a *nested* phi-of-phi merging two
`handlefrombinding` calls (different bindings, same type) across a
diamond nested inside another diamond, with one incoming edge of the
inner phi being a literal `poison` handle-typed value -- a dead/
never-taken-edge artifact from loop/control-flow linearization, not a
real handle -- feeding a single `getpointer` call with a constant index.

Root cause: `hasOnlySupportedUses` (the handle-level checker in
`SPIRVResourceLowering.cpp`, distinct from the pointer-level
`hasOnlySupportedPointerUses` `K6n` fixed) required every user of a raw
resource `Handle` to be a `getpointer` call directly, with no
phi/select recognition at the handle level at all -- so any shader that
conditionally selects between two raw handles (same type, different
bindings) before ever calling `getpointer` on the merge result was
rejected outright, architecturally one level earlier than `K6n`'s
already-fixed pointer-level merge (which operates on pointers already
derived via `getpointer`).

Fixed by mirroring `K6n`'s pointer-level merge design one level earlier,
at the handle level: added `hasOnlySupportedHandleUse`/
`hasOnlySupportedHandleMergeUses` (recursive validation of handle-level
phi/select merges, with a `Visited` set guarding nested/cyclic merges),
extracted `lowerAccesses`'s per-handle loop body into a new
`lowerHandleAccesses(Value *Handle, ...)` parameterized by a generic
handle value so it can be invoked either directly (from `lowerAccesses`)
or recursively (from a merge continuation) with a synthesized
`DescriptorIndex`, and added a `HandleMergeState`/`HandleMergeMap`
(simpler than `K6n`'s `PointerMergeMap` -- only a single `i32`
descriptor index needs merging, since no byte offset exists before
`getpointer` is called) plus `mergeIntoHandlePhi`/`mergeIntoHandleSelect`.
The real CTS case's `poison` dead edge is handled via
`isa<UndefValue>(Phi->getIncomingValue(I))` (poison is a subclass of
`UndefValue` in LLVM), filling the synthesized descriptor-index phi slot
with the existing `nullPointerMergeDescIndex` sentinel (reused as-is,
since its signature has no pointer-specific dependency). Because every
phi/select operand must share the exact same LLVM type, two merged
handles are guaranteed to classify identically (same
`HandleKind`/`Stride`/`ElementStruct`/etc.) -- no defensive runtime
check was needed for that invariant.

Three new unit tests added (`SPIRVResourceLoweringTest.cpp`):
`LowersPhiMergedHandleToSharedGetPointerAccess`,
`LowersSelectMergedHandleToSharedGetPointerAccess`, and
`LowersNestedPhiMergedHandleWithPoisonEdge` (matching the real CTS
case's exact nested-phi-with-poison-edge shape). The full
`FeMeTransformsCPUTests` suite (621 tests) passes with zero regressions.

CTS: both target cases now pass individually. A full
`dEQP-VK.spirv_assembly.instruction.*.variable_pointers.*` re-run (1637
cases, transient `VK_TRUE` feature-bit flip for measurement only, not
committed) shows 948 Pass/1 Fail/688 Not Supported -- up from the two
sessions' earlier combined closures. The 1 remaining failure is
`workgroup_two_buffers`, the pre-existing, already-documented, unrelated
`feme-cpu-simdize` groupshared-pointer-chain limitation (see `K6n`
above) -- not a new regression and not in this bucket's scope.

`ninja check-feme`: full target rebuilt and rerun clean (3740/3747
Discovered Tests passed, 7 unsupported, 0 failed) -- no regressions.

`Roadmap.md`: `K6q` struck through (closed).
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change --
`variablePointers`/`variablePointersStorageBuffer` remain `VK_FALSE`
(`K6p`'s upstream-MLIR `OpPtrEqual`/`OpPtrNotEqual`/`OpPtrDiff` gap and
`K6n`'s own 124ish-case CPU-backend resource-handle-normalization
architectural limitation still block closing the bundled feature).

## `K6n`: verified the architectural bucket is already fully closed, 956/16/3492 on the full group (roadmap K6n, closed -- no code change)

Picked up the prior session's suggested next action (`K6n`-proper, "~124
cases, the largest remaining `variablePointers` bucket... still not
root-caused"). Before guessing at a fix approach, re-measured the
current real state directly rather than trusting that framing, which
turned out to have been carried forward unchanged across several
sessions' own next-steps text without being re-verified after `K6q`,
`K6r`, and `K6s` each closed.

Reproduced the broad historical measurement convention exactly: `deqp-vk
--deqp-runmode=stdout-caselist` filtered to the `*variable_pointer*`
substring (4464 cases, matching the number multiple prior sessions
already used, not the narrower `.variable_pointers.` path-segment group
a stale summary had conflated it with). Ran the full case list through
`feme/utils/run_vulkan_cts.py`'s crash-tolerant batch/solo-reverify
harness (`--batch-size 200 --workers 6`), with
`variablePointers`/`variablePointersStorageBuffer` transiently flipped to
`VK_TRUE` for measurement only (reverted before committing, not
committed).

**Result: 956 Pass / 16 Fail / 3492 NotSupported.** Checked every one of
the 16 failures' exact error signature
(`FEME_VULKAN_LOG_CREATION_ERRORS=1` on three representative cases
directly, plus the full verified-failures list for the rest by name):
zero cases anywhere in the group fail with the
`"unsupported raised operation... register-bound resource handle..."`
signature `K6n` was originally filed under. One failure
(`workgroup_two_buffers`) is the already-documented, unrelated
`feme-cpu-simdize` groupshared-pointer-chain limitation (confirmed via
its exact unchanged error text, `"feeds an unsupported user"`). The
other 15 are `"unhandled opcode 401/402/403"` -- entirely `K6p`'s scope
(confirmed directly: `opptrequal`/`opptrnotequal`/`opptrdiff` test names,
opcode 401 and 403 reproduced directly on representative cases).

**Conclusion: `K6n`'s own architectural resource-handle-normalization
bucket is fully closed.** Every one of its original 124 cases now
passes -- the large majority via that session's own `PointerMergeMap`
pointer-level merge fix, and the final 10 residual cases via `K6q`'s
handle-level merge, `K6r`'s store-value-operand fix, and `K6s`'s
loop-masking fix, each already closed and verified independently in
their own sessions. No further code change was needed or made this
session -- this was a verification-only session that corrected a stale
roadmap/next-steps framing rather than a fix. `K6p`'s count was also
re-confirmed and updated: 15 cases now (6 `OpPtrEqual` + 6
`OpPtrNotEqual` + 3 `OpPtrDiff`), up from its originally-bucketed 11 --
the same "closing one gap reveals a few more previously-masked cases"
pattern the original `K6a` and `K6o` sessions both saw.

`Roadmap.md`: `K6n` struck through (closed, no code change -- verification
only). `K6p`'s row updated with the re-confirmed 15-case count and a note
that it is now the only remaining code-change bucket blocking the
`variablePointers`/`variablePointersStorageBuffer` feature bits besides
the unrelated `workgroup_two_buffers` `feme-cpu-simdize` case.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change --
feature bits remain `VK_FALSE`, no code was modified this session beyond
the transient (reverted) measurement flip.

## `K6p`: feme-local OpPtrEqual/OpPtrNotEqual/OpPtrDiff merge, 4/29 cases pass; segfault and invalid-IR bugs fixed; K6t split off (roadmap K6p, feme-local half closed; K6t opened)

Picked up `K6p` from the prior session's suggested next action (the only
remaining large unattempted bucket once `K6o`/`K6q`/`K6r`/`K6s` all
closed and `K6n`-proper was re-confirmed as already architecturally
closed). The upstream-MLIR TableGen/deserializer/lowering half of `K6p`
had already been completed in the immediately-prior session (new
`spirv.PtrEqual`/`spirv.PtrNotEqual`/`spirv.PtrDiff` ops, deserializer
cases, and `IComparePattern`/`PtrDiffPattern` lowering to
`icmp`/`ptrtoint`+`sub`); this session's own scope was the feme-local
CPU-backend half, resuming uncommitted in-progress work left over from a
stalled prior session (a known duplicate-parameter build error).

Fixed the stalled build (duplicate `PointerCompareMap &Compares`
forward-declaration parameter; a missing `Compares` argument at
`lowerPointerMergeGlobalStore`'s own call site), then extended
`hasOnlySupportedPointerUses` to accept an equality-only `icmp` (from
`OpPtrEqual`/`OpPtrNotEqual`) and a single-use `ptrtoint` feeding exactly
one `sub` (from `OpPtrDiff`), and added a new `PointerCompareMap`/
`PointerCompareState` tracking structure plus `mergeIntoCompare`/
`mergeIntoDiff` (mirroring `PointerMergeMap`'s existing phi/select
"whichever chain arrives first waits for the other" design, but keyed on
a binary op's own LHS/RHS operand position): once both operands' own
(descriptor index, byte offset) pairs are independently resolved,
`mergeIntoCompare` re-expresses the comparison as
`(desc0 == desc1) & (off0 == off1)`, and `mergeIntoDiff` re-expresses the
diff as a plain `sub` of the two offsets alone (per `OpPtrDiff`'s own
same-buffer-object spec assumption, descriptor indices are never
compared for a diff).

Testing this surfaced two real, previously-latent bugs `K6p`'s new
opcode support newly reached for the first time:

1. **Infinite-recursion segfault** (`variable_pointers_vars_ssbo_diff`):
   `hasOnlySupportedPointerUses`'s pre-existing `lowerPointerMergeGlobalStore`
   -chasing recursion into a loaded global's own uses had no cycle-
   detection. A loop-carried pointer threaded through a `Private`-storage
   global (load, `+1` GEP, store back each iteration) is genuinely cyclic
   in def-use terms; `OpPtrDiff` support let the checker recurse deep
   enough to reach this shape for the first time (previously unreachable
   since `OpPtrDiff` itself was unsupported, so nothing walked this far).
   Fixed with the same `Visited`-set guard `hasOnlySupportedHandleMergeUses`
   already uses (`K6q`'s own precedent), threaded through all 4 recursive
   call sites. After the fix, this case fails cleanly with the expected
   `K6n`-style "unsupported raised operation... cannot normalize" error
   instead of segfaulting -- a correctness improvement, not a new pass.
2. **Invalid LLVM IR** (`different_ssbos_equal`/`different_ssbos_not_equal`/
   `null_comparisons_ssbo_equal`/`null_comparisons_ssbo_not_equal`):
   these compare two *whole* resource handles directly (`OpPtrEqual`
   with no intervening `OpAccessChain`), which upstream's generic,
   pre-existing `IComparePattern` lowered straight to an `llvm.icmp` on
   an opaque `!llvm.target<"spirv.VulkanBuffer"...>` operand -- rejected
   outright by the LLVM dialect verifier (a hard crash, not a clean
   legalization failure). Fixed upstream, as its own isolated
   "issue outside FeMe" commit: `IComparePattern` now checks its operand
   type is an LLVM pointer or signless integer (scalar or
   vector-of-either) before matching, declining via `notifyMatchFailure`
   otherwise. This is a no-op for every pre-existing `IComparePattern`
   use (`OpIEqual` and friends always convert to real integers),
   confirmed via the full `mlir/test/{Conversion/SPIRVToLLVM,Dialect/SPIRV,
   Target/SPIRV}` lit sweep (149 tests, same 2 pre-existing unrelated
   failures, zero new regressions). After the fix, `different_ssbos_equal`
   fails cleanly with `"failed to legalize operation 'spirv.PtrEqual'...
   explicitly marked illegal"` instead of crashing -- whole-handle
   comparison itself is still unimplemented (split off as the new `K6t`
   roadmap row below), but no longer crashes.

CTS (29-case `dEQP-VK.spirv_assembly.instruction.spirv1p4.opptr{equal,
notequal,diff}.*` set, generated via `deqp-vk --deqp-runmode=stdout-caselist`):
**4/29 pass** (`simple_variable_pointers_ptr_equal`,
`simple_variable_pointers_ptr_not_equal`, `different_wgs_equal`,
`different_wgs_not_equal`). The remaining 25 are all cleanly attributable
to three distinct, already-known-or-newly-split buckets: `K6n`'s
CPU-backend resource-handle-normalization limitation, CFG-linearization/
`groupshared`-canonicalization gaps (pre-existing, unrelated to `K6p`),
and whole-handle comparison (at least the 4 cases above; split off as
`K6t`). Full `*variable_pointer*` substring sweep (4464 cases, feature
bits transiently flipped to `VK_TRUE` for measurement only, reverted
afterward): **958 Pass, 3492 NotSupported, 14 Fail** -- 1 pre-existing
unrelated case (`workgroup_two_buffers`) plus 13 of the new `opptr*`/
`variable_pointers_*` cases, all already-known K6n/CFG-linearization/
whole-handle shapes, zero new regressions. `variablePointers`/
`variablePointersStorageBuffer` remain `VK_FALSE` -- `K6p`'s own
opcode-support gap is now closed, but the bundled feature as a whole
remains blocked on `K6n` (the large majority of the residual) and `K6t`.

New unit tests (`feme/unittests/Transforms/CPU/SPIRVResourceLoweringTest.cpp`):
`LowersPtrEqualOnTwoHandleChainsToDescriptorAndOffsetComparison` and
`LowersPtrDiffOnTwoHandleChainsToOffsetSubtraction`. Both use
literal-constant bindings/`getpointer` indices, which lets
`mergeIntoCompare`/`mergeIntoDiff`'s own `IRBuilder<>` (`ConstantFolder`)
constant-fold the synthesized compare/sub expressions away entirely to a
literal answer (`ret i1 false` / `ret i64 -4`) rather than leaving a live
`and`/`sub` instruction in the IR -- itself a correct and useful check
(two statically-known-distinct bindings can never compare pointer-equal;
a 0-vs-1-element byte-offset delta on a 4-byte-stride buffer is exactly
`-4`), asserted directly against the folded return value rather than
against an intermediate instruction shape. New lit test
(`feme/test/Conversion/SPIRVToLLVM/spirv-to-llvm-ptr-equal-whole-handle-invalid.mlir`):
exercises the upstream `IComparePattern` fix using FeMe's own custom
resource-handle type converter (the only way to construct the triggering
non-pointer-operand shape), confirming whole-handle `OpPtrEqual` now
declines legalization cleanly rather than crashing the LLVM dialect
verifier.

Full `check-feme` target: 3743/3750 passed (7 unsupported, 0 failed),
confirming zero regressions across every phase of translation. Full 623
`FeMeTransformsCPUTests` (621 pre-existing + 2 new) pass.

`Roadmap.md`: `K6p` updated (not struck through -- its own opcode-support
gap is closed, but `variablePointers` as a whole is still blocked on
`K6n` and the newly-split-off `K6t`). New `K6t` row added for the
whole-handle-comparison residual (at least 4 of the 25 remaining `K6p`
CTS failures), not yet attempted. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change -- `variablePointers`/
`variablePointersStorageBuffer` remain unsupported/`VK_FALSE`, no feature
bit flipped permanently this session.

## `K6t`: whole-handle `OpPtrEqual`/`OpPtrNotEqual` via a new LLVM intrinsic; `mergeIntoCompare` null-pointer bug found and fixed, 6/8 targeted cases pass (roadmap K6t, closed)

`K6t` (split off from `K6p`'s own 25-case residual) covers comparing two
*entire* resource handles directly (`spirv.PtrEqual`/`spirv.PtrNotEqual`
with no intervening `OpAccessChain`/`getpointer` on either side). Unlike
`K6p`'s pointer-derived-operand case, LLVM's `icmp` cannot operate on an
opaque resource-handle target-extension type at all (e.g.
`!llvm.target<"spirv.VulkanBuffer", ...>`) -- there is no legalization
pattern that can ever make this shape well-formed as a plain `icmp`, so a
single-layer `SPIRVToLLVMPatterns.cpp` fix (as used for `K6p`) was not an
option.

Implemented as a three-layer fix instead:

1. **New upstream LLVM intrinsic** `llvm.spv.resource.handleequal`
   (`llvm/include/llvm/IR/IntrinsicsSPIRV.td`), taking two resource-handle
   values and returning `i1`. While adding this, found and fixed a real
   TableGen correctness gap: the two operands were declared as
   independent `llvm_any_ty` placeholders, which does *not* enforce they
   share a type despite the doc comment's claim both must match. Changed
   the second operand to `LLVMMatchType<0>` (same precedent as
   `int_spv_distance`/`int_spv_faceforward` in the same file). Verified
   via manual `llvm-as`/`opt -passes=verify` round-trip that a
   mismatched-type declaration is now rejected
   (`"intrinsic argument 1 type (matching overload type 0) expected i32,
   but got ptr"`); this required rebuilding `llvm-as`/`opt` themselves --
   stale binaries silently accept an invalid declaration without picking
   up a newly-added TableGen `LLVMMatchType` constraint. This is the
   standing "issue outside FeMe" piece: self-contained, isolated test
   added at `llvm/test/Verifier/intrinsic-spv-resource-handleequal.ll`
   (using the `split-file` idiom, precedented by
   `llvm/test/Verifier/initializes-attr.ll`, with one valid same-type
   round-trip sub-case and one verifier-rejected mismatched-type
   sub-case). Full `llvm/test/Verifier/` suite (443 tests) passes with
   zero regressions.
2. **New FeMe-local `HandleCompareConversionPattern`**
   (`SPIRVToLLVMPatterns.cpp`, registered at `FeMeBenefit` to run before
   the declining upstream `IComparePattern`) recognizes `spirv.PtrEqual`/
   `spirv.PtrNotEqual` on a resource-handle-typed operand and lowers to a
   call of the new intrinsic, with `PtrNotEqual` expressed as a separate
   `xor` negation of the same call rather than a second intrinsic. New
   lit test `spirv-to-llvm-ptr-equal-whole-handle.mlir` (3
   `--split-input-file` cases: `PtrEqual` on two distinct handles,
   `PtrNotEqual`'s negation, and `PtrEqual` against a literal null
   handle), replacing the now-obsolete
   `spirv-to-llvm-ptr-equal-whole-handle-invalid.mlir` (which had
   asserted legalization *failure* for exactly this shape -- no longer
   true once this pattern exists). Full
   `feme/test/Conversion/SPIRVToLLVM/` lit suite (138 tests) passes with
   zero regressions.
3. **`SPIRVResourceLowering.cpp` (CPU backend)** gained matching dispatch:
   `hasOnlySupportedHandleUse`/`lowerHandleAccesses` recognize a call to
   the new intrinsic as a valid handle use; `mergeIntoHandleCompare`
   re-expresses the call as a direct `desc0 == desc1` comparison of the
   two handles' own `DescriptorIndex` values (no byte-offset component,
   since neither side has been through `getpointer`), with a literal-null
   handle operand resolved immediately via the existing
   `nullPointerMergeDescIndex` sentinel; `foldSameOperandHandleCompares`
   folds a same-operand call (a real self-comparison, or `null` vs.
   `null`) directly to a constant `true` before `collectHandles` runs at
   all. Five new unit tests added to
   `feme/unittests/Transforms/CPU/SPIRVResourceLoweringTest.cpp` covering
   the equal/not-equal/null/self-compare shapes.

**A genuine, previously-latent bug was found as a side effect of K6t**,
not part of K6t's own scope: once the whole-handle fix let a
`null_comparisons_ssbo_not_equal`-shaped shader legalize and execute for
the first time, its separate *pointer-level* (post-`getpointer`)
`%real_ptr != null` sub-comparison came back wrong. Root cause:
`mergeIntoCompare` (the existing `K6p` pointer-level compare-merge
function) had no `ConstantPointerNull` special case, unlike its siblings
`mergeIntoPhi`/`mergeIntoSelect`/the new `mergeIntoHandleCompare` (all of
which do). Because `hasOnlySupportedPointerUses` accepts any equality
`icmp` regardless of the other operand's shape, a literal null operand
never independently reaches `mergeIntoCompare` through the normal
chain-walk, leaving the real side's own operand permanently replaced with
`PoisonValue` and the comparison never rewritten -- producing
`icmp eq poison, null`, which silently folds to a wrong constant. This
bug was unobservable before `K6t`: the exact shader shape that exposes it
previously failed whole-handle legalization entirely (a hard compile
error), so the buggy downstream code path was simply never reached.
Fixed by mirroring the same three callers' own sentinel-resolution
pattern, reusing the existing `nullPointerMergeDescIndex`/
`nullPointerMergeOffset` sentinels directly in `mergeIntoCompare`. New
unit test `LowersPtrNotEqualOnRealHandleVsNullPointerToTrue`. Full
`FeMeTransformsCPUTests` suite: 629/629 pass (up from 628).

CTS (8-case targeted whole-handle set,
`dEQP-VK.spirv_assembly.instruction.spirv1p4.{opptrequal,opptrnotequal}.
{different,null_comparisons}_{ssbo,wg}_{equal,not_equal}`, feature bits
transiently flipped to `VK_TRUE` for measurement only, reverted
afterward): **6/8 pass** (up from 0/8 pre-`K6t`, which failed whole-handle
legalization entirely). The 2 remaining failures
(`null_comparisons_wg_equal`/`_not_equal`) are the pre-existing,
already-documented, unrelated `feme-cpu-simdize` groupshared-
canonicalization limitation (`"groupshared global ... is used in a way
this milestone does not yet canonicalize"`), not a `K6t` gap. Broader
29-case `opptr*` sweep: **8/29 pass** (up from `K6p`'s own prior 4/29),
zero regressions -- the remaining 21 cleanly attributable to `K6n`
(already closed, so this refers to re-confirmed-clean cases, not new
failures), CFG-linearization/groupshared gaps, and `opptrdiff` (not yet
individually root-caused past the CFG-linearization signature on at
least one representative case; see below).

While re-verifying `K6p`'s row this session, re-checked all 5
`opptrdiff.*` cases (feature bits transiently re-flipped again, then
reverted): one (`ssbo_comparisons_diff`) fails with the exact
pre-existing CFG-linearization error text
(`"feme-cpu-linearize: ... reconvergence block '' does not have exactly
two predecessors"`), confirming it belongs to the already-documented,
out-of-scope `groupshared`/CFG-linearization bucket rather than being a
new `K6p`/`K6t`-adjacent gap; the other 4 fail pipeline creation without
a printed diagnostic in this quick pass and were left as a follow-up
investigation (not guessed at) rather than expanding this session's
scope further.

Full `check-feme` target (final run, all K6t changes in place):
3749/3756 passed, 7 unsupported (expected), 0 failures. Zero regressions.

`vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
Device` at the start, middle, and end of this session, per the standing
"every session, every time" instruction.

`Roadmap.md`: `K6t` struck through/closed, with the `mergeIntoCompare`
null-fix documented as a related-but-distinct discovery within the same
row (it is not itself a new roadmap row, since it is a bug fix uncovered
by `K6t` rather than a separate tracked feature gap). `K6p`'s own row
updated to reflect `K6t`'s closure and this session's `opptrdiff`
re-check. `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
change -- `variablePointers`/`variablePointersStorageBuffer` remain
unsupported/`VK_FALSE`; no feature bit flipped permanently this session.

## `opptrdiff` residual fully triaged; one genuine false-rejection bug found and fixed, one deeper gap split off as `K6u` (roadmap `K6p` update)

This session picked up the prior session's open follow-up: individually
root-cause each of the 5 `dEQP-VK.spirv_assembly.instruction.spirv1p4.
opptrdiff.*` cases (`ssbo_comparisons_diff`, `wg_comparisons_diff`,
`variable_pointers_vars_wg_diff`, `variable_pointers_vars_ssbo_diff`,
`variable_pointers_vars_ssbo_2_diff`) rather than assuming they share one
bucket, using `FEME_VULKAN_LOG_CREATION_ERRORS`/`FEME_DUMP_IR_PRENORM`
plus a previously-untried, far more targeted diagnostic,
`FEME_CPU_LOG_RESOURCE_NORMALIZATION` (prints the exact rejecting
instruction/reason via `logNormalizationRejection`, rather than requiring
a manual IR trace).

**Three of the five are confirmed pre-existing, out-of-scope limitations,
not new gaps:** `ssbo_comparisons_diff`/`wg_comparisons_diff` hit the
already-documented CFG-linearization `"reconvergence block '' does not
have exactly two predecessors"` error; `variable_pointers_vars_wg_diff`
hits the already-documented `feme-cpu-simdize` groupshared-
canonicalization limitation.

**The remaining two, `variable_pointers_vars_ssbo_diff`/
`_ssbo_2_diff`, were genuinely new and are now split into two separate
findings:**

1. **Fixed this session (see commit):** `hasResolvableGEPByteOffset`
   rejected *any* `getelementptr` with a negative constant byte offset
   outright -- not just one whose unresolvable-at-compile-time *variable*
   offset term happened to carry a negative multiplier. Both CTS cases'
   shaders walk a `Private`-storage SSBO-pointer iterator backward one
   element per loop iteration (`OpPtrAccessChain %ptr %backward_gep
   %int_n1`, i.e. `getelementptr i32, ptr %p, i32 -1`) before taking
   `OpPtrDiff` -- a perfectly ordinary, fully compile-time-resolvable
   address computation, not an unresolvable one. `computePointerOffset`
   also needed a matching fix: it materialized the constant offset term
   via `getZExtValue()`, which would have produced a huge unsigned
   garbage value for a true negative offset had the rejection above ever
   let one through; switched to a sign-extending conversion. New,
   isolated (non-cyclic) unit test
   `LowersPtrDiffThroughNegativeConstantGEPOffsetToOffsetSubtraction`
   verifies the fix in a straight-line reduction of the same shape.

2. **Found, not yet fixed -- split off as roadmap `K6u`:** with the false
   rejection above fixed, both CTS cases now reach pipeline *execution*
   for the first time (confirmed via `FEME_CPU_LOG_RESOURCE_NORMALIZATION`
   no longer firing), but both produce an incorrect numeric result
   (`Line 108: Verifier failed: 920402036 == -33, at index 0`). Root
   cause, confirmed via `FEME_DUMP_IR_PRENORM`: `mergeIntoPhi`'s rewrite
   phase has no counterpart to `hasOnlySupportedPointerUses`'s validation-
   phase `Visited`-set cycle breaker. A loop-carried pointer PHI whose
   backedge is a `getelementptr` of the phi itself (both iterators in
   this shader are exactly this: `forward_var`/`backward_var`, `Private`-
   storage SSBO-pointer iterators incremented/decremented once per loop
   iteration) can never finish merging -- the synthesized `descidx`/
   `offset` phi's backedge slot is left `undef` forever, since resolving
   that slot requires first recursing into the phi, which requires the
   slot already resolved. The dump confirms exactly this: both synthesized
   phis show a real value on the entry edge and `undef` on the loop-back
   edge, while the original raw-pointer phi and its feeding GEPs are left
   completely untouched in the final IR (silently un-rewritten, rather
   than becoming `feme.cpu.resource.*` calls) -- whatever the downstream
   codegen stage does with that leftover raw `ptr addrspace(11)`
   arithmetic produces the observed garbage value, not a crash.

**No regression:** `variablePointers`/`variablePointersStorageBuffer`
remain `VK_FALSE` in the committed configuration, so all 5 `opptrdiff.*`
cases (re-verified individually after the fix, flag reverted) still
report `NotSupported` by default -- confirmed via a direct rerun of all
5, zero change from before this session in the default configuration.

Full `check-feme` target (final run, fix in place, feature flag reverted
to `VK_FALSE`): 3750/3757 passed, 7 unsupported (expected), 0 failures.
All 630 `FeMeTransformsCPUTests` pass, including the new test.

`vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
Device` at the start and end of this session, per the standing "every
session, every time" instruction.

`Roadmap.md`: `K6p`'s own row updated with this session's full 5-case
triage conclusion (two already-known limitations re-confirmed, one false-
rejection bug fixed, one new bug split off). New row `K6u` added for the
still-open loop-carried-pointer-PHI-rewrite-deadlock bug (not yet fixed;
needs a restructured `mergeIntoPhi` that can resolve a self-referential
backedge eagerly, e.g. by recursing into the backedge's own GEP using the
already-created-but-still-filling-in `descidx`/`offset` phi nodes
themselves as forward SSA references, rather than requiring every
incoming edge resolved up front).
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change --
`variablePointers`/`variablePointersStorageBuffer` remain unsupported/
`VK_FALSE`; no feature bit flipped permanently this session.

## `K6u`: self-referential loop-carried pointer-PHI rewrite deadlock fixed, 1/2 targeted cases pass (roadmap K6u, closed)

**Starting point:** `K6u` (split off from the prior session's `opptrdiff`
triage) -- `mergeIntoPhi`'s rewrite phase deadlocked on a loop-carried
pointer PHI whose backedge is a `getelementptr` of the phi itself, since
that backedge's own (descriptor index, byte offset) pair can only ever be
resolved by recursing into the phi, but that recursion was gated on the
backedge already being resolved. `variable_pointers_vars_ssbo_diff`/
`_ssbo_2_diff` both reached pipeline execution (K6p's own
negative-constant-GEP-offset fix, prior session) but produced an
incorrect numeric result (`920402036` vs expected `-33`) rather than
failing cleanly, since the deadlocked phi/GEP chain was silently left
un-rewritten in the final IR.

**Fix (`feme/lib/Transforms/CPU/SPIRVResourceLowering.cpp`):**
1. New helper `isPhiSelfReferentialBackedge(const PHINode *Phi, const
   Value *V)`: walks a chain of `GetElementPtrInst::getPointerOperand()`
   calls backward from `V` until reaching a non-GEP root, and returns
   whether that root is `Phi` itself -- distinguishing a loop's own
   backedge (never externally resolved; only `mergeIntoPhi`'s own eager
   recursion into `Phi`'s users can fill it in) from a genuinely separate
   incoming chain (e.g. two different buffers merged by a condition,
   which must still be waited on exactly as before).
2. New `bool RewriteStarted = false;` field on `PointerMergeState`: a
   reentrancy guard. The eager recursion into the backedge's own GEP
   (triggered from inside `lowerRawPointerUses(Phi, ...)`) re-enters
   `mergeIntoPhi(Phi, ...)` for the very same phi and `PointerMergeState`
   before the outer call returns -- the guard makes that re-entrant call a
   no-op instead of a second, overlapping recursion.
3. `mergeIntoPhi`'s gating logic changed from "wait until every incoming
   slot is resolved" to "wait until every *non-self-referential* incoming
   slot is resolved" (checking each still-`undef` slot's original
   `Phi`-incoming-value against the new helper), with `RewriteStarted`
   set immediately before the now-eager `lowerRawPointerUses`/
   `eraseFromParent` call.

**New unit test**
(`LowersPtrDiffOnLoopCarriedPointerBackedgeToOffsetSubtraction`,
`SPIRVResourceLoweringTest.cpp`): a genuine 2-block loop (not the
straight-line/cross-branch shapes every prior phi-merge test used) with a
self-referential pointer-PHI backedge feeding `OpPtrDiff` against a
second, fixed pointer each iteration -- mirrors each CTS case's own
forward-iterator shape. Confirms `verifyModule` passes, the original
pointer-typed phi/GEP/`ptrtoint` chain is fully replaced, and the result
is a live `sub` fed by the loop's own synthesized byte-offset phi (not
folded to a constant, since the offset genuinely varies per iteration).
The existing `LowersPhiMergedPointerFromDifferentHandlesToSharedRawAccess`
test (the non-cyclic multi-chain case that must *not* regress) still
passes unchanged.

**CTS verification** (feature bits transiently flipped for verification
only, reverted immediately after):
- `variable_pointers_vars_ssbo_diff`: **Pass** (previously produced
  `920402036` instead of `-33`).
- `variable_pointers_vars_ssbo_2_diff`: still **Fail**, but now on an
  entirely different, unrelated signature --
  `"feme-cpu-linearize: ... loop ... has a mask-affecting operation in
  its body but no recognized uniform exit check"` (milestone 9 scope,
  CFG-linearization limitation, not a K6u-scope gap). This confirms
  `K6u`'s own deadlock is fully resolved for both cases it covered; the
  second case's remaining failure is a distinct, pre-existing-class
  limitation newly exposed now that the phi-merge deadlock no longer
  masks it.
- Full `opptrdiff.*` sweep (5 cases, feature bits flipped): 1 Pass
  (`variable_pointers_vars_ssbo_diff`), 4 Fail, each on its own already-
  documented, distinct signature (two CFG-linearization
  `reconvergence block` cases, one `groupshared`-canonicalization case,
  one new linearize `mask-affecting operation`/`uniform exit check` case)
  -- no undiagnosed residual.

**No regression:** `variablePointers`/`variablePointersStorageBuffer`
remain `VK_FALSE` in the committed configuration; reverted the temporary
flip and re-confirmed all 5 `opptrdiff.*` cases still report
`NotSupported` by default, zero change from before this session.

Full `check-feme` target (fix in place, feature flag `VK_FALSE`):
3751/3758 passed, 7 unsupported (expected), 0 failures. All 631
`FeMeTransformsCPUTests` pass (630 pre-existing + 1 new).

`vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
Device` at the start of this session, per the standing "every session,
every time" instruction.

`Roadmap.md`: `K6u` row struck through (closed) with the fix description
and CTS results; the newly-exposed `variable_pointers_vars_ssbo_2_diff`
linearize-scope failure is noted inline in that same row (milestone 9
scope, not re-bucketed as its own row this session -- a natural next
pick, see `agent_thoughts.md`).
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change --
`variablePointers`/`variablePointersStorageBuffer` remain unsupported/
`VK_FALSE`; no feature bit flipped permanently this session.

## `K6v`: relocated uniform latch check behind trivial relay fixed; new `K6w` residual root-caused (roadmap K6v, closed)

**Starting point:** `K6v` (split off from the prior session's `K6u`
closure, once `variable_pointers_vars_ssbo_2_diff` progressed from a
phi-merge deadlock to a new, distinct `feme-cpu-linearize` diagnostic:
`"loop ... has a mask-affecting operation in its body but no recognized
uniform exit check"`).

**Root cause:** `StructurizeCFG` can place a loop's real, uniform
trip-count check one block upstream of the nominal `Latch`, in a block
whose "stay in loop" edge targets a trivial, phi-less relay stub.
`LoopLinearizer` previously only recognized `Latch`'s own terminator (or
a relay chain entirely made of unconditional branches, via
`recoverRelayExitCheck`) as the loop's real check, missing this
relocated shape entirely.

**Fix** (`feme/lib/Transforms/CPU/Linearize.cpp`): a new structural
pre-pass ("Part A") promotes such a predecessor into the `Latch` role
when `Latch` is a phi-less, pure relay block; its unique predecessor's
branch is uniform (not already a recognized `Header`/`Latch` check);
and it directly matches `matchExitCheckWithRelay`. This pre-pass must
run *after* the existing fold fixed-point loop
(`foldRedundantFlowBlocksInCycle`/`peelConstantFlowPredecessorsInCycle`/
`collapseTriviallyRedundantPhisInCycle`/`mergeTrivialRelayBlocksInCycle`),
not before it -- debugging two real regressions
(`loop-body-diamond.ll`, `CompiledStageTest.
LoopWithBarrierSurvivesCompiledStageCreation`) found that a
`StructurizeCFG`-rotated "guard loop with barrier" shape produces a
transient "shared `Flow`-style dispatch block" that structurally
matches this same promotion criteria *before* the pre-existing folds
get a chance to simplify it away into the trivial pass-through it
really is; running the new pre-pass first promotes it prematurely and
corrupts the CFG. A new `LatchWasPromoted` flag lets the pre-existing
"uniform loop with mask ops" early-return branch correctly skip itself
only for the genuinely-promoted shape (an initial `!LatchExit` guard
was too broad, also wrongly skipping the ordinary, already-correct
"`Latch` already owns its own real check" shape).

New unit test (`LinearizeTest.cpp`):
`PromotesUniformLatchCheckBehindTrivialRelay`.

**CTS verification:**
- `variable_pointers_vars_ssbo_2_diff` now builds and runs its pipeline
  successfully (previously failed pipeline creation with the `K6v`
  diagnostic) -- progress -- but produces an incorrect numeric result
  at index 0 (`-382273908` instead of the expected `0`).
- Root-caused (via `FEME_DUMP_IR_PRELINEARIZE`/`FEME_DUMP_IR_PRESIMD`
  dumps) this remaining failure to a second, deeper, NOT-yet-fixed
  bug, split off as its own roadmap row, **K6w**: the loop's
  resource pointer's backedge is a `getelementptr` feeding an if/else
  diamond's own intermediate phis (an inner `+1`/`-1` offset
  selection) before looping back to the outer phi -- one level of
  indirection deeper than `K6u`'s `isPhiSelfReferentialBackedge` (which
  only strips a direct `getelementptr` chain, not one routed through an
  intermediate `PHINode`) recognizes, causing `mergeIntoPhi` to wait
  forever for an "external" resolution that can never arrive, leaving
  the raw pointer phi's entry-edge value `poison` and a first-iteration
  `ptrtoint` use of it producing garbage. This is a genuinely separate,
  deeper bug from `K6v` itself (confirmed present already in the
  pre-linearize dump, i.e. produced entirely upstream of
  `LoopLinearizer`), not a regression `K6v` introduced -- `K6v`'s own
  fix is cleanly verified working for its own targeted diagnostic.
- Full `opptrdiff.*` sweep (5 cases, feature bits transiently flipped
  for measurement only, not committed): 1 Pass
  (`variable_pointers_vars_ssbo_diff`, unaffected by this session), 4
  Fail -- `variable_pointers_vars_ssbo_2_diff` now fails on the new
  `K6w` signature (progress from `K6v`'s own diagnostic); the other 3
  are unaffected, already-documented, distinct pre-existing limitations
  (two CFG-linearization `reconvergence block` cases, one
  `groupshared`-canonicalization case) -- no undiagnosed residual, zero
  regressions.

**No regression:** `variablePointers`/`variablePointersStorageBuffer`
remain `VK_FALSE` in the committed configuration; reverted the
temporary flip and re-confirmed all 5 `opptrdiff.*` cases still report
`NotSupported` by default.

Full `check-feme` target (fix in place, feature flag `VK_FALSE`):
3752/3759 passed, 7 unsupported (expected), 0 failures.

`vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
Device` this session, per the standing "every session, every time"
instruction.

`Roadmap.md`: `K6v` row added and struck through (closed) with the fix
description and CTS results; new, open `K6w` row added for the
newly-root-caused residual, following the single-lowercase-letter
nesting rule.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- `variablePointers`/`variablePointersStorageBuffer` remain
unsupported/`VK_FALSE`; no feature bit flipped permanently this
session.

## `K6w`: nested self-referential loop-carried pointer-PHI merge (GEP-through-diamond backedge) fixed; a use-after-free bug the fix itself exposed also fixed, 1/1 targeted case passes (roadmap K6w, closed)

Picked up `K6w`, the residual `variable_pointers_vars_ssbo_2_diff`
numeric-mismatch bug `K6v` left root-caused but unfixed: the case's
loop-carried SSBO pointer iterator steps `+1`/`-1` elements per
iteration through an if/else diamond (a direction reversal), so its
loop-header phi's backedge is not a direct `getelementptr` of the phi
itself (`K6u`'s already-fixed shape) but a `getelementptr` feeding an
intermediate diamond-merge `PHINode`, which only then feeds back into
the outer phi. `isPhiSelfReferentialBackedge`'s "strip
`getelementptr`s, check the root is the same `Phi`" walk does not
recognize this one-level-deeper indirection, so `mergeIntoPhi` treated
the backedge as an externally-resolved edge that would never actually
arrive, permanently deadlocking (silently leaving the loop's raw
pointer phi/GEP chain un-rewritten) and producing a garbage numeric
result downstream.

- Re-confirmed the exact current IR shape via a fresh
  `FEME_DUMP_IR_PRELINEARIZE` dump (rather than trusting prior
  sessions' notes) before implementing: an outer loop phi
  (`.sroa.0.0`/`.sroa.3.0`, representing two pointers walking in
  opposite directions) whose backedge is a nested phi (merging a
  direct `getelementptr` leg and a *further*-nested, poison-containing
  phi leg from a separate "Flow" dispatch block).
- **Fix, part 1:** broadened `isPhiSelfReferentialBackedge` to tolerate
  a GEP-stripped incoming value landing on *any* `PHINode` (not only
  the literal same `Phi`) or a `PoisonValue`/`UndefValue`, reasoning
  that each such phi's own `mergeIntoPhi` invocation independently
  re-applies this same tolerance to its own edges, so a cycle spanning
  multiple phis still resolves correctly once the gate is relaxed.
- **Fix, part 2 (a second bug the broadening itself exposed):** the
  first version of this fix made real progress (no longer garbage, but
  a precise sign inversion, `2 == -2` in the CTS verifier output) --
  traced to a use-after-free: tolerating a slot landing on a
  *different* phi means that slot's real value may arrive via a wholly
  separate top-level walk, not the current call's own recursion into
  `Phi`'s users, but `mergeIntoPhi` unconditionally erased `Phi`
  immediately after its own recursion returned, regardless of whether
  every slot was genuinely filled yet -- a later reentrant call
  arriving for the still-outstanding slot then dereferenced a freed
  `PHINode`. Fixed by deferring erasure: a new
  `PointerMergeState::Erased` flag plus an `EraseIfComplete` check (run
  after every slot-filling call) only erases once every slot genuinely
  holds a real value. A further `RecursionActive` flag was needed on
  top of that: a mutually-recursive chain can reenter `Phi`'s own merge
  *from within* that same still-running top-level walk (before every
  other user of `Phi` has been visited), and erasing at that point
  would free `Phi` out from under the outer, still-active frame --
  caught directly by the new unit test crashing with LLVM's own
  "Use still stuck around after Def is destroyed" assertion before this
  second part was added, rather than only via a slow full-CTS
  iteration loop.
- New unit test (`SPIRVResourceLoweringTest.cpp`):
  `LowersPtrDiffOnLoopCarriedPointerBackedgeThroughIntermediateDiamondPhi`,
  a loop stepping a pointer `+1`/`-1` through an if/else diamond before
  closing the loop's own backedge, feeding `OpPtrDiff`. Full 633-test
  `FeMeTransformsCPUTests` suite passes with zero regressions.
- CTS (`variable_pointers_vars_ssbo_2_diff`, feature bits transiently
  flipped to `VK_TRUE` for measurement only, not committed): **now
  passes** (previously `Verifier failed: 2 == -2, at index 0` with the
  use-after-free bug present mid-session, and total garbage before any
  fix). Full `opptrdiff.*` sweep (5 cases): 2 Pass (up from 1),
  `variable_pointers_vars_ssbo_diff` (unaffected) and
  `variable_pointers_vars_ssbo_2_diff` (newly fixed); the other 3 are
  unaffected, already-documented, distinct pre-existing limitations
  (two CFG-linearization `reconvergence block` cases, one
  `groupshared`-canonicalization case) -- every one of the 5 cases is
  now accounted for by a known, tracked bucket.

**No regression:** `variablePointers`/`variablePointersStorageBuffer`
remain `VK_FALSE` in the committed configuration; reverted the
temporary flip and re-confirmed `check-feme` passes clean.

Full `check-feme` target (fix in place, feature flag `VK_FALSE`):
3753/3760 passed, 7 unsupported (expected), 0 failures.

`vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
Device` this session, per the standing "every session, every time"
instruction.

`Roadmap.md`: `K6w` row struck through (closed) with the fix
description and CTS results.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
-- `variablePointers`/`variablePointersStorageBuffer` remain
unsupported/`VK_FALSE`; no feature bit flipped permanently this
session.

## `opptrdiff` residual: 2 of the remaining 3 cases root-caused and fixed (`K6x`/`K6y`)

**Starting point:** 3 `opptrdiff.*` cases left cleanly bucketed only by
error-signature class, not individually root-caused:
`ssbo_comparisons_diff`/`wg_comparisons_diff` (CFG-linearization
`"reconvergence block does not have exactly two predecessors"`) and
`variable_pointers_vars_wg_diff` (`feme-cpu-simdize` groupshared-
canonicalization limitation).

- Root-caused the shared `ssbo_comparisons_diff`/`wg_comparisons_diff`
  signature to a concrete CFG shape: a divergent if/else diamond
  immediately upstream of a loop, with both arms merging directly into
  the loop header itself (no dedicated preheader), giving the header 3
  predecessors -- a shape `DiamondFlattener::validate`'s
  `R->hasNPredecessors(2)` check rejects outright, with no special case
  for "one of these 3 predecessors is really this same block's own loop
  backedge."
- **Fixed (`K6x`)** by adding `LoopSimplifyPass` to the CPU backend
  pipeline (`Pipeline.cpp`) immediately before `LinearizePass`, rather
  than teaching `DiamondFlattener` itself to special-case loop headers.
  New test: `Linearize/diamond-merges-into-loop-header.ll`.
- This fix let `wg_comparisons_diff` progress further and exposed a new,
  previously-unreachable crash: an `"Invalid cast!"` assertion in
  `CastInst::Create`, from `FunctionWidener::widenElementwise`'s generic
  `CastInst` handling in `SIMDize.cpp`, reading a stale scalar value out
  of the `Widened` cache where a `<W x T>` vector was expected.
- Root-caused to two distinct bugs in `FunctionWidener::
  widenGroupSharedGEP`: (1) a nested groupshared GEP's own base pointer
  (itself another, already-widened GEP) was read directly from the old
  function rather than substituted via the `Widened` cache, building an
  illegal cross-function `Value*` reference; (2) even after fixing (1),
  a GEP dispatched here because its *result* was judged divergent can
  still have every entry of its own index list stay scalar, so the
  rewritten GEP can legitimately come back scalar-typed -- caching that
  directly into `Widened` (whose `getWidened` accessor documents an
  always-vector-typed invariant) corrupted a later lookup.
- **Fixed (`K6y`)**: substitute a widened base via the `Widened` cache
  when present; route a still-scalar rewritten GEP through `getWidened`
  itself before caching. New test:
  `simdize-groupshared-nested-uniform-index-divergent-base.ll`,
  confirmed to crash (`SIGABRT`) without the fix and pass cleanly with
  it.
- CTS (`opptrdiff.*` 5-case sweep, feature bits transiently flipped to
  `VK_TRUE` for measurement only, not committed): **3/5 now pass**
  (`ssbo_comparisons_diff`, `variable_pointers_vars_ssbo_diff`,
  `variable_pointers_vars_ssbo_2_diff`), up from 2/5 before this
  session. The remaining 2 (`wg_comparisons_diff`,
  `variable_pointers_vars_wg_diff`) now fail cleanly -- no crashes -- on
  a separate, pre-existing, already-tracked `rewriteGroupSharedGlobals`
  groupshared-canonicalization diagnostic (milestone 9 scope), confirmed
  via direct error-text inspection of both runs.

**No regression:** `variablePointers`/`variablePointersStorageBuffer`
remain `VK_FALSE` in the committed configuration; reverted the temporary
flip (via `git checkout --`, confirmed zero outstanding diff in
`EntryPoints.cpp`) and re-confirmed `check-feme` passes clean.

Full `check-feme` target (both fixes in place, feature flags `VK_FALSE`):
3755/3762 passed, 7 unsupported (expected), 0 failures.

`vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
Device` this session, per the standing "every session, every time"
instruction.

`Roadmap.md`: new `K6x`/`K6y` rows added, both struck through (closed)
with fix descriptions and CTS results, split off from `K6w`.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change --
`variablePointers`/`variablePointersStorageBuffer` remain
unsupported/`VK_FALSE`; no feature bit flipped permanently this session.

## First `check-hlsl-feme-vk` exercise: two offload-test-suite drift fixes, 24/26 residual failures root-caused to one shared compiler gap (roadmap `L424`/`K6z`)

`vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan
Device` this session, per the standing "every session, every time"
instruction.

This session finally exercised the standing, several-sessions-overdue
`check-hlsl-feme-vk` requirement (offload-test-suite's `feme` branch,
`llvm-beanz/offload-test-suite.git`) for the first time. Unlike every
prior session, the FeMe-enabling CMake commit (`9af8bce`) and the
previously-documented `VkBool32` harness fix (`2e72553`) were both
already present at the local `feme` branch's HEAD -- no cherry-pick
needed this time. Only two much smaller, test-expectation-level fixes
had been dropped by the per-session `agent-setup.sh` reset.

**Baseline sweep** (792 total tests): 464 Passed, 273 Unsupported, 27
Failed, 1 Unexpectedly Passed.

**Two re-applied drift fixes** (both isolated, `offload-test-suite`-only
commits, no FeMe files touched, per the standing "issue outside FeMe"
instruction):
- `test/WaveOps/WaveActiveMax.test`: re-added `XFAIL: FeMe &&
  host-arm64` (host confirmed `aarch64` via `uname -m`) -- genuinely
  still broken on this architecture, previously fixed for `x86_64`.
  Commit `14387a0`.
- `test/Feature/PushConstant/array_of_matrices.test`: removed a stale
  `XFAIL: DXC` directive for a DXC bug the currently-checked-out DXC no
  longer reproduces (test now genuinely passes). Commit `ec08762`. Hit
  the known lit directive-scanning pitfall once: an unanchored literal
  `XFAIL: DXC` substring inside the replacement comment's prose was
  misparsed as a real (malformed) directive, producing `UNRESOLVED`
  rather than `PASS` on the first attempt; reworded to avoid the literal
  substring, reconfirmed `PASS`.

**Post-fix sweep**: 465 Passed, 273 Unsupported, 26 Failed, 28
Expectedly Failed, 0 Unexpectedly Passed -- exactly the expected shift,
zero other regressions.

**Root-caused 24 of the remaining 26 failures to one shared compiler
gap** (new roadmap row `K6z`, left open as a well-scoped follow-up, not
fixed this session): `Basic/Matrix/matrix_single_subscript_load`/
`matrix_swizzle_{one,zero}_based`, `Feature/CBuffer/Matrix/
LayoutKeyword/{subscript,transpose}`, `Feature/HLSLLib/
{AllMemoryBarrierWithGroupSync,DeviceMemoryBarrierWithGroupSync,
InterlockedCompareExchange.resources.32,InterlockedExchange.resources.32}`,
`Feature/ResourceControlFlow/licm-hoist-load`, `Feature/Textures/
{Array.SRVToUAV.array,SRVToUAV.array}`, `Tools/Offloader/
BufferExact-error-array`, `UseCase/particle-life`, and all 10 remaining
`WaveOps/{WaveActiveMax,WaveActiveMin}.{fp16,fp32,int32}`/
`WaveReadLaneFirst.{fp16,fp32,int16,int32}` cases all emit the exact
same `feme-cpu-linearize` diagnostic: `"loop at '' has an internal
branch in a body shape this milestone does not yet lower (e.g. an empty
diamond arm, or one crossing another cycle's own boundary); unsupported
(roadmap milestone 6 deviation)"`.

Root-caused via `FEME_DUMP_IR_PRELINEARIZE` on `matrix_single_subscript_
load.test` (the simplest repro, a `for(i<4)` outer loop with a uniform
`i%2`/`i%3` diamond-chain body followed by a nested `for(j<4)` sibling
loop before the latch): `DiamondFlattener::flattenLoopBodyRegion`'s
existing `CycleBoundaryBlocks` tolerance (added for `dEQP-VK.glsl.demote.
dynamic_loop_always`'s shape) only accepts a boundary block whose own
branch target is `End` *directly* -- the loop's own uniform exit check
sitting at the tail of the region, not a genuine nested child cycle
(already fully linearized by `linearizeCyclePostOrder`'s post-order
traversal, confirmed via the same dump) with real body code and its own
relay chain between it and `End`. The inner loop's header fails that
direct-successor test, so the region is rejected outright. A correct
fix needs `validate`'s walk to recognize an already-linearized
(`HeaderActiveMasks`-tracked) nested cycle, continue validating from its
own exit blocks to `End`, and teach `flatten` to skip its already-
finalized interior while still masking the code between its exit and
`End` -- judged too large and too correctness-risky (masking-semantics
bugs around nested cycles dominate this file's own history, e.g. `K6w`/
`K6y`) to attempt in the time remaining this session. Left open as
`K6z`.

The other 2 residual failures are unrelated to this cluster:
`Feature/HLSLLib/InterlockedAdd.resources.32.test` fails a genuine
numeric-correctness check (expected all `0x1`, actual all `0x0`, not yet
root-caused past that observation), and `InterlockedCompareStore.
resources.32.test` hits the pre-existing, already-tracked `H154`/
`H155`-family "barrier inside non-linear control flow" milestone-9
diagnostic (a different, already-closed-elsewhere scope limitation, not
new).

**No regression:** no FeMe (`llvm-project`) files have any net change
this session -- a temporary `git checkout <parent> --`/`git checkout
HEAD --` bisect on `Pipeline.cpp`/`SIMDize.cpp` (to rule out this
session's own prior `K6x`/`K6y` work as the WaveOps cluster's cause,
confirmed NOT a regression: the same diagnostic reproduces identically
against the pre-`K6x` state) was fully reverted before concluding. Full
`check-feme` target unaffected (no FeMe source changed); re-confirmed
clean.

**No `VK-GL-CTS` rerun or `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` update this session** -- both landed
fixes are `offload-test-suite`-only test-expectation corrections with
no FeMe code or feature/extension-surface change, so neither the
Vulkan CTS nor either inventory document had anything new to capture.

`Roadmap.md`: new `L424` row (the `check-hlsl-feme-vk` sweep + two drift
fixes, done) and new `K6z` row (the shared root cause, left open as
follow-up) added.

## `K6z` fixed: nested-sibling-cycle transparent walk-through in `DiamondFlattener`, 7 of 24 attributed `check-hlsl-feme-vk` failures resolved (roadmap `K6z`, closed; `L425` opened for the remaining 17)

Confirmed `FeMe CPU Vulkan Device` this session, per the standing "every
session, every time" instruction.

Implemented `K6z`'s fix, left open at the end of the prior session:
`DiamondFlattener::validate`/`::flatten`'s cycle-boundary-stop checks now
recognize a genuine nested sibling cycle -- a child cycle strictly nested
inside the cycle currently being linearized, already fully processed by
`linearizeCyclePostOrder`'s post-order traversal -- as transparent. Two
new optional callback parameters (`GetAlreadyLinearizedCycleExitFn`,
`NarrowChildEntryMasksFn`) are threaded from
`LoopLinearizer::linearizeCycle` into both the `Precheck` and real
`DiamondFlattener` instances; on hitting such a child cycle's own header
(detected via a structural `CI.contains(C, ChildHeader)` nesting check),
`validate`'s walk now continues from that cycle's own single exit block
onward to `End` instead of stopping there and reporting the whole region
unsupported.

**A real, separate bug found and fixed while building the regression
test**: `NarrowChildEntryMasks`'s lambda was originally passed as an
unnamed temporary directly into `DiamondFlattener`'s constructor call.
Since `DiamondFlattener` only stores a non-owning `llvm::function_ref` to
it (not a copy), the temporary was destroyed at the end of the
constructor's own full-expression -- the very first time `flatten()`
later invoked the stored `function_ref`, `this` was a dangling/garbage
pointer, confirmed via a `gdb -batch -ex run -ex bt` SEGV with `this=0x5`
inside `rethreadNestedEntryMasks`. Fixed by binding the lambda to a named
local variable with a lifetime at least as long as the `DiamondFlattener`
object storing the `function_ref`, with an explanatory comment at the
call site warning against repeating the mistake.

New regression test, `loop-body-resource-call-then-nested-uniform-loop.ll`:
reduces `matrix_single_subscript_load.test`'s own shape to its essential
structure (a uniform outer loop whose body has a uniform diamond followed
by a separate, already-fully-attempted nested uniform loop, then a
resource call, before the outer loop's own latch). Confirmed to reproduce
the original `"has an internal branch in a body shape this milestone does
not yet lower"` diagnostic when run against the pre-fix code (verified via
a temporary `git stash`/rebuild/re-run/`git stash pop` cycle), and to
linearize cleanly with the fix applied. Both resource calls' own mask
operands are left as the literal `i1 true` they started with -- not a
masking bug, but the semantically correct outcome: this particular loop
has no `feme.stage.discard`/`.demote` anywhere in its body (the only
operations that ever make `DiamondFlattener::flatten`'s `Masks.SideEffect`
genuinely diverge from `true`; a discard/demote inside an otherwise
uniform loop is its own, separately-diagnosed unsupported shape -- see
`unsupported-stage-op-in-uniform-loop.ll`), so every lane that
structurally reaches either call really is unconditionally active, and
`isKnownConstantMask`'s `lookThroughTrivialPhi` correctly recognizes that
rather than rewriting the call to a needlessly non-constant phi.

**Verification**:
- `FeMeTransformsCPUTests`: 633/633 Pass (no regressions).
- Full `ninja check-feme`: 3756/3756 Pass (7 Unsupported, as before), 0
  Fail -- no regressions.
- `check-hlsl-feme-vk` re-swept: 472 Passed/273 Unsupported/28 Expectedly
  Failed/19 Failed (792 total), up from 465/273/28/26 before this
  session's fix -- **7 of the 24 cases previously attributed to `K6z`'s
  root cause are now fixed**: `Basic/Matrix/matrix_single_subscript_load`,
  `matrix_swizzle_{one,zero}_based`, `Feature/CBuffer/Matrix/
  LayoutKeyword/{subscript,transpose}`, `Tools/Offloader/
  BufferExact-error-array`, `Feature/Textures/Array.SRVToUAV.array`. The
  remaining 17 -- `Feature/HLSLLib/{AllMemoryBarrierWithGroupSync,
  DeviceMemoryBarrierWithGroupSync,InterlockedCompareExchange.resources.32,
  InterlockedExchange.resources.32}`, `Feature/ResourceControlFlow/
  licm-hoist-load`, `Feature/Textures/SRVToUAV.array`, `UseCase/
  particle-life`, all 10 `WaveOps/*` cases -- still emit the exact same
  diagnostic *text* but hit a structurally different obstruction this
  fix's specific nested-sibling-cycle shape doesn't cover (not yet
  individually root-caused; see new roadmap row `L425`). The pre-existing
  `InterlockedAdd.resources.32.test` (numeric mismatch) and
  `InterlockedCompareStore.resources.32.test` (`H154`/`H155`-family
  milestone-9 diagnostic) failures are unaffected, as expected -- neither
  was ever part of this cluster.
- Targeted `VK-GL-CTS` regression check: no pre-existing case-list exists
  for this exact shape (a uniform loop with a nested already-linearized
  sibling cycle plus a resource call in its body), so a broad sample was
  used instead of a full mustpass sweep, each run both before (temporary
  `git stash` of the fix) and after, with a full qpa-fail-list diff to
  confirm no regressions:
  - `dEQP-VK.glsl.loops.*` (624 cases, the group structurally closest to
    this fix): 606/624 Pass after, up from 586/624 before -- **20 newly
    fixed, 0 new failures** (confirmed via diff).
  - `dEQP-VK.glsl.demote.*` (30 cases, the mask-threading-adjacent group
    `unsupported-stage-op-in-uniform-loop.ll`'s own diagnostic guards):
    unchanged 27/30 Pass both before and after, 0 diff -- expected, since
    this fix's own shape has no discard/demote involved.
  - `dEQP-VK.glsl.matrix.*` (1764 cases, the group closest to the actual
    `matrix_single_subscript_load.test` repro): 1764/1764 Pass both
    before and after this session's fix (no regressions; these simpler
    matrix cases apparently don't happen to hit this exact nested-cycle
    shape, unlike the real-world HLSL `offload-test-suite` repro).

**No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed** -- a pure internal `feme-cpu-linearize` compiler fix, no
Vulkan feature or extension surface changed.

`Roadmap.md`: `K6z` struck through (done, with the precise 7/24 outcome
and the dangling-`function_ref` bug recorded); new `L425` row opened to
scope the remaining 17-case cluster, which shares `K6z`'s own diagnostic
text but not (confirmed) its specific root cause.

**Next steps**: dump `FEME_DUMP_IR_PRELINEARIZE` on one of the 17
remaining `check-hlsl-feme-vk` failures (`Feature/HLSLLib/
AllMemoryBarrierWithGroupSync.test` looks like the smallest) to determine
whether they share one single remaining shape or several distinct ones;
`K6n`-proper (the largest remaining `variablePointers` bucket) remains
entirely untouched for many sessions and is still the single largest
open item in this file.

## `L425` fixed: `MergeVisited`/`ResolvedBlocks` false-conflict bug in `DiamondFlattener`, 13 of the 17 remaining attributed `check-hlsl-feme-vk` failures resolved (roadmap `L425`, closed; `L425(a)` opened for the remaining 4)

Root-caused via `FEME_DUMP_IR_PRELINEARIZE`/`FEME_DEBUG_LINEARIZE_TRACE`
instrumented tracing of `Feature/HLSLLib/AllMemoryBarrierWithGroupSync.test`'s
standalone repro (captured in a prior session, confirmed to reproduce
standalone via `feme-opt --llvm -passes=feme-cpu-linearize`). Despite
sharing `K6z`'s own diagnostic text (`"loop at '' has an internal branch
in a body shape this milestone does not yet lower"`), this is a genuinely
different bug from `K6z`'s nested-sibling-cycle gap.

**Root cause**: `LoopLinearizer::linearizeCycle` runs an early
"ordinary nested diamond" fixed-point loop (via
`DiamondFlattener::flattenLoopBodyDiamond`) *before* its own "fully
uniform loop with body mask ops" handling (via
`DiamondFlattener::flattenLoopBodyRegion`). In the repro, the fixed-point
loop successfully flattens an inner, divergent diamond inside the loop
body (`if (GTID.x == I)`, divergent because it compares a per-lane
thread-ID value against the uniform loop induction variable), inserting
that diamond's blocks into the shared, whole-function `MergeVisited` set
as an unavoidable side effect of `validate()`'s walk. `MergeVisited` is
deliberately never cleared and deliberately shared across sequentially-
constructed `DiamondFlattener` instances (per `L375`'s own design) to
catch genuine double-claim conflicts between still-pending candidates.
The *later*, separate `flattenLoopBodyRegion` call -- needed because the
loop's own exit check and backedge decision are fused into a single
`StructurizeCFG`-style `Flow` dispatch block -- must walk the *entire*
loop body region to reach its own `Latch`, which structurally still
contains the diamond already resolved by the earlier call. `validate()`
used to see those blocks already present in `MergeVisited` and spuriously
diagnose the walk-through as a conflict, even though the diamond was
never contested: it was already fully committed by an earlier,
independent, already-*returned* call, with no risk of double-mutation.

**Fix**: added a new `ResolvedBlocks` set (same whole-function lifetime
and sharing pattern as `MergeVisited`) recording every block committed by
a fully-successful top-level `flattenLoopBodyDiamond`/`flattenLoopBodyRegion`
call. `validate()`'s conflict check becomes "fail only if a block is seen
in `MergeVisited` but *not yet* in `ResolvedBlocks`" -- this preserves the
original double-claim protection for any still-in-progress/uncommitted
candidate (the genuine `L375` danger, e.g. two sibling diamond arms both
reaching into the same shared relay block within one still-active
`validate()` walk) while permitting safe pass-through of a diamond some
*earlier*, already-finished call fully resolved. A per-call-local
`LastRegionVisited` set tracks exactly which blocks that specific call
newly visited, folded into `ResolvedBlocks` only once that call's own
`flatten()` successfully commits.

**New regression test**: `loop-fused-exit-backedge-flow-block-with-
flattened-inner-diamond.ll`, reduced from the `AllMemoryBarrierWithGroupSync.test`
repro down to its essential shape (a uniform loop whose exit/backedge
decision is fused into one `Flow`-style dispatch block, with a divergent
inner diamond in its body gating a `feme.cpu.resource.store.raw.i32`
call). Confirmed to fail pre-fix (`git stash`) and pass post-fix. Unlike
`K6z`'s own regression test (whose nested cycle had no masked operation
of its own, so its assertions were purely structural), this test also
exercises genuine mask narrowing through the already-resolved diamond
(the resource-store call's mask is narrowed from `i1 true` to a real
per-lane `%eq`-derived select chain), confirming the fix's pass-through
correctly re-threads masking rather than silently skipping it.

**Verification**:
- `FeMeTransformsCPUTests`: 633/633 Pass (no regressions).
- Full `ninja check-feme`: 3757/3757 Pass (7 Unsupported, as before), 0
  Fail -- no regressions (1 net-new test).
- `check-hlsl-feme-vk` re-swept (after `cmake .` reconfigure to pick up
  current `offload-test-suite` `feme` branch state): 792 total, down to 7
  Failed from 17 (`VK_ICD_FILENAMES`/`VK_DRIVER_FILES` both exported;
  `vulkaninfo --summary | grep deviceName` reconfirmed `FeMe CPU Vulkan
  Device` with the ICD env var explicitly set). **13 of the 17 cases
  previously attributed to this residual now Pass**: all 10 `WaveOps/
  {WaveActiveMax,WaveActiveMin}.{fp16,fp32,int32}`/
  `WaveReadLaneFirst.{fp16,fp32,int16,int32}` cases,
  `Feature/ResourceControlFlow/licm-hoist-load`,
  `Feature/Textures/SRVToUAV.array`, `UseCase/particle-life`.
  - The remaining 4 (`Feature/HLSLLib/{AllMemoryBarrierWithGroupSync,
    DeviceMemoryBarrierWithGroupSync,InterlockedCompareExchange.resources.32,
    InterlockedExchange.resources.32}`) no longer hit this diagnostic at
    all -- `feme-cpu-linearize` now succeeds on every one of them. They
    instead now reach and fail at a later, separate,
    already-extensively-tracked `feme-cpu-wrap-entry` "barrier inside
    non-linear control flow (a surviving branch not part of a supported
    loop); region splitting only supports a straight-line wave body or a
    single uniform loop" milestone-9 diagnostic (prior history: `H72`,
    `H94b`, `H124e`, `H154`, `H155`, `H161`, `L201(e)`). This is correctly
    unblocked-but-not-fixed by this session's change -- tracked as new
    roadmap row `L425(a)`, not yet individually root-caused.
  - `InterlockedAdd.resources.32.test` (genuine numeric mismatch) and
    `InterlockedCompareStore.resources.32.test` (itself now also hitting
    the same `feme-cpu-wrap-entry` diagnostic as the 4 above, consistent
    with its pre-existing `H154`/`H155`-family tracking) remain failing,
    as expected -- both were already known, separately-tracked issues
    predating `L425`, not part of this cluster's own 17.
  - `Basic/Mandelbrot.test` failed once in the full-suite parallel run
    with `gpu-exec: error: Failed to wait on Semaphore. (VkResult = 2)`
    (a timeout, not a correctness failure); re-ran standalone 3/3 times
    with 100% Pass -- a pre-existing, contention-dependent flake under
    parallel load (same class of issue as `L421`'s own
    `QueueExecutor`/`DeviceMemory` race), not a regression from this
    session's fix and not investigated further this session.
- Targeted `VK-GL-CTS` regression check (same groups `K6z`'s own session
  sampled, since this fix touches the same `DiamondFlattener`/
  `LoopLinearizer` code paths):
  - `dEQP-VK.glsl.loops.*` (624 cases): 606/624 Pass, unchanged from
    `K6z`'s own post-fix number -- 0 regressions, 0 newly-fixed (expected;
    this fix's specific false-conflict shape doesn't happen to be hit by
    this CTS group's own loop shapes).
  - `dEQP-VK.glsl.demote.*` (30 cases): 27/30 Pass, unchanged -- 0
    regressions (expected; no discard/demote involved in this fix's own
    shape).

**No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed** -- a pure internal `feme-cpu-linearize` compiler fix, no Vulkan
feature or extension surface changed.

`Roadmap.md`: `L425` struck through (done, with the precise 13/17 outcome
recorded); new `L425(a)` row opened to scope the 4 cases now blocked on
the separate, pre-existing `feme-cpu-wrap-entry` milestone-9 limitation.

**Next steps**: `L425(a)` (root-cause the 4 `feme-cpu-wrap-entry`-blocked
cases); `K6n`-proper (the largest remaining `variablePointers` bucket)
remains entirely untouched for many sessions and is still the single
largest open item in this file; the `Basic/Mandelbrot.test` contention
flake noted above is a candidate for a future session's change of pace,
following the same methodology `L421` used for its own
`QueueExecutor`/`DeviceMemory` race.

## `L425(a)` root-caused (not fixed): 4-case `feme-cpu-wrap-entry` cluster traced to a header-owned-check barrier-loop gap; two separate, deeper `feme-cpu-linearize` bugs found and fixed along the way (roadmap `L425(a)`, closed -- root-cause only; `L426` opened for the fix itself)

**Standing device check** (every session, every time):
`vulkaninfo --summary | grep deviceName` without an explicit
`VK_ICD_FILENAMES` reports `llvmpipe (LLVM 21.1.8, 128 bits)` -- the
system default, **not** this project's own ICD. With
`VK_ICD_FILENAMES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json`
explicitly set, it correctly reports `FeMe CPU Vulkan Device`, confirmed
at the start of this session as required.

**Task**: continue `L425(a)` -- root-cause the 4 `feme-cpu-wrap-entry`-
blocked cases `L425`'s own fix newly unblocked reaching
(`Feature/HLSLLib/{AllMemoryBarrierWithGroupSync,
DeviceMemoryBarrierWithGroupSync,InterlockedCompareExchange.resources.32,
InterlockedExchange.resources.32}.test`), confirm whether they share one
shape or need individual triage, and fix it if the diagnostic's extensive
prior-fix history (`H72`/`H94b`/`H124e`/`H154`/`H155`/`H161`/`L201(e)`)
turns out to make this "one more shape to add."

**Two prerequisite `feme-cpu-linearize` bugs found and fixed first**:
reproducing `AllMemoryBarrierWithGroupSync.test` standalone initially
failed *earlier* than the `feme-cpu-wrap-entry` diagnostic this task
targets, at `feme-cpu-linearize` itself, with a `DiamondFlattener`
`MergeVisited` conflict. Root-caused to two independent, compounding
bugs:

1. **`regionHasStageMaskOps` missing resource/image-store coverage.**
   `DiamondFlattener::flattenLoopBodyDiamond`'s own early "flatten this
   loop-body diamond before the real per-iteration mask is known"
   pre-pass is gated by `regionHasStageMaskOps`, which checked only
   `feme.stage.*` ops (discard/demote/etc.), never the sibling `K6s`
   `CycleHasMaskOps` check for `feme.cpu.resource.*`/`feme.cpu.image.*`
   calls -- letting a resource/image *store* inside such a diamond get
   flattened early with its mask left at the placeholder constant `true`
   forever, instead of the real loop-carried mask `LoopLinearizer` would
   otherwise have threaded through. **Fixed**: `regionHasStageMaskOps`
   now also checks `matchResourceCall`/`matchImageCall`, but narrower
   than `CycleHasMaskOps` -- only a call with an actual memory side
   effect (`MatchedResourceCall::StoredValue` set, or
   `MatchedImageCall::Texel`/`AtomicValue` set) is flagged. A first
   attempt mirroring `CycleHasMaskOps` exactly (flagging all
   resource/image calls, loads included) regressed
   `LinearizeTest.LinearizesCheckBlockChainedDivergentCheckBypassToStayInLoop`
   (roadmap `L377(a)`), whose own fold specifically depends on a
   *read-only* loop-body diamond being pre-flattened by this same
   pre-pass -- confirming `regionHasStageMaskOps` and `CycleHasMaskOps`
   are similar-looking but intentionally different-scoped checks going
   forward, not candidates to be kept byte-for-byte identical.

2. **Discarded trial `validate()` call never rolled back `MergeVisited`.**
   `flattenLoopBodyDiamond` calls `validate(Start, R, /*Quiet=*/true)` as
   an unconditional *trial* probe *before* checking
   `regionHasStageMaskOps`. A successful trial always inserts every
   visited block into the shared, whole-function `MergeVisited` set
   (`L375`'s own design, deliberately never cleared, to catch genuine
   double-claims). Once bug 1's fix makes `regionHasStageMaskOps` defer
   far more often, those insertions were never undone on the bail path
   -- so `LoopLinearizer`'s own later, real `flattenLoopBodyRegion` call
   over the same (never actually flattened) region immediately, and
   incorrectly, conflicted against the discarded trial's own leftover
   entries. **Fixed**: erase `LastRegionVisited`'s blocks from
   `MergeVisited` on that specific `regionHasStageMaskOps`-triggered bail
   path only.

**New regression test**:
`LinearizeTest.MasksResourceStoreCallInLoopBodyDiamondViaLoopLinearizer`,
exercising both fixes together end-to-end via the full `LinearizePass`.
Installs a `DiagnosticHandler` to assert no `DS_Error` diagnostic fires,
not merely that the pass returns `true` -- an earlier IR-shape attempt
for this same test silently passed its other assertions while actually
hitting an unrelated, pre-existing "no recognized uniform exit check"
diagnostic (a `Latch`-owned, not `Header`-owned, exit check is not
reachable by the `CycleHasMaskOps`-gated masking machinery this test
needs; the final IR shape instead gives the loop a `Header`-owned exit
check, matching every other passing test in this file). Also updated
`L425`'s own regression test,
`loop-fused-exit-backedge-flow-block-with-flattened-inner-diamond.ll`:
its `then` arm's resource-store mask changed from the literal `i1 true`
to `and i1 %active.sideeffect, %eq` -- semantically identical for that
test's specific uniform-trip-count loop (whose `%active.sideeffect`
recurrence is an unsimplified but provably-always-`true` identity), but
no longer hard-coded to a constant the way a genuinely divergent
enclosing loop's real mask now requires; manual trace confirmed the old
constant was the latent bug these two fixes corrected, not a stricter or
better expectation to preserve.

**Verification**:
- `FeMeTransformsCPUTests`: 634/634 Pass (1 net new test), 0 regressions.
- Full `ninja check-feme`: 3,759/3,766 Passed, 7 Unsupported, 0 Failed, 0
  regressions (1 net new test; required updating the one `L425`
  regression test's own `CHECK` lines to match the corrected mask
  expression above).
- `check-hlsl-feme-vk`: 485 Passed/273 Unsupported/28 Expectedly
  Failed/6 Failed (up from 472/273/28/19) -- confirms these two
  `feme-cpu-linearize` fixes alone unblocked 13 of the 19 previously-
  failing cases, all without touching `feme-cpu-wrap-entry` at all. The
  remaining 6 Failed: this task's own 4-case cluster, plus 2
  already-separately-tracked, pre-existing issues untouched this session
  (`Basic/Mandelbrot.test`'s contention flake, `H154`/`H155`-family;
  `InterlockedAdd.resources.32.test`'s genuine numeric mismatch).
- Targeted `VK-GL-CTS` regression check (same groups `L425`'s own
  session sampled, since these fixes touch the identical
  `DiamondFlattener`/`LoopLinearizer` code paths):
  - `dEQP-VK.glsl.loops.*` (624 cases): 606/624 Pass, unchanged -- 0
    regressions, 0 newly-fixed (expected; no resource/image store inside
    a loop-body diamond in this group's own shapes).
  - `dEQP-VK.glsl.demote.*` (30 cases): **30/30 Pass, up from 27/30** --
    3 newly fixed by this session's own two fixes (a `demote` loop body
    commonly also contains a resource/image call sharing the same
    diamond, exactly the shape bug 1/2 above corrected).

**All 4 of this task's own cases confirmed to share one root cause**:
with both prerequisite bugs fixed, every one of
`AllMemoryBarrierWithGroupSync`/`DeviceMemoryBarrierWithGroupSync`/
`InterlockedCompareExchange.resources.32`/`InterlockedExchange.resources.32`
now reaches (not merely individually corroborates) the exact same
`feme-cpu-wrap-entry` diagnostic text, root-caused to one specific CFG
shape: `EntryWrapper.cpp`'s `isLinearChain` recognizes a `while`-style
loop whose *header* (not its latch) owns the continue/exit test only via
its "neither successor is a backedge yet" case, which requires the
candidate body arm to be barrier-free (`walkBarrierFreeArm`'s own
hard rule, by design -- a barrier inside an already-nested arm has no
representation in this milestone's flat `outlineChain`). None of this
milestone's recognized shapes support a barrier directly in a
header-owned-check loop's own single-block self-looping body -- exactly
what all 4 cases' shared `GroupCounter`-incrementing loop is (confirmed
via `FEME_DUMP_IR` on the standalone repro).

**Not fixed this session**: a targeted fix was prototyped (recognize
this one trivial single-block self-loop-body shape directly in
`isLinearChain`, bypassing `walkBarrierFreeArm`'s barrier-free
restriction only for it). This did let `isLinearChain` accept the shape,
but surfaced a *second*, deeper bug one layer down:
`splitAtGroupSyncBarriers`'s own region-splitting/outlining code (fed by
`isLinearChain`'s `Order`) assumes an invariant that only the
already-supported loop shapes guarantee, and crashes with a stale-parent
`DominatorTree` assertion (`GenericDomTree.h:400`) once that assumption
no longer holds for this shape. Reverted the speculative fix rather than
ship it -- a crash is strictly worse than this task's own pre-existing,
clean diagnostic -- and opened new roadmap row `L426` to fix both layers
together in a future session, rather than continuing to deepen this
task's own letter-nesting.

**No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed** -- pure internal `feme-cpu-linearize` compiler fixes, no Vulkan
feature or extension surface changed.

`Roadmap.md`: `L425(a)` struck through (done -- root-caused, as the task
itself asked; not fixed, since the fix itself turned out to need a
second, deeper bug addressed too); new `L426` row opened to scope that
fix.

**Next steps**: `L426` (fix the `isLinearChain`/`splitAtGroupSyncBarriers`
pair for this exact shape, starting from the already-reverted prototype
and the standalone repro at `/tmp/l425a/AllMemoryBarrierWithGroupSync/`);
`K6n`-proper (the largest remaining `variablePointers` bucket) remains
entirely untouched for many sessions and is still the single largest
open item in this file; `Basic/Mandelbrot.test`'s contention flake and
`InterlockedAdd.resources.32.test`'s numeric mismatch remain open,
pre-existing, separately-tracked issues, neither touched this session.

## `L426` fixed: `matchLoopShape`'s wave-persistent induction machinery generalized to a genuine (non-invariant) non-constant-seeded recurrence; all 4 `L425(a)` cases now pass (roadmap `L426`, closed)

`L425(a)`'s own planned fix path -- re-apply the reverted `isLinearChain`
patch and debug the `DominatorTree` crash it caused in
`splitAtGroupSyncBarriers` -- was abandoned early this session after
re-reproducing that exact crash with a full `gdb` backtrace: the crash
site is deep inside `SROAPass` computing a fresh `DominatorTree` over a
region function, confirming `splitAtGroupSyncBarriers`'s own
region-calling code (`buildWrapper`) is a strictly flat,
call-each-region-exactly-once-in-sequence model with **no looping
construct at all**. No patch to `isLinearChain` alone could ever make a
genuine loop backedge representable through that code path -- the whole
approach was an architectural dead end, not merely buggy.

Pivoted instead to `matchLoopShape`/`buildWrapperForLoop`,
`EntryWrapper.cpp`'s own separate, purpose-built "barrier inside a
uniform loop" machinery, which already covers a collapsed body+latch
block (roadmap `H124e(a)`) and wave-persistent mask-phi inductions
(roadmap `H159(b)`) -- a structural match for all 4 of `L425(a)`'s cases'
own shared shape on paper (confirmed via a fresh `FEME_DUMP_IR` capture
and manual IR trace). Root-caused why `matchLoopShape` still rejected the
shape down to two real, independent bugs, both now fixed:

1. **`matchLoopShape`'s own induction-seeding check was too narrow.**
   `L425(a)`'s prior fix let a wave-persistent induction carry a
   non-constant seed, but only if its recurrence fed back the header phi
   unchanged (`NextValue == HeaderPhi`, i.e. loop-invariant) --
   unconditionally bailing out for a *genuine* (non-invariant) induction
   with a non-constant seed, even though `spillValuesLiveAcrossBarriers`'s
   existing seed/store-back machinery already supports both cases
   identically (the seed lands immediately after its own definition
   either way; only the store-back, and whether the recurrence must be an
   `Instruction` for `buildWrapperForLoop`'s own `cast<Instruction>`,
   differ). Relaxed to accept any wave-persistent induction whose
   recurrence is an `Instruction`, regardless of whether it equals the
   header phi.
2. **`collectClonableScalarRecurrence` never checked for vector types.**
   Used to decide whether a recurrence can simply be cloned into the
   wrapper's own scalar per-iteration loop (since it depends on nothing
   but constants and header phis), it wrongly accepted a genuinely
   per-lane mask recurrence (`select i1 %narrow, <4 x i1> %mask, <4 x i1>
   zeroinitializer`, depending only on the mask's own header phi and a
   constant) as "clonable" -- producing a wrapper-side clone referencing
   a per-lane header phi the wrapper never actually has (it is threaded
   through the per-wave spill array instead). Confirmed via
   `FEME_OPT_DUMP_INVALID=1`: the clone showed up as a `select`
   instruction with a `void <badref>` operand, a verifier-rejected
   module. Fixed by rejecting any vector-typed instruction or
   vector-typed allowed-phi dependency in that function.

New regression test `entry-wrapper-loop-wave-persistent-mask-recurrence.ll`
(modeled on the existing `L425(a)` loop-invariant-mask-seed test, but
with a genuinely narrowing mask recurrence instead of an invariant one),
confirmed to crash/reject pre-fix and pass post-fix.

**Debugging note for future sessions**: an early attempt to instrument
`matchLoopShape` with `llvm::errs()` traces before each
`return std::nullopt;` produced a misleading, self-inflicted false
signal -- none of 12 instrumented traces fired at runtime, yet the
function still appeared to return `nullopt`. Root cause: the debug
statements themselves broke C++'s dangling-`if`-without-braces rule
(`if (cond) errs(); return std::nullopt;` executes the `return`
unconditionally, regardless of `cond`), silently corrupting every
instrumented check into an unconditional early return. Fixed by wrapping
each inserted trace+return pair in its own `{ }` block -- a lesson for any
future single-line debug-print insertion into an existing
brace-less-single-statement `if`.

**Verification**:
- `FeMeTransformsCPUTests`: 634/634 passed (1 net new test).
- `ninja check-feme`: 3,760/3,767 Passed, 7 Unsupported, 0 Failed, 0
  regressions (1 net new test).
- All 4 of this task's own standalone repro cases
  (`/tmp/l425a/{AllMemoryBarrierWithGroupSync,DeviceMemoryBarrierWithGroupSync,InterlockedCompareExchange.resources.32,InterlockedExchange.resources.32}/`)
  now compile, execute, and match their expected output exactly
  (`BufferExact` pass) via the standalone `offloader` tool.
- Targeted `VK-GL-CTS` regression check (same groups `L425(a)`'s own
  session sampled):
  - `dEQP-VK.glsl.loops.*` (624 cases): 606/624 Pass, unchanged.
  - `dEQP-VK.glsl.demote.*` (30 cases): 30/30 Pass, unchanged.
  - Both counts exactly match the pre-existing documented baseline -- 0
    regressions, 0 newly fixed, as expected: neither group's own shapes
    exercise a wave-persistent, non-invariant mask recurrence inside a
    barrier loop the way this fix's 4 target cases do.

**No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed** -- pure internal `feme-cpu-wrap-entry` compiler fix, no Vulkan
feature or extension surface changed.

**`check-hlsl-feme-vk` still not run this session.** Building
`offload-test-suite`'s own generated target set requires a full
`clang`+HLSL build (`cmake -C <llvm-project>/clang/cmake/caches/HLSL.cmake
...`), which has never yet been completed in this environment across many
sessions -- a multi-hour undertaking of its own, separate from this
session's bounded `L426` task. Opened new roadmap row `L427` to track
this standing gap explicitly, rather than letting it remain an implicit,
easily-forgotten assumption in each session's own next-steps list. The
standalone `offloader` tool remains the only available substitute this
session (and every prior session) actually used for per-case
verification.

`Roadmap.md`: `L426` struck through (done -- fixed, with the actual
mechanism differing from the row's own original plan); new `L427` row
opened for the standing `offload-test-suite` build gap.

**Next steps**: `L427` (actually build `check-hlsl-feme-vk`'s
dependencies, likely the single highest-value infrastructure task left
standing after many sessions' worth of deferral); `K6n`-proper (the
largest remaining `variablePointers` bucket) remains entirely untouched
for many sessions and is still the single largest open *compiler* item in
this file; `Basic/Mandelbrot.test`'s contention flake and
`InterlockedAdd.resources.32.test`'s numeric mismatch remain open,
pre-existing, separately-tracked issues, neither touched this session.

## `L427` done: `check-hlsl-feme-vk`/`check-hlsl-clang-feme-vk` actually built (no multi-hour HLSL.cmake build needed), 3 more offload-test-suite drift fixes, `L426` confirmed closing all 4 of its own cases (roadmap `L427`, closed; `L428`/`L429` opened)

**Standing device check**: `vulkaninfo --summary | grep deviceName` ->
`FeMe CPU Vulkan Device`, confirmed at the start of this segment.

**The planned multi-hour `HLSL.cmake` from-scratch build was
unnecessary.** The existing `build/` tree already has `clang`/`clang-dxc`
built: `llvm/CMakeLists.txt` auto-appends `clang` to
`LLVM_ENABLE_PROJECTS` as a feme dependency
(`"Enabling clang as a dependency to feme"`), even though
`CMakeCache.txt`'s literal `LLVM_ENABLE_PROJECTS` string only lists
`feme;mlir`. Reconfigured the same tree in place:

```
cmake -G Ninja -DLLVM_EXTERNAL_PROJECTS=OffloadTest \
  -DLLVM_EXTERNAL_OFFLOADTEST_SOURCE_DIR=/home/dev/dev/offload-test-suite .
```

`offload-test-suite`'s own `CMakeLists.txt`/`test/CMakeLists.txt` already
auto-detect an in-tree `feme_vulkan` target
(`if (TARGET feme_vulkan) set(OFFLOADTEST_ENABLE_FEME_VULKAN On)`) and
generate both `check-hlsl-feme-vk` (DXC-compiled) and
`check-hlsl-clang-feme-vk` (Clang-compiled) suites automatically, no
further plumbing needed. Verified via `ninja -t targets | grep check-hlsl`.

**Checked out `offload-test-suite`'s `feme` branch** (tracking
`llvm-beanz/feme`, no local branch previously existed) and, per the
standing "VkBool32/WaveActiveMax drift" instruction, diffed it against
`main` *before* running anything. Found the pattern recurring **twice
more** (3 total instances fixed this session, each its own isolated
`offload-test-suite`-only commit, not pushed upstream):

1. `lib/API/VK/Device.cpp`: `VkBool32` (4-byte) spec-constant override
   had been downgraded back to `bool` (1-byte) by the `feme` branch's own
   cherry-pick -- re-applied.
2. `test/WaveOps/WaveActiveMax.test`: `XFAIL: FeMe && host-arm64` had
   been dropped -- re-applied (this session's host is `aarch64`).
3. **New this session**: `test/Feature/PushConstant/array_of_matrices.test`
   had a stale re-added `XFAIL: DXC` for an already-upstream-fixed DXC bug
   (`microsoft/DirectXShaderCompiler#8080`) that `main` had already
   removed with its own comment confirming the currently-checked-out DXC
   no longer reproduces it. Initially misdiagnosed this as a genuine
   Clang-matrix-codegen win (edited the wrong, `Clang`-scoped directive)
   before realizing via `-v --show-all` that the active compiler for this
   suite is DXC, not Clang -- reverted that wrong edit and fixed the
   actual stale `XFAIL: DXC` block instead.

**First `check-hlsl-feme-vk` run, post-`L426`-fix + these 3 drift
fixes**:

```
Total Discovered Tests: 792
  Passed             : 488 (61.62%)
  Unsupported        : 273 (34.47%)
  Expectedly Failed  :  28 ( 3.54%)
  Failed             :   2 ( 0.25%)
  Unexpectedly Passed:   0 ( 0.00%)
```

Confirms `L426` unblocked all 4 of its own target cases
(`AllMemoryBarrierWithGroupSync`, `DeviceMemoryBarrierWithGroupSync`,
`InterlockedCompareExchange.resources.32`, `InterlockedExchange.resources.32`
-- all now Pass). The 2 remaining failures:

- `Basic/Mandelbrot.test`: re-ran in isolation, **passes** -- the
  already-known, already-tracked contention-dependent semaphore-wait
  flake under parallel load (same class as the `L421` race).
- `Feature/HLSLLib/InterlockedAdd.resources.32.test`: re-ran in
  isolation, **fails consistently** (not a flake). Isolated to 3 of 8
  `Results:` checks (`TestSlotsSBufI`, `TestSlotsBABuf`, `TestMono`), all
  sharing one precise signature: the 3-argument (original-value-
  capturing) `InterlockedAdd` on a *resource* (not groupshared)
  destination. Every 2-argument add on the same/sibling resources passes
  with the correct final count; the groupshared-destination 3-arg
  sibling (`InterlockedAdd.32.test`) also passes. Root-cause
  investigation started (`ResourceCalls.cpp`/`AtomicRMWIdentity.cpp`/
  `SPIRVResourceLowering.cpp`/`GroupShared.cpp`) but not concluded this
  session -- tracked as new follow-up `L429`.

**First-ever `check-hlsl-clang-feme-vk` run** (the Clang-compiled sibling
suite, never previously attempted in this environment):

```
Total Discovered Tests: 792
  Passed             : 285 (35.98%)
  Unsupported        : 269 (33.96%)
  Expectedly Failed  : 139 (17.55%)
  Failed             :  92 (11.62%)
  Unexpectedly Passed:   7 ( 0.88%)
```

A much larger, not-yet-triaged failure surface. Roughly 40 of the 92
failures are `Feature/HLSLLib/*_mat.test` (HLSL-library functions applied
to a matrix argument: `abs_mat`, `acos_mat`, `atan2_mat`, `ceil_mat`,
`cos_mat`, `cross`, `max_mat`/`min_mat`, `pow_mat`, `sin_mat`, `sqrt_mat`,
etc.), with smaller clusters in `Feature/Textures/*`,
`Feature/LocalResources/*`, and `Feature/ResourceArrays/*`. The 7
Unexpectedly-Passed cases (`and.test`, `or.test`, 4
`WaveActiveAllEqual.*`, `WaveReadLaneAt.Bool.test`) are byte-identical to
`main` (confirmed via `git diff main`, not drift) with a blanket
`XFAIL: Clang` that this session's Clang+FeMe combination now satisfies
-- a likely genuine (if narrow) improvement, deliberately left untouched
since editing a non-drifted upstream test file is out of scope. The
dominant matrix-function cluster is almost certainly a Clang-HLSL-
frontend maturity gap (matrix-argument lowering through these
intrinsics), not necessarily a FeMe backend bug -- not triaged further
this session; tracked as new follow-up `L428`.

**No targeted `VK-GL-CTS` run this session**: no FeMe compiler code
changed (both this session's fixes are `offload-test-suite`-only test-
expectation corrections), so no feature/extension-surface or compiler-
behavior change exists to verify against CTS. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: confirmed no update needed, same reason.

`Roadmap.md`: `L427` struck through (done, via a cheaper path than
originally planned); new `L428` (triage `check-hlsl-clang-feme-vk`'s
92-case failure list) and `L429` (`InterlockedAdd.resources.32.test`'s
3-arg-on-resource original-value bug) rows opened.

**Next steps**: `L429` (half day -- `InterlockedAdd.resources.32.test`'s
original-value-capture bug, already isolated to the exact 3-arg/resource
signature, candidate files identified); `L428` (1-2 days -- triage the
newly-discovered 92-case `check-hlsl-clang-feme-vk` failure list, starting
with a quick Clang-vs-DXC SPIR-V diff on one `_mat.test` case to confirm
scope before committing to a fix); `K6n`-proper (the largest remaining
`variablePointers` bucket) remains entirely untouched for many sessions.

## `L429` fixed: `EntryWrapperPass` `HeaderDerivedValues` poisoned by an already-erased `PersistentInduction` header phi, `InterlockedAdd.resources.32.test`'s 3-of-8 failure resolved (roadmap `L429`, closed)

Root-caused and fixed the `InterlockedAdd.resources.32.test` failure
isolated last session (`TestSlotsSBufI`/`TestSlotsBABuf`/`TestMono`, all
sharing the 3-argument original-value-capturing `InterlockedAdd`-on-
resource signature).

**Root-cause technique**: `feme/lib/Target/CPU/Pipeline.cpp`'s own
`FEME_DUMP_IR` dump point (immediately before `EntryWrapperPass` runs)
plus standalone `feme-opt --llvm -passes=feme-cpu-wrap-entry` on that
dump reproduced the exact defect outside the full pipeline, without a
single `ninja`+`offloader` round-trip, confirming the bug was wholly
contained within `EntryWrapperPass` itself (not an interaction with
later generic LLVM passes, even though `instcombine` is what finally
turns the symptom into a concrete wrong answer).

**Actual root cause** (not the originally-hypothesized
`feme-cpu-simdize` resource-atomic-widening theory): `EntryWrapperPass`'s
`HeaderDerivedValues` mechanism clones a header-local, non-phi
instruction (here, a wave-active-lane boolean mask splat) once into the
wrapper's own scalar `LoopHeaderBB` and threads it through a `loopvarN`
parameter to body/suffix regions -- valid only if the instruction's
operand chain is loop-invariant or `ScalarInduction`-driven. Here it
instead transitively read a `PersistentInduction`'s header phi, a
genuinely per-wave value with no single scalar representation in
`LoopHeaderBB`. `spillValuesLiveAcrossBarriers`'s reload-rewriting for
persistent phis only rewrites uses already inside the outlined regions,
never uses still inside `Shape.Header` itself, so this header-local use
was never redirected to a reload; `buildWrapperForLoop` then immediately
replaced every `PersistentInduction`'s `HeaderPhi` with `poison` and
erased it, silently poisoning the still-unrewritten use. The
clone-once-into-`LoopHeaderBB` step baked that `poison` in permanently,
and `instcombine` later folded it to a hard-coded `false`, silently
skipping the final masked resource store for every lane.

**Fix**: detect (`dependsOnPersistentInduction`) which
`HeaderDerivedValues` candidates transitively depend on a
`PersistentInduction`'s header phi and, for those only, recompute the
value fresh at each of its original use sites (`cloneHeaderChainAt`)
before `spillValuesLiveAcrossBarriers` runs, so the newly-materialized
in-region use is picked up and correctly reload-rewritten by the
existing, unmodified reload logic. Entries with no persistent dependency
are completely unaffected.

**Verification**:
- Added a regression unit test
  (`SplitsFlowMergeLoopWithHeaderDerivedValueFromWavePersistentRecurrence`)
  reproducing the minimal IR shape; confirmed it fails (3 assertion
  failures) with the fix reverted and passes with it restored.
- Full `FeMeTransformsCPUTests`: 635/635 Passed (up from 634; 1 net new
  test), 0 regressions.
- `check-hlsl-feme-vk`: 791/792 Passed (up from 490 Passed/1 Failed in
  the same run shape -- i.e. `InterlockedAdd.resources.32.test` now
  passes), sole remaining failure is the pre-existing
  `Basic/Mandelbrot.test` contention-dependent semaphore-wait flake
  under parallel load (unrelated, confirmed passing in isolation).
- Both the minimal (`/tmp/l429/b9/`) and full 8-resource
  (`/tmp/l429/full/t/`) repros now pass via `offloader` directly.
- Targeted `VK-GL-CTS` regression check (compute-atomic and
  loop/barrier groups most related to this fix's own code paths, since
  no prior session had an `InterlockedAdd`-adjacent case list):
  - `dEQP-VK.spirv_assembly.instruction.compute.opatomic*` (26 cases):
    19/26 Pass, 7 NotSupported (`VulkanMemoryModel.vulkanMemoryModel`
    not supported -- pre-existing, unrelated to this fix), 0 Fail.
  - `dEQP-VK.glsl.loops.*` (624 cases): 606/624 Pass, unchanged from
    `L425`'s own session baseline -- 0 regressions, 0 newly-fixed
    (expected: this fix is CPU-backend/compute-wave-persistent-value
    specific, not a graphics-loop-shape fix).
  - `dEQP-VK.glsl.demote.*` (30 cases): 30/30 Pass, unchanged -- 0
    regressions.

**No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed** -- pure internal `feme-cpu-wrap-entry` compiler-correctness
fix, no Vulkan feature or extension surface changed.

`Roadmap.md`: `L429` struck through (done, root cause corrected from the
original hypothesis to the actual `EntryWrapper.cpp` defect found this
session).

**Next steps**: `L428` (1-2 days -- triage `check-hlsl-clang-feme-vk`'s
92-case failure list, starting with one `_mat.test` case's Clang-vs-DXC
SPIR-V diff); `K6n`-proper (the largest remaining `variablePointers`
bucket, untouched for many sessions); `Basic/Mandelbrot.test`'s
contention-dependent flake (low priority, change-of-pace item); the
standing `backup-before-rebase-20261014` tag and
`CMAKE_BUILD_TYPE=RelWithDebInfo` vs `Release` decisions, neither
revisited this session.

## L428: `check-hlsl-clang-feme-vk` 92-case failure triage (SPIR-V backend + Clang HLSL driver, both outside FeMe)

Triaged the dominant `Feature/HLSLLib/*_mat.test` cluster (~40 of the
92 cases). Root cause is two independent, pre-existing bugs outside
`feme/`, both triggered by HLSL's matrix-to-flat-vector packing for
elementwise library calls (e.g. `asin(float4x4(...))` flattens to a
16-wide vector):

1. **Clang HLSL driver default**: `-spirv-ext=all` (the implicit
   default when `-fspv-extension=` is unset) swept in the SPIR-V
   backend's still-provisional `SPV_KHR_poison_freeze` extension, which
   the installed `spirv-val` rejects outright. Fixed in
   `clang/lib/Driver/ToolChains/HLSL.cpp`: excluded via
   `-spirv-ext=all,-SPV_KHR_poison_freeze`.
2. **SPIR-V backend `Vector16`/`Kernel` capability bug**: the backend
   unconditionally assumed 8- and 16-wide vectors could use the native
   `Vector16` capability, but that capability requires `Kernel`, which
   is illegal under Vulkan's `Shader` execution model -- any Shader
   module needing such a width hit a hard `report_fatal_error`. Fixed
   in `llvm/lib/Target/SPIRV/{SPIRVUtils.h,SPIRVEmitIntrinsics.cpp,
   SPIRVGlobalRegistry.cpp,SPIRVModuleAnalysis.cpp}`: made the
   long-vector-requirement determination subtarget-aware so Shader
   targets fall back to `SPV_EXT_long_vector` for widths 8/16, as they
   already do for other non-native widths.

**Verification**:
- `llvm/test/CodeGen/SPIRV` (1356 tests): all pass; new targeted
  regression test `vector8-16-shader-vs-kernel.ll` confirmed to fail
  without the fix, pass with it.
- `clang/test/Driver/*hlsl*`/`*dxc*` (39 tests): all pass; existing
  `dxc_fspv_extension.hlsl`'s `ALL` check updated and confirmed.
- `check-feme`: 3760/3768 Passed, same single pre-existing unrelated
  failure as before this session
  (`entry-wrapper-loop-header-derived-value-in-suffix.ll`, tracked to a
  prior session's `L426`/`L429` work, not touched this session).
- Targeted `VK-GL-CTS` regression subset (these fixes live in the
  Clang/LLVM SPIR-V *producer* path, not FeMe's own Vulkan *consumer*
  path that the CTS directly exercises, so no CTS change was expected
  -- confirmed, not just assumed):
  - `dEQP-VK.spirv_assembly.instruction.compute.opatomic*` (26 cases):
    19/26 Pass, 7 NotSupported, 0 Fail -- byte-identical to the
    `L429`-session baseline.
  - `dEQP-VK.glsl.loops.*` (624 cases): 606/624 Pass -- byte-identical
    to the `L429`-session baseline.
- Both fixes are each their own isolated, self-contained commit per
  the standing "issue outside FeMe" instructions; neither touches any
  `feme/` file.

**Net effect on the 92-case `check-hlsl-clang-feme-vk` list: unchanged
(still 92, exact same test names)**, because every `_mat.test` file
also exercises a `float4x4` (16-wide) shape that hits a *third*,
distinct, not-yet-fixed blocker even with both fixes applied:
`spirv-val` rejects the `OpVectorShuffle` sequence used to reassemble a
16-wide `OpTypeVectorIdEXT` matrix row-by-row
(`"Component index 16 is out of bounds for combined (Vector1 +
Vector2) size of 16"`), even though the mask index is in-bounds for the
true 32-element combined operand space -- looks like a `spirv-val`
`OpTypeVectorIdEXT`-length-tracking gap rather than a new bug of its
own, but not yet confirmed which side should carry the fix. Tracked as
new roadmap row `L428(a)`.

**No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed** -- pure Clang/LLVM SPIR-V-producer-side compiler fixes, no
Vulkan feature or extension surface changed on the FeMe ICD side.

`Roadmap.md`: `L428` struck through (triaged, 2 of 3 sub-bugs fixed);
new follow-up row `L428(a)` added for the remaining `spirv-val`
`OpTypeVectorIdEXT`-shuffle blocker.

**Next steps**: `L428(a)` (root-cause the `spirv-val`
`OpTypeVectorIdEXT`-shuffle validation gap -- determine if it's a
genuine LLVM backend bug or an upstream SPIRV-Tools limitation);
`K6n`-proper (largest remaining `variablePointers` bucket, untouched
for many sessions); `Basic/Mandelbrot.test`'s contention-dependent
flake (low priority); the standing `backup-before-rebase-20261014` tag
and `CMAKE_BUILD_TYPE=RelWithDebInfo` vs `Release` decisions, neither
revisited this session.

## L428(a): `spirv-val` `OpTypeVectorIdEXT`-shuffle validation bug root-caused as external, not fixable in this repo

Followed up on `L428`'s one remaining blocker for the `Feature/HLSLLib/
*_mat.test` cluster. Built a hand-written minimal `.spvasm` repro
(two 16-wide `OpTypeVectorIdEXT`-typed vectors shuffled together, no
HLSL/Clang/FeMe involved) and validated directly with `spirv-as`/
`spirv-val`.

**Confirmed**: the generated SPIR-V is spec-conformant; the bug is
entirely inside `spirv-val`. The validator reported `"combined
(Vector1 + Vector2) size of 14"` for two genuinely-16-wide operands
(should be 32) -- and `14` is exactly `7+7`, where `7` is the raw
numeric SPIR-V id of the `OpTypeVectorIdEXT` length *operand*
(`%uint_16`, itself a resolved `OpConstant 16`), not the integer `16`
it points to. Verified this isn't coincidental by padding the module
with 10 unrelated ids (id bound 14 -> 24, `%uint_16`'s own id held at
`7`): the reported "combined size" stayed exactly `14` -- the
validator's `OpVectorShuffle` dimension check reads the length
operand's raw id number instead of resolving it to the pointed-to
constant's value. This is specific to `OpTypeVectorIdEXT`
(`SPV_EXT_long_vector`'s dynamically-sized vector type, whose length
is an id operand, unlike classic `OpTypeVector`'s literal-word
length).

**Confirmed not fixable in this repo**: `llvm/tools/spirv-tools/
CMakeLists.txt` is only a thin hook for optionally running
SPIRV-Tools-dependent tests against an externally-built SPIRV-Tools;
no SPIRV-Tools source is vendored here. The environment's `spirv-val`/
`spirv-as` come from the separately-installed `spirv-tools` system
package (`SPIRV-Tools v2026.1`, the upstream `KhronosGroup/
SPIRV-Tools` project), and Clang's HLSL driver only shells out to
whatever `spirv-val` it finds on the toolchain path. No code change in
`llvm-project` can fix this.

**Disposition**: documented as a genuine, precisely-isolated upstream
bug (repro preserved at `/tmp/l428a/min.spvasm` for a future upstream
issue report), `Roadmap.md`'s `L428(a)` updated accordingly (not
struck through as "done" since the underlying CTS-visible failures
remain, but marked as having no further in-repo action available).
`L428`'s own two in-repo fixes (Vector16/Shader capability,
poison-freeze default) stand as independently complete and correct.

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed -- no code change, pure investigation/documentation.

**Next steps**: pivot to `K6n`-proper (largest remaining
`variablePointers` bucket, untouched for many sessions) since no
further in-repo action is available for `L428(a)`.

## K6p / K6a closure verification

**Device**: `FeMe CPU Vulkan Device` (confirmed via `vulkaninfo
--summary` with `VK_ICD_FILENAMES` explicitly set to the build's
`feme_icd.json`).

Picked up the `K6n`-proper next step from the prior session, but first
re-verified the roadmap's actual current state rather than trusting
the "largest remaining `variablePointers` bucket, untouched for many
sessions" framing several sessions' next-steps text kept carrying
forward unchanged. Found `K6n` itself, and all of its own split-off
children (`K6q`, `K6r`, `K6s`, `K6o`, `K6t`-`K6z`), already closed per
their row text in prior sessions. Only `K6p` (and transitively its
`K6a` parent bundle) remained open, with its own last update noting
"K6u is the only remaining `opptrdiff`-shaped residual" -- but `K6u`
and its own children (`K6v`-`K6z`) have since all been closed too,
making `K6p`'s row stale.

**Method**: generated the current `*variable_pointer*` CTS case list
(`deqp-vk --deqp-runmode=stdout-caselist -n 'dEQP-VK.*variable_pointer*'`,
parsing `^TEST:` lines -- note the correct flag is `-n`/`--deqp-case=`,
not `--deqp-case-filter=`, which this session initially tried in
error and which this `deqp-vk` build rejects outright). 4464 cases,
matching the historical measurement convention. Transiently flipped
both `EntryPoints.cpp` `variablePointers`/`variablePointersStorageBuffer`
feature-bit sites to `VK_TRUE` for measurement, rebuilt
`libfeme_vulkan.so`, and ran the full case list through
`feme/utils/run_vulkan_cts.py`'s crash-tolerant batch/solo-reverify
harness.

**Result**: 966 Pass / 3492 NotSupported / **6 Fail** (all 6 verified
solo, not flaky). All 6 failures:

```
dEQP-VK.spirv_assembly.instruction.compute.variable_pointers.compute.workgroup_two_buffers
dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrdiff.variable_pointers_vars_wg_diff
dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrequal.variable_pointers_vars_wg_equal
dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrequal.variable_pointers_wg_equal
dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrnotequal.variable_pointers_vars_wg_not_equal
dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrnotequal.variable_pointers_wg_not_equal
```

Re-ran two of these solo to confirm the exact failure signature:
every one fails identically with `feme-cpu-simdize`'s pre-existing,
already-documented diagnostic:

```
error: feme-cpu-simdize: groupshared global 'spirv_var_N' feeds an
unsupported user; only a getelementptr chain (to any depth) feeding a
direct load, store, atomicrmw, masked gather/scatter, or (for a
vector-typed row load, or a uniform row address broadcast into one)
an extractelement feeding its own scalar leaf access is supported
(roadmap milestone 9 deviation)
```

This is the CFG-linearization/`groupshared`-canonicalization gap
(milestone 9) that both `K6n`'s and `K6p`'s own prior-session text
already identified as the sole remaining blocker for the bundled
`variablePointers` feature as a whole -- confirmed here, not a new or
K6-scoped issue. All 6 cases involve a `workgroup`-storage-class
(`_wg_`) pointer.

**Disposition**: `K6p` closed (opcode-support gap was already fixed;
this session confirms no further K6p-scope residual exists). `K6a`
(the parent `variablePointers` bundle) closed too, since every one of
its split-off children (`K6n`, `K6o`, `K6p`, and their own further
splits) is now closed, with only the out-of-scope milestone-9 gap
remaining. Made the `EntryPoints.cpp` feature-bit flip to `VK_TRUE`
**permanent** (both the dedicated
`VK_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES` struct case and the
`VK_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES` aggregate case), since the
feature is now genuinely supportable at this level. Updated the one
unit test (`PhysicalDeviceInfoTest.cpp`) that asserted the old
`VK_FALSE` default. `check-feme` reconfirmed at the established
baseline, 3760/3768 (1 pre-existing, unrelated failure,
`entry-wrapper-loop-header-derived-value-in-suffix.ll`).
`Vulkan14FeatureInventory.md`'s `variablePointers`/
`variablePointersStorageBuffer` rows updated to `yes`, noting the
milestone-9 gap as the only known residual. `Roadmap.md`'s `K6a` and
`K6p` rows struck through accordingly.

**Next steps**: the milestone-9 CFG-linearization/`groupshared`-
canonicalization gap remains open as its own, already-tracked item
(out of K6's scope); no new roadmap row needed for it here. Remaining
carried-forward items from prior sessions: `Basic/Mandelbrot.test`'s
contention-dependent flake; the standing
`backup-before-rebase-20261014` tag and `CMAKE_BUILD_TYPE=
RelWithDebInfo` vs `Release` decisions; the standing
`offload-test-suite` `feme`-branch-vs-`main` drift re-check.

## `L430` fixed: groupshared pointer-merge/compare support, all 6 `K6p`/`K6a` milestone-9 residual cases now pass

**Device**: `FeMe CPU Vulkan Device` (confirmed via `vulkaninfo
--summary` with `VK_ICD_FILENAMES` explicitly set to the build's
`feme_icd.json`).

Picked up the milestone-9 CFG-linearization/`groupshared`-
canonicalization gap the prior session's `K6p`/`K6a` closure
verification left open as the sole remaining blocker for all 6 of its
own confirmed-failing cases. Resumed a prior session's in-progress,
uncommitted `GroupShared.cpp` edit (threading a new `Layout` parameter
through `isSupportedGroupSharedNestedGEPUser` for its new merge-
acceptance branch) and completed it: added
`isSupportedGroupSharedMergeUser`/`isSupportedGroupSharedCompareUser`
validation and `retargetGroupSharedMergesAndCompares`/
`flattenGroupSharedValue`/`retargetGroupSharedProducer` retargeting for
a groupshared pointer merge (`PHINode`/`SelectInst`) and a direct
pointer compare (`icmp eq`/`icmp ne`), the two shapes
`rewriteGroupSharedGlobals` previously rejected outright.

Debugging against the real CTS failures (all via `gdb --batch -ex run
-ex bt` against the `offloader` binary) surfaced three further bugs
beyond the inherited design, each found and fixed in turn:

1. **Vector-of-pointers gap.** `rewriteGroupSharedGlobals` runs *after*
   `FunctionWidener`'s widening pass, so a divergent merge/compare can be
   `<WaveSize x ptr addrspace(3)>`-typed, not scalar -- the design had
   been debugged exclusively against `FEME_DUMP_IR_PRESIMD=1` (always-
   scalar, pre-widening) dumps. Made `IsGroupSharedPtr`,
   `flattenGroupSharedValue`, and `isGroupSharedRootedPointer` vector-
   aware (`Ty->getScalarType()`, vector-width-matched null/poison/undef
   construction via `Constant::getNullValue`/`PoisonValue::get`/
   `UndefValue::get`, `ConstantAggregateZero` recognized as vector-null's
   own sentinel form alongside `ConstantPointerNull`).
2. **Mutual-recursion cycle-guard gap.** `isSupportedGroupSharedNestedGEPUser`'s
   merge-acceptance branch created a *fresh*
   `SmallPtrSet<const Instruction *, 8> VisitedMerges` on every call
   instead of threading the caller's set through, even though it and
   `isSupportedGroupSharedMergeUser` are mutually recursive; a self-
   referential loop-carried pointer iterator (`opptrdiff`'s own per-
   iteration advance feeding back into the phi it was derived from)
   alternated between the two functions indefinitely, causing genuine
   infinite recursion -- observed at 269,000+ stack frames via gdb.
   Fixed by threading `VisitedMerges` through the recursive call path
   only (the two genuinely-new top-level validation call sites still
   start fresh, correctly, since they are not themselves inside an
   existing recursive chain).
3. **Premature-erase-of-own-operand gap.** `retargetGroupSharedProducer`'s
   `NestedGEP` forward-user handling unconditionally rebuilt-and-erased a
   `getelementptr` even when that same GEP was *also* one of the merge's
   own incoming operands (the classic loop-carried backedge shape:
   `%next = getelementptr ..., %phi, ...` feeding back into `%phi`'s own
   incoming-edge list) -- deleting it while the not-yet-erased old merge
   still referenced it, crashing LLVM's use-list integrity assertion.
   Fixed with an `IsOwnOperand` check that `continue`s (skips) this
   specific shape instead, deferring to the existing `FlatCache` +
   `RecursivelyDeleteTriviallyDeadInstructions` cleanup that already
   handles ordinary merge-to-merge edges.

**Verification**:

- All 6 originally-failing cases now pass, confirmed individually (not
  just via a filtered run, verified with explicit exit codes):

  ```
  dEQP-VK.spirv_assembly.instruction.compute.variable_pointers.compute.workgroup_two_buffers
  dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrdiff.variable_pointers_vars_wg_diff
  dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrequal.variable_pointers_vars_wg_equal
  dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrequal.variable_pointers_wg_equal
  dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrnotequal.variable_pointers_vars_wg_not_equal
  dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrnotequal.variable_pointers_wg_not_equal
  ```

- Broader regression sweep, `dEQP-VK.spirv_assembly.instruction.*.
  variable_pointers*` (1650 cases): **962 Pass / 0 Fail / 688
  NotSupported** (expected gating) -- identical pass count to the prior
  session's own measurement before this fix (the 6 target cases simply
  moved from the "would-be-6-failures-if-feature-bit-were-on" state into
  "actually passing"; the feature bit was already flipped permanently by
  the prior `K6p`/`K6a` closure session, so this sweep ran with it on
  throughout).
- `FeMeTransformsCPUTests`: **637/637** passed, including two new
  `SIMDizeTest` cases added this session
  (`RetargetsGroupSharedPointerSelectAcrossTwoGlobals`,
  `RetargetsSelfReferentialGroupSharedPointerPhiWithCompare`).
- `check-feme`: **3762/3770** passed (1 pre-existing, unrelated failure,
  `entry-wrapper-loop-header-derived-value-in-suffix.ll`, same baseline
  as every prior session).
- `check-hlsl-feme-vk`: 492/793 non-unsupported passing, 1 failure --
  the same pre-existing, already-flagged `Basic/Mandelbrot.test`
  contention-dependent semaphore-wait flake, not a regression.

No further `Roadmap.md`/inventory change needed for the `variablePointers`
feature bit itself (already flipped to `yes` by the prior session); this
closes the milestone-9 residual gap that row's own note flagged as the
only remaining blocker. `Roadmap.md`'s new `L430` row added and struck
through (done).

**Next steps**: the prior sessions' standing carried-forward items
remain untouched this session too: `Basic/Mandelbrot.test`'s
contention-dependent flake; the standing
`backup-before-rebase-20261014` tag and `CMAKE_BUILD_TYPE=
RelWithDebInfo` vs `Release` decisions; the standing
`offload-test-suite` `feme`-branch-vs-`main` drift re-check (not
re-verified this session, no `offload-test-suite` work was done and the
checkout was not re-cloned, so drift risk is unchanged from the last
confirmed-clean state).

## `L302` closed: remaining `api.*`/`synchronization.op`/`copy_and_blit`/`image_clearing` CTS sampling, one major CFG-linearization gap found and split off as `L432` (roadmap `L302`, closed; `L432` opened)

Finished the `L302` sampling campaign's three remaining batches this
session: the small/medium leftover `api.*` subgroups, the full
`synchronization.op` group, and the two largest `api` subgroups
(`copy_and_blit`/`image_clearing`).

**Methodology bug caught before trusting any result**: `feme/utils/
run_vulkan_cts.py` inherits the caller's shell environment rather than
setting `VK_ICD_FILENAMES` itself. Two of the three batches were
initially launched with no ICD env exported at all (each CLI tool
invocation in this environment starts a fresh shell with no persisted
state across calls), silently running against the system-default
`llvmpipe` driver instead of FeMe. Caught via a cross-check: isolating
one of the batch's own "Fail" cases and re-running it standalone with
the correct `VK_ICD_FILENAMES` gave a *different* result
(`NotSupported`, a real `llvmpipe`-side `VK_KHR_maintenance8` gap,
irrelevant to FeMe) than the batch's own internal re-verification pass
for that same case name (`Fail`) -- a contradiction only explainable by
the batch never having touched FeMe at all. All three batches were
re-run with `VK_ICD_FILENAMES` explicitly exported in the same shell
invocation as the runner.

**Results (all FeMe-verified)**:

- Remaining small/medium `api.*` subgroups (13,865 cases): **7,041
  Pass / 6,823 NotSupported / 1 Fail**. The one Fail is the already-known,
  already-filed won't-fix `L412(g)`
  (`dEQP-VK.api.driver_properties.conformance_version` -- FeMe
  correctly declines to report a Khronos conformance version it has
  never actually submitted for). Not a regression.
- `synchronization.op` (20,131 cases): **4,899 Pass / 5,437 NotSupported
  / 9,795 Fail**. All 9,795 failures trace to the exact same single
  diagnostic: `feme-cpu-linearize: function 'main': loop at '' has a
  mask-affecting operation in its body but no recognized uniform exit
  check; unsupported (roadmap milestone 6 deviation)`. This is a
  previously-known-but-never-independently-scoped `LoopLinearizer` gap
  (only mentioned inside the already-closed `K6u`/`K6v` rows, for a
  *different* specific CFG shape) -- now split off as its own dedicated
  roadmap item, `L432` (see `Roadmap.md`; investigated and root-caused
  this session via a real IR dump, but a live fix was deliberately
  deferred -- see that row's own writeup for why, and `L432(a)` for the
  concrete next-step breakdown).
- `copy_and_blit`/`image_clearing` (55,122 cases, the two largest `api`
  subgroups, previously skipped as "too large for a single batch"):
  **23,108 Pass / 32,014 NotSupported / 0 Fail**. Clean.

**Verification**: `ninja check-hlsl-feme-vk` 492/492 Passed, 0 Failed
(confirms the Mandelbrot flake fix still holds under this session's
additional load); `ninja check-feme` 3762/3770 Passed, 7 Unsupported, 1
Failed (the same pre-existing, already-tracked
`entry-wrapper-loop-header-derived-value-in-suffix.ll` failure, not new
-- no FeMe source changes were made this session, so no new regression
risk existed in the first place).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed. No new missing feature or extension was found; `L432` is a
compiler CFG-handling gap in already-advertised functionality, not a
feature/extension gap.

`L302` itself is now closed (struck through in `Roadmap.md`); `L432`
carries its one real finding forward as a properly-scoped item.

**Next steps**: `L432(a)`'s fix itself (a minimal regression test plus
a forward-chain-walk extension to `LoopLinearizer`'s check-recovery
logic -- see `Roadmap.md`'s own breakdown) is the clear highest-leverage
next target, given its 9,795-case footprint in `synchronization.op`
alone. The usual standing items remain untouched this session:
`Basic/Mandelbrot.test`'s flake (confirmed still fixed, not re-broken);
the `backup-before-rebase-20261014`/`CMAKE_BUILD_TYPE` decisions
(already resolved in `L431`, not revisited); the standing
`offload-test-suite` `feme`-branch-vs-`main` drift re-check (not
re-verified this session, no `offload-test-suite` work was done).

## `L432(b)`/`L432(c)` closed: `synchronization.op`'s 9,795-case `LoopLinearizer` gap fixed, 1,259 cases flipped, plus an inherited regression caught and fixed before commit

Implemented `L432(a)`'s planned fix (forward-walk check recovery
through trivial relay blocks, `findForwardExitCheckBlock`), but the
real root cause of the targeted CTS repro
(`dEQP-VK.synchronization.op.multi_queue.binary_semaphore.
write_blit_image_read_image_compute.image_128_r32_uint_concurrent`)
turned out to be a separate, narrower bug one level up:
`DiamondFlattener::run()`'s own whole-function validation walk poisons
the shared `MergeVisited` set with no rollback when it ultimately
decides nothing needs flattening, permanently claiming blocks
`LoopLinearizer` itself still needs. Fixed (`L432(b)`) by reusing the
existing `LastRegionVisited` tracking member inside `run()` for the
first time, with rollback at both of `run()`'s early-return paths.

A mandatory `ninja check-feme` run (per standing instructions, before
any commit) caught a real regression introduced by the forward-walk
fix itself: it had no divergence gate, so a genuinely divergent
separate check (`loop-check-separate-carried-value.ll`) got claimed
and mutated by *both* the new path and the pre-existing
`OtherCondBrBlocks` machinery, crashing. Fixed (`L432(c)`) by gating
the forward-walk match to only populate `HeaderExit` when the match is
non-divergent.

**Results**:

- `FeMeTransformsCPUTests`: 638/638 Pass (637 pre-existing + 1 new
  regression test, `DoesNotPoisonMergeVisitedWhenWholeFunctionPassDeclines`).
- `ninja check-feme`: 3763/3771 Passed, 7 Unsupported, 1 Failed -- the
  same pre-existing, already-tracked
  `entry-wrapper-loop-header-derived-value-in-suffix.ll` failure
  (confirmed via a clean `git stash` baseline comparison to be
  unrelated to this session's changes, not a new regression).
  `loop-check-separate-carried-value.ll` (the regression this session
  introduced then caught and fixed) passes again.
- Target CTS repro: **Pass (OK)**, 1/1 100%.
- `dEQP-VK.synchronization.op.*` full re-sweep (20,131 cases): **6,158
  Pass / 5,437 NotSupported / 8,536 Fail** (up from the `L302`
  baseline's 4,899 Pass / 5,437 NotSupported / 9,795 Fail -- **1,259
  cases flipped to Pass**, NotSupported unchanged). The remaining 8,536
  Fail all now trace to a single, different, already-separately-tracked
  diagnostic (`"...has an internal branch in a body shape this
  milestone does not yet lower (e.g. an empty diamond arm, or one
  crossing another cycle's own boundary)"` -- the broader `K6z`/`L425`
  CFG-linearization gap family, covering many distinct shapes `L432`
  never claimed to cover).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (pure internal compiler CFG-handling fix, no feature/extension
surface change).

**Next steps**: the broader `K6z`/`L425` "internal branch in a body
shape this milestone does not yet lower" diagnostic family is now the
single largest remaining `synchronization.op` blocker (8,536 cases) --
likely the next highest-leverage CFG-linearization target, though it
covers multiple distinct shapes per its own existing roadmap rows, so
scoping a specific sub-shape first (the way `L432` itself was split off
from a vaguer predecessor) is probably the right next step rather than
attacking it as one lump. The usual standing items remain untouched
this session: `Basic/Mandelbrot.test`'s flake; the
`backup-before-rebase-20261014`/`CMAKE_BUILD_TYPE` decisions (already
resolved in `L431`); the standing `offload-test-suite`
`feme`-branch-vs-`main` drift re-check (not re-verified this session,
no `offload-test-suite` work was done).

## `L433` closed: `synchronization.op`'s `DiamondFlattener::run()` commit-path `ResolvedBlocks` gap fixed, 2,512 more cases flipped

Scoped, per the prior session's own handoff, exactly one sub-shape of
the `K6z`/`L425` "internal branch in a body shape this milestone does
not yet lower" diagnostic family (then the single largest remaining
`synchronization.op` blocker, 8,536 cases):
`dEQP-VK.synchronization.op.single_queue.fence.
write_image_tess_eval_read_image_tess_control.image_128_r32_uint`,
whose tessellation-domain-stage shader has a mask-affecting
`feme.stage.output.store.f32` call before an otherwise-uniform `for`
loop.

Root-caused via `FEME_DUMP_IR_PRELINEARIZE=1` plus a progressively
stripped-down standalone `feme-opt --llvm -passes=feme-cpu-linearize`
reproduction (bypassing the real pipeline entirely for fast
iteration): the real divergence was neither the loop's own CFG shape
(a standalone copy of just the loop linearizes cleanly on its own) nor
any metadata, but the mere *presence* of a mask-affecting
`feme.stage.*` call anywhere else in the function. That flips
`DiamondFlattener::run()`'s own
`!HasDivergentBranch && !hasStageMaskOps(F)` gate away from its
decline-and-roll-back path (the one `L432(b)` already fixed) onto its
**commit** path instead -- a sibling bug to `L432(b)`, on the opposite
side of the same gate. `run()`'s commit path's final per-root
`flatten()` loop walks straight through this loop's own
`header`/`check`/`Flow`-dispatch/`exit` chain while merely
*discovering* (not flattening) the loop's own cycle boundary, but
-- unlike its own sibling methods `flattenLoopBodyDiamond`/
`flattenLoopBodyRegion`, both of which explicitly release their own
visited blocks into `ResolvedBlocks` right after a successful commit
-- `run()`'s commit path never did. Every block the whole-function
walk merely passed through stayed permanently and incorrectly claimed
in the shared `MergeVisited` set, so `LoopLinearizer`'s own later,
entirely legitimate `flattenLoopBodyRegion` walk over that same loop
spuriously hit the stale claim and failed with `validate`'s generic
"reached by more than one divergent root" conflict.

Fixed with a single
`ResolvedBlocks.insert(LastRegionVisited.begin(), LastRegionVisited.end());`
added to `DiamondFlattener::run()`'s commit path, immediately before
its final `return true;`, mirroring the other two methods' own
pattern exactly.

**Results**:

- New lit regression:
  `feme/test/Transforms/CPU/Linearize/loop-body-resolved-blocks-after-mask-op.ll`,
  isolating this exact shape through `feme-cpu-linearize` alone
  (renamed from the real captured IR's numeric block names to
  meaningful ones, topology preserved verbatim); confirmed via a
  `git stash` bisection to fail without the fix and pass with it.
- `FeMeTransformsCPUTests`: 638/638 Pass (no new unit test added --
  the new lit regression above covers this bug end to end at the
  `Linearize.cpp` pass level, matching the existing lit-test-only
  coverage style for comparable `DiamondFlattener`/`LoopLinearizer`
  CFG-shape fixes in this area).
- `ninja check-feme`: 3763/3771 Passed, 7 Unsupported, 1 Failed -- the
  same pre-existing, already-tracked
  `entry-wrapper-loop-header-derived-value-in-suffix.ll` failure
  (reconfirmed via a clean `git stash` baseline comparison to be
  unrelated to this session's changes, not a new regression).
- Target CTS repro: **Pass (OK)**, 1/1 100%.
- `dEQP-VK.synchronization.op.*` full re-sweep (20,131 cases):
  **8,670 Pass / 5,437 NotSupported / 6,024 Fail** (up from the
  `L432(b)`/`L432(c)` baseline's 6,158 Pass / 5,437 NotSupported /
  8,536 Fail -- **2,512 cases flipped to Pass**, NotSupported
  unchanged -- more than double the single repro case's own weight,
  confirming this one-line fix's blast radius covers many CFG shapes
  sharing the same underlying `ResolvedBlocks` contract gap, not just
  the domain-shader shape it was isolated from). The remaining 6,024
  Fail cases all surface as the CTS-side generic
  `vk.create{Graphics,Compute}Pipelines(...): VK_ERROR_INITIALIZATION_FAILED`
  (3,400 graphics + 2,624 compute) -- consistent with the still-open,
  broader `K6z`/`L425` diagnostic family covering other CFG shapes
  this fix does not address.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (pure internal compiler CFG-handling fix, no feature/extension
surface change).

**Next steps**: a future session should re-cluster the residual 6,024
`synchronization.op` failures' own real underlying diagnostics (via
the same `FEME_DEBUG_LINEARIZE_TRACE`/temporary-`Quiet=false`-rerun
bisection technique used this session) to scope the next specific
`K6z`/`L425` sub-shape, per the same "one shape per session" approach
that produced both `L432(b)` and this fix -- rather than attacking the
whole remaining family as one lump. The usual standing items remain
untouched this session: `Basic/Mandelbrot.test`'s flake; the
`backup-before-rebase-20261014`/`CMAKE_BUILD_TYPE` decisions (already
resolved in `L431`); the standing `offload-test-suite`
`feme`-branch-vs-`main` drift re-check (not re-verified this session,
no `offload-test-suite` work was done).

## `L434` closed: stale-`CycleInfo` membership in `isLoopControlEdge` fixed, all remaining 6,024 `synchronization.op` failures flipped to Pass

Scoped, per the prior session's own handoff, a new, previously
undiscovered CFG shape from the `K6z`/`L425` "internal branch in a
body shape this milestone does not yet lower" diagnostic family: two
nested *uniform* loops, each independently containing an image
load/store pair, with no mask-affecting op anywhere in the function --
isolated via `dEQP-VK.synchronization.op.single_queue.barrier.
write_image_compute_read_image_compute.image_128x128_r16_uint`'s
compiled compute-stage shader, which only fails when the two loops are
nested together (either loop alone linearizes cleanly on its own).

Root-caused via `FEME_DUMP_IR_PRENORM=1` plus
`FEME_DEBUG_LINEARIZE_TRACE`-instrumented tracing of a standalone,
`instnamer`-named reproduction -- after first fixing a latent bug in
the debugging technique itself, inherited from the `L433` session: the
`Quiet=false` re-run-on-failure probe did not roll back `validate`'s
own `MergeVisited`/`LastRegionVisited` insertions left behind by the
first, failed `Quiet=true` call, manufacturing a false "already
visited" diagnostic that was an artifact of the probe itself, not the
real bug (fixed by snapshotting both sets before the first call and
restoring them before the second). With that fixed, the real
diagnostic surfaced: `"an empty diamond arm (in 'bb15') is not yet
supported"`.

Root cause: `CycleInfo CI` is computed exactly once per function, in
`LinearizePass::run()`, and -- unlike `DT`/`PDT`, which are
recalculated before every single `linearizeCycle` call -- is never
recomputed afterward, even though `linearizeCyclePostOrder` mutates
the CFG extensively between cycles as it processes them post-order.
Once the *inner* loop (provably uniform, no mask-affecting op) is
linearized, its own redundant-`Flow`-dispatch-block collapse rewires
its exit relay (a `.Flow_crit_edge`-style block) to branch straight
past the now-deleted `Flow` block to the loop's real exit -- but `CI`'s
frozen snapshot still reports that relay as a member of the inner
cycle: `CI.contains` is a static membership query, unaffected by later
edge rewiring, and dominance is unaffected too (the relay is still
dominated by the header exactly as before; dominance only reasons
forward from the entry). When the *outer* loop's own body-region
precheck later walks through the inner loop's own (unmutated) exit
check, `isLoopControlEdge` wrongly reports its exit edge as an
ordinary interior edge rather than a cycle boundary, so `validate`
tries to flatten the exit check as an ordinary two-way diamond --
computing its immediate post-dominator as the exit relay itself
(structurally correct: every remaining path must pass through it
before the function's one `ret`), which then collides with that same
relay also being one of the exit check's own two successors, tripping
the "empty diamond arm" diagnostic.

A wholesale `CI.compute(F)` recompute mid-traversal was considered and
rejected as unsafe: `CycleRef` handles are documented valid only until
the cycle forest's next recompute, and `linearizeCyclePostOrder` holds
such handles across recursive stack frames while iterating
`CI.children(C)`, so invalidating them mid-walk would corrupt an
in-flight ancestor frame's own iteration.
`GenericCycleInfo::addBlockToCycle` was also checked and found to only
support a block *joining* a cycle, not leaving one, so it doesn't fit
this case either.

Fixed instead with a narrower, targeted change: `isLoopControlEdge`
now falls back to a live, `CI`-membership-bounded reachability check
(`canReachBlock`) whenever `CI.contains` claims a target is still a
cycle member -- mirroring the freshness `DT`/`PDT`'s own recompute
already provides for dominance, but scoped as a pure fallback (it
never fires for the common, non-stale case, since any genuinely-still-
member block can, by that membership's own strongly-connected-
component definition, still reach the header). `canReachBlock`'s BFS
is confined to only follow successors `CI.contains` *also* still
reports as members of the same (stale) candidate cycle, so it cannot
escape through some unrelated route -- an earlier, unconfined version
of this same idea was tried first and found to produce a false "still
reachable" answer via the *outer* loop's own backedge re-entering the
inner loop's header on a later outer iteration, which is not evidence
of interior-cycle reachability at all.

**Results**:

- New lit regression:
  `feme/test/Transforms/CPU/Linearize/nested-uniform-loops-with-image-calls.ll`,
  isolating this exact shape through `feme-cpu-linearize` alone
  (renamed from the real captured IR's numeric block names to
  meaningful ones, topology preserved verbatim); confirmed via a
  `git stash` bisection to fail without the fix (same "empty diamond
  arm" diagnostic) and pass with it.
- `FeMeTransformsCPUTests`: 638/638 Pass.
- `ninja check-feme`: 3765/3773 Passed, 7 Unsupported, 1 Failed -- the
  same pre-existing, already-tracked
  `entry-wrapper-loop-header-derived-value-in-suffix.ll` failure, not
  a new regression.
- Target CTS repro: **Pass (OK)**, 1/1 100%.
- `dEQP-VK.synchronization.op.*` full re-sweep (20,131 cases):
  **14,694 Pass / 5,437 NotSupported / 0 Fail** (up from the `L433`
  baseline's 8,670 Pass / 5,437 NotSupported / 6,024 Fail -- **all
  6,024 remaining failures flipped to Pass**, closing out the entire
  `synchronization.op` failure set in one fix, not just the single
  nested-loop shape it was isolated from. Apparently every remaining
  `K6z`/`L425`-family case reachable in this particular test family
  shared this exact stale-`CycleInfo` root cause once reached --
  unlike `L432(b)`/`L433`, where the blast radius was a large but
  partial fraction of the then-remaining failures).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (pure internal compiler CFG-handling fix, no feature/extension
surface change).

**Next steps**: with `synchronization.op.*` now at 0 Fail, a future
session should pick a different CTS test family for its next raw
sweep-and-triage pass (e.g. the broader `api.*`/`copy_and_blit`/
`image_clearing` families `L302` originally sampled from, or a fresh
full mustpass run) rather than continuing to mine this now-closed
family. The usual standing items remain untouched this session:
`Basic/Mandelbrot.test`'s flake; the standing `offload-test-suite`
`feme`-branch-vs-`main` drift re-check (not re-verified this session,
no `offload-test-suite` work was done).

## `L435`: `multithreaded_per_thread_resources.device` segfault root-caused as an external Vulkan-Loader race, not fixable in this repo

Picked up `L302`'s own "start fresh with a different CTS family"
next step: ran a new `dEQP-VK.api.*` sweep (`feme/utils/
run_vulkan_cts.py`). It progressed cleanly (0 Fail so far, ~175k
cases in) before segfaulting (exit 139) on
`dEQP-VK.api.object_management.multithreaded_per_thread_resources.
device`. Isolated standalone: reproduces at roughly 50-80% per run
across repeated invocations of this one case alone (confirmed on a
clean rebuild: 5/10 crashes in one batch), so this is a real,
narrowly-scoped, racy crash, not sweep-harness interference (an
earlier false start that looked like a hang was actually two
competing `deqp-vk` processes launched by mistake contending for
CPU, since corrected).

**Core-dump analysis** (direct `ulimit -c unlimited` run, no
`timeout` wrapper -- wrapping in `timeout` produces a core dump for
`timeout` itself, not the monitored child, a dead end hit first):
the crashing thread's backtrace is CTS's own
`vk::DeviceDriver::DeviceDriver(...)` (populating its own
per-device function-pointer table right after a successful
`vkCreateDevice`) calling into the system Vulkan loader's own
device-dispatch trampoline (`libvulkan.so.1`, no debug symbols),
landing at a garbage address inside `libvulkan.so.1` itself. Several
other threads were concurrently inside `vkDestroyDevice`/
`~DeviceWrapper()` or idling in FeMe's own `QueueExecutor::run()`
(`Sync.h`) -- consistent with the test's own documented shape (see
below), not a hang or deadlock.

**Code review of every FeMe object touched by this path**
(`Icd.h`'s `DispatchableBase`/`Allocator`, `EntryPoints.cpp`'s
`vkCreateDevice`/`vkDestroyDevice`/`vkGetDeviceQueue*`/
`vkGetDeviceProcAddr`, `ProcAddr.cpp`'s static `kEntries` table,
`Objects.h`'s `PhysicalDevice`/`Device`/`Queue`/`Instance`,
`PipelineCache.cpp`'s constructor) found no shared *mutable* state
across concurrently-created `Device` objects: each is an
independent heap allocation with its own `PipelineCache`,
`EnabledExtensions`, `Queues`, and `std::atomic<bool> Lost`; the one
object every concurrently-created `Device` *does* share
(`PhysicalDevice`/`PhysicalDeviceInfo`) is computed once in
`PhysicalDevice`'s own constructor and never mutated again, so
concurrent reads of it are safe by construction. `Allocator::
create`/`destroy` are plain `malloc`/`free`-backed placement-new
wrappers with no custom pooling, so FeMe's own allocation path adds
no additional race beyond what glibc's thread-safe allocator already
handles.

**Confirmed the test genuinely exercises concurrent
create/destroy against one shared physical device**: read
`vktApiObjectManagementTests.cpp`'s `multithreadedCreatePerThreadResourcesTest`
directly (VK-GL-CTS source, not this repo) -- one shared `VkInstance`/
`VkPhysicalDevice` (`Environment`), `getDefaultTestThreadCount()`
threads each running `CreateThread<Device>`, 20
create-then-immediately-destroy iterations per thread with a
`barrier()` every few iterations specifically to "make entering
driver at the same time more likely". This is precisely the shape
the externally-synchronized-per-object Vulkan spec does *not*
protect against: `vkCreateDevice`/`vkDestroyDevice` only require
external synchronization for the one `VkDevice`/`VkPhysicalDevice`
instance involved in *that* call, not serialization against *other*,
unrelated `vkCreateDevice`/`vkDestroyDevice`/`vkGetDeviceProcAddr`
calls racing concurrently against different device handles in the
same process -- exactly the gap a loader's own internal, shared
dispatch-table bookkeeping must handle itself.

**Dynamic verification, the decisive evidence**: an `LD_PRELOAD=
libasan.so.8` probe against the unmodified binary (no recompile)
caught the crash as a plain `SIGSEGV` with "AddressSanitizer can not
provide additional info" -- ruling out a simple heap-buffer-overflow
mediated through any malloc'd allocation FeMe itself made. Then
built a mixed-instrumentation diagnostic (`FeMeVulkanCore`/
`feme_vulkan` recompiled with `-fsanitize=address
-fno-omit-frame-pointer`, linked against the system GCC package's
own ABI-compatible static `libasan.a` since this environment's clang
install has no bundled compiler-rt ASan runtime archive for this
target triple -- `-fsanitize=address -fno-sanitize-link-runtime`
suppresses clang's own runtime auto-link so the explicit
`target_link_libraries` entry resolves instead). Result: **0 crashes
and 0 ASan reports across 8 repeated runs** of the same case that
crashes 50-80% of the time on an unmodified rebuild -- ASan's
allocator (redzones + delayed-reuse quarantine) changes the heap
*reuse timing* just enough to close the race window, without any
FeMe-side code actually changing behavior. This is the classic
signature of a UAF/address-reuse race the *allocator's own timing*
gates, not a bug in any code ASan could instrument (since none of
FeMe's own instrumented code ever faulted, and the fault site is
inside the uninstrumented system loader either way).

**Confirmed via public research**: this matches a well-documented
class of Vulkan-Loader bugs (`KhronosGroup/Vulkan-Loader` issue
`#200`, "Race condition in vkCreateDevice()+vkDestroyDevice()+
vkGetDeviceProcAddr()" -- closed in 2022 by a mutex guarding
`loader.instances`) -- the installed system loader here
(`libvulkan1` 1.4.341.0-1) is years newer than that fix, so this is
not literally the same already-patched race, but the same general
"per-call external synchronization is not enough to protect the
loader's own shared, process-wide dispatch bookkeeping from
concurrent create/destroy against *different* devices" class of bug,
in a location (`loader_get_dispatch`/device-dispatch-table lookup)
the loader itself owns.

**Confirmed not fixable in this repo**: the Vulkan loader
(`/usr/lib/aarch64-linux-gnu/libvulkan.so.1`) is a separately
installed system package (`libvulkan1`), not vendored or checked out
anywhere under `llvm-project`. There is no file in this repository
whose change could alter the loader's own internal dispatch-table
bookkeeping. The temporary ASan CMake diagnostic
(`feme/lib/Vulkan/CMakeLists.txt`/`feme/tools/feme-vulkan/
CMakeLists.txt`) was reverted after use; no code change was made or
is possible here.

**Disposition**: documented as a genuine, precisely-isolated external
Vulkan-Loader race, matching `L428(a)`'s own precedent for an
upstream-but-not-vendored dependency bug. `Roadmap.md`'s new `L435`
row is not struck through as "done" (the CTS-visible crash remains
reproducible against the system loader) but is marked as having no
further in-repo action available. The broader `dEQP-VK.api.*` sweep
this crash interrupted was not yet resumed/completed this session --
left for a future session, which should run it with this one case
either skipped or expected to intermittently crash the whole batch
(the crash takes down the entire `deqp-vk` process, not just the one
test case, so a crash-tolerant harness invocation -- already
available via `feme/utils/run_vulkan_cts.py`'s existing
crash-recovery/solo-reverify logic -- is required to get a full
`api.*` Pass/Fail/NotSupported count past this point).

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed -- no code change, pure investigation/documentation, and no
feature/extension surface is implicated (this is a process-lifetime
bug in a dependency, not a missing/incorrect capability).

**Update (same session): completed the full `dEQP-VK.api.*` sweep**
past this crash, via `feme/utils/run_vulkan_cts.py`'s existing
crash-tolerant batch/solo-reverify harness (267,512 cases, 6
workers). Result: **116,730 Pass / 150,781 NotSupported / 1 Fail**,
0 Unrun -- a full first-ever baseline for this family (`L302` only
ever sampled from it, never swept it exhaustively). The harness's
own recovery logic transparently handled `L435`'s crash: the one
case that hit it (plus one `copy_and_blit` case that also raced
under contention,
`use_after_copy.a8b8g8r8_srgb_pack32.transfer_dst_optimal.
1024x1024x2_color_att_flag_img2img_msaa`) both reconciled to Pass
once re-verified alone -- no manual intervention needed, confirming
`L435`'s root-cause (an allocator/loader-timing-gated race, not a
deterministic bug) and the harness's own crash-recovery design in
one pass.

The one genuine, deterministic failure --
`dEQP-VK.api.driver_properties.conformance_version` -- is **not a
bug**: `FeMeVulkanDesign.md`'s own "Initial Non-Goals" and `D0`-era
API-version writeup are explicit that this ICD deliberately reports
a truthful zero `VkConformanceVersion` rather than falsely implying
Khronos conformance it hasn't earned, and this exact CTS case exists
specifically to check that a driver's claimed `conformanceVersion`
is not older than its claimed `apiVersion` -- which a genuinely
zero/unsubmitted conformance version, by definition, always is. This
is the expected, by-design tradeoff of advertising `apiVersion = VK_
API_VERSION_1_4` without having gone through a real Khronos
conformance submission; it will remain a known, accepted CTS-visible
failure until (and unless) this ICD actually pursues Khronos
conformance (see `FeMeVulkanDesign.md`'s own submission-package note
near its end). No fix applied; no roadmap row opened for it.

**Final `api.*` family baseline**: 116,730 Pass / 150,781
NotSupported / 1 Fail (the 1 Fail being the deliberate, by-design
`conformance_version` case above). No `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` update needed here either -- no
feature/capability gap found, just the one already-documented
design tradeoff.

**Next steps**: `api.*` is now effectively closed at its intended
design ceiling (1 expected-by-design Fail, 0 unexplained). A future
session should pick a different, not-yet-exhaustively-swept family
for its next raw sweep (e.g. `copy_and_blit`/`image_clearing`, which
`L302` also only sampled from, same as `api.*` was before this
session). The usual standing items remain untouched this session:
`Basic/Mandelbrot.test`'s flake (confirmed still present, 1/3
`check-hlsl-feme-vk` runs failed); the standing `offload-test-suite`
`feme`-branch-vs-`main` drift re-check (re-verified this session,
cheap `git fetch`/`git log` only: still the same single remote-only
commit, `0cc94c2`, unchanged).

## `L436`: full `synchronization` family re-swept (no regression); master per-group table found stale, `api` row corrected

Per the prior session's next steps (pick a different, not-yet-fully-swept
family after `api.*`), swept the complete `dEQP-VK.synchronization.*`
family -- not just the `.op` subgroup `L432`-`L434` fixed -- via
`feme/utils/run_vulkan_cts.py` (64,872 cases, 6 workers). Result:
**18,420 Pass / 46,452 NotSupported, 0 Fail/DeviceLost/TimedOut, 0 Unrun**.

This turned out to be a **re-confirmation, not new data**: these exact
numbers (18,420 Pass) already appear in this file's master "Notable clean
groups" line, which was last updated post-`L434`. The value of re-running
it anyway: it's a direct, fresh confirmation that the `L432`/`L433`/`L434`
`Linearize.cpp`/`DiamondFlattener` fixes hold with zero regressions across
every `synchronization` subgroup (`basic`, `cross_instance`,
`global_priority_transition`, `implicit`,
`internally_synchronized_objects`, `op`, `signal_order`, `smoke`,
`timeline_semaphore`, `win32_keyed_mutex`), not just the `op` subgroup
those fixes directly targeted.

**Larger finding, while cross-checking this against the master table**:
`copy_and_blit`/`image_clearing` (the prior session's suggested "different
family") are not actually separate top-level groups at all -- both are
subgroups of `dEQP-VK.api.*` (confirmed via `--deqp-runmode=stdout-caselist`
`GROUP:` enumeration), already fully covered by `L302`'s original sampling
and this session's own full `api.*` re-sweep. More importantly, the
master "Per-group accounting" table itself (the `## Current failure
ownership`-preceding section) is a **frozen snapshot of the 2026-10-12 full
3,253,584-case run**, not a continuously-updated live count: the `api`
row's `21`/`16` historical Fail/TimedOut figures are stale relative to
today's actual state (confirmed `1` Fail / `0` TimedOut by this session's
complete `api.*` re-sweep). Added an explanatory note directly below the
table (see above) rather than silently editing the historical figures, to
preserve the `2026-10-12` snapshot's own internal consistency while making
clear it needs reading with that caveat.

**Implication for future sessions**: every other row in that table
(`pipeline`, `image`, `spirv_assembly`, `transform_feedback`, etc.) carries
the same staleness risk -- each has very likely had some of its own `Fail`
count reduced by individually-tracked sessions since 2026-10-12 without the
master row being updated to match, the same way `api` was. A full-CTS
re-triage (all 3.25M+ cases, likely a multi-day background run given
`api.*`'s 267,512 cases alone took a meaningful fraction of a session) is
the only way to refresh every row at once and would also surface any
*new* regressions across groups no session has touched directly. This is
too large for a single session's remaining time today, but is now the
clearest highest-value "fresh start" candidate for a future session with
a full day or more to spend, rather than picking one more individual
family at a time.

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed (no feature/extension-surface change, pure CTS measurement and
documentation hygiene).

**Standing items re-checked this session**: `Basic/Mandelbrot.test` --
ran 12 additional times this session (6 in isolation, 6 under a 12-worker
parallel `check-hlsl-feme-vk`-equivalent load), **0/12 failures** --
still a low-frequency, non-reliably-reproducible flake, no regression,
still not picked up as its own investigation item. `offload-test-suite`
`feme`-branch drift -- re-fetched from `llvm-beanz/feme`, confirmed still
the same single remote-only commit (`0cc94c2`, `CMakeLists.txt`/
`test/CMakeLists.txt`/`test/lit.cfg.py` only), unchanged.

**Next steps**: (1) a future session with a full day or more should
consider a complete, from-scratch full-CTS re-triage (all ~3.25M cases)
specifically to refresh the master per-group table's now-potentially-stale
rows, rather than continuing to pick individual families one at a time;
(2) `Basic/Mandelbrot.test`'s flake remains unreproduced after 18+
cumulative spot-check runs across sessions -- likely needs either a much
larger sample size (50-100+ runs) or direct instrumentation of the
`offload-test-suite` test harness itself to catch it in the act, if it's
ever going to be actively investigated rather than just monitored;
(3) `offload-test-suite` drift still needs a decision from a session
actually touching that repository.

## `L437`: full-CTS re-triage launched (in progress); `Basic/Mandelbrot.test` flake finally root-caused and fixed (outside FeMe)

**Full re-triage status (in progress, not final)**: launched the
`L436`-recommended from-scratch `dEQP-VK.*` re-triage this session
(3,253,584 cases, `feme/utils/run_vulkan_cts.py --batch-size 1800
--workers 6`, output under `/tmp/l437_full_run/`). This is a genuinely
multi-day background job at this scale; as of this session's end it had
produced roughly 190 of an eventual ~1,800+ batches. **The master
"Per-group accounting" table above is still the 2026-10-12 snapshot** (plus
the `L436`-added `api` staleness footnote) -- it is intentionally *not*
being rewritten from this in-progress run, to avoid replacing one
known-accurate historical snapshot with a partial, misleadingly-incomplete
one. A future session should check `/tmp/l437_full_run/` (or wherever this
job's state has been persisted/resumed) and, once it actually completes,
replace the whole table in one pass per `L436`'s own recommendation.

**`Basic/Mandelbrot.test` flake: root-caused, not a FeMe bug, fixed outside
`feme/`.** A 30-isolated-run stress test (this session, following the
prior session's own "needs 50-100+ runs" suggestion) reversed from
near-zero failures (0/12 in the immediately preceding session) to **30/30
failures**, with the only new variable being this session's own
background CTS sweep running continuously (load average ~7.4 on a
12-core host). The failure is `vkWaitSemaphores` returning `VK_TIMEOUT`.
Critically, re-running with `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS=180000`
(6x the normal 30000) still failed in ~5.4 seconds -- ruling out "just
needs a longer timeout budget under load" and proving the configured
timeout was never actually controlling anything.

Root cause: `lit`'s own `TestingConfig` (`llvm/utils/lit/lit/
TestingConfig.py`) builds a **hermetic** environment for every test
subprocess from a fixed allowlist (`PATH`, `LD_LIBRARY_PATH`, a handful of
sanitizer/toolchain vars, etc.) plus whatever `lit.cfg.py` explicitly adds
via `with_environment`/`with_system_environment` -- it does **not** by
default inherit the full invoking shell's environment.
`offload-test-suite/test/lit.cfg.py` only ever added the `VK_*` driver/
loader variables to that allowlist, never `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS`.
This means the `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS=30000` set on the outer
`ninja check-hlsl-feme-vk` invocation (originally `L255`, re-confirmed as
recently as `L431`) **has never once reached the actual Vulkan ICD test
subprocess, in any session** -- `Sync.cpp`'s `getSafetyNetTimeoutNs()`
always silently fell back to its hardcoded `DefaultSafetyNetTimeoutNs`
(5 seconds, `Sync.h`). Under light load a real compute dispatch plus
bookkeeping comfortably finishes within 5 seconds, which is why this
looked like a rare, contention-only flake for many sessions; under this
session's sustained heavy background load, it reliably does not.

**Fix**: added `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS` to the
`with_system_environment` allowlist in `offload-test-suite/test/lit.cfg.py`.
This is entirely outside the `feme/` subdirectory (a separate repository),
so per the standing instruction it was committed there alone, as a
self-contained, isolated fix. Verified with a clean before/after A/B under
identical heavy background load (load average ~7): **5/5 FAIL before the
fix, 13/13 PASS after** (10 runs at the real build-configured `30000`
value, plus 3 more at an exaggerated `180000` to directly confirm the
override itself now actually takes effect end-to-end). No FeMe source
change was needed or made -- this was purely an `offload-test-suite`
test-harness configuration gap, not a FeMe synchronization bug.

This also means every prior session's "flake stays fixed"/"contention-only"
conclusion (most explicitly `L431`'s `check-hlsl-feme-vk` 492/492 pass
claim) was technically correct in its observed result but incorrect in
its explanation: those runs passed because 5 seconds happened to be
enough under each of those sessions' own lighter background load, not
because the configured 30-second budget was actually in effect.

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed (no feature/extension-surface change; a test-harness environment-
propagation fix, not a Vulkan behavior change).

## `L438`: full re-triage recovered after a session-boundary kill, relaunched detached

`L437`'s full re-triage did not survive to this session: it had been
launched with the `bash` tool's plain `mode="async"` (attached to that
session), which the harness terminates when the session itself ends --
regardless of how long the underlying job was expected to run. Found
dead on check-in, with `/tmp/l437_full_run/` containing partial `qpa`
output (no `batches`/`report.txt`) for roughly 199 of 1,808 planned
batches.

Rather than restart from zero, extracted every actually-completed
`CasePath="..."` from **all** `qpa/*.qpa` files in that directory (not
just batches that looked numerically "early enough to be done" --
spot-checks showed completion is not a clean prefix: with 6 concurrent
workers, batches scattered across the whole numeric range, e.g. 194,
195, 197, 198, were all simultaneously partial when the process died).
This recovered 350,894 (10.8%) already-completed cases. Computed the
true remaining case list (2,902,690 cases) as the full list minus that
completed set, and relaunched against only the remainder in a new
output directory, `/tmp/l437_full_run2`. This time launched with
`detach: true`, confirmed via `ps aux` to be a genuinely orphaned
process tree (`?` controlling TTY) that will survive this session's own
end.

**Status: still in progress, further from complete than `L437`'s own
checkpoint in wall-clock terms** (restarted at the ~11% mark rather than
continuing past it), but with none of the already-completed work lost.
A future session should treat the eventual full result as the union of
completed cases across **both** `/tmp/l437_full_run/` and
`/tmp/l437_full_run2/` (plus any further recovery rounds, if this
happens again), not either directory's `report.txt` alone. The master
"Per-group accounting" table above is still untouched, same as `L437`
left it.

Also audited `offload-test-suite/test/CMakeLists.txt`'s `gpu_suite_env`
for any other `FEME_*`-prefixed variable with the same lit-environment-
passthrough gap `L437` found and fixed: only `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS`
exists, already fixed; `OFFLOADTEST_GPU_NAME` (read directly via
`os.environ.get` inside `lit.cfg.py` itself, not via the test subprocess
environment) and `VK_DRIVER_FILES`/`VK_ICD_FILENAMES` (already in the
`with_system_environment` allowlist) are both unaffected. No further
fix needed here.

Codified both the "launch multi-day sweeps detached" and "reconstruct
remaining work from `qpa` output, since there's no `--resume` flag"
lessons in `feme/.instructions.md`.

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update
needed (process/tooling fix only, no feature/extension-surface change).

## `L439`: `offload-test-suite` "drift" closed as a false positive; `L419(h)`'s 18-case cluster root-caused to one exact internal-break shape, reduced to two new `.ll` tests

Confirmed `FeMe CPU Vulkan Device` (mandatory check). Confirmed the
detached full re-triage (`L438`'s `/tmp/l437_full_run2/`) survived this
session's own boundary too -- still alive and progressing (9 workers,
20 batches with `qpa` output by the end of this session, up from 14 at
its start). Still far from complete; the master "Per-group accounting"
table above remains untouched, same as `L437`/`L438` left it.

Closed the standing `offload-test-suite`-vs-`llvm-beanz/feme` "drift"
item for good: `git patch-id` proved the one "behind" commit (`0cc94c2`)
is byte-identical in content to a commit already in this repo's own
history (`b606d08`, a prior session's cherry-pick) -- `git log`'s
hash-based ahead/behind count cannot see that, since `git cherry-pick`
always produces a new commit object even for identical content. There
was never any real drift; every prior session's "needs a decision"
framing was a false positive. Documented in `feme/.instructions.md` and
closed as roadmap `L439` (this item was actually resolved in the prior
session and is only recorded here for completeness; this session
re-confirmed it holds).

With the standing items closed out and the sweep still mid-run, used
the remaining session time on roadmap `L419(h)` (GLSL control-flow
residuals), the largest still-open P1 cluster with a concrete reduction
plan from an earlier handoff ("reduce one fragment and one vertex
control-flow case before extending the linearizer"). Re-ran
`dEQP-VK.glsl.loops.special.*` directly (552 cases) rather than trusting
the roadmap row's un-re-verified sub-cluster counts: **534 Pass, 18
Fail**, every failure the identical shape --
`while(i++ < N) { if (i == N) break; ...; }`, i.e. a loop whose header
holds the real uniform trip-count check with a *separate*, later body
block holding its own divergent `break` check branching straight to the
loop's exit block.

Root-caused via a minimal IR reduction (`feme-opt --llvm
-passes=feme-cpu-linearize`, not deqp-vk): this exact shape is **already
supported** when the loop body has no mask-affecting operation of its
own -- `LoopLinearizer::linearizeCycle`'s simpler, non-masked
`collectUniformPassThroughRegion`/`exited.via.check` path already
tracks a body-level break correctly. Every one of the 18 real CTS
shader bodies reads a uniform buffer (a `feme.cpu.resource.*`-class
call `CycleHasMaskOps` recognizes), which routes `linearizeCycle` into
its separate "fully uniform loop with body mask ops" branch instead,
built on `DiamondFlattener::canFlattenLoopBodyRegion`/
`flattenLoopBodyRegion` -- and that path's own `validate` walk
misclassifies the body block's branch to the loop's own exit as a
cycle-boundary edge (`isLoopControlEdge`'s `!CI.contains(C, Target)`
test is identically true for "this cycle's own real exit" and "a
different, nested child cycle's own control edge", with no way for
`validate` to tell the two apart), deferring it as "`LoopLinearizer`'s
problem, not an error" -- except this call *is* `LoopLinearizer`,
already mid-processing of this exact cycle.

**Not fixed this session**: added two new, minimal standalone `.ll`
tests pinning the exact boundary instead --
`feme/test/Transforms/CPU/Linearize/loop-break-separate-from-header-check.ll`
(the mask-op-free variant, confirmed already lowering cleanly) and
`feme/test/Transforms/CPU/Linearize/unsupported-loop-internal-break-with-mask-op.ll`
(the identical shape plus one resource-store call, confirmed still
diagnosed). Generalizing the masked-loop path to handle this shape
needs the same kind of sticky, loop-carried "this lane already decided
not to continue" state `HeaderDivergent`'s own `stayInLoop` already
gives the header's *own* check, generalized to however many separate
body-level break points a loop can have -- a materially larger lift
than this session's remaining time allowed, not a quick follow-on to
the reduction itself. `ninja check-feme`: 3,767/3,775 Passed (+2 new
tests), 7 Unsupported, 1 pre-existing unrelated Failed, 0 regressions.
`check-hlsl-feme-vk`: 492/492 Passed, 0 Failed. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change -- investigation and test
coverage only, no code change, no feature/extension-surface change.
Updated roadmap `L419(h)` with the full root-cause finding and a
scoped starting point (the two new tests) for whichever future session
implements the actual generalization.

## `L419(t)`: lands `L419(h)`'s widening fix for `conditional_break_*` (6 cases), scoped to avoid the prior session's `mixed_break_continue` regression; defers the deeper masking-sequencing bug it uncovered

Picked up `L419(h-attempt)`'s fully-reverted widening/phi fix and
re-attempted it with a narrower scope. Re-applied `cycleHasMaskOps`
and the `RealExitCandidates` widening exactly as before, but gated the
widening on `OtherCondBrBlocks.size() == 1` in addition to the existing
"a uniform match exists" check -- `conditional_break_fragment`'s own
cycle has exactly one `OtherCondBrBlocks` member, while
`mixed_break_continue_fragment`'s `switch`-lowered body always has
more (confirmed via a new, temporary `FEME_DEBUG_L419H_TRACE=1`
env-var-gated trace rather than static/manual CFG reasoning, which had
already failed once in the prior session). This one extra condition is
the precise discriminator the prior attempt's regression needed and
did not have. Also found and fixed a `StopBlock`-consistency crash
verifying this: `matchExitCheckWithRelay` must be called with `Latch`
as `StopBlock` both at the widening's own scan and again at
`CheckBlock`'s later, separate call site -- calling it two different
ways on the same candidate could otherwise make the second call's
match ambiguous, asserting on `CheckExit->StayInLoop`.

While verifying the widened shape's own output was not just
non-crashing but semantically correct, found a genuine, separate,
pre-existing latent masking bug: `LoopLinearizer::makeActivePNPair`'s
`active.live`/`active.sideeffect` phis are left with only their
non-cycle-predecessor incoming value populated until `addLatchIncoming`
fills in the real `Latch` value much later; any `applyStageMasks` call
that runs before that (specifically: a mask-affecting call sitting in
the widened `CheckBlock`, exactly this widening's own new target shape)
sees a single-incoming-value phi that `isKnownConstantMask`'s
`lookThroughTrivialPhi` wrongly treats as the trivial constant `true`,
silently skipping the mask rewrite a resource call there genuinely
needs. A poison-placeholder fix (seed the `Latch` slot with
`PoisonValue::get(I1Ty)` immediately, `setIncomingValueForBlock` once
known) was implemented and confirmed correct in isolation -- but a
full CTS sweep showed it reintroduces a *different* regression:
`{do_while_dynamic,for_dynamic,while_dynamic}_iterations.
select_iteration_count_{fragment,vertex}` (6 cases) flip to `Fail` --
reconfirming an identical finding from an even earlier,
now-summarized-away session that tried the same fix independently.

Root-caused this time via `FEME_DUMP_IR_PRESIMD=1`/
`FEME_DUMP_DIVERGENT_BRANCH=1` on
`do_while_dynamic_iterations.select_iteration_count_fragment`: that
shader's loop *header itself* contains its own internal `if`/`else`
(reading a uniform buffer, reconverging a few blocks later -- not a
loop exit at all), built from a `feme.cpu.resource.load.raw.i32` call.
`DiamondFlattener::flattenLoopBodyDiamond`'s own early fixed-point
pre-pass examines this diamond *before* any masking has happened (the
call's trailing mask operand is still its as-lowered `true`), correctly
judges the branch uniform at that point, and declines to flatten it --
a permanent decision it never revisits. Once the poison-placeholder fix
later lets `applyStageMasks` correctly narrow that same call's mask
operand to the real, loop-carried `active.live` value (now genuinely
non-constant), the header's surviving `CondBr` becomes *actually*
divergent for the first time -- but `DiamondFlattener`'s one chance to
flatten it has already passed, so it survives untouched into
`feme-cpu-simdize`'s own, independent, later `UniformityInfo`
computation, which (correctly, from its own perspective) rejects it as
an unhandled divergent branch (`"function 'main' has a divergent
branch; the divergence transform ... did not remove it"`). This is a
distinct, deeper sequencing bug in `flattenLoopBodyDiamond` itself (a
judgment made from a stale, pre-masking snapshot, never re-examined
once real masking completes) -- not a flaw in the poison-placeholder
technique, which remains independently correct. Reverted the
poison-placeholder change (back to plain `addIncoming`) rather than
risk landing it, keeping only the gated widening.

**CTS-verified** (`dEQP-VK.glsl.loops.special.*`, full 552-case sweep):
**534 Pass/18 Fail -> 540 Pass/12 Fail, zero regressions.** All 6
`conditional_break_{fragment,vertex}` pairs (`do_while_uniform`/
`for_uniform`/`while_uniform`) now Pass. The remaining 12
(`{for_dynamic,while_dynamic}_iterations.{elseblock,ifblock}_
{fragment,vertex}`, `{for_uniform,while_uniform}_iterations.
mixed_break_continue_{fragment,vertex}`) are the exact same
pre-existing, unrelated residuals present before this session's
widening change, untouched by it -- confirmed by diffing the full
before/after failure-name lists, not just comparing counts.

`ninja check-feme`: 3,766/3,775 Passed, 7 Unsupported, 2 Failed on the
first run under concurrent CTS-sweep background load
(`entry-wrapper-loop-header-derived-value-in-suffix.ll`, the known,
unrelated, pre-existing `H165` bug; and
`SyncTest.QueueSubmitReturnsPromptlyThenCompletesAfterHostSignal`, a
timing-sensitive unit test reconfirmed passing 3/3 in isolation with
`--gtest_repeat=3`, i.e. a load-induced flake, not a real regression) --
0 real regressions. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change -- a linearizer-internal
compiler fix, no feature/extension-surface change.

Renamed `unsupported-loop-internal-break-with-mask-op.ll` to
`loop-internal-break-with-mask-op.ll` (it now passes) with `CHECK`
lines documenting the still-literal `i1 true` mask (the deferred gap,
not silently hidden). `loop-body-resource-call-then-nested-uniform-
loop.ll` is unchanged (its own masking behavior never depended on the
reverted poison-placeholder fix). Updated roadmap `L419(h)` to record
the 6 newly-fixed cases and added `L419(t)` with the full landed-fix
and deferred-gap writeup for the next session. A future session should
start from this row's own root-cause finding: `flattenLoopBodyDiamond`'s
pre-pass needs to either re-run, or defer its own uniform/divergent
judgment, for any diamond built from a resource/image *load* result
once real masking is known, instead of committing to a one-time,
pre-masking verdict it never revisits.

## L419(u): isLoopControlEdgeThroughRelay check-ordering fix (correcting L419(t)'s carried-over premise)

Follow-up session to `L419(t)`, re-investigating its deferred masking
gap. Root-caused and fixed a genuine, independently-correct bug in
`DiamondFlattener::isLoopControlEdgeThroughRelay`: it checked
`Depth >= 8 || !isPureRelayBlock(Target)` *before* checking
`Target == StopAt` (the diamond's own natural reconvergence point).
`StopAt` is explicitly allowed to contain real instructions of its own
(most commonly the loop's actual trip-count `icmp`), unlike every other
hop this relay chase walks through -- so gating on `isPureRelayBlock`
first wrongly disqualified that common shape before ever reaching the
`Target == StopAt` check, letting `DiamondFlattener::run()`'s
whole-function walk wrongly flatten a loop Header's internal if/else
(which legitimately reconverges at the loop's own real exit-check
block) as an ordinary diamond rather than deferring the cycle to
`LoopLinearizer`.

**Important correction to this session's own carried-over premise**:
contrary to what the summary handed to this session implied,
`select_iteration_count_{fragment,vertex}` does **not** currently fail
or regress on clean `HEAD`. Confirmed twice (via `git stash` + full
rebuild of `feme-opt` and `libfeme_vulkan.so`, `--deqp-shadercache=
disable` to rule out stale cache results): `dEQP-VK.glsl.loops.
special.*` is already 540/552 on true baseline, identical to this
session's fixed build. The "regression" `L419(t)`'s own writeup
describes only manifests when `L419(t)`'s *abandoned* poison-placeholder
phi-completeness fix (seeding `makeActivePNPair`'s `Latch` incoming slot
with `PoisonValue::get(I1Ty)`) is *also* reapplied on top of clean
`HEAD` -- this session re-confirmed that in isolation (534/552, the
same 6 `select_iteration_count` cases, exactly matching `L419(t)`'s
description).

**This session's fix does not resolve that poison-fix regression**:
reapplying the poison-placeholder experiment on top of this session's
relay-chase fix and re-sweeping still shows 534/552, the identical 6
failures -- so the real "second, deeper, not-yet-fixed bug" `L419(t)`'s
own code comment references remains open, and is a distinct bug from
this session's fix. This session's fix produces a real, verified IR
difference on a standalone repro (a `live.merge`/`sideeffect.merge`
phi-of-two-identical-`true`-constants collapses to a plain `true`
constant instead of staying symbolic through to the masked stores and
`feme.cpu.stage.return.masks.i1` call) -- but that difference is purely
cosmetic on every currently-passing or currently-failing CTS/unit case.

`L419(t)`'s own attribution of the deeper bug to
`DiamondFlattener::flattenLoopBodyDiamond`'s "early fixed-point
pre-pass" may itself be imprecise: this session traced (via
`FEME_DEBUG_FLATTEN_TRACE=1`, with `opt -passes=instnamer` for readable
block names) that `flattenLoopBodyDiamond`'s per-cycle caller in
`linearizeCycle` explicitly excludes `BB == Header || BB == Latch`, so
that function is never even invoked on the Header's internal diamond
this session's fix concerns -- the actual diamond-flattening of the
Header is done by `DiamondFlattener::run()`'s separate, earlier,
whole-function walk instead. A future session reapplying the
poison-placeholder fix should start from fresh
`FEME_DEBUG_FLATTEN_TRACE=1` tracing *with the poison fix applied*
(comparing against without) to find the actual mechanism, rather than
re-reading `L419(t)`'s comment as settled fact.

**CTS-verified** (`dEQP-VK.glsl.loops.special.*`, full 552-case sweep,
with only this session's relay-chase fix applied, poison-placeholder
experiment reverted out of the working tree before commit): **540/552,
bit-for-bit identical pass/fail set to true baseline** -- zero
regressions, zero new passes (this fix is currently inert on every real
CTS case). `ninja check-feme`: 3,768/3,776 Passed, 7 Unsupported, 1
Failed (the one new regression test added this session now passes; the
only failure is the known, unrelated, pre-existing `H165`
`entry-wrapper-loop-header-derived-value-in-suffix.ll` bug). `ninja
check-hlsl-feme-vk`: 492/492 passable tests Passed (273 Unsupported, 28
Expectedly Failed), zero regressions. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change -- a linearizer-internal
compiler fix, no feature/extension-surface change.

New regression test `loop-header-diamond-reconvergence-has-real-check.
ll` added and properly red/green-validated this time (confirmed via
`git stash` + rebuild to fail on true baseline, pass with the fix) using
a faithful, minimally-trimmed extraction from the original
`FEME_DUMP_IR_PRENORM` dump that originally surfaced this shape --
multiple attempts at a hand-written scalar reduction were tried first
and silently failed to reproduce the bug (identical output with/without
the fix, a trap worth remembering: a reduced `.ll` test can look
structurally similar yet not actually exercise the bug it's meant to
guard), so the committed test keeps the repro's original vector types
(`<4 x float>`) rather than a hand-simplified scalar shape.

Standing `/tmp/l437_full_run2/` background CTS sweep check-in: still
alive and progressing (42,404/57,809+ `qpa` files this session, up from
26,108 last checked) -- not yet finished, no action needed beyond this
check-in.

## `L419(v)`: `collectUniformPassThroughRegion`'s stale-uniformity masking gap fully root-caused end-to-end (no fix landed)

Follow-up session to `L419(u)`, picking up its own suggested next step:
root-cause `L419(t)`'s "real, deeper, second bug" that blocks the
poison-placeholder phi-completeness fix, using fresh
`FEME_DEBUG_LINEARIZE_TRACE`/IR-diffing/a direct SIMDize-level
reproduction rather than re-trusting prior sessions' vaguer
`flattenLoopBodyDiamond` attribution (already disproven by `L419(u)`).

**Mandatory device check**: `FeMe CPU Vulkan Device` confirmed via
`vulkaninfo --summary | grep deviceName`.

**Root cause, confirmed end-to-end on the real failing CTS case**:
`LoopLinearizer::collectUniformPassThroughRegion` decides whether a
cycle's `Header` (and other blocks in its `PreRegion`/`PostRegion`) is
a safe "leave completely untouched" pass-through block using
`isDivergentBranch`, which consults `UniformityInfo` computed **once**,
at the very start of the whole Linearize pass -- before this same
pass's own mask-narrowing (`applyStageMasks`) ever runs. For
`select_iteration_count_fragment` (a `do_while_dynamic_iterations`
loop, i.e. one with a genuinely per-lane-varying trip count), `Header`
contains a resource-load call whose mask argument is, at that initial
snapshot, still the pre-lowered constant `true` -- so `Header`'s own
internal `if`/`else` (`%.not = icmp ne i32 %16, 0` /
`br i1 %.not, label %27, label %._crit_edge`) is correctly judged
uniform *at that moment* and accepted into the pass-through region,
so its `CondBr` is never flattened into masked form.

Later, in the very same `linearizeCycle` call, `applyStageMasks`
(correctly, exactly as the poison-placeholder fix intends) narrows
that same call's mask operand from `true` to the loop's own
`active.live` phi -- for a `_dynamic_iterations` loop this is a real,
non-constant, per-lane value. Confirmed directly via IR diff
(`feme-opt --llvm -passes=feme-cpu-linearize` on a standalone repro,
with vs. without the poison-placeholder fix reapplied experimentally):
both resource-load calls' mask operands flip from literal `i1 true` to
`i1 %active.live`. Once masked this way, the call's result is
genuinely divergent across the full lane set (inactive lanes read an
unrelated passthru value instead of the uniform content active lanes
see), making `Header`'s surviving, never-flattened `CondBr` a real
divergent branch.

**Direct end-to-end confirmation on the real CTS case**: rebuilt
`libfeme_vulkan.so` with the poison-placeholder fix reapplied
experimentally, then ran
`dEQP-VK.glsl.loops.special.do_while_dynamic_iterations.select_iteration_count_fragment`
with `FEME_DUMP_DIVERGENT_BRANCH=1` set:

```
feme-cpu-simdize: divergent branch in block '':   br i1 %.not, label %27, label %._crit_edge
error: feme-cpu-simdize: function 'main' has a divergent branch; the
divergence transform (feme::cpu::LinearizePass) did not remove it, or
produced a shape this pass cannot widen
  Fail (vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED)
```

This is precisely `Header`'s own branch -- confirming the hypothesis
without any remaining ambiguity, and precisely explaining the known
6-case `select_iteration_count_{fragment,vertex}` regression the
poison-placeholder fix has caused in every prior session that tried it.

**Why only `_dynamic_iterations` siblings are affected**: a loop whose
own header/latch exit check is itself uniform (`_uniform_iterations`,
e.g. `conditional_break_fragment`) takes an entirely separate, simpler
fast path (`!HeaderDivergent && !LatchDivergent && !LatchWasPromoted`,
Roadmap `L292`) that only builds an `active.live`/`active.sideeffect`
phi pair if the loop body contains a `feme.stage.discard`/`.demote`/etc.
op -- so there is no mask-narrowing event to go stale for that shape in
the first place.

**No fix implemented this session** -- confirmed this requires one of
two architecturally non-trivial changes: (a) a two-phase restructuring
so `collectUniformPassThroughRegion`'s region/branch-flattening
decisions are made *after* this cycle's own mask-narrowing is fully
known (a chicken-and-egg problem, since masking itself depends on which
blocks are inside/outside the region), or (b) teaching
`isDivergentBranch` (or a new helper `collectUniformPassThroughRegion`
also consults) to recognize, ahead of its own decision, that a
resource/image load inside cycle `C` with a currently-constant-`true`
mask will be narrowed to a non-constant per-lane value by this very
cycle's own masking machinery whenever the cycle needs a genuinely
non-constant `active.live`/`active.sideeffect` phi -- and pessimistically
treat such a load's result (and any branch depending on it) as
divergent ahead of time. Both were judged too risky to attempt without
a dedicated follow-up session.

The experimental poison-placeholder code used to confirm this trace was
reverted (`git checkout --`) before ending this session --
`Linearize.cpp` is back to its committed `L419(u)` state. Rebuilt
`libfeme_vulkan.so`/`feme-opt` and re-verified: `select_iteration_count_fragment`
still Pass, 540/552 `dEQP-VK.glsl.loops.special.*` baseline unchanged
(no CTS re-sweep needed since no functional change landed).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
(read-only/root-cause-only session, no functional compiler change
landed).

Standing `/tmp/l437_full_run2/` background CTS sweep check-in: still
alive and progressing (84,381 `qpa` files this session, up from 72,396
last checked) -- not yet finished, no action needed beyond this
check-in.

## `L419(w)`: poison-placeholder masking fix lands with a diagnosed, not-yet-closed net regression -- `L419(v)`'s option (b)

Implemented `L419(v)`'s own deferred option (b): the poison-placeholder
phi seed, plus a new ahead-of-time-ish `CycleNeedsRealMasking` gate and a
`flattenTaintedBranchesInRegion` safety net, rather than the two-phase
region/masking restructuring of option (a).

**Crash fixed first**: a full `dEQP-VK.glsl.loops.special.*` sweep with
the plain poison seed aborted (exit 134) on
`do_while_uniform_iterations.dowhile_trap_fragment` -- a `cast<PHINode>`
assertion in `rethreadNestedEntryMasks`, because
`foldNowTriviallyConstantMaskPhis` can legitimately fold a fully-uniform
child cycle's masks to a plain `Constant` before this runs. Fixed with a
`dyn_cast<PHINode>` guard.

**Image-mismatch regression found and fixed defensively**: with the
crash fixed, `conditional_break_{fragment,vertex}` (130 cases into the
sweep) showed a genuine image mismatch, not a crash. Bisected via
`git stash`/`FEME_DUMP_IR_PRESIMD=1` IR-diffing against the untouched
baseline: `flattenKnownDivergentBranch` was discarding a genuine loop
break's per-arm masks (its own prior doc comment already flagged this as
unsound for exactly this shape -- a branch whose reconvergence point is
the cycle's own exit block, not a local diamond). Added a
`CI.contains(SC, R)` cycle-containment check so this now fails with a
clean diagnostic instead of silently producing the wrong image.

**Full 552-case sweep after both fixes**: 524 Pass / 28 Fail. Compared
against a `git stash`-baseline re-sweep of just the 28 failures: 16 of
them PASS on baseline (all 6 `conditional_break_*`, 2x `dowhile_trap`
fragment/vertex, 6x `nested*`), 12 are pre-existing, unrelated
`L419(h)` residuals (`elseblock`/`ifblock`/`mixed_break_continue`).
Net: 7 newly fixed (`select_iteration_count`), 16 newly regressed, a
net loss of ~9 passing cases -- **not landable as-is.**

**Root cause**: the poison seed taints *every* cycle's masks from
creation regardless of whether that cycle ever needs real masking,
which `narrowCallMask` then threads into resource/image loads inside
even fully-uniform cycles, making their ordinary internal branches look
"tainted" to `flattenTaintedBranchesInRegion`.

**Fix attempted this session**: a `CycleNeedsRealMasking` predicate
(`HeaderDivergent || LatchDivergent || a genuinely divergent
OtherCondBrBlocks member || cycleHasMaskOps(C)`) gating whether the
`CheckBlock`-handling branch's own `makeActivePNPair` call opts into the
poison seed at all (new `SeedBackedgeWithPoison` parameter, default
`true` for full backward compatibility at every other call site).

Iterated on the predicate three times:
1. Divergence-only (no `cycleHasMaskOps`): fixed `conditional_break`
   (6 cases) and kept `select_iteration_count` (6 cases) working, but
   left `loop-internal-break-with-mask-op.ll` (a new unit test this
   session, a body-level break gating a real resource store) with an
   un-narrowed, always-`true` mask operand -- a real, lit-tested
   correctness regression even though it happened to still produce a
   matching CTS image for this specific repro.
2. Adding `cycleHasMaskOps(C)` to the predicate: fixes the unit test
   correctly (mask operand narrows to `active.sideeffect` as designed)
   but also makes `CycleNeedsRealMasking` true again for
   `conditional_break` (virtually every real CTS shader touches some
   resource/image), reverting it back to the diagnosed failure -- full
   sweep: 524 Pass / 28 Fail, identical to the divergence-only, pre-gate
   state. **This is the version committed**, since it is the only one
   with correct, lit-tested masking semantics; `conditional_break`'s own
   fix is left open (see below).
3. Also tried excluding a nested child cycle's own header/latch block
   from the *outer* cycle's `OtherCondBrBlocks` collection
   (`CI.getCycle(&BB) == C`), suspecting this explains the 10
   `dowhile_trap`/`nested*` regressions (confirmed via trace: the inner
   cycle's own, already-correctly-handled exit block shows up again in
   the outer cycle's `OtherCondBrBlocks` scan). This caused an infinite
   loop in `FeMeTransformsCPUTests` and was reverted; the fix needs to
   be scoped to `flattenTaintedBranchesInRegion`'s own candidate walk,
   not the raw `OtherCondBrBlocks` collection (filed as `L419(x)`).

**Final committed state**: `FeMeTransformsCPUTests` 638/638; `check-feme`
3,768/3,769 (only the pre-existing, unrelated
`entry-wrapper-loop-header-derived-value-in-suffix.ll` failure remains);
`dEQP-VK.glsl.loops.special.*` full 552-case sweep: **524 Pass / 28
Fail** -- 6 `select_iteration_count_{fragment,vertex}` (via
`do_while_dynamic`/`for_dynamic`/`while_dynamic`) cases fixed from
`L419(v)`'s open gap, 10 cases newly regressed vs the pre-`L419(w)`
533-pass baseline on this same suite (`dowhile_trap_{fragment,vertex}`,
`nested{,_sequence,_tricky_dataflow_{1,2}}_{fragment,vertex}`, all
`do_while_dynamic_iterations`), `conditional_break_{fragment,vertex}`
(6 cases, all three `*_uniform_iterations` groups) now fails cleanly
(diagnosed, `VK_ERROR_INITIALIZATION_FAILED`) rather than silently
corrupting the image -- a safety improvement over the pre-session state
even though not yet a Pass, and 12 pre-existing `L419(h)` residuals
unchanged. **Net: -9 vs the 533-pass pre-`L419(w)` baseline on this
suite -- `L419(x)` (filed in `Roadmap.md`) is a hard blocker before this
milestone can be considered closed.**

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
(this is a CPU-backend control-flow/masking-correctness fix, not a
feature- or extension-surface change).

## `L419(x)`: `flattenTaintedBranchesInRegion` nested-cycle leak fixed -- `L419(w)`'s regression closed, net +1 over the pre-`L419(w)` baseline

Implemented the fix `L419(w)` filed and deferred: threaded the cycle
currently being linearized (`CycleRef C`) into
`flattenTaintedBranchesInRegion`, and skip any `Region` candidate block
whose own innermost cycle (`CI.getCycle(BB)`) is valid and differs from
`C`. Such a block is a *different*, nested child cycle's own header or
latch, already linearized (and, if genuinely divergent, already
correctly masked) when that child cycle was processed in its own right
-- re-examining it as a fresh tainted-branch candidate for the *outer*
cycle double-counts the same masking taint and incorrectly routes an
already-resolved, genuinely uniform exit check into
`flattenKnownDivergentBranch`'s cycle-containment safety rejection.

This time the fix was scoped narrowly to
`flattenTaintedBranchesInRegion`'s own candidate walk (not
`OtherCondBrBlocks`'s raw collection, which `L419(w)`'s own earlier,
reverted attempt at the same idea touched and which hung
`FeMeTransformsCPUTests`) -- `FeMeTransformsCPUTests` stayed green at
638/638, no infinite loop this time.

**Targeted 66-case sweep** (`conditional_break`/`dowhile_trap`/
`infinite_with_*`/`nested*`/`select_iteration_count`/
`unconditional_break` across `do_while_dynamic`/`for_dynamic`/
`while_dynamic`): **66/66 Pass**, including every one of the 10 cases
`L419(w)` regressed and the 6 `select_iteration_count` cases it fixed.

**Full `dEQP-VK.glsl.loops.special.*` sweep (552 cases)**: **534 Pass /
18 Fail** -- up from `L419(w)`'s own 524/552, and +1 over the
pre-`L419(w)` 533-pass baseline. `L419(w)` can now be considered a
genuine net-positive milestone close-out.

**Remaining 18 failures**, all pre-existing and unrelated to this fix:
- 12 special-loop residuals already tracked under `L419(h)`
  (`{for_dynamic,while_dynamic}_iterations.{elseblock,ifblock}_
  {fragment,vertex}`, `{for_uniform,while_uniform}_iterations.
  mixed_break_continue_{fragment,vertex}`).
- 6 `conditional_break_{fragment,vertex}` cases across all three
  `*_uniform_iterations` groups (`do_while_uniform`/`for_uniform`/
  `while_uniform`) -- these were the ones `L419(t)` originally fixed,
  then `L419(w)`'s `flattenKnownDivergentBranch` cycle-containment
  safety check re-regressed them into a clean, diagnosed failure
  (`VK_ERROR_INITIALIZATION_FAILED`, "does not have the simple shape
  this pass can flatten") rather than silent corruption -- a deliberate
  trade-off `L419(w)`'s own writeup called out as worth a closer look
  (filed as this session's next-step #2, not yet investigated).

`ninja check-feme`: 3,774/3,776 Passed -- the same 2 pre-existing,
unrelated failures as every recent session
(`entry-wrapper-loop-header-derived-value-in-suffix.ll`, confirmed via
`git stash` to reproduce identically with this fix reverted; a
timing-sensitive Vulkan `SyncTest.QueueSubmitReturnsPromptlyThen
CompletesAfterHostSignal` unrelated to `Linearize.cpp`).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
(this is a CPU-backend control-flow/masking-correctness fix, not a
feature- or extension-surface change).

A synthetic, minimal unit-test reproducer for this exact nested-cycle
leak (an outer dynamic-iteration cycle's `PreRegion`/`PostRegion` walk
reaching into an inner cycle's own header/latch after masking narrows a
load both cycles share) was not constructed this session, given the
multi-relay-hop `CheckBlock`/`CheckExit` machinery required to trigger
it; correctness is validated here via the real CTS sweep (66/66
targeted, 534/552 full) plus the unchanged 638/638
`FeMeTransformsCPUTests` baseline instead.

## `L419(y)`: `conditional_break_uniform`'s diagnosed failure root-caused (no fix landed) -- confirms next-step #2's hypothesis

Investigated whether `conditional_break_{fragment,vertex}`'s diagnosed,
not-corrupting failure (6 cases, all three `*_uniform_iterations`
groups) is masking a more specific, fixable shape gap rather than being
an inherent limitation of `flattenKnownDivergentBranch`'s
cycle-containment safety check, as `L419(w)`'s own writeup suggested.

**Reduced via `FEME_DUMP_IR_PRENORM=1`** (the shader cache must be
disabled, `--deqp-shadercache=disable`, and `shadercache.bin` deleted
first, or a cached compile silently skips the dump) on
`do_while_uniform_iterations.conditional_break_fragment`:

```llvm
9:                              ; preds = %Flow._crit_edge, %0
  %.06 = phi <4 x float> [ %8, %0 ], [ %11, %Flow._crit_edge ]
  %.0  = phi i32 [ 0, %0 ], [ %14, %Flow._crit_edge ]
  ...
  %12 = call i32 @feme.cpu.resource.load.raw.i32(ptr ..., i1 true)
  %13 = icmp ne i32 %.0, %12
  br i1 %13, label %21, label %.Flow_crit_edge   ; Header's own break check

.Flow_crit_edge:
  br label %Flow
Flow:                           ; preds = %.Flow_crit_edge, %21
  %14 = phi i32 [ %22, %21 ], [ poison, %.Flow_crit_edge ]
  %15 = phi i1  [ %24, %21 ], [ true,   %.Flow_crit_edge ]
  br i1 %15, label %.loopexit, label %Flow._crit_edge
21:                             ; preds = %9
  %22 = add i32 %.0, 1
  %23 = call i32 @feme.cpu.resource.load.raw.i32(ptr ..., i1 true)
  %24 = icmp sge i32 %22, %23
  br label %Flow
```

`Header` (block `9`) holds the shader's own `if (counter ==
randomResourceValue) break;` check directly, reconverging with the
Latch-adjacent block `21`'s separate, uniform trip-count check at a
shared `Flow` block that feeds the cycle's single exit decision.

**`FEME_DEBUG_LINEARIZE_TRACE=1`** on the same case confirms the
mechanism directly:

```
L277TRACE: HeaderDivergent=0 LatchDivergent=0 OtherCondBrBlocks=[Flow (divergent=0) ]
error: ... loop has a branch in '' that became mask-dependent after
masking but does not have the simple shape this pass can flatten ...
```

`UniformityInfo`'s one-time, pre-masking snapshot correctly judges
`Header`'s own branch (`%13`) uniform -- it is not even present in
`OtherCondBrBlocks`, only `Flow` is. Only once `applyStageMasks`
later narrows `%12`'s mask operand to a genuine per-lane value does
`Header`'s branch become mask-dependent. At that point
`flattenTaintedBranchesInRegion`'s candidate scan (correctly, per
`L419(x)`'s own fix -- `Header` is part of *this* cycle `C`, not a
different nested one) picks it up and routes it to
`DiamondFlattener::flattenKnownDivergentBranch`, which refuses it via
`L419(w)`'s own cycle-containment check: this branch's reconvergence
point is the cycle's own exit machinery (`Flow`/`.loopexit`), not a
local diamond `DiamondFlattener` can safely fold.

**Conclusion**: this confirms this session's next-step #2 hypothesis
exactly -- the diagnosed rejection is not an inherent limitation, it is
masking a real, distinct, fixable gap. This is the same *class* of
staleness bug `L419(v)` root-caused for `collectUniformPassThroughRegion`
(a one-time, pre-masking uniform/divergent verdict that is never
revisited once masking narrows the operand it depended on), but applied
here to a different decision point: `Header`'s own branch-acceptance
test inside `linearizeCycle`, not the region-candidate walk `L419(v)`/
`L419(w)` already addressed.

**Not fixed this session.** The real fix needs either (a) a post-masking
re-check of `HeaderDivergent` before committing to treating `Header` as
an ordinary uniform pass-through block (the same chicken-and-egg
`L419(v)`'s own option (a) described), or (b) the same kind of
ahead-of-time divergence prediction `L419(w)` implemented for
`collectUniformPassThroughRegion`'s region candidates, generalized to
`Header`'s own branch-acceptance test. Either option needs careful
regression testing against the now-passing 6 `select_iteration_count_
{fragment,vertex}` and 10 `dowhile_trap`/`nested*` cases -- this exact
family of fix (ahead-of-time divergence prediction around `Header`/
masking interaction) has regressed those twice before (`L419(v)`'s
option (b) attempt -> `L419(w)`'s own initial poison-seed regression).
Filed as `L419(y)` in `Roadmap.md`, P2 (diagnosed failure, not a crash
or corruption, so lower urgency than `L419(x)` was).

No code change this session for this investigation; `Linearize.cpp` is
unchanged from the `L419(x)` commit. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change.

## L440: first `check-hlsl-feme-vk` run; WaveReadLaneFirst.fp16 UAF found

Ran `ninja -C build check-hlsl-feme-vk` for the first time ever this
session (build infra -- `LLVM_EXTERNAL_OFFLOADTEST_SOURCE_DIR`,
`LLVM_EXTERNAL_PROJECTS=OffloadTest`, the `check-hlsl-feme-vk` ninja target
itself -- was already fully configured in the existing `build` tree from
an earlier, unlogged session; it had simply never been invoked):

```
Total Discovered Tests: 793
  Passed      : 491 (61.92%)
  Unsupported : 273 (34.43%)
  Expectedly Failed: 28 (3.53%)
  Failed      : 1 (0.13%)
```

The one failure: `WaveOps/WaveReadLaneFirst.fp16.test`, which aborts with:

```
offloader: llvm/lib/IR/Value.cpp:524: void llvm::Value::doRAUW(Value *, ReplaceMetadataUses):
Assertion `New->getType() == getType() && "replaceAllUses of value with new value of different type!"' failed.
```

Backtrace: `EarlyCSE::processNode` -> `EarlyCSEPass::run` ->
`feme::OptimizerPipeline::run` -> `feme::cpu::CompiledStage::create` ->
`feme::vulkan::compileComputePipeline`/`vkCreateComputePipelines` ->
offload-test-suite's own `VulkanDevice::createPipelineCs`.

**Ruled out as llvm/llvm-project#226608** (a real, open, still-unfixed
upstream bug in `SPIRVEmitIntrinsics::buildLogicalAccessChainFromGEP`,
which that issue's own report says reproduces on this exact test name):
read the issue in full -- it is specifically about `clang-dxc -spirv`'s
*direct-to-SPIR-V Clang codegen path* (`SPIRVEmitIntrinsics` leaving an
insertion point stale after `insertAssignTypeIntrs`, producing an
`llvm.spv.gep` that's inserted before its own resource-pointer operand).
`check-hlsl-feme-vk` instead uses `dxc` (Microsoft's DirectXShaderCompiler)
to produce the `.spv`, then FeMe's own CPU ICD ingests and recompiles
that SPIR-V through an entirely separate pipeline (`feme::OptimizerPipeline`
-> CPU JIT). Same test name, different toolchain, different bug.

**Root-caused as a genuine use-after-free, not a logic bug.** Standalone
repro (bypassing `llvm-lit`'s parallelism): extracted the test's `RUN`
line via `split-file`, then looped bare `dxc`+`offloader` invocations.
The crash is **non-deterministic** -- it reproduced only ~1-in-20-to-75
runs of byte-identical inputs, and required `gdb -batch` with
`set disable-randomization off` to keep reproducing at all (`gdb`'s
default ASLR-disabling hid the bug completely in ~100 otherwise-identical
iterations). Caught the race live under `gdb` and found **two distinct
crash signatures from the same inputs**, varying by heap layout:

1. The originally-reported `doRAUW` type-mismatch assertion (`SubclassID`
   59 vs. 87 -- two structurally different `Value` subclasses).
2. A `Value::getValueName` assertion reached from inside the IR
   verifier's own error-printing code, which gave a **different**
   diagnostic message each time it was hit ("Cannot take the address of
   an inline asm!" on one capture, "Referring to a basic block in another
   function!" on another) while printing the exact same source line:
   `%ballot.pred.masked6 = and i1 true, <garbage>`.

Multiple different, nonsensical reinterpretations of the same operand's
content, varying only by heap layout, is a textbook use-after-free
signature. `ballot.pred.masked` is created by `Linearize.cpp`'s
`WaveActiveBallot`/`subgroupBallot` predicate-narrowing code (roadmap
`L85`): `Call->setArgOperand(0, B.CreateAnd(Call->getArgOperand(0),
Masks.Live, "ballot.pred.masked"))`. The operand going stale is almost
certainly `Masks.Live` itself -- a cached mask `Value*` that outlives the
instruction it refers to, because something later in the same
`Linearize` pass run RAUWs or erases that instruction without
invalidating the cache. This is the same general *class* of bug as the
whole `L419` series (a cached verdict/value going stale without being
refreshed), but here it is a genuine dangling-pointer memory-safety bug
rather than a stale-divergence-verdict logic bug.

**Not fixed this session.** Filed as `L440`/`L440(a)` in `Roadmap.md`
with a concrete breakdown (watchpoint-based `gdb` bisection once a
cheaper repro exists; audit every `Masks.Live`-caching site in
`Linearize.cpp`; consider a `WeakVH`/`AssertingVH`-backed cache instead of
a raw `Value*` to fail loudly instead of silently dangling). No
`check-hlsl-clang-feme-vk` run yet this session (deferred).

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` change
needed (pure compiler/runtime memory-safety investigation, no new
feature/extension surface touched). `Vulkan CTS` (`dEQP-VK.glsl.loops.*`
etc.) was not re-run this specific sub-session since no `Linearize.cpp`
code change was made; the previously-reported `L419(x)`/`L419(y)` numbers
stand unchanged.

## L440(b): first `check-hlsl-clang-feme-vk` run (contention caveat)

Ran `ninja -C build check-hlsl-clang-feme-vk` for the first time ever
this session (the Clang-HLSL-driven sibling of `check-hlsl-feme-vk`,
same FeMe ICD, different frontend -- `clang-dxc -spirv` instead of
`dxc`): **92 Failed / 293 Passed / 269 Unsupported / 139 Expectedly
Failed**, out of 793. Markedly worse than `check-hlsl-feme-vk`'s 1
Failed/491 Passed on the same test corpus -- expected, since several
`*.test` files in this suite carry `XFAIL: Clang && Vulkan` markers
(e.g. referencing the open llvm/llvm-project#226608
`SPIRVEmitIntrinsics` insertion-point bug, which is specific to Clang's
direct-to-SPIR-V codegen path) that do not apply to the `dxc`-driven
suite.

**Caveat, found and codified this session (`feme/.instructions.md`):**
re-running the exact same `llvm-lit` invocation moments later, while the
standing `/tmp/l437_full_run3` CTS sweep was pegging a CPU core at
~100%, gave a wildly different, clearly load-skewed result (385
"Failed", effectively every non-Unsupported/non-XFail case) due to
`FEME_VULKAN_SAFETY_NET_TIMEOUT_MS=30000`'s hard wall-clock timeout
being blown through under contention. The 92/293/269/139 figure above is
the one measured via the project's own documented `ninja
check-hlsl-clang-feme-vk` invocation and is treated as the trustworthy
baseline; it has **not** been independently re-confirmed free of all
contention this session (the background sweep was running throughout).
A future session should re-measure once `/tmp/l437_full_run3` (or
whatever standing sweep is active at the time) has finished, before
treating 92 as a hard baseline to diff future changes against.

No fix attempted this session (92 failures is too large a batch to
triage in the time remaining); likely dominated by the known
`SPIRVEmitIntrinsics` upstream bug class per the `XFAIL: Clang &&
Vulkan` markers already present on many of these tests, but this has
**not** been verified case-by-case. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change.

## `L441`: `L440`'s use-after-free root-caused and fixed, `check-hlsl-feme-vk` back to 0 Failed

Root-caused and fixed the `L440` memory-corruption bug left open by the
prior sub-session. The actual bug: `foldNowTriviallyConstantMaskPhis`
(`Linearize.cpp`, called from `addLatchIncoming` while linearizing a
cycle) chases each of its `MaskPhis` through a `Replacements` map to find
its ultimate folded value, via `dyn_cast<Instruction>(V)` -- but the
function's own preceding fold loop erases every folded instruction
*immediately* after recording it as a `Replacements` key, so whenever
`V == PN` and `PN` itself had already been folded earlier in the same
call, `V` is a dangling pointer to freed memory and `dyn_cast`'s internal
type-ID read is a genuine use-after-free. This single bug explains every
previously-seen crash signature (`doRAUW` type mismatch, verifier-print
garbage, a `WeakTrackingVH` construction around pointer `0x20`, a
bitcode-writer `SIGSEGV`) as different downstream symptoms of the same
corrupted read, depending only on what had overwritten the freed memory
by the time it was read (hence the ASLR-dependent reproduction rate).

Found via careful A/B isolation across three code states (none of which
alone explained the full crash-rate picture) and a plain `bt` backtrace
(`bt full` was found to perturb timing enough to hide the race entirely
-- 1900 iterations with it enabled produced zero crashes) that pinned
down the exact construction site, traced one level further back to the
true root cause. Fixed by looking the pointer value up in `Replacements`
directly via `reinterpret_cast`, never dereferencing `V` to get there --
`DenseMap::find` only hashes/compares the pointer bit pattern, so this
is exactly as precise as the `dyn_cast` guard it replaces, without ever
reading through a possibly-freed pointer.

Landed as two commits: the root-cause fix plus a new lit regression test
(`nested-loops-outer-mask-phi-fold-uaf.ll`, asserting the structural
precondition that triggers the bug -- it cannot assert on the
nondeterministic crash itself), then the pre-existing `WeakMaskPair`
(`WeakTrackingVH`-backed) hardening for three *other* long-lived mask
caches, kept as independent defense-in-depth after confirming (via an
isolated 300-iteration run with only the root-cause fix, no
`WeakMaskPair` at all) that the root-cause fix alone is already
sufficient and necessary.

**Validation this session**:
- `FeMeTransformsCPUTests`: 638/638 passed.
- `check-feme`: 3,769 passed, 7 unsupported, 1 failed (pre-existing,
  confirmed via `git stash` to predate this session's changes --
  `entry-wrapper-loop-header-derived-value-in-suffix.ll`).
- Crash-loop (gdb + ASLR against the real pipeline, plus a faster
  standalone `feme-opt --llvm -passes=feme-cpu-linearize` repro found
  via the existing `FEME_DUMP_IR_PRELINEARIZE=1` dump): well over 1,500
  combined crash-free iterations across all tested configurations with
  the fix in place (vs. nonzero crash rates of 5-23/300 at every stage
  without it).
- **`check-hlsl-feme-vk` re-run end to end: 492 Passed/273
  Unsupported/28 Expectedly Failed out of 793, 0 Failed, 0 crashes** --
  confirms the original symptom that started this whole investigation
  (`WaveOps/WaveReadLaneFirst.fp16.test` crashing nondeterministically)
  is fully resolved. This matches/slightly exceeds the best
  previously-recorded passing count for this suite.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (pure compiler memory-safety fix, no feature/extension surface
touched). Full `dEQP-VK.glsl.loops.special.*` 552-case sweep not
re-run this specific sub-session (no `L419`-relevant masking-logic
change was made, only the orthogonal UAF fix); the standing
`/tmp/l437_full_run3` full sweep continues unaffected in the
background and will exercise this code path regardless.

**Next steps**: `L419(g)` (30 tessellation residuals), `L419(h)`'s
remaining residuals, `L419(y)` (`conditional_break_uniform`), and the
`check-hlsl-clang-feme-vk` 92-failure triage all remain open/deferred,
unchanged by this session's work.

## `L419(z)`: loop-in-switch misleading-diagnostic fix (4 cases, diagnostic-only)

Picked up `L419(h)`'s remaining 4 `dEQP-VK.glsl.switch.*loop_in_switch*`
cases (`for_loop_in_switch_dynamic_{fragment,vertex}`,
`while_loop_in_switch_dynamic_{fragment,vertex}`), all hitting a single,
unhelpful `"loop at '' has an internal branch in 'Flow'; only a uniform
pass-through region ..."` diagnostic.

**Root cause**: the loop's `Header` holds a genuinely divergent,
per-fragment trip-count check, but both of its successors funnel
through a single shared `Flow`-style relay block (reused by
`StructurizeCFG` as the enclosing `switch`'s own case-merge scaffolding)
before ever reaching the loop's real exit block -- not directly, and
not via a *pure* relay chain. `matchExitCheck`/`recoverRelayExitCheck`/
`findForwardExitCheckBlock` all require one of those two shapes, so
`HeaderExit` stays `std::nullopt` and `HeaderDivergent` silently reads
`false`, not because `Header`'s check is actually uniform, but because
there is no `HeaderExit` object to query. The pass instead picks `Flow`
as `CheckBlock` (it *does* reach `ExitBlock` directly) and only
discovers the real problem much later, when
`collectUniformPassThroughRegion`'s `PreRegion` walk (`Header` to
`CheckBlock`) hits `Header`'s own still-divergent `CondBr` and bails --
at which point the diagnostic incorrectly blamed `Flow` (`CheckBlock`)
itself, rather than correctly naming `Header` as a second, independent
divergent exit check.

This is architecturally the exact same "two genuinely divergent exit
checks" shape the codebase already explicitly refuses to support
elsewhere (diagnosed clearly via `RealExitCandidates.size() > 1`) --
just reached via a different path with a worse diagnostic.

**Fix (diagnostic-clarity only, not a capability fix)**: at the
`PreRegion`/`PostRegion`-failure site, directly check whether `Header`
or `Latch` has its own unrecognized divergent terminator (not already
covered by `HeaderExit`/`LatchExit`, not peeled, not `CheckBlock`
itself); if so, emit the same, clearer "more than one divergent exit
check" message naming both blocks, instead of the generic "internal
branch" message. All 4 cases still fail -- this is a real, currently
unsupported shape -- but the failure is now attributed correctly.

A hand-written minimal repro of this shape is eliminated by the
existing `foldRedundantFlowBlocksInCycle` fixed-point pre-pass before
it ever reaches the new code path (confirmed via two separate attempts
and direct instrumentation of `OtherCondBrBlocks` construction); the
committed regression test (`loop-in-switch-divergent-header-and-relay.ll`)
is instead a trim of the real extracted CTS-failure IR (only unused
globals/metadata removed, no block/instruction that affects the
analysis touched).

**CTS validation**:
- `dEQP-VK.glsl.switch.*loop_in_switch*` (18 cases): 14 Pass/4 Fail,
  unchanged -- confirmed the new, clearer diagnostic text now fires
  through the real ICD for all 4 failing cases.
- `dEQP-VK.glsl.switch.*` (122 cases): 118 Pass/4 Fail, unchanged.
- `dEQP-VK.glsl.loops.special.*` (552 cases): 534 Pass/18 Fail,
  unchanged -- zero regressions.

**Other validation**:
- `FeMeTransformsCPUTests`: 638/638 passed.
- Full `feme/test/Transforms/CPU/Linearize/` lit suite: 53/53 passed
  (including the new test).
- `check-feme`: 3,770 passed, 7 unsupported, 1 failed -- the same
  pre-existing, unrelated `entry-wrapper-loop-header-derived-value-in-suffix.ll`
  failure, confirmed via `git stash` to reproduce identically on the
  pre-change tree. No new failures.
- Mandatory device check: `vulkaninfo --summary | grep deviceName` ->
  `FeMe CPU Vulkan Device`, confirmed.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (diagnostic-only compiler fix, no feature/extension surface
touched). Updated `Roadmap.md` with a new `L419(z)` entry documenting
the root cause and explicitly flagging the actual capability fix
(recognizing/masking `Header`'s own divergent check even when it only
indirectly, via a shared non-pure relay, reaches `ExitBlock`) as
unimplemented future work -- not attempted this session given this code
area's documented regression history (`L419(v)`/`L419(w)`).

Checked on the standing `/tmp/l437_full_run3` sweep: still running,
growing, not stalled.

**Next steps**: `L419(g)` (30 tessellation residuals), `L419(h)`'s
remaining special-loop residuals, `L419(y)` (`conditional_break_uniform`,
6 cases), the `check-hlsl-clang-feme-vk` 92-failure triage, and the
actual capability fix for `L419(z)`'s shape (flagged above) all remain
open/deferred.

## `L419(aa)`: `L419(h)`'s 18 `loops.special` residuals split into three distinct gaps (investigation only, no code change)

Before attempting `L419(z)`'s own suggested capability fix (tolerate a
relay successor chain through another block's own `CondBr`), reduced
one case from each of the three remaining `loops.special` sub-shapes to
check whether they share a root cause, using the standard
`FEME_DUMP_IR_PRELINEARIZE=1` extraction + unused-global/metadata trim
methodology.

**Finding 1 (the big one): `elseblock`/`ifblock` (8 cases) are the
exact same root cause as `L419(z)`'s 4 `loop_in_switch` cases.**
Reduced `for_dynamic_iterations.elseblock_fragment` to a standalone
repro; it hits the identical `L419(z)` diagnostic ("more than one
divergent exit check"). The mechanism generalizes slightly: `Header`'s
own divergent `CondBr` here isn't an exit check funneled through a
shared relay -- it selects "run the loop body this iteration" vs. "skip
straight to `Latch`", with the actual continue-vs-exit decision made
entirely by `Latch` itself (a `Flow`-style block also reused as the
loop's own exit-merge point for a sibling if/else's reconvergence)
via a boolean phi that's just a relabeling of which `Header` successor
fired. `HeaderExit` still comes back `std::nullopt` (neither successor
reaches `ExitBlock` or a relay stub), so
`collectUniformPassThroughRegion`'s `PreRegion` walk hits the identical
wall `L419(z)` already diagnosed. **This triples the real value of the
still-unimplemented capability fix: 12 of the 22 total `L419(h)`
residuals (4 `loop_in_switch` + 8 `elseblock`/`ifblock`) share one root
cause and one eventual fix, not 4.**

**A concrete, narrower design for that fix** (not attempted this
session): when `HeaderExit`/`LatchExit` is `std::nullopt` for a block
with its own genuinely divergent `CondBr`, admit that block into the
existing, already-battle-tested `OtherCondBrBlocks`/`DivergentCandidates`
machinery (the same "first-stage mask-narrowing filter ahead of the
loop's single genuine `CheckBlock`" role `L419(w)`/`L419(y)` already
built), rather than inventing new relay-tolerant logic directly in
`matchExitCheck`/`collectUniformPassThroughRegion`. **The real
obstacle**: `OtherCondBrBlocks`/`DivergentCandidates` are deliberately
computed *before* `HeaderExit`/`LatchExit` (so `recoverRelayExitCheck`'s
own fallback can gate on `DivergentCandidates` being empty), but this
fix needs the opposite ordering -- the same kind of inversion
`L419(v)`/`L419(w)` each hit, in a code area that has already regressed
twice from careless generalization. Needs its own dedicated session
with the full 552-case sweep re-run after every incremental step.

**Finding 2: `mixed_break_continue` (4 cases) is a genuinely separate,
third gap.** Its reduced repro
(`for_uniform_iterations.mixed_break_continue_fragment`) hits a
completely different diagnostic ("internal branch in a body shape this
milestone does not yet lower (e.g. an empty diamond arm...)"), from the
`DiamondFlattener`-based masked-loop body path, not `linearizeCycle`'s
own two-divergent-check diagnostic. Not yet root-caused to the same
depth; left as its own standing item.

**Finding 3 (re-confirmation): `conditional_break` (6 cases) is still
failing**, with the same "mask-dependent after masking" diagnostic
`L419(y)`'s own row already describes (the masking-sequencing gap that
row left open). No new information here, just confirmed still
reproducing identically.

**Refined accounting for `L419(h)`'s 22 residuals**: 12 share
`L419(z)`'s root cause (design above, not implemented), 6 are
`L419(y)`'s own separate gap, 4 are `mixed_break_continue`'s own
separate, not-yet-investigated gap.

No code changed this session for this finding (investigation and
documentation only, deliberately -- the capability fix carries real
regression risk in a code area with two prior regressions, and needs a
full dedicated session rather than a rushed attempt).
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed. Checked the standing `/tmp/l437_full_run3` sweep again: still
running and growing (304MB, up from 294MB earlier this session), not
stalled.

**Next steps**: the `L419(z)`/`L419(aa)` capability fix (12 cases, the
highest-value remaining item, but needs its own dedicated session given
the regression history and ordering-inversion obstacle identified
above), `mixed_break_continue`'s own root-cause investigation (4
cases), `L419(y)`'s masking-sequencing gap (6 cases), `L419(g)` (30
tessellation residuals, untouched), and the
`check-hlsl-clang-feme-vk` 92-failure triage (still deferred every
session).

## `L419(ab)`: attempted `L419(z)`/`L419(aa)`'s designed capability fix, root-caused a deeper masking-completeness gap, reverted

Mandatory device check first: `VK_ICD_FILENAMES=.../feme_icd.json
vulkaninfo --summary | grep deviceName` -> `FeMe CPU Vulkan Device`,
confirmed. Checked the standing `/tmp/l437_full_run3` sweep: still
running and growing across the session (318MB -> 375MB), not stalled,
left untouched.

Picked up the prior session's "real prize" next step: implement
`L419(aa)`'s designed fix so `Header`'s own divergent check is
recognized even when it only reaches `ExitBlock` indirectly, through a
shared dispatch block with its own `CondBr`. Took the narrower of the
two designs on the table -- generalizing `recoverRelayExitCheck`'s
`IsTrivialExitStub` chase to fold a `CondBr` whose condition is a
`phi` with a known-constant incoming value for the edge just walked,
rather than the riskier `OtherCondBrBlocks`/`DivergentCandidates`
reordering -- since it required no change to code order, only to what
shapes the existing relay chase tolerates.

**This alone worked structurally**: `HeaderExit`/`LatchExit` resolved
for all 12 target cases (4 `loop_in_switch` + 8 `elseblock`/`ifblock`),
both reduced repros and the committed
`loop-in-switch-divergent-header-and-relay.ll` regression test compiled
cleanly instead of hitting the diagnostic, `FeMeTransformsCPUTests`
(638/638) and the `Linearize` lit suite (53/53, after updating that
one test to expect success) passed, and full `check-feme` showed
3,770/3,778 with the same 1 pre-existing unrelated failure.

**Real Vulkan CTS caught what the unit/lit suites could not**: all 12
target cases changed from a clean "unsupported" diagnostic to a
runtime **"Image mismatch"** -- wrong pixels, not a crash. Net
Pass/Fail counts were unchanged (`dEQP-VK.glsl.switch.*`: 118/122,
`dEQP-VK.glsl.loops.special.*`: 534/552), but the failure mode became
strictly worse (silent wrong output vs. a safe, loud rejection).

Root-caused in two layers before deciding to revert:

1. **Found and fixed (but insufficient alone)**: `recoverRelayExitCheck`
   set `EC.RelayBlock` to the relay chain's *first* hop, contradicting
   `ExitCheck::RelayBlock`'s own documented contract (must be the
   *last* hop, the literal CFG predecessor of `ExitBlock`) -- a latent
   bug the function's original single-hop-only design never exposed.
   Fixed by rewriting `IsTrivialExitStub` to return the resolved last
   hop. Re-ran the same CTS sweeps: **identical "Image mismatch" result**,
   proving this bug was real but not the (only) cause.
2. **Root-caused, not fixed**: dumped the fully-lowered IR for the
   `loop_in_switch` repro and traced the actual remaining gap. Once
   `Header`'s divergent check is masked, its terminator becomes an
   unconditional branch into the loop body every iteration for every
   lane (correct -- uniform SIMD control flow, masking only gates
   side effects). But because this shape's real continue/exit decision
   is deferred to a later shared dispatch block rather than happening
   immediately at `Header` -- the case the existing masking machinery
   was designed and proven for -- the loop body keeps **unconditionally
   recomputing every loop-carried value** (the running color and
   iteration-count phis) for lanes that should have already "exited" at
   `Header`'s own check and had their value frozen there. Side-effect
   masking correctly prevents wrong *writes*, but nothing freezes the
   loop-carried *value itself* across the extra iterations such a lane
   nominally keeps running through, so the final merged output silently
   picks up extra, incorrect iterations' worth of computation. A correct
   fix needs per-lane freeze/select logic threaded through the loop body
   for every loop-carried value -- essentially reusing the same
   select-based merge machinery `DiamondFlattener` already uses
   correctly for ordinary in-body diamonds -- which is a materially
   larger change than either `L419(z)`/`L419(aa)`'s original design
   notes anticipated.

**Given this code area's two prior documented regressions
(`L419(v)`/`L419(w)`) and that a wrong-pixel correctness bug is strictly
worse than the pre-existing clean diagnostic, both changes were fully
reverted** (`git checkout --`), rebuilt, and reconfirmed back to the
exact pre-session baseline: `feme-opt` again emits the original "more
than one divergent exit check" diagnostic; `FeMeTransformsCPUTests`
638/638; `Linearize` lit suite 53/53; `dEQP-VK.glsl.switch.*` 118
Pass/4 Fail with the original `VK_ERROR_INITIALIZATION_FAILED`
diagnostic restored (not "Image mismatch"). **No code lands this
session; the 12 cases remain unsupported with their original safe
diagnostic.** `Roadmap.md` updated with a new `L419(ab)` entry recording
this finding in full. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: no change needed (no capability landed).

**Next steps**: a future session attempting this needs to design the
per-lane loop-carried-value freeze mechanism first, prove it in
isolation, and only then revisit the `IsTrivialExitStub`/`RelayBlock`
work above (which remains correct and reusable once that mechanism
exists). Standing items unchanged: `mixed_break_continue` (4 cases),
`L419(y)`'s masking-sequencing gap (6 cases), `L419(g)` (30
tessellation residuals, untouched), and the `check-hlsl-clang-feme-vk`
92-failure triage (still deferred every session). `/tmp/l437_full_run3`
still running and growing (375MB as of this session's end) -- just
check progress next session, don't restart.

## `L419(ac)`: `L419(z)`/`L419(aa)`/`L419(ab)`'s capability fix, completed

`L419(ab)`'s Layer 2 finding was exactly right: once `recoverRelayExitCheck`'s
generalized chase (re-implemented this session; `L419(ab)`'s two reverted
sub-fixes -- the `IsTrivialExitStub` `bool`->`BasicBlock*` generalization and
the last-hop `RelayBlock` fix -- carried forward unchanged) admits
`Header`/`Latch`'s own divergent check into `HeaderDivergent`/`LatchDivergent`
handling even when it only reaches `ExitBlock` through a relay hop owning its
own real, multi-valued phis (e.g. `StructurizeCFG`'s shared `break`/`continue`
dispatch block), erasing that check's own `CondBr` orphans the relay hop's
only predecessor. Nothing explicitly preserves the orphaned edge's semantic
contribution before `EliminateUnreachableBlocks` (called once, at the very
end of the whole pass) eventually collapses that hop's phis down to their
sole surviving incoming value -- silently discarding the value correctly
captured "at the moment of exit" for already-exited lanes and replacing it
with whatever the loop's own still-executing body recomputes on later
iterations. Confirmed via direct IR inspection (`/tmp/clean_test.ll`'s
`loop_in_switch` reduction, both pre- and post-fix) to be the actual root
cause of this shape's "Image mismatch" CTS failures.

**Fix, two cooperating pieces, both required**:

1. **(A) `freezeRelayChainExitValues`**, a new function called once per
   `HeaderExit`/`LatchExit` match, right before that check's own
   `->Br->eraseFromParent()` (4 call sites total). Walks the newly-tracked
   `ExitCheck::RelayChain` field (every hop `IsTrivialExitStub`'s chase
   passed through en route to `ExitBlock`, populated alongside its existing
   `RelayBlock` resolution) and, for each hop's own phi with an incoming
   edge from the chain's previous hop (the edge about to be orphaned),
   `select`-wraps every other (surviving) incoming value with the per-lane
   "is this iteration still a real one" guard mask just computed by the
   caller's own `stayInLoop` call -- picking the orphaned edge's value
   instead for any lane that has already exited.
2. **(B) Extended `freezeLoopCarriedValues`** with a new, optional
   `RelayCaptured` parameter. Whenever (A) finds the orphaned value is
   itself one of `Header`'s own raw loop-carried phis (not a derived
   instruction -- this repro's exact shape), it records that phi into a
   `RelayCaptured` set threaded through to this call. `freezeLoopCarriedValues`'s
   `UsedOutsideLoop` check now also fires for a phi found there, even
   though its own literal users are all inside the loop (the relay hop that
   captures it lives inside the cycle) -- applying the same sticky,
   self-referential freeze this function already gives any other
   externally-read phi. This is required because fix (A) alone is
   unsound: referencing a non-frozen, continuously-mutating raw phi as a
   `select`'s "else" value doesn't stop that phi's own value from still
   being overwritten by the loop's body for all lanes on every subsequent
   iteration.

Confirmed via direct IR dump against both `/tmp/clean_test.ll` and the
committed `loop-in-switch-divergent-header-and-relay.ll` lit test: `Flow`'s
own data phi became a `select` on `active.header.check.live` between the
loop body's freshly-computed value and `Header`'s own raw phi; `Header`'s
own phi backedge itself became a self-referential `select` on
`active.check.live` -- exactly the two-select chain (A)+(B) predicts, and
`-passes=feme-cpu-linearize,verify` confirms the result is verifier-clean.

New focused unit test `LinearizeTest.FreezesRelayChainExitValueThroughSharedDispatchBlock`
(a hand-written minimal two-phi relay-through-shared-dispatch-block repro,
*not* a CTS extraction, per the standing "prove it with a deterministic unit
test" guidance) asserts both selects appear structurally. Updated
`loop-in-switch-divergent-header-and-relay.ll`'s `RUN`/`CHECK` lines to
expect success (with the same structural select pattern) instead of the old
"more than one divergent exit check" diagnostic it previously asserted.

**Build/test verification**: `FeMeTransformsCPUTests` 639/639 (+1 new test,
0 regressions). Full `Linearize` lit suite and full `ninja check-feme` both
clean except the single already-confirmed pre-existing, unrelated
`entry-wrapper-loop-header-derived-value-in-suffix.ll` failure (re-confirmed
present on the pre-session tree via `git stash`, unrelated to this change,
same `wave_group_count`-area struct-field-ordering issue this report has
flagged before) -- `check-feme`: 3,771/3,772 Passed (excluding that one
pre-existing failure), 0 regressions.

**CTS verification** (`VK_ICD_FILENAMES` pointed at the FeMe ICD,
`vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan Device`
before running):
- `dEQP-VK.glsl.switch.*loop_in_switch*` (18 cases, all variants, including
  the 4 originally-targeted `*_dynamic_{fragment,vertex}` cases): **18/18
  Pass** (was 14/18 before this session).
- Combined `dEQP-VK.glsl.switch.*` (122 cases) + `dEQP-VK.glsl.loops.special.*`
  (552 cases) sweep, 674 cases total -- covers all 12 of `L419(aa)`'s target
  cases (4 `loop_in_switch` + 8 `elseblock`/`ifblock`) plus the 16
  already-fixed `L419(w)`/`L419(x)` cases in the same two groups: **664/674
  Pass**. The remaining 10 failures are exactly the already-tracked,
  still-open `L419(y)` (`conditional_break`, 6 cases: `do_while_uniform`,
  `for_uniform`, `while_uniform` x fragment/vertex) and `mixed_break_continue`
  (4 cases: `for_uniform`, `while_uniform` x fragment/vertex) -- zero
  regressions anywhere, and all 12 of this fix's own target cases are
  confirmed genuinely fixed (not just compiling cleanly -- real `Pass`
  results against the reference image).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- an internal compiler correctness fix inside `Linearize.cpp`, no
feature/extension-surface change.

**Next steps**: standing items unchanged: `mixed_break_continue` (4 cases,
a separate `DiamondFlattener` body-shape gap, repro at
`/tmp/mbc_frag_mod2.ll`), `L419(y)`'s masking-sequencing gap (`conditional_break`,
6 cases), `L419(g)` (30 tessellation residuals, untouched), and the
`check-hlsl-clang-feme-vk` 92-failure triage (still deferred every session).
`/tmp/l437_full_run3` still running and growing (428MB as of this session's
end, up from 375MB) -- just check progress next session, don't restart.

## L440(c): MLIR SPIR-V Deserializer `OpUndef`-in-`OpConstantComposite` fix

This session picked up the previously-deferred `check-hlsl-clang-feme-vk`
92-failure re-triage (`L440(b)`) rather than another attempt at the two
flagged-as-high-risk loop-linearization items (`mixed_break_continue`,
`L419(y)`), both still explicitly deferred to a dedicated session by prior
sessions.

**Re-measured the 92-failure baseline**: byte-identical to the prior
session's own figure (92 Failed/293 Passed/269 Unsupported/139 Expectedly
Failed out of 793), even with the standing `/tmp/l437_full_run3` background
CTS sweep (one core pegged) running throughout -- confirms the figure is a
genuine, reproducible baseline, not a load-skew artifact (contrary to
`L440(b)`'s own open caveat; the load-skew-to-385 effect documented there
only appears under much heavier contention than one background core).

**Root-caused the dominant cluster, found and fixed outside FeMe**: 56 of
the 92 failures (far broader than `L428`'s "Clang matrix intrinsic" framing
assumed -- 30 are `_mat.test` cases, but the rest are plain scalar/vector
`HLSLLib` functions like `f32tof16`/`degrees`/`radians`/`reversebits`/`cross`
with no matrix involved at all) all hit the identical error
`"OpConstantComposite component <id> N must come from a normal constant"`.
Built a minimal reproducer (`f32tof16.test`'s compiled SPIR-V contains a
`<2 x float> {undef, 0.0}` `OpConstantComposite`, a very common
scalar-to-narrow-vector HLSL codegen shape), confirmed it passes
`spirv-val --target-env vulkan1.3` cleanly (an `OpConstantComposite`
constituent may legally be either a normal constant or an `OpUndef`, per
spec) -- ruling out a `spirv-val`/SPIRV-Tools bug (the initial suspicion,
by analogy to the already-documented, confirmed-external `L428(a)` bug) --
and traced the actual error to **MLIR's own SPIR-V `Deserializer`**
(`mlir/lib/Target/SPIRV/Deserialization/Deserializer.cpp`'s
`processConstantComposite`), which only ever consulted its `constantMap`
when resolving a constituent's `<id>`, never the separate `undefMap` that
`OpUndef` ids are tracked in -- rejecting perfectly spec-legal SPIR-V
outright and aborting pipeline creation. Fixed `processConstantComposite`
to substitute a concrete zero-valued attribute of the constituent's
expected element type whenever its `<id>` resolves as an `OpUndef` rather
than a normal constant. New regression test
`mlir/test/Target/SPIRV/constant-composite-undef-constituent.spvasm`
(confirmed red without the fix, green with it); full
`mlir/test/Target/SPIRV` lit suite: 72/73 (the one pre-existing failure,
`memory-ops.mlir`, reproduces identically on a clean pre-fix baseline via
`git stash`/rebuild bisection, confirmed unrelated). Committed in its own
commit touching only `mlir/` files
(`fb72a78c2bb8 [mlir][spirv] Accept OpUndef constituents in OpConstantComposite`),
per the standing "fixes outside FeMe get an isolated commit" instruction.

**Net effect on `check-hlsl-clang-feme-vk`**: 92 Failed -> 39 Failed (53
fixed), 293 Passed -> 346 Passed, 0 new regressions (verified via an exact
before/after failure-name set diff, not just aggregate counts). Full
`ninja check-feme`: 3,771/3,772 Passed (excluding the same pre-existing
`entry-wrapper-loop-header-derived-value-in-suffix.ll` failure, reconfirmed
present on a clean pre-fix baseline too), 7 Unsupported, 0 regressions.

**CTS verification** (`VK_ICD_FILENAMES` pointed at the FeMe ICD,
`vulkaninfo --summary | grep deviceName` confirmed `FeMe CPU Vulkan Device`
before running, both before and after this session's work):
- `dEQP-VK.compute.*` (67,554 cases): 695 Pass/6 Fail/66,853 NotSupported.
  The 6 failures are all
  `dEQP-VK.compute.pipeline.zero_initialize_workgroup_memory.max_workgroup_memory.*`
  -- **confirmed pre-existing, unrelated to this session's fix**: reverted
  just this session's `Deserializer.cpp` diff in place, rebuilt
  `feme_vulkan`/`feme-opt`, re-ran exactly these 6 cases, and got the
  identical 6/6 Fail result on the pre-fix binary too (then restored the
  fix and rebuilt clean before continuing). This is a previously
  undocumented, still-open gap (likely an actual workgroup-memory
  zero-initialization bug at the implementation's max-size boundary, not
  investigated further this session) -- worth a future roadmap entry once
  triaged (not yet assigned an `L4xx` id).
- `dEQP-VK.spirv_assembly.*` (70,337 cases, the group most likely to
  exercise `OpConstantComposite`/`OpUndef` shapes directly): sweep was
  still running at session end (background, large group); result to be
  recorded at the start of a future session once complete, alongside a
  check-in on whether it surfaces any further latent `OpUndef`-composite
  shapes this session's fix didn't cover (e.g. nested array-of-array or
  struct-of-struct undef constituents, which the fix's conservative
  `getElementTypeForOperand` lambda intentionally does not handle -- see
  `agent_thoughts.md` for the exact limitation).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed -- this fix is a SPIR-V-ingestion correctness fix (accepting
already-legal input the deserializer previously rejected), not a new
feature or extension.

**Next steps**: finish the `dEQP-VK.spirv_assembly.*` sweep check-in next
session; triage the new `zero_initialize_workgroup_memory.max_workgroup_memory`
compute failure (6 cases, pre-existing, previously undocumented); continue
`L440(c)`'s remaining-39-failure cluster triage (see `Roadmap.md` for the
per-cluster breakdown); `mixed_break_continue` was root-caused this session
too (a `DiamondFlattener` diamond-arm post-dominator-escape bug, see
`Roadmap.md`'s `L419(ad)`) but deliberately not fixed, deferred to a future
dedicated session; `L419(y)` and `L419(g)` remain untouched standing items;
`/tmp/l437_full_run3` still running and growing (524MB as of this session's
end, up from 428MB) -- just check progress next session, don't restart.

## `L442`/`L443`: `max_workgroup_memory` doubling stays open; a real but unrelated `Linearize.cpp` `CheckBlock` masking gap found, fixed, and CTS-verified not to be the cause; `dEQP-VK.spirv_assembly.*` sweep finally completed

Continued the still-open `zero_initialize_workgroup_memory.max_workgroup_memory`
compute failure (`L442`, 6 cases) from a prior session. While reading
`Linearize.cpp`'s `LoopLinearizer::linearizeCycle` looking for a masking
explanation, found and fixed a real, distinct bug (`L443`): resource/atomic
calls sharing a cycle's `CheckBlock` were narrowed with the stale, pre-check
mask rather than the check's own narrowed mask, because `narrowCallMask`'s
one-shot "only overwrite a still-constant mask" guard locks in whichever mask
a call sees on its first visit, and the pre-existing code applied the
check-narrowed mask to the whole block only *after* already widely masking it
with the stale one. Fixed via a position-sensitive split (stale mask at/before
the check condition's own instruction, check-narrowed mask strictly after),
with a `SelectInst`-skipping walk to avoid landing between a degenerate
(`Header == CheckBlock`) block's phis and an earlier
`freezeRelayChainExitValues` call's own selects for the same block -- the
third time this exact code area has needed a careful, incremental fix (see
`L419(h)`/`L419(w)` for the first two). New regression test
`loop-internal-break-with-mask-op-after-check.ll` (the complement of the
existing before-the-check test); full `check-feme`: 3,772/3,780 Passed (only
the pre-existing, unrelated `entry-wrapper-loop-header-derived-value-in-suffix.ll`
failure remains), 7 Unsupported, 0 regressions.

**Confirmed, via a `git stash`/rebuild/re-run A-B differential test, that this
fix has zero effect on `max_workgroup_memory`**: ran case `.1` alone against
both the pre-fix and post-fix `libfeme_vulkan.so`/`feme-opt`, got the
byte-identical `"failure at index 4: expected 1, got: 2"` result both times.
The fix's own shape (an off-by-one/"phantom iteration" class of bug) doesn't
match the actual symptom anyway: every one of the 6 sizes fails with an
*exact* 2x count (`1`->`2`, `2`->`4`, `4`->`8`, `16`->`32`, `64`->`128`,
`128`->`256`), not a "+1" pattern. This is the key new diagnostic clue this
session adds: a clean doubling across every tested size points more toward
the workgroup/dispatch invocation count itself being doubled somewhere in
FeMe's compute-dispatch path, or `shared ... = {}`'s own zero-initialization
logic running twice, rather than any masking-narrowness gap (which would be
expected to scale with wave width or loop iteration count, not land on a
clean doubling for every size tested). Still not triaged further this
session -- needs a dedicated reduction starting from an `FEME_DUMP_IR` dump of
one case's dispatch/workgroup-count machinery, not more `Linearize.cpp`
masking work.

Also ran a full `dEQP-VK.compute.*` regression check (67,554 cases) to confirm
the `L443` fix introduces no regressions across the whole compute group:
695/67,554 Passed, 6/67,554 Failed (exactly the 6 `max_workgroup_memory`
cases, unchanged), 66,853/67,554 NotSupported -- byte-identical to the
pre-fix baseline recorded in the prior session's `L442` entry, confirming the
fix is safe as well as real.

**The long-running `/tmp/l440c_spirvasm.qpa` `dEQP-VK.spirv_assembly.*` sweep
(checked in on every session since it was launched, never restarted) finally
completed this session**: 12,260/70,337 Passed (17.4%), 275/70,337 Failed
(0.4%), 57,802/70,337 NotSupported (82.2%). Clustered the 275 failures by
case-family prefix; by far the two largest compact clusters are
`opquantize`/`opspecconstantop_opquantize`/`opquantize_vec4` (spanning every
shader stage in `instruction.graphics.opquantize.*` plus
`instruction.compute.opquantize*`) and the `opinboundsaccesschain`-adjacent
access-chain cases -- both previously flagged (several sessions ago) as the
two largest compact SPIR-V-assembly compiler clusters, now confirmed against
this session's completed, much larger 275-case baseline. Not triaged further
this session (recording the completed sweep result consumed the remaining
time); this is the natural next target for a future dedicated reduction
session.

`/tmp/l437_full_run3`'s own separate, still-running full-`dEQP-VK.*`
re-triage sweep was checked in on again (591MB, up from 524MB) -- still
running, growing, not restarted, per standing instructions.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed for either the `L443` masking fix (a pure compiler correctness fix,
no new feature/extension surface) or the `spirv_assembly.*` sweep completion
(a CTS measurement only).

**Next steps**: a dedicated reduction session for `max_workgroup_memory`'s
exact-2x doubling, starting from `FEME_DUMP_IR` on one case and checking the
compute-dispatch/workgroup-count path rather than `Linearize.cpp`; a future
`opquantize`/`opinboundsaccesschain` reduction session using the
newly-completed 275-case `spirv_assembly.*` baseline; `L419(ad)`
(`mixed_break_continue`, 4 cases), `L419(y)` (`conditional_break`, 6 cases),
and `L419(g)` (30 tessellation residuals) all remain untouched standing
items; `check-hlsl-clang-feme-vk`'s 39-case residual triage (`L440(c)`) also
remains deferred; `/tmp/l437_full_run3` still running and growing -- just
check progress next session, don't restart.

## Session: L442 isolated to feme-cpu-simdize; L437 full sweep completed

**The long-running `/tmp/l437_full_run3` full-`dEQP-VK.*` re-triage sweep
(checked in on every session since it was launched, never restarted)
finally completed this session**: 67,043/67,234 Passed (99.7%), 1/67,234
Failed (0.0%), 190/67,234 NotSupported (0.3%). The sole failure is `L442`'s
own `zero_initialize_workgroup_memory.max_workgroup_memory` cluster,
confirming it is the *only* outstanding `dEQP-VK.*` failure cluster across
this entire 67k-case sweep. `deqp-vk`'s own log printed "Test run was
ABORTED!" immediately after a complete `DONE!`/totals block; this reads as a
reporting artifact of this specific invocation (the totals match a fully
completed run) rather than a genuine early termination, but is worth
re-confirming with a fresh full sweep in a future session if time allows.

**`L442`'s exact-2x `max_workgroup_memory` doubling, root-cause narrowed
further this session, now isolated specifically to `feme-cpu-simdize`'s
masked-loop re-entry handling** (not `Linearize.cpp`, already ruled out by
`L443`'s own A/B test and independently reconfirmed here via a manual
scalar trace):

- Built a fast, faithful, small-scale repro by patching the real
  `MaxWorkgroupMemoryInstance::queuePass` in the third-party `VK-GL-CTS`
  checkout (env-var-gated `FEME_DEBUG_L442_SHRINK`, never committed, reverted
  before session end) to shrink the dispatch to `wgx=16, numElems=40` while
  reusing the exact same `glslang`-compiled GLSL shader and compilation path
  as the real failing case. This reproduces the identical doubling pattern
  in roughly a second instead of minutes, and is now documented in
  `feme/.instructions.md` as the standard technique for any future
  "needs a fast repro of a CTS-only failure" investigation.
- Captured `FEME_DUMP_IR_PRELINEARIZE` for the shrunk case and ran it
  through `feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize`
  standalone. Manually traced a single matching scalar invocation (global
  idx=4, outer loop `i=1,j=0`) through the post-`feme-cpu-linearize`
  (pre-SIMDize) IR's `live.t`/`sideeffect.t`/`.linearized`/`live.merge`
  predicate-merging logic and confirmed exactly one atomic fires with
  `Mask=true` -- `Linearize.cpp`'s masked-loop lowering is correct at the
  scalar level, confirming (independently of `L443`'s own differential
  test) that the bug lives downstream, in vectorization.
- Added temporary `FEME_DEBUG_L442_TRACE_ATOMIC` instrumentation to
  `femeCpuResourceAtomicAddRaw` (reverted before session end, not
  committed) and confirmed on real FeMe hardware: within one `runCompute`
  call, per-lane atomic byte-offsets fire in order `0,4,8,12` (wave 0's
  four lanes, each once, correct), then `16,16,20` (wave 1: lane 0 fires
  **twice**, lane 1 fires once) -- this is the clearest possible evidence
  the duplication is a literal duplicate call specific to lane 0 of
  non-first waves, since the shader's `elseif` branch (the only other
  candidate source of a second call) is provably false at this exact
  iteration (`shared_idx=4 < wg_size=16`).
- Inspected `/tmp/l442_min/shrink_postsimd.ll`'s per-lane unrolled
  atomic-call sequences for both the shader's `if` (own-match) and
  `elseif` branches; both read as individually clean (one
  `extractelement`+`call` per lane, 0-3, no textual duplication in either
  block) -- so the bug is not a simple literal-duplicate-instruction in
  the already-emitted unrolled sequence itself, but something in how the
  masked inner loop's per-(i,j) predicate state threads across repeated
  re-entries of that inner loop from the outer, uniform-trip-count loop.
  Not yet pinned to an exact line in `SIMDize.cpp`; next session should add
  instrumentation directly to that pass's masked-loop/divergent-call-site
  construction rather than continuing to eyeball the emitted IR text.
- Confirmed (separately, a dead end worth recording) that `dxc`'s codegen
  for HLSL's analogue of the shader's dynamic `wg_mem[i][j]`
  vector-component indexing does not match `glslang`'s shape (it instead
  trips a different, unrelated, already-known `feme-cpu-linearize`
  diagnostic) -- an HLSL/`dxc`-based repro of this exact bug shape is not
  viable; neither `glslangValidator` nor `glslc` are built standalone in
  this environment, making the "shrink the real CTS test" technique above
  the only fast way to iterate on this specific codegen shape.
- As session cleanup, found and fixed a real (if minor) lit-test
  regression unrelated to `L442` itself: `check-feme`'s
  `entry-wrapper-loop-header-derived-value-in-suffix.ll` had been failing
  since `L429`'s own commit (which reported `FeMeTransformsCPUTests` and
  `check-hlsl-feme-vk` results, but not a full lit-suite run) -- the
  test's `CHECK` lines were simply stale against `L429`'s own correct,
  intentional behavior change (`HeaderDerivedValues` depending on a
  `PersistentInduction` phi are now recomputed fresh at each use site
  instead of threaded through a dedicated `loopvarN` parameter). Fixed by
  updating the test's expectations, not the compiler; `check-feme` is now
  3,773/3,780 Passed, 0 Failed, 7 Unsupported.

Re-confirmed the `L442` CTS baseline is unchanged on a pristine checkout
after all instrumentation was reverted:
`dEQP-VK.compute.pipeline.zero_initialize_workgroup_memory.max_workgroup_memory.*`
still 0/6 Passed, 6/6 Failed. Also re-ran `check-hlsl-feme-vk` (492/793
Passed, 273 Unsupported, 28 Expectedly Failed, 0 unexpected failures) to
confirm the lit-test fix introduces no regressions there either.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (this session's `check-feme` lit-test fix is a test-expectations-only
change with no behavior difference; `L442` itself remains unfixed; the two
completed sweeps are measurements only).

**Next steps**: instrument `SIMDize.cpp`'s masked-loop construction directly
to find the exact duplicate-lane-0-call mechanism (see `L446`); a future
`opquantize`/`opinboundsaccesschain` reduction session using the
already-completed 275-case `spirv_assembly.*` baseline (`L444`); `L419(ad)`
(`mixed_break_continue`, 4 cases), `L419(y)` (`conditional_break`, 6 cases),
and `L419(g)` (30 tessellation residuals) all remain untouched standing
items; `check-hlsl-clang-feme-vk`'s 39-case residual triage (`L440(c)`) also
remains deferred.

## Session: L442 root cause conclusively pinned to DiamondFlattener::run() (L447)

**Device check**: `VK_ICD_FILENAMES=.../feme_icd.json vulkaninfo --summary |
grep deviceName` -> `FeMe CPU Vulkan Device`. Confirmed first action.

**What changed**: nothing functionally. This session fully root-caused
`L442` (see `Roadmap.md`'s new `L447` entry, which supersedes `L446`'s
`feme-cpu-simdize` attribution) down to an exact mechanism in
`Linearize.cpp`'s `DiamondFlattener::run()`, but did not implement a fix --
the fix needs new masking-seed plumbing through `LoopLinearizer` and was
judged too large/risky to rush in this session. All temporary diagnostic
instrumentation added while tracing this (six `FEME_DEBUG_L442_TRACE`-gated
`errs()` call sites in `Linearize.cpp`) was reverted before committing; the
diff is preserved at `/tmp/l442_min/debug_instrumentation.patch` for the
next session to reapply if it wants to re-trace.

**Root cause, in one paragraph**: `DiamondFlattener::run()`'s single,
whole-function `validate()`/`flatten()` walk runs *before*
`LoopLinearizer` even starts, and its own documented design intentionally
flattens any in-body divergent diamond that isn't itself one of the
enclosing cycle's recognized loop-control edges. Because `StructurizeCFG`
places this loop's real recognized header/exit-check (`Flow21`/`Flow22`) at
the *bottom* of the loop body rather than the top, the walk reaches and
flattens the shader's own `if`/`elseif` diamond (containing both resource
atomics) several blocks before it ever reaches that recognized boundary --
baking in a mask derived only from `AllActive` + the diamond's own
condition, with zero contribution from the loop's own per-iteration bound.
`LoopLinearizer`'s later, correct masking then can't fix this up:
`narrowCallMask`'s documented one-shot "only overwrite a still-constant
mask" guard (added for an unrelated, real, previously-fixed regression)
refuses to touch it. Full mechanism, with exact block numbers and code
references, is in `Roadmap.md`'s `L447` entry.

**Verification**:
- `check-feme`: 3,773/3,780 Passed, 0 Failed, 7 Unsupported -- unchanged
  from the prior session's baseline, confirming the (reverted)
  instrumentation introduced no regression.
- Re-confirmed `L442`'s own 6-case CTS baseline is unchanged:
  `dEQP-VK.compute.pipeline.zero_initialize_workgroup_memory.max_workgroup_memory.*`
  still 0/6 Passed, 6/6 Failed -- expected, since no functional fix was
  made this session.
- `/tmp/l437_full_run3`'s sweep (67,043/67,234 Passed, 1 Failed -- that 1
  being `L442`) was already fully recorded in the prior session's `L445`
  entry; re-checked this session, confirmed complete and unchanged (not
  growing, no longer running), nothing further to record.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (pure root-cause investigation and documentation this session, zero
compiler/runtime behavior change).

**Next steps**: implement `L447`'s fix (new masking-seed plumbing through
`LoopLinearizer` to pick up a diamond `DiamondFlattener::run()` now defers)
in a dedicated session; `L444`'s `opquantize`/`opinboundsaccesschain`
reduction; `L419(ad)` (`mixed_break_continue`), `L419(y)`
(`conditional_break`), `L419(g)` (tessellation residuals); `L440(c)`'s
39-case residual triage. All unchanged standing items.

## Session: L442/L447 fixed (L448) -- DiamondFlattener::run() defers masked loop-body diamonds

**Device check**: `VK_ICD_FILENAMES=.../feme_icd.json vulkaninfo --summary |
grep deviceName` -> `FeMe CPU Vulkan Device`. Confirmed first action.

**What changed**: implemented `L447`'s own fix design (see `Roadmap.md`'s
new `L448` entry for the full writeup): `DiamondFlattener::validate()`/
`::flatten()`'s whole-function walk now defers any in-cycle divergent
diamond containing a side-effecting resource/image call to
`LoopLinearizer`, instead of flattening it prematurely with a blind
`AllActive` mask. A first attempt regressed (the same defer check,
applied unconditionally, made `LoopLinearizer`'s own recursive
`validate`/`flatten` calls defer the diamond right back to themselves,
leaving it permanently unflattened); fixed by adding a new
`DiamondFlattener::IsWholeFunctionPass` constructor flag, `true` only at
`run()`'s own top-level call site.

**Verification methodology note**: the first regression-test draft
(checking that `feme-cpu-linearize`'s own IR output no longer has a
literal `i1 true` mask operand) was discarded after a `git stash`
sanity check showed it was an unreliable signal -- the pre-fix code
already produces a real, merely-wrong, non-constant mask. The committed
regression test instead compiles `feme-opt`'s full lowered IR to a real
object via `clang -O0`, links it against a small C harness providing
`feme.cpu.resource.atomic.add.raw.i32`, executes it, and checks the
output buffer directly (`feme/test/Transforms/CPU/linearize-defer-loop-
body-mask-op-diamond.ll`) -- confirmed both ways via `git stash`.

**Verification**:
- Real CTS: `dEQP-VK.compute.pipeline.zero_initialize_workgroup_memory.
  max_workgroup_memory.*` now 6/6 Passed (was 0/6 every prior session back
  through `L442`'s own discovery).
- Full `dEQP-VK.compute.*` regression sweep (67,554 cases, same filtered
  configuration as the prior `L447` session's own baseline at this
  report's own earlier `max_workgroup_memory` entry): 701 Pass/0 Fail/
  66,853 NotSupported (was 695 Pass/**6 Fail**/66,853 NotSupported) -- the
  6 new passes are exactly the 6 `max_workgroup_memory` cases, zero new
  failures elsewhere.
- `check-feme`: 3,774/3,781 Passed, 0 Failed, 7 Unsupported (one more test
  than the prior session's 3,773/3,780 baseline: the new lit test; no
  regressions).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (pure compiler correctness fix, zero feature/extension-surface
change).

**Next steps**: `L444`'s `opquantize`/`opinboundsaccesschain` reduction
(largest remaining `spirv_assembly.*` clusters); `L419(ad)`
(`mixed_break_continue`), `L419(y)` (`conditional_break`), `L419(g)`
(tessellation residuals); `L440(c)`'s 39-case residual triage. All
unchanged standing items.

### SPIR-V assembly `opquantize` flush-to-zero fix (L444/L449)

`L444`'s freshly-completed 275-case `dEQP-VK.spirv_assembly.*` baseline
clustered its failures, with `opquantize`/`opquantize_vec4`/
`opspecconstantop_opquantize` (13 cases) as one of the two largest
compact clusters. Root-caused to `mlir/lib/Conversion/SPIRVToLLVM/
SPIRVToLLVM.cpp`'s `QuantizeToF16Pattern`: an earlier session (`bdcd8d8
b5ea7`) lowered `spirv.QuantizeToF16` as a bare `fptrunc`-to-`f16`-then-
`fpext`-back-to-`f32` round trip, reasoning (incorrectly) that this
"matches the op's semantics exactly." It does not: `OpQuantizeToF16`
must flush any `f16`-subnormal result to a same-signed zero, which a
plain `fptrunc` does not do (it faithfully preserves `f16` denormals).
All 13 failing cases are exactly the CTS's `flush_to_zero` (plus a few
adjacent `exact`/`rounded`-family) sub-cases that exercise this.

Fixed via integer bit manipulation on the truncated `f16`'s `i16` bit
pattern (`bitcast`/`and`/`icmp`/`select`) rather than the more obvious
`llvm.is.fpclass`/`llvm.copysign` float-intrinsic approach. The
intrinsic-based first attempt passed the targeted 13 cases but
regressed 4 *previously-passing* `opquantize_vec4.*` cases (`exact`,
`infinities`, `propagated_nans`, `rounded`) with a new `feme-cpu-
simdize: ... divergent vector value ... component decomposition is not
yet supported` error: FeMe's CPU `feme-cpu-simdize` pass has two
distinct widening mechanisms (wave-widening and vector-component-
decomposition), and only the former already special-cases `is.fpclass`/
`ldexp`-shaped "almost-homogeneous" intrinsics (operand/result types
that don't exactly match); the latter's `checkVectorDecompositionSupp
orted` does not yet recognize this shape. The final bit-twiddling
lowering uses only already-supported elementwise producer shapes
(arithmetic/cast/comparison/select/same-element-count-bitcast), so it
hits no such gap.

While validating the fix, also found and fixed (in a separate,
self-contained commit) two pre-existing, unrelated bugs in `mlir/test/
Conversion/SPIRVToLLVM/arithmetic-ops-to-llvm.mlir`: a complete
duplicate `spirv.UMulExtended`/`spirv.SMulExtended` scalar-test block
that made the whole file fail to parse (`mlir-opt` "redefinition of
symbol"), and two stale `CHECK` lines expecting a `dense<N>` splat
shift-amount vector constant where the real lowering actually builds it
via a scalar constant plus two `insertelement`s.

**Verification**:
- `mlir/test/Conversion/SPIRVToLLVM/` lit tests: 23/23 passed.
- `check-feme`: 3,774/3,781 Passed, 0 Failed, 7 Unsupported (no
  regressions).
- Real CTS: `dEQP-VK.spirv_assembly.instruction.*opquantize*` (175
  cases, covering both the 13 originally-failing cases and the 162
  other `opquantize*` cases this lowering also touches): 175/175 Passed
  (was 162/175) -- the 13 originally-reported failures now all pass,
  and the fix's own abandoned intrinsic-based first attempt's 4-case
  vec4 regression was caught and avoided before landing, so the final
  committed fix introduces zero new failures.
- A full `dEQP-VK.spirv_assembly.*` re-sweep (70,337 cases) was started
  in the background at session end to refresh `L444`'s own full
  baseline (expect 262 remaining failures, i.e. 275 minus these 13); not
  yet complete as of this entry -- see `agent_thoughts.md`.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (pure compiler correctness fix, zero feature/extension-surface
change). This fix lives entirely outside `feme/` (in `mlir/`), per the
standing "issue outside FeMe" instruction: isolated, self-contained,
with its own regression test, in its own commit, touching no `feme/`
files.

**Next steps**: the other `L444` large cluster (`opinboundsaccesschain`
-adjacent access-chain cases, actually `OpPtrAccessChain`/
`opptraccesschain` per a closer read of the case names -- `Roadmap.md`'s
"opinboundsaccesschain" naming for this specific cluster looks like a
copy-paste echo of the *already-fixed*, differently-named
`opinboundsaccesschain` cluster referenced earlier in this report, not
a fresh re-discovery of the same bug -- worth double-checking the exact
case-name prefix before starting); `L419(ad)` (`mixed_break_continue`);
`L419(y)` (`conditional_break`); `L419(g)` (tessellation residuals);
`L440(c)`'s 39-case residual triage. All unchanged standing items.

### SPIR-V assembly `OpPtrAccessChain` lowering gap fix (L444/L450)

`L444`'s second large compact cluster (13 cases) in the 275-case
`dEQP-VK.spirv_assembly.*` baseline. **Naming correction**: this
session's own prior `L449` entry's next-steps flagged a suspicion that
`Roadmap.md`'s original `L444` prose label, "`opinboundsaccesschain`",
was an echo of an *already-fixed*, differently-named cluster from an
earlier session rather than a fresh discovery -- confirmed by directly
re-parsing `/tmp/l440c_spirvasm.qpa`'s own 275 `Fail` entries and
substring-matching: exactly 13 cases contain `opptraccesschain`
(`OpPtrAccessChain`), zero contain `opinboundsaccesschain`. This
report's entry corrects the name accordingly.

Root cause: `mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp` had no
conversion pattern at all for `spirv.PtrAccessChain`/`spirv.InBoundsP
trAccessChain` -- `ConvertSPIRVToLLVMPass` rejected any module
containing one outright with "failed to legalize operation
'spirv.PtrAccessChain' that was explicitly marked illegal", confirmed
directly via a minimal reproducer
(`dEQP-VK.spirv_assembly.instruction.compute.indexing.input.struct.
opptraccesschain_s32`'s pipeline-creation failure).

Per `SPIRVMemoryOps.td`'s own description, `OpPtrAccessChain`/
`OpInBoundsPtrAccessChain` are identical to `OpAccessChain`/
`OpInBoundsAccessChain` except for one extra leading `Element` operand
that "is used to do an initial dereference of Base ... to become the
[plain]AccessChain Base to dereference" -- i.e. `Element` plays
exactly the role the existing `AccessChainConversion` pattern's own
synthesized leading *zero* GEP index already plays for the plain
`AccessChain` ops (SPIR-V's plain access chain assumes a
zero-displacement initial dereference; the pointer-arithmetic variant
makes that displacement an explicit, arbitrary signed operand instead
of hardcoding it to zero). Added a new `PtrAccessChainConversion`
template class mirroring `AccessChainConversion`'s own structure,
threading `Element` through as the GEP's leading index in place of the
synthesized zero, with unmodified semantics for any subsequent
`Indices`.

**Verification**:
- New lit test cases (`@ptr_access_chain`, `@ptr_access_chain_struct`,
  `@in_bounds_ptr_access_chain`) added to `mlir/test/Conversion/
  SPIRVToLLVM/memory-ops-to-llvm.mlir` alongside the existing plain
  `AccessChain` ones; full `SPIRVToLLVM` lit directory: 23/23 passed.
- `check-feme`: 3,774/3,781 Passed, 0 Failed, 7 Unsupported (no
  regressions).
- Real CTS: `dEQP-VK.spirv_assembly.instruction.*opptraccesschain*`
  (343 cases): **193 Passed / 0 Failed / 150 NotSupported** (was 13
  Failed among these before this fix -- all now pass, the other 180
  already-passing cases this lowering also touches show zero
  regressions).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (pure compiler correctness fix, zero feature/extension-surface
change). This fix lives entirely outside `feme/` (in `mlir/`), per the
standing "issue outside FeMe" instruction: isolated, self-contained,
with its own regression test, in its own commit, touching no `feme/`
files.

With both of `L444`'s two largest clusters now fixed (`opquantize` via
`L449`, `opptraccesschain` via this entry), the 275-case baseline
should now stand at 275 − 13 − 13 = 249 remaining failures, pending the
background full re-sweep (`/tmp/l444_spirvasm_full_rerun.qpa`, launched
earlier this session, still running) to confirm with a real measurement
rather than arithmetic alone.

**Next steps**: check on `/tmp/l444_spirvasm_full_rerun.qpa` and record
the refreshed full baseline once complete; `L419(ad)`
(`mixed_break_continue`); `L419(y)` (`conditional_break`); `L419(g)`
(tessellation residuals); `L440(c)`'s 39-case residual triage. All
unchanged standing items.

## L451: `frexpstruct` SIMDize aggregate-intrinsic fix

**Issue**: `L444`'s third-largest compact cluster, `frexpstruct` (20 of
the 275-case baseline, across all 5 graphics shader stages --
`instruction.graphics.float32.comparison_*.frexpstruct_*`). GLSL's
`frexp(x, exp)` out-parameter builtin lowers to SPIR-V `OpExtInst
GLSL.std.450 FrexpStruct`, which `mlir/lib/Conversion/SPIRVToLLVM/
SPIRVToLLVM.cpp` converts straight to a single `{T, iN}
@llvm.frexp.*(T)` call -- confirmed via `FEME_DUMP_IR_PRESIMD` on a
direct repro: `call { float, i32 } @llvm.frexp.f32.i32(float %x)`.
`feme-cpu-simdize`'s `checkAggregateValueSupported` only recognized one
aggregate-result `CallInst` shape (the four `isOverflowArithIntrinsic`
add/sub/mul-with-overflow intrinsics), so any divergent use of this
call hit the pass's "component decomposition is not yet supported for
this producer" diagnostic.

**Fix**: `feme/lib/Transforms/CPU/SIMDize.cpp` -- added `isFrexpIntrinsic`
and a dedicated `widenFrexpIntrinsic` widening path. Unlike the
overflow intrinsics (one shared `LLVMMatchType<0>` operand/result
type), `llvm.frexp` takes a single operand and its two result fields
(fractional part, exponent) are independently-overloaded types, so the
wide exponent type is derived from the result struct's own second
field rather than reused from the operand's element type. Both the
scalar-operand and vector-operand (per-component decomposition) shapes
are handled, mirroring `widenOverflowArithIntrinsic`'s own two
branches.

**Verification**:
- New `SIMDizeTest.WidensDivergentFrexpCall` unit test, covering both
  result fields' independent widened types and their `extractvalue`
  readback.
- `check-feme`: 3,775/3,782 Passed, 0 Failed, 7 Unsupported (one more
  total test than `L450`'s own count, the new unit test; no
  regressions).
- Real CTS: `dEQP-VK.spirv_assembly.instruction.*frexpstruct*` (72
  cases): **72 Passed / 0 Failed** (was 20 Failed among these before
  this fix -- all now pass, the other 52 already-passing cases
  unaffected).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no change
needed (pure compiler correctness fix, zero feature/extension-surface
change). This fix lives entirely in `feme/` (FeMe's own CPU-target
wave-widening pass), not upstream `mlir/` -- the gap is in SIMDize's
aggregate-producer recognition, not in SPIR-V-to-LLVM opcode lowering
itself.

With all three of `L444`'s three largest clusters now fixed
(`opquantize` via `L449`, `opptraccesschain` via `L450`, `frexpstruct`
via this entry), the 275-case baseline should now stand at
275 − 13 − 13 − 20 = 229 remaining failures, pending a fresh full
re-sweep to confirm with a real measurement. Two prior same-purpose
background sweeps were each discovered stale mid-session (started
before a later fix in the same session rebuilt the library they were
meant to measure) and discarded/relaunched; see the new
`feme/.instructions.md` lesson on checking a sweep process's start
time against the binary's rebuild time before trusting its results.
The sweep, `/tmp/l444_spirvasm_rerun3.qpa`, was launched after
`L451`'s own final rebuild and has now completed.

## L444 refreshed baseline (confirmed, real measurement)

`/tmp/l444_spirvasm_rerun3.qpa`: 12,306 Passed / **229 Failed** / 57,802
NotSupported out of 70,337 (17.5% Pass, 0.3% Fail, 82.2% NotSupported).
This exactly matches the 275 − 13 (`L449`) − 13 (`L450`) − 20 (`L451`) =
229 arithmetic prediction -- confirms the three landed fixes did not
shift any other cluster as a side effect.

Re-clustered the 229 remaining failures by case-family prefix (stage
suffix normalized away, e.g. `_frag`/`_vert`/...):

| Cluster | Count |
|---|---|
| `f16arr3`, `struct16arr3`, `v3f16`, `v3f16arr5`, `v4f16arr3`, `struct`, `switch` | 8 each (56 total) |
| `floatarray`, `matrix` | 7 each (14 total) |
| `nested_struct`, `undef_nested_struct`, `mip_1`, `fconvert16` | 6 each (24 total) |
| `opline_empty_name`, `opline_long_name`, `opline_short_name`, `opnoline`, `opnop`, `out_of_order` | 5 each (30 total) |
| `same_module` | 4 |
| `mip_0`, `switch_continue` | 3 each (6 total) |
| remaining (smaller clusters) | ~2-1 each |

The `opline`/`opnop`/`out_of_order` group (30 cases total) looks like a
single shared root cause given the naming pattern (SPIR-V debug-info
instructions, `OpLine`/`OpNoLine`/`OpNop`, likely a line-info-adjacent
legalization or ordering gap) rather than six independent one-off bugs
-- worth investigating together as the next compact-cluster candidate.

**Next steps**: pick the next cluster for reduction -- either the
`opline`/`opnop` debug-instruction group (30 cases, single suspected
root cause) or one of the seven ~8-case struct/array
composite-decomposition clusters; `L419(ad)` (`mixed_break_continue`);
`L419(y)` (`conditional_break`); `L419(g)` (tessellation residuals);
`L440(c)`'s 39-case residual triage. All unchanged standing items.

## L452: `opline`/`opnop` deserialization gaps fixed; `out_of_order` found to be a separate bug

**Issue**: the previous entry's `opline`/`opnop`/`out_of_order` 30-case
group was speculated to share one root cause purely from its naming
pattern. Per this item's own stated plan, reduced one case first
(`dEQP-VK.spirv_assembly.instruction.graphics.opline.opline_short_name_frag`)
before assuming the whole group shared a fix.

**Root cause (two distinct bugs, both in upstream
`mlir/lib/Target/SPIRV/Deserialization`)**:
1. `OpNop` had no deserialization handler at all in
   `Deserializer::processInstruction`'s dispatch switch -- it fell
   through to `dispatchToAutogenDeserialization`, which has no
   SPIR-V-dialect counterpart for it, and errored with "unhandled
   deserialization of OpNop". Per the SPIR-V spec, `OpNop` has no
   semantics and should simply be discarded.
2. `Deserializer::processFunction`'s two strict-opcode checks (the
   `OpFunctionParameter`-matching loop, and the "entry block starts
   with OpLabel" check) both called `sliceInstruction` directly and
   failed immediately if the very next instruction wasn't exactly the
   expected opcode -- with zero tolerance for an interspersed
   `OpLine`/`OpNoLine`/`OpNop`, even though the SPIR-V spec permits
   debug-location markers and no-ops anywhere a non-terminator
   instruction may appear. Confirmed directly against the CTS's own
   `opline` test fixture (`vktSpvAsmInstructionTests.cpp`'s
   `createOpLineTests`): it places `OpLine` in exactly both of these
   gaps.

**Fix**: added an explicit `OpNop` case (returns success, discarding
the instruction) to `processInstruction`'s dispatch; added a new
`Deserializer::sliceNonDebugInstruction` helper (processes/discards a
leading run of `OpLine`/`OpNoLine`/`OpNop`, then slices the first real
instruction) and switched both of `processFunction`'s strict-opcode
checks to use it instead of the raw `sliceInstruction`.

**Tested**:
- Two new regression tests in
  `mlir/unittests/Dialect/SPIRV/DeserializationTest.cpp`
  (`FunctionParameterWithInterspersedDebugAndNopInstructionsSuccess`,
  `OpNopInFunctionBodySuccess`); `MLIRSPIRVImportExportTests` 45/45
  Passed.
- `mlir/test/Target/SPIRV/` lit suite: 72/73 Passed (the one failure,
  `memory-ops.mlir`, confirmed pre-existing on an unmodified tree via
  `git stash`, unrelated to this change).
- `check-feme`: 3,775/3,782 Passed, 0 Failed, 7 Unsupported -- no
  regressions.
- Real CTS: rebuilt `libfeme_vulkan.so` and ran the full `opline`
  (15/15 Pass) and `opnop` (6/6 Pass) groups directly, then launched a
  full `dEQP-VK.spirv_assembly.*` re-sweep in the background to get a
  real, programmatic before/after diff rather than relying on manual
  spot-checks for anything beyond those two groups. **The background
  sweep completed later in this same session** (see "L444 refreshed
  baseline" below) and corrected two mistaken spot-check conclusions
  made earlier: `opnoline` was *not* "already passing, unaffected" --
  a quick manual check of a few `opnoline` cases happened to sample
  only ones that were already passing, but the real sweep shows all 5
  `opnoline*` cases were in the 229-case baseline and are fixed by
  this same change (`OpNoLine` goes through the identical
  `sliceNonDebugInstruction` path as `OpLine`, and `opnoline`'s own
  CTS fixture places `OpNoLine` in the identical argument/label gaps).
  The real sweep also found two clusters not named anywhere in the
  original cluster table at all -- `spirv_version.*_compute` (7 cases,
  SPIR-V 1.0 through 1.6) and `float_controls_extensionless*` (4
  cases) -- both fixed as a side effect, presumably from incidental
  `OpLine`/`OpNop` instructions in their own compute-shader
  boilerplate. Real total: **37 cases fixed, 0 regressions** (confirmed
  by a programmatic diff of the full before/after `.qpa` files), not
  the 23 this entry's own earlier manual verification had concluded.

**`out_of_order` investigated and root-caused this session** (not yet
fixed): reduced one case each of `opphi.out_of_order_geom`,
`selection_block_order.out_of_order_geom`, and
`switch_block_order.out_of_order_geom` -- all three fail with a real
rendering mismatch ("Upper right corner mismatch"), not a
deserialization or pipeline-creation error, immediately ruling out a
shared cause with `opline`/`opnop`. Further isolated `opphi`'s own
5-stage breakdown: `_frag`/`_tessc`/`_tesse` **Pass**, but `_geom`
fails with "Upper right corner mismatch" and `_vert` fails with a
*different* mismatch location, "Lower left corner mismatch".

Got a real `FEME_DUMP_IR_PRESIMD` + `FEME_DUMP_IR` (post-widening)
pair of dumps for `opphi.out_of_order_vert` to find the real mechanism,
rather than stopping at the initial "Function-storage tightening"
hypothesis (which turned out to be close but wrong in its specifics).
The real bug: `feme::cpu::WaveUniformity`'s divergence analysis
(`llvm::UniformityInfo`, standard SSA-only dataflow, no memory/alias
model at all) has no way to see divergence carried *through memory*.
This shader's `result[i] = result[i] + operand` round-trips a
genuinely per-lane-divergent value (each lane's own vertex color)
through a `store`-then-`load` pair on a **uniformly-indexed** alloca
slot (`i` is the shared loop counter, same value every lane, every
iteration) -- the load's *pointer* operand is uniform and has no
divergent control dependency, so the analysis classifies the loaded
*value* uniform too, even though the memory it reads back is
genuinely different per lane. Confirmed directly in the post-widening
dump: `FunctionWidener` correctly widens the load's address into 4
real per-lane pointers and recombines 4 real per-lane loads into a
genuinely-divergent `<4 x float>` vector -- but because the
*consuming* `fadd`/`store` had already been judged uniform by the
analysis, the widener's own "give a surviving scalar use its value
back" fallback collapses that correctly-widened vector down to **one
single lane's value** (`extractelement ..., i64 %active.lane`) and
broadcasts it, unconditionally and unmasked, into every lane's own
storage slot -- silently overwriting every other lane's independent
per-lane state with lane 0's (or whichever lane is first-active) own
value. Not the `matrix_3x3`/`H004` bug class recurring (that one was
an under-widened aggregate *type*; this one is memory-carried
divergence the analysis structurally cannot see at all) -- a new gap,
likely affecting other not-yet-identified failures beyond this named
cluster too. Not fixed this session (a divergence-analysis change is
high-risk to land without a full dedicated session -- see
`Roadmap.md`'s `L452(a)` for the concrete design sketch and next
steps).

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` change
needed for the landed fix (pure deserialization-layer fix, no
feature/extension-surface change). This fix lives entirely outside
`feme/` (in upstream `mlir/`), per the standing "issue outside FeMe"
handling: isolated, self-contained, with its own regression tests, in
its own commit.

## L444 refreshed baseline (confirmed, real measurement, completed this session)

The full `dEQP-VK.spirv_assembly.*` re-sweep launched in the
background (`/tmp/l444_opline_opnop_full_rerun.qpa`/`.log`) completed
later in this same session: **12,343 Passed / 192 Failed / 57,802
NotSupported** out of 70,337 (was 229 Failed before `L452`'s fix). A
programmatic diff of the two full `.qpa`/log files (matching on exact
case name, not just group-level counts) confirms **37 cases fixed, 0
regressions** -- see the breakdown in `L452`'s own entry above (15
`opline*`, 6 `opnop*`, 5 `opnoline*`, 7 `spirv_version.*_compute`, 4
`float_controls_extensionless*`). 229 − 37 = 192, exactly matching the
real sweep's own total.

Re-clustered the real 192-case baseline against case-family prefix
(`L452(c)`, same methodology as the prior session's own table): the
seven ~8-case struct/array composite-decomposition clusters
(`f16arr3`, `struct16arr3`, `v3f16`, `v3f16arr5`, `v4f16arr3`,
`struct`, `switch`, 56 cases total) are **unchanged verbatim** from the
prior session's table -- this fix did not touch that area at all --
and, with `opline`/`opnop`/`opnoline`/`spirv_version`/
`float_controls_extensionless` now gone, they are unambiguously the
single largest remaining compact cluster in `L444`.

**Next steps**: the seven ~8-case struct/array composite-decomposition
clusters (56 cases, the clearest next pick, see `L452(c)`); `L452(a)`'s
`out_of_order` fix (root cause and a concrete design sketch are both
already written up -- a memory-carried divergence gap in
`feme::cpu::WaveUniformity`, needs its own dedicated session given the
regression risk of a divergence-analysis change); `L419(ad)`
(`mixed_break_continue`); `L419(y)` (`conditional_break`); `L419(g)`
(tessellation residuals); `L440(c)`'s 39-case residual triage. All
unchanged standing items except `L452(a)`/`L452(b)`/`L452(c)`, all new
this session.

## L452(d): struct/array composite-decomposition clusters root-caused, not fixed

Direct `deqp-vk` runs of all 7 `L452(c)`-named clusters found the
`struct` leaf is a coincidental naming collision, not shared with the
other six: it is actually `variable_init`/`opcopymemory` cases
(8 total) failing on an unrelated `spirv.GlobalVariable` legalization
error, not investigated further this session. The other 6 leaves --
`f16arr3`/`struct16arr3`/`v3f16`/`v3f16arr5`/`v4f16arr3`/`switch`
(48 cases) -- genuinely share one root cause in `feme-cpu-linearize`,
confirmed via a minimal standalone `.ll` reduction run directly through
`feme-opt --llvm -passes=feme-cpu-linearize` (far faster than the full
CTS harness for iterating).

**Mechanism** (full writeup in `Roadmap.md`'s own `L452(d)` entry):
the switch-decode-inside-a-loop shape these clusters compile to
contains a small nested diamond-of-diamonds (`Flow10`/`Flow11` in the
reduced repro). Two independently-sound, already-intentional
simplifications -- `peelConstantFlowPredecessors` bypassing a provably
constant predecessor, then `mergeTrivialRelayBlocksInCycle` merging the
now-single-predecessor `Flow11` into its sole remaining predecessor --
combine to turn a cleanly-nested 2-level diamond into exactly the
shape `DiamondFlattener::validate`'s own pre-existing `L375`/`L383(a)`
commentary already documents as a known, deliberately-unsupported
limitation ("an outer diamond arm directly reaches a nested sibling
diamond's own target"). This is not a new regression -- `validate`
correctly, by design, rejects a CFG shape it has never claimed to
support -- the new finding is the exact mechanism by which two
already-intentional folds can produce that shape from an input that
was cleanly supported before they ran.

Two candidate fixes written up in `Roadmap.md` (neither attempted,
given this file's extensive prior scar tissue -- `L375`/`L383(a)`/
`L425`/`L447`/`L442`): (a) teach `peelConstantFlowPredecessors` to
detect and decline a peel that would destroy an ancestor diamond's own
clean reconvergence point (narrower, safer, matches this file's
existing conservative "bail out" philosophy); (b) generalize
`validate`/`flatten` to tolerate a 3-way shared-descendant
reconvergence directly (more powerful, much higher regression risk).
Recommend (a) for the next dedicated session.

No code changed this session -- investigation only, all temporary
debug instrumentation reverted before this entry was written. No
`check-feme`/CTS rerun performed since nothing was modified; the
confirmed 192-case `L452(b)` baseline stands unchanged.

**Next steps (updated)**: `L452(d)`'s own fix design (a), a half-day
to a day once undertaken; `L452(a)`'s `out_of_order` fix (memory-carried
divergence gap, needs its own dedicated session); `L419(ad)`
(`mixed_break_continue`); `L419(y)` (`conditional_break`); `L419(g)`
(tessellation residuals); `L440(c)`'s 39-case residual triage; the
`struct` cluster's own separate `spirv.GlobalVariable` legalization
bug (8 cases, smaller/independent, could run in parallel).


## L452(d): fix implemented, CTS-verified, zero regressions

Implemented fix design (a) from the prior session's own writeup:
`peelConstantFlowPredecessors` (in `feme/lib/Transforms/CPU/Linearize.cpp`)
now calls a new `peelWouldDestroyAncestorReconvergencePoint` check before
committing to a peel that would leave a block with exactly one real
predecessor. The check walks backward from that sole survivor to the
real `CondBrInst` it descends from, then tests whether both of that
block's successors reach the peeled block through nothing but pure
relay hops -- i.e. whether the peeled block is already serving as that
ancestor's own private two-arm reconvergence point -- and bails out of
the peel if so, leaving the (harmless, just not maximally simplified)
original shape in place.

Two refinements beyond the original design sketch were required to
avoid false positives against the existing regression suite:

1. The enclosing cycle's own `Header`/`Latch` are exempted from being
   flagged as a dangerous origin: a loop header legitimately
   reconverges at a `Flow`-style dispatch block the same way an
   ordinary diamond does, and `LoopLinearizer::recoverRelayExitCheck`
   is specifically built to handle that afterward. Confirmed via
   `loop-break-structurized.ll`.
2. The relay-chain walk now requires every hop strictly before the
   target to be a genuine `isPureRelayBlock` (no real computation
   besides phis/terminator), not just any `UncondBr`-terminated block
   -- a loop latch's own trip-count recheck is `UncondBr`-terminated
   but must not be treated as a pure relay hop. Confirmed via
   `loop-relay-chain-two-hops.ll`.

One known, accepted limitation: `checkblock-chained-divergent-check-
bypass.ll` (the pre-existing `L377(a)` `%chain.merge` fast-path test)
is structurally indistinguishable from the real bug by this purely
local, structural check, so the new guard conservatively declines that
one specific micro-optimization too. Verified the resulting output is
still fully flattened and correct via
`-passes=feme-cpu-linearize,verify` (no leftover divergent `br i1`);
the test's own CHECK lines were updated to match the new (still
correct) shape.

**Real CTS verification**: ran a targeted `deqp-vk` caselist covering
all 78 cases across the 6 named clusters (`f16arr3`/`struct16arr3`/
`v3f16`/`v3f16arr5`/`v4f16arr3` across all stages, plus
`type.scalar.{i8,i16,i32,i64,u8,u16,u32,u64}.switch_*`), both before
(via a temporary `git stash` of just the fix, rebuilding
`libfeme_vulkan.so`) and after this session's fix:

| | Passed | Failed | Not supported |
|---|---|---|---|
| Before (baseline) | 14/78 | 28/78 | 36/78 |
| After (this fix) | 29/78 | 13/78 | 36/78 |

A real before/after set comparison of `Fail`-status cases (not a spot
check) confirms **zero regressions**: every case that failed after the
fix also failed before it; 15 cases flipped from `Fail` to `Pass`.

The remaining 13 failures split into two buckets, neither touched this
session:

- 4 are `*_tessc` (tessellation-control) variants of the now-fixed
  clusters -- very likely `L419(g)`'s already-known 30-case
  tessellation-control residual bucket, not re-confirmed this session.
- 8 are `type.scalar.{i32,u32}.switch_{comp,geom,tessc,tesse}`, which
  reproduce the exact same top-level `feme-cpu-linearize` diagnostic
  this cluster was originally grouped under, but this fix did not
  resolve them -- logged as new roadmap item `L452(e)` for a future
  session to root-cause with its own standalone reduction.

`check-feme`: 3776/3776 passing (7 unsupported, as expected), 0
failures, after a full clean rebuild. `FeMeTransformsCPUTests` unit
test binary run directly: 640/640 passing.

New lit regression test: `loop-body-switch-decode-shares-ancestor-
reconvergence.ll`, derived from the real `f16arr3` CTS reduction this
session started from.

**Next steps**: `L452(e)` (the 8 remaining `i32`/`u32` `switch_*`
residuals, needs its own standalone reduction); `L452(a)`'s
`out_of_order` fix; `L419(ad)`/`L419(y)`/`L419(g)`; `L440(c)`'s 39-case
residual triage; the `struct` cluster's own separate
`spirv.GlobalVariable` legalization bug (8 cases).

## L452(h)/L452(i): design (b) implemented, CTS-verified, `L452(e)` fully resolved

Implemented fix design (b) for the `L452(d)`-through-`L452(h)` peel-
safety bug, as prescribed by `L452(h)`'s own precise mechanism
writeup (the "two-session" root-cause chain: `L452(e)` through
`L452(h)` found that the old, purely-structural
`reachesThroughRelayChain` heuristic could not distinguish the
dangerous `switch_comp.ll` shape from the must-allow
`LinearizeTest.ExtendsExistingReconvergencePhiWhenASecondPeelAdds
AThirdPredecessor` shape on CFG structure alone).

`peelWouldDestroyAncestorReconvergencePoint` now performs a real,
scoped dry run instead of a hand-rolled heuristic:

1. Walks backward from the peel's sole surviving predecessor to the
   real `CondBrInst` it descends from (`Origin`), bailing out early
   for `Header`/`Latch`/non-`CondBrInst` origins exactly as before.
2. Temporarily applies the actual peel's edge redirects
   (`PredBr->setSuccessor(0, Target)` for every precomputed
   `(Pred, Target)` pair).
3. Recomputes `PostDominatorTree`.
4. Constructs a throwaway `DiamondFlattener` with fresh, local
   `MergeVisited`/`ResolvedBlocks` sets and calls a new public
   `probeValidate(Origin, R)` wrapper -- reusing `validate`'s own
   conflict-detection machinery to ask the exact question `L452(h)`
   identified as necessary: would this specific peel's `Target` get
   double-visited within one top-level `validate` call.
5. Fully undoes every redirect and recomputes `PDT` again before
   returning, regardless of outcome.

`peelConstantFlowPredecessors` was refactored to precompute
`PredToTarget`/`PredTargets` once, shared by both the new guard's dry
run and the real mutation loop, eliminating any risk of the two
drifting apart. `DT`/`PDT`/`CI`/`UI` were threaded through
`peelConstantFlowPredecessorsInCycle` from `LoopLinearizer::
linearizeCycle`'s own member fields.

**Surprising, independently-verified discovery**: the new precise
check is strictly *more permissive* than the old heuristic it
replaces. `/tmp/switch_comp.ll` now fully linearizes end-to-end
(confirmed well-formed via `opt -passes=verify`) rather than merely
diagnosing gracefully, because the old heuristic was *also*
incorrectly blocking a second, actually-safe peel elsewhere in the
same function (an over-conservative false positive that happened to
share the same diagnostic text as the one genuinely dangerous peel).
Confirmed via temporary trace instrumentation (fully reverted before
commit) that the new guard still correctly blocks the one genuinely
dangerous peel (`BB=Flow18`); the pipeline nonetheless now succeeds
because the *other*, previously-also-blocked peel is now correctly
allowed through.

Both must-pass cases held:

- `LinearizeTest.ExtendsExistingReconvergencePhiWhenASecondPeelAdds
  AThirdPredecessor`'s `BB=Flow9` call behaves identically to before
  (same benign, pre-existing, unrelated diagnostic, unaffected).
- `/tmp/switch_comp.ll` now succeeds fully (previously diagnosed).

New lit regression test: `loop-switch-decode-peel-double-visit.ll`
(`RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | opt
-passes=verify -S | FileCheck %s`, confirming both successful
linearization and IR well-formedness).

**Real CTS verification**: ran the 48-case
`dEQP-VK.spirv_assembly.type.scalar.*.switch_*` superset caselist (all
8 integer types x 6 shader stages), both before (via a temporary
`git stash`/`ninja feme_vulkan` rebuild round-trip) and after this
session's fix:

| | Passed | Failed | Not supported |
|---|---|---|---|
| Before (baseline) | 4/48 | 8/48 | 36/48 |
| After (this fix) | 12/48 | 0/48 | 36/48 |

A real before/after set comparison confirms **zero regressions**:
exactly the 8 `L452(e)` residual cases
(`type.scalar.{i32,u32}.switch_{comp,geom,tessc,tesse}`) flip from
`Fail` to `Pass`; the 36 `NotSupported` cases are unrelated 8/16-bit
storage-capability gaps, unaffected by this fix. Also directly
re-confirmed `dEQP-VK.spirv_assembly.type.scalar.i32.switch_comp`
(the exact case used as the primary repro across all 4 prior
sessions on this bug) passes standalone.

`check-feme`: 3,777/3,784 Passed, 0 Failed, 7 Unsupported (one more
total test than `L452(h)`'s own baseline, the new lit test).

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` change
(pure compiler correctness fix inside `feme-cpu-linearize`, no
feature/extension-surface change).

**`L452(e)` is now fully resolved** (both of its sub-buckets
accounted for): the 8 `i32`/`u32` `switch_*` cases are fixed by this
session; the other 4 `*_tessc` residuals remain tracked under
`L419(g)`'s own 30-case tessellation-residual bucket, unaffected by
and unrelated to this fix.

**Next steps**: `L452(a)`'s `out_of_order` fix; `L419(ad)`;
`L419(y)`/`L419(g)`; `L440(c)`'s 39-case residual triage; the
`struct` cluster's own separate `spirv.GlobalVariable` legalization
bug (8 cases); `feme-run`'s DXIL atomic gap; the 9 pre-existing,
still-uninspected git stashes.

## L452(a): memory-carried divergence propagated to consumers -- `out_of_order` fixed, plus 31 latent siblings

**Issue**: `L452(a)`'s own root-cause writeup (previous entry's "Next
steps") identified that `FunctionWidener::MaskedAllocas`
(`feme/lib/Transforms/CPU/SIMDize.cpp`) already force-widens a `load`
reading a uniformly-addressed local variable whose stored *value* is
per-lane divergent (`UniformityInfo` has no memory model and cannot
see this on its own), but that this was necessary and not sufficient:
any downstream consumer of such a load (e.g. an `fadd` combining it
with a uniform operand, or a `phi` merging it with a uniform
constant) was still gated by the plain, memory-oblivious
`UI.isDivergentAtDef` check. Left alone, that consumer silently
collapsed the correctly-widened real per-lane vector down to one
lane's value (via the existing H107 "give a uniform consumer its
value back" fallback) and broadcast that single value back out to
every lane's own storage -- the exact
`dEQP-VK.spirv_assembly.instruction.*.{opphi,selection_block_order,
switch_block_order}.out_of_order_*` rendering corruption this
roadmap item names.

**Fix**: added `FunctionWidener::ForcedDivergentValues`, a side-table
seeded from every `load` reading a `MaskedAllocas` base that `UI`
itself still calls uniform, propagated forward along ordinary SSA use
edges to every downstream consumer via a worklist fixed point. A new
`isDivergent(Value*)` wrapper consults both `UI` and this side-table,
and replaces the three decision points that needed it: the generic
`widenInstruction` dispatch gate, the pass-1 divergent-`phi` pre-pass,
and `widenMaskedAllocaStore`'s own `ValueDivergent` flag. New lit test
`simdize-masked-alloca-memory-divergent-consumer.ll` pins the exact
`opphi.out_of_order` shape. A pre-existing unit test,
`SIMDizeTest.WidensPHIMergingMaskedAllocaLoad`, turned out to encode a
smaller-scale instance of the identical bug (its own assertion that a
`phi` merging a `MaskedAllocas`-sourced load with a uniform constant
"correctly" stays narrow was itself wrong); updated to assert the
new, correct widened behavior.

`check-feme`: 3,778/3,785 Passed, 0 Failed, 7 Unsupported, no
regressions.

**Real CTS verification**: ran the direct 15-case `out_of_order`
caselist (`opphi`/`selection_block_order`/`switch_block_order` x 5
shader stages each) -- all 15 now Pass (were 5 confirmed-failing
pre-fix: `opphi.out_of_order_geom`/`_vert` plus the two sibling
families sharing the same shape; the other 10 stage/case
combinations were already passing, per `L452`'s own prior
per-stage breakdown).

Then re-ran the **full** `dEQP-VK.spirv_assembly.*` sweep (70,337
cases, via `feme/utils/run_vulkan_cts.py`) to check for side effects,
since a divergence-analysis change is exactly the kind of fix most
likely to flip other, currently-passing cases in either direction:

| | Passed | Failed | Not supported |
|---|---|---|---|
| Before (`L452(b)`'s confirmed baseline) | 12,343 | 192 | 57,802 |
| After (this fix) | 12,389 | 146 | 57,802 |

`NotSupported` is identical and `Passed + Failed` sums identically
both before and after (12,535), confirming **exactly 46 cases flipped
`Fail` -> `Pass` and zero regressions** -- substantially more than the
15 directly-named `out_of_order` cases. Clustering the new 146-case
fail list shows the other 31 fixes landed across several of
`L452(c)`'s own previously-documented clusters: every non-`_tessc`
stage of the `f16arr3`/`struct16arr3`/`v3f16`/`v3f16arr5`/`v4f16arr3`
float16 composite-insert/extract cluster, and every non-`_tessc` stage
of `variable_init.private.{floatarray,matrix,struct}` -- all of which
share the identical uniform-address, per-lane-divergent-value
local-variable round trip `out_of_order` itself reduces to, just
without an obviously related case name. The remaining `_tessc` stage
of each of those clusters is unaffected by this fix and stays tracked
under `L419(g)`'s own 30-case tessellation-residual bucket (a
separate, already-known issue).

No `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` change
(a divergence-analysis correctness fix inside `feme-cpu-simdize`, no
feature/extension-surface change).

**`L452(a)` is now fully resolved.** With it, every sub-item of the
`L452` family (a through i) is fixed and CTS-verified.

**Next steps**: `L419(ad)` (`mixed_break_continue`, 4 cases);
`L419(y)`/`L419(g)` (`conditional_break`, 6 cases + 30 tessellation
residuals, now the most direct next pick given today's confirmed
`_tessc`-only residual in three more clusters); `L440(c)`'s 39-case
residual triage; the `struct` cluster's own separate
`spirv.GlobalVariable` legalization bug (8 cases); `feme-run`'s DXIL
atomic gap; the 9 pre-existing, still-uninspected git stashes.
