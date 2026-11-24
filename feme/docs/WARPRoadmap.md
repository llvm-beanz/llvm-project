# FeMe WARP-like software adapter roadmap

**Selected direction: a WARP-compatible, application-local user-mode
drop-in using stock Windows D3D12/DXGI and the unmodified WARP kernel-mode
driver.** The user selected this product contract on 2026-11-17, closing
`W0(b)`: FeMe intentionally uses Microsoft's WARP adapter identity (vendor
`0x1414`, device `0x008c`), not a distinct FeMe identity, and does not add or
modify a kernel-mode driver. This is an application-local compatibility path,
not replacement of system WARP binaries or a claim that arbitrary applications
can transparently select FeMe. `W0(f)`'s historical container measurements
and `W0(h)`'s
bounded alternative-B investigation are done. Stock D3D12 reaches owned
`OpenAdapter12`, queries public adapter callbacks and closes the adapter on
host/container inbox and Agility 619 runtimes, without ordinal 199, reference
forwarding or a new/custom kernel driver. These results prove routing and a
narrow callback audit, not a real device or working WARP-kernel resource path.
`W0(g)` remains partially done; the negative public general-provider search
must not be interpreted as routing impossibility. Evidence and limits are in
[W0WindowsIntegrationProbe.md](W0WindowsIntegrationProbe.md#alternative-b-application-local-warp-ddi-routing).
Current development and validation are Windows-VM-first; container references
below describe historical evidence unless explicitly stated otherwise.
`WS0` (image block-geometry + subresource-layout extraction) and `WS1`
(BC decode, view materialization, copy-region-walk, and attachment-view
layer-count extraction into `FeMeImage`) are both done; `WS2` (the
`QueueExecutor` FIFO task-runner extraction and the
`PendingExecutionRefCounted` retained-work-storage extraction into
`FeMeExecution`, `WS2(a)`/`WS2(b)`) is fully done and closed -- its third
original piece, command execution/CPU-draw-worker paths, was evaluated
and found to have no portable kernel left to extract. `WS3` (shared-core
regression gates, `WS3(a)`/`WS3(b)`) is also fully done and closed. `W0(c)`
(classify the local Direct3D test corpus) is partially done -- its
classification and redistribution-review pieces are recorded in
[W0DirectXTestCorpusClassification.md](W0DirectXTestCorpusClassification.md),
but broader legacy-shader coverage and owned-device execution still require
compiler work and a working FeMe runner, respectively. The fixed raw-UAV
legacy oracle and grouped SM6 reference oracle are measured coverage, not
closure of that compatibility gate.

This is the implementation checklist for [FeMeWARPDesign.md](FeMeWARPDesign.md),
not a claim that FeMe is a Direct3D adapter today. The product remains a
D3D12 software adapter with complete feature level 12_2 behavior,
presentation, and separately negotiated modern options, but it intentionally
uses Microsoft's WARP identity. FeMe must not replace system WARP binaries or
assume `EnumWarpAdapter` selects an arbitrary application-local module outside
the selected, measured compatibility path.

Unstruck milestones below are **pending**. Existing FeMe components are reusable
prerequisites, not completed W milestones. Strike through a milestone only
after its exit evidence is recorded; add remaining work as `W2(a)`-style
entries, never deeper letter nesting. The summary in [Roadmap.md](Roadmap.md)
uses the same W0-W7 meanings.

## Evaluation and source baseline

The shared-core architecture is sound. The main changes needed are to refresh
the compute-only assumptions, extract reusable code still under Vulkan, and
turn Windows deployment and test selection into early, measurable gates.
The local sources are an architectural and behavioral reference, not permission
to copy Microsoft's implementation into LLVM.

| Checkout inspected on 2026-11-05 | Revision |
|---|---|
| FeMe, before this documentation change | `ae4574207fa6` |
| `C:\dev\Direct3D` | `1d9469a45630a6114072893da4804511fc179e20` |
| Its `src\D3D12Conf` submodule | `c148818e94859052eb24757bbe7f9ea6c3b3ec16` |

Direct3D paths below are relative to that local checkout. FeMe paths are
relative to this repository. No Direct3D source or test data is vendored here.

| Evidence inspected | Design consequence |
|---|---|
| `src\D3DCore\LowFreq\DXGIDevice.cpp`: runtime loads `OpenAdapter12`, checks adapter callbacks, and obtains OS adapter/thunk services | A UMD export is necessary, not sufficient. W0 must prove the whole runtime/adapter path, including kernel services; a COM implementation is a different frontend. |
| `src\warp\warp10\UMDriver\Adapter.cpp`, `DeviceInitDestroy.cpp`, `CommandQueue.cpp`: adapter-private information and context/queue callbacks | WARP's existing BasicRender/SoftGPU relationship is not a third-party installation recipe. `src\warp\BasicRender` here contains a public header, not a complete miniport implementation. |
| `src\warp\warp10\UMDriver\DeviceDDITables12.cpp` | Pin a DDI version and generate/check its required tables. Do not equate public `ID3D12*` methods with driver callbacks or treat a newer header as implemented support. |
| `src\warp\warp10\HW\CMakeLists.txt`: separate assembly, setup, pixel, tessellation, compute, mesh, ray, and graph units | Preserve these functional boundaries, but use FeMe's own compiler, rasterizer, and runtime. Reusing WARP's JIT/driver internals would not share the Vulkan engine. |
| `feme\include\feme\Target\CPU\CompiledStage.h`, `ResourceHeap.h`, `RuntimeABI.h`, `ResourceInfo.h` | Stage invocation, prepared heaps, image/sampler ABIs, and populated artifact ABI 7 already exist. Reuse them rather than rebuilding the original design's prerequisites. |
| `feme\lib\Graphics` and `feme\lib\Vulkan\GraphicsPipeline.cpp` | Raster, tessellation, geometry, and mesh machinery already have a consumer. Add D3D state/DXIL mapping to that core; neither source comments nor Vulkan pass counts prove D3D completeness. |
| `feme\lib\Vulkan\Format.*`, `Image.*`, `ImageOps.*`, `BC*`, `CommandBuffer.cpp`, `Sync.*` | Image mechanics, decode/preparation, and task scheduling are still entangled with the Vulkan frontend. Extract these in behavior-preserving steps before adding duplicate D3D implementations. |
| `feme\include\feme\Transforms\CPU\RootConstantLowering.h`, `BoundResourceNormalization.cpp`, groupshared regression tests | Root constants are broader than the design's original description; general root-layout-driven CBVs remain a gap. Divergent groupshared indexing exists, but barrier/masking edge cases still need per-phase coverage. |
| `src\D3D12Conf` and `src\warp\tests` CMake files and shared device helpers | Tests are available as source, not automatically executable or adapter-independent. Some select WARP explicitly, use private interfaces, or consume legacy shaders. Classify them before using their results as FeMe evidence. |

Two scope corrections are essential. Direct heap indexing belongs to the
separately negotiated SM 6.6 path, not the FL 12_2 / SM 6.5 minimum.
Also, FeMe already imports DXBC; neither that fact nor D3D11-on-12 proves
legacy shader execution. W0 must resolve mandatory shader compatibility before
the product can claim full D3D12 compliance.

## Sharing contract

Both frontends must lower into the same resource/stage/execution contracts.
Do not link `FeMeDirect3D` against `FeMeVulkanCore`, translate D3D commands into
Vulkan calls, or fork a second SIMD compiler/rasterizer.

| Shared owner | Reuse or extraction | Kept in each API frontend |
|---|---|---|
| Existing `FeMeCore`, transforms, `FeMeTargetCPU`, CPU runtime | Signatures, canonical stage operations, SIMD/waves/barriers, `CompiledStage`, prepared arguments, bounds checks, sampling helpers | DXIL/SPIR-V validation and import policy, root/layout binding maps, shader-model and feature queries |
| Existing `FeMeGraphics` | Normalized pipelines/draws, vertex fetch, clipping, rasterization, interpolation, depth/stencil, output merge, tessellation, geometry, mesh/task assembly | D3D PSO/root signature/IA/OM state; Vulkan pipeline/render-pass/dynamic state; explicit normalization of real semantic differences |
| Proposed small `FeMeImage` library, WS0-WS1 | Checked format/block geometry, subresource storage layouts, conversion/decoding, normalized view preparation and copy/resolve kernels | `VkFormat`/DXGI conversion tables, format capability reporting, D3D footprints/typeless view rules, Vulkan image layouts and copy-region validation |
| Proposed small `FeMeExecution` library, WS2 | Task dispatch/join, execution-owned storage/retention, cancellation/error notification primitives | API command streams, reset/replay rules, queue waits/signals, device-removal codes, residency, state promotion/decay, semaphore/fence semantics |
| Planned ray-tracing core, W5 | BVH build/refit/traversal, intersections, bounded continuations and shader invocation | DXR state objects, shader identifiers/tables/local roots, AS commands; Vulkan AS objects/SBT and synchronization |
| Windows-only boundary | No shared OS object model | DDI/COM ownership, callbacks, LUIDs, handles, DXGI, installation, signing, presentation |

`FeMeImage` must depend only on low-level core/support and the existing runtime
ABI definitions, not on Vulkan, Windows, or `FeMeGraphics`. The graphics and
API libraries can consume it without a dependency cycle. Keep JIT-callable C
sampling helpers in the existing CPU runtime; do not make that runtime depend
on the host graphics executor. Add libraries only with actual migrated users.
The execution library accepts tasks, not a union of Vulkan and D3D commands.

Preserve separate logical D3D resource/sampler heaps even though FeMe uses
separate physical buffer/image/sampler arrays. A common prepared-view structure
must own any decoded texels/layout arrays until execution retires, and retain
the backing allocation through the frontend's legal lifetime rules.

## Dependency order and shared refactoring

```text
WS0 -> WS1 -----------------------> W2
WS2 -----------------------------> W1
W0 -> W1 -> W2 -> W3 -> W4 -> W5 -> W7
W0 + W3 -------------------------> W6 presentation -> W7
W2 ------------------------------> W6 enhanced barriers (if selected)
W5 ------------------------------> W6 later shader/graph profiles (if selected)
WS3 applies at every extraction and feature gate
```

W0 gates Windows-facing product implementation, not portable refactoring.
Vulkan remains the regression consumer throughout; the availability of Windows
sources permits D3D bring-up in parallel with Vulkan conformance work, not
waiting for the entire Vulkan CTS to pass. No API's successful test substitutes
for the other's semantics.

| ID | Bounded change | Exit evidence |
|---|---|---|
| ~~WS0(a)~~ | Extract `formatElementSize`/`blockWidth`/`blockHeight`/`bytesPerBlock`/`isASTCSRGBFormat` out of `feme::vulkan::Format.cpp` into a new API-neutral `FeMeImage` library (`feme/lib/Image`, `feme/include/feme/Image/BlockGeometry.h`); none of them ever depended on `VkFormat`. `Format.{h,cpp}` keep their existing signatures as thin forwarders, so every Vulkan caller and advertised format capability is unchanged. | `FeMeImageTests` (11 cases: ordinary/ASTC-LDR/ASTC-HDR/BC/ETC2-EAC element sizes, block shapes and sRGB); `check-feme` 3,875 Passed/15 Skipped/10 Unsupported, zero failures; bounded 233-case `dEQP-VK.api.smoke.*`/`api.info.format_properties.*` CTS run, 233 Pass/zero Fail; both Vulkan inventories regenerate unchanged. See `VulkanCTSReport.md`. |
| ~~WS0(b)~~ | Separate the checked mip/plane/sample subresource-layout calculations (offsets, strides, overflow/odd-extent/depth-plane handling) from `Image.*` into the same `FeMeImage` library, building on `WS0(a)`'s block-geometry functions. Leave Vulkan creation, allocation binding, layout state, and feature tables in place. | `feme::vulkan::Image.cpp`'s `computeSubresourceLayouts`/`computeSubresourceLayout`/`Image::isInBounds` are now thin forwarders onto `feme::image::computeSubresourceLayouts`/`computeSubresourceLayout`/`isInBoundsSubresource` (`feme/lib/Image/SubresourceLayout.cpp`, `VkImageType` converted to a plain `Is3D` bool). `FeMeImageTests` gained 15 cases (2D/array/3D/block-compressed/multisample layout math, plus mip/extent/array-layer/depth out-of-bounds checks); `check-feme` 3,890 Passed/15 Skipped/10 Unsupported, zero failures; combined bounded CTS run of `dEQP-VK.image.subresource_layout.*` plus a `host_image_copy` array/large-image sample, 3,638 Completed/1,758 Pass/1,880 NotSupported, zero Fail. See `VulkanCTSReport.md`. |
| ~~WS1(a)~~ | Move the BC1-5/BC6H/BC7 (BPTC) block decoders and their shared `BCPartitionTables` out of `feme::vulkan::BCDecode.cpp`/`BC6HDecode.cpp`/`BC7Decode.cpp` into `FeMeImage`; none of them ever depended on `VkFormat` or any Vulkan type. `feme/lib/Vulkan/BCDecode.*`/`BC6HDecode.*`/`BC7Decode.*` keep their existing signatures as thin forwarders so `BCSamplingBridge.cpp` is unaffected; the now-unused Vulkan-side `BCPartitionTables.*` is removed outright. ASTC/ETC2 are not moved -- they do not yet share the extracted path. | `FeMeImageTests` gained 22 cases (10 BC1-5, 6 BC6H, 6 BC7); `check-feme` 3,912 Passed/15 Skipped/10 Unsupported, zero failures; bounded 192-case `dEQP-VK.texture.compressed.bc*` CTS run (the full mustpass BC coverage), 96 Pass/96 NotSupported (sparse-residency, expected)/zero Fail. See `VulkanCTSReport.md`. |
| ~~WS1(b)~~ | Move the portable parts of normalized image-view materialization -- subresource-range clamping/`VK_REMAINING_*`-sentinel resolution and per-mip width/height/depth computation (`materializeImageDescriptor`'s clamping block), plus the compressed-format border-component-mask lookup -- out of `CommandBuffer.cpp` into `FeMeImage` (`feme/include/feme/Image/ViewMaterialization.h`, `feme/lib/Image/ViewMaterialization.cpp`), building on `WS0(a)`/`WS0(b)`/`WS1(a)`. Swizzle resolution (`resolveImageSwizzle`) and the ASTC-decode sRGB-format selection in `materializeImageDescriptor` were evaluated and left alone: both are already one-line wrappers over already-portable `feme::cpu` functions, so there was no further Vulkan-coupled logic in them to extract. `ImageView::resolvedLayerCount()`/`effectiveViewLayerCount` -- a separate, 2D-view-of-3D-image special case used only by `RenderPass.cpp`'s attachment-view resolution, not by `materializeImageDescriptor` -- was intentionally **not** touched; it is carried forward as `WS1(d)` below so this step stays pure code motion. Vulkan's `VkImageView` object creation, descriptor binding, and layout-transition validation stay in place. | `FeMeImageTests` gained 14 cases (whole-image/`VK_REMAINING_MIP_LEVELS`/`VK_REMAINING_ARRAY_LAYERS` sentinel resolution, explicit-count clamping, out-of-bounds base-mip/base-layer/zero-count invalidity, 3D depth-halving, minification floor, and border-component-mask coverage across 1-4-channel BC/ETC2 families) -- 62/62 total; `check-feme` 3,926 Passed/15 Skipped/10 Unsupported, zero failures; bounded 1,062-case CTS run (`dEQP-VK.texture.swizzle.component_mapping.color.r8g8b8a8_unorm_*` + `dEQP-VK.texture.mipmap.3d.*` + `dEQP-VK.texture.filtering.2d_array.*`), 928 Pass/134 NotSupported/zero Fail. See `VulkanCTSReport.md`. |
| ~~WS1(c)~~ | Move `runCopyImage`'s (`feme::vulkan::ImageOps.cpp`; the roadmap's own earlier "`CommandBuffer.cpp`" reference was inaccurate) row/depth/array-layer-vs-3D-depth-slice stride-walking geometry -- block/texel unit conversion, row-byte sizing (including the sample-count fold-in), and the 3D-image/2D-array-image slice-stepping rule -- into `FeMeImage` (`feme/include/feme/Image/CopyWalk.h`, `feme/lib/Image/CopyWalk.cpp`), building on `WS1(b)`'s view materialization. The actual `memcpy` through `Image::texelPointer`/`blockPointer` stays in `ImageOps.cpp`, since it genuinely needs a live `Image`'s backing storage. `runBlitImage`/`runResolveImage` were evaluated for the same extraction and found to have no comparable pure kernel left over: `runBlitImage`'s loop is inseparable from its BC/ETC2/ASTC decode-then-resample pipeline, and `runResolveImage`'s loop is already a direct, un-block-shaped nested walk with no unit-conversion or 3D/array-layer branch to factor out -- only a copy needs one. Vulkan command recording/validation and barrier semantics stay in place. | `FeMeImageTests` gained 9 cases (uncompressed/multisample row-byte sizing, compressed-source block-unit conversion with a differently-shaped uncompressed destination, non-divisible-extent ceiling rounding, and every 2D-array/3D-image slice-stepping combination) -- 71/71 total; `check-feme` 3,935 Passed/15 Skipped/10 Unsupported, zero failures; bounded 126-case CTS run (`dEQP-VK.api.copy_and_blit.core.image_to_image.{3d_images,array,simple_tests}.*` plus `.all_formats.color.2d_to_2d.bc1_rgb_unorm_block.*`), 118 Pass/8 NotSupported/zero Fail. See `VulkanCTSReport.md`. |
| ~~WS1(d)~~ | Move `ImageView::resolvedLayerCount()`/`effectiveViewLayerCount`'s 2D-view-of-3D-image depth-slice-as-array-layer resolution (`RenderPass.cpp`'s attachment-view path) into `FeMeImage` (`feme/include/feme/Image/ViewMaterialization.h`, `feme/lib/Image/ViewMaterialization.cpp`'s `effectiveViewLayerCount`/`resolveAttachmentViewLayerCount`), building on `WS1(b)`'s `resolveImageViewExtent`. Decision: the two paths stay **deliberately distinct**, not merged into `resolveImageViewExtent` -- `ImageView::resolvedLayerCount()` does not clamp an explicit (non-sentinel) `layerCount` against the image's remaining layers the way the sampling/storage path does, and this extraction is pure code motion, not a behavior change; converging them would silently change attachment-view validation. `resolveAttachmentViewLayerCount` documents this difference explicitly so a future change that *does* want convergence has to do so on purpose. | `FeMeImageTests` gained 7 cases (non-3D-image pass-through, 3D-image-with-3D-view pass-through, 3D-image-with-non-3D-view depth substitution, explicit-count pass-through, `VK_REMAINING_ARRAY_LAYERS` resolution against array layers and against depth for the 2D-view-of-3D-image case, and the deliberate non-clamping of an explicit count) -- 78/78 total; `check-feme` 3,967 discovered/3,942 Passed/15 Skipped/10 Unsupported, zero failures; bounded 36-case CTS run (`dEQP-VK.renderpasses.renderpass{1,2}.remaining_array_layers.*` + `.suballocation.multisample_resolve.resolve_into_3d_image.*`), 34 Pass/2 NotSupported (expected sample-count unsupported)/zero Fail. See `VulkanCTSReport.md`. |
| ~~WS2(a)~~ | Move the FIFO single-worker task-runner mechanics underlying `feme::vulkan::QueueExecutor` (`Sync.h`) -- its `std::thread`/mutex/condition-variable/`std::deque<std::function<void()>>` enqueue/drain/join loop -- into a new, API-neutral `FeMeExecution` library (`feme/include/feme/Execution/TaskExecutor.h`, `feme/lib/Execution/TaskExecutor.cpp`); none of it ever depended on any Vulkan type. `QueueExecutor` becomes a thin composed wrapper around `feme::execution::TaskExecutor`, supplying only this queue's own Vulkan-specific idle-wait timeout policy (`getQueueIdleSafetyNetTimeoutNs()`); every existing Vulkan caller's `enqueue(...)`/no-argument `waitIdle()` call sites are unchanged. `Fence`/`Event`/`Semaphore` and the actual `vkQueueSubmit`/`vkQueueSubmit2` task-building logic stay in `Sync.cpp`/`Objects.h` -- they are genuinely Vulkan fence/semaphore/queue semantics, not task-dispatch mechanics, and the retained-work-storage half of WS2's original scope (`ExecutionLifetime.h`'s `PendingExecutionRefCounted`, and the actual command-execution/CPU-draw-worker paths) is carried forward as `WS2(b)` below. | `FeMeExecutionTests` (6 cases: non-blocking enqueue, FIFO task ordering, `waitIdle` draining all pending tasks, `waitIdle` genuinely timing out on a still-running task, destructor draining pending tasks before joining, concurrent enqueue from multiple threads); `check-feme` 3,973 discovered/3,948 Passed/15 Skipped/10 Unsupported, zero failures; bounded 490-case CTS run (`dEQP-VK.api.device_init.*`/`api.smoke.*` plus `synchronization.basic.*`/`synchronization2.timeline_semaphore.{wait_before_signal,one_to_n}.*` samples), 490/490 completed, 477 Pass/12 NotSupported/zero unexpected failures (one unrelated `TimedOut` on `create_instance_device_intentional_alloc_fail`, an allocation-failure-injection test with no dependency on queue task execution). See `VulkanCTSReport.md`. |
| ~~WS2(b)~~ | Generalize `feme::vulkan::PendingExecutionRefCounted` (`ExecutionLifetime.h`)'s retain/release-with-deferred-destroy pattern -- presently storing a `const VkAllocationCallbacks *` as its opaque deferred-destroy payload, the only Vulkan-specific detail in an otherwise type-agnostic counter/mutex class -- into `FeMeExecution` (`feme/include/feme/Execution/DeferredDestroyRefCounted.h`, `feme/lib/Execution/DeferredDestroyRefCounted.cpp`), parameterized on the payload type (`DeferredDestroyRefCounted<PayloadT>`). `PendingExecutionRefCounted` becomes a plain type alias instantiating it on `const VkAllocationCallbacks *` -- not a wrapper like `WS2(a)`'s `QueueExecutor`, since no Vulkan-specific policy sits on top of this class's behavior for any of its twelve consumers. This is WS2's "execution-owned storage/retention" half; `Fence`/`Event`/`Semaphore` state and the actual `vkQueueSubmit`/`executeCommandBuffer` task-building logic (command execution/CPU-draw-worker paths) stay Vulkan-specific and are not in scope here. | `FeMeExecutionTests` gains 5 cases (immediate destroy with no pending tasks, deferred destroy released by one/two retains, release-without-a-destroy-request, and a non-Vulkan `int`/pointer payload type) -- 11/11 total; every existing `ExecutionLifetime.h` consumer (`Pipeline`/`RenderPass`/`Framebuffer`/`Buffer`/`Image`/`ImageView`/`DescriptorSet`/`BufferView`/`Sampler`/`Event`/`QueryPool`/`DeviceMemory`/`CommandBuffer`) recompiles with zero source changes; the existing Vulkan-level `ExecutionLifetimeTest` suite (`SyncTest.cpp`, unchanged) continues to pass unmodified; `check-feme` 3,978 discovered/3,953 Passed/15 Skipped/10 Unsupported, zero failures; bounded 490-case CTS run (same case list as `WS2(a)`), 490/490 completed, 477 Pass/12 NotSupported/zero unexpected failures (same one unrelated `TimedOut`). See `VulkanCTSReport.md`. |
| ~~WS3(a)~~ | Add a `check-feme-shared-core` target (`feme/test/CMakeLists.txt`) for FeMe's shared-core, API-neutral library test binaries (`FeMeExecutionTests`, `FeMeImageTests`), reporting named test failures rather than just an aggregate pass count. The target now uses LIT's GoogleTest format with a scoped filter, not a separate direct-gtest runner; `check-feme-fuzz` likewise uses LIT seed-corpus tests. "Move portable tests with their helpers" was already done incrementally: every `WS0`-`WS2` extraction added its new portable logic's tests directly to one of these two binaries, rather than only exercising it indirectly through `FeMeVulkanTests`/CTS. This remains a fast, scoped gate after shared-core-only changes. | Historical validation before the LIT-target migration: both binaries passed, with named failures (none) reported; full `check-feme` 3,978 discovered/3,953 Passed/15 Skipped/10 Unsupported, zero failures. No production code changed, so no CTS re-run was needed. |
| ~~WS3(b)~~ | Build the still-missing half of `WS3`'s original exit evidence ("byte/image comparisons and unchanged capability manifests"). The "capability manifests" piece is new: `feme/utils/check-feme-capability-manifest.py` re-runs `vk_gen_feature_inventory.py`/`vk_gen_extension_inventory.py` with the same inputs `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` were generated from and diffs each against its doc's committed table, naming the specific drifted row(s) rather than only "something changed" -- automating a check every prior session performed by hand and recorded as unverified prose. The "byte/image comparisons"/named-CTS-failure piece turned out to already exist: `vk_cts_reconcile.py`'s `--baseline-qpa` comparison (built well before this roadmap, for `ENV6`-era CTS triage) already diffs two QPA runs' actual per-case statuses, not aggregate totals, and dEQP-VK's own cases already perform the underlying byte/image comparisons -- there was no new mechanism to build there, only to recognize it already satisfies this exit evidence. The new script is not a `check-feme` build target (it needs a real external `vk.xml` not guaranteed present in every build environment); instead it is covered by its own `check-feme` lit test (`run-check-feme-capability-manifest.test`) against a fake-generator fixture, and documented as a manual gate alongside each inventory doc's own "regenerate after changing" instructions. | `check-feme-capability-manifest.py` reports both inventories "unchanged" against their current committed tables; a deliberate single-row edit (reverted before commit) was correctly reported as drifted, naming that exact row. `run-check-feme-capability-manifest.test` passes, exercising both the "unchanged" and "drifted" paths against a fake generator. `check-feme` 3,979 discovered/3,954 Passed/15 Skipped/10 Unsupported, zero failures. No production code changed, so no CTS re-run was needed; `vk_cts_reconcile.py`'s pre-existing `--baseline-qpa` named-diff capability was verified by reading its own source and `.instructions.md`'s CTS-triage notes, not re-tested here. |

