#FeMe Vulkan ICD : Vulkan - CTS Status Report

This report is regenerated from scratch on every full Vulkan - CTS pass;
it describes the *current *state of `libfeme_vulkan` against `deqp - vk`,
    not the history of how it got there.Previous editions of this file recorded
            a narrative of individual crash fixes; that narrative is now folded into
[Roadmap.md](Roadmap.md) §1.9 and each design document's own Status notes,
and this file is a measurement instead.

- FeMe revision: `10303c63fa33` (roadmap F3, the last functional change before
  this session's own docs-only compute-only-scope pass, commits
  `e9fadabf587a`-`6fb8b60c3cfc` -- none of those touch `libfeme_vulkan` or the
  CPU pipeline, so this run measures the same binary F3's own "measured
  impact" section already did). The headline table below is a fresh full
  54-group run against that revision, the first full re-run since roadmap
  E29 (see "Full run, roadmap E27/E28" and "Roadmap E29: measured impact"
  below for that edition's own numbers); F1-F3 in between only ran targeted
  subsets, per each of their own "measured impact" sections.
- `check-feme`: 1696 passed, 1 unsupported (ccache, assertions-enabled
  `RelWithDebInfo` build) as of this revision; up from E29's 1687 by every
  roadmap row's own new regression tests since (global-priority, subgroup
  rotate, float-controls diagnostics).
- VK-GL-CTS revision: `vulkan-cts-1.4.6.2-413-ge4b225a7d7cd2c53630f0de3f0912c4d33a816f2`,
  plus the same two local fixes D0's own edition already recorded (see
  "Deviations from a stock CTS" below).
- Host: AArch64 Linux, `LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
  `RelWithDebInfo`.

## Headline

This is a genuine full 54-group re-run (the same "every group, six at a
time, per-group crash isolation" methodology "Reproducing this report"
below describes), the third since D0's own headline above (the first,
superseded, is E27/E28's revision; the second, also superseded, is E29's --
see "Roadmap E29: measured impact" below for that edition's own numbers and
how this one differs). It is **not** directly comparable to E29's own
numbers by a large margin the way E29 was to D0's: only F1 (`VK_KHR_global_
priority`), F2 (`VK_KHR_shader_subgroup_rotate`) and F3 (rejecting two
unhonored `VK_KHR_shader_float_controls` execution modes) landed in between,
and each of their own "measured impact" sections already found little to no
headline-level movement (F1/F2 add a handful of real `Pass`es; F3 is a
diagnostic-only change these numbers do not yet exercise, per its own
section below). The totals below are consistent with that: they are within
noise of E29's own headline, not a fresh order-of-magnitude jump the way
E29 was of D0's.

| | Count | Share |
|---|---|---|
| Total cases | 3,234,014 (of 3,237,000 possible: see below) | |
| Passed | 36,759 | 1.14% |
| Failed | 144,753 | 4.47% |
| Not supported | 3,052,501 | 94.30% |
| Quality warning | 1 | |
| **Crashed / timed out** | **2 groups, 2,986 cases short (see below)** | |

52 of the 54 top-level `dEQP-VK.<group>.*` groups now run to completion --
down from 53 in the previous (E29) edition's own headline, because this run
found a **second**, previously unmeasured crashing group,
`synchronization2`, alongside the same still-open `api` crash E29 already
recorded:

| Group | Cases measured (of total) | Crash |
|---|---|---|
| `api` | 266,993 (of 267,222) | `SIGSEGV`, no diagnostic, `object_management.multithreaded_per_thread_resources.device` (pre-existing, unchanged since roadmap E29 -- same case, same signature) |
| `synchronization2` | 78,860 (of 81,617) | `SIGSEGV` in `timeline_semaphore.device_host.write_copy_buffer_to_image_read_copy_image_to_buffer.image_128x128_d16_unorm`, immediately after a run of `VK_ERROR_INITIALIZATION_FAILED` `Fail`s from sibling `write_copy_buffer_to_image*` cases in the same `device_host` timeline-semaphore group -- the same *family* of crash "Roadmap C1: measured impact" already attributed to core `synchronization`'s own `timeline_semaphore.device_host` group (a different exact case, `write_copy_buffer_read_copy_buffer.buffer_262144`, but the same suite exercising the same device-vs-host timeline-semaphore wait path through the `VK_KHR_synchronization2` entry points instead of the core ones), not a new mechanism. Left as a known, out-of-scope issue for a future crash-isolation pass, same as `api`'s. |

26 of the 54 groups have **zero** failures (`conditional_rendering`,
`cooperative_vector`, `data_graph`, `depth`, `descriptor_indexing`, `dgc`,
`drm_format_modifiers`, `fragment_shader_interlock`,
`fragment_shading_barycentric`, `fragment_shading_rate`, `geometry`,
`image_processing`, `mesh_shader`, `multiview`, `postmortem`,
`protected_memory`, `ray_query`, `ray_tracing_pipeline`, `reconvergence`,
`shader_object`, `sparse_resources`, `tensor`, `tessellation`,
`transform_feedback`, `video`, `wsi`) -- the same 26 as E29's own headline,
unchanged since none of F1-F3 touch any of them -- almost all of them
because the feature they cover is not advertised at all, which is the
correct, truthful outcome for this ICD's declared scope; a handful
(`shader_object`, `transform_feedback`) are large groups (243,853 and
133,719 cases respectively) cleanly rejected outright rather than genuinely
exercised.

**"Correct for this ICD's declared scope" still does not cover all 26.** The
declared scope is full Vulkan 1.4 conformance including graphics and ray
tracing (FeMeVulkanDesign.md's "Conformance Target"), so seven of those
groups -- `ray_query`, `ray_tracing_pipeline`, `mesh_shader`, `wsi`,
`tessellation`, `geometry` and `multiview`, 139,043 cases between them, an
unchanged count from E29's own measurement since none of F1-F3 touch
graphics or ray tracing -- are measured gaps rather than truthful
abstentions. See "Scope expansion: the graphics and ray-tracing baseline"
immediately below for the per-group totals and the reason each is
`NotSupported`. The remaining 19 (video, sparse residency, protected
memory, transform feedback, `shader_object`, ...) stay correct abstentions,
per Roadmap.md's Part 4.

**Every failure this table's own `Fail` count includes was, as far as this
run's own per-group logs show, a clean rejection or a genuinely wrong
result attributable to a real, named implementation gap** (a format/limit/
feature this ICD does not yet support, per the E/F-series rows above) --
**not** re-audited case-by-case for this edition, the same caveat E29's own
headline recorded, so that specific claim should be treated as inherited
from those rows' own individual audits rather than freshly re-verified
here.

## Scope expansion: the graphics and ray-tracing baseline

This section is not a roadmap row's "measured impact". It is the baseline
the *scope change* needs: [FeMeVulkanDesign.md](FeMeVulkanDesign.md)'s new
"Conformance Target" makes graphics and ray tracing part of the
conformance claim, which reclassifies a specific set of numbers in the
headline above. The 26 groups with zero failures are not all the same
kind of zero:

- a zero because every case *passes* is a result;
- a zero because every case is `NotSupported` for a capability that is a
  declared non-goal (video, sparse residency, protected memory) is also a
  result;
- a zero because every case is `NotSupported` for a capability now inside
  the scope is a **gap of exactly that size**, and used to read as neither.

Measured directly (this session, same ICD revision as the headline run,
each group run in isolation per "Reproducing this report"):

| Group | Cases | Pass | Fail | NotSupported | Dominant `NotSupported` reason |
|---|---:|---:|---:|---:|---|
| `ray_query` | 49,311 | 0 | 0 | 49,311 (100%) | `VK_KHR_acceleration_structure is not supported` (34,765), `VK_KHR_ray_query is not supported` (14,546) |
| `wsi` | 36,880 | 0 | 0 | 36,880 (100%) | `VK_KHR_surface is not supported` |
| `mesh_shader` | 28,044 | 0 | 0 | 28,044 (100%) | `VK_EXT_mesh_shader is not supported` (26,909), `VK_NV_mesh_shader` (1,123) |
| `ray_tracing_pipeline` | 22,656 | 0 | 0 | 22,656 (100%) | `VK_KHR_acceleration_structure is not supported` (21,817), `VK_KHR_ray_tracing_pipeline` (604) |
| `tessellation` | 1,114 | 0 | 0 | 1,114 (100%) | `Requested core feature is not supported: tessellationShader` (658), `Tessellation shader not supported` (456) |
| `multiview` | 838 | 0 | 0 | 838 (100%) | `VK_KHR_multiview is not supported` (766), `geometryShader` (72) |
| `geometry` | 200 | 0 | 0 | 200 (100%) | `Requested core feature is not supported: geometryShader` (200) |
| **total** | **139,043** | **0** | **0** | **139,043 (100%)** | |

**139,043 cases, none of which this ICD has ever executed a single
instruction of.** That is the number roadmap §1.9.7 (H-series) and
§1.9.8 (J-series) exist to move, and it is deliberately reported here as
a *baseline* rather than a failure count: none of these is a `Fail`
today, and each will become one before it becomes a `Pass`, exactly as
every E-series row's own measurement showed for the compute surface (the
headline's own note: "a rising failure count is largely the expected,
honest cost of advertising more capability").

Three details worth carrying into those rows rather than rediscovering:

- **`ray_query` is 2.2x `ray_tracing_pipeline`, and 70% of it is gated on
  `VK_KHR_acceleration_structure` alone.** The acceleration-structure row
  (J4) therefore unblocks more than either shader-side row, and inline ray
  query (J5) is both the smaller and the earlier of the two consumers —
  which is why §1.9.8 orders them J4 → J5 → J6 rather than by API surface
  size.
- **`multiview`'s cases are gated on the *extension name*, not the feature
  bit** (766 of 838 report `VK_KHR_multiview is not supported`), even
  though multiview is core since 1.1. Advertising the feature without
  adding the name to `getSupportedDeviceExtensions` would move nothing —
  the same "a `deqp-vk` case enables it by name regardless of
  `apiVersion`" pattern roadmap E3/E5/E6 hit, now predicted in advance for
  H2/K3 rather than found afterwards.
- **`tessellation`/`geometry` are small groups (1,114 and 200), and that is
  misleading.** Both stages also gate cases counted under `draw`,
  `pipeline`, `renderpasses` and `graphicsfuzz`, which this measurement
  does not attribute; H4/H5's own before/after must be measured across
  those groups too, not only the eponymous ones.

This measurement changes nothing in the headline table above: the seven
groups here are the same 100%-`NotSupported` groups it already counted,
re-run to establish per-group totals and reasons rather than to detect a
change. It also, deliberately, does not re-run the compute surface --
this change is documentation and generator-script only (no `lib/Vulkan`
source is touched), so the compute numbers cannot have moved, and
`check-feme` is 1676 passed / 1 unsupported (up from 1675 by the
extension-inventory generator's own new lit test).

## Full run, roadmap E27/E28: measured impact

A full 54-group sweep (this section) is what actually found both bugs --
neither was reachable by a targeted single-group run the way most E-series
rows above used, since both are hangs/crashes a narrower run would either
not have exercised or would have mistaken for one more `api`-style,
"unrelated" crash.

**E27** (`VK_REMAINING_ARRAY_LAYERS` infinite loop): the first full sweep
attempt hung indefinitely (30+ minutes of 100% CPU with zero log output)
on `dEQP-VK.api.copy_and_blit.copy_commands2.blit_image.simple_tests.
array.all_remaining_layers`, then, once excluded, on `...array.
not_all_remaining_layers` -- both from the `array_*remaining_layers`
family `maintenance5` (roadmap E5) legalizes. Before this fix, 538 cases
across the `api` group name `remaining_layers`; every one of them would
have hung the same way if reached (most are `buffer_to_image`/
`image_to_buffer` cases the `blit_image` ones above happened to reach
first, alphabetically). After the fix, the full `api` group case list
completes (modulo E28's own separate crash, below) with no hang anywhere
in the 538-case family.

**E28** (`SIGSEGV` copying a 2D-array image into/out of a 3D image): with
E27 fixed, the same `api` group re-run crashed instead of hung, on
`dEQP-VK.api.copy_and_blit.copy_commands2.image_to_image.3d_images.
2d_to_3d_whole`; `gdb` placed the fault inside `runCopyImage`'s own
`memcpy` (CommandBuffer.cpp:838, pre-fix), confirming it as a genuine
FeMe-side bug rather than an "unrelated" one the way D0's original edition
of this report described the (different, unrelated) `api`-group crash it
found. 22,806 cases in the `api` group's own filtered case list name
`2d_to_3d`/`3d_to_2d` (every one of them exercising this same code path);
after the fix, the specific crashing case above passes (`Pass
(CopiesAndBlitting test)`) under a direct, isolated re-run, and the full
`api` group re-run no longer crashes at this case (it instead runs to
completion past it, up to the separate, new `granularity.*` crash the
headline table above catalogs as an untriaged E29 finding).

Both fixes are covered by new, targeted unit tests (`ImageTest.
CopyBufferToImageWithRemainingArrayLayers`, `ImageTest.
CopyImageWithRemainingArrayLayers`, `ImageOpsTest.
BlitsWithRemainingArrayLayers`, `ImageOpsTest.
ResolvesWithRemainingArrayLayers` for E27; `ImageTest.
CopyImage2DArrayToImage3D` for E28), each confirmed to hang/crash without
its respective fix and pass with it; `check-feme` is 1675 passed/1
unsupported after both (up from 1670/1 before this session).

## Roadmap E29: measured impact

The same full run that found E27/E28 found six more distinct crashes, one
per remaining top-level group (`api`'s own second crash, reached once E27/
E28 stopped hiding it; `image`; `glsl`; `spirv_assembly`; `renderpasses`;
`compute`), plus a seventh (`synchronization`) that turned out not to
reproduce. Each of the six real ones is a genuine FeMe bug, independently
root-caused under `gdb`/`valgrind`/a targeted unit test and fixed in its
own commit (Roadmap.md's E29a-E29f); the seventh (E29g) is closed as an
unreproducible, one-off environmental flake instead, per that row's own
detail.

**Before** (this session's own starting point, the E27/E28 revision): 7 of
54 groups crashed partway through -- `api` (granularity), `compute` (zero-
initialize-workgroup-memory bools), `glsl` (texture-function sample-count
query), `image` (subresource layout), `renderpasses` (dynamic-rendering
blend/mask), `spirv_assembly` (`OpSelect` on arrays), and `synchronization`
(unreproducible, see E29g) -- 234,498 cases short of the full 3,237,000
per the headline table above.

**After** (this session's own final revision, E29a-E29g applied): a fresh
full 54-group sweep (this section's own run, same methodology) found only
one crash left, and it is a *different*, already-documented, out-of-scope
one: `api` now reaches `object_management.multithreaded_per_thread_
resources.device` (266,994 of 267,222 cases, 99.9% -- previously it
crashed at 208,840/267,222 on the granularity bug this session fixed) before
a `SIGSEGV` with no diagnostic, in a threading stress test unrelated to any
E29 file -- the exact same crash "Addendum: DXIL `GetDimensions.xy`/AMDGPU
change" above already found and declined to attribute to that addendum's
own (different, non-overlapping) file set. It is left for a future
session's own crash-isolation pass rather than folded into E29, which
never claimed it.

Every one of the other 53 groups now runs to completion, most for the
first time this report has ever recorded them doing so:

| Group | Cases (of total) | Crash before | Crash after |
|---|---:|---|---|
| `api` | 266,994 (of 267,222) | `SIGSEGV` in `granularity.in_dynamic_render_pass.*` | `SIGSEGV`, `object_management.multithreaded_per_thread_resources.device` (pre-existing, out of scope) |
| `compute` | 646,398 (of 646,398, `zero_initialize_workgroup_memory` subset: 646) | `GetElementPtrTypeIterator.h` assertion | none |
| `glsl` | 26,808 (of 26,808) | `SIGSEGV` in `texture_functions.query.texturesamples.*` | none |
| `image` | 142,991 (of 142,991) | `SIGSEGV` in `subresource_layout.*` | none |
| `renderpasses` | 80,880 (of 80,880) | `llvm::Value::setNameImpl` assertion | none |
| `spirv_assembly` | 68,734 (of 68,734) | `llvm_unreachable` in `ResourceCalls.cpp` | none |
| `synchronization` | 64,872 (of 64,872) | (did not reproduce -- see E29g) | none |

(`compute`'s own "cases (of total)" column measures its full 60,811-case
group, not just the 646-case subgroup the crash was in -- both are listed
since the crash's own case count is small enough that "of total" would
otherwise read as a typo.)

Summed across the 53 non-`api` groups this sweep's own totals cover: 7,538
passed, 128,839 failed, 2,833,400 not supported (2,969,778 cases) -- not
directly comparable to the headline table's own 3,002,485/27,313/126,457/
2,848,714 (which includes `api`'s own, different-revision numbers and
predates several of these fixes changing a small number of `Fail`/
`NotSupported` outcomes at the margin, e.g. `spirv_assembly`'s `array_
select` family moving from a crash to a clean `Fail`), but the shape is the
same: the overwhelming majority of the movement this session produced is
"crashed" becoming "ran to completion", not `Fail` becoming `Pass` --
exactly what every E29a-E29f row above claims and no more.

`check-feme` is 1687 passed / 1 unsupported after all six real fixes (up
from 1675/1 before this session), each fix covered by its own new unit
test or lit test, listed in its own Roadmap.md row.

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
  DecomposesVectorPHIAcrossUniformDiamond,
      DecomposesScalarConditionVectorSelect,
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
scissor_with_count}.{
  before, after}` now passes for real, having previously
been `NotSupported` outright because `vkEnumerateDeviceExtensionProperties`
never listed the extension a conformant `deqp-vk` checks for before
attempting any of them. This is the clearest possible confirmation that
the dynamic-state resolution added by C4c (`DynamicGraphicsState`,
`buildExecutorPipeline`) is not just unit-tested but reachable and correct
against an independent, real conformance client.

Four of the +63 newly-`Fail`ed cases are the same "stacked blockers"
pattern C1/C3/C4a/C4b's own sections already established:
`dynamic_state.monolithic.compute_transfer.single.{compute,transfer}.
vertex_input_binding_stride.{
  before, after}` now get far enough to attempt
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
  `dEQP-VK.rasterization.provoking_vertex.draw.default.{
  line_list, line_strip, triangle_fan, triangle_list, triangle_strip}` all fail
  pipeline creation with the same `feme-cpu-simdize` "divergent vector
  value... used outside a supported... pattern" diagnostic (roadmap C8's
  own bucket) regardless of topology -- confirming this is a stage-IO
  compilation gap the shader itself hits, not anything topology-specific.
  `dEQP-VK.pipeline.*.depth.format.d16_unorm.compare_ops.point_list_*`
  (and the equivalent for every other topology) are `NotSupported` on
  `VK_FORMAT_D16_UNORM` alone, before topology is ever considered. The one
  case that *does* reach real image comparison for a new topology,
  `dEQP-VK.pipeline.monolithic.input_assembly.primitive_restart.
  index_type_uint16.restart_disabled_{
  line_strip, triangle_fan}`, fails
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

## Roadmap C8a: measured impact

Roadmap C8a picks up the largest *named* row left in C8's own bucket after
the section above: "matrix/aggregate stage IO (309)". Reading
`CanonicalizeStage.cpp`'s SPIR-V-side rewrite confirmed the gap was real --
it only ever recognized a scalar or `FixedVectorType` `Input`/`Output`
global, so a matrix (`ArrayType` of column vectors) or a
single-member-struct-wrapped variant fell through to a wrong default
(`ComponentCount`/`RowCount` both 1, the whole aggregate treated as one
opaque "scalar" store) -- and fixed it: `getStageIORowShape`/
`peelSingleMemberStruct` now recognize both shapes, and `loadStageIOValue`/
`storeStageIOValue` recursively decompose the aggregate into one
`feme.stage.input.load`/`output.store` per (row, component). The CPU
executor's `buildStageStorage` (`feme/lib/Graphics/Executor.cpp`), which
had explicitly rejected any `RowCount != 1` element outright, now supports
it end to end.

**Finding the single-member-struct shape was itself only possible by
measuring against a real `deqp-vk` run, not by reasoning about SPIR-V's
type system in the abstract.** The plain-matrix fix alone (bare
`ArrayType`, no struct wrapper) passed every unit test written against it,
but running `dEQP-VK.glsl.linkage.varying.struct.mat4x2` against the built
ICD produced a *new* diagnostic shape (`'feme.stage.output.store' ... row 1
is out of range for element 2`) that a hand-written unit test would not
have anticipated. Dumping the actual imported global's LLVM type (a
temporary `FEME_DEBUG_STAGEIO` environment-variable trace, removed before
landing) showed why: glslang wraps a `varying`-block *member* -- even one
that is itself a matrix -- in an outer one-member struct
(`{
  [4 x<2 x float>] }` for a `mat4x2` member), a shape neither the
original matrix fix nor any of this row's own unit tests exercised.

**A second bug was caught only by a real end-to-end triangle-draw test,
not by the unit-level signature/IR tests.** The first version of the
`Executor.cpp` change passed every `CanonicalizeStageTest`/signature-level
unit test, but a dedicated `ExecutorTest.
InterpolatesConstantColorPackedInAMatrixVarying` regression test (a
fully-covered, constant-color triangle whose color is packed into a
`RowCount == 2` varying instead of a plain `float4`) failed: row 0 of the
matrix read back correctly, but row 1 read back as either zero or a
scrambled blend of unrelated values. The root cause was `lerpVertex`
(the Sutherland-Hodgman clip-time interpolation helper) -- unlike
`vertexAt`'s read loop and the fragment-side interpolation write loop,
which both take a `LinkedVarying` and could be made `Row`-aware directly,
`lerpVertex` flattens every varying's components into one linear `Idx`
without ever iterating `Row`, so a clipped vertex's row-1 components were
left at their zero-initialized default instead of being lerped. Since this
row's own test triangle deliberately exceeds the `[-1, 1]` NDC square (so
Sutherland-Hodgman clipping actually runs, producing synthetic lerped
vertices, not just the three original ones), this bug would not have been
caught by a test that only checked signature/IR shape, or one whose
triangle happened to need no clipping.

**Measured against a real `deqp-vk.glsl` run (26,808 cases, before/after
otherwise-identical builds):** every occurrence of the "aggregate type"
`feme-cpu-simdize` diagnostic (21) and the `feme-graphics-validate-stage`
component/row-out-of-range diagnostics this gap produced (42 pre-fix
occurrences of "out of range for element", one specific case's own three
manifestations of the same underlying bug) is gone -- 0 occurrences of
either in the post-fix log. The group's own `Passed`/`Failed`/
`Not supported` totals are byte-for-byte unchanged: **0/26,808 passed,
13,921/26,808 failed, 12,887/26,808 not supported**, identical before and
after (confirmed by diffing the full logs, not just the summary line).
Every one of the 63 affected occurrences (grep-counted, spanning multiple
`dEQP-VK.glsl.linkage.varying.struct.*` cases) now fails one step later
instead: the *same* case's log now shows `feme-cpu-simdize`'s pre-existing
"used outside a supported insertelement-chain/resource-store/
extractelement/select/shufflevector/phi/elementwise pattern" diagnostic
(42 post-fix occurrences, up from the 21 pre-fix "of aggregate type"
occurrences -- a matrix/aggregate value now reaches `SIMDizePass` as a
legalized `feme.stage.*` call, and *that* pass's own divergent-vector
decomposition, roadmap C3's scope, does not yet cover the `insertvalue`/
`extractvalue` chain shape this produces). This is the same
"legalized-but-still-blocked-downstream" outcome the "Roadmap C8: measured
impact" section above found for its own (different) stage-IO gap: a real,
correct, regression-tested fix to this row's own named scope, with zero
observable `dEQP-VK` movement because a distinct, already-tracked (C3) or
newly-named (roadmap C8b) blocker sits immediately downstream of it. See
Roadmap.md's new C8b row for that blocker, and roadmap H2a (which flagged
what looks like the same `feme-cpu-simdize` shape from a completely
different CTS group, `dEQP-VK.multiview`) for why the two should be
triaged together rather than assumed to be the same fix twice over.

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
| `api.info.*` | 18 | `dEQP-VK.api.info.vulkan1p3.{
  features, properties, feature_extensions_consistency}` and ten `get_physical_device_properties2.features.*_features` cases (`image_robustness`, `inline_uniform_block`, `maintenance4`, `pipeline_creation_cache_control`, `private_data`, `shader_demote_to_helper_invocation`, `shader_integer_dot_product`, `shader_terminate_invocation`, `subgroup_size_control`, `synchronization2`, `texture_compression_astc_hdr`, `vulkan13`, plus `vulkan1p3_limits_validation.max_inline_uniform_total_size`) -- exactly the "device_mandatory_features/vulkan1p3_consistency" shape D0's first draft guessed and discarded after checking the top-level `info` group alone (which only gained two new failures, as D0 recorded). It materializes instead in `api.info.*`, a separate subtree deqp-vk also uses for the same class of check; D0's report did not check that subtree. Root cause is D1's already-tracked finding that most of 1.3/1.4's mandatory feature bits are unimplemented, now caught by consistency checks that only run once `apiVersion >= 1.3` makes deqp-vk chain the aggregate `VkPhysicalDeviceVulkan13Features` blob alongside each feature's individual extension struct and compare them. |
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
existing 1-mask `Sync.{
  h, cpp}`/`CommandBuffer.cpp` model -- the same "new
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

## Roadmap E17: measured impact

Roadmap E17 (SPIR-V 1.6's `Nontemporal` image-operand bit) adds a
`hasExactImageOperands` helper alongside the existing `hasImageOperands`
in `SPIRVToLLVMPatterns.cpp`, both now masking out the `ImageOperands::
Nontemporal` bit (a pure cache hint with no defined effect on the
result) before checking whether anything else in the mask is
unsupported. `ImageFetchPattern`/`ImageReadPattern`/`ImageWritePattern`
(via `hasImageOperands`) and `ImageFetchLodPattern`/
`ImageSampleExplicitLodPattern` (via the new `hasExactImageOperands`)
all accept a lone `Nontemporal` bit, or `Lod|Nontemporal`, exactly as
they already accept `None` or a lone `Lod`.

**Targeted CTS run**, against this session's HEAD build, of every case
under `dEQP-VK.spirv_assembly.instruction.compute.*` (19,904 cases,
the group "Roadmap D3: measured impact" attributed its 422-case
`Nontemporal` bucket to), run twice: once against a build with this
row's own `SPIRVToLLVMPatterns.cpp` change reverted (`git stash`) and
once against this session's HEAD, both from the identical CTS build
and identical `feme-vulkan` ICD build flags (assertions + ccache):

| | Passed | Failed | Not supported |
|---|---:|---:|---:|
| Before this row | 264 | 1,387 | 18,253 |
| After this row | 264 | 1,387 | 18,253 |

Identical totals, and a full per-case `Fail`-set diff between the two
runs is empty -- **zero cases flip from `Fail` to `Pass`**, unlike
every other closed row in this document. Tracing why, rather than
stopping at the headline number:

- 351 of the 1,387 `Fail` cases (183 under `imagefetchlod`/explicit-LOD
  sampling, i.e. `spirv.ImageSampleExplicitLod`; 126 under
  `imagefetch`, i.e. `spirv.ImageFetch`; 42 under `imageread`, i.e.
  `spirv.ImageRead`) fail *before* this row's fix with exactly the
  `ImageOperands::Nontemporal`-mask legalization error this row's own
  roadmap text describes (`failed to legalize operation 'spirv.
  ImageFetch'/'spirv.ImageSampleExplicitLod'/'spirv.ImageRead' that was
  explicitly marked illegal ... image_operands = #spirv.image_
  operands<Nontemporal>` or `<Lod|Nontemporal>`). *After* this row's
  fix, every one of those 351 legalizes past that point cleanly -- the
  `Nontemporal`-specific error is gone from all of them -- but every
  one still fails `vkCreateComputePipelines`, now on a different,
  pre-existing error surfacing later in the same lowering:
  `'llvm.getelementptr' op operand #0 must be LLVM pointer type or LLVM
  dialect-compatible vector of LLVM pointer type, but got
  'vector<3xi32>'`. This is not something this row's fix introduces:
  the exact same error, at the exact same case shape, reproduces
  identically on the *non*-`nontemporal`-suffixed sibling case in the
  same log (e.g. `...combined_image_sampler_separate_descriptors.
  all_local_variables.depth_property.depth`, no `_nontemporal` suffix,
  already `Fail` with this same `getelementptr` error before this row's
  change touched anything) -- an address-computation bug in the
  `combined_image_sampler_separate_descriptors` shader variant that
  long predates this row and is untouched by it, now visible in these
  351 cases only because this row's fix cleared the *earlier*,
  `Nontemporal`-specific error that used to mask it.
- The remaining 3 (`memory_access.{nontemporal,aligned_nontemporal,
  aligned_volatile}`) fail on a different, unrelated bit this row never
  touched: `spirv.CopyMemory`'s own `memory_access` operand, spelled
  with `MemoryAccess::Nontemporal` (SPIRVBase.td's `SPIRV_MA_
  Nontemporal`, bit 2) rather than `ImageOperands::Nontemporal`
  (`SPIRV_IO_Nontemporal`, bit 14) -- a same-named but distinct bit in a
  different SPIR-V bit-enum, on an op none of this row's three named
  patterns (or `ImageReadPattern`/`ImageWritePattern`) handle at all.
  These 3 fail identically before and after this row's change, for a
  reason outside its scope.

**This row's fix is real and correctly scoped** -- confirmed directly
by the disappearance of the `ImageOperands::Nontemporal` legalization
error from all 351 relevant cases, and by the three targeted `.mlir`
lit tests added alongside it (`fetch_level_nontemporal`,
`sample_level_nontemporal`, `read_write_nontemporal`, plus a negative
`spirv-to-llvm-image-access-invalid.mlir` case confirming
`Bias|Nontemporal` still correctly fails to legalize, since `Bias`
itself has no pattern here) -- but its measured CTS payoff in this
tree's *current* state is nil, entirely masked by the pre-existing
`combined_image_sampler_separate_descriptors` address-computation bug
above. That bug is a real, newly-surfaced (to this measurement, not
newly-introduced) gap, left open as follow-up work for a future
roadmap row rather than fixed here, being outside `SPIRVToLLVMPatterns.
cpp`'s `ImageFetchPattern`/`ImageFetchLodPattern`/
`ImageSampleExplicitLodPattern`/`ImageReadPattern`/`ImageWritePattern`
scope this row's own text names.

A broader regression check, `check-feme` (1,661 passed, 1 unsupported),
confirms no regression from this row's change.

## Roadmap E18: measured impact

Roadmap E18 had two independent halves: tracing D3's `robustness.
oob_access` 6-case bucket to a specific line before fixing it, and
raising `VK_EXT_texel_buffer_alignment`'s two limit fields from E2's
placeholder `0`/`VK_FALSE`.

**First half -- the `robustness.oob_access` trace.** Re-running the
exact repro D3 named (`rba_texel_buffer_uniform_r32_uint_*`, the format/
usage combination its own text points at) against this session's HEAD
build finds it does not reproduce: every one of those 12 cases reports
`NotSupported ("Format not supported for uniform texel buffers")`, the
same *pre*-D0 result D3's own text describes, not the post-D0 `Fail`
it named. Widening the run to the entire `dEQP-VK.robustness.
oob_access.*` group (121 cases, not just the 6 D3 named) confirms this
is not a narrower fluke of the one format re-tested:

| | Passed | Failed | Not supported |
|---|---:|---:|---:|
| `dEQP-VK.robustness.oob_access.*` | 1 | 0 | 120 |

Zero `Fail`. Reading `Buffer.cpp`'s `vkCreateBufferView` and
`EntryPoints.cpp`'s `vkGetPhysicalDeviceFormatProperties` explains why:
both already gate on the exact same `feme::vulkan::
isTexelBufferFormatSupported` predicate (`Format.cpp`) -- one directly,
one through `formatFeatureFlags`'s `bufferFeatures` computation. That
sharing did not exist when D3 made its own pass; it is a side effect of
roadmap E24/E25 landing afterward (`formatFeatureFlags`'s introduction
and its wiring into `vkGetPhysicalDeviceFormatProperties`/`
Properties2`). The two call sites cannot disagree by construction now,
so the "format/robustness mismatch" this row's own text names has
already been closed as an unintended side effect of unrelated work --
the same "traced, and does not survive re-verification" outcome D3
itself recorded for two of its own five other buckets (`ubo.*.std430`,
`synchronization.op.*`). No further code change was needed or made for
this half.

**Second half -- the two limit fields.** `storageTexelBufferOffset
AlignmentBytes`/`uniformTexelBufferOffsetAlignmentBytes` (both the
aggregate `VkPhysicalDeviceVulkan13Properties` case and a new dedicated
`VkPhysicalDeviceTexelBufferAlignmentProperties` case, `EntryPoints.
cpp`) are now the real `minTexelBufferOffsetAlignment` (256):
`vkCreateBufferView` never enforces anything stricter, and the CPU
runtime's typed texel-buffer load/store helpers
(`femeCpuResourceLoadTypedV4*`/`StoreTypedV4*`, `FeMeRuntimeCPU.c`)
read and write through `__builtin_memcpy`, which has no alignment
requirement of its own -- so both `SingleTexelAlignment` companions are
truthfully `VK_TRUE` too. `VK_EXT_texel_buffer_alignment` is now listed
in `getSupportedDeviceExtensions` (`PhysicalDeviceInfo.cpp`).

**This second half's first draft was not complete, and a targeted CTS
run is why a follow-up commit was needed.** That draft added only the
properties-side plumbing above; running
`dEQP-VK.*texel_buffer_alignment*` against it found:

| | Passed | Failed |
|---|---:|---:|
| `dEQP-VK.api.info.get_physical_device_properties2.features.texel_buffer_alignment_features_ext` | `Fail` (`"Mismatch between VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT"`) |
| `dEQP-VK.api.device_init.create_device_unsupported_features.texel_buffer_alignment_features_ext` | `Fail` (`"Enabling unsupported features didn't return VK_ERROR_FEATURE_NOT_PRESENT"`) |

Unlike every other extension this document tracks, `VK_EXT_
texel_buffer_alignment` promoted only its *properties* struct to core
1.3 (per the Vulkan specification); its features struct,
`VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT`, stays extension-only
with no aggregate `VkPhysicalDeviceVulkan13Features` field to read
through instead, and `vkGetPhysicalDeviceFeatures2` had no case for it
at all. A follow-up commit added one, `texelBufferAlignment =
VK_TRUE` unconditionally (this ICD needs no more than the dedicated
properties case above already promises), closing both cases:

| | Before | After |
|---|---|---|
| `dEQP-VK.*texel_buffer_alignment*` (2 cases) | 2 `Fail` | 2 `Pass` |
| `dEQP-VK.api.info.vulkan1p3.property_extensions_consistency` | `Pass` | `Pass` (unaffected -- agrees with the dedicated struct, exactly as E2's own note anticipated) |

A full `dEQP-VK.api.info.*` re-run (10,484 cases) after both commits
shows the same 404 `Fail` total as before this row, with none of them
naming `texel`/`alignment` -- this row's own scope closes cleanly with
no regression elsewhere in that group. A broader regression check,
`check-feme` (1,663 passed, 1 unsupported), confirms no regression from
either commit.

## Roadmap E19: measured impact (targeted, not a full re-run)

E19 closed four of its six named items (`VK_EXT_4444_formats`,
`VK_EXT_pipeline_creation_feedback`, `VK_KHR_shader_non_semantic_info`,
`VK_EXT_tooling_info`; `VK_KHR_format_feature_flags2` was already closed
by roadmap E25 -- see that row's own inline note) and declined the sixth
(`VK_EXT_ycbcr_2plane_444_formats`, per this row's own "decline if YCbCr
sampling itself is unimplemented" instruction: `samplerYcbcrConversion`
is unconditionally `VK_FALSE` and no multi-planar/YCbCr sampler support
exists anywhere in this ICD). Each closed item is small and independent,
so this section runs one targeted group per item rather than a full
re-run, the same discipline E16 used.

**`dEQP-VK.api.info.*` (10,484 cases, full group)**: 5,542 `Pass` / 404
`Fail` / 4,538 `NotSupported` -- the identical 404-`Fail` total E18's own
re-run recorded, confirming no regression from this row's four commits.
`dEQP-VK.api.info.get_physical_device_properties2.features.
4444_formats_features_ext` (new, unlisted at E18's own pass since the
extension did not exist yet) is `Pass`, and every `vulkan1p3.*`
consistency case (`feature_bits_influence`, `feature_extensions_
consistency`, `features`, `properties`, `property_extensions_
consistency`) stays `Pass`, confirming the new `VkPhysicalDevice
4444FormatsFeaturesEXT` case agrees with `Format.cpp`'s two new
`mapVkFormat` entries.

**`dEQP-VK.api.tooling_info.*` (2 cases, full group)**:
`validate_getter`/`validate_tools_properties` both `Pass` -- the new
`vkGetPhysicalDeviceToolProperties` (reporting zero tools) satisfies both
the query-succeeds and structural-validation checks.

**`dEQP-VK.pipeline.monolithic.creation_feedback.*` (13 cases, full
group)**: 3 `Pass` (`compute_tests.compute_stage`/
`compute_stage_delayed_destroy`/`compute_stage_no_cache`, directly
exercising `fillPipelineCreationFeedback`'s compute path), 4 `Fail`, 6
`NotSupported` (`geometryShader`/`tessellationShader` not implemented,
pre-existing and unrelated). All 4 failures are the identical, already-
documented `feme-cpu-simdize` "divergent vector value ... used outside a
supported ... pattern" error (roadmap milestone 7's own deviation,
D3/E16's "Roadmap D3: measured impact" own regression bucket) on every
`graphics_tests.vertex_stage_fragment_stage*` case -- the vertex/fragment
shader pair these tests share fails to compile at all, before
`vkCreateGraphicsPipelines` ever reaches this row's own
`fillPipelineCreationFeedback` call, so none of the four are attributable
to this row.

**`dEQP-VK.spirv_assembly.instruction.compute.non_semantic_info.*` (8
cases, full group)**: 4 `Pass` (`basic`, `dummy_instruction_set`,
`large_instruction_number`, `many_parameters`) -- **before this row's own
fix, all 8 of this group's cases fail identically** with MLIR's
`dispatchToExtensionSetAutogenDeserialization`'s "unhandled deserialization
of extended instruction set" error at `vkCreateShaderModule`/pipeline-
creation time (reproduced directly: a hand-assembled `OpExtInst` into a
`NonSemantic.DebugPrintf` set fails pre-fix and succeeds post-fix, the
`SPIRVImporterTest.StripsNonSemanticExtInst` unit test added by this row).
The remaining 4 failures are on three *different*, unrelated, pre-existing
gaps this row's own `stripNonSemanticExtInst` scope never touches:
`any_constant_type`/`any_constant_type_used` fail on `"OpConstantComposite
component <id> 30 must come from a normal constant"` (an MLIR SPIR-V
deserializer constant-folding check, unrelated to which extended-
instruction-set a `NonSemantic.*` `OpExtInst` operand belongs to);
`any_non_constant_type` fails to legalize a `Function`-storage-class
struct containing a `vector<3xf32>` member (`SPIRVToLLVM`'s own struct/
vector conversion, independent of this row's scope); `placement` fails on
`feme-cpu-simdize`'s "unsupported divergent call" (the same milestone-7
deviation the pipeline-creation-feedback failures above hit). None of the
four re-produce the "unhandled deserialization of extended instruction
set" error this row's own fix targets, confirming the fix's own scope is
fully closed and these four are pre-existing, independently-tracked gaps.

**`dEQP-VK.api.buffer_view.access.{uniform_storage_texel_buffer.
bind_as_uniform,uniform_texel_buffer}.{a4r4g4b4,a4b4g4r4}_unorm_pack16`
(4 cases)** and **`dEQP-VK.api.copy_and_blit.copy_commands2.blit_image.
all_formats.color.2d.a4r4g4b4_unorm_pack16.*` (30 cases)**: all
`NotSupported` (`"Format not supported"`/`"VK_FORMAT_FEATURE_STORAGE_
TEXEL_BUFFER_BIT not supported"`/`"Destination format not supported"`),
confirming the two new formats are correctly left out of
`isTexelBufferFormatSupported` and every sampled-image/color-attachment
`VkFormatFeatureFlags` bit -- exactly this row's own "recognized but not
yet backed by a pack/unpack case" scope, with no crash on either format
in either group.

`check-feme` (1,670 passed, 1 unsupported): no regression from any of
this row's commits.

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

§1.9.1 was written for a compute-only device and a 1.2 claim. The plan has
since grown four more series against the current, wider scope: §1.9.4 (E,
the 1.3 floor), §1.9.5 (F, the 1.4 floor), §1.9.7 (H, the graphics
surface), §1.9.8 (J, ray tracing) and §1.9.10 (K, the 1.1/1.2 floor an
apiVersion 1.4 claim inherits). §1.9.9 is the five-part definition of done
all of them are measured against, and it starts with this report.

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

## Roadmap E16: measured impact (targeted, not a full re-run)

E16 closed a host-side (not shader-side) `VK_EXT_image_robustness`/
`robustImageAccess` gap: `Image::texelPointer`/`Image::blockPointer`
(Image.{h,cpp}) now return null for any out-of-bounds mip level/array
layer/texel or block coordinate instead of computing a wild pointer, and
`ImageOps.cpp`'s `runBlitImage`/`runResolveImage` -- the only two
operations whose region comes from arbitrary application input rather
than the image's own dimensions -- now clamp an out-of-bounds source read
into the mip's real extent and discard an out-of-bounds destination
write, rather than faulting. This is a pure robustness fix: every
in-bounds region behaves identically to before, so the expectation going
in was zero headline movement, confirmed rather than assumed below.

Targeted subset: **`dEQP-VK.api.copy_and_blit.copy_commands2.blit_image.*`**
and **`dEQP-VK.api.copy_and_blit.copy_commands2.resolve_image.*`**, the two
groups exercising `runBlitImage`/`runResolveImage` directly.

- **`blit_image.*`**: excluding `*all_remaining_layers*`/
  `*layercount_6*` (see "A real, pre-existing hang found, and
  deliberately left open" below), 4,388 cases: 179 passed / 30 failed /
  4,179 not supported, both before and after this row's own commits
  (confirmed with a temporary revert of `Image.{h,cpp}`/`ImageOps.cpp` to
  their pre-E16 state, rebuilt in place, then restored -- not just
  assumed from the diff's shape). The 30 failures are all `_linear`-filter
  1D-image cases (`b10g11r11_ufloat_pack32`, `r32_sfloat`,
  `r32g32_sfloat`, `r32g32b32_sfloat`, `r8g8b8a8_snorm`), each failing
  identically before and after at `vk.queueSubmit(...)
  VK_ERROR_INITIALIZATION_FAILED` (`vkCmdUtil.cpp:338`) -- a pre-existing,
  unrelated gap (1D bilinear blit dispatch), not touched by this row.
- **`resolve_image.*`** (138 cases): 0 passed / 33 failed / 105 not
  supported. None of the 33 failures reach `runResolveImage` at all: every
  one fails earlier, at `vkCreateGraphicsPipelines`, with `feme-cpu-simdize`'s
  already-documented "divergent vector value ... used outside a supported
  ... pattern" limitation (roadmap milestone 7's own deviation) --
  `resolve_image`'s own CTS cases resolve through a render-pass-driven
  multisample draw, not a standalone `vkCmdResolveImage` call, so this
  group provides no direct coverage of this row's own `runResolveImage`
  change either way. The 105 not-supported cases are gated on
  `fragmentStoresAndAtomics`/an unsupported sample count, both pre-existing
  and unrelated. Direct coverage of `runResolveImage`'s own clamp/discard
  path is therefore the five new `ImageOpsTest`/`ImageTest` cases below,
  not this CTS group.

**A real, pre-existing hang was found, and deliberately left open, as
out of this row's own scope.** `blit_image.simple_tests.array.
all_remaining_layers`/`not_all_remaining_layers` (and every
`layercount_6` case, the same shape at a smaller scale) set
`VkImageSubresourceLayers::layerCount` to `VK_REMAINING_ARRAY_LAYERS`
(`VK_KHR_maintenance5`), per the Vulkan spec's own "all layers from
`baseArrayLayer` to the end" meaning for that sentinel in this struct.
`runBlitImage`/`runResolveImage`'s `LayerCount = std::min(srcSubresource.
layerCount, dstSubresource.layerCount)` does not recognize the sentinel
at all, so `LayerCount` becomes `0xFFFFFFFF` and the per-layer loop runs
essentially forever (confirmed: still running after minutes of 100% CPU,
both with a `--deqp-watchdog` -- which cannot preempt a single, synchronous
in-process `vkQueueSubmit` call -- and, via a temporary revert exactly as
above, with this row's own commits entirely absent, so this predates E16
rather than being introduced by it). This is a distinct bug from the one
this row closes (an unbounded loop count, not an out-of-bounds
coordinate) and squarely outside `{Image,ImageOps}.cpp`'s own
`VK_REMAINING_ARRAY_LAYERS`-oblivious `LayerCount` computation this row
never touched -- tracked here as a finding for a future row (recognizing
the sentinel the same way `runClearColorImage`/`runClearDepthStencilImage`
already do for `VK_REMAINING_MIP_LEVELS`/`VK_REMAINING_ARRAY_LAYERS` in a
`VkImageSubresourceRange`) rather than folded into this one.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1660/1661
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row -- the five new tests relative to E26's own report are
`ImageTest`'s `TexelPointerReturnsNullOutOfBounds`/
`BlockPointerReturnsNullOutOfBounds` and `ImageOpsTest`'s
`BlitClampsOutOfBoundsSourceRegion`/
`BlitDiscardsOutOfBoundsDestinationTexels`/`ResolveDiscardsOutOfBoundsRegion`.

## Roadmap F1: measured impact (targeted, not a full re-run)

F1 advertised `VK_KHR_global_priority`, flipped `globalPriorityQuery` to
`VK_TRUE` (both the aggregate `VkPhysicalDeviceVulkan14Features` struct and
a new dedicated `VkPhysicalDeviceGlobalPriorityQueryFeatures` struct), and
filled `VkQueueFamilyGlobalPriorityProperties` with the full mandatory
priority list for every queue family; `vkCreateDevice` already ignored an
unrecognized `pQueueCreateInfos[i].pNext` entry, so the create-time hint
was already a no-op.

Targeted subset: **`dEQP-VK.api.device_init.*`** (246 cases, the group
this row's `vkCreateDevice`/query-feature change is most directly
reachable from) and **`dEQP-VK.synchronization.global_priority_transition.*`**
(396 cases, the group that actually creates queues at each priority level
and exercises priority-based preemption).

- **`api.device_init.*`**: 231 passed / 4 failed / 11 not supported. The
  four `create_device_global_priority`/`create_device_global_priority_query`
  cases' *KHR-suffixed* variants (`create_device_global_priority_khr`,
  `create_device_global_priority_query_khr`) both now `Pass`; their
  *EXT-suffixed* siblings (`create_device_global_priority`,
  `create_device_global_priority_query`) remain `NotSupported
  (VK_EXT_global_priority[_query] is not supported)`, correctly -- this
  row's own scope is `VK_KHR_global_priority` only (per the roadmap row's
  own title), and neither `VK_EXT_global_priority` nor
  `VK_EXT_global_priority_query` is advertised.
  `create_device_unsupported_features.global_priority_query_features`
  (requests `globalPriorityQuery = VK_TRUE` with every other 1.4 feature
  left `VK_FALSE`, expecting success) also `Pass`es. The 4 failures
  (`create_device_unsupported_features.{core,vulkan11_features,
  vulkan12_features,vulkan13_features}`) are a pre-existing, unrelated
  gap: `vkCreateDevice` does not validate a requested feature against
  `vkGetPhysicalDeviceFeatures2`'s own advertised set at all (e.g.
  `fullDrawIndexUint32`, a 1.0 feature `create_device_unsupported_features.
  core` deliberately requests despite it being unsupported, expecting
  `VK_ERROR_FEATURE_NOT_PRESENT`), a gap this row's own 1.4-scoped change
  neither introduces nor is positioned to close.
- **`synchronization.global_priority_transition.*`**: 48 passed / 252
  failed / 96 not supported. The 96 not-supported cases are the
  `VK_EXT_global_priority_query`-gated `GPQCase` variants (out of this
  row's scope, same reasoning as `api.device_init.*` above). Every one of
  the 252 failures is a pipeline-creation failure
  (`vk.create{Compute,Graphics}Pipelines(...): VK_ERROR_INITIALIZATION_
  FAILED`) in the `VK_KHR_global_priority`-gated `PreemptionCase` variants,
  reached only because this row's own extension/feature/property changes
  now let them run at all; every one traces to a pre-existing, unrelated
  shader-lowering gap already documented elsewhere in this report --
  `feme-cpu-simdize`'s "divergent vector value ... used outside a
  supported ... pattern" limitation (roadmap milestone 7's own deviation,
  the same one "Roadmap E16: measured impact" above found blocking
  `resolve_image.*`) for the graphics-pipeline failures, and an
  `'llvm.getelementptr' op operand #0 must be LLVM pointer type ...` (a
  `vector<3xi32>` local-invocation-ID value reaching a GEP base operand
  that expects a pointer) for the compute-pipeline ones -- neither is
  reachable from, or fixable within, this row's own `PhysicalDeviceInfo.
  cpp`/`EntryPoints.cpp` scope. The 48 passes are exactly the
  `PreemptionCase` variants whose shaders happen not to trigger either
  gap, confirming the priority-transition mechanism this row implements
  (advertise the full list, accept every priority at `vkCreateDevice`)
  behaves correctly once a test actually reaches it.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1690/1691
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row -- four new tests relative to E29's own report:
`PhysicalDeviceInfoTest`'s
`GlobalPriorityQueryIsAdvertisedThroughItsOwnDedicatedFeatureStruct`/
`EveryQueueFamilyReportsTheFullMandatoryGlobalPriorityList` and
`DrawTest`'s `AdvertisesDynamicRenderingExtension` (extended)/
`GlobalPriorityCreateInfoIsANoOpAtDeviceCreation`.

## Roadmap F2: measured impact (targeted, not a full re-run)

F2 gave `spirv.GroupNonUniformRotateKHR` a real `spirv`->`llvm` conversion
pattern (`RotateConversionPattern`, `SPIRVToLLVMPatterns.cpp`) and
advertised `VK_KHR_shader_subgroup_rotate`/`shaderSubgroupRotate`/
`shaderSubgroupRotateClustered`. Verified first with FileCheck lit tests
(`spirv-to-llvm-rotate.mlir`/`spirv-to-llvm-rotate-invalid.mlir`), which
confirm the conversion itself is correct in isolation for both the plain
and `cluster_size` forms, and that `Workgroup`-scope rotate (not
implemented) is declined rather than silently miscompiled.

Targeted subset: **`dEQP-VK.subgroups.shuffle.*rotate*`** (2,732 cases,
every `subgroupRotate`/`subgroupClusteredRotate` case across every stage
and value type this row's own feature bits gate).

- **0 passed / 128 failed / 2,604 not supported.** All 128 failures are
  `compute`-stage cases (framebuffer/graphics/mesh/ray_tracing are all
  `NotSupported` for unrelated, pre-existing reasons -- this ICD's
  subgroup support is compute-only, per `FeMeVulkanDesign.md`'s
  "Subgroup size" section), and every one of the 128 -- across every
  value type tested, not just `bool`/`bvec` -- fails identically at
  `vkCreateComputePipelines` with `error: unhandled opcode 341`
  (`OpGroupNonUniformBallotBitExtract`) from MLIR's own SPIR-V
  deserializer (`mlir-tblgen`-generated, `SPIRVUtilsGen.cpp`'s "unhandled
  opcode" case; the op itself is not modeled in MLIR's SPIR-V dialect at
  all, unlike `spirv.GroupNonUniformRotateKHR` which already existed
  there). This is `glslang`'s own shared "active invocation mask" helper
  -- used by every subgroup compute shader in this CTS module regardless
  of which specific op is under test, not something `subgroupRotate`
  itself requires -- confirmed by reproducing the identical failure on an
  entirely unrelated, pre-existing case
  (`dEQP-VK.subgroups.basic.compute.subgroupbarrier`, which needs no
  rotate/ballot functionality at all, fails at the same
  `vkCreateComputePipelines` step for its own unrelated pre-existing
  reason -- an `OpTypeArray` spec-constant-count gap). In other words:
  `dEQP-VK.subgroups.*.compute.*` was not a passing category before this
  row, and F2 does not regress a previously-passing one -- it makes 128
  previously-`NotSupported` cases newly reach (and fail at) a
  pre-existing, unrelated import gap, exactly the same shape "Roadmap F1:
  measured impact" above found for `synchronization.
  global_priority_transition.*`'s `PreemptionCase` failures. Per this
  row's own audit (`Vulkan14FeatureInventory.md`'s new F2 finding),
  `OpGroupNonUniformBallotBitExtract` is one of the ~30 other
  `GroupNonUniform*` ops left unconverted; closing it (which needs a new
  MLIR SPIR-V dialect op added upstream first, since deserialization
  itself fails before any `feme` conversion pattern ever runs) is tracked
  as future work rather than folded into this row.
- The 2,604 not-supported cases are a mix of expected gates unrelated to
  this row: `mesh`/`ray_tracing`-stage cases (no mesh shader/ray tracing
  pipeline support), `graphics`/`framebuffer`-stage cases (no fragment- or
  vertex-stage subgroup support advertised), `int64`/`float64`-typed
  cases (no 64-bit subgroup type support), and the
  `_requiredsubgroupsize` compute variants whose requested size the
  device does not report supporting.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1693/1694
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row -- new tests relative to F1's own report: three new
`spirv-to-llvm-rotate*.mlir` FileCheck tests,
`PhysicalDeviceInfoTest`'s
`ShaderSubgroupRotateIsAdvertisedThroughItsOwnDedicatedFeatureStruct`, and
`DrawTest`'s `AdvertisesDynamicRenderingExtension` (extended).

## Roadmap F3: measured impact (targeted, not a full re-run)

F3 audited `VK_KHR_shader_float_controls`'s per-module execution modes
(`DenormPreserve`/`DenormFlushToZero`/`SignedZeroInfNanPreserve`/
`RoundingModeRTE`/`RoundingModeRTZ`) and found `ExecutionModePattern`
(`SPIRVToLLVMPatterns.cpp`) unconditionally erased every one of them with
no diagnostic. Since no FP op conversion pattern ever sets fast-math
flags or a non-default rounding mode, `DenormPreserve`/`RoundingModeRTE`/
`SignedZeroInfNanPreserve` already describe the code this conversion
always produces and are still silently accepted; `collectEntryPoints`
(`ConvertSPIRVToLLVMPass.cpp`) now instead rejects `DenormFlushToZero`/
`RoundingModeRTZ` with a hard diagnostic, since nothing downstream can
produce flushed-denormal or truncating-rounding code. Verified first with
FileCheck lit tests (`spirv-to-llvm-float-controls.mlir` for the three
accepted modes at every declared bit width,
`spirv-to-llvm-denorm-flush-to-zero-invalid.mlir`/
`spirv-to-llvm-rounding-mode-rtz-invalid.mlir` for the two rejected ones).
`VK_KHR_shader_float_controls`/`VK_KHR_shader_float_controls2` remain
unadvertised (see `Vulkan14FeatureInventory.md`'s updated rows and
roadmap F15).

Targeted subset: **`dEQP-VK.spirv_assembly.instruction.compute.float_controls.*`**
(2,569 cases; the only group this session's case-list regeneration found
under `float_control`/`float_controls`, alongside two unrelated
`api.info`/`api.device_init` bit-advertisement cases this row's `EntryPoints.cpp`
changes do not touch).

- **1 passed / 0 failed / 2,568 not supported**, both before and after
  this row's commits -- a genuine, measured **zero-effect** result, not
  merely an expectation. The single pass
  (`independence_settings.independence_settings`) only checks that the
  advertised independence/feature-bit values are internally consistent,
  which `VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE` plus all-`VK_FALSE`
  already satisfies (both before and after this row: this row changed
  *diagnostics* inside SPIR-V-to-LLVM conversion, not any advertised
  property). Every other case is `NotSupported` at
  `vktSpvAsmComputeShaderCase.cpp:491` on `shaderFloat16`/`shaderFloat64`
  (the fp16/fp64 storage-class-plus-arithmetic features these generated
  cases require and this ICD does not advertise) before `vkCreateDevice`
  ever runs, let alone before a shader reaches
  `feme-convert-spirv-to-llvm`'s `collectEntryPoints`/`ExecutionModePattern` --
  so this session's new rejection path is not exercised by any case in
  this CTS build at all; it is unreachable until `VK_KHR_16bit_storage`+
  `shaderFloat16`/`shaderFloat64` (an unrelated, pre-existing gap) is
  implemented, or a future `dEQP-VK` build adds an fp32-only float-controls
  case, or a hand-written unit/lit test exercises it directly (which this
  row's own new FileCheck tests do).

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1696/1697
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row -- four new tests relative to F2's own report:
`spirv-to-llvm-float-controls.mlir`,
`spirv-to-llvm-denorm-flush-to-zero-invalid.mlir`, and
`spirv-to-llvm-rounding-mode-rtz-invalid.mlir` (three FileCheck files, one
with three independent `--split-input-file` cases).

## Full run, docs-only compute-only-scope pass (this session): measured impact

This session made no functional change: it reworded `FeMeCPUDesign.md` and
`FeMeVulkanDesign.md` passages that described FeMe's planned scope as
permanently compute-only (a Non-Goals bullet, the "Accounting for Graphics
Later" section title, the Summary's "one compute-only queue family"), added
a Deviation note recording that V6 shipped `VK_QUEUE_GRAPHICS_BIT` without
growing `subgroupSupportedStages` to match (now scheduled under roadmap V7),
and reworded `filter_vulkan_cts_cases.py`'s docstring to stop calling the
ICD itself compute-only. None of that touches `libfeme_vulkan` or the CPU
pipeline.

The full 54-group re-run above exists because this report's own top section
was stale (last full re-run at roadmap E29; F1-F3 had each only run a
targeted subset since), not because of this session's own docs changes, and
because the task asked for current, accurate top-of-file numbers. Two
findings from it:

- **The headline moved only within noise of E29's own numbers** (Passed
  36,725 -> 36,759, Failed 144,445 -> 144,753, Not supported 3,055,600 ->
  3,052,501), consistent with F1-F3 being small, mostly non-headline-moving
  rows, each already measured individually in its own section above.
- **A second crashing group, `synchronization2`, was found** that E29's own
  run did not reach in isolation (or reached without crashing -- E29's own
  section above records only `api` as crashing). It segfaults at
  `timeline_semaphore.device_host.write_copy_buffer_to_image_read_copy_
  image_to_buffer.image_128x128_d16_unorm`, after a run of sibling
  `write_copy_buffer_to_image*` cases in the same group fail with
  `VK_ERROR_INITIALIZATION_FAILED` rather than crashing outright -- the same
  device-vs-host timeline-semaphore family "Roadmap C1: measured impact"
  above already attributes a *different* crashing case in core
  `synchronization` to, now also reachable through
  `VK_KHR_synchronization2`'s own entry points. Not investigated further
  this session (out of scope for a docs-and-measurement pass); left as a
  known, tracked gap alongside `api`'s own pre-existing crash.

`ninja check-feme` (`RelWithDebInfo`, `LLVM_ENABLE_ASSERTIONS=ON`,
`LLVM_CCACHE_BUILD=ON`): 1696/1697 passed, 1 unsupported, unchanged by this
session's docs-only commits.

## Roadmap F15a: measured impact (targeted, not a full re-run)

F15a closed the constrained-FP-intrinsics-plumbing prerequisite F3's own
audit split off as F15: `ConstrainedRoundTowardZeroPattern`
(SPIRVToLLVMPatterns.cpp) now routes an entry point's declared
`RoundingModeRTZ` bit width(s) through `llvm.experimental.constrained.*`
intrinsics with an explicit round-toward-zero rounding mode, rather than
F3's hard diagnostic. Verified against LLVM's real, in-tree SPIRV backend
(`spirv-backend-rounding-mode-rtz.mlir`): the round trip produces an
`spirv.FAdd` carrying `fp_rounding_mode = #spirv.fp_rounding_mode<RTZ>`,
concrete evidence of genuinely truncating-rounding-mode code, not just a
passing FileCheck pattern.

Targeted subset: the same
**`dEQP-VK.spirv_assembly.instruction.compute.float_controls.*`**
(2,569 cases) F3's own section measured.

- First run, with `shaderRoundingModeRTZFloat{16,32,64}` flipped to
  `VK_TRUE` (the natural next step once the codegen worked): **1 passed /
  18 failed / 2,550 not supported** -- a *regression* from F3's own
  1/0/2,568. Every one of the 18 new failures is a
  `fp32.{input_args,generated_args}.rounding_rtz_*` case that now reaches
  `vkCreateComputePipelines` (rather than being skipped on the advertised
  feature bit) and fails there on a completely unrelated, pre-existing
  gap: `feme::cpu`'s resource-lowering cannot raise the small,
  2-element runtime-sized storage-buffer bindings these generated shaders
  declare ("register-bound resource handle ... cannot normalize into a
  heap access or the root-constant block" -- confirmed by temporarily
  instrumenting `vkCreateComputePipelines`'s error path, normally
  `consumeError`-silenced). A handful of the 18 are additionally blocked
  on `spirv.CompositeConstruct`/`spirv.OuterProduct` (matrix ops) having
  no conversion pattern at all, also unrelated to float controls.
- Reverted the feature-bit flip (see `EntryPoints.cpp`'s own comment and
  the git history) rather than chase the unrelated resource-lowering gap
  under this row's own scope. Re-ran the same subset after reverting:
  **1 passed / 0 failed / 2,568 not supported**, matching F3's own
  zero-effect baseline exactly -- the codegen fix is real and tested (via
  the new lit tests, independent of CTS), but stays dormant for CTS
  until either the resource-lowering gap closes or `RoundingModeRTZ` is
  advertised anyway (not done here, deliberately: trading a graceful
  skip for a hard failure is a regression, not progress).

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1699/1700
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row -- two new lit tests relative to F3's own report
(`spirv-to-llvm-rounding-mode-rtz.mlir`, replacing the now-stale
`spirv-to-llvm-rounding-mode-rtz-invalid.mlir`, and
`spirv-backend-rounding-mode-rtz.mlir`).

## Roadmap F15b: measured impact (targeted, not a full re-run)

F15b closed F15a's own remaining half: `DenormFlushToZero` now genuinely
flushes any subnormal operand or result of its declared bit width's
arithmetic FP ops to a same-signed zero (`llvm.is.fpclass`/`llvm.copysign`/
`llvm.select`, unified with F15a's `RoundingModeRTZ` handling into one
`FloatControlArithmeticPattern`, `SPIRVToLLVMPatterns.cpp`), rather than
`collectEntryPoints`'s former hard diagnostic. Every
`VK_KHR_shader_float_controls` execution mode is now genuinely honored.

Targeted subset: the same
**`dEQP-VK.spirv_assembly.instruction.compute.float_controls.*`**
(2,569 cases; the CTS checkout's own case count grew by 14 relative to
F15a's report, an unrelated version-to-version difference, not something
this row's change caused) F15a's own section measured.

- Baseline (this row's code landed, `shaderDenormFlushToZeroFloat{16,32,64}`
  still `VK_FALSE`): **1 passed / 0 failed / 2,568 not supported**,
  identical to F15a's own baseline/reverted-to result -- the new codegen is
  real and tested (via the new lit tests), but stays dormant for CTS until
  advertised.
- Flipped `shaderDenormFlushToZeroFloat{16,32,64}` (both
  `VkPhysicalDeviceFloatControlsProperties` and its
  `VkPhysicalDeviceVulkan12Properties` promoted twin) to `VK_TRUE`: **1
  passed / 171 failed / 2,397 not supported** -- the exact same regression
  shape F15a's row found for `RoundingModeRTZ`. Spot-checked one failure
  (`dEQP-VK.spirv_assembly.instruction.compute.float_controls.fp32.
  generated_args.abs_denorm_flush_to_zero`): `vkCreateComputePipelines`
  fails with `VK_ERROR_INITIALIZATION_FAILED`, the identical
  `feme::cpu` resource-lowering gap (small, 2-element runtime-sized
  storage-buffer bindings) F15a's report already attributes its own 18
  failures to -- not a new gap this row introduces, and not investigated
  further here for the same reason F15a's row gave (out of scope; trading
  a graceful skip for a hard failure is a regression, not progress).
- Reverted the flag flip (see `EntryPoints.cpp`'s own comment and the git
  history) and re-ran the same subset: **1 passed / 0 failed / 2,568 not
  supported**, matching the baseline exactly.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1699/1700
passed, 1 unsupported (pre-existing, unrelated), after every commit in
this row -- one new lit test relative to F15a's own report
(`spirv-to-llvm-denorm-flush-to-zero.mlir`, replacing the now-stale
`spirv-to-llvm-denorm-flush-to-zero-invalid.mlir`).


## Roadmap F15c: measured impact (targeted, not a full re-run)

F15c closed `VK_KHR_shader_float_controls2`'s per-instruction decorations:
`FloatControlArithmeticPattern` (SPIRVToLLVMPatterns.cpp, F15a/F15b's own
pattern) now also reads `FPRoundingMode`/`FPFastMathMode` directly off the
individual `spirv.FAdd`/`FSub`/`FMul`/`FDiv`/`FRem` op they decorate, rather
than only F15a/F15b's whole-entry-point execution modes. Verified two ways
independent of this conversion's own IR: `FPRoundingMode`'s `RTP` direction
(neither `VK_KHR_shader_float_controls`'s execution mode nor F15a's own test
can express `RTP`/`RTN` at all) round-trips through LLVM's real, in-tree
SPIRV backend back into `fp_rounding_mode = #spirv.fp_rounding_mode<RTP>`
(`spirv-backend-per-instruction-rounding-mode.mlir`); `FPFastMathMode`
produces real `nnan ninf nsz` flags in the actual translated LLVM IR
(`spirv-to-llvmir-per-instruction-fast-math.mlir`), though LLVM's own SPIRV
backend does not currently re-emit an `FPFastMathMode` decoration from those
flags on the way back to SPIR-V text (a gap in LLVM's SPIRV backend itself,
confirmed empirically, not in this conversion).

Targeted subset:
**`dEQP-VK.spirv_assembly.instruction.compute.float_controls2.*`** (1,977
cases in this session's CTS checkout, the group this extension's own CTS
tests live under, distinct from F15a/F15b's
`...float_controls.*` group). Baseline (this row's code landed, nothing
advertised): 0 passed / 0 failed / 1,977 not supported, unchanged by this
row -- every case is gated by `isFloatControls2FeaturesSupported`
(`vktSpvAsmUtils.cpp`) on `shaderFloatControls2` itself, still `VK_FALSE`.

Went one step further than F15a/b's own probes to see how close the new
per-instruction codegen actually is to CTS-ready, since simply flipping the
aggregate `VkPhysicalDeviceVulkan14Features::shaderFloatControls2` bit (the
obvious first move) was not enough on its own:

- `isFloatControlsFeaturesSupported` (the same gate F15a/F15b's own probes
  hit) additionally requires per-width `VkPhysicalDeviceFloatControlsProperties`
  fields (`shaderRoundingModeRTEFloat32`/`shaderRoundingModeRTZFloat32`/
  `shaderSignedZeroInfNanPreserveFloat32`) to be `VK_TRUE`, matched to what
  each generated test case actually declares.
- `isFloatControls2FeaturesSupported` reads a *dedicated*
  `VkPhysicalDeviceShaderFloatControls2Features` struct via CTS's own
  `DeviceFeatures` machinery, not the aggregate `VkPhysicalDeviceVulkan14Features`
  one -- `EntryPoints.cpp` has no case for that dedicated struct's `sType` at
  all (correctly so: the convention here, confirmed against every other
  still-`VK_FALSE` feature in this file, is that only *advertised* features
  get their own dedicated-struct case) -- and CTS's own `DeviceFeatures` only
  populates that dedicated struct's query at all once the extension is also
  listed in `vkEnumerateDeviceExtensionProperties`'s output
  (`PhysicalDeviceInfo.cpp`'s `getSupportedDeviceExtensions`), independent of
  the struct being 1.4-core-promoted.

Temporarily flipped all of the above (aggregate feature bit, the three
per-width property fields for `fp32`, a temporary dedicated-struct case, and
temporarily listing `VK_KHR_shader_float_controls2` as a supported
extension) to see how far the `fp32` subset (719 cases) could get:
**0 passed / 707 failed / 12 not supported** -- past every feature gate this
time (unlike F15a/F15b's own probes, which reached `vkCreateComputePipelines`
directly), every one of the 707 failures is instead
**`error: unknown capability: 6029`** at SPIR-V *import* time, before this
conversion or its new per-instruction decoration handling ever runs at all:
capability 6029 is `FloatControls2` itself, and MLIR's `spirv` dialect has no
enumerant for it whatsoever (confirmed against `SPIRVBase.td`'s full
`SPIRV_C_*` list) -- every CTS-generated shader exercising this extension's
own decorations necessarily declares `OpCapability FloatControls2`, so none
of them can even be deserialized today, regardless of how correct F15c's own
decoration-handling code is. This is a more fundamental, dialect-level gap
than the `feme::cpu` resource-lowering one F15a/F15b's own probes found (this
one blocks *import*, not just pipeline creation), and, combined with the
already-known `FPFastMathDefault` execution-mode gap, is exactly why F15c
splits its own remaining scope off as F15d rather than closing it here.
Reverted every temporary flip (feature bit, property fields, dedicated
struct case, extension listing) rather than leave any of them in place; none
of it is committed.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1702/1703
passed, 1 unsupported (pre-existing, unrelated), after every commit in this
row -- three new lit tests relative to F15b's own report
(`spirv-to-llvm-per-instruction-float-controls.mlir`,
`spirv-backend-per-instruction-rounding-mode.mlir`,
`spirv-to-llvmir-per-instruction-fast-math.mlir`).

## Roadmap F15d: measured impact (targeted, not a full re-run)

F15d closed F15c's own remaining scope: the `FloatControls2` capability and
`FPFastMathDefault` execution-mode enumerants MLIR's `spirv` dialect was
missing entirely (dialect-level changes, `mlir/include/mlir/Dialect/SPIRV/IR`
and `mlir/lib/Target/SPIRV`), plus `FloatControlArithmeticPattern`/
`collectEntryPoints` (SPIRVToLLVMPatterns.cpp/ConvertSPIRVToLLVMPass.cpp)
applying an entry point's declared per-type `FPFastMathDefault` to every
undecorated arithmetic op of that type.

Targeted subset, same as F15c's own:
**`dEQP-VK.spirv_assembly.instruction.compute.float_controls2.*`** (1,977
cases). Baseline (this row's code landed, nothing advertised): 0 passed / 0
failed / 1,977 not supported, unchanged from F15c's own baseline -- the
same `shaderFloatControls2`/`isFloatControls2FeaturesSupported` gates F15c's
report describes are still `VK_FALSE`/ungated by this row, which touches
codegen and dialect plumbing, not feature advertisement.

Repeated F15c's own "temporarily flip every gate and see" experiment
(aggregate feature bit, the three per-width `fp32` property fields, a
temporary dedicated-struct case, and temporarily listing
`VK_KHR_shader_float_controls2` as a supported extension) against the
`fp32` subset (719 cases) to measure whether the dialect-level fix this row
made actually unblocks import the way it should. It (partially) does: of
276 cases run before an unrelated crash (see below) truncated the session,
**0** now fail with `unknown capability: 6029` -- F15c's own blocking
finding is gone -- and **263** instead reach the already-known
`feme::cpu` resource-lowering gap F15a/F15b's own probes first found
(`vk.createComputePipelines(...): VK_ERROR_INITIALIZATION_FAILED`, small
runtime-sized storage-buffer bindings), with the remaining 12 `NotSupported`
for unrelated missing features (e.g. `shaderFloat16`/`shaderFloat64` on
cases outside the `fp32` subset's own filter). This is the same shape
F15a/F15b's own probes found for `VK_KHR_shader_float_controls` itself --
codegen and import both now genuinely work, blocked from a conformant claim
only by the unrelated resource-lowering gap those rows already tracked.

The session was truncated by a **new, unrelated** discovery rather than
running to completion: case
`float_controls2.fp32.input_args.frexp_st_testedWithout_NSZ_arg1_minusZero_
arg2_one_res_minusZero_deco` crashed `deqp-vk` itself with
`Deserializer.cpp:1506: LogicalResult
mlir::spirv::Deserializer::processStructType(ArrayRef<uint32_t>): Assertion
`decoration.has_value()' failed` -- an assertion failure deserializing the
two-member struct `OpExtInst ... FrexpStruct`/`ModfStruct` (GLSL.std.450)
return, nothing to do with float controls at all, and the first time this
whole F15 family of rows' own probes got far enough into real,
CTS-generated shaders to hit it. Split off as its own row, F16 (see
`Roadmap.md`), rather than investigated further here, since it is out of
this row's own scope. Reverted every temporary flip (feature bit, property
fields, dedicated struct case, extension listing) immediately after; a
second baseline run (excluding nothing, the full 1,977-case group) confirmed
the exact same 0/0/1,977 as the first, so no partial revert leaked through.

`ninja check-feme` (assertions-enabled, ccache build, this session's
existing `./build`): 1704 discovered, 1703 passed, 1 unsupported
(pre-existing, unrelated) -- one new lit test relative to F15c's own report
(`spirv-to-llvmir-fp-fast-math-default.mlir`), plus two new upstream MLIR
lit tests (`mlir/test/Dialect/SPIRV/IR/structure-ops.mlir`'s and
`mlir/test/Target/SPIRV/execution-mode-id.mlir`'s own new cases, run as part
of `check-mlir-dialect-spirv`/`check-mlir-target-spirv` rather than
`check-feme`, both 100% passing).

## Roadmap F16: measured impact

F16 fixed the `processStructType` assertion F15d's own targeted re-run
found, and closed the modeling gap it also surfaced: `spirv.GL.ModfStruct`
did not exist in this dialect at all (only `spirv.GL.FrexpStruct` did), and
neither instruction had a `SPIRVToLLVM` conversion pattern.

Reproducing the exact crash through a full CTS run was not attempted again
this row -- F15d's own report above already isolated the single triggering
case precisely (`frexp_st_testedWithout_NSZ_..._deco`, decorating the
`struct_fi` *type* itself with `FPFastMathMode`) well enough to construct a
standalone reproduction directly: a hand-assembled SPIR-V binary
(`spirv-as`/`spirv-val`) matching that shader's own `struct_fi`
(`OpMemberDecorate ... Offset` on both members, `OpDecorate %struct_fi
FPFastMathMode NotNaN` on the type) reproduced
`Deserializer.cpp:1506: Assertion `decoration.has_value()' failed` exactly,
letting the fix be verified against `mlir-translate --deserialize-spirv`
directly (crashes before the fix, imports -- decoration included -- after
it) and then round-tripped back through `--serialize-spirv` and `spirv-val`
again, without needing a live CTS process for either step.

A CTS re-run *was* attempted, to see whether F15d's own truncated
`fp32.input_args.frexp_st_*` cases would now proceed past the crash: same
methodology as F15d's own (temporarily advertising `shaderFloatControls2`
plus the three FP32 float-control property fields, both in the dedicated
`VkPhysicalDeviceFloatControlsProperties` struct case and its 1.2-promoted
twin). It did not get far enough to reach the fixed code path at all: CTS
separately requires `VkPhysicalDeviceShaderFloatControls2FeaturesKHR`'s own
`shaderFloatControls2` field (a distinct, dedicated chained struct this ICD
does not handle at all, still `NotSupported: ShaderFloatControls2.
shaderFloatControls2`), which is a new, unrelated feature-advertisement gap
this row's own scope does not cover (F15d's row already tracks
`shaderFloatControls2` advertisement as blocked on the unrelated
`feme::cpu` resource-lowering gap; this dedicated-struct gap is a further,
so-far-unrecorded prerequisite that same future advertisement work will
need). Reverted immediately, nothing committed, the same as every "flip
and see" experiment this F15/F16 family has run.

`ninja check-feme` (assertions-enabled, ccache build): 1706 discovered,
1705 passed, 1 unsupported (pre-existing, unrelated) -- two new tests this
row added (`feme/test/Conversion/SPIRVToLLVM/
spirv-to-llvm-frexp-modf-struct.mlir`, `feme/test/Import/SPIRV/
spirv-import-struct-fast-math-mode.mlir`), plus new upstream MLIR lit
coverage run via `check-mlir-dialect-spirv`/`check-mlir-target-spirv`/
`check-mlir-conversion` (58/52/411 tests respectively, all passing).



## Roadmap F4: measured impact (targeted, not a full re-run)

F4 gave `spirv.KHR.AssumeTrue`/`spirv.KHR.Expect` (already modeled by
MLIR's own `spirv` dialect, needing no dialect-level work first, unlike
several other F-rows) real `spirv`->`llvm` conversion patterns
(`AssumeTrueConversionPattern`/`ExpectConversionPattern`,
`SPIRVToLLVMPatterns.cpp`), lowering directly to `llvm.assume`/
`llvm.expect`, and advertised `VK_KHR_shader_expect_assume`/
`shaderExpectAssume`. Verified first with FileCheck lit tests
(`spirv-to-llvm-expect-assume.mlir`, covering the scalar `assume`/`expect`
forms and `spirv.KHR.Expect`'s vector-of-integer form, which -- unlike
`spirv.KHR.AssumeTrue`'s scalar-only condition -- LLVM's own `llvm.expect`
intrinsic cannot represent directly and this row expands into one
`llvm.expect` call per lane instead), then against a real, hand-assembled
SPIR-V binary (`spirv-as`/`spirv-val`) deserialized through MLIR's own
importer (not just textual `spirv` dialect parsing), and finally against
LLVM's real, in-tree SPIRV backend (`spirv-backend-expect-assume.mlir`),
which already lowers both intrinsics back to `OpAssumeTrueKHR`/
`OpExpectKHR` (`SPIRVPrepareFunctions.cpp`'s `lowerExpectAssume`) once its
own `-spirv-ext` allow-list enables `SPV_KHR_expect_assume` (empty by
default, unlike every capability every other backend round-trip test in
this repository exercises so far, discovered when the first version of
this test silently lost both intrinsics with no diagnostic).

Targeted subset: **`dEQP-VK.glsl.shader_expect_assume.*`** (141 cases, the
dedicated CTS module for this extension, shared by all three
`vertex`/`fragment`/`compute` stages).

- **0 passed / 69 failed / 72 not supported.** The 72 not-supported cases
  are `int8`/`int16`/`int64` data-class cases this ICD does not advertise
  storage support for (`uniformAndStorageBuffer8/16BitAccess`,
  `shaderInt64`) -- expected, unrelated gates. All 69 failures split into
  two pre-existing, unrelated gaps, neither touched by this row's own
  patterns (confirmed by reproducing each on an entirely unrelated
  sibling case that needs no `assume`/`expect` functionality at all):
  - **23 `compute`-stage failures** all fail identically at
    `vkCreateComputePipelines` with `'llvm.getelementptr' op operand #0
    must be LLVM pointer type or LLVM dialect-compatible vector of LLVM
    pointer type, but got 'vector<3xi32>'` -- a pre-existing gap in this
    ICD's storage-buffer access-chain lowering for the
    `RuntimeArray-of-vecN` layout CTS's shared `ShaderExecutor` compute
    harness always uses to marshal results (every op-under-test writes
    through this same buffer shape, regardless of which op), not
    anything specific to `assume`/`expect`. Reproduced identically on
    `dEQP-VK.glsl.builtin.function.integer.bitcount.int_highp_compute`
    (an entirely unrelated, long-implemented builtin function test using
    the same harness), confirming this is a `ShaderExecutor`-framework-
    wide compute gap this row's own patterns do not cause and do not fix.
  - **46 `vertex`/`fragment`-stage failures** all fail at
    `vkCreateGraphicsPipelines` with `VK_ERROR_INITIALIZATION_FAILED`
    (`FEME_VULKAN_LOG_CREATION_ERRORS=1` reveals the real reason: "color
    attachment 0 names a format this driver cannot render into") --
    a pre-existing, unrelated gap in this ICD's graphics-pipeline
    color-attachment-format support, not anything `assume`/`expect`-
    specific either.
  - In other words: `dEQP-VK.glsl.shader_expect_assume.*` was not a
    passing category before this row, and F4 does not regress a
    previously-passing one -- it makes 69 previously-`NotSupported`
    cases newly reach (and fail at) two pre-existing, unrelated gaps,
    the same shape "Roadmap F1/F2: measured impact" above found. Neither
    gap is folded into this row: the access-chain one needs a real
    `ArrayedBlockAccessChainPattern` fix (`SPIRVToLLVMPatterns.cpp`) for
    vector-element sub-indexing, and the color-attachment one needs
    auditing this ICD's supported render-target format list
    (`Image.cpp`/`Format.cpp`) against what `ShaderExecutor`'s fragment
    harness actually requests -- both comparably-sized, unrelated efforts
    of their own, left as future work.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1709
discovered, 1708 passed, 1 unsupported (pre-existing, unrelated), after
every commit in this row -- new tests relative to F16's own report:
`spirv-to-llvm-expect-assume.mlir` and `spirv-backend-expect-assume.mlir`
(both FileCheck), `PhysicalDeviceInfoTest`'s
`ShaderExpectAssumeIsAdvertisedThroughItsOwnDedicatedFeatureStruct`, and
`DrawTest`'s `AdvertisesDynamicRenderingExtension` (extended).

## Roadmap F5: measured impact (targeted, not a full re-run)

F5 generalized `executeDraws`' line-topology quad expansion (roadmap
C4d) to variable width and `VkLineRasterizationModeKHR`'s three styles
(`Rectangular`/`Bresenham`/`RectangularSmooth`, `feme::graphics::
RasterState::LineMode`/`LineWidth`), then added stippling as a
per-sample pattern test against `VkPipelineRasterizationLineStateCreate
InfoKHR`'s stipple factor/pattern (`StippledLineEnable`/`StippleFactor`/
`StipplePattern`, `ScreenTriangle::ArcLength`). `rectangularLines`/
`bresenhamLines`/`smoothLines` and their three `stippled*` variants are
now advertised, and `VK_KHR_line_rasterization`/`vkCmdSetLineStippleKHR`/
`VK_DYNAMIC_STATE_LINE_WIDTH` are implemented.

Targeted subset: **`dEQP-VK.rasterization.primitives.*`** (the dedicated
CTS group exercising `VkPipelineRasterizationLineStateCreateInfo`,
219 cases matching this pattern against this ICD).

- **0 passed / 44 failed / 175 not supported.** Every one of the 175
  not-supported cases gates on a feature this row deliberately leaves
  out of scope, not a regression: 108 need `geometryShader` (the
  `*_with_adjacency` topology variants, roadmap R34's own scope, not
  F5's), 52 need `wideLines` (roadmap H7's scope, not this row's, per
  its own deviation note above), 14 need "strict rasterization"
  (`nonStrictLines`-adjacent limit gating, also H7's scope), and 1 needs
  `largePoints`. **All 44 failures are the exact same pre-existing,
  unrelated gap**, confirmed by checking every single one's own error
  message: `vkCreateGraphicsPipelines` fails before any line-rasterization
  logic ever runs, with `feme-cpu-simdize: function 'main' has a
  divergent vector value '' used outside a supported insertelement-
  chain/resource-store/extractelement/select/shufflevector/phi/
  elementwise pattern; component decomposition is not yet supported for
  this use (roadmap milestone 7 deviation)` -- this test module's shared
  vertex shader (used by every one of its cases, including
  `no_stipple.triangles`/`triangle_strip`/`triangle_fan`, which name no
  line-rasterization state at all) hits a pre-existing `feme::cpu`
  SIMDization gap unrelated to this row's own translation/rasterizer
  changes. Reproduced by inspecting `no_stipple.triangles`'s own failure
  message, which is byte-for-byte identical to every line-mode/stipple
  case's, confirming this is a shared-harness-wide gap this row's own
  code does not cause and cannot fix from within its own scope (a real
  fix needs `feme-cpu-simdize`'s divergent-value handling generalized to
  cover whatever vector-component-decomposition pattern this shader uses,
  a materially larger, separate unit of work in `feme::cpu`, not
  `feme::graphics`/`feme::vulkan`). In other words: this CTS group was
  not a passing category before this row (identically `NotSupported`
  across the board, since `VK_KHR_line_rasterization` itself did not
  exist), and F5 does not regress a previously-passing one -- it makes
  219 previously-fully-`NotSupported` cases newly reach (and either
  legitimately not-support on an explicitly out-of-scope feature, or
  fail at) one pre-existing, unrelated gap.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1717
discovered, 1716 passed, 1 unsupported (pre-existing, unrelated), after
every commit in this row -- new tests relative to F4's own report:
`ExecutorTest`'s `RendersAWideRectangularLine`/
`RendersABresenhamDiagonalLine`/`RendersAStippledLine`/
`RectangularSmoothLineAntialiasesItsEdge`, `GraphicsPipelineTest`'s
`TranslatesLineRasterizationState`/
`DynamicLineWidthAndStippleOverrideStaticState`,
`PhysicalDeviceInfoTest`'s
`LineRasterizationIsAdvertisedThroughItsOwnDedicatedStructs`, and
`DrawTest`'s `DynamicLineWidthWidensTheLine` (extended) plus
`AdvertisesDynamicRenderingExtension` (extended again, now 21).

## Roadmap F6: measured impact (targeted, not a full re-run)

F6 translated `VkPipelineVertexInputDivisorStateCreateInfo`'s per-binding
divisor into a new `Divisor` field on `VertexInputBinding`
(`GraphicsPipeline.cpp`'s `translateVertexInput`), rejecting an invalid
binding reference, a `VK_VERTEX_INPUT_RATE_VERTEX` target, or a
too-large value at creation, and generalized the executor's existing
per-instance vertex fetch (`Executor.cpp`) to the spec's own
`firstInstance + (instanceIndex - firstInstance) / divisor` formula
(`divisor == 0` being the one further, explicit case, "every instance
reads `firstInstance`"). `vertexAttributeInstanceRateDivisor`/
`vertexAttributeInstanceRateZeroDivisor` and `VK_KHR_vertex_attribute_
divisor` are now advertised.

Targeted subset: every CTS case naming `vertex_attribute_divisor`/
`attrib_divisor` in `dEQP-VK.api.*` and `dEQP-VK.draw.*`
(`VK_DRIVER_FILES` pointed at this ICD's built `feme_icd.json`).

- **The three `api.*` cases that exercise this row's own advertised
  feature/property directly all pass**:
  `dEQP-VK.api.device_init.create_device_unsupported_features.vertex_
  attribute_divisor_features`, `dEQP-VK.api.info.get_physical_device_
  properties2.features.vertex_attribute_divisor_features`, and
  `dEQP-VK.api.info.vulkan1p2_limits_validation.khr_vertex_attribute_
  divisor` -- confirming `vertexAttributeInstanceRateDivisor`/
  `vertexAttributeInstanceRateZeroDivisor` and `maxVertexAttribDivisor`/
  `supportsNonZeroFirstInstance` are consistently reported across every
  path CTS queries them through.
- **Every `dEQP-VK.draw.*` case naming `attrib_divisor` fails at
  `vkCreateGraphicsPipelines`, but not for a reason this row's own code
  causes.** Re-running with `FEME_VULKAN_LOG_CREATION_ERRORS=1`
  (`Diagnostics.h`'s opt-in error log) surfaces the same message for
  every one of them, divisor value and topology held equal:
  `"rasterizer discard, depth clamp, depth bias, and non-fill polygon
  modes are not implemented"` (`GraphicsPipeline.cpp`'s
  `translateRasterState`) -- a pre-existing, unrelated gap in this
  ICD's rasterization-state translation, not anything F6 touches.
  Confirmed by two baseline checks that name no divisor state at all:
  `dEQP-VK.draw.renderpass.basic_draw.draw.triangle_list.1` and
  `dEQP-VK.draw.renderpass.instanced.draw_indexed_vk_primitive_
  topology_line_list` both fail with the identical message, so this is
  the whole `draw` module's shared `vktDrawTests` fixture setting one of
  those three static rasterization fields the way `dEQP-VK.rasterization.
  primitives.*`'s shared vertex shader hit F5's own unrelated
  `feme-cpu-simdize` gap above -- a pre-existing block on the entire
  module, not a regression this row introduces or a gap within this
  row's own scope to close. This row's actual draw-time correctness (the
  fetch-index formula itself, including the `divisor == 0` case and a
  `firstInstance != 0` base) is instead verified by two new, real
  end-to-end `DrawTest` cases below that avoid this unrelated blocker by
  using the same minimal fixed-function state `RendersPerInstanceVertex
  Attribute` already established.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1723
discovered, 1722 passed, 1 unsupported (pre-existing, unrelated), after
every commit in this row -- new tests relative to F5's own report:
`GraphicsPipelineTest`'s `TranslatesVertexAttributeDivisorState`/
`AcceptsZeroVertexAttributeDivisor`/
`RejectsInvalidVertexAttributeDivisorState`, `PhysicalDeviceInfoTest`'s
`VertexAttributeDivisorIsAdvertisedThroughItsOwnDedicatedStructs`, and
`DrawTest`'s `RendersVertexAttributeInstanceRateDivisor`/
`RendersVertexAttributeInstanceRateZeroDivisor` plus
`AdvertisesDynamicRenderingExtension` (extended again, now 22).


## Roadmap F7: measured impact (targeted, not a full re-run)

F7 gave `feme::graphics::IndexType` a third enumerator, `UInt8`, alongside
its pre-existing `UInt16`/`UInt32`: `CommandBuffer.cpp`'s draw-time
validation (the index-type check, the `IndexBinding.Type` translation, and
`validateDrawFetchBounds`'s `IndexSize` bounds check) now accepts
`VK_INDEX_TYPE_UINT8` alongside its two pre-existing cases, and the
executor's index fetch (`Executor.cpp`) reads a 1-byte-per-element index
(`ElemSize == 1`) with that type's own all-1-bits restart marker (`0xFF`,
not 16-/32-bit's `0xFFFF`/`0xFFFFFFFF`) for `primitiveRestartEnable` --
mirroring the pre-existing 16-/32-bit cases' own shape exactly, not a
parallel code path. `feme-render`'s scene YAML gained a matching
`format: uint8` index-buffer encoding. `indexTypeUint8`/
`VK_KHR_index_type_uint8` are now advertised.

Targeted subset: every CTS case naming `index_type_uint8` in
`dEQP-VK.pipeline.monolithic.*` and the two `dEQP-VK.api.*` cases that
exercise this row's own advertised feature struct directly
(`VK_DRIVER_FILES` pointed at this ICD's built `feme_icd.json`).

- **The two `api.*` cases pass**: `dEQP-VK.api.device_init.
  create_device_unsupported_features.index_type_uint8_features` and
  `dEQP-VK.api.info.get_physical_device_properties2.features.
  index_type_uint8_features` -- confirming `indexTypeUint8` is
  consistently reported across both paths CTS queries it through.
- **Every `dEQP-VK.pipeline.monolithic.input_assembly.primitive_restart.
  index_type_uint8.*` case is blocked by pre-existing, unrelated gaps, not
  anything this row's own code causes.** Of the 57 cases run: 43 are
  `NotSupported` (`VK_EXT_primitive_topology_list_restart`, an unrelated
  unimplemented extension a list-topology restart case requires, or
  `geometryShader`, an unrelated unimplemented core feature a
  `*_with_adjacency` case requires -- neither is this row's own
  `indexTypeUint8` bit), 12 `Fail` with `"Vulkan vertex buffer format is
  not supported"` (the amber-script harness's own vertex format choice,
  not this row's index-buffer path), and 2 `Fail` with the same
  pre-existing `feme-cpu-simdize` divergent-vector diagnostic F5/F6's own
  reports already hit for an unrelated triangle-strip vertex shader
  pattern (`"has a divergent vector value ... roadmap milestone 7
  deviation"`). None of these 57 failures/skips name an 8-bit-index-read
  problem anywhere in their own diagnostic text. This row's actual 8-bit
  fetch correctness (element size, the type's own restart marker, and
  bounds checking) is instead verified by four new, real tests below that
  avoid these unrelated blockers: two `ExecutorTest` cases (a plain
  8-bit-indexed triangle, and 8-bit primitive restart on a
  `TriangleStrip`) plus a `DrawTest` end-to-end case exercising
  `vkCmdBindIndexBuffer(..., VK_INDEX_TYPE_UINT8)` through the real ICD
  entry points, plus a `feme-render` lit test
  (`draw-indexed-uint8.test`) mirroring `draw-indexed.test`'s own scene
  through an 8-bit index buffer.

`ninja check-feme` (`RelWithDebInfo` + `LLVM_ENABLE_ASSERTIONS=ON` +
`LLVM_CCACHE_BUILD=ON`, this session's existing `./build`): 1729
discovered, 1728 passed, 1 unsupported (pre-existing, unrelated), after
every commit in this row -- new tests relative to F6's own report:
`ExecutorTest`'s `RendersTheSameTriangleThroughAnEightBitIndexBuffer`/
`HonorsPrimitiveRestartOnIndexedTriangleStripWithEightBitIndices`,
`PreparedDrawTest`'s `DescribesAnEightBitIndexedDraw`,
`PhysicalDeviceInfoTest`'s
`IndexTypeUint8IsAdvertisedThroughItsOwnDedicatedFeatureStruct`, and
`DrawTest`'s `RendersIndexedDrawWithEightBitIndices` plus
`AdvertisesDynamicRenderingExtension` (extended again, now 23), plus the
new `Tools/feme-render/draw-indexed-uint8.test` lit test.

## Roadmap F8: measured impact (confirmed-unsupported, not a full run)

F8 implemented `vkCmdSetRenderingAttachmentLocations`/`vkCmdSetRendering
InputAttachmentIndices` as real, validated commands -- the former's
attachment-location remap genuinely honored by the executor -- but left
`dynamicRenderingLocalRead`/`VK_KHR_dynamic_rendering_local_read` and its
two limit fields unadvertised, since no shader-side SPIR-V `subpassInput`
local-read consumption exists yet (split off as roadmap F8a; see this
row's own status note in `feme/docs/FeMeVulkanDesign.md`'s "Render passes
and dynamic rendering" section).

Every CTS case that actually exercises local-read rendering
(`dEQP-VK.renderpasses.dynamic_rendering.*.local_read.*`,
`vktDynamicRenderingLocalReadTests.cpp`/
`vktDynamicRenderingLocalReadMaint10Tests.cpp`) calls
`requireDeviceFunctionality("VK_KHR_dynamic_rendering_local_read")` before
doing anything else, so with the extension unadvertised every one of them
reports `NotSupported` rather than exercising any of this row's own code --
confirmed, not assumed, by running one such case
(`dEQP-VK.renderpasses.dynamic_rendering.partial_secondary_cmd_buff.
local_read.depth_mapping_stencil_not`) against this session's built
`feme_icd.json`:

```
NotSupported (VK_KHR_dynamic_rendering_local_read is not supported at vktTestCase.cpp:1393)
```

The two `dEQP-VK.api.*` cases that query the dedicated feature struct
directly (not gated behind `requireDeviceFunctionality`, since querying an
unsupported feature is itself the point) both pass, confirming
`dynamicRenderingLocalRead` is truthfully reported `VK_FALSE` everywhere
CTS looks for it:

```
dEQP-VK.api.info.get_physical_device_properties2.features.dynamic_rendering_local_read_features
  Pass (Querying not supported)
```

Since no CTS case reaches any of this row's own new code with the
extension unadvertised, this row's actual behavior -- the attachment-
location remap's real effect, and every validation path the two commands
add -- is instead verified through the eleven new tests described in
`agent_thoughts.md`'s F8 entry (two `ExecutorTest` cases, five `DrawTest`
end-to-end cases, all through the real ICD entry points). `ninja
check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`, this
session's existing `./build`): 1735 discovered, 1734 passed, 1 unsupported
(pre-existing, unrelated), matching the pre-F8 baseline exactly (no
existing case regressed, none newly passed since none reach this row's own
new code).

## Roadmap F8a: measured impact (extension now advertised)

F8a closed F8's remaining gap: `feme.stage.subpass.load` (a new
`feme::StageOpKind`, created by `feme::spirv::SubpassLoadPattern` in
SPIRVToLLVMPatterns.cpp from a `Dim::SubpassData` `spirv.ImageRead`) gives
a fragment shader's `subpassInput` local read real pixels, resolved
through `vkCmdSetRenderingInputAttachmentIndices`'s mapping
(`feme::vulkan::runDraw`'s `buildSubpassInputHeap`, CommandBuffer.cpp).
`dynamicRenderingLocalRead`/`VK_KHR_dynamic_rendering_local_read` are now
advertised (`EntryPoints.cpp`/`PhysicalDeviceInfo.cpp`), scoped to a
single-sample color attachment.

Two audit findings surfaced while getting a real `subpassInput` shader
through the whole pipeline, both fixed in this same series (see git log,
not repeated here): MLIR's SPIR-V deserializer *and* serializer had no
case for the `InputAttachmentIndex` decoration at all (an upstream gap,
not a `feme`-specific one); and `PhysicalDeviceInfo.cpp` keeps a second,
by-name extension list (separate from `vk_gen_entrypoints.py`'s
`SUPPORTED_EXTENSIONS`) that real CTS cases enable regardless of
`apiVersion` -- confirmed missing for this extension by actually running
a local-read case, which reported `NotSupported` until fixed.

With the extension genuinely enabled, `dEQP-VK.renderpasses.
dynamic_rendering.*.local_read.*` (54 cases total) now actually attempts
to run instead of reporting `NotSupported` for every case:

```
Test run totals:
  Passed:        0/54 (0.0%)
  Failed:        38/54 (70.4%)
  Not supported: 16/54 (29.6%)
```

The 16 `NotSupported` cases gate on an unrelated, unimplemented extension
this row never touched (`VK_EXT_graphics_pipeline_library`/
`VK_EXT_shader_object`, e.g. `remap_single_attachment_fast_lib`/
`_shader_object`). Every one of the 38 failures is the *same*
pre-existing, unrelated deviation -- confirmed by inspecting each
failure's own message, not assumed from the count alone:

```
error: feme-cpu-simdize: function 'main' has a divergent vector value ''
used outside a supported insertelement-chain/resource-store/
extractelement/select/shufflevector/phi/elementwise pattern; component
decomposition is not yet supported for this use (roadmap milestone 7
deviation)
```

This is `feme::cpu::SIMDizePass`'s own documented milestone-7 gap
(component decomposition for a divergent vector used outside the shapes
it currently recognizes) -- these CTS cases' real GLSL fragment shaders
happen to produce that exact shape regardless of whether they read a
`subpassInput` at all, so it blocks pipeline *creation* before this row's
own subpass-read code ever runs. Confirmed unrelated to F8a: `feme-render`
draws (this row's own `DrawTest.
SubpassLoadReadsBackTheColorAttachmentItWrote`, and every existing
`ExecutorTest`/`DrawTest` fragment shader, hand-written rather than
`glslang`-compiled from real GLSL control flow) never exercise this shape,
which is why the dedicated end-to-end test needed for this row could pass
while the CTS suite's own shaders cannot yet reach it. Fixing the
milestone-7 gap itself is out of this row's own scope.

The two `dEQP-VK.api.*` cases that do not depend on rendering a real
shader both pass:

```
dEQP-VK.api.info.get_physical_device_properties2.features.dynamic_rendering_local_read_features
  Pass (Querying succeeded)
dEQP-VK.api.device_init.create_device_unsupported_features.dynamic_rendering_local_read_features
  Pass (Pass)
```

`dEQP-VK.api.info.vulkan1p4.feature_extensions_consistency` (the
aggregate-vs-dedicated-struct cross-check E2's own note describes)
reports `NotSupported (At least Vulkan 1.4 required to run test)`: this
ICD's advertised `apiVersion` is still 1.2, so the check does not apply
yet, exactly as E2 already found for every other 1.3/1.4-gated
consistency case.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1742 discovered, 1741 passed, 1
unsupported (pre-existing, unrelated) -- 8 more passing than F8's own
1734-passed baseline (the new `StageOpsTest`, lit, and `DrawTest`/
`PhysicalDeviceInfoTest` cases this series adds), no regressions.

## Roadmap F8b: measured impact (depth/stencil coverage, extension unchanged)

F8b closed the remainder of F8a's own status note for a depth/stencil
attachment: the CPU runtime's texel-unpack table (FeMeRuntimeCPU.c) now
decodes `D16_UNORM`/`D32_FLOAT`/`S8_UINT`, and `buildSubpassInputHeap`
(CommandBuffer.cpp) now populates a multisampled attachment's heap slot with
a correct per-sample layout too (proven by unit tests, not yet by an
end-to-end multisample `subpassLoad`, since no caller threads an explicit
sample index through one -- see agent_thoughts.md and roadmap F8c).
`dynamicRenderingLocalReadDepthStencilAttachments` is now advertised
`VK_TRUE`.

Re-running the same `dEQP-VK.renderpasses.dynamic_rendering.*.local_read.*`
suite F8a's own report measured produces *identical* totals:

```
Test run totals:
  Passed:        0/54 (0.0%)
  Failed:        38/54 (70.4%)
  Not supported: 16/54 (29.6%)
```

This is expected, not a sign F8b had no effect: every one of the 38
failures is still gated on the same pre-existing, unrelated
`feme::cpu::SIMDizePass` milestone-7 deviation F8a's own report already
attributed them to (confirmed again by inspecting the failure messages,
which are byte-for-byte the same `error: feme-cpu-simdize: ... component
decomposition is not yet supported` text) -- it blocks pipeline *creation*
for these CTS cases' real `glslang`-compiled shaders regardless of whether
they touch a depth/stencil `subpassInput` at all, so F8b's own code never
gets a chance to run inside this particular suite either. This is exactly
why the roadmap row asked for a dedicated CTS-*shaped* test rather than
relying on the real suite: `DrawTest.SubpassLoadReadsBackTheDepthAttachment
ItWrote`/`SubpassLoadReadsBackTheStencilAttachmentItWrote` (hand-written,
not `glslang`-compiled) exercise the same `subpassLoad`/`OpTypeImage(Dim=
SubpassData)` path this suite's shaders would, without needing the
milestone-7 SIMDize gap fixed first.

The two `dEQP-VK.api.*` cases unaffected by that gap still pass:

```
dEQP-VK.api.info.get_physical_device_properties2.features.dynamic_rendering_local_read_features
  Pass (Querying succeeded)
```

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1748 discovered, 1747 passed, 1
unsupported (pre-existing, unrelated) -- 6 more passing than F8a's own
1741-passed baseline (the new `ImageSamplingTest` format/multisample cases
and `DrawTest`'s two depth/stencil subpass-load tests), no regressions.
One incidental fix surfaced along the way: making the CPU runtime's
`SampleStride` field load-bearing exposed a latent, previously-inert
`SampleStride == SlicePitch` mistake in `decodeASTCImageForSampling`
(CommandBuffer.cpp) that made `ASTCSampledImageDispatchTest.
SamplesARealDecodedTexelRatherThanAllZero` start failing the moment the
field stopped being ignored; fixed in the same series (see
agent_thoughts.md for the full trace).

## Roadmap F8c: measured impact (multisample coverage, F8 closed)

F8c closed F8b's own remaining piece: `feme::StageOpKind::SubpassLoad`
gained a `sample` operand, `SubpassLoadPattern` (SPIRVToLLVMPatterns.cpp)
now reads a real `spirv.ImageRead` `Sample` image operand instead of
rejecting one, and `lowerFragmentSubpassLoad`/the CPU runtime's
`femeRTFetchTexel2D` thread that sample index into the texel address
`buildSubpassInputHeap`'s own per-sample layout (F8b) already supports.
`dynamicRenderingLocalReadMultisampledAttachments` is now advertised
`VK_TRUE`, closing `VK_KHR_dynamic_rendering_local_read`'s scope in full
(F8/F8a/F8b/F8c all done).

Re-running the identical `dEQP-VK.renderpasses.dynamic_rendering.*.local_read.*`
suite F8a/F8b's own reports measured produces, again, *identical* totals:

```
Test run totals:
  Passed:        0/54 (0.0%)
  Failed:        38/54 (70.4%)
  Not supported: 16/54 (29.6%)
```

Expected, for the same reason F8b's report gave: every one of the 38
failures is still gated on the pre-existing, unrelated `feme::cpu::
SIMDizePass` milestone-7 "component decomposition is not yet supported"
deviation, which rejects pipeline *creation* for these CTS cases' real
`glslang`-compiled shaders before any of them reach a `subpassLoad` at
all -- confirmed again by inspecting the failure messages, byte-for-byte
identical to F8a/F8b's own. This is exactly why the roadmap row asked for
a dedicated CTS-*shaped* test: `DrawTest.SubpassLoadReadsBackAnExplicit
SampleOfTheColorAttachmentItWrote` (hand-written, not `glslang`-compiled)
exercises the same `subpassLoad`/`OpTypeImage(Dim=SubpassData,
MultiSampled)` explicit-sample path this suite's shaders would, without
needing the milestone-7 SIMDize gap fixed first.

The `dEQP-VK.api.*` feature-consistency case unaffected by that gap still
passes:

```
dEQP-VK.api.info.get_physical_device_properties2.features.dynamic_rendering_local_read_features
  Pass (Querying succeeded)
```

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1751 discovered, 1750 passed, 1
unsupported (pre-existing, unrelated) -- 3 more passing than F8b's own
1747-passed baseline (`StageOpsTest`'s new explicit-sample case, the two
new `ImageSamplingTest` sample-addressing cases, and `DrawTest`'s own new
multisample subpass-load test), no regressions.

Two incidental, tightly-coupled fixes surfaced while writing this row's
own test and were fixed in the same series:

- MLIR's own generic image-operand verifier (`mlir/lib/Dialect/SPIRV/IR/
  ImageOps.cpp`) asserted `Sample` was an unimplemented Image Operand
  unconditionally for *every* `spirv.ImageRead`/`ImageFetch`/`ImageWrite`/
  `ImageDrefGather`, not just this ICD's own conversion pattern -- a real
  `Dim::SubpassData`, `MultiSampled` `spirv.ImageRead` with a `["Sample"]`
  operand could not even be constructed and verified before this fix, let
  alone converted. A minimal, spec-shaped validation case (integer-scalar
  operand, legal only on a fetch/read/write op whose image type has
  `MS=1`) was added, mirroring the existing `Lod` case's own shape.
- `GraphicsPipeline.cpp`'s `getRenderTargets` always compared a dynamic-
  rendering pipeline's `VkPipelineMultisampleStateCreateInfo::
  rasterizationSamples` against a hardcoded single-sample default at
  pipeline *creation* time, since (unlike a `VkRenderPass`'s
  `VkAttachmentDescription::samples`) `VkPipelineRenderingCreateInfo`
  carries no sample-count field of its own. Every genuinely multisampled
  dynamic-rendering pipeline was rejected at creation before this fix --
  with no dynamic-rendering multisample pipeline test anywhere in this
  suite to have caught it until this row's own test needed exactly that
  combination. The real per-draw sample-count check (`CommandBuffer.cpp`,
  against the actually bound attachment) already existed and is
  unaffected; this fix only stops the creation-time check from rejecting
  a case draw time would have accepted.

## Roadmap F9: measured impact (`VK_EXT_pipeline_protected_access`)

F9 confirmed the spec's actual conformance requirement is a *bind-time*
rule (`VUID-vkCmdBindPipeline-pipelineProtectedAccess-07408`/`-07409`),
not the roadmap row's own initial guess of a creation-time "mixed
pipeline" rejection -- see Roadmap.md's F9 row for the full trace.
`Pipeline::createFlags` now records `VkPipelineCreateInfo::flags`
verbatim on every `VkPipeline`, and `vkCmdBindPipeline` (CommandBuffer.cpp)
silently rejects binding a `VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT`
pipeline, since this ICD's `protectedMemory` feature always reports
`VK_FALSE` (no protected command buffer ever exists to legally bind one
in).

The feature-consistency case passes:

```
dEQP-VK.api.info.get_physical_device_properties2.features.pipeline_protected_access_features
  Pass (Querying succeeded)
```

(`dEQP-VK.api.info.get_physical_device_properties2.features.protected_memory_features`
still fails in this same run, but that is `protectedMemory`'s own
pre-existing, unrelated gap -- roadmap K9's own row, not F9's -- caused by
this ICD having no case at all for the dedicated
`VkPhysicalDeviceProtectedMemoryFeatures` struct; untouched by this
change.)

`dEQP-VK.pipeline.monolithic.image.*.pipeline_protected_flag_*` (the one
real CTS suite that exercises `VK_PIPELINE_CREATE_NO_PROTECTED_ACCESS_BIT_EXT`
without needing `protectedMemory` itself, gated on
`context.requireDeviceFunctionality("VK_EXT_pipeline_protected_access")`)
now actually attempts these cases instead of skipping them outright with
`NotSupported (VK_EXT_pipeline_protected_access is not supported)`, the
pre-F9 baseline this row re-measured directly (`git stash` back to the
pre-F9 tree, same case list, same build): every case now reaches real
pipeline creation, but fails there for the same pre-existing,
already-documented milestone-7 `feme::cpu::SIMDizePass` "component
decomposition is not yet supported" deviation F8a/F8b/F8c's own reports
describe (a real `glslang`-compiled image-sampling shader, not this row's
own flag logic, is what these cases fail on) -- confirmed by inspecting
the failure message directly:

```
error: feme-cpu-simdize: function 'main' has a divergent vector value ''
used outside a supported insertelement-chain/resource-store/
extractelement/select/shufflevector/phi/elementwise pattern; component
decomposition is not yet supported for this use (roadmap milestone 7
deviation)
```

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1756 discovered, 1755 passed, 1
unsupported (pre-existing, unrelated) -- 5 more passing than F8c's own
1750-passed baseline (`PipelineTest`'s two new create-flag tests,
`GraphicsPipelineTest`'s own, and `CommandBufferTest`'s two new
bind-time-rejection tests), no regressions.

## Roadmap F10: measured impact (`VK_EXT_pipeline_robustness`)

`dEQP-VK.api.info.get_physical_device_properties2.features.pipeline_robustness_features`
passes cleanly:

```
Test case 'dEQP-VK.api.info.get_physical_device_properties2.features.pipeline_robustness_features'..
  Pass (Querying succeeded)
```

`dEQP-VK.robustness.pipeline_robustness.*` (`--deqp-case`, same reproduction
recipe as F9's own section above; 1667 cases actually executed under that
glob) is the CTS suite gated on `context.requireDeviceFunctionality
("VK_EXT_pipeline_robustness")`. Every one of its `NotSupported` reasons
is a *different*, already-documented prerequisite this ICD doesn't
implement -- never "VK_EXT_pipeline_robustness is not supported" itself,
confirming this row's own extension gate is genuinely passed, not
silently short-circuiting the whole suite:

| Reason | Count | Cause |
|---|---|---|
| `Scalar block layout not supported` | 996 | `scalarBlockLayout` (core 1.2, `VK_EXT_scalar_block_layout`) is unimplemented -- roadmap 1.9.10's K-series, unrelated to this row |
| `VK_EXT_graphics_pipeline_library not supported` | 534 | Not in this ICD's declared 1.4 conformance scope (an optional extension); every `_fast_gpl`/`_optimized_gpl` case variant needs it |
| `VK_EXT_shader_image_atomic_int64 is not supported` | 173 | Also not in scope; a 64-bit image-atomic capability this software rasterizer does not implement |
| `robustImageAccess not supported` | 24 | Roadmap E16's own deliberate scoping decision (`robustImageAccess` stays `VK_FALSE` pending an audit of `CommandBuffer.cpp`'s copy-command paths) -- this row does not reopen that decision, since `VkPipelineRobustnessCreateInfo::images` and the whole-device `robustImageAccess` feature are different, independently gateable things per the Vulkan spec |
| `Vertex pipeline stores and atomics not supported` | 22 | `vertexPipelineStoresAndAtomics` (core 1.0) unimplemented, unrelated |
| `Fragment shader stores not supported` | 12 | `fragmentStoresAndAtomics` (core 1.0) unimplemented, unrelated |
| `robustBufferAccess2 not supported` | 2 | `VK_EXT_robustness2` itself is unimplemented -- exactly the reason `EntryPoints.cpp`'s new `defaultRobustness*` values claim the weaker, non-`..._2` behavior rather than over-claiming it (see this row's own comment) |

None of these 1667 cases reach a `Pass` or `Fail` for this ICD today,
since every one needs at least one of the prerequisites above alongside
`VK_EXT_pipeline_robustness` itself; that is an honest reflection of this
ICD's current feature surface, not a defect in this row's own change --
each prerequisite is already tracked as its own separate roadmap item
(K-series, E16) rather than folded into F10.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1763 discovered, 1762 passed, 1
unsupported (pre-existing, unrelated) -- 7 more passing than F9's own
1755-passed baseline (the new `DrawTest` vertex-fetch-robustness case,
plus `PipelineTest`'s/`GraphicsPipelineTest`'s/`PhysicalDeviceInfoTest`'s
new pipeline-robustness cases), no regressions.

## Roadmap F11: measured impact (`VK_EXT_host_image_copy`)

`dEQP-VK.image.host_image_copy.*` (`--deqp-case`, same reproduction recipe
as F9/F10's own sections above): 73289 cases discovered.

```
Passed:        1121/73289 (1.5%)
Failed:        1496/73289 (2.0%)
Not supported: 70672/73289 (96.4%)
```

The `Not supported` majority is this row's own deliberately narrow
`getSupportedHostImageCopySrcLayouts`/`DstLayouts` list (`{GENERAL,
TRANSFER_{SRC,DST}_OPTIMAL}`): `HostImageCopyTestCase::checkSupport`
(`vktImageHostImageCopyTests.cpp`) skips any case whose own
`srcLayout`/`dstLayout`/`intermediateLayout` parameter isn't in that list
(e.g. every `color_attachment_optimal`/`depth_stencil_attachment_optimal`/
`shader_read_only_optimal` variant), never "`VK_EXT_host_image_copy` is
not supported" itself -- confirming the extension gate is genuinely
passed, exactly like F9/F10's own sections found for their own
extensions.

Every one of the 1496 `Fail`s breaks down as:

| Reason | Count | Cause |
|---|---|---|
| `retcode: VK_ERROR_INITIALIZATION_FAILED` (`vk.createGraphicsPipelines`), `error: feme-graphics-validate-stage: 'feme.stage.input.load' ... refers to element N with the wrong direction` | 1468 | A pre-existing vertex-input-direction gap in shader compilation, unrelated to this row -- the `depth_stencil` subgroup's own render-pass-based test draws a full-screen triangle, hitting a shader-compilation class of failure this ICD already has outside host image copy entirely (the same "resource handle this ICD's CPU target cannot normalize" class E6/F8's own reports note) |
| `vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED` | 4 | `feme-cpu-simdize`'s own divergent-value rejection, the same pre-existing pipeline-creation-class gap as the row above, coincidentally hit by four more `depth_stencil` cases |
| ~~`Depth copy failed`~~ | ~~24~~ | This row's own real, documented gap at the time -- `copyBufferImageRegion` (`ImageOps.cpp`) used to cleanly *reject* a single-aspect copy of a combined depth/stencil format (`D24_UNORM_S8_UINT`/`D32_FLOAT_S8X24_UINT`) rather than mis-sizing or crashing on it (see this row's own "two real, pre-existing gaps" note in Roadmap.md) -- these 24 `simple.{d16_unorm_s8_uint,d24_unorm_s8_uint,d32_sfloat_s8_uint}.*` cases were exactly that rejection surfacing as a clean `Fail` instead of `NotSupported`. **Fixed by roadmap F11a**, which implements the real per-aspect read-modify-write this row's own finding called for; see "Roadmap F11a: measured impact" below for the confirming re-run (all 24 now `Passed`, `Failed` down to 1472/73289 from this row's own original 1496/73289) |

None of the 1496 failures is a crash, a hang, or memory corruption --
every one is a clean `VkResult`/`llvm::Error` propagated back to a
`tcu::TestStatus::fail`, matching this codebase's own "an unimplemented
capability fails cleanly" convention throughout.

**Two real, pre-existing gaps this row's own first full run against this
group found** (see Roadmap.md's F11 row for the same finding, condensed
here with the actual before/after evidence):

1. **An outdated system Vulkan loader silently no-ops a purely-1.4-core
   command.** The first full run against this group crashed instead of
   producing any of the numbers above: `vkTransitionImageLayout`/
   `vkCopyMemoryToImage`/etc. (no pre-promotion `EXT`-suffixed alias any
   `--deqp-case` here calls) resolve to a *non-null* function pointer
   through this environment's installed loader (`libvulkan.so.1.3.275`,
   Ubuntu's `libvulkan1` package) even though this ICD's own
   `vkGetDeviceProcAddr` (confirmed via a standalone reproduction and
   temporary instrumentation of `ProcAddr.cpp`) is *never actually
   called* for any of the four core-spelled names -- the loader recognizes
   them as "core" from its own compiled-in, 1.3-vintage table and hands
   back a generic trampoline into a per-device dispatch slot it never
   populates for a command that postdates 1.3, so the very first call
   jumps through a null pointer. Building a current (1.4.328)
   `Vulkan-Loader` from source and re-running the identical failing case
   with `LD_LIBRARY_PATH` pointed at it instead made it pass cleanly --
   confirming this is this *environment's* loader version, not an ICD
   defect. `feme/docs/FeMeVulkanDesign.md`'s own scope already
   distinguishes the ICD from the loader it runs under (see "Project and
   Library Boundaries"); this finding is recorded here rather than acted
   on further, since upgrading the system's installed loader is outside
   this row's -- or this codebase's -- own scope.
2. **`copyBufferImageRegion`'s buffer-side sizing ignored a copy's own
   aspect for a combined depth/stencil format**, silently computing a row
   size from the *whole* image format (`D32_FLOAT_S8X24_UINT`'s 8 bytes)
   instead of the single named aspect's own smaller size (its depth
   aspect's real 4). `vkCmdCopyBufferToImage`'s own bound `VkBuffer` size
   usually caught the resulting oversized row first (a clean rejection,
   never previously noticed as a bug); `vkCopyMemoryToImage`'s raw host
   pointer has no such size to catch it against at all, turning the exact
   same latent mis-sizing into a real out-of-bounds host-memory read/write
   (observed as both a segfault inside `memcpy` and, non-deterministically
   depending on allocation timing, a "corrupted double-linked list" glibc
   heap-corruption abort). Fixed by rejecting the case cleanly in
   `copyBufferImageRegion` itself, which benefits the pre-existing
   `vkCmdCopyBufferToImage`/`vkCmdCopyImageToBuffer` commands too, not just
   this row's own new ones (the 24 `Depth copy failed` cases in the table
   above are that rejection); real per-aspect support is split off as
   roadmap F11a.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1773 discovered, 1772 passed, 1
unsupported (pre-existing, unrelated) -- 10 more passing than F10's own
1762-passed baseline (`ImageTest`'s new combined-depth/stencil-rejection
case, `HostImageCopyTest`'s six new cases, `EntryPointsTest`'s new
`VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT` case, and
`PhysicalDeviceInfoTest`'s two new dedicated feature/properties cases),
no regressions.

## Roadmap F11a: measured impact (combined depth/stencil aspect copy)

`dEQP-VK.image.host_image_copy.*` (`--deqp-case`, same reproduction recipe
as F9/F10/F11's own sections above, and the same environment/loader
workaround F11's own section documents -- `LD_LIBRARY_PATH` pointed at a
current, 1.4.328 `Vulkan-Loader` built from source, since this
environment's installed system loader (1.3.275) still silently no-ops the
purely-1.4-core commands this group also calls): 73289 cases discovered,
re-run against the exact same group F11's own section measured.

```
Passed:        1145/73289 (1.6%)
Failed:        1472/73289 (2.0%)
Not supported: 70672/73289 (96.4%)
```

`Passed` is up by exactly 24 and `Failed` down by exactly 24 from F11's
own 1121/1496 baseline -- precisely the 24 `Depth copy failed` cases F11's
own table attributed to `copyBufferImageRegion`'s clean rejection of a
single-aspect combined-depth/stencil copy. None of the 24
`simple.{d16_unorm_s8_uint,d24_unorm_s8_uint,d32_sfloat_s8_uint}.*` cases
appear anywhere in this re-run's failure list any more (`grep -c "Depth
copy failed"` on the new log is `0`); the remaining 1472 failures are
exactly F11's own two pre-existing, unrelated shader-compilation gaps
(1468 + 4, the same `vk.createGraphicsPipelines`-class failures F11's own
table already attributed to a gap this row's own scope does not touch),
confirming this row's fix neither missed a case nor introduced a
regression anywhere else in the group.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1786 discovered, 1785 passed, 1
unsupported (pre-existing, unrelated) -- 6 more discovered than an
intermediate rebuild with only this row's production-code fix applied and
its two old rejection-expecting tests (`ImageTest`'s
`CopyBufferToImageRejectsCombinedDepthStencilFormat`,
`HostImageCopyTest`'s `CopyMemoryToImageRejectsCombinedDepthStencilFormat`)
still in place, which failed for the expected reason (the rejection they
asserted no longer happens): `ImageFixtureTest` gains four new
`D32_FLOAT_S8X24_UINT`/region-copy cases, `ImageTest` replaces its one old
rejection case with two new real depth/stencil-aspect-copy cases plus one
new ambiguous-aspect-mask rejection case, and `HostImageCopyTest` replaces
its own one old rejection case with one new round-trip case, for a net six
more discovered and zero failing.

## Roadmap F12: measured impact (`VK_KHR_push_descriptor`)

`dEQP-VK.pipeline.monolithic.push_descriptor.*` (`--deqp-case`, same
reproduction recipe as F9/F10/F11's own sections above): 76 cases
discovered.

```
Passed:        0/76 (0.0%)
Failed:        76/76 (100.0%)
Not supported: 0/76 (0.0%)
```

Zero `Not supported`, confirming the extension gate itself is genuinely
passed (`VK_KHR_push_descriptor` advertised, every `checkSupport` in this
group's own test source accepted it) -- but every one of the 76 cases still
fails, all before this row's own new commands ever run: every failure is at
`vkCreateComputePipelines`/pipeline construction, not at
`vkCmdPushDescriptorSet`/`vkCmdPushDescriptorSetWithTemplate` themselves.
None is a crash, hang, or memory corruption; every one is a clean
`VkResult`/MLIR diagnostic propagated to `tcu::TestStatus::fail`, matching
this codebase's "an unimplemented capability fails cleanly" convention.

| Reason | Count | Cause |
|---|---|---|
| `vk.createComputePipelines(...): VK_ERROR_INITIALIZATION_FAILED` (`vkRefUtil.cpp:46`), no diagnostic printed | 33 | The same pre-existing, orthogonal gap "Roadmap E6: measured impact" already root-caused (a temporary debug print at `Pipeline.cpp`'s `compileComputePipeline` error path, that section's own methodology) for `secondary_push_constants_2`'s sibling `writeonly buffer Output { vec4 color; }`: a storage/uniform buffer block with one or two non-array `vec4` fields, not the `rtarray`/fixed-array shape this compiler's resource-handle normalization actually supports. Every `compute.binding*` buffer/image/sampler/texel-buffer case in this group declares its `Output`/`Input` block exactly that way, unrelated to push descriptors themselves -- all 33 are `compute.*` cases |
| `error: failed to legalize operation 'spirv.AccessChain' that was explicitly marked illegal` | 4 | A newly-found, unrelated gap (split off as roadmap F12a below), all 4 the `compute.incremental_updates*` cases: modeled by this report's own `PushDescriptorSetDispatchTest.WritesToOneBindingAccumulateAcrossPushes` unit test, minus the exact SPIR-V shape that trips this. The shader dynamically indexes a `layout(std140) uniform Input { uint data[16]; }` block by `gl_GlobalInvocationID.x`; the equivalent `std430 buffer` (storage buffer) array indexes dynamically without issue everywhere else in this report, so `std140`'s own wider (16-byte, vs. `std430`'s 4-byte) per-scalar-element array stride is the one shape difference this SPIR-V->LLVM lowering does not yet handle |
| `error: feme-cpu-simdize: ... divergent vector value ... (roadmap milestone 7 deviation)` | 39 | The same pre-existing SIMDize decomposition gap F5-F8's own sections already document repeatedly (e.g. "Roadmap F8: measured impact") -- every one of this group's `graphics.*` cases (all 39 of them) drives a per-quad fragment color through a divergent (per-invocation) value this compiler cannot yet decompose outside the small set of patterns it already handles |

**This row's own new mechanism was verified end to end a different way**,
since every CTS case in this group is blocked by one of the three
pre-existing, orthogonal gaps above before ever reaching it: `CommandBuffer.cpp`'s
`unittests/Vulkan/CommandBufferTest.cpp` gained a dedicated
`PushDescriptorSetDispatchTest` fixture -- reusing `kStorageBufferCopyShader`'s
own already-working `rtarray`-shaped storage buffers rather than a
CTS-shaped single-field block -- with five new cases proving `vkCmdPushDescriptorSet`
reads/writes through no `VkDescriptorSet` object at all, that a second push to
the same slot only rewriting one binding leaves the other's earlier push in
place (`WritesToOneBindingAccumulateAcrossPushes`, modeled on
`PushDescriptorIncrementalUpdatesComputeTest`'s own scenario, since that exact
CTS case is one of the four blocked by F12a above), that
`vkCmdPushDescriptorSetWithTemplate` and `vkCmdPushDescriptorSet2` produce the
same result, and that `vkBeginCommandBuffer` drops every push descriptor set
from the previous recording.

A targeted re-run of `dEQP-VK.api.command_buffers.secondary_push_descriptor_set_2`/
`secondary_push_descriptor_set_with_template` -- `NotSupported ("VK_KHR_push_descriptor
is not supported")` in "Roadmap E6: measured impact" above -- now attempts
both for real and hits the same non-array-block gap (`VK_ERROR_INITIALIZATION_FAILED`
at `vkCreateComputePipelines`) as `secondary_push_constants_2` already does,
not a new one: `VK_KHR_push_descriptor` is genuinely advertised and consumed,
even though this particular pre-existing shader-compilation gap still blocks
a `Pass`.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`, this
session's existing `./build`): 1780 discovered, 1779 passed, 1 unsupported
(pre-existing, unrelated) -- 7 more passing than F11's own 1772-passed
baseline (`CommandBufferTest`'s five new `PushDescriptorSetDispatchTest`
cases, `DescriptorTest`'s template-type-acceptance/rejection cases, and
`PhysicalDeviceInfoTest`'s new dedicated `VkPhysicalDevicePushDescriptorProperties`
case, netting one fewer than the eight new test names since one, `Descriptor
Test.PushDescriptorTemplateTypeIsRejected`, was renamed/repurposed to `...
IsAccepted` rather than added), no regressions.

## Roadmap F12a: measured impact (std140 uniform buffer array stride)

`dEQP-VK.pipeline.monolithic.push_descriptor.*` (`--deqp-case`, same
reproduction recipe as F9-F11's own sections above), re-run against the
exact same 76-case group F12's own section measured.

```
Passed:        0/76 (0.0%)
Failed:        76/76 (100.0%)
Not supported: 0/76 (0.0%)
```

Every count is unchanged from F12's own 0/76/0 baseline, but the *reason*
for the 4 `compute.incremental_updates*` cases' own failure changed: `grep
-c "failed to legalize operation 'spirv.AccessChain'"` on the new log is
`0` (down from 4), confirming this row's fix closes the exact diagnostic
its own text names. Those same 4 cases still fail `vkCreateComputePipelines`,
now with a different, previously-masked diagnostic instead --
`'llvm.getelementptr' op operand #0 must be LLVM pointer type ... but got
'vector<3xi32>'` -- split off as new roadmap row F12b rather than folded
into this one (see that row's own text for the root cause: this shader's
`gl_GlobalInvocationID.x` reaches an `spirv.AccessChain` pattern gap
entirely unrelated to `std140` array strides). The other two pre-existing
categories F12's own table already attributed (33 silent
`vk.createComputePipelines` failures, 39 `feme-cpu-simdize` divergent-vector
failures) are exactly unchanged in this re-run (`37` total
`vk.createComputePipelines`-failing cases either way -- 33 silent + 4 now
diagnosed, instead of 33 silent + 4 differently-diagnosed -- plus the same
39 `graphics.*` cases), confirming this row's fix neither missed a case
nor introduced a regression anywhere else in the group.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1790 discovered, 1789 passed, 1
unsupported (pre-existing, unrelated) -- 10 more discovered than F12's own
1780-discovered baseline: `SPIRVResourceLoweringTest` gains four new cases
covering `HandleKind::UniformArray` (a dynamically-indexed load, a rejected
store, a rejected conflicting-stride re-declaration, and the range-size
metadata each already-passing case implicitly covers), a new
`spirv-resource-lowering-uniform-array.ll` lit test covers the same shape
end to end through `feme::cpu::SPIRVResourceLoweringPass`, and a new
`read_std140_element` case is added to
`spirv-to-llvm-glslang-blocks.mlir` (one existing case in that same file,
`read_element`'s own natural-stride array, changed shape to match --
recognized as this row's own new wrapper shape too, rather than the
direct/unwrapped one it used before, since the two are now handled
identically regardless of whether the stride happens to be natural).

## Roadmap F12b: measured impact (builtin `Input` vector lane access chain)

`dEQP-VK.pipeline.monolithic.push_descriptor.*`, re-run against the exact
same 76-case group F12/F12a's own sections above measured.

```
Passed:        0/76 (0.0%)
Failed:        76/76 (100.0%)
Not supported: 0/76 (0.0%)
```

Every count is unchanged from F12a's own 0/76/0 baseline, and `grep -c
"getelementptr' op operand"`/`grep -c "failed to legalize operation
'spirv.AccessChain'"` on the new log are both `0`, confirming this row's
new `BuiltInAccessChainPattern` closes the exact diagnostic this row's own
text names without reopening F12a's own. The `vkCreateComputePipelines`
failure count for the group is unchanged too (37, same as F12a's own
re-run: 33 silent + 4 `compute.incremental_updates*`), but -- unlike
F12a's own diagnostic, which is only visible through `FEME_VULKAN_LOG_
CREATION_ERRORS` on the *graphics* pipeline path (`GraphicsPipeline.cpp`'s
own `logCreationFailure` call; `Pipeline.cpp`'s compute path has no
equivalent hook and always `consumeError`s silently) -- confirming what
now trips instead needed reproducing this row's own 4 cases' shader
directly through the standalone `feme` driver (`glslangValidator`-compiled
from the exact GLSL `PushDescriptorIncrementalUpdatesComputeTest::
initPrograms` embeds, then `feme --target=x86_64-unknown-linux-gnu`)
rather than through `deqp-vk` itself: before this row's own fix, that
reproduction fails identically to F12b's own text (`'llvm.getelementptr'
op operand #0 must be LLVM pointer type ... but got 'vector<3xi32>'`);
after it, SPIR-V->LLVM legalization succeeds, and the same shader instead
hits `feme: unsupported raised operation: '...
llvm.spv.resource.handlefrombinding...' is a register-bound resource
handle the FeMe CPU target cannot normalize into a heap access or the
root-constant block` -- the same pre-existing, already-tracked "resource
handle the FeMe CPU target cannot normalize" class of gap E6/F8's own
reports already note (a `std140` uniform block reached through a runtime
`vkCmdPushDescriptorSet` write, rather than a statically-bound descriptor
this ICD's CPU target can already normalize), not a new one this row
needs to split off. This closes F12b's own scope exactly as its own text
describes, without turning any of the 4 CTS cases green (their own
remaining failure was already out of scope before this row, and remains
so after it).

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1790 discovered, 1789 passed, 1
unsupported (pre-existing, unrelated) -- unchanged from F12a's own
1790-discovered baseline: the new `spirv-to-llvm-builtin-variables.mlir`
case (`read_global_invocation_id_x`, covering the new
`BuiltInAccessChainPattern`) is a new `CHECK` block inside an
already-counted lit test file, not a new discovered test of its own.

## Roadmap F13: measured impact (`VK_KHR_load_store_op_none`)

`dEQP-VK.renderpasses.*load_store_op_none*` (`--deqp-case`, same
reproduction recipe as F9-F12's own sections above): 273 cases discovered.

```
Passed:        0/273 (0.0%)
Failed:        151/273 (55.3%)
Not supported: 122/273 (44.7%)
```

Zero `Not supported ("VK_KHR_load_store_op_none is not supported")` --
confirming the extension gate itself is genuinely passed now that it is
advertised -- and every one of the 122 `Not supported` cases is instead
the pre-existing, unrelated "Depth-stencil format not supported"
(`vktRenderPassLoadStoreOpNoneTests.cpp:500`, a combined
`D32_FLOAT_S8X24_UINT`/`X8_D24_UNORM_PACK32` format this ICD does not
implement at all, orthogonal to load/store ops). Every one of the 151
failures is at pipeline construction, before this row's own load/store-op
handling (already correct, per its own closing note in Roadmap.md) is ever
reached:

| Reason | Count | Cause |
|---|---|---|
| `vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED` (`vkPipelineConstructionUtil.cpp:176`), `feme-cpu-simdize: ... divergent vector value ...` | 100 | The same pre-existing SIMDize decomposition gap F5-F12's own sections already document repeatedly (e.g. "Roadmap F12: measured impact") -- this group's own fragment shader drives a per-quad color through a divergent (per-invocation) value this compiler cannot yet decompose |
| `vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED` (`vkPipelineConstructionUtil.cpp:176`), no diagnostic printed | 46 | The `complete_secondary_cmd_buff` variant's own pipeline-library construction path, a pre-existing gap unrelated to load/store ops (this group's `renderpass2`/plain `dynamic_rendering` variants, which do not use pipeline libraries, hit the SIMDize gap above instead, not this one) |
| `vk.queueSubmit(...): VK_ERROR_INITIALIZATION_FAILED` (`vkCmdUtil.cpp:338`) | 5 | The `*_resolve` cases' multisample-resolve variant, downstream of the same pre-existing gaps above once a pipeline that failed to build is submitted anyway |

**This row's own load/store-op handling was verified end to end a
different way**, since every CTS case in this group is blocked by one of
the pre-existing, orthogonal gaps above before ever reaching a real draw:
`RenderPassTest.CompilesLoadStoreOpNone` proves `VK_ATTACHMENT_LOAD_OP_NONE`/
`STORE_OP_NONE` round-trip through `vkCreateRenderPass` unchanged, and
`DrawTest.LoadStoreOpNoneLeavesUntouchedTexelsAlone` proves the real
behavioral claim end to end: a `LOAD_OP_NONE` color attachment pre-filled
with a sentinel pattern keeps that sentinel outside a draw's own scissor
(unlike every other test in this file, whose `LOAD_OP_CLEAR` render pass
would have zeroed it), while the draw's own pixels still land normally,
confirming `STORE_OP_NONE` does not suppress them either.

A targeted re-run of `dEQP-VK.api.info.extension_core_versions.
extension_core_versions`/`vulkan1p2.feature_extensions_consistency`/
`vulkan1p3.feature_extensions_consistency` (the consistency checks a
newly-advertised extension name could most plausibly regress) all still
`Pass`, confirming `VK_KHR_load_store_op_none`'s addition to
`getSupportedDeviceExtensions` introduced no inconsistency with any other
advertised feature or extension.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1792 discovered, 1791 passed, 1
unsupported (pre-existing, unrelated) -- up from F12b's own 1790 by this
row's own two new regression tests (`RenderPassTest.
CompilesLoadStoreOpNone`, `DrawTest.LoadStoreOpNoneLeavesUntouchedTexelsAlone`).


## Roadmap F14: measured impact (`VK_KHR_map_memory2`)

`dEQP-VK.memory.mapping.*` (`--deqp-case`, same reproduction recipe as
F9-F13's own sections above -- this group is the only one in the whole
CTS tree that exercises `vkMapMemory2`/`vkUnmapMemory2` at all, one
`_map2`-suffixed variant per existing plain-`vkMapMemory`/`vkUnmapMemory`
case): 4466 cases discovered.

```
Passed:        4466/4466 (100.0%)
Failed:        0/4466 (0.0%)
Not supported: 0/4466 (0.0%)
```

The first attempt at this row crashed instead of running: a real
`deqp-vk` invocation SIGSEGV'd inside
`vkt::memory::(anonymous namespace)::testMemoryMapping` at a null
function pointer (`gdb -batch -ex run -ex bt`), the exact "frame 0 at
address 0x0" shape `agent_thoughts.md`'s own V0-era crash-isolation loop
first documented for a missing dispatch-table entry. Root cause,
confirmed by reading `vkInitDeviceFunctionPointers.inl`: this CTS
checkout only loads `vkMapMemory2`/`vkUnmapMemory2` (the core,
non-`KHR`-suffixed names this driver first implemented) when its own
negotiated `usedApiVersion` for a test is `>= 1.4`, falling back to the
`KHR` name otherwise -- and this driver's `icd.json` `api_version`
(`1.1.0`) makes the *core* name unreachable through the real system
Vulkan loader for any caller below that, the same "loader cannot reach a
newer core name" gap `vk_gen_entrypoints.py`'s `SUPPORTED_EXTENSIONS`
comment already documents for `VK_KHR_maintenance5`'s granularity/
subresource-layout commands. Implementing `vkMapMemory2KHR`/
`vkUnmapMemory2KHR` as thin wrappers around the core names (`Memory.cpp`)
and adding `VK_KHR_map_memory2` to `SUPPORTED_EXTENSIONS` so the
generator's dispatch table actually carries the `KHR` names fixed it: the
full group above then ran clean, with every `_map2` variant passing
alongside its pre-existing plain counterpart.

A targeted re-run of `dEQP-VK.api.info.extension_core_versions.
extension_core_versions` (the consistency check a newly-advertised
extension name could most plausibly regress, per F13's own section
above) still `Pass`es, confirming `VK_KHR_map_memory2`'s addition to
`getSupportedDeviceExtensions` introduced no inconsistency with any other
advertised feature or extension.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1795 discovered, 1794 passed, 1
unsupported (pre-existing, unrelated) -- up from F13's own 1792 discovered
by this row's own three new `MemoryTest` cases
(`MapMemory2WriteUnmap`, `MapMemory2RejectsOutOfRange`,
`UnmapMemory2AcceptsReserveBit`, plus the extension-count/name updates to
`DrawTest.AdvertisesDynamicRenderingExtension`) and the generator's own
`vk-gen-entrypoints-split-features.test` fixture update.


## Roadmap H2: measured impact (layered rendering + `multiview`)

`dEQP-VK.multiview.*` (`--deqp-case`, same reproduction recipe as
F9-F14's own sections above): 838 cases discovered.

```
Passed:        0/838 (0.0%)
Failed:        499/838 (59.5%)
Not supported: 339/838 (40.5%)
```

This is a much narrower "closed" than this row's own roadmap text
("Closes ... the whole `dEQP-VK.multiview` group") claimed, and every
one of the 499 failures traces to a cause outside this row's own file
scope (`RenderPass.cpp`/`CommandBuffer.cpp`/`Executor.cpp`'s object model
and per-view draw loop):

| Cause | Cases | Detail |
|---|---|---|
| `feme-cpu-simdize` "divergent vector value ... component decomposition is not yet supported" | 454 | A *different* shape than the four `phi`/`select`/`shufflevector`/`extractelement` patterns roadmap C3 already closed -- C3's own fix (confirmed still in place) does not reach whatever these multiview fragment shaders' own divergent, per-invocation output does. Root cause unstarted; tracked as new roadmap row H2a |
| `vk.createRenderPass(2)(...): VK_ERROR_FORMAT_NOT_SUPPORTED` | 28 | `dEQP-VK.multiview.{,renderpass2.}view_mask_iteration.*`'s own `VK_FORMAT_R8G8B8A8_UINT` color attachment -- an integer format `isSupportedColorAttachmentFormat` (`RenderPass.cpp`) correctly, and pre-existingly, declines (see "every other format is ... an integer format no fragment output writes yet" in that function's own comment). Roadmap H8's mandatory-format-table gap, not a new one |
| `vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED` (dynamic-rendering `view_mask_iteration`) | 14 | Same `VK_FORMAT_R8G8B8A8_UINT` cause as above, surfacing at pipeline creation instead of render-pass creation because dynamic rendering has no separate render-pass object to reject it at |
| `vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED` (`depth_without_fragment_shader`, all three render-pass-type variants) | 3 | A pipeline with zero color attachments (a legal, depth-only Vulkan pipeline shape): `feme::graphics::executeDraws` unconditionally requires at least one (`"a draw needs at least one color attachment"`). Not multiview-specific; tracked as new roadmap row H2b |

The remaining 339 `NotSupported` cases are legitimate: `dEQP-VK.multiview.
secondary_cmd_buffer_geometry.*` needs `geometryShader` (still `VK_FALSE`,
roadmap H5), and a handful need `VK_EXT_depth_range_unrestricted`

## Roadmap H2a: measured impact (root-causing the divergent-vector shape)

Reproduced the same 454-case `feme-cpu-simdize` failure this row's own
finding names, with `errs()`-instrumented, un-committed local diagnostics
(the diagnostic itself carries no instruction/function detail beyond an
always-empty `Value::getName()`) added temporarily to
`FunctionWidener::checkVectorDecompositionSupported`
(`feme/lib/Transforms/CPU/SIMDize.cpp`) to print the offending instruction,
its user, and the whole function -- reverted before this row's own commit,
since the actual fix (see below) needs none of it.

**This row's own framing ("`dEQP-VK.multiview`'s own fragment shaders")
does not hold.** Every one of the 454 cases is the *vertex* shader's
`gl_Position` write, not a fragment shader's output at all -- confirmed by
running one representative case
(`dEQP-VK.multiview.clear_attachments.no_queries.15`) and dumping the
function that fails:

```llvm
%1 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
%2 = insertelement <4 x float> poison, float %1, i64 0
...
%8 = insertelement <4 x float> %6, float %7, i64 3
store <4 x float> %8, ptr addrspace(8) @spirv_var_13, align 4   ; gl_Position
...
call void @feme.cpu.masked.stage.output.store.f32(i32 3, i32 0, i32 0, float %24, i32 0, i1 true) ; out_color, correctly legalized
```

`@spirv_var_13`'s type -- `{ <4 x float>, float, [1 x float], [1 x float] }`
-- is glslang's implicit `gl_PerVertex` interface *block* (`Position`,
`PointSize`, `ClipDistance[1]`, `CullDistance[1]`), and it carries **no**
`!spirv.Decorations` metadata at all (confirmed by dumping every global in
the module), unlike every other stage-IO global in the same function
(`out_color`'s own store, three lines below the failing one, is already a
correctly-legalized `feme.cpu.masked.stage.output.store` call). Root cause:
SPIR-V decorates a `BuiltIn` interface block's members individually
(`OpMemberDecorate ... BuiltIn Position`, one per member), not the block
variable itself (`OpDecorate`) the way an ordinary standalone builtin
variable is -- but `SPIRVToLLVMPatterns.cpp`'s `buildStageIODecorationsAttr`
only ever reads a *whole-variable* `built_in`/`location` attribute
(`Op.getBuiltIn()`), never a struct type's own per-member decorations
(`mlir::spirv::StructType::getMemberDecorations`, already used elsewhere in
this file for a storage-buffer block's `NonWritable` member decoration --
see `isBufferBlockWritable`). `feme::graphics::CanonicalizeStagePass`'s
`isSPIRVStageIOGlobal` (`CanonicalizeStage.cpp`) requires that metadata to
recognize a stage-IO global at all, so the whole block is silently
skipped, and its store is left exactly as raw as it started, reaching
`feme::cpu::SIMDizePass` directly -- which correctly diagnoses the
now-divergent vector value it was never supposed to see in the first
place, since `gl_Position` is virtually always computed from per-vertex
attribute data (divergent across the wave by construction).

This explains the case count precisely: every `dEQP-VK.multiview` vertex
shader writes `gl_Position` (mandatory in every vertex shader), so this one
gap reaches essentially the entire suite regardless of which other feature
each subgroup (`clear_attachments`, `draw_indexed`, `masks`, `instanced`,
...) exercises -- 454 distinct cases across 19 top-level subgroups (see
each one's own `Test case` lines in the reproduction log), all failing with
byte-for-byte the same diagnostic text.

**Triage against roadmap C8, as this row's own text asked for**: this is
*not* the same shape as C8b's matrix/aggregate `insertvalue`/`extractvalue`
finding -- C8b's own gap is in `feme::cpu::SIMDizePass` itself, once a
value has *already* been correctly legalized into `feme.stage.*` calls;
this gap never reaches that pass's legalized form at all, since
`CanonicalizeStagePass` never recognizes the global in the first place. It
is closer to (but distinct from) C8's original, since-closed finding about
a raw, un-canonicalized `Input`/`Output` global reaching `feme-cpu-simdize`
directly: that finding's root cause was "`CanonicalizeStagePass` is never
invoked at all" (fixed by wiring it into `runPipeline`, though the fix
never mattered for a real `deqp-vk` case, `GraphicsPipeline.cpp` having
always called it directly). This finding's root cause is narrower and
different: `CanonicalizeStagePass` *is* invoked, but its own
`isSPIRVStageIOGlobal` recognition has a scope gap -- a struct-typed,
per-member-decorated graphics builtin interface block was never a shape
either roadmap R19 (SPIR-V import) or R20 (`feme.stage.*` legalization)
covered; both were scoped to "a non-builtin `Input`/`Output` variable" and
"a builtin *variable*" (`BuiltInGlobalVariablePattern`), neither of which
describes a builtin interface *block*. It is a new member of C8's "shader
long tail" bucket, exactly as this row's own text predicted, not a new
root-cause bucket of its own.

A regression test locking down the current (still-broken) behavior,
`CanonicalizeStageTest.DoesNotRecognizeMemberDecoratedInterfaceBlockAsStageIO`
(`unittests/Transforms/Graphics/CanonicalizeStageTest.cpp`), reproduces the
exact shape found above (a struct-typed, undecorated `Output` global) at
the `CanonicalizeStagePass` unit level, independent of a real SPIR-V
import -- notably, the *existing* `MapsSPIRVBuiltInsToSystemValues` test's
own `@gl_Position` fixture turns out to have modeled `gl_Position`
incorrectly all along (as a standalone, whole-variable-decorated global,
which it never actually is), which is exactly why this gap went unnoticed
by the existing unit suite and was only caught by a real `deqp-vk` run.

This row does not implement the fix -- extending both
`buildStageIODecorationsAttr` (to recognize and preserve a struct-typed
interface block's per-member decorations) and `CanonicalizeStage.cpp`'s
`isSPIRVStageIOGlobal`/signature-building/load-store legalization (to
decompose the block into one `SignatureElement` per member, each keeping
its own `BuiltIn`/system-value identity, rather than the current one
value-equals-one-element assumption) is nontrivial enough to warrant its
own measured row -- see roadmap rows H2c and H2d.
(unimplemented, out of this row's scope).

No case in the group passes outright: every one of its color/depth
attachments and fragment shaders happens to hit one of the three causes
above (mostly the SIMDize gap, which this run found affects the large
majority of the group's own shader shapes). Since the CTS run itself
cannot isolate whether the *multiview* wiring specifically (as opposed to
those three surrounding gaps) is correct, a new self-contained regression
test, `DrawTest.MultiviewRendersDifferentColorPerViewIntoItsOwnLayer`,
exercises exactly that in isolation instead: a two-layer framebuffer, a
render pass with `viewMask == 0b11`, and a fragment shader that reads
`gl_ViewIndex` (`BuiltIn ViewIndex`) to pick red for view 0 and green for
view 1 -- confirmed each view's own color lands in its own array layer
(layer 0 red, layer 1 green), not both clobbering layer 0. Two more new
`RenderPassTest` cases confirm a layered framebuffer is accepted when its
view covers enough layers and rejected when it does not, and
`PhysicalDeviceProperties2Test.MultiviewFeaturesReportMultiviewTrueAmplificationFalse`
confirms the flipped feature bit.

A targeted re-run of `dEQP-VK.api.info.get_physical_device_properties2.
features.multiview_features` and `dEQP-VK.api.info.vulkan1p2_limits_validation.
khr_multiview` (the two cases that most directly probe `multiview`'s own
feature/limit reporting) both `Pass`, and
`dEQP-VK.api.info.vulkan1p2.{feature,property}_extensions_consistency`/
`vulkan1p3.{feature,property}_extensions_consistency` still `Pass` too,
confirming `multiview`'s new `VK_TRUE` and `VK_KHR_multiview`'s addition
to `getSupportedDeviceExtensions` introduced no inconsistency with any
other advertised feature or extension. (One unrelated, pre-existing
failure was observed in the same sweep --
`get_physical_device_properties2.features.shader_subgroup_rotate_property_consistency_khr`,
a `VkPhysicalDeviceShaderSubgroupRotateFeaturesKHR`/`Vulkan11Properties`
mismatch with no connection to this row's own files -- left untouched.)

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1800 discovered, 1799 passed, 1
unsupported (pre-existing, unrelated) -- up from F14's own 1795 discovered
by this row's own five new regression tests
(`RenderPassTest.{RenderPass2AcceptsMultiviewAndRecordsViewMask,
RenderPass2AcceptsNonZeroDependencyViewOffset,
FramebufferAcceptsLayeredAttachmentWithEnoughLayers,
FramebufferRejectsViewWithTooFewLayers,FramebufferRejectsZeroLayers}`,
`DrawTest.MultiviewRendersDifferentColorPerViewIntoItsOwnLayer`, and
`CanonicalizeStageTest.MapsSPIRVViewIndexBuiltInToSystemValue` -- six new
cases plus two renamed/rewritten ones for the flipped `multiview`
behavior, `PhysicalDeviceProperties2Test.
MultiviewFeaturesReportMultiviewTrueAmplificationFalse` and
`RenderPassTest.{RenderPass2AcceptsMultiviewAndRecordsViewMask,
RenderPass2AcceptsNonZeroDependencyViewOffset}`, plus the extension-count
update to `DrawTest.AdvertisesDynamicRenderingExtension`).

This edition's regeneration of `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` also found and restored one more instance
of the F13-discovered `AdvertisedPromotedFeatures.txt`/
`AdvertisedPromotedExtensions.txt`/`AdvertisedExtensions.txt` drift, this
time from roadmap F14: `VK_KHR_map_memory2` was genuinely implemented
(`Memory.cpp`) but never recorded in any of the three tracking files,
understating the 1.4 extension row as 14 of 16 rather than its true 15 of
16 and the total advertised-extension count as 29 rather than 31 (with
`VK_KHR_multiview` itself the other +1). Restored alongside this row's
own bookkeeping.

## Roadmap H2c: measured impact (builtin interface block per-member decorations)

`dEQP-VK.multiview.*` (`--deqp-case`, same reproduction recipe as F9-F14's
own sections above): 838 cases discovered.

```
Passed:        0/838 (0.0%)
Failed:        499/838 (59.5%)
Not supported: 339/838 (40.5%)
```

Identical to roadmap H2's own baseline and to H2a's confirmation of it.
This is the expected outcome, not a regression: H2c only teaches the
`spirv` -> `llvm` dialect conversion to preserve a builtin interface
block's per-member decorations as a new `feme.spirv.member.decorations`/
`feme.spirv.MemberDecorations` side channel (see `buildMemberDecorationsAttr`
in `SPIRVToLLVMPatterns.cpp` and `collectStageIOMemberDecorations`/
`attachStageIOMemberDecorations` in `StageIODecorations.cpp`).
`feme::graphics::CanonicalizeStagePass`'s `isSPIRVStageIOGlobal` still only
recognizes a stage-IO global by its whole-variable `!spirv.Decorations`
metadata, which a builtin interface block's global still does not carry
(only the new per-member channel does) -- so `gl_Position`'s write through
the implicit `gl_PerVertex` block is, exactly as before H2c, left
completely un-legalized, reaching `feme::cpu::SIMDizePass` raw and
triggering the same 454-case "divergent vector value ... component
decomposition is not yet supported" diagnostic H2a root-caused. Consuming
the new metadata to actually decompose the block into per-member signature
elements is H2d's own scope, not this row's.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1811 discovered, 1810 passed, 1
unsupported (pre-existing, unrelated) -- up from H2's own 1800 discovered
by this row's own five new `SPIRVToLLVMTest` cases
(`BuiltinInterfaceBlockPreservesMemberDecorations,
UnrecognizedMemberDecorationIsFilteredOut,
CollectStageIOMemberDecorationsFindsDecoratedGlobals,
AttachStageIOMemberDecorationsBuildsMetadata,
AttachStageIOMemberDecorationsIgnoresMissingGlobals`) and the new lit
cases (`spirv-to-llvm-stage-io.mlir`'s new `gl_PerVertex` split, and the
new `spirv-to-llvmir-stage-io-member-decorations.mlir`).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: unchanged --
this row touches no feature/extension advertisement, only an internal
SPIR-V -> LLVM conversion detail.

## Roadmap H2d: measured impact (per-member interface block decomposition)

`dEQP-VK.multiview.*` (`--deqp-case`, same reproduction recipe as F9-F14's
own sections above): 838 cases discovered.

```
Passed:        78/838 (9.3%)
Failed:        421/838 (50.2%)
Not supported: 339/838 (40.5%)
```

Up from H2/H2a/H2c's own 0/838 baseline -- the first real pass count this
group has ever had. **Deviation**: the row's own text ("closes the whole
`dEQP-VK.multiview` group's own remaining largest blocker") is accurate in
the narrow sense that landed (the SIMDize-crash blocker H2a root-caused is
gone, and 78 cases now genuinely pass), but a first real `deqp-vk` run
against the initial landing found the fix incomplete in a way no unit test
had caught: `CanonicalizeStageTest`'s own fixtures (and this row's first
commit) modeled a builtin interface block as loaded/stored *as one whole
aggregate value* (`store {<4 x float>, float, [1 x float], [1 x float]}
%agg, ptr addrspace(8) @gl_PerVertex`) -- a shape that, per this
investigation, **never occurs in practice**. Real SPIR-V-derived IR
addresses each member -- and even each individual component of
`gl_Position` -- with its own scalar load/store instead: SPIR-V's own
offset-0 member access (`gl_Position.x`) folds down to a bare global
load/store with no `getelementptr` at all (LLVM's own constant-folding, a
zero-offset GEP being a no-op), and every other member/component is a
`getelementptr (i8, ptr @block, i64 ByteOffset)` `ConstantExpr` -- LLVM's
own canonical byte-offset form, not the struct-member-indexed shape
(`getelementptr StructTy, ptr @block, i32 0, i32 M`) a naive
`GetElementPtrInst`-only walk would expect. The first landing's
`cast<StructType>` (assuming every multi-`ElementID` global's load/store
value is the whole struct) crashed immediately on the first real
`gl_PerVertex.Position` store it saw (`dEQP-VK.multiview.clear_attachments
.no_queries.15`, an assertion failure, not a diagnosed error). A follow-up
commit replaces the whole-struct-only resolution with
`getStageIOBaseAndOffset` (`Value::stripAndAccumulateConstantOffsets`,
which uniformly unwraps both `GetElementPtrInst` and `ConstantExpr` GEP
chains) and `resolveStageIOAccess`, which uses the block's own
`StructLayout` to turn a constant byte offset into (member, row,
component), reusing `loadStageIOValue`/`storeStageIOValue`'s existing
recursion for whichever sub-shape remains.

With that fix, the crash is gone and 78 cases pass outright. The remaining
421 failures break down as:

| Failure | Count | Root cause |
| --- | --- | --- |
| `vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED` (`depth_without_fragment_shader`, all three render-pass-type variants) | 3 | A pipeline with zero color attachments. Pre-existing, tracked as roadmap H2b |
| `vk.createRenderPass(2)(...): VK_ERROR_FORMAT_NOT_SUPPORTED` (`view_mask_iteration.*`) | 28 | `VK_FORMAT_R8G8B8A8_UINT` color attachment, an integer format `isSupportedColorAttachmentFormat` correctly declines. Pre-existing, tracked as roadmap H8 |
| `vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED` (`dynamic_rendering.view_mask_iteration.*`) | 14 | Confirmed (via `FEME_VULKAN_LOG_CREATION_ERRORS=1`) to be `"color attachment 0 names a format this driver cannot render into"` -- the *same* `VK_FORMAT_R8G8B8A8_UINT` root cause as the row above, just surfacing at `vkCreateGraphicsPipelines` instead of `vkCreateRenderPass(2)` since `dynamic_rendering` has no render pass object to reject it earlier. Also roadmap H8, not a new finding |
| `vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED` (`input_instance.*`, all three render-pass-type variants) | 24 | A **new** finding: `error: feme-graphics-validate-stage: 'feme.stage.input.load' ... refers to element 5 with the wrong direction`. This shader's own GLSL reads back an `Output` it already wrote in the same invocation (SPIR-V permits reading an `Output` storage-class variable after writing it, e.g. for a compound `gl_PerVertex.gl_Position.x += 1.0`-shaped update -- confirmed against this test's own dumped IR, a `load float, ptr addrspace(8) @gl_PerVertex` immediately following a `store` to the same address). `feme.stage.input.load`/`.output.store`'s own Input-vs-Output dichotomy (and `ValidateStagePass`'s enforcement of it) has no representation for "read back what this invocation already wrote" -- a real hardware rasterizer's input/output storage is normally physically separate, but SPIR-V's `Output` storage class does not make that same promise. New roadmap row H2e |
| `Fail (occlusion availability bit N is 0 ...)` (`non_precise_queries_with_availability.*`) | 18 | A **new**, unrelated finding: occlusion-query availability reporting appears incorrect under multiview specifically. Not investigated further (out of this row's own interface-block-decomposition scope); new roadmap row H2f |
| `Fail (Fail)` (image comparison) | 334 | A **new**, large finding spanning most subgroups (`renderpass2`/`dynamic_rendering` variants of `clear_attachments`, `index`, `secondary_cmd_buffer`, `readback_{implicit,explicit}_clear`, `multisample{,_resolve}`, `masks`, `instanced`, `input_attachments`, `draw_indirect{,_indexed}`, `draw_indexed`, `stencil`, `depth`). Every one of these previously hit the SIMDize crash (H2a) before ever reaching image comparison, so this is the *first* time their actual rendered output has been checked at all. Spot-checking `instanced.no_queries.15` (a plain, unconditional multi-branch `gl_Position`/`out_color` write, no read-back) shows this is not simply H2e's own read-back gap recurring -- a genuine rendering-correctness bug, root cause not yet determined. New roadmap row H2g |

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, `LLVM_CCACHE_BUILD=ON`,
this session's existing `./build`): 1814 discovered, 1813 passed, 1
unsupported (pre-existing, unrelated) -- up from H2c's own 1811 by this
row's own new `CanonicalizeStageTest` cases
(`RecognizesMemberDecoratedInterfaceBlockAsStageIO`,
`RecognizesInterfaceBlockPerMemberByteOffsetAccess`, and the renamed
`DoesNotRecognizeUndecoratedInterfaceBlockAsStageIO`) and the two new
`spirv-canonicalize-stage-interface-block{,-byte-offset}.ll` lit tests.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: unchanged --
this row touches no feature/extension advertisement, only
`CanonicalizeStagePass`'s own SPIR-V-derived-IR legalization.

## Roadmap H2e: measured impact (`Output` storage-class read-back)

`dEQP-VK.multiview.*` (`--deqp-case`, same reproduction recipe as F9-F14's
own sections above): 838 cases discovered.

```
Passed:        78/838 (9.3%)
Failed:        421/838 (50.2%)
Not supported: 339/838 (40.5%)
```

Identical headline totals to H2d's own baseline -- expected, and not a
sign the fix did nothing: this row's own scope is a single root cause
(`'feme.stage.input.load' ... refers to element N with the wrong
direction`, `input_instance`'s 24 cases), and every one of those 24 no
longer hits that diagnostic (`grep -c "wrong direction"` against the full
log now returns 0, down from 24). What changed is *which* bucket they fall
into: all 24 now build and run to completion, and reach real image
comparison -- landing in the same `Fail (Fail)` bucket roadmap H2g already
tracks (334 -> 358), rather than failing earlier at
`vkCreateGraphicsPipelines` with a diagnosed compiler error. This is the
same "closes the root cause, not the test" shape H2d's own report
predicted for exactly this row ("a real, targeted run" would be needed to
tell whether fixing the read-back gap alone makes `input_instance` pass,
versus merely uncovering a different, pre-existing failure underneath --
H2g's own rendering-correctness gap turns out to be that different
failure).

The fix itself: `canonicalizeSPIRVStage` now routes every `Output`-
direction leaf scalar (one per (`ElementID`, `Row`, `Component`), the same
granularity `loadStageIOValue`/`storeStageIOValue`'s existing recursion
already decomposes every access to) through its own shadow `AllocaInst`
(`ShadowValueMap`), rather than lowering an `Output` read-back into a
wrong-direction `feme.stage.input.load`. Every rewritten store also writes
through to its own shadow alloca; once every instruction in the function
has been rewritten, `PromoteMemToReg` converts every shadow alloca to SSA
form, resolving each read-back to the dominance-correct reaching store
(inserting a `phi` for a real control-flow join, e.g. `input_instance`'s
own `if (gl_VertexIndex == 1) gl_Position.y += 1.0f;`-shaped guard) --
exactly the SSA construction a compiler's own `mem2reg` pass performs for
a local variable, which a linear "last stored value" scan could not do
correctly in general. `CanonicalizeStageTest` gains
`OutputReadBackResolvesToStoredValueStraightLine` and
`OutputReadBackResolvesAcrossControlFlow` (the latter confirming the `phi`
case); a new `spirv-canonicalize-stage-output-readback.ll` lit test covers
the same control-flow-join shape end-to-end. `ninja check-feme`
(assertions-enabled, ccache build) passes in full, 1817/1818 (1
pre-existing, unrelated `Unsupported`), up from 1814/1815 before this row.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: unchanged --
this row touches no feature/extension advertisement, only
`CanonicalizeStagePass`'s own SPIR-V-derived-IR legalization.

## Roadmap H2f: measured impact (multiview occlusion query availability)

`dEQP-VK.multiview.*` (`--deqp-case`, same reproduction recipe as F9-F14's
own sections above): 838 cases discovered.

```
Passed:        96/838 (11.5%)
Failed:        403/838 (48.1%)
Not supported: 339/838 (40.5%)
```

Up from H2e's own 78/838, and every one of the additional 18 passes is
`non_precise_queries_with_availability`'s own case set -- `grep -c
"availability bit"` against the full log now returns 0, down from 18. Root
cause: per the Vulkan spec, `vkCmdBeginQuery`/`vkCmdEndQuery` for an
occlusion query recorded inside a multiview render pass instance
implicitly span one query index per set bit of the active subpass's view
mask (ordered by ascending bit position), not the single index a
non-multiview query uses. `feme::vulkan::QueryPool`/`CommandBuffer.cpp`
had no representation for this at all: `begin`/`markAvailable` always
touched exactly one index, and `runDraw`'s per-view loop summed every
rendered view's own passed-sample count into one shared `PassedSamples`
scalar applied to every currently-active query pool broadcast-style
(`accumulateActiveOcclusionSamples`) -- so a two-view query's second
(and every further) implicit index was simply never written, staying
permanently unavailable, and its first index held the sum across both
views rather than either view's own count.

The fix: `QueryPool::begin`/`markAvailable` take a `ViewCount` (the
popcount of `GraphicsState::Binding.ViewMask` at `vkCmdBeginQuery` time,
1 outside multiview) and operate on `[Query, Query+ViewCount)`;
`accumulateActiveOcclusionSamples`'s "broadcast to every active index" is
replaced by `accumulateOcclusionSamples(Query, Samples)`, adding to one
explicit index. `CommandBuffer.cpp` tracks each active occlusion query as
an explicit `ActiveOcclusionQuery{Pool, FirstQuery, ViewCount}` (found at
`vkCmdEndQuery` time by matching `(Pool, FirstQuery)`) rather than a plain
`QueryPool *` set, and `runDraw`'s per-view loop now resets its own
`PassedSamples` counter for each rendered view and routes each view's own
count to its own enumerated query index.

`DrawTest.MultiviewOcclusionQueryWritesOneQueryIndexPerView` (a two-view
`viewMask == 0b11` render pass, a single `vkCmdBeginQuery`/
`vkCmdEndQuery` pair at query index 0) locks this down at the unit level
-- confirmed to fail against the pre-fix code with exactly this row's own
symptom (query index 0 holding the summed total, query index 1 permanently
unavailable) before the fix landed. `ninja check-feme`
(`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in full, 1818/1819 (1
pre-existing, unrelated `Unsupported`), up from 1817/1818 before this row.

The remaining 403 failures are unchanged from H2d/H2e's own breakdown
(358 `Fail (Fail)` image-comparison cases tracked by roadmap H2g, 17
`vkCreateGraphicsPipelines` failures split between roadmap H2b's 3 and
roadmap H8's 14 `dynamic_rendering.view_mask_iteration` cases, and 28
`vkCreateRenderPass(2)` `VK_ERROR_FORMAT_NOT_SUPPORTED` cases tracked by
roadmap H8) -- this row's own scope is the multiview occlusion-query
availability gap alone, and touches none of those other buckets.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: unchanged --
this row touches no feature/extension advertisement, only
`QueryPool`/`CommandBuffer.cpp`'s own occlusion-query bookkeeping.

## Roadmap H2g: measured impact (the triage pass, and the two root causes it found)

`dEQP-VK.multiview.*` (`--deqp-case`, same reproduction recipe as F9-F14's
own sections above): 838 cases discovered.

Baseline (H2f's own numbers):

```
Passed:        96/838 (11.5%)
Failed:        403/838 (48.1%)
Not supported: 339/838 (40.5%)
```

After both of this row's own fixes:

```
Passed:        420/838 (50.1%)
Failed:         79/838 (9.4%)
Not supported: 339/838 (40.5%)
```

**Triage method**: rather than guess, this row extracted the actual
`Result`/`Reference` PNGs the `Fail (Fail)` cases' own `--deqp-log-images`
output embeds (one `ImageSet` per rendered layer, plus a montage `Result`
set) and diffed them pixel-by-pixel. `instanced.no_queries.15` (the case
H2d/H2e's own text already spot-checked) showed a very specific shape: its
two rendered layers were each internally uniform-but-swapped -- the top
half of the rendered image held the color the reference expected in the
bottom half, and vice versa, in both layers identically. That is exactly
what a vertical (Y-axis) mirror of an otherwise-correct image looks like,
not a wrong color computation.

**Root cause 1 (the majority: 270 of the 358, `96/838` &rarr; `366/838`)**:
`feme::graphics::Executor::executeDraws`'s `projectVertex` (the Vulkan
"coordinate transformations" viewport transform) maps clip-space `NdcY`
into window space as `(1 - (NdcY * 0.5 + 0.5)) * Height` -- i.e. it flips
Y. That flip is correct for DXIL/HLSL's own clip space (Y increases
*upward*, matching Direct3D's NDC convention, so the flip converts it to
window space's Y-down layout) but wrong for SPIR-V/GLSL, whose clip space
already increases *downward* -- identical to window space, needing no
flip at all. Grepping the whole `feme/lib` tree found no compensating
negation anywhere in the SPIR-V import/canonicalization path (`grep -rn
FNeg feme/lib` turns up nothing related to `Position` at all): a SPIR-V
vertex shader's `gl_Position.y` reaches the shared executor completely
unmodified, gets the DXIL-shaped flip applied anyway, and comes out
upside down. Confirmed directly against `clear_attachments.no_queries.15`
(a plain, `gl_PerVertex`-free-of-multiview-specific-logic quad-color test):
its top-left texel held quad 1's color (the quad authored at NDC `y in
[0, 1]`) where the reference expected quad 0's (`y in [-1, 0]`) -- exactly
the pre-existing `ExecutorTest` unit tests' own documented convention
("Y flipped by the viewport transform") applied to an SPIR-V-sourced
position it was never meant to apply to.

Fix: `canonicalizeSPIRVStage` (`CanonicalizeStage.cpp`) now negates a
`SignatureSystemValue::Position` *output* store's Y component
(`negateSystemValuePositionY`) -- a whole-vector `gl_Position = ...` store
negates lane 1; a single-component `gl_Position.y = ...` store negates
directly when its (statically-known) component is 1. A `Position`
*input* (`gl_FragCoord`/`SV_Position` in a fragment shader, the other
reuse of the same `SystemValue`, per `getSystemValueForBuiltIn`'s own
comment) is untouched, since it is already a genuine, correctly-oriented
window-space value in both APIs and the fix only ever runs on the store
side. This is the first place either SPIR-V or DXIL import applies any
sign convention at all to a position value; since this repository's own
graphics test suite (`ExecutorTest`, `PreparedDrawTest`, `SceneTest`) all
construct their clip-space positions directly in IR (bypassing SPIR-V
import entirely), none of them depend on the pre-fix behavior, and no
currently-passing non-multiview `dEQP-VK` case reaches real image
comparison yet (every one this session tried -- `dEQP-VK.draw.
renderpass.basic_draw.draw.triangle_list.1`,
`dynamic_rendering.complete_secondary_cmd_buff.basic_draw.draw.
triangle_list.1` -- still fails at `vkCreateGraphicsPipelines`, an
unrelated, pre-existing gap), so this fix could only improve, never
regress, real rendered output. Two of this repository's own SPIR-V-
sourced unit tests *did* encode the pre-fix convention in their input
data and needed updating to match: `spirv-canonicalize-stage-interface-
block-byte-offset.ll`'s own `gl_Position.y` store (expected value negated
from `2.0` to `-2.0`) and `DrawTest.DynamicLineWidthWidensTheLine`'s
`LineVertexSource` (NDC `y` flipped from `0.25` to `-0.25`, `-0.25` now
being the real, spec-correct value for a 4-row target's row-1 pixel
center).

**Root cause 2 (54 more of the 358 -- all of `clear_attachments`/
`readback_explicit_clear`, plus 6 of `readback_implicit_clear`'s 24;
`366/838` &rarr; `420/838`)**: `vkCmdClearAttachments`'s
`clearAttachmentRects` (`ImageOps.cpp`) resolved and wrote directly into
one attachment view with no awareness of multiview at all -- unlike
`CommandBuffer.cpp`'s own `runDraw`, which slices each color/depth/stencil
attachment down to the current view's own array layer
(`sliceAttachmentLayer`) once per set `viewMask` bit before ever calling
into the executor. Per the Vulkan spec, a clear rect's own
`baseArrayLayer`/`layerCount` are relative to the current subpass's view
mask inside a multiview render pass instance, exactly like a draw's own
implicit per-view replication -- but this repository's clear path simply
ignored `RenderTargetBinding::ViewMask` and always wrote layer 0 alone.
Confirmed against `clear_attachments.no_queries.15`'s own per-layer PNGs
once root cause 1 was fixed: layer 0 matched the reference exactly, but
layers 1-3 each still held their pre-clear (drawn) content in the region
the reference expected the `vkCmdClearAttachments` blue rectangle to
have overwritten.

Fix: `clearAttachmentRects` now loops over `ViewMask`'s own set bits (`1`,
outside multiview -- the same normalization `runDraw` already uses) and
slices the resolved `AttachmentView` down to each one's own array layer
first, reusing the same byte-offset arithmetic `sliceAttachmentLayer`
already established (`feme::graphics::getAttachmentLayerByteOffset`).
`DrawTest.MultiviewClearAttachmentsClearsEveryViewsOwnLayer` (a two-view
`viewMask == 0b11` render pass: a full-screen red draw, then a
`vkCmdClearAttachments` covering the whole render area) locks this down
at the unit level.

**What's left (34 of the 358, newly triaged into two independent gaps --
see roadmap H2h/H2i for the full writeup)**: 16 `input_attachments`
cases render a totally blank image (every pixel black/transparent, not
merely a wrong color or missing rectangle -- a different failure shape
from either root cause above, not yet investigated); 18 of
`readback_implicit_clear`'s 24 still fail, but only its multi-subpass
view-mask combinations (`1_2_4_8`, `5_10_5_10`, `8_1_1_8`, etc.) --
every single-subpass case (e.g. `15`) now passes, suggesting a load-op/
multi-subpass interaction this row's own `vkCmdClearAttachments` fix does
not cover.

The remaining 45 failures are unchanged from H2d/H2e/H2f's own breakdown:
`vkCreateGraphicsPipelines`/`vkCreateRenderPass(2)` failures split between
roadmap H2b's 3 and roadmap H8's 14 + 28.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full, 1820/1821 (1 pre-existing, unrelated `Unsupported`), up from
1818/1819 before this row (the one new `DrawTest.
MultiviewClearAttachmentsClearsEveryViewsOwnLayer` case).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: unchanged --
both fixes correct existing rendering/clear behavior; neither advertises,
nor requires advertising, any new feature or extension.

## Roadmap H2h: measured impact

`dEQP-VK.multiview.*` (`--deqp-case`, same reproduction recipe as every
row above): 838 cases.

Baseline (H2g's own numbers):

```
Passed:        420/838 (50.1%)
Failed:         79/838 (9.4%)
Not supported: 339/838 (40.5%)
```

After this row's fix:

```
Passed:        436/838 (52.0%)
Failed:         63/838 (7.5%)
Not supported: 339/838 (40.5%)
```

All 16 `input_attachments`/`renderpass2.input_attachments` cases named in
H2g's own triage now pass (confirmed via `--deqp-case=dEQP-VK.multiview.
input_attachments.*`/`renderpass2.input_attachments.*`, 8/8 and 8/8);
`dynamic_rendering`'s own `input_attachments_geometry` variants (24 cases)
remain, unaffected, `NotSupported` (`geometryShader` is not implemented --
unrelated to this fix). The other 63 remaining `Failed` cases are every
one of H2g's own already-tracked, unrelated gaps: 18
`readback_implicit_clear` multi-subpass cases (roadmap H2i, untouched by
this fix, as expected -- a different attachment path entirely), 42
`view_mask_iteration` (`VK_FORMAT_R8G8B8A8_UINT`, roadmap H8), and 3
`depth_without_fragment_shader` (roadmap H2b) -- `79 - 16 = 63` matches
exactly.

**Root cause**: `RenderTargetBinding` (`RenderPass.h`) -- the shape both
a classic `VkRenderPass`+`VkFramebuffer` and a `vkCmdBeginRendering`
instance normalize into before reaching `CommandBuffer.cpp`'s draw path --
never captured `SubpassDescription::InputAttachments` at all, only
`Colors`/`Depth`/`Stencil`. `buildSubpassInputHeap`'s only attachment-
resolution mechanism was `ColorIndexFor`/`Gfx.ColorAttachmentInputIndices`,
an identity-mapping fallback built solely for `VK_KHR_dynamic_rendering_
local_read` (which has no separate classic input-attachment list, and so
maps a `subpassInput`'s `InputAttachmentIndex` directly onto one of the
current subpass's own color attachments, `Attachments`). That fallback
was silently reused for classic render passes too, which is wrong
whenever a subpass's input attachment is *not* one of its own color
attachments -- exactly `dEQP-VK.multiview.input_attachments`'s own shape,
where a later subpass reads back an *earlier* subpass's color output
through a `subpassInput`. Since that earlier attachment was never present
in `Attachments` (the later subpass's own color-attachment list) at all,
`subpassLoad` addressed index 0 against the later subpass's own (freshly-
cleared) color attachment instead -- reading transparent-black, not the
earlier subpass's real content, and rendering every pixel of every one
of the 16 cases blank.

Fix: `RenderTargetBinding` gains an `Inputs` field (one resolved
`ImageView *` per `InputAttachmentIndex`, `nullptr` for
`VK_ATTACHMENT_UNUSED`), populated by `buildRenderTargetBinding` from
`SubpassDescription::InputAttachments`, resolved against the whole
framebuffer's own attachment list (not just the current subpass's color
attachments). `runDraw` resolves and (per multiview view) slices these
into a new `SubpassInputs` list exactly like every other attachment kind,
and threads it into a new `buildSubpassInputHeap` parameter of the same
name. When non-empty, `SubpassInputs` is now authoritative -- `Subpass
Inputs[I]` maps directly onto input-attachment index `I`, bypassing the
old identity-mapping fallback entirely; that fallback still applies,
unchanged, whenever `SubpassInputs` is empty (i.e. every `vkCmdBegin
Rendering` instance, which always leaves `RenderTargetBinding::Inputs`
empty since it has no classic input-attachment list of its own). The two
paths cannot conflict.

`DrawTest.MultiviewInputAttachmentReadsBackAnEarlierSubpassColorOutput`
(a two-subpass classic `VkRenderPass`, `viewMask == 0b11` both subpasses:
subpass 0 writes attachment 0 solid red per-view; subpass 1 declares
attachment 0 -- not one of its own color attachments, which is attachment
1 -- as its input attachment, reads it back with the existing
`SubpassLoadFragmentSource` shader, and writes solid green to attachment
1) locks this fix down at the unit level; confirmed (via `git stash` of
just the fix, keeping the new test) that it fails -- reading uninitialized/
garbage bytes instead of green -- on the pre-fix code, and passes with the
fix restored.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full, 1762/1821 passed (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from 1761/1820 before this row's own new `DrawTest` case.
The 59-vs-1 `Unsupported` count differs from H2g's own claimed "1
pre-existing Unsupported" -- believed to be sandbox/Vulkan-loader-version
sensitivity in this session's own environment rather than anything this
row's change affects (`Failed` is 0 in both cases either way, which is
what determines pass/fail here).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: unchanged --
this fix corrects existing input-attachment rendering behavior under a
classic multi-subpass `VkRenderPass`; it advertises no new feature or
extension (`multiview`/`VK_KHR_multiview` were already `yes`/`Advertised`
since roadmap H2, and `VK_KHR_dynamic_rendering_local_read`'s own input-
attachment support, since roadmap F8/F8a, is untouched).


## Roadmap H2i: measured impact

`dEQP-VK.multiview.readback_implicit_clear.*` (`--deqp-case`, same
reproduction recipe as every row above): 24 cases
(`renderpass2`/`dynamic_rendering` variants included).

Baseline (H2g/H2h's own numbers -- untouched by H2h's own fix, which was
a different attachment path entirely):

```
Passed:         6/24 (25.0%)
Failed:        18/24 (75.0%)
Not supported:  0/24 (0.0%)
```

After this row's fix:

```
Passed:        24/24 (100.0%)
Failed:         0/24 (0.0%)
Not supported:  0/24 (0.0%)
```

A full `dEQP-VK.multiview.*` run (838 cases) rose from H2h's own 436/838
(52.0%) to 454/838 (54.2%) passed, `Failed` falling from 63 to 45 --
exactly this row's own 18 cases. The other 45 remaining `Failed` cases
are every one of H2h's own already-tracked, unrelated gaps: 42
`view_mask_iteration` (`VK_FORMAT_R8G8B8A8_UINT`, roadmap H8) and 3
`depth_without_fragment_shader` (roadmap H2b) -- `63 - 18 = 45` matches
exactly.

**Root cause**: two compounding bugs in `CommandBuffer.cpp`'s render-pass
load-op path, found by re-running the 8 `readback_implicit_clear.no_
queries.*` cases individually against the baseline build:

```
Test case 'dEQP-VK.multiview.readback_implicit_clear.no_queries.15'..
  Pass (Pass)
Test case 'dEQP-VK.multiview.readback_implicit_clear.no_queries.15_15_15_15'..
  Pass (Pass)
Test case 'dEQP-VK.multiview.readback_implicit_clear.no_queries.1_2_4_8'..
  Fail (Fail)
Test case 'dEQP-VK.multiview.readback_implicit_clear.no_queries.1_2_4_8_16_32'..
  Fail (Fail)
Test case 'dEQP-VK.multiview.readback_implicit_clear.no_queries.5_10_5_10'..
  Fail (Fail)
Test case 'dEQP-VK.multiview.readback_implicit_clear.no_queries.8'..
  Fail (Fail)
Test case 'dEQP-VK.multiview.readback_implicit_clear.no_queries.8_1_1_8'..
  Fail (Fail)
Test case 'dEQP-VK.multiview.readback_implicit_clear.no_queries.max_multi_view_view_count'..
  Fail (Fail)
```

`8` is a *single*-subpass case (`viewMask == 0b1000`, view 3 alone) and
still failed -- disproving the roadmap row's own framing ("exactly the
cases whose numeric suffix names more than one subpass") and pointing at
a first, simpler bug independent of multi-subpass anything: `applyClear`
(the `VK_ATTACHMENT_LOAD_OP_CLEAR` handler run at `vkCmdBeginRenderPass`/
`vkCmdBeginRendering`) never sliced its target attachment by array layer
at all -- unlike H2g's own `clearAttachmentRects` fix for
`vkCmdClearAttachments`, it iterated `Y`/`X`/`S` and computed each texel's
offset directly against `Attachment->Width`/`Height`, with no per-`View
Mask`-bit loop and no call to `sliceAttachmentLayer` -- so it always
cleared only the *first* attachment layer's own byte range, regardless of
which views `RenderTargetBinding::ViewMask` actually named. A mask that
happened to include view 0 (`15`, `15_15_15_15`) passed by coincidence;
any mask that didn't (`8`) failed outright, real multi-subpass shape or
not.

Fixing bug 1 alone is not sufficient for the multi-subpass cases, though:
`vkCmdNextSubpass` never called `applyLoadOps` at all -- only
`vkCmdBeginRenderPass`/`vkCmdBeginRendering` did. Every one of this
test's variants binds a *single* color attachment shared by every
subpass, each declaring its own view mask (`1_2_4_8`: four subpasses,
masks `0b0001`/`0b0010`/`0b0100`/`0b1000`, one view each). Per the Vulkan
spec, an attachment's load op fires "at the beginning of the subpass
where it is first used" -- and, per `imageData`'s own reference-image
construction in `vktMultiViewRenderTests.cpp` (which accumulates each
layer's expected background color per *subpass*, walking the view masks
in execution order), that "first use" is tracked per *view*, not once for
the whole attachment: a view no earlier subpass's mask included still
needs its own share of the clear the first time a later subpass's mask
introduces it. Since `nextSubpass` never re-ran `applyLoadOps`, subpasses
1-3 of `1_2_4_8` (views 1-3) never got their own share of the clear at
all, leaving their layers with whatever the freshly bound image's memory
already held.

Naively calling `applyLoadOps` at every `nextSubpass` is not correct
either, though: `5_10_5_10`'s masks (`0b0101`/`0b1010`/`0b0101`/`0b1010`)
repeat -- subpass 2 re-uses view 0 and view 2, both already used (and,
in a case with real per-subpass draws layered on top, already *drawn
into*) by subpass 0. Re-clearing them again at subpass 2 would erase
subpass 0's own content. Fix (`CommandBuffer.cpp`): `applyClear` gains a
`ViewMask` parameter and a `GraphicsState::LoadedAttachmentViewMask` map
(keyed by each attachment's own `ImageView*`, with the low pointer bit
repurposed to distinguish the stencil half of a combined depth/stencil
attachment from its `Depth` counterpart, since both otherwise share one
`ImageView*`) it both reads and updates: only the bits of `ViewMask` not
yet marked loaded for that attachment are cleared (now correctly sliced
per view, fixing bug 1 too), and the *entire* `ViewMask` is recorded as
loaded afterward, whether or not this particular attachment's `LoadOp`
was actually `CLEAR` (a `LOAD`/`DONT_CARE` subpass touching a view still
means no *later* subpass may clear over it, since `LoadOp` is fixed
per-attachment for the whole render pass and cannot differ between
subpasses). `applyLoadOps` is now called at every `vkCmdNextSubpass`
too, not just at the render-pass instance's own start; both call sites
reset `LoadedAttachmentViewMask` to empty first (a fresh instance has
loaded nothing yet).

`DrawTest.MultiviewLoadOpClearAppliesEachSubpassOwnNewlyIntroducedViews`
(a three-subpass classic `VkRenderPass`, one shared two-layer color
attachment: subpass 0, `viewMask == 0b01`, draws solid red into view 0;
subpass 1, `viewMask == 0b10`, draws nothing, relying solely on its own
share of the load op to paint view 1's layer with the clear color;
subpass 2, `viewMask == 0b01` again, also draws nothing) locks both
fixes down at the unit level at once -- view 1's layer being the clear
color (not left as whatever the fresh image's memory held) exercises the
`nextSubpass` fix, and view 0's layer still being subpass 0's own red
(not reset to the clear color by subpass 2's own re-use) exercises the
`LoadedAttachmentViewMask` guard. Confirmed (via `git stash` of just the
`CommandBuffer.cpp` fix, keeping the new test) to fail on the pre-fix
code -- view 1 held uninitialized image memory rather than the clear
color -- and pass with the fix restored.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full, 1763/1822 (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from 1762/1821 before this row's own new `DrawTest` case.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: confirmed no
change needed -- this fix corrects existing multiview render-pass
load-op behavior; it advertises no new feature or extension
(`multiview`/`VK_KHR_multiview` were already `yes`/`Advertised` since
roadmap H2).

## Roadmap H2b: measured impact (zero color attachments)

`dEQP-VK.multiview.depth_without_fragment_shader*` (`--deqp-case`, same
reproduction recipe as every row above): 3 cases (the base group plus its
`dynamic_rendering`/`renderpass2` siblings, matching this row's own
originally-cited count).

Before this row's fix:

```
Test case 'dEQP-VK.multiview.depth_without_fragment_shader.no_queries.3_6_12_9_6_12_9_3_6_12_9_3'..
  Fail (vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED at vkRefUtil.cpp:37)
```

`FEME_VULKAN_LOG_CREATION_ERRORS=1` attributes this to `GraphicsPipeline.
cpp`'s own `"a graphics pipeline needs at least one color attachment"`
check (`Targets->Colors.empty()`), one step ahead of `feme::graphics::
executeDraws`'s analogous `"a draw needs at least one color attachment"`
this row's own text names -- pipeline creation never gets far enough to
reach the executor at all.

After this row's two fixes (`executeDraws`'s and `GraphicsPipeline.cpp`'s
own empty-`Colors`/empty-`Attachments` rejections both relaxed):

```
Test case 'dEQP-VK.multiview.depth_without_fragment_shader.no_queries.3_6_12_9_6_12_9_3_6_12_9_3'..
vkCreateGraphicsPipelines: a graphics pipeline needs both a vertex and a fragment stage
  Fail (vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED at vkRefUtil.cpp:37)
```

Still `Fail`, at the same call, for a different reason: with the color-
attachment checks out of the way, `GraphicsPipeline.cpp`'s unconditional
"both stages required" check is next in line. `deqp-vk`'s own test
(`vktMultiViewRenderTests.cpp`) does not merely bind a fragment shader
that writes no color output, as this row's own title assumed -- it omits
`VK_SHADER_STAGE_FRAGMENT_BIT` from `VkGraphicsPipelineCreateInfo::
pStages` entirely, which is separately legal Vulkan whenever the bound
render target has no color attachments. This is a distinct, materially
larger gap (making the fragment stage itself optional through pipeline
creation, compilation, and the executor's per-quad shading loop) than
the attachment-count relaxation this row's own text scoped to; it is
spun off as new roadmap row H2j rather than folded into this one.

A full `dEQP-VK.multiview.*` run (838 cases) after both of this row's
own fixes:

```
Passed:        454/838 (54.2%)
Failed:         45/838 (5.4%)
Not supported: 339/838 (40.5%)
```

Unchanged from H2i's own baseline (454/838, 45 `Failed`) -- expected,
since this row's own named case (and its two siblings) still fail, just
at a later point in pipeline creation than before. `Vulkan14
FeatureInventory.md`/`VulkanExtensionInventory.md`: confirmed no change
needed -- this row relaxes existing attachment-count validation; it
advertises no new feature or extension.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full, 1765/1824 (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from 1763/1822 before this row's own two new tests
(`ExecutorTest.RendersWithZeroColorAttachments`,
`GraphicsPipelineTest.AcceptsZeroColorAttachments`).

## Roadmap H2j: measured impact (fragment stage genuinely optional)

`dEQP-VK.multiview.depth_without_fragment_shader*` (`--deqp-case`, same
reproduction recipe as every row above): 3 cases (the base group plus its
`dynamic_rendering`/`renderpass2` siblings), same case H2b's own row left
failing.

Before this row's fix:

```
Test case 'dEQP-VK.multiview.depth_without_fragment_shader.no_queries.3_6_12_9_6_12_9_3_6_12_9_3'..
vkCreateGraphicsPipelines: a graphics pipeline needs both a vertex and a fragment stage
  Fail (vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED at vkRefUtil.cpp:37)
```

After making the fragment stage genuinely optional (`translateFixedFunctionState`
only requiring a vertex stage; `compileAndValidateStages`/`validateStageInterfaces`
skipping fragment compilation and its half of interface validation; `GraphicsPipeline`'s
`FragmentStage` becoming a genuinely optional `shared_ptr<CompiledStage>`; and
`executeDraws` skipping its whole fragment-invocation loop):

```
Test case 'dEQP-VK.multiview.depth_without_fragment_shader.no_queries.3_6_12_9_6_12_9_3_6_12_9_3'..
vkCreateGraphicsPipelines: the pipeline declares 1 color blend state(s) but its render target has 0 color attachment(s)
  Fail (vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED at vkRefUtil.cpp:37)
```

Still `Fail`, at the same call, for a different, previously-latent reason
(this row's own Deviation): `translateColorBlendState` still unconditionally
validated `Info->attachmentCount == Targets.Colors.size()`, but a pipeline
with no fragment shader has no fragment output interface, so
`pColorBlendState` -- including its own `attachmentCount` -- must be
entirely ignored, not validated
(`VUID-VkGraphicsPipelineCreateInfo-renderPass-06055`'s scope only applies
when a fragment output interface state is present). `deqp-vk`'s own test
(`vktMultiViewRenderTests.cpp`) hardcodes `pColorBlendState.attachmentCount = 1`
unconditionally regardless of whether the render target actually has any
color attachments, exactly triggering this. Fixed by threading a
`HasFragmentStage` flag into `translateColorBlendState` that skips the
`attachmentCount` check (and the rest of `pColorBlendState`) whenever it is
false; tightly coupled to this row's own change, so fixed as part of it
rather than spun off to another row.

After both fixes:

```
Test case 'dEQP-VK.multiview.depth_without_fragment_shader.no_queries.3_6_12_9_6_12_9_3_6_12_9_3'..
  Pass (Pass)

Test case 'dEQP-VK.multiview.dynamic_rendering.depth_without_fragment_shader.no_queries.3_6_12_9_6_12_9_3_6_12_9_3'..
  Pass (Pass)

Test case 'dEQP-VK.multiview.renderpass2.depth_without_fragment_shader.no_queries.3_6_12_9_6_12_9_3_6_12_9_3'..
  Pass (Pass)

Test run totals:
  Passed:        3/3 (100.0%)
```

All 3 cases this row (and H2b before it) cite now pass.

A full `dEQP-VK.multiview.*` run (838 cases) after this row's fixes:

```
Passed:        457/838 (54.5%)
Failed:         42/838 (5.0%)
Not supported: 339/838 (40.5%)
```

Up from H2b's own 454/838 (54.2%, 45 `Failed`) baseline by exactly the 3
named cases, with no regressions elsewhere (`Failed` drops from 45 to 42,
`Not supported` unchanged). `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md`: confirmed no change needed -- this row makes
an existing, always-legal pipeline shape (a fragment-less depth/stencil-only
pipeline) actually work; it advertises no new feature or extension.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full, 1769/1828 (59 pre-existing, unrelated `Unsupported`, 0 `Failed`), up
from 1768/1827 before this row's own four new tests
(`GraphicsPipelineTest.AcceptsMissingFragmentStage`,
`.RejectsMissingFragmentStageWithColorAttachments`,
`.AcceptsMissingFragmentStageWithMismatchedColorBlendState`,
`ExecutorTest.RendersWithNoFragmentStage`).

## Roadmap H3: measured impact (multiple viewports and scissors)

`dEQP-VK.draw.*.shader_viewport_index.*` (`--deqp-case`, all four
render-target-path variants: `renderpass`, `dynamic_rendering.primary_cmd_buff`,
`dynamic_rendering.partial_secondary_cmd_buff`,
`dynamic_rendering.complete_secondary_cmd_buff`; `vktDrawShaderViewportIndexTests.cpp`):
196 cases.

Before this milestone (`maxViewports == 1`, `multiViewport == VK_FALSE`,
`vkCmdSetViewportWithCount`/the pipeline path both rejecting anything but
one viewport):

```
Test case 'dEQP-VK.draw.renderpass.shader_viewport_index.vertex_shader_2'..
NotSupported (Requested core feature is not supported: multiViewport at vktTestCase.cpp:1497)

Test run totals:
  Passed:          0/196 (0.0%)
  Failed:          0/196 (0.0%)
  Not supported: 196/196 (100.0%)
```

Every case in the group short-circuits at `checkSupport` on the
`multiViewport` feature bit alone -- `deqp-vk` never even reaches a
viewport count above 1, so this group measured nothing about this
milestone's own code before it, only the bit's own absence.

After `PhysicalDeviceInfo.h`'s `MaxViewportCount` (16) replacing the old
hard-coded `maxViewports == 1`, `GraphicsPipeline.cpp`'s
`translateViewportState`/`DynamicGraphicsState`/`GraphicsPipelineState`
carrying full viewport/scissor arrays, `CommandBuffer.cpp`'s
`vkCmdSetViewport{,WithCount}`/`vkCmdSetScissor{,WithCount}` writing into
(and, for the `WithCount` pair, truncating) that same array,
`Executor.cpp`'s `resolvePrimitiveState` routing each primitive through the
array element its `ViewportArrayIndex` stage output names, and
`PhysicalDeviceInfo.cpp` advertising `multiViewport = VK_TRUE`:

```
Test run totals:
  Passed:         64/196 (32.7%)
  Failed:         68/196 (34.7%)
  Not supported:  64/196 (32.7%)
```

Every `vertex_shader_N` case (`gl_ViewportIndex` written from the vertex
stage, read back only by the fixed-function viewport/scissor/clip
transform, never by the fragment shader itself) now passes: 64/64 across
all four render-target-path variants (16 each). The 64 `Not supported`
are `geometry_shader_N`/`tessellation_*` variants this ICD has no
geometry or tessellation stage to run yet (roadmap H4/H5, unchanged by
this milestone).

**Deviation**: the remaining 68 `Failed` are every `fragment_shader_N`
case (17 each across the four render-target-path variants, including
`fragment_shader_implicit`) -- these additionally read `gl_ViewportIndex`
*back* as a flat-shaded fragment-shader input (`GL_ARB_shader_viewport_layer_array`'s
other half: `out_color = color[gl_ViewportIndex]`), not merely write it
from the vertex stage:

```
Test case 'dEQP-VK.draw.renderpass.shader_viewport_index.fragment_shader_2'..
error: feme-cpu-wrap-fragment: fragment stage wrapper requires attached feme.signature metadata
  Fail (vk.createGraphicsPipelines(device, pipelineCache, 1u, pCreateInfo, pAllocator, &object): VK_ERROR_INITIALIZATION_FAILED at vkRefUtil.cpp:37)
```

Root cause not yet isolated to a single line, but narrowed to the
fragment-stage signature/metadata-attachment path (`FragmentWrapper.cpp`'s
`lowerFragmentStageOps` finding no `feme.signature` metadata on the
fragment entry function at all, rather than a metadata mismatch): every
existing consumer of a `SignatureSystemValue` decorated builtin in this
codebase is either an *output* (`Position`, `RenderTargetArrayIndex`,
`ViewportArrayIndex` from a pre-rasterization stage) or one of the small
set of fragment-stage builtin *inputs* already wired up before this
milestone (`FragCoord`/`Position`, `FrontFacing`, `SampleId`, `SampleMask`);
`ViewportArrayIndex` read back as a genuine fragment-stage input appears
to be new, uncovered territory this milestone's own scope (the roadmap
text names `ViewportIndex` only "as a stage output") did not include.
Broken out as roadmap H3a to track and fix on its own, since root-causing
and fixing the fragment-stage signature path is a materially separate
piece of work from the viewport-count/pipeline/executor plumbing this row
itself closes.

`dEQP-VK.multiview.*` (838 cases, the same regression check every H2-series
row above ran, since this milestone's `CommandBuffer.cpp`/`Executor.cpp`
changes touch the same per-view attachment-slicing and per-primitive
layer-resolution code multiview itself depends on):

```
Passed:        457/838 (54.5%)
Failed:         42/838 (5.0%)
Not supported: 339/838 (40.5%)
```

Unchanged from H2j's own 457/838 (54.5%, 42 `Failed`) baseline -- this
milestone introduces no multiview regression, confirming the restored
per-view `ResolveAttachments`/`DepthStencil` slicing (this row's own
bugfix over the stashed partial progress it started from, needed to keep
`Executor.cpp`'s `getDrawLayerCount()` agreeing with every attachment kind
once again) is correct.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full, 1776/1835 (59 pre-existing, unrelated `Unsupported`, 0 `Failed`), up
from 1771/1830 before this row's own five new tests
(`GraphicsPipelineTest.AcceptsMultipleViewportsAndScissors`,
`.RejectsTooManyViewports`, `DrawTest.DynamicViewportWithCountRoutesInstancesToDifferentViewports`,
`.OutOfRangeViewportIndexDiscardsThePrimitive`,
`PhysicalDeviceInfoTest.ShaderViewportIndexLayerFeaturesAreTrue`, plus
`OnlyRobustBufferAccessDualSrcBlendAndASTCLDRAreAdvertised` renamed/extended
to `OnlyRobustBufferAccessDualSrcBlendASTCLDRAndMultiViewportAreAdvertised`).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` updated:
`multiViewport` (1.0) and `shaderOutputViewportIndex`/`shaderOutputLayer`
(1.2) move from `no`/`VK_FALSE` to `yes`/`VK_TRUE` (46 of 150 features
advertised, up from 43); `VK_EXT_shader_viewport_index_layer` moves from
"Planned" to "Implemented (core, not advertised by name)" (18 of 51
core-but-unadvertised extensions, up from 17; 49 of 150 planned, down from
50).

## Roadmap H3a: measured impact (`gl_ViewportIndex` as a fragment-shader input)

`dEQP-VK.draw.*.shader_viewport_index.fragment_shader_*` (`vktDrawShaderViewportIndexTests.cpp`,
`initFragmentTestPrograms`): 68 cases (17 each across the four
render-target-path variants, including `fragment_shader_implicit`).

**Environment note**: `deqp-vk` must be run with
`VK_ICD_FILENAMES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json`
explicitly set. The shell's ambient `VK_ICD_FILENAMES` (Mesa Lavapipe's
`lvp_icd.json`) silently makes every case in this group appear to pass
against the *wrong* driver -- a false positive discovered while reproducing
this row, since Lavapipe fully supports `gl_ViewportIndex` as a fragment
input and this milestone's own bug therefore never showed up until feme's
own ICD was targeted explicitly.

Before this row (H3's own baseline, reproduced against feme's own ICD):

```
Test case 'dEQP-VK.draw.renderpass.shader_viewport_index.fragment_shader_2'..
error: feme-cpu-wrap-fragment: fragment stage wrapper requires attached feme.signature metadata
  Fail (vk.createGraphicsPipelines(device, pipelineCache, 1u, pCreateInfo, pAllocator, &object): VK_ERROR_INITIALIZATION_FAILED at vkRefUtil.cpp:37)

Test run totals:
  Passed:          0/68 (0.0%)
  Failed:         68/68 (100.0%)
  Not supported:   0/68 (0.0%)
```

Root cause was **not** the fragment-stage-signature-attachment path H3's
own triage suspected (`CanonicalizeStage.cpp`'s builtin-to-`SignatureSystemValue`
mapping and its `InputGlobals`/`OutputGlobals` collection loop were
re-verified correct for this exact shape via a hand-written minimal
`gl_ViewportIndex`-reading fragment shader run through `feme-opt
-passes=feme-graphics-canonicalize-stage`: `!feme.signature` metadata was
attached correctly, both on a single pass run and simulating the real
double-run pipeline). The real dEQP shader differs from that minimal repro
in one way: it reads `gl_ViewportIndex` to index into a bound `Colors`
uniform block (`out_color = color[gl_ViewportIndex]`) -- and that bound
resource is what actually triggers the bug, four independent gaps deep:

1. **`SPIRVResourceLowering.cpp`'s `addResourceEnvParams`** (and the
   DXIL-oriented twin, `ResourceLowering.cpp`'s own identically-shaped
   helper) rewrites any function using a bound resource handle into a new
   `Function` via `Function::Create`+`copyAttributesFrom` to append the
   resource-heap ABI parameters. `llvm::GlobalObject::copyAttributesFrom`
   copies calling convention/attributes/linkage/GC/personality/prefix-
   prologue data, but **not** function-attached metadata -- so the
   `!feme.signature` metadata `CanonicalizeStagePass` had already attached
   was silently dropped the moment the fragment entry function touched its
   bound UBO, explaining exactly the observed error and why it was
   invisible to any repro without a bound resource. Fixed with an explicit
   `NewF->copyMetadata(&F, /*Offset=*/0)` right after `copyAttributesFrom`
   in both files.
2. Once metadata survived, **`FragmentWrapper.cpp`'s `loadFragmentSystemValue`**
   had no `case SignatureSystemValue::ViewportArrayIndex`, hitting the
   "unsupported fragment system value for element 0" error path (the small
   set of fragment-input builtins wired up before this milestone --
   `FragCoord`/`Position`, `FrontFacing`, `SampleId`, `SampleMask` -- never
   included it). Fixed by adding a new per-lane `ViewportIndex` field to
   `FemeFragmentInvocation` (`RuntimeABI.h`) and the mirrored
   `FragmentInvocationField` enum/`getFragmentInvocationType` builder
   (`StageArgsLayout.h`), plus the new read case in `loadFragmentSystemValue`.
3. Once codegen succeeded, JIT'ing the real shader failed with `Symbols
   not found: [ feme.cpu.resource.load.raw.v4f32 ]`: `ResourceCalls.cpp`'s
   `mangleResourceCallName`/`isSupportedRawElementType` already generically
   support vector-typed raw/structured buffer element loads, but
   `FeMeRuntimeCPU.c` only ever defined the scalar
   `feme.cpu.resource.load/store.raw.i32`/`.f32` runtime helpers -- a
   pre-existing "raw buffer views" completeness gap, unrelated to
   `ViewportIndex` specifically, that this shader's whole-`vec4` UBO load
   was simply the first thing in this codebase's test/CTS surface to
   exercise. Fixed by adding `femeCpuResourceLoadRawV4F32`/
   `femeCpuResourceStoreRawV4F32` (16-byte unaligned memcpy, mirroring the
   existing scalar raw load/store pattern).
4. With all three of the above fixed, pipeline creation and rendering both
   succeeded, but the rendered image was still wrong: `Executor.cpp`'s
   `resolvePrimitiveState` already computed the resolved `gl_ViewportIndex`
   value locally (via `resolveViewportArrayIndex`, added by H3 itself, to
   select the viewport/scissor array element) but discarded it once that
   selection was made, never threading it into the per-lane
   `FemeFragmentInvocation` the fragment shader body actually reads from.
   Fixed by adding a `ViewportIndex` field to `PrimitiveState`/
   `ScreenTriangle` and filling `Inv.ViewportIndex[Lane]` in the per-lane
   invocation-fill loop, mirroring the existing `TargetLayer`/`ViewIndex`
   wiring.

After all four fixes:

```
Test run totals:
  Passed:         68/68 (100.0%)
  Failed:          0/68 (0.0%)
  Not supported:   0/68 (0.0%)
```

The full `shader_viewport_index` group (196 cases, H3's own measurement)
now reads 132/196 passed (68 fragment + 64 vertex, unchanged), 64 `Not
supported` (`geometry_shader`/`tessellation_*`, H4/H5, unrelated), 0
`Failed` -- H3's own Deviation is fully closed.

**Regression checks** (no regressions found; two showed a net improvement):

- `dEQP-VK.multiview.*` (838 cases, since the `FemeFragmentInvocation`
  layout change shifts the `ViewIndex` field's own struct offset): 457/838
  (54.5%) passed, 42 `Failed`, 339 `Not supported` -- byte-identical to the
  pre-fix baseline (confirmed via `git stash`/rebuild/re-run). The 42
  `Failed` are pre-existing `VK_ERROR_FORMAT_NOT_SUPPORTED` renderpass
  failures, unrelated to this row.
- `dEQP-VK.ubo.*` (a representative every-15th-case sample of the full
  13240-case group, 882 cases, since the `SPIRVResourceLowering.cpp`/
  runtime changes touch any fragment shader using a bound UBO, not just
  `ViewportIndex` ones): 32/882 passed pre-fix vs. **47/882 passed
  post-fix**, 345 `Failed` pre-fix vs. 330 `Failed` post-fix -- 0 new
  failures, and 15 additional cases now pass, a direct side benefit of the
  metadata-preservation and vector-raw-load runtime fixes generalizing
  beyond this row's own `ViewportIndex` motivation.
- A broad `dEQP-VK.draw.*` run (29419 cases) hit one unrelated pre-existing
  crash (`SelectInst::init` assertion in
  `negative_viewport_height.front_ccw_cull_back`) and one unrelated
  pre-existing `VK_ERROR_INITIALIZATION_FAILED` (`multiple_interpolation`);
  both reproduced identically after `git stash`ing this row's changes,
  confirming neither is a regression from this row.

New unit tests (one per translation phase this row touches):
`SPIRVResourceLoweringTest.PreservesFunctionMetadataAcrossEnvParamRewrite`,
`ResourceLoweringTest.PreservesFunctionMetadataAcrossEnvParamRewrite`
(gap #1), `FragmentWrapperTest.LowersViewportArrayIndexSystemValueInput`
(gap #2), `RuntimeCPUTest.RawLoadV4F32IdentityFormat`/
`.RawLoadV4F32InactiveMaskReadsZero`/`.RawStoreV4F32RoundTrips`/
`.RawStoreV4F32DroppedWithoutUavFlag`/`.RawLoadV4F32StructuredKindIsAccepted`
(gap #3), `DrawTest.FragmentShaderReadsBackViewportIndex` (gap #4,
end-to-end). `ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build)
passes in full, 1785/1844 (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H3's own 1776/1835 baseline by exactly the 9 new tests
above.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no changes
needed -- H3 already flipped `multiViewport`/`shaderOutputViewportIndex`/
`shaderOutputLayer` to `VK_TRUE` and `VK_EXT_shader_viewport_index_layer`
to "Implemented"; this row is a pure bugfix underneath those same feature
bits, not a new capability.

## Roadmap H4: measured impact (tessellation stages, executor half)

`dEQP-VK.tessellation.*`: 1114 cases, run against this driver before and
after this row's four commits.

**Before** (`bb096339e499`, the pre-H4 tree) and **after**
(`ccceee4cb69c`), identically:

```
Test run totals:
  Passed:        0/1114 (0.0%)
  Failed:        0/1114 (0.0%)
  Not supported: 1114/1114 (100.0%)
  Warnings:      0/1114 (0.0%)
  Waived:        0/1114 (0.0%)
```

Every one of the 1114 reports `NotSupported (Tessellation shader not
supported)`, i.e. `deqp-vk` gates the entire group on
`VkPhysicalDeviceFeatures::tessellationShader`, which this driver still
reports as `VK_FALSE`.

**This is the intended outcome for this row, and the number is expected to
stay at 0/0/1114 until roadmap H4a lands.** The four commits here are the
graphics-executor half of H4: `feme::graphics::Executor::executeDraws` now
actually runs the hull control-point phase, the patch-constant phase,
`feme::graphics::tessellate` and the domain stage for a patch-list draw and
rasterizes the domain stage's output (roadmap R34's own "`Executor` does not
call `invokePatch`/`invokePatchConstant`/`invokeDomain` at all" item). None
of that is *reachable* from `vkCreateGraphicsPipelines` yet, because
`CanonicalizeStage.cpp` still refuses to reflect any entry point that is not
`Vertex` or `Fragment` (H4a) and `GraphicsPipeline.cpp` still rejects the two
tessellation stage bits (H4b). Flipping `tessellationShader` to `VK_TRUE`
ahead of those would convert 1114 honest `NotSupported`s into 1114 `Fail`s,
so it was deliberately left alone; `Vulkan14FeatureInventory.md` and
`VulkanExtensionInventory.md` consequently need no change for this row.

**Regression sample.** `Graphics/Executor.cpp` is shared by every draw, and
this row rewrote its post-vertex-stage plumbing (the `RasterSig`/`RasterOut`
indirection, the absolute `AbsTriIndices`/`AbsLineIndices` index lists, and
`RasterPrimitiveClass` replacing direct topology comparisons on the
point/line/triangle paths), so a `dEQP-VK.draw.*` sample was run on both
trees. The sample is every 15th case of the 29419-case group with the
`*viewport_height*` families removed (1957 cases); those are excluded
because they still hit the same pre-existing, unrelated
`SelectInst::init` "Invalid operands for select" assertion this report
already documents under H3a, which aborts the whole `deqp-vk` process and
truncates the run. Both trees:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        133/1957 (6.8%)
  Not supported: 1812/1957 (92.6%)
```

and the sorted list of failing case names is byte-identical between the two
runs (`diff` clean, 128 distinct names). **0 regressions, 0 new passes** --
as expected, since a pipeline with no tessellation stages takes exactly the
paths it took before, with `RasterSig`/`RasterOut` bound to the vertex
stage's own signature and output block.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full after every one of the four commits: **1800/1859** (59 pre-existing,
unrelated `Unsupported`, 0 `Failed`), up from H3a's own **1785/1844**
baseline by exactly the 15 new tests this row adds --
`StageLinkTest.cpp`'s 6 (`LinksByLocationNotByElementID`,
`LinksSystemValuesBySystemValue`, `RejectsAConsumerInputWithNoProducer`,
`RejectsAComponentCountMismatch`, `HonorsAConsumerFilter`,
`CopiesLinkedElementsRemappingInvocations`), `TessellatorTest.cpp`'s 4
(`TriangleTessellationIsCrackFreeAtEveryFactor`,
`TriangleTessellationIsCrackFreeWithUnequalEdgeFactors`, and the two
`Quad` equivalents), `PatchPipelineTest.cpp`'s 1 net new test after its
rewrite around
`linkPatchPipeline`, and `ExecutorTest.cpp`'s 4
(`TessellatedPatchListCoversTheWholeViewport`,
`TessellationFactorZeroCullsTheWholePatch`,
`RejectsAPatchListWithoutTessellationStages`,
`RejectsAPatchListDrawWithAPartialPatch`).

**Reproducing.** Same invocation as the rest of this report, with
`VK_DRIVER_FILES` (which is more reliable than `VK_ICD_FILENAMES` on this
loader) and a `vulkan` data symlink in the working directory:

```
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.tessellation.*" --deqp-log-filename=tess.qpa
```

`deqp-vk` still exits 134 *after* printing `DONE!` and the totals, throwing
`tcu::NotSupportedError: Device fault tests execution not supported in
Linux-like OSs`; that is a CTS teardown quirk unrelated to this driver, and
the totals printed before it are the real result.

## Roadmap H4a: measured impact (SPIR-V tessellation entry-point reflection)

**Still 0/0/1114, and that is the correct, expected result.** H4a makes
`feme::graphics::CanonicalizeStagePass` reflect `TessellationControl`/
`TessellationEvaluation` SPIR-V entry points at all (splitting a single
tessellation-control entry into FeMe's two D3D-shaped hull phases at its one
required group-sync barrier, capturing SPIR-V's tessellation execution
modes into `feme::graphics::TessellationState`, and mapping the
tessellation `BuiltIn`s onto `SignatureSystemValue`s), but nothing in
`vkCreateGraphicsPipelines` calls any of that yet: `GraphicsPipeline.cpp`
still accepts only `VK_SHADER_STAGE_VERTEX_BIT`/`VK_SHADER_STAGE_FRAGMENT_BIT`
and `PhysicalDeviceInfo.cpp` still reports `tessellationShader = VK_FALSE`
(both H4b). `dEQP-VK.tessellation.*` therefore still gates on
`VkPhysicalDeviceFeatures::tessellationShader` before it ever reaches a
pipeline, exactly as under H4:

```
Test run totals:
  Passed:        0/1114 (0.0%)
  Failed:        0/1114 (0.0%)
  Not supported: 1114/1114 (100.0%)
```

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` need no
change for this row, for the same reason H4's own report entry gives:
flipping `tessellationShader` ahead of H4b would convert these 1114 honest
`NotSupported`s into 1114 `Fail`s.

**Regression sample.** None of this row's changes touch any code on the
vertex/fragment-only path that every other `dEQP-VK.draw.*` case still
takes (`CanonicalizeStagePass::run`'s Hull/Domain branch is new code, not a
rewrite of the Vertex/Fragment one; `StageArgsLayout.h`'s `getPatchArgsType`
fix and the CPU hull/domain/patch-constant wrapper additions are likewise
new/patch-stage-only code paths that a non-tessellated pipeline never
reaches), but the same `dEQP-VK.draw.*` sample this report has used since H4
was re-run anyway, as a cheap confidence check on the shared `Executor.cpp`
plumbing:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        133/1957 (6.8%)
  Not supported: 1812/1957 (92.6%)
```

Byte-identical to H4's own recorded totals for the same 1957-case sample
(every 15th case of `dEQP-VK.draw.*`'s 29419 cases with the
`*viewport_height*` families removed). **0 regressions, 0 new passes.**

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1815/1874** (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from H4's own **1800/1859** baseline by exactly the 15 new tests this row
adds -- `CanonicalizeStageTest.cpp`'s 4
(`HullStageWithNoBarrierIsNotSplit`, `SplitsHullEntryAtGroupSyncBarrier`,
`HullStageMapsInvocationIdAndPatchVertices`,
`DomainStageMapsTessCoordAndPatchInput`), `TessellationTest.cpp`'s 6 (new
file, `getTessellationState` round-trip coverage), one new
`spirv-to-llvm-tessellation-execution-modes.mlir` lit test (4
`RUN`/`CHECK` blocks in one `lit` test), and four new CPU wrapper tests
covering the genuinely new lowering paths this row adds --
`HullWrapperTest.LowersPatchVerticesInput`,
`DomainWrapperTest.LowersPatchVerticesInput`,
`PatchConstantWrapperTest.LowersOutputControlPointIDAsZero`, and
`PatchConstantWrapperTest.LowersInputPatchVerticesCount`.

**Reproducing.** Same invocation as the rest of this report:

```
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.tessellation.*" --deqp-log-filename=tess.qpa
```

and, for the draw sample, a case list built the same way H4's report built
it (`grep -v viewport_height draw.txt | awk 'NR%15==1'` against
`external/vulkancts/mustpass/main/vk-default/draw.txt`), then
`--deqp-caselist-file=draw_sample.txt`. `deqp-vk` still exits non-zero after
printing `DONE!` and the totals (the same `tcu::NotSupportedError: Device
fault tests execution not supported in Linux-like OSs` teardown quirk this
report already documents); the totals printed before it are the real
result.

## Roadmap H4b: measured impact (`vkCreateGraphicsPipelines` tessellation acceptance)

**8/227/879, up from H4a's own 0/0/1114 -- real functional progress, with
real, triaged failures.** H4b makes `GraphicsPipeline.cpp`'s `mapStage`/
stage-mask loop accept `VK_SHADER_STAGE_TESSELLATION_{CONTROL,EVALUATION}_
BIT`, requiring exactly one of each when either is present and
`VK_PRIMITIVE_TOPOLOGY_PATCH_LIST` iff both are; validates
`VkPipelineTessellationStateCreateInfo::patchControlPoints` against
`maxTessellationPatchSize`; compiles the tessellation-control module twice
(once for its own control-point entry, once for its H4a-produced
`<entry>.patchconstant` phase) and the tessellation-evaluation module once,
merging their independently-optional reflected `TessellationState` halves;
and calls `graphics::GraphicsPipeline::setTessellationStages`, which
`Executor::executeDraws` has consumed since H4. `PhysicalDeviceInfo.cpp`
advertises `tessellationShader = VK_TRUE` (`maxTessellationPatchSize`/
`maxTessellationGenerationLevel` were already the true
`feme::graphics::MaxPatchControlPoints` (32) / `DefaultMaxTessFactor` (64)
ceilings, just gated behind the false feature bit).

This row's own work also surfaced (and fixed, as a prerequisite) a bug in
H4a's own barrier-splitting logic: `isSPIRVGroupSyncBarrier`
(`CanonicalizeStage.cpp`) only ever recognized the
`llvm.spv.*.barrier.with.group.sync` intrinsics HLSL's own builtin path
lowers to, not the mangled `_Z22__spirv_ControlBarrieriii` call MLIR's real
upstream `ControlBarrierPattern` lowers `spirv.ControlBarrier` to (confirmed
against `mlir/test/Conversion/SPIRVToLLVM/barrier-ops-to-llvm.mlir`'s own
lowering shape) -- meaning `splitTessellationControlEntry` never actually
fired for any real SPIR-V-imported tessellation-control shader until this
fix, which is why this section's own measured numbers below are the first
real ones for this group.

```
Test run totals:
  Passed:        8/1114 (0.7%)
  Failed:        227/1114 (20.4%)
  Not supported: 879/1114 (78.9%)
```

**Root-cause triage of the 227 `Fail`s.** Categorizing each failing case's
own first `error:`/JIT diagnostic (case counts, not incident counts --
several cases hit the same root cause):

| Count | Root cause | Tracking |
|---|---|---|
| 88 | `feme-cpu-simdize: function 'main' has a divergent value ... of vector type` | pre-existing, C8's matrix/aggregate-legalization bucket -- unrelated to tessellation, now reachable for the first time because a tessellation pipeline can reach code generation at all |
| 74 | `'llvm.getelementptr' op operand #N must be LLVM pointer type ... but got '!llvm.array<...>'` (four distinct array/struct shapes) | pre-existing, same C8 bucket (matrix/aggregate/array stage-IO legalization) |
| 24 | `feme-canonicalize-stage: tessellation-control SPIR-V entry point's patch-constant region cannot yet capture SSA values defined before the barrier` | new, this milestone's own gap -- roadmap H4c |
| 24 | JIT `Symbols not found: [ spirv_var_N, spirv_var_N ]` (all of `dEQP-VK.tessellation.winding.*`) | new, this milestone's own gap, root cause not yet isolated -- roadmap H4d |
| 12 | `feme-cpu-wrap-vertex: vertex stage wrapper requires attached feme.signature metadata` | pre-existing, same shape as roadmap H3a's fragment-side finding (metadata dropped by a `Function::Create`+`copyAttributesFrom` rebuild somewhere upstream), not yet independently tracked for the vertex side |
| 7 | assorted SPIR-V-to-LLVM legalization gaps (`spirv.CompositeConstruct`/`spirv.Constant`/`spirv.All` "explicitly marked illegal", one `unhandled Decoration : 'Component'`) | pre-existing, C8 bucket's "unhandled opcode/diagnostic tail" |

Of the 227 `Fail`s, only 48 (H4c's 24 + H4d's 24) are gaps this milestone's
own tessellation-control splitting design introduced or exposed; the
remaining 179 are pre-existing SPIR-V/codegen limitations already named in
C8's own bucket (or, for the 12 vertex-wrapper-metadata cases, a close
sibling of roadmap H3a's own already-fixed fragment-side finding), simply
unreachable for this test group before H4b because every one of its cases
was `NotSupported` outright.

**Regression sample.** A targeted `dEQP-VK.draw.*` regression sample (1957
of the 29419-case mustpass list, every 15th case with `*viewport_height*`
removed, the same sample H2f/H3/H4/H4a's own reports use) run against this
row's build:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        139/1957 (7.1%)
  Not supported: 1806/1957 (92.3%)
```

Diffing this against the same sample run on the pre-H4b build (12 `Pass`,
133 `Fail`, 1812 `NotSupported` -- rebuilt from `eb8ee3f12f52`, the commit
immediately before this row's own first commit, in a separate worktree to
get an exact byte-for-byte baseline) with a per-case status comparison
(not a raw text diff of failing-case lists, which mis-tracks: the same
`grep`-based line count differed by more than the real change because
several unrelated case names happen to share result-code words) shows
exactly 6 cases change status, and only 6:

```
NotSupported -> Fail   dEQP-VK.draw.dynamic_rendering.complete_secondary_cmd_buff.shader_layer.tessellation_shader_1
NotSupported -> Fail   dEQP-VK.draw.dynamic_rendering.complete_secondary_cmd_buff.shader_viewport_index.tessellation_shader_12
NotSupported -> Fail   dEQP-VK.draw.dynamic_rendering.partial_secondary_cmd_buff.shader_viewport_index.tessellation_shader_5
NotSupported -> Fail   dEQP-VK.draw.dynamic_rendering.primary_cmd_buff.shader_layer.tessellation_shader_3
NotSupported -> Fail   dEQP-VK.draw.dynamic_rendering.primary_cmd_buff.shader_viewport_index.tessellation_shader_7
NotSupported -> Fail   dEQP-VK.draw.renderpass.shader_viewport_index.tessellation_shader_3
```

Every one of these six is itself a `tessellation_shader_N` case -- gated on
`tessellationShader` the same way the main `dEQP-VK.tessellation.*` group
is, previously `NotSupported` outright and now correctly attempted, hitting
the same already-triaged gaps above (not a new, distinct failure mode).
**0 regressions**: every one of the sample's other 1951 cases is byte-for-
byte unchanged between the two builds.

`Vulkan14FeatureInventory.md` updated: `tessellationShader` (1.0) moves to
`VK_TRUE`, `AdvertisedPromotedFeatures.txt` grown to match, and the "N of M
unimplemented 1.0 feature bits are graphics capabilities" finding's count
and list updated (51 -> 50 unimplemented, 16 -> 15 graphics-capability
bits, `tessellationShader` dropped from the list). `VulkanExtensionInventory.md`
confirmed no change needed -- tessellation adds no extension of its own.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1821/1880** (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from H4a's own 1815/1874 baseline by exactly the new tests this row
adds (`CanonicalizeStageTest.SplitsHullEntryAtMangledSPIRVControlBarrierCall`,
`GraphicsPipelineTest.AcceptsTessellationStages`/
`.RejectsUnpairedTessellationStage`/`.RejectsTopologyTessellationStageMismatch`/
`.RejectsInvalidPatchControlPoints`, and `PhysicalDeviceInfoTest`'s updated
feature-bit assertion plus its new `TessellationLimitsMatchImplementationCeilings`).

**Reproducing.** Same invocation as H4a's own report entry:

```
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.tessellation.*" --deqp-log-filename=tess.qpa
```

and, for the draw sample, the same `grep -v viewport_height draw.txt | awk
'NR%15==1'` case list against `external/vulkancts/mustpass/main/vk-default/
draw.txt`, then `--deqp-caselist-file=draw_sample.txt`. `deqp-vk` still exits
non-zero after printing `DONE!` and the totals (the same pre-existing
`tcu::NotSupportedError: Device fault tests execution not supported in
Linux-like OSs` teardown quirk this report already documents); the totals
printed before it are the real result.

## Roadmap H4c: measured impact (threading a captured SSA value through a synthetic patch-scoped global)

**The 24-case diagnostic is gone, but the headline `dEQP-VK.tessellation.*`
totals do not move.** H4c replaces `splitTessellationControlEntry`'s old
`"...cannot yet capture SSA values defined before the barrier"`
`emitError`+bail with the design H4c's own roadmap row named as "likely the
right shape in general": for every SSA value the post-barrier
(patch-constant) region still references after the clone, the pass now
creates one new `PrivateLinkage`, address-space-8 (`Output`-storage-class)
`GlobalVariable` per captured value, carrying a synthetic `Location`
decoration chosen not to collide with any real stage-IO element
(`computeNextSyntheticLocation`); stores the captured value into it
immediately after the value's own definition in the control-point phase;
and loads it back at the top of a new `patchconst.captures` block prepended
to the patch-constant phase, before branching into the cloned post-barrier
entry. No new linking mechanism was needed: `classifySPIRVElement` already
classifies an address-space-8 global with only a `Location` decoration (no
`Patch`/tess-factor `BuiltIn`) as an ordinary per-vertex `Output` element in
`HullControlPoint` and as an ordinary, non-`FromInputPatch` `Input` element
(`isOutputPatchElement`) in `HullPatchConstant` -- the same "OutputPatch"
shape a real `gl_out[]` read-back after the barrier already uses -- so
`PatchPipeline.cpp`'s existing `linkStageElements`-based
`HullToPatchConstant` link picks the new pair up automatically, by
`Location`, with zero changes to `PatchPipeline.cpp`/`StageLink.cpp`. This
is sound unconditionally (not just for the common case): SPIR-V only
defines cross-invocation reads as well-defined *after* a barrier, so
anything the patch-constant region captures from *before* the one barrier
this pass splits at can only be the current invocation's own
already-computed state, never another invocation's -- unlike
re-materializing/cloning the pre-barrier computation (this row's other
named option), which is unsound whenever that computation itself reads
another invocation's per-vertex output.

```
Test run totals:
  Passed:        8/1114 (0.7%)
  Failed:        227/1114 (20.4%)
  Not supported: 879/1114 (78.9%)
```

Byte-identical to H4b's own baseline. This is expected, not a sign the fix
did nothing: per-case attribution (built the same way H4b's own triage
table was, by isolating the group into `--deqp-caselist-file` batches that
each stop *before* the next crash -- see "Reproducing this row" below for
why that was necessary this time) shows all 24 of H4c's own named cases
(every `dEQP-VK.tessellation.winding.{default,lower_left,upper_left}_domain.
hlsl_{quads,triangles}_{ccw,cw}[_yflip]` case) no longer hit the SSA-capture
diagnostic at all -- the split now succeeds and the compiled pipeline
reaches real code generation -- but every one of them immediately hits a
different, pre-existing, out-of-scope gap one step further in: `feme.cpu.
masked.load`/`.store`'s scalar-element-type mangling
(`MaskIntrinsics.cpp`'s `appendScalarMangling`) has no case for a
matrix/aggregate element type, the same underlying legalization gap C8's
bucket already tracks (it is very likely the same root cause as the 88
`feme-cpu-simdize: ... divergent value ... of vector type` and 74
`llvm.getelementptr` cases H4b's own triage table names, reached this time
through `feme::cpu::SIMDizePass`'s masked-memory-op path instead of
`feme-cpu-simdize`'s own diagnostic or a raw `getelementptr` -- not yet
independently confirmed, but the type shapes line up), so all 24 still
count as failing, just one step later and by a different mechanism. Net
result: 0 new `Pass`, 0 new `Fail`, 0 new `NotSupported` -- the fix is
real and necessary (H4c's own diagnostic is gone, and the pass now
produces a correctly split, correctly linked module for this shape, locked
down at the unit level below), but this measured group cannot show it move
the headline until the C8-bucket gap it now reaches is itself closed.

**A newly-surfaced severity concern, not a new root cause**: those same 24
cases do not fail gracefully any more -- `appendScalarMangling`'s
`llvm_unreachable("unsupported feme.cpu.masked.* element type")` aborts the
whole `deqp-vk` process (`SIGABRT`), rather than the graceful, per-case
`Fail`/diagnostic every other unsupported shape in this codebase produces.
Confirmed pre-existing rather than introduced by this row's change: an A/B
rebuild (`git stash`, rebuild only `bin/feme`/`lib/libfeme_vulkan.so`, run
`dEQP-VK.tessellation.winding.default_domain.hlsl_quads_ccw` in isolation,
`git stash pop`) shows the pre-H4c build never reaches
`appendScalarMangling` for this case at all -- it stops at H4c's own
diagnostic first -- so this is a pre-existing C8-bucket gap, coincidentally
unblocked by this row's own fix and reached for the first time via a
masked-SIMD path rather than the `getelementptr`/`feme-cpu-simdize` paths
C8's bucket was previously measured through. Deliberately **not** fixed
here: hardening `MaskIntrinsics.cpp`'s scalar mangling to diagnose this
shape gracefully instead of aborting is real, valuable follow-up work, but
it is CPU-backend masked-memory-op legalization, not tessellation-control
splitting -- a different milestone's scope (C8's own "best attacked after
C2/C3" bucket), not something "directly caused by or tightly coupled to"
this row's change merely because this row's fix is what first reaches it.
Tracked as a new roadmap follow-up, H4e, below, rather than folded into
this row's own commit.

**Regression sample.** The same `dEQP-VK.draw.*` sample H4/H4a/H4b's own
reports use (1957 of the 29419-case mustpass list, every 15th case with
`*viewport_height*` removed):

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        139/1957 (7.1%)
  Not supported: 1806/1957 (92.3%)
```

Byte-identical to H4b's own recorded totals for the same sample. **0
regressions, 0 new passes** -- expected, since this row's change is scoped
entirely to `splitTessellationControlEntry`'s capture-handling branch, not
reachable from any non-tessellation pipeline.

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` need no
change for this row: it neither flips a feature bit nor touches an
extension, only changes how an already-accepted tessellation-control
module is split.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1822/1881** (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from H4b's own 1821/1880 baseline by exactly the one new test this row
adds, `CanonicalizeStageTest.SplitsHullEntryThreadingCapturedSSAValue`
(asserts the split now succeeds for this shape, that the control-point
phase gains a matching new `Output` element and the patch-constant phase a
matching new, non-`FromInputPatch` `Input` element for the captured value,
and that no instruction in the patch-constant phase still references a
value defined in the control-point phase).

**Reproducing this row.** Same ICD build and case-list generation as the
rest of this report (see "Reproducing this report" above), but the full
`dEQP-VK.tessellation.*` group can no longer be run in one `deqp-vk`
invocation the way H4b's own report did it: the `SIGABRT` above kills the
whole process, silently truncating every case after the first crash the
same way the `pipeline`-group `ResourceError` this report already
documents does. Instead, generate the group's own case list
(`--deqp-runmode=txt-caselist`) and run it through a resume loop that, on
each abort, greps the log for the last `Test case '...'..` line with no
result line after it, appends that one case to an exclusion list, and
reruns the remainder via `--deqp-caselist-file`:

```shell
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-case="dEQP-VK.tessellation.*" \
    --deqp-runmode=txt-caselist
grep -oP "^TEST: \K.*" dEQP-VK-cases.txt > remaining.txt
while [ -s remaining.txt ]; do
  VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
    deqp-vk --deqp-caselist-file=remaining.txt \
      --deqp-log-filename=iter.qpa > iter.log 2>&1
  grep -q "^DONE!" iter.log && break
  LAST=$(grep -o "Test case '[^']*'" iter.log | tail -1 | \
    sed -E "s/Test case '([^']*)'/\1/")
  grep -vFx "$LAST" remaining.txt > remaining.txt.new
  mv remaining.txt.new remaining.txt
done
```

Every case this loop excludes this way (24 total, all
`dEQP-VK.tessellation.winding.*_domain.hlsl_{quads,triangles}_*`) is the
`MaskIntrinsics.cpp` abort above; summing that group's own
`Passed`/`Failed`/`Not supported` counts across every iteration plus one
`Failed` for each excluded case reproduces the totals above. The draw
sample and `check-feme` reproduce exactly as H4b's own entry describes.

## Roadmap H4d: measured impact (per-element addressing for a bare array-typed stage-IO global)

**Root cause, isolated from a real, glslang-compiled repro rather than a
synthetic one.** The roadmap row's own `GlobalDCE`/two-`LLJIT`
cross-phase-collision hypothesis was checked first and refuted: a
synthetic no-barrier tessellation-control entry (a single scalar `patch`
global, no `gl_out` write at all) reproduced a clean
`VK_ERROR_INITIALIZATION_FAILED` from `selectEntryPoint` failing to find
`main.patchconstant` -- not the crash. The real shape needed a real
shader: `dEQP-VK.tessellation.winding.default_domain.glsl_quads_ccw`'s own
tessellation-control GLSL (`vktTessellationWindingTests.cpp`) was compiled
to real SPIR-V with a minimal hand-written glslang driver (no
`glslangValidator` binary was available in this environment, and no
network access to install one; the driver links directly against
`VK-GL-CTS`'s own already-built `libglslang.a`/`libSPIRV.a`/
`libSPIRV-Tools*.a` static libraries) and fed straight into
`vkCreateShaderModule` in a temporary gtest, reproducing the exact
symptom: `JIT session error: Symbols not found: [ gl_TessLevelOuter,
gl_TessLevelInner ]`. A `FEME_DEBUG_DUMP_STAGE`-gated dump of the
post-`CanonicalizeStagePass` LLVM IR (temporary, not part of the final
change) showed the actual defect: only the *first* store to each of
`gl_TessLevelInner`/`gl_TessLevelOuter` (byte offset 0) was rewritten into
a `feme.stage.output.store.f32` call; every other element's store (byte
offsets 4/8/12, i.e. `gl_TessLevelInner[1]`/`gl_TessLevelOuter[1..3]`) was
left as a raw `store float ..., ptr addrspace(8) getelementptr(...)`
referencing the still-`external`, never-defined SPIR-V-imported global
directly -- an unresolvable symbol at `LLJIT` link time.

`resolveStageIOAccess` (`CanonicalizeStage.cpp`) treated every
single-`ElementID` stage-IO global (i.e. every one *not* part of a builtin
interface block/struct) as whole-value-only, unconditionally rejecting
any nonzero byte offset with `return std::nullopt`. That is correct for a
scalar global, but `gl_TessLevelInner`/`gl_TessLevelOuter` are
`BuiltIn`+`Patch`-decorated `[2 x f32]`/`[4 x f32]` arrays with a *single*
`ElementID` each (unlike a builtin interface block's one-`ElementID`-per-
member shape) -- exactly the shape every GLSL tessellation-control shader
writes one element at a time, with no interface block wrapping it. Fixed
by applying the same `resolveRowComponent`-based byte-offset-to-(Row,
Component) resolution the builtin-interface-block-member branch already
used (`gl_PerVertex`'s own `gl_ClipDistance[i]`/`gl_CullDistance[i]`
handling), to the single-`ElementID` branch's own global type instead of a
struct member's type, using the full byte offset as the residual (there is
no struct member offset to subtract first).

**Unit tests.** `CanonicalizeStageTest.
RewritesSPIRVArrayOutputStorePerElementByteOffset` locks the fix down at
the LLVM-IR level: a bare `[4 x float]` `Output` global (no interface
block, mirroring `gl_TessLevelOuter`'s own shape) written at 4 different
byte offsets, asserting no raw `store` survives and all 4 (Row, 0)
`feme.stage.output.store` pairs are seen -- confirmed to fail (rows 1-3
unresolved) with the fix reverted. `GraphicsPipelineTest.
AcceptsTessellationControlMultiElementArrayOutput` locks it down end to
end: a real tessellation pipeline (`spirv.ControlBarrier`-split, matching
`TessControlSource`'s own shape) whose control-point phase writes 2
elements of a `gl_TessLevelOuter`-shaped `BuiltIn("TessLevelOuter")
{
  patch}` global now `vkCreateGraphicsPipelines`-succeeds; confirmed, by
reverting the fix and rebuilding, to reproduce the exact reported crash
instead (`JIT session error: Symbols not found: [ tess_outer ]`,
`VK_ERROR_INITIALIZATION_FAILED`). `ninja check-feme`
(`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in full: **1824/1883**
(59 pre-existing, unrelated `Unsupported`, 0 `Failed`), up from H4c's own
1822/1881 baseline by exactly the two new tests this row adds.

```
Test run totals (dEQP-VK.tessellation.winding.*, glsl variants only, 24 cases):
  Passed:        0/24 (0.0%)
  Failed:        24/24 (100.0%)
  Not supported: 0/24 (0.0%)
```

**0 `"Symbols not found"` JIT crashes** (was 24/24) -- confirmed by
`grep -c "Symbols not found"` against the run's own log, 0 occurrences,
down from 24 before this fix. But all 24 still `Fail`: a new, distinct,
previously-unreached gap this fix itself surfaces (not introduced by it,
the same "fix the named symptom, immediately hit the next blocking issue"
shape H4b -> H4c -> H4e already established) --
`splitTessellationControlEntry` only clones a `<entry>.patchconstant`
phase when it finds a barrier to split at; winding's own trivial
`layout(vertices=1)` control shader has none (a single output control
point needs no cross-invocation synchronization, so glslang correctly
never emits one), so `PatchConstantPhase` stays `nullptr` and no
`.patchconstant` function is ever created. `compileAndValidateStages`
(H4b), which always expects a `.patchconstant` sibling to exist and
compiles it as a second stage unconditionally, now fails cleanly instead:
`"no hull entry point named 'main.patchconstant'"` ->
`VK_ERROR_INITIALIZATION_FAILED`, confirmed via a temporary diagnostic
capture in `compileAndValidateStages`'s own error path (removed before
this change was committed). This is a distinct gap in the barrier-split
design itself (R34/H4a's own scope), not a `resolveStageIOAccess`
addressing bug, so it is broken out as follow-up roadmap H4f rather than
fixed in this row.

**The same fix, applied at the `resolveStageIOAccess` level rather than
winding-specifically, also unblocks 8 more cases**, discovered while
resuming the full group's own crash-prone run (see "Reproducing this row"
below): `dEQP-VK.tessellation.shader_input_output.{
  barrier, gl_position_tcs_to_tes, gl_position_vs_to_tcs,
      gl_position_vs_to_tcs_to_tes, patch_vertices_10_in_5_out,
      patch_vertices_5_in_10_out, primitive_id_tcs, primitive_id_tes}`. Before
this fix, all 8 hit a different, pre-existing `Fail` one step later in the
pipeline (confirmed unchanged by an A/B rebuild with this fix reverted):
`error: 'llvm.getelementptr' op operand #0 must be LLVM pointer type or
LLVM dialect-compatible vector of LLVM pointer type, but got
'!llvm.array<32 x ...>'` -- the same underlying single-`ElementID`
nonzero-offset gap this row fixes, just manifesting against a bare
`OutputPatch`-shaped (`gl_out[]`/`gl_in[]`-style, 32 = `MaxPatchControlPoints`)
array global addressed per control point rather than against
`gl_TessLevelInner`/`Outer`'s own per-tess-factor addressing. After this
fix, all 8 reach real code generation and immediately hit H4e's own
already-tracked `MaskIntrinsics.cpp` `llvm_unreachable` abort instead --
the identical "unblocked into the next, already-tracked gap" pattern H4c's
own 24 hlsl-winding cases established, just for a different 8 cases this
time.

**Net result on the full group: byte-identical to H4b/H4c's own
baseline.**

```
Test run totals (dEQP-VK.tessellation.*, 1114 cases):
  Passed:          8/1114 (0.7%)
  Failed:        227/1114 (20.4%)
  Not supported: 879/1114 (78.9%)
```

Expected, not a sign the fix did nothing: of the 227 `Fail`s, 32 now
resolve through the H4e crash bucket (24 winding-glsl + 8
shader_input_output, both newly unblocked into it by this fix) instead of
their own prior, distinct `Fail`/crash reasons -- but `Fail` is `Fail`
either way the three headline buckets are concerned, so converting one
`Fail` mechanism into another `Fail` mechanism cannot move Pass/Fail/
NotSupported by itself, exactly as H4c's own report already established
for its own 24 cases.

**Regression sample.** The same `dEQP-VK.draw.*` sample H4/H4a/H4b/H4c's
own reports use (1957 of the 29419-case mustpass list, every 15th case
with `*viewport_height*` removed):

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        139/1957 (7.1%)
  Not supported: 1806/1957 (92.3%)
```

Byte-identical to H4b/H4c's own recorded totals. **0 regressions, 0 new
passes** -- expected, since this row's change is scoped entirely to
`resolveStageIOAccess`'s single-`ElementID` nonzero-byte-offset handling,
which no non-tessellation pipeline in the sample reaches (no other stage
in this codebase's own test corpus writes a bare, non-block array/vector
stage-IO global one element at a time the way a tessellation-control
shader's tess-factor/`gl_out` writes do).

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` need no
change for this row: it neither flips a feature bit nor touches an
extension, only widens which stage-IO store shapes `CanonicalizeStagePass`
resolves correctly.

**Reproducing this row.** Same ICD build and case-list generation as the
rest of this report (see "Reproducing this report" above). The winding-glsl
subset:

```shell
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-case="dEQP-VK.tessellation.winding.*glsl*" \
    --deqp-log-filename=winding_glsl.qpa
```

The full `dEQP-VK.tessellation.*` group needs H4c's own resume-on-abort
loop (see its "Reproducing this row" above), generalized slightly: rather
than assuming every abort's last case is the one to exclude and every
other case prints a result line, drive the loop purely off "does this
case's own block contain a `Pass`/`Fail`/`NotSupported` result line", and
treat any case whose block does not (the process died before it could
print one) as a `Fail` when summing -- this run hit crashes at 31 distinct
points (32 cases total across all resumed iterations: 24
`winding.*_domain.hlsl_*` plus the 8 `shader_input_output.*` cases named
above), not H4c's own single 24-case bucket, so a loop that only ever
excludes the *literal last-started* case per iteration (H4c's own
simplification, valid when it only ever needed to skip one specific,
already-known 24-case family) needs the "any unterminated block is a
`Fail`" generalization to reproduce the correct total without hand-listing
every crash point in advance. The draw sample and `check-feme` reproduce
exactly as H4b/H4c's own entries describe.

## Roadmap H4e: measured impact (`MaskIntrinsics.cpp` graceful diagnostic instead of `llvm_unreachable`)

**What changed.** `feme/lib/Transforms/CPU/MaskIntrinsics.cpp`'s
`appendScalarMangling` (the helper `mangleMaskedMemOpName` uses to build a
`feme.cpu.masked.load`/`.store`/`.atomicrmw`'s type-mangled declaration
name) called `llvm_unreachable("unsupported feme.cpu.masked.* element
type")` for any element type it did not recognize -- reached, per H4c's
and H4d's own reports, by all 24 `dEQP-VK.tessellation.winding.*.hlsl_*`
cases and the 8 `dEQP-VK.tessellation.shader_input_output.*` cases those
two rows respectively unblocked into this exact gap, a `SIGABRT` that
killed `deqp-vk`'s entire remaining test run rather than a per-case
`Fail`. Fixed by making `appendScalarMangling` return `bool`, reporting an
unsupported `Type` (with its LLVM IR spelling) through its own
`LLVMContext::emitError` and returning `false` instead of asserting
unreachable; `mangleMaskedMemOpName` now returns `std::optional<std::
string>` (propagating that failure as `std::nullopt`); and
`getOrInsertMaskedLoad`/`getOrInsertMaskedStore`/
`getOrInsertMaskedAtomicRMW` and their `createMasked*` wrappers all
propagate a `nullptr` in turn. `feme::cpu::LinearizePass`'s
`applyStageMasks` (Linearize.cpp) -- the only caller of the three
`createMasked*` entry points -- now checks each for `nullptr` and, when
one is returned, leaves the original `load`/`store`/`atomicrmw`
un-rewritten and un-erased rather than dereferencing a null `CallInst *`,
so the rest of the block keeps being processed. `LLVMContext::emitError`
is already caught, module-wide, by `feme::cpu::runPipeline`'s
`ErrorDiagnosticGuard` (Pipeline.cpp), which turns "a pass printed a
diagnostic" into "`runPipeline` returns a graceful `llvm::Error`" -- the
same mechanism every other diagnosed-rather-than-asserted gap in this
codebase already relies on -- so no new plumbing was needed to turn this
row's diagnostic into a clean pipeline failure instead of a crash.

This is the "harden `appendScalarMangling` to `emitError`+return a
null/sentinel" half of H4e's own roadmap description; the "root
legalization fix" half (actually teaching `feme::cpu::SIMDizePass`/
`feme.cpu.masked.*` how to widen a matrix/aggregate-typed masked memory
access) remains C8's own scope and is not attempted here -- these 32
cases still `Fail`, just cleanly instead of crashing the process.

Locked down by three new `MaskIntrinsicsTest` cases (one per
`createMasked*` entry point, each asserting a `nullptr` result and a
captured `DS_Error` diagnostic mentioning "unsupported feme.cpu.masked.*
element type" for a struct-typed operand -- a `{float, float}` standing in
for the matrix/aggregate shapes this milestone does not yet decompose) and
one new `LinearizeTest` case (`UnsupportedAggregateMaskedStoreDiagnosesGracefullyInsteadOfCrashing`:
a `store {
  float, float}` under a divergent `feme.stage.discard` mask
that must diagnose and survive as a plain, unmasked `StoreInst` rather
than crash `LinearizePass`, checked by running the full pass rather than
calling `MaskIntrinsics.cpp` directly, so a regression in either layer
would be caught). `ninja check-feme` (assertions-enabled, ccache build)
passes in full, 1828/1887 (59 pre-existing, unrelated `Unsupported`, 0
`Failed`).

**Measured impact.** Reproduced with the same 24-case
`dEQP-VK.tessellation.winding.*hlsl*` case list H4c's own report used to
first find this gap:

```
Test run totals:
  Passed:         0/24 (0.0%)
  Failed:        24/24 (100.0%)
  Not supported:  0/24 (0.0%)
```

Byte-identical *headline* totals to H4c/H4d's own recorded numbers for
this same 24-case list (all still `Fail`), but the qualitative difference
this row exists for: `deqp-vk` no longer aborts partway through the run.
Before this fix, the process died with `SIGABRT` on the first of these 24
cases and every case after it in the same run was lost (H4c's own
"per-case-resume methodology" existed only to work around this); after
this fix, `deqp-vk` prints a clean `error: feme-cpu-masked-mem-op:
unsupported feme.cpu.masked.* element type '{ [4 x float], [2 x float]
}' ...` diagnostic and a `Fail` result line for all 24 cases in a single,
uninterrupted run, then reaches `DONE!` and prints its own totals summary
normally (the harness's own unrelated post-`DONE!` `tcu::NotSupportedError:
Device fault tests execution not supported in Linux-like OSs` teardown
exception, already present in every prior report in this file, is the only
thing that still makes the process exit non-zero).

**Full group.** `dEQP-VK.tessellation.*` (1114 cases):

```
Test run totals:
  Passed:          8/1114 (0.7%)
  Failed:        227/1114 (20.4%)
  Not supported: 879/1114 (78.9%)
```

Byte-identical to H4b/H4c/H4d's own recorded totals, as expected: this row
converts a crash into a `Fail` for the 32 cases (24 winding-hlsl + 8
shader_input_output) H4c/H4d's own fixes newly routed into this gap, which
does not by itself move any of the three headline buckets -- the same
"`Fail` is `Fail` either way" conclusion H4c's and H4d's own reports
already reached for their own newly-unblocked cases. The practical benefit
is entirely in *how* the run reaches those 32 `Fail`s (a clean diagnostic
and a surviving process, not a lost remainder of the run), which the
24-case reproduction above demonstrates directly and the full-group
byte-identical totals confirm did not regress anything else.

**Regression sample.** The same `dEQP-VK.draw.*` sample H4/H4a/H4b/H4c/
H4d's own reports use (1957 of the 29419-case mustpass list, every 15th
case with `*viewport_height*` removed):

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        139/1957 (7.1%)
  Not supported: 1806/1957 (92.3%)
```

Byte-identical to H4b/H4c/H4d's own recorded totals. **0 regressions, 0
new passes** -- expected, since no non-tessellation pipeline in this
codebase's own test corpus creates a masked memory access over a
matrix/aggregate-typed value in the first place (the only reachable
callers of `feme.cpu.masked.*` are `feme::cpu::LinearizePass`'s own
`applyStageMasks`, gated on a genuinely divergent mask), so this sample
never exercised the crash this row fixes either before or after it.

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` need no
change for this row: it neither flips a feature bit nor touches an
extension, only hardens an existing diagnostic path (matching H4c's and
H4d's own "no change needed" conclusion for the same reason).

**Reproducing this row.** Same ICD build and case-list generation as the
rest of this report (see "Reproducing this report" above):

```shell
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-case="dEQP-VK.tessellation.winding.*hlsl*" \
    --deqp-log-filename=winding_hlsl.qpa
```

now completes in one pass with no crash and no need for H4c's own
per-case-resume loop; the full `dEQP-VK.tessellation.*` group and the draw
sample (`grep -v viewport_height draw.txt | awk 'NR%15==1'` against
`external/vulkancts/mustpass/main/vk-default/draw.txt`, then
`--deqp-caselist-file=draw_sample.txt`) both also complete directly, with
no resume loop needed at all -- the resume-loop methodology H4c/H4d's own
reports required is itself made obsolete by this row for every case that
used to hit this specific abort (any case that still crashes after this
fix would indicate a different, not-yet-tracked abort site, none of which
were observed in this reproduction).

## Roadmap H4f: measured impact (barrier-less tessellation-control split)

**What changed.** `splitTessellationControlEntry`
(`feme/lib/Transforms/Graphics/CanonicalizeStage.cpp`) only ever cloned a
`<entry>.patchconstant` phase when it found a barrier; a tessellation-
control shader with none -- legally the case whenever `OutputVertices ==
1`, since a single control-point invocation needs no cross-invocation
synchronization, exactly `dEQP-VK.tessellation.winding.*`'s own shape --
left `PatchConstantPhase == nullptr`, while `compileAndValidateStages`
(H4b) unconditionally expects that sibling to exist and compiles it as a
second stage regardless. Fixed via option (a) of this row's own two
proposed fixes: a new `isPatchConstantOnlyEntry(Function &F)` scans every
address-space-8 store an entry makes and returns `true` only when at
least one exists and every one is patch-frequency (`Patch`-decorated or a
tess-factor `BuiltIn`), conservatively `false` for any global carrying
`feme.spirv.MemberDecorations` (a real GLSL tess-factor write is always a
plain global, never an interface-block member); a new
`splitBarrierlessTessellationControlEntry` clones the whole entry as
`<name>.patchconstant` via `CloneFunctionInto` when that holds, and
replaces the original function's body with a trivial `ret void` stub,
leaving the barrier-found path untouched. Option (b) (an unconditional
whole-function clone whenever there is no barrier) was rejected: tracing
`classifySPIRVElement`'s `HullPatchConstant`-phase branch shows it
classifies any non-`Patch`-decorated address-space-8 write as
`Direction::Input` (a read-back of a captured cross-barrier value, never
a store), so cloning a function with an ordinary per-vertex output store
into the patch-constant phase would misclassify that store's direction
and trip `ValidateStagePass`'s `OutputStore` direction check.

Locked down by `CanonicalizeStageTest.NoBarrierPatchConstantOnlyEntryIsSplitWhole`
(a no-barrier hull entry writing only `gl_TessLevelOuter` splits into a
trivial `main`, no signature, one `ret void` block, and a
`main.patchconstant` clone whose signature carries one
`PatchOutput`/`TessFactorEdge` element); the pre-existing
`HullStageWithNoBarrierIsNotSplit` (an ordinary per-vertex write, no
barrier) continues to correctly not split, confirming the new gate is not
over-broad. `ninja check-feme` (assertions-enabled, ccache build) passes
in full, 1829/1888 (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from 1828/1887 before this row.

**Measured impact.** Reproduced with `FEME_VULKAN_LOG_CREATION_ERRORS=1`
against `dEQP-VK.tessellation.winding.default_domain.glsl_*` (8 cases):
before this fix, every one failed inside `GraphicsPipeline.cpp`'s
compile step because `compileAndValidateStages` could not find a
`.patchconstant` sibling function at all (`no hull entry point named
'main.patchconstant'`, a diagnostic-and-`Fail` since roadmap H4e). After
this fix, that specific error is gone (`grep -c "no hull entry point
named"` against a full `dEQP-VK.tessellation.*` re-run returns 0, down
from 24), but all 24 winding-glsl cases still `Fail` -- root-caused as a
second, distinct blocker this row's own fix exposed (H4g below), and,
after H4g's own fix, a third, still-open one (H4h). **Full group**
(`dEQP-VK.tessellation.*`, 1114 cases) is byte-identical to H4b/H4c/H4d/
H4e's own recorded totals (8 `Pass`/227 `Fail`/879 `NotSupported`) --
expected, since this row's own fix alone does not yet turn any case
green; it only changes *which* diagnostic the 24 winding-glsl cases hit.
**Regression sample**: the same `dEQP-VK.draw.*` 1957-case sample used by
every prior H4 row is byte-identical too (12 `Pass`/139 `Fail`/1806
`NotSupported`) -- 0 regressions, expected, since no non-tessellation
pipeline in this codebase's own test corpus reaches
`splitTessellationControlEntry` at all.

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` need no
change for this row: `tessellationShader` was already flipped in H4/H4b,
and this row neither adds nor removes a feature bit or extension, only
fixes a stage-splitting gap.

**Reproducing this row.** Same ICD build and case-list generation as the
rest of this report:

```shell
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
FEME_VULKAN_LOG_CREATION_ERRORS=1 \
  deqp-vk --deqp-case="dEQP-VK.tessellation.winding.default_domain.glsl_*" \
    --deqp-log-filename=winding_glsl.qpa
```

## Roadmap H4g: measured impact (absent signature reflection for a zero-stage-IO compiled stage)

**What changed.** H4f's own fix immediately exposed a second, distinct
blocker: a compiled stage with zero SPIR-V stage-IO globals never got
`!feme.signature` metadata attached at all, hit both by H4f's own new
trivial control-point stub and, independently, by
`dEQP-VK.tessellation.winding.*`'s own genuinely-empty vertex shader
(`void main (void) {}`). `canonicalizeSPIRVStage`'s guard (`if
(!InputGlobals.empty() || !OutputGlobals.empty())`) skips attaching any
signature when a function has no stage-IO globals to begin with, so
`feme::cpu::CompiledStage::create` never serialized a `Signature` byte
vector for it, and `GraphicsPipeline.cpp`'s `getStageSignature` then
hard-errors ("compiled stage carries no signature reflection") on the
empty `Bytes`, reached unconditionally via `validateStageInterfaces`'s
`getStageSignature(VertexStage)` call.

Fixed at the narrowest safe layer, `CompiledStage::create`'s own
serialization step (`feme/lib/Target/CPU/CompiledStage.cpp`), which now
always serializes `feme::dxil::getEntrySignature(**Entry).value_or(
EntrySignature{})` rather than only when a signature exists, treating an
entirely-absent signature identically to an explicit empty one at the
artifact-serialization boundary only -- matching
`CanonicalizeStagePass::run`'s own pre-existing documented philosophy
("an absent signature is treated as an empty one") without touching
`canonicalizeSPIRVStage`'s `Changed`/rewriting behavior anywhere. A more
general fix attempted first -- attaching an explicit empty
`!feme.signature` in `canonicalizeSPIRVStage` itself whenever no stage-IO
globals are found and no signature already exists -- was rejected and
reverted: it cannot distinguish a genuinely-empty SPIR-V entry from an
unresolved DXIL-origin entry using only that pass's local view, and would
have broken the pre-existing `CanonicalizeStageTest.
UnresolvableLoadInputIsLeftAlone` test (a DXIL-origin fragment entry with
no `!feme.signature` metadata, asserting `run(*M)` returns `false`),
which continues to pass unmodified under the layer actually chosen.

`ninja check-feme` (assertions-enabled, ccache build) passes in full,
1829/1888 (59 pre-existing, unrelated `Unsupported`, 0 `Failed`) -- same
headline as H4f's own row, since this fix changes serialization content,
not pass/fail counts, and adds no new dedicated unit test of its own
(covered indirectly by the existing `FeMeTargetCPUTests`/
`FeMeTransformsGraphicsTests`/`FeMeVulkanTests` suites, including the
DXIL-path test above, all of which continue to pass).

**Measured impact.** Reproduced against the same
`dEQP-VK.tessellation.winding.default_domain.glsl_*` 8-case list: the
"compiled stage carries no signature reflection" error is gone entirely
(0 occurrences across a full `dEQP-VK.tessellation.*` re-run, down from
all 24 winding-glsl cases). **Full group** and **regression sample**
totals are unchanged from H4f's own row (8/227/879 and 12/139/1806
respectively) -- no case turns green yet, since a third, later blocker
(H4h) still rejects every one of these 24 cases at
`vkCreateGraphicsPipelines`.

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` need no
change for this row, for the same reason as H4f's own.

**Reproducing this row.** Identical reproduction command to H4f's own
row above; the "no signature reflection" error is what to check is gone.

## Roadmap H4h: measured impact (relaxing `SV_Position` to the domain stage under tessellation)

**What changed.** H4f and H4g's own fixes let a `dEQP-VK.tessellation.
winding.*` glsl pipeline reach `validateStageInterfaces`
(`GraphicsPipeline.cpp`), which unconditionally required the *vertex*
stage's own signature to carry a 4-component `SV_Position`/`gl_Position`
output -- correct for the ordinary vertex -> fragment pipeline, but wrong
once a tessellation-evaluation stage exists: `winding`'s own vertex
shader is a genuinely empty `void main (void) {}`, and its evaluation
shader computes `gl_Position` purely from `gl_TessCoord`, never reading
any vertex-stage output back via `gl_in[]`.

Fixed by threading the already-compiled `DomainStage` (`compileAndValidateStages`
already has it, for the tessellation-state merge) through as a new,
optional parameter to `validateStageInterfaces`: when non-`nullptr`, the
`SV_Position` check now parses and inspects *its* signature instead of
the vertex stage's, leaving every other check in the function (fragment
varying linkage against the vertex stage's own outputs, per-attachment
`SV_TargetN` outputs, vertex-input-attribute coverage) completely
unchanged. An ordinary two-stage pipeline (`DomainStage == nullptr`)
still checks the vertex stage exactly as before.

`ninja check-feme` (assertions-enabled, ccache build) passes in full,
1831/1890 (59 pre-existing, unrelated `Unsupported`, 0 `Failed`), up from
1829/1888 before this row -- the two new `GraphicsPipelineTest` cases:
`AcceptsTessellationPipelineWithEmptyVertexShader` (a real four-stage
tessellation pipeline with a genuinely empty vertex module now creates
successfully) and `RejectsEmptyVertexShaderWithoutTessellation` (the same
empty vertex module, no domain stage, is still correctly rejected,
confirming the relaxation doesn't leak into the non-tessellation path).

**Measured impact.** Reproduced against
`dEQP-VK.tessellation.winding.*glsl*` (24 cases,
`FEME_VULKAN_LOG_CREATION_ERRORS=1`): the `"vertex stage does not write a
4-component SV_Position output"` error is gone entirely (0 occurrences,
down from all 24), and every one of the 24 cases now reaches
`vkCreateGraphicsPipelines` success and produces a rendered image --
still all 24 `Fail`, but now at a pixel-comparison mismatch inside the
test's own image verification (e.g. `dEQP-VK.tessellation.winding.
default_domain.glsl_quads_ccw`: "Note: got 4081 white and 15 red pixels" /
"Failure: expected only white pixels (full-viewport quad)"), a distinct,
later bug tracked as roadmap H4i, not a pipeline-creation error.

**Full group** (`dEQP-VK.tessellation.*`, 1114 cases) is byte-identical
to H4g's own recorded totals (8 `Pass`/227 `Fail`/879 `NotSupported`) --
expected, since none of these 24 cases turn green yet, they only move
from a creation-time `Fail` to a render-time `Fail`. **Regression
sample**: the same `dEQP-VK.draw.*` 1957-case sample used throughout this
report is also byte-identical (12 `Pass`/139 `Fail`/1806 `NotSupported`)
-- 0 regressions, expected, since no non-tessellation pipeline in this
codebase's own test corpus passes a non-`nullptr` `DomainStage` into
`validateStageInterfaces`.

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` need no
change for this row, for the same reason as H4f/H4g's own: no feature bit
or extension is added or removed, only a validation-layer relaxation.

**Reproducing this row.** Same ICD build as the rest of this report:

```shell mkdir run &&cd run ln - sfn / home / dev / dev / VK - GL -
    CTS / external / vulkancts / data / vulkan vulkan VK_DRIVER_FILES =
    <feme - build> / tools / feme / tools / feme -
    vulkan / feme_icd.json FEME_VULKAN_LOG_CREATION_ERRORS =
        1 deqp - vk-- deqp - case =
            "dEQP-VK.tessellation.winding.*glsl*" --deqp - log - filename =
                winding_glsl.qpa
```

                ##Roadmap H4i: measured impact (`VkTessellationDomainOrigin` winding fix)

**What changed.** H4h's own relaxation let all 24
`dEQP-VK.tessellation.winding.*glsl*` cases reach real rendering, where
they failed at a systematic front-face/winding-orientation mismatch. The
first hypothesis tried -- that `Tessellator.cpp`'s own `appendTriangle`
Cw/Ccw operand-swap convention was simply inverted (`TessellatorTest.cpp`'s
own comment calls it "not the standard convention") -- was tested via a
fast `git stash`-and-rebuild A/B against a real `deqp-vk` run before
committing to it, and found to be **wrong**: it fixed `lower_left_domain`'s
8 cases but broke the previously-nearly-correct `default_domain`/
`upper_left_domain`'s 16, an exact swap of which subgroup failed rather
than a net fix. That A/B is what isolated the real root cause: FeMe never
parsed `VkTessellationDomainOrigin` anywhere (`grep -r DomainOrigin
feme/lib feme/include` found nothing), silently treating every
`VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT` pipeline identically to the
spec-default upper-left one. Per the Vulkan spec (confirmed against
`vktTessellationWindingTests.cpp`'s own `verifyResultImage`/
`expectVisiblePrimitive` formula, which is a direct function of
`domainOrigin`), the lower-left domain origin mirrors the tessellator's
`(u,v)` parameter frame, which reverses every generated triangle's
winding as a side effect -- independent of, and not fixable by touching,
the tessellator's own (already-correct) Cw/Ccw convention.

Fixed in `feme/lib/Vulkan/GraphicsPipeline.cpp`:
`hasLowerLeftTessellationDomainOrigin` walks
`VkPipelineTessellationStateCreateInfo::pNext` for a chained
`VkPipelineTessellationDomainOriginStateCreateInfo`, true only when its
`domainOrigin` is explicitly `VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT`;
`flipTessellationWindingForDomainOrigin` swaps `TriangleCw`/`TriangleCcw`
(leaving `Point`/`Line` untouched) and is applied to the merged
`TessellationState::OutputPrimitive` once, at the end of
`compileGraphicsPipeline`, only when a tessellation-control stage is
present and the lower-left origin was requested.

`ninja check-feme` (assertions-enabled, ccache build) passes in full,
1833/1892 (59 pre-existing, unrelated `Unsupported`, 0 `Failed`), up from
1831/1890 before this row -- the two new `GraphicsPipelineTest` cases:
`FlipsTessellationWindingForLowerLeftDomainOrigin` (an explicit
lower-left domain origin flips `OutputPrimitive` from `TriangleCcw` to
`TriangleCw`) and `KeepsTessellationWindingForExplicitUpperLeftDomainOrigin`
(an *explicit* upper-left domain origin behaves identically to omitting
the struct entirely, confirming the check reads the field's value, not
merely the struct's presence).

**Measured impact.** A real before/after `deqp-vk` A/B against
`dEQP-VK.tessellation.winding.*glsl*` (24 cases,
`FEME_VULKAN_LOG_CREATION_ERRORS=1`) confirms the fix: **before**,
`lower_left_domain`'s 8 cases showed a genuine, complete front-face
inversion -- one pipeline of every `_ccw`/`_cw` pair rendered exactly the
opposite of both pipelines in `default_domain`/`upper_left_domain` (e.g.
"got 0 white and 4096 red pixels" where the sibling subgroup's equivalent
pipeline got "got 4081 white and 15 red pixels", and vice versa). **After**,
all three domain-origin subgroups (`default_domain`, `lower_left_domain`,
`upper_left_domain`) show the *identical* pattern -- `lower_left_domain`'s
own systematic inversion is gone. Per-case final totals (all 24 still
`Fail`, but uniformly, at a distinct and much smaller defect):

```
default_domain.glsl_quads_ccw:            got 4081 white and 15 red pixels / got 0 white and 4096 red pixels
default_domain.glsl_quads_ccw_yflip:       got 0 white and 4096 red pixels / got 4081 white and 15 red pixels
default_domain.glsl_quads_cw:              got 0 white and 4096 red pixels / got 4081 white and 15 red pixels
default_domain.glsl_quads_cw_yflip:        got 4081 white and 15 red pixels / got 0 white and 4096 red pixels
default_domain.glsl_triangles_ccw:         got 2047 white and 2049 red pixels / got 0 white and 4096 red pixels
default_domain.glsl_triangles_ccw_yflip:   got 0 white and 4096 red pixels / got 2050 white and 2046 red pixels
default_domain.glsl_triangles_cw:          got 0 white and 4096 red pixels / got 2047 white and 2049 red pixels
default_domain.glsl_triangles_cw_yflip:    got 2050 white and 2046 red pixels / got 0 white and 4096 red pixels
lower_left_domain.*  -- byte-identical shape to default_domain.* above (confirming the fix)
upper_left_domain.*  -- byte-identical shape to default_domain.* above (confirming the spec-mandated equivalence)
```

In every case, the "Note" pair is (visible-pipeline result, culled-pipeline
result); the culled pipeline is always exactly correct (0 white/4096 red
for quads, or its exact complement); only the pipeline that is supposed to
render visibly ever shows the residual 15-pixel (quads) or ~1-pixel-row
(triangles, via `verifyResultImage`'s top/bottom-row-fill count landing
at 64/64 or 1/64 instead of the expected 63/0) defect. This is spun off
as roadmap H4j, since it does not correlate with front-face, winding, or
domain origin at all (identical across all three subgroups) and is
almost certainly a rasterizer tie-break/rounding or tessellator
crack-free-bridging-seam issue rather than a winding bug.

**Full group** (`dEQP-VK.tessellation.*`, 1114 cases) is byte-identical
to H4h's own recorded totals (8 `Pass`/227 `Fail`/879 `NotSupported`).
**Regression sample**: the same `dEQP-VK.draw.*` 1957-case sample used
throughout this report is also byte-identical (12 `Pass`/139 `Fail`/1806
`NotSupported`) -- 0 regressions, expected, since `VkTessellationDomainOrigin`
was previously silently ignored everywhere, and this fix only changes
behavior for a pipeline that actually chains the lower-left struct (none
of this codebase's own non-winding tests do).

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` need no
change for this row: `VkPipelineTessellationDomainOriginStateCreateInfo`
is a core Vulkan 1.2 (originally `VK_KHR_maintenance2`) pipeline-creation
struct, not a feature bit or an extension name to advertise -- it was
always legal for an application to chain this struct, FeMe simply
ignored its contents until now, the same "always-accepted struct now
actually honored" shape as H4b/H4h's own tessellation-state parsing,
neither of which needed an inventory change either.

**Reproducing this row.** Same ICD build as the rest of this report:

```shell
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
FEME_VULKAN_LOG_CREATION_ERRORS=1 \
  deqp-vk --deqp-case="dEQP-VK.tessellation.winding.*glsl*" \
    --deqp-log-filename=winding_glsl.qpa
```

## Roadmap H4j: measured impact (rasterizer double-precision coverage test + top-left tie-break polarity fix)

**What changed.** H4i's own domain-origin fix left all 24
`dEQP-VK.tessellation.winding.*glsl*` cases uniformly failing at a much
smaller, distinct defect: exactly 15/4096 stray red pixels for
`glsl_quads_*`, and a 1-pixel top/bottom-row-fill off-by-one for
`glsl_triangles_*`. Both were isolated entirely inside `Executor.cpp`'s
per-sample coverage test, not `Tessellator.cpp`'s crack-free bridging (one
of the roadmap row's own two candidates) -- direct `FEME_DEBUG_XY`-gated
instrumentation at the CTS's own failing pixel (16,16) confirmed the two
candidate triangles there share bit-identical vertex positions, ruling
out any divergent/duplicate domain-point evaluation on the tessellator
side.

**Root cause 1 (quads).** The coverage test's `edgeFn` (`float`) is not
bit-exactly antisymmetric under vertex-order reversal: `edgeFn(A,B,P)`
computes `P - A` while `edgeFn(B,A,P)` computes `P - B`, a different
subtraction whose rounding does not exactly cancel even though the two
results are mathematically exact negations of each other. Two adjacent
triangles sharing an exact diagonal edge (the tessellator's own
quad-core-cell split) each independently round a sample landing almost
exactly on that shared edge to a spuriously *negative* value and both
reject it -- a floating-point rasterization crack. Confirmed in isolation
with a Python float32 emulation (`struct`/`numpy.float32`): recomputing
the same edge function in `double` from the same float32 vertex inputs
resolves the tie to a genuine, non-degenerate sign.

**Root cause 2 (triangles).** Independent pre-existing polarity bug in
`isTopLeftEdge`. Tessellation factor 5 (`gl_TessLevelInner/Outer = 5.0`
throughout this CTS group) lands every domain edge on a clean multiple
of `64/5 = 12.8`, so a lone tessellated triangle's own outer hypotenuse
can land a sample exactly on `E == 0.0` at the corners of the tessellated
domain. The old (backwards) tie-break formula, `(Dy == 0 && Dx > 0) ||
Dy < 0`, classified that edge as "included" when Vulkan's spec-mandated
top-left rule (matching D3D/OpenGL) requires it excluded -- confirmed
identical in both `_ccw` and `_cw` "visible" pipelines (independent of
winding or domain origin, exactly matching this roadmap row's own
observation that the defect does not correlate with either).

**Fixes (`feme/lib/Graphics/Executor.cpp`).**

1. A new `edgeFnD` (the same edge function, evaluated in `double`)
   replaces `edgeFn` for the coverage test's three inside/outside
   comparisons (`E0`/`E1`/`E2`) only; the barycentric interpolation
   weights (`Bary0`/`Bary1`/`Bary2`) stay `float`, since those are
   interpolated *values*, not coverage *decisions*. This shrinks the
   rounding from `float`'s ~2^-23 relative precision to `double`'s
   ~2^-52, several orders of magnitude below any crack this rasterizer's
   own coordinate range produces.
2. `isTopLeftEdge`'s polarity is flipped to `(Dy == 0 && Dx < 0) ||
   Dy > 0`, re-derived geometrically for a positively-wound triangle
   (`Area > 0`, an invariant the existing `SArea < 0.0f`
   triangle-assembly reorder already guarantees before the coverage
   test runs): a "top" edge is horizontal and points leftward (interior
   below it), a "left" edge points downward (interior to its right).

**New unit tests (`ExecutorTest.cpp`).**

- `DoublePrecisionEdgeTestClosesAFloatRoundingCrackBetweenAdjacentTriangles`:
  two triangles sharing a non-axis-aligned, non-"nice"-fraction edge
  reached through the executor's own NDC-to-screen `projectVertex`
  transform (not hand-picked screen coordinates), chosen so pixel
  (16,16)'s sample point lands almost exactly on the shared edge.
  Confirmed (via a temporary revert) to fail against the pre-fix code
  and pass against the fix.
- `TopLeftTieBreakExcludesALoneTrianglesOwnBoundaryEdge`: a single
  triangle whose hypotenuse is a 4x4 viewport's own anti-diagonal
  (`x + y == 4`); confirms all four boundary pixel centers landing
  exactly on it are excluded, leaving exactly the 6 strictly-interior
  pixels filled. Also confirmed to fail pre-fix and pass post-fix.

`ninja check-feme` (assertions-enabled, ccache build) passes in full,
1835/1894 (59 pre-existing, unrelated `Unsupported`, 0 `Failed`), up from
1833/1892 before this row (the two new tests).

**Measured impact.** A real `deqp-vk` run against
`dEQP-VK.tessellation.winding.*glsl*` (24 cases) confirms the fix:

```
Test run totals:
  Passed:        24/24 (100.0%)
  Failed:        0/24 (0.0%)
```

All 24 cases now pass, up from 0/24 before this row.

**Full group** (`dEQP-VK.tessellation.*`, 1114 cases):

```
Test run totals:
  Passed:        32/1114 (2.9%)
  Failed:        203/1114 (18.2%)
  Not supported: 879/1114 (78.9%)
```

Up from H4i's own 8/227/879 -- exactly the 24 newly-fixed winding cases
moving from `Fail` to `Pass`, with `NotSupported` unchanged -- confirming
zero regressions elsewhere in the group.

**Regression sample**: the same `dEQP-VK.draw.*` 1957-case sample used
throughout this report is byte-identical to H4i's own recorded totals
(12 `Pass`/139 `Fail`/1806 `NotSupported`) -- 0 regressions, expected,
since neither fix changes any coverage-test outcome that was not already
a genuine floating-point tie or crack (an exact tie or a value within a
few ULPs of zero); no other test in either group exercises that
condition differently than before.

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` need no
change for this row: this is a pure rasterizer-precision bug fix in the
software rasterizer's own coverage test, touching no feature bit or
extension.

**Reproducing this row.** Same ICD build as the rest of this report:

```shell
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-case="dEQP-VK.tessellation.winding.*glsl*" \
    --deqp-log-filename=winding_glsl.qpa
```

and, for the full group / regression sample, the same invocations H4a's
own report entry documents (`--deqp-case="dEQP-VK.tessellation.*"`, and
`grep -v viewport_height draw.txt | awk 'NR%15==1'` against
`external/vulkancts/mustpass/main/vk-default/draw.txt` for the draw
sample's case list).

## Roadmap H5a: measured impact (SPIR-V geometry entry-point execution-mode reflection)

**Still 0/0/200, and that is the correct, expected result** -- the same
shape H4a's own report entry recorded for tessellation. H5a adds
`feme::graphics::GeometryState`/`Geometry.h` (mirroring
`TessellationState`/`Tessellation.h`) and teaches
`ConvertSPIRVToLLVMPass::collectEntryPoints` to capture a geometry entry
point's input primitive class (`InputPoints`/`InputLines`/
`InputLinesAdjacency`/`Triangles`/`InputTrianglesAdjacency`), output
primitive class (`OutputPoints`/`OutputLineStrip`/`OutputTriangleStrip`),
instance count (`Invocations`, defaulting to 1) and maximum emitted vertex
count (`OutputVertices`) into `feme.geometry.*` passthrough attributes,
disambiguating the two SPIR-V enumerant values geometry shares with
tessellation (`Triangles`, also a tessellation-evaluation domain; and
`OutputVertices`, also a hull stage's output control point count) by the
declaring entry point's own `Stage`. Unlike H4a, this row does **not** yet
lift `CanonicalizeStagePass::run`'s stage filter to include
`ShaderStage::Geometry` -- see "Roadmap H5: what H5a found, and why it
stops here" below for why that turned out not to be safe to do yet, and
new roadmap rows H5b-H5e for the remaining work. Nothing in
`vkCreateGraphicsPipelines` or `PhysicalDeviceInfo.cpp` changes at all, so
`dEQP-VK.geometry.*` is unaffected:

```
Test run totals:
  Passed:        0/200 (0.0%)
  Failed:        0/200 (0.0%)
  Not supported: 200/200 (100.0%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no change:
`geometryShader` stays `VK_FALSE`, and this row advertises nothing new.

**Regression sample.** This row's only new code paths are
`feme::graphics::getGeometryState`/`Geometry.cpp` (a new file nothing else
calls yet) and `ConvertSPIRVToLLVMPass`'s new `EntryPointInfo` fields/
execution-mode cases (populated only for a `Geometry`-stage entry point, or
read only when disambiguating `Triangles`/`OutputVertices` by `Stage` --
both branches confirmed by the new lit test to leave a non-geometry entry's
own attributes unchanged). The same `dEQP-VK.draw.*` 1957-case sample this
report has used since H4:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        133/1957 (6.8%)
  Not supported: 1812/1957 (92.6%)
```

Byte-identical to H4a's own recorded totals. **0 regressions, 0 new
passes.**

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1843/1902** (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from H4j's own **1835/1894** baseline by exactly the 8 new tests this row
adds -- `GeometryTest.cpp`'s 7 (new file, `getGeometryState` round-trip
coverage, mirroring `TessellationTest.cpp`) and one new
`spirv-to-llvm-geometry-execution-modes.mlir` lit test (4 `RUN`/`CHECK`
blocks in one `lit` test, including the `Triangles`/`OutputVertices`
disambiguation case).

**Reproducing.**

```
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.geometry.*" --deqp-log-filename=geom.qpa
```

and, for the draw regression sample, the same invocation H4a's own report
entry documents.

## Roadmap H5: what H5a found, and why it stops here

Investigating H5's full scope (mirroring H4a+H4b) before writing any code
found that geometry's own Vulkan-API surface needs *more* new machinery
than H4a/H4b's tessellation precedent suggested, for one specific reason:
a geometry entry point's per-vertex inputs (SPIR-V's `gl_in[]`-shaped
arrayed `Input` variable, one array element per assembled primitive
vertex) are read with a **dynamic, non-constant SPIR-V array index** in
the general case (`for (int i = 0; i < gl_in.length(); i++) ... gl_in[i]
...`), not the single implicit "this invocation's own control point"
index a hull control-point-phase entry's own per-control-point inputs are
restricted to (`classifySPIRVElement`'s `FromInputPatch` handling, Hull-
stage-only). `loadStageIOValue`/`storeStageIOValue`'s existing recursive
per-(struct member, row, component) decomposition (built for a matrix/
aggregate value, C8a) always passes a constant `Zero` as
`feme.stage.input.load`'s `Vertex` operand -- there is no code path today
that threads a real, non-constant SPIR-V array-index SSA value into that
operand at all, even though `createStageInputLoad`'s signature already
has a `Vertex` parameter for exactly this purpose, and even though
`feme::cpu::GeometryWrapperPass` (built already, under G5, prior to this
milestone) already consumes that operand generically ("`lowerGeometryInputLoad`
... places no restriction on that operand", GeometryWrapper.cpp's own file
comment) -- it has simply never had a producer.

Lifting `CanonicalizeStagePass::run`'s stage filter to accept
`ShaderStage::Geometry` *before* that producer exists would not fail loudly
the way this codebase's own precedent (H2a discovering H2c/H2d, H4c/H4d
diagnosing rather than mis-splitting) insists on: a geometry entry point's
`gl_in[i]` access would silently resolve through the existing "always
`Vertex = 0`" path, always reading input vertex 0 regardless of `i` --
wrong output, not a diagnostic, for every real GLSL geometry shader (they
essentially all loop over `gl_in`). Per this codebase's stated preference
for a real, tested diagnostic over a silent wrong answer, H5a stops at
capturing the execution modes (self-contained, real, and independently
useful for H5b) and does not flip the stage filter. The array-indexed
per-vertex input read is broken out as roadmap H5b below, which needs to
land before `CanonicalizeStagePass::run` can safely accept
`ShaderStage::Geometry` at all.

## Roadmap H5b: measured impact (dynamic `Vertex` operand for `gl_in[i]`-shaped access)

**Still 0/0/200 on `dEQP-VK.geometry.*` -- correctly so.** H5b builds the
machinery `CanonicalizeStagePass::run` will need once H5c finally accepts
`ShaderStage::Geometry`, but it does not itself flip that filter (H5c's own
job, and still deliberately not done here -- see "Roadmap H5: what H5a
found, and why it stops here" above). With no geometry entry point ever
reaching `canonicalizeSPIRVStage` yet, none of this row's new code paths
are reachable from a real `deqp-vk` run at all:

```
Test run totals:
  Passed:        0/200 (0.0%)
  Failed:        0/200 (0.0%)
  Not supported: 200/200 (100.0%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no change:
`geometryShader` stays `VK_FALSE`, and this row advertises nothing new.

**What this row actually adds.** `CanonicalizeStage.cpp` gains
`getDynamicVertexIndexedAccess`, recognizing the one specific IR shape
`gl_in[i]` (or any other per-vertex-arrayed `Input`-storage-class SPIR-V
global, block or plain) compiles down to: a `GetElementPtrInst` -- not a
constant-foldable `ConstantExpr`, since `i` is a genuine loop-carried SSA
value -- whose first index is the ordinary pointer-to-aggregate `0` and
whose second is that non-constant vertex index, with everything past it
(a builtin interface block's own member selection, or a matrix row within
that one vertex's own value) still constant and resolved into a byte
offset exactly the way the existing constant-offset path
(`getStageIOBaseAndOffset`/`resolveRowComponent`) already does, just
starting one array dimension in. `resolveStageIOAccess` tries this shape
first, threading the extracted `Value*` through as `feme.stage.input.load`'s
`Vertex` operand in place of the ordinary constant `i32 0` every other
stage-IO access seeds it with; both of this pass's stage-IO discovery
loops (`usesSPIRVStageIO`, and `canonicalizeSPIRVStage`'s own) are updated
to recognize such a global too (via the new shared `getStageIOGlobal`
helper), since `getStageIOBaseAndOffset` alone can never fold a
non-constant GEP index and so never discovers it. The signature-building
side (`addElements`) is taught to peel exactly one outer array dimension
before checking for a builtin interface block's per-member
`feme.spirv.MemberDecorations` metadata, so a geometry `gl_in[]` block's
own per-member `SignatureElement`s are built from each member's *own*
value type (e.g. `gl_Position`'s bare `<4 x float>`), never conflating the
per-vertex array dimension with a member's own `RowCount` the way a real
matrix's row dimension is (see the note below on the one thing this row
deliberately leaves alone).

`ValidateStage.cpp` gains `validateVertex`, diagnosing a non-constant
`Vertex` operand anywhere except the geometry stage -- the only stage
whose ABI (`FemeGeometryArgs`'s primitive-major `Inputs` layout,
`GeometryWrapper.cpp`'s own `lowerGeometryInputLoad`: "the vertex-in-
primitive operand ... may be any value in `[0, VerticesPerPrimitive)`") is
actually built to address one at runtime. Since `ValidateStagePass::run`
itself still only validates the vertex/fragment stages (Hull/Domain
already unvalidated before this row; Geometry remains so too -- both are
H5e's own open item, not this row's), this check is exercised today only
against the stages it *does* validate, confirming it fires as a diagnostic
rather than becoming reachable dead code once H5e lands.

**One thing this row deliberately does not do.** A *constant* vertex index
into one of these arrays (`gl_in[0].gl_Position`, or an entirely unrolled
loop) still resolves through the pre-existing `getStageIOBaseAndOffset`
byte-offset path exactly as before H5b, folding that constant array index
into the member's own `Row` operand rather than `Vertex` -- the roadmap
entry's own title scopes this row to the *non-constant* case only, and
every real GLSL geometry shader loops over `gl_in` rather than unrolling
it by hand, so this is not expected to matter in practice. Reconciling
that (a per-vertex-array global's `RowCount` in the signature not
matching what a real matrix's `RowCount` means, and a constant index
still routing through `Row` instead of `Vertex`) is left to a later
roadmap row, once H5c starts routing real geometry entries through this
pass and a real consumer needs to tell the two apart reliably regardless
of whether the shader's own index happens to be constant.

**Regression sample.** Every new code path above is reached only by a
non-constant-vertex-array-indexed `Input` global's own access (`resolveStageIOAccess`'s
new fallback) or a `feme.stage.input.load`/`.output.store`'s non-constant
`Vertex` operand (`validateVertex`) -- neither shape appears anywhere in
this codebase's existing vertex/fragment/hull/domain-stage tests, so no
regression is expected. Confirmed against the same `dEQP-VK.draw.*`
1957-case sample this report has used since H4:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        139/1957 (7.1%)
  Not supported: 1806/1957 (92.3%)
```

Byte-identical between this row's build and H5a's own baseline (both
measured fresh for this row, since the CTS mustpass/environment this
report runs against has drifted since H4a's own **133**-failure figure was
first recorded -- confirmed by re-running H5a's unmodified tree through
the same sample and getting the same **139**/1806 totals, so the drift is
environmental, not caused by this row). **0 regressions, 0 new passes**
either way.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1847/1906** (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from H5a's own **1843/1902** baseline by exactly the 4 new tests this
row adds -- `CanonicalizeStageTest.cpp`'s
`ThreadsDynamicVertexIndexIntoInputLoad` and
`ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberLoad`, and
`ValidateStageTest.cpp`'s
`NonConstantVertexOperandIsDiagnosedOutsideGeometry` and
`NonConstantVertexOperandIsDiagnosedOnOutputStore`.

**Reproducing.** Same invocation as the rest of this report:

```
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.geometry.*" --deqp-log-filename=geom.qpa
```

and, for the draw sample, the same `grep -v viewport_height draw.txt | awk
'NR%15==1'`-built case list H4b's own report entry documents
(`--deqp-caselist-file=draw_sample.txt`). `deqp-vk` still exits non-zero
after printing `DONE!` and the totals (the same `tcu::NotSupportedError`
teardown quirk this report already documents); the totals printed before
it are the real result. One case in the full (non-sampled)
`dEQP-VK.draw.*` set --
`dynamic_rendering.complete_secondary_cmd_buff.negative_viewport_height.front_ccw_cull_back`
-- aborts the whole run with an unrelated, pre-existing `SelectInst::init`
assertion failure (confirmed present on H5a's own unmodified tree too,
hence `grep -v viewport_height` excluding it from the sample); not
something this row touches or fixes.

## Roadmap H5c: measured impact (`CanonicalizeStagePass` accepts `ShaderStage::Geometry`)

**Still 0/0/200 on `dEQP-VK.geometry.*` -- correctly so, for a different
reason than H5b/H5f/H5g's "the filter isn't lifted yet".** This row *does*
lift `CanonicalizeStagePass::run`'s stage filter, and a geometry entry
point now really does reach `canonicalizeSPIRVStage(*F,
ShaderStage::Geometry, SPIRVCanonicalPhase::Ordinary)` -- but
`vkCreateGraphicsPipelines` still rejects `VK_SHADER_STAGE_GEOMETRY_BIT`
outright (`GraphicsPipeline.cpp`'s `translateFixedFunctionState`, H5e's
own row), so no real SPIR-V geometry module is ever compiled far enough
to reach this pass at all in a `deqp-vk` run:

```
Test run totals:
  Passed:        0/200 (0.0%)
  Failed:        0/200 (0.0%)
  Not supported: 200/200 (100.0%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no
change: `geometryShader` stays `VK_FALSE`, and this row advertises
nothing new.

**What this row actually adds.** `CanonicalizeStagePass::run`'s stage
filter now also accepts `ShaderStage::Geometry`, routed through the same
`else` branch Domain already uses (`canonicalizeSPIRVStage(*F, *Stage,
SPIRVCanonicalPhase::Ordinary)`) -- no barrier-splitting the way Hull
needs (`canonicalizeSPIRVHullStage`), since GLSL/SPIR-V compiles a whole
geometry shader to one entry point already (`GeometryWrapper.cpp`'s own
file comment). `getSystemValueForBuiltIn` needed no new cases:
`gl_PrimitiveIDIn`/`gl_PrimitiveID` (SPIR-V `BuiltIn PrimitiveId`, code 7,
disambiguated by storage class rather than by value), `gl_InvocationID`
(code 8), `gl_Layer` (code 9), and `gl_ViewportIndex` (code 10) already
mapped onto `SignatureSystemValue::{
  PrimitiveID, InvocationID, RenderTargetArrayIndex, ViewportArrayIndex}` respectively, from H2/H4a's
own earlier work.

**Regression sample.** `CanonicalizeStageTest.GeometryStageMapsSystemValues`
(new, mirroring `HullStageMapsInvocationIdAndPatchVertices`/
`DomainStageMapsTessCoordAndPatchInput`) exercises a geometry entry point
reading `gl_PrimitiveIDIn`/`gl_InvocationID` and writing
`gl_Layer`/`gl_ViewportIndex`/`gl_PrimitiveID`, confirming each maps to
its expected `SignatureSystemValue` with the right `SignatureDirection`.
`ninja check-feme` (assertions-enabled, ccache build) passes in full,
1851/1910 (59 pre-existing, unrelated `Unsupported`), up from H5g's
1850/1909 by exactly the 1 new test this row adds. The same
`dEQP-VK.draw.*` 1957-case sample this report has used since H4 is
byte-identical to H5g's own baseline (12 `Pass`/139 `Fail`/1806
`NotSupported`, 0 regressions).

**Reproducing.** Same invocation as the rest of this report:

```
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.geometry.*" --deqp-log-filename=geom.qpa
```

and, for the draw sample, the same `grep -v viewport_height draw.txt | awk
'NR%15==1'`-built case list H4b's own report entry documents
(`--deqp-caselist-file=draw_sample.txt`), excluding the same pre-existing,
unrelated `SelectInst::init` crash this report already documents.

## Roadmap H5f: measured impact (constant `Vertex` operand, `RowCountIsVertexArray`)

**Still 0/0/200 on `dEQP-VK.geometry.*` -- correctly so**, for the same
reason H5b's own edition above gives: `CanonicalizeStagePass::run` still
does not accept `ShaderStage::Geometry` (H5c's own job), so no geometry
entry point reaches any of this row's code either:

```
Test run totals:
  Passed:        0/200 (0.0%)
  Failed:        0/200 (0.0%)
  Not supported: 200/200 (100.0%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no change:
this row advertises nothing new, same as H5a/H5b.

**What this row actually adds.** It closes the one thing H5b's own
"deliberately does not do" note (above) left open: a *constant* `gl_in[k]`
index (or any other per-vertex-arrayed `Input` global's constant index)
now folds into the same `Vertex` operand a non-constant one already does,
instead of an ordinary `Row`. The new shared predicate
`isPerVertexArrayInputGlobal` (structural only, matching
`getDynamicVertexIndexedAccess`'s own precedent of not trying to
disambiguate a genuine geometry per-vertex array from some other stage's
real per-invocation matrix attribute at this level -- `ValidateStagePass`'s
`validateVertex` is what actually diagnoses stage-inappropriate use)
replaces `getDynamicVertexIndexedAccess`'s own inlined check, and is now
also consulted by `resolveStageIOAccess`'s ordinary constant-offset
fallback: when the resolved global is this shape and the access is not a
whole-global aggregate one, the byte offset is split into a vertex index
(the outer array dimension) and a residual offset within that one
vertex's own value, exactly mirroring the dynamic path's own peeling, just
with a constant `Value*` instead of a genuine SSA one.

Reconciling the other half of that same note -- `SignatureElement.RowCount`
for such a global being indistinguishable from a real matrix's row count
in the signature -- adds `SignatureElement::RowCountIsVertexArray`
(`feme/include/feme/Core/Signature.h`, `SignatureAbiVersion` bumped 3 → 4):
true only for a whole (non-block) per-vertex-arrayed `Input` global's own
element, set in `addElements` via the same `isPerVertexArrayInputGlobal`
predicate; always false for a builtin interface block's own per-member
elements, whose `RowCount` never included the per-vertex array dimension
to begin with (it is peeled off one layer before `addElement` ever sees a
member's type). Every pre-existing serialized `feme.signature` blob this
codebase's own tests embed (`feme-render`'s `draw-*.test` scenes, the CPU
stage-wrapper lit tests) is regenerated to the new version-4 byte layout
(a mechanical re-serialization, not a semantic change -- every value
`RowCountIsVertexArray` least-significant zero-fills for a pre-H5f
signature is the correct "not a per-vertex array" default for all of
them).

**Regression sample.** `ninja check-feme` needed no other production-code
IR shape change: the two existing H5b tests
(`ThreadsDynamicVertexIndexIntoInputLoad`,
`ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberLoad`) still pass
unmodified except for asserting the now-populated
`RowCountIsVertexArray` flag, confirming this row does not disturb the
non-constant case it builds on. The same `dEQP-VK.draw.*` 1957-case sample
this report has used since H4 is byte-identical to H5b's own baseline:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        139/1957 (7.1%)
  Not supported: 1806/1957 (92.3%)
```

**0 regressions, 0 new passes** -- expected, since every real
`dEQP-VK.draw.*`/`dEQP-VK.geometry.*` shader either has no per-vertex-array
global at all (draw) or never reaches this pass yet (geometry, pending
H5c).

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1849/1908** (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from H5b's own **1847/1906** baseline by exactly the 2 new tests this
row adds -- `CanonicalizeStageTest.cpp`'s
`FoldsConstantVertexIndexIntoVertexOperand` and
`FoldsConstantVertexIndexIntoInterfaceBlockArrayMemberVertexOperand` --
plus `SignatureTest.cpp`'s existing `SerializeParseRoundTrips` extended
(not added) to cover the new field.

**Reproducing.** Same invocation as H5b's own edition above (`dEQP-VK.
geometry.*` and the `draw_sample.txt` caselist); no new command needed.

## Roadmap H5g: measured impact (`StageIOGlobalVariablePattern` array-of-block member decorations)

**Still 0/0/200 on `dEQP-VK.geometry.*` -- correctly so**, same reason as
H5b/H5f above: `CanonicalizeStagePass::run` still does not accept
`ShaderStage::Geometry` (H5c's own job), so a geometry entry never reaches
`CanonicalizeStage.cpp` at all regardless of what metadata now reaches it:

```
Test run totals:
  Passed:        0/200 (0.0%)
  Failed:        0/200 (0.0%)
  Not supported: 200/200 (100.0%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no change:
this row advertises nothing new, same as H5a/H5b/H5f.

**What this row actually adds.** `StageIOGlobalVariablePattern::
matchAndRewrite` (`SPIRVToLLVMPatterns.cpp`) only ever looked for a bare
`mlir::spirv::StructType` pointee to attach `feme.spirv.MemberDecorations`
metadata to -- correct for `gl_PerVertex`-shaped outputs, but not for a
geometry entry's own `gl_in[]`-shaped *input*, whose pointee is an
`mlir::spirv::ArrayType` **of** that same per-vertex block struct (SPIR-V
still decorates the inner struct's own members, not the array wrapping
it). Before this row, such a global reached `CanonicalizeStage.cpp` with
no member-decoration metadata at all -- H5b's own `addElements` peeling
logic (the `if (auto *ArrTy = dyn_cast<ArrayType>(BlockTy)) BlockTy =
ArrTy->getElementType();` step) had nothing to peel in front of, silently
falling through to the single-element, whole-global path instead of the
per-member one a real `gl_in[]` needs. The fix peels the pointee's own
outer `ArrayType` (if present) before checking for a `StructType`,
attaching the inner struct's per-member decorations unconditionally of
which of the two shapes wraps it -- matching `addElements`'s own existing
"peel one array dimension, then look for `MemberDecorations`" precedent
exactly, just on the producing side instead of the consuming one.

**Regression sample.** Two new tests cover this directly:
`SPIRVToLLVMTest.PerVertexArrayInterfaceBlockPreservesMemberDecorations`
(gtest, asserting the metadata attribute is present and the ordinary
whole-variable decoration attribute is not, mirroring the existing bare
-block `BuiltinInterfaceBlockPreservesMemberDecorations` case) and a new
`CHECK` block in `spirv-to-llvm-stage-io.mlir` (lit, asserting the exact
`feme.spirv.member.decorations` attribute shape for an `Input`
array-of-struct global). The same `dEQP-VK.draw.*` 1957-case sample this
report has used since H4 is byte-identical to H5f's own baseline:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        139/1957 (7.1%)
  Not supported: 1806/1957 (92.3%)
```

**0 regressions, 0 new passes on `draw`** -- expected, since no
`dEQP-VK.draw.*` shader declares an array-of-block stage-IO global; this
row's effect is only observable once H5c lets a real geometry entry reach
`CanonicalizeStage.cpp` at all.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1850/1909** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H5f's own **1849/1908** baseline by exactly the 1 new
test this row adds under `check-feme` (the lit `CHECK` block is a new
`RUN` split within an existing test file, not a new file, so it does not
add a second discovered test the way the gtest case does -- FileCheck
counts one `RUN` line per test regardless of how many `CHECK` blocks it
verifies, and this row appended to an existing `RUN` line's own file
rather than adding a new one).

**Reproducing.** Same invocation as H5b/H5f's own editions above
(`dEQP-VK.geometry.*` and the `draw_sample.txt` caselist); no new command
needed.

## Roadmap H5d: measured impact (geometry stage chained into `Executor::executeDraws`)

`dEQP-VK.geometry.*`: 200 cases, run against this driver before and after
this row's three commits.

**Before** and **after**, identically:

```
Test run totals:
  Passed:        0/200 (0.0%)
  Failed:        0/200 (0.0%)
  Not supported: 200/200 (100.0%)
  Warnings:      0/200 (0.0%)
  Waived:        0/200 (0.0%)
```

Every case still reports `NotSupported (Requested core feature is not
supported: geometryShader)`, i.e. `deqp-vk` still gates the whole group on
`VkPhysicalDeviceFeatures::geometryShader`, which `PhysicalDeviceInfo.cpp`
still reports as `VK_FALSE`. **This is the intended outcome for this row**,
matching H4's own precedent for the tessellation executor half: this row is
the graphics-executor side of geometry-stage support (`Executor::
executeDraws` now actually assembles a draw's primitives -- including the
four adjacency topologies -- gathers their vertex attributes into a
compiled geometry stage's own input batch, invokes it, and rasterizes its
merged emitted-vertex stream), but none of it is reachable from
`vkCreateGraphicsPipelines` yet: `GraphicsPipeline.cpp`'s
`translateFixedFunctionState` still rejects `VK_SHADER_STAGE_GEOMETRY_BIT`
outright (H5e's own job), so no real Vulkan geometry pipeline can be
created to exercise this row's new code path today. Flipping
`geometryShader` to `VK_TRUE` ahead of H5e would convert 200 honest
`NotSupported`s into (at best) 200 `Fail`s, so it was deliberately left
alone; `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`
consequently need no change for this row, same as H5a/H5b/H5c/H5f/H5g.

**Regression sample.** `Graphics/Executor.cpp` is shared by every draw, and
this row extends its post-vertex-stage plumbing again (the `RasterSig`/
`RasterOut`/`RasterClass` indirection H4's own tessellation chaining
introduced now also considers a bound geometry stage, and the topology
validation switch's four `*WithAdjacency` cases now conditionally accept
rather than unconditionally reject), so the same `draw_sample.txt`
1957-case sample this report has used since H4 was run again:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        139/1957 (7.1%)
  Not supported: 1806/1957 (92.3%)
```

and the sorted list of failing case names is byte-identical to H5c/H5f/
H5g's own baseline (139 distinct names, `diff`-clean via a small Python
`TestCaseResult`/`Result StatusCode` parse rather than raw `diff`, since the
`.qpa` XML is not line-stable across runs). **0 regressions, 0 new passes**
-- expected, since a pipeline with no geometry stage (every case in this
sample) takes exactly the paths it took before, with `RasterSig`/
`RasterOut`/`RasterClass` all still bound to the vertex/domain stage's own
signature and output block, and every adjacency-topology draw in the
sample (there are none) would otherwise still need a bound geometry stage
to pass validation, same as before this row.

**Unit coverage.** `ExecutorTest.cpp` gains two hand-compiled cases:
`GeometryStagePassesThroughATriangleCoveringTheViewport` (a geometry stage
that writes `SV_Position` itself and passes every vertex through
unchanged, checked against the same full-viewport solid-color fill
`FillsFullyCoveredTriangleWithSolidColor` already checks for the ordinary
vertex/fragment-only pipeline -- the merged stream really did reach
rasterization only if this matches) and
`RejectsAdjacencyTopologyWithoutAGeometryStage` (an adjacency-topology draw
still errors with no geometry stage bound). `ninja check-feme`
(`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in full: **1853/1912**
(59 pre-existing, unrelated `Unsupported`, 0 `Failed`), up from H5c's own
**1851/1910** baseline by exactly the 2 new tests this row adds.

**Reproducing.** Same invocation as H5b/H5f/H5g's own editions above
(`dEQP-VK.geometry.*` and the `draw_sample.txt` caselist); no new command
needed.

## Roadmap H5d-a: measured impact (`GeometryState::Invocations` / `gl_InvocationID`)

`dEQP-VK.geometry.*`: 200 cases, run against this driver before and after
this row's three commits.

**Before** and **after**, identically:

```
Test run totals:
  Passed:        0/200 (0.0%)
  Failed:        0/200 (0.0%)
  Not supported: 200/200 (100.0%)
  Warnings:      0/200 (0.0%)
  Waived:        0/200 (0.0%)
```

Every case still reports `NotSupported (Requested core feature is not
supported: geometryShader)`, same as H5d: this row is entirely about
`Executor::executeDraws`'s own handling of `GeometryState::Invocations` for a
hand-built pipeline, not about anything `vkCreateGraphicsPipelines` exposes,
so `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no
change, same as H5a/H5b/H5c/H5d/H5f/H5g.

**Regression sample.** The same `draw_sample.txt` 1957-case sample this
report has used since H4, run again against `Graphics/Executor.cpp`'s own
widened geometry invocation-building loop and the coupled
`GeometryStreamBuilder` capacity fix (see below):

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        139/1957 (7.1%)
  Not supported: 1806/1957 (92.3%)
```

The sorted list of failing case names is byte-identical to H5d's own
baseline (139 distinct names, diffed via the same small Python
`TestCaseResult`/`StatusCode` parse H5d's own edition used). **0
regressions, 0 new passes** -- expected, since no case in this sample binds
a geometry stage at all, so `Invocations` (which only takes effect when
`GraphicsPipeline::hasGeometryStages()`) never changes any of this sample's
own code paths.

**What changed and why real coverage was needed.** `GeometryState::
Invocations` (`feme/include/feme/Graphics/Geometry.h`) already existed from
H5a, but nothing downstream read it: `FemeGeometryInvocation` had no
`InvocationID` field, `GeometryWrapperPass` had no lowering for
`SystemValue::InvocationID`, and `Executor.cpp`'s H5d geometry-chaining
block always built exactly one invocation record per assembled input
primitive. Three commits close this gap: (1) `FemeGeometryInvocation` gains
an `InvocationID` field (`RuntimeABI.h`/`StageArgsLayout.h`), and
`GeometryWrapperPass` gains `lowerGeometryInvocationID` (sharing a new
`lowerGeometryInvocationField` helper with the existing
`lowerGeometryPrimitiveID`); (2) `buildGeometryInvocations`
(`GeometryInputs.h`/`.cpp`) gains an optional `InvocationIDs` array
parameter; (3) `Executor.cpp` computes `Invocations = max(GState.
Invocations, 1)` and builds `RowCount = PrimitiveCount * Invocations` rows,
repeating each primitive's input vertex attributes once per invocation
(primitive-major, invocation-minor: row `= P * Invocations + Inv`), each
stamped with both its `SV_PrimitiveID` and its own `gl_InvocationID`.

The roadmap flagged `collectGeometryStreams`/`mergeGeometryStreamsInLaneOrder`'s
lane-ordering contract as needing confirmation "with a real test rather than
assumed" for N invocations per primitive. A new `ExecutorTest.cpp` case,
`GeometryStageInvocationsRunOncePerDeclaredInvocationCount`, does exactly
that: a single input triangle with `GeometryState::Invocations = 2`, whose
geometry shader emits a full-viewport strip colored red for invocation 0 and
green for invocation 1 (selected via `gl_InvocationID`, no branching), with
`BlendMode::Replace` (last write wins) -- the test expects solid green,
confirming invocation 1's lane is merged/rasterized strictly after
invocation 0's. **This test failed on first run** (came back solid red, the
*first* lane's color, not the last), which led to finding a real,
pre-existing bug rather than a new one introduced by this row: the merged
`GeometryStreamBuilder`'s own capacity
(`GeometryStreamBuilder Combined(1, GState.MaxOutputVertices)`) was sized
from a single row's own per-invocation `MaxOutputVertices` bound, not the
combined `RowCount * GState.MaxOutputVertices` total every row could
together emit. Since `mergeGeometryStreamsInLaneOrder` is a monotonic
bump-allocator that truncates any lane whose vertices don't fit the
combined builder's remaining capacity, invocation 1's emissions were
silently dropped once invocation 0's own budget was exhausted -- present
since H5d, never before exercised because every prior geometry test used
exactly one row (one primitive, one invocation), so total emissions never
exceeded a single row's own bound. Fixing the capacity computation to
`RowCount * GState.MaxOutputVertices` made the new test pass without any
change to the lane-ordering logic itself, confirming the roadmap's
suspicion that "the merge's existing 'lane order' concept may already
tolerate" multiple invocations per primitive -- it does; only the unrelated
capacity bug stood in the way.

**Unit coverage.** New tests, one per compiler phase touched:
`GeometryWrapperTest.LowersInvocationIDInputLoad` (IR-level lowering),
`CompiledStageTest.InvokeGeometryReadsInvocationIDDistinctFromPrimitiveID`
(JIT-compiled end-to-end ABI read-back), `GeometryInputsTest.
BuildsDistinctInvocationIDsForRepeatedPrimitives` (host-side record
building, plus an extended `BuildsOneInvocationPerPrimitiveIdInOrder`), and
`ExecutorTest.GeometryStageInvocationsRunOncePerDeclaredInvocationCount`
(full executor/rasterizer path, the lane-ordering confirmation above).
`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1857/1916** (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from H5d's own **1853/1912** baseline by exactly the 4 new tests this
row adds.

**Reproducing.** Same invocation as H5d's own edition above
(`dEQP-VK.geometry.*` and the `draw_sample.txt` caselist); no new command
needed.

## Roadmap H5e: measured impact (`vkCreateGraphicsPipelines` geometry-stage acceptance)

**0/200, up from 0/200 -- but the composition changes completely, and that
is the expected, correctly-diagnosed result.** Before this row,
`dEQP-VK.geometry.*` reported `NotSupported (Requested core feature is not
supported: geometryShader)` for all 200 cases, since the feature bit was
still `VK_FALSE`. This row's Vulkan-layer work (`GraphicsPipeline.cpp`
accepting `VK_SHADER_STAGE_GEOMETRY_BIT`, `PhysicalDeviceInfo.cpp`'s
`geometryShader = VK_TRUE`) makes the feature real enough for real test
cases to actually attempt pipeline creation -- and the large majority
immediately hit the next, still-open gap this row's own investigation
found (see below), turning what was a uniform "not supported" result into
a mostly-`Fail` one:

```
Test run totals:
  Passed:        0/200 (0.0%)
  Failed:        167/200 (83.5%)
  Not supported: 33/200 (16.5%)
```

**Root-cause triage of the 167 failures**, grouped by first diagnostic:

| Count | Root cause | Bucket |
|---|---|---|
| 116 | `error: failed to legalize operation 'spirv.EmitVertex'` | New, this row's own discovered gap (H5e-a) |
| 6 | `error: failed to legalize operation 'spirv.EndPrimitive'` | New, this row's own discovered gap (H5e-a) |
| 14 | `vk.createImage(...)`: `VK_ERROR_INITIALIZATION_FAILED` (`dEQP-VK.geometry.layered.3d.*`, a 3D-image-type layered render target) | Pre-existing, unrelated to geometry stage compilation at all -- the failure is in image creation, before any geometry shader is even touched |
| 8 | `error: ... unsupported fragment system value for element 0` (`dEQP-VK.geometry.layered.*.fragment_layer`, reading back `gl_Layer` in the fragment stage) | Pre-existing layered-rendering fragment-input gap, orthogonal to this row's own geometry-stage acceptance work |
| 21 | `Fail (vk.createGraphicsPipelines(...))`, **no diagnostic printed at all** (`dEQP-VK.geometry.emit.*_emit_0_end_0`'s degenerate zero-emit shaders, `dEQP-VK.geometry.input.basic_primitive.{line_strip,line_strip_adjacency,triangle_fan}`, `dEQP-VK.geometry.input.triangle_strip_adjacency.vertex_count_*`, and two `builtin_variable.in_block.primitive_id_in*` cases) | Not yet individually isolated -- flagged for H5e-a's own CTS re-run to re-triage once the dominant `EmitVertex`/`EndPrimitive` noise is gone (see below) |
| 1 | `error: feme-cpu-simdize: ... divergent value ...` | Pre-existing `SIMDize` limitation (already documented, roadmap milestone 7 deviation), unrelated to geometry |
| 1 | `error: 'llvm.getelementptr' op operand #0 must be LLVM pointer type ...` | Pre-existing struct/array lowering gap, unrelated to geometry |

**The dominant bucket (122 of 167, 73%) is exactly the gap this row's own
investigation predicted before writing any Vulkan-layer code**: grepping
the whole tree for `EmitVertexOp`/`EndPrimitiveOp` found zero occurrences
in `ConvertSPIRVToLLVMPass.cpp`/`SPIRVToLLVMPatterns.cpp` -- SPIR-V's
`spirv.EmitVertex`/`spirv.EndPrimitive` ops (which do exist in MLIR's own
SPIR-V dialect) have no lowering into the `feme.stage.stream.emit`/`.cut`
intrinsics `GeometryWrapperPass` has fully implemented and tested since
G5. Virtually every real GLSL geometry shader calls both at least once
(that is the entire point of a geometry stage), so this alone accounts for
most of the group. **This row deliberately does not fix that gap**: it is
a new SPIRVToLLVM conversion pattern, not a `vkCreateGraphicsPipelines`
acceptance-path change, so it is broken out as roadmap H5e-a rather than
folded into this row (mirroring H5a's own precedent of stopping at
execution-mode reflection and spinning off H5b for the per-vertex-input
addressing gap it found).

**The 33 `NotSupported` cases** are the pre-existing, unrelated feature
gates `dEQP-VK.geometry.*` already exercised before this row --
`vertexPipelineStoresAndAtomics`, `fragmentStoresAndAtomics`,
`shaderTessellationAndGeometryPointSize`, `depth/stencil format is not
supported` -- none of them geometry-specific, all unaffected by this row's
own changes.

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` are
updated (see below): `geometryShader` and `multiviewGeometryShader` both
flip from an unimplemented-feature-bit count entry to an implemented one.

**Regression sample.** `dEQP-VK.draw.*`'s 1957-case sample, same
`draw_sample.txt` this report has used since H4:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        155/1957 (7.9%)
  Not supported: 1790/1957 (92.5%)
```

**12 passing cases unchanged (0 regressions)**; `Failed` moves from H5d-a's
own **139** baseline to **155** (+16), and `Not supported` correspondingly
moves from **1806** to **1790** (-16, exactly offsetting). This is
the expected shape, not a regression: the 16 newly-failing cases are
`dEQP-VK.draw.*` pipelines that previously reported `NotSupported
(Requested core feature is not supported: geometryShader)` (gated on the
feature bit this row flips to `VK_TRUE`) and now proceed far enough to
attempt real pipeline creation, landing in the same pre-existing/new-gap
buckets the dedicated geometry run above triages (mostly `EmitVertex`/
`EndPrimitive`, a few `getelementptr`/`divergent value`). No previously-
passing case's own status changed.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1862/1921** (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from H5d-a's own **1857/1916** baseline by exactly the 5 new tests this
row adds -- `GraphicsPipelineTest.cpp`'s `AcceptsGeometryStage`,
`AcceptsAdjacencyTopologyWithGeometryStage`,
`RejectsAdjacencyTopologyWithoutGeometryStage`, and
`PhysicalDeviceInfoTest.cpp`'s `GeometryLimitsMeetCore10Minimums`,
`AggregateVulkan11FeaturesReportMultiviewGeometryShader` (the existing
`MultiviewFeaturesReportMultiviewTrueAmplificationFalse` and
`OnlyRobustBufferAccessDualSrcBlendASTCLDRAndMultiViewportAreAdvertised`
tests were updated in place, not added, to assert `multiviewGeometryShader`/
`geometryShader` are now `VK_TRUE`).

**One correctness fix bundled with this row.**
`validateStageInterfaces`'s fragment-input/vertex-output location-linkage
loop always checked against the raw vertex stage's own signature
(`*VSSig`), even for a tessellating pipeline, where the actual last
pre-rasterization producer is the domain stage. This was never exercised
before (no existing test or CTS shape has a domain-stage output at a
`Location` a fragment input also reads), but adding geometry -- which
also needs to be treated as "the last pre-raster stage" for both this
check and the pre-existing `SV_Position` check right next to it -- means
both now consistently use the same `PositionSig` (`GeomSig` if present,
else `DomainSig`, else `VSSig`) selection the `SV_Position` check already
used. This is a strictly more-correct fix in the same code this row
already had to touch, not unrelated scope creep, but it does change
tessellation-pipeline validation behavior too; no existing or new test
regressed as a result.

**Reproducing.**

```
mkdir -p run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.geometry.*" --deqp-log-filename=geom.qpa

grep -v viewport_height \
  /home/dev/dev/VK-GL-CTS/external/vulkancts/mustpass/main/vk-default/draw.txt \
  | awk 'NR%15==1' > draw_sample.txt
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-caselist-file=draw_sample.txt --deqp-log-filename=draw_sample.qpa
```

## Roadmap H5e-a: measured impact (`spirv.EmitVertex`/`spirv.EndPrimitive` lowering)

**Change.** `SPIRVToLLVMPatterns.cpp` gains `EmitVertexConversionPattern`/
`EndPrimitiveConversionPattern`, converting `spirv.EmitVertex`/
`spirv.EndPrimitive` (both zero-operand, zero-result, non-terminator ops,
confirmed present in MLIR's own SPIR-V dialect by
`SPIRVPrimitiveOps.td`) into a call to `feme.stage.stream.emit(0)`/
`feme.stage.stream.cut(0)` respectively -- the same
`feme::StageOpKind::StreamEmit`/`StreamCut` intrinsics
`feme::cpu::lowerGeometryStreamEmit`/`lowerGeometryStreamCut`
(GeometryWrapper.cpp, built and fully tested under roadmap G5) already
know how to lower into a `GeometryStreamBuilder::emit`/`cut` call. The
stream operand is always the constant `0`, matching both ops' own SPIR-V
spec text ("must only be used when only one stream is present") and
`GeometryState`/`FemeGeometryArgs`'s current single-output-stream-only
support. Mirrors `SubpassLoadPattern`'s existing precedent of calling a
`feme.stage.*` function directly (an ordinary named call, not an
`llvm.spv.*` intrinsic) rather than adding a bespoke LLVM IR shape.
`ninja check-feme` gains one new lit test,
`spirv-to-llvm-geometry-stream.mlir` (two cases: a plain emit-then-cut,
and three emits sharing one `feme.stage.stream.emit` declaration).

**`dEQP-VK.geometry.*` re-run (200 cases), before/after:**

```
Before (H5e's own baseline):
  Passed:        0/200 (0.0%)
  Failed:        167/200 (83.5%)
  Not supported: 33/200 (16.5%)

After (this row):
  Passed:        1/200 (0.5%)
  Failed:        166/200 (83.0%)
  Not supported: 33/200 (16.5%)
```

`NotSupported` is byte-identical (still the same pre-existing,
geometry-unrelated feature/limit/format gates); one previously-failing
case now passes outright
(`dEQP-VK.geometry.varying.vertex_no_op_geometry_out_1`, a shader that
declares varyings but never actually calls `EmitVertex`/`EndPrimitive`
conditionally in a way the old "always fail to legalize" gap would have
blocked regardless -- now it reaches real rendering and matches).

**The dominant `EmitVertex`/`EndPrimitive` failure bucket (122 cases,
73% of H5e's 167) is completely gone**, confirmed by grepping the new
log for `spirv.EmitVertex`/`spirv.EndPrimitive`: zero matches. Every one
of those 122 cases now proceeds further into the pipeline and lands in
one of the buckets below -- this is the "re-triage the whole group" this
row's roadmap entry called for, not a re-count of the same failures:

| Count | Root cause | Bucket |
|---|---|---|
| 60 | `error: 'llvm.getelementptr' op operand #0 must be LLVM pointer type ... but got '!llvm.array<N x struct<...>>'`/`'!llvm.array<N x vector<4xf32>>'` (`dEQP-VK.geometry.basic.*`, `varying.*`) | New (was masked by `EmitVertex`): a geometry stage's per-invocation output-vertex-array storage (an `N`-element array of a vertex's own output signature, addressed once per `EmitVertex`) lowers to a plain LLVM array type rather than a pointer-typed alloca/global a `getelementptr` can index -- a geometry-specific stack/storage-allocation gap, not a SPIR-V-conversion one. Root cause not yet isolated to a single file; flagged for a follow-on row |
| 24 | `error: feme-cpu-linearize: function 'main': loop at '' has an internal branch in 'Flow'; unsupported (roadmap milestone 6 deviation)` (`dEQP-VK.geometry.layered.*.readback`) | Pre-existing, documented `LinearizePass` limitation (roadmap milestone 6 deviation), unrelated to geometry or this row |
| 21 | `Fail (vk.createGraphicsPipelines(...))`, no diagnostic (`dEQP-VK.geometry.builtin_variable.in_block.primitive_id_in*`, `input.basic_primitive.{
  line_strip, line_strip_adjacency, triangle_fan}`, `input.triangle_strip_adjacency.vertex_count_*`, `emit.*_emit_0_end_0`) | Exactly H5e's own flagged "~21-case silent `VK_ERROR_INITIALIZATION_FAILED`" bucket, unchanged in composition and count now that the `EmitVertex`/`EndPrimitive` noise is gone -- still not individually isolated, spun off below as H5e-b |
| 20 | `NotSupported (Requested core feature is not supported: fragmentStoresAndAtomics ...)` (`dEQP-VK.geometry.layered.*.secondary_cmd_buffer`) | Pre-existing, unrelated feature gate |
| 18 | `Fail (vk.queueSubmit(...): VK_ERROR_INITIALIZATION_FAILED ...)` (`dEQP-VK.geometry.layered.{1d_array,2d_array}.*.multiple_layers_per_invocation`/`render_to_one`/`render_to_default_layer`/`render_to_all`) | New (was masked by `EmitVertex`): layered-rendering-specific geometry execution failure at submit time, distinct from the `basic`/`varying` `getelementptr` bucket above (these use `gl_Layer` output, not just varying output-array storage). Not yet isolated; spun off below as H5e-c |
| 14 | `Fail (vk.createImage(...): VK_ERROR_INITIALIZATION_FAILED ...)` (`dEQP-VK.geometry.layered.3d.*`) | Pre-existing, unrelated to geometry-stage compilation at all (image creation, before any geometry shader is touched) -- unchanged from H5e's own report |
| 9 | `error: feme-cpu-simdize: ... divergent value ... (roadmap milestone 7 deviation)` | Pre-existing `SIMDize` limitation, already documented, unrelated to geometry (was previously undercounted at 1 while `EmitVertex` masked the rest of this bucket) |
| 8 | `error: feme-cpu-wrap-fragment: unsupported fragment system value for element 0` (`dEQP-VK.geometry.layered.*.fragment_layer`) | Pre-existing layered-rendering fragment-input gap, unchanged from H5e's own report |
| 6 | `error: feme-cpu-wrap-geometry: geometry stage wrapper requires attached feme.signature metadata` (`dEQP-VK.geometry.emit.*_emit_0_end_1`) | New (was masked by `EmitVertex`): a geometry entry point compiled from one of these specific `emit`-count shapes reaches `GeometryWrapperPass` without the `feme.signature` metadata it requires -- a reflection/attachment gap upstream of the wrapper, not this row's own lowering. Not yet isolated; spun off below as H5e-d |
| 6 | `Fail (Rendered images are incorrect)` (`dEQP-VK.geometry.layered.2d_array.*.multiple_layers_per_invocation` variants) | New (was masked by `EmitVertex`): pipeline now runs to completion and produces a wrong image -- a genuine rendering-correctness bug, not a legalization/creation failure. Not yet isolated; spun off below as H5e-e |
| 4+4 | `NotSupported (Unsupported limit: maxGeometryShaderInvocations < {64,127})` | Pre-existing, unrelated limit gate (`GeometryLimitsMeetCore10Minimums`'s own advertised minimum is smaller than these two cases request) |
| 2 | `NotSupported (Requested core feature is not supported: vertexPipelineStoresAndAtomics ...)` | Pre-existing, unrelated feature gate |
| 2 | `NotSupported (Depth/stencil format is not supported ...)` | Pre-existing, unrelated format gate |
| 1 | `NotSupported (Requested core feature is not supported: shaderTessellationAndGeometryPointSize ...)` | Pre-existing, unrelated feature gate |
| 1 | `Pass` | `dEQP-VK.geometry.varying.vertex_no_op_geometry_out_1` -- the one case this row newly turns into an outright pass |

Total: 1 (Pass) + 166 (Fail) + 33 (NotSupported) = 200, matching the run.

**New lettered rows spun off from this re-triage** (added to
Roadmap.md, mirroring H5a/H5e's own precedent of spinning off whatever a
re-triage finds rather than folding it back into the row that found it):
H5e-b (the 21-case silent `vkCreateGraphicsPipelines` failure, unchanged
from H5e's own flagged bucket), H5e-c (the 18-case layered
`vkQueueSubmit` failure), H5e-d (the 6-case missing `feme.signature`
metadata in `GeometryWrapperPass`), and H5e-e (the 6-case wrong-image
rendering bug). The 60-case `getelementptr`/output-array-storage bucket
and the pre-existing/already-documented buckets (`LinearizePass`,
`SIMDize`, fragment-layer, layered.3d image creation, and the various
`NotSupported` feature/limit/format gates) are left uncategorized into a
new row for now, since they are either already tracked elsewhere or too
large/unisolated to scope into one lettered row without further
investigation.

**Regression sample.** `dEQP-VK.draw.*`'s 1957-case `draw_sample.txt`
sample, same file H5e's own report used:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        155/1957 (7.9%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical to H5e's own baseline (12/155/1790, 0 regressions) --
expected, since `EmitVertex`/`EndPrimitive` calls only ever occur inside
a geometry-stage shader module, and no `dEQP-VK.draw.*` case in this
sample exercises one.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1863/1922** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H5e's own **1862/1921** baseline by exactly the 1 new
lit test this row adds (`spirv-to-llvm-geometry-stream.mlir`, 2 `RUN`
splits counted as 1 discovered test file with 2 `CHECK` blocks under
`--split-input-file`).

## Roadmap H5e-b: measured impact (silent `vkCreateGraphicsPipelines`/`vkQueueSubmit` no-diagnostic bucket)

**Change.** Two independent, unrelated false-positive validation bugs in
`GraphicsPipeline.cpp` accounted for all 21 of this row's own flagged
cases, both stale relative to work later milestones had already landed
elsewhere; neither needed a new feature, just bringing an already-shipped
capability's validation into agreement with itself.

1. **Stale primitive-restart topology gate.** The creation-time
   `primitiveRestartEnable` check still only allowed
   `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP`, unchanged since roadmap V6
   first added it. But roadmap H5d's own `Executor.cpp` change ("Chain
   the geometry stage into `Executor::executeDraws`") had, in the same
   commit, quietly extended the *runtime* restart condition to also cover
   `LineStrip`, `TriangleFan`, and the two `*StripWithAdjacency`
   topologies -- without anyone updating the creation-time gate to match.
   Every pipeline using one of those four topologies plus restart was
   therefore rejected at creation, before the (already-correct) executor
   ever got a chance to run it. Fixed by factoring a single shared
   helper, `feme::graphics::topologySupportsPrimitiveRestart`
   (`Pipeline.h`/`Pipeline.cpp`, mirroring the existing
   `topologyHasAdjacency` precedent), and pointing both
   `GraphicsPipeline.cpp`'s gate and `Executor.cpp`'s own
   `RestartEnabled` condition at it, so the two can never drift apart
   again. Accounts for 18 of the 21 cases
   (`builtin_variable.in_block.primitive_id_in{
  , _restarted}`,
   `input.basic_primitive.{
  line_strip, line_strip_adjacency, triangle_fan}`,
   `input.triangle_strip_adjacency.vertex_count_*`).
2. **Degenerate zero-emit geometry shader rejected outright.**
   `dEQP-VK.geometry.emit.{line_strip,points,triangle_strip}_emit_0_end_0`
   compile a geometry entry point whose body is a deliberate no-op (CTS's
   own `emitCountA=emitCountB=endCountA=endCountB=0` shape: no
   `gl_Position` write, no varying write, no `EmitVertex`/`EndPrimitive`
   call at all). SPIR-V only lists an entry point's *used* interface
   variables, so such a shader's reflected `EntrySignature` has **zero
   elements outright** -- not merely a missing `Position` element.
   `validateStageInterfaces`'s "the last pre-rasterization stage must
   write a 4-component SV_Position" check (and the fragment-input/
   vertex-output location-linkage loop right beside it, which would
   otherwise reject the fragment stage's now-unmatched varying input)
   both unconditionally tripped on this empty signature. Since a
   geometry stage that provably emits nothing can never contribute
   anything to rasterization regardless of whether it ever would have
   written a position or a varying, both checks now treat a fully empty
   geometry signature (`GeometryNeverWrites`) as a legal no-op instead of
   an error. Fixing only the creation-time check exposed a masked
   third, exactly analogous runtime check in `Executor.cpp`'s
   `executeDraws` (the same "last pre-rasterization stage does not write
   an SV_Position" condition, now checked against
   `GSSig->Elements.empty()` and short-circuited to `Error::success()`
   before the `VSPosition` requirement). Accounts for the remaining 3
   cases, all three now passing outright.

`FEME_VULKAN_LOG_CREATION_ERRORS=1` (the existing opt-in-only diagnostic
env var `Diagnostics.cpp` already implements) reruns of the exact 21-case
list confirmed each fix in isolation before and after: 18 cases printed
`"primitive restart is only implemented for
VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP"`, 3 printed `"geometry stage does
not write a 4-component SV_Position output"` -- both real, specific
diagnostics that were simply never surfaced because this env var is off
by default during a normal CTS run, which is this row's own "and no
diagnostic printed at all" observation explained, not a logging bug to
fix on its own.

**`dEQP-VK.geometry.*` re-run (200 cases), before/after:**

```
Before (H5e-a's own baseline):
  Passed:        1/200 (0.5%)
  Failed:        166/200 (83.0%)
  Not supported: 33/200 (16.5%)

After (this row):
  Passed:        4/200 (2.0%)
  Failed:        163/200 (81.5%)
  Not supported: 33/200 (16.5%)
```

`NotSupported` is byte-identical (still the same pre-existing,
geometry-unrelated feature/limit/format gates). All 21 of this row's own
cases were re-examined individually after both fixes landed:

| Count | Outcome | Detail |
|---|---|---|
| 3 | `Pass` | `emit.{line_strip,points,triangle_strip}_emit_0_end_0` -- the degenerate zero-emit fix closes these outright |
| 18 | `Fail` with a real, printed diagnostic (`error: 'llvm.getelementptr' op operand #0 must be LLVM pointer type ... but got '!llvm.array<N x struct<...>>'`) | The restart-topology fix lets these 18 proceed past pipeline creation into the same pre-existing 60-case `getelementptr`/output-array-storage bucket H5e-a already flagged and left unisolated -- correctly reclassified into an already-tracked bucket, not a new open item this row needs to carry forward |

Total: 4 (Pass) + 163 (Fail) + 33 (NotSupported) = 200, matching the run.
This row's own literal ask -- isolate the root cause behind the silent,
un-diagnosed 21-case bucket -- is complete: every one of the 21 cases now
either passes or fails with a real, attributed diagnostic; none are
silent anymore.

**Regression sample.** `dEQP-VK.draw.*`'s 1957-case `draw_sample.txt`
sample, same file H5e/H5e-a's own reports used:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        155/1957 (7.9%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical to H5e-a's own baseline (12/155/1790, 0 regressions) --
expected, since neither fix touches any non-geometry topology/pipeline
path (the restart-topology fix only changes which topologies restart is
*accepted for*, not how restart itself behaves; the degenerate-zero-emit
fix only relaxes checks that require a bound, non-trivial geometry
stage in the first place).

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1867/1926** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H5e-a's own **1863/1922** baseline by exactly the 4
new unit tests this row adds:
`GraphicsPipelineTest.AcceptsPrimitiveRestartOnStripAndFanTopologies`,
`GraphicsPipelineTest.AcceptsGeometryStageThatNeverEmits`,
`PrimitiveTopologyTest.SupportsPrimitiveRestartIdentifiesEveryStripAndFanKind`,
and `ExecutorTest.ExecutesDrawsAsNoOpWhenGeometryStageNeverEmits`.

## Roadmap H5e-c: measured impact (layered geometry-draw `vkQueueSubmit` failure bucket)

**Change.** Two independent, small fixes, landed as two commits.

1. **`vkQueueSubmit`'s own error path now logs, instead of silently
   discarding, a failed command buffer's `llvm::Error`.**
   `executeCommandBuffers` (`Sync.cpp`, shared by `vkQueueSubmit`/
   `vkQueueSubmit2`) called a bare `consumeError` on the `llvm::Error`
   `executeCommandBuffer` returned, unlike `vkCreateGraphicsPipelines`'s
   own creation-failure path, which already routes through
   `feme::vulkan::logCreationFailure` so the existing opt-in
   `FEME_VULKAN_LOG_CREATION_ERRORS` env var can surface the real
   diagnostic. This alone is why this row's own flagged bucket looked
   like it had "no diagnostic emitted" -- the diagnostic was simply never
   given a channel to reach, not actually absent.
2. **`resolveAttachmentView` (`RenderPass.cpp`) now accepts a
   `VK_IMAGE_VIEW_TYPE_1D`/`_1D_ARRAY`/`_CUBE`/`_CUBE_ARRAY` render-target
   view, not only `_2D`/`_2D_ARRAY`.** With (1) landed, re-running the
   exact failing case list with `FEME_VULKAN_LOG_CREATION_ERRORS=1`
   immediately surfaced the real root cause directly:

   ```
   vkQueueSubmit: only a 2D or 2D-array image view may be a render target
     Fail (vk.queueSubmit(queue, 1u, &submitInfo, *fence): VK_ERROR_INITIALIZATION_FAILED at vkCmdUtil.cpp:338)
   ```

   All four of `VK_IMAGE_VIEW_TYPE_1D`/`_1D_ARRAY`/`_CUBE`/`_CUBE_ARRAY`
   are legal Vulkan color-attachment view types; `resolveAttachmentView`
   rejected every one of them outright. Fixed by accepting all four
   alongside the already-accepted `_2D`/`_2D_ARRAY`, addressed identically
   (`Image` itself never has a "1D" or "cube" dimension -- only the
   *view's* own `ImageDimension` records the addressing convention, and
   the existing layer-major `Img.data() + baseArrayLayer * SlicePitch`
   addressing already honors it with no change).

**This row's own "1d_array,2d_array" framing undercounted the actual
scope.** A targeted re-run of `dEQP-VK.geometry.layered.{1d_array,
2d_array}.*` (40 cases, before fix (1) alone, just to confirm the
diagnostic wiring):

```
1d_array.*: 6 Fail (vk.queueSubmit ... VK_ERROR_INITIALIZATION_FAILED)
2d_array.*: 0 Fail (vk.queueSubmit ...) -- already Fail (Rendered images are incorrect) or Fail (vk.createGraphicsPipelines ...)
```

Only `1d_array` actually hit a `vkQueueSubmit` failure; `2d_array`'s
`Texture2DArray` dimension was already accepted, so its own
`multiple_layers_per_invocation`/`render_to_one`/`render_to_default_layer`
cases were already landing in roadmap H5e-e's "Rendered images are
incorrect" bucket by the time this row was investigated (a genuine,
still-open rendering-correctness bug, left there). A full
`dEQP-VK.geometry.*` re-run found the other 12 cases this row's own "18"
headline count implied: `dEQP-VK.geometry.layered.cube.*` and
`cube_array.*` hit the *identical* `resolveAttachmentView` rejection
(their own render-target view is `VK_IMAGE_VIEW_TYPE_CUBE`/
`_CUBE_ARRAY`, never even considered by this row's own text), for exactly
6 cases each:

```
Before (fix (2) applied):
  1d_array.{12_1_6,64_1_4}.{multiple_layers_per_invocation,render_to_default_layer,render_to_one}: 6 cases
  cube.{36_36_6,64_64_6}.{multiple_layers_per_invocation,render_to_default_layer,render_to_one}: 6 cases
  cube_array.{36_36_12,64_64_12}.{multiple_layers_per_invocation,render_to_default_layer,render_to_one}: 6 cases
  Total: 18 cases, all Fail (vk.queueSubmit ... VK_ERROR_INITIALIZATION_FAILED)
```

**`dEQP-VK.geometry.*` re-run (200 cases), before/after fix (2):**

```
Before (H5e-b's own baseline):
  Passed:        4/200 (2.0%)
  Failed:        163/200 (81.5%)
  Not supported: 33/200 (16.5%)

After (this row):
  Passed:        4/200 (2.0%)
  Failed:        163/200 (81.5%)
  Not supported: 33/200 (16.5%)
```

Byte-identical totals -- expected, since every one of the 18 cases moves
from one `Fail` reason to another, not out of `Failed` entirely. All 18
were individually re-examined after the fix: every one now runs to
completion and lands in `Fail (Rendered images are incorrect)`, the same
bucket roadmap H5e-e already tracks (previously scoped to 6
`2d_array.*.multiple_layers_per_invocation` variants; now 24 cases across
`1d_array`/`2d_array`/`cube`/`cube_array`, still a genuine
rendering-correctness bug, left to H5e-e). This row's own literal ask --
stop these 18 cases from failing at `vkQueueSubmit` with no diagnostic --
is complete: zero `vkQueueSubmit` failures remain anywhere in the
`dEQP-VK.geometry.*` group.

**Regression sample.** `dEQP-VK.draw.*`'s 1957-case `draw_sample.txt`
sample, same file every prior row's own report used:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        155/1957 (7.9%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical to H5e-b's own baseline (12/155/1790, 0 regressions) --
expected, since no `dEQP-VK.draw.*` case in this sample exercises a
geometry stage or a 1D/cube(-array) render target.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1870/1929** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H5e-b's own **1867/1926** baseline by exactly the 3
new unit tests this row adds: `RenderPassTest.
ResolveAttachmentViewAcceptsOneDArrayView`, `RenderPassTest.
ResolveAttachmentViewAcceptsCubeArrayView`, and `RenderPassTest.
ResolveAttachmentViewRejects3DView`.

## Roadmap H5e-d: measured impact (`GeometryWrapperPass` missing-signature bucket)

**Change.** `canonicalizeSPIRVStage` (`feme/lib/Transforms/Graphics/
CanonicalizeStage.cpp`) gains a new `else if (Stage == ShaderStage::
Geometry)` branch, run whenever its existing signature-building branch
(`if (!InputGlobals.empty() || !OutputGlobals.empty())`) does not fire,
attaching an explicit empty `EntrySignature` via `dxil::setEntrySignature`.

**Root cause.** This row's own text already named the shape correctly: a
geometry entry compiled from an `emit`-count combination whose `emitCountA
== 0` (the CTS's own `EmitTest::shaderGeometry` generates its
`EmitVertex`/`gl_in[]`/output-writing loop body `emitCountA` times, so
`emitCountA == 0` means that loop contributes *zero* IR instructions,
leaving only a bare `EndPrimitive()` -- already lowered to
`feme.stage.stream.cut` by roadmap H5e-a's own `SPIRVToLLVMPatterns` fix,
independent of this row) discovers no `Input`/`Output` stage-IO globals at
all during `canonicalizeSPIRVStage`'s own discovery loop. Its
signature-building branch, guarded on at least one such global existing,
never runs, so `dxil::setEntrySignature` is never called and the entry is
left with no `!feme.signature` metadata whatsoever.
`feme::cpu::GeometryWrapperPass` (`GeometryWrapper.cpp`)'s
`lowerGeometryStageOps` then hard-requires that metadata for any geometry
entry using so much as one stage op -- a stream cut included -- and
errors out instead of tolerating its absence the way ordinary
`loadInput`/`storeOutput` resolution elsewhere in this file does.

This is the same underlying shape roadmap H4g's own tessellation
investigation already found once (a genuinely stage-IO-free entry never
gets a signature attached), but a different consumer's tolerance for it:
H4g's own fix landed one layer further downstream, at
`CompiledStage::create`'s serialization boundary, specifically *because*
`canonicalizeSPIRVStage` cannot safely distinguish a genuinely-empty
SPIR-V entry from an unresolved DXIL-origin one for `Vertex`/`Fragment`
(both dispatched through this same function by
`CanonicalizeStagePass::run`, alongside `canonicalizeDXILStage`) --
attaching an unconditional empty signature there was tried and reverted
for exactly that reason, since it broke `CanonicalizeStageTest.
UnresolvableLoadInputIsLeftAlone`'s DXIL-origin fragment entry, which
relies on *staying* signature-less. `Geometry` has no such ambiguity:
`CanonicalizeStagePass::run` only ever routes a `Geometry`-stage function
through `canonicalizeSPIRVStage`, never `canonicalizeDXILStage`, so this
row's fix is scoped to `Stage == ShaderStage::Geometry` only, landing the
fix at the same layer H4g's own first (reverted) attempt wanted to, but
now provably safe to do so for this one stage.

**Unit test.** `CanonicalizeStageTest.
GeometryStreamCutOnlyEntryStillGetsASignature`: a geometry entry
containing only a bare `feme.stage.stream.cut` call and no stage-IO
globals at all, confirming `CanonicalizeStagePass::run` now reports
`Changed` and attaches an empty (zero-element) `!feme.signature`, exactly
the shape `GeometryWrapperPass` needs to see and previously never did.
The pre-existing `CanonicalizeStageTest.UnresolvableLoadInputIsLeftAlone`
and `GeometryStageMapsSystemValues` tests continue to pass unmodified,
confirming the DXIL-origin-ambiguity case this row's fix is scoped away
from, and the ordinary geometry-signature-building path, are both
untouched.

`ninja check-feme` (assertions-enabled, ccache build) passes in full,
**1871/1930** (59 pre-existing, unrelated `Unsupported`, 0 `Failed`), up
from H5e-c's own **1870/1929** by exactly the 1 new unit test this row
adds.

**`dEQP-VK.geometry.emit.*_emit_0_end_1` re-run (3 cases), before/after:**

```
Before (H5e-c's own baseline, inherited unchanged from H5e-b):
  Fail (feme-cpu-wrap-geometry: geometry stage wrapper requires attached
        feme.signature metadata) -- all 3 cases

After (this row):
  Pass -- all 3 cases (3/3, 100%)
```

**`dEQP-VK.geometry.*` re-run (200 cases), before/after:**

```
Before (H5e-c's own baseline):
  Passed:        4/200 (2.0%)
  Failed:        163/200 (81.5%)
  Not supported: 33/200 (16.5%)

After (this row):
  Passed:        10/200 (5.0%)
  Failed:        157/200 (78.5%)
  Not supported: 33/200 (16.5%)
```

Exactly 6 new passes, not 3: the fix is not specific to `endCountA == 1`
the way this row's own title suggests -- it applies to *any*
`emitCountA == 0` shape, so `dEQP-VK.geometry.emit.{line_strip,points,
triangle_strip}_emit_0_end_2` (the `endCountA == 2` siblings, calling
`EndPrimitive()` twice instead of once, otherwise identical) flip from
the exact same failure to `Pass` alongside the 3 this row's own text
named. (`emit_0_end_0`'s own 3 cases were already fixed by roadmap H5e-b,
a separate `vkCreateGraphicsPipelines`-time fix, and are unaffected by
this row -- accounting for the 4-case baseline above.) Zero
`feme-cpu-wrap-geometry: ... requires attached feme.signature metadata`
errors remain anywhere in the group.

**Regression sample.** `dEQP-VK.draw.*`'s 1957-case `draw_sample.txt`
sample, same file every prior row's own report used:

```
Test run totals:
  Passed:        12/1957 (0.6%)
  Failed:        155/1957 (7.9%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical to H5e-c's own baseline (12/155/1790, 0 regressions) --
expected, since no `dEQP-VK.draw.*` case in this sample exercises a
geometry stage at all.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: this is a pure reflection/metadata-attachment fix inside
`canonicalizeSPIRVStage`, touching no feature bit or extension (the
`geometryShader` feature bit was already advertised, and the failure mode
this row fixes is unrelated to capability advertisement).

## Roadmap H5e-e: measured impact (non-multiview layered-rendering `gl_Layer` routing)

**Change.** Two independent fixes in `feme/lib/Vulkan/CommandBuffer.cpp`:

1. A new `fullLayerMask(uint32_t Layers)` helper (returns an all-ones mask
   over `Layers` bits, saturating at `~0u` once `Layers >= 32`), used by
   `applyLoadOps` in place of the previous unconditional `1u` fallback
   whenever a render-target binding's `ViewMask` is `0` (no multiview).
2. `runDraw`'s per-view loop now only calls `sliceAttachmentLayer` on
   `Attachments`/`SubpassInputs`/`ResolveAttachments`/`DepthStencil` when
   `Gfx.Binding.ViewMask != 0` (a genuine multiview instance); a plain
   (non-multiview) draw instead passes every attachment through with its
   full, unsliced layer range untouched.

**Root cause.** This row's own text suspected `gl_Layer` routing,
per-invocation output-vertex addressing, or stream-merge ordering under
`Invocations > 1` -- none of those. The actual bucket this row owns is
broader than its own "6 `2d_array`" framing (24 cases: `render_to_one`,
`render_to_default_layer`, `multiple_layers_per_invocation`, across all 4
non-`3d` view types x 2 sizes), and both real bugs live entirely in
`CommandBuffer.cpp`, upstream of any geometry-stage-specific logic:

- **`applyLoadOps`'s clear mask.** `uint32_t ViewMask = Binding.ViewMask ?
  Binding.ViewMask : 1u;` collapsed to a single bit for any non-multiview
  render pass, regardless of the attachment's real array-layer count
  (`Binding.Layers`, already correctly populated from `Fb.layers()`/
  `pRenderingInfo->layerCount` but never consulted for this). Per Vulkan
  semantics, `VK_ATTACHMENT_LOAD_OP_CLEAR` on a layered (even
  non-multiview) attachment must clear *every* layer up front, since a
  geometry (or vertex, per below) stage's `gl_Layer` output can route to
  any of them -- this left layers 1..N with whatever bytes the image's
  backing memory happened to start with.
- **`runDraw`'s per-view attachment slicing.** The per-view loop
  unconditionally called `sliceAttachmentLayer(A, ViewIndex)` on every
  attachment for every draw -- correct and necessary for true multiview
  (each view bit maps to one specific layer via `gl_ViewIndex`, no
  shader-side `gl_Layer` write involved), but the loop still ran once
  (`ViewIndex == 0`) for the overwhelmingly common non-multiview case too,
  permanently slicing every attachment down to exactly 1 layer (layer 0)
  *before* `Executor.cpp` ever got a chance to run its own,
  already-correct per-primitive `gl_Layer`-driven layer routing
  (`resolvePrimitiveState`/`resolveRenderTargetArrayLayer`). The result:
  `getDrawLayerCount(Draw)` always saw `Attachment.ArrayLayers == 1`, so
  `resolveRenderTargetArrayLayer(RequestedLayer, 1)` rejected any
  non-zero requested layer outright, silently discarding the primitive.
  Confirmed directly with temporary debug instrumentation in
  `resolvePrimitiveState` (since removed): `RequestedLayer=3,
  DrawLayerCount=1` for a `2d_array` 6-layer image with `targetLayer=3`.

Fixing (1) alone (verified independently mid-investigation) turned every
`render_to_default_layer` case (8 of the 24) from "layers 1..N render as
random garbage" to a full pass, since that test type never writes
`gl_Layer` at all and only depends on the clear being complete. Fixing
(2) is what unblocks the remaining 16 `render_to_one`/
`multiple_layers_per_invocation` cases, which do write a non-zero
`gl_Layer` from the geometry stage.

**Unit test.** `DrawTest.
GeometryStageLayerOutputRoutesToANonMultiviewLayer`: a real (not just
pipeline-creation-only, unlike `GraphicsPipelineTest.cpp`'s
`GeometrySource`/`EmptyGeometrySource`) `EmitVertex`/`EndPrimitive`-driven
geometry stage writes a constant `gl_Layer = 1` and emits one
oversized-triangle-strip primitive, over a plain (`viewMask == 0`,
non-multiview) two-layer render pass. Confirms both fixes at once: layer
0 (never the geometry stage's target) must read back as
`LOAD_OP_CLEAR`'s own solid black, not garbage (fix 1); layer 1 (the
`gl_Layer` target) must read back the fragment stage's solid red, not
have been silently discarded (fix 2). Verified this test fails without
either fix present (reverting just the `CommandBuffer.cpp` changes
reproduces exactly the predicted garbage-bytes failure in layer 1).

`ninja check-feme` (assertions-enabled, ccache build) passes in full,
**1872/1931** (59 pre-existing, unrelated `Unsupported`, 0 `Failed`), up
from H5e-d's own **1871/1930** by exactly the 1 new unit test this row
adds.

**`dEQP-VK.geometry.layered.*` re-run (100 cases), before/after:**

```
Before (H5e-c's own baseline):
  Passed:         0/100 (0.0%)
  Failed:        78/100 (78.0%)
  Not supported: 22/100 (22.0%)

After fix (1) only (intermediate; not committed on its own):
  Passed:         8/100 (8.0%)
  Failed:        70/100 (70.0%)
  Not supported: 22/100 (22.0%)

After both fixes (this row):
  Passed:        24/100 (24.0%)
  Failed:        54/100 (54.0%)
  Not supported: 22/100 (22.0%)
```

All 24 of this row's own target cases (`render_to_one`,
`render_to_default_layer`, `multiple_layers_per_invocation` x
`1d_array`/`2d_array`/`cube`/`cube_array` x 2 sizes) now pass. The
remaining 54 `Fail`/22 `NotSupported` are entirely `layered.3d.*`'s
pre-existing, unrelated `vkCreateImage` gap and the other,
already-separately-tracked test types (`render_to_all`, `fragment_layer`,
`invocation_per_layer`, `readback`, `secondary_cmd_buffer*`) -- none of
which this row owns.

**`dEQP-VK.geometry.*` re-run (200 cases), before/after:**

```
Before (H5e-d's own baseline):
  Passed:        10/200 (5.0%)
  Failed:        157/200 (78.5%)
  Not supported: 33/200 (16.5%)

After (this row):
  Passed:        34/200 (17.0%)
  Failed:        133/200 (66.5%)
  Not supported: 33/200 (16.5%)
```

Exactly 24 new passes, matching this row's own target bucket precisely.
`NotSupported` stays byte-identical (33/200), confirming 0 regressions.

**Regression sample.** `dEQP-VK.draw.*`'s 1957-case `draw_sample.txt`
sample, same file every prior row's own report used:

```
Before (H5e-d's own baseline):
  Passed:        12/1957 (0.6%)
  Failed:        155/1957 (7.9%)
  Not supported: 1790/1957 (91.5%)

After (this row):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

2 new passes, 0 regressions: `dEQP-VK.draw.*shader_layer.vertex_shader_5`
(the `renderpass` and `partial_secondary_cmd_buff` variants sampled),
which route a *vertex*-shader-written `gl_Layer` into a non-multiview
layered render target -- confirming fix (2) is not specific to a
geometry stage at all, but to any pre-rasterization stage's `gl_Layer`
output. The sample's own `shader_layer.tessellation_shader_*` cases
remain `Fail` at `vkCreateGraphicsPipelines`, an unrelated,
pre-existing tessellation-pipeline gap this row does not touch.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: this is a pure rendering-correctness bug fix inside
`CommandBuffer.cpp`'s render-target-binding/draw-recording path, touching
no feature bit or extension (layered rendering itself was already
advertised and already partly working, e.g. `render_to_default_layer`'s
clear-only path and true multiview both already had working, if
incomplete, support before this row).

## Roadmap H6a: measured impact (SPIR-V mesh entry-point execution-mode reflection)

**Still 0/0/28044, and that is the correct, expected result** -- the same
shape H5a's own report entry recorded for geometry (and H4a's for
tessellation). H6a adds `feme::graphics::MeshState`/`Mesh.h` (mirroring
`GeometryState`/`Geometry.h`) and teaches
`ConvertSPIRVToLLVMPass::collectEntryPoints` to capture a mesh entry
point's output topology (`OutputPoints`/`OutputLinesEXT`/
`OutputTrianglesEXT`), maximum emitted vertex count (`OutputVertices`) and
maximum emitted primitive count (`OutputPrimitivesEXT`) into `feme.mesh.*`
passthrough attributes, disambiguating the two SPIR-V enumerant values mesh
shares with geometry/tessellation (`OutputPoints`, also a geometry entry's
own point-output mode; `OutputVertices`, also a hull entry's output control
point count and a geometry entry's own maximum emitted vertex count) by the
declaring entry point's own `Stage`, the same way H5a already disambiguates
`Triangles`/`OutputVertices` between tessellation and geometry. A task
entry point's workgroup size is captured the same way a compute entry
point's already is (`hlsl.numthreads`); it gets no `feme.mesh.*` attributes
of its own, since it declares no output shape.

This row does not lift `CanonicalizeStagePass::run`'s stage filter to
include `ShaderStage::Mesh`/`ShaderStage::Amplification`, does not touch
`vkCreateGraphicsPipelines`/`PhysicalDeviceInfo.cpp`, and does not
advertise `VK_EXT_mesh_shader` -- see the new roadmap rows H6b onward for
that remaining work. `dEQP-VK.mesh_shader.*` (both the `khr`/`VK_EXT_mesh_shader`
and legacy `nv`/`VK_NV_mesh_shader` sub-groups) is entirely unaffected:

```
Test run totals:
  Passed:        0/28044 (0.0%)
  Failed:        0/28044 (0.0%)
  Not supported: 28044/28044 (100.0%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no change:
`VK_EXT_mesh_shader` stays absent, and this row advertises nothing new.

**Regression sample.** This row's only new code paths are
`feme::graphics::getMeshState`/`Mesh.cpp` (a new file nothing else calls
yet) and `ConvertSPIRVToLLVMPass`'s new `EntryPointInfo` fields/execution-
mode cases (populated only for a `Mesh`-stage entry point, or read only
when disambiguating `OutputPoints`/`OutputVertices` by `Stage` -- both
branches confirmed by the new lit test to leave a non-mesh entry's own
attributes unchanged, including a task entry point's, which gets no
`feme.mesh.*` attribute at all). `dEQP-VK.draw.*`'s 1957-case
`draw_sample.txt` sample, same file every prior row's own report used:

```
Before (H5e-e's own baseline):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)

After (this row):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical to H5e-e's own recorded totals. **0 regressions, 0 new
passes.**

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1879/1938** (59 pre-existing, unrelated `Unsupported`, 0 `Failed`),
up from H5e-e's own **1872/1931** baseline by exactly the 7 new tests this
row adds -- `MeshTest.cpp`'s 6 (new file, `getMeshState` round-trip
coverage, mirroring `GeometryTest.cpp`) and one new
`spirv-to-llvm-mesh-execution-modes.mlir` lit test (4 `RUN`/`CHECK` blocks
in one `lit` test, including the `OutputPoints`/`OutputVertices`
disambiguation case and the task-entry no-attributes case).

**Reproducing.**

```
mkdir run && cd run
ln -sfn /home/dev/dev/VK-GL-CTS/external/vulkancts/data/vulkan vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.mesh_shader.*" --deqp-log-filename=mesh.qpa
```

and, for the draw regression sample, the same invocation H4a's own report
entry documents.

## Roadmap H6: what H6b found, and why it stops here

Investigating H6b's full stated scope (lift `CanonicalizeStagePass::run`'s
stage filter to accept `ShaderStage::Mesh`/`ShaderStage::Amplification`,
plus canonicalize both a mesh entry's bounded per-vertex/per-primitive
output-array writes *and* a task entry's bounded payload write) before
writing any code, mirroring H5a's own investigation, found this milestone
is not a mechanical repeat of H5b/H5c's own geometry precedent -- it hits
**two** real blockers, one of them a genuine regression trap rather than a
missing-producer gap like H5b's:

1. **A mesh entry's `Output`-array write is the store-side mirror of
   H5b's `Input`-array read, but `Output` storage already has a real,
   legitimate constant-index use that `Input` does not.**
   `getDynamicVertexIndexedAccess`/`isPerVertexArrayInputGlobal` (the
   machinery H5b built and H5f extended) is `Input`-only (address space
   7): a mesh entry's own `gl_MeshVerticesEXT[]`/`gl_MeshPrimitivesEXT[]`
   writes to an `Output`-storage-class array (address space 8) instead,
   and hit exactly `getDynamicVertexIndexedAccess`'s own "not recognized"
   fallback the same way `gl_in[i]` did before H5b. The tempting fix --
   simply widening `isPerVertexArrayInputGlobal`/H5f's constant-index-fold
   path to accept address space 8 too, mirroring H5b/H5f verbatim -- was
   tried first, and it **regressed a pre-existing, real test**:
   `RewritesSPIRVArrayOutputStorePerElementByteOffset`. Unlike `Input`
   (whose own matrix loads, `RewritesSPIRVMatrixInputLoadOneRowAtATime`,
   always load the *whole* array in one instruction and let
   `loadStageIOValue`'s own recursion decompose it), `Output` storage
   already has legitimate, tested, production **constant**-per-row
   `getelementptr` access patterns for an ordinary matrix output written
   one row at a time. Folding a constant `Output`-array index into
   `Vertex` (as H5f safely does for `Input`) silently misroutes that real
   matrix row index away from `Row`, corrupting genuine non-mesh output
   canonicalization. The fix landed instead: `isPerVertexArrayInputGlobal`
   (and the things that key off it -- `RowCountIsVertexArray`, the
   constant-offset-fold path in `resolveStageIOAccess`) stay strictly
   `Input`-only, and a new, narrower `isDynamicIndexedArrayGlobal` helper
   (address space 7 *or* 8) is introduced and used *only* by
   `getDynamicVertexIndexedAccess`'s genuinely-non-constant-index
   recognition -- since a real shader's own constant array/matrix index is
   essentially always compile-time-folded already, a non-constant index is
   safe to treat generically as a per-vertex/per-primitive write regardless
   of storage class, without touching the constant-index path at all.
   Landing this also surfaced a second, independent latent bug: the
   store-rewrite loop in `canonicalizeSPIRVStage` had never threaded
   `StageIOAccess::Vertex` through to `storeStageIOBlockValue` at all --
   it always passed a constant `Zero`, unlike the load path's own
   `Access->Vertex ? Access->Vertex : Zero` -- so store-side dynamic
   vertex/primitive indexing was dead code until this row's own new tests
   exercised it for the first time and caught it.

2. **A task entry's payload write cannot be canonicalized at all today,
   because it cannot even be *imported* as an LLVM global.**
   `TaskPayloadWorkgroupEXT` (SPIR-V storage class enum 5402) has **no
   address-space mapping whatsoever** in LLVM's own SPIR-V backend
   (`storageClassToAddressSpace` in `llvm/lib/Target/SPIRV/SPIRVUtils.h`
   hits its `default: report_fatal_error(...)` case) -- unlike
   `Input`(7)/`Output`(8)/`Workgroup`(3)/`PushConstant`(13), which FeMe's
   own `StageIOGlobalVariablePattern`/`WorkgroupGlobalVariablePattern`/
   `PushConstantGlobalVariablePattern`
   (`feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`) already
   reuse straight from that fixed mapping. This is a genuine upstream gap,
   not something any of FeMe's own passes introduced, and it sits at the
   SPIRVToLLVM conversion layer entirely outside `CanonicalizeStage.cpp`'s
   file scope: before `canonicalizeSPIRVStage` has anything to
   canonicalize a payload write *into*, a task entry's payload variable
   needs its own address-space convention and a new
   `TaskPayloadGlobalVariablePattern` (mirroring the two precedents named
   above) so it can be imported as an LLVM global at all.

Per this codebase's stated preference for a real, tested fix over a
mechanical port that happens to also (silently) regress something else,
H6b lands only the safely-generalizable half of its stated scope: the
mesh `Output`-array dynamic-index machinery (finding 1, above, entirely
within `CanonicalizeStage.cpp`/`ValidateStage.cpp`). It does **not** touch
task payload import (finding 2, broken out as roadmap H6h, a
SPIRVToLLVM-layer prerequisite), and it does **not** lift
`CanonicalizeStagePass::run`'s stage filter to accept
`ShaderStage::Mesh`/`ShaderStage::Amplification` (broken out as roadmap
H6i, which depends on both this row and H6h) -- mirroring how H5b built
geometry's own per-vertex machinery before H5c actually flipped the
filter, rather than flipping it early and risking the same kind of silent
wrong-output class of bug H5's own investigation (above) warned against.

## Roadmap H6b: measured impact (mesh `Output`-array dynamic vertex/primitive indexing)

**Still 0/0/28044 on `dEQP-VK.mesh_shader.*`, and that is the correct,
expected result** -- `CanonicalizeStagePass::run`'s stage filter is
deliberately *not* lifted by this row (see above), so no mesh or task
entry is routed through `canonicalizeSPIRVStage` yet; this row's new
machinery is exercised only by its own new unit tests (which, mirroring
`ThreadsDynamicVertexIndexIntoInputLoad`'s own H5b precedent, deliberately
tag their test module `"feme.shader.stage"="vertex"` rather than
`"mesh"`, since Mesh is not in the real filter to route through yet
either):

```
Test run totals:
  Passed:        0/28044 (0.0%)
  Failed:        0/28044 (0.0%)
  Not supported: 28044/28044 (100.0%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no
change: `VK_EXT_mesh_shader` stays absent, and this row advertises
nothing new.

**Regression sample.** This row's only new/changed code paths are gated
behind `getDynamicVertexIndexedAccess` recognizing a genuinely
non-constant index into an `Output`-storage-class array (address space
8) -- a shape that, per finding 1 above, real non-mesh `Output` writes
never take (they use a constant per-row index, handled by an entirely
separate, untouched path) -- plus the store-rewrite loop's now-correct
`Vertex` threading, which only changes behavior when `Access->Vertex` is
non-null (previously impossible for any stage this codebase's own
`canonicalizeSPIRVStage` callers reach). `dEQP-VK.draw.*`'s 1957-case
`draw_sample.txt` sample, same file every prior row's own report used:

```
Before (H6a's own baseline):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)

After (this row):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical to H6a's own recorded totals. **0 regressions, 0 new
passes.**

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1881/1940** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H6a's own **1879/1938** baseline by exactly the 2 net
new tests this row adds to `CanonicalizeStageTest.cpp`
(`ThreadsDynamicVertexIndexIntoOutputStore`,
`ThreadsDynamicVertexIndexIntoInterfaceBlockArrayMemberStore` -- a third
candidate test, asserting a constant `Output`-array index also folds into
`Vertex`, was written, found to encode finding 1's own now-rejected
design, and deleted rather than landed).

**Reproducing.**

```
cd /home/dev/dev/VK-GL-CTS/run
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.mesh_shader.*" --deqp-log-filename=mesh_h6b.qpa
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-caselist-file=draw_sample.txt --deqp-log-filename=draw_h6b.qpa
```

## Roadmap H6c: measured impact (reuse compute group/groupshared/barrier lowering for mesh/task)

**Still 0/0/28044 on `dEQP-VK.mesh_shader.*`, and that is the correct,
expected result** -- this row adds no SPIR-V import path, no
`CanonicalizeStagePass`/`ValidateStagePass` filter change, and no
`vkCreateGraphicsPipelines`/`PhysicalDeviceInfo.cpp` change; nothing new is
reachable from a real Vulkan mesh/task pipeline yet. This row only adds
CPU-target-side infrastructure -- `feme::graphics::MeshOutputBuilder`/
`TaskPayloadBuilder`, `FemeMeshArgs`/`FemeTaskArgs`, `MeshResources`/
`TaskResources`/`PreparedMeshBatch`/`PreparedTaskBatch`,
`CompiledStage::invokeMesh`/`invokeTask`, and `Pipeline.cpp`'s routing of
`ShaderStage::Mesh`/`Amplification` through the existing, unmodified
`feme::cpu::EntryWrapperPass` -- exercised only by this row's own new unit
tests, which hand-build mesh/task-tagged IR directly (bypassing SPIR-V
import and canonicalization entirely, the same established strategy
`GeometryWrapperTest.cpp`/`CompiledStageTest.cpp`'s existing geometry cases
already use):

```
Test run totals:
  Passed:        0/28044 (0.0%)
  Failed:        0/28044 (0.0%)
  Not supported: 28044/28044 (100.0%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no change:
`VK_EXT_mesh_shader` stays absent, and this row advertises nothing new.

**Regression sample.** This row's only new/changed non-additive code path
is `Pipeline.cpp`'s stage-wrapper-selection switch, which previously had no
`case` for `ShaderStage::Mesh`/`Amplification` at all (any module tagged
with those stages would have hit the switch's default/unhandled path);
adding cases that call the same `EntryWrapperPass()` compute already uses
cannot change behavior for any `ShaderStage::Compute`/`Vertex`/`Fragment`/
etc. module, since those stages' own `case`s are untouched.
`dEQP-VK.draw.*`'s 1957-case `draw_sample.txt` sample, same file every
prior row's own report used:

```
Before (H6b's own baseline):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)

After (this row):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical to H6b's own recorded totals. **0 regressions, 0 new
passes.**

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1897/1956** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H6b's own **1881/1940** baseline by exactly the 16 new
tests this row adds: `MeshOutputTest.cpp` (7, new file), `TaskPayloadTest.cpp`
(5, new file), and 4 new `CompiledStageTest.cpp` cases
(`InvokeMeshReusesComputeGroupSharedAndBarrierLowering`,
`InvokeMeshRejectsANonMeshStage`,
`InvokeTaskReusesComputeGroupSharedAndBarrierLowering`,
`InvokeTaskRejectsANonTaskStage`).

**Reproducing.**

```
cd /home/dev/dev/VK-GL-CTS/run  # or any directory with a `vulkan` symlink
                                # to external/vulkancts/data/vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.mesh_shader.*" --deqp-log-filename=mesh_h6c.qpa
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-caselist-file=draw_sample.txt --deqp-log-filename=draw_h6c.qpa
```

## Roadmap H6c-a: why this row could not land

Investigating H6c-a's stated scope before writing any code (mirroring
every prior row's own "investigate first" discipline) found it is not
merely *incomplete* the way H6b was -- it has **no independently-landable
content at all** yet. `feme::graphics::MeshOutputBuilder`/
`TaskPayloadBuilder` (H6c) are bounded, tested, in-memory builders with
no producer wired to them; H6c-a's own ask is to wire that producer in,
but the producer does not exist:

1. `feme/include/feme/Core/StageOps.h`'s `StageOpKind` enum (the
   complete, closed vocabulary of every `feme.stage.*` operation this
   codebase can canonicalize a SPIR-V/DXIL entry into) has no
   mesh-output-store or task-payload-store enumerator at all -- confirmed
   by reading the enum in full, not merely grepping for a name that might
   not match. The nearest existing op, `OutputStore`, already covers
   mesh's per-vertex/per-primitive `Output`-array writes (H6b's own
   `Vertex`-operand generalization), so no new op is actually needed for
   *mesh* output specifically; a task entry's payload write has no op of
   any kind yet, new or old.
2. `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` has no
   `TaskPayloadGlobalVariablePattern` and no address-space convention for
   `TaskPayloadWorkgroupEXT` (confirmed absent, matching H6b's own
   finding 2) -- a task entry's payload variable still cannot be imported
   as an LLVM global at all, so there is nothing for any canonicalization
   pass to read a payload write's operands from.
3. `CanonicalizeStagePass::run`'s stage filter (`CanonicalizeStage.cpp`)
   still does not accept `ShaderStage::Mesh`/`Amplification` (confirmed
   by reading the filter directly) -- even mesh output's own
   already-existing `OutputStore` generalization (H6b) is never reached
   for a real mesh entry today, since nothing routes a mesh module
   through `canonicalizeSPIRVStage` at all yet.

All three are exactly the prerequisites H6c-a's own dependency list
already named (H6d, H6h, H6i); this investigation's value is confirming
none of the three has landed since H6c wrote that list, rather than
assuming so and mechanically attempting a wiring that has nothing to
attach to. Unlike H6b, which found a real, independently-shippable fix
inside a superficially-blocked scope, this row's stated scope truly has
none: every code path `MeshOutputBuilder`/`TaskPayloadBuilder` would need
to be wired into is either entirely unwritten (task payload's op and
import pattern) or unreachable from any real entry point (mesh output's
`OutputStore` generalization, gated behind the same stage filter).

**A useful narrower finding, though: mesh output and task payload do not
share the same blocker set.** Mesh output's canonicalization already
exists (H6b); it is blocked only on H6d (so a mesh workgroup's emitted
outputs become a consumable meshlet) and H6i (so a mesh entry is
canonicalized at all) -- not on H6h, which is entirely about
`TaskPayloadWorkgroupEXT` import and has no bearing on mesh output.
Task payload needs all three, since H6i's own payload-canonicalization
half cannot exist before H6h's import prerequisite does. H6c-a is split
into H6c-a-a (mesh output, depends on H6d/H6i) and H6c-a-b (task payload,
depends on H6d/H6h/H6i) in the Roadmap to track this distinction, rather
than leaving one deceptively-monolithic row whose two halves actually
unblock at different times.

No code changes result from this investigation (there is nothing safe or
correct to land yet, per the analysis above); `ninja check-feme`
(`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) was re-run to confirm the
tree is still at H6c's own recorded baseline, unaffected by a
documentation-only change: **1897/1956** (59 pre-existing, unrelated
`Unsupported`, 0 `Failed`), byte-identical to H6c's own totals. Since no
product code changed, `dEQP-VK.mesh_shader.*` (still 0/0/28044) and the
`dEQP-VK.draw.*` 1957-case regression sample are guaranteed unaffected
and were not re-run; `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` need no change (`VK_EXT_mesh_shader` stays
absent, nothing new is advertised).

## Roadmap H6d: measured impact (checked amplification dispatch queues and meshlet assembly)

**Still 0/0/28044 on `dEQP-VK.mesh_shader.*`, and that is the correct,
expected result** -- this row adds two new `feme::graphics` classes,
`AmplificationDispatchQueue` (`AmplificationDispatch.h`/`.cpp`) and
`Meshlet`/`assembleMeshlet` (`Meshlet.h`/`.cpp`), and nothing else: no
SPIR-V import path change, no `CanonicalizeStagePass`/`ValidateStagePass`
filter change, no `Executor::executeDraws` chaining (left to H6e), and no
`vkCreateGraphicsPipelines`/`PhysicalDeviceInfo.cpp` change. Nothing new is
reachable from a real Vulkan mesh/task pipeline yet; both new classes are
exercised only by their own new unit tests, which hand-build
`MeshOutputBuilder`/group-count inputs directly, the same established
strategy H6c's own `CompiledStageTest.cpp` cases already use for mesh/task
IR.

```
Test run totals:
  Passed:        0/28044 (0.0%)
  Failed:        0/28044 (0.0%)
  Not supported: 28044/28044 (100.0%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no
change: `VK_EXT_mesh_shader` stays absent, and this row advertises
nothing new (`AmplificationDispatchLimits`'s fields are plain constructor
parameters today, not read from any `PhysicalDeviceInfo`-advertised
limit -- that wiring is H6f's own "advertise only what the
implementation actually enforces" job, once a real Vulkan mesh pipeline
exists to advertise limits for).

**Regression sample.** This row touches no existing file except two
`CMakeLists.txt` additions (new source/test file names); no existing
class, function, or behavior changed. `dEQP-VK.draw.*`'s 1957-case
`draw_sample.txt` sample, same file every prior row's own report used:

```
Before (H6c's own baseline):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)

After (this row):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical to H6c's own recorded totals. **0 regressions, 0 new
passes.**

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1906/1965** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H6c's own **1897/1956** baseline by exactly the 9 new
tests this row adds: `AmplificationDispatchTest.cpp` (6, new file --
`AcceptsAGroupCountWithinBothBounds`,
`RejectsAPerDimensionCountBeyondTheLimit`,
`RejectsATotalCountBeyondTheLimitEvenIfEachDimensionFits`,
`ComputesTheProductWithoutA32BitOverflow`,
`EnumeratesGroupIDsXFastestZSlowest`,
`ZeroGroupCountIsAValidEmptyDispatch`) and `MeshletTest.cpp` (3, new file
-- `AssemblesOnlyTheDeclaredActualCounts`,
`EmptyOutputAssemblesToAnEmptyMeshlet`,
`DiagnosesAnOutOfRangeVertexIndexRatherThanReadingOOB`).

**Reproducing.**

```
cd /home/dev/dev/VK-GL-CTS/run  # or any directory with a `vulkan` symlink
                                # to external/vulkancts/data/vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.mesh_shader.*" --deqp-log-filename=mesh_h6d.qpa
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-caselist-file=draw_sample.txt --deqp-log-filename=draw_h6d.qpa
```

## Roadmap H6e: measured impact (chained the mesh path into `Executor::executeDraws`)

**Still 0/0/28044 on `dEQP-VK.mesh_shader.*`, and that is the correct,
expected result** -- this row chains `Executor::executeDraws` itself
(`GraphicsPipeline::setMeshStage`/`hasMeshStages` in `Pipeline.h`/`.cpp`,
`MeshDrawCommand`/`PreparedDraw::MeshDraws` in `PreparedDraw.h`, and a new
mesh/task dispatch-and-rasterize branch in `Executor.cpp` that reuses
H6d's own `AmplificationDispatchQueue`/`Meshlet`/`assembleMeshlet` and
feeds the assembled meshlets into the very same `RasterizePrimitives`
tail vertex/geometry primitives already share, mirroring H5d's own
geometry-chaining precedent) -- but nothing new is reachable from a real
Vulkan mesh/task pipeline yet: no SPIR-V import path change, no
`vkCreateGraphicsPipelines`/`PhysicalDeviceInfo.cpp` change, and no
`VK_EXT_mesh_shader` advertisement. The new branch is exercised only by
its own new `PipelineTest.cpp`/`ExecutorTest.cpp` unit tests, which
directly hand-build `GraphicsPipeline`s with compiled mesh/task IR and
call `executeDraws`, the same established strategy every prior mesh-path
row (H6a-H6d) already uses.

```
Test run totals:
  Passed:        0/28044 (0.0%)
  Failed:        0/28044 (0.0%)
  Not supported: 28044/28044 (100.0%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no
change: `VK_EXT_mesh_shader` stays absent, and this row advertises
nothing new. This row's own `AmplificationDispatchLimits` value is still
a permissive hardcoded placeholder (`MaxGroupCount={65535,65535,65535}`,
`MaxTotalGroupCount=4194304`), documented in `Executor.cpp` as pending
H6f's own "advertise only what the implementation actually enforces"
job. Likewise, every meshlet this row's chain assembles is legitimately
empty at runtime: no compiled mesh/task entry point can yet write
`FemeMeshArgs::ActualVertexCount`/`VertexOutputs`/`FemeTaskArgs::MeshGroupCount`
from real IR (blocked on H6h's `TaskPayloadWorkgroupEXT` lowering and
H6i's `CanonicalizeStagePass` stage-filter lift for `Mesh`), so this row's
new `ExecutorTest.cpp` cases instead prove dispatch/`GroupID` correctness
end-to-end (a bound UAV buffer written via groupshared+barrier,
mirroring `CompiledStageTest.cpp`'s own precedent) while asserting the
color attachment stays untouched -- the same "correctly wired but
produces nothing yet" shape as this row's own
`ExecutesDrawsAsNoOpWhenGeometryStageNeverEmits` (H5d) precedent.

**Regression sample.** This row touches no Vulkan-facing file at all
(`Executor.cpp`/`Pipeline.h`/`Pipeline.cpp`/`PreparedDraw.h` are all
`feme::graphics`-internal, upstream of any real `vkCreateGraphicsPipelines`/
`vkCmdDraw*` entry point); no existing class, function, or behavior
observable from Vulkan changed. `dEQP-VK.draw.*`'s 1957-case
`draw_sample.txt` sample, same file every prior row's own report used:

```
Before (H6d's own baseline):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)

After (this row):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical to H6d's own recorded totals. **0 regressions, 0 new
passes.**

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1910/1969** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H6d's own **1906/1965** baseline by exactly the 4 new
tests this row adds: `PipelineTest.cpp`
(`SetMeshStageRecordsTheMeshAndTaskStagesAndState`,
`SetMeshStageAllowsAnOmittedTaskStage`) and `ExecutorTest.cpp`
(`RunsEveryMeshWorkgroupDirectlyWhenNoTaskStageIsBoundAndRastersNothing`,
`TaskStageDispatchDrivesWhichMeshWorkgroupsRunAndNoneRunUntilItRequestsAny`).

**Reproducing.**

```
cd /home/dev/dev/VK-GL-CTS/run  # or any directory with a `vulkan` symlink
                                # to external/vulkancts/data/vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.mesh_shader.*" --deqp-log-filename=mesh_h6e.qpa
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-caselist-file=draw_sample.txt --deqp-log-filename=draw_h6e.qpa
```

## Roadmap H6f: measured impact (mesh pipeline creation, `vkCmdDrawMeshTasks*` routing, and `VK_EXT_mesh_shader` advertisement)

This row closes out H6f: `vkCreateGraphicsPipelines` now accepts a mesh
pipeline (task stage optional, mesh stage required, no
vertex-input/input-assembly state -- `GraphicsPipeline.cpp`'s
`translateFixedFunctionState`/`compileAndValidateStages`),
`vkCmdDrawMeshTasksEXT`/`vkCmdDrawMeshTasksIndirectEXT`/
`vkCmdDrawMeshTasksIndirectCountEXT` are implemented and route through the
same `runPreparedDraw` helper `vkCmdDraw*` already shares
(`CommandBuffer.cpp`, refactored out of the pre-existing `runDraw`), and
`PhysicalDeviceInfo.cpp`/`EntryPoints.cpp` advertise `VK_EXT_mesh_shader`,
`taskShader`/`meshShader` = `VK_TRUE`, and every
`VkPhysicalDeviceMeshShaderPropertiesEXT` field at this implementation's
own honest, bounded ceiling.

**A real bug found via CTS, not just unit tests: `maxMeshWorkGroupSize`/
`maxTaskWorkGroupSize`/`maxMeshOutputComponents`/`maxMeshOutputMemorySize`/
`maxMeshPayloadAndOutputMemorySize`/`maxMeshOutputLayers` were initially
set below `VK_EXT_mesh_shader`'s own specification-mandated minimums.**
The first properties revision mirrored `maxComputeWorkGroupSize`/
`Invocations` verbatim (dimension 2 = 64, below the mesh/task spec floor
of 128) and used placeholder values for the output-related fields (64,
8192, 8192, 1) below their own spec floors (128, 32768, 48128, 8). Running
`dEQP-VK.mesh_shader.*` against that revision surfaced 80 "maxMeshOutput
Components too low to run this test" failures and 1 "Some properties
failed the limits test" failure (`vktMeshShaderPropertyTestsEXT.cpp`'s
`CHECK_LIMITS_MIN` checks). Investigating whether raising these values
would be dishonest (i.e., whether some fixed-size internal buffer
actually caps them lower) found the opposite: no code path validated a
mesh/task entry's declared group size against anything at all (a real
enforcement gap, not merely a low-but-honest ceiling), and
`feme::graphics::MeshOutputBuilder`'s per-row storage
(`Graphics/MeshOutput.h`) is a dynamically-sized `std::vector<float>` with
no fixed component/memory ceiling either. The fix was therefore twofold:
add genuine pipeline-creation-time enforcement of the work-group-size
pair (`validateMeshOrTaskGroupSize` in `GraphicsPipeline.cpp`, reusing
`GroupSize.h`'s `resolveComputeGroupSize` extended to accept the
`MeshEXT`/`TaskEXT` execution models alongside `GLCompute`, checked
against new `feme::vulkan::MaxMeshWorkGroupSize`/`MaxTaskWorkGroupSize`/
`*Invocations` constants in `GraphicsPipeline.h`), and raise every
now-genuinely-unbounded property to the specification's own mandatory
floor rather than an arbitrary smaller guess.

```
Before this fix:
  Passed:        0/28044 (0.0%)
  Failed:        338/28044 (1.2%)
  Not supported: 27706/28044 (98.8%)

After this fix:
  Passed:        1/28044 (0.0%)
  Failed:        337/28044 (1.2%)
  Not supported: 27706/28044 (98.8%)
```

The 80 previously-`maxMeshOutputComponents`-blocked cases now correctly
report `NotSupported` (`shaderFloat16 feature not supported`, an
unrelated, genuine feature gap) instead of failing on the property check,
and the one properties-limits meta-test now passes. The remaining 337
failures are unrelated to this row's own scope, unchanged in kind from
H6f's earlier (pre-fix) run: 235 `vkCreateGraphicsPipelines` ->
`VK_ERROR_INITIALIZATION_FAILED` (real mesh/task shader *content*
compilation -- expected, blocked on H6h's `TaskPayloadWorkgroupEXT`
lowering and H6i's `CanonicalizeStagePass` mesh-stage support; up from
155 by exactly the 80 cases the property fix newly lets reach real
pipeline creation), 68 `vkCreateRenderPass` ->
`VK_ERROR_FORMAT_NOT_SUPPORTED` (an unrelated render-pass format gap, out
of this row's scope), 33 `vkPipelineConstructionUtil.cpp` ->
`VK_ERROR_INITIALIZATION_FAILED` (graphics-pipeline-library variants of
the same content-compilation cases, same blocker), and 1
`vkGetPhysicalDeviceImageFormatProperties` ->
`VK_ERROR_FORMAT_NOT_SUPPORTED` (the same unrelated format gap). See
roadmap H6g for the follow-up row triaging/closing out these remaining
categories.

`VulkanExtensionInventory.md` moves `VK_EXT_mesh_shader` from "Planned" to
"Advertised" (31 -> 32 advertised, 49 -> 48 planned). `Vulkan14FeatureInventory.md`
needs no change: it tracks core 1.0-1.4 feature/limit/extension surface
only, not `VK_EXT_mesh_shader` (a non-core `EXT` extension).

**Regression sample.** `dEQP-VK.draw.*`'s 1957-case `draw_sample.txt`
sample, same file every prior row's own report used:

```
Before (H6e's own baseline):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)

After (this row):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical failing-case set to H6e's own recorded totals (diffed by
name, not just count). **0 regressions, 0 new passes.**

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1930/1989** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H6e's own **1910/1969** baseline by this row's new
`DrawTest.cpp` mesh-draw-command coverage
(`DrawMeshTasksRunsWithoutErrorAndProducesNoOutputYet`,
`DrawMeshTasksWithTaskStageRunsWithoutError`,
`DrawMeshTasksIndirectReadsBuffer`,
`RejectsOutOfBoundsIndirectMeshTasksDraw`,
`DrawMeshTasksIndirectCountClampsToCountBuffer`,
`DrawMeshTasksRejectsANonMeshPipeline`,
`DrawMeshTasksWithoutBoundPipelineFails`), this row's new
`GraphicsPipelineTest.cpp` work-group-size enforcement coverage
(`RejectsMeshWorkGroupSizeExceedingLimits`,
`RejectsTaskWorkGroupSizeExceedingLimits`), and this row's new
`GroupSizeTest.cpp` `MeshEXT`/`TaskEXT` acceptance coverage
(`ResolvesFromLocalSizeForMeshEntryPoint`,
`ResolvesFromLocalSizeForTaskEntryPoint`).

**Reproducing.**

```
cd /home/dev/dev/VK-GL-CTS/run  # or any directory with a `vulkan` symlink
                                # to external/vulkancts/data/vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-case="dEQP-VK.mesh_shader.*" --deqp-log-filename=mesh_h6f.qpa
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-caselist-file=draw_sample.txt --deqp-log-filename=draw_h6f.qpa
```

## Roadmap H6h: measured impact (`TaskPayloadWorkgroupEXT` address space and global-variable import)

This row closes out H6h: a new `TaskPayloadGlobalVariablePattern`
(`SPIRVToLLVMPatterns.cpp`), mirroring
`WorkgroupGlobalVariablePattern`/`PushConstantGlobalVariablePattern`,
converts a `TaskPayloadWorkgroupEXT`-storage-class `spirv.GlobalVariable`
(SPIR-V enum 5402 -- a task entry's bounded payload variable) to an
ordinary `llvm.mlir.global` in address space 14, a FeMe-only convention:
unlike `Workgroup`(3)/`Input`(7)/`Output`(8)/`StorageBuffer`(11)/
`Uniform`(12)/`PushConstant`(13), LLVM's own SPIR-V backend
(`storageClassToAddressSpace` in `llvm/lib/Target/SPIRV/SPIRVUtils.h`) has
no mapping at all for this storage class, so 14 -- the next value after
the highest one that switch does define, and otherwise unused anywhere in
this conversion layer -- is picked here rather than reused from it. A
matching `spirv::PointerType` conversion routes any access-chain result
reaching into the variable's contents to the same address space, so a
plain `spirv.AccessChain`/`spirv.Load`/`spirv.Store` through it converts
with MLIR's own generic patterns, exactly as a `Workgroup` variable's own
access already does -- no further FeMe-specific pattern is needed for
those. This is purely conversion-layer plumbing: a payload variable can
now be imported as an LLVM global at all, but nothing yet reads or writes
one through it (`CanonicalizeStagePass::run` still does not accept
`ShaderStage::Mesh`/`Amplification`, tracked separately as H6i), so no
`feme.stage.*` payload op exists for a canonicalization pass to produce
yet.

**Expected, and confirmed, zero behavioral change.** Since this row adds
an import path with no caller reachable from any entry point
`CanonicalizeStagePass`/the mesh/task pipeline actually processes today,
a real `dEQP-VK.mesh_shader.*` re-run should be -- and is -- byte-identical
to H6f's own recorded baseline:

```
Before this row (H6f's own baseline):
 Passed:        1/28044 (0.0%)
 Failed:        337/28044 (1.2%)
 Not supported: 27706/28044 (98.8%)

After this row:
 Passed:        1/28044 (0.0%)
 Failed:        337/28044 (1.2%)
 Not supported: 27706/28044 (98.8%)
```

0 regressions, 0 new passes -- exactly as expected, and as H6g's own text
already anticipated ("blocked on H6h's `TaskPayloadWorkgroupEXT` lowering
*and* H6i's `CanonicalizeStagePass` mesh-stage support"): landing H6h
alone does not yet let any of the 235+33 content-compilation failures
H6g tracks clear, since H6i's own canonicalization-filter change has not
landed yet.

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` need no
change: this row touches no feature bit, limit, or extension -- it is a
pure SPIR-V-to-LLVM conversion-layer addition, the same shape H5e-d's own
entry recorded needing no inventory update for an analogous
reflection/metadata-only fix.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1931/1990** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H6f's own **1930/1989** baseline by this row's new
`spirv-to-llvm-task-payload.mlir` lit test (2 `RUN`/`CHECK` blocks:
storing through a payload array element, and loading a scalar payload
variable directly, mirroring `spirv-to-llvm-workgroup.mlir`'s own
coverage shape for the precedent this row mirrors).

**Reproducing.**

```
feme-opt --feme-convert-spirv-to-llvm --split-input-file \
 feme/test/Conversion/SPIRVToLLVM/spirv-to-llvm-task-payload.mlir

cd /home/dev/dev/VK-GL-CTS/run  # or any directory with a `vulkan` symlink
                               # to external/vulkancts/data/vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
 /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
 --deqp-case="dEQP-VK.mesh_shader.*" --deqp-log-filename=mesh_h6h.qpa
```

## Roadmap H6i: measured impact (`CanonicalizeStagePass` accepts `ShaderStage::Mesh`/`Amplification`, task payload store canonicalization)

This row closes out H6i: `CanonicalizeStagePass::run`'s stage filter now
also accepts `ShaderStage::Mesh`/`ShaderStage::Amplification`, routing both
through the same `canonicalizeSPIRVStage` path Domain/Geometry already
use (no barrier-splitting needed, mirroring H5c's own reasoning for
Geometry). A mesh entry's own per-vertex/per-primitive `Output`-array
writes (H6b) were already implemented in `canonicalizeSPIRVStage` itself,
just unreachable through the full pass until this filter change -- a new
`MeshStageCanonicalizesOutputArrayStore` unit test re-runs
`ThreadsDynamicVertexIndexIntoOutputStore`'s own shape through a genuine
`"mesh"`-tagged entry point (rather than that test's own `"vertex"`
stand-in, needed at the time since the filter did not accept `Mesh` yet)
to confirm the whole pass, not just the function called directly, now
reaches it.

A task entry's bounded payload write is new: a new `TaskPayloadStore`
`feme.stage.*` op (`feme.stage.task.payload.store(offset, value)`,
StageOps.h/.cpp) canonicalizes an ordinary store through
`TaskPayloadGlobalVariablePattern`'s own address-space-14 global import
shape (roadmap H6h) by its resolved byte offset -- reusing
`getStageIOBaseAndOffset`'s existing constant-offset walk (already generic
over any address space, not gated on `isSPIRVStageIOGlobal`) rather than
adding a parallel one. Unlike a stage-IO store, this carries no
`SignatureElement`: the payload is raw, task-defined memory shared
verbatim with the mesh workgroups a task entry's `EmitMeshTasksEXT`
dispatches, not a piece of the vertex/fragment-style signature
`EntrySignature` describes, so `!feme.signature` stays absent for an
otherwise-payload-only task entry. `isStageOpLegalForStage`/`validateCall`
in `ValidateStage.cpp`, the uniformity switch in `WaveUniformity.cpp`, and
the widening switch in `SIMDize.cpp` all gained an explicit
`TaskPayloadStore` case to keep their exhaustive `switch`es over
`StageOpKind` covering the new enumerator (mirroring how `StreamEmit`/
`StreamCut` were recorded as "not yet reachable" cases before geometry
validation landed) -- none of the three is actually reachable for this op
yet (`ValidateStagePass::run` does not validate Amplification, and neither
`WaveUniformity`/`SIMDize` widening path is exercised until H6c-a-b wires
a real caller through `EntryWrapperPass`).

Two new `CanonicalizeStageTest.cpp` cases cover this row directly:
`MeshStageCanonicalizesOutputArrayStore` (above) and
`AmplificationStageCanonicalizesTaskPayloadStore` (a two-field payload
struct store, one constant-`i32`-valued field at offset 0 and one
`float`-argument-valued field at offset 4, confirming both the resolved
offsets and that no `!feme.signature` gets attached). A new
`StageOpsTest.cpp` case, `TaskPayloadStoreIsVoidAndOverloadedOnValue`,
covers the new builder/table entry directly, mirroring
`OutputStoreIsVoidAndOverloadedOnValue`.

**Expected, and confirmed, zero behavioral change against H6f/H6h's own
recorded totals.** `feme.stage.task.payload.store` has no caller reachable
from `EntryWrapperPass`/`CompiledStage::invokeTask` yet -- wiring a real
`TaskPayloadBuilder` into it is H6c-a-b, itself still blocked on H6d's own
checked dispatch queue giving the payload somewhere real to be read back
from -- so a real `dEQP-VK.mesh_shader.*` re-run is, and is expected to be,
byte-identical to H6h's own baseline:

```
Before this row (H6h's own baseline):
 Passed:        1/28044 (0.0%)
 Failed:        337/28044 (1.2%)
 Not supported: 27706/28044 (98.8%)

After this row:
 Passed:        1/28044 (0.0%)
 Failed:        337/28044 (1.2%)
 Not supported: 27706/28044 (98.8%)
```

0 regressions, 0 new passes -- exactly as H6h's own text already
anticipated ("landing H6h alone does not yet let any of the 235+33
content-compilation failures H6g tracks clear, since H6i's own
canonicalization-filter change has not landed yet"): this row lands that
filter change, but H6c-a-a/H6c-a-b's own wiring into `EntryWrapperPass`
still has to land before any of those failures can actually clear.

`Vulkan14FeatureInventory.md` and `VulkanExtensionInventory.md` need no
change: this row touches no feature bit, limit, or extension -- it is a
pure IR-canonicalization addition, the same shape H6b/H6h's own entries
recorded needing no inventory update for.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1934/1993** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H6h's own **1931/1990** baseline by this row's three new
unit tests (`TaskPayloadStoreIsVoidAndOverloadedOnValue`,
`MeshStageCanonicalizesOutputArrayStore`,
`AmplificationStageCanonicalizesTaskPayloadStore`).

**Reproducing.**

```
cd /home/dev/dev/VK-GL-CTS/run  # or any directory with a `vulkan` symlink
                               # to external/vulkancts/data/vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
 /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
 --deqp-case="dEQP-VK.mesh_shader.*" --deqp-log-filename=mesh_h6i.qpa
```

## Roadmap H6g: measured impact (`dEQP-VK.mesh_shader.*` post-H6h/H6i triage)

This row closes out H6g's own triage scope. H6g's original text assumed
that once H6h (`TaskPayloadWorkgroupEXT` address-space/import) and H6i
(`CanonicalizeStagePass` mesh/amplification acceptance) both landed, a
re-run would let the 235 `vkCreateGraphicsPipelines`/33
`vkPipelineConstructionUtil.cpp` -> `VK_ERROR_INITIALIZATION_FAILED`
content-compilation failures clear. Both prerequisites did land (H6h,
H6i), so this row re-ran the full `dEQP-VK.mesh_shader.*` group and the
`dEQP-VK.draw.*` 1957-case regression sample to check.

```
dEQP-VK.mesh_shader.* (28044 cases):
Before this row (H6h/H6i's own recorded baseline):
  Passed:        1/28044 (0.0%)
  Failed:        337/28044 (1.2%)
  Not supported: 27706/28044 (98.8%)

After this row:
  Passed:        1/28044 (0.0%)
  Failed:        337/28044 (1.2%)
  Not supported: 27706/28044 (98.8%)
```

Byte-identical, and the 337 failures split by API call and error code
into the exact same four buckets H6f's original run found: 235
`vkCreateGraphicsPipelines` -> `VK_ERROR_INITIALIZATION_FAILED`, 68
`vkCreateRenderPass` -> `VK_ERROR_FORMAT_NOT_SUPPORTED`, 33
`vkPipelineConstructionUtil.cpp` -> `VK_ERROR_INITIALIZATION_FAILED`, 1
`vkGetPhysicalDeviceImageFormatProperties` ->
`VK_ERROR_FORMAT_NOT_SUPPORTED` (235+68+33+1 = 337).

**This means H6g's own original text under-scoped the real blocker for
the content-compilation buckets.** H6h/H6i landing was necessary but not
sufficient: real mesh/task content still cannot compile end-to-end
because nothing wires `MeshOutputBuilder`/`TaskPayloadBuilder` into a
canonicalized entry's actual output/payload writes yet -- that wiring is
H6c-a-a (mesh output) and H6c-a-b (task payload), the two rows H6c-a's
own investigation split out *before* H6g was written, and H6g's
dependency list should have named them directly instead of only their
own already-satisfied prerequisites (H6d, H6h, H6i, all now done). H6i's
own text already flagged this precisely ("`feme.stage.task.payload.store`
has no caller reachable from `EntryWrapperPass` ... until H6c-a-b wires
one in"); this row's job is to confirm that measurement holds and correct
H6g's own roadmap text and dependency list accordingly, not to re-litigate
it.

**The format-related bucket (68+1) is independently decidable today,
regardless of H6c-a-a/H6c-a-b.** Every one of these 69 cases fails
because a render-pass attachment format or an image-format query names a
format/usage combination outside the mandatory table roadmap H8 ("Format
table completeness for the graphics profile") already exists to close --
nothing about *which* formats these particular mesh-shader cases exercise
is mesh-shading-specific. Decision: no new roadmap row. This bucket folds
into H8's own existing, still-open scope; H8's own entry now cross-
references this finding.

**Regression sample.** `dEQP-VK.draw.*`'s 1957-case `draw_sample.txt`
sample, same file every prior row's own report used:

```
Before (H6i's own baseline):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)

After (this row):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical failing-case set (diffed by name, not just count). **0
regressions, 0 new passes.**

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` need no
change: this row is pure CTS triage plus a roadmap/report update, with no
source change at all -- no feature bit, limit, or extension is touched.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1934/1993** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), unchanged from H6i's own baseline -- this row adds no new unit
tests, since there is no new source behavior to cover.

**Reproducing.**

```
cd /home/dev/dev/VK-GL-CTS/run  # or any directory with a `vulkan` symlink
                               # to external/vulkancts/data/vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
 /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
 --deqp-case="dEQP-VK.mesh_shader.*" --deqp-log-filename=mesh_h6g.qpa
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
 /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
 --deqp-caselist-file=draw_sample.txt --deqp-log-filename=draw_h6g.qpa
```

## Roadmap H6c-a-a: measured impact (wire `MeshOutputBuilder` into a mesh entry's canonicalized output store)

This row closes out H6c-a-a: a mesh entry's own canonicalized (H6b-shaped)
`feme.stage.output.store` -- a masked, per-lane store with a dynamic
`Vertex` operand, by the time `LinearizePass` and `CanonicalizeStagePass`
(H6i) are done with it -- now actually reaches `FemeMeshArgs::
VertexOutputs`/`PrimitiveOutputs`, the same buffers `feme::graphics::
Executor::runMeshWorkgroup`'s already-built `MeshOutputBuilder` consumer
(Executor.cpp, landed alongside H6e) reads from.

**New `feme::cpu::MeshOutputWrapperPass`** (`MeshOutputWrapper.h`/`.cpp`)
is a narrow, single-purpose companion pass that runs immediately before
`EntryWrapperPass` in `Pipeline.cpp`'s `ShaderStage::Mesh` case (split out
of the combined `Amplification`/`Mesh` case H6c originally wrote, since
amplification's own task-payload wiring is H6c-a-b's separate job, not
this row's). It appends six new named wave-body parameters --
`mesh_vertex_output_layout`, `mesh_vertex_outputs`,
`mesh_primitive_output_layout`, `mesh_primitive_outputs`,
`mesh_max_output_vertices`, `mesh_max_output_primitives` -- unconditionally
to every mesh entry's wave body, mirroring every other stage wrapper's own
"always append params, conditionally lower" convention (see
VertexWrapper.cpp), then lowers every masked output store it finds by
branching on the referenced `SignatureElement::Frequency`:
`SignatureFrequency::PerVertex` addresses `VertexOutputs`/
`VertexOutputLayout`, `PerPrimitive` addresses `PrimitiveOutputs`/
`PrimitiveOutputLayout` instead -- the only reason this needs its own pass
rather than reusing `VertexWrapperPass`'s single-array addressing
directly.

**Defensive clamping, since `SetMeshOutputsEXT` is still unwired.** A mesh
output store's dynamic `Vertex` operand is the compiled entry's own
runtime value; nothing yet validates it against the workgroup's *actual*
declared output count (`SetMeshOutputsEXT` has no canonicalized
`feme.stage.*` form yet -- explicitly out of this row's scope, matching
H6c-a's own text). Unlike a resource-heap index (which has a bounds-checked
lookup as a safety net, see `SPIRVResourceLowering.cpp`'s
`computeClampedIndex`), an out-of-range mesh output write would be a raw
OOB memory write with no such net, so `lowerMeshOutputStore` clamps the
slot index into `[0, Max)` via `llvm::Intrinsic::umin` against
`mesh_max_output_vertices`/`mesh_max_output_primitives` (guarding
`Max == 0` with a `select` first, to avoid `0 - 1` wrapping back to
`UINT32_MAX` and defeating the clamp). This bounds a real out-of-range
write to the declared *maximum*, not the declared *actual* count -- a
tighter bound is left to whichever future row wires `SetMeshOutputsEXT`
itself.

**`feme::cpu::EntryWrapperPass` extended, not replaced.** `WrapperEnv`
gained six new (default-null) fields mirroring the params above;
`buildWrapperEnv` gained an `IsMesh` parameter that, when true, loads them
out of `getMeshArgsType`'s longer struct instead of `getDispatchArgsType`'s
shorter one (both share the same field indices for the four fields they
have in common -- `Resources`/`GroupID`/`GroupCount`/`GroupShared` --
which is what let `EntryWrapperPass` already read that shared prefix
through either struct type interchangeably, confirmed at H6c time);
`buildWaveLoop`'s exhaustive by-name parameter dispatch gained six new
`else if` arms threading the loaded values through by name, the same way
`resource_heap`/`wave_groupshared`/etc. already are. All three call sites
that build a top-level wrapper function (`buildWrapper`, the common case;
`buildWrapperForLoop`/`buildWrapperForBranch`, the barrier-split cases)
now compute `IsMesh` from `feme::getShaderStage(WaveBodyIn)` and thread it
through consistently, so a mesh entry containing a group-sync barrier
alongside an output store is handled correctly too, not just the common
no-barrier case.

**New unit tests.** `MeshOutputWrapperTest.cpp` (4 cases) isolates the new
pass: lowering a per-vertex-frequency store, lowering a per-primitive-
frequency store into the *other* output array, confirming params are
still appended even with no output store present (matching the "always
append" convention), and chaining into `EntryWrapperPass` to confirm the
combined pipeline builds a real `feme_cpu_entry_ms_main` wrapper.
`CompiledStageTest.cpp` gained one new end-to-end case,
`InvokeMeshWritesPerVertexOutputStore`: a canonicalized-shaped mesh entry
(its own `GroupID`, standing in for a real bounded per-vertex loop
variable, as the dynamic `Vertex` operand) compiled through the real
`CompiledStage::create` pipeline and invoked via `CompiledStage::
invokeMesh` writes its own group id, as a float, into `FemeMeshArgs::
VertexOutputs` at exactly that group's own slot -- the first test in this
codebase to prove a *real* value (not just zero-initialized memory)
reaches `VertexOutputs` through the full compiled path.

**What this row still leaves unwired, honestly re-scoping H6g-b's own
optimistic assumption.** `SetMeshOutputsEXT` has no canonicalized
`feme.stage.*` op, so `FemeMeshArgs::ActualVertexCount`/
`ActualPrimitiveCount` stay 0 after a real compiled `invokeMesh` call --
`feme::graphics::MeshOutputBuilder::setOutputCounts`/`assembleMeshlet`
already tolerate this (an out-of-range or absent `setOutputCounts` call
legitimately assembles an empty meshlet, per `MeshOutputBuilder.h`'s own
comment), so a real mesh-shader draw still assembles nothing to
rasterize even with this row's own wiring landed. Separately,
`Executor::runMeshWorkgroup`'s own `flattenMeshRow` (Executor.cpp, H6e)
does not yet filter by `SignatureElement::Frequency` -- it treats every
`Output` element as per-vertex, so a `PerPrimitive`-frequency element this
row's own `MeshOutputWrapperPass` correctly routes to
`PrimitiveOutputs` would not actually be read back out by the executor
today. Both gaps are new roadmap rows below (R34's own lettering
convention), not silently left implicit:

 - **H6c-a-a-i**: canonicalize `SetMeshOutputsEXT` into a new
   `feme.stage.*` op and wire it into `MeshOutputWrapperPass`/
   `EntryWrapperPass` so `ActualVertexCount`/`ActualPrimitiveCount` (and,
   ultimately, `assembleMeshlet`'s trimmed meshlet) reflect a real
   declared count instead of always 0.
 - **H6c-a-a-ii**: teach `Executor::runMeshWorkgroup`'s `flattenMeshRow`
   to route a `PerPrimitive`-frequency `Output` element into
   `MeshResources::PrimitiveOutputs`/`PrimitiveOutputLayout` (currently
   never populated at all) instead of unconditionally treating every
   element as per-vertex.

Because of both gaps, **this row's own CTS impact is correctly zero**,
matching H6c/H6d/H6i's own precedent of a CPU-lowering-only row not yet
being reachable from a real Vulkan draw:

```
dEQP-VK.mesh_shader.* (28044 cases):
Before this row (H6i/H6g's own recorded baseline):
  Passed:        1/28044 (0.0%)
  Failed:        337/28044 (1.2%)
  Not supported: 27706/28044 (98.8%)

After this row:
  Passed:        1/28044 (0.0%)
  Failed:        337/28044 (1.2%)
  Not supported: 27706/28044 (98.8%)
```

Byte-identical failing-case set. **0 regressions, 0 new passes** --
expected, since neither `SetMeshOutputsEXT` (H6c-a-a-i) nor
`PrimitiveOutputs` readback (H6c-a-a-ii) exist yet, so a real mesh-shader
draw still assembles an empty meshlet regardless of this row's own output-
store wiring.

**Regression sample.** `dEQP-VK.draw.*`'s 1957-case `draw_sample.txt`
sample, same file every prior row's own report used:

```
Before (H6i/H6g's own baseline):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)

After (this row):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical failing-case set. **0 regressions, 0 new passes.**

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: this row touches no feature bit, limit, or extension --
pure CPU-target IR-lowering, the same shape H6c/H6d/H6i's own entries
recorded needing no inventory update for.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1939/1998** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H6i's own **1934/1993** baseline by this row's five new
unit tests (`MeshOutputWrapperTest.cpp`'s 4,
`CompiledStageTest.cpp`'s 1 new `InvokeMeshWritesPerVertexOutputStore`
case).

**Reproducing.**

```
cd /home/dev/dev/VK-GL-CTS/run  # or any directory with a `vulkan` symlink
                               # to external/vulkancts/data/vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
 /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
 --deqp-case="dEQP-VK.mesh_shader.*" --deqp-log-filename=mesh_h6c_a_a.qpa
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
 /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
 --deqp-caselist-file=draw_sample.txt --deqp-log-filename=draw_h6c_a_a.qpa
```

## Roadmap H6c-a-b: measured impact (wire `TaskPayloadBuilder` into a task entry's canonicalized payload-store operation)

This row closes out H6c-a-b: a task (amplification) entry's own
canonicalized (H6i-shaped) `feme.stage.task.payload.store` -- a masked,
per-lane store with a compile-time-constant `offset` operand, by the time
`LinearizePass` is done with it -- now actually reaches `FemeTaskArgs::
Payload`, and `feme::graphics::Executor::executeDraws`'s own
`feme::graphics::TaskPayloadBuilder` instance backs that same memory end
to end into `FemeMeshArgs::Payload` for every mesh workgroup the task
workgroup's own (still H6c-a-a-i/-ii-blocked) `EmitMeshTasksEXT` request
dispatches.

**New `feme::cpu::TaskPayloadWrapperPass`** (`TaskPayloadWrapper.h`/`.cpp`)
is `MeshOutputWrapperPass`'s deliberately simpler task-stage counterpart,
run immediately before `EntryWrapperPass` in `Pipeline.cpp`'s
`ShaderStage::Amplification` case. It appends two new named wave-body
parameters -- `task_payload`, `task_max_payload_bytes` -- unconditionally
to every task entry's wave body (the same "always append params,
conditionally lower" convention every stage wrapper follows), then lowers
every masked payload store it finds into a store at `task_payload +
offset`. Unlike a mesh output store's dynamic per-lane `Vertex` operand, a
task payload store's `offset` is always the *same* compile-time constant
for every lane at one call site (`StageOpKind::TaskPayloadStore`'s own
documented contract, confirmed by `CanonicalizeStage.cpp`'s constant-
offset walk), so every lane targets one shared address -- the same "every
lane may write, the mask decides whose value survives" load-select-store
pattern `MeshOutputWrapper.cpp`'s `lowerMeshOutputStore` already uses, just
against one fixed address per call site instead of one address per output
slot, and with no `FemeStageLayout`/element-ID lookup needed at all. A
defensive `Offset + ByteSize <= MaxPayloadBytes` bounds check is ANDed into
each lane's own mask (mirroring `MeshOutputWrapper.cpp`'s own
`umin`-based clamp precedent for the same "a canonicalized store's own
operand is not yet validated against this dispatch's real runtime bound"
gap), since no execution-mode or captured attribute exists yet for a task
shader's own declared payload byte size to check against instead.

**Masking plumbing extended through `Linearize`/`SIMDize`.**
`StageMaskCalls.h`/`.cpp` gained `createMaskedTaskPayloadStore`/
`isMaskedTaskPayloadStoreCall` (mirroring `MaskedOutputStore`'s own
3-operand `(offset, value, mask)` shape); `Linearize.cpp`'s
`applyStageMasks`/`hasStageMaskOps` gained a `TaskPayloadStore` case
alongside `OutputStore`/`StreamEmit`/`StreamCut`; `SIMDize.cpp`'s
`FunctionWidener` gained `widenMaskedTaskPayloadStore` (keeps the constant
offset scalar, widens the value across the wave, ANDs the mask with
`Env.SideEffectMask`) and its dispatch check.

**`feme::cpu::EntryWrapperPass` extended, not replaced**, mirroring
H6c-a-a's own `IsMesh` precedent exactly: `WrapperEnv` gained two new
(default-null) fields (`TaskPayload`/`TaskMaxPayloadBytes`);
`buildWrapperEnv` gained an `IsTask` parameter that, when true, loads them
out of new `getTaskArgsType`'s `FemeTaskArgs`-shaped struct
(`StageArgsLayout.h`'s new `TaskArgsField` enum, sharing `Resources`/
`GroupID`/`GroupCount`/`GroupShared`'s field indices with
`getDispatchArgsType`/`getMeshArgsType`, the same shared-prefix trick H6c
established); `buildWaveLoop`'s by-name parameter dispatch gained
`task_payload`/`task_max_payload_bytes` arms; all three wrapper-building
call sites (`buildWrapper`, `buildWrapperForLoop`, `buildWrapperForBranch`)
now compute both `IsMesh` and `IsTask` and thread both through
consistently (never both true at once, since a function has exactly one
`ShaderStage`).

**Host-side wiring.** `feme::graphics::TaskPayloadBuilder` gained a
`getMutableBytes()` accessor (`TaskPayload.h`/`.cpp`) -- the only way this
class's own storage ever picks up a real compiled write, since a compiled
task entry has no way to call back into `write()`.
`feme::graphics::GraphicsPipeline::setMeshStage` gained a new
`MaxTaskPayloadBytes` parameter (mirroring how `MeshLimits`/`TaskLimits`
already flow the same way), populated from a new
`feme::vulkan::MaxTaskPayloadBytes` constant (`GraphicsPipeline.h`, the
specification's own 16384-byte floor, replacing `EntryPoints.cpp`'s
previously-hardcoded literal so the two can never disagree, the same
"shared ceiling" discipline `MaxMeshOutputVertices` already follows).
`Executor::executeDraws`'s task-dispatch loop now constructs one
`TaskPayloadBuilder` per task workgroup, backs `TaskResources::Payload`
with `getMutableBytes()` before `invokeTask`, and passes `getBytes()` into
`runMeshWorkgroup`'s own `Payload` parameter for every mesh workgroup that
task workgroup's `EmitMeshTasksEXT` request dispatches -- replacing the two
hardcoded `/*Payload=*/{}` call sites H6c-a-a's own row left in place.

**New unit tests.** `TaskPayloadWrapperTest.cpp` (4 cases) isolates the new
pass: lowering a single payload store, confirming params are still
appended even with no payload store present, lowering two stores at
distinct offsets in one entry, and chaining into `EntryWrapperPass` to
confirm the combined pipeline builds a real `feme_cpu_entry_as_main`
wrapper. `CompiledStageTest.cpp` gained one new end-to-end case,
`InvokeTaskWritesPayloadStore`: a canonicalized-shaped task entry compiled
through the real `CompiledStage::create` pipeline and invoked via
`CompiledStage::invokeTask` writes a real value into `FemeTaskArgs::
Payload` at exactly its own declared byte offset, leaving every other byte
untouched -- mirroring `InvokeMeshWritesPerVertexOutputStore`'s own "prove
a real value reaches host memory through the full compiled path" shape.

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **1944/2003** (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H6c-a-a's own **1939/1998** baseline by exactly this
row's 5 new tests.

**Measured CTS impact: correctly zero, for the same reason H6c-a-a's own
closing re-run found.** `FemeMeshArgs::Payload` now receives real bytes
end to end, but `H6c-a-a-i` (`SetMeshOutputsEXT` canonicalization) and
`H6c-a-a-ii` (`flattenMeshRow`'s `PerPrimitive` routing) remain open --
both already-tracked, pre-existing gaps this row's own text does not
depend on or touch -- so a real mesh-shader draw still assembles an empty
meshlet regardless of this row's own payload wiring, and no `dEQP-VK.
mesh_shader.*` case exercises reading a task payload back from a real
mesh invocation yet in any case (that would additionally need a mesh-side
`TaskPayloadWorkgroupEXT` *load* canonicalized into a `feme.stage.*` op,
explicitly out of this row's own scope per its roadmap text: "somewhere
real to be read from", not "read from"):

```
dEQP-VK.mesh_shader.* (28044 cases):
Before this row (H6c-a-a/H6i's own recorded baseline):
  Passed:        1/28044 (0.0%)
  Failed:        337/28044 (1.2%)
  Not supported: 27706/28044 (98.8%)

After this row:
  Passed:        1/28044 (0.0%)
  Failed:        337/28044 (1.2%)
  Not supported: 27706/28044 (98.8%)
```

Byte-identical failing-case set. **0 regressions, 0 new passes** --
expected, matching H6c/H6c-a-a/H6d/H6i's own precedent of a CPU-lowering-
and-host-wiring-only row not yet reachable from a real Vulkan draw until
`H6c-a-a-i`/`H6c-a-a-ii` also land.

**Regression sample.** `dEQP-VK.draw.*`'s 1957-case `draw_sample.txt`
sample, same file every prior row's own report used:

```
Before (H6c-a-a's own baseline):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)

After (this row):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical failing-case set. **0 regressions, 0 new passes.**

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: `maxTaskPayloadSize`'s advertised *value* is unchanged
(still the specification's own 16384-byte floor, now read from a shared
constant instead of a duplicated literal) and no feature bit or extension
changed -- the same shape H6c/H6c-a-a/H6d/H6i's own entries recorded
needing no inventory update for.

**Reproducing.**

```
cd /home/dev/dev/VK-GL-CTS/run  # or any directory with a `vulkan` symlink
                               # to external/vulkancts/data/vulkan
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
 /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
 --deqp-case="dEQP-VK.mesh_shader.*" --deqp-log-filename=mesh_h6c_a_b.qpa
VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json \
 /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
 --deqp-caselist-file=draw_sample.txt --deqp-log-filename=draw_h6c_a_b.qpa
```

## Roadmap H6c-a-a-i: measured impact (canonicalize `SetMeshOutputsEXT`, wire into `MeshOutputWrapperPass`/`EntryWrapperPass`)

This row closes out H6c-a-a-i: a mesh entry's `SetMeshOutputsEXT` call --
converted directly into a new `feme.stage.set_mesh_outputs` call at the
MLIR SPIR-V-to-LLVM conversion level (`SetMeshOutputsEXTConversionPattern`,
`SPIRVToLLVMPatterns.cpp`), masked/widened through `Linearize`/`SIMDize`
exactly like every other stage op, and lowered by `MeshOutputWrapperPass`
-- now actually writes `FemeMeshArgs::ActualVertexCount`/
`ActualPrimitiveCount` through two new always-appended trailing wave-body
params (`mesh_actual_vertex_count`, `mesh_actual_primitive_count`), the
same "append params, lower masked calls" shape H6c-a-a's own per-vertex/
per-primitive output-store wiring and H6c-a-b's own task-payload wiring
both already established. See Roadmap.md's own H6c-a-a-i entry for the
full implementation summary (the `EXTSetMeshOutputsOp`/`spirv.EXT.
SetMeshOutputs` real-mnemonic gotcha, the "workgroup-uniform but still
masked" design choice, and the exact test list).

`ninja check-feme` (`LLVM_ENABLE_ASSERTIONS=ON`, ccache build) passes in
full: **2007 discovered, 1948 passing** (59 pre-existing, unrelated
`Unsupported`, 0 `Failed`), up from H6c-a-b's own **2003 discovered, 1944
passing** baseline by exactly this row's 4 new tests (`StageOpsTest`'s
`SetMeshOutputsIsVoidAndNotOverloaded`, `MeshOutputWrapperTest`'s
`LowersSetMeshOutputsCall`, the new `spirv-to-llvm-set-mesh-outputs.mlir`
conversion test, and `CompiledStageTest`'s `InvokeMeshWritesSetMeshOutputs`
end-to-end case).

**Regression sample.** `dEQP-VK.draw.*`'s 1957-case `draw_sample.txt`
sample, same file every prior row's own report used:

```
Before (H6c-a-b's own baseline):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)

After (this row):
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

Byte-identical failing-case set. **0 regressions, 0 new passes.**

**`dEQP-VK.mesh_shader.*` (28044 cases): 0 new passes, as expected --
plus a newly-discovered crash affecting 28 cases, tracked as new row
H6c-a-a-iii.**

```
Before this row (H6c-a-b's own recorded baseline):
  Passed:        1/28044 (0.0%)
  Failed:        337/28044 (1.2%)
  Not supported: 27706/28044 (98.8%)

After this row:
  Passed:        1/28044 (0.0%)
  Failed:        309/28044 (1.1%)
  Not supported: 27706/28044 (98.8%)
  Unresolved (deqp-vk process aborts, no clean result -- see below): 28/28044 (0.1%)
```

Passed and Not-supported both stay byte-identical to the prior baseline
-- **expected and correct**, since `H6c-a-a-ii` (`flattenMeshRow`'s
`PerPrimitive` routing) remains open, so no real mesh-shader draw can yet
assemble a correctly-routed, non-empty meshlet from this row's wiring
alone. The `Failed` bucket's own count drop (337 -> 309) is *not* 28 new
passes: it is exactly the same 28 cases moving from `Failed` into the new
`Unresolved` bucket below, a strictly worse failure mode for those 28
cases, not an improvement.

**Newly-discovered crash (28 cases), tracked as new roadmap row
H6c-a-a-iii.** Before this row landed, a mesh entry referencing
`SetMeshOutputsEXT` failed pipeline creation immediately and cleanly, at
SPIR-V-to-LLVM conversion (`error: failed to legalize operation
'spirv.EXT.SetMeshOutputs' that was explicitly marked illegal`), reported
as an ordinary `Fail (retcode: VK_ERROR_INITIALIZATION_FAILED at
vkPipelineConstructionUtil.cpp:176)`. Now that this row makes that
conversion succeed, compilation reaches further into
`CanonicalizeStagePass`, and 28 of these same cases (each a real mesh
shader whose builtin interface uses a `PerPrimitiveEXT`-decorated or
otherwise arrayed builtin block -- e.g. `dEQP-VK.mesh_shader.ext.builtin.
cull_primitives`, `.draw_index_in_{mesh,task}`, `.layer*`, `.
local_invocation_{id,index}_in_{mesh,task}`, `.num_work_groups_*`, `.
position`, `.primitive_id_*`, `.viewport_index*`, `.work_group_id_in_
{mesh,task}`, and 6 `ext.smoke.monolithic.*` cases) instead abort the
entire `deqp-vk` process with:

```
deqp-vk: llvm/include/llvm/Support/Casting.h:572: decltype(auto)
llvm::cast(From *) [To = llvm::StructType, From = llvm::Type]:
Assertion `isa<To>(Val) && "cast<Ty>() argument of incompatible type!"'
failed.
```

This assertion lives in `CanonicalizeStage.cpp`'s
`resolveOffsetWithinElement` (pre-existing code this row's own new
`feme.stage.set_mesh_outputs` wiring never touches): its multi-
`ElementID` path (`IDs.size() != 1`, a builtin interface block with more
than one member) unconditionally `cast<StructType>`s the block's own
value type, a shape that has always held for every builtin interface
block reachable before this row (e.g. a non-arrayed `gl_PerVertex`
block), but does not hold for a mesh entry's own arrayed builtin blocks
(`PerPrimitiveEXT`-decorated or otherwise, one array element per
primitive/vertex rather than one plain struct) -- a shape that was
*never reachable* before this row, since every one of these 28 cases'
own pipelines used to fail at the earlier, unconverted-`SetMeshOutputsEXT`
rejection first. **0 `Pass`/`Fail` regressions** (byte-identical failing-
case set, just a worse failure mode), but a real robustness regression:
a `deqp-vk` run (or a fuzzer) without this report's own resume-loop
workaround (below) would silently lose every case after the first crash,
the same class of problem H4b's own tessellation triage already
documented for an unrelated crash. Filed as new roadmap row H6c-a-a-iii,
scoped to `CanonicalizeStage.cpp`'s general per-primitive/per-vertex
arrayed-builtin-block resolution -- deliberately *not* fixed in this row,
to keep this row's own scope to exactly `SetMeshOutputsEXT`'s own
canonicalization and wiring, its own stated deliverable.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: this row changes no advertised feature bit or extension,
only CPU-side lowering.

**Reproducing this row.** Same ICD build as the rest of this report. The
full `dEQP-VK.mesh_shader.*` group can no longer be run in one `deqp-vk`
invocation without losing cases past the first H6c-a-a-iii crash above,
so use the same resume-loop shape H4b's tessellation triage established,
generalized to blacklist every case whose own `Test case '...'..` line
printed with no following result line (not just the single last one --
one crash can leave several such cases pending in one batch):

```shell
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-case="dEQP-VK.mesh_shader.*" --deqp-runmode=txt-caselist
grep -oP "^TEST: \K.*" dEQP-VK-cases.txt > remaining.txt
# resume loop: repeatedly run against `remaining.txt`, parse each
# iteration's log for every "Test case '...'.." with no following
# Pass/Fail/NotSupported/... line, add those to a blacklist, remove
# both resolved and blacklisted cases from `remaining.txt`, and stop
# once a `DONE!` line appears with no unresolved case that iteration.
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-caselist-file=draw_sample.txt \
    --deqp-log-filename=draw_h6c_a_a_i.qpa
```

## Roadmap H6c-a-a-ii: measured impact (`flattenMeshRow`'s `PerPrimitive` routing)

**Root cause, confirmed by reverting the fix and re-running the new test.**
`Executor::executeDraws`'s `runMeshWorkgroup` lambda built exactly one
`StageStorage` for a mesh entry's whole `Output` signature, sized by
`Mesh.MaxOutputVertices`, and wired it into `MeshResources::
VertexOutputLayout`/`VertexOutputs` alone -- `PrimitiveOutputLayout`/
`PrimitiveOutputs` (fields that have existed on `MeshResources` since
H6c, per `ResourceHeap.h`'s own struct) were simply never assigned,
left at their default null/empty state. `MeshOutputWrapperPass`'s own
per-element lowering (`lowerMeshOutputStore`, H6c-a-a) already branches
correctly on `SignatureElement::Frequency`, addressing
`MEnv.PrimitiveOutputLayout`/`PrimitiveOutputs` for a `PerPrimitive`
element -- but those IR-level pointers are populated at runtime from
`FemeMeshArgs::PrimitiveOutputLayout`/`PrimitiveOutputs`, which
`Executor.cpp` sources from `MeshResources`. The moment a real compiled
mesh entry point calls an output store for a `PerPrimitive`-frequency
element, the generated code loads a field through that null layout
pointer and stores through the null data pointer -- an unconditional
crash, not merely stale or zeroed data. Confirmed directly: reverting
just this row's `Executor.cpp` change and re-running the new
`ExecutorTest.cpp` case below reproduces a `SIGSEGV` inside
`MeshOutputWrapperPass`'s compiled lowering, immediately.

**The fix.** `runMeshWorkgroup` now builds a second `StageStorage` for
the same `*MeshSig` signature, sized by `Mesh.MaxOutputPrimitives`
instead (mirroring the existing vertex one -- `buildStageStorage`
builds a dense, `ElementID`-indexed table across *every* `Output`
element regardless of which elements a particular storage block is
"for", exactly like the vertex block already does, so this needs no new
signature-filtering machinery of its own), and threads its layout/data
into `MeshResources::PrimitiveOutputLayout`/`PrimitiveOutputs`.
`flattenMeshRow` itself gained a `SignatureFrequency` parameter and a
matching `Storage` parameter, filtering `MeshSig->Elements` to just that
frequency (`Elt.Frequency == Freq`) rather than walking every `Output`
element unconditionally the way it always had; the vertex loop calls it
as `flattenMeshRow(*VertexOut, SignatureFrequency::PerVertex, V)` and the
new primitive loop as `flattenMeshRow(*PrimitiveOut,
SignatureFrequency::PerPrimitive, P)`, routing the primitive row into
`MeshOutputBuilder::setPrimitive` (implemented since H6c, but never
called by `Executor.cpp` until now) alongside the existing
`setPrimitiveIndices` call.

**A second, closely-related bug found and fixed along the way.**
`unflattenMeshRow` -- the merge-side counterpart that reconstructs a
flat `Merged` `StageStorage` from each assembled `Meshlet`'s own vertex
rows, downstream of `assembleMeshlet` -- still walked every `Output`
element unconditionally. For any signature mixing `PerVertex` and
`PerPrimitive` elements, `flattenMeshRow`'s now-narrower per-vertex-only
rows (fewer floats than before, since `PerPrimitive` elements are
excluded) would desynchronize from `unflattenMeshRow`'s own unfiltered
walk, corrupting `Row[Idx++]`'s indexing (an out-of-bounds/misaligned
read, not merely stale data) the moment a real mixed-frequency signature
reached rasterization. Fixed with the identical `Elt.Frequency !=
SignatureFrequency::PerVertex` filter, restoring the symmetry the two
functions always needed to have with each other.

**New unit test.** `ExecutorTest.cpp` gained
`RoutesAPerPrimitiveOutputElementIntoPrimitiveOutputsAlongsideAPerVertexOne`:
a real compiled mesh entry (canonicalized `feme.stage.set_mesh_outputs`/
`feme.stage.output.store` calls, reaching the full `CompiledStage::
create` pipeline through `MeshOutputWrapperPass`/`EntryWrapperPass`,
mirroring `CompiledStageTest.cpp`'s own `InvokeMeshWritesPerVertexOutputStore`/
`InvokeMeshWritesSetMeshOutputs` precedent but exercised through
`executeDraws` instead of `invokeMesh` directly, since the bug this row
fixes lives in `Executor.cpp`, not `CompiledStage`) declaring one
`PerVertex` `SV_Position` element and one `PerPrimitive` scalar element
from the same one-workgroup, one-vertex, one-primitive dispatch, run end
to end with `MeshOutputTopology::Points` (needing no separate
`PrimitiveIndices` write, which remains a distinct, still-open gap --
see `MeshOutputWrapper.h`'s own comment -- so this is the cleanest mixed-
frequency shape reachable today). Asserts the point still rasterizes to
the expected pixel, proving the frequency split does not misalign the
per-vertex path, and (by not crashing) that the primitive store's own
read/write is now safe. Reverting just this row's `Executor.cpp` change
and re-running this same test reproduces the pre-fix `SIGSEGV` directly
(see "Root cause" above), confirming both the bug and the fix in
isolation.

`ninja check-feme` (assertions-enabled, ccache build): **2008/2008**
discovered, **1949** passing (59 pre-existing, unrelated `Unsupported`,
0 `Failed`), up from H6c-a-a-i's own 2007/1948 baseline by exactly the 1
new test this row adds.

**`dEQP-VK.mesh_shader.*` (28044 cases): re-run using the same
generalized resume-loop methodology H6c-a-a-i's own report entry
established** (`H6c-a-a-iii`'s `resolveOffsetWithinElement` crash remains
open and independent of this row's own runtime-only `Executor.cpp`
change, so the full group still cannot run in a single `deqp-vk`
invocation):

```
Before this row (H6c-a-a-i's own recorded baseline):
  Passed:        1/28044 (0.0%)
  Failed:        309/28044 (1.1%)
  Not supported: 27706/28044 (98.8%)
  Unresolved (deqp-vk process aborts, no clean result): 28/28044 (0.1%)

After this row:
  Passed:        1/28044 (0.0%)
  Failed:        328/28044 (1.2%)
  Not supported: 27706/28044 (98.8%)
  Unresolved (deqp-vk process aborts, no clean result): 9/28044 (0.0%)
```

`Passed` and `Not supported` both stay byte-identical to the prior
baseline -- **expected and correct**: no case in today's suite exercises
a user-defined `PerPrimitive` varying without a mesh entry's builtin
interface also using an arrayed block, which still independently
crashes via `H6c-a-a-iii`'s own open `resolveOffsetWithinElement` bug,
so this row's own fix has nothing new to make pass yet. The crashing/
`Unresolved` bucket shrinks from 28 to 9 cases (all `ext.builtin.*`:
`cull_primitives`, `draw_index_in_{mesh,task}`,
`local_invocation_{id,index}_in_task`, `position`, `primitive_id_glsl`,
`work_group_id_in_{mesh,task}`), and `Failed` rises by the same 19-case
difference (309 -> 328) -- a real, reproducible re-run using the
identical full-caselist resume-loop methodology, **not a regression this
row introduces**: this row's own `Executor.cpp` change is purely a
runtime (host-side) fix, unreachable from `H6c-a-a-iii`'s own
compile-time `CanonicalizeStage.cpp` crash path, so it cannot itself
change which cases hit that crash. The exact root cause of the 28-vs-9
count difference is not yet isolated (a plausible candidate is
case-batching/ordering sensitivity in the resume loop itself, or
pipeline-cache/`deqp-vk`-process state carried between iterations of a
large caselist run -- neither investigated further here, since it does
not change this row's own `Passed`/`Not supported` counts and
`H6c-a-a-iii` remains the correctly-scoped owner of the crash itself)
and is noted here rather than asserted away.

**`dEQP-VK.draw.*`'s 1957-case `draw_sample.txt` regression sample**
stays byte-identical to every prior row's own recorded totals:

```
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

**0 regressions.**

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: this row changes no advertised feature bit or extension,
only CPU-side (host, not compiled-IR) executor lowering.

**Reproducing this row.** Same ICD build and resume-loop methodology as
H6c-a-a-i's own entry above:

```shell
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-case="dEQP-VK.mesh_shader.*" --deqp-runmode=txt-caselist
grep -oP "^TEST: \K.*" dEQP-VK-cases.txt > remaining.txt
# resume loop: repeatedly run against `remaining.txt`, parse each
# iteration's log for every "Test case '...'.." with no following
# result line, add those to a blacklist, remove both resolved and
# blacklisted cases from `remaining.txt`, and stop once a `DONE!` line
# appears with an empty `remaining.txt`.
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-caselist-file=draw_sample.txt \
    --deqp-log-filename=draw_h6c_a_a_ii.qpa
```

## Roadmap H6c-a-a-iii: measured impact (fix `resolveOffsetWithinElement`'s arrayed-builtin-block crash)

**Root cause, confirmed by reverting the fix and re-running the new
test.** `resolveOffsetWithinElement` reaches a multi-`ElementID` builtin
interface block (`IDs.size() != 1`) two ways: `resolveStageIOAccess`'s
`getDynamicVertexIndexedAccess` path -- which always peels one array
dimension into a per-vertex/per-primitive `ElemTy` before calling in, so
`ElemTy` is always a plain struct there -- and its own ordinary
constant-offset fallback, which does *not* peel that dimension for
`Output`-storage globals (`isPerVertexArrayInputGlobal` is deliberately
`Input`-only, per H6b's own comment on why a constant `Output`-array
index must not be folded into `Vertex` the same way). A **constant**-
indexed access into a mesh entry's arrayed `Output` builtin block (e.g.
`gl_MeshPrimitivesEXT[1].gl_PrimitiveID = ...`, exactly the shape a real
unrolled GLSL-compiled per-primitive write takes) therefore lands in
that fallback with `ElemTy` still the whole array-of-struct, not a
struct -- hitting `cast<StructType>` head-on. Confirmed directly:
reverting just the `dyn_cast`/`cast` change below and re-running the new
`CanonicalizeStageTest.cpp` case reproduces the assertion failure
(aborting the whole test binary), immediately.

**The fix.** `resolveOffsetWithinElement` now returns
`std::optional<StageIOAccess>` instead of `StageIOAccess`, and its
`IDs.size() != 1` branch uses `dyn_cast<StructType>` instead of
`cast<StructType>`, returning `std::nullopt` when `ElemTy` is not a
struct -- the same "leave this access unrewritten, `ValidateStagePass`
diagnoses it later" treatment every other unmodeled shape in this file
already gets (`UnresolvableLoadInputIsLeftAlone`'s own precedent). No
call-site change was needed beyond the signature update:
`resolveStageIOAccess` (the function's only caller) already returns
`std::optional<StageIOAccess>` itself, and both of its own call sites
(`return resolveOffsetWithinElement(...)`) already forward the result
directly.

**New test.** `CanonicalizeStageTest.cpp`'s
`ConstantIndexIntoArrayedBuiltinInterfaceBlockIsLeftUnrewritten`: a
multi-`ElementID` builtin interface block (`!feme.spirv.MemberDecorations`,
2 members) declared as an array of that struct (address space 8,
mirroring `gl_MeshPrimitivesEXT[]`'s own shape), stored into through a
constant array index (`getelementptr ... i32 0, i32 1, i32 0`) -- the
exact shape that used to crash. Confirms the store is left as a raw,
unrewritten `StoreInst` (no `feme.stage.output.store` call) after the
fix, and (by reverting the fix, above) that it crashed before.

`ninja check-feme` (assertions-enabled, ccache build):

```
Total Discovered Tests: 2009
  Unsupported:   59 (2.94%)
  Passed     : 1950 (97.06%)
```

**2009/2009** discovered, **1950** passing (59 pre-existing, unrelated
`Unsupported`, 0 `Failed`), up from H6c-a-a-ii's own 2008/1949 baseline
by exactly the 1 new test this row adds.

**`dEQP-VK.mesh_shader.*`'s own 28044-case group**, a real full re-run
using the identical resume-loop methodology H6c-a-a-i/H6c-a-a-ii's own
entries established (this time completing in a single iteration, with an
empty blacklist -- **0 crashes**):

```
Result counts: {'Fail': 337, 'NotSupported': 27706, 'Pass': 1}
Blacklisted (crashed): 0
```

`Passed` stays byte-identical at 1/28044. `Failed` rises from H6c-a-a-ii's
own baseline of 328 to **337** -- exactly the 9 cases H6c-a-a-ii's own
report found still crashing (`cull_primitives`,
`draw_index_in_{mesh,task}`, `local_invocation_{id,index}_in_task`,
`position`, `primitive_id_glsl`, `work_group_id_in_{mesh,task}`), now
resolving as a clean `Fail (retcode: VK_ERROR_INITIALIZATION_FAILED at
vkPipelineConstructionUtil.cpp:176)` instead of aborting the whole
process. `NotSupported` stays byte-identical at 27706/28044. **0
`Pass`/`Fail` regressions** -- exactly the milestone's own "same
failing-case set, just a worse failure mode" framing, now resolved back
to the better mode: a `deqp-vk` run (or a fuzzer) no longer loses any
case past one of these 9, and no resume-loop workaround is needed for
this group any more.

**`dEQP-VK.draw.*`'s 1957-case `draw_sample.txt` regression sample**
stays byte-identical to every prior row's own recorded totals:

```
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

**0 regressions** (expected: this fix only changes what happens when an
unmodeled shape is *reached*, not any already-modeled one
`dEQP-VK.draw.*` exercises).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: this row changes no advertised feature bit or extension,
only a robustness fix to existing CPU-side canonicalization.

**Reproducing this row.** Same ICD build as the rest of this report. The
full group can now be run in one `deqp-vk` invocation without losing any
case, no resume loop required any more:

```shell
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-case="dEQP-VK.mesh_shader.*" --deqp-runmode=txt-caselist
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-caselist-file=dEQP-VK-cases.txt --deqp-log-filename=mesh_h6c_a_a_iii.qpa
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-caselist-file=draw_sample.txt \
    --deqp-log-filename=draw_h6c_a_a_iii.qpa
```

## Roadmap H6g-b: measured impact (re-run `dEQP-VK.mesh_shader.*`, confirm the 235/33-case content-compilation bucket clears)

**Starting point.** With `H6c-a-a-iii` landed (0 crashes, `Fail`
337/28044), this row's own literal ask was to re-run
`dEQP-VK.mesh_shader.*` and confirm the 235 `vkCreateGraphicsPipelines`
(`vkRefUtil.cpp:37`) / 33 `vkPipelineConstructionUtil.cpp:176` ->
`VK_ERROR_INITIALIZATION_FAILED` bucket clears now that real mesh/task
content can compile end to end.

**What the re-run found.** It did not clear -- but not because
`H6c-a-a-iii`'s own fix was wrong. Re-running just the 235-case
`vkRefUtil.cpp:37` bucket with `FEME_VULKAN_LOG_CREATION_ERRORS=1`
found the actual diagnostic for a representative case
(`dEQP-VK.mesh_shader.ext.api.draw.draw_count_0.*`) was:

```
vkCreateGraphicsPipelines: a mesh pipeline may not declare pVertexInputState
  Fail (vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED at vkRefUtil.cpp:37)
```

`GraphicsPipeline.cpp`'s own mesh-stage carve-out rejected any non-null
`pVertexInputState`/`pInputAssemblyState` outright, but the Vulkan spec
(`VkGraphicsPipelineCreateInfo::pVertexInputState`/`pInputAssemblyState`,
`VK_EXT_mesh_shader`/`VK_NV_mesh_shader` carve-out) only requires both
be **ignored** when the pipeline includes a mesh shader stage, not null
-- and `dEQP-VK.mesh_shader.*` itself builds every pipeline through the
shared, non-mesh-aware `vkObjUtil.cpp` `makeGraphicsPipeline` helper,
which unconditionally sets a non-null default `pVertexInputState`/
`pInputAssemblyState` regardless of which stages are present. This was
this row's own real, previously-unreported bug, found and fixed here.

**The fix.** `GraphicsPipeline.cpp`'s `MeshInfo` branch no longer reads
either pointer at all for a mesh pipeline -- simply not consulting them
is exactly the spec's own "ignored" treatment, with no further check
needed (neither pointer's contents feed anything else in this function
for a mesh pipeline; `Executor.cpp`'s mesh path already drives entirely
off `MeshState::OutputTopology`). `GraphicsPipelineTest.cpp`'s
`RejectsMeshPipelineWith{VertexInput,InputAssembly}State` are replaced
with `AcceptsMeshPipelineWith{VertexInput,InputAssembly}State`, now
expecting `VK_SUCCESS` and a real, destroyed pipeline handle.

```
ninja check-feme (assertions-enabled, ccache build):
Total Discovered Tests: 2009
  Unsupported:   59 (2.94%)
  Passed     : 1950 (97.06%)
```

**2009/2009** discovered, **1950** passing -- unchanged from
`H6c-a-a-iii`'s own baseline (this row replaces 2 existing tests with 2
new ones, rather than adding any).

**A real `dEQP-VK.mesh_shader.*` re-run (28044 cases)** after the fix,
same resume-loop methodology:

```
Result counts: {'Fail': 334, 'NotSupported': 27706, 'Pass': 1}
Blacklisted (crashed): 3
  dEQP-VK.mesh_shader.ext.misc.emit_in_control_flow
  dEQP-VK.mesh_shader.ext.misc.emit_in_control_flow_bad_emit_last
  dEQP-VK.mesh_shader.ext.misc.payload_not_accessed
```

`Passed` stays byte-identical at 1/28044. The 337-case `Fail` bucket
breaks down as: 232 at `vkRefUtil.cpp:37`, 68 at `vkRefUtilImpl.inl:508`
(the already-tracked `VK_ERROR_FORMAT_NOT_SUPPORTED` bucket, H6g-a/H8,
untouched by this row), 33 at `vkPipelineConstructionUtil.cpp:176`, and
1 at `vkQueryUtil.cpp:305`. Of this row's own named 235+33 bucket: **3**
of the 235 moved into a brand-new crash bucket (H6g-b-b below); a
diagnostic-logged re-run of the remaining 232 found 202 now fail with
`error: unhandled Decoration : 'PerPrimitiveEXT'` (an MLIR SPIR-V
dialect deserialization gap, H6g-b-a below, the dominant single cause)
and the other 30 spread across roughly ten distinct, smaller gaps out
of this row's own scope; all 33 of the `vkPipelineConstructionUtil.cpp`
cases remain, now root-caused (via a representative case,
`dEQP-VK.mesh_shader.ext.builtin.cull_primitives`) to `JIT session
error: Symbols not found: [ spirv_var_16 ]` -- H6c-a-a-iii's own fix
correctly left an unresolvable arrayed-builtin-block access unrewritten
rather than crashing, but nothing downstream (`ValidateStagePass`,
which still does not validate `ShaderStage::Mesh`) ever diagnoses that
left-alone access before it reaches the JIT as a genuinely undefined
symbol (H6g-b-c below).

**`dEQP-VK.draw.*`'s 1957-case `draw_sample.txt` regression sample**
stays byte-identical to every prior row's own recorded totals:

```
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

**0 regressions** (expected: this fix only changes mesh-pipeline
vertex-input/input-assembly-state handling, which no
`dEQP-VK.draw.*` case exercises).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed
no change needed: `VK_EXT_mesh_shader` was already `Advertised` (H6f);
this is a pure pipeline-validation robustness fix, touching no
advertised feature bit or extension.

**Milestone H6 does not close.** This row's own re-run replaced one
known blocker (the 235/33-case bucket, as originally framed) with three
newly-isolated ones -- H6g-b-a, H6g-b-b, H6g-b-c -- added to the
roadmap below.

**Reproducing this row.** Same ICD build and resume-loop methodology as
every prior mesh-shading row, run from `VK-GL-CTS/run` (relative
shader-source resource paths require it):

```shell
cd /path/to/VK-GL-CTS/run
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-case="dEQP-VK.mesh_shader.*" --deqp-runmode=txt-caselist
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-caselist-file=dEQP-VK-cases.txt --deqp-log-filename=mesh_h6g_b.qpa
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-caselist-file=draw_sample.txt \
    --deqp-log-filename=draw_h6g_b.qpa
```

## Roadmap H6g-b-a: measured impact (fix `PerPrimitiveEXT` decoration deserialization/serialization gap)

**Starting point.** H6g-b's own re-run found the dominant single cause
of its 232-case `vkCreateGraphicsPipelines`/`vkRefUtil.cpp:37` bucket
(202 of 232 cases) was `ConvertSPIRVToLLVMPass`/the MLIR SPIR-V
dialect deserializer rejecting `PerPrimitiveEXT` outright with `error:
unhandled Decoration : 'PerPrimitiveEXT'`, failing SPIR-V module
deserialization before `feme` ever saw the module. This row's own ask
was to isolate the root cause (upstream MLIR SPIR-V dialect decoration
table, or a `feme`-local import shim over it) and fix it.

**Root cause.** Purely upstream MLIR, not any `feme`-local shim.
`SPIRVBase.td`'s `Decoration` enum already lists
`SPIRV_D_PerPrimitiveEXT` (value 5271, gated on the
`SPV_EXT_mesh_shader` extension and `MeshShadingEXT` capability, same
as every other `PerPrimitiveEXT`-adjacent enum entry). But
`Deserializer.cpp`'s `processDecoration` is a hand-written switch over
`spirv::Decoration` (its own comment: "TODO: This function should also
be auto-generated. For now, since only a few decorations are
processed/handled in a meaningful manner, going with a manual
implementation") -- and that switch's unit-attribute case group
(`Aliased`, `Flat`, `NoPerspective`, `Patch`, `Coherent`, etc., all
zero-operand decorations that just attach a `UnitAttr`) never listed
`PerPrimitiveEXT`, so it fell through to the `default:` case's
`"unhandled Decoration"` error. Per the SPIR-V spec, `PerPrimitiveEXT`
is itself a zero-operand decoration -- it belongs in exactly that
group. `Serializer.cpp`'s matching case list had the identical gap for
the reverse direction (MLIR -> SPIR-V), and its `getDecorationName`
helper (which guesses a decoration's SPIR-V name back from its
`snake_case` MLIR attribute name via
`llvm::convertToCamelFromSnakeCase`) needed the same kind of explicit
override the `cache_control_load_intel`/`cache_control_store_intel`
cases already use, since that conversion cannot recover an all-caps
`EXT` suffix any more than it could recover `INTEL`'s.

**The fix.** Added `spirv::Decoration::PerPrimitiveEXT` to
`Deserializer.cpp`'s existing unit-attribute case group (identical
`words.size() != 2` shape check, attaches `opBuilder.getUnitAttr()`)
and to `Serializer.cpp`'s mirror-image case group, plus a
`per_primitive_ext` -> `"PerPrimitiveEXT"` override in
`getDecorationName`. Three-line diff to each of
`mlir/lib/Target/SPIRV/{Deserialization/Deserializer,Serialization/Serializer}.cpp`.

**New test.** `mlir/test/Target/SPIRV/mesh-ops.mlir` gained a new
split (a `PerPrimitiveEXT`-decorated `spirv.GlobalVariable` inside a
`MeshShadingEXT`-requiring module), exercised by the file's existing
`--test-spirv-roundtrip` `RUN` line (MLIR -> SPIR-V binary -> MLIR,
`FileCheck`'d) and, where `spirv-tools` is available, its
`--serialize-spirv`/`spirv-val` `RUN` lines (real binary
serialization, validated by `spirv-val` itself).

```
ninja check-mlir-target-spirv: 58/58 passing (was 57/58; +1 new test)
ninja check-mlir-dialect-spirv: 52/52 passing (unaffected)
ninja check-feme (assertions-enabled, ccache build):
Total Discovered Tests: 2009
  Unsupported:   59 (2.94%)
  Passed     : 1950 (97.06%)
```

`check-feme` stays byte-identical to H6g-b's own baseline (1950/2009,
0 `Failed`), since this is a pure upstream-MLIR fix touching no
`feme`-local source.

**A real `dEQP-VK.mesh_shader.*` re-run (28044 cases)** after the fix,
same resume-loop methodology:

```
Result counts: {'Fail': 323, 'NotSupported': 27706, 'Pass': 1}
Blacklisted (crashed): 14 (was 3)
```

A diagnostic-logged re-run of exactly the 232-case
`vkRefUtil.cpp:37` bucket H6g-b's own text named (via
`FEME_VULKAN_LOG_CREATION_ERRORS=1`) confirms this row's own fix
directly: **0** occurrences of `"unhandled Decoration"` remain (down
from 202) -- this row's own named dominant cause is fully cleared.
Tracing where those 202 formerly-blocked cases landed:

- **3** now progress past pipeline creation entirely, failing later at
  a brand-new `vkCmdUtil.cpp:338` location (out of this row's scope, a
  genuine forward-progress signal this fix produced).
- **11** now reach `feme::cpu::SIMDizePass` far enough to hit
  H6g-b-b's own already-tracked `FunctionWidener::widenMaskedStore`
  assertion (confirmed via `gdb` backtrace on one of them,
  `dEQP-VK.mesh_shader.ext.misc.barrier_in_task`: an identical stack
  to H6g-b-b's own originally-reported 3 cases). H6g-b-b's own crash
  count grows from 3 to 14 as a direct, expected consequence; see its
  updated roadmap text.
- The remaining **188**, plus the **30** cases H6g-b's own text
  already called out of its own scope, now fail for other, further
  downstream reasons. A diagnostic-logged re-run of the resulting
  **218**-case `vkRefUtil.cpp:37` bucket breaks down as:

  | Count | Cause |
  |---|---|
  | 80 | `failed to legalize operation 'spirv.AccessChain' that was explicitly marked illegal` (new dominant cause, tracked as **H6g-b-a-i**) |
  | 68 | `feme-cpu-simdize: ... has a divergent vector value ... used outside a supported insertelement-chain/...` |
  | 15 | `feme-cpu-wrap-mesh-output`/`feme-cpu-wrap-fragment` metadata-missing errors |
  | 14 | `feme-cpu-linearize` unsupported-control-flow-shape errors |
  | 11 | `failed to legalize operation 'spirv.MemoryBarrier'` |
  | 5 | `Symbols not found: [ spirv_var_N ]` (already tracked by H6g-b-c) |
  | 9 | `spirv.AtomicExchange`/`spirv.EXT.EmitMeshTasks`/`spirv.Any`/`spirv.All` legalization failures (singletons/small groups) |

  All 33 `vkPipelineConstructionUtil.cpp:176` cases H6g-b's own text
  named remain, unaffected either way (still tracked by H6g-b-c).

**`dEQP-VK.draw.*`'s 1957-case `draw_sample.txt` regression sample**
stays byte-identical to every prior row's own recorded totals:

```
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

**0 regressions** (expected: this fix only changes SPIR-V decoration
deserialization/serialization for a mesh-shading-only decoration, which
no `dEQP-VK.draw.*` case exercises).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed
no change needed: this is a pure MLIR SPIR-V dialect fix, touching no
`feme`-advertised feature bit or extension.

**Milestone H6 does not close.** This row's own fix directly clears
the dominant cause it named, but the same re-run that confirms that
also surfaces a new dominant cause in its place --
`spirv.AccessChain` legalization failures, tracked as new roadmap row
H6g-b-a-i -- alongside the already-tracked H6g-b-b (now grown from 3
to 14 crashing cases) and H6g-b-c.

**Reproducing this row.** Same ICD build and resume-loop methodology
as every prior mesh-shading row, run from `VK-GL-CTS/run` (relative
shader-source resource paths require it):

```shell
cd /path/to/VK-GL-CTS/run
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  python3 resume_run.py
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  FEME_VULKAN_LOG_CREATION_ERRORS=1 \
  deqp-vk --deqp-caselist-file=vkrefutil_fail_cases.txt \
    --deqp-log-filename=diag_check.qpa
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-caselist-file=draw_sample.txt \
    --deqp-log-filename=draw_h6g_b_a.qpa
```

## Roadmap H6g-b-a-i: measured impact (fix Vulkan array-stride correctness in `VulkanLayoutUtils`/`ConvertSPIRVToLLVMPass`)

**Starting point.** H6g-b-a's own re-run found the new dominant single
cause of its 218-case `vkCreateGraphicsPipelines`/`vkRefUtil.cpp:37`
bucket (80 of 218 cases) was `ConvertSPIRVToLLVMPass` failing to
legalize `spirv.AccessChain` outright ("failed to legalize operation
'spirv.AccessChain' that was explicitly marked illegal"). This row's
own ask was to isolate which `spirv.AccessChain` shape the existing
conversion patterns don't cover, and fix it.

**Root cause.** Reproduced directly by re-running the exact 218-case
bucket through feme's own Vulkan ICD
(`VK_ICD_FILENAMES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json`,
`FEME_VULKAN_LOG_CREATION_ERRORS=1`) rather than theorizing: the base
pointer type on every one of the 80 failing `spirv.AccessChain` ops
was a `StorageBuffer`/`Block`-decorated struct holding ordinary
vertex-attribute data (`vec2`/`vec3`/`vec4` float/int arrays of 4
elements each) -- a generic CTS `RWStructuredBuffer`-style SSBO, not
mesh-shader-specific content at all. It only surfaces in this test
group because H6g-b-a's own fix is the first thing to let this
particular SPIR-V shape reach legalization at all here (it is very
likely exercised by other, non-mesh `dEQP-VK` groups already, but
confirming/fixing that is out of this row's own scope).

`convertArrayType` (`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`)
validates a `!spirv.array<N x T, stride=S>`'s declared stride `S`
against `T`'s own compact `SPIRVType::getSizeInBytes()` and rejects
the array outright if they don't match exactly. For `vector<3xf32>`,
that compact size is 12 bytes -- but the Vulkan spec's std430/std140
layout rules require an array's stride to be a multiple of its
element's *base alignment*, and a 3-component vector's base alignment
is rounded up to that of a 4-component one (16 bytes), specifically
*because* 3-component vectors are otherwise irregular to pack. Every
conformant SPIR-V producer (glslang included) therefore emits
`ArrayStride=16` for `array<N x vec3<f32>>`, not 12 -- so this compact-
size check was spuriously rejecting every single one of these
(extremely common) arrays as having an "unnatural" stride. Confirmed
independently that MLIR's own `DataLayout::getTypeSize` already
computes a real LLVM `!llvm.array<N x vector<3xf32>>`'s per-element
size as 16 bytes too (`getDefaultTypeSizeInBits` in
`DataLayoutInterfaces.cpp` rounds a vector's innermost dimension up to
the next power of 2), meaning a plain, unpadded LLVM array genuinely
does reproduce stride 16 in practice -- rejecting it was simply wrong,
not a conservative-but-safe check.

A second, independently-triggerable copy of the same flaw was found in
`VulkanLayoutUtils::decorateType(spirv::ArrayType, Size&, Size&)`
(`mlir/lib/Dialect/SPIRV/Utils/LayoutUtils.cpp`), which computes the
*canonical* stride and total size for an array when decorating an
undecorated composite type. It also used the element's raw compact
size (both for the stride it assigns and, less obviously, for the
array's own total size used to place any struct member that follows
it) instead of the alignment-rounded value. This function backs (a)
`SPIRVCompositeTypeLayoutPass` (`decorate-spirv-composite-type-layout`,
which emits real `ArrayStride` decorations into SPIR-V -- meaning it
likely emitted too-small strides/offsets for such arrays historically)
and (b) `convertStructTypeWithOffset`'s own struct-identity check in
`SPIRVToLLVM.cpp` (not used by feme, which has its own struct
converter for an unrelated, already-solved reason, but a real
consumer of the same utility nonetheless).

**The fix.** Added a new public helper,
`VulkanLayoutUtils::getNaturalArrayStride(Type elementType)`, that
returns the element's size rounded up to its own alignment (reusing
the existing private `decorateType(Type, Size&, Size&)` overload to
compute both). Wired it into two places:

1. `convertArrayType`'s stride-validity check, replacing the direct
   `SPIRVType::getSizeInBytes()` comparison.
2. `VulkanLayoutUtils::decorateType(spirv::ArrayType, ...)`'s own
   stride computation (`llvm::alignTo(elementSize, elementAlignment)`)
   and total-size computation (`stride * numElements`, not
   `elementSize * numElements`), so the canonical layout this utility
   produces is now internally consistent with the corrected stride.

Small, surgical diff across
`mlir/include/mlir/Dialect/SPIRV/Utils/LayoutUtils.h`,
`mlir/lib/Dialect/SPIRV/Utils/LayoutUtils.cpp`, and
`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`.

**New tests.**

- `mlir/test/Conversion/SPIRVToLLVM/spirv-types-to-llvm.mlir`:
  `array_with_natural_vector3_stride`, a
  `!spirv.array<4 x vector<3xf32>, stride=16>` now converting to
  `!llvm.array<4 x vector<3xf32>>` (previously rejected).
- `mlir/test/Conversion/SPIRVToLLVM/spirv-types-to-llvm-invalid.mlir`:
  `array_with_unnatural_vector3_stride`, the same element type but
  `stride=12` (the wrong, unrounded value) confirmed still correctly
  rejected -- guards against the fix becoming overly permissive.
- `mlir/test/Dialect/SPIRV/Transforms/layout-decoration.mlir`: a
  struct with an undecorated `!spirv.array<4xvector<3xf32>>` member
  now decorated with `stride=16` (not 12) and its following `f32`
  member correctly placed at offset 64 (not 48), covering the
  `VulkanLayoutUtils::decorateType` fix specifically.
- `feme/test/Conversion/SPIRVToLLVM/spirv-to-llvm-storage-buffer.mlir`:
  `read_vec3_array_element`, a `StorageBuffer` block with a fixed-size
  `vec3` array member (the exact shape that reproduced the real CTS
  failure) converting through feme's own resource-handle conversion
  path to the expected `llvm.spv.resource.getpointer`/GEP/load
  sequence, mirroring the file's existing `rtarray`-based cases.

```
ninja check-mlir-target-spirv: 23/23 passing (unaffected; this row
  doesn't touch Target/SPIRV)
ninja check-mlir-dialect-spirv: 52/52 passing (0 regressions; +1 new
  split-input-file case added to the existing
  `layout-decoration.mlir` -- lit counts files, not `RUN`-split cases
  within them, so the discovered-test total is unchanged)
ninja check-mlir-conversion-spirvtollvm (SPIRVToLLVM lit suite): 23/23
  passing (0 regressions; +2 new split-input-file cases added to
  existing files, same file-granularity reasoning as above)
MLIRSPIRVToLLVMTests (gtest unit suite): 3/3 passing, unaffected
ninja check-feme (assertions-enabled, ccache build):
Total Discovered Tests: 2009
  Unsupported:   59 (2.94%)
  Passed     : 1950 (97.06%)
```

`check-feme` stays byte-identical to H6g-b-a's own baseline
(1950/2009, 0 `Failed`) -- this row extends an existing test file with
a new `RUN`-split case rather than adding a new discovered test.

**A diagnostic-logged re-run of the exact 218-case `vkRefUtil.cpp:37`
bucket** H6g-b-a's own text named, through feme's actual Vulkan ICD,
confirms the fix directly:

```
| Count | Cause (before -> after this row's fix) |
|---|---|
| 80 -> 0  | `spirv.AccessChain` legalization failures (this row's own named cause -- fully cleared) |
| 1  -> 81 | `spirv.All` legalization failures (new dominant cause, tracked as H6g-b-a-i-a) |
| 11 -> 11 | `spirv.MemoryBarrier` legalization failures (unaffected, already out of scope) |
| 4  -> 4  | `spirv.AtomicExchange` legalization failures (unaffected, already out of scope) |
| 1  -> 1  | `spirv.EXT.EmitMeshTasks` legalization failure (unaffected, already out of scope) |
| 1  -> 1  | `spirv.Any` legalization failure (unaffected, already out of scope) |
```

All 80 formerly-`spirv.AccessChain`-blocked cases now progress further
and land squarely in the new `spirv.All` bucket (81 = 80 + the
pre-existing 1) -- a clean, single-cause hand-off consistent with this
milestone's established pattern (fixing the dominant blocker in a
bucket routes the same cases into whatever the next dominant blocker
is, rather than spreading them across many new failure modes). Overall
bucket totals are unchanged (218/218 still fail, as expected: none of
these cases can succeed until every remaining legalization gap in
their shared SPIR-V content is closed), but the specific cause each
one hits has moved strictly forward, confirming real progress.

**`dEQP-VK.draw.*`'s 1957-case `draw_sample.txt` regression sample**
stays byte-identical to every prior row's own recorded totals:

```
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

**0 regressions** (expected: `vector<3xf32>`/similar array-of-vector
layouts affected by this fix are common but this fix only makes
previously over-rejected, spec-conformant SPIR-V accepted -- it never
changes behavior for any SPIR-V this sample already exercised
successfully).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed
no change needed: this is a pure MLIR SPIR-V layout/type-conversion
correctness fix, touching no `feme`-advertised feature bit or
extension.

**Milestone H6 does not close.** This row's own fix directly clears
the dominant cause it named, but the same re-run that confirms that
also surfaces a new dominant cause in its place -- `spirv.All`
legalization failures, tracked as new roadmap row H6g-b-a-i-a --
alongside the already-tracked H6g-b-b and H6g-b-c.

**Reproducing this row.** Same ICD build and methodology as every
prior mesh-shading row, run from `VK-GL-CTS/run` (relative
shader-source resource paths require it):

```shell
cd /path/to/VK-GL-CTS/run
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  FEME_VULKAN_LOG_CREATION_ERRORS=1 \
  deqp-vk --deqp-caselist-file=<218-case-bucket>.txt \
    --deqp-log-filename=diag_check.qpa
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-caselist-file=draw_sample.txt \
    --deqp-log-filename=draw_h6g_b_a_i.qpa
```

## Roadmap H6g-b-a-i-a: measured impact (register a `ConvertSPIRVToLLVMPass` conversion pattern for `spirv.All`/`spirv.Any`)

**Starting point.** H6g-b-a-i's own re-run found the new dominant
single cause of its 218-case
`vkCreateGraphicsPipelines`/`vkRefUtil.cpp:37` bucket (81 of 218
cases, up from a pre-existing, already out-of-scope 1) was
`ConvertSPIRVToLLVMPass` failing to legalize `spirv.All` outright
("failed to legalize operation 'spirv.All' that was explicitly marked
illegal"). This row's own ask was to isolate whether MLIR's
SPIRVToLLVM conversion is simply missing a pattern for
`spirv.All`/`spirv.Any` outright, or has one that doesn't cover this
operand shape, and fix it.

**Root cause.** `mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`'s
`populateSPIRVToLLVMConversionPatterns` has a `Logical ops` pattern
section covering `spirv.LogicalAnd`, `spirv.LogicalOr`,
`spirv.LogicalEqual`, `spirv.LogicalNotEqual`, and `spirv.LogicalNot`
-- but never had an entry for `spirv.All` or `spirv.Any` at all. The
conversion's target correctly marks both ops illegal for this same
conversion (they must not survive into the LLVM dialect), but with no
pattern registered to actually rewrite them, every occurrence hits the
pass's generic "explicitly marked illegal" failure path regardless of
operand shape or vector width -- confirming the first, simpler of this
row's own two named alternatives, not a shape-specific gap in an
existing pattern.

**Fix.** `spirv.All`/`spirv.Any` each reduce a vector of `i1` values to
a scalar `i1` (per their own `SPIRV_AllOp`/`SPIRV_AnyOp` definitions in
`SPIRVLogicalOps.td`) -- exactly the shape the LLVM dialect's existing
vector-reduction intrinsics already model
(`llvm.intr.vector.reduce.and`/`llvm.intr.vector.reduce.or`, exposed as
`LLVM::vector_reduce_and`/`LLVM::vector_reduce_or`, and already used
the same way by `ConvertVectorToLLVM`'s own `vector.reduction`
`AND`/`OR` lowering). Fixed by registering two new
`DirectConversionPattern<spirv::AllOp, LLVM::vector_reduce_and>`/
`DirectConversionPattern<spirv::AnyOp, LLVM::vector_reduce_or>`
instantiations in the `Logical ops` pattern list -- the same
one-line-per-op registration every other `DirectConversionPattern` use
in this file already follows, needing no new pattern class.

**Tests.** New lit test cases added to the existing
`mlir/test/Conversion/SPIRVToLLVM/logical-ops-to-llvm.mlir`
(`all_vector`/`any_vector`, each a `vector<4xi1>` `spirv.All`/
`spirv.Any` converting to the expected
`llvm.intr.vector.reduce.and`/`.or`).
`mlir/docs/SPIRVToLLVMDialectConversion.md`'s "Logical ops" section
updated with the new op-to-intrinsic mapping.

```
ninja check-mlir-conversion-spirvtollvm (SPIRVToLLVM lit suite): 23/23
  passing (0 regressions; +2 new split-input-file cases added to an
  existing file, so the discovered-test total is unchanged)
ninja check-mlir-target-spirv: 58/58 passing (unaffected; this row
  doesn't touch Target/SPIRV)
ninja check-mlir-dialect-spirv: 52/52 passing (unaffected; this row
  doesn't touch Dialect/SPIRV)
MLIRSPIRVToLLVMTests (gtest unit suite): 3/3 passing, unaffected
ninja check-feme (assertions-enabled, ccache build):
Total Discovered Tests: 2009
  Unsupported:   59 (2.94%)
  Passed     : 1950 (97.06%)
```

`check-feme` stays byte-identical to H6g-b-a-i's own baseline
(1950/2009, 0 `Failed`) -- this is a pure upstream-MLIR fix, touching
no `feme`-local code at all.

**A diagnostic-logged re-run of the exact 218-case `vkRefUtil.cpp:37`
bucket** H6g-b-a-i's own text named, through feme's actual Vulkan ICD,
confirms the fix directly: **0** `spirv.All`/`spirv.Any` legalization
failures remain (down from 81), with all 81 formerly-blocked cases now
progressing further. The bucket's `vkCreateGraphicsPipelines` failure
causes now partition its full 218 cases into 9 distinct buckets:

```
| Count | Cause (before -> after this row's fix) |
|---|---|
| 1  -> 82 | `feme::cpu::UnsupportedOps` register-bound resource handle rejection (new dominant cause, tracked as H6g-b-a-i-a-i) |
| ?  -> 68 | `feme-cpu-simdize` divergent-vector-value rejections (already out of scope) |
| ?  -> 17 | `feme-cpu-wrap-mesh-output`/`feme-cpu-wrap-fragment` metadata-missing errors (already out of scope) |
| 11 -> 17 | `spirv.MemoryBarrier` legalization failures (already out of scope; count includes both `spirv.MemoryBarrier` scope variants) |
| ?  -> 12 | `feme-cpu-linearize` unsupported-control-flow-shape errors (already out of scope) |
| ?  -> 7  | unimplemented specialization constants (already out of scope) |
| ?  -> 7  | unimplemented rasterizer-discard/depth-clamp/depth-bias/non-fill polygon modes (already out of scope) |
| ?  -> 5  | `Symbols not found` cases (already tracked by H6g-b-c) |
| ?  -> 3  | pipeline declares more color-blend-state entries than its render target has color attachments (already out of scope) |
```

(`?` marks causes whose exact pre-fix count within this specific
218-case bucket was not separately isolated by H6g-b-a-i's own
diagnostic run, which only isolated the `spirv.AccessChain` and
`spirv.All`/`spirv.Any` counts explicitly; all of these were already
named as "out of this row's own scope" in that row's own text, and
their bucket membership and 218-case total are unaffected either way.)

The 81 formerly-`spirv.All`/`spirv.Any`-blocked cases progress further
and land squarely in the new `feme::cpu::UnsupportedOps` bucket (82 =
81 + the pre-existing 1) -- the same clean, single-cause hand-off
pattern this milestone's investigation has shown at every prior step
(fixing the dominant blocker in a bucket routes the same cases into
whatever the next dominant blocker is). Overall bucket totals are
unchanged (218/218 still fail, as expected: none of these cases can
succeed until every remaining legalization/lowering gap in their
shared SPIR-V content is closed), but the specific cause each one hits
has moved strictly forward, confirming real progress. This is tracked
as new roadmap row H6g-b-a-i-a-i.

**`dEQP-VK.draw.*`'s 1957-case `draw_sample.txt` regression sample**
stays byte-identical to every prior row's own recorded totals:

```
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

**0 regressions** (expected: this fix only makes previously-rejected,
spec-conformant `spirv.All`/`spirv.Any` legalize -- it never changes
behavior for any SPIR-V this sample already exercised successfully,
and none of `draw_sample.txt`'s cases exercise a mesh entry point at
all).

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed
no change needed: this is a pure upstream MLIR SPIRVToLLVM
conversion-pattern-coverage fix, touching no `feme`-advertised feature
bit or extension.

**Milestone H6 does not close.** This row's own fix directly clears
the dominant cause it named, but the same re-run that confirms that
also surfaces a new dominant cause in its place --
`feme::cpu::UnsupportedOps` rejecting a register-bound resource
handle, tracked as new roadmap row H6g-b-a-i-a-i -- alongside the
already-tracked H6g-b-b and H6g-b-c.

**Reproducing this row.** Same ICD build and methodology as every
prior mesh-shading row, run from `VK-GL-CTS/run` (relative
shader-source resource paths require it):

```shell
cd /path/to/VK-GL-CTS/run
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  FEME_VULKAN_LOG_CREATION_ERRORS=1 \
  deqp-vk --deqp-caselist-file=<218-case-bucket>.txt \
    --deqp-log-filename=diag_h6g_b_a_i_a.qpa
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-caselist-file=draw_sample.txt \
    --deqp-log-filename=draw_h6g_b_a_i_a.qpa
```

## Roadmap H6g-b-a-i-a-i: measured impact (lower direct struct-typed `StorageBuffer` field/array accesses in `SPIRVResourceLoweringPass`)

**Starting point.** H6g-b-a-i-a's own diagnostic re-run of the exact
218-case `vkCreateGraphicsPipelines`/`vkRefUtil.cpp:37` bucket found a
new dominant single cause: 82 cases hit
`feme::cpu::UnsupportedOps`'s register-bound-resource-handle rejection,
up from a pre-existing, already-out-of-scope 1 once H6g-b-a-i-a's own
`spirv.All`/`spirv.Any` fix let the other 81 cases progress far enough
to reach `feme-cpu`'s resource-handle normalization. This row's own ask
was to isolate which resource-declaration/use shape the CPU target still
failed to normalize, and whether the fix belonged in
`UnsupportedOps`/`RootConstantLowering` or earlier.

**Root cause.** The rejection was only the reporter. The real gap lived
in `feme::cpu::SPIRVResourceLoweringPass`'s all-or-nothing
`collectHandles`/`hasOnlySupportedUses` classification. Importing one of
the real failing fragment shaders from the bucket (a glslang-generated
`layout(set=0,binding=*) std430 readonly buffer { ... }` block with
many fixed-size array members) showed that
`ConvertSPIRVToLLVMPass` had already lowered each SPIR-V
`AccessChain` into exactly:

1. a `llvm.spv.resource.handlefrombinding` for a struct-typed
   `spirv.VulkanBuffer` in SPIR-V storage class `StorageBuffer` (12),
2. a `llvm.spv.resource.getpointer` selecting a **constant struct field**,
   then
3. an ordinary LLVM `getelementptr` chain indexing within that field's
   fixed-size array/vector content before the final load.

`SPIRVResourceLowering.cpp` only accepted a flat direct load/store user
of the `getpointer` result. Any further GEP into the selected field made
the pass reject that handle, and because the pass is all-or-nothing per
function, one such use left every bound resource in the same function
untouched to be rejected later by `UnsupportedOps`. The fix therefore
belongs in `SPIRVResourceLoweringPass` itself, not in the later
`UnsupportedOps`/`RootConstantLowering` reporters.

**Fix.** `SPIRVResourceLowering.cpp` now recognizes direct
struct-typed `spirv.VulkanBuffer` handles in SPIR-V
`StorageBuffer` class as their own kind (distinct from the pre-existing
uniform/root-constant struct path), validates ordinary constant/affine
LLVM `getelementptr` chains rooted at their
`llvm.spv.resource.getpointer` result, and lowers those GEP chains
recursively by folding the computed byte offset into the existing raw
resource byte offset before calling the already-existing
`feme.cpu.resource.load.raw.*` / `store.raw.*` helpers. The negative
shape this row intentionally leaves unsupported -- a **dynamic top-level
field selector** on a direct storage block -- still stays rejected.

**Tests.** New `SPIRVResourceLoweringTest.cpp` coverage:

- `LowersDirectStorageBlockFieldAndNestedArrayAccessToResourceLoad`
- `LowersStructuredStorageBufferFieldAccessToFieldOffset`
- `LeavesDirectStorageBlockDynamicFieldSelectorUnchanged`

and the existing lit regression
`feme/test/Transforms/CPU/spirv-resource-lowering-unsupported.ll`
updated so its `field_access` case now checks for the lowered raw
resource load while the still-unsupported image/sampler and unbounded
descriptor-array cases remain untouched.

```
ninja check-feme (assertions-enabled, ccache build):
Total Discovered Tests: 2012
  Unsupported:   59 (2.93%)
  Passed     : 1953 (97.07%)

Targeted follow-up:
  FeMeTransformsCPUTests / SPIRVResourceLoweringTest:
    3/3 new row-specific tests passing
  FEME :: Transforms/CPU/spirv-resource-lowering-unsupported.ll:
    1/1 passing after updating the existing expectation
```

The discovered-test total rises from H6g-b-a-i-a's 2009 to 2012 by
exactly the 3 new `SPIRVResourceLoweringTest` cases this row adds; the
lit file remains a single discovered test.

**A real ICD re-run of the exact 218-case `vkRefUtil.cpp:37` bucket**
confirms the named blocker is cleared almost completely. H6g-b-a-i-a's
own 82 register-bound-handle rejections drop to **1** (the lone
remainder is a pre-existing sampled-image/sampler combined-handle shape,
already outside this row's storage-buffer scope), so the 81
newly-unblocked storage-buffer cases all progress further. To make the
hand-off explicit, I repeated the rerun with a single combined
stdout/stderr log and classified each case by the **first** emitted
FeMe/MLIR diagnostic when present:

```
| Count | First emitted FeMe/MLIR diagnostic after this row's fix |
|---|---|
| 148 | `feme-cpu-simdize` divergent-vector-value rejection (new dominant cause; tracked as H6g-b-a-i-a-i-a) |
| 15 | `feme-cpu-wrap-mesh-output` metadata-missing rejection |
| 13 | `feme-cpu-linearize` unsupported-control-flow-shape rejection |
| 11 | `spirv.MemoryBarrier` legalization failure |
| 11 | no FeMe/MLIR diagnostic emitted before failure / `deqp-vk`'s own Linux-only device-fault-test abort path |
| 7  | specialization constants unsupported |
| 5  | `Symbols not found` (already tracked as H6g-b-c) |
| 4  | `spirv.AtomicExchange` legalization failure |
| 2  | `feme-cpu-wrap-fragment` metadata-missing rejection |
| 1  | `spirv.EXT.EmitMeshTasks` legalization failure |
| 1  | remaining sampled-image/sampler `UnsupportedOps` rejection |
```

That table totals the full 218 cases. The key point for this row is the
direct before/after on its own named cause:

```
register-bound resource-handle rejections: 82 -> 1
```

-- the same "fix one dominant blocker, expose the next" progression the
earlier H6g-b-a-* rows already established.

**`dEQP-VK.draw.*`'s 1957-case `draw_sample.txt` regression sample**
stays byte-identical to H6g-b-a-i-a's own baseline:

```
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

**0 regressions** -- and the per-case status map is identical to
`draw_h6g_b_a_i_a.qpa`, not only the totals.

`FeMeCPUDesign.md` needs no update: this is a completeness fix inside
the design's already-documented resource-normalization model, not a new
resource model. `Vulkan14FeatureInventory.md` and
`VulkanExtensionInventory.md` also need no update: no advertised Vulkan
feature bit or extension changed.

**Milestone H6 does not close.** This row's own fix clears the dominant
storage-buffer-handle blocker it named, but the same rerun surfaces a
new dominant blocker in its place -- `feme-cpu-simdize` divergent vector
values, tracked as new roadmap row H6g-b-a-i-a-i-a -- alongside the
already-tracked H6g-b-b and H6g-b-c.

**Reproducing this row.** Same ICD build and methodology as every prior
mesh-shading row, run from `VK-GL-CTS/run` (relative shader-source
resource paths require it):

```shell
cd /path/to/VK-GL-CTS/run
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  FEME_VULKAN_LOG_CREATION_ERRORS=1 \
  deqp-vk --deqp-caselist-file=<218-case-bucket>.txt \
    --deqp-log-filename=diag_h6g_b_a_i_a_i.qpa \
    > diag_h6g_b_a_i_a_i_combined.log 2>&1
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-caselist-file=draw_sample.txt \
    --deqp-log-filename=draw_h6g_b_a_i_a_i.qpa
```

## Roadmap H6g-b-a-i-a-i-a: measured impact (`feme.cpu.masked.store.*` divergent-vector-value consumer gap in `SIMDize.cpp`)

**Starting point.** H6g-b-a-i-a-i's own diagnostic re-run of the exact
218-case `vkCreateGraphicsPipelines`/`vkRefUtil.cpp:37` bucket found a
new dominant single cause: 148 cases hit `feme-cpu-simdize`'s "function
'main' has a divergent vector value ... used outside a supported
insertelement-chain/resource-store/extractelement/select/shufflevector/
phi/elementwise pattern" rejection, up from that row's own baseline,
once its direct-storage-buffer-handle fix let those shaders progress
far enough to reach `SIMDizePass`'s own decomposition precondition
check. This row's own ask was to reduce a real failing shader down to
the exact divergent-vector use shape still outside
`checkVectorDecompositionSupported`'s accepted patterns, and to decide
whether the fix belonged in `SIMDize.cpp` itself or an earlier
canonicalization/legalization pass.

**Root cause.** Temporarily instrumenting `checkVectorDecompositionSupported`'s
own rejection point to dump the offending instruction and its users,
then re-running a single representative failing case
(`dEQP-VK.mesh_shader.ext.api.draw.draw_count_0...no_task_shader`)
directly against the real ICD, isolated the exact IR shape: a mesh
entry point's own `gl_PrimitiveTriangleIndicesEXT[col] = uvec3(indices.x,
indices.y, indices.z)` write. Unlike an ordinary per-vertex/per-primitive
output element, `PrimitiveIndices` has no canonicalized `feme.stage.*`
op of its own yet (documented in `MeshOutputWrapper.h`'s own file
comment), so it never becomes a `feme.cpu.resource.*`/masked-output-store
call the way other mesh outputs do -- it survives all the way to
`feme::cpu::LinearizePass` as an ordinary LLVM `store` of a divergent
`<3 x i32>` value, which `LinearizePass` masks into a
`feme.cpu.masked.store.v3i32` call under divergent control flow exactly
like any other non-resource, non-groupshared store.

That call type was never taught to `checkVectorDecompositionSupported`
as an accepted consumer of a decomposed divergent vector value (only a
matched `feme.cpu.resource.*` store's stored-value operand was). Worse,
`FunctionWidener::widenMaskedStore` itself was unconditionally calling
the scalar-only `getWidened` helper on its stored-value operand; for a
vector-typed, already-decomposed value (tracked only in
`WidenedVectorComponents`, not `Widened`), that fell through to
`getWidened`'s generic uniform-broadcast fallback and attempted to
build an illegal `<W x <3 x i32>>` vector-of-vectors (`llvm.masked.scatter`
has no vector-of-vector-element form to represent a per-lane vector
value at all). `checkVectorDecompositionSupported`'s own strict,
enumerate-every-accepted-shape design meant this reached a clean
diagnostic rather than the illegal-type assertion the underlying
`widenMaskedStore` bug would otherwise have caused -- confirming, once
again, that the precondition checker is doing its job as designed. The
fix therefore belongs in `SIMDize.cpp` itself, not an earlier pass:
both the consumer-acceptance gap and the `widenMaskedStore` widening bug
live there.

**Fix.** `checkVectorDecompositionSupported` now accepts a matched
`feme.cpu.masked.store.*` call's stored-value operand as a supported
consumer of a decomposed divergent vector, the same way it already
accepted a matched resource-store call's. `FunctionWidener::widenMaskedStore`
now recognizes a vector-typed stored value, reassembles each lane's own
`<3 x i32>` from the decomposed per-component wide values via
`getVectorComponents` plus per-lane extractelement/insertelement
(mirroring `widenResourceCall`'s equivalent per-lane reassembly), and
writes each lane individually with a load-select-store idiom (mirroring
`MeshOutputWrapper.cpp`'s own `lowerMeshOutputStore`) instead of
`llvm.masked.scatter`. The scalar/pointer-typed value path is unchanged.

**Tests.** New lit regression
`feme/test/Transforms/CPU/simdize-masked-memop-vector-divergent.ll`
(a divergent-control-flow IR reduction storing an insertelement-chain-built
`<3 x i32>` to a non-groupshared pointer, verified through
`feme-cpu-linearize,feme-cpu-simdize`) and new unit test
`SIMDizeTest.DecomposesInsertElementChainIntoMaskedVectorStore`
(asserting no nested vector types appear and exactly 4 real `<3 x i32>`
stores are produced for a 4-lane wave).

```
ninja check-feme (assertions-enabled, ccache build):
Total Discovered Tests: 2014
  Unsupported:   59 (2.93%)
  Passed     : 1955 (97.07%)
```

The discovered-test total rises from H6g-b-a-i-a-i's 2012 to 2014 by
exactly the 2 new tests this row adds (1 lit + 1 unit); 0 regressions.

**A real ICD re-run of the exact 218-case `vkRefUtil.cpp:37` bucket**
(`bucket_refutil37_h6g_b_a_i_a.txt`, unchanged from the prior row) with
a single combined stdout/stderr diagnostic log confirms the fix
directly:

```
| Count | First emitted FeMe/MLIR diagnostic after this row's fix |
|---|---|
| 83 | `feme-cpu-wrap-mesh-output` metadata-missing rejection (up from 15; +68) |
| 80 | `feme-cpu-simdize` divergent-vector-value rejection (down from 148; new dominant cause is a vector `fcmp`/`icmp` comparison consumer, tracked as H6g-b-a-i-a-i-b) |
| 13 | `feme-cpu-linearize` unsupported-control-flow-shape rejection (unchanged) |
| 11 | `spirv.MemoryBarrier` legalization failure (unchanged) |
| 11 | no FeMe/MLIR diagnostic emitted before failure / `deqp-vk`'s own Linux-only device-fault-test abort path (unchanged) |
| 7  | specialization constants unsupported (unchanged) |
| 7  | rasterizer/depth-stencil state mismatch (unchanged) |
| 5  | `Symbols not found` (already tracked as H6g-b-c, unchanged) |
| 4  | `spirv.AtomicExchange` legalization failure (unchanged) |
| 3  | color-blend-state mismatch (unchanged) |
| 2  | `feme-cpu-wrap-fragment` metadata-missing rejection (unchanged) |
| 1  | `spirv.EXT.EmitMeshTasks` legalization failure (unchanged) |
| 1  | remaining sampled-image/sampler `UnsupportedOps` rejection (unchanged) |
```

That table totals the full 218 cases. Every bucket other than this
row's own named cause and its direct hand-off target
(`feme-cpu-wrap-mesh-output`) is bit-for-bit unchanged from
H6g-b-a-i-a-i's own table, confirming a clean, isolated fix:

```
feme-cpu-simdize divergent-vector-value rejections: 148 -> 80
feme-cpu-wrap-mesh-output rejections:                15 -> 83
```

**An unplanned but verified side effect: H6g-b-b also closes.** A full
`dEQP-VK.mesh_shader.*` re-run (28044 leaf cases, same resume-loop
methodology as every prior full-group row) completed in a single clean
iteration with **0** cases needing blacklisting, compared to the 14
cases (and 15 resume-loop iterations) H6g-b-b's own row required. Its
crash (`FunctionWidener::widenMaskedStore` -> `getWidened` ->
`ConstantVector::getSplat`'s `isValidElementType` assertion) is the
exact same code path this row's fix touches: that assertion rejects a
vector *element* type exactly the way it would reject a genuine
struct/array element type, so H6g-b-b's "aggregate (non-scalar,
non-pointer) element type" description was an imprecise characterization
of the same vector-of-vector-element shape this row root-caused, not a
distinct genuine-aggregate bug -- no aggregate-typed `StoredValue` was
ever isolated in H6g-b-b's own investigation or in this row's. Each of
H6g-b-b's own 14 previously-crashing cases (`barrier_in_task`,
`group_memory_barrier_in_task_{array,float,struct,uint64,vector}`,
`memory_barrier_shared_in_task_{array,float,struct,uint64,vector}`,
`emit_in_control_flow`, `emit_in_control_flow_bad_emit_last`,
`payload_not_accessed`) was confirmed individually landing in this same
`vkRefUtil.cpp:37` bucket with an ordinary, non-crashing diagnostic
instead (spot-checked `payload_not_accessed`: now cleanly hits
`feme-cpu-wrap-mesh-output`, no crash, no `gdb` needed) -- the full-run
bucket accordingly grows from 218 to **232** cases (218 + these 14),
confirmed by diffing the two bucket case-name lists directly rather than
assumed from the count alone. See H6g-b-b's own updated roadmap row.

**`dEQP-VK.draw.*`'s 1957-case `draw_sample.txt` regression sample**
stays byte-identical to every prior row's baseline:

```
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

**0 regressions.**

`FeMeCPUDesign.md` needs no update beyond a small clarifying note added
to its "Phase 4: Widening" section documenting the newly-supported
masked-store consumer shape (a completeness fix within the design's
already-documented "divergent vectors become per-lane components"
decomposition model, not a new widening model). `Vulkan14FeatureInventory.md`
and `VulkanExtensionInventory.md` need no update: no advertised Vulkan
feature bit or extension changed -- this is a pure CPU-target `SIMDize`
widening completeness fix.

**Milestone H6 does not close.** This row's own fix clears its named
`feme-cpu-simdize` masked-store blocker and incidentally closes H6g-b-b,
but the same rerun surfaces a new dominant blocker in its place -- a
divergent vector value used as an `fcmp`/`icmp` comparison operand,
tracked as new roadmap row H6g-b-a-i-a-i-b -- alongside the
already-tracked H6g-b-c.

**Reproducing this row.** Same ICD build and methodology as every prior
mesh-shading row, run from `VK-GL-CTS/run` (relative shader-source
resource paths require it):

```shell
cd /path/to/VK-GL-CTS/run
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  FEME_VULKAN_LOG_CREATION_ERRORS=1 \
  deqp-vk --deqp-caselist-file=bucket_refutil37_h6g_b_a_i_a.txt \
    --deqp-log-filename=diag_h6g_b_a_i_a_i_a.qpa \
    > diag_h6g_b_a_i_a_i_a_combined.log 2>&1
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  deqp-vk --deqp-caselist-file=draw_sample.txt \
    --deqp-log-filename=draw_h6g_b_a_i_a_i_a.qpa
python3 resume_run_h6g_b_a_i_a_i_a.py  # full dEQP-VK.mesh_shader.* group
```

## Roadmap H6g-b-a-i-a-i-b: measured impact (`fcmp`/`icmp`, `llvm.vector.reduce.*`, and vectorizable-intrinsic producer/consumer gaps in `SIMDize.cpp`)

**Starting point.** H6g-b-a-i-a-i-a's own real-ICD re-run of the exact
218-case `vkRefUtil.cpp:37` bucket found 80 cases hitting
`feme-cpu-simdize`'s "used outside a supported ... pattern" rejection
as the new dominant cause, once that row's masked-store fix let those
shaders progress further. This row's own ask, per its roadmap text, was
to reduce a real failing shader (named as
`dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_0.mesh_only`)
down to the exact IR shape and decide whether the fix was a narrow
`fcmp`/`icmp` consumer-and-producer addition or the broader per-lane
`<N x i1>` `select`-condition decomposition the design doc's own
existing deviation note anticipated.

**Fix 1 (`fcmp`/`icmp` + `select`-condition, `dc7fe7b15f2f`).** Reducing
the named case down to its IR confirmed the row's own cited shape
exactly: `%8 = insertelement <4 x float> %6, float %7, i64 3` used by
`%16 = fcmp ole <4 x float> %8, %15`. Added `fcmp`/`icmp` (`CmpInst`) as
a supported vector producer (its `<N x i1>` result decomposes into `N`
per-component `<W x i1>` comparisons like ordinary elementwise
arithmetic) and consumer, and generalized `select`'s condition to accept
either a shared scalar `i1` or a per-lane `<N x i1>` vector (decomposed
into `N` widened components in `widenVectorSelect`, one used per
per-component `select`) -- closing the design doc's own "a `select`
with a per-lane `<N x i1>` condition remains diagnosed" deviation note
in the same commit. New lit test `simdize-vector-fcmp-select.ll`,
updated `simdize-vector-select.ll`/`simdize-vector-unsupported.ll`, unit
test `SIMDizeTest.DecomposesVectorComparisonIntoPerLaneSelectCondition`
(`bf6a561b0531`).

**This fix alone was necessary but not sufficient.** Re-running the
same real named case against the real `deqp-vk`/`feme` Vulkan ICD
(`FEME_VULKAN_LOG_CREATION_ERRORS=1`) still failed with the same class
of `feme-cpu-simdize` diagnostic. Temporarily instrumenting
`checkVectorDecompositionSupported`'s own rejection points with a
one-off diagnostic dump (the same technique H6g-b-a-i-a-i-a used) found
the *real* rejected shape was not a `select` at all:

```
%16 = fcmp ole <4 x float> %8, %15
%17 = call i1 @llvm.vector.reduce.and.v4i1(<4 x i1> %16)
```

-- glslang's own lowering of GLSL's `all(lessThanEqual(a, b))` builtin,
an `llvm.vector.reduce.*` intrinsic call folding a divergent vector's
components together, a shape `checkVectorDecompositionSupported` did
not accept as a consumer at all.

**Fix 2 (`llvm.vector.reduce.*` consumer, `eaf8216ce1f4`).** Added
`isSupportedVectorReduceIntrinsic` (accepting the integer/comparison
reduces `and`/`or`/`xor`/`add`/`mul`/`smax`/`smin`/`umax`/`umin`; the
floating-point reduces `fadd`/`fmul`/`fmax`/`fmin` are excluded as out
of scope/unneeded -- none of this row's real cases exercise them, and
`fadd`/`fmul`'s extra scalar `start` operand would complicate the fold)
and `widenVectorReduce`, which folds a (possibly divergent,
possibly-decomposed) vector operand's `N` components together two at a
time with the matching op, landing the result -- itself not
vector-typed, since a reduce always returns its operand's scalar
element type -- in the ordinary `Widened` map rather than
`WidenedVectorComponents`. New lit test `simdize-vector-reduce.ll`, unit
test `SIMDizeTest.DecomposesVectorComparisonIntoReduceAnd`.

**Still not sufficient.** Re-running the same real named case again
still failed, with a *third* new blocker found via the same
diagnostic-dump technique:

```
%31 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(...)
%33 = call <4 x float> @llvm.minnum.v4f32(<4 x float> %31, <4 x float> %32)
```

-- a GLSL `min()`/`max()`/`clamp()` builtin lowering to
`llvm.minnum`/`llvm.maxnum`/`llvm.smin`/`llvm.smax` intrinsic calls over
an already-decomposed divergent vector operand: `widenVectorElementwise`
only special-cased `BinaryOperator`/`UnaryOperator`/`CastInst`/`CmpInst`
producers, not an arbitrary elementwise-vectorizable `CallInst`.

**Fix 3 (homogeneous vectorizable-intrinsic producer/consumer,
`eaf8216ce1f4`).** Generalized `checkVectorDecompositionSupported`,
`widenVectorElementwise` (now also dispatching on `CallInst`, using
`I.args()` in place of `I.operands()` since a call's own operand list
also includes its callee), and `widenInstruction`'s dispatch to accept
any vector-typed, homogeneous "trivially vectorizable" intrinsic call
(`isElementwiseVectorizableIntrinsic`, already used elsewhere for the
uniform-broadcast case) as both a producer and a consumer over a
decomposed operand, decomposing it exactly like ordinary elementwise
arithmetic: one scalar-element intrinsic call per component, each
keeping the widened `<W x elemT>` overload. New lit test
`simdize-vector-intrinsic.ll`, unit test
`SIMDizeTest.DecomposesHomogeneousVectorizableIntrinsicCall`
(`08c1e2b9d4e5`/`3cb7e072fda2`).

**Tests.**

```
ninja check-feme (assertions-enabled, ccache build):
Total Discovered Tests: 2020
  Unsupported:   59 (2.92%)
  Passed     : 1961 (97.08%)
```

The discovered-test total rises from H6g-b-a-i-a-i-a's 2014 to 2020 by
exactly the 6 new tests these three fixes add across all of them (3 lit
+ 3 unit); 0 regressions.

**A real ICD re-run confirms this row's own SIMDize-level fix is
complete.** With all three fixes landed, re-running the same single
named case (`dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_0.mesh_only`)
against the real `deqp-vk`/`feme` Vulkan ICD now gets past
`feme-cpu-simdize` entirely -- it fails only at
`vkCreateGraphicsPipelines` time on an unrelated JIT-link error:

```
JIT session error: Symbols not found: [ feme.cpu.resource.load.raw.v2f32, feme.cpu.resource.load.raw.v3i32, feme.cpu.resource.load.raw.v3f32 ]
vkCreateGraphicsPipelines: Failed to materialize symbols: { (main, { feme.cpu.resource.load.raw.i32, feme_cpu_entry_main, feme.cpu.resource.load.raw.v4f32, feme.cpu.resource.load.raw.f32 }) }
```

Given the group's size, this row's real-CTS validation used the
tractable `dEQP-VK.mesh_shader.ext.in_out.*` subgroup (560 cases, the
row's own cited 80/218-case bucket, extracted from `dEQP-VK-cases.txt`
via `--deqp-caselist-file`) rather than a full 218-case
`vkRefUtil.cpp:37`-bucket or full `dEQP-VK.mesh_shader.*` (28044-case)
re-run:

```
Passed:        0/560 (0.0%)
Failed:        80/560 (14.3%)
Not supported: 480/560 (85.7%)
```

Unchanged from H6g-b-a-i-a-i-a's own 80-case count -- but spot-checking
several distinct failing cases across the bucket (not just the one
named case) confirms every one now hits the same new
`feme.cpu.resource.load.raw.v2f32`/`v3f32`/`v2i32`/`v3i32`
missing-JIT-symbol error instead of `feme-cpu-simdize`'s diagnostic,
confirming the bucket's dominant blocker has moved entirely, not just
for the one reduced case. Root-causing that new blocker
(`feme/runtime/CPU/FeMeRuntimeCPU.c` only defines the scalar and
full-`<4 x T>`-width `feme.cpu.resource.load.raw.*` overloads, not the
2-/3-component ones a `vec2`/`vec3`/`ivec2`/`ivec3` mesh-shader
input/output needs) is a different subsystem entirely (JIT runtime
symbol registration, not `SIMDizePass` decomposition) and is out of
scope for this row -- tracked as new roadmap row H6g-b-a-i-a-i-c.

`FeMeCPUDesign.md` updated (`13051214ea77`): Phase 4's decomposition
deviation bullet now documents all nine producer shapes (was six before
this row) and the reduce/vectorizable-intrinsic consumer shapes, and
retires the now-resolved "a `select` with a per-lane `<N x i1>`
condition remains diagnosed" deviation note this row's own Fix 1 closed.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: a `SIMDizePass`-completeness fix within its existing
"vectors become components" decomposition scope, not a new feature or
extension surface.

**Milestone H6 does not close.** This row's own three fixes land and
are confirmed complete at the `SIMDizePass` level, but the same
real-ICD re-run surfaces H6g-b-a-i-a-i-c as the bucket's new dominant
blocker, alongside the already-tracked H6g-b-c.

**Reproducing this row.** Same ICD build as every prior mesh-shading
row:

```shell
cd /path/to/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  FEME_VULKAN_LOG_CREATION_ERRORS=1 \
  ./deqp-vk --deqp-case=dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_0.mesh_only \
    --deqp-log-filename=single_h6g_b_a_i_a_i_b.qpa
grep "^TEST: dEQP-VK.mesh_shader.ext.in_out\." dEQP-VK-cases.txt | sed 's/^TEST: //' > cases_h6g_b_a_i_a_i_b.txt
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=cases_h6g_b_a_i_a_i_b.txt \
    --deqp-log-filename=in_out_h6g_b_a_i_a_i_b.qpa \
    --deqp-log-images=disable --deqp-log-shader-sources=disable
```

## Roadmap H6g-b-a-i-a-i-c: measured impact (missing `feme.cpu.resource.load.raw.v2f32`/`v3f32`/`v2i32`/`v3i32`/`v4i32` runtime overloads)

**Starting point.** H6g-b-a-i-a-i-b's own real-ICD re-run of the named
`dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_0.mesh_only`
case, and the full 560-case `dEQP-VK.mesh_shader.ext.in_out.*` bucket,
found all 80 previously-`feme-cpu-simdize`-blocked cases now failing at
`vkCreateGraphicsPipelines` time with a JIT-link "Symbols not found"
error naming `feme.cpu.resource.load.raw.v2f32`/`v3f32`/`v3i32`/`v2i32`/
`v4i32`. This row's own ask was to root-cause and fix that gap.

**Root cause.** `feme/runtime/CPU/FeMeRuntimeCPU.c` defined only the
scalar (`.i32`/`.f32`) and full-`<4 x T>`-width (`.v4f32`) raw-buffer
overloads; the 2- and 3-component overloads a `vec2`/`vec3`/`ivec2`/
`ivec3` mesh-shader input/output needs (and, it turned out, `.v4i32`
too -- missing on both the load and store side, not just load as the
row's own initial text guessed) were simply absent. `feme::cpu::
ResourceCalls`/`ResourceLowering.cpp` needed no changes at all:
`mangleResourceCallName` mangles any `FixedVectorType` generically by
element type and width, so the compiler side already emitted calls to
these names for every vector width; only the runtime definitions were
missing.

**Fix.** Added 10 new `asm`-labeled functions to `FeMeRuntimeCPU.c`:
raw load and store for `v2f32`, `v3f32`, `v2i32`, `v3i32`, and `v4i32`,
each mirroring the existing `v4f32` bindless-descriptor-lookup-then-
masked-load/store shape exactly (new `FemeRTv2f32`/`FemeRTv3f32`/
`FemeRTv2i32`/`FemeRTv3i32` vector typedefs, plus `...Unaligned`
variants, alongside the pre-existing `FemeRTv4f32`/`v4i32` ones).

**A related, real bug found and fixed while adding the v3-wide
stores.** Clang's C ABI coerces a by-value `<3 x float>`/`<3 x i32>`
parameter into a `<4 x i32>` register pair for the *function's own
compiled definition*, and pads `sizeof()` to 16 (the next power of two
above the 12 logical bytes). Writing the store body the same way the
existing `v4f32`/`v4i32` functions do -- `*(Unaligned *)Ptr = Value;`,
or even `__builtin_memcpy(Ptr, &Value, sizeof(Value))` -- both compile
to a widened, out-of-bounds 16-byte `store <4 x T>` instead of the
intended 12-byte one, verified directly via `opt -S` on the compiled
bitcode. Fixed with `__builtin_memcpy(Ptr, &Value, 12)`, an explicit
*literal* byte count (not `sizeof`), which produces an exact 12-byte
store. The load side has no equivalent issue: a return value isn't
ABI-coerced by Clang the same way a parameter is, so `load <3 x T>`
already emits an exact 12-byte read.

**Confirmed the ABI coercion mismatch does not affect the real
production path.** The runtime's own *compiled definition* sees the
coerced `<4 x i32>` parameter type, but the real caller
(`feme::cpu::ResourceCalls::createRawStore`, reached via
`ResourceLoweringPass`) declares and calls with the canonical,
uncoerced `<3 x T>` type, and that declaration exists before
`Linker::linkInModule` merges in the runtime bitcode
(`Pipeline.cpp`/`CompiledStage.cpp`, `Linker::Flags::LinkOnlyNeeded`).
Verified via `llvm-link` + `opt -passes=verify` that LLVM's linker/
verifier does not catch this signature mismatch (opaque-pointer call
sites are never re-validated against the callee's actual
`FunctionType` after RAUW), and wrote a temporary probe test mimicking
the real production flow end-to-end (declare canonical type in a
caller module, link in the runtime bitcode, execute via MCJIT) that
round-trips correctly on this host -- confirming the real pipeline is
safe, and this ABI quirk only mattered for the runtime's own C source
correctness, not the JIT-linked shader's.

**Tests.** Added 5 new `TEST_F(RuntimeCPUTest, RawLoadStoreRoundTrip{
V2F32,V3F32,V2I32,V3I32,V4I32})` round-trip tests to
`RuntimeCPUTest.cpp`, generalizing its pre-existing `addStoreWrapper`
helper to detect when the runtime `Function`'s actual (ABI-coerced)
parameter type differs from the logical vector type and adapt
(shufflevector-widen + bitcast) before calling, mirroring what a real
coerced C call site does -- transparent for the pre-existing v2/v4
tests (no coercion there), necessary for the new v3 ones. Added 10 new
`CHECK-DAG` lines to `runtime-cpu-bitcode.test` for the new symbol
names.

```
ninja check-feme (assertions-enabled, ccache build):
Total Discovered Tests: 2025
  Unsupported:   59 (2.91%)
  Passed     : 1966 (97.09%)
```

Rises from H6g-b-a-i-a-i-b's own 2020/1961 by exactly the 5 new tests
this row adds; 0 regressions.

**A real ICD re-run confirms the exact reported blocker is gone.**
Re-running this row's own named case
(`dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_0.mesh_only`)
against the real `deqp-vk`/`feme` Vulkan ICD
(`FEME_VULKAN_LOG_CREATION_ERRORS=1`) no longer hits the reported JIT
symbol gap at all; it now fails at a different, unrelated point:

```
error: feme-cpu-wrap-mesh-output: unexpected stage op left for the mesh output wrapper
  Fail (vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED at vkRefUtil.cpp:37)
```

Re-running the full 560-case `dEQP-VK.mesh_shader.ext.in_out.*` bucket
(same caselist source and technique as H6g-b-a-i-a-i-b):

```
Passed:        0/560 (0.0%)
Failed:        80/560 (14.3%)
Not supported: 480/560 (85.7%)
```

Unchanged from H6g-b-a-i-a-i-b's own 80-case count, but spot-checking
every one of the 80 failures (not just the one named case) by grepping
the run's combined log for both the resolved error string and every
remaining error line confirms none of the 80 hit "Symbols not found"
naming any `feme.cpu.resource.load.raw.*` name any more -- the specific
blocker this row targeted is fully resolved for the whole bucket, not
just the reduced case. The 80 split cleanly into two new causes,
neither of which is this row's own concern:

```
     40 feme-cpu-wrap-mesh-output: unexpected stage op left for the mesh output wrapper
     40 JIT session error: Symbols not found: [ spirv_var_NN ]  (NN varies per case)
```

The 40 `spirv_var_NN` cases are not a new blocker: they match
H6g-b-c's own already-tracked description exactly (a mesh entry's
unresolved arrayed-builtin-block access survives, uncanonicalized, all
the way to the JIT because `ValidateStagePass::run` still does not
validate `ShaderStage::Mesh`) -- this row's own re-run is further
confirmation of that row's scope, not a new one. The other 40, a new
`feme-cpu-wrap-mesh-output` diagnostic from
`MeshOutputWrapperPass::lowerMeshStageOps`'s catch-all rejection of any
surviving `feme.stage.*`/masked-output-store call that is neither
`OutputStore` nor `SetMeshOutputs`, is genuinely new and is out of scope
for this row -- tracked as new roadmap row H6g-b-d.

`FeMeCPUDesign.md` checked: the "Runtime Support Library" section
describes the runtime as providing "descriptor lookup ... for every
supported format" generically, without enumerating specific vector
widths, so it already accurately describes this row's fix with no
wording change needed. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` confirmed no change needed: `VK_EXT_mesh_
shader` was already "Advertised" (roadmap H6f) before and after this
fix, since this row closes a runtime-symbol gap within the extension's
existing scope rather than changing what is advertised.

**Milestone H6 does not close.** This row's own fix lands and is
confirmed complete for the exact symbol gap it targeted, but the same
real-ICD re-run splits the bucket's 80 cases across two other blockers:
the already-tracked H6g-b-c, and the newly discovered H6g-b-d.

**Reproducing this row.** Same ICD build as every prior mesh-shading
row:

```shell
cd /path/to/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  FEME_VULKAN_LOG_CREATION_ERRORS=1 \
  ./deqp-vk --deqp-case=dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_0.mesh_only \
    --deqp-log-filename=single_h6g_b_a_i_a_i_c.qpa
grep "^TEST: dEQP-VK.mesh_shader.ext.in_out\." dEQP-VK-cases.txt | sed 's/^TEST: //' > cases_h6g_b_a_i_a_i_c.txt
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=cases_h6g_b_a_i_a_i_c.txt \
    --deqp-log-filename=in_out_h6g_b_a_i_a_i_c.qpa \
    --deqp-log-images=disable --deqp-log-shader-sources=disable
```

## Roadmap H6g-b-d: measured impact (`MeshOutputWrapperPass::lowerMeshStageOps` catch-all too broad)

**Starting point.** H6g-b-a-i-a-i-c's own real-ICD re-run of the 560-case
`dEQP-VK.mesh_shader.ext.in_out.*` bucket found the 80 previously-JIT-
symbol-blocked cases split evenly: 40 hitting the already-tracked
H6g-b-c `spirv_var_NN` gap, and 40 hitting a new
`feme-cpu-wrap-mesh-output: unexpected stage op left for the mesh
output wrapper` diagnostic from `MeshOutputWrapperPass::
lowerMeshStageOps`'s catch-all. This row's own ask was to find the real
IR shape reaching that catch-all and fix it (in `MeshOutputWrapperPass`
itself or upstream), rather than assume either of the two shapes the
row's own filing text guessed at (an unmasked `feme.stage.output.store`,
or an illegal `StageOpKind` like `InputLoad`).

**Investigation.** Reasoned through the CPU pipeline's own ordering
first (`ResourceLoweringPass` -> `LinearizePass` -> `SIMDizePass` ->
`WaveLoweringPass` -> `MeshOutputWrapperPass`, per `Pipeline.cpp`) to
rule out both guesses on paper: every `feme.stage.output.store`/
`feme.stage.set_mesh_outputs` call is created upstream of
`LinearizePass` (`CanonicalizeStage.cpp`, or directly at SPIR-V import
for `SetMeshOutputsEXT`), and `LinearizePass::run`'s own closing
`hasStageMaskOps(F)` check already diagnoses (with a distinctly
different, `feme-cpu-linearize`-prefixed message) any mask-affecting
stage op that survives its own `DiamondFlattener`/`LoopLinearizer`
lowering unmasked -- so an unmasked `OutputStore` should never reach
`MeshOutputWrapperPass` silently. Read the real GLSL these tests
generate (`vktMeshShaderInOutTestsEXT.cpp`'s `IfaceVar::
getAssignmentStatement`) and confirmed it is fully unrolled at
shader-generation time (no shader-side loop at all), rejecting a
loop-masking-gap theory too. Rather than keep reasoning from first
principles, used the same one-off diagnostic-dump-and-single-case-rerun
technique H6g-b-a-i-a-i-a/-b/-c used: added a temporary `errs()` print
of the offending `CallInst` directly in `lowerMeshStageOps`'s own
catch-all, rebuilt `libfeme_vulkan.so`, and re-ran this row's own named
case (`dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_0.
mesh_only`) against the real ICD with
`FEME_VULKAN_LOG_CREATION_ERRORS=1`:

```
DEBUG unexpected stage op:   %3409 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 416, i1 true)
```

Neither guess: the offending call is an entirely ordinary, already-
correctly-lowered storage-buffer read (`pvd.name[i]`/`ppd.name[i]` in
the real GLSL, feeding the mesh output store's own value) -- not a
`feme.stage.*` op at all.

**Root cause.** The bug was in `lowerMeshStageOps`'s own per-instruction
loop, not anywhere upstream. Its `UsesStageOps` gate at the top of the
function correctly only requires *some* call in `F` to need this pass's
attention before proceeding (an `OutputStore`/`SetMeshOutputs`/masked
variant), matching the function's own comment. But the loop that
follows then treated *every remaining `CallInst` in the whole function*
as needing to be one of the two shapes it lowers (`isMaskedOutputStoreCall`/
`isMaskedSetMeshOutputsCall`), unconditionally erroring on anything else
-- including calls with nothing to do with stage ops at all (resource
loads/stores, or any other intrinsic), which every earlier phase had
already correctly left untouched. Any mesh entry that both stores an
output *and* reads a resource to compute that output's value -- which is
every case in this bucket -- hit this unconditionally.

**Fix.** `feme/lib/Transforms/CPU/MeshOutputWrapper.cpp`: the catch-all
now only fires when the surviving call actually `isStageOpCall`,
matching the function's own documented contract ("diagnoses ... if F
uses a `feme.stage.*` op this pass does not support") for the first
time; any other call falls through to `continue`, left alone.

**Tests.** Added `MeshOutputWrapperTest.
LeavesUnrelatedResourceLoadCallAlone` (`MeshOutputWrapperTest.cpp`): a
mesh entry mixing a `feme.cpu.resource.load.raw.f32` call with an
ordinary per-vertex output store, run through the real
`LinearizePass`/`SIMDizePass`/`WaveLoweringPass`/`MeshOutputWrapperPass`
chain. Confirmed to fail (asserting `SawError` via a diagnostic-handler
callback, the same pattern `LinearizeTest.cpp`/`SIMDizeTest.cpp` already
use) without this fix, and pass with it -- verified both ways by
temporarily reverting the fix and re-running the test in isolation.

```
ninja check-feme (assertions-enabled, ccache build):
Total Discovered Tests: 2026
  Unsupported:   59 (2.91%)
  Passed     : 1967 (97.09%)
```

Rises from H6g-b-a-i-a-i-c's own 2025/1966 by exactly the 1 new test
this row adds; 0 regressions.

**A real ICD re-run confirms the fix, and finds a new bug in its
place.** Re-running this row's own named case: the
`feme-cpu-wrap-mesh-output` diagnostic is gone entirely; it now
progresses to `vkQueueSubmit` instead:

```
vkQueueSubmit: vertex output and fragment input at location 0 disagree on component/row count or type
  Fail (vk.queueSubmit(queue, 1u, &submitInfo, *fence): VK_ERROR_INITIALIZATION_FAILED at vkCmdUtil.cpp:338)
```

Re-running the full 560-case `dEQP-VK.mesh_shader.ext.in_out.*` bucket
(same caselist source and technique as every prior row in this chain):

```
Passed:        0/560 (0.0%)
Failed:        80/560 (14.3%)
Not supported: 480/560 (85.7%)
```

Unchanged from H6g-b-a-i-a-i-c's own 80-case count, but grepping the
full run's combined log confirms zero remaining occurrences of
`feme-cpu-wrap-mesh-output: unexpected stage op left for the mesh
output wrapper` across all 80 failures, not just the one named case --
this row's own targeted diagnostic is fully resolved bucket-wide. The
80 failures split the same two ways H6g-b-a-i-a-i-c's own re-run
already found, just with this row's own 40 now further along:

```
     40 JIT session error: Symbols not found: [ spirv_var_NN ]  (NN varies per case; already-tracked H6g-b-c)
     40 vk.queueSubmit(...): VK_ERROR_INITIALIZATION_FAILED (vertex output and fragment input at location 0 disagree)
```

The 40 `spirv_var_NN` cases are confirmed unchanged (still H6g-b-c's own
already-tracked bucket, not a duplicate). The other 40 are a genuinely
new bug, out of this row's own scope -- filed as a new roadmap row one
level under H6 (not nested any deeper under H6g-b, per the standing
instruction against nesting milestone IDs more than one lowercase
letter deep going forward), H6j.

`FeMeCPUDesign.md` checked: `MeshOutputWrapperPass`'s own described
scope ("lowers every masked mesh output store ... in a mesh entry")
already accurately describes what this row restores, with no wording
change needed -- this is a bug-fix to an over-broad rejection, not a
scope change. `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`
confirmed no change needed: a pure CPU-lowering-pass fix within
`VK_EXT_mesh_shader`'s already-advertised scope, touching no feature
bit or extension.

**Milestone H6 does not close.** This row's own fix lands and is
confirmed complete for the exact diagnostic it targeted, but the same
real-ICD re-run splits the bucket's 40 newly-unblocked cases into a new
blocker: H6j.

**Reproducing this row.** Same ICD build as every prior mesh-shading
row:

```shell
cd /path/to/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  FEME_VULKAN_LOG_CREATION_ERRORS=1 \
  ./deqp-vk --deqp-case=dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_0.mesh_only \
    --deqp-log-filename=single_h6g_b_d.qpa
grep "^TEST: dEQP-VK.mesh_shader.ext.in_out\." dEQP-VK-cases.txt | sed 's/^TEST: //' > cases_h6g_b_d.txt
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=cases_h6g_b_d.txt \
    --deqp-log-filename=in_out_h6g_b_d.qpa \
    --deqp-log-images=disable --deqp-log-shader-sources=disable
```

## Roadmap H6j: measured impact (`CanonicalizeStage.cpp` mesh-output `RowCount` reflection)

H6g-b-d's own real-ICD re-run left this row a single, specific, named
diagnostic to root-cause: 40 of the 80 `dEQP-VK.mesh_shader.ext.in_out.*`
bucket's failures reached `vkQueueSubmit` and failed there with:

```
vkQueueSubmit: vertex output and fragment input at location 0 disagree on component/row count or type
  Fail (vk.queueSubmit(queue, 1u, &submitInfo, *fence): VK_ERROR_INITIALIZATION_FAILED at vkCmdUtil.cpp:338)
```

**Root cause, found by direct code inspection (no reduction technique
needed this time -- both sides of the check are read directly out of
`CanonicalizeStage.cpp`/`Executor.cpp`/`GraphicsPipeline.cpp`).** Neither
of the two byproducts this row's own text speculated about
(`MeshOutputWrapperPass`/`EntryWrapperPass`) touches a `SignatureElement`'s
`RowCount` at all -- both operate purely on already-canonicalized
`feme.stage.*` calls, well after signature reflection has already run. The
actual bug is in `CanonicalizeStage.cpp`'s own `addElements` lambda: a mesh
entry's own plain (non-block) per-vertex/per-primitive `Output` global
(e.g. `layout(location=0) out vec4 v_color[];`, the shape
`vktMeshShaderInOutTestsEXT.cpp` actually generates) was reflected the same
way roadmap H5f's `Input`-side treatment reflects a geometry entry's
`gl_in[]`-shaped varying: the whole declared type -- outer per-vertex array
dimension included -- becomes `RowCount` via `getStageIORowShape`, with
`SignatureElement::RowCountIsVertexArray` flagging that the dimension is a
per-vertex array's own extent rather than a real matrix's row count. That
flag-and-fold treatment is safe for `Input` because nothing downstream
ever links an `Input` element's `RowCount` against another stage's one --
but a mesh entry's `Output` element *is* linked, by `Location`, against
the fragment stage's own unarrayed input, by both `GraphicsPipeline.cpp`'s
`validateStageInterfaces` and `feme::graphics::executeDraws`'s own
varying-linking loop, and neither consults `RowCountIsVertexArray` when
comparing the two `RowCount`s. Left folded in, a 3-vertex mesh entry's own
`vec4` per-vertex output was wrongly reflected with `RowCount == 3` (the
mesh's own `OutputVertices`) instead of `1` (the fragment-visible
per-vertex shape), disagreeing with the fragment input's own
`RowCount == 1` at the same location -- exactly this row's own named
diagnostic, despite both sides sharing the same `layout(location=...)`.

**The fix.** `CanonicalizeStage.cpp`'s `addElements` lambda now peels a
mesh entry's own plain `Output` global's outer per-vertex/per-primitive
array dimension off before building its `SignatureElement` -- the same
peeling a builtin interface block's own per-member element already gets --
leaving `RowCountIsVertexArray` at its default `false` to match, rather
than folding the dimension in and flagging it the way `Input`'s H5f
treatment does. New `CanonicalizeStageTest.
MeshStagePeelsPerVertexArrayFromOutputRowCount` reproduces the bug
directly at the unit level: confirmed to fail (`RowCount` wrongly `3`, not
`1`) without this fix and pass with it. The pre-existing
`MeshStageCanonicalizesOutputArrayStore` test (which only asserts the
store rewrite itself, not the resulting `RowCount`) and
`ThreadsDynamicVertexIndexIntoOutputStore` (deliberately tagged a
non-`Mesh` stage, confirming this fix does not touch a real per-stage
matrix output's own `RowCount`) both remain unaffected.

```
$ ninja -C <feme-build> check-feme
...
Total Discovered Tests: 2027
  Unsupported:   59 (2.91%)
  Passed     : 1968 (97.09%)
```

Up from H6g-b-d's own 1967/2026 by exactly the 1 new test this row adds
(0 pre-existing tests newly failed).

**A real ICD re-run confirms the fix, and finds a new bug in its place.**
Re-running this row's own named case: the interface mismatch is gone, and
it now progresses all the way to a genuine image comparison:

```
 <Text>Image comparison failed: max difference = (0, 0, 1, 1), threshold = (0.005, 0.005, 0.005, 0.005)</Text>
  Fail (Result does not match reference; check log for details at vktMeshShaderInOutTestsEXT.cpp:1590)
```

Re-running the full 560-case `dEQP-VK.mesh_shader.ext.in_out.*` bucket hit
the same class of problem H6c-a-a-iii's own crash first ran into: several
of the 40 newly-unblocked cases now reach real mesh-stage execution for
the first time and crash the whole `deqp-vk` process outright, losing
every case after the first crash in one run. Using the same per-case
resume-loop methodology H6c-a-a-iii's own reproduction established
(blacklisting each case whose `Test case '...'..` line printed with no
following result line, removing both resolved and blacklisted cases from
the remaining list, and repeating), the full bucket resolves after 33
iterations:

```
Passed:         0/560 (0.0%)
Failed:        48/560 (8.6%)
Not supported: 480/560 (85.7%)
Crashed (blacklisted, no clean result): 32/560 (5.7%)
```

Grepping every iteration's combined log confirms **zero** remaining
occurrences of "disagree on component/row count or type", down from 40 --
this row's own targeted diagnostic is fully resolved bucket-wide, not just
for the one named case. The bucket's 80 pre-existing failures split three
ways now, instead of H6g-b-d's own two:

```
     40 JIT session error: Symbols not found: [ spirv_var_NN ]  (already-tracked H6g-b-c, unchanged)
      8 Result does not match reference (vktMeshShaderInOutTestsEXT.cpp:1590) -- genuine image-comparison failure, out of this row's own scope
     32 SIGSEGV/SIGABRT inside feme::graphics::executeDraws itself -- new, not yet root-caused
```

The 40 `spirv_var_NN` cases are confirmed unchanged (still H6g-b-c's own
already-tracked bucket, not a duplicate). The 8 clean image-comparison
failures are a real, further, out-of-scope bug this row was never going to
fix (correct interface linkage does not imply correct pixel output) and
are left unfiled, since nothing about them is FeMe/MLIR-diagnosable yet --
a `deqp-vk` reference-image mismatch, not a `feme`-side error. The
remaining 32 are a genuinely new, distinct crash, confirmed by a real
`gdb` backtrace to be heap corruption (a bad `free()` several frames inside
`llvm::Expected<feme::graphics::StageStorage>`'s destructor) reached only
through `executeDraws`/`runPreparedDraw`/`runMeshDraw`/`vkQueueSubmit` --
never reachable before this row's own fix let these cases past submission-
time validation in the first place. Filed as a new roadmap row one level
under H6 (not nested any deeper under H6g-b, per the standing instruction
against nesting milestone IDs more than one lowercase letter deep going
forward), H6k.

`FeMeGraphicsDesign.md`'s G6 status paragraph updated with a short note
explaining why this direction could not reuse H5f's `Input`-side
flag-and-fold `RowCountIsVertexArray` treatment verbatim.
`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: a pure signature-reflection fix within
`VK_EXT_mesh_shader`'s already-advertised scope, touching no feature bit
or extension.

**Milestone H6 does not close.** This row's own fix lands and is confirmed
complete for the exact diagnostic it targeted, but the same real-ICD
re-run finds a new blocker in the same bucket: H6k.

**Reproducing this row.** Same ICD build as every prior mesh-shading row:

```shell
cd /path/to/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-case=dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_0.mesh_only \
    --deqp-log-filename=single_h6j.qpa
grep "^TEST: dEQP-VK.mesh_shader.ext.in_out\." dEQP-VK-cases.txt | sed 's/^TEST: //' > cases_h6j.txt
# The full bucket needs the resume-loop below -- several of the 40
# newly-unblocked cases crash the whole process outright (H6k):
cp cases_h6j.txt remaining.txt
: > results.txt; : > blacklist.txt
while [ -s remaining.txt ]; do
  VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
    ./deqp-vk --deqp-caselist-file=remaining.txt \
      --deqp-log-filename=iter.qpa \
      --deqp-log-images=disable --deqp-log-shader-sources=disable \
      > iter.stdout 2>&1
  # Parse iter.stdout: every "Test case '...'.." line with no following
  # Pass/Fail/NotSupported/... line before the process died is a crash;
  # append it to blacklist.txt, append every other case to results.txt,
  # then remove both from remaining.txt and repeat.
done
```

## Roadmap H6k: measured impact (`CanonicalizeStage.cpp` mesh-output constant-vertex-index fold)

H6j's own real-ICD re-run left this row a single, specific, named crash
to root-cause: 32 of the 40 cases its own interface-matching fix
unblocked reached real mesh-stage execution for the first time and
crashed the whole `deqp-vk` process instead of completing:

```
Program received signal SIGABRT, Aborted.
...
#4  0x... in llvm::Expected<feme::graphics::StageStorage>::~Expected ()
#5  0x... in feme::graphics::executeDraws (...)
#6  0x... in feme::graphics::runMeshDraw (...)
#7  0x... in feme::graphics::runPreparedDraw (...)
```

**Reproducing the crash today (first surprise).** Re-running the same 32
formerly-crashing cases individually against a fresh rebuild (`ninja
feme_vulkan`, confirmed via `md5sum`) produced **zero** crashes -- but a
*clean* diagnostic instead:

```
error: feme-graphics-validate-stage: 'feme.stage.output.store' in function 'main' row 1 is out of range for element 0
```

This is not this row's own bug converting itself; it is
`feme::graphics::ValidateStagePass` (wired into `ShaderStage::Mesh` by the
already-landed H6g-b-c) catching, at compile time, the exact same
out-of-bounds `Row` this row's crash used to hit only at runtime, past
that check's own addition. The crash and the diagnostic are the *same*
underlying bug -- H6g-b-c's row-range check simply turns what used to be
silent heap corruption into a clean, compile-time-visible rejection
instead of ever reaching `executeDraws` at all. (A second, unrelated
surprise during reproduction: the very first full-560-case-bucket re-run
showed the diagnostic recurring for a case that had just tested clean
standalone; deleting/disabling `deqp-vk`'s own `--deqp-shadercache`
ruled out shader-result caching as the cause, and the diagnostic turned
out to be entirely real and deterministic once traced -- see below.)

**Root cause, found via a real reduced-IR trace (temporary debug prints in
`resolveStageIOAccess`, confirmed against the real SPIR-V disassembly
`deqp-vk --deqp-log-shader-sources` reports for
`dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_1.mesh_only`'s
own `mesh` stage).** `CanonicalizeStage.cpp`'s `resolveStageIOAccess`
constant-offset fold path (`isPerVertexArrayInputGlobal`) deliberately
never folds a *constant* per-vertex/per-primitive index into `Output`'s
`Vertex` operand for any stage but `Mesh` (roadmap H6b: a real per-stage
matrix output has a legitimate constant-per-row store shape that must not
be misrouted into `Vertex`) -- but glslang's own generated GLSL for a
real mesh entry (confirmed against the actual `ShaderSource` embedded in
this case's own `.qpa` log) unrolls a small, compile-time-bounded
per-vertex output loop into one *constant*-indexed store per vertex
(`vert_i32d1_flat_0[0] = ...; [1] = ...; [2] = ...; [3] = ...;`), not a
dynamically-indexed one. A first attempt at fixing this (adding a new,
mesh-only `isPerVertexArrayMeshOutputGlobal` helper, scoped to a *plain*,
non-block `Output` array only) fixed the plain-array shape but left a
second, real shape in the same shader unhandled: `gl_MeshVerticesEXT`
itself (`out gl_MeshPerVertexEXT { vec4 gl_Position; }
gl_MeshVerticesEXT[];`) is a *builtin interface block* -- its own
`!feme.spirv.MemberDecorations` metadata -- and a first version of this
row's fix deliberately excluded any block-shaped global from the new
fold, reasoning (incorrectly) that a block's own per-member `ElementID`s
were not addressed the same way a plain global's single one is. Real
SPIR-V disassembly confirmed `gl_MeshVerticesEXT`'s own constant-indexed
`gl_Position` write (`%p = OpAccessChain ... %35 %vertex_idx %0; OpStore
%p %pos`) still fell through to the pre-H6k default path, folding its
constant vertex index into an ordinary matrix `Row` instead -- exactly
this row's own crash, for the single-member-block shape specifically
(component 0's own `gl_Position` element, confirmed via debug print:
`Row=0,1,2,3` where `Vertex=0` was expected, the values swapped from what
the fold should have produced).

**The fix, corrected.** `isPerVertexArrayMeshOutputGlobal` no longer
excludes a builtin-interface-block global: `resolveOffsetWithinElement`
(the same function `getDynamicVertexIndexedAccess`'s own *non*-constant
counterpart already relies on for exactly this) already knows how to pick
the right member's own `ElementID` out of a struct-shaped element type,
whether that struct has one member (`gl_MeshVerticesEXT`'s own
`gl_Position`) or several (`gl_MeshPrimitivesEXT`'s own
`gl_PrimitiveID`-plus-others shape) -- there was nothing block-specific
left to add once the constant vertex index itself was peeled into
`Vertex` before reaching it, mirroring the dynamic-index path exactly.
`CanonicalizeStageTest.ConstantIndexIntoArrayedBuiltinInterfaceBlockIsLeftUnrewritten`
(previously asserting this shape stayed deliberately unrewritten, per a
mistaken H6c-a-a-iii cross-reference) is renamed and rewritten as
`FoldsConstantIndexIntoArrayedBuiltinInterfaceBlockMemberStore`, now
asserting the correct rewritten shape instead; a new
`FoldsConstantVertexIndexIntoSingleMemberInterfaceBlockOutputStore` test
covers the exact single-member-block shape (`gl_MeshVerticesEXT`'s own
`gl_Position`) the real CTS case exercises, alongside the pre-existing
`FoldsConstantVertexIndexIntoOutputStoreForMesh` (the plain-array shape).

```
$ ninja -C <feme-build> check-feme
...
Total Discovered Tests: 2033
  Unsupported:   59 (2.90%)
  Passed     : 1974 (97.10%)
```

Up from H6j's own 1968/2027 by exactly the 1 previously-renamed test's own
new assertions plus the 1 wholly new single-member-block test this row
adds (0 pre-existing tests newly failed).

**A real ICD re-run confirms the fix.** Re-running the same 32
formerly-crashing cases together in one process:

```
Test run totals:
  Passed:         0/32 (0.0%)
  Failed:        32/32 (100.0%)
```

Zero crashes, zero "row"/"component is out of range" diagnostics. The 32
split exactly the way H6j's own report predicted the non-crashing 8 would:
27 reach a genuine, clean image-comparison `Fail`
(`vktMeshShaderInOutTestsEXT.cpp:1590`, out of this row's own scope -- a
`deqp-vk` reference-image mismatch, not a `feme`-side error) and 5 hit the
already-tracked, unrelated H6g-b-c `spirv_var_NN` JIT-symbol class for
`task_mesh` variants specifically. Re-running the full 560-case
`dEQP-VK.mesh_shader.ext.in_out.*` bucket in one process (no resume-loop
needed this time -- nothing crashes) confirms this bucket-wide:

```
Passed:         0/560 (0.0%)
Failed:        80/560 (14.3%)
Not supported: 480/560 (85.7%)
```

The 80 failures split cleanly in two: 40 genuine image-comparison
failures and 40 already-tracked `spirv_var_NN` JIT-symbol failures (more
`task_mesh` permutations than the 32-case blacklist alone covers) --
**zero** crashes, **zero** row/component-range diagnostics anywhere in
the full bucket.

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: a pure signature-reflection fix within
`VK_EXT_mesh_shader`'s already-advertised scope, touching no feature bit
or extension. `FeMeGraphicsDesign.md`'s G6 status paragraph updated with
a short note on why the constant-vertex-index fold needed to cover a
builtin interface block, not just a plain per-vertex array.

**A side effect on H6l.** H6l's own row describes a *different* real
mesh case (`dEQP-VK.mesh_shader.ext.builtin.cull_primitives`) hitting
`row N is out of range` errors from what this row suspected might be the
same underlying bug class. Re-running the full `dEQP-VK.mesh_shader.ext.
builtin.*` group (37 cases) both immediately before and immediately after
this row's own fix (via `git stash`) confirms the total pass/fail split
is unchanged bucket-wide (22/37 `Failed` either way -- this row's fix is
not a regression there), but the specific diagnostics this row's own fix
left behind for `cull_primitives` changed shape: the originally-reported
`row N is out of range for element {4,5}` is gone, replaced by a mix of
`row`/`component is out of range` errors spread across elements 1-5. H6l
is updated (not closed) to reflect this; it needed its own further,
separate investigation this row's own scope does not cover.

**Milestone H6 does not close.** H6k's own fix lands and is confirmed
complete for the exact crash it targeted -- the full 560-case in_out
bucket now has zero crashes and zero row/component-range diagnostics --
but H6l remains open with its own, distinct (if related) failure.

**Reproducing this row.** Same ICD build as every prior mesh-shading row:

```shell
cd /path/to/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_1.mesh_only \
    --deqp-shadercache=disable
# Full bucket, no resume-loop needed after this row's own fix:
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=cases_h6j.txt --deqp-shadercache=disable
```

## Roadmap H6l: measured impact (`CanonicalizeStage.cpp` mesh-output packed-vertex-size fix)

H6k's own real-ICD re-run left this row a single, specific,
already-narrowed diagnostic to root-cause: `cull_primitives`'s own
`row`/`component is out of range` errors, changed in shape (not closed)
by H6k's own fix.

**Root cause, found via the same real-ICD-plus-debug-print technique this
whole H6g-b/H6j/H6k chain has used throughout** (temporary `llvm::errs()`
prints in `resolveStageIOAccess`'s constant-vertex-index fold, confirmed
against the real SPIR-V disassembly `deqp-vk
--deqp-log-shader-sources=enable` reports for this exact case's own
`mesh` stage). The fold computes `VertexSize =
DataLayout::getTypeAllocSize(ElemTy)`, the ABI-alignment-padded
allocation size of the per-vertex/per-primitive array's own element type,
and divides each access's constant byte offset by it to recover
`VertexIdx`/`Residual`. A debug-print trace of the real failing case's
own two affected globals showed this size disagreeing with the real,
SPIR-V-embedded stride in exactly the way roadmap C1/H6g-b-a-i's own
prior "ABI padding vs. real layout" bug class does, but scoped this time
to `CanonicalizeStage.cpp`'s own, separate `VertexSize` computation:

```
H6l-debug: GV=spirv_var_16 ByteOffset=28 VertexSize=32 VertexIdx=0 Residual=28
H6l-debug:   struct alloc=32 memberOff[0]=0 memberOff[1]=16 memberOff[2]=20 memberOff[3]=24
H6l-debug: GV=spirv_var_43 ByteOffset=12 VertexSize=16 VertexIdx=0 Residual=12
H6l-debug:   vector scalarAlloc=4 numElts=3
```

`spirv_var_16` (`gl_MeshVerticesEXT`, a *full* four-member
`gl_PerVertex`-shaped block -- `gl_Position`/`gl_PointSize`/
`gl_ClipDistance`/`gl_CullDistance`, not H6k's own single-member
`gl_Position`-only fixture) ends its last member (`gl_CullDistance`) at
byte 24 plus its own 4-byte size, 28 total -- but *allocates* to 32,
rounded up to the struct's own 16-byte alignment, itself driven by the
leading `<4 x float>` member. Vertex 1's real, SPIR-V-embedded
`gl_Position` write lands at byte 28 (confirmed against the real SPIR-V
disassembly's own constant `getelementptr` offsets), so dividing by the
ABI-padded 32 instead computed `VertexIdx == 0`, `Residual == 28` --
landing past `gl_Position`'s own member (offset 0, `RowCount ==
ComponentCount == ` its own 4-component vector) and inside
`gl_CullDistance`'s (offset 24, a 1-element array), an out-of-range
`Row`/`Component` for that far narrower member -- exactly this row's own
observed diagnostic mix (never elements 0 or 5, the two globals this
gap did not touch). `spirv_var_43`
(`gl_PrimitiveTriangleIndicesEXT`, `[N x <3 x i32>]`, a plain array, not
a block) is a narrower instance of the identical gap: `uvec3` allocates
to 16 bytes (LLVM pads a 3-wide vector up to a 4-wide SIMD register) but
is addressed 12 bytes apart in the real embedded offsets, `uvec3`'s own
tightly packed 3-`i32` size -- silently, since this whole-vector store
shape reaches no `Row`/`Component` range check `ValidateStage.cpp` could
catch (only `Vertex`'s own non-constant-ness is checked, never its
range), so this half of the gap misdirects which primitive's own
triangle indices a store lands on without ever being diagnosed at all.

**The fix.** A new `getPackedMeshElementSize` helper
(`CanonicalizeStage.cpp`) computes the *tightly packed* size of a mesh
entry's own per-vertex/per-primitive array element -- a struct's own
last member's offset plus that member's own packed size (skipping
`DataLayout::getStructLayout`'s own ABI tail padding), a vector's own
element count times its own packed scalar size, an array's own element
count times its own packed element size, recursing -- and
`resolveStageIOAccess`'s constant-vertex-index fold now calls it instead
of `DataLayout::getTypeAllocSize` directly for `VertexSize`. This is
scoped to the fold's own `VertexSize` computation alone;
`resolveRowComponent`'s own separate row/component peeling (a single
member's own row shape, never one of the two shapes described above)
still uses `getTypeAllocSize`, since a single stage-IO member's own row
array or vector has no equivalent trailing-alignment gap to correct for.

Two new unit tests cover the two shapes directly:
`FoldsConstantVertexIndexIntoMultiMemberInterfaceBlockOutputStore`
(the four-member `gl_PerVertex`-shaped block, asserting vertex 1's real
byte-28 store resolves `Vertex == 1`, not `0`) and
`FoldsConstantVertexIndexIntoPlainVectorArrayOutputStoreWithPadding`
(the `uvec3` array, asserting primitive 2's real byte-24 store resolves
`Vertex == 2`, not `1`) -- both fail without this fix (reproducing this
row's own observed misrouting directly) and pass with it. The
pre-existing `FoldsConstantVertexIndexIntoSingleMemberInterfaceBlockOutputStore`/
`FoldsConstantVertexIndexIntoOutputStoreForMesh`/
`FoldsConstantIndexIntoArrayedBuiltinInterfaceBlockMemberStore` tests
(whose own fixtures happen not to need any ABI padding -- a lone `vec4`
member, or an `{i32, i32}` member pair, both already packed) are
unaffected, confirming the new helper is a no-op wherever packed and
ABI-allocated sizes already agree.

```
$ ninja -C <feme-build> check-feme
...
Total Discovered Tests: 2035
  Unsupported:   59 (2.90%)
  Passed     : 1976 (97.10%)
```

Up from H6k's own 1974/2033 by exactly the 2 new tests this row adds (0
pre-existing tests newly failed).

**A real ICD re-run confirms the fix.** Re-running `cull_primitives`
alone:

```
Test run totals:
  Passed:         0/1 (0.0%)
  Failed:         1/1 (100.0%)
```

Still `Fail`, but no longer for this row's own reason: grepping the
re-run's own log confirms **zero** `feme-graphics-validate-stage`
occurrences at all (down from the 16 `row`/`component is out of range`
diagnostics this row's own text described). With
`FEME_VULKAN_LOG_CREATION_ERRORS=1` (this ICD's opt-in error-logging
env var, `Diagnostics.cpp`), the real failure reached instead is:

```
vkQueueSubmit: stage element 5 has a 1-bit scalar; only 32-bit elements are implemented yet
```

-- `StageStorage.cpp`'s own long-standing, generic "32-bit scalars only"
scope limit (`StageStorage.h`'s own documented scope, unrelated to mesh
shading at all), reached for the first time by this case only because
this row's own fix lets it clear compile-time validation and reach
`vkQueueSubmit` at all; `gl_CullPrimitiveEXT` is a SPIR-V/GLSL `bool`
(`i1`), element 5 in this case's own signature. This is the same
narrowing pattern every H6g-b/H6j/H6k/H6l row in this chain has followed
-- fixing one blocker surfaces the next, narrower one -- so it is filed
as a new, sibling row one level under H6 (not nested any deeper under
H6l, per the standing one-lowercase-letter-deep instruction): H6m.

Re-running the full `dEQP-VK.mesh_shader.ext.builtin.*` group (37 cases)
confirms this row is not a regression bucket-wide:

```
Test run totals:
  Passed:         0/37 (0.0%)
  Failed:        22/37 (59.5%)
  Not supported: 15/37 (40.5%)
```

Byte-identical to H6k's own recorded 22/37 `Failed` split, and grepping
the full re-run's own combined log confirms **zero**
`feme-graphics-validate-stage` occurrences anywhere in the bucket (down
from the 16 this row's own case alone used to contribute). A re-run of
the 1957-case `draw_sample.txt` regression sample (`dEQP-VK.draw.*`,
`VK-GL-CTS/run/draw_sample.txt`) confirms no regression there either,
byte-identical to every prior recorded run:

```
Test run totals:
  Passed:        14/1957 (0.7%)
  Failed:       153/1957 (7.8%)
Not supported: 1790/1957 (91.5%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: a pure lowering-correctness fix within
`VK_EXT_mesh_shader`'s already-advertised scope, touching no feature bit
or extension.

**Milestone H6 does not close.** This row's own fix lands and is
confirmed complete for the exact diagnostic it targeted -- zero
`feme-graphics-validate-stage` row/component-out-of-range occurrences
anywhere in the 37-case builtin group, no regression in the wider
mesh-shading or draw regression samples -- but `cull_primitives` itself
still does not pass, blocked now by the distinct, newly surfaced H6m.

**Reproducing this row.** Same ICD build and CTS checkout as every prior
mesh-shading row:

```shell
cd /path/to/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n dEQP-VK.mesh_shader.ext.builtin.cull_primitives \
    --deqp-shadercache=disable
# Opt-in error logging to see the H6m blocker directly:
FEME_VULKAN_LOG_CREATION_ERRORS=1 VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n dEQP-VK.mesh_shader.ext.builtin.cull_primitives \
    --deqp-shadercache=disable
# Full builtin group and draw regression sample:
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-case="dEQP-VK.mesh_shader.ext.builtin.*" --deqp-shadercache=disable
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=draw_sample.txt --deqp-shadercache=disable
```

## Roadmap H7b: measured impact (widen `materializeImageDescriptor`'s dimension/array-layer support)

H7b's own text named `CommandBuffer.cpp`'s `materializeImageDescriptor`
hard-rejecting any view whose dimension wasn't `Texture2D`, and any
nonzero `baseArrayLayer`, as the blocker keeping `imageCubeArray`
`VK_FALSE`. That gap is real and now fixed: `materializeImageDescriptor`
widens to `Texture2DArray`/`TextureCube`/`TextureCubeArray` views and any
`baseArrayLayer`, building a per-mip adjusted subresource layout table
(`Offset` shifted by `BaseArrayLayer * SlicePitch`) rather than pointing
directly into `Image::mipLayouts()`'s shared, layer-0-relative table.

**check-feme**: `ninja check-feme` (assertions-enabled, ccache build)
passes in full, 2038 discovered tests, 1979 `Passed` (59 pre-existing,
unrelated `Unsupported`, 0 `Failed`), up from H7a's own 2037/1978 baseline
by exactly the 1 new
`SecondArrayLayerSampledImageDispatchTest.SamplesTheBoundLayerRatherThanLayerZeroOrAllZero`
test this row adds -- a real end-to-end dispatch confirming a
`VK_IMAGE_VIEW_TYPE_2D` view with `baseArrayLayer=1` over a two-layer
image now samples layer 1's own texels rather than layer 0's or an
all-zero read.

**Real `deqp-vk` runs**:

```sh
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=cases_h7b_cube_array.txt \
    --deqp-log-images=disable --deqp-log-shader-sources=disable
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=draw_sample.txt \
    --deqp-log-images=disable --deqp-log-shader-sources=disable
```

`cases_h7b_cube_array.txt` is the 12 `dEQP-VK.image.image_size.cube_array.*`
cases from `external/vulkancts/mustpass/main/vk-default/image/image-size.txt`
-- the only real cube-array cases in the mustpass tree cheap enough to run
directly and not gated behind unrelated, still-missing storage-image
support (`load-store.txt`'s own 386 `cube_array` cases all need a
`feme.cpu.image.store.*` runtime helper this ICD does not have yet,
unrelated to this row). All 12 report **`NotSupported (Requested core
feature is not supported: imageCubeArray)`**, unchanged from before this
row's own fix -- confirming, as the roadmap text above already concludes,
that this widening alone is invisible to any real CTS case until
`imageCubeArray` itself flips `VK_TRUE` (roadmap H7b-a), since real
Vulkan CTS cases gate on the advertised feature bit rather than on
whether the underlying descriptor path happens to work.

The 1957-case `draw_sample.txt` regression sample stays byte-identical to
H7a's own recorded totals (14 Passed/179 Failed/1764 NotSupported):
**0 regressions.**

**Inventories**: `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`
confirmed to need no change -- `imageCubeArray` stays `VK_FALSE`, no
extension or feature bit is touched by this row.

**Milestone H7b does not close.** The descriptor-materialization gap
its own text named is fixed and tested, but a real code survey while
implementing it found a second, more fundamental, previously-unnamed
blocker (`SPIRVResourceLowering.cpp`/`ResourceLowering.cpp`'s
`Dim=2D`/`Texture2D`-only shader-visible handle classification) that
this row's own fix cannot reach. Filed as roadmap H7b-a.

## Roadmap H7a: measured impact (five already-implemented core 1.0 feature bits)

Roadmap H7 asked for a full survey of the ~20 named candidate optional
Vulkan 1.0 core feature bits this ICD still advertised `VK_FALSE`, to find
which ones the executor/pipeline layer already genuinely implements and
simply never advertised. The survey found five: `independentBlend`,
`logicOp`, `occlusionQueryPrecise`, `multiDrawIndirect`, and
`drawIndirectFirstInstance` -- each backed by real, already-working code
(`GraphicsPipeline.cpp`'s per-attachment `BlendState`/`logicOpEnable`/
`logicOp` translation, `Executor.cpp`'s `applyLogicOp`/`mergeColor`,
`QueryPool.cpp`'s real per-sample occlusion accumulation, and
`CommandBuffer.cpp`'s `readIndirectDraws`/`readIndirectMeshDraws` looping
over an arbitrary indirect `DrawCount` with a nonzero `firstInstance`
already copied through unconditionally). Flipped together as one small,
low-risk commit, with `maxDrawIndirectCount` raised from `1` (the
spec-mandated floor while `multiDrawIndirect` is false) to `UINT32_MAX`
to match.

**check-feme**: `ninja check-feme` (assertions-enabled, ccache build)
passes in full, 1978/2037 (59 pre-existing, unrelated `Unsupported`, 0
`Failed`), up from H6m's own 1977/2036 baseline by exactly the 1 new
`MaxDrawIndirectCountAllowsMultiDraw` test this row adds (the existing
exhaustive feature-bit test was extended in place, not added as a new
test).

**Real `deqp-vk` re-runs**, each compared before/after via `git stash`:

| Group | Cases | Before (Pass/Fail/NotSupported) | After (Pass/Fail/NotSupported) |
|---|---|---|---|
| `dEQP-VK.pipeline.monolithic.*logic_op*` | 976 | 0/0/976 | 0/192/784 |
| `dEQP-VK.query_pool.occlusion_query.*` | 441 | 0/283/158 | 0/385/56 |
| multi-draw-indirect + `first_instance` sample (`draw_indirect`/`draw_indexed_indirect` `triangle_list` `*multi_draw*` + `dEQP-VK.draw.*first_instance*`) | 740 | 0/60/680 | 0/248/492 |
| `draw_sample.txt` regression sample | 1957 | 14/153/1790 | 14/179/1764 |

Every group's own `Passed` count is unchanged before/after -- confirming
zero regression in any case that previously passed -- while a real block
of cases moves from `NotSupported` to a genuine execution attempt in each
group (192/102/188/26 cases respectively), exactly the effect flipping an
honestly-implemented feature bit should have. Every one of the newly
attempted cases still fails, but on a separate, already-tracked,
pre-existing gap, not a bug in any of these five bits themselves:

- The 192 newly-attempted `logic_op` cases all fail with
  `feme-cpu-simdize: ... divergent value ... of vector type`, C8's own
  already-tracked matrix/aggregate legalization limitation (see roadmap
  H4e), reached here through an sRGB-format logic-op shader shape rather
  than the tessellation/geometry shapes H4b/H4c/H4d/H4e found it through
  originally -- not a new gap, just a new caller of the same one.
- The occlusion-query and multi-draw/`first_instance` groups' own newly
  attempted cases all fail with a generic
  `vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED`,
  a broader, not-yet-independently-triaged pipeline-creation content gap
  unrelated to any of these five feature bits (the same diagnostic shape
  for every failing case in both groups, none of it occlusion-query- or
  indirect-draw-specific).

**Inventories**: `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`
confirmed to need no change -- no extension or Vulkan-1.1+ feature struct
is touched, only five Vulkan-1.0-core `VkPhysicalDeviceFeatures` bits and
one core 1.0 limit (`maxDrawIndirectCount`).

**Milestone H7 does not close**: the remaining ~15 candidate bits this
row's own survey found each need real, independent work first, broken
down as Roadmap.md's H7b-H7j rows (none started).

Reproduction:

```sh
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-case="dEQP-VK.pipeline.monolithic.*logic_op*" --deqp-shadercache=disable
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-case="dEQP-VK.query_pool.occlusion_query.*" --deqp-shadercache=disable
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=draw_sample.txt --deqp-shadercache=disable
```

## Roadmap H6m: measured impact (canonicalize a `bool` stage-IO scalar to 32 bits)

H6l's own real-ICD re-run left this row a single, specific,
already-narrowed blocker to fix: `cull_primitives` now clears
`feme-graphics-validate-stage` entirely, but `vkQueueSubmit` itself fails
with `"stage element 5 has a 1-bit scalar; only 32-bit elements are
implemented yet"` -- `StageStorage.cpp`'s own long-standing, generic
"32-bit scalars only" scope limit, reached for the first time in the
stage-IO path.

**Root cause, found via a real IR reduction** (the same technique this
whole H6g-b/H6j/H6k/H6l chain has used throughout): a minimal mesh entry
storing an `i1` value into a `PerPrimitiveEXT`-decorated interface
block's own `BuiltIn 4485` (`CullPrimitiveEXT`) member --
`gl_MeshPrimitivesEXT[i].gl_CullPrimitiveEXT = %v` -- reproduces the
exact failure directly:

```
vkQueueSubmit: stage element 0 has a 1-bit scalar; only 32-bit elements are implemented yet
```

`getComponentType` (`CanonicalizeStage.cpp`) maps an LLVM `IntegerType`
straight to `{SInt, IntTy->getBitWidth()}`, so SPIR-V's `OpTypeBool`
(represented, like every other SPIR-V-to-LLVM-IR path in this project,
as a plain `i1`) reflects as a 1-bit `SignatureElement` --
`StageStorage::buildStageStorage`'s per-element layout
(`InvocationStride`/`ComponentStride`/`RowStride`) is hard-coded to a
4-byte scalar throughout and has no addressable representation for
anything narrower, so it errors rather than laying out storage the
compiled wrapper would misread.

**Choosing a fix.** The roadmap row's own text posed two options: widen
`StageStorage`'s own layout to a genuine 1-bit (or 1-byte, packed)
scalar, or canonicalize a `bool` stage-IO element to an ordinary 32-bit
integer at the `CanonicalizeStage.cpp`/SPIR-V-to-LLVM boundary before it
ever reaches `StageStorage`. The real IR reduction settled it: a 1-bit
scalar has no clean fit in `StageStorage`'s existing byte-oriented
addressing model (every other scalar width this project supports --
8/16/32/64, `Signature.cpp`'s own `checkBitWidth` -- is already
byte-addressable; 1 bit is not, the same "not byte-addressable" class of
gap roadmap E29's own `Workgroup`-storage addressable-`i1` fix rejected
outright rather than solved), and canonicalizing at the boundary instead
mirrors how a real GPU driver actually represents a shader-visible
`bool` in memory (a full 32-bit word, not a packed bit) -- so it needed
no redesign of `StageStorage`'s own addressing at all, only two small,
local changes in `CanonicalizeStage.cpp`.

**The fix.** `getComponentType` now special-cases `i1` ahead of the
generic `IntegerType` case, returning `{SignatureComponentType::Bool,
32}` instead of `{SInt, 1}` -- reusing the `Bool`/`StageLayoutScalarKind::
Bool` pairing `StageStorage.cpp`'s own `scalarKindFor` already had a case
for, just never a producer of. `loadStageIOValue`/`storeStageIOValue`'s
own terminal (leaf-scalar) cases widen/narrow the actual value to match:
a store zero-extends its `i1` operand to `i32` before calling
`createStageOutputStore`, and a load calls `createStageInputLoad` with an
`i32` result type and truncates it back down to `i1`. The shadow-alloca
read-back path (`ShadowValueMap`, roadmap H2e) is untouched -- it never
reaches `StageStorage` at all, so it keeps the value's own natural `i1`
type. `StageStorage.cpp`/`.h` needed no change whatsoever.

A new unit test,
`CanonicalizesBoolPerPrimitiveOutputStoreToA32BitElement`
(`CanonicalizeStageTest.cpp`), covers the whole boundary directly: the
reduced `gl_CullPrimitiveEXT`-shaped IR from the root-cause reduction
above, asserting the reflected element is `{Bool, 32}`, the emitted
`feme.stage.output.store`'s value operand is an `i32` `zext` of the
original `i1`, and a direct call to `buildStageStorage` on the resulting
signature succeeds where it previously errored with this row's own exact
message. Confirmed to fail without this fix (reproducing "stage element 0
has a 1-bit scalar..." via `buildStageStorage`'s own `Error` directly)
and pass with it.

```
$ ninja -C <feme-build> check-feme
...
Total Discovered Tests: 2036
  Unsupported:   59 (2.90%)
  Passed     : 1977 (97.10%)
```

Up from H6l's own 1976/2035 by exactly the 1 new test this row adds (0
pre-existing tests newly failed).

**A real ICD re-run confirms the fix.** Re-running `cull_primitives`
alone:

```
Test run totals:
  Passed:        0/1 (0.0%)
  Failed:        1/1 (100.0%)
```

Still `Fail`, but no longer at `vkQueueSubmit`: the shader now compiles,
links, and submits successfully, and the case fails instead on a clean
pixel-comparison mismatch --

```
Pixel (0, 0) failed: expected (0, 0, 1, 1) and found (0, 0, 0, 1)
...
Check log for details at vktMeshShaderBuiltinTestsEXT.cpp:641
```

-- every covered pixel renders black instead of the expected blue,
i.e. `gl_CullPrimitiveEXT` now compiles and stores its value correctly,
but the CPU rasterizer does not yet honor it to actually skip a culled
primitive. This is a distinct, later rendering-correctness gap -- not
filed as a new sibling row, since it blocks no other case and sits
squarely inside this milestone's own already-tracked "bounded
payload/output limits reported truthfully" scope, rather than being a
new kind of compile-time blocker the way every prior H6g-b/H6j/H6k/H6l/
H6m row in this chain was.

Re-running the full `dEQP-VK.mesh_shader.ext.builtin.*` group (37 cases)
confirms this row is not a regression bucket-wide:

```
Test run totals:
  Passed:         0/37 (0.0%)
  Failed:        22/37 (59.5%)
  Not supported: 15/37 (40.5%)
```

Byte-identical to H6l's own recorded 22/37 `Failed` split, confirmed via a
`git stash` before/after comparison of the full 37-case group (same
split either way; only `cull_primitives`'s own failure *reason* changed,
from `vkQueueSubmit: ... 1-bit scalar ...` to the pixel-comparison
mismatch above). A re-run of the 1957-case `draw_sample.txt` regression
sample (`dEQP-VK.draw.*`, `VK-GL-CTS/run/draw_sample.txt`) confirms no
regression there either, byte-identical to every prior recorded run:

```
Test run totals:
  Passed:        14/1957 (0.7%)
  Failed:       153/1957 (7.8%)
Not supported: 1790/1957 (91.5%)
```

`Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` confirmed no
change needed: a pure lowering-correctness fix within
`VK_EXT_mesh_shader`'s already-advertised scope, touching no feature bit
or extension.

**Roadmap H6m closes.** This row's own fix lands and is confirmed
complete for the exact diagnostic it targeted -- zero `"N-bit scalar;
only 32-bit elements are implemented yet"` occurrences anywhere in the
37-case builtin group, no regression in the wider mesh-shading or draw
regression samples -- and does not reopen or narrow into a further
sibling row the way H6g-b/H6j/H6k/H6l each did: `cull_primitives`'s own
remaining gap (rendering, not compiling) is out of this row's own scope.
**Milestone H6 does not close**: `cull_primitives` and the wider 37-case
builtin group still do not pass.

**Reproducing this row.** Same ICD build and CTS checkout as every prior
mesh-shading row:

```shell
cd /path/to/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk -n dEQP-VK.mesh_shader.ext.builtin.cull_primitives \
    --deqp-shadercache=disable
# Full builtin group and draw regression sample:
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-case="dEQP-VK.mesh_shader.ext.builtin.*" --deqp-shadercache=disable
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=draw_sample.txt --deqp-shadercache=disable
```

## Roadmap H6c-a: closed by its own split

Re-checking H6c-a's own literal ask now that its three named
prerequisites and its own two split rows have all landed, rather than
assuming the earlier "why this row could not land" investigation is
still the final word on it.

**Prerequisite status, re-confirmed by direct inspection, not by
assuming the roadmap's own struck-through text is accurate:**

- H6d (`feme/lib/Graphics/AmplificationDispatch.cpp`,
  `feme/lib/Graphics/Meshlet.cpp`): landed. `AmplificationDispatchQueue`
  and `assembleMeshlet` both exist and are exercised by
  `AmplificationDispatchTest.cpp`/`MeshletTest.cpp`.
- H6h (`feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`):
  landed. `TaskPayloadGlobalVariablePattern` converts a
  `TaskPayloadWorkgroupEXT` global into address space 14, confirmed
  present by reading the pattern list directly.
- H6i (`feme/lib/Transforms/Graphics/CanonicalizeStage.cpp`): landed.
  `CanonicalizeStagePass::run`'s stage filter accepts
  `ShaderStage::Mesh`/`Amplification`, and a new `TaskPayloadStore`
  `feme.stage.*` op (`feme/include/feme/Core/StageOps.h`) canonicalizes
  a task entry's bounded payload write.

**This row's own two split children, re-confirmed the same way:**

- H6c-a-a wires `MeshOutputBuilder` into a mesh entry's canonicalized
  `feme.stage.output.store` `Vertex`-operand writes via the new
  `feme::cpu::MeshOutputWrapperPass`, run immediately before
  `EntryWrapperPass` in `Pipeline.cpp`'s `ShaderStage::Mesh` case.
- H6c-a-b wires `TaskPayloadBuilder` into a task entry's canonicalized
  `feme.stage.task.payload.store` via the new
  `feme::cpu::TaskPayloadWrapperPass`, run immediately before
  `EntryWrapperPass` in `Pipeline.cpp`'s `ShaderStage::Amplification`
  case.

Both wrappers are confirmed reached from `EntryWrapperPass`'s reused
compute-lowering path (the exact routing H6c-a's own text asks for) by
`CompiledStageTest.cpp`'s `InvokeMeshWritesPerVertexOutputStore` and
`InvokeTaskWritesPayloadStore` cases, each compiling a canonicalized-
shaped mesh/task entry through the real `CompiledStage::create`
pipeline and observing the resulting write land in `FemeMeshArgs::
VertexOutputs`/`FemeTaskArgs::Payload` through `invokeMesh`/`invokeTask`
end to end.

**No further source change results from this row.** Everything H6c-a's
own text names -- wiring `MeshOutputBuilder`/`TaskPayloadBuilder` into
real `feme.stage.*` mesh-output-store/task-payload-store operations
reaching the reused `EntryWrapperPass` path -- was already implemented
under the row's own H6c-a-a/H6c-a-b children once their shared
prerequisites landed; this row's remaining work is bookkeeping (closing
it in the Roadmap) rather than a new patch, mirroring H6g-a's own
"folds into an existing row, no source change from this row itself"
precedent.

`ninja check-feme` (assertions-enabled, ccache build) re-run to confirm
no regression from this bookkeeping-only closure: 1966/2025 passing (59
pre-existing, unrelated `Unsupported`, 0 `Failed`), byte-identical to
the baseline already recorded by H6g-b-a-i-a-i-c's own closing entry --
expected, since no source file changes with this row.

**Milestone H6 still does not close.** H6c-a's own literal ask is now
fully satisfied, but a real `dEQP-VK.mesh_shader.*` run still hits the
two gaps H6c-a-a's and H6g-b-a-i-a-i-c's own closing re-runs already
found and split out: H6g-b-c (a mesh entry's unresolved
arrayed-builtin-block access reaches the JIT as an undefined symbol,
since `ValidateStagePass` does not yet validate `ShaderStage::Mesh`) and
H6g-b-d (`MeshOutputWrapperPass::lowerMeshStageOps`'s catch-all rejects
a surviving `feme.stage.*`/masked-output-store call that is neither
`OutputStore` nor `SetMeshOutputs`). Neither is this row's own concern;
both stay tracked separately. `Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` confirmed no change needed: this is a pure
Roadmap bookkeeping closure, touching no feature bit, limit, or
extension.

## Roadmap H6g-b-c: measured impact (wire `ShaderStage::Mesh` into `ValidateStagePass`, diagnose an unresolved stage-IO global-variable access)

**The fix.** `feme::graphics::ValidateStagePass::run` validated only
`Vertex`/`Fragment` entry points until this row; every prior row that
touched mesh validation (`H6a` on) left `ShaderStage::Mesh` unreachable.
Two changes were needed to wire it in correctly:

1. `isStageOpLegalForStage`'s `InputLoad`/`OutputStore` case previously
   only accepted `Vertex`/`Fragment` -- but a mesh entry's own
   per-vertex/per-primitive output write reuses `OutputStore` (roadmap
   H6b), so simply adding `Mesh` to `ValidateStagePass::run`'s stage
   filter without also widening this case would have made every
   legitimate mesh output store newly, wrongly rejected as "not legal in
   function ... (stage 'mesh')". Fixed by extending the case to accept
   `Mesh` too.
2. A new check, `validateStageIOGlobalAccess`, walks every load/store's
   pointer operand back through any `getelementptr` chain
   (`llvm::GEPOperator`, covering both the instruction and
   constant-expression forms) to find its underlying `GlobalVariable`.
   One `isSPIRVStageIOGlobal` still recognizes (address space 7/8,
   carrying `!spirv.Decorations`/`!feme.spirv.MemberDecorations`) means
   `CanonicalizeStagePass` left that particular access un-rewritten --
   exactly the shape `resolveOffsetWithinElement` (roadmap H6c-a-a-iii)
   returns `std::nullopt` for today, e.g. a mesh entry's arrayed
   `PerPrimitiveEXT`/`PerVertexEXT` builtin interface-block access -- so
   it is now diagnosed directly instead of silently surviving to
   `feme::cpu`'s JIT as an undefined symbol.

`isSPIRVStageIOGlobal` itself was factored out of `CanonicalizeStage.cpp`'s
anonymous namespace into a new shared header,
`feme/include/feme/Transforms/Graphics/StageIOGlobal.h`, with no behavior
change to `CanonicalizeStagePass`, so both passes recognize the identical
global-variable shape.

**Unit tests.** Four new cases in `ValidateStageTest.cpp`:
`MeshOutputStoreIsLegal`/`DiscardInMeshStageIsIllegal` (confirming the
newly-wired stage's own legality rules in both directions),
`UnresolvedStageIOGlobalAccessInMeshIsDiagnosed` (reusing
`CanonicalizeStageTest.
ConstantIndexIntoArrayedBuiltinInterfaceBlockIsLeftUnrewritten`'s own
arrayed-`PerPrimitiveEXT`-block IR directly against `ValidateStagePass`,
confirming the exact shape this row targets is caught with the expected
message and global name), and `OrdinaryAllocaAccessInMeshIsNotDiagnosed`
(a negative control, confirming an unrelated local `alloca` access is not
flagged).

```
$ ninja -C <feme-build> check-feme
...
Total Discovered Tests: 2031
  Unsupported:   59 (2.90%)
  Passed     : 1972 (97.10%)
```

Up from H6k's own 1968/2027 baseline by exactly the 4 new tests this row
adds (0 pre-existing tests newly failed).

**A real ICD re-run confirms the fix directly.** Re-running this row's
own named case:

```shell
cd /path/to/VK-GL-CTS/build/external/vulkancts/modules/vulkan
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-case=dEQP-VK.mesh_shader.ext.builtin.cull_primitives \
    --deqp-log-filename=single_h6g_b_c.qpa
```

```
error: feme-graphics-validate-stage: function 'main' has an unresolved
stage-IO global-variable access to 'spirv_var_16', a shape
CanonicalizeStagePass does not yet canonicalize into a 'feme.stage.*'
call
...
  Fail (retcode: VK_ERROR_INITIALIZATION_FAILED at vkPipelineConstructionUtil.cpp:176)
```

The undiagnosed `JIT session error: Symbols not found: [ spirv_var_16 ]`
this row's own text cited is gone -- the exact same global name
(`spirv_var_16`) is now named directly in a clean, compile-time
diagnostic emitted well before pipeline creation fails. The case still
fails (`VK_ERROR_INITIALIZATION_FAILED` at the same call site), exactly
this row's own scope: diagnosing the gap, not fixing the underlying
unmodeled arrayed-block shape (still `H6c-a-a-iii`'s own open item).

**A full re-run of the real `dEQP-VK.mesh_shader.ext.builtin.*` group
(37 cases) confirms this at scale**:

```shell
grep '^TEST: dEQP-VK.mesh_shader.ext.builtin\.' dEQP-VK-cases.txt \
  | sed 's/^TEST: //' > cases_h6g_b_c.txt
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=cases_h6g_b_c.txt \
    --deqp-log-filename=builtin_h6g_b_c.qpa \
    --deqp-log-images=disable --deqp-log-shader-sources=disable
```

```
  Passed:        0/37 (0.0%)
  Failed:       22/37 (59.5%)
  Not supported: 15/37 (40.5%)
```

Unchanged totals from before this row (expected -- diagnosing a failure
earlier and more cleanly does not itself turn it into a pass). Of the
22 `Failed` cases, 12 now hit this row's own new clean diagnostic instead
of a raw JIT symbol error -- the mesh-portion of the 9-case-turned-33-case
set `H6c-a-a-ii`/`H6c-a-a-iii`'s own reports named, grown by three further
cases this group's own re-run finds sharing the identical shape
(`num_work_groups_mesh`, `num_work_groups_task_and_mesh`,
`primitive_id_spirv`):

```
cull_primitives, draw_index_in_mesh, draw_index_in_task,
local_invocation_id_in_task, local_invocation_index_in_task,
num_work_groups_mesh, num_work_groups_task_and_mesh, position,
primitive_id_glsl, primitive_id_spirv, work_group_id_in_mesh,
work_group_id_in_task
```

The other 10 failing cases in the group (`global_invocation_id_in_{mesh,
task}`, `layer`, `layer_no_write`, `layer_shared`,
`local_invocation_{id,index}_in_mesh`, `viewport_index`,
`viewport_index_no_write`, `viewport_index_shared`) hit an unrelated,
pre-existing `feme-cpu-simdize` divergent-vector-value diagnostic further
down the pipeline -- confirmed unaffected by this row (0
`feme-graphics-validate-stage` diagnostics reported for any of them).

**A new, distinct, narrower bug surfaces in the same re-run, entirely out
of this row's own scope.** `cull_primitives` alone (no other case in the
group) additionally reports three `feme.stage.output.store` row-out-of-
range errors:

```
error: feme-graphics-validate-stage: 'feme.stage.output.store' in function 'main' row 1 is out of range for element 4
error: feme-graphics-validate-stage: 'feme.stage.output.store' in function 'main' row 2 is out of range for element 4
error: feme-graphics-validate-stage: 'feme.stage.output.store' in function 'main' row 1 is out of range for element 5
error: feme-graphics-validate-stage: 'feme.stage.output.store' in function 'main' row 2 is out of range for element 5
error: feme-graphics-validate-stage: 'feme.stage.output.store' in function 'main' row 3 is out of range for element 5
```

These only became visible now that `ValidateStagePass` validates the
mesh stage at all for the first time -- not investigated further here
(root cause not yet isolated; see the roadmap's own H6l entry for a
first candidate theory), filed as new row H6l.

**`dEQP-VK.draw.*`'s 1957-case `draw_sample.txt` regression sample**
stays byte-identical to every prior row's own recorded totals:

```
  Passed:        14/1957 (0.7%)
  Failed:        153/1957 (7.8%)
  Not supported: 1790/1957 (91.5%)
```

**0 regressions.**

`FeMeGraphicsDesign.md`/`Vulkan14FeatureInventory.md`/
`VulkanExtensionInventory.md` confirmed no change needed: this is a pure
diagnostic-completeness fix within `ValidateStagePass`'s existing scope
(catching a shape `CanonicalizeStagePass` already documented it would
leave for this pass to diagnose), touching no feature bit, limit, or
extension.

**Milestone H6 does not close.** This row's own fix lands and is confirmed
complete for the exact diagnostic it targeted, but the same real-ICD
re-run finds a new, narrower blocker in the same group: H6l.

## Roadmap H7b-a: measured impact (widen shader-visible sampled-image handle classification to Cube/CubeArray/2DArray)

H7b's own writeup above closed `materializeImageDescriptor`'s descriptor-
materialization gap but explicitly left `imageCubeArray` `VK_FALSE`,
because a real code survey while implementing it found a second,
previously-unnamed blocker even further up the pipeline:
`SPIRVResourceLoweringPass::classifySampledImage2DHandle`
(lib/Transforms/CPU/SPIRVResourceLowering.cpp) and DXIL's
`classifyImageHandle` (lib/Transforms/CPU/ResourceLowering.cpp) both
hard-rejected every sampled-image handle whose SPIR-V `Dim`/DXIL
`ResourceKind` was not exactly `Dim2D`/`Texture2D`, non-arrayed -- at
pipeline-creation time, before any descriptor lookup (H7b's own fix)
could ever run. This row widens both to accept `Dim::Cube`/arrayed
`Dim::2D`/arrayed `Dim::Cube` (SPIR-V) and
`Texture2DArray`/`TextureCube`/`TextureCubeArray` (DXIL), adds a new CPU
runtime `femeRTSelectCubeFace` major-axis cube-face-selection primitive
(feme/runtime/CPU/FeMeRuntimeCPU.c), and flips `imageCubeArray` to
`VK_TRUE` now that both halves of the gap (descriptor materialization and
handle classification) are closed together.

**check-feme**: `ninja check-feme` (assertions-enabled, ccache build)
passes in full, 2058 discovered tests, 1999 `Passed` (59 pre-existing,
unrelated `Unsupported`, 0 `Failed`), up from H7b's own 2038/1979 baseline
by the 22 new tests this row (plus H7b's own flip) adds: 5 new
`SPIRVResourceLoweringTest` cases (Cube/CubeArray/2DArray handle
classification), 7 new `ResourceLoweringTest` cases (the DXIL mirror --
this file had *zero* pre-existing image-lowering coverage of any kind
before this row), 1 new end-to-end `CommandBufferTest`
(`CubeArraySampledImageDispatchTest.SamplesTheSelectedFaceAndCubeArrayElement`,
a real compute dispatch sampling a `VK_IMAGE_VIEW_TYPE_CUBE_ARRAY` view's
+Z face at array element 1, its expected texel value hand-derived from
`femeRTSelectCubeFace`'s own major-axis formula and confirmed correct on
the first run), and the exhaustive `PhysicalDeviceInfoTest` feature-bit
enumeration extended in place for `imageCubeArray` (not a new test).

**Real `deqp-vk` runs**:

```sh
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=cases_h7b_cube_array.txt \
    --deqp-log-images=disable --deqp-log-shader-sources=disable
VK_ICD_FILENAMES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  ./deqp-vk --deqp-caselist-file=draw_sample.txt \
    --deqp-log-images=disable --deqp-log-shader-sources=disable
```

The same 12 `cases_h7b_cube_array.txt` cases H7b's own row measured
(`dEQP-VK.image.image_size.cube_array.*`) all still report
**`NotSupported`, but for a different, unrelated reason than before**:
previously `Requested core feature is not supported: imageCubeArray`
(the exact gate this row's own fix removes), now `Format not supported
for the specified usage`. These 12 cases exclusively request
`VK_IMAGE_USAGE_STORAGE_BIT` (`vktImageSizeTests.cpp`'s own
`readonly`/`writeonly` naming), and `Format.cpp`'s `formatFeatureFlags`
never sets `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` for *any* format today
(a pre-existing, already-documented gap: "V5: Images and sampling"'s own
status note in `FeMeVulkanDesign.md` names the missing
`feme.cpp.image.store.*` runtime helper). This is expected and confirms
the fix worked as scoped: the feature-bit gate these 12 cases used to hit
is gone, and the *next* thing they hit is an orthogonal, already-tracked
storage-image-write gap this row was never meant to close.

No case in the current mustpass tree exercises an ordinary (non-shadow)
sampled cube-array read without also depending on a *different* still-
unimplemented feature that gates pipeline/descriptor creation before ever
reaching this row's own fix -- surveyed directly rather than assumed:
`texture.txt`'s own 1536 `cube_array` cases are all `dEQP-VK.texture.
shadow.cube_array.*` (depth-compare `OpImageSampleDref*`, a different,
still-unimplemented SPIR-V op family, unrelated to this row's scope);
`binding-model.txt`'s 9108 `cube_array` cases all use an immutable-sampler
or descriptor-array binding shape that already fails pipeline creation
identically for a plain, non-array, non-cube `2d` case in the same test
family (confirmed directly: `dEQP-VK.binding_model.shader_access.
primary_cmd_buf.bind.combined_image_sampler_immutable.compute.
single_descriptor.2d` fails `vkCreateComputePipelines` the same way as
its own `.cube_array` sibling), so this is an orthogonal, pre-existing gap
this row's own fix cannot and does not need to touch;
`robustness.txt`'s `sampled_image.*.cube_array.comp` case reports
`NotSupported (robustImageAccess not supported)`; and
`pipeline/pipeline-library.txt`'s 443 `cube_array` cases all require
`VK_EXT_attachment_feedback_loop_layout`, also unadvertised. The mustpass
tree's own coverage of *sampled* cube-array reads is, today, entirely
gated behind one or more of these three unrelated features for every
single case -- confirmed by direct survey, not by inference -- so this
row's own real end-to-end confirmation is
`CommandBufferTest.cpp`'s `CubeArraySampledImageDispatchTest` above: a
genuine compute dispatch through the real SPIR-V-to-LLVM lowering,
`feme-cpu-resource-lowering`, and CPU runtime, not a mock.

The 1957-case `draw_sample.txt` regression sample stays byte-identical to
H7b's own recorded totals (14 Passed/179 Failed/1764 NotSupported):
**0 regressions.**

**Inventories**: `Vulkan14FeatureInventory.md` updated -- `imageCubeArray`
flips to `yes`, the 1.0 feature-advertised count rises from 11 of 55 to 12
of 55 (total 54 of 150), and the "graphics-specific unimplemented" bullet
drops from 11 to 10. `VulkanExtensionInventory.md` confirmed to need no
change: no extension is touched by this row, only a core 1.0 feature bit.
`FeMeVulkanDesign.md`'s "V5: Images and sampling" status note updated with
the cube(array)-over-2D-array addressing convention (a cube view is
purely an addressing convention over the same array-of-2D-layers storage,
never a distinct physical dimension) and the cube-face-selection
algorithm choice (`femeRTSelectCubeFace`'s major-axis formula, the
standard convention with no seam blending).

**Milestone H7b-a closes.** Both halves of the gap H7b's own writeup
identified -- descriptor materialization (H7b itself) and shader-visible
handle classification (this row) -- are now fixed and tested, `check-feme`
passes in full, and `imageCubeArray` honestly reads `VK_TRUE`.

## Roadmap H7c: measured impact (`fillModeNonSolid` -- `VK_POLYGON_MODE_LINE`/`_POINT`)

**What changed**: `GraphicsPipeline.cpp`'s `translateRasterState` now maps
`VkPipelineRasterizationStateCreateInfo::polygonMode` onto a new
`feme::graphics::PolygonMode` (`Fill`/`Line`/`Point`) instead of rejecting
anything but `VK_POLYGON_MODE_FILL` outright. `Executor.cpp`'s
solid-triangle assembly loop decomposes a `Line`-mode triangle into its
own 3 edges (reusing F5's existing `LineWidth`/`LineMode`/stipple-aware
line rasterizer unmodified, per `VK_KHR_line_rasterization`'s own spec
text extending those fields to "any line segment ... drawn ... when
polygonMode is VK_POLYGON_MODE_LINE") or a `Point`-mode triangle into its
own 3 vertices (reusing the existing point quad expansion), instead of
the ordinary filled-interior path. `PhysicalDeviceInfo.cpp` now
advertises `fillModeNonSolid = VK_TRUE`.

**Real `deqp-vk` reproduction**: built a combined case list from every
group a source survey of the VK-GL-CTS checkout under test
(`grep -rl fillModeNonSolid external/vulkancts/modules/vulkan/`) found
touching `polygonMode`/`fillModeNonSolid` directly: `dEQP-VK.draw.
renderpass.non_line_with_params.*` (60 cases -- the dedicated
non-solid-fill-mode draw test family, `vktDrawNonLineTests.cpp`),
`dEQP-VK.rasterization.polygon_as_large_points.*`/`dEQP-VK.rasterization.
primitive_size.*` (156 cases touching `polygon_mode` in their own name),
and every other case across `dEQP-VK.pipeline.{monolithic,
fast_linked_library,pipeline_library,shader_object_unlinked_spirv}` whose
name references `polygonmode`/`polygon_mode`/`fillmode` -- 216 total
unique cases after removing duplicates, run in one `deqp-vk` invocation
directly against `libfeme_vulkan.so` (`VK_ICD_FILENAMES` pointed at
`feme_icd.json`, no `lvp_icd.json` involved):

```
Test run totals:
  Passed:        15/216 (6.9%)
  Failed:        0/216 (0.0%)
  Not supported: 201/216 (93.1%)
```

**0 failures.** All 15 `Passed` cases are exactly the ones this
milestone's own scope covers and nothing else: `dEQP-VK.draw.renderpass.
non_line_with_params.vtx_{points,triangles}_mode_{fill,point,line}_
line_raster_{bresenham,rect,smooth}` (a plain vertex-fed point or
triangle topology, no geometry/tessellation stage, rasterized through
each of the 3 `polygonMode`/`lineRasterizationMode` combinations this row
implements) -- `vtx_points_mode_line_*` in particular confirms the
`PolygonMode::Line` path renders correctly even when fed a real
point-topology primitive turned into degenerate zero-length "edges" by
the test's own construction, not just the triangle case this row's own
text focused on. Every one of the 201 `NotSupported` cases is gated
behind an orthogonal, unimplemented feature this row does not touch and
was never in scope for: `shaderTessellationAndGeometryPointSize` (every
`_geom_*`/`_tess_*`-suffixed case in both groups -- a geometry/
tessellation-stage point-size write, unrelated to polygon mode itself),
`standardSampleLocations` (the `default_size.*_polygon_mode` multisample
variants), and `largePoints` (the plain `primitive_size.points.
point_size_*` cases, H7e's own row, not this one's). No `vtx_triangles_
mode_line_*` (non-geometry-stage) case exists in the CTS's own test
matrix at all -- confirmed by a direct grep of the full case list --
so there is no missing coverage there, just an absent combination in
the upstream suite itself.

**Inventories**: `Vulkan14FeatureInventory.md` updated -- `fillModeNonSolid`
flips to `yes`, the 1.0 feature-advertised count rises from 12 of 55 to
13 of 55, and the "graphics-specific unimplemented" bullet drops from 10
to 9. `VulkanExtensionInventory.md` confirmed to need no change: no
extension is touched by this row, only a core 1.0 feature bit.
`FeMeGraphicsDesign.md`'s F5 line-rasterization status paragraph gained a
new note describing how `PolygonMode::Line`/`Point` reuses that
machinery rather than inventing a second rasterizer.

**Milestone H7c closes.** `translateRasterState` accepts
`VK_POLYGON_MODE_LINE`/`_POINT`, `Executor.cpp` rasterizes both
end to end (proven both by unit tests and this row's own real `deqp-vk`
run), `check-feme` passes in full (2002/2061, 59 pre-existing unrelated
`Unsupported`, 0 `Failed`), and `fillModeNonSolid` honestly reads
`VK_TRUE`.

## Roadmap H7d: measured impact (`depthClamp`/`depthBiasClamp`/`depthBounds`)

**Case-list construction**: every group a source survey of the VK-GL-CTS
checkout under test found touching `depthClamp`/`depthBias`/
`depthBoundsTestEnable` directly or indirectly (`grep -rl
"depthClampEnable\|depthBiasEnable\|depthBoundsTestEnable"
external/vulkancts/modules/vulkan/`): `dEQP-VK.clipping.clip_volume.
depth_clamp.*` (10 cases, the dedicated depth-clamp correctness test,
`vktClippingTests.cpp`), `dEQP-VK.pipeline.{monolithic,
fast_linked_library,pipeline_library,shader_object_unlinked_spirv}.
depth_bias.*` and `.dynamic_state.**.depth_bounds*`/`.depth_bias*`, and
`dEQP-VK.renderpasses.**.depth_bounds_test`/`.depth_bias*` -- 911 total
unique cases after removing duplicates, run in one `deqp-vk` invocation
directly against `libfeme_vulkan.so`.

**Before this milestone's own two CTS-discovered bug fixes** (feature
bits landed but not yet correctness-validated): 16 Passed / 176 Failed /
719 NotSupported. All 10 `clip_volume.depth_clamp.*` cases failed
"Rendered image(s) are incorrect" -- a newly-surfaced, in-scope
regression (this whole case group was `NotSupported` before H7d, since
`depthClamp` read `VK_FALSE`).

**Root cause #1** (this row's own scope): depth clamping was applied
**per-vertex, before interpolation** (`projectVertex` clamped, then
barycentric interpolation ran on already-clamped per-vertex depths).
Vulkan's own depth clamp is a per-fragment operation applied to the
**interpolated** depth: a triangle with one vertex in-range and two
out-of-range must clamp to a *uniform* value across the whole triangle,
not a false linear gradient produced by interpolating already-clamped
vertices. Fixed by moving the clamp to the single barycentric
depth-interpolation site in `Executor.cpp` (`ScreenTriangle` gained
`DepthClampLo`/`DepthClampHi`, populated from the primitive's own
viewport at both `ScreenTriangle`-construction sites; `projectVertex`
now always returns the raw, unclamped depth).

**Root cause #2** (pre-existing, unrelated to depth clamp specifically):
diagnosed via env-var-gated `fprintf` debug tracing plus `--deqp-log-
images=enable` qpa-image extraction that the *test's own* fragment
shader output (`vec4(1.0, gl_FragCoord.z, 0.0, 1.0)`, encoding depth
directly in its G channel) was reading the **wrong component** --
always `.x` instead of `.z`. `FragmentWrapper.cpp`'s
`lowerFragmentInputLoad` resolved a system-value vector load (`Position`/
`FragCoord`) using only the signature element's `FirstComponent` (a
SPIR-V I/O-packing decoration, always 0 for a whole-`vec4` builtin),
completely ignoring the load intrinsic's own per-call `Component`
operand that the ordinary (non-system-value) varying-load path already
consumed correctly. Nothing before this milestone's own CTS coverage had
read any `gl_FragCoord` component but `.x`/`.y`, and those two happen to
route through a separate, unaffected helper -- so the bug had never been
exercised. Fixed by resolving the requested component from the load
call's own operand before dispatching to `loadFragmentSystemValue`.

**After both fixes**: 19/911 (2.1%) Passed, 173/911 (19.0%) Failed,
719/911 (78.9%) NotSupported -- **+3 newly passing, 0 regressions**
(176 -> 173 Failed exactly matches the 3 newly-passing cases). Of the
10-case `clip_volume.depth_clamp.*` group specifically: `triangle_list`/
`triangle_strip`/`triangle_fan` now **Pass**; the four `*_with_adjacency`
(triangle and line) variants fail with a pre-existing, out-of-scope
`VK_ERROR_INITIALIZATION_FAILED` at pipeline creation (a geometry-shader-
related gap this row does not touch); `line_list`/`line_strip`/
`point_list` fail "Rendered image(s) are incorrect" for a distinct,
pre-existing, out-of-scope reason -- traced (via a temporary debug
`fprintf` in `emitPointQuad`) to every one of this specific CTS case's
point/line vertices landing with their quad centered **exactly** on an
integer pixel-grid intersection, which this rasterizer's point/line-quad
coverage test excludes on both sides of the boundary (a general point/
line rasterization edge-case gap, not a depth-clamp-specific one, and not
introduced by this row -- no point-topology draw path existed in this
project's own unit-test coverage before this investigation either).
Neither the adjacency-pipeline gap nor the point/line grid-alignment gap
is in scope for H7d; both are left as follow-on roadmap items (see
`Roadmap.md`).

**Inventories**: `Vulkan14FeatureInventory.md`'s `depthClamp` row updated
to describe the corrected (post-interpolation) clamp site.
`VulkanExtensionInventory.md` confirmed to need no change: no extension
is touched by this row, only three core 1.0 feature bits.

**Milestone H7d closes** for `depthClamp`/`depthBiasClamp`/`depthBounds`
proper: `GraphicsPipeline.cpp` accepts all three states,
`Executor.cpp` implements all three end to end with correctness proven
by unit tests and this row's own real `deqp-vk` run, `check-feme` passes
in full (2011/2070, 59 pre-existing unrelated `Unsupported`, 0 `Failed`),
and `depthClamp`/`depthBiasClamp`/`depthBounds` all honestly read
`VK_TRUE`. The point/line grid-alignment rasterization gap and the
adjacency-pipeline gap surfaced by this row's own CTS run are unrelated,
pre-existing, out-of-scope limitations, broken out as new roadmap
follow-on rows rather than blocking this milestone's own completion.

## Roadmap H7e: measured impact (`wideLines`/`largePoints`)

**What changed**: `PhysicalDeviceInfo.cpp` raises `lineWidthRange[1]`/
`pointSizeRange[1]` from the degenerate `1.0` floor to `64.0` and flips
`wideLines`/`largePoints` to `VK_TRUE`. `Executor.cpp`'s line rasterizer
already threaded a real, variable `LineWidth` through its quad-expansion
path since roadmap F5, so `wideLines` needed no executor change at all.
`largePoints` needed real new work: a new `SignatureSystemValue::PointSize`
(mapped from SPIR-V `BuiltIn` 1 by `CanonicalizeStage.cpp`), a new
`RasterVertex::PointSize` field populated from an optional `VSPointSize`
signature-element lookup, and `emitPointQuad`'s half-extent now derives
from `Vtx.PointSize` clamped to `[1.0, RasterState::MaxPointSize]` (a new
`RasterState` field, `64.0` by default, cross-referenced by comment with
`PhysicalDeviceInfo.cpp`'s `pointSizeRange[1]` rather than a shared
constant -- the Vulkan layer depends on the Graphics layer, not the
reverse, so `Executor.cpp` cannot include `PhysicalDeviceInfo.h`) --
previously hardcoded to a hardware-independent 1 pixel regardless of any
written `gl_PointSize`.

**Case-list construction**: every group a source survey of the VK-GL-CTS
checkout under test found touching `wideLines`/`largePoints` directly
(`grep -rl "wideLines\|largePoints" external/vulkancts/modules/vulkan/`):
`dEQP-VK.rasterization.primitive_size.points.*` (8 cases, the dedicated
`largePoints` correctness test, `vktRasterizationTests.cpp`),
`dEQP-VK.rasterization.polygon_as_large_points.*` (roadmap H7c's own
`PolygonMode::Point` group, several variants of which additionally
require `largePoints`/`VK_KHR_maintenance5`'s `polygonModePointSize`),
`dEQP-VK.rasterization.line_width.*`, `dEQP-VK.draw.renderpass.
point_size_clamp.*` (the dedicated derived-point-size-clamp test,
`vktDrawPointClampTests.cpp`), `dEQP-VK.dynamic_state.*.line_width*` (78
cases, `vktDynamicStateLineWidthTests.cpp`), and every
`dEQP-VK.clipping.*point*`/`dEQP-VK.multiview.*point*`/`dEQP-VK.pipeline.
*line_width*` case -- 126 total unique cases after removing duplicates,
run in one `deqp-vk` invocation directly against `libfeme_vulkan.so`
(`VK_ICD_FILENAMES` pointed at `feme_icd.json`):

```
Test run totals:
  Passed:        31/126 (24.6%)
  Failed:        12/126 (9.5%)
  Not supported: 83/126 (65.9%)
```

**31 newly-passing cases**, all gated on `wideLines`/`largePoints` and
`NotSupported` before this row (confirmed: this device advertised neither
feature until this change). Of the 12 `Failed`:

- **2 are the pre-existing, already-tracked H7k grid-alignment gap**
  (`dEQP-VK.clipping.clip_volume.{depth_clamp,outside}.point_list`) --
  identical failure shape ("Rendered image(s) are incorrect") to the one
  H7d's own CTS run already found and broke out as roadmap H7k, not
  introduced by this row. These two cases do not depend on
  `largePoints`/`wideLines` at all (a `PointList`'s default, unwritten
  point size is 1.0 either way); they are only in this row's own
  126-case sample because the broad `*point*`/`*point_size*` glob this
  row's own case-list construction used incidentally swept them in.
- **9 are a previously-known-but-untracked generic pipeline-construction
  gap**, newly reachable because these 9 cases were gated behind
  `wideLines`/`largePoints` and never ran before:
  `dEQP-VK.draw.renderpass.point_size_clamp.point_size_clamp_max` and 8
  of `dEQP-VK.dynamic_state.monolithic.line_width.{dyna_static,
  static_dyna}.*` fail `vkCreateGraphicsPipelines` with
  `VK_ERROR_INITIALIZATION_FAILED`, `feme-cpu-wrap-vertex`/
  `feme-cpu-wrap-fragment` reporting "... stage wrapper requires attached
  feme.signature metadata" -- the exact same shape roadmap H3a already
  root-caused and fixed for the *fragment* side (a `Function::Create`+
  `copyAttributesFrom` rebuild in `SPIRVResourceLowering.cpp`/
  `ResourceLowering.cpp` silently drops function-attached metadata), but
  this row's own reproduction (a separate real `dEQP-VK.dynamic_state.
  monolithic.cb_state.*` sample also shows a mixed pass/fail rate
  unrelated to line width or point size) confirms the equivalent
  *vertex*-side gap H3a's own writeup had already flagged as "not yet
  independently tracked" is still open -- broken out below as a new
  follow-on row, H7m, rather than fixed as part of this one (out of
  scope: unrelated to point-size/line-width logic itself, and a real fix
  needs its own root-cause investigation mirroring H3a's).
- **1 is the pre-existing, already-tracked mesh-shading milestone-6
  deviation** (`dEQP-VK.dynamic_state.monolithic.rs_state.
  line_width_mesh`, `error: feme-cpu-linearize: ... has an internal
  branch in 'Flow'; unsupported (roadmap milestone 6 deviation)`) -- the
  error message itself names its own existing tracking, confirmed
  unrelated to line width by a same-group `rs_state.*` sample (7/10
  failing for other, equally pre-existing, unrelated reasons).

**0 regressions; all 12 failures pre-existing.** The 83 `NotSupported`
cases are all legitimately gated on something else entirely: this
device's `maxFramebufferWidth`/`maxFramebufferHeight`/
`maxViewportDimensions` (`4096`) reject the 3 largest
`primitive_size.points.point_size_{8192,9216,10240}`-equivalent render
sizes outright (an honest, pre-existing device-limit gate, not a new
gap), several `primitive_size.points.point_size_{128,256,512,2048}`
cases reject an sRGB/float attachment format combination this device
does not support, and every `polygon_as_large_points`
`polygonModePointSize`-gated variant correctly reports `NotSupported`
since this device does not implement `VK_KHR_maintenance5` at all (an
orthogonal, unrelated extension gap).

**Inventories**: `Vulkan14FeatureInventory.md` updated -- `wideLines` and
`largePoints` both flip to `yes`, the 1.0 feature-advertised count rises
from 13 of 55 to 15 of 55 (total 54 of 150 to 56 of 150), and the
"graphics-specific unimplemented" bullet drops from 6 to 4.
`VulkanExtensionInventory.md` confirmed to need no change: no extension
is touched by this row, only two core 1.0 feature bits.

**Milestone H7e closes.** `GraphicsPipeline.cpp`/`PhysicalDeviceInfo.cpp`
advertise both features honestly, `Executor.cpp` implements both end to
end (line width needing no change, point size proven by 3 new
`ExecutorTest.cpp` cases and this row's own real `deqp-vk` run), and
`check-feme` passes in full (2016/2075, 59 pre-existing unrelated
`Unsupported`, 0 `Failed`), up from H7d's own 2011/2070 by exactly the 5
new tests this row adds (3 `ExecutorTest.cpp` `PointSize` cases, 1
`CanonicalizeStageTest.cpp` `PointSize` builtin-mapping case, 1
`PhysicalDeviceInfoTest.cpp` line-width/point-size-range case). The
generic vertex-side signature-metadata gap this row's own CTS run
confirmed still open is broken out separately as roadmap H7m.

## Roadmap H7f: measured impact (`sampleRateShading`/`alphaToOne`)

**What changed**: `GraphicsPipeline.cpp`'s `translateGraphicsPipeline` now
translates `sampleShadingEnable`/`alphaToOneEnable` into
`Result.SampleShadingEnable`/`Result.AlphaToOneEnable` instead of
rejecting both outright (`alphaToCoverageEnable` remains rejected, broken
out below as roadmap H7n). `Executor.cpp`'s `processTile` wraps its
FSInput-build/dispatch/merge sequence in an outer per-sample pass loop
(`PassCount = Pipeline.getSampleShadingEnable() ? SampleCount : 1`), each
pass narrowing a local `PassInvocations` copy's `SampleIndex`/`Coverage`
to that one sample before dispatch -- always shading at full sample rate
when `sampleShadingEnable` is set (a spec-conformant simplification:
running every sample trivially satisfies "at least
`ceil(minSampleShading * rasterizationSamples)`" for any
`minSampleShading` in `[0,1]`, so its exact value is never stored). Alpha
forcing (`RGBA[3] = 1.0` when `getAlphaToOneEnable()`) applies to every
attachment right after the existing alpha-to-coverage multiply.

**Case-list construction**: 224 cases from the real VK-GL-CTS mustpass
list (`external/vulkancts/mustpass/main/vk-default/pipeline/monolithic/
{multisample.txt,multisample-shader-builtin.txt,
extended-dynamic-state.txt}`), restricted to feme-supported sample counts
(1/2/4/8): `multisample_shader_builtin.sample_id.*` (6), `multisample.
alpha_to_one.samples_{1,2,4,8}[_sparse]` (68), `multisample.
min_sample_shading_{enabled,disabled}.min_*.samples_{2,4,8}.quad` (150),
and `extended_dynamic_state.after_pipelines.*alpha_to_one*` (4), run in
one `deqp-vk` invocation directly against `libfeme_vulkan.so`.

**First run** (both feature bits flipped to `VK_TRUE`):

```
Test run totals:
  Passed:          4/224 (1.8%)
  Failed:         96/224 (42.9%)
  Not supported: 124/224 (55.4%)
```

The 4 `Passed` are all 4 real `alpha_to_one.samples_{1,2,4,8}` cases
(confirms `alphaToOneEnable`'s own executor implementation end to end).
All 96 `Failed` are `min_sample_shading_{enabled,disabled}.*`/
`sample_id.*` cases, every one failing identically at shader-compilation
time: `error: feme-cpu-simdize: function 'main' has a divergent value ''
of vector type; only a constant-index insertelement chain, a phi, a
select, a shufflevector, elementwise arithmetic/cast, a vector
comparison, a homogeneous vectorizable intrinsic call, or a
resource/image load is supported (roadmap milestone 7 deviation)`,
`VK_ERROR_INITIALIZATION_FAILED` at pipeline creation, before any
fragment-stage execution -- meaning before any of this row's own executor
per-sample-shading code would even run.

**Root cause investigated**: the real CTS shader source
(`vktPipelineMultisampleShaderBuiltInTests.cpp:5512`,
`vktPipelineMultisampleTests.cpp:6899`) computes a per-invocation buffer
index from `gl_SampleID` (`pos = ((coord.y * width) + coord.x) * samples
+ int(gl_SampleID)`) and stores through it into a storage buffer.
Crucially, **`min_sample_shading_disabled.*` cases fail identically to
`min_sample_shading_enabled.*`** (both use the same fixed shader,
regardless of whether the pipeline itself enables sample shading) --
confirming the gap is generic to a divergent (per-invocation-computed)
buffer store address, not anything specific to per-sample shading or to
this row's own executor changes. `SIMDize.cpp`'s divergent-vector-value
producer classification has no case for a `GetElementPtrInst` with a
divergent index, and its resource-call handling explicitly excludes a
`StoredValue` call's own address operand from producer classification
(`if ((Matched && !Matched->StoredValue) || matchImageCall(*CI))`) --
unlike groupshared memory, which already has real scatter-store support
for a divergent address. A minimal, hand-written-IR `ExecutorTest.cpp`
case reading `SV_SampleIndex` and doing ordinary cast/arithmetic on it
(no buffer store) compiles and passes cleanly through `SIMDize.cpp`,
confirming the gap is specifically about a divergent *store address*, not
about reading a per-lane system value at all. Broken out as its own
follow-on row, H7o, rather than fixed here: scoping a fix needs a real IR
reduction of this exact case, the same technique the H6g-b/H6j/H6k/H6l
chain used throughout, and is a substantial, separate compiler-pass
change (most likely a new `llvm.masked.scatter`-based lowering) out of
this row's own scope.

**Decision**: `alphaToOne` flips to `VK_TRUE` (fully conformant).
`sampleRateShading` stays `VK_FALSE` -- advertising it before a real
conformance case exercising it can pass would itself be a conformance
violation -- pending H7o.

**Second run** (`alphaToOne` on, `sampleRateShading` reverted to off):

```
Test run totals:
  Passed:          4/224 (1.8%)
  Failed:          0/224 (0.0%)
  Not supported: 220/224 (98.2%)
```

**0 regressions; 0 failures.** The 220 `NotSupported` cases are all
`min_sample_shading*`/`sample_id.*` (now correctly gated on
`sampleRateShading` again, `NotSupported (Requested core feature is not
supported: sampleRateShading at vktTestCase.cpp:1497)`) and the 64
`extended_dynamic_state.after_pipelines.*alpha_to_one*` cases (gated on
`VK_EXT_extended_dynamic_state3`, an orthogonal, unrelated extension this
device does not implement).

**Inventories**: `Vulkan14FeatureInventory.md` updated -- `alphaToOne`
flips to `yes`; `sampleRateShading` stays `no`, with a note pointing at
H7o. The 1.0 feature-advertised count rises from 18 of 55 to 19 of 55
(total 59 of 150 to 60 of 150 -- this edition also found the "Findings"
table's own 1.0 row/total had drifted stale since H7a, corrected
alongside this row's own change), and the "graphics-specific
unimplemented" bullet drops from 4 to 3. `VulkanExtensionInventory.md`
confirmed to need no change: no extension is touched, only one core 1.0
feature bit. `FeMeVulkanDesign.md`'s H7 status paragraph updated.

**Milestone H7f partially closes.** `alphaToOne` is done: translated,
executor-implemented, unit-tested (`ExecutorTest.cpp`'s
`AlphaToOneEnableForcesOutputAlphaToOne`, `GraphicsPipelineTest.cpp`'s
`TranslatesSampleShadingAndAlphaToOneState`), and confirmed conformant by
real CTS. `sampleRateShading`'s own translation/executor plumbing is
equally done and unit-tested (`ExecutorTest.cpp`'s
`SampleShadingEnableInvokesFragmentOncePerSample`), but its feature bit
stays closed pending a new, separately-tracked compiler gap, H7o.
`alphaToCoverageEnable` (found, during this row's own investigation, to
have been inaccurately described as "already-implemented" by H7f's
original roadmap text) remains unimplemented and untracked with its own
feature bit, broken out as H7n. `check-feme` passes in full (2020/2079,
59 pre-existing unrelated `Unsupported`, 0 `Failed`), up from H7e's own
2016/2075 by exactly the 4 new tests this row adds.

## Roadmap H7o: measured impact (two `sampleRateShading` pipeline-creation-time gaps)

**What changed**: two real, distinct fixes, both to CPU-target compiler
passes, neither touching any feature bit directly.

1. `SIMDize.cpp`'s `checkVectorDecompositionSupported` gained a new
   producer case: a plain (non-groupshared) `LoadInst` of vector type at a
   divergent address is now a supported producer, decomposed into `N`
   widened per-component values (`widenScalarizedFallback`) exactly like a
   resource-call load already was. A groupshared load of vector type stays
   excluded (its own gather-based path, `widenGroupSharedLoad`, does not
   yet support a vector-typed result).
2. `RootConstantLowering.cpp`'s `addRootConstantParams` and
   `SPIRVPushConstantLowering.cpp`'s inline `SPIRVPushConstantLoweringPass
   ::run` `Function::Create` block both now copy the original function's
   metadata (`getAllMetadata`/`setMetadata`) onto the rebuilt function that
   appends trailing `root_constants`/`root_constant_size` ABI parameters --
   matching the pattern the other ~4 such Function-replacement helpers
   (`ResourceLowering.cpp`, `SPIRVResourceLowering.cpp`,
   `SPIRVSubpassLowering.cpp`) already used. `GlobalObject::
   copyAttributesFrom()` does not copy function-attached metadata; these
   two helpers had silently dropped it entirely.

**Root cause investigated**: a real IR reduction, capturing the actual
module right before `SIMDizePass` for a real
`min_sample_shading_enabled.min_0_0.samples_2.quad` re-run (temporary
`FEME_DEBUG_DUMP_PRE_SIMDIZE` instrumentation in `Pipeline.cpp`, reverted
after use), showed H7f's own original diagnosis was wrong: the failing
value was not a `GetElementPtrInst`-addressed divergent buffer *store*
(already fully supported, see `widenResourceCall`) but the **vertex**
shader (`quad_vert`) of the same test group, whose classic
`positions[gl_VertexIndex]` local constant lookup table -- a
per-invocation-divergent index into an ordinary, non-groupshared
`<4 x float>` array -- had no producer case at all in
`checkVectorDecompositionSupported`'s switch. `widenScalarizedFallback`
(the generic instruction-widening fallback) would have built an illegal
nested `<W x <N x T>>` vector type had a vector-typed load been let
through unconditionally, explaining why the check existed in the first
place; the fix instead decomposes the per-lane load's own vector result
into `N` separate widened components. Confirmed against the real captured
module (`feme-opt --llvm -passes=feme-cpu-simdize`): compiled cleanly,
zero errors, after the fix.

Re-flipping `sampleRateShading` to `VK_TRUE` and re-running the 4 targeted
`min_sample_shading_*.samples_2.quad` cases with only this first fix
applied surfaced a **second**, distinct failure, reachable only once the
first was fixed: `feme-cpu-wrap-fragment: fragment stage wrapper requires
attached feme.signature metadata`, for all 4 cases. Root-caused by
manually reducing the group's second fragment shader
(`copy_sample_frag`, `subpassLoad` + a push constant, no bound resource)
through `glslangValidator` → `feme-translate --spirv-to-llvmir` →
`feme-opt --feme-graphics-canonicalize-stage,feme-graphics-validate-stage`
(confirming `!feme.signature` metadata is correctly attached at that
point) and then, since `feme-opt`'s tool driver has no registered
pass-pipeline callback for `SPIRVSubpassLoweringPass` (making a
`subpassLoad`-using shader impossible to reduce standalone via
`feme-opt` alone), a second temporary debug dump
(`FEME_DEBUG_DUMP_PRE_WRAP`, before `WaveLoweringPass`, also reverted
after use) against a real failing case -- confirming `!feme.signature`
was genuinely missing by that point, and tracing backward through every
Function-replacement helper in the pipeline to find the two (of ~6) that
were missing the metadata-copy step every other one already had.

**Targeted re-run** (both fixes applied, `sampleRateShading` re-flipped to
`VK_TRUE`), the same 4 cases from H7f's own targeted sweep:

```
min_sample_shading_disabled.min_0_0.samples_2.quad   Pass
min_sample_shading_disabled.min_1_0.samples_2.quad   Pass
min_sample_shading_enabled.min_0_0.samples_2.quad    Pass
min_sample_shading_enabled.min_1_0.samples_2.quad    Fail (Got less unique colors than requested through minSampleShading)
```

**A third, distinct, still-open bug**: `min_sample_shading_enabled.
min_1_0.samples_2.quad` (`min_1_0` = `minSampleShading` 1.0, the
strictest setting, requiring one genuinely distinct shaded value per
covered sample) fails for real, not merely `NotSupported`. The full
`min_sample_shading_*` group (60 cases) confirms this is reproducible in
isolation (10 Pass, 0 Fail when run one case at a time; a grouped/glob run
showed apparent nondeterminism for this exact case, `NotSupported`
instead of `Fail`, not fully understood but not trusted -- the isolated
result is the reliable one). Root cause: this case's own `color_frag`
shader derives its output purely from `fract(gl_FragCoord.xy)`, with no
`gl_SampleID`/`SV_SampleIndex` dependency at all. `Executor.cpp`'s
`processTile` per-sample pass loop (H7f) deliberately keeps every
interpolated varying, including `gl_FragCoord`, at the pixel center on
every pass -- an explicit, documented design choice ("Interpolated
varyings stay pixel-center on every pass... a shader whose output does
not depend on `SV_SampleIndex`/`gl_SampleID` simply recomputes the same
color on every pass") that turns out to be insufficient for full
per-sample-value conformance: any invocation-count requirement is
over-satisfied (this loop always runs at full sample rate, exceeding any
`minSampleShading` minimum), but a `gl_FragCoord`-derived shader's
per-sample *value* never actually varies, so the CTS's own "sufficient
color diversity" check fails at `minSampleShading = 1.0` specifically.
Not fixed here -- tracked as a new roadmap follow-on, H7p (widening
`Executor.cpp`'s per-sample loop to evaluate `gl_FragCoord` at the actual
per-sample position `SamplePositions` already tracks, not the pixel
center, on every pass).

**Full sweep** (124 cases, the same construction H7f's own 224-case sweep
used -- `multisample_shader_builtin.sample_id.*`, `multisample.
alpha_to_one.samples_{1,2,4,8}[_sparse]`, `multisample.
min_sample_shading_{enabled,disabled}.min_*.samples_{2,4,8}.quad`,
`extended_dynamic_state.after_pipelines.*alpha_to_one*`, restricted to
feme-supported sample counts -- rerun with `sampleRateShading` correctly
left `VK_FALSE` given the H7p gap above):

```
Test run totals:
  Passed:          4/124 (3.2%)
  Failed:          0/124 (0.0%)
  Not supported: 120/124 (96.8%)
```

**0 regressions; 0 failures.** Identical shape to H7f's own second run:
the 4 `Passed` are `alpha_to_one`'s own real cases (unaffected by this
row), and all `NotSupported` cases are gated on `sampleRateShading`
staying off (H7p) or the orthogonal, unrelated `VK_EXT_extended_dynamic_
state3` extension.

**Decision**: `sampleRateShading` stays `VK_FALSE` -- the H7p gap above is
a real, mandatory-case conformance blocker, so flipping the bit now would
be a conformance violation. Both real fixes land regardless: they are
genuine, valuable, independently-useful compiler-correctness
improvements (a common vertex-shader idiom and a common push-constant-
only fragment-shader shape both now compile correctly where they
previously did not), confirmed by `ninja check-feme` and by the CTS
sweep above showing zero regressions.

**Inventories**: `Vulkan14FeatureInventory.md` updated -- `sampleRateShading`
stays `no`, comment updated to describe both fixes and point at the new
H7p blocker instead of H7o (now itself fixed). `VulkanExtensionInventory.md`
confirmed to need no change. `FeMeVulkanDesign.md`'s H7 status paragraph
updated. `FeMeCPUDesign.md`'s `SIMDizePass` producer-shape count updated
from nine to ten (the new `LoadInst` case).

**Milestone H7o closes** (struck through in `Roadmap.md`), leaving a new,
narrower follow-on, **H7p**, to unblock `sampleRateShading`'s own feature
bit. `check-feme` passes in full (2022/2081, 59 pre-existing unrelated
`Unsupported`, 0 `Failed`), up from H7f's own 2020/2079 by exactly the 2
new tests this row adds (`SIMDizeTest`'s
`DecomposesPrivateMemoryDivergentLoadIntoExtractElement` and a new lit
test, `Transforms/CPU/simdize-private-vector-load-scalarize.ll`; the
metadata-copy fix's own coverage extends two pre-existing lit tests,
`Transforms/CPU/root-constant-lowering.ll` and
`Transforms/CPU/spirv-push-constant-lowering.ll`, each gaining a new
`CHECK`'d case, adding no further test count).

## Roadmap H7p: measured impact (closing `sampleRateShading` for real)

**Two independently-blocking gaps, both fixed this row.**

**Gap 1 (`gl_FragCoord` not varying per sample position)**: `Executor.cpp`'s
`processTile` per-sample shading loop (H7f) deliberately left every
interpolated varying, including `gl_FragCoord`/`SV_Position`, at the pixel
center on every one of a pixel's per-sample passes. Harmless for a shader
whose color genuinely depends on `gl_SampleID`, but wrong for one that
depends on `gl_FragCoord` alone (like the real
`dEQP-VK.pipeline.monolithic.multisample.min_sample_shading_enabled.
min_1_0.samples_2.quad` case's own `color_frag` shader,
`fragColor = vec4(fract(gl_FragCoord.xy), 0.0, 1.0)`): every pass recomputed
the identical color, failing the CTS's own per-pixel "at least
`ceil(minSampleShading * sampleCount)` unique colors" check for the
strictest `minSampleShading = 1.0` case. Fixed by re-deriving each pass's
`Position.xy` from `SamplePositions[PassSample]`'s own real per-sample
offset instead of leaving it pixel-center.

Proven correct in isolation with a scratch, temporary unit test (not
committed) directly exercising the raw MSAA write path across a full
32x32, 2-sample image with an `R32G32B32A32_FLOAT` attachment (avoiding
UNORM8 saturation, which produced a false failure signal on a first,
incorrect attempt using an unclamped raw-`gl_FragCoord` shader) and a
manual `fract()` check in the C++ code: **0 mismatches**.

**Gap 2 (subpass-input sample-count mismatch)**: despite gap 1's fix, the
real CTS case still failed identically. Decoding the QPA log's embedded
per-sample readback PNGs showed a suspicious column-based checkerboard
pattern -- the two "single-sampled" per-sample images were byte-identical
and showed data addressed at the wrong stride, not simply "stale"/
"unwritten" data. Root cause: the real CTS test structure renders subpass
0 with a genuinely multisampled pipeline (writing `color_frag`'s output
into a real multisample color attachment), then N further subpasses, each
with a *different*, single-sample pipeline, each reading back exactly one
sample of subpass 0's own attachment via `subpassLoad(imageMS,
pushConstants.sampleId)`. `CommandBuffer.cpp`'s `buildSubpassInputHeap`
computed every heap entry's `RowPitch`/`SampleStride` -- including subpass
**input** attachments -- from the *currently bound draw's own pipeline*
sample count, not the input attachment's own real sample count. For this
case, subpass 1+'s single-sample pipeline was used to address subpass 0's
4x-multisampled attachment, making `RowPitch` too small by the real sample
factor and `SampleStride` an outright `0` (`SampleCount > 1 ? ElemSize :
0` evaluated false), aliasing every sample read to the same texel --
exactly the observed checkerboard artifact.

Fixed by adding a new `SubpassInputSampleCounts` vector to
`ResolvedDrawAttachments` (populated per input-attachment index from each
view's own image in `resolveDrawAttachments`), threading it through
`buildSubpassInputHeap`'s new `SubpassInputSampleCounts` parameter, and
using it (rather than the blanket pipeline sample count) specifically for
the `SubpassInputs` heap-entry path; the `Attachments`/`DepthStencil`
fallback path still correctly uses the bound pipeline's own sample count,
since a subpass's own color/depth/stencil attachments must match its own
pipeline's sample count by spec.

New permanent regression test, `DrawTest.cpp`'s
`SubpassLoadReadsAnEarlierSubpassMultisampledColorOutputWithADifferentPipelineSampleCount`
(seeded-memory technique, modeled on the neighboring
`SubpassLoadReadsBackAnExplicitSampleOfTheColorAttachmentItWrote`/
`MultiviewInputAttachmentReadsBackAnEarlierSubpassColorOutput` tests):
builds a classic 2-subpass render pass where subpass 0's attachment is
genuinely 4x-multisampled (seeded directly, no real draw, via
`LOAD_OP_LOAD`) and subpass 1 uses a different, single-sample pipeline to
`subpassLoad` one explicit sample back into a single-sample attachment.
**Confirmed to fail without the fix** (reverted `CommandBuffer.cpp`
locally, rebuilt, reran: fails with wrong values at the exact same
checkerboard-style pattern as the real CTS failure) and to **pass with
the fix**.

**Real CTS re-run, both fixes in place:**

```
dEQP-VK.pipeline.monolithic.multisample.min_sample_shading_enabled.min_1_0.samples_2.quad
  Pass (Got proper count of unique colors)
```

Broader sweep across every feme-supported-sample-count
`min_sample_shading_enabled`/`min_sample_shading_disabled` case (7 cases:
`min_0_25`/`min_0_5`/`min_1_0` at `samples_2`, `min_1_0` at
`samples_4.quad`, `samples_1` baseline, and the disabled-vs-enabled pair
at `samples_2`/`samples_4`):

```
Test run totals:
  Passed:        7/7 (100.0%)
  Failed:        0/7 (0.0%)
```

No regression in the neighboring `alpha_to_one.*` cases (3/3 still pass).
The already-tracked, unrelated `sample_id.*` pipeline-creation-time
failures (H7o's own `SIMDize.cpp` divergent-store-address gap, distinct
from anything H7p touches) remain exactly as before -- confirmed via a
72-case sweep across `min_sample_shading_*`/`sample_id.*`: 30 pass, 6 fail
(all `sample_id.*`, all `VK_ERROR_INITIALIZATION_FAILED` at
`vkPipelineConstructionUtil.cpp:176`, unrelated to this row), 36
`NotSupported` (higher sample counts, e.g. `samples_8`/`samples_64`, not
supported by this software rasterizer).

**Decision**: with every mandatory, feme-supported-sample-count
`min_sample_shading*` case now passing for real, `sampleRateShading`
honestly flips to `VK_TRUE` in `PhysicalDeviceInfo.cpp`.

**Inventories**: `Vulkan14FeatureInventory.md` updated -- `sampleRateShading`
now `yes`; the 1.0 feature-advertised count rises from 19 of 55 to 20 of
55 (total 60 of 150 to 61 of 150); the "graphics capabilities among
unimplemented 1.0 bits" count drops from 3 to 2
(`shaderClipDistance`/`shaderCullDistance` only). `VulkanExtensionInventory.md`
confirmed to need no change (a core feature-bit row, not an extension).
`FeMeVulkanDesign.md`'s H7 status paragraph updated.

**Milestone H7p closes** (struck through in `Roadmap.md`). `check-feme`
passes in full (2024/2083, 59 pre-existing unrelated `Unsupported`, 0
`Failed`), up from H7o's own 2022/2081 by exactly 1 new test
(`DrawTest.cpp`'s new subpass-sample-count regression test;
`PhysicalDeviceInfoTest.cpp`'s `sampleRateShading == VK_TRUE` update edits
an existing case rather than adding one; a scratch diagnostic test used
during investigation was written, used, and then removed, never
committed).

## Roadmap H7n: measured impact (`alphaToCoverageEnable`, plus H7q/H7r follow-ons)

**Feature.** `VkPipelineMultisampleStateCreateInfo::alphaToCoverageEnable`
had no implementation at all: `GraphicsPipeline.cpp` rejected it at
pipeline-creation time, and an exhaustive search of `Executor.cpp` found no
coverage-mask handling anywhere in the blend/coverage path. Unlike
`alphaToOneEnable` (roadmap H7f), `alphaToCoverageEnable` has no
`VkPhysicalDeviceFeatures` gate in the Vulkan spec at all -- confirmed by
reading the real CTS's own `MultisampleTest`/`AlphaToCoverageTest`
`checkSupport` overrides, neither of which checks any feature bit beyond the
base `MultisampleTest::checkSupport` -- so there is no feature-bit flip for
this row, only real behavior.

**Algorithm.** Reading the real CTS's `AlphaToCoverageInstance::verifyImage`
(`vktPipelineMultisampleTests.cpp`) showed the verification is a
resolved-average tolerance check, not an exact-value one, and that every
geometry type's vertex color is `(1.0, 0.0, 0.0, alpha)` with `alpha` one of
`1.0` (opaque), `0.25` (translucent), or `0.0` (invisible). A per-sample
threshold `T_S = (S + 0.5) / SampleCount`, clearing a sample's coverage bit
whenever the shaded fragment's location-0 alpha falls below it, produces
exactly `round(alpha * SampleCount)` covered samples for any alpha in
`[0, 1]` -- verified by hand against all three tolerance bounds (opaque:
`[0.99, 1.01]`; translucent: `[0.0, 0.52]`; invisible: `[0.0, 0.01]`).

**Implementation.** `GraphicsPipeline.cpp` now translates
`alphaToCoverageEnable` directly (no rejection, no feature-bit check) into a
new `GraphicsPipelineState::AlphaToCoverageEnable`/`GraphicsPipeline::
getAlphaToCoverageEnable()` field, mirroring the existing `AlphaToOneEnable`
plumbing, and includes it in the pipeline cache key. `Executor.cpp` adds a
dedicated `FSAlphaToCoverage` signature-element lookup (`findElementByLocation
(FSSig, Output, 0)`, looked up independently of `FSColors[0]` specifically to
also cover the real CTS's own "unused color attachment" shape, where
location 0's alpha is the only coverage-relevant output but location 0 itself
is not bound to any real color attachment), forces `UseEarlyDepthStencil =
false` whenever the feature is enabled (the mask depends on the fragment
stage's own shaded output, unknown until after it runs -- the same reason
`SV_Depth`/discard/demote already force the late path), and introduces a new
per-lane `BaseCoverage` local: the raw per-sample coverage mask ANDed with the
alpha-derived mask, read by both the depth/stencil test block's own
sample-iteration loop and the post-test `PassMask` fallback, so alpha-to-
coverage culling applies uniformly whether or not a depth/stencil test
actually runs for a given pipeline.

**Unit tests (new).** `GraphicsPipelineTest.cpp`'s `TranslatesAlphaToCoverageState`
(alphaToCoverageEnable alone translates and defaults correctly) and an update
to the existing `TranslatesSampleShadingAndAlphaToOneState` to also exercise
it, replacing the now-obsolete `RejectsAlphaToCoverageEnable`.
`ExecutorTest.cpp`'s `AlphaToCoverageEnableGeneratesPerSampleCoverageFromAlpha`
(SampleCount=4, alpha=0.25 fully-covering triangle, raw non-resolved MSAA
attachment, asserts exactly sample 0 is covered and samples 1-3 stay at their
clear value -- directly validating the per-sample threshold math).

**Real CTS re-run.** `dEQP-VK.pipeline.monolithic.multisample.
alpha_to_coverage.*` (the main group): **12/12 feme-supported-sample-count
cases pass, 0 failures** (36 `NotSupported` for sample counts 32/64 and
"sparse" image backing -- pre-existing, unrelated gaps).

Two related CTS groups exercise the same feature but surfaced two further,
independent gaps unrelated to the coverage-mask logic itself, each broken out
into its own roadmap row rather than folded into this one:

- `alpha_to_coverage_no_color_attachment.*` (a `RENDER_TYPE_DEPTHSTENCIL_ONLY`
  render, zero color attachments): all feme-supported-sample-count cases
  initially failed pipeline creation with `"the pipeline's rasterization
  sample count disagrees with its render target's"` (diagnosed via
  `FEME_VULKAN_LOG_CREATION_ERRORS=1`). Root cause: `GraphicsPipeline.cpp`'s
  `getRenderTargets` only ever set `Targets.SampleCount` inside the loop over
  `Subpass.ColorAttachments`, which never executes when that list is empty,
  silently leaving the render target's derived sample count at its
  single-sample default for any depth/stencil-only subpass. Fixed by roadmap
  H7q (falling back to the depth/stencil attachment's own sample count when
  there is no color attachment); re-run after that fix: **3/3
  feme-supported-sample-count cases pass** (up from 0/3, 9 `NotSupported` for
  other sample counts).
- `alpha_to_coverage_unused_attachment.*` (color output written to location
  1, location 0 unused, `AlphaToCoverageColorUnusedAttachmentInstance`'s own
  hard-coded `VK_FORMAT_R5G6B5_UNORM_PACK16` color format): every
  feme-supported-sample-count case fails at `vkCreateImage` time with
  `VK_ERROR_FORMAT_NOT_SUPPORTED` -- confirmed via a repository-wide search
  that no packed 16-bit format has any support anywhere in the image/format
  layer, entirely unrelated to alpha-to-coverage's own logic. Tracked as a
  new follow-on, H7r, not yet fixed. The `FSAlphaToCoverage` direct-location-0
  lookup design (built specifically to support this CTS shape) remains
  unverified against real CTS pending H7r, verified only by the
  `ExecutorTest.cpp` unit test above.

A broader `dEQP-VK.pipeline.monolithic.multisample.*` sweep (10576 cases) was
also run to check for regressions from the H7q render-target sample-count
fix: 189 pass, 63 fail, 10324 `NotSupported`. Every failure was confirmed
pre-existing and unrelated to this row's own changes -- `mixed_count`,
`sampled_image.*`, `3d.*` (pre-existing, unrelated image/sampling gaps),
`a2c_with_a2one.*`/`sample_rate_a2c.*` (combination cases that fail before
ever reaching coverage-mask computation, on pre-existing push-constant-range
coverage gaps, a JIT symbol-materialization gap for `frag_depth` export, and
an unsupported large-resource-array binding shape), and
`alpha_to_coverage_unused_attachment.*` (H7r's own `R5G6B5` gap, above) --
none newly introduced.

**`ninja check-feme`.** Passes in full at **2026/2085** (59 pre-existing,
unrelated `Unsupported`, 0 `Failed`), up from H7p's own 2024/2083 baseline by
3 new tests: `GraphicsPipelineTest`'s `TranslatesAlphaToCoverageState` and
`AcceptsMultisampledZeroColorRenderPass` (H7q's own regression test), and
`ExecutorTest`'s `AlphaToCoverageEnableGeneratesPerSampleCoverageFromAlpha`.

**Documentation.** `Vulkan14FeatureInventory.md` updated with a confirmatory
note (not a feature-bit row, since none exists for this field).
`VulkanExtensionInventory.md` confirmed no change needed (a core
pipeline-state field, not an extension). `FeMeVulkanDesign.md`'s H7 status
paragraph updated. `Roadmap.md`'s H7n struck through; H7q (the
render-target-sample-count fix) struck through alongside it; H7r (the
`R5G6B5` format gap) added as a new, open, P3 follow-on.

## Roadmap H7r: measured impact (packed 16-bit formats)

**Gap.** `dEQP-VK.pipeline.monolithic.multisample.alpha_to_coverage_unused_attachment.*`
(H7n's own follow-on) failed every feme-supported-sample-count case at
`vkCreateImage` time with `VK_ERROR_FORMAT_NOT_SUPPORTED`, because
`AlphaToCoverageColorUnusedAttachmentInstance`'s own real color image is
hard-coded to `VK_FORMAT_R5G6B5_UNORM_PACK16`, and no packed 16-bit format
had any support anywhere in `feme`'s image/format layer. Investigating the
real CTS's own test structure (`vktPipelineMultisampleTests.cpp`) further
showed this CTS case in fact needs *two* independent things: real
`R5G6B5_UNORM_PACK16` support (this row's own scope), and support for a
`VK_ATTACHMENT_UNUSED` color attachment slot in a subpass (a separate,
larger, previously-untracked gap, broken out as H7s below rather than
attempted here).

**Format survey.** The real CTS's own `vktApiImageClearingTests.cpp`
`colorImageFormatsToTest` array is a ready-made, authoritative "packed
16-bit formats worth testing" list. Cross-referencing it against `feme`'s
existing `ResourceFormat` enum selected 7 missing core Vulkan 1.0 formats:
`R4G4B4A4_UNORM_PACK16`, `B4G4R4A4_UNORM_PACK16` (distinct bit order from
the already-supported EXT `A4R4G4B4`/`A4B4G4R4`), `R5G6B5_UNORM_PACK16`,
`B5G6R5_UNORM_PACK16` (3-component, no alpha), `R5G5B5A1_UNORM_PACK16`,
`B5G5R5A1_UNORM_PACK16`, and `A1R5G5B5_UNORM_PACK16`. `R4G4_UNORM_PACK8` was
excluded as out of the "16-bit" scope. Exact bit layouts were confirmed
against the real Vulkan spec's own "Packed Formats" section
(`docs.vulkan.org/spec/latest/chapters/formats.html`), not trusted from an
initial, partially-unreliable web-search summary alone.

**A critical layering constraint.** `feme/runtime/CPU/FeMeRuntimeCPU.c`'s
image-sampling tables (`femeRTImageFormatElementSize`/
`femeRTUnpackImageTexel`/`femeRTUnpackImageTexelI32`) switch on
`ResourceFormat`'s raw enum *ordinal* via hard-coded integer case labels
(e.g. `case 28:`), not symbolic names -- an ABI the compiler and CPU
runtime share informally. Inserting the 7 new values anywhere before the
existing tail (`ASTC_12x12_SFLOAT`) would silently renumber every
subsequent value and break these hard-coded cases. All 7 were instead
appended at the very end of the enum, guaranteeing zero ordinal shift for
any existing value.

**Implementation.** The 7 new formats are real, working color-attachment
formats -- matching `A1B5G5R5_UNORM`'s precedent (roadmap E5), not
`A4R4G4B4_UNORM`/`A4B4G4R4_UNORM`'s recognized-only one (roadmap E19),
since the blocking CTS case needs a real, functional attachment, not just
a legal `VkFormat`. `Format.cpp`'s `mapVkFormat`/`formatElementSize`,
`RenderPass.cpp`'s `isSupportedColorAttachmentFormat`, and
`ImageFixture.cpp`'s `getFormatInfo`/`parseFixtureFormat`/
`packClearColor`/`unpackColor` were all updated with real bit-packing
logic per the confirmed layouts. None of the 7 gets a `FeMeRuntimeCPU.c`
sampling case (matching the `A4R4G4B4_UNORM` precedent for un-sampled
recognized formats), so `Format.cpp`'s `formatFeatureFlags` correctly
leaves `SAMPLED_IMAGE_BIT` unset for all 7 via its existing `default:`
fallthrough -- verified by inspection, no code change needed there.
`R5G6B5_UNORM`/`B5G6R5_UNORM` have no alpha channel: `packClearColor`
ignores the clear color's alpha component and `unpackColor` always reads
it back as fully opaque (`1.0`), the same "missing channel reads as its
identity value" precedent `A8_UNORM` already uses for its own missing
color channels.

**Unit tests (new).** `ImageFixtureTest.cpp` adds 7 round-trip
pack/unpack tests, one per new format, using a color with 4 (or 3 plus an
ignored alpha) distinguishable components so a bit-width or component-swap
mistake would be caught; `PacksAndUnpacksB4G4R4A4Unorm` additionally
confirms the packed word actually differs from `R4G4B4A4_UNORM`'s for the
same input color (i.e. the R/B swap is real). `FormatTest.cpp` adds
`MapsRemainingPackedSixteenBitFormats` (all 7 `VkFormat` values map
correctly and report a 2-byte element size). `RenderPassTest.cpp` adds
`CompilesRemainingPackedSixteenBitColorAttachments` (all 7 now compile a
real one-color-attachment render pass), and retargets the pre-existing
`RejectsUnsupportedAttachmentFormat` from `R5G6B5_UNORM_PACK16` (now
supported) to `A4R4G4B4_UNORM_PACK16` (still not attachment-capable).

**Real CTS re-run.** `dEQP-VK.api.image_clearing.core.clear_color_image.*`
filtered to each of the 7 new formats: 0 failures attributable to this
change for any format (some pre-existing `NotSupported` results for
specific 3D image extents/multi-subresource-range shapes, confirmed
identical across every format tested, including long-established ones,
via a `deqp-vk` cross-check against `r8g8b8a8_unorm`).
`dEQP-VK.api.image_clearing.core.clear_color_attachment.*` (full group,
5145 cases): 618 failures total, but every failure touching one of the 7
new formats is confined to the `cube_layers`/`multiple_layers` subgroups
or an MSAA `sample_count_{2,4,8}` variant within `single_layer` -- and a
`deqp-vk` cross-check confirmed the *identical* failure pattern for
`r8g8b8a8_unorm` (a long-established, fully-supported format) and for the
pre-existing `a1b5g5r5_unorm_pack16` (roadmap E5) in the same subgroups,
confirming these are a pre-existing, generic multi-layer/MSAA-attachment-
clear gap, not anything introduced by this row's own format work; every
`single_layer`, non-MSAA case for all 7 new formats passes.

Re-running the original blocker, `alpha_to_coverage_unused_attachment.*`,
confirms the format gate is now cleared: every case now fails at
`vkCreateGraphicsPipelines` with `"an unused color attachment slot is not
implemented"` (`GraphicsPipeline.cpp`'s `getRenderTargets`, confirmed via
`FEME_VULKAN_LOG_CREATION_ERRORS=1`) instead of
`VK_ERROR_FORMAT_NOT_SUPPORTED`. This confirms H7r's own format gap is
fully closed; the remaining `VK_ATTACHMENT_UNUSED` subpass gap is tracked
as a new, separate follow-on, H7s.

**`ninja check-feme`.** Passes in full at **2035/2094** (59 pre-existing,
unrelated `Unsupported`, 0 `Failed`), up from H7n/H7q's own 2026/2085
baseline by 9 new tests: `ImageFixtureTest`'s 7 pack/unpack round-trip
cases, `FormatTest.MapsRemainingPackedSixteenBitFormats`, and
`RenderPassTest.CompilesRemainingPackedSixteenBitColorAttachments`.

**Documentation.** `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`
confirmed no change needed (these are core format capabilities, not
feature bits or extensions). `FeMeVulkanDesign.md`'s format-support section
updated with the 7 new formats and the append-only `ResourceFormat`
ordinal-safety rationale. `Roadmap.md`'s H7r struck through; H7s (the
`VK_ATTACHMENT_UNUSED` subpass gap) added as a new, open, P2 follow-on.

## Roadmap H7s: measured impact (`VK_ATTACHMENT_UNUSED` color attachment)

**Gap.** `dEQP-VK.pipeline.monolithic.multisample.alpha_to_coverage_unused_attachment.*`
(H7r's own follow-on, its own format gate now cleared) failed every
feme-supported-sample-count case at `vkCreateGraphicsPipelines` time with
`"an unused color attachment slot is not implemented"`. Two independent
rejection points existed with the identical error message:
`GraphicsPipeline.cpp`'s `getRenderTargets` (pipeline-creation time,
building `PipelineRenderTargets.Colors`) and `CommandBuffer.cpp`'s
`buildRenderTargetBinding` (`vkCmdBeginRenderPass` time, building
`RenderTargetBinding.Colors`).

**Design: reuse the existing E5 mechanism.** `VK_KHR_maintenance5`'s own
dynamic-rendering `VkRenderingAttachmentInfo::imageView == VK_NULL_HANDLE`
case (roadmap E5) already implements exactly this concept end-to-end: a
color slot that is present (counts toward `colorAttachmentCount()`) but
unused (no bound image, its write silently discarded). Tracing the full
data flow confirmed every downstream consumer already handles
`RenderTargetView::View == nullptr` generically and correctly --
`resolveDrawAttachments` pushes an empty `AttachmentView` for it,
`Executor.cpp`'s fragment-output linkage and color-merge loop both already
treat an empty `AttachmentView` (`Att.Data.empty()`) as "nothing to read
or write here." A classic `VkRenderPass`'s `VK_ATTACHMENT_UNUSED` color
slot is mechanically identical, so the fix is simply to make the two
rejection points push the same placeholder shapes
(`ResourceFormat::Unknown` for `PipelineRenderTargets.Colors`,
default-constructed `RenderTargetView{}` for `RenderTargetBinding.Colors`)
instead of erroring, with zero changes needed in `Executor.cpp` or
`resolveDrawAttachments` themselves.

**A second, newly-reachable validation gap.** Once both rejection points
were fixed and a real classic-render-pass test with an unused slot
compiled a pipeline, a distinct, pre-existing bug in `validateStageInterfaces`
surfaced: its per-color-attachment fragment-output-location loop
unconditionally required a `SV_TargetN` output at *every* location from 0
to `colorAttachmentCount`, including unused ones -- rejecting a
perfectly legal pipeline where the fragment stage declares no output at
all for an unused slot's location. Fixed by threading the render targets'
own per-slot format list (`AttachmentFormat`, already built by
`getRenderTargets`) through `compileAndValidateStages`/
`validateStageInterfaces` (replacing a bare `ColorAttachmentCount`), and
skipping the location-output requirement whenever that slot's format is
`ResourceFormat::Unknown` (the same "unused" placeholder). The pre-existing
H7n depth/stencil-only sample-count fallback in `getRenderTargets` (`if
(Subpass.ColorAttachments.empty()) ...`) was also widened to `if
(!AnyColorSampleCount) ...`, since a subpass whose color-attachment list
is non-empty but every slot in it is unused is the same "no color
attachment ever set it" gap H7n fixed, just reachable a different way.

**Unit tests (new).** `DrawTest.cpp`'s `ClassicRenderPassSkipsUnusedColorAttachment`:
a real classic `VkRenderPass` with a 2-slot subpass color-attachment list
(location 0 `VK_ATTACHMENT_UNUSED`, location 1 a real attachment), a
dual-output fragment shader writing both locations, confirming the
unused location's write is silently discarded while the real location's
write lands correctly -- mirrors the pre-existing dynamic-rendering
sibling test, `DynamicRenderingSkipsNullColorAttachment`, which continues
to pass unchanged (no regression in the E5 mechanism this row reuses).

**Real CTS re-run.** `dEQP-VK.pipeline.monolithic.multisample.alpha_to_coverage_unused_attachment.*`:
the `"an unused color attachment slot is not implemented"` rejection is
gone entirely (confirmed via `FEME_VULKAN_LOG_CREATION_ERRORS=1`) -- this
row's own two fixes are both real and confirmed necessary (without the
`validateStageInterfaces` fix, every case still failed pipeline creation,
now with `"fragment stage has no 4-component floating-point output at
location 1 (SV_Target1)"`, since location 1 is the real/used slot, not
the unused one). However, the CTS case still does not pass end-to-end
even with both fixes: it surfaces a *third*, independent, still-unfixed
gap -- the real attachment's own fragment output (`fragColor1 =
vtxColor.rgb`, a `vec3`) has only 3 components, not the 4 both
`validateStageInterfaces` and `Executor.cpp` still hard-require for a
*used* attachment's own output. This is unrelated to the unused-slot
mechanism itself (confirmed: this row's own regression test, which uses
full 4-component outputs throughout, passes cleanly with no vec3
involved), so it is broken out as a new, separate follow-on, H7t, rather
than folded into this row.

**`ninja check-feme`.** Passes in full at **2036/2095** (59 pre-existing,
unrelated `Unsupported`, 0 `Failed`), up from H7r's own 2035/2094 baseline
by exactly the 1 new test this row adds (`DrawTest.cpp`'s
`ClassicRenderPassSkipsUnusedColorAttachment`).

**Documentation.** `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`
confirmed no change needed (a core render-pass/subpass capability, not a
feature bit or extension). `FeMeVulkanDesign.md`'s render-target-binding
design section updated with an "Update (roadmap H7s, closed)" paragraph.
`Roadmap.md`'s H7s struck through; H7t (the narrower-than-4-component
fragment output gap) added as a new, open, P2 follow-on.
## Roadmap H7t: measured impact (fragment color outputs narrower than 4 components)

**Gap.** `dEQP-VK.pipeline.monolithic.multisample.alpha_to_coverage_unused_attachment.*`
(H7s's own follow-on, its own unused-slot gate now cleared) still failed
every feme-supported-sample-count case at `vkCreateGraphicsPipelines` time,
now with `"fragment stage has no 4-component floating-point output at
location 1 (SV_Target1)"`. The real attachment's own fragment output
(`fragColor1 = vtxColor.rgb`) is a 3-component `vec3` -- legal per spec (the
missing trailing components default to their identity value: `0.0` for a
missing G/B, `1.0` for a missing A) -- but `GraphicsPipeline.cpp`'s
`validateStageInterfaces` and three separate sites in `Executor.cpp`'s
fragment-output linkage (`FSColors`, the dual-source-blend `FSColor1`, and
`FSAlphaToCoverage`) all hard-required `ComponentCount == 4`.

**Design.** Rather than requiring every fragment output to declare all 4
components, all four sites now accept `ComponentCount` in `{1, 2, 3, 4}`.
A new shared helper, `readFragmentColor` (`Executor.cpp`'s anonymous
namespace, alongside `mergeColor`), reads a fragment output's up-to-4
components, substituting the spec-defined identity default for any
component at or beyond the element's own `ComponentCount` rather than
reading out-of-bounds `StageStorage` (storage is only ever allocated for
`[FirstComponent, FirstComponent + ComponentCount)`). The primary
(`FSColors`) and dual-source-blend (`FSColor1`) read loops were both
switched to call this helper; the alpha-to-coverage alpha read (which only
ever needs component 3) was given a simple ternary default instead, since
it has no use for a full 4-component fill. The design precedent is the
pre-existing `ImageFixture.cpp` `unpackColor` function, which already
implements exactly this "missing channel reads as its identity value"
behavior for color formats lacking a channel entirely (e.g.
`R5G6B5_UNORM`'s missing alpha already defaults to `1.0` there).

**Unit tests (new, one per phase).** `GraphicsPipelineTest.cpp`'s
`AcceptsFragmentOutputNarrowerThan4Components` (pipeline-creation-time: a
real SPIR-V fragment shader with a `vec3` `SV_Target0` output, confirming
`vkCreateGraphicsPipelines` now succeeds). `DrawTest.cpp`'s
`RendersFragmentOutputNarrowerThan4Components` (full SPIR-V-to-pixel: a
`vec3` green fragment output, attachment cleared to *transparent* black
first so a broken "missing alpha defaults to 0" implementation would be
distinguishable from a correct "defaults to 1" one, confirming the
rendered texel is solid green with `0xFF` alpha). `ExecutorTest.cpp`'s
`FillsTriangleWithColorOutputNarrowerThan4Components` (direct-LLVM-IR,
Executor-linkage-focused: a hand-built `EntrySignature` with a
`ComponentCount == 3` fragment-output element, exercising the same guard
without going through SPIR-V import at all).

**Real CTS re-run.** `dEQP-VK.pipeline.monolithic.multisample.alpha_to_coverage_unused_attachment.*`
now passes in full: **6/6 feme-supported-sample-count cases pass** (0
failed, 18 pre-existing `NotSupported` for other sample counts) -- the
entire H7n->H7q->H7r->H7s->H7t chain's own original motivating case is
closed.

A broader `dEQP-VK.pipeline.monolithic.multisample.*` sweep (10576 cases)
was re-run to check for regressions: **195 pass, 57 fail, 10324
`NotSupported`**, up from H7q's own 189/63/10324 baseline by exactly the 6
newly-passing cases above (57 = 63 - 6). Every remaining failure was
confirmed pre-existing and unrelated to this row's own changes --
`mixed_count`, `sampled_image.*`, `3d.*` (pre-existing, unrelated
image/sampling gaps), `a2c_with_a2one.*`/`sample_rate_a2c` (pre-existing
combination-case gaps noted in H7n's own sweep), and
`compatible_render_pass.dynamic` (confirmed via a `git stash`/rebuild/
re-run of just this case to fail identically -- `VK_ERROR_INITIALIZATION_FAILED`
-- without this row's own changes, so pre-existing and unrelated) -- none
newly introduced.

**`ninja check-feme`.** Passes in full at **2039/2098** (59 pre-existing,
unrelated `Unsupported`, 0 `Failed`), up from H7s's own 2036/2095 baseline
by the 3 new tests above.

**Documentation.** `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`
confirmed no change needed (a core signature-linkage capability, not a
feature bit or extension). `FeMeVulkanDesign.md`'s H7 status paragraph
updated to reflect H7t's closure. `Roadmap.md`'s H7t struck through.

## Roadmap H7g: measured impact (`vertexPipelineStoresAndAtomics`/`fragmentStoresAndAtomics`, plus H7u's push-descriptor fix)

**Gap.** `PhysicalDeviceInfo.cpp` reported both `vertexPipelineStoresAndAtomics`
and `fragmentStoresAndAtomics` as `VK_FALSE`. No prior investigation had
confirmed whether the CPU lowering pipeline (`feme::cpu::runPipeline`,
`Pipeline.cpp`) gated storage-buffer/-image writes by stage at all.

**Investigation.** An exhaustive read of every pass touching a resource
store (`SPIRVResourceLoweringPass`, `ResourceLoweringPass`, `LinearizePass`,
`SIMDizePass`, `WaveLoweringPass`) confirmed none of them branch on
`StageCompileOptions::Stage`; the only stage-conditioned logic in the whole
pipeline is (a) whether graphics stage-IO validation runs at all (skipped
for `ShaderStage::Compute`, which has no `feme.stage.*` ops), and (b) which
stage-specific entry-wrapper pass runs last. Neither restricts stores. Real
SPIR-V atomic operations (`OpAtomicIAdd` etc.) are separately confirmed
entirely unimplemented, for every stage including compute -- the existing
compute "storage buffer" unit test uses a plain load+`IAdd`+store, not a
true atomic RMW -- but cross-checking the real CTS's own
`vktBindingShaderAccessTests.cpp` (`checkSupportShaderAccess`) confirmed
these two feature bits gate ordinary storage-buffer/image *write* access
from a stage, not specifically atomic RMW, so a plain-store test is
sufficient evidence to flip the bits honestly.
`VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` write support is also confirmed entirely
absent for every stage (`Format.cpp`'s own comment: no `feme.cpu.image.
store.*` runtime helper exists for any format yet) -- a separate,
pre-existing, stage-independent gap unaffected by and out of scope for this
row, since the feature bits only promise writability for storage-resource
kinds the device does support (storage buffers, and storage texel buffers
via the existing `Buffer`-dim `spirv.ImageWrite` lowering).

**Unit tests (new).** `DrawTest.cpp`'s `VertexStageWritesStorageBuffer` and
`FragmentStageWritesStorageBuffer`: a real SPIR-V-dialect vertex/fragment
shader each storing into a bound `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`
(vertex: `buf[gl_VertexIndex] = (gl_VertexIndex + 1) * 11`, one distinct
slot per invocation; fragment: `buf[0] = 42`, an identical constant every
invocation), each deliberately avoiding any read-modify-write race since
real atomics aren't implemented. Both passed immediately against the
unmodified compiler/executor, confirming zero code changes were needed
there -- mirroring the H7a "already-implemented feature bits" pattern.
`createPipeline`'s test helper gained an optional `CustomLayout` parameter
to support a custom descriptor-set-bound pipeline layout.

**Feature-bit flip.** `PhysicalDeviceInfo.cpp`: both
`vertexPipelineStoresAndAtomics`/`fragmentStoresAndAtomics` set `VK_TRUE`.
`PhysicalDeviceInfoTest.cpp`'s all-features assertion test updated
accordingly.

**Real CTS re-run.** `dEQP-VK.binding_model.shader_access.*.storage_buffer.
{vertex,fragment,vertex_fragment}*` (excluding the `bind2` subset, tracked
separately below as H7v): **1200/1200 pass, 0 fail, 0 `NotSupported`** (up
from 0 pass/0 fail/1200 `NotSupported` before the flip, since every case
was previously rejected up front by `checkSupportShaderAccess`).

**A newly-reachable crash, found and fixed (roadmap H7u).** The same
re-run's `with_push` subset (`DESCRIPTOR_UPDATE_METHOD_WITH_PUSH`, i.e.
`VK_KHR_push_descriptor`) initially SIGSEGV'd on its very first case,
previously unreached since every `with_push` case was rejected
`NotSupported` before this row's own flip. `gdb -batch -ex run -ex bt`
showed a null-function-pointer call inside CTS's own
`vk::DescriptorSetUpdateBuilder::updateWithPush`. Root cause:
`vk_gen_entrypoints.py`'s `SUPPORTED_EXTENSIONS` never listed
`VK_KHR_push_descriptor`, despite `PhysicalDeviceInfo.cpp` already
advertising it (roadmap F12) -- so `vkCmdPushDescriptorSetKHR`/
`vkCmdPushDescriptorSetWithTemplateKHR` (the extension's own, `_KHR`-
suffixed original names, as opposed to their core-VK_VERSION_1_4-promoted
unsuffixed aliases already implemented and registered) were never read out
of `<extensions>` at all, so `vkGetDeviceProcAddr` returned null for them
unconditionally. Fixed by adding `VK_KHR_push_descriptor` to
`SUPPORTED_EXTENSIONS` (`vk_gen_entrypoints.py`), registering the two
`_KHR` names in `ImplementedEntrypoints.txt`, and implementing them as thin
forwarders to the already-implemented core names (`CommandBuffer.cpp`/
`EntryPoints.h`). The pre-existing `vk-gen-entrypoints-split-features.test`
lit test's synthetic `vk-split-features.xml` fixture was updated to declare
this eighth `SUPPORTED_EXTENSIONS` entry too, so the generator test itself
still exercises it. A new `CommandBufferTest.cpp` regression test,
`PushDescriptorSetDispatchTest.KHRSuffixedEntryPointsProduceTheSameResult`,
calls `vkCmdPushDescriptorSetKHR` directly and confirms identical behavior
to the core name. Re-running the full `with_push*` subset (20 cases)
post-fix: **20/20 pass**, and the fix is included in the 1200/1200 total
above.

**A second, unrelated gap found, not fixed (roadmap H7v, new).** The same
broader re-run's `bind2` subset (`VK_KHR_maintenance6`'s
`vkCmdBindDescriptorSets2`) also failed: vertex/fragment-stage cases
SIGSEGV (a null-pointer call inside `writeDrawCmdBuffer`'s
`bindDescriptorSets` helper, at a different call site than the push-
descriptor crash above -- `x8`'s dereferenced value at the crash site was
non-null, ruling out the same "unregistered `_KHR` name" root cause),
while compute-stage cases instead fail cleanly with `VK_ERROR_
INITIALIZATION_FAILED` at `vkCreateComputePipelines`. A `git stash`/
rebuild/re-run of `dEQP-VK.binding_model.shader_access.primary_cmd_buf.
bind2.storage_buffer.compute*` confirmed the exact same 30/30 failure
without any of this row's own changes -- i.e. `bind2` is pre-existing and
entirely stage-independent, unrelated to storage-buffer stage-write
support itself. Tracked as a new follow-on, H7v, left open.

**`ninja check-feme`.** Passes in full at **2042/2101** (59 pre-existing,
unrelated `Unsupported`, 0 `Failed`), up from H7t's own 2039/2098 baseline
by the 3 new tests this row and its H7u push-descriptor fix add
(`DrawTest.cpp`'s `VertexStageWritesStorageBuffer`/
`FragmentStageWritesStorageBuffer`, `CommandBufferTest.cpp`'s
`KHRSuffixedEntryPointsProduceTheSameResult`).

**Documentation.** `Vulkan14FeatureInventory.md` updated (both feature
bits now advertised). `VulkanExtensionInventory.md` updated (`VK_KHR_
push_descriptor`'s row notes the `_KHR` dispatch-table fix; `VK_KHR_
maintenance6`'s row notes the new, open H7v `bind2` gap).
`FeMeVulkanDesign.md`'s H7 status paragraph updated. `Roadmap.md`'s H7g
struck through; H7v added as a new, single-lowercase-letter follow-on.

## Roadmap H7v: measured impact (`bind2`'s "compute fails cleanly" symptom root-caused as an unrelated, pre-existing resource-lowering gap; the actual `bind2` SIGSEGV root-caused as an environment limitation)

**Gap (as originally scoped).** H7g's own re-run found `VK_KHR_
maintenance6`'s `vkCmdBindDescriptorSets2` (`bind2`) failing for every
stage: a SIGSEGV for vertex/fragment, and a "clean" `vkCreateComputePipelines`
failure (`VK_ERROR_INITIALIZATION_FAILED`) for compute -- tracked as one
row, H7v, on the assumption both symptoms shared a `bind2`-specific cause.

**Investigation: the "compute fails cleanly" half is not about `bind2` at
all.** Re-running the *ordinary*, non-`bind2` `bind.storage_buffer.
compute.single_descriptor.offset_view_zero` case reproduced the identical
`vkCreateComputePipelines` failure and identical error text. A full sweep
of every `dEQP-VK.binding_model.shader_access.*.storage_buffer.compute*`
case, across every update method (`bind`, `bind2`, `with_push`,
`with_push_template`), failed identically: **0/160 pass** before any fix,
regardless of `bind2`. This proves H7v's original "compute fails cleanly"
text conflated a real, pre-existing, `bind2`-independent gap with the
actual `bind2` bug.

**Root cause (found via a temporary IR dump instrumented into
`compileComputePipeline`, `Pipeline.cpp`, since the real caller silently
`consumeError`s the compile failure).** The failing shader's SPIR-V (a
`dEQP-VK.binding_model.shader_access` compute case's own `b_instance`
input storage buffer, `buffer BufferName { vec4 colorA; vec4 colorB; }`)
imports as a `spirv.VulkanBuffer` handle with storage class `Uniform` (2),
not the dedicated `StorageBuffer` class (12) -- glslang's default,
pre-SPIR-V-1.3 spelling for a `buffer` block still uses the `Uniform`
storage class plus a `BufferBlock` decoration rather than the newer,
explicit `StorageBuffer` class. `classifyVulkanBufferHandle`
(`SPIRVResourceLowering.cpp`) treated *any* struct-shaped handle with
storage class `Uniform` as a read-only uniform block
(`HandleKind::Uniform`), ignoring the handle's own second int parameter
(`Writable`, which `convertBufferBlockType`/`SPIRVToLLVMPatterns.cpp`
already carries correctly for exactly this reason). This shader's
struct-shaped output-buffer access (`b_out.read_colors[gl_WorkGroupID.x]
= result_color`) needs a writable, GEP-indexable access -- but
`hasOnlySupportedUses` disallows both writes and GEPs for
`HandleKind::Uniform` -- so `collectHandles` bailed on the whole
function, leaving every handle un-normalized and hitting `checkSupported
RaisedOps`'s generic "unsupported raised operation" rejection at compile
time.

**Fix.** `classifyVulkanBufferHandle` now checks the handle's `Writable`
int parameter for a `Uniform`-class struct handle: `Writable=1` classifies
it as `HandleKind::StorageStruct` (a real, writable storage buffer
block), matching the dedicated `StorageBuffer`-class case; `Writable=0`
(a real, read-only uniform block) is unaffected.

**Unit tests (new).** `SPIRVResourceLoweringTest.cpp`'s
`LowersLegacyUniformClassStorageBlockFieldToResourceLoad` (a `Uniform`-
class, `Writable=1` struct handle's field load now lowers, like the
existing `StorageBuffer`-class case) and
`LowersLegacyUniformClassStorageBlockStoreToResourceStore` (the same
handle now also accepts a store, unlike a real read-only uniform block --
confirming the fix classifies by `Writable`, not storage class alone).
The pre-existing `LowersUniformBufferFieldToResourceLoad`/`LeavesUniform
BufferStoreUnchanged` (`Writable=0`) cases are unaffected.

**Real CTS re-run.** `dEQP-VK.binding_model.shader_access.*.storage_buffer.
compute*`'s three non-`bind2` update methods: `bind` (30/30),
`bind.with_template` (30/30), and `bind.with_push`+`with_push_template`
(20/20) -- **80/80 pass, 0 fail** (up from 0/80 before the fix), all
previously failing with the same "unsupported raised operation" error.

**The actual `bind2` SIGSEGV, re-investigated.** With the resource-
lowering gap fixed, a `bind2.storage_buffer.compute*` case now reaches
pipeline creation and *dispatch* for the first time, where it crashes --
confirming this is not the same bug as the fixed compute gap above, but
the original, real `bind2` SIGSEGV, now also reachable from compute. A
fresh `gdb -batch -ex run -ex bt` against both a `vertex` case
(`bind2.storage_buffer.vertex.single_descriptor.offset_view_zero`) and the
newly-reachable `compute` case
(`bind2.storage_buffer.compute.descriptor_array.offset_view_nonzero`)
shows the identical crash shape in both: a `VkBindDescriptorSetsInfoKHR`
struct (confirmed via its `sType` immediate, `0x3ba31aeb` ==
`VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO_KHR`) is built correctly on
the stack, then a call through a *null* function-pointer slot in CTS's
own cached `vk::DeviceInterface`/`DeviceFunctionPointers` struct (a plain
member load plus indirect call, at struct offsets 296 and 288
respectively for the two cases -- not a virtual dispatch, just a null
cached pointer). Traced to its root: this development environment's
system Vulkan loader (`libvulkan.so.1`, Ubuntu package `libvulkan1`
1.3.275.0) predates `VK_KHR_maintenance6`/`vkCmdBindDescriptorSets2`
(added to the Vulkan-Headers/loader around 1.3.284, promoted to Vulkan
core in 1.4) -- the loader's own compiled-in device-command dispatch
table has no entry for this command name at all, so when CTS's
`DeviceDriver` constructor asks the loader-resolved `vkGetDeviceProcAddr`
for `"vkCmdBindDescriptorSets2"`, it returns null, even though this ICD's
own `EntryPoints.cpp`/`ProcAddr.cpp` fully implement and correctly expose
the command (`VulkanEntrypoints.inc` registers `FEME_VK_COMMAND_IMPL
(vkCmdBindDescriptorSets2, DEVICE)`, and `CommandBufferTest.cpp`'s
pre-existing `BindDescriptorSets2AndPushConstants2ReachTheDispatch` unit
test calls the ICD function directly and passes). Bypassing the system
loader via `deqp-vk --deqp-vk-library-path=.../libfeme_vulkan.so` (loading
the ICD directly) hits an unrelated, earlier instance-creation error
before reaching this code path, so it does not itself serve as a clean
confirmation, but the dispatch-offset/null-pointer evidence above is
conclusive on its own. This is an environment/toolchain limitation of
this development machine's installed Vulkan loader, not a defect in this
repository's code -- there is no ICD-side change to make, so H7v closes
as "root-caused to a non-fixable, out-of-repo cause," rather than a code
fix, for this half.

**`ninja check-feme`.** Passes in full at **2044/2103** (59 pre-existing,
unrelated `Unsupported`, 0 `Failed`), up from H7g's own 2042/2101 baseline
by the 2 new tests this row adds
(`LowersLegacyUniformClassStorageBlockFieldToResourceLoad`/
`...StoreToResourceStore`).

**Documentation.** `Vulkan14FeatureInventory.md` confirmed no change
needed (a resource-lowering correctness fix, not a feature-bit change).
`VulkanExtensionInventory.md`'s `VK_KHR_maintenance6` row updated to note
this row's closure and root cause (the extension itself was already
advertised, and its own command dispatch is already correctly implemented
in this repository). `FeMeVulkanDesign.md`'s H7 status paragraph updated.
`Roadmap.md`'s H7v struck through as closed (investigated to a conclusive
root cause; the collateral resource-lowering bug fixed, the `bind2` crash
itself confirmed to be a non-fixable environment limitation).

## Roadmap H7h: measured impact (`shaderClipDistance`/`shaderCullDistance`)

**Implementation.** `CanonicalizeStage.cpp`'s `getSystemValueForBuiltIn` now
maps SPIR-V `BuiltIn ClipDistance` (3) and `CullDistance` (4) onto their own
`SignatureSystemValue`s (previously `None`, the same "unmodeled" treatment
an unrecognized DXIL semantic gets). The pre-existing generic `gl_PerVertex`
per-member decomposition (`getStageIORowShape`) already folds a
`[N x float]` array member into `RowCount = N`, so this was purely a
builtin-to-system-value mapping gap, confirmed via a new
`CanonicalizeStageTest.cpp` case, `MapsSPIRVClipCullDistanceBuiltInsToSystemValues`.

`Executor.cpp` gained the real consumer:

- `gl_ClipDistance` becomes one additional Sutherland-Hodgman half-space
  clip per declared plane (up to 8), run after the existing 7 fixed
  frustum planes, evaluated directly against each vertex's own shader
  output value via a new `RasterVertex::ClipDistances` array (linearly
  interpolated across a clipped edge like any other varying).
- `gl_CullDistance` discards a whole primitive outright, before it ever
  reaches clipping, when one declared cull-plane index is negative for
  every one of the primitive's (pre-clip) vertices
  (`isCulledByCullDistance`), per the Vulkan spec's per-plane,
  all-vertices-negative rule.

Two new `ExecutorTest.cpp` cases confirm this end to end:
`ClipsATriangleAgainstAWrittenClipDistance` (a full-screen-covering
triangle with `gl_ClipDistance[0]` set to each vertex's own NDC Y,
verifying the negative-Y half is clipped away) and
`CullsATriangleWhenCullDistanceIsNegativeForEveryVertex` (a uniformly
negative `gl_CullDistance[0]` at all three vertices, verifying the whole
primitive is discarded).

This session's scope is deliberately narrow: the **vertex stage only**,
with **compile-time-constant array indices only**, and **no fragment-stage
read-back** of the interpolated value -- matching the roadmap row's own
text ("a clip-distance consumer in `clipTriangle` and a cull-distance
consumer wherever primitive culling happens"), nothing about dynamic
indexing, fragment reads, or non-vertex stages.

**`ninja check-feme`.** Passes in full at **2047/2106** (59 pre-existing,
unrelated `Unsupported`, 0 `Failed`), up from H7v's own 2044/2103 baseline
by the 3 new tests this row adds
(`CanonicalizeStageTest.MapsSPIRVClipCullDistanceBuiltInsToSystemValues`,
`ExecutorTest.ClipsATriangleAgainstAWrittenClipDistance`,
`ExecutorTest.CullsATriangleWhenCullDistanceIsNegativeForEveryVertex`).

**Real Vulkan CTS re-run.** With `shaderClipDistance`/`shaderCullDistance`
provisionally flipped to `VK_TRUE` purely to let CTS attempt these cases
(a device that reports the bit `VK_FALSE` makes every one of them report
`NotSupported` rather than exercising the real implementation), a full
re-run of `dEQP-VK.clipping.user_defined.{clip_distance,clip_cull_distance}
[_dynamic_index].{vert,vert_tess,vert_geom}.*` gives:

- `clip_distance.vert.*` (non-`_fragmentshader_read`): **8/8 pass**.
- `clip_distance.vert.*_fragmentshader_read`: **0/8 pass** -- every case
  fails at `vkCreateGraphicsPipelines` with an LLVM
  `'llvm.getelementptr' op operand #0 must be LLVM pointer type ... but
  got '!llvm.array<N x f32>'` error. No fragment-side system-value-linked
  input path exists for these two builtins yet.
- `clip_cull_distance.vert.*` (non-`_fragmentshader_read`): **8/8 pass**.
- `clip_cull_distance.vert.*_fragmentshader_read`: **0/8 pass**, identical
  failure shape to `clip_distance`'s own `_fragmentshader_read` cases.
- `clip_distance_dynamic_index.vert.*`: **0/16 pass** -- every case fails
  with `feme-graphics-validate-stage: function 'main' has an unresolved
  stage-IO global-variable access to 'spirv_varN', a shape
  CanonicalizeStagePass does not yet canonicalize into a 'feme.stage.*'
  call`. A non-constant array index into `gl_ClipDistance`/
  `gl_CullDistance` is not recognized by `CanonicalizeStagePass` at all.
- `clip_cull_distance_dynamic_index.vert.*`: **0/16 pass**, identical
  failure shape.
- `clip_distance.vert_tess.1`, `clip_distance.vert_geom.1` (sampled):
  **0/2 pass** -- both fail at `vkCreateGraphicsPipelines` with the same
  class of LLVM `getelementptr`-into-an-array(-of-struct)-typed-SSA-value
  error as the `_fragmentshader_read` cases, but from the
  tessellation-evaluation/geometry stage's own output-composition code
  rather than a fragment-stage input.

Totals: **16 pass / 66 sampled fail**, out of this feature's roughly 330
real mandatory CTS cases once every stage/indexing-mode/fragment-read
combination is counted. Only the vertex-stage, static-index,
non-fragment-read subset is real conformance today.

**Feature-bit decision.** Given that the bulk of this feature's own
mandatory CTS surface still fails, `shaderClipDistance`/
`shaderCullDistance` (`PhysicalDeviceInfo.cpp`) are left at their
zero-initialized `VK_FALSE` -- advertising either bit now would be a
conformance violation, the same standard set by roadmap
H7o/`sampleRateShading` (which stayed `VK_FALSE` until a real passing
case existed for every gate blocking it). Re-running the same case list
against the unmodified (bit still `VK_FALSE`) build confirms every case
correctly reports `NotSupported` (`"Shader ClipDistance not supported"`)
rather than a false pass or a hard failure.

**Documentation.** `Vulkan14FeatureInventory.md`'s `shaderClipDistance`/
`shaderCullDistance` rows updated with the measured 16/~330 split and
pointers to the three follow-on rows. `VulkanExtensionInventory.md`
confirmed no change needed (a core feature-bit row, not an extension).
`FeMeGraphicsDesign.md` gained a new "Status (roadmap H7h)" subsection.
`Roadmap.md`'s H7h struck through as partially closed (the vertex-stage,
static-index consumer is real and tested; the feature bit itself stays
blocked), with three new, properly-scoped follow-on rows added: H7w
(dynamic indexing), H7x (fragment-shader read-back), H7y
(tessellation/geometry-stage clip/cull-distance writes).

## Roadmap H7w: measured impact (`gl_ClipDistance[i]`/`gl_CullDistance[i]` dynamic index)

**Implementation.** `CanonicalizeStage.cpp` gained
`getDynamicRowIndexedAccess`, recognizing a `GetElementPtrInst` rooted
directly at a stage-IO global with a constant prefix of indices (member
selection/nested-array peeling) and exactly one trailing non-constant
index selecting a row within an `ArrayType` member -- e.g.
`gl_PerVertex.gl_ClipDistance[i]` with a loop-carried `i` -- wired into
both `getStageIOGlobal` (discovery) and `resolveStageIOAccess` (full
resolution) as a third fallback, after the existing constant-offset and
dynamic-vertex-index paths. `ShadowValueMap` (roadmap H2e's
`Output`-direction read-back scheme) needed a real extension to support
this: a non-constant `Row` cannot key a per-array-element scalar shadow
`AllocaInst` the way every prior (constant-`Row`) shape did, so it now
gets its own `RowCount`-sized array alloca per (`ElementID`,
`Component`), GEP'd by the runtime `Row` value -- deliberately excluded
from `PromoteMemToReg`'s own list (a variable-index GEP is not
promotable), so it stays ordinary stack memory. Confirmed by a new
`CanonicalizeStageTest.cpp` case,
`ThreadsDynamicRowIndexIntoClipDistanceOutputStore`.

A real IR reduction (`glslangValidator -V` on a minimal GLSL vertex
shader mirroring the CTS shape → `feme-translate
--import-spirv --spirv-to-llvmir` → `feme-opt --llvm
-passes='feme-graphics-canonicalize-stage,feme-graphics-validate-stage'`)
reproduced the pre-fix "unresolved stage-IO global-variable access to
'spirv_varN'" diagnostic exactly as the roadmap row's own text describes,
for both the `ClipDistance` and `CullDistance` members, and confirmed it
no longer fires post-fix.

**`ninja check-feme`.** Passes in full at **2048/2107** (59 pre-existing,
unrelated `Unsupported`, 0 `Failed`), up from H7h's own 2047/2106 baseline
by the 1 new test this row adds
(`CanonicalizeStageTest.ThreadsDynamicRowIndexIntoClipDistanceOutputStore`).

**Real Vulkan CTS re-run.** With `shaderClipDistance`/`shaderCullDistance`
provisionally flipped to `VK_TRUE` purely to let CTS attempt these cases
(reverted before committing, matching H7h's own measurement discipline), a
re-run of the 16 `dEQP-VK.clipping.user_defined.{clip_distance,
clip_cull_distance}_dynamic_index.vert.*` cases (non-`_fragmentshader_read`,
the primary target this row's own text scopes to) gives:

```
Test run totals:
  Passed:        0/16 (0.0%)
  Failed:        16/16 (100.0%)
  Not supported: 0/16 (0.0%)
```

Every case now fails with a **different** error than before this row:

```
error: feme-cpu-linearize: function 'main': loop at '' has an internal
branch in ''; unsupported (roadmap milestone 6 deviation)
  Fail (vk.createGraphicsPipelines(...): VK_ERROR_INITIALIZATION_FAILED)
```

This is the exact same, already-documented, pre-existing
`feme::cpu::LinearizePass` limitation this report already noted for
`dEQP-VK.geometry.layered.*.readback` (roadmap R27 intentionally scopes
`LinearizePass` to the divergent-diamond/divergent-loop-exit shapes it
already supports) -- unrelated to stage-IO canonicalization, and not
something this row's own scope (a `CanonicalizeStage.cpp` pattern-
recognition gap) touches or should fix. A sampled cross-check against
`clip_distance_dynamic_index.vert.1_fragmentshader_read`,
`.vert_tess.1`, and `.vert_geom.1` reproduces the identical failure
message, confirming it is generic to this CTS shader's own loop shape,
not specific to the `.vert`-only, non-fragment-read subset.

**What this confirms.** The "unresolved stage-IO global-variable access"
diagnostic this row's own text names as its target **no longer occurs
anywhere** in this case list -- real, measured forward progress, just not
yet a passing image comparison, since a separate, unrelated,
already-scoped-by-design gap sits immediately downstream. Unlike H7h's
own three newly-discovered follow-on gaps (each a genuinely new,
previously-untracked limitation), this one is already a known, named,
intentionally-scoped limitation of a different pass entirely (roadmap
R27's own `LinearizePass`), so no new roadmap row is added for it here;
closing it is a `LinearizePass`-generalization project of its own,
well outside a stage-IO canonicalization row's scope.

**Feature-bit decision.** `shaderClipDistance`/`shaderCullDistance` stay
`VK_FALSE` -- this row's fix alone does not clear a real passing CTS case
end to end, the same standard set by H7h/H7o.

**Documentation.** `FeMeGraphicsDesign.md` gained a new "Status (roadmap
H7w)" subsection (including the one deliberate deviation from the
roadmap row's own text: no bounds check was added, matching
`getDynamicVertexIndexedAccess`'s own established precedent).
`Vulkan14FeatureInventory.md`'s `shaderClipDistance`/`shaderCullDistance`
rows updated with a pointer to this section. `VulkanExtensionInventory.md`
confirmed no change needed (a core feature-bit row, not an extension).
`Roadmap.md`'s H7w struck through as closed (the row's own scope, the
canonicalization gap, is real, tested, and CTS-confirmed to no longer
reproduce its own named failure) -- H7x/H7y remain open and untouched by
this row.
