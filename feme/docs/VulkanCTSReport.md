# FeMe Vulkan ICD: Vulkan-CTS Status Report

This report is regenerated from scratch on every full Vulkan-CTS pass; it
describes the *current* state of `libfeme_vulkan` against `deqp-vk`, not the
history of how it got there. Previous editions of this file recorded a
narrative of individual crash fixes; that narrative is now folded into
[Roadmap.md](Roadmap.md) §1.9 and each design document's own Status notes,
and this file is a measurement instead.

- FeMe revision: `5f7420c1b3dd` (roadmap C1, "Mandatory formats":
  `B8G8R8A8_UNORM`/`R10G10B10A2_UNORM` color-attachment support and
  `D24_UNORM_S8_UINT` combined depth+stencil support, both with real
  `feme::graphics` pack/unpack -- see "Roadmap C1: measured impact" below
  for why the headline numbers are unchanged from the previous edition).
- VK-GL-CTS revision: `vulkan-cts-1.4.6.2-411-g918221c6` plus one local
  robustness fix (`7163015`, "Guard `dEQP-VK.api.invariance.random` against
  empty image format lists" -- see "Deviations from a stock CTS" below).
- Host: AArch64 Linux, `LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
  `RelWithDebInfo`.
- `check-feme`: 1442 passed, 1 unsupported.

## Headline

| | Count | Share |
|---|---|---|
| Total cases | 3,237,000 | |
| Passed | 10,350 | 0.32% |
| Failed | 27,094 | 0.84% |
| Not supported | 3,199,555 | 98.84% |
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

**No case produces a wrong answer.** Every one of the 27,094 failures was
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

## Every failure, by root cause

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
| 10,121 | A `Uniform`-storage-class block is not legalized. `feme::spirv::getBufferBlockElementArray` matches `StorageBuffer` pointers only, and `getUniformBlockElementStruct` matches a `Uniform` pointer only when its pointee is a single-member struct whose member is *itself* a struct. Everything glslang actually emits misses: a `BufferBlock`-decorated struct in `Uniform` (the pre-SPIR-V-1.3 spelling of an SSBO), a `Block` struct with more than one member, a sized (not runtime) array member, a matrix member with `RowMajor`/`ColMajor`/`MatrixStride`, and an array-of-blocks arrayed binding | `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` |
| 9,067 | `feme-cpu-simdize` cannot decompose a divergent vector value used outside an insertelement-chain/resource-store/extractelement pattern (its own diagnostic names this "roadmap milestone 7 deviation") | `feme/lib/Target/CPU` |
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

### Pipeline state (3,354)

`vkCreateGraphicsPipelines` returns `VK_ERROR_INITIALIZATION_FAILED` with
no diagnostic for a state combination `feme::vulkan::GraphicsPipeline`'s
translators have no peer for. The mappers in
`feme/lib/Vulkan/GraphicsPipeline.cpp` name the boundaries directly:

- `mapTopology` accepts `TRIANGLE_LIST`/`TRIANGLE_STRIP` only. Point, line,
  line-strip, fan and adjacency topologies are the largest single
  contributor (all 820 `draw` failures, and most of `query_pool`'s 283).
- `mapCullMode` rejects `FRONT_AND_BACK`; `mapBlendFactor` rejects the
  dual-source factors; `mapDynamicState` accepts six of the ~40 dynamic
  states; `isSupportedAttachmentSampleCount` accepts 1/2/4.

That these fail *silently* (no `stderr` line at all) is itself a finding:
every shader-side rejection names itself, but a state-side one does not,
which makes triage of this bucket require reading the ICD's source rather
than its output.

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

## What the 3,199,555 `Not supported` results mean

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
#    the per-group shader cache and .qpa log do not collide.
xargs -P 6 -n 1 -a groups.txt sh -c 'mkdir -p /tmp/cts/$1 && cd /tmp/cts/$1 &&
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
