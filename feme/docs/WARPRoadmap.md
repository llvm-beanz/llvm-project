# FeMe WARP-like software adapter roadmap

**Next implementation step: W0(a), prove the Windows integration boundary.**
`WS0` (image block-geometry + subresource-layout extraction) is done; `WS1`
can continue the shared image-copy/decode boundary without committing to a
Windows deployment model.

This is the implementation checklist for [FeMeWARPDesign.md](FeMeWARPDesign.md),
not a claim that FeMe is a Direct3D adapter today. The product remains a
distinctly identified D3D12 software adapter with complete feature level 12_2
behavior, presentation, and separately negotiated modern options. It must not
replace system WARP or assume `EnumWarpAdapter` selects FeMe.

All milestones below are **pending**. Existing FeMe components are reusable
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
| WS1(c) | Move the reusable copy/resolve kernels (row/depth/sample-stride walking shared by `vkCmdCopyImage`/`vkCmdBlitImage`/`vkCmdResolveImage`) out of `CommandBuffer.cpp` into `FeMeImage`, building on `WS1(b)`'s view materialization. Keep Vulkan command recording/validation and barrier semantics in place. | Row/depth/sample-stride and decoded-storage-lifetime cases get direct `FeMeImageTests` coverage; `check-feme` and a bounded `dEQP-VK.api.copy_and_blit.*`/`dEQP-VK.pipeline.multisample.*` CTS selection stay green. Vulkan and a future D3D consumer share the same helpers. |
| WS1(d) | Move `ImageView::resolvedLayerCount()`/`effectiveViewLayerCount`'s 2D-view-of-3D-image depth-slice-as-array-layer resolution (currently only in `RenderPass.cpp`'s attachment-view path, not shared with `materializeImageDescriptor`) into `FeMeImage`, building on `WS1(b)`'s `resolveImageViewExtent`. Decide whether attachment-view and sampled/storage-view resolution should converge on one shared function or remain deliberately distinct, and document the choice here or in the design doc. | `FeMeImageTests` coverage for the 3D-image-as-2D-array-attachment case; `check-feme` and a bounded `dEQP-VK.pipeline.render_to_image.*`/`dEQP-VK.renderpass*.*` CTS selection with 3D-image attachments stay green. |
| WS2 | Extract task execution and retained work storage from `Sync.*`, command execution, and existing CPU/draw worker paths incrementally. Preserve Vulkan queue/semaphore behavior; do not export its `QueueExecutor` object model wholesale. | Submission replay, independent queues, wait-before-signal, error wakeup, destruction and single/multi-worker tests. A lower-valued D3D fence signal is tested separately from Vulkan's monotonic timeline rules. |
| WS3 | Establish shared-core regression gates before WS0, and apply them to every following commit. Move portable tests with their helpers, retaining loader-level tests in Vulkan. | `check-feme`, affected CTS groups, byte/image comparisons and unchanged capability manifests; compare named failures, not only pass totals. |

Separate mechanical extraction from new behavior. First preserve the Vulkan
callers and test results, then add a small D3D consumer. Do not combine a file
move, scheduler rewrite, ABI change, and new feature in one commit.

## Direct3D milestones

### W0: Integration and test feasibility

Depends on no shared refactoring. Its output is a written decision and a
throwaway-capable prototype, not an adapter compliance claim.

| Step | Work and acceptance |
|---|---|
| W0(a) | Pin OS builds, SDK/WDK, DDI, inbox/Agility runtime versions, and reference WARP binary. Prove discovery, opening, callback negotiation, and teardown for a distinct FeMe identity. Test the installed render-only/software path and assess `CreateSoftwareAdapter` without assuming either works. Record miniport/INF/signing/CI requirements; kernel installation and reboot tests belong on a suitable Windows host/VM, not this process-isolated container. |
| W0(b) | If the installed path is not viable, explicitly choose the application-local compatibility boundary and document the added COM, DXGI, debug-layer/DRED, and distribution work. A custom runtime harness can test a backend but does not prove stock Windows adapter installation. Neither path patches system WARP or relies on private WARP ordinals. |
| W0(c) | Classify the local test corpus: public black-box tests, WARP-private/white-box tests, DXBC/DXIL inputs, optional/preview features, display/HLK dependencies. Determine required legacy shader support from the selected contract. Review source/test redistribution permissions before reuse; author LLVM-owned focused tests from public contracts. |
| W0(d) | Add an explicit FeMe adapter selection/identity assertion to the future runner. List and execute one applicable test on reference WARP, then demonstrate selection of the prototype through the chosen boundary. Record unavailable infrastructure as blocked, not Pass. WARP-only white-box tests remain oracle tests, never generic FeMe coverage. |

### W1: Objects and first headless compute

Depends on W0 and WS2; reuses existing `CompiledStage` and artifact reflection.

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
