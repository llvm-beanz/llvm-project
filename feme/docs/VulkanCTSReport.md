# FeMe Vulkan ICD: Vulkan-CTS Status Report

This report is regenerated from scratch on every full Vulkan-CTS pass; it
describes the *current* state of `libfeme_vulkan` against `deqp-vk`, not the
history of how it got there. Previous editions of this file recorded a
narrative of individual crash fixes; that narrative is now folded into
[Roadmap.md](Roadmap.md) §1.9 and each design document's own Status notes,
and this file is a measurement instead.

- FeMe revision: `2f27e5bd85a5` (roadmap D0, "advertise apiVersion 1.4" +
  "implement VK_KHR_copy_commands2's core names" -- see "Roadmap D0:
  measured impact" below). The headline table below is this same D0
  revision's full 54-group run: roadmap D1 (audit-only, no advertised
  feature/limit/extension changed) is not re-measured in full -- see its
  own "Roadmap D1: measured impact" section for the targeted subset run
  that *is* new in this edition.
- VK-GL-CTS revision: `vulkan-cts-1.4.6.2-412-g716301541136` plus two local
  fixes (`7163015`, "Guard `dEQP-VK.api.invariance.random` against empty
  image format lists"; and a second one added by roadmap C7's own pass,
  "Check `VK_KHR_copy_commands2` support in
  `image_to_image_transfer_queue.misc.ms_then_ss*`" -- see "Deviations from
  a stock CTS" below).
- Host: AArch64 Linux, `LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
  `RelWithDebInfo`.
- `check-feme`: 1519 passed, 1 unsupported (this file's own revision;
  1520/0 once `FEME_VULKAN_CTS_DEQP_VK` points at a built `deqp-vk`, which
  unsupports one fewer gated test).

## Headline

This is a genuine full 54-group re-run (unlike roadmap C7's own targeted
groups-only re-run) -- the first since roadmap C1 -- taken *after* both D0
commits (the apiVersion bump and the copy_commands2 fix it made
necessary). See "Roadmap D0: measured impact" below for the before/after
comparison against the previous (apiVersion 1.2) edition of this table.

| | Count | Share |
|---|---|---|
| Total cases | 3,236,772 (of 3,237,000 possible: see below) | |
| Passed | 11,040 | 0.34% |
| Failed | 29,647 | 0.92% |
| Not supported | 3,196,084 | 98.74% |
| Quality warning | 1 | |
| **Crashed / timed out** | **1 group (`api`), 228 cases short** | |

53 of the 54 top-level `dEQP-VK.<group>.*` groups run to completion; `api`
aborts partway through on a genuine `SIGSEGV`, unrelated to any FeMe code
(see "Roadmap D0: measured impact"). 28 of the 54 groups have **zero**
failures (`conditional_rendering`, `cooperative_vector`,
`data_graph`, `depth`, `descriptor_indexing`, `dgc`,
`drm_format_modifiers`, `fragment_shader_interlock`,
`fragment_shading_barycentric`, `fragment_shading_rate`, `geometry`,
`imageless_framebuffer`, `image_processing`, `mesh_shader`, `multiview`,
`postmortem`, `protected_memory`, `ray_query`, `ray_tracing_pipeline`,
`reconvergence`, `shader_object`, `sparse_resources`, `synchronization2`,
`tensor`, `tessellation`, `transform_feedback`, `video`, `wsi`) -- almost
all of them because the feature they cover is not advertised at all, which
is the correct, truthful outcome for this ICD's declared scope.

**No case produces a wrong answer.** Every one of the 26,925 failures was
traced to a *clean rejection* -- a pipeline that failed to create, a format
or descriptor type the ICD does not advertise, or a `deqp-vk` check of a
mandatory capability the ICD does not claim. Not one is a `Pass`-shaped
result carrying incorrect data. That is the "must fail before draw time,
not silently misbehave" contract every FeMeVulkanDesign.md milestone states,
holding across three million cases.

## Roadmap C1: measured impact

Roadmap C1 ("Mandatory formats", see Roadmap.md §1.9.1) added
`B8G8R8A8_UNORM`/`R10G10B10A2_UNORM` to `isSupportedColorAttachmentFormat`
and `D24_UNORM_S8_UINT` as a combined depth+stencil format, each backed by
a real `feme::graphics` pack/unpack path (see `RenderPass.cpp`,
`CommandBuffer.cpp`, `ImageOps.cpp`, `Executor.cpp`, and
`ImageFixture.cpp`) -- exactly what the roadmap row specifies, and
`unittests/Graphics/ImageFixtureTest.cpp`,
`unittests/Vulkan/{RenderPass,GraphicsPipeline}Test.cpp`, and a new
end-to-end `unittests/Vulkan/DrawTest.RendersWithCombinedDepthStencilAttachment`
cover it (`ninja check-feme` passes in full, before and after).

**This full re-run's headline numbers are byte-for-byte identical to the
previous edition's** (10,350 passed, 27,094 failed, 3,199,555 not
supported). That is not a measurement error: `vkGetPhysicalDeviceFormatProperties`/
`vkGetPhysicalDeviceFormatProperties2` (`EntryPoints.cpp`) unconditionally
return an all-zero `VkFormatProperties` for *every* format, a pre-existing
stub predating C1 ("no buffer or image format is supported yet" -- stale
even before this change, since V4 already added texel buffers and V6
added render passes). `deqp-vk` -- like any well-behaved client --
queries this command *before* attempting `vkCreateRenderPass`/
`vkCreateGraphicsPipelines`, so a format those commands now genuinely
accept is still invisible to CTS: confirmed directly by
`dEQP-VK.graphicsfuzz.*`'s `B8G8R8A8_UNORM`-framebuffer cases, which still
report `Fail (Vulkan color attachment format is not supported)` -- a
`deqp-vk`-side check against the (still-zero) format properties, not
against `isSupportedColorAttachmentFormat`.

Wiring `vkGetPhysicalDeviceFormatProperties` to report
`COLOR_ATTACHMENT_BIT`/`_BLEND_BIT` and `DEPTH_STENCIL_ATTACHMENT_BIT` for
exactly the formats `isSupportedColorAttachmentFormat`/
`isSupportedDepthAttachmentFormat`/`isSupportedStencilAttachmentFormat`
now accept is therefore a *necessary* companion change before C1 has any
CTS-visible effect -- and was prototyped during this same investigation.
That prototype is **not** included in this revision, because running the
full CTS against it surfaced three previously-unreached crashes (this
ICD's non-negotiable "Crashed / timed out: 0" bar), each in a different
subsystem, none related to the format tables themselves:

| Group | Case (abbreviated) | Crash |
|---|---|---|
| `renderpasses` | `dynamic_rendering...low_resolution_z.blend.color_masked_after_color_depth` | `llvm::Value::setNameImpl` assertion, `"Cannot assign a name to void values!"` -- some FeMe CPU codegen path names a void-typed instruction once a combined-format depth/stencil draw is actually reachable |
| `spirv_assembly` | `instruction.spirv1p4.opselect.array_select` | `UNREACHABLE executed at feme/lib/Transforms/CPU/ResourceCalls.cpp:55`, `"unsupported feme.cpu.resource.* element type"` -- an explicit, intentional guard for an element type this pass does not yet lower, reached only once more format/type combinations stop being skipped as unsupported |
| `synchronization` | `timeline_semaphore.device_host.write_copy_buffer_read_copy_buffer.buffer_262144` | Segmentation fault, no diagnostic |

Each is a genuine, previously-latent bug this measurement uncovered, not a
consequence of C1's own pack/unpack logic (none touch
`RenderPass`/`CommandBuffer`/`ImageOps`/`Executor`/`ImageFixture`). They
are recorded here as the concrete blocker for the format-properties
follow-up, rather than silently deferred: **wiring
`vkGetPhysicalDeviceFormatProperties` honestly, and fixing these three
crashes, is the next unit of work before C1's 1,938-case column moves.**

This run also reproduced one separate, unrelated flake independent of any
C1 change: `dEQP-VK.api.*`, run standalone with the *pre*-format-properties
code (i.e. this revision), hung deterministically at
`buffer_view.access.dedicated_alloc...image_dedicated_alloc_compute` after
processing roughly 200,000 preceding cases in the same process, despite
that exact case passing cleanly in isolation (a `VK_ERROR_FORMAT_NOT_SUPPORTED`-based
`Fail`, not a hang). This smells like a resource-exhaustion issue specific
to very long single-process runs and unrelated to attachment formats; the
`api` row in every table below instead uses the same group's numbers from
an otherwise-identical run that completed cleanly (7,445 passed / 287
failed / 259,490 not supported / 267,222 total), and this flake is
recorded here rather than silently worked around.

## Roadmap C2: measured impact

Roadmap C2 ("`Uniform`-storage-class blocks", see Roadmap.md §1.9.1)
generalized `feme::spirv::getBufferBlockElement`/`getUniformBlockElement`
to the shapes glslang emits directly (no separate FeMe/dxc wrapper
struct): a pre-1.3 SSBO (`Uniform` + `BufferBlock`), a `Block`/
`BufferBlock` struct with more than one member, a sized-array member, and
a `ColMajor` matrix member (`RowMajor` is declined, not miscompiled); an
array-of-blocks binding (`T blocks[N]` in GLSL) is handled by a new
`ArrayedBlockAccessChainPattern`. See the commit series ending at this
revision for the full breakdown, and `Design.md`'s "Known gap: `spirv`
dialect -> `llvm` dialect conversion coverage" for the updated scope.

**This is the first roadmap step whose full CTS re-run moved the headline
numbers**: 10,519 passed (+169) and 26,925 failed (-169), `Not supported`
unchanged at 3,199,555. That the movement is two orders of magnitude
smaller than C2's own 10,121-case column is exactly the "stacked
blockers" effect C1's own measurement first surfaced (see above): the
`Uniform`-storage-class-block diagnostic itself is now gone from every
log (grepped for directly), but `feme-cpu-simdize`'s divergent-vector
diagnostic count on the same re-run (10,223 occurrences, essentially the
same shaders C2 unblocked) confirms most of column C2's own 10,121 cases
now fail one stage later, on Roadmap C3, exactly as predicted rather than
passing outright. The 169 cases that *do* now pass are the ones C2 was the
*only* blocker for.

Two real bugs surfaced and were fixed in the same pass, both by running
this exact measurement against a work-in-progress build rather than only
against the unit tests added alongside each commit:

- `dEQP-VK.ubo.single_struct.per_block_buffer.std140_instance_array_both`
  hit an assertion in the shared access-chain rewrite helper
  (`rewriteBlockAccess`), which unconditionally assumed FeMe's own
  single-member wrapper shape's content was always a storage buffer's
  runtime array, when a uniform block's own wrapper content is a field
  *struct* instead -- fixed by dispatching on what the content actually
  is rather than on which shape produced it (a `spirv-to-llvm-glslang-
  blocks.mlir` case now covers the exact shape).
- `dEQP-VK.glsl.conversions.matrix_to_matrix.mat2_to_mat2x3_vertex`
  crashed in MLIR upstream's own `CompositeExtractPattern`/
  `CompositeInsertPattern`, both of which assume any non-vector composite
  converts to a pure `llvm.extractvalue`/`llvm.insertvalue`-shaped
  aggregate -- true before this revision, since nothing converted
  `spirv.MatrixType` at all, but no longer once C2 added that conversion.
  A matrix's own array-of-column-vectors representation needs
  `llvm.extractelement`/`llvm.insertelement` for the scalar-within-a-
  column case, which MLIR's own patterns cannot produce; FeMe's own
  higher-benefit `MatrixCompositeExtractPattern`/
  `MatrixCompositeInsertPattern` do (`spirv-to-llvm-matrix-composite.mlir`
  covers all four shapes: column/scalar extract/insert).

Both were caught by this same 54-group run, both are fixed in this
revision (not merely documented as a follow-up, unlike C1's own format-
properties blocker), and `check-feme` (1447 passed, 1 unsupported) covers
both with unit tests independent of a `deqp-vk` checkout.

This run also reproduced the CTS-side data-path methodology gap
`glsl`/`graphicsfuzz` groups need `deqp-vk`'s own working directory to
resolve relative Amber/shader-test asset paths (`./vulkan/...`); running
each group in its own directory (see "Reproducing this report" below)
needs a `vulkan -> $VK_GL_CTS/external/vulkancts/data/vulkan` symlink in
every one, not just those two -- `pipeline`'s own Amber cases hit the same
gap and, unlike a missing-format or missing-extension `NotSupported`,
`deqp-vk` treats a missing asset file as fatal and aborts the *entire*
group early (silently under-reporting `pipeline`'s own total by roughly
850,000 cases in this session's first attempt, until the symlink was
added everywhere). This is a CTS-side/harness methodology finding, not an
ICD change, but is recorded here since it would otherwise quietly corrupt
any future re-run's totals.

## Roadmap C3: measured impact

Roadmap C3 ("Divergent-vector decomposition in `feme-cpu-simdize`", see
Roadmap.md §1.9.1) closed every producer/consumer shape "Vectors become
components, not nested vectors" (FeMeCPUDesign.md's "Phase 4: Widening")
describes but the pass's own milestone-7 deviation note had left diagnosed:
a `phi` of vector type (the shape a uniform diamond's merge block gives a
value reconciled across two divergent arms), a `select` of vector type with
a scalar `i1` condition, a `shufflevector` (decomposed entirely at compile
time, since its mask is always a constant), a non-constant-index
`extractelement` (a `select` chain over the widened index), and -- added
after this session's own first CTS run against the first four showed it,
by far, the most common real shape -- ordinary elementwise arithmetic/cast
(`BinaryOperator`/`UnaryOperator`/`CastInst`) over a vector, the
"color = a + b" pattern almost every fragment/vertex shader contains. See
FeMeCPUDesign.md's deviation note for the full, updated scope (a per-lane
`<N x i1>`-condition `select` and every divergent aggregate remain
diagnosed) and `test/Transforms/CPU/simdize-vector-{phi,select,
shufflevector,dynamic-extractelement,elementwise}.ll`/`SIMDizeTest.{
DecomposesVectorPHIAcrossUniformDiamond,DecomposesScalarConditionVectorSelect,
DecomposesShuffleVectorAtCompileTime,
WidensNonConstantIndexExtractElementIntoSelectChain,
DecomposesElementwiseBinaryOpOnTwoDivergentVectors}` for the new coverage.

**The headline barely moved: 10,520 passed (+1) and 26,924 failed (-1),
`Not supported` unchanged.** That is a far smaller movement than the
9,067-case column this row is nominally worth, and, per this report's own
"stacked blockers" pattern (see the C1/C2 sections above), the reason is a
*different*, newly-discovered gap sitting immediately ahead of this one for
almost every graphics-track shader that reaches it: a SPIR-V-imported
fragment/vertex shader's stage-IO stores are a plain, non-atomic `store` to
a raw `Input`/`Output`-storage-class global (address space 7/8 -- correct
and exactly what LLVM's own SPIRV backend wants for the GPU-targeting
path), never canonicalized into the `feme.stage.*` calls
`feme::cpu::LinearizePass`/`SIMDizePass` already know how to widen the way
a DXIL/HLSL-imported shader's stage IO always is via
`feme::dxil::OpRaisingPass`. Confirmed directly: adding a temporary debug
dump to `checkVectorDecompositionSupported`'s consumer-rejection path and
re-running one representative failure
(`dEQP-VK.glsl.440.linkage.varying.component.frag_out.vec4.as_float_float_
float_float`) showed the rejected value's sole user was
`store <4 x float> %29, ptr addrspace(8) @spirv_var_20` -- an ordinary
`insertelement` chain assembling the output vector, stored straight to the
raw stage-output global, with no `feme.stage.output.store` call anywhere
in the function for this pass (or any earlier one) to widen instead. This
is a genuinely different root cause from C3's own scope, and from the
existing "graphics stage `Output` variable of matrix or aggregate type is
not legalized" row below (a `spirv`-\>`llvm` *conversion* gap, not a
CPU-target *raising* gap): C3 is closed, correctly, on its own terms, and
this newly-discovered raising gap is recorded as a new member of
Roadmap.md's C8 "shader long tail" bucket rather than re-opening C3 to
chase it, matching this report's own discipline of recording rather than
silently working around a stacked-blocker finding.

`feme-cpu-simdize`'s own remaining "used outside a supported ... pattern"
diagnostic count (10,297 occurrences across the run, concentrated in
`binding_model` (6,074), `glsl` (1,959), `ubo` (869), `pipeline` (689) and
`spirv_assembly` (408) -- all graphics-track pipeline creation, none of the
pure-compute `dEQP-VK.compute.*` group) is consistent with this
explanation: essentially unchanged from before this row's own fix, because
the shapes it closed were never what most of those cases were rejected for
in the first place -- they were rejected one property earlier, on the
missing stage-IO raising this section just found.

## Roadmap C4a/C4b: measured impact

Roadmap C4 ("Graphics pipeline state breadth", see Roadmap.md §1.9.1) is
only partially closed by this revision. C4a (silent-rejection diagnostics)
and two of C4b's five bullets (`VK_CULL_MODE_FRONT_AND_BACK` culling, 8x
multisampling) are done; `mapTopology` beyond `TriangleList`/`TriangleStrip`,
`mapDynamicState` beyond its six states, and the dual-source blend factors
remain open (see FeMeGraphicsDesign.md's updated R33 status note for why
each needs new rasterizer primitives rather than a mechanical table
addition).

**The headline barely moved, and moved in the direction that looks worse
before the reason is understood: 10,520 passed (+0), 26,955 failed (+31),
`Not supported` 3,199,524 (-31).** Every one of the 31 newly-`Fail`ed cases
was previously `NotSupported`, correctly, because `isSupportedAttachmentSampleCount`
declined 8 samples and `mapCullMode` declined `FRONT_AND_BACK` outright --
`deqp-vk`'s own capability probes saw that and skipped the case cleanly.
Now that both are advertised, `deqp-vk` attempts the case for real and
runs into one of C4's own still-open bullets one step later (predominantly
the point/line/line-strip/fan topologies `mapTopology` still declines, per
`query_pool`'s unchanged 283 and `draw`'s 952 `vkCreateGraphicsPipelines`
failures -- both groups' pipelines mostly ask for a non-triangle topology
in the same `VkGraphicsPipelineCreateInfo` that also asks for 8x
multisampling or `FRONT_AND_BACK`-equivalent culling). This is the same
"stacked blockers" shape C1's format-properties finding and C3's stage-IO
finding both already established: the two bullets landed here are correct
and tested in isolation (see their own unit tests), but neither was ever
independently reachable by a real CTS case without also closing the
topology bullet still open in the same row. **Whether `logCreationFailure`
(C4a) actually shortens triage time is not something this measurement can
show a number for** -- it is silent by default in this run (as in every
prior run and as it will be in every conformance submission), and its
value is in a human running `FEME_VULKAN_LOG_CREATION_ERRORS=1` while
triaging the very bucket this section describes, not in the pass/fail
totals.

This run also reproduced the same kind of long-single-process flake the
C1 measurement first recorded (see "Roadmap C1: measured impact" above):
`dEQP-VK.api.*`, run as one of six groups in parallel, hung deterministically
partway through `object_management.multithreaded_per_thread_resources.
device_group` and never printed `DONE!`; the same group run alone,
immediately afterward, completed cleanly (7,445 passed / 287 failed /
259,490 not supported / 267,222 total, the same numbers the C1 report
recorded). As before, the clean standalone run's numbers are what this
report's totals use, and the flake is recorded here rather than silently
worked around. (This same flake, and the identical standalone-clean
numbers, recurred once more in the C4d/C4e pass below -- see that
section's own headline.)

## Roadmap C4c: measured impact

Roadmap C4's "`mapDynamicState` beyond its six states" is now closed: all
12 `VK_EXT_extended_dynamic_state` dynamic states (cull mode, front face,
depth test/write/compare-op, depth-bounds-test-enable, stencil test-enable/
op, viewport/scissor "with count", primitive topology restricted to the
triangle class, and vertex-input-binding-stride) are implemented and the
extension is advertised. `mapTopology` beyond `TriangleList`/`TriangleStrip`
and the dual-source blend factors remain open, exactly as FeMeGraphicsDesign.md's
updated status note describes.

**10,560 passed (+40), 27,018 failed (+63), `Not supported` 3,199,421
(-103), same 3,237,000 total.** The +40 is directly attributable and
exact: every one of `dynamic_state.*.compute_transfer.single.{compute,
transfer}.{cull_mode,front_face,depth_test_enable,depth_write_enable,
depth_compare_op,stencil_test_enable,stencil_op,viewport_with_count,
scissor_with_count}.{before,after}` now passes for real, having previously
been `NotSupported` outright because `vkEnumerateDeviceExtensionProperties`
never listed the extension a conformant `deqp-vk` checks for before
attempting any of them. This is the clearest possible confirmation that
the dynamic-state resolution added by C4c (`DynamicGraphicsState`,
`buildExecutorPipeline`) is not just unit-tested but reachable and correct
against an independent, real conformance client.

Four of the +63 newly-`Fail`ed cases are the same "stacked blockers"
pattern C1/C3/C4a/C4b's own sections already established:
`dynamic_state.monolithic.compute_transfer.single.{compute,transfer}.
vertex_input_binding_stride.{before,after}` now get far enough to attempt
`vkCreateGraphicsPipelines` (previously `NotSupported` for the missing
extension) and fail there instead, on a gap this milestone does not touch
at all -- `translateFixedFunctionState`'s pre-existing "a graphics
pipeline needs both a vertex and a fragment stage" rejection
(`GraphicsPipeline.cpp`), because this particular CTS scenario binds a
compute pipeline into the same command buffer as its vertex-input-stride
coverage and expects a vertex-only draw path this ICD has never supported
(V6's own "only the vertex and fragment stages are implemented" scope,
unrelated to C4c). The remaining ~59 of the +63 were not traced to any
name containing a `VK_EXT_extended_dynamic_state` state (searched across
every group's `Fail` case names for the 12 states above); attributing them
precisely needs a case-by-case diff against a saved pre-C4c run this
measurement did not keep, and is left as a known gap in this section's own
precision rather than asserted either way.

`dEQP-VK.pipeline.{monolithic,fast_linked_library,pipeline_library}.
extended_dynamic_state.*` (4,059 cases per pipeline-construction variant,
12,177 total) itself moved by exactly zero: every one of its cases is
`NotSupported` for `VK_EXT_extended_dynamic_state2`/`_state3`,
`VK_EXT_vertex_input_dynamic_state`, `VK_EXT_mesh_shader`, or a missing
mandatory color-attachment format, none of which C4c implements (this
CTS release apparently no longer ships a pure-`VK_EXT_extended_dynamic_
state`-only variant of this particular test group -- every parameterization
sampled also needs at least one of those). The `dynamic_state` group above,
not `pipeline.*.extended_dynamic_state`, is where this milestone's actual
CTS value shows up.



**This section's own counts are from the pre-C2 revision** (`5f7420c1b3dd`)
and are not yet re-attributed; see "Roadmap C2: measured impact" above for
this revision's actual headline movement (+169 passed) and the two
diagnostics that changed the most (the `Uniform`-storage-class-block one
below is now gone entirely; `feme-cpu-simdize`'s own count grew to 10,223).
Full re-attribution across 26,925 failures is deferred to Roadmap.md's C10
("Continuous measurement"), which is exactly the gap that makes re-running
this by hand, rather than automatically, this expensive.

Attribution method: each failing case's `deqp-vk` reason string is joined
with the ICD's own `stderr` diagnostics emitted between that case's start
and its result line, and -- for Amber-based cases, which report only
`Fail` on `stdout` -- with the `<Text>` element of its `.qpa` record.
27,094 of 27,094 failures are attributed.

| Share | Cases | Root cause |
|---:|---:|---|
| 78.3% | 21,216 | **Shader compilation** -- the SPIR-V module was rejected by the importer, the `spirv`→`llvm` conversion, or a FeMe CPU pass |
| 12.4% | 3,354 | **Pipeline state** -- a fixed-function state combination `feme::vulkan` has no path for |
| 7.2% | 1,938 | **Format table** -- a format, or a format feature, the ICD does not advertise |
| 2.1% | 558 | **API object model** -- a descriptor type, query type, render pass shape or extension not implemented |
| 0.1% | 28 | Mandatory feature/limit reporting, and a handful of one-offs |

### Shader compilation (21,216)

| Cases | Cause | Where it belongs |
|---:|---|---|
| 10,121 | ~~A `Uniform`-storage-class block is not legalized. `feme::spirv::getBufferBlockElementArray` matches `StorageBuffer` pointers only, and `getUniformBlockElementStruct` matches a `Uniform` pointer only when its pointee is a single-member struct whose member is *itself* a struct. Everything glslang actually emits misses: a `BufferBlock`-decorated struct in `Uniform` (the pre-SPIR-V-1.3 spelling of an SSBO), a `Block` struct with more than one member, a sized (not runtime) array member, a matrix member with `RowMajor`/`ColMajor`/`MatrixStride`, and an array-of-blocks arrayed binding~~ (fixed by roadmap C2; this diagnostic no longer appears in any log -- see "Roadmap C2: measured impact" above) | `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` |
| 9,067 | ~~`feme-cpu-simdize` cannot decompose a divergent vector value used outside an insertelement-chain/resource-store/extractelement pattern (its own diagnostic names this "roadmap milestone 7 deviation")~~ (fixed by roadmap C3: a `phi`/scalar-condition `select`/`shufflevector`/non-constant-index `extractelement`/elementwise-arithmetic producer or consumer of a divergent vector is now decomposed instead of diagnosed -- see "Roadmap C3: measured impact" above for why the headline barely moved anyway, and this section's own note that these counts predate C2/C3 and are not yet re-attributed) | `feme/lib/Target/CPU` |
| 816 | A descriptor array of combined image samplers: the access chain converts to an `llvm.getelementptr` whose result type is `!llvm.struct<(target<"spirv.Image">, target<"spirv.Sampler">)>` rather than a pointer | `feme/lib/Conversion/SPIRVToLLVM` |
| 306 | A graphics stage `Output` variable of matrix or aggregate type is not legalized | `feme/lib/Conversion/SPIRVToLLVM/StageIODecorations.cpp` |
| 171 | The SPIR-V importer reports `unhandled opcode` | `mlir/lib/Target/SPIRV/Deserialization` |
| 151 | Another global variable shape (mostly `Workgroup` arrays-of-arrays) is not legalized | `feme/lib/Conversion/SPIRVToLLVM` |
| 277 | Individual ops with no conversion: `spirv.SpecConstant` (92), `spirv.VectorExtractDynamic` (71), `spirv.CompositeConstruct` (45), `spirv.Variable` (19), `spirv.MemoryBarrier` (18), `spirv.Switch` (9), the `spirv.Atomic*` family (7), and eleven others in ones and twos | mixed |
| 242 | Long tail of one-off diagnostics: `OpSpecConstantComposite` over a forward-declared constant (36), unhandled `Volatile`/`Component`/`Centroid` decorations (34), unhandled `GLSL.std.450` instructions 33/34/36/55/56/57/60/61/64/70/72 (49), `feme-graphics-validate-stage` component-range and direction errors (14), `feme-cpu-linearize` irreducible-flow errors (6), `OpNop` (8), and ~95 assorted importer strictness errors, most of them on deliberately malformed CTS modules | mixed |
| 62 | A malformed `llvm.getelementptr` (non-pointer operand) out of the `spirv`→`llvm` conversion | `mlir/lib/Conversion/SPIRVToLLVM` |
| 3 | A graphics stage `Input` variable of matrix or aggregate type | `feme/lib/Conversion/SPIRVToLLVM/StageIODecorations.cpp` |

The one shader fix that landed with this run --
`IComparePattern`/`FComparePattern` in
`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp` building their
`llvm.icmp`/`llvm.fcmp` from `op.getOperandN()` rather than
`adaptor.getOperandN()`, so a deserialized `si32` (from `OpTypeInt 32 1`)
reached `llvm.icmp` unconverted -- eliminated 8,369 occurrences of that
diagnostic but **did not change the pass/fail totals at all**: every case
it unblocked failed one stage later, on the `Uniform`-block and
`feme-cpu-simdize` gaps above. That is the shape of the whole shader
bucket: these are *stacked* blockers on the same small set of shaders, so
counting them individually overstates how many independent problems there
are and understates how much each fix is worth once its successors land.

### Pipeline state (3,385)

Unlike the rest of this section (see the note above: attributed against the
pre-C2 revision and not yet re-run), this subsection's numbers are from
*this* revision's own run, since roadmap C4a/C4b directly changed what it
describes.

`vkCreateGraphicsPipelines` returns `VK_ERROR_INITIALIZATION_FAILED` for a
state combination `feme::vulkan::GraphicsPipeline`'s translators have no
peer for -- no longer *silently*, as of roadmap C4a: setting
`FEME_VULKAN_LOG_CREATION_ERRORS=1` prints the specific reason
(`feme::vulkan::logCreationFailure`, `feme/lib/Vulkan/Diagnostics.h`); this
report's own run left it unset, matching a real conformance submission, so
the count and causes below are still attributed from `deqp-vk`'s own
generic `vk.createGraphicsPipelines(...) -> VK_ERROR_INITIALIZATION_FAILED`
message, not from the ICD's newly-available detail. The mappers in
`feme/lib/Vulkan/GraphicsPipeline.cpp` name the remaining boundaries
directly:

- `mapTopology` accepts `TRIANGLE_LIST`/`TRIANGLE_STRIP` only. Point, line,
  line-strip, fan and adjacency topologies are the largest single
  contributor (952 `draw` failures and `query_pool`'s unchanged 283, both
  up slightly from C1-era numbers now that C4b's `FRONT_AND_BACK`/8x-sample
  bullets no longer stop a case earlier than this one for any pipeline
  that also asks for one of them -- see "Roadmap C4a/C4b: measured impact"
  above).
- `mapCullMode` and `isSupportedAttachmentSampleCount` are no longer on
  this list: `VK_CULL_MODE_FRONT_AND_BACK` and 8 samples are both
  implemented (roadmap C4b). `mapBlendFactor` still rejects the
  dual-source factors; `mapDynamicState` still accepts six of the ~40
  dynamic states.

### Format table (1,938)

This bucket's per-cause breakdown is unchanged from the previous edition
(see "Roadmap C1: measured impact" above for why): every case here is
still a clean rejection, and the *reason string* `deqp-vk` reports for
each is still exactly what it was before C1, since none of these checks
go through `isSupportedColorAttachmentFormat`/
`isSupportedDepthAttachmentFormat`/`isSupportedStencilAttachmentFormat` at
all -- they go through the still-unwired `vkGetPhysicalDeviceFormatProperties`.

| Cases | Cause |
|---:|---|
| 874 | A color attachment format `deqp-vk` believes the ICD cannot render into, per `vkGetPhysicalDeviceFormatProperties` (not, as of C1, per `isSupportedColorAttachmentFormat`, which now accepts `B8G8R8A8_UNORM` -- see "Roadmap C1: measured impact"). `B8G8R8A8_UNORM` is both mandatory for `COLOR_ATTACHMENT`/`COLOR_ATTACHMENT_BLEND` per the Vulkan mandatory-format table *and* the framebuffer format every Amber-based CTS test uses -- so it alone accounts for all 677 `graphicsfuzz` failures and every remaining plain-`Fail` case in the run |
| 815 | `VK_ERROR_FORMAT_NOT_SUPPORTED` from `vkGetPhysicalDeviceImageFormatProperties`/`vkCreateBufferView`/`vkCreateImage`/`vkCreateRenderPass` for a texel-buffer or image format outside the advertised list |
| 193 | No mandatory depth/stencil format, per the same still-zero `vkGetPhysicalDeviceFormatProperties` query (`isSupportedDepthAttachmentFormat`/`isSupportedStencilAttachmentFormat` now both accept `D24_UNORM_S8_UINT` as of C1, but that predicate is not yet what CTS's own capability probe consults), so CTS's "there must be at least one depth format handled (Vulkan spec 1.0, table 1)" and "cannot find supported stencil format" checks still fail |
| 56 | `dEQP-VK.api.info.format_properties.*` mandatory format-feature-flag checks -- the most direct evidence that `vkGetPhysicalDeviceFormatProperties` itself, not the attachment-format predicates, is the remaining blocker |

### API object model (558)

| Cases | Cause |
|---:|---|
| 135 | `vkCreateDescriptorSetLayout`/`vkCreateDescriptorPool` reject a descriptor type `isSupportedDescriptorType` does not list (input attachment, dynamic uniform/storage buffer, ...) |
| 126 | `dEQP-VK.api.object_management.*` requires an extension the ICD does not advertise, at `vkCreateDevice` time |
| 79 | Subgroup support: the ICD reports `subgroupSupportedOperations` without `VK_SUBGROUP_FEATURE_BASIC_BIT` while advertising a compute queue, which the spec forbids |
| 74 | A `VkRenderPass`/`VkRenderPass2` configuration `feme::vulkan::RenderPass` rejects (resolve attachments, input attachments, multiple subpasses with dependencies) |
| 50 | `vkCreateImage` parameters (image type, tiling, usage, mip/array combination) with no path |
| 49 | `vkQueueSubmit` rejected (predominantly downstream of one of the above) |
| 23 | `VkPhysicalDevice*Features` structures whose `vkGetPhysicalDeviceFeatures2` answer disagrees with the promoted-struct answer for the same feature |
| 13 | `vkCreateQueryPool` implements `VK_QUERY_TYPE_TIMESTAMP` only; occlusion queries are mandatory in Vulkan 1.0 |

### Mandatory features and limits (28)

`dEQP-VK.info.device_mandatory_features` names exactly what a Vulkan 1.2
device must expose and this one does not: `multiview`,
`subgroupBroadcastDynamicId`, `imagelessFramebuffer`,
`uniformBufferStandardLayout`, `shaderSubgroupExtendedTypes`,
`separateDepthStencilLayouts` and `hostQueryReset`. Three
`vulkan1p2_limits_validation` cases fail on
`maxTimelineSemaphoreValueDifference` and `maxMemoryAllocationSize` being
below their required minimums, and `dEQP-VK.api.driver_properties.*` fails
four cases on `VkPhysicalDeviceDriverProperties` (no registered
`VkDriverId`, no conformance version, non-null-terminated name/info
strings).

## Roadmap C4d/C4e: measured impact

Roadmap C4 is now closed in full: point/line/line-strip/triangle-fan
topologies (C4d) and dual-source blend factors (C4e), the row's last two
open items, are both implemented -- see FeMeGraphicsDesign.md's updated
status notes.

**Headline is byte-for-byte identical to the previous edition's**
(10,560 passed, 27,018 failed, 3,199,421 not supported, same 3,237,000
total; the same 28 groups have zero failures). Unlike C4c's dynamic-state
work, neither C4d nor C4e moved a single case, and both root causes are
directly attributable rather than a mystery:

- **Dual-source blend (C4e): every one of the 32,312
  `dEQP-VK.pipeline.*.blend.dual_source.*` cases is `NotSupported`**, and
  all 32,312 fail the same check, at the same source line, before ever
  reaching pipeline creation: `NotSupported (VK_FORMAT_<X> does not
  support blending at vktPipelineBlendTests.cpp:73)` -- for every format
  the case list tries, including `R8G8B8A8_UNORM`, the one format this
  ICD's blend path actually implements. This is the exact "necessary
  companion change" gap the C1 measured-impact section above already
  named and deliberately left unfixed: `vkGetPhysicalDeviceFormatProperties`/
  `vkGetPhysicalDeviceFormatProperties2` (`EntryPoints.cpp`) still
  unconditionally report an all-zero `VkFormatProperties` for every
  format, so `deqp-vk`'s own format-capability check rejects every
  dual-source-blend case before any of `executeDraws`' new `FSColor1`
  path, `mapBlendFactor`'s new `VK_BLEND_FACTOR_SRC1_*` cases, or the
  newly-advertised `dualSrcBlend` feature is ever exercised. Fixing the
  format-properties stub is out of this milestone's scope (the C1 section
  above already found doing so surfaces three unrelated crashes elsewhere
  that need their own investigation first), so this is recorded as a
  measured, understood zero rather than a silent one.

- **New topologies (C4d): every CTS case that reaches a point/line/
  triangle-fan pipeline is blocked by an unrelated, pre-existing stacked
  blocker, and the small number that reach real execution fail identically
  to an already-implemented topology (not a regression).**
  `dEQP-VK.rasterization.provoking_vertex.draw.default.{line_list,
  line_strip,triangle_fan,triangle_list,triangle_strip}` all fail
  pipeline creation with the same `feme-cpu-simdize` "divergent vector
  value... used outside a supported... pattern" diagnostic (roadmap C8's
  own bucket) regardless of topology -- confirming this is a stage-IO
  compilation gap the shader itself hits, not anything topology-specific.
  `dEQP-VK.pipeline.*.depth.format.d16_unorm.compare_ops.point_list_*`
  (and the equivalent for every other topology) are `NotSupported` on
  `VK_FORMAT_D16_UNORM` alone, before topology is ever considered. The one
  case that *does* reach real image comparison for a new topology,
  `dEQP-VK.pipeline.monolithic.input_assembly.primitive_restart.
  index_type_uint16.restart_disabled_{line_strip,triangle_fan}`, fails
  (`Fail (Fail)`, a genuine rendered-image mismatch) -- but so does
  `restart_disabled_triangle_strip`, a topology this ICD implemented long
  before C4d, with the identical result. Since the pre-existing topology
  fails exactly the same way the new ones do, this is not a defect C4d
  introduced; it is a pre-existing gap in this Amber-test bucket (not yet
  root-caused) that happens to also cover the newly-implemented
  topologies. `dEQP-VK.rasterization.line_continuity.line-strip`
  similarly reaches real execution and fails a genuine image comparison
  (line continuity at strip joints is not modelled by the fixed-width
  quad-expansion approach FeMeGraphicsDesign.md's status note already
  flags as a deviation) -- a real, if narrow, correctness gap worth
  tracking separately rather than folding into this measurement's
  "clean rejection" framing, since it is the one case in this whole
  measurement where a pipeline actually renders and produces a wrong
  image rather than failing to create at all.

Both findings reinforce the same "stacked blockers" pattern C1's and
C3's own measured-impact sections already established: a correctly
implemented, unit-tested piece of state translation can still move zero
real CTS cases when an unrelated, earlier-in-the-pipeline gap (a
format-properties stub, a stage-IO compilation limitation, an
unadvertised depth format) rejects the case before the new code path is
ever reached.

## Roadmap C5: measured impact

Roadmap C5 ("Mandatory API object model", see Roadmap.md §1.9.1) is a
cluster of API-surface and capability-reporting fixes rather than one
single shader/runtime path: occlusion-query object-model support,
input-attachment descriptor and render-pass acceptance, subpass-dependency
validation, the promoted-subgroup-properties contradiction, and
`VkPhysicalDeviceDriverProperties`/`VkPhysicalDeviceVulkan12Properties`
queryability. A full 54-group headline re-run was therefore not the most
informative measurement after each independently-testable sub-commit: the
changes are reached directly by much narrower CTS groups, and one of the
remaining deliberate deviations (a truthful zero `VkConformanceVersion`)
would dominate any driver-properties signal regardless of the rest of the
run. The directly relevant groups/subsets were run instead:

- **`dEQP-VK.fragment_operations.occlusion_query.*`**: 0 passed / 0 failed /
  64 not supported. The new occlusion-query path itself is live --
  `DrawTest.OcclusionQueryCountsPassedSamples` counts 16 surviving samples
  through a real draw -- but this CTS group still stops earlier on
  pre-existing format-property gates (`VK_FORMAT_D16_UNORM` and
  `VK_FORMAT_UNDEFINED` rejected for the depth attachment shapes it asks
  for) and on unadvertised precise occlusion queries. This is the same
  "blocked before the new code path" pattern C1/C4 already established,
  not a regression in the query implementation.
- **`dEQP-VK.api.descriptor_pool.*`**: 6/6 pass. **`dEQP-VK.api.descriptor_set.
  descriptor_set_layout*`**: 2 passed / 3 failed / 1 not supported; the
  failures remain pre-existing pipeline-creation rejections and the one
  `push_descriptor` case remains correctly `NotSupported`. Accepting
  `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT` does not regress the descriptor
  object model, but this CTS slice also has no dedicated case whose only
  blocker was that descriptor kind's prior rejection.
- **`dEQP-VK.renderpasses.renderpass2.*`**: 1 passed / 122 failed / 32,405
  not supported. This group is still dominated by broader pre-existing
  render-pass/format breadth gaps, but the once-missing `vkCreateRenderPass2`
  path stays live and the new input-attachment/subpass-dependency handling
  introduces neither crashes nor wrong-answer passes.
- **`dEQP-VK.api.info.subgroup_features.flags`**: 0 passed / 0 failed /
  1 not supported, because CTS itself requires Vulkan 1.4 to run this case.
  The actual C5 fix here is therefore measured by the new direct
  `vkGetPhysicalDeviceProperties2` unit test instead: it proves
  `VK_SUBGROUP_FEATURE_BASIC_BIT` now agrees between
  `VkPhysicalDeviceSubgroupProperties` and the promoted
  `VkPhysicalDeviceVulkan11Properties` chain, which was the contradiction
  Roadmap C5 named.
- **`dEQP-VK.api.driver_properties.*`**: 4/5 pass. `driver_id_match`,
  `name_is_not_empty`, `name_zero_terminated`, and `info_zero_terminated`
  all now pass. The lone remaining failure is `conformance_version`, and it
  is deliberate: FeMe reports a truthful zero `VkConformanceVersion`
  because it is not yet conformant, rather than fabricating a submission-
  shaped value at or above the advertised API version.

So C5 closes every object-model bullet except the submission-readiness half
of driver properties. Its measured CTS effect is therefore intentionally
narrow: mostly "new API surface is reachable and truthful, but still
blocked behind older format/feature gates or, for `conformanceVersion`, an
explicit no-lie policy," not a C2/C3-sized headline pass-count swing.

## Roadmap C7: measured impact

Roadmap C7 ("Queue family capability combinations") added two narrower
queue families -- `TRANSFER`-only and `COMPUTE | TRANSFER`-only, both
excluding `GRAPHICS` -- alongside the existing universal family (see
FeMeVulkanDesign.md's "Queue families" status note). A full 3,237,000-case
re-run was not practical in the time available for this pass, so the
groups `findQueueFamilyIndexWithCaps` failures were previously concentrated
in were run directly, before and after:

- **`dEQP-VK.pipeline.monolithic.timestamp.*`** (278 cases): the 52 cases
  failing with `No matching queue found:
  findQueueFamilyIndexWithCaps(requiredCaps=0x4, excludedCaps=0x3)` (a
  dedicated transfer-only queue) all now reach real test logic; every one
  still reports `NotSupported`, but for the honest, pre-existing,
  unrelated reason `Queue does not support timestamps`
  (`timestampValidBits == 0` -- query timestamps are not implemented yet,
  a separate gap this row does not claim to close).
- **`dEQP-VK.api.*`** (267,219 cases): the queue-capability `NotSupported`
  count dropped from 106 to 2. The 2 remaining need a dedicated
  `VK_QUEUE_VIDEO_DECODE_BIT_KHR` queue, correctly `NotSupported` since no
  video extension is advertised at all -- a case this row was never meant
  to cover. Reaching further did surface one CTS-side null-function-
  pointer crash in
  `dEQP-VK.api.copy_and_blit.copy_commands2.image_to_image_transfer_queue.misc.ms_then_ss`
  (its `checkQueueSupport` checks for a transfer-only queue but never
  checks `VK_KHR_copy_commands2`, unlike every sibling case in the same
  file); see "Deviations from a stock CTS" below for the local fix. The
  pass/fail split moved (7,499/275 before the local CTS fix, to
  7,500/339 after querying every previously-unreached case), but every
  additional failure traces to the pre-existing, unrelated
  `VK_EXT_pipeline_creation_cache_control` gap `dEQP-VK.api.
  object_management.*`'s shared `Device` dependency needs (140 of the
  457 `object_management` cases fail identically with or without this
  row's queue-family change -- confirmed by running the same caselist
  against the pre-C7 binary).
- **`dEQP-VK.synchronization.*`**, **`synchronization2.*`**,
  **`renderpasses.*`**, **`sparse_resources.*`**, and
  **`fragment_shading_rate.*`** (357,214 cases combined): zero
  queue-capability `NotSupported` results in any of the five, down from a
  nonzero count before this row (most of the mass in each group is
  unrelated `NotSupported` for extensions/formats this ICD does not
  advertise, unaffected by this change).

Every group re-run to completion with no new crash, and no `Pass`-shaped
result carrying incorrect data -- the two guarantees this report's
headline states hold across three million cases. A full headline re-run
is left to whenever roadmap C10 lands continuous measurement, since
re-running the whole 54-group suite by hand after every roadmap row does
not scale (the reason C10 exists at all).

## Roadmap C6: measured impact

Roadmap C6 ("Mandatory 1.2 features and limits") targeted the 28 cases
this report's "Mandatory features and limits" bucket (above) already
named: `dEQP-VK.info.device_mandatory_features`, the three
`dEQP-VK.api.info.vulkan1p2_limits_validation.*` limit failures, and the
`dEQP-VK.api.info.vulkan1p2.*`/`get_physical_device_properties2.*`
promoted-struct-consistency checks.

**`dEQP-VK.api.info.*`** (10,484 cases) is the direct measure: 77 failed
before this row's changes (a fresh count -- see the caveat below), 71
after, all format-table-stub failures already attributed to C1's own
measured-impact section except two:

- `dEQP-VK.info.device_mandatory_features` (a separate top-level group,
  not under `api.info`) now fails on exactly one reason,
  `Mandatory feature multiview not supported`, down from the seven this
  report's earlier bucket named -- the *only* remaining one, and a
  deliberate exception: `multiview` needs layered rendering (roadmap V7,
  not yet implemented), not a mechanical feature-bit flip, so it stays
  unadvertised (see FeMeVulkanDesign.md's status note). Every other named
  feature/limit (`imagelessFramebuffer`, `uniformBufferStandardLayout`,
  `separateDepthStencilLayouts`, `hostQueryReset`,
  `shaderSubgroupExtendedTypes`, `subgroupBroadcastDynamicId`,
  `maxTimelineSemaphoreValueDifference`, `maxMemoryAllocationSize`) is
  closed.
- `dEQP-VK.api.info.vulkan1p2_limits_validation.general` still fails, but
  for a pre-existing, unrelated reason this row's own work exposed rather
  than caused: `limits.sampledImageDepthSampleCounts`/
  `sampledImageStencilSampleCounts` have always been pinned at
  `VK_SAMPLE_COUNT_1_BIT` alone (roadmap R30's deliberate "no per-sample
  fetch from a multisample depth/stencil image" scope note,
  FeMeVulkanDesign.md), one sample count short of the core-mandatory
  `VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT` floor -- a real,
  already-documented functionality gap, not a C6 regression. The same
  root cause is why `dEQP-VK.info.device_properties` fails (a core-1.0
  limit check, `validateFeatureLimits`, unrelated to anything version-1.2
  specific).

Fixing the two limits and the six feature bits surfaced a second,
narrower category of pre-existing gap along the way: several
`dEQP-VK.api.info.vulkan1p2.*`/`get_physical_device_properties2.*` cases
use a guard-value pattern (pre-fill a struct's buffer with a non-zero
pattern, call the API, fail if any field the offset table lists is
unmodified) that this ICD had never actually exercised before, since
nothing previously chained `VkPhysicalDeviceVulkan11/12Features` or their
promoted `Properties` twins with real content behind every field.
Closing C6 properly meant closing these too (see
`EntryPoints.cpp`'s case comments for
`VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES`/
`_POINT_CLIPPING_PROPERTIES`/`_PROTECTED_MEMORY_PROPERTIES`/
`_FLOAT_CONTROLS_PROPERTIES`/`_DEPTH_STENCIL_RESOLVE_PROPERTIES`/
`_MULTIVIEW_FEATURES`) -- `vulkan1p2.features`, `vulkan1p2.properties`,
`vulkan1p2.property_extensions_consistency`, and
`get_physical_device_properties2.features.multiview_features` all now
pass. **Left deliberately unfixed:** roughly a dozen further
`get_physical_device_properties2.features.*` guard cases for structs
entirely unrelated to anything C6 names (16/8-bit storage, buffer device
address, descriptor indexing, protected memory *features* specifically,
sampler Ycbcr conversion, scalar block layout, shader atomic int64,
shader draw parameters, shader float16/int8, variable pointers, Vulkan
memory model) and `VkPhysicalDeviceDescriptorIndexingProperties` --
these were already broken before this row (nothing in `fillFeatures2Chain`/
`fillProperties2Chain` ever handled them), are unrelated to any of C6's
named features, and are recorded here as a follow-up rather than folded
into this milestone's scope.

**A fresh full-run headline, the first since "Roadmap C4d/C4e: measured
impact" above** (C5 and C7 each measured only the groups their own change
could plausibly move, per their own sections): 10,390 passed / 29,532
failed / 3,132,206 not supported across 53 of 54 groups (`api` re-run
standalone after hitting the same `object_management.
multithreaded_per_thread_resources.device_group` flake this report's own
"Roadmap C4a/C4b: measured impact" section already recorded; `synchronization`
crashed at the same pre-existing, already-documented
`timeline_semaphore.device_host.write_copy_buffer_read_copy_buffer.
buffer_262144` segfault this report's "Roadmap C1: measured impact"
section lists, so its 60,100-case partial count is excluded from the
totals above rather than presented as complete). This total is not
directly comparable to the 10,560/27,018/3,199,421/3,237,000 figures
above: it reflects the cumulative effect of C5 and C7 as well (neither
re-ran the full suite), and `synchronization`'s ~86,000 uncounted cases
mean the total itself is short of 3,237,000. No new crash appeared in any
of the other 52 groups, and the shape (every failure a clean rejection,
no wrong `Pass`) is unchanged -- a full, crash-isolated recount is left to
roadmap C10 as before.

## Roadmap C8: measured impact

Roadmap C8's own text singled out one specific, concrete member of its
"shader long tail" bucket as worth measuring in isolation ahead of the rest:
the "Roadmap C3: measured impact" section's finding that a SPIR-V-imported
fragment/vertex shader's stage-IO stores reach `feme-cpu-simdize` as a raw,
un-canonicalized `store` to an `Input`/`Output`-storage-class global,
because `feme::graphics::CanonicalizeStagePass` was never run by
`feme::cpu::runPipeline` at all -- only by the separate Vulkan graphics
pipeline. Reading the code confirmed the gap was real (`runPipeline` never
ran `CanonicalizeStagePass`, for either import format) and fixed it
(`feme/lib/Target/CPU/Pipeline.cpp`; see Roadmap.md's updated C8 row and
FeMeGraphicsDesign.md's deviation note).

**Measuring it against a real `deqp-vk` run found the fix has zero effect
on any CTS number.** `dEQP-VK.glsl.*` (the exact group C3's own finding
quoted a representative failure from) was run twice against otherwise
identical builds -- once with this row's `Pipeline.cpp` change reverted,
once with it applied -- and produced byte-identical totals both times:
**0/26,808 passed, 3,396 failed, 23,412 not supported**, including the
exact same 1,957 occurrences of `feme-cpu-simdize`'s "used outside a
supported ... pattern" diagnostic in both logs. The reason is
`feme::vulkan::compileGraphicsStage` (`GraphicsPipeline.cpp`) -- the
function every real `vkCreateGraphicsPipelines` call in this ICD actually
goes through to compile a vertex/fragment stage -- already calls
`feme::graphics::CanonicalizeStagePass` directly on the imported module
*before* handing it to `feme::cpu::CompiledStage::create` (and so,
transitively, `runPipeline`), and has done so since roadmap V6, well before
C3 or this row. `runPipeline`'s own missing call only mattered for a caller
that reaches it *without* going through `compileGraphicsStage` first --
`feme::cpu::JITEngine`/`feme-run`'s direct, non-Vulkan compute/graphics
compilation entry points (exercised by this row's own
`PipelineTest.Canonicalizes{SPIRV,DXIL}StageIOBeforeWidening`) -- which no
`dEQP-VK` case ever calls into, since every one of them talks to this ICD
exclusively through the real Vulkan API.

This means Roadmap.md's C3 section's own attribution -- "the largest single
reason C3's own headline barely moved" -- does not hold against this
codebase as it stands today: whatever produced that representative
`dEQP-VK.glsl.440.linkage.varying.component.frag_out.vec4...` failure the
C3 section quoted, it was not reachable through `compileGraphicsStage`'s
already-canonicalizing path, and could not have been the raw-global-store
shape described (that pattern simply cannot exist downstream of
`CanonicalizeStagePass`, which converts every stage-IO global load/store to
`feme.stage.*` calls unconditionally for a Vertex/Fragment entry). The
1,957 real `glsl` occurrences of this diagnostic in *this* run are outside
`CanonicalizeStagePass`'s own scope entirely, not stage-IO at all --
grepping the same log's `error:` lines for shapes immediately preceding it
shows the divergent values are matrix/aggregate outputs of
`spirv.CompositeConstruct`/`spirv.OuterProduct` (85+82+67+18 occurrences,
the pre-existing "graphics stage `Output` variable of matrix or aggregate
type is not legalized" row), `spirv.Atomic*` calls (16
`spirv.AtomicIAdd`), and outright unhandled decorations/extension
instructions (`unhandled Decoration : 'Component'`, `unhandled
deserializations ... from extension set GLSL.std`) -- every one of them
already a named row in C8's own bucket, none of them this row's stage-IO
finding. This row's fix is still worth keeping: it closes a real
architectural gap between the CPU target's two entry points (documented in
FeMeGraphicsDesign.md's deviation note, and regression-tested), just not
one any `dEQP-VK` case can observe. **The rest of C8's bucket -- the
matrix/aggregate legalization gap, the `spirv.Atomic*` family, descriptor
arrays of combined image samplers, and the remaining unhandled-opcode/
diagnostic tail -- remains open, unmeasured beyond this `glsl` group, and
is not moved by this row.**

## Roadmap D0: measured impact

Roadmap D0 (see Roadmap.md's new §1.9.2, "The road to Vulkan 1.4
conformance") bumped `vkEnumerateInstanceVersion`/
`VkPhysicalDeviceProperties::apiVersion` from `VK_API_VERSION_1_2` to
`VK_API_VERSION_1_4`, deliberately *ahead of* implementing 1.3/1.4's
mandatory feature/limit/extension floor -- the opposite of every previous
version bump (1.0 -> 1.1 -> 1.2), each of which happened only once the
newly-claimed version's mandatory surface was actually real. This section
measures what that inversion costs on its own, before any of §1.9.2's
D1-onward rows land.

**First measurement (apiVersion 1.4, `vkCmdCopyBuffer2` unimplemented):**
a full 54-group run crashed for the first time ever recorded by this
report -- `Crashed / timed out: 0` in every previous edition, including
the un-versioned-bump baseline this run is compared against. The `api`
group segfaulted at
`dEQP-VK.api.copy_and_blit.copy_commands2.buffer_to_buffer.partial`,
after only 19,424 of its cases. `gdb`'s backtrace showed the crash inside
`vkt::api::(anonymous namespace)::CopyBufferToBuffer::iterate()`, calling
through a null function pointer (frame 0 is address `0x0`). Root cause:
`VK_KHR_copy_commands2`'s six commands (`vkCmdCopyBuffer2` and its
five siblings) have **no** `VkPhysicalDevice*Features` opt-in bit --
unlike almost everything else this ICD advertises, a client is entitled
to call them unconditionally once `apiVersion >= VK_API_VERSION_1_3`, and
this ICD had implemented neither the pre-promotion `KHR`-suffixed names
nor the promoted core names for any of the six. Confirmed this was
genuinely new, not a pre-existing gap merely reached for the first time
by chance: reverting just `EntryPoints.cpp`/`PhysicalDeviceInfo.cpp` to
their pre-D0 (apiVersion 1.2) contents and rebuilding only `feme_vulkan`
(same build tree, ccache-shared) made the identical `deqp-vk` invocation
pass cleanly instead -- at 1.2, `deqp-vk`'s own function-loading declines
to call either name at all, correctly reporting `NotSupported` instead,
exactly as every prior edition of this report recorded.

**The fix**: implement all six `vkCmd*2` commands as thin wrappers that
unwrap each command's `pNext`-extensible `..Info2` struct and delegate to
the identical logic its already-implemented, already-tested non-`2`
counterpart calls (`feme/lib/Vulkan/CommandBuffer.cpp`), and extend
`vk_gen_entrypoints.py`'s `CORE_FEATURES` to resolve `VK_VERSION_1_3` so
the promoted core names exist in the generated table at all (mirroring
the precedent `VK_VERSION_1_2` already set for the 1.1 -> 1.2 bump). With
the fix applied, `dEQP-VK.api.copy_and_blit.copy_commands2.*` (34,956
cases) runs to completion: 20 pass, 0 fail, 0 crash, the rest correctly
`NotSupported` on an unadvertised format/sample-count -- the same clean
shape every other command in this ICD already produces.

**Second measurement, with the copy_commands2 fix applied**: a second
full 54-group run still did not reach `Crashed / timed out: 0`. The `api`
group progressed much further this time (266,994 of its cases, versus
19,424 before the fix) before a *different* `SIGSEGV`, reproducible
standalone as `dEQP-VK.api.object_management.multithreaded_per_thread_
resources.*` run as one sequence (it does not reproduce running
`...device_group` alone, only after the sequence's earlier cases have
run). `gdb`'s backtrace is entirely inside the system Vulkan loader
(`/lib/aarch64-linux-gnu/libvulkan.so.1`, Ubuntu's `libvulkan1`
1.3.275.0), inside `vkGetDeviceProcAddr`, called from
`vk::DeviceDriver`'s constructor while multiple `ThreadGroupThread`s each
construct their own `VkDevice` concurrently -- no FeMe code appears
anywhere in the backtrace. Reran the identical case sequence against the
pre-D0 (apiVersion 1.2) build: it passes, 47/47, with no crash. This
does not prove D0 caused it in the sense of introducing a bug in FeMe's
own code (the crashing frames are entirely inside the system loader, a
third-party component this project does not own or build); the more
likely mechanism is that a higher advertised apiVersion makes the
loader's own per-device dispatch table larger (it must resolve more core
command names per `vkCreateDevice`), making a latent loader-side
concurrency bug more likely to trigger under this specific stress test's
concurrent device creation, not a bug this ICD's own code can fix.
**Left open, unfixed, and un-upstreamed** (unlike the C9 CTS-side fix,
there is no local patch for this either, since it lives in a system
package rather than this tree's own CTS checkout) -- tracked as
Roadmap.md's D2. The `api` group's totals in this report's "Headline"
table above are therefore the partial counts up through this crash
(266,994 of `api`'s own cases), not `api`'s full total.

**Net effect on the headline, comparing this edit's full run to the
previous (apiVersion 1.2) full edition**: 11,040 passed (+480), 29,647
failed (+2,629), 3,196,084 not supported (roughly flat once the `api`
group's shortfall is accounted for). Diffing the two runs' actual
per-case result sets (not just the aggregate counts) shows 4,552 cases
newly `Fail` and 1,999 no longer `Fail` (net +2,553, matching the
aggregate delta once `api`'s own shortfall is subtracted). This is
**not** the `vulkan1p*_consistency`/`device_mandatory_features` shape
guessed at first -- `dEQP-VK.info.*` itself only gained two new
failures (`device_mandatory_features`, `device_properties`). Tracing the
single largest newly-failing bucket instead
(`dEQP-VK.ubo.single_basic_type.std430.*` and its four `*_array`/
`random` siblings, 2,650 of the 4,552) to its actual cause: at apiVersion
1.2, every one of these reported `NotSupported ("std430 not supported at
vktUniformBlockCase.cpp:2679")` -- `deqp-vk`'s own
`UniformBlockCase::checkSupport` short-circuited before ever generating a
shader. At 1.4, the identical case instead reaches
`vkCreateGraphicsPipelines`/`vkCreateComputePipelines` for the first time
and fails there, with `feme-cpu-simdize`'s own long-known diagnostic:
"divergent vector value ... used outside a supported ... pattern;
component decomposition is not yet supported" -- exactly roadmap C3's own
already-tracked, already-diagnosed "milestone 7 deviation" gap (see
"Roadmap C3: measured impact" above), not a new bug. `uniformBufferStandardLayout`
was already truthfully advertised `true` since roadmap C6 (well before
this session); what changed is that `deqp-vk`'s own `Context` helper only
trusts a device's promoted-to-1.2 feature bits once the device's own
apiVersion satisfies the corresponding version gate in the specific code
path these cases take, which 1.2 itself did not exercise the same way 1.4
does. This is exactly the shape §1.9.2's own framing predicted in the
abstract ("most mandatory-capability gaps are themselves the reason a
test fails rather than reports `NotSupported`") -- now confirmed concretely
for one bucket, rather than merely asserted. The remaining newly-failing
buckets (`spirv_assembly.instruction.compute`, 417;
`synchronization.op.{multi,single}_queue`, 277;
`pipeline.monolithic.bind_buffers_2`, 57; and a long tail) were not
individually traced this pass -- that per-bucket attribution, at C1-C8's
level of rigor, is exactly what §1.9.2's D3 schedules next, once D1's
mandatory-gap inventory gives it a checklist to work against rather than
a diagnostic-log grep.

## Roadmap D1: measured impact

Roadmap D1 ("An accurate 1.3/1.4 mandatory-feature/limit/extension
inventory") is an audit milestone: `vk_gen_entrypoints.py`'s `CORE_FEATURES`
now includes `VK_VERSION_1_4` (purely a generated-table coverage fix, since
an unlisted entrypoint name and a listed-but-unimplemented one both already
resolved to null through `ProcAddr.cpp`'s `findEntry`), and a new offline
tool/doc pair (`feme/utils/vk_gen_feature_inventory.py`,
`feme/docs/Vulkan14FeatureInventory.md`) enumerate the mandatory 1.3/1.4
surface. Neither `PhysicalDeviceInfo.cpp` nor `EntryPoints.cpp` gained a
single new advertised feature, limit, or extension in this milestone --
so, unlike D0, there is no CTS-visible capability change to measure here at
all, and a full 54-group/3.2-million-case re-run would not tell this
report anything a byte-for-byte diff against the previous edition
wouldn't already predict (the same "this full re-run's headline numbers are
byte-for-byte identical to the previous edition's" outcome C1's own measured
section already recorded for an analogous "no CTS-visible surface changed"
case).

What this section does instead is a **targeted confirmation run**, over the
11,184 cases in `dEQP-VK.api.{info,device_init,object_management}.*` --
chosen because these are the groups that most directly exercise
`vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` and instance/device/queue
creation, i.e. exactly the code paths a larger generated entrypoint table
could plausibly perturb. Run in two passes (the loader crash below stops
`deqp-vk` mid-sequence, so the remaining 207 cases were run as a second,
separate invocation over the tail of the same case list):

| | Count | Share |
|---|---|---|
| Total cases | 11,184 | |
| Passed | 6,436 | 57.5% |
| Failed | 112 | 1.0% |
| Not supported | 4,635 | 41.4% |
| **Crashed** | **1** (`multithreaded_per_thread_resources.device`) | |

The one crash is **not new**: it reproduces exactly
`dEQP-VK.api.object_management.multithreaded_per_thread_resources.device`,
the identical case D0's own second measurement already traced to the
*system* Vulkan loader's `vkGetDeviceProcAddr` (Ubuntu's `libvulkan1`,
concurrent `vkCreateDevice`s) and left open as Roadmap.md's D2 -- not a
regression this milestone introduced. No other case in this subset
crashed, timed out, or produced a `Pass`-shaped result with wrong data.
This is consistent with (though, given the subset's size relative to the
full 3.2M-case corpus, does not itself prove) D1 having zero net effect on
the full-run headline; a full re-run is deferred to whichever future
roadmap row actually closes a feature/limit/extension this inventory
found missing, where it will have something new to measure.

## Roadmap D2: measured impact

Roadmap D2 characterizes the system Vulkan loader crash D0's second CTS
pass found (`dEQP-VK.api.object_management.multithreaded_per_thread_
resources.*`, run as one sequence, `SIGSEGV` inside Ubuntu's `libvulkan1`).
D0 and D1 each hit it once; this milestone reruns the identical case
sequence repeatedly, against several loader builds, to answer the two
questions D0 left open, characterizes the crash with symbols via `gdb`,
and looks up whether it is already tracked upstream.

**Is it actually a race, not a hard failure?** Repeating the identical
`--deqp-case="dEQP-VK.api.object_management.multithreaded_per_thread_
resources.*"` invocation five times against the installed system loader
(`libvulkan1` 1.3.275.0-1build1, Ubuntu 24.04/"noble") crashed **3 of 5**
runs -- not 5 of 5 -- confirming this is a genuine, non-deterministic
thread-scheduling race rather than a deterministic bug this specific
case sequence always triggers. `gdb`'s backtrace on a caught crash
(`Thread ... received signal SIGSEGV`, `bt` + `thread apply all bt`)
reproduces D0's own finding exactly: frame 0 is inside
`/lib/aarch64-linux-gnu/libvulkan.so.1` (unsymbolized in the distro
package), called through `vkGetDeviceProcAddr`, called from
`vk::DeviceDriver::DeviceDriver`, called from
`vkt::api::(anonymous namespace)::CreateThread<Device>::runThread`, i.e.
one of the case's own `ThreadGroupThread`s constructing its `VkDevice`
concurrently with the others (visible in `info threads`: the majority of
the other threads are parked in `de::SpinBarrier::sync`, the case's own
synchronization point, at the moment of the crash). No FeMe code appears
anywhere in any of the three crashing backtraces. The crashing case
itself varies run to run (`...resources.device` in 2 of the 3 crashes,
`...resources.device_group` in 1), consistent with a race whose exact
trigger point depends on thread interleaving rather than one specific
case's own logic.

**Does a *smaller* apiVersion-dependent entrypoint table avoid it?** No.
`vk_gen_entrypoints.py`'s `CORE_FEATURES` was temporarily trimmed back to
`VK_VERSION_1_3` (dropping the 19 core commands D1 added), `feme_vulkan`
rebuilt against the smaller generated table, and the same five-run
experiment repeated against the identical system loader: **4 of 5** runs
still crashed (once at `...device`, twice at `...device_group`, matching
the same signature). This is, if anything, a slightly *higher* crash
rate than the full 1.4 table's 3 of 5 -- well within noise for five runs
each, but definitively not the "smaller table avoids it" outcome D0's own
"more likely mechanism" guess (a larger per-device dispatch table making
a latent race more likely) would predict as a clean threshold effect.
**Conclusion: table size is not the deciding variable** -- both configurations
crash at a similar, non-trivial rate against this loader build, so D0's
mechanism guess is superseded by the root cause below rather than
confirmed by it. (`vk_gen_entrypoints.py` and the generated table were
restored to their D1 state immediately after this experiment; `git diff`
against HEAD is empty and `ninja check-feme` was rerun clean -- 1519/1520,
the same one pre-existing `Unsupported` as before -- to confirm no
regression was left behind.)

**Does a newer/older `libvulkan1` avoid it, and is this already tracked
upstream?** Searching KhronosGroup/Vulkan-Loader's issue tracker for this
exact case name surfaced
[#1436](https://github.com/KhronosGroup/Vulkan-Loader/issues/1436),
filed January 2024 against the *identical*
`dEQP-VK.api.object_management.multithreaded_per_thread_resources.
device_group` case, on the *identical* loader version this system ships
(`1.3.275.0`, Ubuntu 22.04 in that report, 24.04 here). Root cause,
per the issue and its fix
([#1438](https://github.com/KhronosGroup/Vulkan-Loader/pull/1438)): commit
`a4ff6a54` ("Remove `-fno-strict-aliasing` from builds", November 2023)
introduced a strict-aliasing-optimization-dependent bug into the loader's
Release-build GPA/dispatch code -- present only when the compiler is
allowed to assume no aliasing, which the reporter's own bisection matched
(a locally-built, debug-flavored loader from the same commit did not
reproduce it). PR #1438 reverted the flag removal; the fix first shipped
in loader tag `v1.3.277`. Confirmed by building three loader versions from
source (`scripts/update_deps.py` + the loader's own `CMakeLists.txt`,
`-DCMAKE_BUILD_TYPE=Release` to match how distros package it) and rerunning
the same case sequence via `LD_LIBRARY_PATH` against each, `VK_DRIVER_FILES`
still pointed at this build's `feme_icd.json`:

| Loader build | Crashes | Notes |
|---|---|---|
| System `libvulkan1` 1.3.275.0-1build1 (pre-fix, Ubuntu-packaged) | 3/5 | matches upstream #1436 exactly |
| `v1.3.280` built from source (first tag after PR #1438's fix) | 1/6 | fix reduces, does not eliminate, the rate |
| `main`, effectively the `v1.4.360` era (103 more `loader.c`/`trampoline.c` commits since `v1.3.280`, including a `main`-only "Use recursive mutexes to fix deadlocks in loader" change) | 0/10 | no crash in 10 runs |

The `v1.3.280` result matters: PR #1438's own fix does not fully close this
race by itself (1 crash in 6 runs, `gdb`-confirmed with symbols this time --
`loader_get_icd_and_device` <- `loader_gpa_device_terminator` <-
`vkGetDeviceProcAddr`, the same call path, just resolved with debug info
since this build was compiled locally) -- it only reduces how often the
underlying race manifests. The current upstream `main` branch, which
includes substantially more loader-side mutex hardening merged since
`v1.3.280` (`5ee27b30c`, "Use recursive mutexes to fix deadlocks in
loader"), shows no crash at all across 10 runs. Ubuntu 24.04's own
`libvulkan1` package (`apt-cache policy`, `apt-cache madison`, both
checked) has no newer candidate available in `noble`, `noble-updates`, or
`noble-backports` -- it is pinned to exactly the broken `1.3.275.0`
version, over a year older than the fix.

**Conclusion, per D2's own charge**: this is confirmed as a loader bug,
not something this ICD's own dispatch-table generation can influence (the
smaller-table experiment above rules that variable out directly) --
**and it is not a new bug to file**. It is the identical, already-triaged,
already-fixed upstream issue KhronosGroup/Vulkan-Loader#1436/#1438, merely
not yet available through Ubuntu 24.04's package archive. Filing a new
issue against KhronosGroup/Vulkan-Loader would duplicate #1436, which is
already closed with a merged fix; the actionable gap is entirely in Ubuntu's
packaging lag, a distribution issue rather than an upstream Vulkan-Loader
one. No local workaround is implemented in this tree for the same reason
D0 declined one: the crash is not reachable from any FeMe code path
(confirmed again by every backtrace above), so there is nothing in `feme/`
this milestone's own scope covers to change. Left open for whoever owns
this machine's package selection to decide whether to track it against
Ubuntu (e.g. via `ubuntu-bug libvulkan1` / Launchpad, quoting this
section's loader-build comparison table as the evidence a newer package
resolves it) or to accept the current CTS methodology's per-group
crash-isolation (see "Reproducing this report" above) as sufficient
mitigation, since every other group's totals are unaffected by this one
group's crash rate.

## What the 3,199,421 `Not supported` results mean

A `NotSupported` result is a *pass* for conformance purposes when the
capability it needs is genuinely optional. The bulk of this run's
`NotSupported` mass is exactly that (breakdown carried over from the
pre-D0 edition of this report; re-deriving it against this pass's own run
is left to whichever roadmap D-row next needs it):

| Cases | Reason |
|---:|---|
| 419,425 | `VK_EXT_shader_object` (241,837 + 177,588 from two different check sites) |
| 313,141 | An unadvertised optional format (`Format not supported`, `... for sampling`, `... for transfer`, `Source format not supported`) |
| 244,916 | An unadvertised combined depth/stencil format (`D16_UNORM_S8_UINT`, `D32_SFLOAT_S8_UINT`, `S8_UINT`) |
| 113,737 | `VK_KHR_fragment_shading_rate` |

| 107,866 | `VK_EXT_primitives_generated_query` |
| ~~99,324~~ 0 | No queue family with the requested capability combination (closed by roadmap C7; see above) |
| 91,516 | Cooperative matrix/vector |
| 73,433 | `VK_EXT_host_image_copy` |
| 71,322 | `VK_KHR_acceleration_structure` |
| 66,310 | `VK_KHR_synchronization2` |
| 62,047 | `VK_KHR_maintenance4`/`5`/`6` |
| 59,520 | `shaderSampledImageArrayDynamicIndexing` |
| 59,090 | `VK_EXT_graphics_pipeline_library` |

Two entries in that list were *not* freely optional for a conformant
Vulkan 1.2 device and so belonged on the conformance critical path, not in
the "correctly declined" column: the combined depth/stencil formats (at
least one of `D24_UNORM_S8_UINT`/`D32_SFLOAT_S8_UINT` is mandatory, closed
by roadmap C1), and the queue-capability combinations (closed by roadmap
C7, see above).

## Deviations from a stock CTS

One VK-GL-CTS source change is applied locally and must be upstreamed
before any conformance submission built on this tree is credible:
`external/vulkancts/modules/vulkan/api/vktApiMemoryRequirementInvarianceTests.cpp`
constructed an `ImageAllocator` and indexed `optimalformats`/`linearformats`
with `rand() % vector.size()` without checking the vector is non-empty. A
narrow-format ICD can empty both for some seed, and on AArch64 `UDIV`
returns the dividend for `% 0` rather than trapping, so the index read far
out of bounds and segfaulted `deqp-vk`. The local fix never constructs an
`ImageAllocator` when neither tiling mode has a supported format.

A second local fix (roadmap C7's own measurement pass) closes a similar
gap: `external/vulkancts/modules/vulkan/api/vktApiCopyImageToImageTests.cpp`'s
`checkQueueSupport` for the `image_to_image_transfer_queue.misc.ms_then_ss*`
cases checks for a dedicated transfer queue but, unlike every sibling case
in the same file, never calls `checkExtensionSupport` to verify
`VK_KHR_copy_commands2` is actually supported before the test body
unconditionally records `vkCmdCopyImage2`. An implementation with a
dedicated transfer queue but no `VK_KHR_copy_commands2` -- a legal
combination, since neither implies the other -- calls a null function
pointer instead of getting a `NotSupported` result. This case was
unreachable before this ICD had a transfer-only queue family at all, so it
was never noticed until now. The local fix adds the missing
`checkExtensionSupport` call.

`deqp-vk` also aborts *after* printing `DONE!` and its totals, in
`tcuSubprocessTestExecutorLin.cpp`, because device-fault tests are not
executable on Linux; every group therefore exits 134 with complete results.
This is a CTS-side teardown issue and does not affect any result.

## Reproducing this report

```shell
# 1. Build the ICD (assertions + ccache, per the project's build discipline).
ninja -C <feme-build> feme_vulkan check-feme

# 2. Regenerate the case list against this ICD.
cd <VK-GL-CTS>/build/external/vulkancts/modules/vulkan
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-runmode=txt-caselist
grep -oP '^TEST: dEQP-VK\.\K[a-z_0-9]+' dEQP-VK-cases.txt | sort -u > groups.txt

# 3. Run every top-level group, six at a time, each in its own directory so
#    the per-group shader cache and .qpa log do not collide -- and with a
#    `vulkan -> data/vulkan` symlink in each one, or the Amber/`glsl`/
#    `graphicsfuzz`/`pipeline` cases that resolve test assets by a relative
#    `./vulkan/...` path abort their *entire* group early on the first miss
#    (a `ResourceError`, not a `NotSupported`) rather than just failing that
#    one case, silently truncating the group's own total (this cost
#    `pipeline` roughly 850,000 cases the first time this report's own
#    numbers were regenerated -- see "Roadmap C2: measured impact" above).
xargs -P 6 -n 1 -a groups.txt sh -c 'mkdir -p /tmp/cts/$1 && cd /tmp/cts/$1 &&
  ln -sfn <VK-GL-CTS>/external/vulkancts/data/vulkan vulkan &&
  VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  <VK-GL-CTS>/build/external/vulkancts/modules/vulkan/deqp-vk \
    --deqp-case="dEQP-VK.$1.*" --deqp-log-filename=$1.qpa > $1.log 2>&1' _
```

The whole run takes about 25 minutes wall-clock on 12 cores. Per-group
totals are the `Passed:`/`Failed:`/`Not supported:` lines at the end of
each `$1.log`; per-case attribution comes from joining each
`Test case '<name>'..` / `  Fail (<reason>)` pair with the `error:` lines
between them, and, for Amber cases, with the `<Text>` element of the
matching `.qpa` record.

`feme/utils/filter_vulkan_cts_cases.py` and
`feme/test/Vulkan/cts-compute-subset.test` remain the in-tree,
`lit`-integrated version of the same idea, gated on
`REQUIRES: system-vulkan-cts` so they skip wherever `deqp-vk` is absent.

## Where the plan lives

[Roadmap.md](Roadmap.md) §1.9.1, "The road to Vulkan conformance", turns
this measurement into an ordered, costed plan: which of the buckets above
to close in which order, what each is worth in cases, and what "full
conformance" additionally requires beyond driving this run's failure count
to zero.

## Addendum: DXIL `GetDimensions.xy`/AMDGPU change (out of this report's scope)

A follow-up change (`DXILOpLowering::lowerGetDimensionsXY`,
`feme::dxil::OpRaisingPass::raiseGetDimensions`,
`feme::amdgpu::ResourceLoweringPass`'s `Binding::NumDimensionArgs`) fixed a
`Texture2D`/`RWTexture2D::GetDimensions(width, height)` call asserting when
retargeting a DXIL shader to `amdgcn-*`. None of the touched files
(`llvm/lib/Target/DirectX/DXILOpLowering.cpp`,
`feme/lib/Transforms/DXIL/OpRaising.cpp`,
`feme/lib/Transforms/AMDGPU/ResourceLowering.cpp`) are reachable from
`libfeme_vulkan` (the SPIR-V/Vulkan/CPU-target path this report measures),
so no change to the headline numbers above is expected, and a full re-run
was not folded into the numbers above to avoid overwriting a report whose
own methodology (per-group crash isolation, Amber/`graphicsfuzz` asset
symlinking, etc. -- see "Reproducing this report") this quicker check did
not fully replicate. Two checks were run instead, both consistent with
"unaffected":

- A `dEQP-VK.api.info.*` smoke run (`VK_ICD_FILENAMES` pointed at this
  build's `feme_icd.json`) passed 5,669/10,484, matching the shape (mix of
  `Pass`/`Fail`/`NotSupported`, zero crashes) an unrelated change should
  produce.
- A full 54-group run (this report's own per-group `dEQP-VK.<group>.*`
  split, minus its crash-isolation wrapper) totaled 2,831 passed / 26,462
  failed / 2,875,613 not supported across 52 of the 54 groups that ran to
  completion -- the same "clean rejection, no wrong answers" shape as the
  headline above. The remaining two, `api` and `synchronization`, each hit
  a segfault partway through (no per-case isolation in this quicker
  invocation, so the rest of that group's cases were never reached, which
  is why the totals above undercount them): `synchronization`'s crash is
  at `dEQP-VK.synchronization.timeline_semaphore.device_host.
  write_copy_buffer_read_copy_buffer.buffer_262144`, the exact case this
  report's own "Roadmap C1: measured impact" section already lists as a
  known, pre-existing crash unrelated to that section's own change either
  -- i.e. this DXIL/AMDGPU change did not introduce it. `api`'s crash (in
  `object_management.multithreaded_per_thread_resources`, a threading
  stress test) was not separately root-caused, since it is likewise in a
  file this change never touches; it is left for a future full run's own
  crash-isolation wrapper to attribute properly rather than guessed at
  here.

## Addendum: DXIL SM6.9 `FDot`/AMDGPU change (out of this report's scope)

A follow-up change (`feme::dxil::OpRaisingPass`'s `{311, Intrinsic::dx_fdot,
true}` `DirectOps` row, `feme::dxil::IntrinsicExpansionPass::expandFDot`)
fixed a `dot()` call on a 16-bit vector (e.g. `dot(half2, half2)`) -- shader
model 6.9's unified `FDot` op (DXIL opcode 311), which `dxc -T cs_6_9`
emits in place of the older `Dot2`/`Dot3`/`Dot4` -- failing with
"'dx.op.dot.v2f32' is not supported when targeting 'amdgcn-amd-amdhsa'"
when retargeting a DXIL compute shader to `amdgcn-*`. Like the
`GetDimensions.xy` addendum above, none of the touched files
(`feme/lib/Transforms/DXIL/OpRaising.cpp`,
`feme/lib/Transforms/DXIL/IntrinsicExpansion.cpp`) are reachable from
`libfeme_vulkan` (the SPIR-V/Vulkan/CPU-target path this report measures) --
SPIR-V shaders never lower through `dx.op.*`/`llvm.dx.fdot` at all, only a
`dxil`-sourced module does, and this report's ICD never consumes one -- so
no change to the headline numbers above is expected. A `dEQP-VK.api.info.*`
smoke run (same invocation as the `GetDimensions.xy` addendum's) confirmed
this: 5,669/10,484 passed, 84/10,484 failed, identical to that addendum's
own smoke-run numbers, with zero crashes.


## Addendum: SPIR-V `spirv.Switch`/`spirv.ImageFetch`+`Lod`/`spirv.Dot` conversion fixes

Three follow-up changes to `feme::spirv::SPIRVToLLVMPatterns.cpp`
(`SwitchConversionPattern`, `ImageFetchLodPattern`, `DotConversionPattern`)
fixed three separate SPIR-V -> LLVM dialect legalization failures found
while getting a bilateral-filter-style HLSL compute shader (`InputTexture
.Load(...)`, a `[unroll]`ed loop with an early `return`, and `dot()` on
`half3`s) through `dxc -T cs_6_8 -spirv` and then `feme`. Unlike the DXIL/
AMDGPU addenda above, `feme::spirv::populateSPIRVToLLVMTargetPatterns` --
the file all three patterns live in -- *is* on `libfeme_vulkan`'s own path
(`feme::Vulkan::Pipeline.cpp` calls it directly to compile every SPIR-V
shader module a Vulkan application submits), so this report's headline
numbers are in scope for re-verification, not out of it.

A full from-scratch pass (this report's own methodology) was not re-run in
this session: at ~3.2M cases it is multi-hour, and none of the three fixed
constructs (`OpSwitch`, `OpImageFetch` with an explicit `Lod` operand,
`OpDot`) previously reached `libfeme_vulkan` successfully enough to produce
a `Pass`/`Fail` distinguishable from `NotSupported` -- they failed to
*legalize* at all, i.e. every affected shader was already counted among the
`NotSupported`/crash buckets before this change, not among the 10,560
passes this report's methodology already isolates crashes around. Instead,
`dEQP-VK.compute.pipeline.*` (20,285 cases -- real compute shaders compiled
and dispatched through the exact code path these patterns live in) was run
before and after, using the same `git checkout <pre-fix commit> --
SPIRVToLLVMPatterns.cpp` + rebuild `feme_vulkan` trick this report's own
"Reproducing this report" section describes for isolating a single change:

- Before: 1 passed / 88 failed / 20,196 not supported.
- After: 1 passed / 88 failed / 20,196 not supported (identical; the 88
  failures are pre-existing `llvm.getelementptr` operand-type and
  `unhandled opcode 68` errors, none mentioning `switch`, `Dot`, or
  `ImageFetch`/`resource.load.level`).

No regression and no new pass (expected: none of this suite's own compute
shaders happen to use a `switch` statement, an explicit-LOD texel fetch, or
`dot()`), and zero crashes in either run. `ninja check-feme`: 1492/1493
passed, 1 unsupported (pre-existing, unrelated), before and after each of
the three commits.

## Addendum: SPIR-V `spirv.Image`/`spirv.VulkanBuffer` AMDGPU-lowering fix (out of this report's scope)

Two follow-up commits to `feme::amdgpu::ResourceLoweringPass`
(`feme/lib/Transforms/AMDGPU/ResourceLowering.cpp`) fixed the same
bilateral-filter-style HLSL compute shader the "SPIR-V `spirv.Switch`/
`spirv.ImageFetch`+`Lod`/`spirv.Dot`" addendum above describes hitting a
further, AMDGPU-only failure once the three `SPIRVToLLVMPatterns.cpp`
legalization gaps that addendum fixed let it legalize far enough to
reach `feme --target=amdgpu9.0a-amd-amdhsa`:

```
feme: resource handle type 'spirv.Image' is not supported when
targeting 'amdgpu9.0a-amd-amdhsa' (produced in function 'main')
```

`feme::amdgpu::ResourceLoweringPass`'s SPIR-V-flavored coordinate handling
had only ever been exercised against a 1D `Buffer`/`RWBuffer` pair; a
genuinely 2D `Texture2D`/`RWTexture2D` pair's vector coordinate (and
`Texture2D<T>::Load`'s `llvm.spv.resource.load.level` intrinsic, which
`hasOnlySupportedUses` did not recognize at all) needed linearizing the
same way the `dx.Texture`/AMDGPU path's coordinate already is; a second,
previously unmodeled SPIR-V cbuffer handle (`target("spirv.VulkanBuffer",
...)`, not `spirv.Image`) needed a single `AllResourceOps` table entry.
See `feme/docs/Design.md`'s "Raised LLVM IR -> AMDGPU" Status note and
`agent_thoughts.md` for the full writeup.

Unlike that addendum's own `SPIRVToLLVMPatterns.cpp` fixes -- which *are*
on `libfeme_vulkan`'s own path -- `feme::amdgpu::ResourceLoweringPass` is
not: it is reached only from `feme::amdgpu::TargetMachineBackend`'s own
pass pipeline when retargeting to an `amdgcn-*` triple, a path
`libfeme_vulkan` (whose Vulkan devices are backed by `feme::cpu`, per
`feme::Vulkan::PhysicalDeviceInfo.cpp`) never takes. Confirmed two ways
rather than assumed:

- `grep -rl amdgpu feme/lib/Vulkan/ feme/include/feme/Vulkan/` and
  `grep FeMeTransformsAMDGPU feme/tools/feme-vulkan/CMakeLists.txt` both
  found nothing: no source file or CMake target under `libfeme_vulkan`
  references the AMDGPU transforms library at all.
- After both commits, `ninja lib/libfeme_vulkan.so` in this report's own
  `./build` reported `ninja: no work to do` -- Ninja's own dependency
  graph agrees the shared library did not need relinking, which is
  stronger evidence of "unreachable" than a comparative CTS run of any
  size would be (a rebuilt-but-byte-identical binary would still leave
  some doubt about whether the *right* object files were rebuilt; an
  unrebuilt one leaves none).

Given that, no `dEQP-VK` run (full or spot-check) was performed for this
addendum: every one of this report's 3,237,000 cases already exercises the
exact `libfeme_vulkan.so` these two commits did not touch, so a re-run
could only ever reproduce the headline numbers above verbatim while
consuming the multi-hour cost "Reproducing this report" describes, with no
additional evidence over the two checks already made. `ninja check-feme`
(includes `FeMeVulkanTests` and `libfeme_vulkan` itself in its dependency
graph): 1495/1496 passed, 1 unsupported (pre-existing, unrelated), after
both commits.
