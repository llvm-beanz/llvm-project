# FeMe Vulkan ICD: First Real Vulkan-CTS Run Report

Every prior FeMeVulkanDesign.md milestone (V4's "Begin Vulkan CTS runs for
the intentionally advertised subset" and V6's "Run the graphics subset of
the CTS ... " bullets) recorded the same deviation: `deqp-vk` was not
available in the environment, so only the *infrastructure* to run it
(`feme/utils/filter_vulkan_cts_cases.py`, `test/Vulkan/cts-compute-subset.test`)
existed, with no actual pass/fail result. This pass had a real checkout of
[VK-GL-CTS](https://github.com/KhronosGroup/VK-GL-CTS) available
(`/home/dev/dev/VK-GL-CTS`, `vulkan-cts-1.4.6.2-411-g918221c6`) and used it
to close that gap for the first time.

## Setup

- Built `deqp-vk` from the vendored `external/` sources (`glslang`,
  `spirv-tools`, `spirv-headers`, `amber`, `vulkan-video-samples`, ...) with
  `-DDEQP_TARGET=vulkan_headless` -- the `default` target links a
  `tcu::Platform` base class whose `getVulkanPlatform()` unconditionally
  throws `NotSupportedError` (no windowing system is needed for a
  device-only/offscreen ICD, and this sandboxed environment has no `DISPLAY`
  or `XDG_RUNTIME_DIR` in any case); `vulkan_headless` is the target that
  actually implements it.
- Pointed the real Khronos Vulkan loader (`libvulkan.so.1`, already present)
  at this ICD with `VK_DRIVER_FILES=<build>/tools/feme/tools/feme-vulkan/feme_icd.json`,
  exactly as `test/Vulkan/*.test`'s `%feme_vulkan_icd_manifest` substitution
  does -- `vulkaninfo` confirms the loader enumerates exactly one device,
  `FeMe CPU Vulkan Device`, apiVersion 1.2.
- Generated `deqp-vk`'s full case list (`--deqp-runmode=txt-caselist`,
  ~3.2M leaf test cases across every module the binary was built with, not
  just Vulkan's core -- `ray_tracing`, `video`, `mesh_shader`, `data_graph`,
  ... included) and ran every top-level group (`dEQP-VK.<group>.*`)
  separately against `libfeme_vulkan`, each under a 600s timeout, so one
  group hanging or crashing could not lose data for the rest. Execution
  turned out fast enough (an entire group is typically under 15 seconds,
  even the largest ones) that no sampling was needed -- every case in every
  group that did not crash the process ran to completion.

## Aggregate results (completed groups)

Across the 47 of 54 top-level groups that ran to completion (excluding the
7 that crashed the process before finishing -- see below):

| | Count |
|---|---|
| Total cases | 1,659,818 |
| Passed | 2,885 |
| Failed | 14,055 |
| Not supported | 1,642,877 |

The overwhelming `Not supported` share is expected and by design, not a
regression: this ICD intentionally advertises a narrow, truthful surface
(apiVersion 1.2, no WSI/swapchain, no sparse/ray-tracing/mesh/tessellation/
geometry/video/YCbCr/transform-feedback/protected-memory, a bounded texel-
buffer and attachment-format list -- see "Initial Non-Goals" and each
milestone's own deviation list in FeMeVulkanDesign.md), and a CTS case
naming an unadvertised extension or feature correctly reports
`NotSupported` rather than running. That is precisely the CTS behavior a
truthful "must fail before draw time, not silently misbehave" ICD is
supposed to produce.

## Crashes found and fixed this pass

The most valuable signal a real CTS run adds over this ICD's own unit
tests is exactly this: four core Vulkan commands crashed the process
(segfault through a null device-dispatch-table entry) because this ICD
had never implemented them at all, rather than merely rejecting them at
creation. All four are fixed and covered by new unit tests in this pass
(see the corresponding commits):

| Command(s) | Found by | Root cause | Fix |
|---|---|---|---|
| `vkTrimCommandPool` | `dEQP-VK.api.command_buffers.trim_command_pool` | Core VK_VERSION_1_1 command, never implemented | No-op body (spec only requires trimming to *possibly* help; never that it does) |
| `vkCreateRenderPass2`, `vkCmdBeginRenderPass2`, `vkCmdNextSubpass2`, `vkCmdEndRenderPass2` | `dEQP-VK.renderpasses.renderpass2.*` | Core VK_VERSION_1_2 commands, never implemented | `vkCreateRenderPass2` converts to the classic structures and delegates to `vkCreateRenderPass`; the three command-buffer entry points are signature adapters onto the existing render-pass instance state machine |
| `vkCreateDescriptorUpdateTemplate`, `vkDestroyDescriptorUpdateTemplate`, `vkUpdateDescriptorSetWithTemplate` | `dEQP-VK.binding_model.descriptorset_random.*` | Core VK_VERSION_1_1 feature, never implemented | New `DescriptorUpdateTemplate` object; `vkUpdateDescriptorSetWithTemplate` walks its entries against a `writeDescriptorFromRaw` helper shared with (factored out of) `vkUpdateDescriptorSets` |
| `vkCmdSetLineWidth`, `vkCmdSetDepthBias`, `vkCmdSetDepthBounds`, `vkCmdSetDeviceMask` | `dEQP-VK.dynamic_state.monolithic.compute_transfer.multi.transfer.after` | Core commands, never implemented | No-op bodies -- every state they would govern (depth bias/bounds, wide lines, a second device) is already rejected at pipeline creation or does not exist on this single-physical-device ICD, so no accepted pipeline could ever observe a difference |

Each was root-caused with `gdb`'s backtrace against the exact failing case
(isolated to a single-entry case list first), confirmed fixed by rerunning
that same case list, and confirmed not to regress `check-feme` (1430
passed, 1 unsupported both before and after, plus the new unit tests).

## Crashes found and *not* fixed this pass

Two further, distinct crash classes were root-caused but are deferred --
both go well beyond this ICD's own code:

### 1. `dEQP-VK.api.invariance.random` -- a CTS-side robustness gap

Segfaults inside CTS's own
`vkt::api::(anonymous)::ImageAllocator::ImageAllocator`
(`external/vulkancts/modules/vulkan/api/vktApiMemoryRequirementInvarianceTests.cpp:183`):
`m_colorFormat = (VkFormat)optimalformats[deRandom_getUint32(&random) %
optimalformats.size()]`, with no check that `optimalformats` is non-empty.
On AArch64 (unlike x86, where integer division by zero traps), a
`% 0` returns the dividend unmodified per the architecture's `UDIV`
semantics, so the index becomes a large pseudo-random number and the
vector access reads out of bounds. This is reachable because this ICD's
advertised format support is much narrower than a real GPU's (see the
attachment-format lists throughout FeMeVulkanDesign.md), so the random
test's format-selection loop can, for some seeds, filter down to zero
candidate formats -- something the test's own constructor does not defend
against for *any* driver, not just this one. Not fixed here: the fix
belongs in VK-GL-CTS's own `ImageAllocator` constructor (check for an
empty `linearformats`/`optimalformats`/`memoryTypes` vector and report
`NotSupported` instead of indexing it), not in this ICD.

