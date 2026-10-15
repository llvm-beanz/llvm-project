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
