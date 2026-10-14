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
