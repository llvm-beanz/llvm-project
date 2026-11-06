# FeMe WARP-like software adapter roadmap

**Selected direction: container-first, application-local user-mode software
driver using stock Windows D3D12/DXGI.** `W0(f)`'s container measurements
and `W0(h)`'s user-authorized bounded alternative-B investigation are done.
Stock D3D12 reaches owned `OpenAdapter12`, queries public adapter callbacks
and closes the adapter on host/container inbox and Agility 619 runtimes,
without ordinal 199, reference forwarding or a new/custom kernel driver.
This uses Microsoft's WARP identity, not a distinct FeMe identity, and no
supported DDI version or FeMe device exists yet. `W0(g)` is partially done;
the routing blocker is resolved, while `W0(b)`'s product/compatibility
decision remains open. The negative public general-provider search must
not be interpreted as routing impossibility. Evidence and limits are in
[W0WindowsIntegrationProbe.md](W0WindowsIntegrationProbe.md#alternative-b-application-local-warp-ddi-routing).
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
but its legacy-shader audit and FeMe execution tests still require the selected
stock-runtime contract to be audited and a working FeMe runner, respectively.

This is the implementation checklist for [FeMeWARPDesign.md](FeMeWARPDesign.md),
not a claim that FeMe is a Direct3D adapter today. The product remains a
distinctly identified D3D12 software adapter with complete feature level 12_2
behavior, presentation, and separately negotiated modern options. It must not
replace system WARP or assume `EnumWarpAdapter` selects FeMe.

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
| ~~WS3(a)~~ | Add a `check-feme-shared-core` CMake custom target (`feme/test/CMakeLists.txt`, `feme/utils/check-feme-shared-core.py`) that runs FeMe's shared-core, API-neutral library test binaries (`FeMeExecutionTests`, `FeMeImageTests`) directly and reports each one's own named test failures, not just an aggregate pass count -- mirroring the existing `check-feme-fuzz` custom-target precedent. "Move portable tests with their helpers" was, in practice, already done incrementally: every `WS0`-`WS2` extraction added its new portable logic's tests directly to one of these two binaries as it went, rather than only exercising it indirectly through `FeMeVulkanTests`/CTS. What this step adds is a fast, scoped way to run just those binaries locally after a shared-core-only change, without paying for the full `check-feme` lit suite's project-wide test discovery. | `cmake --build . --target check-feme-shared-core` builds and runs both binaries cleanly, 2/2 passed, 0 failures, with each binary's own named failures (none) printed rather than only a pass/fail total; full `check-feme` re-run after the CMake change unaffected, 3,978 discovered/3,953 Passed/15 Skipped/10 Unsupported, zero failures. No production code changed, so no CTS re-run was needed. |
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
| W0(a) | Pin OS builds, SDK/WDK, DDI, inbox/Agility runtime versions, and reference WARP binary. Prove discovery, opening, callback negotiation, and teardown for a distinct FeMe identity through the selected user-mode stock-runtime path. W0(e) supplies host measurements; W0(f) supplies matching container measurements; W0(g) supplies the prototype/DDI-negotiation proof. Installed render-only/software-adapter feasibility, miniport/INF/signing/CI requirements are a separate deployment evaluation, not prerequisites for this bring-up. Kernel installation and reboot tests, if required, belong on an approved host/VM. |
| W0(b) | Product/compatibility decision if the selected boundary cannot meet the intended identity/deployment contract: explicitly approve an installed adapter, WARP-specific compatibility, or an application-local COM runtime with its added COM/DXGI/debug-layer/DRED/distribution work. The user authorized bounded investigation of B; W0(h) proves owned stock-runtime routing, not shipping adoption or a distinct FeMe identity. Do not repeat the research-approval question or infer a kernel requirement from the negative public-provider search. Private ABI dependencies, product identity changes and system changes still need separate approval. See [alternative-B evidence](W0WindowsIntegrationProbe.md#alternative-b-application-local-warp-ddi-routing) and [alternative costs](W0WindowsIntegrationProbe.md#w0b-alternative-costscope-evaluation-not-a-selection). |
| W0(c) | Classify the local test corpus: public black-box tests, WARP-private/white-box tests, DXBC/DXIL inputs, optional/preview features, display/HLK dependencies. Determine required legacy shader support from the selected contract. Review source/test redistribution permissions before reuse; author LLVM-owned focused tests from public contracts. |
| W0(d) | Add an explicit FeMe adapter selection/identity assertion to the future runner. List and execute one applicable test on reference WARP, then demonstrate selection of the prototype through the chosen boundary. Record unavailable infrastructure as blocked, not Pass. WARP-only white-box tests remain oracle tests, never generic FeMe coverage. |
| ~~W0(e)~~ | ~~Measure the no-install host integration paths: inbox/Agility runtime and reference-WARP versions, `CreateSoftwareAdapter`, executable-local WARP substitution, and an LLVM-owned rejection stub with DDI entry-point counters. Run an applicable public-API WARP queue/fence test.~~ Completed 2026-11-06; evidence and reproduction instructions in [W0WindowsIntegrationProbe.md](W0WindowsIntegrationProbe.md). This closes measurements only, not FeMe device creation. |
| ~~W0(f)~~ | ~~Container-first user-mode proof: build the standalone probe in the Windows container; run all four CTest cases and record OS/kit/runtime/WARP identities and exact outcomes before implementation. Verify reference WARP and the expected pre-DDI stub rejection; do not substitute host results.~~ Completed 2026-11-06; container evidence, an extended NuGet-WARP/Agility-core matrix, and reproduction instructions are in [W0WindowsIntegrationProbe.md](W0WindowsIntegrationProbe.md#container-measurements). All four CTest cases passed with no discrepancy from the host measurements. This closes the container measurement step only -- it is not FeMe DDI negotiation; `W0(g)` below carries that forward. |
| W0(g) | ~~Identify a public general-provider identity/thunk bootstrap~~ -- search completed with a negative result. ~~Reach owned `OpenAdapter12` through stock runtime and query/close its adapter table~~ -- W0(h) now supplies host/container inbox/Agility evidence under Microsoft's WARP identity. Remaining: audit and negotiate a pinned supported DDI, provide required owned adapter/device/thunk services, prove device creation/teardown, and resolve truthful FeMe identity/deployment through W0(b). Zero advertised versions and unsupported device creation do not close this milestone. Keep public-DDI investigation container-first; stop before unapproved private ABI or shipping-scope changes. |
| ~~W0(h)~~ | ~~Bounded alternative-B investigation: isolate reference forwarding, named-export forwarding, ordinal-199 forwarding, bare owned DDI and owned adapter-table cases; compare WARP selection with generic software-adapter rejection; build/test inside the container and verify inbox/Agility runtime paths.~~ Completed 2026-11-06: owned open/query/close needs neither private ordinal nor forwarding at this stage. Explicit preloading retains diagnostic counters; a debugger also proves executable-local entry without preloading. [Evidence, exact counters and remaining limits](W0WindowsIntegrationProbe.md#alternative-b-application-local-warp-ddi-routing). No real FeMe device, supported DDI, distinct identity or shipping compatibility is claimed. |

W0(a) and W0(d) are **partially done**, not complete. W0(e) supplies the host
version/loading measurements, W0(f) supplies the matching container
measurements, and both supply W0(d)'s reference-test evidence. W0(h) adds
OS-routed owned DDI evidence, including version/capability queries and close,
but still under Microsoft's existing adapter identity. W0(g)'s supported
DDI/device/thunk work and W0(b)'s identity/shipping decision remain open.
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
incidental one. The remaining legacy-shader audit must determine the
selected stock-runtime contract's requirements and schedule compatibility
gaps before claiming compliance. LLVM-owned reference tests now exist in the
standalone probe, but FeMe execution tests still need the prototype/runner
from `W0(g)`/`W1(a)`, and `W0(g)` itself is now blocked on `W0(b)`'s decision
gate. These are contract/implementation dependencies, not a
blanket host/VM blocker. `W0(c)` is left unstruck rather than
split into further lettered rows -- both blockers are existing upstream `W0`/
`W1` milestones, not new work needing its own breakdown.

`W0(d)`'s FeMe future-runner adapter-selection/identity-assertion design (not
yet an implementation) is also recorded in that document. W0(e)/W0(f)'s
standalone runner now implements reference-WARP identity checking and the
public queue/fence oracle test in both the host and the container, but it
cannot assert a FeMe device identity until `W0(g)`/`W1(a)` supply one, and
`W0(g)` cannot proceed with a prototype runtime path until `W0(b)` is
resolved.

### W1: Objects and first headless compute

Depends on the selected W0 integration proof (W0(g), or a separately approved
W0(b) alternative) and WS2; reuses existing `CompiledStage` and artifact
reflection. Pending installed-driver, display or HLK evaluation does not block
user-mode headless W1 once the selected boundary is proven.

| Step | Work and acceptance |
|---|---|
| W1(a) | Optional Windows build/export target, device and direct/compute/copy queues, allocators/lists, buffers/heaps, fences, PSOs, allocation accounting, and device-removal propagation. Keep runtime/DDI handles outside portable execution code. Check the export set and coexistence with DXC and the Vulkan ICD in one process. Test invalid handles/sizes, OOM, close/reset/replay, and teardown with pending work. |
| W1(b) | Resource-free DXIL dispatch, then one structured/raw-buffer write, copy/readback and fence. Validate IDs, partial waves, multi-wave groupshared barriers, concurrent PSO compilation, context/code lifetime, and repeated execution against scalar expectations and WARP. |
| W1(c) | Build a versioned capability inventory tied to named tests and normative SDK requirements. Early partial execution is an internal harness, not a falsely advertised lower feature level: even FL 11_0 requires graphics. Pin/query actual wave policy; test requested wave sizes separately. |

### W2: Binding, resources, and synchronization

Depends on W1 and WS0-WS1. General CBVs and unbounded/non-uniform binding are
compiler prerequisites, not work that a DDI wrapper can hide.

| Step | Work and acceptance |
|---|---|
| W2(a) | Root Signature 1.0/1.1 parsing and immutable binding plans: register class/space/range, visibility, offsets, constants, root descriptors, tables and static samplers. Replace use-inferred cbuffer promotion with an explicit root-layout mapping; test multiple CBVs, overlapping register numbers of different classes, arrays, dynamic indices and non-32-bit accesses. |
| W2(b) | Descriptor creation/copies, execution-time preparation, volatility/static promises, unbounded and per-lane non-uniform ranges, UAV counters/atomics, and GPU-VA-to-bounded-allocation lookup. Keep bounds checks; never treat an arbitrary GPU VA as a trusted CPU pointer. Test updates after recording, heap rebinding, null/OOB descriptors, aliasing and allocation retention. |
| W2(c) | Committed/placed/reserved resources, residency/budgets, typeless view compatibility, planes/subresources, texture copy footprints and shared image/sampler preparation. Test D3D row/placement alignment separately from internal storage; exercise BC, sRGB, integer, depth/stencil and multisample operations individually. |
| W2(d) | Queue-ordered copies/dispatch/indirect commands, legacy transitions including promotion/decay and split/alias/UAV barriers, queries/predication, and host/queue fence events. Preserve the dependency model needed by enhanced barriers. Test cross-queue progress, decreasing fence values, replay, timeout/removal wakeup, and indirect argument overflow. |

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