`WS2` is now **closed** with no further lettered rows: its third original
piece, command execution/CPU-draw-worker paths (`makeSubmissionTask`'s
wait/execute/signal closure and `Fence`-signal closure in `Sync.cpp`, and
`executeCommandBuffer`'s dispatch loop in `CommandBuffer.cpp`), was
re-examined directly (not just by inference from `WS2(a)`/`WS2(b)`'s own
scope notes) and found to have no portable kernel left to extract after
`WS2(a)`'s task-runner and `WS2(b)`'s retained-work-storage pieces are
both already out: `makeSubmissionTask`'s closure body is built entirely
from Vulkan-specific types (`Device`, `CommandBuffer`, `SemaphoreOp`,
`Fence`, `VkResult`) with no generic task-dispatch mechanics left inside
it, and `executeCommandBuffer`'s dispatch loop is inseparable from bound
pipeline/descriptor-set/push-constant/graphics state -- genuine command
execution semantics, not task-dispatch machinery `FeMeExecution` should
own. This closes the open question `WS2(a)`/`WS2(b)`'s own exit-evidence
notes both carried forward.

`WS3` is now also **closed** with no further lettered rows: `WS3(a)`'s
`check-feme-shared-core` and `WS3(b)`'s `check-feme-capability-manifest`
together cover every piece of the original row's exit-evidence text
("`check-feme`, affected CTS groups, byte/image comparisons and
unchanged capability manifests; compare named failures, not only pass
totals") -- the test-subset/named-gtest-failure half, the
capability-manifest-drift half, and (via the pre-existing
`vk_cts_reconcile.py`) the named-CTS-failure half.

Separate mechanical extraction from new behavior. First preserve the Vulkan
callers and test results, then add a small D3D consumer. Do not combine a file
move, scheduler rewrite, ABI change, and new feature in one commit.

## Direct3D milestones

### W0: Integration and test feasibility

Depends on no shared refactoring. Its output is a written decision and a
throwaway-capable prototype, not an adapter compliance claim.

| Step | Work and acceptance |
|---|---|
| W0(a) | Pin OS builds, SDK/WDK, DDI, inbox/Agility runtime versions, and reference WARP binary. Prove discovery, opening, callback negotiation, and teardown for the selected WARP identity through the application-local user-mode path, with the existing WARP kernel-mode driver unmodified. W0(e) supplies host measurements; W0(f) supplies historical matching container measurements; W0(g) supplies the prototype/DDI-negotiation proof. Installed render-only/software-adapter feasibility, miniport/INF/signing/CI requirements are outside the selected deployment contract. |
| ~~W0(b)~~ | ~~Product/compatibility decision.~~ User decision (2026-11-17): select a WARP-compatible application-local UMD drop-in using Microsoft's WARP identity/device IDs and relying on its existing, unmodified kernel-mode driver. Do not add a kernel driver, replace system WARP binaries, or build an application-local COM/DXGI runtime. "Drop-in" is limited to the selected application-local packaging/selection path; the existing probe proves routing but not a working resource/device contract or universal transparent substitution. Do not depend on private WARP ABI/ordinal 199 without a separate approval. See [decision and limits](W0WindowsIntegrationProbe.md#w0b-decision-warp-compatible-application-local-umd). |
| W0(c) | Classify the local test corpus: public black-box tests, WARP-private/white-box tests, DXBC/DXIL inputs, optional/preview features, display/HLK dependencies. Determine required legacy shader support from the selected contract. Review source/test redistribution permissions before reuse; author LLVM-owned focused tests from public contracts. |
| W0(d) | Add an explicit selection assertion to the future runner: verify Microsoft's WARP vendor/device/software identity and independently prove that the selected application-local UMD is the FeMe build, not the system WARP module. List and execute one applicable test on reference WARP, then demonstrate selection of the prototype through the chosen boundary. Record unavailable infrastructure as blocked, not Pass. WARP-only white-box tests remain oracle tests, never generic FeMe coverage. |
| ~~W0(e)~~ | ~~Measure the no-install host integration paths: inbox/Agility runtime and reference-WARP versions, `CreateSoftwareAdapter`, executable-local WARP substitution, and an LLVM-owned rejection stub with DDI entry-point counters. Run an applicable public-API WARP queue/fence test.~~ Completed 2026-11-06; evidence and reproduction instructions in [W0WindowsIntegrationProbe.md](W0WindowsIntegrationProbe.md). This closes measurements only, not FeMe device creation. |
| ~~W0(f)~~ | ~~Container-first user-mode proof: build the standalone probe in the Windows container; run all four CTest cases and record OS/kit/runtime/WARP identities and exact outcomes before implementation. Verify reference WARP and the expected pre-DDI stub rejection; do not substitute host results.~~ Completed 2026-11-06; container evidence, an extended NuGet-WARP/Agility-core matrix, and reproduction instructions are in [W0WindowsIntegrationProbe.md](W0WindowsIntegrationProbe.md#container-measurements). All four CTest cases passed with no discrepancy from the host measurements. This closes the container measurement step only -- it is not FeMe DDI negotiation; `W0(g)` below carries that forward. |
| W0(g) | ~~Identify a public general-provider identity/thunk bootstrap~~ -- search completed with a negative result. ~~Reach owned `OpenAdapter12` through stock runtime and query/close its adapter table~~ -- W0(h) supplies historical host/container inbox/Agility evidence under Microsoft's WARP identity. ~~Audit one pinned supported DDI version's public GetCaps/thunk-service surface~~ -- historical container evidence (2026-11-06) shows pinning a version alone does not reach `CreateDevice`; truthfully answering the first observed cap reaches it at pinned `D3D12DDI_SUPPORTED_0010`. ~~Audit one user-mode callback's liveness~~ -- `p12UMCallbacks->pfnSetErrorCb` completed under SEH; kernel-routed (`pKTCallbacks`) allocation/residency callbacks remain uninvoked. The VM-first public DDI matrix (WDK 10.0.26100.0, Windows 11 Enterprise build 26300 ARM64, inbox runtime 10.0.26100.9818) found that builds `D3D12DDI_SUPPORTED_0070` and `_0071` return `DXGI_ERROR_UNSUPPORTED` before `CalcPrivateDeviceSize`/`CreateDevice`; `_0072` and all other tested versions reach the deliberate `E_NOTIMPL` stub. The 0070/0071 results repeated three times; the reason for this narrow rejection is unknown. Remaining: explain the 0070/0071 rejection, implement the required owned DDI/device services using the unmodified WARP kernel-mode driver, exercise the required kernel-routed callback/resource lifecycle, and prove real device creation/teardown under the selected WARP identity. An `E_NOTIMPL` audit stub is not a working device. Keep investigation VM-first and stop before unapproved private ABI or system-WARP replacement. [Earlier negotiation/callback evidence](W0WindowsIntegrationProbe.md#w0g-follow-up-createdevice-is-reached-both-thunk-service-pointer-families-are-non-null), [callback-probe/version-control evidence](W0WindowsIntegrationProbe.md#w0g-follow-up-callback-liveness-probe-and-newer-version-control), [VM matrix evidence](W0WindowsIntegrationProbe.md#w0g-public-ddi-version-matrix-vm-measurements). |
| ~~W0(h)~~ | ~~Bounded alternative-B investigation: isolate reference forwarding, named-export forwarding, ordinal-199 forwarding, bare owned DDI and owned adapter-table cases; compare WARP selection with generic software-adapter rejection; build/test inside the container and verify inbox/Agility runtime paths.~~ Completed 2026-11-06: owned open/query/close needs neither private ordinal nor forwarding at this stage. Explicit preloading retains diagnostic counters; a debugger also proves executable-local entry without preloading. [Evidence, exact counters and remaining limits](W0WindowsIntegrationProbe.md#alternative-b-application-local-warp-ddi-routing). No real FeMe device or supported DDI is claimed; the selected WARP-compatible product contract does not imply a functioning drop-in yet. |

W0(a) and W0(d) are **partially done**, not complete. W0(e) supplies the host
version/loading measurements, W0(f) supplies the matching container
measurements, and both supply W0(d)'s reference-test evidence. W0(h) adds
OS-routed owned DDI evidence, including version/capability queries and close,
but still under Microsoft's existing adapter identity. W0(b)'s product choice
is now closed; W0(g)'s supported DDI/device/thunk work remains open.
Answering the two public `D3D12DDICAPS_TYPE` capability queries truthfully
let `CreateDevice` be reached with both thunk-service pointer families
populated, and one user-mode diagnostic callback was called successfully.
The kernel-routed allocation/residency callbacks remain untested, and the
audit stub is not a working device or proof that WARP's kernel-mode driver
accepts FeMe's resource contract.
The [strict negotiation and versioned callback follow-up](W0WindowsIntegrationProbe.md#w0g-strict-negotiation-gates-and-versioned-callback-audit-vm)
now gates the expected rejection/`CreateDevice` stage, exact negotiated DDI,
owned-module identity, and successful diagnostic callback completion.
The 0070/0071 rejection also reproduces natively on ARM64 with inbox and
Agility 619, so it is not limited to x64 emulation; its exact cause remains
unknown. The public 0022-and-later core-layer callback table supplies
allocation/deallocation entries even when the legacy KT entries are null.
These entries are audited, not invoked; this is not resource-lifecycle
evidence or closure of W0(g).
The subsequent [public paging-queue lifecycle probe](W0WindowsIntegrationProbe.md#w0g-public-kernel-paging-queue-lifecycle-vm)
does invoke the KT create/destroy paging-queue pair: both return `S_OK`,
with valid queue/sync handles and a fence mapping, on x64 inbox, native
ARM64 inbox and native Agility 619 at DDIs 0010/0022/0072/0110.
The probe is opt-in and has strict outcome and mock failure/cleanup tests.
All standalone Windows probe suites now use LIT through the
`check-feme-windows-probes` build target, with no CTest registrations; see the
[runner correction and current validation](W0WindowsIntegrationProbe.md#w0g-lit-test-runner-correction).
This closes the pointer-only limitation for that pair, not W0(g): buffer/
texture allocation and residency through the runtime-owned device, and a
real owned D3D12 device, are still unproven. The 0070/0071 rejection remains
unexplained.
The separate [public KMT standard-allocation control](W0WindowsIntegrationProbe.md#w0g-public-kmt-standard-allocation-and-residency-control-vm)
now opens the selected WARP LUID, creates a separate KMT device and a 64 KiB
existing-heap allocation without private driver data, makes it resident,
waits for its paging fence, evicts it and tears everything down. This
passes on native ARM64 and x64, with strict status/output checks and mock
failure/cleanup coverage. It establishes a public OS allocation/residency
control, not integration with FeMe's runtime-owned allocation callbacks,
an owned D3D12 heap/resource or closure of W0(g). No transfer between those
devices is implemented; their handles must not be interchanged.
The [reference buffer-copy oracle](W0WindowsIntegrationProbe.md#w0d-public-buffer-copy-and-readback-oracle-vm)
now verifies an ordinary default heap/placed buffer, upload/readback
resources, copy-list submission, fence completion and all 65,536 result
bytes on x64 inbox, native ARM64 inbox and native Agility 619. This supplies
W0(d)'s resource-data reference test for the future W1(a) heap/buffer path,
not FeMe execution or a bridge from the separate KMT device. Both reference
entry paths and nine CPU validation controls run through LIT.
The [public GPU VA reservation investigation](W0WindowsIntegrationProbe.md#w0g-public-gpu-va-reservation-boundary-vm)
does not supply the missing allocation path: a 64 KiB unmapped reservation
through the runtime-owned KT callback returns `E_INVALIDARG` on x64 inbox,
native inbox and native Agility 619. A separate public KMT diagnostic reports
`GpuMmuSupported=0`, `IoMmuSupported=1` and also rejects the reservation.
Strict LIT gates distinguish this measured rejection from successful
reserve/free; only mocks exercise successful free and its failure paths.
Do not substitute GpuMmu address-space management for an audited WARP
CPU-memory/resource contract or infer that all public integration is blocked.
The [runtime-owned monitored-fence lifecycle](W0WindowsIntegrationProbe.md#w0g-runtime-owned-monitored-fence-cpu-lifecycle-vm)
now succeeds on all three configurations at DDIs 0010/0022/0072/0110:
create an initially-zero fence, register a pending CPU event wait, signal
value 7 through the CPU callback, observe event completion and the read-only
mapped value, then destroy the object. Strict LIT checks and 26 mock
scenarios cover this independent synchronization service, not a real FeMe
fence/device or allocation. The public IoMmu contract still requires VidMm
allocation; shared CPU addresses do not justify bypassing it with
`VirtualAlloc` alone. W0(g)'s resource/device gate remains open.
The negative public general-provider search and the successful WARP-specific
route are different findings; do not classify all further research as blocked.
Installed-path evaluation remains separate. No driver installation, signing
changes, system-DLL replacement or reboot was performed on host or container.

`W0(c)` is **partially done**, recorded in
[W0DirectXTestCorpusClassification.md](W0DirectXTestCorpusClassification.md):
the local test corpus is classified into the five named categories (public
black-box, WARP-private/white-box, DXBC/DXIL-input, optional/preview-feature,
display/HLK-dependency), and source/test redistribution permissions are
reviewed and recorded there. The DXBC/DXIL-input category's one-by-one
triage is now complete (2026-11-06): of the ten candidate files, nine
(`BasicAPI.cpp`, `GBVExecuteIndirect.cpp`, `Library.cpp`, `MultiGPU.cpp`,
`PipelineStateObject.cpp`, `Raytracing.cpp`, `ResourceBinding.cpp`,
`ShaderRootSig.cpp`, `util.{cpp,h}`) are confirmed DXBC-input (hardcoded DXBC
blobs, `D3DCompile` calls with legacy `vs`/`ps_4_0`/`5_0`/`5_1` profiles, or
direct DXBC-container parsing); only `pch.hpp` is merely DXBC-adjacent (a
header include, no blob or parsing logic of its own).
`ResourceBinding.cpp`'s explicit `bTestDXBCShaders` flag is the strongest
evidence that WARP's own suite treats DXBC shader-model support as a
first-class, deliberately dual-tested compatibility surface rather than an
incidental one. The [public-contract and real-FXC audit](W0DirectXTestCorpusClassification.md#legacy-shader-compatibility-audit-vm-2026-11-18)
now establishes the SM 5.1 input obligation and tests original HLSL compiled
as `cs_5_0`/`cs_5_1`, including a nonzero register space. Import/translation
passes and the audit fixed dropped compute thread-group metadata, with
portable LIT regression coverage. The
[legacy raw-buffer export follow-up](W0DirectXTestCorpusClassification.md#legacy-raw-buffer-dxil-export-follow-up-2026-11-20)
fixes the unraised legacy resource call and unused-coordinate encoding
behind the legalization failure. Both FXC profiles now export DXIL, with
LIT payload re-import/LLVM verification and arithmetic, binding and
thread-group assertions; 171 related regressions pass. The
[reference compute oracle](W0WindowsIntegrationProbe.md#w0d-legacy-compute-dispatchreadback-oracle-vm-2026-11-20)
now executes both profiles as FXC DXBC and SDK-validated FeMe DXIL,
checking 256 scalar results and the entire 64 KiB output/guard buffer.
It fixed leaked private metadata and validator/PSV version mismatch;
bare DXIL export is still rejected and requires public SDK validation.
The ARM64 inbox, x64 inbox and ARM64 Agility 619 LIT matrices each pass
40/40. Owned-DDI/PSO execution, broader bindings and all-stage coverage
remain under the existing W1/W2/W3/W4 gates. This is not completed legacy
compatibility or an owned FeMe device pass.
LLVM-owned reference tests now exist in the
standalone probe, but owned-DDI FeMe execution tests still need the prototype/runner
from `W0(g)`/`W1(a)`. The W0(b) product choice is closed, but these
implementation dependencies still block execution evidence, not a
blanket host/VM blocker. `W0(c)` is left unstruck rather than
split into further lettered rows -- both blockers are existing upstream `W0`/
`W1` milestones, not new work needing its own breakdown.

`W0(d)`'s future-runner WARP-identity and FeMe-module-provenance assertion
is also recorded in that document. The strict owned-shim audit now checks
adapter identity, loaded-module handle identity and absence of reference
forwarding, but no real FeMe device/build-provenance pairing exists yet.
W0(e)/W0(f)'s
standalone runner implements reference-WARP identity checking and the
public queue/fence oracle test in both the host and the container. The VM
follow-up adds the byte-exact buffer-copy/readback oracle described above. It cannot
yet verify a real FeMe module/device pairing; the selected WARP-compatible
path is no longer blocked on a product-choice gate.

### W1: Objects and first headless compute

Depends on the selected W0 integration proof (W0(a)/W0(g)) and WS2; reuses
existing `CompiledStage` and artifact reflection. Pending installed-driver,
display or HLK evaluation does not block
user-mode headless W1 once the selected boundary is proven.

| Step | Work and acceptance |
|---|---|
| W1(a) | Optional Windows build/export target, device and direct/compute/copy queues, allocators/lists, buffers/heaps, fences, PSOs, allocation accounting, and device-removal propagation. Keep runtime/DDI handles outside portable execution code. Check the export set and coexistence with DXC and the Vulkan ICD in one process. Test invalid handles/sizes, OOM, close/reset/replay, and teardown with pending work. |
| W1(b) | Resource-free DXIL dispatch, then one structured/raw-buffer write, copy/readback and fence. Validate IDs, partial waves, multi-wave groupshared barriers, concurrent PSO compilation, context/code lifetime, and repeated execution against scalar expectations and WARP. |
| W1(c) | Build a versioned capability inventory tied to named tests and normative SDK requirements. Early partial execution is an internal harness, not a falsely advertised lower feature level: even FL 11_0 requires graphics. Pin/query actual wave policy; test requested wave sizes separately. |

W1(b) prerequisite progress (2026-11-20): the
[grouped SM6 oracle](W0WindowsIntegrationProbe.md#w1b-grouped-sm6-compute-oracle-vm-2026-11-20)
executes original DXC and SDK-validated FeMe-re-emitted DXIL with
`(8,4,2)` threads across `(2,2,2)` groups, 3-D IDs, shared-memory exchange
and three group barriers. It verifies 512 results and every byte of the
64 KiB output/guard buffer. LLVM DXIL preparation now removes the modern
GUID metadata added when reading older bitcode, with a portable regression
that preserves debug information. The three reference-runtime LIT matrices
passed 59/59 at that checkpoint. The
[legacy grouped follow-up](W0WindowsIntegrationProbe.md#w1b-legacy-grouped-compute-follow-up-vm-2026-11-20)
now translates raw/structured TGSM declarations and accesses plus DXBC
synchronization flags. Original FXC `cs_5_0`/`cs_5_1` shaders and their
SDK-validated FeMe DXIL execute the same grouped oracle through both public
adapter paths; all three expanded reference-runtime LIT matrices pass 67/67.
Nonstandard group-UAV/thread-only barrier modes translate to LLVM IR but
remain explicitly unsupported for DXIL export rather than asserting.
The [partial-wave follow-up](W0WindowsIntegrationProbe.md#w1b-partial-wave-policy-and-repeated-compute-oracle-vm-2026-11-20)
now queries the public runtime's wave bounds and executes a 30-thread
`(5,3,2)` group across `(3,2,1)` groups. All three reference runtimes report
and produce four-lane waves, with six partial waves per dispatch. The
oracle reconstructs membership without assuming local-thread/lane ordering,
checks wave reductions/first-lane identity and every output/guard byte,
and verifies three dispatches using the same PSO/resources and reset
allocator/list, reinitializing output before each dispatch.
DXIL import now raises `WaveReadLaneFirst`; the expanded reference LIT
matrices each pass 99/99. This establishes reference-only wave-policy,
partial-wave and sequential reuse evidence, not an owned wave policy or
requested-wave-size support. Concurrent owned PSO/context lifetimes and
owned execution remain unproven. The
[FeMe CPU follow-up](W0WindowsIntegrationProbe.md#w1b-feme-cpu-partial-wave-execution-vm-2026-11-20)
now runs that same original DXC and re-emitted DXIL shader through
`feme-run` at widths 4/8/16/32, using the identical wave-membership,
scalar-value and full-buffer guard oracle. It fixes dedicated
`WaveReadLaneFirst` CPU uniformity/widening/lowering, including vector
operands, divergent-region masks and empty masks. Six partial waves are
verified at every selected width. The combined reference/CPU LIT matrices
pass 130/130 each; the rebuilt complete suite passes 4,000 tests with zero
failures. This proves FeMe's portable compiler/executor behavior, not an
owned D3D12 PSO/device, requested D3D wave-size attributes or concurrent
owned lifetimes. W1(b) stays open, and owned `CreateDevice` remains
deliberately `E_NOTIMPL`.

The [retained CPU execution follow-up](W0WindowsIntegrationProbe.md#w1b-retained-cpu-dispatch-and-compilation-context-lifetimes-vm-2026-11-20)
adds `feme-run --repeat=3`: each of those sixteen CPU cases now compiles
once and validates all three completed dispatches, keeping code, the
worker pool and resource storage alive. Stateful JIT/reference/AOT tests
reject skipped dispatches, stale snapshots and implicit resource resets;
a real DXC seven-thread shader checks previous-output exchange across
full/partial waves with a load-bearing group barrier. Independent stages
also compile concurrently at widths 4/8, outlive their compilation
contexts, and reuse prepared arguments for concurrent guarded execution.
All three Windows LIT matrices still pass 130/130, now including 48 CPU
dispatch readbacks each; the full suite passes 4,003 tests with zero
failures. These are portable compiled-stage lifetime/reuse prerequisites,
not concurrent D3D12 PSO compilation, pending-command teardown or owned
command-list/fence replay. W1(a)/W1(b) and owned `CreateDevice` remain open.

The [legacy CPU follow-up](W0WindowsIntegrationProbe.md#w1b-legacy-dxbc-cpu-execution-vm-2026-11-20)
now accepts original FXC DXBC in `feme-run`, sharing container detection
with `feme::Driver` and reusing DXSA translation plus DXIL raising. The
real pilot exposed missing CPU integer multiply-add normalization; both
SIMD and scalar-reference paths now reuse the shared DXIL intrinsic
expansion, with wrapping-overflow regression coverage. Twenty-four CPU
cases execute raw/grouped SM5.0/5.1 originals and re-emitted DXIL at widths
4/8, including SM5.1 spaces 0/7. Every case validates three complete
64 KiB readbacks with the same scalar/grouped WARP oracle. All three
expanded LIT matrices pass 158/158; FXC-only and default configurations
pass 103/103 and 82/82. The rebuilt full suite with FXC enabled passes
4,010 tests with zero failures, and both capability inventories reproduce
unchanged. This advances W0(c)'s mandatory legacy compute prerequisite,
not general legacy/all-stage compatibility or owned D3D12 execution.
W0(c), W0(g), W1(a) and W1(b) remain open.

### W2: Binding, resources, and synchronization

Depends on W1 and WS0-WS1. General CBVs and unbounded/non-uniform binding are
compiler prerequisites, not work that a DDI wrapper can hide.

| Step | Work and acceptance |
|---|---|
| W2(a) | Root Signature 1.0/1.1 parsing and immutable binding plans: register class/space/range, visibility, offsets, constants, root descriptors, tables and static samplers. Replace use-inferred cbuffer promotion with an explicit root-layout mapping; test multiple CBVs, overlapping register numbers of different classes, arrays, dynamic indices and non-32-bit accesses. |
| W2(b) | Descriptor creation/copies, execution-time preparation, volatility/static promises, unbounded and per-lane non-uniform ranges, UAV counters/atomics, and GPU-VA-to-bounded-allocation lookup. Keep bounds checks; never treat an arbitrary GPU VA as a trusted CPU pointer. Test updates after recording, heap rebinding, null/OOB descriptors, aliasing and allocation retention. |
| W2(c) | Committed/placed/reserved resources, residency/budgets, typeless view compatibility, planes/subresources, texture copy footprints and shared image/sampler preparation. Test D3D row/placement alignment separately from internal storage; exercise BC, sRGB, integer, depth/stencil and multisample operations individually. |
| W2(d) | Queue-ordered copies/dispatch/indirect commands, legacy transitions including promotion/decay and split/alias/UAV barriers, queries/predication, and host/queue fence events. Preserve the dependency model needed by enhanced barriers. Test cross-queue progress, decreasing fence values, replay, timeout/removal wakeup, and indirect argument overflow. |

W2(a) prerequisite progress (2026-11-21): the
[root-binding-plan follow-up](W0WindowsIntegrationProbe.md#w2a-root-signature-parsing-and-immutable-binding-plans-vm-2026-11-21)
adds owned, read-only Root Signature 1.0/1.1 reflection and
`feme-run --inspect-root-signature`, accepting raw serialized layouts and
DXBC/DXIL RTS0 containers without dispatch. Plans preserve parameter kinds,
register classes/spaces/ranges, visibility, constants, root descriptors,
static samplers and serialized 1.1 flags, and resolve APPEND offsets.
They check register/table overlap, overflowing ranges and the 64-DWORD root
budget. LLVM's shared container views now check complete array bounds and
honor absolute range offsets, including noncontiguous/backward ranges and
samplers preceding parameter payloads. Canonical root metadata has an
error-returning plan reader that distinguishes absence from invalid input.
Twelve new unit cases and three source fixtures include real DXC/FXC
shader/root-only containers; the rebuilt full FeMe LIT suite passes
4,025 tests with zero failures. Both capability inventories reproduce
unchanged. This is a compiler/frontend parsing prerequisite, not root-driven
CPU lowering, descriptor preparation, shader/root compatibility validation
or enforcement of static/volatility promises. Explicit root-layout mapping
and multiple-CBV execution remain open under W2(a); W1 and owned
`CreateDevice` remain open.

W2(a) constant-row prerequisite progress (2026-11-21): the
[wide-row follow-up](W0WindowsIntegrationProbe.md#w2a-wide-constant-rows-and-bounded-reads-vm-2026-11-21)
extends the existing single use-inferred binding to homogeneous 16-/32-/64-bit
integer and floating-point rows, sharing the DXIL canonical shape matcher.
Array/row offsets use 64-bit arithmetic, declared spans must fit the
32-bit ABI size, and each scalar is independently bounded against its
array element and supplied bytes. Valid partial-row components survive;
invalid or incomplete components read zero without cross-element borrowing.
Odd strides use conservative alignment. AOT `feme-run --object` now receives
YAML root bytes through the same packing helper as normal/reference execution.
Three new unit cases and four source fixtures cover all six scalar types,
real native-16-bit DXC packing, varying indices, arithmetic overflow,
partial waves, repeated dispatches, and short/empty AOT input. Focused LIT
passes 700 cases; the rebuilt full suite passes 4,032 with zero failures.
This is not root-driven binding, general descriptor-backed CBVs, D3D12
execution or a W2(a) completion claim.

W2(a) explicit-constant selection progress (2026-11-21): the
[explicit-root follow-up](W0WindowsIntegrationProbe.md#w2a-explicit-root-constant-selection-vm-2026-11-21)
adds opt-in `--root-signature-constants` to `feme` and `feme-run`, with
matching stage/JIT API options. It selects the sole visible inline-constant
parameter from the checked 1.0/1.1 plan; missing/malformed layouts,
multiple visible constant parameters, denied stages and local signatures
are errors, not inferred fallbacks. The parameter's DWORD span controls
reflection and clips reads even when additional host bytes are supplied.
Descriptor-backed CBVs and cbuffer arrays are not promoted. Shared frontend
RTS0 preservation fixes the container payload previously lost before CPU
selection, including original FXC DXBC. Nine unit cases and three source
fixtures cover normal/reference/AOT execution, real DXC 1.0/1.1 layouts,
native 16-/64-bit packing, FXC constants, visibility, diagnostics and the
unchanged inferred default. Focused/full LIT pass 839/4,044 cases with
zero failures. This closes a single-parameter root-constant mapping subset,
not W2(a): multiple inline parameters, general CBVs, table preparation,
full shader/root compatibility and owned D3D12 execution remain open.

W2(a) declared-binding preflight progress (2026-11-21): opt-in explicit
constant selection now validates live bound-resource declarations before
normalization and selection metadata attachment. The entry/direct-helper
graph must match the checked root plan's class, space and stage visibility,
with each complete declaration contained in one binding. Native DXC rejects
an array split across adjacent singleton ranges; the regression retains
that SDK rejection and uses `/Vd` to independently test FeMe's rejection.
Dynamic/malformed declarations and indirect calls fail explicitly;
unrelated functions and dead handles do not contribute bindings. Seven unit
cases and a real DXC source fixture cover classes, holes, adjacency,
unbounded endpoints, samplers, helper reachability and exact repeated
normal/reference/AOT buffer-array output. The inferred default is unchanged.
This is binding identity validation, not descriptor preparation, class-aware
heap remapping, full resource-kind/promise compatibility or W2(a) completion.
Focused LIT passes 827 cases; the rebuilt full suite passes 4,052, with
15 skipped, 10 unsupported and zero failures, from 4,077 discovered tests.

W2(a) buffer-namespace progress (2026-11-21): bound raw/typed buffers now
retain distinct DXIL SRV/UAV identities through normalization, reflection
and physical heap preparation. Equal register numbers no longer merge or
conflict across classes; each array gets its own bounded prefix range.
Host `BoundResourceBinding` and `feme-run` YAML support namespace
qualification (`register-class: srv|uav`). Unqualified bindings remain
compatible when unambiguous; ambiguous, invalid, duplicate qualified and
unmatched qualified inputs fail explicitly before JIT execution. Metadata
uses six-field `.v2` ranges while retaining legacy/SPIR-V metadata reading.
Artifact ABI 8 preserves namespaces in AOT reflection and rejects version-7
objects; shader argument layouts are unchanged. Combined-prefix overflow
and unbounded declarations remain fail-closed. Six unit cases and a native
DXC fixture cover class isolation, array output, retained dispatches,
partial waves, reference/AOT execution and negative host inputs. This
advances the overlapping-register acceptance subset, not descriptor/table
materialization, multiple inline parameters, general CBVs or owned D3D12
execution.
Focused LIT passes 841 cases; the rebuilt full suite passes 4,059, with
15 skipped, 10 unsupported and zero failures, from 4,084 discovered tests.

W2(a) root-descriptor-kind progress (2026-11-21): explicit-root preflight
now checks the containing parameter kind as well as class/space/range.
Root SRV/UAV descriptors admit raw/structured buffers, not typed buffers or
textures. Descriptor tables retain the existing resource-kind behavior.
A canonical type matrix covers both Root Signature 1.0/1.1, SRV/UAV,
raw/structured/typed buffers, textures, multisample and feedback textures.
The native DXC fixture retains the SDK's typed-SRV root-descriptor rejection
and compiles that layout with `/Vd` to independently verify FeMe's rejection
in normal/reference/AOT compilation. Valid raw/structured roots and typed
tables execute repeated dispatches with full input/output/guard readbacks;
the inferred default remains unchanged. This does not validate counters,
descriptor promises or contents, materialize D3D12 tables, support general
CBVs/multiple inline parameters or close W2(a)/owned device execution.
Focused LIT passes 844 cases; the rebuilt full suite passes 4,062, with
15 skipped, 10 unsupported and zero failures, from 4,087 discovered tests.

W2(a) unread-root-contract progress (2026-11-21): the shared reflection
reader now retains the selected inline parameter's register/space and full
DWORD span even if the shader has no surviving cbuffer reads. It merges
validated selection metadata with existing resource usage, preserving heap
flags/indices/ranges, and creates root-only reflection for resource-free
functions. Lowering and shader argument layouts remain unchanged. Three
unit cases cover absent/use-based metadata, both compilation backends with
and without heap access, and actual native-object reflection bytes. A real
DXC fixture verifies absent cbuffer loads and repeated normal/reference/AOT
execution, partial waves and untouched guards, including resource-free
shaders and omitted unused root bytes. Focused/full LIT pass 848/4,066 cases
with zero failures; artifact ABI stays 8 and inferred behavior is unchanged.
This is stable single-parameter reflection, not multiple inline parameters,
general CBVs, descriptor materialization or owned D3D12 execution.

W2(a)/W2(b) native-heap-flag progress (2026-11-21): opt-in explicit-root
preflight now requires the matching resource/sampler direct-index root flag
for live original heap handles in the entry/direct-helper graph. It runs
before normalization; traditional bound emulation does not require native
flags. Two unit cases cover both signature versions, independent flags,
canonical resource/sampler types, helpers and dead/unrelated handles.
A real SM 6.6 DXC fixture executes nonuniform two-entry resource-heap
indexing on partial waves, retaining three normal/reference/AOT dispatches
with complete guard readbacks. The measured SDK accepts missing/wrong flags
without `/Vd`; FeMe independently rejects both in explicit-root compilation,
while inferred execution is preserved. Focused LIT passes 851 cases in
6.53 seconds; full LIT passes 4,069 with 15 skipped, 10 unsupported and
zero failures, from 4,094 discovered tests in 124.85 seconds. Timings are
not a controlled performance comparison. This is root-flag admission, not full
dynamic descriptor preparation, new sampler execution or owned D3D12
execution; artifact ABI remains 8 and W2(a)/W2(b) stay open.

W2(a) static-sampler-mode progress (2026-11-21): explicit-root preflight
now checks visible static filters against canonical sampler declarations.
Default mode accepts ordinary/minimum/maximum filters; comparison mode
requires comparison filters. The complete reduction field is checked,
not bit 7 alone. Static lookup is shared with range coverage and retains
register/space/visibility and helper/dead-handle behavior. Two unit cases
cover all 36 valid filters, default/comparison/unsupported modes, both
signature versions, tables, native heaps and stage-disjoint samplers.
Canonical IR verifies exact normal/reference/AOT mismatch diagnostics.
Native SDK DXC accepts the mismatched declarations without `/Vd`; a
separate fixture preserves that measurement and existing unraised bound
sampling rejection. Those DXC shaders do not yet reach canonical sampler
preflight, and no new sampler raising/materialization/execution is claimed.
Focused LIT passes 855 in 10.91 seconds; full LIT passes 4,073, with
15 skipped, 10 unsupported and zero failures, from 4,098 discovered tests
in 73.94 seconds. Timings are not a controlled performance comparison.
Inferred admission, artifact ABI 8 and shader argument layouts are unchanged;
table/native-heap descriptor contents and owned D3D12 execution remain
open, as do W2(a)/W2(b).

W2(a) native legacy-sampler progress (2026-11-22): the private
`!dx.resources` reader now preserves checked default/comparison sampler
modes, and legacy `createHandle` raises class-qualified sampler handles
through the existing cast bridge. Register/space/range and relative
indexing are retained; malformed/unsupported modes remain unraised for
admission to reject. A transform fixture covers modes, rebasing, unbounded
ranges and opaque consumers, including an SRV-mislabeled sampler guard.
The real DXC fixture now verifies static-mode mismatches through
normal/reference/AOT compilation with Root Signature 1.0/1.1, without
`/Vd`. Valid comparison linkage retains unsupported comparison-access
admission; ordinary/inferred sampling now reaches the explicit unsupported
bound-descriptor boundary. This supersedes the earlier native-shader
preflight gap, not descriptor materialization or sampling execution.
Focused LIT passes 858 in 4.70 seconds; full LIT passes 4,074, with
15 skipped, 10 unsupported and zero failures, from 4,099 discovered tests
in 69.15 seconds. One new transform fixture adds one pass. Timings are not
a controlled performance comparison. Artifact ABI 8 and shader argument
layouts are unchanged; W2(a)/W2(b) and owned D3D12 execution remain open.

W2(a) level-zero comparison mapping progress (2026-11-22): LLVM now lowers
scalar FP32 `llvm.dx.resource.samplecmplevelzero` to DXIL opcode 65, and
FeMe raises the supported wire form back to that canonical intrinsic.
Real forward/reverse roundtrips cover 1D, 2D and cube-array coordinates,
comparison references and nonzero offsets. Raising requires a comparison
sampler and field-zero scalar uses; status, aggregate, extra-component,
malformed-type and unsupported-dimension cases remain unraised without
adding a duplicate sample read. The actual SDK DXC comparison fixture now
reaches unsupported bound-descriptor admission after valid root linkage.
Canonical bindless comparison sampling is explicitly rejected in
normal/reference/AOT CPU compilation until CPU lowering exists. Implicit
`SampleCmp`, clamps, half/vector results and descriptor/static-sampler
materialization remain open; this is not sampling execution.

Focused FeMe LIT passes 861 in 14.71 seconds; full FeMe LIT passes 4,077,
with 15 skipped, 10 unsupported and zero failures, from 4,102 discovered
tests in 157.62 seconds. DirectX LIT passes 630, with one expected failure
and one unsupported, from 632 discovered tests in 97.36 seconds. The latter
completed serially after an eight-worker run lost a worker process without
a test-specific failure; the interruption's cause is not established.
Three new FeMe fixtures add three passes. Assertions, disabled PCH and the
two-worker build limit are retained; timings are not a controlled speedup.
Artifact ABI 8 and argument layouts are unchanged. W2(a)/W2(b), owned
D3D12 execution and `CreateDevice` (`E_NOTIMPL`) remain open.

W2(a) bounded comparison CPU execution progress (2026-11-22): heap-backed
scalar FP32 `Texture2D::SampleCmpLevelZero` now lowers through the existing
shared comparison image helper. Admission and lowering share one predicate
for texture/sampler handles, two FP32 coordinates, reference and constant
i32 texel offsets. Explicit LOD zero, zero derivatives/bias and no shader
MinLod clamp preserve the level-zero contract. Consumed dead handles are
removed for AOT, and comparison use sets the existing sampler-heap
reflection/artifact flag without changing ABI 8 or argument layouts.

Canonical and actual SDK DXC fixtures verify point and bilinear comparison,
negative offsets, divergent coordinates/references, nonuniform two-entry
image/sampler heaps, five-thread partial waves, complete output guards and
repeated normal/reference/AOT dispatches. Transform tests check explicit
LOD zero, positive/negative offsets, sampler reflection, repeat-pass
idempotence and unsupported dimension/vector/dynamic-offset preservation.
Default sampler admission still fails explicitly. Native multi-component
raw stores remain unsupported; the DXC fixture uses supported scalar stores.
Full FeMe LIT passes 4,080, with 15 skipped, 10 unsupported and zero failures,
from 4,105 discovered tests in 102.27 seconds. Three new fixtures add three
passes; assertions, disabled PCH and the two-worker build limit are retained.
Timings are not a controlled speedup comparison.

Bound image/sampler normalization, static descriptor materialization,
other comparison dimensions/types, implicit `SampleCmp`, clamps and
dynamic/undefined offsets remain open. This proves bounded runner execution,
not an owned D3D12 device or full sampling conformance. `CreateDevice`
remains `E_NOTIMPL`; W2(a)/W2(b) are not closed. No new adapter-matrix or
external Vulkan CTS result is claimed.

W2(a) array comparison CPU execution progress (2026-11-22): scalar FP32
heap-backed `Texture2DArray::SampleCmpLevelZero` now lowers through the
existing shared array comparison helper. Admission and lowering share
shape-aware coordinate validation: three FP32 coordinates for an array
versus two for plain 2D, with two constant spatial i32 offsets in either
case. The array layer is passed separately and is never treated as an
offset. Explicit LOD zero, sampler reflection, dead-handle cleanup and
argument/artifact ABI 8 behavior are retained.

Four new fixtures cover array lowering/idempotence/reflection, missing-layer
admission errors in normal/reference/AOT, canonical point/bilinear comparison
and real SDK DXC execution. Distinct layer contents, negative offsets,
divergent coordinates, five-thread partial waves, complete output guards
and repeated dispatches make layer/offset mistakes observable. The real
LLVM forward/reverse fixture also now covers 2D-array coordinates and
positive/negative spatial offsets.

Focused LIT passes 9 in 5.16 seconds. Full FeMe LIT passes 4,084, with
15 skipped, 10 unsupported and zero failures, from 4,109 discovered tests
in 74.39 seconds. Four added fixtures add four passes. Assertions, disabled
PCH and the two-worker build limit are retained; timings are not a controlled
performance comparison. 1D/cube comparison CPU execution, implicit comparison,
clamps, half/vector results and dynamic/undefined offsets remain open.
Bound image/sampler normalization, static descriptor materialization and
owned D3D12 execution are not established. `CreateDevice` remains
`E_NOTIMPL`; W2(a)/W2(b) are not closed. No new adapter-matrix or external
Vulkan CTS result is claimed.

W2(a) 1D comparison CPU execution progress (2026-11-22): scalar FP32
heap-backed `Texture1D`/`Texture1DArray::SampleCmpLevelZero` now lowers
through the existing shared 1D comparison helpers. Plain 1D uses scalar
FP32 U and a constant scalar i32 spatial offset; the array uses FP32
(U, layer) coordinates with the same scalar offset. Eligibility remains
shared between admission and lowering. The comparison path no longer
depends on ordinary 2D/cube handle classification, so ordinary DXIL 1D
sampling/loading is deliberately unchanged.

Four new fixtures verify helper operands, signed scalar offsets, sampler
reflection, idempotence, malformed shape preservation, unchanged ordinary
1D `SampleLevel` and dynamic-offset rejection in normal/reference/AOT.
Canonical point/linear comparisons and actual SDK DXC execution cover
divergent coordinates/layers, five-thread partial waves, complete guards
and repeated normal/reference/AOT dispatches. The real LLVM forward/reverse
fixture now also covers 1D-array coordinates and a scalar signed offset.

Focused LIT passes 14 in 8.07 seconds. Full FeMe LIT passes 4,088, with
15 skipped, 10 unsupported and zero failures, from 4,113 discovered tests
in 91.24 seconds. Four added fixtures add four passes. Assertions, disabled
PCH and the two-worker build limit are retained; timings are not a controlled
performance comparison. Artifact ABI 8, argument layouts and Vulkan behavior
are unchanged. Cube comparison CPU execution, implicit comparison, clamps,
half/vector results, dynamic/undefined offsets, bound/static descriptor
materialization and owned D3D12 execution remain open. `CreateDevice`
remains `E_NOTIMPL`; W2(a)/W2(b) are not closed. No new adapter-matrix or
external Vulkan CTS result is claimed.

W2(a) cube comparison CPU execution progress (2026-11-22): scalar FP32
heap-backed `TextureCube`/`TextureCubeArray::SampleCmpLevelZero` now lowers
through the existing shared cube comparison helpers. Three FP32 direction
components select a face, with a fourth FP32 cube-array element coordinate.
Explicit LOD zero, zero direction derivatives, sampler reflection and
dead-handle cleanup are retained. Unused scalar i32 offsets accept
poison/undef or zero; nonzero/dynamic offsets fail admission.

The runner now prepares all six physical faces for a cube. Cube-array
`array-layers` retains physical-layer meaning and must explicitly be a
positive multiple of six (12 means two cubes). Nonzero square face extents
and checked subresource-size multiplication prevent malformed or overflowing
cube heaps. Host vector allocation limits are rejected explicitly as well.
This fixes runner preparation, not D3D descriptor materialization.

Five new fixtures verify helper operands, idempotence/reflection, offset
admission, malformed heap geometry and overflow, canonical execution and
actual SDK DXC execution. Thirteen-thread guarded repeated normal/reference/
AOT dispatches sample every face and both array elements; five reference
thresholds encode six distinct face depths, making wrong face/element
selection observable. The real LLVM forward/reverse fixture now includes
plain cubes alongside its existing cube-array case.

Focused LIT passes 19 in 7.04 seconds. Full FeMe LIT passes 4,093, with
15 skipped, 10 unsupported and zero failures, from 4,118 discovered tests
in 78.58 seconds. The added plain-cube roundtrip and host allocation-limit
case also pass targeted reruns. Five added fixtures add five passes.
Assertions, disabled PCH and
the two-worker build limit are retained; timings are not a controlled
performance comparison. Artifact ABI 8 and argument layouts are unchanged.
Implicit comparison, clamps, half/vector results, broader sampling
conformance, bound/static descriptor materialization and owned D3D12
execution remain open. `CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)
are not closed. No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a) implicit comparison compiler mapping progress (2026-11-22): scalar
FP32 `SampleCmp` now has numbered opcode-64 LLVM forward lowering and
checked FeMe reverse raising. Coordinates, signed spatial offsets,
comparison references and the final optional FP32 MinLod clamp survive.
Undefined/poison clamps select the plain canonical intrinsic; real zero,
nonzero and dynamic clamps select `samplecmp.clamp`. Unsupported result
uses, malformed clamps, default samplers and 3D textures remain unraised
without introducing another sample read.

Six new fixtures cover LLVM positive/invalid lowering, real forward/reverse
roundtrips across all six comparison shapes, raw raising guards, native SDK
DXC plain/dynamic-clamp pixel-shader import and normal/reference/AOT CPU
admission. Both implicit forms explicitly require derivative-aware CPU
lowering; implicit LOD is not approximated as zero.

Focused combined LIT passes 35 in 16.89 seconds. Full FeMe LIT passes 4,097,
with 15 skipped, 10 unsupported and zero failures, from 4,122 discovered
tests in 78.90 seconds. Serial DirectX LIT passes 632, with one expected
failure and one unsupported, from 634 discovered tests in 105.11 seconds.
Four FeMe fixtures add four passes; two LLVM fixtures add two passes.
Assertions, disabled PCH, sccache and the two-worker build limit are retained;
timings are not a controlled performance comparison. Artifact ABI 8 and
argument layouts remain unchanged. Implicit comparison CPU execution,
shader clamps, broader sampling conformance, bound/static descriptor
materialization and owned D3D12 execution remain open. `CreateDevice`
remains `E_NOTIMPL`; W2(a)/W2(b) are not closed. No new adapter-matrix or
external Vulkan CTS result is claimed.

W2(a)/W3(b) fragment comparison CPU execution progress (2026-11-22):
heap-backed scalar FP32 `Texture2D::SampleCmp`, including the optional shader
MinLod clamp, now lowers in fragment stages through the existing comparison
runtime. Real coarse U/V quad derivatives select implicit LOD; comparison
references, constant signed offsets, sampler reflection and dead-handle
cleanup are retained. Admission, signature growth and lowering share one
read-only predicate. Compute/unannotated stages, other implicit shapes and
dynamic/undefined offsets still reject explicitly, rather than substituting
LOD zero. Existing six-shape level-zero execution is unchanged.

One new LIT fixture and two unit tests cover exact helper/derivative/clamp
operands, idempotence/reflection, admission and compiled fragment execution.
Distinct mip depths make derivative-selected mip 1, explicit mip 0 and
shader-clamped mip 2 observably different. Helper-only horizontal lanes
contribute to derivatives without writing outputs. Wave sizes 4/8,
one/three quads, partial final waves, guards, result masks and repeated
invocation pass. The existing compute admission fixture retains normal/
reference/AOT rejection. This is canonical compiled fragment evidence,
not native DXC shader execution through an owned D3D12 device.

Selected CPU/comparison LIT passes 821 in 5.48 seconds. Full FeMe LIT passes
4,100, with 15 skipped, 10 unsupported and zero failures, from 4,125
discovered tests in 78.78 seconds. Three added tests add three passes.
Assertions, disabled PCH, sccache and the two-worker build limit are retained;
timings are not a controlled performance comparison. Artifact ABI 8 and
argument layouts remain unchanged. Broader comparison conformance, other
implicit shapes/stages, bound/static descriptor materialization and owned
D3D12 execution remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed. No new adapter-matrix or external Vulkan
CTS result is claimed.

W2(a)/W3(b) array fragment comparison CPU progress (2026-11-23): heap-backed
scalar FP32 `Texture2DArray::SampleCmp`, including the optional shader
MinLod clamp, now lowers in fragment stages through the shared array
comparison helper. The third FP32 coordinate selects the layer separately;
only U/V are differentiated and signed constant offsets remain spatial.
The existing read-only matcher gates admission, signature growth and
lowering. Sampler reflection, dead-handle cleanup, plain 2D and six-shape
level-zero behavior are retained. Compute/unannotated stages, other
implicit shapes, missing-layer coordinates and dynamic/undefined offsets
still reject explicitly.

One new LIT fixture and two unit tests cover exact array helper operands,
four spatial derivatives, idempotence/reflection, admission and compiled
fragment execution. Complementary layer depths across three mips make
wrong layer or mip selection observable. Divergent layers/references,
per-lane dynamic zero/two MinLod clamps, helper-only horizontal lanes,
wave sizes 4/8, one/three quads, partial final waves, output guards, result
masks and repeated invocation pass. Existing fixtures also verify plain
array lowering and actual SDK DXC plain/dynamic-clamp array pixel-shader
import. This is canonical compiled fragment execution and native import
evidence, not native DXC execution through an owned D3D12 device.

Selected CPU/comparison LIT passes 824 in 16.22 seconds. Full FeMe LIT
passes 4,103, with 15 skipped, 10 unsupported and zero failures, from
4,128 discovered tests in 129.34 seconds. Three added tests add three passes.
Assertions, disabled PCH, sccache and the two-worker build limit are
retained; timings are not a controlled performance comparison. Artifact
ABI 8 and argument layouts are unchanged. Other implicit shapes/stages,
broader sampling conformance, bound/static descriptor materialization and
owned D3D12 execution remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed. No new adapter-matrix or external Vulkan
CTS result is claimed.

W2(a)/W3(b) 1D fragment comparison CPU progress (2026-11-23): heap-backed
scalar FP32 `Texture1D`/`Texture1DArray::SampleCmp` now lowers in fragment
stages through existing 1D comparison helpers. Plain U/offset are scalar;
arrays add a separate FP32 layer coordinate. Two coarse U derivatives
select implicit LOD; the layer is never differentiated. Signed constant
offsets, references and optional shader MinLod clamps survive. Shared
admission, signature growth, sampler reflection and dead-handle cleanup
are retained. Compute/unannotated stages, other implicit shapes,
missing-layer coordinates and dynamic/undefined offsets still reject
explicitly. Ordinary DXIL 1D sampling/loading eligibility is unchanged.

Two new LIT fixtures and two unit tests cover exact helper/derivative/
reference/offset/clamp operands, idempotence/reflection, admission, actual
SDK DXC plain/clamped 1D/array pixel-shader import and compiled execution.
A combined plain/array execution test distinguishes derivative-selected
mip 1, explicit mip 0 and clamped mip 2 with both X- and Y-derived
footprints. Complementary array-layer depths, divergent references/layers,
dynamic zero/two clamps, helper-only lanes, wave sizes 4/8, one/three quads,
partial final waves, output guards, result masks and repeated invocation
pass. This is canonical compiled execution and native import evidence,
not native DXC execution through an owned D3D12 device.

Selected CPU/comparison LIT passes 828 in 8.59 seconds. Full FeMe LIT
passes 4,107, with 15 skipped, 10 unsupported and zero failures, from
4,132 discovered tests in 69.41 seconds. Four added tests add four passes.
Assertions, disabled PCH, sccache and the two-worker build limit remain;
timings are not a controlled performance comparison. Artifact ABI 8 and
argument layouts are unchanged. Implicit cube/array and other stages,
broader sampling conformance, bound/static descriptor materialization and
owned D3D12 execution remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed. No new adapter-matrix or external Vulkan
CTS result is claimed.

W2(a)/W3(b) cube fragment comparison CPU progress (2026-11-23): heap-backed
scalar FP32 `TextureCube`/`TextureCubeArray::SampleCmp` now lowers in fragment
stages through existing shared cube comparison helpers. Six coarse
derivatives of the raw three-component direction feed the existing
face-projection/implicit-LOD runtime. The fourth cube-array element
coordinate stays separate and is never differentiated. References and
optional shader MinLod clamps survive. Unused scalar offsets accept
poison/undef or zero only; nonzero/dynamic offsets and malformed layers
reject explicitly. The shared matcher now admits all six comparison
shapes, still only in fragment stages. Compute/unannotated stages reject
rather than substituting LOD zero. Signature growth, sampler reflection,
dead-handle cleanup and level-zero behavior remain unchanged.

Two new LIT fixtures and two unit tests cover exact helper/derivative/
reference/layer/clamp operands, idempotence/reflection, admission, actual
SDK DXC plain/clamped cube/array pixel-shader import and compiled execution.
A combined plain/array test distinguishes implicit mip 1, explicit mip 0
and clamped mip 2 on all six faces, for both cube elements and both screen
derivative axes. Distinct face/layer/mip depths, divergent references/layers,
dynamic zero/two clamps, helper-only lanes, wave sizes 4/8, one/three quads,
partial final waves, output guards, result masks and repeated invocation
pass. This is canonical compiled execution and native import evidence,
not native DXC execution through an owned D3D12 device.

Selected CPU/comparison LIT passes 832 in 14.99 seconds. Full FeMe LIT
passes 4,111, with 15 skipped, 10 unsupported and zero failures, from
4,136 discovered tests in 137.56 seconds. Four added tests add four passes.
Assertions, disabled PCH, sccache and the two-worker build limit remain;
timings are not a controlled performance comparison. Artifact ABI 8 and
argument layouts are unchanged. Other stages/shapes, broader sampling
conformance, bound/static descriptor materialization and owned D3D12
execution remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed. No new adapter-matrix or external Vulkan
CTS result is claimed.

W2(a)/W3(b) biased-sampling compiler progress (2026-11-23): FP32
`SampleBias` now has checked opcode-61 reverse raising against LLVM's
existing forward lowering. Scalar and two-/three-/four-component FP32
texels and default samplers are supported for 1D/array, 2D/array, 3D and
cube/array textures. Coordinates/layers, bias and signed/dynamic spatial
offsets survive. Undefined/poison clamps select the plain intrinsic;
real zero/nonzero/dynamic MinLod clamps remain clamped. Unused offset
slots must be poison/undef or zero; cubes receive an unused scalar poison
offset. Operand types, complete result shape and every result use are
checked before adding a sample. Status/aggregate/out-of-width uses,
comparison samplers, half/integer texels, unsupported shapes and malformed
calls stay unraised without introducing an extra read.

Four new LIT fixtures and one unit test cover seven-shape LLVM forward/
reverse roundtrips with exact coordinate/layer/bias/offset/clamp values,
scalar and two-/three-/four-component results, raw guards, idempotence,
actual SDK DXC plain/zero-clamped/dynamic-clamped pixel-shader import and
normal/reference/AOT CPU rejection. Both biased intrinsics fail explicit
CPU admission in fragment, compute and unannotated stages; dead declarations
remain harmless. This closes an import prerequisite, not biased execution:
derivative-aware CPU lowering, gradient import/execution and broader
sampling conformance remain open. No implicit LOD is replaced with zero.

Expanded CPU/DXIL/comparison LIT passes 895 in 7.43 seconds. Full FeMe LIT
passes 4,116, with 15 skipped, 10 unsupported and zero failures, from
4,141 discovered tests in 65.41 seconds. Five added tests add five passes.
Assertions, disabled PCH, sccache and the two-worker build limit remain;
timings are not controlled speedups. Existing comparison execution,
SPIR-V bias handling, artifact ABI 8 and argument layouts are unchanged.
Bound/static descriptor materialization and owned D3D12 execution remain
open. `CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) fragment biased-sampling CPU progress (2026-11-23): heap-backed
FP32 `Texture2D::SampleBias` now lowers in fragment stages through the
existing shared 2D sampler helper. Real coarse U/V derivatives select
implicit LOD; bias, constant signed spatial offsets and optional shader
MinLod clamps survive. Scalar/vector results up to the declared FP32
texel width (at most four) narrow the runtime's four-component result.
Admission, signature growth and lowering share a read-only matcher.
Sampler reflection and post-traversal dead-handle cleanup are retained.
Compute/unannotated stages, other shapes, non-heap handles, comparison
samplers and dynamic/undefined offsets still reject explicitly.

One new LIT fixture and two unit tests cover exact helper/derivative/
bias/offset/clamp operands, scalar/vector result shaping, idempotence,
reflection, admission and compiled fragment execution. A combined compiled
test distinguishes derivative-selected mip 1 from negative-/positive-bias
mips 0/2 and shader-clamped mip 2. Distinct mip/texel/component values make
wrong LOD, offset or channel selection observable. Both screen derivative
axes, divergent -1/0/+1 bias and zero/two clamps, helper-only lanes, wave
sizes 4/8, one/three quads, partial final waves, output guards, result
masks and repeated invocation pass. Existing seven-shape LLVM roundtrips,
native SDK DXC import and normal/reference/AOT compute rejection still pass.
This is canonical compiled execution, not native DXC execution through
an owned D3D12 device.

Expanded CPU/DXIL/comparison LIT passes 898 in 9.42 seconds. Full FeMe LIT
passes 4,119, with 15 skipped, 10 unsupported and zero failures, from
4,144 discovered tests in 65.33 seconds. Three added tests add three passes.
Assertions, disabled PCH, sccache and the two-worker build limit remain;
timings are not controlled speedups. Existing comparison execution,
ordinary DXIL sampling, SPIR-V bias handling, artifact ABI 8 and argument
layouts are unchanged. Other biased shapes/stages, gradient mapping/
execution, broader sampling conformance, bound/static descriptor
materialization and owned D3D12 execution remain open. `CreateDevice`
remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) array biased-sampling CPU progress (2026-11-23): heap-backed
FP32 `Texture2DArray::SampleBias` now lowers in fragment stages through
the shared array sampler helper. The third FP32 coordinate selects the
layer independently; only U/V are differentiated and both signed
constant offsets remain spatial. Bias, optional shader MinLod clamps,
scalar/vector result shaping, sampler reflection and dead-handle cleanup
are retained. Admission, signature growth and lowering share the matcher.
Compute/unannotated stages, other biased shapes, malformed layer coordinates
and dynamic/undefined offsets still reject explicitly.

One new LIT fixture and two unit tests cover exact layer/derivative/bias/
offset/clamp operands, result shaping, idempotence/reflection, admission
and compiled execution. Distinct layer/mip/texel/component values detect
wrong layer, mip, offset or channel selection. Both screen derivative
axes, divergent layers and -1/0/+1 bias, dynamic zero/two clamps,
helper-only lanes, wave sizes 4/8, one/three quads, partial final waves,
output guards, result masks and repeated invocation pass. Expanded
fixtures verify native SDK DXC plain/clamped array import and array-specific
normal/reference/AOT compute rejection. Native import remains distinct
from canonical execution and owned-device execution.

Expanded CPU/DXIL/comparison LIT passes 901 in 16.27 seconds. Full FeMe LIT
passes 4,122, with 15 skipped, 10 unsupported and zero failures, from
4,147 discovered tests in 95.68 seconds. Three added tests add three passes.
Assertions, disabled PCH, sccache and the two-worker build limit remain;
timings are not controlled speedups. Existing plain 2D sampling/comparison,
SPIR-V bias handling, artifact ABI 8 and argument layouts are unchanged.
Other biased shapes/stages, gradient mapping/execution, broader sampling
conformance, bound/static descriptor materialization and owned D3D12
execution remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) cube biased-sampling CPU progress (2026-11-23): heap-backed
FP32 `TextureCube`/`TextureCubeArray::SampleBias` now lowers in fragment
stages through the existing cube sampler helpers. Six coarse raw-direction
derivatives feed the shared face-projection/implicit-LOD runtime. The
fourth cube-array element coordinate is passed separately and never
differentiated. Bias, optional shader MinLod clamps, scalar/vector result
shaping, sampler reflection and post-traversal dead-handle cleanup are
retained. Admission, signature growth and lowering share the matcher.
Unused scalar offsets accept poison/undef or zero only; nonzero/dynamic
offsets, malformed coordinates and compute/unannotated stages still reject.

One new LIT fixture and two unit tests cover exact six-derivative/element/
bias/clamp operands, result shaping, idempotence/reflection, admission and
compiled execution. Distinct face/element/mip/component values distinguish
all six faces, both cube elements and derivative-selected mip 1 from biased
mips 0/2 and shader-clamped mip 2. Both screen derivative axes, divergent
elements and -1/0/+1 bias, dynamic zero/two clamps, helper-only lanes,
wave sizes 4/8, one/three quads, partial final waves, output guards, result
masks and repeated invocation pass. Expanded fixtures verify native SDK
DXC plain/clamped cube/array import and shape-specific normal/reference/AOT
compute rejection. Native import remains distinct from canonical execution
and owned-device execution.

Expanded CPU/DXIL/comparison LIT passes 904 in 10.30 seconds. Full FeMe LIT
passes 4,125, with 15 skipped, 10 unsupported and zero failures, from
4,150 discovered tests in 73.22 seconds. Three added tests add three passes.
Assertions, disabled PCH, sccache and the two-worker build limit remain;
timings are not controlled speedups. Existing 2D/array biased sampling,
comparison execution, ordinary DXIL sampling, SPIR-V bias handling,
artifact ABI 8 and argument layouts are unchanged. 1D/3D biased execution,
other stages, gradient mapping/execution, broader sampling conformance,
bound/static descriptor materialization and owned D3D12 execution remain
open. `CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) 1D biased-sampling CPU progress (2026-11-23): heap-backed
FP32 `Texture1D`/`Texture1DArray::SampleBias` now lowers in fragment stages
through the existing 1D sampler helpers. Plain 1D uses scalar FP32 U and
one constant signed scalar i32 offset; arrays add a separate second layer
coordinate. Only U is differentiated, and offsets remain spatial. Bias,
optional shader MinLod clamps, scalar/vector result shaping, sampler
reflection and post-traversal dead-handle cleanup are retained. The shared
matcher gates admission, signature growth and lowering without broadening
ordinary DXIL 1D sampling/loading. Malformed coordinates, dynamic/poison/
undefined offsets and compute/unannotated stages still reject explicitly.

One new LIT fixture and two unit tests cover exact coordinate/layer/
derivative/bias/offset/clamp operands, result shaping, idempotence/reflection,
admission and compiled execution. Distinct layer/mip/texel/component values,
mip-0/mip-1 footprints, both screen derivative axes, divergent layers and
-1/0/+1 bias, dynamic zero/two clamps, scalar/vector widths one through four,
helper-only lanes, wave sizes 4/8, one/three quads, partial final waves,
output guards, result masks and repeated invocation pass. Expanded fixtures
verify native SDK DXC plain/clamped 1D/array import and shape-specific normal/
reference/AOT compute rejection. Native import remains distinct from
canonical execution and owned-device execution.

Expanded CPU/DXIL/comparison LIT passes 907 in 10.19 seconds. Full FeMe LIT
passes 4,128, with 15 skipped, 10 unsupported and zero failures, from
4,153 discovered tests in 66.09 seconds. Three added tests add three passes.
Assertions, disabled PCH, sccache and the two-worker build limit remain;
timings are not controlled speedups. Existing 2D/cube biased sampling
(including arrays), comparison execution, ordinary DXIL sampling, SPIR-V
bias handling, artifact ABI 8 and argument layouts are unchanged. 3D biased
execution, other stages, gradient mapping/execution, broader sampling
conformance, bound/static descriptor materialization and owned D3D12
execution remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) 3D biased-sampling CPU progress (2026-11-23): heap-backed
FP32 `Texture3D::SampleBias` now lowers in fragment stages through the
existing 3D sampler helper. FP32 (U, V, W), six coarse spatial derivatives,
bias, three constant signed i32 offsets and optional shader MinLod clamps
are preserved. Scalar/vector results up to the declared texel width (at
most four), sampler reflection and post-traversal dead-handle cleanup are
retained. Admission, signature growth and lowering share the matcher.
Malformed coordinates/offset widths, dynamic/poison/undefined or partially
undefined offsets and compute/unannotated stages still reject explicitly.
Ordinary DXIL 3D sampling/loading is not broadened.

One new LIT fixture and two unit tests cover exact six-derivative/XYZ
offset/bias/clamp operands, result shaping, idempotence/reflection,
admission and compiled execution. Distinct mip/XYZ-texel/component values
isolate U-, V- and W-driven LOD on both screen derivative axes, baseline
mip 1, biased mips 0/2 and shader-clamped mip 2. Positive/negative offsets
on all three axes, divergent -1/0/+1 bias and dynamic zero/two clamps,
scalar/vector widths one through four, helper-only lanes, wave sizes 4/8,
one/three quads, partial final waves, output guards, result masks and repeated
invocation pass. Expanded fixtures verify native SDK DXC plain/clamped
3D import and normal/reference/AOT compute rejection for both forms.
Native import remains distinct from canonical execution and owned-device
execution.

Expanded CPU/DXIL/comparison LIT passes 910 in 21.49 seconds. Full FeMe LIT
passes 4,131, with 15 skipped, 10 unsupported and zero failures, from
4,156 discovered tests in 145.31 seconds. Three added tests add three passes.
Assertions, disabled PCH, sccache and the two-worker build limit remain;
timings are not controlled speedups. This completes bounded fragment FP32
biased execution for all seven raised texture shapes, not broader sampling
conformance. Existing 1D/2D/cube biased sampling (including arrays),
comparison execution, ordinary DXIL sampling, SPIR-V bias handling,
artifact ABI 8 and argument layouts are unchanged. Other stages, gradient
mapping/execution, broader sampling conformance, bound/static descriptor
materialization and owned D3D12 execution remain open. `CreateDevice`
remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) gradient-sampling compiler progress (2026-11-23): FP32 DXIL
`SampleGrad` now raises opcode 63 to `llvm.dx.resource.samplegrad[.clamp]`
against LLVM's existing forward lowering. Scalar/vector FP32 texels up to
four components use default samplers for 1D/array, 2D/array, 3D and cube/
array textures. Coordinates/layers, dP/dX, dP/dY, signed/dynamic spatial
offsets and optional real MinLod clamps survive. Gradient width excludes
array layers: one for 1D/array, two for 2D/array, three for 3D/cube/array.
Undefined/poison clamps select the plain intrinsic; zero and dynamic real
clamps remain clamped. Unused gradient/offset wire slots must be undef/
poison or zero. Bias and gradient raising share complete operand/result/
use validation before adding a sample, so status/aggregate/out-of-width
uses and malformed calls stay unraised without introducing a second read.
Comparison samplers, half/integer texels and unsupported shapes stay unraised.

Four LIT fixtures and one unit test cover seven-shape LLVM forward/reverse
roundtrips, exact coordinate/layer/gradient/offset/clamp values, scalar and
two-/three-/four-component FP32 results, idempotence, four accepted raw
clamp/dynamic cases, eighteen rejected raw cases and native SDK DXC plain/
clamped pixel-shader import for all seven shapes. Both gradient intrinsics
still fail explicit CPU admission in every stage; normal/reference/AOT
compute rejection is tested. This is compiler mapping, not gradient CPU
execution, descriptor materialization or owned-device execution.

Expanded CPU/DXIL/comparison/gradient LIT passes 915 in 14.55 seconds. Full
FeMe LIT passes 4,136, with 15 skipped, 10 unsupported and zero failures,
from 4,161 discovered tests in 70.58 seconds. Five added tests add five
passes. Assertions, disabled PCH, sccache and the two-worker build limit
remain; timings are not controlled speedups. Existing biased/comparison
execution, ordinary DXIL sampling, SPIR-V gradient handling, artifact ABI 8
and argument layouts are unchanged. Gradient CPU execution, broader sampling
conformance, bound/static descriptor materialization and owned D3D12
execution remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) explicit 2D gradient CPU execution progress (2026-11-23):
heap-backed FP32 plain `Texture2D::SampleGrad` now lowers through the existing
shared 2D sampling runtime. Admission and lowering share a predicate for
heap-backed handles, default samplers, shape-matched coordinates/gradients,
constant signed offsets and scalar/vector results up to the declared texel
width (at most four). Plain and shader-MinLod-clamped calls are supported.
Supplied dP/dX and dP/dY drive LOD directly, with zero shader bias and no
synthesized derivatives or LOD-zero approximation. Unlike implicit/bias
sampling, explicit gradients need no fragment-only gate: compute, graphics
and unannotated canonical functions share this bounded admission.

Transform tests check exact derivative ordering, offsets, clamp forwarding,
result shaping, handle cleanup, reflection, idempotence and malformed-input
rejection. Compiled fragment tests independently vary all four gradient
components to select mips 0/1/2 despite lane-uniform coordinates, with
divergent clamps, signed offsets, scalar/vector components, helper lanes,
partial waves, output guards and repeats. Normal/reference compute tests
verify divergent supplied-gradient mip selection and clamps. A five-lane
normal/reference/AOT runner fixture checks signed offsets, result components,
partial waves, guards and repeats on one mip; runner image YAML still
prepares only one mip. Other gradient shapes remain explicitly rejected,
including normal/reference/AOT array compute dispatch.

The assertions-enabled, PCH-disabled, sccache ARM64 build passes with `-j2`.
Focused CPU/DXIL/sampling LIT passes 920 tests in 18.21 seconds. Full FeMe LIT
passes 4,141 tests, with 15 skipped, 10 unsupported and zero failures, from
4,166 discovered tests in 77.26 seconds. Five new tests add five passes.
Timings are not controlled speedups. Existing biased/comparison execution,
ordinary DXIL sampling/loading eligibility, SPIR-V gradient handling,
artifact ABI 8 and argument layouts are unchanged. Other gradient shapes,
broader sampling conformance, bound/static descriptor materialization and
owned D3D12 execution remain open. Native SDK import is not owned-device
execution; `CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) explicit 2D-array gradient CPU execution progress (2026-11-23):
heap-backed FP32 `Texture2DArray::SampleGrad` now executes through the existing
shared array sampling runtime. The shared admission/lowering predicate admits
three-component `(U,V,layer)` coordinates and two-component dP/dX and dP/dY:
the layer is never differentiated. Default samplers, scalar/vector results up
to the declared texel width (at most four), constant signed spatial offsets
and optional shader MinLod clamps retain the plain-2D contract. Supplied
gradients select LOD in any stage, without synthesized derivatives, an
LOD-zero approximation or a new runtime entry point.

Transform tests verify exact derivative ordering and separate layer
forwarding, offsets, clamps, result shaping, reflection, handle cleanup and
idempotence. Missing layers and layer-width gradients stay untouched;
admission tests reject dynamic, undefined, poison and partially undefined
offsets in fragment, vertex, compute and unannotated functions. Compiled
fragment tests independently vary all four signed gradient components to
select mips 0/1/2 despite lane-uniform U/V, with divergent layers/clamps,
offsets, components, helpers, partial waves, guards and repeats.
Normal/reference compute tests independently vary layer and mip selection.
A five-lane normal/reference/AOT runner fixture checks divergent layers,
gradients/clamps, scalar/vector results, signed offsets, partial waves,
guards and repeats on one mip; runner image YAML still prepares only one
mip. Unsupported normal/reference/AOT gradient admission now uses 3D.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling LIT passes 925 in 18.93 seconds; full FeMe LIT
passes 4,146 with 15 skipped, 10 unsupported and zero failures among
4,171 discovered tests in 76.97 seconds. Five new tests add five passes.
Timings are not controlled speedups. Existing biased/comparison execution,
ordinary DXIL sampling/loading eligibility, SPIR-V gradient handling,
artifact ABI 8 and argument layouts remain unchanged. 1D/array, 3D and
cube/array gradient CPU execution, broader conformance, bound/static
descriptor materialization and owned D3D12 execution remain open. Native
SDK import remains separate from canonical execution and owned-device
execution; `CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) explicit cube gradient CPU execution progress (2026-11-23):
heap-backed FP32 `TextureCube::SampleGrad` and `TextureCubeArray::SampleGrad`
now execute through the existing cube projection/sampling runtime. Shared
admission and lowering require default samplers, shape-matched FP32 coordinates/
gradients and scalar/vector results up to the declared texel width (at most
four). Both gradients have three raw-direction components; cube-array
coordinates add a separate element that is never differentiated. Unused
scalar zero/undef/poison offsets and optional shader MinLod clamps are
supported; nonzero/dynamic cube offsets remain rejected. Six supplied
direction derivatives drive existing cube LOD with zero shader bias,
without synthesized derivatives, a fragment-only gate or an LOD-zero
approximation. Existing tangent-plane projection/LOD behavior is unchanged.

Transform tests verify all six derivative operands in runtime order,
separate array elements, clamps, narrowing, reflection, handle cleanup and
idempotence. Missing elements, layer-width gradients and nonzero offsets
stay untouched; admission tests cover every stage and zero/undef/poison
versus nonzero/dynamic offsets. Compiled fragment tests independently vary
all six signed gradient components on all six faces, selecting mips 0/1/2
despite lane-uniform directions and retaining the existing major-axis
projection behavior. Divergent clamps/elements, scalar/vector components,
helpers, partial waves, guards and repeated prepared invocations are checked.
Normal/reference compute tests select divergent mips on all faces and both
array elements. A 13-lane normal/reference/AOT runner fixture covers plain/
clamped cube/array forms, scalar/vector results, divergent gradients/clamps,
faces/elements, partial waves, guards and repeats on one mip; runner image
YAML still prepares only one mip.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling LIT passes 930; full FeMe LIT passes 4,151 with
15 skipped, 10 unsupported and zero failures among 4,176 discovered tests.
Five new tests add five passes. Existing biased/comparison execution,
ordinary DXIL sampling/loading eligibility, SPIR-V gradient handling,
artifact ABI 8 and argument layouts are unchanged. 1D/array and 3D gradient
CPU execution, broader sampling conformance, bound/static descriptor
materialization and owned D3D12 execution remain open. Native SDK import
remains separate from canonical CPU and owned-device execution;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) explicit 1D gradient CPU execution progress (2026-11-23):
heap-backed FP32 `Texture1D::SampleGrad` and `Texture1DArray::SampleGrad`
now execute through the existing plain/array 1D sampling runtime. Shared
admission/lowering requires scalar U for plain 1D or `(U,layer)` for arrays,
scalar dU/dX and dU/dY, constant signed scalar offsets, default samplers and
scalar/vector results up to the declared texel width (at most four).
Optional shader MinLod clamps survive. Array layers remain coordinate-only.
Supplied gradients select LOD in any stage with zero shader bias, without
synthesized derivatives, a fragment-only gate or an LOD-zero approximation.
Ordinary DXIL 1D sampling/loading eligibility remains unchanged.

Transform tests verify scalar derivative/offset forwarding, separate array
layers, clamps, result shaping, reflection, handle cleanup and idempotence.
Vector gradients/offsets and missing array layers stay untouched; admission
tests reject dynamic/undefined/poison offsets in fragment, vertex, compute
and unannotated functions. Compiled fragment tests independently supply
positive dU/dX or negative dU/dY to select mips 0/1/2 despite lane-uniform U,
with divergent layers/clamps, both signed offsets, scalar/vector components,
helpers, partial waves, guards and repeated prepared invocations.
Normal/reference compute tests select divergent supplied-gradient mips and
independent layers/clamps. A five-lane normal/reference/AOT runner fixture
covers plain/clamped 1D/array forms, signed scalar gradients/offsets,
scalar/vector components, divergent layers/clamps, partial waves, guards
and repeats on one mip; runner image YAML still prepares only one mip.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling LIT passes 935 in 62.70 seconds; full FeMe LIT
passes 4,156 with 15 skipped, 10 unsupported and zero failures among
4,181 discovered tests in 149.03 seconds. Five new tests add five passes.
Timings are not controlled speedups. Existing biased/comparison execution,
ordinary DXIL sampling/loading eligibility, SPIR-V gradient handling,
artifact ABI 8 and argument layouts remain unchanged. 3D is the only raised
SampleGrad shape without CPU execution; broader sampling conformance,
bound/static descriptor materialization and owned D3D12 execution remain
open. Native SDK import remains separate from canonical CPU and owned-device
execution; `CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) explicit 3D gradient CPU execution progress (2026-11-23):
heap-backed FP32 `Texture3D::SampleGrad` now executes through the existing
volume sampling runtime. Shared admission/lowering requires three-component
U/V/W coordinates, dP/dX and dP/dY, constant signed XYZ offsets, default
samplers and scalar/vector results up to the declared texel width (at most
four). Optional shader MinLod clamps survive. Six supplied spatial
derivatives select LOD in any stage with zero shader bias, without
synthesized derivatives, a fragment-only gate or an LOD-zero approximation.
All seven raised FP32 SampleGrad shapes now have bounded CPU execution.

Transform tests verify all six derivatives in runtime order, signed XYZ
offsets, clamps, result shaping, reflection, handle cleanup and idempotence.
Malformed coordinate/gradient/offset widths and dynamic offsets stay
untouched; admission tests reject dynamic, undefined, poison and partially
undefined offsets in fragment, vertex, compute and unannotated functions.
Compiled fragment tests independently supply positive dP/dX or negative
dP/dY for each U/V/W component to select mips 0/1/2 despite lane-uniform
coordinates, with clamps, signed XYZ offsets, scalar/vector components,
helpers, partial waves, guards and repeated prepared invocations.
Normal/reference compute tests select divergent mips 0/2 using only
negative dW/dY and independently clamp them. A five-lane normal/reference/
AOT runner fixture covers plain/clamped 3D forms, scalar/vector components,
signed XYZ offsets, divergent gradients/clamps, partial waves, guards and
repeats on one mip; runner image YAML still prepares only one mip.
Negative runner tests now reject dynamic W and partially poison XYZ
offsets instead of rejecting the valid 3D shape.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling LIT passes 940 in 26.11 seconds; full FeMe LIT
passes 4,161 with 15 skipped, 10 unsupported and zero failures among
4,186 discovered tests in 83.68 seconds. Five new tests add five passes;
timings are not controlled speedups. Existing biased/comparison execution,
ordinary DXIL sampling/loading eligibility, SPIR-V gradient handling,
artifact ABI 8 and argument layouts remain unchanged. Broader sampling
conformance, bound/static descriptor materialization and owned D3D12
execution remain open. Native SDK import remains separate from canonical
CPU and owned-device execution; `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed. No new adapter-matrix or external Vulkan
CTS result is claimed.

W2(a)/W3(b) explicit 1D LOD CPU execution progress (2026-11-23):
heap-backed FP32 `Texture1D::SampleLevel` and `Texture1DArray::SampleLevel`
now execute through the existing plain/array 1D sampling runtime. Shared
admission/lowering requires default samplers, scalar U or `(U,layer)`
coordinates, FP32 LOD, constant signed scalar offsets and scalar/vector
results up to the declared texel width (at most four). Explicit LOD is
preserved in any stage with zero derivatives/bias and no shader MinLod
clamp. Array layers remain separate; existing sampler-owned LOD behavior
is unchanged. Implicit DXIL 1D sampling/loading is not promoted.

Transform tests verify LOD/offset forwarding, layers, result shaping,
reflection, image-only signature growth, handle cleanup and idempotence.
Missing layers, vector/dynamic/poison offsets, comparison samplers,
non-heap handles and out-of-texel-width results stay untouched. Admission
tests reject dynamic/undefined/poison offsets in fragment, vertex, compute
and unannotated functions. The old 1D comparison fixture now preserves
implicit `Sample` rejection instead of rejecting the supported `SampleLevel`.
Compiled fragment tests select explicit mips 0/1/2 despite lane-uniform U,
with independent layers, signed offsets, all four result widths, helpers,
partial waves, guards and repeated prepared invocations. Normal/reference
compute tests select divergent mips and independent layers, including
sampler minimum/maximum LOD clamps. Actual SDK DXC SM6.6 bindless plain/
array shaders execute through normal/reference/AOT paths with signed
offsets, components, divergent layers/LODs, five lanes, guards and repeats
on one mip; runner image YAML still prepares only one mip.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Expanded focused CPU/DXIL/sampling LIT passes 959 in 18.88 seconds,
including existing ordinary image-sampling regressions. Full FeMe LIT
passes 4,167 with 15 skipped, 10 unsupported and zero failures among
4,192 discovered tests in 89.51 seconds. Six new tests add six passes;
timings are not controlled speedups. Other ordinary sampling/loading
shapes, biased/comparison/gradient execution, SPIR-V behavior, artifact
ABI 8 and argument layouts remain unchanged. Native DXC import plus
canonical CPU execution is not owned D3D12 execution. Broader sampling
conformance and bound/static descriptor materialization remain open;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) implicit 1D sampling CPU execution progress (2026-11-23):
heap-backed FP32 `Texture1D::Sample` and `Texture1DArray::Sample` now
execute through existing plain/array 1D helpers in fragment stages. Shared
admission/lowering requires default samplers, scalar U or `(U,layer)`,
constant signed scalar offsets and scalar/vector results up to the declared
texel width (at most four). Real coarse dU/dX and dU/dY drive derivative-based
LOD with zero shader bias and no shader MinLod clamp, not an LOD-zero
approximation. Array layers remain separate and undifferentiated.

Transform tests verify exact U derivative/offset forwarding, separate
layers, narrowing, reflection, image-only signature growth, handle cleanup
and idempotence. Missing layers, dynamic/poison offsets, out-of-texel-width
results and compute calls stay untouched. Admission tests reject dynamic/
undefined/poison offsets and vertex/compute/unannotated calls. Compiled
fragment tests independently vary positive/negative U derivatives on both
screen axes to select mips 0/1/2, with signed offsets, independent layers,
all four result widths, helpers, partial waves, guards and repeated
prepared invocations. Actual SDK DXC plain/array pixel shaders pass import/
raising; real shader-clamped variants stay unraised. Native import remains
separate from canonical CPU and owned-device execution. Normal/reference/
AOT compute admission rejects even valid constant-offset implicit 1D calls.
The earlier explicit-LOD fixture now preserves compute-stage implicit
rejection rather than rejecting the newly supported fragment shape.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling LIT passes 964 in 41.53 seconds; full FeMe LIT
passes 4,172 with 15 skipped, 10 unsupported and zero failures among
4,197 discovered tests in 157.87 seconds. Five new tests add five passes;
timings are not controlled speedups. DXIL 1D loading, other ordinary shapes,
explicit LOD, biased/comparison/gradient execution, SPIR-V behavior,
artifact ABI 8 and argument layouts remain unchanged. Broader sampling
conformance and bound/static descriptor materialization remain open;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) explicit 3D LOD CPU execution progress (2026-11-23):
heap-backed FP32 `Texture3D::SampleLevel` now executes through the existing
volume sampler. Shared admission/lowering requires default samplers,
three-component U/V/W coordinates, FP32 LOD, constant signed XYZ offsets
and scalar/vector results up to the declared texel width (at most four).
Explicit LOD is preserved in any stage with six zero derivatives, zero
shader bias and no shader MinLod clamp. Existing sampler-owned LOD behavior
remains in the runtime. Implicit DXIL 3D sampling/loading is not promoted.

Transform tests verify LOD/XYZ offset forwarding, six zero derivatives,
result shaping, reflection, image-only signature growth, handle cleanup
and idempotence. Missing W coordinates, short/dynamic/partially poison
offsets, comparison samplers, non-heap handles and out-of-texel-width
results stay untouched. Admission tests reject dynamic, whole undefined/
poison and every individually undefined/poison XYZ component in fragment,
vertex, compute and unannotated functions. The old 3D bias fixture now
retains implicit `Sample` rejection instead of rejecting valid `SampleLevel`.
Compiled fragment tests select mips 0/1/2 despite lane-uniform coordinates,
with signed XYZ offsets, all four result widths, helpers, partial waves,
guards and repeated prepared invocations. Normal/reference compute tests
select divergent mips with sampler minimum/maximum LOD clamps. Actual SDK
DXC SM6.6 bindless 3D shaders execute through normal/reference/AOT paths
with signed XYZ offsets, components, divergent coordinates/LODs, five
lanes, guards and repeats on one mip; runner image YAML still prepares
only one mip.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling LIT passes 970 in 34.64 seconds; full FeMe LIT
passes 4,178 with 15 skipped, 10 unsupported and zero failures among
4,203 discovered tests in 261.65 seconds. Six new tests add six passes;
timings are not controlled speedups. Existing 1D execution, other ordinary
shapes, biased/comparison/gradient execution, SPIR-V behavior, artifact
ABI 8 and argument layouts remain unchanged. Native SDK import plus
canonical CPU execution is not owned D3D12 execution. Broader sampling
conformance and bound/static descriptor materialization remain open;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) implicit 3D sampling CPU execution progress (2026-11-23):
heap-backed FP32 `Texture3D::Sample` now executes in fragment stages
through the existing volume sampler. Shared admission/lowering requires
default samplers, three-component U/V/W coordinates, constant signed XYZ
offsets and scalar/vector results up to the declared texel width (at most
four). All six real coarse spatial derivatives reach the helper with
zero shader bias, derivative-driven LOD and no shader MinLod clamp; this
is not an LOD-zero approximation. Sampler-owned LOD behavior is unchanged.

Transform tests verify exact derivative/offset forwarding, result shaping,
reflection, image-only signature growth, handle cleanup and idempotence.
Missing W, short/dynamic/partially poison offsets, comparison samplers,
non-heap handles and out-of-texel-width results stay untouched. Admission
rejects dynamic, whole undefined/poison and every individually undefined/
poison XYZ component, plus compute, vertex and unannotated calls. Earlier
3D bias/level fixtures retain non-fragment implicit rejection.

Compiled fragment tests independently vary positive/negative U/V/W
derivatives on both screen axes to select mips 0/1/2, with signed XYZ
offsets, all four result widths, helper-only lanes, wave sizes 4/8,
one/three quads, partial final waves, output guards, masks and repeated
prepared invocations. Native SDK DXC pixel shaders pass import/raising;
real shader-clamped variants remain unraised. This import evidence is
distinct from canonical CPU execution; bound/static descriptors are not
materialized. Normal/reference/AOT compute admission rejects even valid
constant-offset implicit 3D calls.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling LIT passes 975 in 67.96 seconds; full FeMe LIT
passes 4,183 with 15 skipped, 10 unsupported and zero failures among
4,208 discovered tests in 297.78 seconds. Five new tests add five passes;
timings are not controlled speedups. Non-fragment implicit 3D sampling,
real shader MinLod clamps and 3D loading remain unsupported. Existing 1D
execution, explicit LOD, other ordinary shapes, biased/comparison/gradient
execution, SPIR-V behavior, artifact ABI 8 and argument layouts remain
unchanged. Broader sampling conformance and owned D3D12 execution remain
open; `CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) 3D texel-load CPU execution progress (2026-11-24):
heap-backed FP32 `Texture3D::Load` now executes in any stage through the
existing volume-load helper. Shared admission/lowering requires
three-component i32 XYZ coordinates, an i32 mip, constant signed XYZ
offsets and scalar/vector results up to the declared texel width (at most
four). Plain wrapping integer adds preserve offsets without `nsw`/`nuw`;
the explicit mip is forwarded with sample zero, no sampler and no stage
derivatives. Native DXIL's all-undefined omitted 3D offset is normalized
to zero while raising; partially undefined offsets remain unsupported,
as do whole undefined/poison offsets in already-canonical calls.

The shared volume-load runtime now checks all three per-mip extents,
not base X/Y extents, before fetching. Negative/out-of-range coordinates
and invalid mips read zero, preventing higher-mip coordinates from
aliasing adjacent rows/slices. This correction also applies to existing
SPIR-V users of the helper; SPIR-V lowering is unchanged.

Transforms verify XYZ offset arithmetic, explicit mip, sample/mask,
narrowing, reflection, image-only signature growth, handle cleanup and
idempotence without derivatives. Missing Z, short offsets, non-i32 mips,
integer texels, non-heap handles and out-of-texel-width results stay
unlowered. Admission tests reject dynamic, whole undefined/poison and
every individually undefined/poison XYZ offset component in fragment,
vertex, compute and unannotated functions. Runtime tests check each
mip's lower/upper XYZ bounds, invalid mips and a false mask.

Compiled normal/reference compute tests distinguish mips 0/1/2, XYZ and
components with all four result widths, signed offsets, bounds, invalid
mips, wave sizes 4/8, five lanes, guards and repeated prepared dispatches.
Fragment tests verify divergent mips, every bounds axis, helpers, partial
waves, masks, guards and repeats. Actual SDK DXC SM6.6 bindless shaders
execute through normal/reference/AOT paths with signed offsets, components,
divergent coordinates, invalid mips, five lanes, guards and repeats on one
mip; runner YAML still prepares one mip. Dynamic-offset admission rejects
in all three paths. Native import plus canonical CPU execution is not
owned D3D12 execution or bound/static descriptor materialization.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling LIT passes 982 in 46.12 seconds; full FeMe LIT
passes 4,191 with 15 skipped, 10 unsupported and zero failures among
4,216 discovered tests in 136.87 seconds. Eight new tests add eight passes;
timings are not controlled speedups. Integer 3D loads, non-fragment implicit
3D sampling and real shader MinLod clamps on ordinary `Sample` remain
unsupported. Other loading/sampling, artifact ABI 8 and argument layouts
are unchanged. Broader conformance, bound/static descriptors and owned
D3D12 execution remain open; `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

### W3: Conventional graphics on the existing executor

Depends on W2; reuses `FeMeGraphics` rather than implementing a D3D rasterizer.

| Step | Work and acceptance |
|---|---|
| W3(a) | Normalize PSOs, IA bindings and input semantics, stage linkage, root arguments and RTV/DSV state into `GraphicsPipeline`/`PreparedDraw`. Render an indexed/instanced off-screen triangle. Explicitly test clip/depth range, viewport orientation, winding, provoking vertex and base vertex/instance conventions; do not assume one global Y flip suffices. |
| W3(b) | Validate derivatives/helper side effects, interpolation, discard, early/late depth/stencil, blend/logic operations, MRT, MSAA/resolve, stream output, indirect draws, queries and predication. Add only missing shared behavior, with explicit semantic policies when APIs differ. |
| W3(c) | Map DXIL hull/domain/geometry stages to existing patch/tessellator/geometry storage and wrappers. Test adjacency, patch constants, fractional partitioning, culling, output streams and layered/multi-viewport rendering. Keep residual workgroup/barrier limitations visible. |
| W3(d) | Validated PSO libraries/caches and parallel compilation. Key shaders, root layout, state, host policy and ABI; reject corrupt/stale/untrusted executable cache entries. Run the conventional graphics corpus on FeMe, WARP and equivalent Vulkan scenes with specification-appropriate tolerances. |

### W4: FL 12_2 advanced graphics and resource closure

Depends on W3. Existing Vulkan mesh support is a starting point, not D3D Tier 1.

| Step | Work and acceptance |
|---|---|
| W4(a) | Map amplification/mesh DXIL and payloads onto existing task/mesh machinery. Complete and test all reported Tier 1 limits, output attributes, dispatch/indirect behavior, queries, synchronization and rejection paths. |
| W4(b) | Implement shared mechanics plus D3D mappings for VRS Tier 2, sampler feedback Tier 0.9, conservative raster Tier 3, Tiled Resources Tier 3 and depth bounds. Test rate combiners/coverage, feedback encoding/resolve and sparse/null/packed-mip behavior rather than inferring support from CPU memory. |
| W4(c) | Close binding Tier 3, Root Signature 1.1, required formats/casting, wave/int64 behavior, `WriteBufferImmediate`, copy-queue timestamps and GPU-VA minima. Generate a gap report against the complete selected FL 12_2 table; keep direct heap indexing on the separate SM 6.6 track. |

### W5: Shared ray tracing and DXR 1.1

Depends on W4 for product closure; portable BVH work can start after WS1.
Coordinate with existing G7/G8 and R36/R37 entries, not a second ray engine.

| Step | Work and acceptance |
|---|---|
| W5(a) | Shared deterministic BVH build/refit, triangle/procedural intersections, instance transforms/masks/culling and inline query semantics. Test against brute-force scalar traversal and both APIs' mapped geometry/flags; validate hostile ranges before dereference. |
| W5(b) | Shared bounded continuation ABI and ray-stage execution; D3D state objects/collections, shader identifiers/tables, global/local roots and dispatch. Test any-hit/intersection/callable behavior, payload/attribute limits, recursion, failure and termination without unbounded host recursion. |
| W5(c) | Prebuild sizing, update, compaction, copy/serialization, postbuild information and DXR 1.1 closure. Run applicable local ray tests and Vulkan ray suites against the same engine; report FL 12_2 only after every W1-W5 capability requirement, including any required legacy shader support, passes. |

### W6: Presentation and independently negotiated modern profiles

These are separate workstreams, not prerequisites of each other.

| Step | Dependencies, work and acceptance |
|---|---|
| W6(a) | W2: enhanced barriers, translating sync/access/layout ranges, split/discard and alias behavior to the shared dependency mechanisms. Validate with the enhanced-barrier corpus; a queue-wide drain is not the final advertised implementation. |
| W6(b) | W0 and W3: prove the presentation path early, then complete DXGI/shared-resource/shared-fence interoperability. Test rotation/resize, flip/composition, occlusion, frame latency, removal and reported color/HDR/tearing modes on a display-capable host. A CPU upload/copy prototype is not complete flip-model interoperability. |
| W6(c) | W5: selected later shader models/options, SM 6.6 direct resource/sampler indexing, Work Graphs and any selected generic programs. Reuse bounded scheduler/storage primitives, but implement node records/backing memory/forward progress explicitly. Evaluate D3D11-on-12 with legacy shader coverage; native D3D11 and video remain separate scope decisions. |

### W7: Product and conformance evidence

Depends on W0-W5, W6(b), and any W6(a)/W6(c) capabilities selected for release.

Reconcile every query, format, limit, shader model and tier with focused tests,
the applicable Microsoft runtime conformance corpus and the installed adapter's
HLK playlists. Record exact runtime/DDI/driver identities, test selections,
skips, failures, device identity and artifacts alongside Vulkan measurements.
Full runs must include supported cases that reduced-iteration smoke runs omit.
Performance has its own compile-time, draw/dispatch throughput, latency and
memory benchmarks against pinned WARP; no WARP performance parity is implied.
WHCP/WHQL/signing and distribution approval are separate from behavioral
compliance. A compatibility runtime must identify its different qualification
scope rather than claim installed-driver HLK coverage.

## Test and build gates

Use these local suites to seed capability-specific selections, after W0's
adapter/shader/dependency audit. Directory presence is not a passing result.

| Milestone | Local Direct3D evidence to select |
|---|---|
| W0-W1 | `src\d3dtest\func\d3d12\IndependentDevices`, `src\D3D12Conf\DirectCommandListTests`, `HeapTests`, `GPUSynchronizationTests` |
| W2 | `src\D3D12Conf\BindingTests`, `ResourceTests`, `AllocationInfo`, `TypedUAVLoad`, `IndirectDrawTests`, `CountersAndQueries` |
| W3 | `src\D3D12Conf\ROV`, `ProgrammableMSAA`, `RenderPass`, `RTVPFromVSDS`, and conventional graphics tests under `src\d3dtest\func\d3d12` |
| W4 | `src\D3D12Conf\MeshShaders`, `VariableRateShading`, `SamplerFeedback`, `ConsRas`, `DepthBoundsTest`, `ExecuteTileMappings`, `CopyQueueTimestamps` |
| W5 | `src\D3D12Conf\Raytracing`, separating inline-query and ray-pipeline tests and excluding unreported preview features |
| W6 | `src\D3D12Conf\EnhancedBarriers`, `WorkGraphs`, `GenericPrograms`, `Displayable`, plus applicable swap-chain/shared-handle functional tests |
| Oracle only unless audited | `src\warp\tests\ShaderUnitTest12`, `UMDriverWhitebox`, `PixelJIT`, `SIMDJit`, `ThreadPoolWhitebox`; the CMake build gates some white-box targets out of WARP-only builds |

Each behavioral change needs coverage at every affected compiler boundary:

| Phase | Required focused evidence |
|---|---|
| Container import and metadata | DXIL validation/import and preserved signature/root metadata; legal and malformed fixtures. Add DXBC import/translation cases wherever W0 requires them. |
| Raising and canonical graphics IR | System values, interpolation, stage selection and resource identities; positive and rejected shapes through `feme-opt`/FileCheck plus unit tests. |
| Binding/resource lowering | Root/descriptor mapping, image/sampler operations, bounds, overflow and non-uniform indexing, including mixed resource classes. |
| CPU middle end | Linearization, SIMD masks, wave operations, groupshared/barrier live values and helper-lane side effects, checked before and after the affected pass. |
| ABI/code generation and execution | JIT/AOT reflection/layout agreement, prepared-storage lifetime, invocation output against scalar references, then frontend readback/image and cross-API tests. |

Run `check-feme` as the build target, not only `llvm-lit` on already-built
binaries. Keep `feme-test-depends`/`FEME_TEST_DEPENDS` wired to every new test
tool, generated header/shader, optional driver and unit-test binary; verify the
graph on a clean build. Future Windows integration targets must build their
runner and selected backend before executing. Existing `check-hlsl-feme-vk`
exercises the Vulkan frontend, not a FeMe D3D adapter; keep it as complementary
shared-compiler coverage.

Use assertions, object caching, and no PCH in every development build. The
Windows LLVM build currently uses `sccache`, embedded MSVC debug information,
`LLVM_ENABLE_ASSERTIONS=ON` and `CMAKE_DISABLE_PRECOMPILE_HEADERS=ON`.
Preserve safe job limits. Enable supported sanitizers on portable tests and
stress deterministic/parallel schedules and forced failures.

For local Direct3D tests, first build/install the selected targets into their
configuration-specific directory, then run TAEF from there. Disable their
declared PCH targets too, use object caching and an assertion-enabled
configuration, and honor the host's build job limits. Functional
`/p:Warp=true` and conformance `/p:UseWarp=true /inproc` select Microsoft's
reference path, **not FeMe**. Use explicit adapter selection and check the LUID
on the device actually created. Reduced iterations (`/p:RITP`) are useful for
smoke runs but not final qualification. Do not assume WARP ShaderUnitTest12,
UMDriverWhitebox or PixelJIT tests are public-interface or DXIL-only tests.
Honor generated-shader install/dependency rules when building the local corpus.

After each implementation/refactoring change, rebuild the ICD, confirm
`FeMe CPU Vulkan Device`, run a bounded affected CTS selection from the local
`C:\dev\VK-GL-CTS` checkout, and update [VulkanCTSReport.md](VulkanCTSReport.md)
and both Vulkan inventories. Preserve historical full-run results separately;
a smoke pass is not full conformance. For unchanged advertisements, record
"unchanged" with the actual validation scope rather than inventing support.