### 2. SPIR-V spec-constant composites over non-spec-constant constituents

Segfaults inside MLIR's own SPIR-V deserializer,
`mlir::spirv::Deserializer::processSpecConstantComposite`
(`mlir/lib/Target/SPIRV/Deserialization/Deserializer.cpp:1997`):
`getSpecConstant(operands[i])` returns null whenever a constituent is not
itself a previously-declared spec constant, and the very next line,
`SymbolRefAttr::get(elementInfo)`, dereferences that null unconditionally.
Per the SPIR-V spec, `OpSpecConstantComposite`'s constituents may be *any*
`Constant` or `Spec Constant` declaration -- a `mat2` spec constant's
columns, for instance, are ordinary `OpConstantComposite` vectors, not
spec constants, since only the whole matrix (not each column) is
specialized. `mlir::spirv::SpecConstantCompositeOp` models every
constituent as a symbol reference into the spec-constant symbol table,
which has no representation for a plain (non-symbol, inline-attribute)
constant constituent -- so fixing this is a modeling change to the
`spirv` dialect op itself (allowing a mixed operand list), not a small
null check, and is out of scope for this pass.

This single root cause is responsible for every one of the remaining six
crashed groups, confirmed by each group's abort message being one of two
assertions reached from the same code path (`DenseElementsAttr::get`'s
`hasSameNumElementsOrSplat`, or `LLVMArrayType::get`'s null-subtype
assertion further down the same lowering, both downstream of the
deserializer handing a null-derived attribute forward): `memory_model`
(crashed after 21 of 17,300 cases), `pipeline` (523,668 of 1,171,653 --
`spec_constant.compute.composite.array.array_mat2`), `glsl` (74 of
26,808 -- `arrays.constructor.bool_mat3_vertex`), `spirv_assembly` (7,722
of 68,734), `ssbo` (123 of 12,225), and `ubo` (537 of 13,240). Each
group's partial results up to its crash point are preserved in
`/tmp/cts_runs/<group>.log` for whoever picks this up (not checked into
the tree -- see "Reproducing this report" below).

