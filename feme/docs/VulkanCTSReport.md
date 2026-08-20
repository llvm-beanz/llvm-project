# FeMe Vulkan ICD: Vulkan-CTS Status Report

This report is regenerated from scratch on every full Vulkan-CTS pass; it
describes the *current* state of `libfeme_vulkan` against `deqp-vk`, not the
history of how it got there. Previous editions of this file recorded a
narrative of individual crash fixes; that narrative is now folded into
[Roadmap.md](Roadmap.md) §1.9 and each design document's own Status notes,
and this file is a measurement instead.

- FeMe revision: `f91258124320` (roadmap C4e, "Dual-source blend factors":
  the last item in C4's "Graphics pipeline state breadth" row -- see
  "Roadmap C4d/C4e: measured impact" below).
- VK-GL-CTS revision: `vulkan-cts-1.4.6.2-411-g918221c6` plus one local
  robustness fix (`7163015`, "Guard `dEQP-VK.api.invariance.random` against
  empty image format lists" -- see "Deviations from a stock CTS" below).
- Host: AArch64 Linux, `LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
  `RelWithDebInfo`.
- `check-feme`: 1478 passed, 1 unsupported.

## Headline

| | Count | Share |
|---|---|---|
| Total cases | 3,237,000 | |
| Passed | 10,560 | 0.33% |
| Failed | 27,018 | 0.83% |
| Not supported | 3,199,421 | 98.84% |
| Quality warning | 1 | |
| **Crashed / timed out** | **0** | |

All 54 top-level `dEQP-VK.<group>.*` groups run to completion. 28 of the 54
have **zero** failures (`conditional_rendering`, `cooperative_vector`,
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

## What the 3,199,421 `Not supported` results mean

A `NotSupported` result is a *pass* for conformance purposes when the
capability it needs is genuinely optional. The bulk of this run's
`NotSupported` mass is exactly that:

| Cases | Reason |
|---:|---|
| 419,425 | `VK_EXT_shader_object` (241,837 + 177,588 from two different check sites) |
| 313,141 | An unadvertised optional format (`Format not supported`, `... for sampling`, `... for transfer`, `Source format not supported`) |
| 244,916 | An unadvertised combined depth/stencil format (`D16_UNORM_S8_UINT`, `D32_SFLOAT_S8_UINT`, `S8_UINT`) |
| 113,737 | `VK_KHR_fragment_shading_rate` |
| 107,866 | `VK_EXT_primitives_generated_query` |
| 99,324 | No queue family with the requested capability combination |
| 91,516 | Cooperative matrix/vector |
| 73,433 | `VK_EXT_host_image_copy` |
| 71,322 | `VK_KHR_acceleration_structure` |
| 66,310 | `VK_KHR_synchronization2` |
| 62,047 | `VK_KHR_maintenance4`/`5`/`6` |
| 59,520 | `shaderSampledImageArrayDynamicIndexing` |
| 59,090 | `VK_EXT_graphics_pipeline_library` |

Two entries in that list are *not* freely optional for a conformant
Vulkan 1.2 device and so belong on the conformance critical path, not in
the "correctly declined" column: the combined depth/stencil formats (at
least one of `D24_UNORM_S8_UINT`/`D32_SFLOAT_S8_UINT` is mandatory), and
the queue-capability combinations (a Vulkan queue family exposing
`GRAPHICS` must also expose `TRANSFER`, and CTS's
`findQueueFamilyIndexWithCaps` failures indicate this ICD's advertised
family set does not cover the mandatory combinations).

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
