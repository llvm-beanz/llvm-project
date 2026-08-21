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
  that *is* new in this edition. Roadmap E1/E2/E3/E4 (aggregate 1.3/1.4
  feature/property struct wiring, `synchronization2`, and `maintenance4`)
  are likewise measured only over the targeted case sets each of their own
  "measured impact" sections names, not a full re-run -- see those
  sections. Roadmap E4's own session additionally re-ran the full 54-group
  sweep once (see "Roadmap E4: measured impact"'s own closing note); that
  full-sweep total is cumulative across every session since D0's own
  headline run below, not attributable to E4 alone.
- `check-feme`: 1541 passed, 1 unsupported as of roadmap E4 (see "Roadmap
  E4: measured impact" below); the headline table above predates
  E1/E2/E3/E4 and is not affected by any of the four (no crash, timeout,
  or full 54-group case-count change).
- VK-GL-CTS revision: `vulkan-cts-1.4.6.2-412-g716301541136` plus two local
  fixes (`7163015`, "Guard `dEQP-VK.api.invariance.random` against empty
  image format lists"; and a second one added by roadmap C7's own pass,
  "Check `VK_KHR_copy_commands2` support in
  `image_to_image_transfer_queue.misc.ms_then_ss*`" -- see "Deviations from
  a stock CTS" below).
- Host: AArch64 Linux, `LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
  `RelWithDebInfo`.

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

## Roadmap D3: measured impact

Roadmap D3 is per-bucket attribution of the net +2,553 newly-failing cases
"Roadmap D0: measured impact" above found but did not individually trace,
beyond `ubo.*.std430` -- at the same rigor C1-C8 applied to their own
fixes, and D0 itself applied to that one bucket (diffing real per-case
result sets, not aggregate counts).

**Reproducing D0's own comparison first, honestly.** Rather than trust
the earlier headline numbers, this pass rebuilt both sides of D0's own
diff from the exact commits: the pre-D0 apiVersion-1.2 commit
(`45cc60d99cc8`) and the post-D0, pre-D1 apiVersion-1.4-plus-copy_commands2
commit (`c0ed4968a920`), each in its own worktree and build tree (ccache
shared with the main tree, `LLVM_ENABLE_ASSERTIONS=ON`), and ran the
identical documented 54-group recipe against each. Both `check-feme` runs
passed clean (1478/1518 and 1479/1519 respectively, no failures, matching
each commit's own expected `Unsupported` count). One correction to the
recipe itself was necessary: `api` crashed partway through the concurrent
54-group run for *both* commits this time (D2's already-tracked loader
race, triggered here by resource contention from six concurrent `deqp-vk`
processes rather than by anything apiVersion-specific -- it reproduced at
1.2 as readily as at 1.4 once run under the same load), so `api` was
re-run alone, uncontended, for both commits before diffing; both isolated
re-runs completed all 267,222 of `api`'s own cases cleanly.

**This does not reproduce a net +2,553.** Diffing the two runs' actual
per-case `Fail` sets across all 54 groups (not aggregate counts) gives
525 newly-`Fail` and 942 no-longer-`Fail` -- a net **-417**, the opposite
sign from what "Roadmap D0: measured impact" recorded. Two things account
for most of the gap, and neither is a measurement mistake in this pass:
first, this tree's checked-out VK-GL-CTS has drifted one local commit
past what D0's own report cites (`vulkan-cts-1.4.6.2-412-g716301541136`
there, `-413-ge4b225a7d7cd` here -- the local `ms_then_ss` copy_commands2
fix roadmap C7 added), and this report's own methodology
("Reproducing this report" above) has never pinned an exact CTS commit,
only a checked-in tree; second, D0's own headline table explicitly
recorded `api` as "Crashed / timed out ... 228 cases short" for its
post-D0 run and never re-measured it complete, so its 1,999-case
no-longer-failing figure could not have included any of the 120
`api`-group cases this pass's *complete* diff finds (below) -- an
undercount baked into the original number, not a new discrepancy this
pass introduced. Per this file's own stated purpose ("regenerated from
scratch on every full pass ... describes the *current* state, not the
history of how it got there"), the numbers below supersede D0's for
per-bucket attribution purposes; D0's own headline table above is left
unchanged since it documents what that pass's revisions actually measured
at the time.

**Newly-`Fail` (525), by group:**

| Group | Cases | Root cause |
|---|---|---|
| `spirv_assembly.instruction.compute` | 422 | Genuine regression, matches D0's own 417 closely. `SPIRVToLLVMPatterns.cpp`'s `ImageFetchPattern`/`ImageFetchLodPattern` (and the analogous `spirv.ImageSampleExplicitLod` patterns) each require an *exact* `image_operands` match (no operands, or exactly `Lod`) and reject everything else as illegal; SPIR-V 1.6 (which deqp-vk only emits once `apiVersion >= 1.3`, matching every one of these cases' pre-D0 `NotSupported ("Vulkan higher than or equal to 1.3 is required")`) adds a `Nontemporal` cache hint bit that combines with `Lod` or stands alone, and no pattern in this file tolerates it -- a pure cache hint with no defined effect on the result, currently unhandled anywhere in this ICD, not a semantic gap the way `ubo`'s is. |
| `graphicsfuzz` | 72 | `VK_KHR_shader_terminate_invocation`, promoted to core at `VK_VERSION_1_3` per `Vulkan14FeatureInventory.md`'s row for it ("no" -- not yet implemented). deqp-vk's own extension-support check treats any extension promoted to core at or below the claimed `apiVersion` as present without querying this ICD's advertised extension list -- the identical mechanism D0's own `copy_commands2` finding already established, now hitting a different extension. These Amber tests exercise `OpTerminateInvocation`'s distinct-from-`discard` semantics, which this ICD does not actually implement differently, so they now run (instead of correctly reporting `NotSupported`) and produce a wrong image (`Fail (Fail)`, an image-comparison mismatch, not a pipeline-creation error). |
| `api.info.*` | 18 | `dEQP-VK.api.info.vulkan1p3.{features,properties,feature_extensions_consistency}` and ten `get_physical_device_properties2.features.*_features` cases (`image_robustness`, `inline_uniform_block`, `maintenance4`, `pipeline_creation_cache_control`, `private_data`, `shader_demote_to_helper_invocation`, `shader_integer_dot_product`, `shader_terminate_invocation`, `subgroup_size_control`, `synchronization2`, `texture_compression_astc_hdr`, `vulkan13`, plus `vulkan1p3_limits_validation.max_inline_uniform_total_size`) -- exactly the "device_mandatory_features/vulkan1p3_consistency" shape D0's first draft guessed and discarded after checking the top-level `info` group alone (which only gained two new failures, as D0 recorded). It materializes instead in `api.info.*`, a separate subtree deqp-vk also uses for the same class of check; D0's report did not check that subtree. Root cause is D1's already-tracked finding that most of 1.3/1.4's mandatory feature bits are unimplemented, now caught by consistency checks that only run once `apiVersion >= 1.3` makes deqp-vk chain the aggregate `VkPhysicalDeviceVulkan13Features` blob alongside each feature's individual extension struct and compare them. |
| `compute.pipeline.zero_initialize_workgroup_memory` | 7 | Same "promoted extension assumed implemented" shape as `graphicsfuzz`: `VK_KHR_zero_initialize_workgroup_memory` is promoted to `VK_VERSION_1_3` and, per `Vulkan14FeatureInventory.md`, not yet implemented (`shaderZeroInitializeWorkgroupMemory` feature bit: "no"). Pre-D0 these correctly reported `NotSupported`; post-D0 they run and fail. |
| `robustness.oob_access` | 6 | `rba_texel_buffer_uniform_*` cases: pre-D0 these reported `NotSupported ("Format not supported for uniform texel buffers")`; post-D0 the same format now passes that check (a mandatory-format-table consequence, not traced further this pass) and reaches `vkCreateBufferView`, which throws `VK_ERROR_FORMAT_NOT_SUPPORTED` -- an internal inconsistency between what this ICD's format-support query reports and what its own `vkCreateBufferView` accepts, not yet root-caused past that point. |

**No-longer-`Fail` (942), by group -- a D1-tracked gap paying off as an
accidental improvement:**

| Group | Cases | Root cause |
|---|---|---|
| `draw.dynamic_rendering.*` | 613 | Pre-D0: `Fail (vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED)` or the equivalent at `vkQueueSubmit`/`vkCmdUtil.cpp` -- a crash-adjacent failure, not a clean rejection. Post-D0: `NotSupported ("VK_KHR_dynamic_rendering is not supported")`. Root cause: deqp-vk's own support check for this extension consults `VkPhysicalDeviceVulkan13Features.dynamicRendering` once `apiVersion >= 1.3` (rather than the pre-promotion `VkPhysicalDeviceDynamicRenderingFeatures` struct these tests' pre-D0 path used); `vkGetPhysicalDeviceFeatures2` has **no case at all** for `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES` (confirmed by inspection -- absent from `EntryPoints.cpp`'s switch), so that blob is left zero-initialized and reads back `dynamicRendering = false` even though it is truthfully advertised through the older struct. This is exactly D1's own finding ("only `dynamicRendering` is genuinely implemented, and only through its pre-promotion ... struct, not yet the aggregate one") now observed converting a bad-shaped `Fail` into a correctly-truthful `NotSupported`, purely by accident of which struct a version-gated check happens to consult. |
| `renderpasses.dynamic_rendering.*` | 204 | Identical mechanism and identical `NotSupported` message to `draw` above. |
| `pipeline.monolithic.*.dynamic_rendering_postpass` | 5 | Identical mechanism. |
| `api.object_management.alloc_callback_fail.*` | 120 | Pre-D0: `Fail (createDeviceInternal(...): VK_ERROR_EXTENSION_NOT_PRESENT)`. Post-D0: `Pass`. These tests request a specific device extension unconditionally; at 1.4 more of the requested extensions are core (not separately enumerable, and no longer rejected as "not present"), so device creation now succeeds -- a genuine, uncomplicated improvement from the version bump, not a gap. |

**Correcting D0's own already-traced bucket.** D0's report treated
`ubo.*.std430` (2,650 cases) as its one rigorously-traced bucket, gated by
`apiVersion` reaching `UniformBlockCase::checkSupport`'s
`getUniformBufferStandardLayoutFeatures()` query for the first time at
1.3+. Re-checking it at the same rigor applied above: the `ubo` group's
full log is **byte-identical** between the pre-D0 and post-D0 builds (same
5,687 `Fail`, 7,553 `NotSupported`, 0 `Pass`, confirmed with a plain
`diff`) -- neither newly-`Fail` nor no-longer-`Fail` contains a single
`ubo.*` case. Reading `vkDeviceFeatures.cpp`'s own gate
(`vk12Supported = apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0)`) and
`EntryPoints.cpp`'s `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES`
case (which has set `uniformBufferStandardLayout = VK_TRUE` since roadmap
C6, well before D0) together confirm why: that gate is already satisfied
at exactly `apiVersion == 1.2.0`, so `checkSupport` already passed and
already reached `feme-cpu-simdize`'s divergent-vector diagnostic before
D0's version bump. This bucket's cases were already failing pre-D0; they
were never newly created *or* newly reached by roadmap D0, and should not
have been counted in its net delta. The underlying compiler gap itself is
real and remains open (still C3's own "milestone 7 deviation," still
unfixed) -- only its attribution to D0 is corrected here.

**`synchronization.op.{multi,single}_queue` does not reproduce either.**
Both groups were run standalone against both commits (avoiding the `api`
crash entirely) and re-run a second time against the post-D0 build to
check for nondeterminism: all four runs produced the exact same 222/276
`Fail` counts, and a full case-name diff between the pre-D0 and post-D0
`Fail` sets is empty in both directions. Every failure inspected traces to
`spirv.AtomicIAdd` legalization failing on a Uniform-storage-class pointer
-- a pre-existing shader-compilation gap with no version dependency at
all. D0's 277-case figure for this bucket does not hold up under a direct
per-case diff; it was likely an artifact of attributing an aggregate
group-level count difference (between two runs that were not otherwise
diffed at the case level, unlike `ubo`) rather than a verified newly-`Fail`
set.

**Scope discipline.** This milestone is attribution, matching D1's own
"audit, not an implementation pass" framing and D2's "characterize... not
attempt a local workaround" precedent: no `feme/` source changes land in
this milestone. The `Nontemporal` image-operand gap, the unimplemented
`VK_KHR_shader_terminate_invocation`/`VK_KHR_zero_initialize_workgroup_
memory` functionality, the missing `VkPhysicalDeviceVulkan13Features`
case in `vkGetPhysicalDeviceFeatures2`, and the texel-buffer-format/
`vkCreateBufferView` inconsistency are each real, now-documented gaps a
future roadmap row can close with its own measured before/after, the same
way this section's own methodology demands.

## Roadmap E1: measured impact

Roadmap E1 ("Wire the aggregate `VkPhysicalDeviceVulkan13Features`/
`Vulkan14Features` `vkGetPhysicalDeviceFeatures2` cases") closes exactly
the gap "Roadmap D3: measured impact" documented above ("the missing
`VkPhysicalDeviceVulkan13Features` case in `vkGetPhysicalDeviceFeatures2`"):
`EntryPoints.cpp` now has cases for `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_
VULKAN_1_3_FEATURES`/`_1_4_FEATURES`, so `dynamicRendering` reads back
`VK_TRUE` from the aggregate struct, matching the pre-promotion
`VkPhysicalDeviceDynamicRenderingFeatures` struct instead of silently
disagreeing with it.

**Targeted before/after run**, the same "diff real per-case results, not
aggregate counts" rigor D3 established, over the four groups D3's own
findings named: `api.info`, `draw.dynamic_rendering`,
`renderpasses.dynamic_rendering`, and
`pipeline.monolithic.*.dynamic_rendering_postpass` (this checked-out CTS
revision has drifted since D3's own pass -- see "Roadmap D3: measured
impact"'s own note on this -- so only 1 case matches the last glob here,
not D3's cited 5). Built pre-E1 (`git stash` on `EntryPoints.cpp` alone)
and post-E1, each run against the identical 4-group case list, in
isolation (not the contended 54-group recipe) to avoid D2's loader race:

| Group | Total | Transition | Count |
|---|---:|---|---:|
| `api.info` | 10,484 | `Fail` &rarr; `Pass` | 3 |
| `draw.dynamic_rendering` | 16,973 | `NotSupported` &rarr; `Fail` | 613 |
| `renderpasses.dynamic_rendering` | 28,602 | `NotSupported` &rarr; `Fail` | 204 |
| `renderpasses.dynamic_rendering` | 28,602 | `NotSupported` &rarr; `Pass` | 1 |
| `pipeline.monolithic.*.dynamic_rendering_postpass` | 1 | `NotSupported` &rarr; `Fail` | 1 |

**The 3 newly-`Pass` cases are exactly this milestone's own target**:
`get_physical_device_properties2.features.vulkan13_features`,
`vulkan1p3.feature_extensions_consistency`, and `vulkan1p3.features` --
the consistency checks that chain the aggregate `VkPhysicalDeviceVulkan13
Features` blob alongside `VkPhysicalDeviceDynamicRenderingFeatures` and
compare `dynamicRendering` between them. (D3's report guessed 18 such
cases from an older CTS revision's group contents; this revision's
`api.info` subtree only contains these 3 for `dynamicRendering`
specifically -- the other properties/limits-consistency cases D3 named
are gated on feature bits E1 does not touch, e.g. `image_robustness`,
`inline_uniform_block`, and remain `Fail` unchanged.)

**The 818 newly-`NotSupported`&rarr;`Fail` cases are not a regression this
milestone introduced.** Truthfully advertising `dynamicRendering` makes
`deqp-vk` actually attempt these cases instead of correctly-but-
accidentally declining them (per D3's own framing, "converting a bad-
shaped `Fail` into a correctly-truthful `NotSupported`, purely by accident
of which struct a version-gated check happens to consult" -- E1 removes
the accident, so the cases now run against a real implementation gap
instead). Re-running one failing `draw.dynamic_rendering` case with
`FEME_VULKAN_LOG_CREATION_ERRORS=1` shows the actual cause:
`vkCreateGraphicsPipelines: rasterizer discard, depth clamp, depth bias,
and non-fill polygon modes are not implemented` (508 of 613) and a
`feme-cpu-simdize` divergent-vector diagnostic (105 of 613) --
`GraphicsPipeline.cpp`'s existing rasterizer-state and shader-compilation
gaps, both present and equally reachable through the ordinary
`VkRenderPass` path today: `dEQP-VK.draw.renderpass.basic_draw.draw.
line_strip.1` (no dynamic rendering involved at all) fails with the
identical `VK_ERROR_INITIALIZATION_FAILED`/reason, confirming these gaps
predate E1 and are simply reached through a second code path now that
`dynamicRendering` is truthfully advertised. `renderpasses.dynamic_
rendering`'s 204 additionally break down as 100 of the same rasterizer-
state gap, 98 `vkCreateImage: VK_ERROR_FORMAT_NOT_SUPPORTED` (an
unadvertised mandatory format, not traced further here), and 4
`vkQueueSubmit` failures -- each a pre-existing, independent gap outside
this milestone's "wire the struct plumbing" scope (see
[FeMeVulkanDesign.md](FeMeVulkanDesign.md)'s own rasterizer-state status
note for where the largest of these belongs). None of these 818 cases
crashed, timed out, or produced a
`Pass`-shaped result with wrong image data -- each is a clean, correctly-
reasoned `Fail` now that the capability check itself is honest.

## Roadmap E2: measured impact

Roadmap E2 ("Wire the aggregate `VkPhysicalDeviceVulkan13Properties`/
`Vulkan14Properties` `vkGetPhysicalDeviceProperties2` cases") closes the
other half of the gap D1's inventory found alongside E1's own target: all
70 mandatory 1.3/1.4 limit fields were unenumerated by either promoted
`...Properties` struct.

**Targeted before/after run over the whole `dEQP-VK.api.info.*` group**
(10,484 cases -- broader than E1's own four-group pass, since this
milestone's own target, `vulkan1p3.properties`/`vulkan1p4.properties`,
lives specifically in `api.info` and nowhere else), diffing real per-case
results the same way D3/E1 did:

| Case | Transition |
|---|---|
| `dEQP-VK.api.info.vulkan1p3.properties` | `Fail` &rarr; `Pass` |

Exactly **one** case transitions. `vulkan1p4.properties` (the 1.4
counterpart of this milestone's own target) stays `NotSupported` in both
runs -- a pre-existing, E2-independent gap: `dEQP-VK.api.info.vulkan1p4.*`
throws `NotSupportedError("At least Vulkan 1.4 required to run test")`
because `Context::contextSupports(1, 4, 0)` returns false in this
environment even though this ICD's own `vkGetPhysicalDeviceProperties`
truthfully reports `apiVersion == VK_API_VERSION_1_4` (confirmed
byte-identical before and after this change, and unrelated to any field
this row writes -- `deqp-vk`'s own `usedApiVersion` negotiation, not this
ICD's advertised version, is the limiting factor here; out of this row's
scope to chase further).

**This row's first draft was not this conservative, and a CTS run is why
it changed.** An earlier version of this change reported real,
already-computable values for a handful of fields this ICD tracks for
other reasons already (`minSubgroupSize`/`maxSubgroupSize` from the
pinned wave size, `maxComputeWorkgroupSubgroups` from the existing
compute-invocation limit, `storageTexelBufferOffsetAlignmentBytes`/
`uniformTexelBufferOffsetAlignmentBytes` from the existing texel-buffer
offset alignment, `maxBufferSize` from the host-memory-backed allocation
ceiling, `lineSubPixelPrecisionBits` from the rasterizer precision,
`maxVertexAttribDivisor`/`maxCombinedImageSamplerDescriptorCount` as a
real `1`, `defaultRobustnessStorageBuffers`/`UniformBuffers`/
`VertexInputs` as `ROBUST_BUFFER_ACCESS`, `identicalMemoryTypeRequirements`
as `VK_TRUE`). Running the same before/after recipe against that draft
found a **second** transition alongside the intended one:
`dEQP-VK.api.info.vulkan1p3.property_extensions_consistency` went
`Pass` &rarr; `Fail` (`"Mismatch between
VkPhysicalDeviceSubgroupSizeControlProperties and
VkPhysicalDeviceVulkan13Properties"`). Reading that test's own source
(`vktApiFeatureInfo.cpp`) explains why: it cross-checks *every* aggregate
1.3/1.4 property field against its own pre-promotion, per-extension
dedicated struct (`VkPhysicalDeviceSubgroupSizeControlProperties`,
`VkPhysicalDeviceInlineUniformBlockProperties`,
`VkPhysicalDeviceShaderIntegerDotProductProperties`,
`VkPhysicalDeviceTexelBufferAlignmentProperties`,
`VkPhysicalDeviceMaintenance4Properties` for 1.3;
`VkPhysicalDeviceLineRasterizationPropertiesKHR`,
`VkPhysicalDeviceMaintenance5PropertiesKHR`,
`VkPhysicalDeviceMaintenance6PropertiesKHR`,
`VkPhysicalDevicePushDescriptorPropertiesKHR`,
`VkPhysicalDeviceVertexAttributeDivisorPropertiesKHR`,
`VkPhysicalDeviceHostImageCopyPropertiesEXT`,
`VkPhysicalDevicePipelineRobustnessPropertiesEXT` for 1.4) -- unconditionally,
once apiVersion >= 1.3/1.4, exactly the same "assumed real once the
version is claimed" pattern D3/E1 already found for `dynamicRendering`.
None of those dedicated structs has its own `vkGetPhysicalDeviceProperties2`
case in this ICD yet, so every one of them still reads back as an
all-zero `initVulkanStructure()` default. A real, nonzero value in the
aggregate struct therefore *disagrees* with that zero and fails the
consistency check -- the draft would have converted one `Fail` into a
`Pass` while introducing a different one, a net wash rather than the
intended improvement. Landing every field at the conservative `0`/
`VK_FALSE`/`nullptr` this report's own table above shows avoids the
regression entirely while still closing `vulkan1p3.properties` (which
only requires every field to be *written*, not nonzero -- it fills the
struct with a guard byte pattern first and fails only if any field still
holds that pattern afterward). Each field's own later roadmap row is
responsible for raising it together with adding that row's own
dedicated-struct case, so the two remain honestly in sync -- see
Roadmap.md's E2 row and `feme/lib/Vulkan/EntryPoints.cpp`'s case comment
for the full per-row mapping.

## Roadmap E3: measured impact

Roadmap E3 ("`VK_KHR_synchronization2`/`synchronization2`") implements
`vkCmdPipelineBarrier2`/`vkCmdWriteTimestamp2`/`vkQueueSubmit2`/
`vkCmdSetEvent2`/`vkCmdResetEvent2`/`vkCmdWaitEvents2`, translating
`VkDependencyInfo`'s per-resource `VkMemoryBarrier2`/`VkBufferMemoryBarrier2`/
`VkImageMemoryBarrier2` (2-stage-mask, 2-access-mask shape) down to the
existing 1-mask `Sync.{h,cpp}`/`CommandBuffer.cpp` model -- the same "new
entrypoint, old backing model" pattern roadmap C7 used for queue families.
`synchronization2` now reads `VK_TRUE` from both the aggregate
`VkPhysicalDeviceVulkan13Features` struct (E1's own case) and its own
dedicated `VkPhysicalDeviceSynchronization2Features` struct, and
`VK_KHR_synchronization2` is now listed by `getSupportedDeviceExtensions`
(unlike `VK_KHR_copy_commands2` in roadmap D0, whose core, non-`KHR`-suffixed
names alone sufficed: `dEQP-VK.synchronization2`'s own multi-queue/custom-
device cases explicitly enable this extension by name at `vkCreateDevice`
regardless of the advertised `apiVersion` -- see below).

**Targeted before/after run**, built pre-E3 (`git stash` on every touched
`feme/lib/Vulkan/*.cpp`/`*.h` file, keeping tests unstashed) and post-E3,
each run in isolation against the `synchronization2` group and the
`api.info.vulkan1p3`/`get_physical_device_properties2.features.
synchronization2_features` cases D1's own inventory named:

| Case(s) | Before | After |
|---|---|---|
| `dEQP-VK.synchronization2.*` (81,617 total) | 2 `Pass`, 0 `Fail`, 81,615 `NotSupported` | 310 `Pass`, 872 `Fail`, 80,435 `NotSupported` |
| `api.info.get_physical_device_properties2.features.synchronization2_features` | `Fail` (struct mismatch) | `Pass` |
| `api.info.vulkan1p3.*` (5 cases) | 5 `Pass` (already consistent: both structs agreed on `VK_FALSE`) | 5 `Pass` (now agree on `VK_TRUE`) |

**The dedicated-struct `Fail`&rarr;`Pass` is exactly this milestone's own
target**, the same consistency check E1/E2 already established for
`dynamicRendering`: `vulkan1p3.feature_extensions_consistency` was already
passing before E3 (both the aggregate and -- absent, so implicitly
zero-initialized by the test -- the dedicated struct agreed on
`VK_FALSE`), so it is unaffected; the dedicated-struct case above is the one
that actually exercises `EntryPoints.cpp`'s new
`VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES` case.

**Advertising the extension name was not optional.** A first pass left
`getSupportedDeviceExtensions` untouched, matching roadmap D0's
`VK_KHR_copy_commands2` precedent (core names alone, no extension-name
advertisement, since `apiVersion` already satisfies the promotion). That
left 305 `dEQP-VK.synchronization2.*` cases failing
`vkCreateDevice(...)`: `VK_ERROR_EXTENSION_NOT_PRESENT` --
`vktCustomInstancesDevices.cpp`'s multi-queue/custom-device tests
explicitly enable `VK_KHR_synchronization2` by name regardless of
`apiVersion`, unlike the ordinary default-device path most `copy_commands2`
cases use. Adding the extension to `getSupportedDeviceExtensions` (this
report's headline numbers above already include it) dropped that to 0 and
raised the group's `Pass` count from 251 to 310.

**The 872 remaining `Fail` cases are not a regression this milestone
introduced.** 860 of them (747 `vkCreateComputePipelines`/113
`vkCreateGraphicsPipelines`, both `VK_ERROR_INITIALIZATION_FAILED`) are
pre-existing shader/pipeline gaps (e.g. SSBO writes in stages this ICD does
not yet support in that combination) reached through a second code path
now that `synchronization2` is truthfully advertised, exactly the same
"converts `NotSupported` into a real `Fail` against an existing gap"
pattern E1 documented for `dynamicRendering`. The remaining 12
(`timeline_semaphore.device_host.*`/`timeline_semaphore.wait.*`, all
`synchronizationWrapper->queueSubmit(...): VK_ERROR_INITIALIZATION_FAILED`)
are a pre-existing limitation of this ICD's synchronous `vkQueueSubmit`
execution model (Sync.h's file comment: a host signal racing a device wait
is not something a synchronous ICD can resolve), not specific to
`vkQueueSubmit2`'s own translation -- the identical, non-`2`
`dEQP-VK.synchronization.timeline_semaphore.device_host.write_copy_buffer_
read_copy_buffer.buffer_16384` case fails with the exact same error at the
exact same call site. None of these 872 crashed, timed out, or produced a
`Pass`-shaped result with wrong data.

## Roadmap E4: measured impact

Roadmap E4 (`VK_KHR_maintenance4`/`maintenance4`) adds
`vkGetDeviceBufferMemoryRequirements`/`vkGetDeviceImageMemoryRequirements`/
`vkGetDeviceImageSparseMemoryRequirements` (Buffer.cpp/Image.cpp share their
sizing/validation with the live `vkGetBufferMemoryRequirements(2)`/
`vkGetImageMemoryRequirements(2)` entrypoints), flips `maintenance4` to
`VK_TRUE` in the aggregate `VkPhysicalDeviceVulkan13Features` struct and
adds its own dedicated `VkPhysicalDeviceMaintenance4Features`/`Properties`
cases (`maxBufferSize` now reads the same real host-memory-size value
`VkPhysicalDeviceMaintenance3Properties::maxMemoryAllocationSize` already
did), and fixes a `SPIRVToLLVMPatterns.cpp` legalization gap that
previously rejected any `LocalSizeId` compute shader outright (see
"The LocalSizeId compilation gap" below). Auditing the row's own claimed
"relaxes ... the zero-size-descriptor-array rule" found no code to
relax -- `vkCreateDescriptorSetLayout`/`vkAllocateDescriptorSets` already
accept a `descriptorCount == 0` binding with no special-casing needed --
so that part of this row is closed by a regression test alone
(`DescriptorTest.AcceptsZeroSizeReservedBinding`), the same
"row's own premise gets corrected" outcome E2/E3 each recorded.

**Targeted before/after run**, built pre-E4 (`git checkout` every touched
`feme/lib/Vulkan/{Buffer,Image,Memory,EntryPoints}.{cpp,h}`/
`feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` file back to
`55d8fb02b4b3`, keeping tests at HEAD) and post-E4, each run in isolation:

| Case(s) | Before | After |
|---|---|---|
| `dEQP-VK.api.buffer_memory_requirements.*` (240 total) | 12 `Pass`, 228 `NotSupported` | 24 `Pass`, 216 `NotSupported` (the `method2`, i.e. `vkGetDeviceBufferMemoryRequirements`, variant of every case `method1` already covered) |
| `dEQP-VK.api.invariance.memory_requirements_matching` | `Pass` (no `VkMemoryDedicatedRequirements` chained) | `Pass` |
| `dEQP-VK.api.invariance.memory_dedicated_requirements_matching` | `NotSupported` (`VK_KHR_maintenance4` not advertised) | `Fail` on a first pass (see "The dedicated-requirements pNext gap" below), `Pass` once fixed |
| `api.info.get_physical_device_properties2.features.maintenance4_features` | `Fail` (struct mismatch) | `Pass` |
| `api.info.vulkan1p3_limits_validation.khr_maintenance4` | `NotSupported` | `Pass` |
| `api.device_init.create_device_unsupported_features.maintenance4_features` | `Pass` (device creation already correctly rejects an unsupported feature bit request) | `Pass` |
| `dEQP-VK.binding_model.*` (150,259 total; zero-size-descriptor-array coverage lives here, not under a `maintenance4`-named case) | 1 `Pass`, 10,318 `Fail`, 139,940 `NotSupported` | identical (1/10,318/139,940) -- confirms the zero-size-descriptor-array audit's finding that nothing needed to change |

**The dedicated-requirements pNext gap.** A first pass landed the three
new entrypoints and the feature-bit wiring without touching
`vkGet{Buffer,Image}MemoryRequirements2`'s own `pNext`-chain handling
(neither had ever walked one at all). `dEQP-VK.api.invariance.
memory_dedicated_requirements_matching` (the same
`vktApiMemoryRequirementInvarianceTests.cpp` file already named in
"Deviations from a stock CTS" below) chains a `VkMemoryDedicatedRequirements`
onto both the live and info-only calls and requires them to agree; since
neither touched it, each retained whatever sentinel value the test itself
pre-filled, and the two disagreed by construction, not because either
computed a different answer. `Memory.h`/`.cpp`'s new
`fillMemoryRequirements2PNextChain` (shared by all four
`vkGet*MemoryRequirements(2)`/`vkGetDevice*MemoryRequirements`
entrypoints, reporting `VK_FALSE` for both
`prefersDedicatedAllocation`/`requiresDedicatedAllocation` -- this ICD
never requires or prefers a dedicated allocation) fixed it, confirmed by
the retest in the table above.

**The `LocalSizeId` compilation gap.** `GroupSize.cpp`'s
`resolveComputeGroupSize` already resolved a `LocalSizeId` entry point's
group size correctly and was already unit-tested doing so
(`GroupSizeTest.ResolvesFromLocalSizeIdDefaults`), but no test exercised
`LocalSizeId` through the *whole* `vkCreateShaderModule`/
`vkCreateComputePipelines` pipeline until this row's own
`PipelineTest.CompilesLocalSizeIdComputeShader` did, and it failed
legalization: neither `spirv.SpecConstant` (`LocalSizeId`'s only way to
spell its three operands) nor `spirv.ExecutionModeId` itself has a
conversion pattern in upstream MLIR's SPIRVToLLVM conversion, unlike plain
`spirv.ExecutionMode`, which FeMe's own `ExecutionModePattern` already
erases (its contents are read from the raw SPIR-V word stream before this
pass runs, same as `LocalSizeId`'s). `SPIRVToLLVMPatterns.cpp`'s new
`ExecutionModeIdPattern`/`SpecConstantErasurePattern` erase both, mirroring
that same precedent; no dedicated CTS case for this exists under a
`local_size_id`-named path in this VK-GL-CTS revision (confirmed empty:
`dEQP-VK.pipeline.spirv_assembly.instruction.compute.local_size_id.*` has
0 cases), so `PipelineTest.CompilesLocalSizeIdComputeShader` is this fix's
only regression coverage.

**Full 54-group re-run** (this session's own, not attributed to E4 alone
-- see the caveat in "Headline" above): 3,237,000 total cases, 11,542
`Pass`, 31,436 `Fail`, 3,194,021 `NotSupported`, no crash, hang, or
truncated group across the whole sweep. `dEQP-VK.api.*`'s own
267,222-case run needed one adjustment: run alone rather than alongside
the other 53 groups, since (independent of E4, reproduced identically
against the pre-E4 baseline) `dEQP-VK.api.object_management.
multithreaded_per_thread_resources.*`'s concurrent pipeline/device
creation intermittently corrupts this ICD's single shared MLIR/JIT state
when run under the six-way concurrent load the documented recipe uses
(`error: 'llvm.getelementptr' op operand #0 must be LLVM pointer
type...`, garbled/interleaved diagnostic text) -- a pre-existing,
unrelated thread-safety gap (this ICD's compilation path is not
documented anywhere as thread-safe against concurrent `vkCreate*Pipelines`
calls across independent `VkDevice`s), not anything E4 touched. Isolated,
`api.*`'s own multithreaded subgroup completes cleanly with the same 2
pre-existing `Fail`s both before and after E4 (see the table above's own
methodology). This thread-safety gap is out of E4's scope and not yet a
tracked roadmap row.

## Roadmap E5: measured impact

Roadmap E5 (`VK_KHR_maintenance5`/`maintenance5`) skips rather than
rejects a dynamic-rendering color attachment whose `VkRenderingAttachment
Info::imageView` is `VK_NULL_HANDLE` (`CommandBuffer.cpp`'s per-attachment
write/clear/resolve loops, and `Executor.cpp`'s per-attachment fragment
write loop -- not `RenderPass.cpp`, a correction to this row's own file
attribution, since `normalizeRenderingAttachment` already produced a null
`View` for this case; every downstream consumer that unconditionally
called `resolveAttachmentView` on it needed the fix, not the normalizer),
adds `VK_FORMAT_A8_UNORM`/`A1B5G5R5_UNORM_PACK16` to `Format.cpp`/
`ImageFixture.cpp`/`RenderPass.cpp`'s format tables, and adds
`vkCmdBindIndexBuffer2` (`CommandBuffer.cpp`, sharing `bindIndexBuffer`'s
recording and `runDraw`/`validateDrawFetchBounds`'s bounds checking, minus
`vkCmdBindIndexBuffer`'s own "whole buffer" assumption). `maintenance5`
now reads `VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan14
Features` struct and its own dedicated `VkPhysicalDeviceMaintenance5
FeaturesKHR` struct.

**Targeted CTS runs**, against this session's HEAD build:

| Case(s) | Result |
|---|---|
| `dEQP-VK.api.info.get_physical_device_properties2.features.maintenance5_features` | `Pass` |
| `dEQP-VK.api.device_init.create_device_unsupported_features.maintenance5_features` | `Pass` |
| `dEQP-VK.api.maintenance5.*` (10 total: `flags`/`format` × `image_format_props(2)`/`sparse_image_format_props(2)`/`device_format_props(2)`) | 10 `Pass` |
| `dEQP-VK.draw.renderpass.indexed_draw.draw_indexed_triangle_list*maintenance_5` (12 total) | 12 `Fail` (`vkCreateGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED`), but the identical, non-`maintenance_5`-suffixed baseline case (`draw_indexed_triangle_list`) fails identically -- confirmed by running it in isolation. This whole `DrawIndexedTest` family is blocked by a pre-existing, orthogonal gap in this pipeline's own creation path that has nothing to do with `bindIndexBuffer2`/`maintenance5` (uninvestigated further; out of this row's own scope), so E5 neither causes nor fixes these 12 |
| `dEQP-VK.robustness.bind_index_buffer2.*` (41 total) | 41 `NotSupported` (`VK_KHR_robustness2`/`VK_EXT_robustness2`, `VK_KHR_draw_indirect_count`, or `VK_EXT_multi_draw` not supported) -- every one of this group's cases needs a second, unrelated, unimplemented extension alongside `vkCmdBindIndexBuffer2` itself; a clean, correct `NotSupported`, not a crash or wrong-shaped result |
| `dEQP-VK.api.buffer_view.access.uniform_texel_buffer.{a8_unorm,a1b5g5r5_unorm_pack16}` | `NotSupported (Format not supported)` -- `vkGetPhysicalDeviceFormatProperties` unconditionally reports zero support for every format regardless of `mapVkFormat`/`isSupportedColorAttachmentFormat` (the same pre-existing, separate stub C1's own report already traced; these two formats are simply new instances of that same gap, not a new one E5 introduces) |

**A second finding this row's own premise did not anticipate**: even
though `maintenance5` is genuinely core-promoted at this ICD's advertised
`apiVersion` (1.4) and the aggregate `VkPhysicalDeviceVulkan14Features`
struct now honestly reports it, `dEQP-VK.draw.*maintenance_5`/`dEQP-VK.api.
maintenance5.*`'s own `context.requireDeviceFunctionality
("VK_KHR_maintenance5")` calls still failed `NotSupported ("VK_KHR_
maintenance5 is not supported")` until `VK_KHR_MAINTENANCE_5_EXTENSION_
NAME` was added to `getSupportedDeviceExtensions` -- the exact same "core-
promoted extension still needs its name listed for `vkCreateDevice` to
accept it explicitly" gap E3's own `synchronization2` row already found
(see "Roadmap E3: measured impact" above), now recurring for a different
extension. Confirmed by the retest: the `dEQP-VK.api.maintenance5.*`/
`device_init`/`get_physical_device_properties2` rows in the table above
all failed `NotSupported ("VK_KHR_maintenance5 is not supported")` before
this fix and pass (or, for the `draw.*` row, reach real pipeline-creation
failure instead of an early `NotSupported`) after it.

## Roadmap E6: measured impact

Roadmap E6 (`VK_KHR_maintenance6`/`maintenance6`) adds `vkCmdBindDescriptorSets2`/
`vkCmdPushConstants2` (`CommandBuffer.cpp`), each a pure argument-shape wrapper
around `vkCmdBindDescriptorSets`/`vkCmdPushConstants`'s own recording --
`VkBindDescriptorSetsInfo`'s `stageFlags` and `VkPushConstantsInfo`'s
`layout`/`stageFlags` need no translation, since this model already stores one
shared set of bound descriptor sets/push-constant bytes across every pipeline
bind point. `vkCmdPushDescriptorSet2` is deliberately left unimplemented,
per this row's own fallback clause, since F12's `pushDescriptor` groundwork
has not landed. `maintenance6` now reads `VK_TRUE` from both the aggregate
`VkPhysicalDeviceVulkan14Features` struct and a new dedicated
`VkPhysicalDeviceMaintenance6Features` struct; `maxCombinedImageSampler
DescriptorCount` is a real `1` (this ICD supports no multi-planar/YCbCr
samplers, so a combined image sampler descriptor always consumes exactly one
slot) in both the aggregate `VkPhysicalDeviceVulkan14Properties` case and a
new dedicated `VkPhysicalDeviceMaintenance6Properties` case.

**Targeted CTS runs**, against this session's HEAD build:

| Case(s) | Result |
|---|---|
| `dEQP-VK.api.info.get_physical_device_properties2.features.maintenance6_features` | `Pass` |
| `dEQP-VK.api.device_init.create_device_unsupported_features.maintenance6_features` | `Pass` |
| `dEQP-VK.api.maintenance6_check.maintenance6_properties` | `Pass` |
| `dEQP-VK.api.info.vulkan1p3.*` (5 total: `feature_bits_influence`/`feature_extensions_consistency`/`features`/`properties`/`property_extensions_consistency`) | 5 `Pass`, confirming `maxCombinedImageSamplerDescriptorCount`'s new nonzero value does not repeat E2's own first-draft regression -- this field is 1.4-only, and `vulkan1p4.*`'s own consistency case never runs in this environment (see below), so nothing cross-checks it against the dedicated struct yet regardless; landing both the aggregate and dedicated structs in the same commit means they can never disagree once that case does run |
| `dEQP-VK.api.info.vulkan1p4.*` (5 total) | 5 `NotSupported ("At least Vulkan 1.4 required to run test")` -- a pre-existing, E2-documented environment gap ("Roadmap E2: measured impact" above): `Context::contextSupports(1, 4, 0)` returns false here even though this ICD's own `vkGetPhysicalDeviceProperties` truthfully reports `apiVersion == VK_API_VERSION_1_4`; `deqp-vk`'s own `usedApiVersion` negotiation, not this ICD, is the limiting factor, and out of this row's scope to chase further |
| `dEQP-VK.api.command_buffers.secondary_push_descriptor_set_2` | `NotSupported ("VK_KHR_push_descriptor is not supported")` -- a clean, correct rejection: `vkCmdPushDescriptorSet2` is unimplemented this row, and the test's own `secCmdExtraCaseSupportCheck` requires `VK_KHR_push_descriptor` (deferred to F12) before attempting it |
| `dEQP-VK.api.command_buffers.secondary_push_constants_2` | `Fail (vk.createComputePipelines(...): VK_ERROR_INITIALIZATION_FAILED)` |
| `dEQP-VK.api.command_buffers.*` (131 total) | 38 `Pass`, 77 `Fail`, 16 `NotSupported` |

**The one `Fail` this row's own scope touches, root-caused before closing
the row.** A temporary debug print at `Pipeline.cpp`'s `compileComputePipeline`
error path (reverted before landing) showed `secondary_push_constants_2`'s
real failure: `"unsupported raised operation: 'llvm.spv.resource.
handlefrombinding...' is a register-bound resource handle the FeMe CPU target
cannot normalize into a heap access or the root-constant block"`. The test's
own compute shader (`vktApiCommandBuffersTests.cpp`) declares its output as
`layout (set=0, binding=0, std430) buffer OutBlock { vec4 value; }` -- a
storage buffer block with a single, non-array `vec4` field, not the
`rtarray`/fixed-array shape every other passing storage-buffer shader in this
report uses. This is not a regression `vkCmdPushConstants2` introduces: the
identical `VK_ERROR_INITIALIZATION_FAILED` at the identical call site accounts
for 73 of this same CTS group's 77 failures, including all 64
`indirect_compute_dispatch_offsets_*` cases (confirmed pre-existing and
unrelated to any command this row adds, by inspection of their own,
E6-independent shader sources) -- a pre-existing, orthogonal gap in this
compiler's resource-handle normalization for a non-array-typed storage
buffer, the same "stacked blockers" pattern C1/C2/E5 already established for
this report, out of this row's own scope to fix.

## Roadmap E7: measured impact

Roadmap E7 (`VK_EXT_subgroup_size_control`/`subgroupSizeControl` +
`computeFullSubgroups`) adds the override path `GroupSize.cpp`'s own file
comment anticipated a future row would need: `Pipeline.cpp`'s
`compileComputePipeline` now reads a
`VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` chained onto a compute
stage and forwards its `requiredSubgroupSize` straight to
`feme::cpu::JITOptions::WaveSize` (which `feme::cpu::resolveWaveSize` already
validates against exactly the same power-of-two-in-`[MinWaveSize,
MaxWaveSize]` range this row reports as `minSubgroupSize`/`maxSubgroupSize`),
and rejects a `VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT`
pipeline whose workgroup's local size in X is not a multiple of the
resolved subgroup size (the only way this CPU target's SIMD-widened
dispatch can honor that flag's "every subgroup is fully populated" promise).
`PipelineCache.cpp`'s `computePipelineCacheKey` now folds in both
`requiredSubgroupSize` and the stage's `VkPipelineShaderStageCreateFlags`,
so two otherwise-identical creations that disagree in either no longer
collide on the same cached artifact. `minSubgroupSize`/`maxSubgroupSize`/
`maxComputeWorkgroupSubgroups`/`requiredSubgroupSizeStages` (E2's
placeholders) are now real (`4`/`128`/`32`/`VK_SHADER_STAGE_COMPUTE_BIT`),
in both the aggregate `VkPhysicalDeviceVulkan13Properties` case and a new
dedicated `VkPhysicalDeviceSubgroupSizeControlProperties` case;
`subgroupSizeControl`/`computeFullSubgroups` read `VK_TRUE` from both the
aggregate `VkPhysicalDeviceVulkan13Features` case and a new dedicated
`VkPhysicalDeviceSubgroupSizeControlFeatures` case.

Unlike E2's own file attribution guess ("`GroupSize.cpp` ... needs an
override path"), the override actually lives in `Pipeline.cpp`:
`GroupSize.cpp`'s `resolveComputeGroupSize` resolves a shader's *workgroup*
size (`LocalSize`/`LocalSizeId`/`BuiltIn WorkgroupSize`) from its SPIR-V
words, an entirely different quantity from a compute dispatch's *subgroup*
(wave) size, which was already resolved elsewhere (`PhysicalDeviceInfo.cpp`'s
device-wide default, `feme::cpu::CompiledStage::create`'s per-pipeline
resolution) before this row touched anything -- the roadmap's own name-pun
premise ("`GroupSize.cpp` ... per its name") conflated the two. No change to
`GroupSize.cpp` itself was needed or made.

**Targeted CTS runs**, against this session's HEAD build:

| Case(s) | Result |
|---|---|
| `dEQP-VK.api.info.get_physical_device_properties2.features.subgroup_size_control_features` | `Pass` |
| `dEQP-VK.api.device_init.create_device_unsupported_features.subgroup_size_control_features` | `Pass` |
| `dEQP-VK.api.info.vulkan1p3.*` (5 total) | 5 `Pass`, confirming the four newly-real `VkPhysicalDeviceVulkan13Properties` fields agree with the new dedicated `VkPhysicalDeviceSubgroupSizeControlProperties` struct rather than repeating E2's own first-draft regression |
| `dEQP-VK.subgroups.size_control.*` (63 total) | 1 `Pass` (`generic.subgroup_size_properties`), 54 `NotSupported`, 8 `Fail` (all pre-existing, see below) |
| `dEQP-VK.subgroups.size_control.{framebuffer,mesh,ray_tracing}.*` and `graphics.required_subgroup_size_{max,min}` (37 total) | `NotSupported ("Shader stage is required to support subgroup operations!")` -- a correct rejection: `requiredSubgroupSizeStages` truthfully advertises `VK_SHADER_STAGE_COMPUTE_BIT` only, since no other stage's pipeline creation consults a required-subgroup-size override yet |
| `dEQP-VK.subgroups.size_control.compute.require_full_subgroups*` (9 total) | `NotSupported ("Device does not support subgroup ballot operations")` -- a pre-existing, correct rejection unrelated to this row: `SubgroupSupportedOperations` is `VK_SUBGROUP_FEATURE_BASIC_BIT` only (no ballot ops), from before this row landed |

**The 8 `Fail` cases, root-caused before closing the row.** Every one
(`compute.allow_varying_subgroup_size*`, `compute.required_subgroup_size_
{max,min}`, and the three `graphics.allow_varying_subgroup_size*` cases,
which reach the identical failure through an internal compute-pipeline
capability probe before ever creating a graphics pipeline) fails identically:
`error: failed to legalize operation 'spirv.SpecConstantComposite' that was
explicitly marked illegal`. These shaders declare their workgroup size via
`local_size_{x,y,z}_id` (spec constants) *and* read the `gl_WorkGroupSize`
builtin from the shader body to report the resolved size back to the host --
a `BuiltIn WorkgroupSize`-decorated `spirv.SpecConstantComposite` genuinely
referenced by the entry point, not merely present as `LocalSizeId`'s
supporting operands (`GroupSize.h`'s own file comment already distinguishes
these: "a specialization constant genuinely read by the shader body ... is a
distinct, still-unimplemented feature"). Roadmap E4's `SpecConstantErasurePattern`
only erases the scalar `spirv.SpecConstant` operands `LocalSizeId` needs
(safe to drop since nothing in the entry point's own body references them);
no conversion pattern exists for `spirv.SpecConstantComposite` at all, erased
or otherwise, and this row's own scope (`GroupSize.cpp`/`Pipeline.cpp`/
`EntryPoints.cpp`) never touches `SPIRVToLLVMPatterns.cpp`. This is not a
regression this row introduces -- before E7, every one of these 8 cases
failed `NotSupported ("VK_EXT_subgroup_size_control is not supported")`
before ever reaching pipeline creation, so the underlying gap was simply
unreached, the same "advertising a real capability exposes a
previously-unreached gap instead of introducing one" pattern D3/E4/E6 above
already established. Lowering a genuinely-read `BuiltIn WorkgroupSize`
specialization-constant composite to a real LLVM constant vector is a
distinct, out-of-scope follow-up for whichever future row needs
`gl_WorkGroupSize` read from a shader body at all.

## Roadmap E8: measured impact

Roadmap E8 (`VK_KHR_shader_integer_dot_product`/`shaderIntegerDotProduct`)
adds six new `spirv`->`llvm` conversion patterns
(`SPIRVToLLVMPatterns.cpp`): `spirv.SDot`/`spirv.UDot`/`spirv.SUDot` and
their `*AccSat` counterparts, none of which upstream MLIR converts at all
(the same "MLIR has no pattern for this op" gap `DotConversionPattern`
already closed for the unrelated float `spirv.Dot`). Each lowers to a
per-lane sign/zero-extend, multiply, and add chain over either a real
vector operand's elements or -- for a scalar 32-bit operand, legal only
with the `PackedVectorFormat4x8Bit` format -- its four unpacked
constituent bytes, with a final `llvm.intr.sadd.sat`/`uadd.sat` for the
`*AccSat` variants. `shaderIntegerDotProduct` now reads `VK_TRUE` from
both the aggregate `VkPhysicalDeviceVulkan13Features` struct and a new
dedicated `VkPhysicalDeviceShaderIntegerDotProductFeatures` struct.
`getSupportedDeviceExtensions` gained `VK_KHR_shader_integer_dot_product`,
the same "CTS enables it by name regardless of `apiVersion`" reason
E3/E5/E6 already established for their own extensions.

None of the 36 `integerDotProduct*Accelerated` limit bits (E2's largest
single placeholder cluster) are raised, in either the aggregate
`VkPhysicalDeviceVulkan13Properties` case or a new dedicated
`VkPhysicalDeviceShaderIntegerDotProductProperties` case: this CPU target
executes every one of the six new patterns as an ordinary scalar
multiply-add sequence, not a genuinely accelerated one, so a uniform
`VK_FALSE` is the truthful answer this row's own premise anticipated,
confirmed rather than merely assumed by the runs below.

**Targeted CTS runs**, against this session's HEAD build:

| Case(s) | Result |
|---|---|
| `dEQP-VK.api.info.get_physical_device_properties2.features.shader_integer_dot_product_features` | `Pass` (previously `Fail`, per D1/D3's own `api.info.*` bucket -- see "Roadmap D3: measured impact" above) |
| `dEQP-VK.api.device_init.create_device_unsupported_features.shader_integer_dot_product_features` | `Pass` |
| `dEQP-VK.api.info.vulkan1p3.*` (5 total) | 5 `Pass`, confirming the new dedicated `VkPhysicalDeviceShaderIntegerDotProductFeatures`/`Properties` structs agree with the aggregate `VkPhysicalDeviceVulkan13Features`/`Properties` cases rather than repeating E2's own first-draft regression |
| `dEQP-VK.spirv_assembly.instruction.compute.{opsdotkhr,opudotkhr,opsudotkhr,opsdotaccsatkhr,opudotaccsatkhr,opsudotaccsatkhr}.*` (1,248 total) | 80 `Pass`, 0 `Fail`, 1,168 `NotSupported` |

**The 1,168 `NotSupported` cases are a correct rejection, not a gap this
row leaves open.** Each requires an operand width or capability this ICD
does not implement at all, independent of this row's own scope: `i16`
vectors need `shaderInt16` (unimplemented), `i64` vectors need
`shaderInt64` (unimplemented), and every combination requiring
`DotProductInputAll` beyond the 4-lane `i8` vector/4x8-packed-`i32` shapes
this row's own lane-extraction/unpacking logic handles falls back to the
same still-missing capability. Every case within this row's actual scope
(4-lane `vector<4xi8>` operands and 4x8-bit-packed scalar `i32` operands,
each output width the six ops themselves support) passes: 8 `Pass` each
for `opsdotkhr`/`opudotkhr`/`opsudotkhr` (the three binary ops), 20 `Pass`
each for `opsdotaccsatkhr`/`opsudotaccsatkhr`, and 16 `Pass` for
`opudotaccsatkhr` (fewer combinations, since `UDotAccSat` has no
mixed-signedness variant of its own). Zero `Fail` across all six groups
confirms the per-lane extend/multiply/add/saturate sequence
(`extractIntegerDotProductLanes`/`reduceIntegerDotProductLanes` in
`SPIRVToLLVMPatterns.cpp`) is correct for every shape CTS actually
exercises against it, not merely plausible by inspection.

## Roadmap E9: measured impact

Roadmap E9 (`VK_EXT_pipeline_creation_cache_control`/
`pipelineCreationCacheControl`) is a flag-only addition, exactly as its
own premise anticipated: `vkCreateComputePipelines`/
`vkCreateGraphicsPipelines` (Pipeline.cpp/GraphicsPipeline.cpp) now honor
`VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT` on a cache
miss (or with no cache at all), reporting `VK_PIPELINE_COMPILE_REQUIRED`
and leaving that pipeline null instead of compiling for real; a
`VkPipelineCache` created with `VK_PIPELINE_CACHE_CREATE_EXTERNALLY_
SYNCHRONIZED_BIT` (PipelineCache.{h,cpp}) skips the new internal mutex
guarding `lookup`/`insert`/`lookupGraphics`/`insertGraphics` entirely.
`pipelineCreationCacheControl` now reads `VK_TRUE` from both the
aggregate `VkPhysicalDeviceVulkan13Features` struct and a new dedicated
`VkPhysicalDevicePipelineCreationCacheControlFeatures` struct.
`getSupportedDeviceExtensions` gained
`VK_EXT_pipeline_creation_cache_control` itself, the same "CTS enables it
by name regardless of `apiVersion`" reason E3/E5/E6/E8 already
established.

**Targeted CTS runs**, against this session's HEAD build:

| Case(s) | Result |
|---|---|
| `dEQP-VK.api.info.get_physical_device_properties2.features.pipeline_creation_cache_control_features` | `Pass` (previously `Fail`, per D1/D3's own `api.info.*` bucket -- see "Roadmap D3: measured impact" above) |
| `dEQP-VK.api.info.vulkan1p3.feature_extensions_consistency` | `Pass`, confirming the new dedicated `VkPhysicalDevicePipelineCreationCacheControlFeatures` struct agrees with the aggregate `VkPhysicalDeviceVulkan13Features` case rather than repeating E2's own first-draft regression |
| `dEQP-VK.pipeline.monolithic.creation_cache_control.*` (18 total) | 1 `Pass`, 17 `InternalError` |

**The single `Pass`,
`creation_cache_control.compute_pipelines.single_pipeline_no_compile`, is
this row's own actual scope working correctly:** a lone
`VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT` compute
pipeline creation with no cache reports
`VK_PIPELINE_COMPILE_REQUIRED`, exactly as expected. **The 17
`InternalError` cases are two pre-existing, out-of-scope gaps, neither
one this row's own bits:**

- 8 of the 9 `compute_pipelines.*` cases beyond `single_pipeline_no_compile`
  fail identically, with `error: 'llvm.getelementptr' op operand #0 must
  be LLVM pointer type or LLVM dialect-compatible vector of LLVM
  pointer type, but got 'vector<3xi32>'` -- this group's shared compute
  shader indexes an output buffer by `gl_GlobalInvocationID.x`, and this
  ICD's SIMD-widened dispatch lowering produces a vector-of-addresses GEP
  base this particular access pattern does not expect, the same
  "resource handle the FeMe CPU target cannot normalize" class of gap
  E6's own measured-impact section already found (73 of that section's
  77 failures). It reproduces identically whether or not
  `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT` is present
  on any `VkComputePipelineCreateInfo` in the batch: the batch/derivative
  tests this affects only differ from the passing case in *how many*
  pipelines they create and in what order, never in this row's own flag
  handling.
- All 9 `graphics_pipelines.*` cases, including
  `single_pipeline_no_compile` itself, fail with `vkCreateGraphicsPipelines:
  a graphics pipeline needs both a vertex and a fragment stage` --
  `compileGraphicsPipeline` (GraphicsPipeline.cpp) has always required
  both stages to be present (a pre-existing, unrelated structural
  requirement of the monolithic graphics-pipeline-creation path, not
  something this row narrows or widens), and this CTS group's own
  graphics pipelines are deliberately minimal (built only to exercise
  cache-control bookkeeping, not to render), so every one of them is
  rejected before this row's own `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_
  COMPILE_REQUIRED_BIT` check is ever reached.

Neither gap is a regression this row introduces -- both reproduce for any
compute/graphics pipeline creation through this ICD today, cache-control
bits or not -- and closing either is out of this row's own scope (a
CPU-target SIMD-lowering limitation and a monolithic-pipeline single-stage
restriction, respectively, tracked separately from `VK_EXT_pipeline_
creation_cache_control`'s own two bits).

## Roadmap E10: measured impact

Roadmap E10 (`VK_EXT_private_data`/`privateData`) is a new, self-contained
object, exactly as its own premise anticipated: `PrivateDataSlot`
(`PrivateData.{h,cpp}`) is a map from `(VkObjectType, uint64_t handle)` to
a `uint64_t`, independent of every other object in `Objects.h`, backing
`vkCreatePrivateDataSlot`/`vkSetPrivateData`/`vkGetPrivateData`/
`vkDestroyPrivateDataSlot`. `privateData` now reads `VK_TRUE` from both the
aggregate `VkPhysicalDeviceVulkan13Features` struct and a new dedicated
`VkPhysicalDevicePrivateDataFeatures` struct; `getSupportedDeviceExtensions`
gained `VK_EXT_private_data` itself, the same "CTS enables it by name
regardless of `apiVersion`" reason E3/E5/E6/E8/E9 already established.

**Targeted CTS runs**, against this session's HEAD build:

| Case(s) | Result |
|---|---|
| `dEQP-VK.api.info.get_physical_device_properties2.features.private_data_features` | `Pass` (previously `Fail`, per D1/D3's own `api.info.*` bucket -- see "Roadmap D3: measured impact" above) |
| `dEQP-VK.api.info.vulkan1p3.*` (5 total) | 5/5 `Pass`, confirming the new dedicated struct agrees with the aggregate `VkPhysicalDeviceVulkan13Features` case rather than repeating E2's own first-draft regression |
| `dEQP-VK.api.device_init.create_device_unsupported_features.private_data_features` | `Pass` |
| `dEQP-VK.api.object_management.private_data.*` (40 total) | 37 `Pass`, 2 `Fail`, 1 `NotSupported` |

**The `NotSupported` case** (`image_view_cube_arr`) is a pre-existing,
out-of-scope gap: `imageCubeArray` is not implemented by this ICD at all, a
prerequisite this private-data test case's own object under test happens to
need, unrelated to any of `VK_EXT_private_data`'s own bits. **Both `Fail`
cases are likewise pre-existing, out-of-scope gaps, neither one this row's
own entrypoints:**

- `compute_pipeline` fails identically to E9's own measured-impact section
  (`'llvm.getelementptr' op operand #0 must be LLVM pointer type ...`), the
  same "resource handle the FeMe CPU target cannot normalize" class of gap
  E6/E9 already found -- this test's compute shader indexes a buffer by
  `gl_GlobalInvocationID.x`, and reproduces whether or not a private data
  slot is ever attached to the pipeline.
- `graphics_pipeline` fails with `feme-cpu-simdize`'s own divergent-vector
  diagnostic, the same "roadmap milestone 7 deviation" C3/D3 already
  tracked (`ubo.*.std430`'s own 2,650-case bucket, "Roadmap D3: measured
  impact" above) -- a pre-existing SIMD-widening limitation this test's
  graphics shader happens to hit, not anything `vkSetPrivateData`/
  `vkGetPrivateData` themselves touch.

Neither gap is a regression this row introduces: both reproduce for any
compute/graphics pipeline creation through this ICD today, with or without
a private data slot attached, and closing either is out of this row's own
scope.

## Roadmap E11: measured impact

Roadmap E11 (`VK_EXT_shader_demote_to_helper_invocation`/
`shaderDemoteToHelperInvocation`) needed more than its own premise
anticipated: the audit found no `spirv`->`llvm` conversion pattern for
*either* `OpKill` or `OpDemoteToHelperInvocation`, and MLIR's own upstream
SPIR-V dialect had no op at all for the latter (despite already having its
`Capability`/`Extension` enum cases), so `mlir::spirv::deserialize` would
reject any real module using it. This row added `spirv.
DemoteToHelperInvocation` to MLIR itself (a non-terminator, unlike the
deprecated `spirv.Kill`), a new `llvm.spv.demote.to.helper.invocation`
LLVM intrinsic mirroring `llvm.spv.discard`'s shape,
`SPIRVToLLVMPatterns.cpp`'s new `DemoteToHelperInvocationConversionPattern`
converting the op to that intrinsic, and `CanonicalizeStage.cpp` raising
it into `feme.stage.demote(true)` -- unconditional, matching
`llvm.spv.discard`'s own existing raising into `feme.stage.discard(true)`
-- whose reference/SIMD lowering (`StageOpKind::Demote`) already existed.
`shaderDemoteToHelperInvocation` now reads `VK_TRUE` from both the
aggregate `VkPhysicalDeviceVulkan13Features` struct and a new dedicated
`VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures` struct;
`getSupportedDeviceExtensions` gained `VK_EXT_shader_demote_to_helper_
invocation` itself, the same "CTS enables it by name regardless of
`apiVersion`" reason E3/E5/E6/E8/E9/E10 already established.

**Targeted CTS runs**, against this session's HEAD build:

| Case(s) | Result |
|---|---|
| `dEQP-VK.api.info.get_physical_device_properties2.features.shader_demote_to_helper_invocation_features` | `Pass` (previously `Fail`, per D1/D3's own `api.info.*` bucket -- see "Roadmap D3: measured impact" above) |
| `dEQP-VK.api.info.vulkan1p3.*` (5 total) | 5/5 `Pass`, confirming the new dedicated `VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures` struct agrees with the aggregate `VkPhysicalDeviceVulkan13Features` case rather than repeating E2's own first-draft regression |
| `dEQP-VK.api.device_init.create_device_unsupported_features.shader_demote_to_helper_invocation_features` | `Pass` |
| `dEQP-VK.*demote*` (60 total, wildcard across every group naming this concept `demote`) | 2 `Pass` (the two `api.*` cases above), 58 `NotSupported` |
| `dEQP-VK.*helper_invocation*` (94 total, wildcard across every group naming this concept `helper_invocation`) | 2 `Pass` (the two `api.*` cases above), 5 `Fail`, 87 `NotSupported` |

**Every one of the 58+87 `NotSupported` cases is a pre-existing,
out-of-scope prerequisite gap, not this row's own bits:** the `demote`-named
cases need either an unsupported depth/stencil format
(`vktRenderPassDepthStencilWriteConditionsTests.cpp`'s
`depth_stencil_write_conditions.*`), `VK_EXT_shader_stencil_export` (its
`stencil_demote_*` siblings), fragment-shader stores/atomics
(`rasterization.frag_side_effects.*`), or subgroup operations in the
fragment stage (`reconvergence.maximal.fragment.demote_*`) -- none of
which `VK_EXT_shader_demote_to_helper_invocation` itself requires; the
`helper_invocation`-named cases are almost entirely `VK_KHR_acceleration_
structure` (ray query), likewise unrelated. **The 5 `Fail` cases
(`dEQP-VK.glsl.helper_invocations.{load_from_image,load_from_ssbo,
load_from_texture,load_from_ubo,output_variables}`) are also a
pre-existing, out-of-scope gap, not this row's own entrypoints:** all five
fail identically, at `vkCreateRenderPass` with `VK_ERROR_FORMAT_NOT_
SUPPORTED`, before any shader referencing `demote`/helper-invocation state
is ever compiled or executed -- an unadvertised render-pass attachment
format these particular tests happen to need, the same class of
pre-existing `Format.cpp` gap several earlier rows in this report already
found, unrelated to `OpDemoteToHelperInvocation`.

## Roadmap E12: measured impact

Roadmap E12 (`VK_KHR_shader_terminate_invocation`/
`shaderTerminateInvocation`) needed the same shape of prerequisite gap E11
found: the audit found no `spirv`->`llvm` conversion pattern for SPIR-V's
`OpTerminateInvocation` at all, and MLIR's own upstream SPIR-V dialect had
no op for it either (despite the `SPV_KHR_terminate_invocation` extension
enum case already existing), so `mlir::spirv::deserialize` would reject
any real module using it. This row added `spirv.TerminateInvocation` to
MLIR itself (a true terminator, unlike `spirv.DemoteToHelperInvocation`,
requiring no capability beyond the existing `Shader` one) and
`SPIRVToLLVMPatterns.cpp`'s new `TerminateInvocationConversionPattern`,
converting the op into exactly the unconditional discard-and-return the
roadmap row's own premise specified: a call to the same `llvm.spv.discard`
intrinsic `OpKill` itself would use (already raised into
`feme.stage.discard(true)` by the existing `CanonicalizeStagePass`
renaming, unmodified by this milestone), followed by an `llvm.return`.
`shaderTerminateInvocation` now reads `VK_TRUE` from both the aggregate
`VkPhysicalDeviceVulkan13Features` struct and a new dedicated
`VkPhysicalDeviceShaderTerminateInvocationFeatures` struct;
`getSupportedDeviceExtensions` gained `VK_KHR_shader_terminate_invocation`
itself, the same "CTS enables it by name regardless of `apiVersion`"
reason E3/E5/E6/E8/E9/E10/E11 already established.

**Targeted CTS runs**, against this session's HEAD build:

| Case(s) | Result |
|---|---|
| `dEQP-VK.api.info.get_physical_device_properties2.features.shader_terminate_invocation_features` | `Pass` |
| `dEQP-VK.api.info.vulkan1p3.{features,properties,feature_extensions_consistency}` (3 total) | 3/3 `Pass`, confirming the new dedicated `VkPhysicalDeviceShaderTerminateInvocationFeatures` struct agrees with the aggregate `VkPhysicalDeviceVulkan13Features` case |
| `dEQP-VK.api.device_init.create_device_unsupported_features.shader_terminate_invocation_features` | `Pass` |
| The 72 `dEQP-VK.graphicsfuzz.*` cases this table's own `graphicsfuzz` row above (72, "closes D3's `graphicsfuzz` 72-case regression") names, identified directly from the 72 `*.amber` source files under `external/vulkancts/data/vulkan/amber/graphicsfuzz/` that reference `OpTerminateInvocation` | 0/72 `Pass`, 72/72 `Fail` -- see correction below |

**This is a premise correction to the `graphicsfuzz` row's own D3-era
characterization above ("run ... and produce a wrong image, an
image-comparison mismatch, not a pipeline-creation error"), not a sign
this row's own conversion pattern is wrong.** All 72 cases fail identically
at Amber's own pre-flight check, before any pipeline is created or shader
executed: `Vulkan color attachment format is not supported`
(`external/amber/src/src/vulkan/engine_vulkan.cc`'s `CreatePipeline`,
querying `vkGetPhysicalDeviceFormatProperties`). This is not new, and not
specific to `OpTerminateInvocation`: a control sample of 20 arbitrary
`graphicsfuzz` cases that do not reference `OpTerminateInvocation` at all
fails identically, 20/20, at the same check -- confirming this is the
same pre-existing, already-documented `vkGetPhysicalDeviceFormatProperties`
stub gap this report's own "Headline"/C1 sections trace (it unconditionally
reports zero format-feature support regardless of `isSupportedColorAttachmentFormat`,
a gap "deliberately left unfixed" per this report's own later note), which
blocks the entire 757-case `graphicsfuzz` group uniformly, not just these
72. That gap is out of this milestone's own scope (§1.2's SPIR-V
conversion patterns, not `vkGetPhysicalDeviceFormatProperties`) and
unrelated to `OpTerminateInvocation`, so it is not fixed here. This
milestone's own actual scope -- the `OpTerminateInvocation` conversion
pattern itself -- is instead verified directly at the SPIR-V/LLVM IR level
by `feme/test/Conversion/SPIRVToLLVM/spirv-to-llvm-terminate-invocation.mlir`
(new this row), which confirms the exact `llvm.call_intrinsic
"llvm.spv.discard"()` + `llvm.return` shape the roadmap row's own premise
specified, and the feature/extension advertisement is verified end-to-end
by the three passing `api.info`/`device_init` cases above.

## Roadmap E13: measured impact

Roadmap E13 (`VK_KHR_zero_initialize_workgroup_memory`/
`shaderZeroInitializeWorkgroupMemory`) needed a different shape of
prerequisite gap than E11/E12's own missing conversion patterns: the audit
found SPIR-V `Workgroup`-storage-class globals (GLSL `shared`/HLSL
`groupshared` declared directly in SPIR-V, as opposed to raised from DXIL)
had *no* conversion pattern at all -- MLIR's own upstream `GlobalVariablePattern`
only supports `Input`/`Private`/`Output`/`StorageBuffer`/`UniformConstant`,
so any real SPIR-V module declaring one failed to convert entirely,
regardless of zero-initialization. `feme::spirv::WorkgroupGlobalVariablePattern`
(SPIRVToLLVMPatterns.cpp) fills that gap, converting one to an ordinary
`llvm.mlir.global` in address space 3 (the same convention Clang's own HLSL
`groupshared` codegen already uses), plus a new pointer-type conversion
routing an ordinary access chain through the same address space.

Getting the zero-initializer itself imported needed a second, independent
MLIR gap fixed first: `spirv.GlobalVariable`'s own `Initializer` operand
resolution (`processGlobalVariable` in the deserializer) only accepted a
global variable, specialization constant, or specialization constant
composite symbol -- not the plain `OpConstantNull` this feature's own
zero-initializer always is (`Workgroup`'s Initializer may hold no other
value per spec) -- and `processConstantNull` itself only built a null
value for a scalar/vector/tensor type, erroring out for the composite
(`spirv.array`/`spirv.struct`) shapes a real `shared` variable's own type
usually takes. Both are fixed in MLIR itself (`zero_initialized`, a new
unit attribute on `spirv.GlobalVariable`, and `getNullAttrForType`'s
recursive generalization), with the same "extend MLIR, not just feme"
shape E11/E12's own audits already established. `GroupSharedLayout` gains
`NeedsZeroInit` (GroupShared.h), read off `hasInitializer()` on the
imported global; `feme::cpu::EntryWrapperPass` `memset`s the flat
groupshared buffer to zero, once per group, when it is set
(EntryWrapper.cpp). `shaderZeroInitializeWorkgroupMemory` now reads
`VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan13Features`
struct and a new dedicated
`VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures` struct;
`getSupportedDeviceExtensions` gained
`VK_KHR_zero_initialize_workgroup_memory` itself, the same "CTS enables it
by name regardless of `apiVersion`" reason E3/E5/E6/E8/E9/E10/E11/E12
already established.

**Targeted CTS run**, against this session's HEAD build, of every
sub-group under `dEQP-VK.compute.pipeline.zero_initialize_workgroup_memory.*`
(the row's own named 7-case regression -- see below for why the real
count is much larger once the extension is genuinely advertised):

| Sub-group | Result |
|---|---|
| `types.{bool,float32_t,int32_t,uint32_t}` (4 cases) | **`Pass`** -- a scalar `shared TYPE x = {};` variable, the exact shape this row's own scope targets, reads back zero on every lane |
| `types.*` (remaining 71 of 75) | `Fail`/`NotSupported` -- see correction below |
| `max_workgroup_memory.*` (6), `composites.*` (15), `max_workgroups.*` (3), `specialize_workgroup.*` (512), `repeat_pipeline.*` (32) | `Fail`, blocked before any zero-initialization is even exercised -- see correction below |
| `shared_memory_blocks.*` (1) | `Fail` -- same shape as `composites.*` below |

**This is a premise correction to the row's own "Closes D3's
`compute.pipeline.zero_initialize_workgroup_memory` 7-case regression"
text, not a sign this row's own zero-initialization is wrong**: the 4
genuine passes above confirm the actual feature works end-to-end for a
scalar variable. Every other sub-case is blocked by one of two further,
pre-existing gaps this row's own scope does not cover, found by the same
audit-first discipline E11/E12 already established:

- **`OpTypeArray`'s Length operand must come from a normal constant**
  (`mlir::spirv::Deserializer::processConstantArray`'s own pre-existing
  check, unmodified by this row): every non-scalar case in this CTS
  source file (`vktComputeZeroInitializeWorkgroupMemoryTests.cpp`) sizes
  its `shared` array from a `layout(constant_id = N) const uint ...`
  specialization constant, not a plain constant, so the whole module fails
  to deserialize (`OpTypeArray count <id> ... can only come from normal
  constant right now`) before this row's own `WorkgroupGlobalVariablePattern`
  ever runs. This blocks `max_workgroup_memory`, `composites`,
  `max_workgroups`, most of `specialize_workgroup`/`repeat_pipeline`, and
  every non-scalar `types` case.
- **A vector/matrix `shared` variable's own element-indexed
  `spirv.AccessChain` produces a `<3 x i32>`-vector (not scalar) GEP base**
  for `uvec3`/`ivec3`/... element types (`'llvm.getelementptr' op operand
  #0 must be LLVM pointer type ...`) -- a divergent-index shape this row's
  own scope (a flat scalar zero-init) does not model, the same class of
  gap `feme::cpu::SIMDizePass`'s own groupshared canonicalization narrows
  around (see GroupShared.h). This blocks every vector/matrix `types` case
  (`uvec2`/`uvec3`/`uvec4`/...).
- **A boolean-typed composite's `getelementptr` crashes `mlir::translateModuleToLLVMIR`**
  with an LLVM-side assertion (`Not byte-addressable`,
  `GetElementPtrTypeIterator.h`'s `getSequentialElementStride`) rather than
  failing gracefully -- an `i1`-element `spirv.array`/`spirv.struct` GEP's
  constant-folding path assumes every element type is byte-addressable,
  which `i1` is not. This is a real, pre-existing robustness gap (an ICD
  should never abort a CTS *process*, only fail a case), found via
  `composites.2`; isolated with `gdb -batch -ex run -ex bt` against the
  single case, confirming it is deterministic and specific to a
  boolean/composite shape, not a flake.

Both are out of this row's own scope (§1.2/§1.6, a zero-initializer for an
already-importable `Workgroup` global -- not `OpTypeArray`'s own Length
operand or LLVM's own GEP constant-folding) and unrelated to
zero-initialization itself: the identical `OpTypeArray`
specialization-constant gap would block these same shaders' import even
with `shaderZeroInitializeWorkgroupMemory` left `VK_FALSE`, since
`WorkgroupGlobalVariablePattern` never gets a chance to run before
deserialization itself fails. Recorded here (and in `Roadmap.md`'s E13
entry) rather than fixed as a drive-by, the same discipline E12's own
`vkGetPhysicalDeviceFormatProperties` correction and E6's own
`secondary_push_constants_2` finding already established for this report.

## Roadmap E14: measured impact

Roadmap E14 (`VK_EXT_inline_uniform_block`/`inlineUniformBlock` +
`descriptorBindingInlineUniformBlockUpdateAfterBind`) added a third
per-binding storage kind to `feme::vulkan::DescriptorSet` -- a plain byte
blob, alongside the existing handle-array buffer/image ones (Descriptor.
{h,cpp}) -- and a `VkWriteDescriptorSetInlineUniformBlock` pNext case to
`vkUpdateDescriptorSets`, `vkUpdateDescriptorSetWithTemplate`, and
`VkCopyDescriptorSet`'s copy path. Per this row's own stated scope, this is
the descriptor object model only: no `feme::cpu::SPIRVResourceLoweringPass`
conversion consumes an inline uniform block from a real dispatch yet.

**Targeted CTS run**, against this session's HEAD build, of every case
under `dEQP-VK.binding_model.inline_uniform_blocks.*` (the dedicated
group) plus every `*inline_uniform_block*`-named case under
`dEQP-VK.api.*` and the rest of `dEQP-VK.binding_model.*` (136 cases
total):

| Result | Count | Detail |
|---|---:|---|
| `Pass` | 3 | `api.device_init.create_device_unsupported_features.inline_uniform_block_features`, `api.info.get_physical_device_properties2.features.inline_uniform_block_features`, `api.info.vulkan1p2_limits_validation.ext_inline_uniform_block` -- the feature/property advertisement itself, exactly this row's own scope |
| `Fail` (`vk.createGraphicsPipelines(...)`) | 17 | Every graphics-shaped case (`descriptor_buffer.*`, `descriptor_copy.graphics*`) fails at graphics pipeline creation -- this ICD is compute-only (`FeMeVulkanDesign.md`'s "Initial Non-Goals"), unrelated to inline uniform blocks specifically |
| `Fail` (`vk.createComputePipelines(...)`) | 8 | `descriptor_copy.compute.inline_uniform_block_*`: a real compute shader that actually *reads* through an inline-uniform-block binding fails pipeline creation cleanly, because `SPIRVResourceLoweringPass` has no conversion for this resource kind yet -- exactly the "object model only, dispatch consumption deferred" scope this row's own text states, not a regression |
| `NotSupported` | 108 | 78 need `VK_EXT_descriptor_buffer`, 24 need `VK_EXT_descriptor_indexing`, 6 exceed `maxBoundDescriptorSets` -- none of which this row touches |

**This also found and fixed a genuine, in-scope limits bug**: the first
run of `dEQP-VK.api.info.vulkan1p2_limits_validation.ext_inline_uniform_block`
failed, reporting `maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks`/
`maxDescriptorSetUpdateAfterBindInlineUniformBlocks` as `0` against a
required `>= 4` floor. Unlike Vulkan 1.2's own descriptor-indexing
`UpdateAfterBind` limits (which stay `0` alongside a `VK_FALSE`
`descriptorIndexing`, and are not cross-checked unconditionally), these two
`VK_EXT_inline_uniform_block` limits are required independent of
`descriptorBindingInlineUniformBlockUpdateAfterBind`'s own value -- both
now equal their non-`UpdateAfterBind` counterparts (4) instead of a literal
`0`, and the same targeted case now passes.

**No case produces a wrong answer**: every failure above is a clean
rejection (`VK_ERROR_INITIALIZATION_FAILED`) at pipeline creation, not a
`Pass`-shaped result carrying incorrect descriptor data, matching the
"must fail before draw time, not silently misbehave" contract every
FeMeVulkanDesign.md milestone states. A broader regression check --
`check-feme` (1586 passed, 1 unsupported, unchanged) plus a full
`dEQP-VK.api.*` (267,222 cases: 8,108 passed, 249 failed, same shape as
before this row, with `vulkan1p3.feature_extensions_consistency`/
`property_extensions_consistency` and `get_physical_device_properties2.
features.inline_uniform_block_features` all newly `Pass`) and full
`dEQP-VK.binding_model.*` (150,259 cases: 1 passed, 20,379 failed, all
clean `VK_ERROR_INITIALIZATION_FAILED`/`VK_ERROR_FORMAT_NOT_SUPPORTED`
rejections, no crash) -- found no regression from advertising this
extension.

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

## Roadmap E15: no CTS run (verify-first gate resolved by a documentation-only split)

E15's own text gated implementation on first verifying LDR ASTC's
status in `Format.cpp`; that verification found no ASTC decode of any
kind (LDR or HDR), no block-compressed format support at all, and a
second, independent gap in `Image.cpp`'s per-texel (not block-based)
subresource layout. Per the row's own instruction ("should be split")
and roadmap G4, this pass's only output is the split itself -- new
Roadmap.md rows E20 (LDR prerequisite + block-layout groundwork) and
E21 (E15's original HDR-only scope) -- plus the corresponding
`Vulkan14FeatureInventory.md`/`FeMeVulkanDesign.md` notes recording the
finding. See `agent_thoughts.md` for the full investigation.

No production code changed (`mapVkFormat` still returns `std::nullopt`
for every ASTC format, `textureCompressionASTC_HDR` stays `VK_FALSE` --
both already the correct, honest answer). `ninja lib/libfeme_vulkan.so`
reported `ninja: no work to do` after this change, the same
stronger-than-a-rebuild evidence the "SPIR-V `spirv.Image`/
`spirv.VulkanBuffer` AMDGPU-lowering fix" addendum above used: no
`dEQP-VK` run (full or spot-check) was performed, since every existing
case in this report already exercises the exact binary this change did
not touch. `ninja check-feme`: 1586/1587 passed, 1 unsupported
(pre-existing, unrelated), before and after.

## Roadmap E20: measured impact (targeted, not a full re-run)

Unlike E15, this row's own code did change `libfeme_vulkan.so`:
`Format.h`/`Image.{h,cpp}` gained real block-aware layout math and 28
new `mapVkFormat` entries, and a new `ASTCDecode.{h,cpp}` landed a real
decoder. But `vkCreateImage` still rejects every ASTC `VkFormat`
outright (`VK_ERROR_FORMAT_NOT_SUPPORTED`, the same result an
unrecognized format already produced before this row), and
`textureCompressionASTC_LDR` stays `VK_FALSE` -- both unchanged from
`libfeme_vulkan`'s pre-E20 behavior, by design (see Image.h's file
comment and Roadmap.md's E20/E22 rows). A full 3-million-case re-run
would only be expected to reproduce the headline table above verbatim,
so two targeted subsets were run instead to confirm that expectation
rather than assume it:

- `dEQP-VK.api.info.*` (10,484 cases, the same subtree the
  "GetDimensions.xy/AMDGPU change" addendum above used for its own
  "unaffected" spot check): 5,873 passed / 73 failed / 4,538 not
  supported -- the same mix-of-outcomes, zero-crash shape as that
  addendum's own 5,669/10,484 baseline (the difference is this
  session's CTS revision drift, `vulkan-cts-1.4.6.2-413-ge4b225a7d7cd`
  vs. that addendum's, not this row's own change: none of the 73
  failures involve `astc` in their name).
- `dEQP-VK.*astc*` (98,927 cases spanning `api`, `image`, `pipeline`,
  `sparse_resources`, and `texture`): 873 passed / 1 failed / 98,053
  not supported. The one failure,
  `dEQP-VK.api.info.get_physical_device_properties2.features.
  texture_compression_astchdr_features`, is roadmap E15/G4's
  already-tracked, unrelated `textureCompressionASTC_HDR` aggregate-
  vs-dedicated-struct mismatch (this ICD advertises no
  `VK_EXT_texture_compression_astc_hdr` struct case at all, since the
  extension itself is not advertised) -- not a new failure this row
  introduced, and not an LDR-named case. Every LDR-format-named case
  (`*_astc_4x4_unorm*`, `*_astc_12x12_srgb*`, ...) is `NotSupported
  (Format not supported at vktTextureTestUtil.cpp:1678)`, i.e. the
  exact same clean rejection `libfeme_vulkan` already produced before
  this row, confirming `vkCreateImage`'s continued rejection did not
  regress any previously-passing case or newly fail one.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1599/1600
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row.

## Roadmap E21: measured impact (targeted, not a full re-run)

Same shape as "Roadmap E20: measured impact" above, and for the same
reason: this row's own code changed `libfeme_vulkan.so` (14 new
`mapVkFormat` entries for the `_SFLOAT_BLOCK_EXT` formats, plus a new
`feme::vulkan::decodeASTCBlockHDR`), but `vkCreateImage` still rejects
every ASTC `VkFormat` outright and `textureCompressionASTC_HDR` stays
`VK_FALSE` -- both unchanged from `libfeme_vulkan`'s pre-E21 behavior
(see Roadmap.md's E21/E22 rows). The same two targeted subsets from
E20's own run were repeated rather than assumed unaffected:

- `dEQP-VK.api.info.*` (10,484 cases): 5,873 passed / 73 failed / 4,538
  not supported -- byte-for-byte the same split E20's own run recorded,
  confirming this row did not touch anything that subtree exercises.
- `dEQP-VK.*astc*` (98,927 cases): 873 passed / 1 failed / 98,053 not
  supported -- again the identical headline numbers to E20's own run.
  The one failure is the same already-tracked
  `dEQP-VK.api.info.get_physical_device_properties2.features.
  texture_compression_astchdr_features` mismatch E20's report recorded
  (roadmap E15/G4's aggregate-vs-dedicated-struct gap, unrelated to
  this row). No case name in this subtree matches any of the 14 2D
  `astc_*_sfloat_block_ext` footprints this row's own `mapVkFormat`
  change added -- this CTS revision's case generator only emits
  `*_sfloat_block_ext`-named cases for the separate 3D "full profile"
  ASTC footprints (`astc_3x3x3_sfloat_block_ext`, ...), which
  `VK_EXT_texture_compression_astc_hdr` itself does not define (see
  that extension's own text: "additional ASTC formats (the 'Full
  profile') exist which support 3D data... not defined by either the
  LDR or HDR profiles") -- so this row's own new formats have no
  matching case at all yet in this CTS build, gated behind the
  extension advertisement CTS's 2D case generator apparently requires
  and this ICD does not yet provide. Every one of the 5,540
  3D-footprint `*_sfloat_block_ext` cases present is `NotSupported`,
  unchanged from before this row.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1609/1610
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row.

## Roadmap E22: measured impact (targeted, not a full re-run) -- a real, pre-existing gap found blocking any headline movement

E22 ("ASTC LDR copy/sampling pipeline wiring") changed `libfeme_vulkan.so`
more than E20/E21 did: `vkCreateImage` accepts a block-compressed
`VkFormat`, `Image::blockPointer`/`CommandBuffer.cpp`'s
`vkCmdCopyImage`/`vkCmdCopyBufferToImage`/`vkCmdCopyImageToBuffer` address
one a whole block at a time, `ImageOps.cpp`'s `runBlitImage` decodes an
LDR ASTC source through `decodeASTCBlock`, and `textureCompressionASTC_LDR`
now reads `VK_TRUE`. The same two targeted subsets E20/E21 used were run
again to measure the effect:

- `dEQP-VK.api.info.*` (10,484 cases): 5,873 passed / 73 failed / 4,538
  not supported -- byte-for-byte the same split every prior row in this
  ASTC sequence recorded. The 73 failures are unchanged (the one
  ASTC-named failure is still
  `dEQP-VK.api.info.get_physical_device_properties2.features.
  texture_compression_astchdr_features`, E15/G4's already-tracked HDR
  aggregate-struct mismatch, unrelated to this row's LDR-only scope).
- `dEQP-VK.*astc*` (98,927 cases): 873 passed / 1 failed / 98,053 not
  supported -- again byte-for-byte the same headline numbers as E20/E21's
  own runs, despite `textureCompressionASTC_LDR` now being `VK_TRUE`.
  Every LDR-format-named texture case (e.g.
  `dEQP-VK.texture.swizzle.texture_coordinate.astc_4x4_unorm_block_2d_pot_xx`)
  is still `NotSupported (Format not supported at
  vktTextureTestUtil.cpp:1678)`, unchanged.

**Root-caused rather than assumed unaffected.** `vktTextureTestUtil.cpp:1678`
is not a format-specific ASTC check: it is this test fixture's own
capability probe, `vkGetPhysicalDeviceImageFormatProperties`, called
before creating *any* texture image, of any format. Reading
`feme/lib/Vulkan/EntryPoints.cpp` found why it never varies: this
entrypoint unconditionally returns `VK_ERROR_FORMAT_NOT_SUPPORTED` --
for every `VkFormat`, every `VkImageType`, every usage -- and its sibling
`vkGetPhysicalDeviceFormatProperties` unconditionally reports an all-zero
`VkFormatProperties`. Both are stubs whose own comments still read "no
image is supported yet (images are out of scope before V5)"; `git log`
confirms this text (and the unconditional rejection it justifies) predates
E22's own commits by more than 10 -- a real, pre-existing gap E22's CTS
run surfaced, not one it introduced. Because CTS gates *every* texture-
creation-shaped case on this same capability probe regardless of format,
no case shaped like "create a `VkImage` of format X and sample/copy it"
can ever pass in this CTS build today, independent of whether format X is
ASTC, `R8G8B8A8_UNORM`, or anything else -- so E22's own real, tested
functional change (confirmed correct by `FeMeVulkanTests`'
`ImageTest.{AcceptsASTCFormat,BlockPointerAddressesBlockGrid,
CopyBufferToASTCImageAndBack,CopyASTCImageToImage}`/`ImageOpsTest.{
BlitDecodesASTCSource,RejectsBlitToBlockCompressedDestination,
RejectsBlitOfHDRASTCSource,RejectsResolveOfBlockCompressedImage}`) has no
way to show up in this report's own headline numbers until that separate,
much larger gap closes. Tracked as new roadmap row E24 (unlike E23, not a
narrower follow-up to this row -- it predates E22 entirely and blocks
every format's texture-shaped CTS case, not only ASTC's).

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1616/1617
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row.

## Roadmap E23: measured impact (targeted, not a full re-run) -- unchanged headline, blocked by the same pre-existing E24 gap

E23 ("ASTC LDR shader-sampling wiring") changed only
`CommandBuffer.cpp`'s `materializeImageDescriptor`: a bound ASTC LDR image
is now decoded into a per-texel RGBA8 buffer before a compute shader's
`OpImageSample`/`OpImageFetch` ever reaches it, rather than reading
all-zero. The same two targeted subsets E20/E21/E22 used were run again:

- `dEQP-VK.api.info.*` (10,484 cases): 5,873 passed / 73 failed / 4,538
  not supported -- byte-for-byte identical to E22's own run; this row
  touches no feature/property advertisement at all, so no movement here
  was ever expected.
- `dEQP-VK.*astc*` (98,927 cases): 873 passed / 1 failed / 98,053 not
  supported -- again byte-for-byte identical to every prior row in this
  ASTC sequence. Every texture-creation-shaped ASTC case is still
  `NotSupported (Format not supported at vktTextureTestUtil.cpp:1678)`,
  for the exact reason E22's own report section above already root-caused
  and tracked as E24: `vkGetPhysicalDeviceImageFormatProperties`
  unconditionally fails for every `VkFormat` before any texture-shaped
  case can even create the `VkImage` it would need to sample -- a shader
  cannot sample an image CTS itself refuses to create. This row's real
  functional change (a shader that *does* get a live ASTC image bound to
  it -- which no CTS case can arrange today -- now samples real decoded
  data) is confirmed correct by `FeMeVulkanTests`'
  `ASTCSampledImageDispatchTest.SamplesARealDecodedTexelRatherThanAllZero`
  instead, the same "unit-test the change directly, since CTS can't reach
  it yet" situation E22's own report already established for its own
  `ImageTest`/`ImageOpsTest` additions. E24 remains the row that has to
  close before *any* of E20/E21/E22/E23's real, tested functional changes
  can show up in this report's own headline numbers.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1617/1618
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row -- the +1 discovered/passed test relative to E22's own report is
`ASTCSampledImageDispatchTest`'s new case, confirmed (by temporarily
reverting `CommandBuffer.cpp`'s change and re-running) to actually fail
without this row's fix rather than passing vacuously.

## Roadmap E24: measured impact (targeted, not a full re-run) -- the real headline movement E20-E23 had been blocked on

E24 replaced both `vkGetPhysicalDeviceFormatProperties`'s unconditional
all-zero `VkFormatProperties` and `vkGetPhysicalDeviceImageFormatProperties`'s
unconditional `VK_ERROR_FORMAT_NOT_SUPPORTED` with real answers
(`feme::vulkan::formatFeatureFlags`, new `Format.{h,cpp}` function, backed
by already-implemented predicates -- see Roadmap.md's E24 row for the
full derivation). The same two targeted subsets E20-E23 used were run
again, plus a fresh baseline capture of each *before* this row's own
changes to attribute every difference precisely:

- **`dEQP-VK.api.info.*`** (10,484 cases): 5,873 passed / 73 failed / 4,538
  not supported before this row (confirmed by re-running against a
  temporarily-reverted build -- byte-for-byte identical to E20-E23's own
  runs, as expected). After: 5,385 passed / **561** failed / 4,538 not
  supported. The 4,538 "not supported" count is unchanged (this row
  touches no version/extension/feature advertisement); every point of
  movement is `Passed` cases becoming `Failed`, broken down by joining
  each new failure's own case name against the prior run:
  - 54 `dEQP-VK.api.info.format_properties.*` (down from a 57-case
    pre-existing baseline also present before this row -- 3 fewer, not
    more, since a few formats' real feature sets happen to satisfy their
    mandatory floor where an all-zero stub never could).
  - 61 + 61 `dEQP-VK.api.info.unsupported_image_usage.{optimal,linear}`,
    59 + 59 `image_format_properties.3d.{optimal,linear}`, 59 + 59
    `image_format_properties.1d.{optimal,linear}`, 43
    `image_format_properties.2d.optimal`, 31
    `image_format_properties.2d.linear` -- 432 cases, all newly reachable
    because `vkGetPhysicalDeviceImageFormatProperties` used to fail
    before any of these checks could even run. Every one of these is a
    genuine, honestly-reported mandatory-format-support shortfall, not a
    bug in this row's own query logic: this ICD's CPU runtime only
    actually samples `R32G32B32A32_FLOAT`/`R8G8B8A8_UNORM`/`_UNORM_SRGB`
    (plus ASTC LDR, bridged, roadmap E23) and `RenderPass.cpp`'s color-
    attachment table covers a narrower set than Vulkan's own mandatory
    floor, so `formatFeatureFlags` honestly reports most other mandatory
    sampled/attachment formats as unsupported, and this query now says so
    where the old stub could not even be asked. Tracked as new roadmap
    row E25 rather than fixed here: closing it means broadening the CPU
    runtime's typed-sample table and `feme::graphics`'s pack/unpack
    table, not another capability-query fix.
  - 59 single-case `get_physical_device_properties2.pnext_format_
    properties.*` (one per format checked, a guard-value-pattern check
    against `VkFormatProperties2`'s `pNext` chain) plus the 14
    `get_physical_device_properties2.features`/1
    `vulkan1p2_limits_validation`/1 `get_physical_device_properties2.
    properties` cases already present in both the before and after runs
    -- unrelated to this row, the same pre-existing gaps E20-E23's own
    73-case baseline already carried forward unchanged.
- **`dEQP-VK.*astc*`** (98,927 cases): 873 passed / 1 failed / 98,053 not
  supported before this row (confirmed identical to E20-E23's own
  baseline by the same revert-and-rerun check). After: **8,237 passed /
  12,225 failed / 78,465 not supported** -- the real, substantial
  headline movement this entire ASTC sequence (E15, E20-E23) had been
  completely blocked on, exactly as E22's own report predicted: every
  texture-creation-shaped case can now actually create the `VkImage` it
  needs, and 8,237 of them (up from 873, none of which needed to create
  an image at all) now run for real and pass.

  **A genuine crash was found and fixed measuring this, not just a
  numbers regression.** The first attempt at this run aborted partway
  through on a `SIGABRT`, not a clean `Fail`/`NotSupported`:
  `dEQP-VK.api.copy_and_blit.copy_commands2.image_to_image.all_formats.
  color.2d_to_1d.astc_10x10_srgb_block.r32g32b32a32_uint.general_general`
  hit `feme::vulkan::Image::blockPointer`'s own assertion
  (`Image.cpp:129`, "blockPointer is for a block-compressed Format
  only"). Root cause: `CommandBuffer.cpp`'s `runCopyImage` derived a
  single `Compressed` flag from the *source* image's format alone and
  used it to choose `blockPointer` vs. `texelPointer` for *both* sides of
  a `vkCmdCopyImage` call -- correct when both images are, or neither is,
  block-compressed, but wrong the moment one side is and the other isn't.
  This exact shape (one ASTC block and one `R32G32B32A32_UINT` texel are
  both 16 bytes, so real Vulkan's "compatible formats" copy rule already
  permits pairing them) was unreachable before this row: with
  `vkGetPhysicalDeviceImageFormatProperties` unconditionally failing,
  `deqp-vk` could never create *either* image, let alone copy between
  them. Fixed by tracking each side's compressed-ness (and block shape)
  independently (`CommandBuffer.cpp`); a new
  `ImageTest.CopyASTCImageToCompatibleUncompressedFormat` regression test
  reproduces the exact case shape and is confirmed, by temporarily
  reverting the fix and re-running, to hit the identical assertion
  without it. With the fix, the full `dEQP-VK.*astc*` run completes
  cleanly (no crash) with the numbers above.

  The 12,225 new failures are a *different*, narrower class of gap than
  the crash: mostly the same real, honestly-surfaced mandatory-format-
  support shortfalls `dEQP-VK.api.info.*` found above (a texture case
  naming a format this ICD cannot actually sample now fails at the point
  it tries to, rather than being rejected before it could try at all),
  plus pre-existing, unrelated graphics-path gaps this subtree's own
  fragment-shader cases exercise for the first time now that they reach
  pipeline creation at all (e.g. `feme-cpu-simdize`'s "divergent vector
  value ... used outside a supported ... pattern" limitation, an already-
  documented roadmap milestone 7 deviation, unrelated to ASTC). Also
  tracked under new row E25 rather than root-caused case by case here,
  since E25's own scope (broadening real per-format feature support) is
  what closing most of them requires.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1637/1638
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row -- the +2 discovered/passed tests relative to E23's own report
are `FormatTest`'s new `formatFeatureFlags` cases and `EntryPointsTest`
(a new file covering both entrypoints directly, exercising real
`VkPhysicalDevice`/`VkInstance` objects the same way `ImageTest`/
`PhysicalDeviceInfoTest` already do), plus
`ImageTest.CopyASTCImageToCompatibleUncompressedFormat`, the regression
test for the `runCopyImage` crash above -- the latter confirmed, by
temporarily reverting the `CommandBuffer.cpp` fix and re-running, to
actually crash on the identical assertion without it, the same "prove
the test catches the regression" check every prior row in this sequence
has applied to its own new cases.




## Roadmap E25: measured impact (targeted, not a full re-run)

E25 broadened the CPU runtime's typed sample table
(`femeRTImageFormatElementSize`/`femeRTUnpackImageTexel`,
FeMeRuntimeCPU.c) from three formats to every non-integer,
non-block-compressed, non-depth/stencil format `feme::cpu::ResourceFormat`
lists, and `formatFeatureFlags` (Format.cpp) now advertises
`SAMPLED_IMAGE_BIT`/`SAMPLED_IMAGE_FILTER_LINEAR_BIT` for that broadened
set. The same two targeted subsets E24 used were re-run against this
build, with E24's own "after" numbers as the baseline (E25 makes no
version/extension/feature-advertisement change, so, per this report's own
established practice, a revert-and-rerun re-confirmation was not repeated
for that unrelated portion of the diff):

- **`dEQP-VK.api.info.*`** (10,484 cases): 5,385 passed / 561 failed /
  4,538 not supported before this row (E24's own "after" figure). After
  broadening the sample table alone (before the pNext fix below): 5,359
  passed / 587 failed -- **worse**, not better, and confirmed to be a
  real regression this row's own broadening surfaced rather than a
  measurement artifact (see "A real, pre-existing bug found and fixed"
  below for why).

  **A real, pre-existing bug was found and fixed measuring this, not
  introduced by it.** `vkGetPhysicalDeviceFormatProperties2`
  (EntryPoints.cpp) never walked its own `pNext` chain: `VkFormatProperties3`
  (a core Vulkan 1.3 struct, always chainable once an ICD's advertised
  `apiVersion` is >= 1.3, whether or not it also lists
  `VK_KHR_format_feature_flags2` as an advertised extension name, which
  this one still does not -- roadmap E19) was left completely untouched,
  silently discarding every bit `formatFeatureFlags` computed for any
  caller that chained one. `dEQP-VK.api.info.unsupported_image_usage.*`'s
  own `Context::getFormatProperties` helper chains exactly this struct
  once it sees a >=1.3-capable device, so every one of its checks was
  comparing a real `vkGetPhysicalDeviceImageFormatProperties` answer
  against an all-zero "what the format supports" baseline it read back --
  already true, and already silently wrong, for every format this ICD
  supported *before* this row (confirmed: `sampled_r8g8b8a8_unorm`/
  `sampled_r32g32b32a32_sfloat`, sampled since the original three-format
  table, already failed this exact check pre-E25). E25's own broadening
  simply added more formats reaching the same already-broken check, which
  is why the raw failure count went up rather than down on the first
  measurement. Fixed by filling `VkFormatProperties3`'s three feature
  fields from the same `VkFormatProperties` result
  `vkGetPhysicalDeviceFormatProperties2` already computes
  (`EntryPoints.cpp`); a new `EntryPointsTest.
  FormatProperties2FillsChainedFormatProperties3` regression test checks
  the chained struct now matches, and (per this report's established
  "prove the test catches the regression" practice) was confirmed to fail
  against a temporarily-reverted build before the fix landed.

  After both changes: **5,556 passed / 390 failed / 4,538 not supported**
  -- a net improvement of +171 passing cases over the E24 baseline, not
  just a recovery of this row's own broadening. Breaking down the
  remaining 390 failures by joining each against the case-name buckets
  E24's own report already used:
  - 0 `unsupported_image_usage.*` and 0
    `get_physical_device_properties2.pnext_format_properties.*` -- both
    fully closed by the `VkFormatProperties3` fix above (down from 138
    and 59 respectively, mid-fix).
  - 320 `image_format_properties.{1d,2d,3d}.*` (up from E24's 310: ten
    more of this row's own newly-sampled formats now reach this
    unrelated, still-open check rather than being rejected before they
    could). Every one of these fails with `"Required sample counts not
    supported"`, confirmed (via `dEQP-VK.api.info.image_format_properties.
    2d.optimal.r8g8b8a8_unorm`/`r32g32b32a32_sfloat`, both sampled since
    before this row) to be a real, pre-existing gap unrelated to this
    row's own format broadening: `vktApiFeatureInfo.cpp`'s own check
    requires `VkImageFormatProperties::sampleCounts` cover a mandatory
    minimum whenever a format supports `COLOR_ATTACHMENT_BIT`/
    `DEPTH_STENCIL_ATTACHMENT_BIT` at all, for *any* usage-flag subset
    being queried (not only one that itself requests one of those two
    usages) -- `Image.cpp`'s `supportedSampleCounts` instead narrows to
    `VK_SAMPLE_COUNT_1_BIT` for a usage subset (e.g. transfer-only) that
    names none of `SAMPLED`/`STORAGE`/`COLOR_ATTACHMENT`/
    `DEPTH_STENCIL_ATTACHMENT`. Genuinely separate work from per-format
    feature support (a `VkImageFormatProperties::sampleCounts`-computation
    gap, not a format-table gap), left as a follow-up rather than folded
    into this row.
  - 54 `format_properties.*` (unchanged from E24's own baseline -- this
    row's broadening does not touch `bufferFeatures`, the reason every
    one of these already failed).
  - 14 `get_physical_device_properties2.features`/1
    `vulkan1p2_limits_validation`/1 `get_physical_device_properties2.
    properties` (unchanged pre-existing gaps, same as E24's own report).
- **`dEQP-VK.*astc*`** (98,927 cases): 8,237 passed / 12,225 failed /
  78,465 not supported before this row (E24's own "after" figure). After:
  **8,349 passed / 12,113 failed / 78,465 not supported** -- +112 passing,
  -112 failing, "not supported" unchanged (no new format became
  recognized/rejected outright by this row). Roughly half the remaining
  failures (5,916 of 12,113) are still `feme-cpu-simdize`'s "divergent
  vector value ... used outside a supported ... pattern" limitation
  (roadmap milestone 7's own already-documented deviation, unrelated to
  per-format feature support and not touched by this row); the rest are
  the same mandatory-sampled-format and sample-count gaps the
  `api.info.*` breakdown above already accounts for, reached through a
  texture test rather than a capability-query one.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1646/1647
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row -- the new tests relative to E24's own report are
`ImageSamplingTest`'s nine new per-format `feme.cpu.image.load.2d.v4f32`
cases (one per format this row added to the CPU runtime's sample table),
`FormatTest`'s updated `FormatFeatureFlagsSampledImageMatchesRuntimeUnpackScope`
(now covering the broadened set), and `EntryPointsTest`'s
`FormatProperties2FillsChainedFormatProperties3` (the `VkFormatProperties3`
regression test above), plus a corrected expectation in
`EntryPointsTest.ImageFormatPropertiesRejectsUnsupportedUsage` (retargeted
from `R32_SFLOAT`, now sampled by this row, to an integer format, still
correctly rejected per roadmap E26).

## Roadmap E26: measured impact (targeted, not a full re-run)

E26 raised an integer 2D `OpImageFetch` to a new
`feme.cpu.image.load.2d.v4i32` entry point (`feme::cpu::ImageCalls`'
`ImageCallKind::Load2DI32`), backed by a new `femeRTUnpackImageTexelI32`
decode table (`FeMeRuntimeCPU.c`) covering the mandatory-sampled
`_UINT`/`_SINT` formats the Vulkan spec's own "Mandatory Format Support"
tables list (`R32G32B32A32_UINT`/`_SINT`, `R16G16B16A16_UINT`/`_SINT`,
`R8G8B8A8_UINT`/`_SINT`, `R10G10B10A2_UINT`), and `formatFeatureFlags`
(Format.cpp) now advertises `SAMPLED_IMAGE_BIT` (never
`_FILTER_LINEAR_BIT`) for exactly that set. The same two targeted subsets
E24/E25 used were re-run against this build, with E25's own final
"after" numbers as the baseline (E26 makes no version/extension/feature-
advertisement change of its own, so, per this report's established
practice, a revert-and-rerun re-confirmation was not repeated for that
unrelated portion of the diff):

- **`dEQP-VK.api.info.*`** (10,484 cases): 5,556 passed / 390 failed /
  4,538 not supported before this row (E25's own final "after" figure).
  After: **5,542 passed / 404 failed / 4,538 not supported** -- "not
  supported" exactly unchanged, +14 failing / -14 passing, all of it
  concentrated in `image_format_properties.{1d,2d,3d}.{optimal,linear}`
  (320 -> 334). Attributing precisely: exactly 42 of this bucket's cases
  newly fail for the 7 formats this row's own `formatFeatureFlags` change
  touches (`r8g8b8a8_{u,s}int`, `r16g16b16a16_{u,s}int`,
  `a2b10g10r10_uint_pack32`, `r32g32b32a32_{u,s}int`, each across six
  `{1d,2d,3d} x {optimal,linear}` variants), offset by 28 cases elsewhere
  in the same bucket that now pass instead (a `VkImageFormatProperties`
  query result that changed shape without becoming wholly disqualified,
  once these formats stopped being rejected outright before reaching any
  usage-flag-specific check). The 42 new fails are a **different**
  failure reason than E24/E25's own already-documented
  `image_format_properties` gap ("Required sample counts not supported"):
  every one of these instead fails with `VK_ERROR_FORMAT_NOT_SUPPORTED
  returned for required image parameter combination` (confirmed via
  `dEQP-VK.api.info.image_format_properties.2d.optimal.r8g8b8a8_uint`/
  `r32g32b32a32_sint`, both spot-checked in the raw log). Cross-checked
  against `vktApiFeatureInfo.cpp`'s own mandatory-format table
  (`{VK_FORMAT_R8G8B8A8_UINT, SAIM | BLSR | TRSR | TRDS | COAT | BLDS |
  STIM}`, similarly for the other six): every one of these formats is
  *also* mandatory `COAT` (color-attachment) and `STIM` (storage-image)
  per this CTS's own table, neither of which `formatFeatureFlags`
  advertises for *any* integer format (`RenderPass.cpp`'s
  `isSupportedColorAttachmentFormat` has no integer-format case, and
  `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` is deliberately never set for any
  format at all -- "V5: Images and sampling"'s own already-documented "not
  yet writable" deviation). So this test's own mandatory-combination
  check now runs (where it used to be rejected before reaching it) and
  correctly fails on two genuinely separate, already-known gaps neither
  this row nor E25 touch: integer-format color-attachment support (an
  extension of `isSupportedColorAttachmentFormat`'s own table, not this
  row's `feme.cpu.image.*` sampling path) and storage-image support in
  general (needs a `feme.cpu.image.store.*` runtime helper that does not
  exist yet, for any format). Both are left as their own follow-up rather
  than folded into this row, the same "verify before assuming a
  regression" practice E25's own `pNext` investigation established.
  `format_properties.*` (54, unchanged from E25's own baseline --
  `bufferFeatures` is untouched by this row) and the remaining
  `get_physical_device_properties2.features`/`vulkan1p2_limits_
  validation`/`get_physical_device_properties2.properties` cases (16,
  same pre-existing gaps E24/E25's own reports already carried forward)
  are exactly unchanged.
- **`dEQP-VK.*astc*`** (98,927 cases): **8,349 passed / 12,113 failed /
  78,465 not supported**, byte-for-byte identical to E25's own final
  figure -- expected and confirmed rather than assumed: no ASTC format is
  integer-channel, so this row's own scope (an integer-format sampling
  path) cannot touch any ASTC-format case, and the run above confirms it
  did not.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1655/1656
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row -- the new tests relative to E25's own report are
`SPIRVResourceLoweringTest`'s `LowersIntegerImageFetchToImageLoadV4I32`/
`LeavesAnIntegerSampledImageHandleUsedForSampleAlone`,
`ImageSamplingTest`'s six new `feme.cpu.image.load.2d.v4i32` cases (one
per newly-decoded format shape, plus out-of-range/inactive-lane zero-read
coverage), `FormatTest`'s updated
`FormatFeatureFlagsSampledImageMatchesRuntimeUnpackScope` (now covering
the 7 newly-sampled integer formats), and `EntryPointsTest`'s new
`ImageFormatPropertiesAcceptsMandatoryIntegerFormat` plus a corrected
comment on `ImageFormatPropertiesRejectsUnsupportedUsage` (the stale "no
`feme.cpu.image.*` entry point returns an integer vector" claim this
row's own work made false).