## What the `Fail` results represent

Excluding the two crash classes above, the ~14,055 `Fail` results seen in
completed groups are overwhelmingly *expected* failures against
documented, already-tracked roadmap gaps, not new discoveries -- spot
checks across `compute`, `dynamic_state`, `draw`, `image`, `query_pool`,
`rasterization`, `robustness`, `subgroups`, `synchronization`, `texture`,
and `ycbcr` all attribute to one of:

- Atomics not raised from SPIR-V at all (V4's own deviation note).
- A divergent vector value used outside a supported insertelement-chain/
  resource-store pattern (`feme-cpu-simdize`'s own diagnostic, explicitly
  labeled "roadmap milestone 7 deviation").
- An unsupported SPIR-V construct for this ICD's scope (an unsized runtime
  array in a binding not already tracked, a packed narrow-channel format,
  a subgroup operation, ...), reported by the importer/legalizer as a
  clean, non-crashing pipeline-creation failure -- exactly the "fails at
  creation, not draw time" contract every milestone's deviation list
  requires.
- `VK_EXT_shader_object`/`VK_EXT_graphics_pipeline_library`-only test
  variants (`dynamic_state`'s `shader_object_unlinked_spirv`/
  `fast_linked_library` groups, `pipeline`'s equivalents), which this ICD
  does not implement and reports `NotSupported` for at pipeline
  construction, except where the test itself asserts an internal utility
  precondition (`vkPipelineConstructionUtil.cpp`) before reaching that
  check.

No new correctness bug (a `Pass`-shaped result that is actually wrong)
was found in this pass; every `Fail` traced to a spot check maps onto a
documented, intentional scope boundary or a `feme-cpu-simdize`/importer
diagnostic naming its own roadmap deviation.

## Reproducing this report

```shell
# Build deqp-vk (from a VK-GL-CTS checkout with external/ already vendored):
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release -DDEQP_TARGET=vulkan_headless
ninja -C build deqp-vk

# Run one group against libfeme_vulkan:
VK_DRIVER_FILES=<feme-build>/tools/feme/tools/feme-vulkan/feme_icd.json \
  build/external/vulkancts/modules/vulkan/deqp-vk \
  --deqp-caselist-file=<a case list, e.g. from --deqp-runmode=txt-caselist> \
  --deqp-log-filename=/tmp/out.qpa
```

`feme/utils/filter_vulkan_cts_cases.py` (V4) and
`test/Vulkan/cts-compute-subset.test` remain the in-tree, `lit`-integrated
version of the same idea, gated on `REQUIRES: system-vulkan-cts` so they
skip cleanly wherever `deqp-vk` is not installed.
